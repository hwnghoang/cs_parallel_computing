// Concurrent Counter - Assignment 1, part 3.
// Counter methods: mutex, CAS, CAS + exponential backoff, fetch-and-add,
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
    long v = 0;

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
        // Được gọi sau khi tất cả các luồng (workers) đã hoàn thành (lệnh join)
        return v;
    }
};

// Compare-and-swap
struct counter_compare_and_swap
{
    std::atomic<long> v{0};

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

// 3) CAS + exponential backoff: sau mỗi lần CAS thất bại, 
// chờ theo time lần lượt 1, 2, 4, ..., tối đa 1024 vòng rồi đi tiếp vòng mới
struct counter_compare_and_swap_backoff
{
    std::atomic<long> v{0};

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

// 4) Fetch-and-add
struct counter_fetch_and_add
{
    std::atomic<long> v{0};

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

// Test thử với race condition
// - Nếu check() phát hiện ra counter này bị sai và báo [FAIL]
// - Nếu check() cho counter này [PASS] -> kiểm tra kết quả đủ điều kiện đúng không
struct counter_racy
{
    // Biến đếm tổng dùng chung cho mọi luồng, khai báo dạng nguyên tử
    std::atomic<long> v{0};

    // Hàm tăng đếm chứa lỗi Race Condition cố ý
    long inc()
    {
        // Đọc giá trị hiện tại của v lưu ở old_value.
        long old_value = v.load(std::memory_order_relaxed);

        // BGhi giá trị (old_value + 1) ngược trở lại v.
        // Lệnh store(): atomic write.
        // Tách thao tác đọc và ghi ra 2 lệnh riêng biệt
        // Dẫn đến mất cập nhật khi có luồng khác ghi đè vào
        v.store(old_value + 1, std::memory_order_relaxed);

        return old_value;
    }

    long get() 
    {
        return v.load(std::memory_order_relaxed); 
    }
};

#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <string>
#include <stdexcept>

// =========================================================================
// CÁC STRUCT PHỤ TRỢ CHO COUNTER
// =========================================================================

// Wrapper để các thread gọi hàm inc(). Mặc định sẽ gọi thẳng vào counter dùng chung.
// Việc này giúp hàm check() và bench() có thể dùng chung 1 template cho mọi loại counter.
template<class Counter>
struct handle_of
{
    Counter& shared_counter;
    
    long inc()
    {
        return shared_counter.inc(); 
    }
};

template<class Counter> constexpr bool returns_global_tickets = true;



// Giả lập số lượng công việc tốn thời gian
__attribute__((noinline)) void private_work(int work_size)
{
    // Dùng volatile để cấm trình biên dịch (khi bật -O2) tối ưu hóa hoặc xóa mất vòng lặp này
    volatile unsigned int dummy_value = 0; 
    
    for (int k = 0; k < work_size; ++k)
    {
        dummy_value = dummy_value * 1664525u + 1013904223u;
    }
}

// Chia công việc để tính toán số lượng inc mà mỗi thread phải thực hiện.
long calculate_share(long total, int num_threads, int thread_idx)
{
    long base = total / num_threads;
    long remainder = total % num_threads;
    
    // Những thread đầu tiên sẽ gánh thêm phần dư
    if (thread_idx < remainder)
    {
        return base + 1;
    }
    return base;
}

// Cấu trúc quản lý nhóm worker, giúp các thread xuất phát cùng lúc
struct worker_group
{
    // 0 = đang chờ lệnh, 1 = bắt đầu chạy, -1 = hủy
    std::atomic<int> state{0}; 
    std::vector<std::thread> workers;

    // Các thread sau khi tạo sẽ gọi hàm này và bị chặn lại ở đây (spin chờ)
    bool should_run()
    {
        int current_state = 0;
        while (true)
        {
            current_state = state.load(std::memory_order_acquire);
            if (current_state != 0)
            {
                break;
            }
            std::this_thread::yield(); // Nhường CPU trong lúc chờ
        }
        return current_state > 0;
    }

    // Phát lệnh cho chạy và đợi tất cả thread xong việc
    void start_and_join()
    {
        state.store(1, std::memory_order_release);
        for (std::thread& w : workers)
        {
            if (w.joinable())
            {
                w.join();
            }
        }
    }

