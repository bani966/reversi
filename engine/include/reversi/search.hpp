#pragma once

#include "reversi/cancellation.hpp"
#include "reversi/eval.hpp"
#include "reversi/position.hpp"
#include "reversi/tt.hpp"

#include <chrono>
#include <cstdint>

namespace reversi {

struct SearchResult {
    int bestMove = -1;
    int score = 0;
    std::uint64_t nodes = 0;
    // The look-ahead depth bestMove/score actually reflect: the requested depth for a
    // completed fixed-depth search, the deepest fully-finished iteration for searchIterative,
    // and 0 whenever completed is false (nothing was finished, so no depth can be claimed).
    int depth = 0;
    // False iff `cancellation` requested a stop before this search finished exploring every
    // move — bestMove/score reflect whatever was explored so far, not a complete answer, and
    // callers that care about correctness (e.g. a GUI applying the chosen move) must discard
    // the result rather than act on it when this is false.
    bool completed = true;
};

// Fixed-depth negamax with fail-soft alpha-beta pruning, PVS (zero-window probes for
// non-first moves with a full-window re-search on an in-window fail-high), and move ordering:
// TT move first, then this search's killer moves, then history scores with a corner-biased
// static fallback. Ordering and PVS are pruning/scheduling optimizations: they must never
// change the returned score (asserted against the frozen pre-ordering baseline in
// tests/support/baseline_search.*), but they MAY legitimately change WHICH of several
// equal-scoring moves is returned, since fail-soft returns the first strict improvement in
// visit order. `depth` counts real moves only: a
// forced pass re-searches at the *same* depth (see search.cpp) rather than consuming one,
// since a pass isn't a decision worth spending look-ahead budget on — this is a different,
// independent convention from perft's "a pass consumes a ply" (see perft.hpp), which exists
// to match a published node-counting convention rather than to describe search intent.
// `nodes` counts negamax invocations below the root, for reporting nps.
// `cancellation`, if non-null, is polled once per node; a requested stop collapses the
// remaining recursion promptly (see search.cpp) rather than running the search to completion.
// `tt`, if non-null, is probed/filled during the search. A correct table is a pure
// time-saver: it must never change the score at a given depth (asserted directly by
// tests/engine/tt_test.cpp on the shared benchmark set), only how much of the tree gets
// visited to compute it — though its move hints feed the ordering, so the returned move may
// be a different but equally-scoring one (verified optimal via tests/support/search_checks.*).
// Nothing is stored from a cancelled subtree, so a table shared across searches can't be
// poisoned by an abort.
// Precondition: hasLegalMove(p). The caller checks this / applies forced passes itself
// before calling search — search never needs to pick a move for a position that has none.
SearchResult search(const Position& p, int depth, const EvalFn& eval = evaluateDiscDifferential,
                    const CancellationToken* cancellation = nullptr,
                    TranspositionTable* tt = nullptr);

// Iterative deepening driver over search(): runs complete fixed-depth searches at depth
// 1, 2, ..., maxDepth and returns the deepest fully-finished iteration's result, with `nodes`
// accumulated across every iteration (including a final aborted one). Without a cancellation
// request this returns exactly search(p, maxDepth)'s bestMove/score - a scheduling change,
// not an algorithmic one (asserted by tests) - at the cost of the cheaper warm-up iterations.
// The payoff lands later in M4: a cancellation/deadline mid-iteration degrades gracefully to
// the previous depth's complete answer instead of an untrustworthy partial one, and the TT +
// move-ordering steps feed each iteration's discoveries into the next one's ordering.
// `completed` is true iff at least the depth-1 iteration finished, i.e. iff bestMove/score
// are trustworthy. A `tt` passed here is deliberately shared across iterations (the intended
// hot path — see tt.hpp); the same never-changes-the-answer contract as search()'s applies.
// From depth 2 on, each iteration first tries an aspiration window centered on the previous
// iteration's score (see search.cpp's kAspirationDelta); a result that fails the window is
// treated as the bound it is — never trusted as an answer — and re-searched full-window, so
// the returned score is always exact (asserted against fixed-depth search by tests).
// Same precondition as search(): hasLegalMove(p).
SearchResult searchIterative(const Position& p, int maxDepth,
                             const EvalFn& eval = evaluateDiscDifferential,
                             const CancellationToken* cancellation = nullptr,
                             TranspositionTable* tt = nullptr);

// Root search over an explicit (alpha, beta) window — the primitive underneath aspiration
// windows, exposed so the fail-high/fail-low paths are directly testable. If the true value
// lies strictly inside (alpha, beta), the result is exact and bestMove provably achieves it.
// Otherwise the search FAILED the window and the result is only a bound: score <= alpha
// means the true value is <= score (fail low), score >= beta means it is >= score (fail
// high), and bestMove must NOT be trusted (on a fail-low it is whichever move was probed
// first, not a validated choice). Callers must widen and re-search before acting on a failed
// window — searchIterative does exactly that. search(p, depth, ...) is this with the full
// (-inf, +inf) window.
SearchResult searchWindow(const Position& p, int depth, int alpha, int beta,
                          const EvalFn& eval = evaluateDiscDifferential,
                          const CancellationToken* cancellation = nullptr,
                          TranspositionTable* tt = nullptr);

struct TimeBudget {
    // Soft limit: once elapsed, no NEW iteration is started (an iteration in flight runs on).
    std::chrono::milliseconds soft{0};
    // Hard limit: the search aborts outright mid-iteration (checked every few thousand
    // nodes); the deepest fully-completed iteration's result stands. Must be >= soft.
    std::chrono::milliseconds hard{0};
};

// searchIterative driven by wall-clock time instead of only a depth cap: iterates up to
// maxDepth but stops early per `budget`. Running out of time is normal operation here, not
// cancellation — the result still has completed == true as long as at least the depth-1
// iteration finished (with a non-trivial budget it always does; depth 1 costs microseconds).
// `cancellation` still works on top for external aborts (e.g. the GUI closing mid-search).
SearchResult searchTimed(const Position& p, int maxDepth, const TimeBudget& budget,
                         const EvalFn& eval = evaluateDiscDifferential,
                         const CancellationToken* cancellation = nullptr,
                         TranspositionTable* tt = nullptr);

} // namespace reversi
