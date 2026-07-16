#include "reversi/analysis.hpp"

#include "../support/benchmark_positions.hpp"
#include "reversi/moves.hpp"
#include "reversi/tt.hpp"

#include <bit>
#include <chrono>
#include <cstddef>
#include <gtest/gtest.h>
#include <vector>

namespace reversi {
namespace {

constexpr TimeBudget kQuickBudget{std::chrono::milliseconds{50}, std::chrono::milliseconds{200}};

TEST(AnalyzeTopMoves, ScoresAreNonIncreasingAcrossRanks) {
    for (const Position& pos : bench::benchmarkPositions()) {
        TranspositionTable tt(std::size_t{1} << 12);
        const std::vector<RankedMove> lines =
            analyzeTopMoves(pos, 3, 4, kQuickBudget, evaluateDiscDifferential, nullptr, &tt);
        for (std::size_t i = 1; i < lines.size(); ++i) {
            EXPECT_GE(lines[i - 1].score, lines[i].score);
        }
    }
}

// Regression test for a real defect caught by manual GUI testing (see analysis.hpp's own doc
// comment): an earlier version time-budgeted every pass independently, so excluding moves (which
// shrinks the root's branching factor) let a later pass's iterative deepening reach a DEEPER
// completed depth than pass 0 in the same wall-clock budget - producing scores that looked
// "ranked" but were actually incomparable across different search depths. Every rank must search
// to the exact same depth for the ranking to mean anything.
TEST(AnalyzeTopMoves, AllRanksSearchToTheSameDepth) {
    for (const Position& pos : bench::benchmarkPositions()) {
        TranspositionTable tt(std::size_t{1} << 12);
        const std::vector<RankedMove> lines =
            analyzeTopMoves(pos, 3, 4, kQuickBudget, evaluateDiscDifferential, nullptr, &tt);
        for (std::size_t i = 1; i < lines.size(); ++i) {
            EXPECT_EQ(lines[i].depth, lines[0].depth);
        }
    }
}

TEST(AnalyzeTopMoves, NoDuplicateMovesAcrossRanks) {
    for (const Position& pos : bench::benchmarkPositions()) {
        TranspositionTable tt(std::size_t{1} << 12);
        const std::vector<RankedMove> lines =
            analyzeTopMoves(pos, 3, 4, kQuickBudget, evaluateDiscDifferential, nullptr, &tt);
        for (std::size_t i = 0; i < lines.size(); ++i) {
            for (std::size_t j = i + 1; j < lines.size(); ++j) {
                EXPECT_NE(lines[i].move, lines[j].move);
            }
        }
    }
}

TEST(AnalyzeTopMoves, MaxLinesGreaterThanLegalMoveCountReturnsExactlyLegalMoveCount) {
    // The start position has exactly 4 legal moves - request far more than that.
    TranspositionTable tt(std::size_t{1} << 12);
    const std::vector<RankedMove> lines = analyzeTopMoves(
        Position::start(), 20, 4, kQuickBudget, evaluateDiscDifferential, nullptr, &tt);
    EXPECT_EQ(lines.size(),
             static_cast<std::size_t>(std::popcount(legalMoves(Position::start()))));
}

TEST(ExtractPrincipalVariation, EveryMoveIsLegalInTheReplayedPosition) {
    for (const Position& pos : bench::benchmarkPositions()) {
        TranspositionTable tt(std::size_t{1} << 14);
        const SearchResult result =
            searchTimed(pos, 6, kQuickBudget, evaluateDiscDifferential, nullptr, &tt);
        ASSERT_TRUE(result.completed);
        ASSERT_NE(result.bestMove, -1);
        const std::vector<int> pv = extractPrincipalVariation(pos, result.bestMove, tt, 10);
        ASSERT_FALSE(pv.empty());
        ASSERT_EQ(pv.front(), result.bestMove);

        Position replay = pos;
        for (int move : pv) {
            while (!isGameOver(replay) && !hasLegalMove(replay)) {
                replay = applyPass(replay);
            }
            ASSERT_FALSE(isGameOver(replay));
            ASSERT_TRUE((legalMoves(replay) & bit(move)) != 0);
            replay = applyMove(replay, move);
        }
    }
}

TEST(ExtractPrincipalVariation, EmptyTableReturnsJustFirstMove) {
    TranspositionTable tt(std::size_t{1} << 10); // fresh, never probed/stored into
    const Position pos = Position::start();
    const Bitboard moves = legalMoves(pos);
    const int firstMove = std::countr_zero(moves);
    const std::vector<int> pv = extractPrincipalVariation(pos, firstMove, tt, 10);
    ASSERT_EQ(pv.size(), 1u);
    EXPECT_EQ(pv.front(), firstMove);
}

} // namespace
} // namespace reversi
