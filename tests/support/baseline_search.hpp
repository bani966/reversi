#pragma once

#include "reversi/eval.hpp"
#include "reversi/position.hpp"

#include <cstdint>

namespace reversi::baseline {

struct BaselineResult {
    int bestMove = -1;
    int score = 0;
    std::uint64_t nodes = 0;
};

// Frozen copy of the M2-era engine: fixed-depth negamax with fail-soft alpha-beta, no
// transposition table, no move ordering (moves visited in ascending square order), no PVS.
// Deliberately kept as a living reference while reversi::search() matures through M4:
//   - M4 step 3+ correctness tests assert the matured search still computes this exact score,
//     and measure their node-count reductions against this implementation's counts;
//   - the M4 exit criterion plays the matured search against precisely this baseline, so the
//     measured strength gain isolates search improvements (same eval on both sides).
// Same conventions as reversi::search(): depth counts real moves only (a forced pass
// re-searches at the same depth), precondition hasLegalMove(p).
BaselineResult search(const Position& p, int depth, const EvalFn& eval = evaluateDiscDifferential);

} // namespace reversi::baseline
