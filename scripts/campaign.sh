#!/usr/bin/env bash
# ==============================================================
# Campaign (all locks except backoff)
# ==============================================================
# Measures every lock with fixed parameters over the assignment's job
# counts:
#   locks   = atomic, tas, ttas, aq, alog, mcs
#   jobs    = 1, 2, 4, 6, 8, 10
#   samples = 50
#
# backoff is NOT run here: it has its own parameter sweep in
# backoff_explore.sh, and the final backoff.csv is taken from the best
# combination of that sweep. Use run_all.sh to do both in one round.
#
# This is a superset of the initial atomic/tas/ttas campaign (Table 1):
# rerunning regenerates every CSV from scratch, so it is reproducible
# and restartable. If it stops, run it again.

set -euo pipefail

# Project paths (this script lives in scripts/, root is one level up).
root="$(cd "$(dirname "$0")/.." && pwd)"
bin="$root/build/locks"
out="$root/results"
mkdir -p "$out" "$(dirname "$bin")"

# Build the program fresh with the required flags (must be warning-free).
g++-12 -std=c++20 -O2 -Wall -Wextra -pedantic -Wno-interference-size \
    -I"$root/include" "$root/src/main.cpp" -o "$bin"

# Record the measurement environment next to the results.
bash "$root/scripts/machine_info.sh" > "$out/machine_info.txt"

# Campaign parameters (all fixed-parameter locks; backoff is separate).
locks="atomic tas ttas aq alog mcs"
jobs_list="1 2 4 6 8 10"
samples=50

# One CSV per lock: write the header once, then append the samples for
# each job count. Data goes to stdout (the CSV); progress to stderr.
for lock in $locks; do
    csv="$out/$lock.csv"
    echo "jobs,lock,runtime_ns" > "$csv"
    for jobs in $jobs_list; do
        "$bin" --lock "$lock" --jobs "$jobs" --samples "$samples" >> "$csv"
        echo "done: lock=$lock jobs=$jobs" >&2
    done
done

echo "campaign finished, CSVs in $out" >&2
