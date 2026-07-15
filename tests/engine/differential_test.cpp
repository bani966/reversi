#include "../support/naive_reference.hpp"
#include "reversi/moves.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <gtest/gtest.h>
#include <random>
#include <vector>

namespace reversi {
namespace {

std::vector<int> bitboardMoveList(Bitboard moves) {
    std::vector<int> squares;
    for (Bitboard b = moves; b != 0; b &= b - 1) {
        squares.push_back(std::countr_zero(b));
    }
    return squares;
}

std::vector<int> sorted(std::vector<int> v) {
    std::sort(v.begin(), v.end());
    return v;
}

// Plays one randomly-chosen game to completion, keeping a bitboard Position and a naive
// Board in lockstep. At every single ply it cross-checks legal-move sets, game-over status,
// and the resulting position between the two independent implementations.
void playAndCompareOneGame(std::mt19937& rng) {
    Position pos = Position::start();
    naive::Board board = naive::start();
    constexpr int kMaxPlies = 200; // real games terminate well under this; guards against a
                                    // hang if a bug makes isGameOver never agree.

    for (int ply = 0; ply < kMaxPlies; ++ply) {
        ASSERT_EQ(naive::toPosition(board), pos) << "position drift at ply " << ply;
        ASSERT_EQ(isGameOver(pos), naive::isGameOver(board)) << "isGameOver disagreement at ply " << ply;
        if (isGameOver(pos)) {
            return;
        }

        const std::vector<int> bitboardMoves = sorted(bitboardMoveList(legalMoves(pos)));
        const std::vector<int> naiveMoves = sorted(naive::legalMoves(board));
        ASSERT_EQ(bitboardMoves, naiveMoves) << "legal move set disagreement at ply " << ply;

        if (bitboardMoves.empty()) {
            // Forced pass: neither implementation has a move, but the opponent might.
            pos = applyPass(pos);
            board = naive::applyPass(board);
            continue;
        }

        std::uniform_int_distribution<std::size_t> pick(0, bitboardMoves.size() - 1);
        const int square = bitboardMoves[pick(rng)];
        pos = applyMove(pos, square);
        board = naive::applyMove(board, square);
    }
    FAIL() << "game exceeded kMaxPlies without reaching isGameOver";
}

TEST(Differential, RandomSelfPlayMatchesNaiveReferenceEveryPly) {
    std::mt19937 rng(20260715); // fixed seed: deterministic, reproducible failures.
    constexpr int kGames = 3000;
    for (int game = 0; game < kGames; ++game) {
        playAndCompareOneGame(rng);
    }
}

} // namespace
} // namespace reversi
