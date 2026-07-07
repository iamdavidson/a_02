#!/usr/bin/env bash
# ==============================================================
# Backoff exploration campaign
# ==============================================================
# Sweeps several backoff min/max combinations to find good parameters
# for the backoff lock. All combinations are written into one CSV
# (results/backoff_explore.csv), one line per sample, using the lock
# name backoff-<min>-<max> so the combinations can be told apart later.
#
# Parameter grid (powers of two, so each step is roughly one extra
# doubling of the backoff window):
#   min = 1 16 64      # almost no initial wait  ->  start fairly high
#   max = 64 512 4096  # cap the window early    ->  allow a wide window
# Only combinations with min <= max are run. 50 samples per combination
# and per job count, over the same job counts as the initial campaign,
# so the tuning plot stays directly comparable to the overview.
#
# Rerunning regenerates the CSV from scratch (reproducible/restartable).

set -euo pipefail

# Project paths (this script lives in scripts/, root is one level up).
root="$(cd "$(dirname "$0")/.." && pwd)"
bin="$root/build/locks"
out="$root/results"
mkdir -p "$out" "$(dirname "$bin")"

# Build the program fresh with the required flags (must be warning-free).
g++-12 -std=c++20 -O2 -Wall -Wextra -pedantic -Wno-interference-size \
    -I"$root/include" "$root/src/main.cpp" -o "$bin"

# Exploration grid and campaign parameters.
mins="1 16 64"
maxs="64 512 4096"
jobs_list="1 2 4 6 8 10"
samples=50

# One CSV for the whole sweep: write the header once, then append the
# samples for every combination and job count. Data goes to stdout (the
# CSV); progress to stderr.
csv="$out/backoff_explore.csv"
echo "jobs,lock,runtime_ns" > "$csv"

for min in $mins; do
    for max in $maxs; do

        # Only valid windows: the minimum must not exceed the maximum.
        if [ "$min" -gt "$max" ]; then
            continue
        fi

        for jobs in $jobs_list; do
            "$bin" --lock backoff --jobs "$jobs" --samples "$samples" \
                --backoff-min "$min" --backoff-max "$max" >> "$csv"
            echo "done: backoff-$min-$max jobs=$jobs" >&2
        done
    done
done

echo "backoff exploration finished, CSV in $csv" >&2
