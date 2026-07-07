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

# Frequency scaling, turbo, and SMT settings, read-only. Recorded so the
# measurement can be reproduced on an identically configured machine
# (assignment: relevant BIOS/UEFI/OS settings). Deep BIOS switches are
# not readable from user space; these are the observable effects.
# Missing sysfs paths just yield empty values instead of failing.
echo "cpu_mhz_min: $(lscpu | sed -n 's/^CPU min MHz: *//p')"
echo "cpu_mhz_max: $(lscpu | sed -n 's/^CPU max MHz: *//p')"
echo "cpu_governor: $(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null | sort -u | paste -sd, -)"
echo "cpu_scaling_driver: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_driver 2>/dev/null)"

# Turbo/boost: interpret the vendor flag and keep the raw value too.
if [ -r /sys/devices/system/cpu/intel_pstate/no_turbo ]; then
    no_turbo=$(cat /sys/devices/system/cpu/intel_pstate/no_turbo)
    [ "$no_turbo" = "0" ] && turbo=enabled || turbo=disabled
    echo "turbo_boost: $turbo (intel_pstate no_turbo=$no_turbo)"
elif [ -r /sys/devices/system/cpu/cpufreq/boost ]; then
    boost=$(cat /sys/devices/system/cpu/cpufreq/boost)
    [ "$boost" = "1" ] && turbo=enabled || turbo=disabled
    echo "turbo_boost: $turbo (cpufreq boost=$boost)"
else
    echo "turbo_boost: unknown"
fi

echo "threads_per_core: $(lscpu | sed -n 's/^Thread(s) per core: *//p')"
echo "smt_active: $(cat /sys/devices/system/cpu/smt/active 2>/dev/null)"
echo "numa_nodes: $(lscpu | sed -n 's/^NUMA node(s): *//p')"
echo "cpu_idle_states: $(cat /sys/devices/system/cpu/cpu0/cpuidle/state*/name 2>/dev/null | paste -sd, -)"
