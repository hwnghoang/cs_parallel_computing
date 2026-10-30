#!/usr/bin/env bash
# Builds build/test_barrier and build/bench_barrier.
#   ./build.sh                 # g++ by default; CXX=clang++ ./build.sh to change
#   NO_OPENMP=1 ./build.sh     # skip OpenMP (detected automatically otherwise)
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build

CXX="${CXX:-g++}"
FLAGS="-std=c++20 -O2 -Wall -Wextra -pthread"

OMP=""
if [[ "${NO_OPENMP:-0}" != "1" ]]; then
    if echo 'int main(){return 0;}' | "$CXX" -x c++ -fopenmp - -o build/omp_check >/dev/null 2>&1; then
        OMP="-fopenmp"
    else
        echo "note: $CXX cannot build with -fopenmp; the OpenMP barrier will be skipped"
    fi
    rm -f build/omp_check build/omp_check.exe
fi

# FLAGS and OMP are intentionally unquoted so they split into separate options.
"$CXX" $FLAGS test_barrier.cpp -o build/test_barrier
"$CXX" $FLAGS $OMP bench_barrier.cpp -o build/bench_barrier
echo "built: build/test_barrier build/bench_barrier (compiler: $CXX${OMP:+, with OpenMP})"
