#include "cli.hpp"
#include "ticker.hpp"

#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

// ==============================================================
// Worker
// ==============================================================
// Every thread runs the same work: increment the shared ticker
// exactly 10^5 times. The ticker is shared by reference, so all
// threads contend on the same lock / atomic.

constexpr std::size_t iterations = 100000;   // 10^5 increments per thread

static void worker(Ticker& ticker) {
    for (std::size_t i = 0; i < iterations; i++) {
        ticker++;
    }
}

// ==============================================================
// Lock creation
// ==============================================================
// Build the concrete Lock for a given config. The atomic baseline has
// no lock and is handled separately, so it never reaches here. backoff
// takes its min/max window and aq its thread count from the config. All
// lock ids are implemented; the default case is a defensive fallback
// (main reports it) in case a new LockId is added without wiring here.
static std::unique_ptr<Lock> make_lock(const Config& config) {

    switch (config.lock) {
        case LockId::tas:     return std::make_unique<TASLock>();
        case LockId::ttas:    return std::make_unique<TTASLock>();
        case LockId::backoff: return std::make_unique<BackoffLock>(config.backoff_min,
                                                                   config.backoff_max);
        case LockId::aq:      return std::make_unique<ALock>(config.jobs);
        case LockId::alog:    return std::make_unique<ALogLock>();
        case LockId::mcs:     return std::make_unique<MCSLock>();
        default:              return nullptr;
    }
}

// ==============================================================
// Measurement
// ==============================================================
// Run one sample: spawn `jobs` threads, let each do its 10^5
// increments, wait for all of them, and return the elapsed time. Only
// the parallel work section is measured, matching the assignment flow:
// spawn threads, execute work, wait for all threads to finish.
//
// The thread container is passed in and reused across samples. It is
// cleared here (outside the timed region); clear() keeps its capacity,
// so the emplace_back spawns below allocate no buffer memory and only
// the real spawn/join cost lands inside the measurement.
static std::chrono::nanoseconds run_sample(Ticker& ticker, std::size_t jobs,
                                           std::vector<std::jthread>& threads) {

    threads.clear();

    const auto start = std::chrono::steady_clock::now();

    for (std::size_t j = 0; j < jobs; j++) {
        threads.emplace_back(worker, std::ref(ticker));
    }

    // Explicit wait step: the clock must only stop once every thread
    // has finished, so we join here instead of relying on the
    // jthread destructor.
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    const auto stop = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start);
}

// ==============================================================
// Main
// ==============================================================

int main(int argc, char* argv[]) {

    // Argument parsing
    std::optional<Config> config = parse_args(argc, argv);
    if (!config) return 1;

    // Lock instantiation
    //  - atomic is the baseline and uses STLAtomicTicker, no lock.
    //  - every other id needs a concrete Lock; if make_lock returns
    //    nullptr the lock is not implemented in this phase yet.
    const bool is_atomic = (config->lock == LockId::atomic);

    std::unique_ptr<Lock> lock;
    if (!is_atomic) {
        lock = make_lock(*config);
        if (!lock) {
            std::cerr << "error: lock '" << config->lock_name
                      << "' is not implemented yet\n";
            return 1;
        }
    }

    // Thread container is allocated once, before any measurement, and
    // reused for every sample. reserve(jobs) sizes its buffer up front
    // so the per-sample spawns never reallocate inside the timed region.
    std::vector<std::jthread> threads;
    threads.reserve(config->jobs);

    // Sample loop
    // A fresh Ticker is created for every sample (outside the timed
    // region) so each measurement starts from a clean counter. One CSV
    // data line is printed per sample: jobs,lock,runtime_ns.
    for (std::size_t s = 0; s < config->samples; s++) {

        std::chrono::nanoseconds elapsed{};

        if (is_atomic) {
            STLAtomicTicker ticker;
            elapsed = run_sample(ticker, config->jobs, threads);
        }
        else {
            LockedTicker ticker(*lock);
            elapsed = run_sample(ticker, config->jobs, threads);
        }

        std::cout << config->jobs << ',' << config->lock_name << ','
                  << elapsed.count() << '\n';
    }

    return 0;
}
