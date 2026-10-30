// Correctness tests for the barriers in barriers.hpp.
//
// In episode e every thread writes e into its own slot, counts its arrival,
// calls the barrier, and then checks:
//   (I1) every slot holds e          - no thread left before all had written,
//                                      and the writes are visible (ordering)
//   (I2) arrivals >= (e+1)*N         - no thread left before all had arrived
//   (I3) arrivals <= (e+2)*N - 1     - no thread passed the next barrier early
// Slots alternate between two arrays by episode parity, so a thread that is
// one episode ahead never overwrites a slot that is still being checked.
// A watchdog reports a hang when no thread arrives for --timeout seconds.
//
// --buggy runs the incorrect barriers instead; each one should FAIL or HANG.
#include "barriers.hpp"
#include "cli.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto relaxed = std::memory_order_relaxed;
constexpr const char* kUsage =
    "[--threads 2,3,4] [--episodes N] [--impl NAME] [--no-delay] [--timeout SECONDS] [--buggy]";

struct Config {
    std::vector<int> threads;
    long episodes = 100000;
    bool delays = true;
    std::string impl;       // substring filter, empty = all
    long timeout_s = 0;     // 0 = default (120 s, or 10 s with --buggy)
    bool buggy = false;
};

// Relaxed atomics keep the test free of data races even for a broken
// barrier, while adding no ordering of their own.
struct alignas(kCacheLine) Slot {
    std::atomic<long> v{-1};
};

struct Result {
    long errors = 0;
    double seconds = 0;
};

template <class B>
Result run_test(int n, long episodes, const Config& cfg, std::string_view name) {
    B barrier(n);
    std::unique_ptr<Slot[]> buf[2] = {std::make_unique<Slot[]>(n), std::make_unique<Slot[]>(n)};
    alignas(kCacheLine) std::atomic<long> arrivals{0};
    alignas(kCacheLine) std::atomic<long> errors{0};
    std::atomic<bool> reported{false};
    std::atomic<int> done{0};

    auto report = [&](int tid, long e, const char* what, long got, long want) {
        errors.fetch_add(1, relaxed);
        if (!reported.exchange(true))
            std::fprintf(stderr, "    first error: thread %d, episode %ld: %s (got %ld, expected %ld)\n",
                         tid, e, what, got, want);
    };

    auto worker = [&](int tid) {
        std::minstd_rand rng(12345u + static_cast<unsigned>(tid));
        for (long e = 0; e < episodes; ++e) {
            Slot* cur = buf[e & 1].get();
            if (cfg.delays && (rng() & 15) == 0) spin_for(rng() % 2000);

            cur[tid].v.store(e, relaxed);
            arrivals.fetch_add(1, relaxed);

            barrier.arrive_and_wait(tid);

            for (int j = 0; j < n; ++j) {
                long v = cur[j].v.load(relaxed);
                if (v != e) report(tid, e, "a thread's write before the barrier is not visible", v, e);
            }
            long a = arrivals.load(relaxed);
            if (a < (e + 1) * n) report(tid, e, "left before all threads arrived", a, (e + 1) * n);
            if (a > (e + 2) * n - 1) report(tid, e, "another thread skipped the next barrier", a, (e + 2) * n - 1);
        }
        done.fetch_add(1);
    };

    const auto t0 = Clock::now();
    std::vector<std::thread> threads;
    threads.reserve(n);
    for (int i = 0; i < n; ++i) threads.emplace_back(worker, i);

    long last_arrivals = -1;
    auto last_progress = t0;
    while (done.load() < n) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const auto now = Clock::now();
        const long a = arrivals.load(relaxed);
        if (a != last_arrivals) {
            last_arrivals = a;
            last_progress = now;
        } else if (now - last_progress > std::chrono::seconds(cfg.timeout_s)) {
            // Hung threads cannot be stopped or joined, so the program ends here.
            std::printf("%-16s %4d %9ld   HANG   (stuck in episode %ld for %ld s, %ld errors so far)%s\n",
                        std::string(name).c_str(), n, episodes, a / n, cfg.timeout_s, errors.load(),
                        cfg.buggy ? "  <- bug detected, as expected" : "");
            std::printf("Stopping: a hung run cannot be recovered. Use --impl to run the rest.\n");
            std::fflush(stdout);
            std::fflush(stderr);
            std::_Exit(cfg.buggy ? 0 : 2);
        }
    }
    for (auto& t : threads) t.join();

    return {errors.load(), std::chrono::duration<double>(Clock::now() - t0).count()};
}

