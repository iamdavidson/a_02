#!/usr/bin/env python3
# ==============================================================
# Overview plot for the initial campaign
# ==============================================================
# Reads one or more campaign CSV files (columns: jobs,lock,runtime_ns)
# and draws one line per file: the x-axis is the job count, the y-axis
# is the mean end-to-end runtime for that job count. Error bars show the
# sample standard deviation of the runtimes. The figure is saved to
# plots/overview.png.
#
# Usage:
#   python3 scripts/plot_campaign.py results/atomic.csv results/tas.csv results/ttas.csv
#
# Super simple by design: CSV paths are plain positional arguments and
# the output path is fixed. No argument-parsing library and no pandas -
# just the stdlib csv module, numpy for mean/std, and matplotlib.

import sys
import csv
import os

import numpy as np
import matplotlib
matplotlib.use("Agg")  # write a file, no display needed
import matplotlib.pyplot as plt


# Output goes next to the other artifacts, resolved relative to this
# script (project root is one level up from scripts/), so the script
# works no matter which directory it is called from.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_PATH = os.path.join(ROOT, "plots", "overview.png")


# Read one campaign CSV and collect the runtimes per job count.
# Returns (lock_name, {jobs: [runtime_ns, ...]}).
def read_campaign(path):

    runtimes = {}
    lock_name = None
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            jobs = int(row["jobs"])
            lock_name = row["lock"]
            runtimes.setdefault(jobs, []).append(float(row["runtime_ns"]))
    return lock_name, runtimes


def main():

    paths = sys.argv[1:]
    if not paths:
        print("usage: plot_campaign.py <csv> [<csv> ...]", file=sys.stderr)
        return 1

    fig, ax = plt.subplots()

    # One line per CSV: mean runtime per job count, error bars = std dev.
    for path in paths:
        lock_name, runtimes = read_campaign(path)
        if not runtimes:
            print(f"warning: no data in {path}, skipping", file=sys.stderr)
            continue

        job_counts = sorted(runtimes)
        means = [np.mean(runtimes[j]) for j in job_counts]
        stds = [np.std(runtimes[j], ddof=1) for j in job_counts]

        label = lock_name if lock_name else os.path.basename(path)
        ax.errorbar(job_counts, means, yerr=stds, marker="o", capsize=3, label=label)

    ax.set_xlabel("jobs (threads)")
    ax.set_ylabel("mean end-to-end runtime [ns]")
    ax.set_title("Initial campaign: mean runtime vs. thread count")
    ax.legend()
    ax.grid(True, linestyle=":", alpha=0.5)

    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    fig.savefig(OUT_PATH, dpi=150, bbox_inches="tight")
    print(f"saved {OUT_PATH}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
