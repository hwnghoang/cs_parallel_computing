# python run.py [total_increments=500000] [repetitions=5]
# python run.py 500000 5
# counter.csv, counter_study.log, environment.json, scalability_summary.csv, scalability.png

import csv
import datetime
import json
import platform
import statistics
import subprocess
import os
import sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

os.chdir(os.path.dirname(os.path.abspath(__file__)))
# increament each counter
if len(sys.argv) > 1:
    total = int(sys.argv[1])
else:
    total = 500000

# total repetitions for each counter
if len(sys.argv) > 2:
    repeat_times = int(sys.argv[2])
else:
    repeat_times = 5

cxx = os.environ.get('CXX', 'g++')                        
exe = os.path.abspath('counter.exe' if os.name == 'nt' else 'counter')

counters = ['mutex', 'cas', 'cas_backoff', 'faa']

# loại luồng trùng lặp
num_cores = os.cpu_count() or 1
threads = [1, 2, 4, 8]
if num_cores not in threads:
    threads.append(num_cores)
threads.sort()

# 0 = no work, 128 = some work, 4096 = more work
works = [0, 128, 4096]                                   

# Check compiler
subprocess.run([cxx, '-O2', '-std=c++17', '-Wall', '-Wextra', '-pthread', 'counter.cpp', '-o', exe], check = True)

# Check correctness of all counters.
test = subprocess.check_output([exe, 'test'], text = True) + subprocess.check_output([exe, 'test_worker_failure'], text = True)

# Measure.
times = {}  # (counter, threads, work) -> list of seconds
with open('counter_results.log', 'w', encoding = 'utf-8') as log, open('counter.csv', 'w', newline = '') as f:
    log.write(test)
    out = csv.writer(f)
    out.writerow(['counter', 'threads', 'total', 'work', 'seconds', 'incs_per_sec'])
    for work in works:
        for thread_count in threads:
            for name in counters:
                for _ in range(repeat_times):
                    line = subprocess.check_output([exe, name, str(thread_count), str(total), str(work)], text = True)
                    log.write(line)
                    row = line.strip().split(',')
                    out.writerow(row)
                    times.setdefault((name, thread_count, work), []).append(float(row[4]))
        print(f'Completed work={work}', flush = True)

# Note the environment build
with open('environment.json', 'w', encoding = 'utf-8') as f:
    json.dump(dict(date = datetime.datetime.now().isoformat(), 
                   os = platform.platform(), 
                   cpu = platform.processor(),
                   logical_processors = os.cpu_count(),
                   compiler = subprocess.check_output([cxx, '--version'], text = True).splitlines()[0],
                   total_increments = total, repetitions = repeat_times), f, indent = 2)

# Summary the scalability
with open('scalability_summary.csv', 'w', newline = '') as f:
    out = csv.writer(f)
    out.writerow(['counter', 'threads', 'work', 'median_seconds', 'min_seconds', 'max_seconds', 'speedup', 'efficiency'])
    for (name, thread_count, work), v in times.items():
        speedup = statistics.median(times[name, 1, work]) / statistics.median(v)
        out.writerow([name, thread_count, work, statistics.median(v), min(v), max(v), speedup, speedup / thread_count])

# Draw scalability chart
fig, axes = plt.subplots(1, len(works), figsize = (15, 4.5))
for ax, work in zip(axes, works):
    for name in counters:
        # Time in milliseconds for each thread count
        duration_ms = ([[s * 1000 for s in times[name, thread_count, work]] for thread_count in threads])

        # Calculate median, min, max for each thread count
        medians = [statistics.median(sublist) for sublist in duration_ms]
        mins = [min(sublist) for sublist in duration_ms]
        maxs = [max(sublist) for sublist in duration_ms]

        # set error bars
        lower_err = [median - min_val for median, min_val in zip(medians, mins)]
        upper_err = [max_val - median for median, max_val in zip(medians, maxs)]
        
        ax.errorbar(threads, medians, yerr = [lower_err, upper_err],
                    marker = 'o', capsize = 3, label = name)

        
    ax.set(title = f'work={work}', xlabel = 'Threads', ylabel = 'Median time (duration_ms; log scale)', yscale = 'log', xticks = threads)
    ax.grid(True, which = 'both', alpha = 0.25)
    ax.legend()

fig.suptitle(f'{total:,} increments; {repeat_times} runs; error bars: min/max')
fig.tight_layout()
fig.savefig('scalability.png', dpi = 160)