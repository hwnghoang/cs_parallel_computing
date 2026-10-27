// Concurrent Counter
// Assignment 1, Part 3: mutex, compare-and-swap (plain and with exponential backoff), fetch-and-add counters.
// Import libraries
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// Mutex lock
struct counter_mutex 
{
    std::mutex lock;
    long v;

    counter_mutex() 
    {
        v = 0;
    }

    long inc() 
    {
        // Lấy khóa mutex để bảo vệ đoạn critical section
        // std::lock_guard tự động quản lý việc khóa và mở khóa mutex (khác so với unique_lock)
        std::lock_guard<std::mutex> guard(lock);
        
        // Lưu lại giá trị hiện tại trước khi v tăng
        long old_value = v;
        v = v + 1;
        
        // Trả về giá trị trước khi tăng
        return old_value;
    }

    long get() 
    {
        // Hàm này chỉ được gọi sau khi tất cả các luồng (workers) đã hoàn thành (join)
        return v;
    }
};

// Compare-and-swap
struct counter_compare_and_swap
{
    std::atomic<long> v;

    counter_compare_and_swap() 
    {
        v.store(0, std::memory_order_relaxed);
    }

    long inc() 
    {
        // Bước 1: Đọc giá trị hiện tại của v và lưu vào biến cục bộ
        long old_value = v.load(std::memory_order_relaxed);
        
        // Cờ đánh dấu trạng thái cập nhật thành công hay thất bại
        bool success = false;
        
        // Bước 2: Vòng lặp thử lại (retry loop) cho đến khi cập nhật thành công
        while (success == false) 
        {
            // Tính toán giá trị mới dựa trên giá trị cũ vừa đọc được
            long new_value = old_value + 1;
            
            // compare_exchange_strong thực hiện thao tác so sánh và hoán đổi nguyên tử:
            // 1. So sánh giá trị hiện tại của 'v' với 'old_value'.
            // 2. Nếu v == old_value (chưa bị luồng nào khác thay đổi): 
            //    - Gán v = new_value.
            //    - Trả về true (cập nhật thành công).
            // 3. Nếu v != old_value (đã bị luồng khác ghi đè):
            //    - Không làm gì với v.
            //    - Tự động cập nhật 'old_value' bằng giá trị mới nhất của 'v'.
            //    - Trả về false (cập nhật thất bại, vòng lặp sẽ tiếp tục thử lại).
            success = v.compare_exchange_strong(old_value, new_value, std::memory_order_relaxed);
        }
        
        // Trả về giá trị cũ ngay trước thời điểm luồng này cập nhật thành công
        return old_value;
    }

    long get() 
    {
        // Trả về giá trị hiện tại của v
        return v.load(std::memory_order_relaxed);
    }
};

// CAS with exponential backoff: after each failed CAS, spin 2^k iterations (k capped) before retrying.
struct counter_compare_and_swap_backoff 
{
    std::atomic<long> v;

    counter_compare_and_swap_backoff() 
    {
        v.store(0, std::memory_order_relaxed);
    }

    long inc()
    {
        long old_value = v.load(std::memory_order_relaxed);
        unsigned int delay = 1;
        bool success = false;

        while (success == false) 
        {
            long new_value = old_value + 1;
            success = v.compare_exchange_strong(old_value, new_value, std::memory_order_relaxed);
            
            if (success == false)
            {
                // Exponential backoff: Vòng lặp rỗng để trì hoãn luồng
                for (volatile unsigned int i = 0; i < delay; i = i + 1) 
                {
                    // Không làm gì cả
                }
                
                // Giới hạn thời gian trễ tối đa
                if (delay < 1024) 
                {
                    delay = delay * 2;
                }
            }
        }
        
        return old_value;
    }

    long get() 
    {
        return v.load(std::memory_order_relaxed);
    }
};

// Fetch-and-add
struct counter_fetch_and_add 
{
    std::atomic<long> v;

    counter_fetch_and_add() 
    {
        v.store(0, std::memory_order_relaxed);
    }

    long inc() 
    {
        // fetch_add thực hiện phép cộng và trả về giá trị trước khi cộng
        long old_value = v.fetch_add(1, std::memory_order_relaxed);
        return old_value;
    }

