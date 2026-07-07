#pragma once

#include "lock.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <charconv>
#include <iostream>

// ==============================================================
// Argument parsing
// ==============================================================
// Manual key-value scan over argv, using only the STL. Every flag
// starts with "--" and consumes the following token as its value, so
// argument order does not matter. Missing, unknown, or invalid
// arguments are errors: they print to std::cerr and abort parsing,
// keeping stdout free for CSV data.
//
// Backoff arguments (--backoff-min / --backoff-max) are added in
// Phase 7, not here.

// Parsed and validated command line.
struct Config {
    LockId lock{};
    std::string lock_name;   // canonical name, reused for CSV output later
    std::size_t jobs{};
    std::size_t samples{};
};

// One shared error path for all CLI failures: message plus usage, on
// stderr, so nothing pollutes the CSV stream on stdout.
inline void print_error(std::string_view msg) {

    std::cerr << "error: " << msg << "\n"
              << "usage: locks --lock <id> --jobs <n> --samples <n>\n"
              << "  <id>: atomic | tas | ttas | backoff | aq | alog | mcs\n"
              << "  <n> : positive integer (>= 1)\n";
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

    Config config;
    config.lock = *lock;
    config.lock_name = lock_name;
    config.jobs = *jobs;
    config.samples = *samples;
    return config;
}
