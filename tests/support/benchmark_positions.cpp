#include "benchmark_positions.hpp"

#include "reversi/moves.hpp"
#include "reversi/players.hpp"

#include <random>

namespace reversi::bench {
namespace {

// Plays one fixed-seed random game to the end, collecting the sample positions described in
// the header. Sampled positions that happen to violate search()'s precondition (mover has no
// legal move) are skipped rather than passed through - the set's contract is "search can be
// called on every element", and pass coverage is provided separately by the post-pass sample.
void collectFromPlayout(unsigned seed, std::vector<Position>& out) {
    std::mt19937 rng(seed);
    Position p = Position::start();
    int plies = 0;
    bool passSampleTaken = false;
    while (!isGameOver(p)) {
        if (!hasLegalMove(p)) {
            p = applyPass(p);
            // Not game over and the previous mover had no move, so the new mover must have
            // one - the precondition holds by construction here.
            if (!passSampleTaken && !isGameOver(p)) {
                out.push_back(p);
                passSampleTaken = true;
            }
            continue;
        }
        p = applyMove(p, pickRandomMove(p, rng));
        ++plies;
        if (plies % 8 == 0 && plies <= 48 && !isGameOver(p) && hasLegalMove(p)) {
            out.push_back(p);
        }
    }
}

std::vector<Position> generate() {
    std::vector<Position> positions;
    positions.push_back(Position::start());
    for (const unsigned seed : {11u, 22u, 33u}) {
        collectFromPlayout(seed, positions);
    }
    return positions;
}

} // namespace

const std::vector<Position>& benchmarkPositions() {
    static const std::vector<Position> kPositions = generate();
    return kPositions;
}

} // namespace reversi::bench
