#include "reversi/move_selector.hpp"

#include <optional>

namespace reversi {

SearchResult selectMove(const Position& p, const EvalFn& eval, const MoveSelectorConfig& config,
                        const CancellationToken* cancellation, TranspositionTable* searchTt,
                        TranspositionTable* solverTt) {
    if (config.book != nullptr) {
        const std::optional<int> bookMove = config.book->lookup(p);
        if (bookMove.has_value()) {
            SearchResult result;
            result.bestMove = *bookMove;
            result.completed = true;
            return result;
        }
    }

    if (p.emptyCount() <= config.exactSolverEmptyThreshold) {
        return solveExact(p, cancellation, solverTt);
    }
    return searchTimed(p, config.maxDepth, config.budget, eval, cancellation, searchTt);
}

} // namespace reversi
