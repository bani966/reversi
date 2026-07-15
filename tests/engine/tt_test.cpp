#include "reversi/tt.hpp"

#include "../support/benchmark_positions.hpp"
#include "../support/search_checks.hpp"
#include "reversi/moves.hpp"
#include "reversi/search.hpp"

#include <cstdint>
#include <gtest/gtest.h>

namespace reversi {
namespace {

TEST(Zobrist, DeterministicForTheSamePosition) {
    EXPECT_EQ(zobristHash(Position::start()), zobristHash(Position::start()));
    const Position mid = bench::benchmarkPositions().back();
    EXPECT_EQ(zobristHash(mid), zobristHash(mid));
}

TEST(Zobrist, SideToMoveIsPartOfTheHash) {
    // Same physical board, other side to move (own/opp swapped) must hash differently -
    // treating these as the same position would let search reuse a score computed for the
    // opponent's turn.
    const Position p = Position::start();
    EXPECT_NE(zobristHash(p), zobristHash(applyPass(p)));
}

TEST(Zobrist, DistinctPositionsInTheBenchmarkSetHashDistinctly) {
    const auto& positions = bench::benchmarkPositions();
    for (std::size_t i = 0; i < positions.size(); ++i) {
        for (std::size_t j = i + 1; j < positions.size(); ++j) {
            if (!(positions[i] == positions[j])) {
                EXPECT_NE(zobristHash(positions[i]), zobristHash(positions[j]))
                    << "positions " << i << " and " << j;
            }
        }
    }
}

TEST(TranspositionTable, ProbeMissesOnEmptyTableAndHitsAfterStore) {
    TranspositionTable tt(1024);
    const std::uint64_t key = zobristHash(Position::start());
    EXPECT_EQ(tt.probe(key), nullptr);

    tt.store(key, 5, 17, Bound::Lower, 42);
    const TTEntry* entry = tt.probe(key);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->key, key);
    EXPECT_EQ(entry->score, 17);
    EXPECT_EQ(entry->depth, 5);
    EXPECT_EQ(entry->bound, Bound::Lower);
    EXPECT_EQ(entry->bestMove, 42);
    EXPECT_EQ(tt.hits(), std::uint64_t{1});
}

TEST(TranspositionTable, IndexCollisionKeepsTheDeeperEntry) {
    // Capacity 1 forces every key into the same slot, making replacement behavior fully
    // deterministic to test: a shallower different-key store must not evict a deeper entry,
    // a deeper one must.
    TranspositionTable tt(1);
    EXPECT_EQ(tt.capacity(), std::size_t{1});
    tt.store(111, 8, 5, Bound::Exact, 10);
    tt.store(222, 3, -2, Bound::Upper, 20); // shallower, different key: rejected
    ASSERT_NE(tt.probe(111), nullptr);
    EXPECT_EQ(tt.probe(222), nullptr);
    tt.store(222, 9, -2, Bound::Upper, 20); // deeper, different key: evicts
    EXPECT_EQ(tt.probe(111), nullptr);
    ASSERT_NE(tt.probe(222), nullptr);
}

TEST(TranspositionTable, SameKeyStoreAlwaysUpdates) {
    TranspositionTable tt(1024);
    tt.store(333, 7, 4, Bound::Exact, 11);
    tt.store(333, 7, 6, Bound::Exact, 12); // same key, same depth: fresher result wins
    const TTEntry* entry = tt.probe(333);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->score, 6);
    EXPECT_EQ(entry->bestMove, 12);
}

TEST(TranspositionTable, ClearEmptiesEverySlot) {
    TranspositionTable tt(64);
    tt.store(1, 3, 1, Bound::Exact, 1);
    tt.store(2, 3, 2, Bound::Exact, 2);
    tt.clear();
    EXPECT_EQ(tt.probe(1), nullptr);
    EXPECT_EQ(tt.probe(2), nullptr);
    EXPECT_EQ(tt.hits(), std::uint64_t{0});
}

