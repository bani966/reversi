#include "../support/naive_reference.hpp"
#include "reversi/position.hpp"

#include <algorithm>
#include <gtest/gtest.h>

namespace reversi::naive {
namespace {

std::vector<int> sorted(std::vector<int> v) {
    std::sort(v.begin(), v.end());
    return v;
}

TEST(NaiveReference, StartPositionHasFourClassicMoves) {
    const Board b = start();
    std::vector<int> expected = {
        *squareFromString("c4"),
        *squareFromString("d3"),
        *squareFromString("e6"),
        *squareFromString("f5"),
    };
    EXPECT_EQ(sorted(legalMoves(b)), sorted(expected));
    EXPECT_TRUE(hasLegalMove(b));
}

TEST(NaiveReference, ApplyMoveFlipsBracketedDiscsAndSwapsSides) {
    const Board b = start();
    const Board after = applyMove(b, *squareFromString("d3"));

    // Relative to the next mover (white): white owns only e5, black (opp) owns the rest.
    const auto file = [](std::string_view s) { return static_cast<int>(s[0] - 'a'); };
    const auto rank = [](std::string_view s) { return static_cast<int>(s[1] - '1'); };

    EXPECT_EQ(after.cells[rank("e5")][file("e5")], Cell::Own);
    EXPECT_EQ(after.cells[rank("d3")][file("d3")], Cell::Opp);
    EXPECT_EQ(after.cells[rank("d4")][file("d4")], Cell::Opp);
    EXPECT_EQ(after.cells[rank("d5")][file("d5")], Cell::Opp);
    EXPECT_EQ(after.cells[rank("e4")][file("e4")], Cell::Opp);

    int ownCount = 0;
    int oppCount = 0;
    for (const auto& row : after.cells) {
        for (const Cell c : row) {
            ownCount += c == Cell::Own ? 1 : 0;
            oppCount += c == Cell::Opp ? 1 : 0;
        }
    }
    EXPECT_EQ(ownCount, 1);
    EXPECT_EQ(oppCount, 4);
}

TEST(NaiveReference, PositionRoundTripsThroughBoard) {
    const Position p = Position::start();
    EXPECT_EQ(toPosition(fromPosition(p)).own, p.own);
    EXPECT_EQ(toPosition(fromPosition(p)).opp, p.opp);
}

TEST(NaiveReference, NoOpponentDiscsMeansNoLegalMove) {
    Board b;
    for (auto& row : b.cells) {
        row.fill(Cell::Own);
    }
    b.cells[0][0] = Cell::Empty;
    EXPECT_FALSE(hasLegalMove(b));
    EXPECT_TRUE(legalMoves(b).empty());
    EXPECT_TRUE(isGameOver(b));
}

} // namespace
} // namespace reversi::naive
