#pragma once

#include "reversi/cancellation.hpp"
#include "reversi/eval.hpp"
#include "reversi/mpc.hpp"
#include "reversi/position.hpp"
#include "reversi/shared_tt.hpp"
#include "reversi/tt.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

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
// `mpc`, if non-null and `mpc->model` is non-null (see mpc.hpp), enables Multi-ProbCut at
// internal nodes: `mpc == nullptr` is the default on every call site and disables it entirely,
// with zero overhead - the check happens before any extra work is attempted. When engaged, MPC
// is a PROBABILISTIC pruning technique (unlike everything else this function does) - it can
// occasionally cut a node it shouldn't, changing the returned score/move; nothing else in this
// signature carries that risk.
// Precondition: hasLegalMove(p). The caller checks this / applies forced passes itself
// before calling search — search never needs to pick a move for a position that has none.
SearchResult search(const Position& p, int depth, const EvalFn& eval = evaluateDiscDifferential,
                    const CancellationToken* cancellation = nullptr,
                    TranspositionTable* tt = nullptr, const MpcConfig* mpc = nullptr);

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
// Same precondition as search(): hasLegalMove(p). `mpc`: same contract as search()'s.
SearchResult searchIterative(const Position& p, int maxDepth,
                             const EvalFn& eval = evaluateDiscDifferential,
                             const CancellationToken* cancellation = nullptr,
                             TranspositionTable* tt = nullptr, const MpcConfig* mpc = nullptr);

// Root search over an explicit (alpha, beta) window — the primitive underneath aspiration
// windows, exposed so the fail-high/fail-low paths are directly testable. If the true value
// lies strictly inside (alpha, beta), the result is exact and bestMove provably achieves it.
// Otherwise the search FAILED the window and the result is only a bound: score <= alpha
// means the true value is <= score (fail low), score >= beta means it is >= score (fail
// high), and bestMove must NOT be trusted (on a fail-low it is whichever move was probed
// first, not a validated choice). Callers must widen and re-search before acting on a failed
// window — searchIterative does exactly that. search(p, depth, ...) is this with the full
// (-inf, +inf) window. `mpc`: same contract as search()'s.
SearchResult searchWindow(const Position& p, int depth, int alpha, int beta,
                          const EvalFn& eval = evaluateDiscDifferential,
                          const CancellationToken* cancellation = nullptr,
                          TranspositionTable* tt = nullptr, const MpcConfig* mpc = nullptr);

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
// `mpc`: same contract as search()'s.
SearchResult searchTimed(const Position& p, int maxDepth, const TimeBudget& budget,
                         const EvalFn& eval = evaluateDiscDifferential,
                         const CancellationToken* cancellation = nullptr,
                         TranspositionTable* tt = nullptr, const MpcConfig* mpc = nullptr);

// search()/searchTimed() with an extra constraint: any square in `excludedRootMoves` is skipped
// entirely at the root - never explored, never compared, never returned as bestMove. NOT a
// change to search()/searchWindow()/searchIterative()/searchTimed() themselves (their signatures
// and behavior are unchanged); these are new sibling functions built for MultiPV analysis
// (engine/include/reversi/analysis.hpp's analyzeTopMoves(), which calls
// searchTimedExcludingMoves() once per ranked line, excluding every move already ranked). Same
// contracts as search()/searchTimed() otherwise (result.completed, tt, cancellation, mpc all mean
// exactly what they do there).
// Precondition: same as search()/searchTimed() (hasLegalMove(p)), PLUS at least one legal move of
// `p` is not present in `excludedRootMoves`.
SearchResult searchExcludingMoves(const Position& p, int depth,
                                  const std::vector<int>& excludedRootMoves,
                                  const EvalFn& eval = evaluateDiscDifferential,
                                  const CancellationToken* cancellation = nullptr,
                                  TranspositionTable* tt = nullptr, const MpcConfig* mpc = nullptr);
SearchResult searchTimedExcludingMoves(const Position& p, int maxDepth, const TimeBudget& budget,
                                       const std::vector<int>& excludedRootMoves,
                                       const EvalFn& eval = evaluateDiscDifferential,
                                       const CancellationToken* cancellation = nullptr,
                                       TranspositionTable* tt = nullptr,
                                       const MpcConfig* mpc = nullptr);

// Per-thread depth-start stagger for searchLazySmp() below: thread `i`'s iterative-deepening
// loop starts at `1 + (i % kLazySmpJitterPeriod)` instead of always 1 (thread 0, the "main"
// thread, is the one exception - it always starts at 1, see searchLazySmp's own doc comment).
// Deliberately simple: unjittered threads would all search the identical depth in lockstep at
// the same wall-clock moment, making the shared table nearly useless (every thread hits/misses
// identically); staggering start depths means different threads are usually at different
// depths at any given moment, so their TT contributions are actually new information to
// whichever thread finds them.
constexpr int kLazySmpJitterPeriod = 4;

// Lazy SMP (M8): `threadCount` independent iterative-deepening searches over the SAME shared,
// concurrent-safe transposition table (SharedTranspositionTable, shared_tt.hpp), scaling via
// redundant parallel exploration rather than explicit tree-splitting (not YBWC - no work is
// ever assigned to a specific thread by subtree; every thread just runs its own full search
// and opportunistically benefits from what other threads have already found).
//
// threadCount <= 1 runs thread 0's own work on the calling thread directly - no std::thread is
// spawned at all, so this has zero threading overhead beyond what the single-threaded path
// already has. threadCount > 1 spawns exactly `threadCount` threads (including thread 0);
// their own iterative-deepening loop is the exact same iterativeDriver() searchIterative()/
// searchTimed() already use, just parameterized with a jittered start depth per thread.
//
// Thread 0 ("main") never gets jittered - it always starts at depth 1, runs the identical
// sequence searchTimed() always has, and its own SearchResult is the one returned:
// completed/score/bestMove/depth describe thread 0's search exactly as if it had run alone,
// except it may benefit from TT entries other threads populated. This can only ever save work,
// never change the returned score: any TT entry that passes SharedTranspositionTable::probe()'s
// validation is a genuinely-computed true result for its exact position/depth, from WHATEVER
// thread computed it (see shared_tt.hpp's own doc comment) - the same "a correct table is a
// pure time-saver, never changes the answer" invariant this project already relies on for the
// single-threaded table, extended unchanged to the concurrent case (verified directly by
// tests/engine/search_lazy_smp_test.cpp: searchLazySmp's score at a fixed depth exactly matches
// plain search()'s, threadCount aside). Helper threads 1..threadCount-1's own results are
// discarded entirely - their only contribution is the TT entries they leave behind.
//
// `result.nodes` is the SUM of every thread's own node count (total computational work - what
// nps-scaling measurements need), a deliberate difference from every other function in this
// header, which reports one search's own node count.
//
// `eval` and `mpc` must be safe to call concurrently from multiple threads with no external
// synchronization - true for every EvalFn/MpcModel this project ships (evaluateDiscDifferential
// is a stateless function; PatternEvaluator::evaluate() and MpcModel::lookup() are const methods
// over data loaded once at construction and never mutated afterward), but worth stating as a
// precondition for any future custom eval.
// Same precondition as search(): hasLegalMove(p). Also: threadCount >= 1.
SearchResult searchLazySmp(const Position& p, int maxDepth, const TimeBudget& budget,
                           int threadCount, const EvalFn& eval = evaluateDiscDifferential,
                           const CancellationToken* cancellation = nullptr,
                           SharedTranspositionTable* sharedTt = nullptr,
                           const MpcConfig* mpc = nullptr);

} // namespace reversi
