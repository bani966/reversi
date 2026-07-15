#pragma once

#include "reversi/position.hpp"

#include <random>

namespace reversi {

// Uniformly random legal move, via the caller-owned RNG (no hidden global state, so callers
// stay reproducible with a fixed seed). Precondition: hasLegalMove(p).
int pickRandomMove(const Position& p, std::mt19937& rng);

// The legal move maximizing the mover's own disc count immediately after playing it (i.e.
// maximizing flips; no look-ahead). Ties broken by lowest square index, so this is fully
// deterministic. Precondition: hasLegalMove(p).
int pickGreedyMove(const Position& p);

} // namespace reversi
