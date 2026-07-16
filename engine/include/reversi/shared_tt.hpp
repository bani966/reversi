#pragma once

#include "reversi/tt.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace reversi {

// Concurrent-safe counterpart to TranspositionTable (M8's Lazy SMP - see search.hpp's
// searchLazySmp()). TranspositionTable itself is intentionally left completely unsynchronized
// and untouched - it is still exactly what every single-threaded caller (search(), solveExact(),
// selectMove(), every M1-M7 test) uses, unmodified. This is a genuinely different type, not a
// drop-in replacement: TranspositionTable::probe()'s pointer-returning API is fundamentally
// incompatible with safe concurrent access regardless of how writes are synchronized - handing
// out a raw pointer into memory another thread can concurrently overwrite is a real
// time-of-check/time-of-use gap even when each individual write is itself safe. A genuinely
// concurrent-safe probe must return a validated COPY, which is a different contract.
//
// Design: TTEntry (key u64 + score i32 + depth i16 + bound u8 + bestMove i8) is exactly 16
// bytes - two u64 words. Each slot packs the non-key fields into one atomic word ("data") and
// stores `key XOR data` in a second atomic word ("keyXorData") - the standard lockless-hashing
// scheme (not a novel invention; the same technique real lazy-SMP engines use). On probe, both
// words are loaded independently and XORed back together; if the result doesn't match the
// queried key, the entry is treated as a miss - this covers BOTH a genuine miss (a different
// position occupies the slot) and a torn read (interleaved with a concurrent store) identically,
// because a torn combination of one store's "data" and a different store's "keyXorData" will
// not XOR back to any real key except with the same negligible probability as an ordinary
// 64-bit Zobrist hash collision - a risk class this project already accepts everywhere hashing
// is used, not a new one introduced here.
//
// Memory ordering: every load/store of the two atomic words uses memory_order_relaxed,
// deliberately, not as an oversight. This is safe because (a) each individual atomic op is
// indivisible - no UB, no torn bits within one 64-bit word - and (b) nothing OUTSIDE the two
// words themselves needs to be published or observed in any particular order - the XOR check
// alone is what makes any cross-store interleaving self-detecting, so no acquire/release
// ordering between the two loads (or the two stores) is needed for correctness.
//
// Store's depth-preferred replacement check (same policy as TranspositionTable) reads the old
// slot contents WITHOUT XOR-validating them first - this is a deliberate, safe simplification:
// a torn or stale read there can only cause a slightly-wrong REPLACEMENT decision (keep an
// entry that should have been evicted, or vice versa), which is a benign race on a performance
// heuristic, not a correctness-critical value. It never corrupts data (only store()'s own two
// atomic writes do that, and those are safe per the paragraph above) and never causes probe()
// to return a wrong answer (probe() re-validates independently, every time, via the XOR check).
class SharedTranspositionTable {
public:
    // `entryCount` is rounded up to the next power of two (the probe index is a mask), matching
    // TranspositionTable's own constructor convention exactly.
    explicit SharedTranspositionTable(std::size_t entryCount);

    // Returns a validated copy of the stored entry for `key`, or nullopt on a genuine miss OR a
    // detected torn/inconsistent read (both handled identically - see the class doc comment for
    // why this is safe). Never returns a pointer into internal storage - a concurrent store
    // could invalidate it the instant after this function returns.
    std::optional<TTEntry> probe(std::uint64_t key) const;

    void store(std::uint64_t key, int depth, int score, Bound bound, int bestMove);

    // NOT safe to call concurrently with any other probe()/store() call on this table - clear()
    // exists for the same "reset between unrelated searches" use case TranspositionTable's own
    // clear() serves, which is inherently a quiescent-table operation in this project's usage
    // (matches how TranspositionTable::clear() is only ever called between games, never mid
    // search, elsewhere in this codebase).
    void clear();

    std::size_t capacity() const { return slots_.size(); }
    // Successful probes since construction/clear() - same purpose as TranspositionTable::hits(),
    // exposed so tests can assert the table is actually consulted, not silently bypassed.
    std::uint64_t hits() const { return hits_.load(std::memory_order_relaxed); }

    // Test-only hook: writes RAW words into the slot for `key`, bypassing store()'s normal
    // packing entirely - lets tests deterministically construct a torn/inconsistent slot (one
    // word from one logical store, one from another) to exercise probe()'s rejection path
    // directly, without needing to actually win a real data race. Never used by production
    // code - exposed for the same reason solver.hpp exposes oddParitySquares: so an
    // easy-to-get-wrong piece of logic has a direct, deterministic test independent of the
    // rest of the class's behavior.
    void debugWriteRawWordsForTesting(std::uint64_t key, std::uint64_t keyXorData,
                                      std::uint64_t data);

private:
    struct Slot {
        std::atomic<std::uint64_t> keyXorData{0};
        std::atomic<std::uint64_t> data{0};
    };
    std::vector<Slot> slots_;
    std::uint64_t mask_ = 0;
    mutable std::atomic<std::uint64_t> hits_{0};
};

} // namespace reversi
