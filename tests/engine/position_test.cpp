#include "reversi/position.hpp"

#include <gtest/gtest.h>

namespace reversi {
namespace {

TEST(Position, StartHasTwoDiscsPerSide) {
    const Position p = Position::start();
    EXPECT_EQ(p.ownCount(), 2);
    EXPECT_EQ(p.oppCount(), 2);
    EXPECT_EQ(p.discCount(), 4);
}

TEST(Position, StartSidesDoNotOverlap) {
    const Position p = Position::start();
    EXPECT_EQ(p.own & p.opp, Bitboard{0});
    EXPECT_EQ(std::popcount(p.empty()), kBoardSquares - 4);
}

TEST(Position, StartSquaresAreTheCenterFour) {
    const Position p = Position::start();
    // Black (side to move) on d5 and e4; white on d4 and e5.
    EXPECT_EQ(p.own, bit(*squareFromString("d5")) | bit(*squareFromString("e4")));
    EXPECT_EQ(p.opp, bit(*squareFromString("d4")) | bit(*squareFromString("e5")));
}

TEST(Notation, RoundTripsAllSquares) {
    for (int sq = 0; sq < kBoardSquares; ++sq) {
        const std::string s = squareToString(sq);
        const auto back = squareFromString(s);
        ASSERT_TRUE(back.has_value()) << s;
        EXPECT_EQ(*back, sq);
    }
}

TEST(Notation, KnownSquares) {
    EXPECT_EQ(squareToString(0), "a1");
    EXPECT_EQ(squareToString(63), "h8");
    EXPECT_EQ(squareFromString("f5"), squareIndex(5, 4));
    EXPECT_EQ(squareFromString("F5"), squareIndex(5, 4));
}

TEST(Notation, RejectsInvalidInput) {
    EXPECT_FALSE(squareFromString("").has_value());
    EXPECT_FALSE(squareFromString("i1").has_value());
    EXPECT_FALSE(squareFromString("a9").has_value());
    EXPECT_FALSE(squareFromString("a0").has_value());
    EXPECT_FALSE(squareFromString("f55").has_value());
}

} // namespace
} // namespace reversi
