#include "reversi/moves.hpp"

#include <gtest/gtest.h>

namespace reversi {
namespace {

Bitboard squareBit(std::string_view s) {
    return bit(*squareFromString(s));
}

TEST(Moves, StartPositionHasFourClassicMoves) {
    const Bitboard expected = squareBit("c4") | squareBit("d3") | squareBit("e6") | squareBit("f5");
    EXPECT_EQ(legalMoves(Position::start()), expected);
    EXPECT_TRUE(hasLegalMove(Position::start()));
}

TEST(Moves, ApplyMoveFlipsBracketedDiscsAndSwapsSides) {
    const Position after = applyMove(Position::start(), *squareFromString("d3"));
    // Relative to the next mover (white): white owns only e5, black (opp) owns the rest.
    EXPECT_EQ(after.own, squareBit("e5"));
    EXPECT_EQ(after.opp, squareBit("d3") | squareBit("d4") | squareBit("d5") | squareBit("e4"));
}

TEST(Moves, ApplyPassOnlySwapsSides) {
    const Position p = Position::start();
    const Position passed = applyPass(p);
    EXPECT_EQ(passed.own, p.opp);
    EXPECT_EQ(passed.opp, p.own);
}

TEST(Moves, IsGameOverFalseAtStart) {
    EXPECT_FALSE(isGameOver(Position::start()));
}

TEST(Moves, IsGameOverTrueOnFullBoard) {
    Position p;
    for (int sq = 0; sq < 32; ++sq) {
        p.own |= bit(sq);
    }
    for (int sq = 32; sq < 64; ++sq) {
        p.opp |= bit(sq);
    }
    EXPECT_TRUE(isGameOver(p));
}

// --- Edge-wraparound regressions -------------------------------------------------------
//
// Move generation shifts the whole board one step per direction; shifts with an east/west
// component must mask out the file they would otherwise wrap onto (e.g. shifting a file-h
// disc east lands on file a of the next rank, which is not a real adjacency). Each case
// below places an own disc plus a 3-disc opponent run marching all the way to the board
// edge in one direction, so the run has nowhere legal to land — if masking were missing or
// wrong, the shift would instead wrap onto a real square on the board and fabricate a move.

Position chainTowardEdge(std::string_view ownSquare, int df, int dr) {
    const int ownSq = *squareFromString(ownSquare);
    const int ownFile = ownSq % 8;
    const int ownRank = ownSq / 8;
    Position p;
    p.own = bit(ownSq);
    for (int step = 1; step <= 3; ++step) {
        p.opp |= bit(squareIndex(ownFile + df * step, ownRank + dr * step));
    }
    return p;
}

TEST(Moves, EastRunDoesNotWrapPastFileH) {
    EXPECT_EQ(legalMoves(chainTowardEdge("e4", +1, 0)), Bitboard{0});
}

TEST(Moves, WestRunDoesNotWrapPastFileA) {
    EXPECT_EQ(legalMoves(chainTowardEdge("d4", -1, 0)), Bitboard{0});
}

TEST(Moves, NorthEastRunDoesNotWrapPastFileH) {
    EXPECT_EQ(legalMoves(chainTowardEdge("e5", +1, -1)), Bitboard{0});
}

TEST(Moves, NorthWestRunDoesNotWrapPastFileA) {
    EXPECT_EQ(legalMoves(chainTowardEdge("d5", -1, -1)), Bitboard{0});
}

TEST(Moves, SouthEastRunDoesNotWrapPastFileH) {
    EXPECT_EQ(legalMoves(chainTowardEdge("e4", +1, +1)), Bitboard{0});
}

TEST(Moves, SouthWestRunDoesNotWrapPastFileA) {
    EXPECT_EQ(legalMoves(chainTowardEdge("d4", -1, +1)), Bitboard{0});
}

TEST(Moves, EastLandsOnEdgeFileWhenNotWrapping) {
    // own e4, opp f4/g4, empty h4: a legitimate capture landing on the edge column itself,
    // to confirm masking doesn't also suppress real edge-column landings.
    Position p;
    p.own = squareBit("e4");
    p.opp = squareBit("f4") | squareBit("g4");
    EXPECT_EQ(legalMoves(p), squareBit("h4"));
}

TEST(Moves, WestLandsOnEdgeFileWhenNotWrapping) {
    Position p;
    p.own = squareBit("d4");
    p.opp = squareBit("c4") | squareBit("b4");
    EXPECT_EQ(legalMoves(p), squareBit("a4"));
}

} // namespace
} // namespace reversi
