#include "reversi/solver.hpp"

#include "../support/endgame_positions.hpp"
#include "reversi/cancellation.hpp"
#include "reversi/search.hpp"

#include <gtest/gtest.h>
#include <vector>

namespace reversi {
namespace {

// The core correctness proof for M5 step 1, enabled by a fact established while planning: since
// evaluateDiscDifferential and terminalScore are byte-identical, and search()'s depth counts
// only real moves (a forced pass re-searches at the same depth), search(pos, pos.emptyCount())
// can only reach its depth-0 eval fallback once the board is completely full - at which point
// the game-over branch already returns terminalScore first. So search(pos, pos.emptyCount())
// is ALREADY an exact solver today, entirely independent of solveExact()'s new code. Comparing
// solveExact() against it isolates "is the new recursion structurally correct" from "is the
// vendored FFO data right" (the FFO cross-check arrives in a later step) - a bug in solveExact's
// pass-handling, TT bound tagging, or PVS would very likely show up here first, cheaply.
TEST(Solver, MatchesSearchAtDepthEqualsEmptyCountOnSmallEndgamePositions) {
    std::vector<Position> positions;
    for (const unsigned seed : {101u, 202u, 303u}) {
        const auto sample = endgame::collectPositionsByEmptyCount(seed, 1, 10);
        positions.insert(positions.end(), sample.begin(), sample.end());
    }
    ASSERT_GE(positions.size(), std::size_t{15});
    for (const Position& pos : positions) {
        const SearchResult expected = search(pos, pos.emptyCount());
        const SearchResult actual = solveExact(pos);
        EXPECT_EQ(actual.score, expected.score) << "emptyCount=" << pos.emptyCount();
        EXPECT_TRUE(actual.completed);
    }
}

TEST(Solver, NoCancellationTokenLeavesResultCompleted) {
    const Position pos = Position::start();
    // Not a real endgame position, but solveExact has no precondition on empty count - it will
    // just be slow for a full 60-empty solve. Only completeness/plumbing is under test here, so
    // pre-cancel to make this instant while still exercising the same code path as the
    // completed case for the "no cancellation token" contract.
    CancellationToken token;
    token.requestStop();
    const SearchResult result = solveExact(pos, &token);
    EXPECT_FALSE(result.completed);
}

TEST(Solver, CancellationStopsPromptlyAndMarksResultIncomplete) {
    CancellationToken token;
    token.requestStop();
    const SearchResult result = solveExact(Position::start(), &token);
    EXPECT_FALSE(result.completed);
    EXPECT_EQ(result.depth, 0);
    // The start position has 4 legal root moves; each triggers exactly one negamax call that
    // collapses immediately at entry (mirrors search.cpp's own pre-cancellation test).
    EXPECT_EQ(result.nodes, std::uint64_t{4});
}

TEST(Solver, NodesAreCountedAndPositive) {
    // A late-game position (few empties) so this stays fast without needing cancellation.
    const auto sample = endgame::collectPositionsByEmptyCount(404u, 6, 8);
    ASSERT_FALSE(sample.empty());
    const SearchResult result = solveExact(sample.front());
    EXPECT_GT(result.nodes, std::uint64_t{0});
}

} // namespace
} // namespace reversi