Config parse_args(int argc, char** argv) {
    Config c;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (!std::strcmp(arg, "--threads") && has_value) c.threads = parse_list(argv[++i], kUsage, argv[0]);
        else if (!std::strcmp(arg, "--episodes") && has_value) c.episodes = parse_positive(argv[++i], kUsage, argv[0]);
        else if (!std::strcmp(arg, "--impl") && has_value) c.impl = argv[++i];
        else if (!std::strcmp(arg, "--timeout") && has_value) c.timeout_s = parse_positive(argv[++i], kUsage, argv[0]);
        else if (!std::strcmp(arg, "--no-delay")) c.delays = false;
        else if (!std::strcmp(arg, "--buggy")) c.buggy = true;
        else usage_error(kUsage, argv[0]);
    }
    if (c.timeout_s == 0) c.timeout_s = c.buggy ? 10 : 120;
    return c;
}

}  // namespace

int main(int argc, char** argv) {
    Config cfg = parse_args(argc, argv);
    const int hw = hardware_threads();
    if (cfg.threads.empty()) cfg.threads = {2, 3, 4, hw, 2 * hw};
    sort_unique(cfg.threads);

    if (cfg.buggy) {
        std::printf("Running BROKEN barriers. Each one should FAIL or HANG.\n");
        std::printf("(sense-relaxed may PASS on x86, whose hardware orders memory strongly;\n"
                    " it is more likely to fail on ARM, e.g. Apple M-series.)\n\n");
    }
    std::printf("hardware threads: %d, episodes per run: %ld, random delays: %s\n\n", hw, cfg.episodes,
                cfg.delays ? "on" : "off");
    std::printf("%-16s %4s %9s   %-6s %s\n", "barrier", "N", "episodes", "result", "details");

    int runs = 0;
    int failures = 0;
    auto run_one = [&](auto tag, std::string_view name) {
        using B = typename decltype(tag)::type;
        if (!cfg.impl.empty() && name.find(cfg.impl) == std::string_view::npos) return;
        for (int n : cfg.threads) {
            // With more threads than cores each episode needs OS rescheduling,
            // which pure spinning makes very slow, so run fewer episodes.
            long episodes = cfg.episodes;
            if (n > hw) episodes = std::min(episodes, name == "sense-spin" ? 300L : 3000L);

            const Result r = run_test<B>(n, episodes, cfg, name);
            const bool ok = r.errors == 0;
            ++runs;
            if (!ok) ++failures;
            std::printf("%-16s %4d %9ld   %-6s %.2f s, %ld errors%s%s\n", std::string(name).c_str(), n, episodes,
                        ok ? "PASS" : "FAIL", r.seconds, r.errors, n > hw ? " (oversubscribed)" : "",
                        !ok && cfg.buggy ? "  <- bug detected, as expected" : "");
            std::fflush(stdout);
        }
    };

    if (cfg.buggy) for_each_buggy_barrier(run_one);
    else for_each_barrier(run_one);

    if (runs == 0) {
        std::printf("no barrier matched --impl '%s'\n", cfg.impl.c_str());
        return 1;
    }
    std::printf("\n%d of %d runs failed\n", failures, runs);
    return cfg.buggy || failures == 0 ? 0 : 1;
}
