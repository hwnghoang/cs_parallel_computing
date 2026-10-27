# Build, test and measure. Run in a separate copy on each machine.

# python run.py [total_increments=500000] [repetitions=5]
# Optional CXX environment variable: compiler executable path.
# Existing CSV/log/environment files are replaced.

import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent

def main():
    total = int(sys.argv[1]) if len(sys.argv) > 1 else 500000
    reps = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    if len(sys.argv) > 3 or not 1 <= total <= 1000000000 or reps < 1:
        raise ValueError('usage: python run.py [positive total <= 1000000000] [positive repetitions]')
    windows_compiler = Path('C:/Program Files/CodeBlocks/MinGW/bin/g++.exe')
    compiler = os.environ.get('CXX') or (str(windows_compiler) if windows_compiler.exists() else shutil.which('g++'))
    if not compiler:
        raise RuntimeError('C++17 compiler not found; set CXX to its executable path')
    flags = ['-O2', '-std=c++17', '-Wall', '-Wextra', '-pthread']
    version = subprocess.check_output([compiler, '--version'], text=True).splitlines()[0]
    binary = ROOT / ('counter.exe' if os.name == 'nt' else 'counter')
    subprocess.run([compiler, *flags, str(ROOT / 'counter.cpp'), '-o', str(binary)], check=True)
    test = subprocess.check_output([str(binary), 'test'], text=True)
    worker_failure = subprocess.check_output([str(binary), 'test_worker_failure'], text=True)
    threads = sorted({1, 2, 4, 8, os.cpu_count() or 1})
    counters, works = ['mutex', 'cas', 'cas_backoff', 'faa'], [0, 128, 4096]
    with (ROOT / 'counter_study.log').open('w', encoding='utf-8') as log, (ROOT / 'counter.csv').open('w', encoding='utf-8') as csv:
        log.write(test)
        log.write(worker_failure)
        csv.write('counter,threads,total,work,seconds,incs_per_sec\n')
        for work in works:
            for t in threads:
                for name in counters:
                    for _ in range(reps):
                        row = subprocess.check_output([str(binary), name, str(t), str(total), str(work)], text=True)
                        log.write(row)
                        csv.write(row)
            print(f'Completed work={work}', flush=True)
    cpu = platform.processor()
    if os.name == 'nt':
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'HARDWARE\DESCRIPTION\System\CentralProcessor\0') as key:
            cpu = winreg.QueryValueEx(key, 'ProcessorNameString')[0].strip()
    env = dict(date_local=datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=7))).isoformat(), os=platform.platform(),
               cpu=cpu, logical_processors=os.cpu_count(), compiler=version, compiler_path=compiler,
               flags=' '.join(flags), total_increments=total, repetitions=reps,
               counters=counters, threads=threads, work=works,
               test_threads=sorted({int(line.split()[2].split('=')[1]) for line in test.splitlines()}),
               thread_affinity='not pinned',
               timing='steady_clock from before worker creation until all workers joined; process startup and CSV output excluded',
               private_work='volatile unsigned recurrence, independent per call',
               sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in ['counter.cpp', 'run.py', 'analyze.py']})
    (ROOT / 'environment.json').write_text(json.dumps(env, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    subprocess.run([sys.executable, str(ROOT / 'analyze.py'), '--plot'], check=True)

if __name__ == '__main__':
    main()