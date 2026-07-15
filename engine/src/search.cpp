#include "reversi/search.hpp"

#include "reversi/moves.hpp"

#include <bit>

namespace reversi {

namespace {

// Far enough from the true score range (disc differentials live in [-64, 64]) that negating
// it can never overflow, unlike negating INT_MIN.
constexpr int kInfinity = 1'000'000;

int negamax(const Position& pos, int depth, int alpha, int beta, const EvalFn& eval,
            std::uint64_t& nodes, const CancellationToken* cancellation, TranspositionTable* tt) {
    ++nodes;
    if (cancellation != nullptr && cancellation->stopRequested()) {
        // Collapse immediately rather than recursing further: every nested call checks this
        // same condition at its own entry, so a requested stop unwinds the whole remaining
        // call stack in O(current depth), not O(remaining tree). The exact value returned
        // here is irrelevant — search() marks the overall result incomplete and callers that
        // care about correctness discard it rather than trust this number.
        return eval(pos);
    }
    const Bitboard moves = legalMoves(pos);
    if (moves == 0) {
        const Position passed = applyPass(pos);
        if (!hasLegalMove(passed)) {
            return terminalScore(pos); // neither side can move: game over
        }
        if (depth == 0) {
            return eval(pos);
        }
        // Forced pass: doesn't consume depth, see search.hpp. No TT interaction here — a pass
        // node is a pure forwarder, and the post-pass position probes/stores for itself.
        return -negamax(passed, depth, -beta, -alpha, eval, nodes, cancellation, tt);
    }
    if (depth == 0) {
        return eval(pos);
    }

    const std::uint64_t hash = tt != nullptr ? zobristHash(pos) : 0;
    if (tt != nullptr) {
        // Only trust entries computed with at least this node's remaining depth. (Within one
        // fixed-depth search this is always an equality: depth remaining is a function of the
        // disc count, since real moves each add a disc and passes don't consume depth. The >=
        // guard is what keeps a table shared across iterative-deepening iterations — where
        // shallower iterations' entries do linger — from ever shortcutting a deeper demand.)
        if (const TTEntry* entry = tt->probe(hash); entry != nullptr && entry->depth >= depth) {
            switch (entry->bound) {
            case Bound::Exact:
                return entry->score;
            case Bound::Lower: // true value >= score: usable only to raise alpha
                alpha = alpha > entry->score ? alpha : entry->score;
                break;
            case Bound::Upper: // true value <= score: usable only to lower beta
                beta = beta < entry->score ? beta : entry->score;
                break;
            }
            if (alpha >= beta) {
                return entry->score;
            }
        }
    }

    const int alphaOriginal = alpha;
    int best = -kInfinity;
    int bestSquare = -1;
    for (Bitboard b = moves; b != 0; b &= b - 1) {
        const int square = std::countr_zero(b);
        const int score = -negamax(applyMove(pos, square), depth - 1, -beta, -alpha, eval, nodes,
                                   cancellation, tt);
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

    if (tt != nullptr && (cancellation == nullptr || !cancellation->stopRequested())) {
        // The cancellation re-check matters: a stop request mid-loop makes `best` reflect
        // collapsed subtrees, and storing that would poison the table for later searches.
        const Bound bound = best <= alphaOriginal ? Bound::Upper
                            : best >= beta        ? Bound::Lower
                                                  : Bound::Exact;
        tt->store(hash, depth, best, bound, bestSquare);
    }
    return best;
}

} // namespace

SearchResult search(const Position& p, int depth, const EvalFn& eval,
                    const CancellationToken* cancellation, TranspositionTable* tt) {
    const Bitboard moves = legalMoves(p);
    SearchResult result;
    int alpha = -kInfinity;
    const int beta = kInfinity;
    for (Bitboard b = moves; b != 0; b &= b - 1) {
        const int square = std::countr_zero(b);
        const int score = -negamax(applyMove(p, square), depth - 1, -beta, -alpha, eval,
                                   result.nodes, cancellation, tt);
        if (result.bestMove == -1 || score > result.score) {
            result.score = score;
            result.bestMove = square;
        }
        if (result.score > alpha) {
            alpha = result.score;
        }
    }
    result.completed = cancellation == nullptr || !cancellation->stopRequested();
    result.depth = result.completed ? depth : 0;
    if (tt != nullptr && result.completed) {
        // The root runs a full window (alpha only ever rises from -inf, beta stays +inf), so
        // a completed root score is always exact. Stored mainly for the root bestMove, which
        // seeds move ordering across iterative-deepening iterations (M4 step 3).
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
