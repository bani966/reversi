#pragma once

#include "reversi/position.hpp"

#include <vector>

namespace reversi::bench {

// The fixed benchmark position set shared by every M4 search-correctness check (iterative
// deepening, transposition table on/off, move ordering/PVS, aspiration windows). Decided once,
// up front, so each optimization step is validated against the same positions rather than an
// ad-hoc per-step selection whose coverage could quietly drift.
//
// Contents (deterministically generated, no hardcoded bitboard blobs):
//   - the start position,
//   - fixed-seed random playouts sampled every 8 plies (ply 8..48), covering opening, midgame
//     branching-factor peaks, and low-empties endgame,
//   - from each playout, the first position reached via a forced pass, so "a pass occurs
//     inside the search horizon" is always represented.
// Every returned position satisfies search()'s precondition (hasLegalMove, not game over).
const std::vector<Position>& benchmarkPositions();

} // namespace reversi::bench
