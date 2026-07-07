#!/usr/bin/env bash
# ==============================================================
# Full measurement run
# ==============================================================
# Single entry point for the whole measurement round on the test
# machine. It runs, one after another:
#   1. campaign.sh        - all fixed-parameter locks
#                           (atomic, tas, ttas, aq, alog, mcs)
#   2. backoff_explore.sh - the backoff min/max parameter sweep
#
# Together they produce every CSV the analysis needs and record the
# machine info, replacing any earlier results. Run this once on the CIP:
#
#   bash scripts/run_all.sh
#
# The two sub-scripts each rebuild the program first, so the extra build
# is intentional and keeps them independently runnable.

set -euo pipefail

# Project paths (this script lives in scripts/, root is one level up).
root="$(cd "$(dirname "$0")/.." && pwd)"

bash "$root/scripts/campaign.sh"
bash "$root/scripts/backoff_explore.sh"

echo "full run finished, all CSVs in $root/results" >&2
