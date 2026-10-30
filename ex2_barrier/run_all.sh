#!/usr/bin/env bash
# Builds everything, records the machine's specs, runs the correctness tests
# and the benchmarks, and packs the results into results_<name>.tar.gz.
#
#   ./run_all.sh <name>          e.g. ./run_all.sh platform2_trang
#
# Optional environment variables:
#   TEST_EPISODES   episodes per correctness run   (default 20000)
#   BENCH_EPISODES  episodes per benchmark run     (default 100000)
#   BENCH_REPS      benchmark repetitions          (default 5)
set -euo pipefail
cd "$(dirname "$0")"

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <name>    (e.g. $0 platform2_trang)" >&2
    exit 1
fi
NAME="$1"
OUT="results/$NAME"
TEST_EPISODES="${TEST_EPISODES:-20000}"
BENCH_EPISODES="${BENCH_EPISODES:-100000}"
BENCH_REPS="${BENCH_REPS:-5}"
mkdir -p "$OUT"

step() { echo; echo "==> $*"; }

step "1/6 Building"
./build.sh 2>&1 | tee "$OUT/build.txt"

step "2/6 Recording system information -> $OUT/sysinfo.txt"
{
    echo "name: $NAME"
    echo "date: $(date)"
    echo "uname: $(uname -a)"
    case "$(uname -s)" in
        Linux)
            lscpu 2>/dev/null || grep -m1 "model name" /proc/cpuinfo
            grep MemTotal /proc/meminfo
            ;;
        Darwin)
            sysctl -n machdep.cpu.brand_string
            sysctl hw.ncpu hw.physicalcpu hw.memsize
            sysctl hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu 2>/dev/null || true
            sw_vers
            ;;
        MINGW* | MSYS* | CYGWIN*)
            powershell.exe -NoProfile -Command '
                Get-CimInstance Win32_Processor |
                    Format-List Name, NumberOfCores, NumberOfLogicalProcessors, MaxClockSpeed, L2CacheSize, L3CacheSize
                (Get-CimInstance Win32_OperatingSystem).Caption + " " + (Get-CimInstance Win32_OperatingSystem).Version
                "RAM GB: " + [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB, 1)
                "Model: " + (Get-CimInstance Win32_ComputerSystem).Model
                powercfg /getactivescheme'
            echo
            ;;
    esac
    echo "compiler: $("${CXX:-g++}" --version | head -n 1)"
} > "$OUT/sysinfo.txt" 2>&1
cat "$OUT/sysinfo.txt"

step "3/6 Correctness tests ($TEST_EPISODES episodes per run; runs with N = all cores can take minutes)"
build/test_barrier --episodes "$TEST_EPISODES" 2>&1 | tee "$OUT/test.txt" ||
    echo "WARNING: some correctness tests failed (see $OUT/test.txt); continuing"

step "4/6 Broken barriers (each should FAIL or HANG; a HANG stops after 10 s)"
build/test_barrier --buggy --episodes "$TEST_EPISODES" 2>&1 | tee "$OUT/buggy.txt"

step "5/6 Benchmark ($BENCH_EPISODES episodes x $BENCH_REPS reps)"
build/bench_barrier --episodes "$BENCH_EPISODES" --reps "$BENCH_REPS" \
    > "$OUT/bench.csv" 2> >(tee "$OUT/bench_summary.txt" >&2)

step "6/6 OpenMP with OMP_WAIT_POLICY=active and passive"
OMP_WAIT_POLICY=active build/bench_barrier --impl openmp --episodes "$BENCH_EPISODES" --reps "$BENCH_REPS" \
    > "$OUT/omp_active.csv"
OMP_WAIT_POLICY=passive build/bench_barrier --impl openmp --episodes "$BENCH_EPISODES" --reps "$BENCH_REPS" \
    > "$OUT/omp_passive.csv"

PYTHON="$(command -v python3 || command -v python || true)"
if [[ -n "$PYTHON" ]] && "$PYTHON" -c "import matplotlib" 2>/dev/null; then
    "$PYTHON" plot_results.py "$OUT/bench.csv"
else
    echo "(matplotlib not found: skipping the plot; the CSV is enough)"
fi

tar -czf "results_$NAME.tar.gz" -C results "$NAME"
echo
echo "Done. Send this file back: $(pwd)/results_$NAME.tar.gz"
