#pragma once

#include <atomic>
#include <optional>
#include <string_view>
#include <random>
#include <vector>
#include <cstddef>
#include <new>

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

// Exponential Backoff Lock (--lock backoff).
// Transcribed from lecture/07-SpinLocks.pdf, slide 60 "Exponential
// Backoff Lock" (class Backoff : public TTASlock), with the delay idea
// from slides 58-59.
//
// Once the lock looks free but our test_and_set() still loses the race,
// there is real contention, so instead of colliding again we wait a
// random duration before retrying. The delay window starts at
// backoff_min and doubles after each failed attempt, capped at
// backoff_max.
//
// Two deliberate deviations from the slide pseudocode:
//  - The slide sleeps; we busy-wait actively instead. Thread sleeping
//    has millisecond-scale granularity, far too coarse for this
//    micro-benchmark, and would measure the scheduler rather than the
//    lock (PHASES.md Phase 7, note 6; slide 62 "not portable"). The
//    delay unit is therefore a number of busy-wait iterations, not
//    nanoseconds.
//  - The random generator is thread_local. Several threads call lock()
//    on the same lock object at once, so one shared generator would be
//    a data race. Each thread keeps its own std::mt19937 (task 5).

// Active busy-wait for `count` iterations. The volatile sink store on
// every iteration is an observable side effect, so the compiler cannot
// optimize the spin away. (A volatile loop counter would trip the C++20
// -Wvolatile deprecation, hence the separate sink.)
static void backoff_busy_wait(const int count) {

    volatile int sink = 0;
    for (int i = 0; i < count; i++) {
        sink = i;
    }
    static_cast<void>(sink);
}

// A random wait length in [0, delay), drawn from a per-thread RNG.
// A delay of 0 means "no backoff", so we return 0 and avoid an empty
// distribution range.
static int backoff_random_wait(const int delay) {

    if (delay <= 0) {
        return 0;
    }

    thread_local std::mt19937 gen{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, delay - 1);
    return dist(gen);
}

class BackoffLock : public TTASLock {
    int backoff_min;
    int backoff_max;

public:
    BackoffLock(const int backoff_min_, const int backoff_max_)
    : backoff_min(backoff_min_), backoff_max(backoff_max_) {}

    void lock() override {

        int delay = backoff_min;
        while (true) {

            // Wait until the lock looks free (cheap read-only spin).
            while (state.test()) {}

            // Try to grab it; on success we are done.
            if (!state.test_and_set()) {
                return;
            }

            // Lost the race under contention: back off a random amount,
            // then widen the window up to backoff_max.
            backoff_busy_wait(backoff_random_wait(delay));
            if (delay < backoff_max) {
                delay = 2 * delay;
            }
        }
    }
};

// Anderson Queue Lock (--lock aq).
// Transcribed from lecture/07-SpinLocks.pdf, slides 65-66
// (template<unsigned int N> class ALock). Threads take a slot in a ring
// of flags and spin on their own slot; unlock hands the lock to the
// next slot. Two required adaptations from the slide:
//  - The thread count is a RUNTIME constructor argument, not a template
//    parameter (assignment: lock = ALock(N), not ALock<N>()), so the
//    flags live in a heap vector sized at construction.
//  - Each flags element sits on its own cacheline, so threads spinning
//    on neighbouring slots do not false-share one line. We pad each slot
//    with std::hardware_destructive_interference_size (that is what the
//    build flag -Wno-interference-size is there for).
class ALock : public Lock {

    // One flag per slot, each padded to its own cacheline (see above).
    struct alignas(std::hardware_destructive_interference_size) Slot {
        std::atomic<bool> flag{false};
    };

    std::vector<Slot> flags;
    std::atomic<std::size_t> next{0};
    std::size_t size;
    static inline thread_local std::size_t my_slot{0};

public:
    explicit ALock(const std::size_t n)
    : flags(n), size(n) {

        // Slot 0 starts free, all others taken, so the first acquirer
        // proceeds immediately (slide: flags = {true, false, ...}).
        flags[0].flag.store(true);
    }

    void lock() override {

        // Take the next slot (folding the slide's "% N" into the index)
        // and spin until the previous holder frees it.
        my_slot = next.fetch_add(1) % size;
        while (!flags[my_slot].flag.load()) {}
        flags[my_slot].flag.store(false);
    }

    void unlock() override {
        flags[(my_slot + 1) % size].flag.store(true);
    }
};

// ALog Lock (--lock alog): modified Anderson Queue Lock.
// From Exercise Sheet 09, task 9.2 ("Stop this unary madness already"):
// the Anderson lock needs an array of N flags, i.e. O(N) memory.
// Removing that array and letting every thread spin on a single shared
// "now serving" counter collapses the state to two integer counters,
// i.e. O(log2 N) memory complexity, while keeping the FIFO order. This
// is the classic ticket lock.
//
// Effect on the memory bus (sheet 09 follow-up question): it changes
// for the worse under contention. Anderson lets each waiter spin on its
// OWN slot (local spinning); here all waiters spin on the one owner
// counter, so every unlock invalidates that single cacheline on all
// waiting cores -> more coherence traffic than Anderson.
class ALogLock : public Lock {

    std::atomic<std::size_t> next_ticket{0};   // next ticket to hand out
    std::atomic<std::size_t> now_serving{0};   // ticket allowed in right now
    static inline thread_local std::size_t my_ticket{0};

public:
    void lock() override {
        my_ticket = next_ticket.fetch_add(1);
        while (now_serving.load() != my_ticket) {}
    }

    void unlock() override {

        // We hold the lock, so now_serving == my_ticket; hand it to the
        // next ticket in line. A plain store is enough because only the
        // holder ever writes now_serving.
        now_serving.store(my_ticket + 1);
    }
};

// MCS Queue Lock (--lock mcs).
// Transcribed from lecture/07-SpinLocks.pdf, slides 92-95. Threads form
// a linked queue through tail; each spins only on its own node's locked
// flag (local spinning). The per-thread node is thread_local BY VALUE,
// so there is no raw owning `new` as in the slide pseudocode.
class MCSLock : public Lock {

    struct QNode {
        std::atomic<QNode*> next;
        std::atomic<bool> locked;
        QNode() noexcept : next(nullptr), locked(false) {}
    };

    std::atomic<QNode*> tail{nullptr};
    static inline thread_local QNode my_node;

public:
    void lock() override {

        // Append our node to the queue. A non-null predecessor means we
        // must wait; we tell it to notify us and then spin on our flag.
        QNode* pred = tail.exchange(&my_node);
        if (pred != nullptr) {
            my_node.locked.store(true);
            pred->next.store(&my_node);
            while (my_node.locked.load()) {}
        }
    }

    void unlock() override {

        if (my_node.next.load() == nullptr) {

            // No known successor: try to close the queue. If the CAS
            // wins we were the tail and are done.
            QNode* self = &my_node;
            if (tail.compare_exchange_strong(self, nullptr)) {
                return;
            }

            // A successor is linking itself in right now; wait for it.
            while (my_node.next.load() == nullptr) {}
        }

        // Hand the lock to the successor and reset our node for reuse.
        my_node.next.load()->locked.store(false);
        my_node.next.store(nullptr);
    }
};
