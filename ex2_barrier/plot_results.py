"""Plot median barrier throughput against thread count.

Usage: python plot_results.py results.csv [more.csv ...]
Writes <name>.png next to each CSV. Requires matplotlib.
"""
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def plot(csv_path: Path) -> Path:
    rates = defaultdict(list)  # (impl, threads) -> episodes/s of each repetition
    hw_threads = "?"
    with csv_path.open(newline="") as f:
        for row in csv.DictReader(f):
            rates[(row["impl"], int(row["threads"]))].append(float(row["episodes_per_sec"]))
            hw_threads = row["hw_threads"]

    fig, ax = plt.subplots(figsize=(8, 5))
    for impl in sorted({impl for impl, _ in rates}):
        threads = sorted(t for i, t in rates if i == impl)
        ax.plot(threads, [statistics.median(rates[(impl, t)]) for t in threads], marker="o", label=impl)

    ax.set_xscale("log", base=2)
    ax.set_yscale("log")
    ax.set_xlabel("threads")
    ax.set_ylabel("episodes per second (median)")
    ax.set_title(f"{csv_path.stem} ({hw_threads} hardware threads)")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend()
    fig.tight_layout()
    out = csv_path.with_suffix(".png")
    fig.savefig(out, dpi=150)
    return out


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for arg in sys.argv[1:]:
        print("wrote", plot(Path(arg)))
