#pragma once

#include "reversi/position.hpp"

#include <cstdint>
#include <vector>

namespace reversi {

// 64-bit Zobrist hash of `p`: XOR of one fixed random key per (side, square) pair over all
// discs on the board. No separate side-to-move key is needed - Position is already stored
// relative to the mover, so the same physical board with the other side to move has its
// own/opp bitboards swapped and hashes differently by construction. The key table comes from
// a fixed-seed std::mt19937_64, whose output sequence the standard specifies exactly, so
// hashes are deterministic across runs, platforms, and compilers.
std::uint64_t zobristHash(const Position& p);

// What a stored score means relative to the search window it was computed under. Trusting a
// Lower/Upper bound as if it were Exact is precisely the class of silent wrong-move bug the
// TT correctness tests exist to catch.
enum class Bound : std::uint8_t {
    Exact, // score is the true value of the node at `depth`
    Lower, // node failed high: true value >= score
    Upper, // node failed low: true value <= score
};

struct TTEntry {
    std::uint64_t key = 0;
    int score = 0;
    std::int16_t depth = -1; // remaining search depth the score was computed at; -1 = empty slot
    Bound bound = Bound::Exact;
    std::int8_t bestMove = -1; // best move found at this node, -1 if none (fail-low nodes)
};

// Fixed-size, single-probe transposition table. Not thread-safe (M8's lazy SMP will revisit
// that); one table belongs to one search at a time.
//
// Replacement strategy: an incoming entry replaces the slot unless the slot holds a *deeper*
// entry for a *different* key (depth-preferred on index collisions; a same-key store always
// updates, since a same-depth-or-deeper result for the same position is at least as good).
// There is no aging/generation scheme yet - callers that reuse a table across unrelated
// searches should clear() between them; reuse across the iterations of one iterative-deepening
// run is the intended hot path and needs no clearing.
class TranspositionTable {
public:
    // `entryCount` is rounded up to the next power of two (the probe index is a mask).
    explicit TranspositionTable(std::size_t entryCount);

    // The stored entry for `key`, or nullptr on a miss. Non-const only because it counts
    // hits; it never mutates table contents.
    const TTEntry* probe(std::uint64_t key);

    void store(std::uint64_t key, int depth, int score, Bound bound, int bestMove);

    void clear();

    std::size_t capacity() const { return entries_.size(); }
    // Successful probes since construction/clear(). Exposed so tests can assert the table is
    // actually consulted by search, not silently bypassed.
    std::uint64_t hits() const { return hits_; }

private:
    std::vector<TTEntry> entries_;
    std::uint64_t mask_ = 0;
    std::uint64_t hits_ = 0;
};

} // namespace reversi
