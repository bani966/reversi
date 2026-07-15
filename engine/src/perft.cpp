#include "reversi/perft.hpp"

#include "reversi/moves.hpp"

#include <bit>

namespace reversi {

std::uint64_t perft(const Position& p, int depth) {
    if (depth == 0) {
        return 1;
    }
    const Bitboard moves = legalMoves(p);
    if (moves != 0) {
        std::uint64_t nodes = 0;
        for (Bitboard b = moves; b != 0; b &= b - 1) {
            nodes += perft(applyMove(p, std::countr_zero(b)), depth - 1);
        }
        return nodes;
    }
    const Position passed = applyPass(p);
    if (hasLegalMove(passed)) {
        return perft(passed, depth - 1);
    }
    return 1; // game over: terminal leaf regardless of remaining depth
}

} // namespace reversi
