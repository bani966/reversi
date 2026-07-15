#include "reversi/players.hpp"

#include "reversi/moves.hpp"

#include <gtest/gtest.h>
#include <random>

namespace reversi {
namespace {

// Same constructed position as Search.DepthOneMatchesTheStrictlyBetterMove: own=e4,
// opp=d4/f4/g4. c4 brackets only d4 (1 flip); h4 brackets f4 and g4 (2 flips), so h4 is the
// unique greedy choice.
Position twoMoveFixture() {
    Position p;
    p.own = bit(*squareFromString("e4"));
    p.opp =
        bit(*squareFromString("d4")) | bit(*squareFromString("f4")) | bit(*squareFromString("g4"));
    return p;
}

TEST(Players, GreedyPicksTheMaxFlipMoveDeterministically) {
    const Position p = twoMoveFixture();
    EXPECT_EQ(pickGreedyMove(p), *squareFromString("h4"));
    EXPECT_EQ(pickGreedyMove(p), *squareFromString("h4")); // deterministic: no hidden state
}

TEST(Players, RandomAlwaysReturnsALegalMove) {
    std::mt19937 rng(42);
    const Position start = Position::start();
    for (int i = 0; i < 500; ++i) {
        const int square = pickRandomMove(start, rng);
        EXPECT_NE(legalMoves(start) & bit(square), Bitboard{0});
    }
}

} // namespace
} // namespace reversi
