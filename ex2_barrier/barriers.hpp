// Barrier implementations for Assignment 1, Exercise 2.
//
// Every barrier has the same interface, so tests and benchmarks are templates:
//     explicit B(int num_threads);
//     void arrive_and_wait(int tid);   // tid in [0, num_threads)
#pragma once

#include <atomic>
#include <barrier>
#include <cstddef>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#if defined(__has_include)
#  if __has_include(<pthread.h>) && !defined(__APPLE__)  // macOS lacks pthread_barrier_t
#    include <pthread.h>
#    define HAVE_PTHREAD_BARRIER 1
#  endif
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#  include <immintrin.h>
inline void cpu_relax() { _mm_pause(); }
#elif defined(_M_ARM64)
#  include <intrin.h>
inline void cpu_relax() { __yield(); }
#elif defined(__aarch64__) || defined(__arm__)
inline void cpu_relax() { asm volatile("yield" ::: "memory"); }
#else
inline void cpu_relax() {}
#endif

// 128 bytes: Intel prefetches cache lines in pairs; Apple M-series lines are 128 bytes.
inline constexpr std::size_t kCacheLine = 128;

inline void spin_for(unsigned iters) {
    for (unsigned i = 0; i < iters; ++i) cpu_relax();
}

// ---- Wait policies: called once per failed check of the spin loop. ----

struct SpinWait {
    void pause() { cpu_relax(); }
};

// Exponential backoff: 1, 2, 4, ... MaxSpins pauses.
template <unsigned MaxSpins = 1024>
struct BackoffWaitT {
    unsigned limit = 1;
    void pause() {
        spin_for(limit);
        if (limit < MaxSpins) limit <<= 1;
    }
};
using BackoffWait = BackoffWaitT<>;

// Spin briefly, then yield the core to the OS (useful with more threads than cores).
template <unsigned SpinsBeforeYield = 128>
struct YieldWaitT {
    unsigned spins = 0;
    void pause() {
        if (spins < SpinsBeforeYield) {
            ++spins;
            cpu_relax();
        } else {
            std::this_thread::yield();
        }
    }
};
using YieldWait = YieldWaitT<>;

// ---- Sense-reversing barrier (lecture 04, slide 14). ----

enum class Order {
    AcqRel,   // weakest correct ordering (default)
    SeqCst,   // C++ default ordering, for comparison
    Relaxed,  // incorrect; only used to check that the tests detect missing ordering
};

// The order to use for one operation, given its choice under Order::AcqRel.
template <Order O>
constexpr std::memory_order order_for(std::memory_order acq_rel_choice) {
    if constexpr (O == Order::SeqCst) return std::memory_order_seq_cst;
    else if constexpr (O == Order::Relaxed) return std::memory_order_relaxed;
    else return acq_rel_choice;
}

template <class Wait = SpinWait, Order O = Order::AcqRel>
class SenseReversingBarrier {
public:
    explicit SenseReversingBarrier(int num_threads)
        : n_(num_threads), local_(static_cast<std::size_t>(num_threads)) {}

    SenseReversingBarrier(const SenseReversingBarrier&) = delete;
    SenseReversingBarrier& operator=(const SenseReversingBarrier&) = delete;

    void arrive_and_wait(int tid) {
        const bool my_sense = !local_[tid].sense;
        local_[tid].sense = my_sense;

        // Release publishes this thread's writes; acquire lets the last
        // arriver, which does not read sense_, see everyone else's writes.
        if (count_.fetch_add(1, kRmw) == n_ - 1) {
            // Relaxed is enough: threads re-enter only after the release
            // store below, which is ordered after this reset.
            count_.store(0, kReset);
            sense_.store(my_sense, kRelease);
        } else {
            Wait w;
            while (sense_.load(kAcquire) != my_sense) w.pause();
        }
    }

private:
    static constexpr std::memory_order kRmw = order_for<O>(std::memory_order_acq_rel);
    static constexpr std::memory_order kRelease = order_for<O>(std::memory_order_release);
    static constexpr std::memory_order kAcquire = order_for<O>(std::memory_order_acquire);
    static constexpr std::memory_order kReset = order_for<O>(std::memory_order_relaxed);

