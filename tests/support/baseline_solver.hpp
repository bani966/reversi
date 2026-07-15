#pragma once

#include "baseline_search.hpp"

#include "reversi/position.hpp"
#include "reversi/tt.hpp"

namespace reversi::baseline {

// Frozen copy of solveExact() as it existed at M5 step 1: fail-soft alpha-beta + PVS, TT
// support, plain ascending-square-index move order (no fastest-first, no parity). Same purpose
// as baseline::search() above for M4: a living reference so step 2's ordering work can measure
// its node-count reduction and prove the answer is unchanged, without needing to keep two live
// copies of reversi::solveExact() in sync by hand.
// Same conventions as reversi::solveExact(): no EvalFn (every leaf is the exact final disc
// differential), precondition hasLegalMove(p). `tt`, if non-null, must not be shared with any
// other solver/search call (see reversi::solveExact's doc comment for why).
BaselineResult solveExact(const Position& p, TranspositionTable* tt = nullptr);

} // namespace reversi::baseline
