#pragma once

#include "reversi/eval.hpp"
#include "reversi/position.hpp"

#include <cstdint>

namespace reversi {

struct SearchResult {
    int bestMove = -1;
    int score = 0;
    std::uint64_t nodes = 0;
};

// Fixed-depth negamax with fail-soft alpha-beta pruning. `depth` counts real moves only: a
// forced pass re-searches at the *same* depth (see search.cpp) rather than consuming one,
// since a pass isn't a decision worth spending look-ahead budget on — this is a different,
// independent convention from perft's "a pass consumes a ply" (see perft.hpp), which exists
// to match a published node-counting convention rather than to describe search intent.
// `nodes` counts negamax invocations below the root, for reporting nps.
// Precondition: hasLegalMove(p). The caller checks this / applies forced passes itself
// before calling search — search never needs to pick a move for a position that has none.
SearchResult search(const Position& p, int depth, const EvalFn& eval = evaluateDiscDifferential);

} // namespace reversi
