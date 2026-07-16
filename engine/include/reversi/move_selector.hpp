#pragma once

#include "reversi/cancellation.hpp"
#include "reversi/eval.hpp"
#include "reversi/opening_book.hpp"
#include "reversi/position.hpp"
#include "reversi/search.hpp"
#include "reversi/solver.hpp"
#include "reversi/tt.hpp"

namespace reversi {

// The shared composition point this project was missing before M6 Phase 2: previously neither
// app/ nor cli/ dispatched between the exact endgame solver and heuristic search at all (each
// gameplay path called searchTimed()/search() directly, unconditionally), so there was nowhere
// for an opening-book check to plug in without becoming a THIRD independently-duplicated
// decision on top of an already-duplicated one. selectMove() is that one place: book (if
// present and it has a move) -> solveExact (if emptyCount() <= exactSolverEmptyThreshold) ->
// searchTimed.
struct MoveSelectorConfig {
    // nullptr disables the opening book entirely - this IS the "toggleable" flag from the
    // original feature spec: a boolean-shaped control point (present or absent), not a
    // settings-panel UI (that's still M9). Mirrors GameController's existing
    // lastMoveHighlightEnabled_ pattern: the control point exists, default off.
    const OpeningBook* book = nullptr;
    // Overridable copy of solver.hpp's kExactSolverEmptyThreshold - defaulted to the same
    // value, but a caller (e.g. a future "solve harder" setting) can raise or lower it without
    // touching solver.hpp itself.
    int exactSolverEmptyThreshold = kExactSolverEmptyThreshold;
    int maxDepth = 60;
    TimeBudget budget;
};

// Precondition: hasLegalMove(p), same as search()/solveExact() - the caller applies any forced
// pass itself before calling selectMove(), exactly as it would before calling search() or
// solveExact() directly.
//
// A book hit returns immediately without touching solveExact or searchTimed at all:
// completed=true, bestMove set to the book's move, depth=0/nodes=0/score=0 - this honestly
// reflects "looked up, not searched" rather than fabricating a depth/score/node count that was
// never actually computed. Callers that need to distinguish book/solved/searched results can do
// so via these fields (depth==0 && nodes==0 is unambiguous: the cheapest a real search or solve
// can finish is depth>=1 with nodes>=1).
//
// IMPORTANT (same constraint as solveExact()'s own doc comment / CLAUDE.md): if both `searchTt`
// and `solverTt` are provided, they MUST be different objects - never share one table between
// the solver and heuristic search branches this function may take.
SearchResult selectMove(const Position& p, const EvalFn& eval, const MoveSelectorConfig& config,
                        const CancellationToken* cancellation = nullptr,
                        TranspositionTable* searchTt = nullptr,
                        TranspositionTable* solverTt = nullptr);

} // namespace reversi
