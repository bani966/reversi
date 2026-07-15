#pragma once

#include "reversi/position.hpp"

#include <vector>

namespace reversi::endgame {

// Plays one fixed-seed random game to completion, collecting every position reached with
// `minEmpty`..`maxEmpty` empty squares (inclusive) where the mover has a legal move (matching
// search()/solveExact()'s shared precondition hasLegalMove(p) - positions where the mover is
// forced to pass are skipped, not passed through). Used by the M5 solver test suite
// (solver_test.cpp, solver_ordering_test.cpp, solver_tt_test.cpp) to sample small-empty
// positions on demand, since tests/support/benchmark_positions.hpp's fixed ply-based sampling
// isn't guaranteed to land in any particular empty-count range.
std::vector<Position> collectPositionsByEmptyCount(unsigned seed, int minEmpty, int maxEmpty);

} // namespace reversi::endgame
