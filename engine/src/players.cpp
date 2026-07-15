#include "reversi/players.hpp"

#include "reversi/moves.hpp"

#include <bit>
#include <vector>

namespace reversi {

int pickRandomMove(const Position& p, std::mt19937& rng) {
    std::vector<int> squares;
    for (Bitboard b = legalMoves(p); b != 0; b &= b - 1) {
        squares.push_back(std::countr_zero(b));
    }
    std::uniform_int_distribution<std::size_t> pick(0, squares.size() - 1);
    return squares[pick(rng)];
}

int pickGreedyMove(const Position& p) {
    int bestSquare = -1;
    int bestOwnCountAfter = -1;
    for (Bitboard b = legalMoves(p); b != 0; b &= b - 1) {
        const int square = std::countr_zero(b);
        // applyMove returns the position swapped for the next mover, so the current mover's
        // new disc count (after placing + flips) is on .opp, not .own.
        const int ownCountAfter = applyMove(p, square).oppCount();
        if (ownCountAfter > bestOwnCountAfter) {
            bestOwnCountAfter = ownCountAfter;
            bestSquare = square;
        }
    }
    return bestSquare;
}

} // namespace reversi