    long get() 
    {
        return v.load(std::memory_order_relaxed);
    }
};

void private_work(int work) 
{
    volatile unsigned int value = 0;
    for (int k = 0; k < work; k = k + 1) 
    {
        value = value * 1664525u + 1013904223u;
    }
}

long share(long total, int threads, int t) 
{
    long base_share = total / threads;
    long remainder = total % threads;
    
    if (t < remainder) 
    {
        return base_share + 1;
    } 
    else 
    {
        return base_share;
    }
}

// Release waiting workers and join started threads even if emplace_back throws.
struct worker_group 
{
    std::atomic<int> state; // 0=waiting, 1=run, -1=cancel
    std::vector<std::thread> workers;

    worker_group() 
    {
        state.store(0, std::memory_order_relaxed);
    }

    bool should_run() 
    {
        int current = state.load(std::memory_order_acquire);
        
        while (current == 0) 
        {
            std::this_thread::yield();
            current = state.load(std::memory_order_acquire);
        }
        
        if (current > 0)
        {
            return true;
        }
        else
        {
            return false;
        }
    }
    
    void start_and_join()
    {
        state.store(1, std::memory_order_release);
        
        for (size_t i = 0; i < workers.size(); i = i + 1)
        {
            workers[i].join();
        }
    }
    
    ~worker_group()
    {
        if (state.load(std::memory_order_relaxed) == 0)
        {
            state.store(-1, std::memory_order_release);
        }
        
        for (size_t i = 0; i < workers.size(); i = i + 1)
        {
            if (workers[i].joinable() == true)
            {
                workers[i].join();
            }
        }
    }
};

template<class Counter>
bool check(const char* name, int threads, long total)
{
    Counter counter;
    std::vector<std::vector<long>> got(threads);
    worker_group group;
    
    for (int t = 0; t < threads; t = t + 1)
    {
        got[t].reserve(share(total, threads, t));
    }
    
    for (int t = 0; t < threads; t = t + 1)
    {
        group.workers.emplace_back([&counter, &got, &group, total, threads, t]()
        {
            if (group.should_run() == false)
            {
                return;
            }
            
            long n = share(total, threads, t);
            for (long i = 0; i < n; i = i + 1)
            {
                long val = counter.inc();
                got[t].push_back(val);
            }
        });
    }
    
    group.start_and_join();
    
    std::vector<bool> seen(total, false);
    bool ok = true;
    
    if (counter.get() != total)
    {
        ok = false;
    }
    
    for (size_t t = 0; t < got.size(); t = t + 1)
    {
        const std::vector<long>& values = got[t];
        
        for (size_t i = 0; i < values.size(); i = i + 1)
        {
            long r = values[i];
            
            // Tách các điều kiện kiểm tra tính hợp lệ
            if (r < 0 || r >= total)
            {
                ok = false;
            }
            else if (seen[r] == true)
            {
                ok = false;
            }
            else if (i > 0 && r <= values[i - 1])
            {
                ok = false;
            }
            else
            {
                seen[r] = true;
            }
        }
    }
    
    if (ok == true)
    {
        std::printf("[PASS] %s threads=%d total=%ld final=%ld\n", name, threads, total, counter.get());
    }
    else
    {
        std::printf("[FAIL] %s threads=%d total=%ld final=%ld\n", name, threads, total, counter.get());
    }
    
    return ok;
}

template<class Counter>
double bench(int threads, long total, int work)
{
    Counter counter;
    worker_group group;
    const auto start = std::chrono::steady_clock::now();
    
    for (int t = 0; t < threads; t = t + 1)
    {
        group.workers.emplace_back([&counter, &group, total, threads, t, work]()
        {
            if (group.should_run() == false)
            {
                return;
            }
            
            long n = share(total, threads, t);
            for (long i = 0; i < n; i = i + 1)
            {
                counter.inc();
                private_work(work);
            }
        });
    }
    
    group.start_and_join();
    
    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();
    
    if (counter.get() != total)
    {
        throw std::runtime_error("incorrect final count");
    }
    
    return seconds;
}

