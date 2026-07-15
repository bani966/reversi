#include "reversi/search.hpp"

#include "reversi/moves.hpp"

#include <bit>

namespace reversi {

namespace {

// Far enough from the true score range (disc differentials live in [-64, 64]) that negating
// it can never overflow, unlike negating INT_MIN.
constexpr int kInfinity = 1'000'000;

int negamax(const Position& pos, int depth, int alpha, int beta, const EvalFn& eval,
            std::uint64_t& nodes) {
    ++nodes;
    const Bitboard moves = legalMoves(pos);
    if (moves == 0) {
        const Position passed = applyPass(pos);
        if (!hasLegalMove(passed)) {
            return terminalScore(pos); // neither side can move: game over
        }
        if (depth == 0) {
            return eval(pos);
        }
        // Forced pass: doesn't consume depth, see search.hpp.
        return -negamax(passed, depth, -beta, -alpha, eval, nodes);
    }
    if (depth == 0) {
        return eval(pos);
    }
    int best = -kInfinity;
    for (Bitboard b = moves; b != 0; b &= b - 1) {
        const int square = std::countr_zero(b);
        const int score =
            -negamax(applyMove(pos, square), depth - 1, -beta, -alpha, eval, nodes);
        if (score > best) {
            best = score;
        }
        if (best > alpha) {
            alpha = best;
        }
        if (alpha >= beta) {
            break; // fail-soft: `best` is returned as-is, not clamped to beta
        }
    }
    return best;
}

} // namespace

SearchResult search(const Position& p, int depth, const EvalFn& eval) {
    const Bitboard moves = legalMoves(p);
    SearchResult result;
    int alpha = -kInfinity;
    const int beta = kInfinity;
    for (Bitboard b = moves; b != 0; b &= b - 1) {
        const int square = std::countr_zero(b);
        const int score =
            -negamax(applyMove(p, square), depth - 1, -beta, -alpha, eval, result.nodes);
        if (result.bestMove == -1 || score > result.score) {
            result.score = score;
            result.bestMove = square;
        }
        if (result.score > alpha) {
            alpha = result.score;
        }
    }
    return result;
}

} // namespace reversi
