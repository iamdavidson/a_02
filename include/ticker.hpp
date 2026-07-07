#pragma once

#include "lock.hpp"

#include <atomic>
#include <cstddef>

// ==============================================================
// Ticker interface
// ==============================================================
// A Ticker is the shared counter that the worker threads increment.
// Every worker calls ticker++ exactly 10^5 times, so the whole
// measurement runs through a single virtual operator++. Two variants:
//  - STLAtomicTicker: the baseline, no explicit lock.
//  - LockedTicker:    a plain counter guarded by a Lock instance.
//
// The measurement loop only ever sees Ticker&, so it does not care
// which variant it is driving.
class Ticker {
public:
    virtual ~Ticker() = default;

    // Post-increment on purpose: the assignment specifies "ticker++".
    virtual void operator++(int) = 0;
};

// ==============================================================
// Ticker implementations
// ==============================================================

// Baseline case (--lock atomic).
// There is no explicit lock/unlock step here: the atomic read-modify-
// write IS the whole synchronization. This is why the baseline must
// use this ticker and never the LockedTicker path.
class STLAtomicTicker : public Ticker {
    std::atomic_size_t value{0};

public:
    void operator++(int) override {
        value++;
    }
};

// Locked case (all --lock ids except atomic).
// The counter itself is a plain std::size_t; correctness comes purely
// from the surrounding lock. operator++ acquires the lock, does the
// unsynchronized increment inside the critical section, then releases.
class LockedTicker : public Ticker {
    Lock& lock_;
    std::size_t value{0};

public:
    explicit LockedTicker(Lock& lock) : lock_(lock) {}

    void operator++(int) override {
        lock_.lock();
        value++;
        lock_.unlock();
    }
};