// The single most important test in M4: a correct transposition table is a pure time-saver
// and must NEVER change the answer - identical score at the same depth, with or without it.
// TT bugs (trusting an Upper/Lower bound as exact, reading an entry written at a shallower
// depth, hash collisions) don't crash; they make the engine quietly pick a subtly wrong
// move, which no other test in this suite would notice. Since M4 step 3 the TT's move hints
// also feed move ordering, which legitimately changes WHICH of several equal-scoring moves
// fail-soft search returns - so the returned move is verified to provably achieve the score
// (see search_checks.hpp) instead of being compared for strict equality.
TEST(TTSearch, SearchWithTTMatchesSearchWithoutOnBenchmarkSet) {
    for (const Position& pos : bench::benchmarkPositions()) {
        for (int depth = 1; depth <= 5; ++depth) {
            const SearchResult baseline = search(pos, depth);
            TranspositionTable tt(std::size_t{1} << 16);
            const SearchResult withTT = search(pos, depth, evaluateDiscDifferential, nullptr, &tt);
            EXPECT_EQ(withTT.score, baseline.score) << "depth " << depth;
            EXPECT_EQ(checks::rootMoveValue(pos, withTT.bestMove, depth), withTT.score)
                << "depth " << depth;
        }
    }
}

// Same contract for the intended hot path: one table deliberately shared across all
// iterations of an iterative-deepening run. Shallower iterations' entries linger in the
// table while deeper iterations run - the depth guard on probes is what this exercises.
TEST(TTSearch, IterativeWithSharedTTMatchesFixedDepthOnBenchmarkSet) {
    for (const Position& pos : bench::benchmarkPositions()) {
        const SearchResult baseline = search(pos, 5);
        TranspositionTable tt(std::size_t{1} << 16);
        const SearchResult iterative =
            searchIterative(pos, 5, evaluateDiscDifferential, nullptr, &tt);
        EXPECT_EQ(iterative.score, baseline.score);
        EXPECT_EQ(checks::rootMoveValue(pos, iterative.bestMove, 5), iterative.score);
        EXPECT_TRUE(iterative.completed);
    }
}

// Depth-7 variant of the equivalence check above, where transpositions (and therefore TT
// cutoffs) are far denser than at depth 5. Measured at ~0.5s for the whole set - cheap
// enough to run always, unlike the multi-minute DISABLED_ exit-criterion matches.
TEST(TTSearch, DeepSearchWithTTMatchesSearchWithout) {
    for (const Position& pos : bench::benchmarkPositions()) {
        const SearchResult baseline = search(pos, 7);
        TranspositionTable tt(std::size_t{1} << 20);
        const SearchResult withTT = search(pos, 7, evaluateDiscDifferential, nullptr, &tt);
        EXPECT_EQ(withTT.score, baseline.score);
        EXPECT_EQ(checks::rootMoveValue(pos, withTT.bestMove, 7), withTT.score);
    }
}

// Guards against the table being silently bypassed (which would pass every equality test
// above while making the TT decorative): search must actually hit it, and across the whole
// set it must remove work. Aggregate rather than per-position since M4 step 3: TT move hints
// reshuffle the ordering, which on an individual position can occasionally cost a few nodes
// even though the net effect is strongly positive.
TEST(TTSearch, TableIsConsultedAndRemovesWorkInAggregate) {
    std::uint64_t totalHits = 0;
    std::uint64_t nodesWithout = 0;
    std::uint64_t nodesWith = 0;
    for (const Position& pos : bench::benchmarkPositions()) {
        nodesWithout += search(pos, 5).nodes;
        TranspositionTable tt(std::size_t{1} << 16);
        nodesWith += search(pos, 5, evaluateDiscDifferential, nullptr, &tt).nodes;
        totalHits += tt.hits();
    }
    EXPECT_LT(nodesWith, nodesWithout);
    EXPECT_GT(totalHits, std::uint64_t{0});
}

} // namespace
} // namespace reversi