    // Destructor dọn dẹp để tránh lỗi std::terminate nếu văng lỗi khi tạo thread
    ~worker_group()
    {
        if (state.load(std::memory_order_relaxed) == 0)
        {
            state.store(-1, std::memory_order_release);
        }
        for (std::thread& w : workers)
        {
            if (w.joinable())
            {
                w.join();
            }
        }
    }
};


// Test and check benchmark
template<class Counter>
bool check(const char* counter_name, int num_threads, long total)
{
    Counter counter;
    std::vector<std::vector<long>> tickets_got(num_threads); 
    worker_group group;

    // Cấp phát trước bộ nhớ để tránh việc đang chạy lại đi xin thêm RAM làm nhiễu kết quả
    for (int t = 0; t < num_threads; t++)
    {
        tickets_got[t].reserve(calculate_share(total, num_threads, t));
    }

    // Tạo các threads
    for (int t = 0; t < num_threads; t++)
    {
        group.workers.emplace_back([&counter, &tickets_got, &group, total, num_threads, t]()
        {
            if (!group.should_run()) return;
            
            handle_of<Counter> handle{counter};
            long target_incs = calculate_share(total, num_threads, t);
            
            for (long i = 0; i < target_incs; i++)
            {
                tickets_got[t].push_back(handle.inc());
            }
        });
    }

    group.start_and_join();

    bool is_ok = (counter.get() == total);

    // Nếu là ticket cấp toàn cục thì kiểm tra xem có ai bị lặp ticket hay ticket sai quy tắc không
    if (returns_global_tickets<Counter>)
    {
        std::vector<bool> is_seen(total, false);
        
        for (const auto& thread_tickets : tickets_got)
        {
            for (size_t i = 0; i < thread_tickets.size(); i++)
            {
                long ticket_val = thread_tickets[i];
                
                // Viết rõ ràng từng điều kiện lỗi thay vì gộp chung
                if (ticket_val < 0 || ticket_val >= total)
                {
                    is_ok = false; // ticket nằm ngoài phạm vi
                } 
                else if (is_seen[ticket_val])
                {
                    is_ok = false; // ticket bị cấp trùng cho thread khác
                } 
                else if (i > 0 && ticket_val <= thread_tickets[i - 1])
                {
                    is_ok = false; // ticket trong cùng 1 thread không tăng dần
                } 
                else
                {
                    is_seen[ticket_val] = true;
                }
            }
        }
    }

    std::printf("[%s] %s | threads: %d | expected: %ld | actual: %ld\n", 
                is_ok ? "PASS" : "FAIL", counter_name, num_threads, total, counter.get());
                
    return is_ok;
}


template<class Counter>
double bench(int num_threads, long total, int work_size)
{
    Counter counter;
    worker_group group;

    for (int t = 0; t < num_threads; t++)
    {
        group.workers.emplace_back([&counter, &group, total, num_threads, t, work_size]()
        {
            if (!group.should_run()) return;
            
            handle_of<Counter> handle{counter};
            long target_incs = calculate_share(total, num_threads, t);
            
            for (long i = 0; i < target_incs; ++i)
            {
                handle.inc();
                private_work(work_size);
            }
        });
    }

    // Chỉ bấm giờ từ lúc bắt đầu thả cửa chạy
    auto start_time = std::chrono::steady_clock::now();
    group.start_and_join();
    auto end_time = std::chrono::steady_clock::now();

    double seconds = std::chrono::duration<double>(end_time - start_time).count();

    if (counter.get() != total)
    {
        throw std::runtime_error("Đếm sai tổng số ở hàm bench!");
    }
    
    return seconds;
}


// Tìm số nguyên từ chuỗi ký tự
long parse_number(const char* arg, long min_val, long max_val)
{
    size_t end_idx;
    long value = std::stol(arg, &end_idx);
    
    if (arg[end_idx] != '\0' || value < min_val || value > max_val)
    {
        throw std::invalid_argument("Tham số đầu vào không hợp lệ hoặc nằm ngoài dải cho phép.");
    }
    return value;
}


int main(int argc, char** argv)
{
    try
    {
        const std::string mode = (argc > 1) ? argv[1] : "";

        // Test lỗi
        if (argc == 2 && mode == "test_worker_failure")
        {
            std::atomic<int> run_count{0};
            try
            {
                worker_group group;
                for (int t = 0; t < 3; t++)
                {
                    if (t == 2)
                    {
                        throw std::runtime_error("Giả lập văng lỗi khi tạo thread thứ 3");
                    }
                    group.workers.emplace_back([&group, &run_count]()
                    {
                        if (group.should_run())
                        {
                            run_count.fetch_add(1, std::memory_order_relaxed); 
                        }
                    });
                }
            } 
            catch (const std::runtime_error&)
            {
            }
            
            if (run_count.load(std::memory_order_relaxed) != 0)
            {
                throw std::runtime_error("Lỗi: Các thread vẫn chạy dù đã bị hủy.");
            }
            
            std::puts("PASS: Không treo chương trình");
            return 0;
        }

        if (argc == 2 && mode == "list")
        {
            std::puts("mutex\ncas\ncas_backoff\nfaa");
            return 0;
        }

        // Chế độ kiểm thử correctness
        if (argc == 2 && mode == "test")
        {
            int num_cores = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
            std::vector<int> test_threads = {1, 2, 4, num_cores};
            
            // Xóa phần tử trùng nếu num_cores rơi vào 1, 2 hoặc 4
            std::sort(test_threads.begin(), test_threads.end());
            test_threads.erase(std::unique(test_threads.begin(), test_threads.end()), test_threads.end());

            bool all_passed = true;

            for (int threads : test_threads)
            {
                // 3: test số lượng nhỏ
                // 1003: test chia việc không đều
                // 200000: test tranh chấp thực tế
                std::vector<long> workloads = {3L, 1003L, 200000L};
                
                for (long n : workloads)
                {
                    // Chạy từng lệnh 
                    bool res_mutex = check<counter_mutex>("mutex", threads, n);
                    bool res_cas = check<counter_compare_and_swap>("cas", threads, n);
                    bool res_cas_back = check<counter_compare_and_swap_backoff>("cas_backoff", threads, n);
                    bool res_faa = check<counter_fetch_and_add>("faa", threads, n);
                    if (!res_mutex || !res_cas || !res_cas_back || !res_faa)
                    {
                        all_passed = false;
                    }
                }
            }

            // Thử chạy với racy counter
            if (num_cores > 1)
            {
                bool is_detected = false;
                // Thử tối đa 20 lần xem có bắt được lỗi race condition không
                for (int attempt = 0; attempt < 20; attempt++)
                {
                    if (!check<counter_racy>("racy(expected FAIL)", num_cores, 200000L))
                    {
                        is_detected = true;
                        break;
                    }
                }
                
                if (is_detected)
                {
                    std::puts("PASS: Hàm check đã phát hiện lỗi của racy counter.");
                }
                else
                {
                    std::puts("FAIL: Hàm check không phát hiện ra racy counter!");
                    all_passed = false;
                }
            }
            
            return all_passed ? 0 : 1; 
        }

        // benchmark mode: counter_name, num_threads, total, work_size
        if (argc != 5)
        {
            std::fprintf(stderr, "Cú pháp: %s test | list | <counter_name> <num_threads> <total> <work_size>\n", argv[0]);
            return 1;
        }

        int threads = static_cast<int>(parse_number(argv[2], 1, 1024));
        long total = parse_number(argv[3], 1, 1000000000L);
        int work = static_cast<int>(parse_number(argv[4], 0, 1000000));
        
        double seconds = 0.0;
        
        if (mode == "mutex")
        {
            seconds = bench<counter_mutex>(threads, total, work);
        }
        else if (mode == "cas")
        {
            seconds = bench<counter_compare_and_swap>(threads, total, work);
        }
        else if (mode == "cas_backoff")
        {
            seconds = bench<counter_compare_and_swap_backoff>(threads, total, work);
        }
        else if (mode == "faa")
        {
            seconds = bench<counter_fetch_and_add>(threads, total, work);
        }
        else
        {
            throw std::invalid_argument("Counter is not available.");
        }

        // Xuất kết quả dạng CSV
        std::printf("%s, %d, %ld, %d, %.9f, %.0f\n", mode.c_str(), threads, total, work, seconds, total / seconds);
        return 0;

    } 
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "LỖI FATAL: %s\n", error.what());
        return 1;
    }
}