# Validate measurements and summarize strong scaling; optional Matplotlib plot.
import csv
import hashlib
import itertools
import json
import math
from pathlib import Path
import statistics
import sys

ROOT = Path(__file__).resolve().parent

def analyze():
    env = json.loads((ROOT / 'environment.json').read_text(encoding='utf-8'))
    for name, digest in env['sha256'].items():
        assert hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == digest, name
    expected = set(itertools.product(env['counters'], env['threads'], env['work']))
    with (ROOT / 'counter.csv').open(newline='', encoding='utf-8-sig') as f:
        rows = list(csv.DictReader(f))
    groups = {}
    for row in rows:
        key = row['counter'], int(row['threads']), int(row['work'])
        seconds = float(row['seconds'])
        assert key in expected and int(row['total']) == env['total_increments'], row
        assert math.isfinite(seconds) and seconds > 0, row
        # C++ prints throughput with %.0f, so allow half an increment/second of rounding.
        assert math.isclose(float(row['incs_per_sec']), int(row['total']) / seconds,
                            rel_tol=1e-5, abs_tol=0.5), row
        groups.setdefault(key, []).append(seconds)
    assert set(groups) == expected
    assert all(len(v) == env['repetitions'] for v in groups.values())
    log = (ROOT / 'counter_study.log').read_text(encoding='utf-8-sig')
    assert '[FAIL]' not in log and 'ERROR' not in log
    tests = set()
    for line in log.splitlines():
        if line.startswith('[PASS]'):
            _, name, t, n, final = line.split()
            t, n, final = (int(x.split('=')[1]) for x in (t, n, final))
            assert final == n
            tests.add((name, t, n))
    test_threads = env['test_threads']
    assert tests == set(itertools.product(env['counters'], test_threads, (3, 1003, 200000)))
    summary = []
    for work, name, threads in itertools.product(env['work'], env['counters'], env['threads']):
        values = groups[name, threads, work]
        median = statistics.median(values)
        speedup = statistics.median(groups[name, 1, work]) / median
        summary.append(dict(counter=name, threads=threads, work=work, runs=len(values),
                            median_seconds=median, min_seconds=min(values), max_seconds=max(values),
                            incs_per_sec=env['total_increments'] / median,
                            speedup=speedup, efficiency=speedup / threads))
    with (ROOT / 'scalability_summary.csv').open('w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)
    print(f'PASS: {len(tests)} correctness checks; {len(rows)} samples; {len(summary)} configurations; source hashes match.')
    if '--plot' in sys.argv:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        fig, axes = plt.subplots(1, len(env['work']), figsize=(15, 4.5), squeeze=False)
        for ax, work in zip(axes[0], env['work']):
            for name in env['counters']:
                group = [r for r in summary if r['counter'] == name and r['work'] == work]
                medians = [r['median_seconds'] * 1000 for r in group]
                ax.errorbar(env['threads'], medians, marker='o', capsize=3, label=name,
                            yerr=[[1000*(r['median_seconds']-r['min_seconds']) for r in group],
                                  [1000*(r['max_seconds']-r['median_seconds']) for r in group]])
            ax.set(title=f'work={work}', xlabel='Threads', ylabel='Median time (ms; log scale)', yscale='log')
            ax.set_xticks(env['threads'])
            ax.grid(True, which='both', alpha=0.25)
            ax.legend()
        fig.suptitle(f"{env['total_increments']:,} increments; {env['repetitions']} runs; error bars: min/max")
        fig.tight_layout()
        fig.savefig(ROOT / 'scalability.png', dpi=160)
        plt.close(fig)
    return env, summary

if __name__ == '__main__':
    analyze()