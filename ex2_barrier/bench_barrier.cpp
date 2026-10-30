// Barrier throughput: completed episodes per second with no work between barriers.
//
// CSV goes to stdout, a median summary to stderr:
//     bench_barrier [--threads 1,2,4] [--episodes N] [--reps R] [--impl NAME] > results.csv
#include "barriers.hpp"
#include "cli.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;
constexpr const char* kUsage = "[--threads 1,2,4] [--episodes N] [--reps R] [--impl NAME]";

struct Config {
    std::vector<int> threads;
    long episodes = 100000;
    long reps = 5;
    std::string impl;  // substring filter, empty = all
};

long warmup_episodes(long episodes) { return std::max(100L, episodes / 10); }

// Seconds for `episodes` back-to-back episodes on n threads, after an untimed
// warm-up. The calling thread takes part as thread 0.
template <class B>
double time_object_barrier(int n, long episodes) {
    B barrier(n);
    const long warmup = warmup_episodes(episodes);
    Clock::time_point t_start, t_end;

    auto worker = [&](int tid) {
        for (long e = 0; e < warmup; ++e) barrier.arrive_and_wait(tid);
        if (tid == 0) t_start = Clock::now();
        for (long e = 0; e < episodes; ++e) barrier.arrive_and_wait(tid);
        if (tid == 0) t_end = Clock::now();
    };

    std::vector<std::thread> threads;
    threads.reserve(n);
    for (int i = 1; i < n; ++i) threads.emplace_back(worker, i);
    worker(0);
    for (auto& t : threads) t.join();
    return std::chrono::duration<double>(t_end - t_start).count();
}

#ifdef _OPENMP
// Same measurement for `#pragma omp barrier`; depends on OMP_WAIT_POLICY.
double time_omp_barrier(int n, long episodes) {
    const long warmup = warmup_episodes(episodes);
    double t_start = 0, t_end = 0;
    int team_size = 0;
    omp_set_dynamic(0);
#pragma omp parallel num_threads(n)
    {
        for (long e = 0; e < warmup; ++e) {
#pragma omp barrier
        }
        if (omp_get_thread_num() == 0) {
            team_size = omp_get_num_threads();
            t_start = omp_get_wtime();
        }
        for (long e = 0; e < episodes; ++e) {
#pragma omp barrier
        }
        if (omp_get_thread_num() == 0) t_end = omp_get_wtime();
    }
    if (team_size != n) std::fprintf(stderr, "warning: OpenMP used %d threads instead of %d\n", team_size, n);
    return t_end - t_start;
}
#endif

Config parse_args(int argc, char** argv) {
    Config c;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (!std::strcmp(arg, "--threads") && has_value) c.threads = parse_list(argv[++i], kUsage, argv[0]);
        else if (!std::strcmp(arg, "--episodes") && has_value) c.episodes = parse_positive(argv[++i], kUsage, argv[0]);
        else if (!std::strcmp(arg, "--reps") && has_value) c.reps = parse_positive(argv[++i], kUsage, argv[0]);
        else if (!std::strcmp(arg, "--impl") && has_value) c.impl = argv[++i];
        else usage_error(kUsage, argv[0]);
    }
    return c;
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const std::size_t m = v.size() / 2;
    return v.size() % 2 ? v[m] : (v[m - 1] + v[m]) / 2;
}

}  // namespace

int main(int argc, char** argv) {
    Config cfg = parse_args(argc, argv);
    const int hw = hardware_threads();
    if (cfg.threads.empty()) {
        for (int t = 1; t <= hw; t *= 2) cfg.threads.push_back(t);
        cfg.threads.push_back(hw);
    }
    sort_unique(cfg.threads);

    std::fprintf(stderr, "hardware threads: %d, episodes: %ld, reps: %ld\n", hw, cfg.episodes, cfg.reps);
    std::fprintf(stderr, "%-16s %4s %16s %16s\n", "barrier", "N", "median eps/s", "median ns/ep");
    std::printf("impl,threads,hw_threads,episodes,rep,seconds,episodes_per_sec\n");

    auto measure = [&](std::string_view name, auto&& time_fn) {
        if (!cfg.impl.empty() && name.find(cfg.impl) == std::string_view::npos) return;
        for (int n : cfg.threads) {
            // Spinning with more threads than cores is very slow.
            const long episodes = n > hw ? std::max(100L, cfg.episodes / 100) : cfg.episodes;
            std::vector<double> rates;
            for (long r = 0; r < cfg.reps; ++r) {
                const double s = time_fn(n, episodes);
                rates.push_back(episodes / s);
                std::printf("%s,%d,%d,%ld,%ld,%.6f,%.1f\n", std::string(name).c_str(), n, hw, episodes, r, s,
                            rates.back());
                std::fflush(stdout);
            }
            const double med = median(rates);
            std::fprintf(stderr, "%-16s %4d %16.0f %16.1f\n", std::string(name).c_str(), n, med, 1e9 / med);
        }
    };

    for_each_barrier([&](auto tag, std::string_view name) {
        using B = typename decltype(tag)::type;
        measure(name, [](int n, long e) { return time_object_barrier<B>(n, e); });
    });
#ifdef _OPENMP
    measure("openmp", [](int n, long e) { return time_omp_barrier(n, e); });
#else
    std::fprintf(stderr, "built without OpenMP: skipping the OpenMP barrier\n");
#endif
    return 0;
}