long number(const char* arg, long min, long max)
{
    size_t end;
    std::string text(arg);
    long value = std::stol(text, &end);
    
    if (end != text.size())
    {
        throw std::invalid_argument("argument out of range");
    }
    if (value < min)
    {
        throw std::invalid_argument("argument out of range");
    }
    if (value > max)
    {
        throw std::invalid_argument("argument out of range");
    }
    
    return value;
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 2)
        {
            if (std::string(argv[1]) == "test_worker_failure")
            {
                std::atomic<int> ran;
                ran.store(0, std::memory_order_relaxed);
                
                try
                {
                    worker_group group;
                    for (int t = 0; t < 3; t = t + 1)
                    {
                        if (t == 2)
                        {
                            throw std::runtime_error("simulated thread creation failure");
                        }
                        group.workers.emplace_back([&group, &ran]()
                        {
                            if (group.should_run() == true)
                            {
                                ran.fetch_add(1, std::memory_order_relaxed);
                            }
                        });
                    }
                }
                catch (const std::runtime_error& e)
                {
                    // Bắt exception và bỏ qua
                }
                
                if (ran.load(std::memory_order_relaxed) != 0)
                {
                    throw std::runtime_error("workers ran after cancellation");
                }
                
                std::puts("PASS: started workers joined after simulated creation failure");
                return 0;
            }
            
            if (std::string(argv[1]) == "list")
            {
                std::puts("mutex\ncas\ncas_backoff\nfaa");
                return 0;
            }
            
            if (std::string(argv[1]) == "test")
            {
                unsigned int hardware_cores = std::thread::hardware_concurrency();
                int hw = static_cast<int>(std::max(1u, hardware_cores));
                
                std::vector<int> counts;
                counts.push_back(1);
                counts.push_back(2);
                counts.push_back(4);
                counts.push_back(hw);
                
                std::sort(counts.begin(), counts.end());
                counts.erase(std::unique(counts.begin(), counts.end()), counts.end());
                
                bool ok = true;
                std::vector<long> n_values;
                n_values.push_back(3L);
                n_values.push_back(1003L);
                n_values.push_back(200000L);
                
                for (size_t i = 0; i < counts.size(); i = i + 1)
                {
                    int t = counts[i];
                    for (size_t j = 0; j < n_values.size(); j = j + 1)
                    {
                        long n = n_values[j];
                        
                        bool res_mutex = check<counter_mutex>("mutex", t, n);
                        if (res_mutex == false)
                        {
                            ok = false;
                        }
                        
                        bool res_cas = check<counter_compare_and_swap>("cas", t, n);
                        if (res_cas == false)
                        {
                            ok = false;
                        }
                        
                        bool res_cas_backoff = check<counter_compare_and_swap_backoff>("cas_backoff", t, n);
                        if (res_cas_backoff == false)
                        {
                            ok = false;
                        }
                        
                        bool res_faa = check<counter_fetch_and_add>("faa", t, n);
                        if (res_faa == false)
                        {
                            ok = false;
                        }
                    }
                }
                
                if (ok == true)
                {
                    return 0;
                }
                else
                {
                    return 1;
                }
            }
        }
        
        if (argc != 5)
        {
            std::fprintf(stderr, "usage: %s test | list | <mutex|cas|cas_backoff|faa> <threads> <total> <work>\n", argv[0]);
            return 1;
        }
        
        const std::string name = argv[1];
        int threads = static_cast<int>(number(argv[2], 1, 1024));
        long total = number(argv[3], 1, 1000000000L);
        int work = static_cast<int>(number(argv[4], 0, 1000000));
        double seconds = 0.0;
        
        if (name == "mutex")
        {
            seconds = bench<counter_mutex>(threads, total, work);
        }
        else if (name == "cas")
        {
            seconds = bench<counter_compare_and_swap>(threads, total, work);
        }
        else if (name == "cas_backoff")
        {
            seconds = bench<counter_compare_and_swap_backoff>(threads, total, work);
        }
        else if (name == "faa")
        {
            seconds = bench<counter_fetch_and_add>(threads, total, work);
        }
        else
        {
            throw std::invalid_argument("unknown counter");
        }
        
        double throughput = total / seconds;
        std::printf("%s,%d,%ld,%d,%.9f,%.0f\n", name.c_str(), threads, total, work, seconds, throughput);
        return 0;
        
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "ERROR: %s\n", error.what());
        return 1;
    }
}