#pragma once

#include "lock.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <charconv>
#include <iostream>
#include <limits>

// ==============================================================
// Argument parsing
// ==============================================================
// Manual key-value scan over argv, using only the STL. Every flag
// starts with "--" and consumes the following token as its value, so
// argument order does not matter. Missing, unknown, or invalid
// arguments are errors: they print to std::cerr and abort parsing,
// keeping stdout free for CSV data.
//
// Backoff arguments (--backoff-min / --backoff-max) are non-negative
// integers. They are REQUIRED for --lock backoff (with
// backoff-min <= backoff-max) and IGNORED for every other lock. This
// keeps one uniform CLI and a single campaign path; the choice is
// documented here and in the usage text.

// Parsed and validated command line.
struct Config {
    LockId lock{};
    std::string lock_name;   // canonical name, reused for CSV output later
    std::size_t jobs{};
    std::size_t samples{};
    int backoff_min{0};      // used only for --lock backoff
    int backoff_max{0};      // used only for --lock backoff
};

// One shared error path for all CLI failures: message plus usage, on
// stderr, so nothing pollutes the CSV stream on stdout.
inline void print_error(std::string_view msg) {

    std::cerr << "error: " << msg << "\n"
              << "usage: locks --lock <id> --jobs <n> --samples <n>\n"
              << "             [--backoff-min <m> --backoff-max <M>]\n"
              << "  <id>    : atomic | tas | ttas | backoff | aq | alog | mcs\n"
              << "  <n>     : positive integer (>= 1)\n"
              << "  <m>,<M> : non-negative integers; required for --lock backoff\n"
              << "            (backoff-min <= backoff-max), ignored for other locks\n";
}

// Parse a whole token as a std::size_t.
// std::from_chars is used instead of std::stoull because it does not
// throw, rejects a leading '-' (std::stoull would silently wrap it to
// a huge value), and reports where parsing stopped so we can confirm
// the ENTIRE token was consumed. That makes "8abc" or "" a clean
// error instead of a silent 8 or 0.
inline std::optional<std::size_t> parse_size(std::string_view text) {

    std::size_t value{};
    const char* begin = text.data();
    const char* end = text.data() + text.size();

    auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }

    return value;
}

// Scan argv and build a validated Config.
//  - Accepts --lock, --jobs, --samples in any order.
//  - Rejects unknown flags, missing values, and out-of-range numbers.
//  - Requires all three arguments; --lock has no default.
inline std::optional<Config> parse_args(int argc, char* argv[]) {

    std::optional<LockId> lock;
    std::string lock_name;
    std::optional<std::size_t> jobs;
    std::optional<std::size_t> samples;
    std::optional<std::size_t> backoff_min;
    std::optional<std::size_t> backoff_max;

    // Upper bound for the backoff values: they are stored as int in the
    // lock, so reject anything that would not fit.
    constexpr std::size_t int_max =
        static_cast<std::size_t>(std::numeric_limits<int>::max());

    int i = 1;
    while (i < argc) {

        std::string_view flag = argv[i];

        if (!flag.starts_with("--")) {
            print_error(std::string("unexpected argument: ") + std::string(flag));
            return std::nullopt;
        }

        // Every known flag consumes exactly one value; it must exist.
        if (i + 1 >= argc) {
            print_error(std::string("missing value for ") + std::string(flag));
            return std::nullopt;
        }

        std::string_view value = argv[i + 1];

        if (flag == "--lock") {
            lock = parse_lock_id(value);
            if (!lock) {
                print_error(std::string("unknown lock id: ") + std::string(value));
                return std::nullopt;
            }
            lock_name = std::string(value);
        }
        else if (flag == "--jobs") {
            jobs = parse_size(value);
            if (!jobs || *jobs < 1) {
                print_error("--jobs must be a positive integer (>= 1)");
                return std::nullopt;
            }
        }
        else if (flag == "--samples") {
            samples = parse_size(value);
            if (!samples || *samples < 1) {
                print_error("--samples must be a positive integer (>= 1)");
                return std::nullopt;
            }
        }
        else if (flag == "--backoff-min") {
            backoff_min = parse_size(value);
            if (!backoff_min || *backoff_min > int_max) {
                print_error("--backoff-min must be a non-negative integer");
                return std::nullopt;
            }
        }
        else if (flag == "--backoff-max") {
            backoff_max = parse_size(value);
            if (!backoff_max || *backoff_max > int_max) {
                print_error("--backoff-max must be a non-negative integer");
                return std::nullopt;
            }
        }
        else {
            print_error(std::string("unknown argument: ") + std::string(flag));
            return std::nullopt;
        }

        i += 2;
    }

    // All required arguments must be present.
    if (!lock || !jobs || !samples) {
        print_error("missing required argument (need --lock, --jobs, --samples)");
        return std::nullopt;
    }

    // Backoff arguments are required for --lock backoff (with
    // min <= max) and ignored for every other lock.
    if (*lock == LockId::backoff) {
        if (!backoff_min || !backoff_max) {
            print_error("--lock backoff requires --backoff-min and --backoff-max");
            return std::nullopt;
        }
        if (*backoff_min > *backoff_max) {
            print_error("--backoff-min must be <= --backoff-max");
            return std::nullopt;
        }
    }

    Config config;
    config.lock = *lock;
    config.jobs = *jobs;
    config.samples = *samples;

    if (*lock == LockId::backoff) {
        config.backoff_min = static_cast<int>(*backoff_min);
        config.backoff_max = static_cast<int>(*backoff_max);

        // The CSV lock name carries the parameters: backoff-<min>-<max>.
        config.lock_name = "backoff-" + std::to_string(config.backoff_min)
                         + "-" + std::to_string(config.backoff_max);
    }
    else {
        config.lock_name = lock_name;
    }

    return config;
}
