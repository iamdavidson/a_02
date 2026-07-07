#!/usr/bin/env bash
# ==============================================================
# Machine information
# ==============================================================
# Prints the measurement environment to stdout so a campaign can save
# it next to its results. Fields that are not available stay empty
# instead of failing the whole script.

echo "date: $(date -Is)"
echo "host: $(hostname)"
echo "os: $(uname -srmo)"
if [ -r /etc/os-release ]; then
    echo "distro: $(. /etc/os-release && echo "$PRETTY_NAME")"
fi
echo "cpu_threads: $(nproc)"
echo "cpu_model: $(lscpu | sed -n 's/^Model name: *//p')"
echo "memory: $(free -h | awk '/^Mem:/ {print $2}')"
echo "compiler: $(g++-12 --version | head -1)"
if command -v python3 >/dev/null; then
    echo "python: $(python3 --version)"
fi