    struct alignas(kCacheLine) LocalSense {
        bool sense = true;
    };

    const int n_;
    // Separate lines: arrivals write count_ while waiters spin on sense_.
    alignas(kCacheLine) std::atomic<int> count_{0};
    alignas(kCacheLine) std::atomic<bool> sense_{true};
    std::vector<LocalSense> local_;
};

// ---- Library barriers for comparison. ----

class StdBarrier {
public:
    explicit StdBarrier(int n) : b_(n) {}
    void arrive_and_wait(int) { b_.arrive_and_wait(); }

private:
    std::barrier<> b_;
};

#ifdef HAVE_PTHREAD_BARRIER
class PthreadBarrier {
public:
    explicit PthreadBarrier(int n) { pthread_barrier_init(&b_, nullptr, static_cast<unsigned>(n)); }
    ~PthreadBarrier() { pthread_barrier_destroy(&b_); }
    PthreadBarrier(const PthreadBarrier&) = delete;
    PthreadBarrier& operator=(const PthreadBarrier&) = delete;
    void arrive_and_wait(int) { pthread_barrier_wait(&b_); }

private:
    pthread_barrier_t b_;
};
#endif

// #pragma omp barrier is benchmarked separately in bench_barrier.cpp.

// ---- Incorrect barriers from the lecture, used by `test_barrier --buggy`. ----

// Slide 10. Waiters spin until count_ == N-1, which fails two ways: the
// second-to-last arriver sets N-1 itself and leaves early, and a waiter that
// misses the short window before the reset spins forever.
class BuggyCountBarrier {
public:
    explicit BuggyCountBarrier(int n) : n_(n) {}
    void arrive_and_wait(int) {
        if (count_.fetch_add(1) == n_ - 1) {
            count_.store(0);
        } else {
            while (count_.load() != n_ - 1) cpu_relax();
        }
    }

private:
    const int n_;
    alignas(kCacheLine) std::atomic<int> count_{0};
};

// Slide 12. The flag is never re-armed, so from the second episode on
// threads pass without waiting.
class BuggyFlagBarrier {
public:
    explicit BuggyFlagBarrier(int n) : n_(n) {}
    void arrive_and_wait(int) {
        if (count_.fetch_add(1) == n_ - 1) {
            count_.store(0);
            wait_.store(false);
        } else {
            while (wait_.load()) cpu_relax();
        }
    }

private:
    const int n_;
    alignas(kCacheLine) std::atomic<int> count_{0};
    alignas(kCacheLine) std::atomic<bool> wait_{true};
};

// ---- Registries: f(std::type_identity<Barrier>{}, name) for each barrier. ----

template <class F>
void for_each_barrier(F&& f) {
    using std::type_identity;
    f(type_identity<SenseReversingBarrier<SpinWait>>{}, std::string_view("sense-spin"));
    f(type_identity<SenseReversingBarrier<BackoffWait>>{}, std::string_view("sense-backoff"));
    f(type_identity<SenseReversingBarrier<YieldWait>>{}, std::string_view("sense-yield"));
    f(type_identity<SenseReversingBarrier<SpinWait, Order::SeqCst>>{}, std::string_view("sense-seqcst"));
    f(type_identity<StdBarrier>{}, std::string_view("std-barrier"));
#ifdef HAVE_PTHREAD_BARRIER
    f(type_identity<PthreadBarrier>{}, std::string_view("pthread-barrier"));
#endif
}

// buggy-count runs last because it usually hangs, which ends the program.
template <class F>
void for_each_buggy_barrier(F&& f) {
    using std::type_identity;
    f(type_identity<BuggyFlagBarrier>{}, std::string_view("buggy-flag"));
    f(type_identity<SenseReversingBarrier<SpinWait, Order::Relaxed>>{}, std::string_view("sense-relaxed"));
    f(type_identity<BuggyCountBarrier>{}, std::string_view("buggy-count"));
}
