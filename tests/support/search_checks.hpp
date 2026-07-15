#pragma once

#include "reversi/position.hpp"

namespace reversi::checks {

// The true depth-`depth` value of playing `square` from `p`, from the mover's perspective,
// mirroring search's conventions exactly (a forced pass consumes no depth). Computed with the
// frozen baseline search (tests/support/baseline_search.*), so it is independent of whichever
// search optimization is currently under test; the baseline's own scores are anchored by the
// unpruned-negamax cross-check in search_test.cpp.
//
// Why this exists: move ordering (M4 step 3) legitimately changes which of several
// EQUAL-scoring root moves fail-soft alpha-beta returns, so post-step-3 correctness tests
// can't assert strict best-move equality against a differently-ordered search. The honest
// check is: identical score, plus the returned move provably achieving that score - which is
// exactly rootMoveValue(p, result.bestMove, depth) == result.score.
// Precondition: `square` is a legal move in `p`, depth >= 1.
int rootMoveValue(const Position& p, int square, int depth);

} // namespace reversi::checks
