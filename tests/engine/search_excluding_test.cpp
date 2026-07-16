#include "reversi/search.hpp"

#include "../support/benchmark_positions.hpp"
#include "reversi/moves.hpp"

#include <bit>
#include <gtest/gtest.h>
#include <vector>

namespace reversi {
namespace {

TEST(SearchExcludingMoves, EmptyExclusionListMatchesPlainSearch) {
    for (const Position& pos : bench::benchmarkPositions()) {
        const SearchResult plain = search(pos, 4);
        const SearchResult excluding = searchExcludingMoves(pos, 4, {});
        EXPECT_EQ(excluding.score, plain.score);
        EXPECT_EQ(excluding.bestMove, plain.bestMove);
    }
}

TEST(SearchExcludingMoves, ExcludingTheBestMoveReturnsADifferentLegalMove) {
    for (const Position& pos : bench::benchmarkPositions()) {
        const Bitboard moves = legalMoves(pos);
        if (std::popcount(moves) < 2) {
            continue; // nothing else to find if only one legal move exists
        }
        const SearchResult first = search(pos, 4);
        const SearchResult second = searchExcludingMoves(pos, 4, {first.bestMove});
        EXPECT_NE(second.bestMove, first.bestMove);
        EXPECT_NE(second.bestMove, -1);
        EXPECT_TRUE((moves & bit(second.bestMove)) != 0);
    }
}

TEST(SearchExcludingMoves, ExcludingAllButOneLegalMoveReturnsExactlyThatMove) {
    for (const Position& pos : bench::benchmarkPositions()) {
        const Bitboard moves = legalMoves(pos);
        if (std::popcount(moves) < 2) {
            continue;
        }
        // Exclude every legal move except the lowest-indexed one.
        const int survivor = std::countr_zero(moves);
        std::vector<int> excluded;
        for (Bitboard b = moves & (moves - 1); b != 0; b &= b - 1) {
            excluded.push_back(std::countr_zero(b));
        }
        const SearchResult result = searchExcludingMoves(pos, 4, excluded);
        EXPECT_EQ(result.bestMove, survivor);
    }
}

TEST(SearchExcludingMoves, ExcludedSquaresNeverAppearAsBestMoveAcrossBenchmarkSet) {
    for (const Position& pos : bench::benchmarkPositions()) {
        const Bitboard moves = legalMoves(pos);
        if (std::popcount(moves) < 2) {
            continue;
        }
        const int firstExcluded = std::countr_zero(moves);
        const SearchResult result = searchExcludingMoves(pos, 4, {firstExcluded});
        EXPECT_NE(result.bestMove, firstExcluded);
    }
}

TEST(SearchTimedExcludingMoves, EmptyExclusionListMatchesPlainSearchAtEqualDepth) {
    // A generous budget so the comparison isn't muddied by wall-clock timing - both sides should
    // comfortably finish every iteration up to maxDepth.
    const TimeBudget budget{std::chrono::milliseconds{2000}, std::chrono::milliseconds{5000}};
    const Position pos = bench::benchmarkPositions()[0];
    const SearchResult plain = searchIterative(pos, 4);
    const SearchResult excluding = searchTimedExcludingMoves(pos, 4, budget, {});
    EXPECT_EQ(excluding.score, plain.score);
    EXPECT_EQ(excluding.bestMove, plain.bestMove);
    EXPECT_EQ(excluding.depth, plain.depth);
}

} // namespace
} // namespace reversi
