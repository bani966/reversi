#include "reversi/search.hpp"

#include "reversi/moves.hpp"

#include <bit>
#include <gtest/gtest.h>
#include <limits>
#include <vector>

namespace reversi {
namespace {

// Advances `p` by `plies`, always taking the lowest-indexed legal move (or passing when
// there is none), stopping early if the game ends. Deterministic and self-contained (no RNG,
// no dependency on the baseline players that arrive later in M2) — just a cheap way to reach
// a handful of real, reachable mid-game positions for the cross-check below.
Position advanceByLowestMove(Position p, int plies) {
    for (int i = 0; i < plies && !isGameOver(p); ++i) {
        const Bitboard moves = legalMoves(p);
        p = moves != 0 ? applyMove(p, std::countr_zero(moves)) : applyPass(p);
    }
    return p;
}

// Plain, unpruned negamax mirroring search.cpp's recursion exactly but always exploring every
// child (no alpha/beta at all) — a reference to cross-check that fail-soft pruning never
// changes the computed score, only how much of the tree it visits.
int fullWindowNegamax(const Position& pos, int depth, const EvalFn& eval) {
    const Bitboard moves = legalMoves(pos);
    if (moves == 0) {
        const Position passed = applyPass(pos);
        if (!hasLegalMove(passed)) {
            return terminalScore(pos);
        }
        if (depth == 0) {
            return eval(pos);
        }
        return -fullWindowNegamax(passed, depth, eval);
    }
    if (depth == 0) {
        return eval(pos);
    }
    int best = std::numeric_limits<int>::min();
    for (Bitboard b = moves; b != 0; b &= b - 1) {
        const int score = -fullWindowNegamax(applyMove(pos, std::countr_zero(b)), depth - 1, eval);
        if (score > best) {
            best = score;
        }
    }
    return best;
}

// A position with exactly two legal moves and a hand-verifiable winner: own has a single
// disc at e4, opp has discs at d4 (west) and f4/g4 (east). c4 brackets only d4 (1 flip); h4
// brackets f4 and g4 (2 flips). Depth-1 negamax's leaf score is exactly the post-move disc
// differential (negamax(child, 0) = eval(child), and score = -eval(child) = the mover's own
// count minus opp count right after the move), so this is provably equivalent to "greedy by
// flip count" at depth 1, not a coincidence — h4 must win.
TEST(Search, DepthOneMatchesTheStrictlyBetterMove) {
    Position p;
    p.own = bit(*squareFromString("e4"));
    p.opp =
        bit(*squareFromString("d4")) | bit(*squareFromString("f4")) | bit(*squareFromString("g4"));
    ASSERT_EQ(legalMoves(p), bit(*squareFromString("c4")) | bit(*squareFromString("h4")));

    const SearchResult result = search(p, 1);
    EXPECT_EQ(result.bestMove, *squareFromString("h4"));
    EXPECT_EQ(result.score, 3); // own=e4,h4,f4,g4 (4) vs opp=d4 (1) => differential 3
}

TEST(Search, FailSoftAlphaBetaScoreMatchesUnprunedNegamax) {
    const std::vector<Position> positions = {
        Position::start(),
        advanceByLowestMove(Position::start(), 2),
        advanceByLowestMove(Position::start(), 4),
        advanceByLowestMove(Position::start(), 6),
        advanceByLowestMove(Position::start(), 8),
    };
    for (const Position& pos : positions) {
        if (isGameOver(pos) || !hasLegalMove(pos)) {
            continue;
        }
        for (int depth = 1; depth <= 4; ++depth) {
            const SearchResult pruned = search(pos, depth);
            const int unpruned = fullWindowNegamax(pos, depth, evaluateDiscDifferential);
            EXPECT_EQ(pruned.score, unpruned) << "depth " << depth;
        }
    }
}

TEST(Search, NodesAreCountedAndPositive) {
    const SearchResult result = search(Position::start(), 3);
    EXPECT_GT(result.nodes, std::uint64_t{0});
}

} // namespace
} // namespace reversi
