#pragma once

#include "reversi/cancellation.hpp"
#include "reversi/position.hpp"
#include "reversi/search.hpp"
#include "reversi/tt.hpp"

namespace reversi {

// Empty-square count at or below which the exact endgame solver (solveExact) should be used
// instead of the heuristic, depth-limited search. Deliberately conservative to start (README
// suggests 10-14); raise it once solveExact's measured speed justifies going deeper. Named so
// callers (GUI/CLI, once wired) never hardcode this threshold themselves.
constexpr int kExactSolverEmptyThreshold = 12;

// Perfect-play search to the true end of the game: no heuristic evaluation function anywhere —
// every leaf is `terminalScore`, the actual final disc differential. `Position::emptyCount()`
// squares remain, so this is exhaustive (not depth-limited in the heuristic-search sense).
//
// Reuses the same building blocks as search() (Position/moves, terminalScore, TranspositionTable
// with the same Exact/Lower/Upper bound convention, CancellationToken) but is a deliberately
// separate implementation, not a mode flag on search()'s negamax: move ordering here is
// endgame-specific (fastest-first + empty-region parity, see solver.cpp) rather than the
// midgame-tuned TT-move/killer/history/corner-bias ordering search.cpp uses, and there is no
// EvalFn parameter to thread through at all - forcing one shared implementation would mean
// carrying unused eval/killer/history machinery through the hot path for no benefit.
//
// SearchResult.score is the exact final disc differential from `p`'s mover's perspective when
// completed is true. `nodes` counts solver-internal negamax calls (a separate counter space
// from search()'s - not meant to be compared node-for-node across the two).
//
// IMPORTANT: `tt`, if provided, must be a table used ONLY by solveExact - never one shared with
// search()/searchIterative()/searchTimed(). Both use the same TTEntry/Bound encoding, but the
// meaning of Bound::Exact differs: for search(), it means "the true value at the depth that
// search happened to reach," which is only ever a lower bound on the true game-theoretic value;
// for solveExact(), it means the actual final result. A table shared between the two would let
// a heuristic-search entry be trusted by the solver as if it were exact, silently corrupting an
// otherwise-perfect solve. (The reverse - solveExact() entries read by search() - is safe: an
// exact value is always at least as good as any depth-bounded heuristic claim; only mixing them
// into ONE solveExact() call is dangerous. Still: use separate tables, don't rely on this
// asymmetry.)
//
// Precondition: hasLegalMove(p), same as search() - the caller applies any forced pass itself
// before calling.
SearchResult solveExact(const Position& p, const CancellationToken* cancellation = nullptr,
                        TranspositionTable* tt = nullptr);

} // namespace reversi
