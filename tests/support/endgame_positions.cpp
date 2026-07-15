#include "endgame_positions.hpp"

#include "reversi/moves.hpp"
#include "reversi/players.hpp"

#include <random>

namespace reversi::endgame {

std::vector<Position> collectPositionsByEmptyCount(unsigned seed, int minEmpty, int maxEmpty) {
    std::vector<Position> positions;
    std::mt19937 rng(seed);
    Position p = Position::start();
    while (!isGameOver(p)) {
        if (!hasLegalMove(p)) {
            p = applyPass(p);
            continue;
        }
        if (p.emptyCount() >= minEmpty && p.emptyCount() <= maxEmpty) {
            positions.push_back(p);
        }
        p = applyMove(p, pickRandomMove(p, rng));
    }
    return positions;
}

} // namespace reversi::endgame
