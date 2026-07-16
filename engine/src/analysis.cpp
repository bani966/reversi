#include "reversi/analysis.hpp"

#include "reversi/moves.hpp"

#include <bit>

namespace reversi {

std::vector<RankedMove> analyzeTopMoves(const Position& p, int maxLines, int maxDepth,
                                        const TimeBudget& perLineBudget, const EvalFn& eval,
                                        const CancellationToken* cancellation,
                                        TranspositionTable* tt) {
    std::vector<RankedMove> results;
    std::vector<int> excluded;
    const int legalCount = std::popcount(legalMoves(p));
    if (maxLines <= 0 || legalCount == 0) {
        return results;
    }

    // Pass 0 is the only time-budgeted pass. It sets the depth every later pass must match
    // EXACTLY (via the fixed-depth searchExcludingMoves, not another time-budgeted call) -
    // excluding moves shrinks the root's branching factor, which under a wall-clock budget alone
    // would let a later pass's iterative deepening reach a DEEPER completed iteration than pass 0
    // did in the same wall-clock time. Comparing scores from different depths isn't a real
    // strength ranking, just an artifact of unequal search depth (caught by manual GUI testing:
    // an earlier version of this function occasionally ranked a WORSE-scoring but MORE-deeply-
    // searched move above a better-scoring but shallower one). Fixing every later pass to pass
    // 0's own achieved depth makes every ranked line's score genuinely comparable.
    const SearchResult first =
        searchTimedExcludingMoves(p, maxDepth, perLineBudget, excluded, eval, cancellation, tt);
    if (!first.completed) {
        return results;
    }
    results.push_back({first.bestMove, first.score, first.depth, first.nodes});
    excluded.push_back(first.bestMove);

    for (int i = 1; i < maxLines && static_cast<int>(excluded.size()) < legalCount; ++i) {
        const SearchResult result =
            searchExcludingMoves(p, first.depth, excluded, eval, cancellation, tt);
        if (!result.completed) {
            break; // don't report a partial pass - mirrors SearchResult::completed's own contract
        }
        results.push_back({result.bestMove, result.score, result.depth, result.nodes});
        excluded.push_back(result.bestMove);
    }
    return results;
}

std::vector<int> extractPrincipalVariation(const Position& p, int firstMove,
                                           TranspositionTable& tt, int maxLength) {
    std::vector<int> pv;
    if (maxLength <= 0) {
        return pv;
    }
    pv.push_back(firstMove);
    Position pos = applyMove(p, firstMove);
    while (static_cast<int>(pv.size()) < maxLength) {
        if (isGameOver(pos)) {
            break;
        }
        if (!hasLegalMove(pos)) {
            pos = applyPass(pos); // forced pass: doesn't consume a ply slot in the PV itself
            continue;
        }
        const TTEntry* entry = tt.probe(zobristHash(pos));
        if (entry == nullptr || entry->bestMove == -1) {
            break;
        }
        pv.push_back(entry->bestMove);
        pos = applyMove(pos, entry->bestMove);
    }
    return pv;
}

} // namespace reversi
