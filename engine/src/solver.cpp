#include "reversi/solver.hpp"

#include "reversi/moves.hpp"

#include <algorithm>
#include <array>
#include <bit>

namespace reversi {

namespace {

// Matches search.cpp's convention: far enough from the true score range ([-64, 64]) that
// negating it can never overflow.
constexpr int kInfinity = 1'000'000;

struct SolverContext {
    const CancellationToken* cancellation;
    TranspositionTable* tt;
    std::uint64_t nodes = 0;
};

bool stopRequested(const SolverContext& ctx) {
    return ctx.cancellation != nullptr && ctx.cancellation->stopRequested();
}

struct MoveList {
    std::array<int, 34> squares; // 33 is the known maximum number of legal moves in Othello
    int count = 0;
};

// Step 1: plain ascending-square-index order (whatever legalMoves's bit-scan already gives),
// with the TT move (if any) promoted to the front. Endgame-specific ordering (fastest-first +
// parity) replaces this in a later step - kept deliberately simple here to isolate "is the new
// recursion structurally correct" from "is the new ordering correct."
MoveList orderedMoves(Bitboard moves, int ttMove) {
    MoveList list;
    if (ttMove != -1 && (moves & bit(ttMove)) != 0) {
        list.squares[list.count++] = ttMove;
        moves &= ~bit(ttMove);
    }
    for (Bitboard b = moves; b != 0; b &= b - 1) {
        list.squares[list.count++] = std::countr_zero(b);
    }
    return list;
}

int negamax(const Position& pos, int alpha, int beta, SolverContext& ctx) {
    ++ctx.nodes;
    if (stopRequested(ctx)) {
        // Collapsed value is irrelevant: the root marks the overall result incomplete and
        // callers that care about correctness discard it rather than trust this number.
        return terminalScore(pos);
    }
    const Bitboard moves = legalMoves(pos);
    if (moves == 0) {
        const Position passed = applyPass(pos);
        if (!hasLegalMove(passed)) {
            return terminalScore(pos); // neither side can move: the game is actually over
        }
        // Forced pass: no depth/empty-count concept is consumed by a pass either way, since
        // this solver has none - it just keeps recursing until the game truly ends.
        return -negamax(passed, -beta, -alpha, ctx);
    }

    const std::uint64_t hash = ctx.tt != nullptr ? zobristHash(pos) : 0;
    int ttMove = -1;
    if (ctx.tt != nullptr) {
        if (const TTEntry* entry = ctx.tt->probe(hash); entry != nullptr) {
            // No depth gating needed (unlike search.cpp's TT usage): every entry this solver
            // ever stores already reflects a full solve of its position, so any hit is
            // unconditionally trustworthy - see solver.hpp's doc comment on why this table must
            // never be shared with the heuristic search.
            ttMove = entry->bestMove;
            switch (entry->bound) {
            case Bound::Exact:
                return entry->score;
            case Bound::Lower:
                alpha = std::max(alpha, entry->score);
                break;
            case Bound::Upper:
                beta = std::min(beta, entry->score);
                break;
            }
            if (alpha >= beta) {
                return entry->score;
            }
        }
    }

    const int alphaOriginal = alpha;
    const MoveList list = orderedMoves(moves, ttMove);
    int best = -kInfinity;
    int bestSquare = -1;
    for (int i = 0; i < list.count; ++i) {
        const int square = list.squares[i];
        const Position child = applyMove(pos, square);
        int score;
        if (i == 0) {
            score = -negamax(child, -beta, -alpha, ctx);
        } else {
            // PVS: zero-window probe for every move after the first, full-window re-search
            // only on an in-window fail-high. Safe here for the same reason as search.cpp -
            // it's a pruning/scheduling technique that never changes the computed score.
            score = -negamax(child, -alpha - 1, -alpha, ctx);
            if (score > alpha && score < beta && !stopRequested(ctx)) {
                score = -negamax(child, -beta, -alpha, ctx);
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
            break; // fail-soft: `best` is returned as-is, not clamped to beta
        }
    }

    if (ctx.tt != nullptr && !stopRequested(ctx)) {
        const Bound bound = best <= alphaOriginal ? Bound::Upper
                            : best >= beta        ? Bound::Lower
                                                  : Bound::Exact;
        // emptyCount() stands in for "depth" in the replacement policy's collision heuristic
        // (a position with more empties represents a larger solved subtree, worth keeping over
        // one with fewer) - it plays no role in trust/validity here, unlike search.cpp's TT.
        ctx.tt->store(hash, pos.emptyCount(), best, bound, bestSquare);
    }
    return best;
}

} // namespace

SearchResult solveExact(const Position& p, const CancellationToken* cancellation,
                        TranspositionTable* tt) {
    SolverContext ctx{cancellation, tt};
    int ttMove = -1;
    if (tt != nullptr) {
        if (const TTEntry* entry = tt->probe(zobristHash(p)); entry != nullptr) {
            ttMove = entry->bestMove;
        }
    }
    const MoveList list = orderedMoves(legalMoves(p), ttMove);
    SearchResult result;
    int alpha = -kInfinity;
    const int beta = kInfinity;
    for (int i = 0; i < list.count; ++i) {
        const int square = list.squares[i];
        const Position child = applyMove(p, square);
        int score;
        if (i == 0) {
            score = -negamax(child, -beta, -alpha, ctx);
        } else {
            score = -negamax(child, -alpha - 1, -alpha, ctx);
            if (score > alpha && !stopRequested(ctx)) { // score < beta always: beta is +inf
                score = -negamax(child, -beta, -alpha, ctx);
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
    result.depth = result.completed ? p.emptyCount() : 0;
    if (tt != nullptr && result.completed) {
        tt->store(zobristHash(p), p.emptyCount(), result.score, Bound::Exact, result.bestMove);
    }
    return result;
}

} // namespace reversi
