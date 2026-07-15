#include "reversi/search.hpp"

#include "../support/baseline_search.hpp"
#include "../support/benchmark_positions.hpp"
#include "../support/search_checks.hpp"

#include <cstdint>
#include <gtest/gtest.h>

namespace reversi {
namespace {

// M4 step 3's correctness contract. Move ordering and PVS are pruning/scheduling
// optimizations: the returned SCORE must be identical to the frozen pre-ordering baseline's
// at every depth, while the returned MOVE may legitimately differ among equal-scoring moves
// (fail-soft returns the first strict improvement in visit order, and ordering changes that
// order) - so the move is instead verified to provably achieve the score.
TEST(Ordering, ScoreMatchesBaselineAndMoveIsOptimalOnBenchmarkSet) {
    for (const Position& pos : bench::benchmarkPositions()) {
        for (int depth = 1; depth <= 5; ++depth) {
            const baseline::BaselineResult expected = baseline::search(pos, depth);
            const SearchResult ordered = search(pos, depth);
            EXPECT_EQ(ordered.score, expected.score) << "depth " << depth;
            EXPECT_EQ(checks::rootMoveValue(pos, ordered.bestMove, depth), ordered.score)
                << "depth " << depth;
        }
    }
}

// Same contract with the full stack stacked together: TT + ordering + PVS via iterative
// deepening, where TT hints from shallower iterations drive deeper iterations' ordering.
TEST(Ordering, FullStackScoreMatchesBaselineAndMoveIsOptimalOnBenchmarkSet) {
    for (const Position& pos : bench::benchmarkPositions()) {
        const int depth = 6;
        const baseline::BaselineResult expected = baseline::search(pos, depth);
        TranspositionTable tt(std::size_t{1} << 18);
        const SearchResult full =
            searchIterative(pos, depth, evaluateDiscDifferential, nullptr, &tt);
        EXPECT_EQ(full.score, expected.score);
        EXPECT_EQ(checks::rootMoveValue(pos, full.bestMove, depth), full.score);
    }
}

// The other half of the step's exit condition: the optimizations must actually prune.
// Deterministic (no RNG anywhere), so the thresholds below guard stable measurements, not
// statistics. Measured on this benchmark set at depth 7 when step 3 landed:
//   baseline (plain alpha-beta):        1,562,556 nodes total
//   ordering + PVS, no TT:                712,940 (45.6% of baseline)
//   ordering + PVS + TT (fixed depth):    530,629 (34.0% of baseline)
// The asserted bounds sit well above those measurements so benign tweaks (e.g. adjusting
// the static bias table) don't trip them, while a real ordering/PVS regression - which
// costs multiples, not percent - still would. (The pruning advantage compounds with depth;
// depth 7 is what fits a routine ctest run.)
TEST(Ordering, OrderingAndPvsPruneMeasurablyVsBaselineAtDepth7) {
    std::uint64_t baselineNodes = 0;
    std::uint64_t orderedNodes = 0;
    std::uint64_t fullStackNodes = 0;
    for (const Position& pos : bench::benchmarkPositions()) {
        baselineNodes += baseline::search(pos, 7).nodes;
        orderedNodes += search(pos, 7).nodes;
        TranspositionTable tt(std::size_t{1} << 20);
        fullStackNodes += search(pos, 7, evaluateDiscDifferential, nullptr, &tt).nodes;
    }
    EXPECT_LT(orderedNodes, baselineNodes * 60 / 100);
    EXPECT_LT(fullStackNodes, baselineNodes * 50 / 100);
    EXPECT_LE(fullStackNodes, orderedNodes); // the TT must not cost nodes on top of ordering
}

} // namespace
} // namespace reversi
