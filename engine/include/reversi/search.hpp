#pragma once

#include "reversi/cancellation.hpp"
#include "reversi/eval.hpp"
#include "reversi/position.hpp"
#include "reversi/tt.hpp"

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

// Fixed-depth negamax with fail-soft alpha-beta pruning. `depth` counts real moves only: a
// forced pass re-searches at the *same* depth (see search.cpp) rather than consuming one,
// since a pass isn't a decision worth spending look-ahead budget on — this is a different,
// independent convention from perft's "a pass consumes a ply" (see perft.hpp), which exists
// to match a published node-counting convention rather than to describe search intent.
// `nodes` counts negamax invocations below the root, for reporting nps.
// `cancellation`, if non-null, is polled once per node; a requested stop collapses the
// remaining recursion promptly (see search.cpp) rather than running the search to completion.
// `tt`, if non-null, is probed/filled during the search. A correct table is a pure
// time-saver: it must never change bestMove or score at a given depth (asserted directly by
// tests/engine/tt_test.cpp on the shared benchmark set) — only how much of the tree gets
// visited to compute them. Nothing is stored from a cancelled subtree, so a table shared
// across searches can't be poisoned by an abort.
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
// Same precondition as search(): hasLegalMove(p).
SearchResult searchIterative(const Position& p, int maxDepth,
                             const EvalFn& eval = evaluateDiscDifferential,
                             const CancellationToken* cancellation = nullptr,
                             TranspositionTable* tt = nullptr);

} // namespace reversi
