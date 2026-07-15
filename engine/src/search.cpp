#include "reversi/search.hpp"

#include "reversi/moves.hpp"

#include <algorithm>
#include <array>
#include <bit>

namespace reversi {

namespace {

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

// Per-search state threaded through the recursion. Killers/history are fresh per search()
// call (they describe *this* search's tree, unlike the TT, which is designed to outlive it).
struct SearchContext {
    const EvalFn& eval;
    const CancellationToken* cancellation;
    TranspositionTable* tt;
    std::uint64_t nodes = 0;
    std::array<std::array<int, 2>, kMaxPly> killers{}; // filled with -1 by makeContext
    std::array<int, kBoardSquares> history{};
};

SearchContext makeContext(const EvalFn& eval, const CancellationToken* cancellation,
                          TranspositionTable* tt) {
    SearchContext ctx{eval, cancellation, tt};
    for (auto& plyKillers : ctx.killers) {
        plyKillers = {-1, -1};
    }
    return ctx;
}

bool stopRequested(const SearchContext& ctx) {
    return ctx.cancellation != nullptr && ctx.cancellation->stopRequested();
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

} // namespace

SearchResult search(const Position& p, int depth, const EvalFn& eval,
                    const CancellationToken* cancellation, TranspositionTable* tt) {
    SearchContext ctx = makeContext(eval, cancellation, tt);
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
    int alpha = -kInfinity;
    const int beta = kInfinity;
    for (int i = 0; i < list.count; ++i) {
        const int square = list.squares[i];
        const Position child = applyMove(p, square);
        int score;
        if (i == 0) {
            score = -negamax(child, depth - 1, 1, -beta, -alpha, ctx);
        } else {
            score = -negamax(child, depth - 1, 1, -alpha - 1, -alpha, ctx);
            if (score > alpha && !stopRequested(ctx)) { // score < beta always: beta is +inf
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
    }
    result.nodes = ctx.nodes;
    result.completed = cancellation == nullptr || !cancellation->stopRequested();
    result.depth = result.completed ? depth : 0;
    if (tt != nullptr && result.completed) {
        // The root runs a full window (alpha only ever rises from -inf, beta stays +inf), so
        // a completed root score is always exact. Stored mainly for the root bestMove, which
        // seeds move ordering across iterative-deepening iterations.
        tt->store(zobristHash(p), depth, result.score, Bound::Exact, result.bestMove);
    }
    return result;
}

SearchResult searchIterative(const Position& p, int maxDepth, const EvalFn& eval,
                             const CancellationToken* cancellation, TranspositionTable* tt) {
    SearchResult deepest;
    deepest.completed = false; // stays false unless some iteration actually finishes
    std::uint64_t totalNodes = 0;
    for (int depth = 1; depth <= maxDepth; ++depth) {
        const SearchResult iteration = search(p, depth, eval, cancellation, tt);
        totalNodes += iteration.nodes;
        if (!iteration.completed) {
            break; // aborted mid-iteration: the previous depth's result stands
        }
        deepest = iteration;
    }
    deepest.nodes = totalNodes;
    return deepest;
}

} // namespace reversi
