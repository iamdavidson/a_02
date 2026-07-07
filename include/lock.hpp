#pragma once

#include <atomic>
#include <optional>
#include <string_view>

// ==============================================================
// Lock interface
// ==============================================================
// Abstract base for all spin-lock implementations. The concrete
// locks (tas, ttas, backoff, aq, alog, mcs) derive from this and are
// added in later phases. The measurement loop uses Lock only through
// a base-class reference, so it never depends on the concrete type.
//
// Per-thread bookkeeping (queue nodes for mcs, slot indices for aq)
// is kept inside the concrete lock, so the public API stays a plain
// lock()/unlock() pair for every implementation.
//
// Note: the atomic baseline (--lock atomic) does NOT use this
// interface. It uses STLAtomicTicker directly, since it has no
// explicit lock/unlock step.
class Lock {
public:
    virtual ~Lock() = default;

    virtual void lock() = 0;
    virtual void unlock() = 0;
};

// ==============================================================
// Lock registry
// ==============================================================
// The set of lock IDs accepted on the command line. atomic is the
// baseline; the rest are the spin locks implemented across phases.

enum class LockId {
    atomic,
    tas,
    ttas,
    backoff,
    aq,
    alog,
    mcs,
};

// Translate a --lock argument into a known LockId.
// Returns std::nullopt for an unknown name, which the CLI parser
// turns into an error.
inline std::optional<LockId> parse_lock_id(std::string_view name) {

    if (name == "atomic")  return LockId::atomic;
    if (name == "tas")     return LockId::tas;
    if (name == "ttas")    return LockId::ttas;
    if (name == "backoff") return LockId::backoff;
    if (name == "aq")      return LockId::aq;
    if (name == "alog")    return LockId::alog;
    if (name == "mcs")     return LockId::mcs;

    return std::nullopt;
}

// ==============================================================
// Lock implementations
// ==============================================================
// Transcribed from lecture/07-SpinLocks.pdf, slides "Test-and-Set
// Lock" and "Test-and-Test-and-Set Locks".
//
// Memory ordering: we deliberately keep the default (seq_cst) order
// used on the slides instead of hand-tuning acquire/release. The goal
// of this assignment is to reproduce the lecture's comparison of these
// locks, so the primitives should match the slides one-to-one rather
// than being optimized on our own.

// Test-and-Set Lock (--lock tas).
//  - lock():   spin on test_and_set() until it returns false (we won).
//  - unlock(): clear the flag so the next waiter can acquire it.
// state is protected so TTASLock can reuse it, matching the slides.
class TASLock : public Lock {
protected:
    std::atomic_flag state{};

public:
    void lock() override {
        while (state.test_and_set()) {}
    }

    void unlock() override {
        state.clear();
    }
};

// Test-and-Test-and-Set Lock (--lock ttas).
// Same state as TAS, but spins on a plain read (test()) while the lock
// looks taken and only issues the expensive test_and_set() once the
// flag appears free. This keeps the contended waiters in their local
// cache instead of hammering the bus with RMW operations.
class TTASLock : public TASLock {
public:
    void lock() override {
        while (true) {

            // Wait until the lock looks free (cheap read-only spin).
            while (state.test()) {}

            // Then try to grab it; if we lose the race, go back to spinning.
            if (!state.test_and_set()) {
                return;
            }
        }
    }
};
