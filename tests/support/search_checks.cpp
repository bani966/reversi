#include "search_checks.hpp"

#include "baseline_search.hpp"

#include "reversi/eval.hpp"
#include "reversi/moves.hpp"

namespace reversi::checks {

// Mirrors what negamax(child, depth - 1) computes for the root's child, then negates it into
// the root mover's perspective. The pass and terminal cases must replicate search's exact
// conventions or the equality checks built on this would produce false alarms.
int rootMoveValue(const Position& p, int square, int depth) {
    const Position child = applyMove(p, square);
    if (!hasLegalMove(child)) {
        const Position passed = applyPass(child);
        if (!hasLegalMove(passed)) {
            return -terminalScore(child); // game over immediately after the move
        }
        if (depth - 1 == 0) {
            return -evaluateDiscDifferential(child);
        }
        // The child is forced to pass (no depth consumed): two perspective flips cancel out.
        return baseline::search(passed, depth - 1).score;
    }
    if (depth - 1 == 0) {
        return -evaluateDiscDifferential(child);
    }
    return -baseline::search(child, depth - 1).score;
}

} // namespace reversi::checks
