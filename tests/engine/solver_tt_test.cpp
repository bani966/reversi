#include "reversi/solver.hpp"

#include "../support/endgame_positions.hpp"

#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

namespace reversi {
namespace {

// Mirrors tt_test.cpp's TTSearch group for reversi::search(): the single most important
// property of any transposition table is that it must never change the answer, only how fast
// the answer is reached. Unlike search()'s TT usage, solveExact's stored entries need no depth
// gating to be trustworthy (every entry already reflects a full solve of its exact position -
// see solver.hpp's doc comment) - so the "TT on vs off" comparison here is testing that the
// bound-tagging/replacement logic is wired correctly, not a depth-validity subtlety.
TEST(SolverTT, SolveWithTTMatchesSolveWithoutOnSmallEndgamePositions) {
    std::vector<Position> positions;
    for (const unsigned seed : {55u, 66u, 77u}) {
        const auto sample = endgame::collectPositionsByEmptyCount(seed, 4, 12);
        positions.insert(positions.end(), sample.begin(), sample.end());
    }
    ASSERT_GE(positions.size(), std::size_t{10});
    for (const Position& pos : positions) {
        const SearchResult baseline = solveExact(pos);
        TranspositionTable tt(std::size_t{1} << 18);
        const SearchResult withTT = solveExact(pos, nullptr, &tt);
        EXPECT_EQ(withTT.score, baseline.score) << "emptyCount=" << pos.emptyCount();
    }
}

// The real intended usage pattern: ONE table solving many different positions in sequence
// (e.g. a whole FFO test run, or repeated GUI hint requests within a game), never cleared
// between them. Safe because Zobrist hashing is purely position-based (same argument
// tt_test.cpp and the M4 exit criterion's wall-clock test both rely on) - a stored entry for a
// given board remains valid no matter which earlier solve encountered it.
TEST(SolverTT, OneSharedTableAcrossManyPositionsStillMatchesWithoutTT) {
    const auto positions = endgame::collectPositionsByEmptyCount(88u, 3, 10);
    ASSERT_GE(positions.size(), std::size_t{5});
    TranspositionTable tt(std::size_t{1} << 18);
    for (const Position& pos : positions) {
        const SearchResult baseline = solveExact(pos);
        const SearchResult withTT = solveExact(pos, nullptr, &tt); // same table, not cleared
        EXPECT_EQ(withTT.score, baseline.score) << "emptyCount=" << pos.emptyCount();
    }
}

// Guards against the table being silently bypassed (which would pass the equality tests above
// while making it decorative): the solver must actually hit it, and in aggregate the table must
// remove work, not merely fail to add any.
TEST(SolverTT, TableIsConsultedAndRemovesWorkInAggregate) {
    const auto positions = endgame::collectPositionsByEmptyCount(99u, 8, 12);
    ASSERT_GE(positions.size(), std::size_t{5});
    std::uint64_t nodesWithout = 0;
    std::uint64_t nodesWith = 0;
    std::uint64_t totalHits = 0;
    for (const Position& pos : positions) {
        nodesWithout += solveExact(pos).nodes;
        TranspositionTable tt(std::size_t{1} << 18);
        nodesWith += solveExact(pos, nullptr, &tt).nodes;
        totalHits += tt.hits();
    }
    EXPECT_LT(nodesWith, nodesWithout);
    EXPECT_GT(totalHits, std::uint64_t{0});
}

} // namespace
} // namespace reversi
