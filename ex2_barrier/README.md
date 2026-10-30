# Exercise 2: Barrier Synchronization

Sense-reversing barrier (lecture 04, slide 14) in C++20, with a correctness
test harness and a throughput benchmark against `std::barrier`,
`pthread_barrier_t` and `#pragma omp barrier`.

| File | Content |
|---|---|
| `barriers.hpp` | Sense-reversing barrier (3 wait policies, 2 memory orderings), library barrier wrappers, and the incorrect barriers from slides 10 and 12 |
| `cli.hpp` | Command-line helpers shared by the two programs |
| `test_barrier.cpp` | Correctness tests with a hang watchdog |
| `bench_barrier.cpp` | Episodes per second with no work between barriers (CSV output) |
| `build.sh` | Builds both programs; detects OpenMP support |
| `run_all.sh` | Builds, records system info, runs all tests and benchmarks, packs the results |
| `plot_results.py` | Plots a benchmark CSV (needs matplotlib) |
| `results/` | Measured results, one folder per platform |

## Barriers

| Name | Description |
|---|---|
| `sense-spin` | Sense-reversing, acquire/release ordering, spins with `PAUSE` |
| `sense-backoff` | Same, exponential backoff while waiting (1 to 1024 pauses) |
| `sense-yield` | Same, spins 128 times then yields to the OS |
| `sense-seqcst` | Same as `sense-spin` with `seq_cst` ordering everywhere |
| `std-barrier` | C++20 `std::barrier` |
| `pthread-barrier` | `pthread_barrier_t` (not available on macOS) |
| `openmp` | `#pragma omp barrier` (benchmark only) |

## Requirements

A C++20 compiler: GCC 11+ or Clang 15+.

- **Windows:** [MSYS2](https://www.msys2.org/). In the *MSYS2 UCRT64* shell:
  `pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libgomp`
- **Linux:** `sudo apt install g++` (or your distribution's equivalent)
- **macOS:** Xcode command line tools (`xcode-select --install`). Apple clang
  has no OpenMP; `build.sh` then skips the OpenMP barrier. Use
  `brew install gcc` and `CXX=g++-<version> ./build.sh` (e.g. `g++-15`) to include it.

## Usage

Run everything on one machine:

```bash
./run_all.sh platform2_name      # creates results_platform2_name.tar.gz
```

Or run the steps by hand:

```bash
./build.sh
build/test_barrier                          # all barriers, N = 2, 3, 4, all cores, 2x cores
build/test_barrier --impl sense --threads 8 --episodes 1000000
build/test_barrier --buggy                  # incorrect barriers: each should FAIL or HANG
build/bench_barrier > bench.csv             # N = 1, 2, 4, ... all cores; 5 repetitions
OMP_WAIT_POLICY=active build/bench_barrier --impl openmp > omp_active.csv
python plot_results.py bench.csv
```

## How the tests work

In episode `e`, each thread writes `e` to its own slot, increments a shared
arrival counter, calls the barrier, then checks:

1. every slot holds `e` (no early departure, and the writes are visible);
2. arrivals ≥ (e+1)·N (all threads arrived before anyone left);
3. arrivals ≤ (e+2)·N − 1 (no thread passed the next barrier early).

Slots are relaxed atomics, so only the barrier's own ordering makes the
writes visible. Random delays vary the arrival order. A watchdog reports
`HANG` when no thread arrives for `--timeout` seconds.

`--buggy` checks that the tests catch real bugs: `buggy-flag` (slide 12)
fails, `buggy-count` (slide 10) fails or hangs, and `sense-relaxed` (no
memory ordering) may pass on x86, whose hardware orders memory strongly, but
is expected to fail on ARM.

With N equal to all hardware threads, spinning barriers can be very slow
on a busy machine: when the OS pauses one thread, the others spin until it
runs again. This is slow, not wrong.
