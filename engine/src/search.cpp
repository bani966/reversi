#include "reversi/search.hpp"

#include "reversi/moves.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>

namespace reversi {

namespace {

using Clock = std::chrono::steady_clock;

// Far enough from the true score range (disc differentials live in [-64, 64]) that negating
// it can never overflow, unlike negating INT_MIN.
constexpr int kInfinity = 1'000'000;

// An Othello game path is at most 60 real moves, and passes never occur twice in a row (that
// would be game over), so a root-to-leaf path never exceeds ~120 plies.
constexpr int kMaxPly = 128;

// Ordering-score tiers. TT move first, then the two killers; everything else competes on
// history + static bias, which is capped well below the killer tier so the tiers can't mix.
constexpr int kTtMoveScore = 1 << 30;
constexpr int kKillerScore = 1 << 29;
constexpr int kHistoryCap = 1 << 27;

// Corner-biased static ordering fallback for moves with no TT/killer/history signal yet:
// corners are almost always strong in Othello, the X squares (diagonal corner neighbors) and
// C squares (orthogonal corner neighbors) usually hand the corner away, other edges are
// mildly good. Ordering hints only - these values never leak into a returned score.
constexpr std::array<int, kBoardSquares> kStaticOrderBias = [] {
    std::array<int, kBoardSquares> bias{};
    for (int square = 0; square < kBoardSquares; ++square) {
        const int fileDist = std::min(square % 8, 7 - square % 8); // distance to nearest edge
        const int rankDist = std::min(square / 8, 7 - square / 8);
        if (fileDist == 0 && rankDist == 0) {
            bias[square] = 100; // corner
        } else if (fileDist == 1 && rankDist == 1) {
            bias[square] = -50; // X square
        } else if (fileDist + rankDist == 1) {
            bias[square] = -20; // C square
        } else if (fileDist == 0 || rankDist == 0) {
            bias[square] = 10; // other edge square
        }
    }
    return bias;
}();

// The external cancellation token is polled every node (a cheap atomic load), but the hard
// deadline's clock read is amortized to once per this many nodes - frequent enough that a
// deadline overshoot stays in the sub-millisecond range at realistic search speeds.
constexpr int kDeadlineCheckInterval = 2048;

// Per-search state threaded through the recursion. Killers/history are fresh per search()
// call (they describe *this* search's tree, unlike the TT, which is designed to outlive it).
struct SearchContext {
    const EvalFn& eval;
    const CancellationToken* cancellation;
    TranspositionTable* tt;
    const Clock::time_point* hardDeadline; // may be null: no time limit
    std::uint64_t nodes = 0;
    bool timedOut = false; // sticky once the hard deadline is observed passed
    int deadlineCheckCountdown = kDeadlineCheckInterval;
    std::array<std::array<int, 2>, kMaxPly> killers{}; // filled with -1 by makeContext
    std::array<int, kBoardSquares> history{};
};

SearchContext makeContext(const EvalFn& eval, const CancellationToken* cancellation,
                          TranspositionTable* tt, const Clock::time_point* hardDeadline) {
    SearchContext ctx{eval, cancellation, tt, hardDeadline};
    for (auto& plyKillers : ctx.killers) {
        plyKillers = {-1, -1};
    }
    return ctx;
}

bool stopRequested(SearchContext& ctx) {
    if (ctx.timedOut) {
        return true;
    }
    if (ctx.cancellation != nullptr && ctx.cancellation->stopRequested()) {
        return true;
    }
    if (ctx.hardDeadline != nullptr && --ctx.deadlineCheckCountdown <= 0) {
        ctx.deadlineCheckCountdown = kDeadlineCheckInterval;
        if (Clock::now() >= *ctx.hardDeadline) {
            ctx.timedOut = true;
            return true;
        }
    }
    return false;
}

struct MoveList {
    std::array<int, 34> squares; // 33 is the known maximum number of legal moves in Othello
    int count = 0;
};

// Extracts the set moves and insertion-sorts them by descending ordering score:
// TT move > killer 1 > killer 2 > history + static corner bias.
MoveList orderedMoves(Bitboard moves, int ttMove, const SearchContext& ctx, int ply) {
    MoveList list;
    std::array<int, 34> scores;
    for (Bitboard b = moves; b != 0; b &= b - 1) {
        const int square = std::countr_zero(b);
        int score;
        if (square == ttMove) {
            score = kTtMoveScore;
        } else if (square == ctx.killers[ply][0]) {
            score = kKillerScore;
        } else if (square == ctx.killers[ply][1]) {
            score = kKillerScore - 1;
        } else {
            score = ctx.history[square] + kStaticOrderBias[square];
        }
        int i = list.count++;
        while (i > 0 && scores[i - 1] < score) {
            list.squares[i] = list.squares[i - 1];
            scores[i] = scores[i - 1];
            --i;
        }
        list.squares[i] = square;
        scores[i] = score;
    }
    return list;
}

// A move that just caused a beta cutoff gets remembered: as a killer for this ply (tried
// early at sibling nodes) and in the history table (tried early everywhere), weighted by
// depth^2 so cutoffs near the root count for more than leaf-adjacent ones.
void recordCutoff(SearchContext& ctx, int ply, int square, int depth) {
    if (ctx.killers[ply][0] != square) {
        ctx.killers[ply][1] = ctx.killers[ply][0];
        ctx.killers[ply][0] = square;
    }
    ctx.history[square] = std::min(kHistoryCap, ctx.history[square] + depth * depth);
}

int negamax(const Position& pos, int depth, int ply, int alpha, int beta, SearchContext& ctx) {
    ++ctx.nodes;
    if (stopRequested(ctx)) {
        // Collapse immediately rather than recursing further: every nested call checks this
        // same condition at its own entry, so a requested stop unwinds the whole remaining
        // call stack in O(current depth), not O(remaining tree). The exact value returned
        // here is irrelevant — search() marks the overall result incomplete and callers that
        // care about correctness discard it rather than trust this number.
        return ctx.eval(pos);
    }
    const Bitboard moves = legalMoves(pos);
    if (moves == 0) {
        const Position passed = applyPass(pos);
        if (!hasLegalMove(passed)) {
            return terminalScore(pos); // neither side can move: game over
        }
        if (depth == 0) {
            return ctx.eval(pos);
        }
        // Forced pass: doesn't consume depth, see search.hpp. No TT interaction here — a pass
        // node is a pure forwarder, and the post-pass position probes/stores for itself.
        return -negamax(passed, depth, ply + 1, -beta, -alpha, ctx);
    }
    if (depth == 0) {
        return ctx.eval(pos);
    }

    const std::uint64_t hash = ctx.tt != nullptr ? zobristHash(pos) : 0;
    int ttMove = -1;
    if (ctx.tt != nullptr) {
        if (const TTEntry* entry = ctx.tt->probe(hash); entry != nullptr) {
            // The stored move is a useful ordering hint at ANY stored depth; the stored score
            // is only trusted at sufficient depth, and strictly per its bound type. (Within
            // one fixed-depth search the depth comparison is always an equality - remaining
            // depth is a function of the disc count - so the >= guard exists for tables
            // shared across iterative-deepening iterations, where shallower entries linger.)
            ttMove = entry->bestMove;
            if (entry->depth >= depth) {
                switch (entry->bound) {
                case Bound::Exact:
                    return entry->score;
                case Bound::Lower: // true value >= score: usable only to raise alpha
                    alpha = std::max(alpha, entry->score);
                    break;
                case Bound::Upper: // true value <= score: usable only to lower beta
                    beta = std::min(beta, entry->score);
                    break;
                }
                if (alpha >= beta) {
                    return entry->score;
                }
            }
        }
    }

    const int alphaOriginal = alpha;
    const MoveList list = orderedMoves(moves, ttMove, ctx, ply);
    int best = -kInfinity;
    int bestSquare = -1;
    for (int i = 0; i < list.count; ++i) {
        const int square = list.squares[i];
        const Position child = applyMove(pos, square);
        int score;
        if (i == 0) {
            // PVS: only the first (best-ordered) move gets a full window.
            score = -negamax(child, depth - 1, ply + 1, -beta, -alpha, ctx);
        } else {
            // The rest are probed with a zero-width window - cheap to refute when the first
            // move really was best - and re-searched with the full window only when the probe
            // fails high inside the window (meaning "better than the best so far", so an
            // exact score is now actually needed). Skipped after a stop request: the exact
            // value would be discarded anyway.
            score = -negamax(child, depth - 1, ply + 1, -alpha - 1, -alpha, ctx);
            if (score > alpha && score < beta && !stopRequested(ctx)) {
                score = -negamax(child, depth - 1, ply + 1, -beta, -alpha, ctx);
            }
        }
        if (score > best) {
            best = score;
            bestSquare = square;
        }
        if (best > alpha) {
            alpha = best;
        }
        if (alpha >= beta) {
            recordCutoff(ctx, ply, square, depth);
            break; // fail-soft: `best` is returned as-is, not clamped to beta
        }
    }

    if (ctx.tt != nullptr && !stopRequested(ctx)) {
        // The cancellation re-check matters: a stop request mid-loop makes `best` reflect
        // collapsed subtrees, and storing that would poison the table for later searches.
        const Bound bound = best <= alphaOriginal ? Bound::Upper
                            : best >= beta        ? Bound::Lower
                                                  : Bound::Exact;
        ctx.tt->store(hash, depth, best, bound, bestSquare);
    }
    return best;
}

// Root search over an explicit (alphaInit, betaInit) window; see searchWindow's contract in
// search.hpp for what a failed window means for the returned score/bestMove.
SearchResult windowedSearch(const Position& p, int depth, int alphaInit, int betaInit,
                            const EvalFn& eval, const CancellationToken* cancellation,
                            TranspositionTable* tt, const Clock::time_point* hardDeadline) {
    SearchContext ctx = makeContext(eval, cancellation, tt, hardDeadline);
    int ttMove = -1;
    if (tt != nullptr) {
        if (const TTEntry* entry = tt->probe(zobristHash(p)); entry != nullptr) {
            // Seeds root ordering from the previous iterative-deepening iteration's answer.
            // Only the move hint is used at the root: the root must always search fully.
            ttMove = entry->bestMove;
        }
    }
    const MoveList list = orderedMoves(legalMoves(p), ttMove, ctx, 0);
    SearchResult result;
    int alpha = alphaInit;
    const int beta = betaInit;
    for (int i = 0; i < list.count; ++i) {
        const int square = list.squares[i];
        const Position child = applyMove(p, square);
        int score;
        if (i == 0) {
            score = -negamax(child, depth - 1, 1, -beta, -alpha, ctx);
        } else {
            score = -negamax(child, depth - 1, 1, -alpha - 1, -alpha, ctx);
            if (score > alpha && score < beta && !stopRequested(ctx)) {
                score = -negamax(child, depth - 1, 1, -beta, -alpha, ctx);
            }
        }
        if (result.bestMove == -1 || score > result.score) {
            result.score = score;
            result.bestMove = square;
        }
        if (result.score > alpha) {
            alpha = result.score;
        }
        if (alpha >= beta) {
            break; // root fail-high: the result is a lower bound; the caller must widen
        }
    }
    result.nodes = ctx.nodes;
    result.completed = !ctx.timedOut && (cancellation == nullptr || !cancellation->stopRequested());
    result.depth = result.completed ? depth : 0;
    if (tt != nullptr && result.completed) {
        // Tagged with the bound the window actually proved - storing a failed aspiration
        // window's score as Exact is precisely the "stale bound trusted incorrectly" bug
        // class this milestone's plan warns about. (With the full window this is always
        // Exact, since real scores can't reach +/-kInfinity.) On a fail-low the stored
        // bestMove is just whichever move was probed first - fine as an ordering hint,
        // which is all stored moves are ever used for.
        const Bound bound = result.score <= alphaInit ? Bound::Upper
                            : result.score >= beta    ? Bound::Lower
                                                      : Bound::Exact;
        tt->store(zobristHash(p), depth, result.score, bound, result.bestMove);
    }
    return result;
}

// Aspiration half-width, in disc-differential units. Wide enough that most iteration-to-
// iteration score drifts stay inside (no re-search), narrow enough to actually tighten the
// window. The disc-differential eval swings hard between depths, so the failure paths are
// exercised heavily in practice, in both directions: measured 49 re-searches across the
// benchmark set's depth-6 iterative runs alone when this landed.
constexpr int kAspirationDelta = 8;

SearchResult iterativeDriver(const Position& p, int maxDepth, const EvalFn& eval,
                             const CancellationToken* cancellation, TranspositionTable* tt,
                             const Clock::time_point* softDeadline,
                             const Clock::time_point* hardDeadline) {
    SearchResult deepest;
    deepest.completed = false; // stays false unless some iteration actually finishes
    std::uint64_t totalNodes = 0;
    for (int depth = 1; depth <= maxDepth; ++depth) {
        if (depth > 1 && softDeadline != nullptr && Clock::now() >= *softDeadline) {
            break; // soft budget spent: don't start another iteration
        }
        SearchResult iteration;
        if (!deepest.completed) {
            // First iteration (nothing to center a window on): full width.
            iteration = windowedSearch(p, depth, -kInfinity, kInfinity, eval, cancellation, tt,
                                       hardDeadline);
            totalNodes += iteration.nodes;
        } else {
            // Aspiration: assume this depth's score lands near the previous depth's.
            const int alpha = deepest.score - kAspirationDelta;
            const int beta = deepest.score + kAspirationDelta;
            iteration = windowedSearch(p, depth, alpha, beta, eval, cancellation, tt, hardDeadline);
            totalNodes += iteration.nodes;
            if (iteration.completed && (iteration.score <= alpha || iteration.score >= beta)) {
                // Failed the window: the score is only a bound and the move untrustworthy.
                // Never let that result stand - re-search with the full window. (One-step
                // widening straight to full width keeps the failure path trivially correct;
                // the happy path is where the speed lives anyway.)
                iteration = windowedSearch(p, depth, -kInfinity, kInfinity, eval, cancellation, tt,
                                           hardDeadline);
                totalNodes += iteration.nodes;
            }
        }
        if (!iteration.completed) {
            break; // aborted mid-iteration: the previous depth's result stands
        }
        deepest = iteration;
    }
    deepest.nodes = totalNodes;
    return deepest;
}

} // namespace

SearchResult search(const Position& p, int depth, const EvalFn& eval,
                    const CancellationToken* cancellation, TranspositionTable* tt) {
    return windowedSearch(p, depth, -kInfinity, kInfinity, eval, cancellation, tt, nullptr);
}

SearchResult searchWindow(const Position& p, int depth, int alpha, int beta, const EvalFn& eval,
                          const CancellationToken* cancellation, TranspositionTable* tt) {
    return windowedSearch(p, depth, alpha, beta, eval, cancellation, tt, nullptr);
}

SearchResult searchIterative(const Position& p, int maxDepth, const EvalFn& eval,
                             const CancellationToken* cancellation, TranspositionTable* tt) {
    return iterativeDriver(p, maxDepth, eval, cancellation, tt, nullptr, nullptr);
}

SearchResult searchTimed(const Position& p, int maxDepth, const TimeBudget& budget,
                         const EvalFn& eval, const CancellationToken* cancellation,
                         TranspositionTable* tt) {
    const Clock::time_point start = Clock::now();
    const Clock::time_point softDeadline = start + budget.soft;
    const Clock::time_point hardDeadline = start + budget.hard;
    return iterativeDriver(p, maxDepth, eval, cancellation, tt, &softDeadline, &hardDeadline);
}

} // namespace reversi
