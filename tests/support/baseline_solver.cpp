#include "baseline_solver.hpp"

#include "reversi/moves.hpp"

#include <bit>

namespace reversi::baseline {

namespace {

constexpr int kInfinity = 1'000'000;

int negamax(const Position& pos, int alpha, int beta, TranspositionTable* tt,
            std::uint64_t& nodes) {
    ++nodes;
    const Bitboard moves = legalMoves(pos);
    if (moves == 0) {
        const Position passed = applyPass(pos);
        if (!hasLegalMove(passed)) {
            return terminalScore(pos);
        }
        return -negamax(passed, -beta, -alpha, tt, nodes);
    }

    const std::uint64_t hash = tt != nullptr ? zobristHash(pos) : 0;
    if (tt != nullptr) {
        if (const TTEntry* entry = tt->probe(hash); entry != nullptr) {
            switch (entry->bound) {
            case Bound::Exact:
                return entry->score;
            case Bound::Lower:
                alpha = alpha > entry->score ? alpha : entry->score;
                break;
            case Bound::Upper:
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
    bool first = true;
    for (Bitboard b = moves; b != 0; b &= b - 1) {
        const int square = std::countr_zero(b);
        const Position child = applyMove(pos, square);
        int score;
        if (first) {
            score = -negamax(child, -beta, -alpha, tt, nodes);
            first = false;
        } else {
            score = -negamax(child, -alpha - 1, -alpha, tt, nodes);
            if (score > alpha && score < beta) {
                score = -negamax(child, -beta, -alpha, tt, nodes);
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
            break;
        }
    }

    if (tt != nullptr) {
        const Bound bound = best <= alphaOriginal ? Bound::Upper
                            : best >= beta        ? Bound::Lower
                                                  : Bound::Exact;
        tt->store(hash, pos.emptyCount(), best, bound, bestSquare);
    }
    return best;
}

} // namespace

BaselineResult solveExact(const Position& p, TranspositionTable* tt) {
    const Bitboard moves = legalMoves(p);
    BaselineResult result;
    int alpha = -kInfinity;
    const int beta = kInfinity;
    bool first = true;
    for (Bitboard b = moves; b != 0; b &= b - 1) {
        const int square = std::countr_zero(b);
        const Position child = applyMove(p, square);
        int score;
        if (first) {
            score = -negamax(child, -beta, -alpha, tt, result.nodes);
            first = false;
        } else {
            score = -negamax(child, -alpha - 1, -alpha, tt, result.nodes);
            if (score > alpha) {
                score = -negamax(child, -beta, -alpha, tt, result.nodes);
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
    if (tt != nullptr) {
        tt->store(zobristHash(p), p.emptyCount(), result.score, Bound::Exact, result.bestMove);
    }
    return result;
}

} // namespace reversi::baseline
