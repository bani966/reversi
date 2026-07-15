#pragma once

#include "reversi/position.hpp"

#include <functional>

namespace reversi {

// A heuristic score for `p` from the mover's (own's) perspective; higher is better for own.
// std::function rather than a raw function pointer so a future eval (e.g. M6's WTHOR-trained
// pattern table) can close over loaded weight data without search ever changing.
using EvalFn = std::function<int(const Position&)>;

// Disc-count differential from the mover's perspective. Placeholder until M6's WTHOR-trained
// pattern evaluation; search only depends on the EvalFn signature, never this body.
int evaluateDiscDifferential(const Position& p);

// Exact final-game score for a position where isGameOver(p) is true: the actual Othello
// result (whoever has more discs wins), independent of whichever heuristic EvalFn is plugged
// into search. Also the scoring function the endgame solver (M5) will search to.
int terminalScore(const Position& p);

} // namespace reversi
