#pragma once

#include "reversi/cancellation.hpp"
#include "reversi/eval.hpp"
#include "reversi/position.hpp"
#include "reversi/search.hpp"
#include "reversi/tt.hpp"

#include <cstdint>
#include <vector>

// On-demand position analysis (M9 phase 3): "how do several candidate moves compare," a
// distinct question from move_selector.hpp's selectMove() ("what move should actually be
// played"), so this is its own composition point rather than folded into that one - the same
// one-concern-per-header granularity already used for pattern.hpp/mpc.hpp/opening_book.hpp.
namespace reversi {

struct RankedMove {
    int move = -1;
    int score = 0; // mover-relative, terminalScore()-scale, exactly like SearchResult::score
    int depth = 0;
    std::uint64_t nodes = 0;
};

// Ranks up to `maxLines` legal moves of `p`: pass 0 finds the best move outright via
// searchTimedExcludingMoves() (time-budgeted); pass i (i >= 1) excludes every move already
// ranked in passes 0..i-1 and searches via the FIXED-DEPTH searchExcludingMoves(), locked to
// EXACTLY pass 0's own completed depth - not a second time-budgeted call. This deliberately does
// NOT give every pass the same time budget: excluding moves shrinks the root's branching factor,
// which under a wall-clock budget alone lets a later pass's iterative deepening reach a DEEPER
// completed iteration than pass 0 did in the same wall-clock time - and scores from different
// depths aren't a real strength ranking, just an artifact of unequal search depth (a real defect
// caught by manual GUI testing during phase 3's own development: an earlier version of this
// function occasionally ranked a WORSE-scoring but more-deeply-searched move above a
// better-scoring but shallower one). Locking every later pass to pass 0's achieved depth makes
// every ranked line's score genuinely comparable. Neither pass touches search()/searchTimed()'s
// own internals - both searchExcludingMoves()/searchTimedExcludingMoves() are pure sibling
// functions (search.hpp), and this is a pure orchestration layer above them.
//
// `tt` is reused across all N passes of one call (fine and beneficial: exclusion only changes
// which root move CAN be picked, never what's stored/read for child positions) - but should be a
// table dedicated to analysis, never shared with a live game's own move-search table, since this
// typically runs on its own thread (see GameController's analysisTt_).
//
// Returns fewer than maxLines entries if p has fewer legal moves, or if a pass is cancelled/times
// out before completing (an incomplete pass is dropped, not included partially - mirrors
// SearchResult::completed's own "don't trust an incomplete result" contract).
//
// Never consults an opening book or the exact endgame solver, regardless of emptyCount() - always
// ranks via heuristic search. Blending book/solver awareness into MultiPV ranking is a real
// future enhancement, not attempted here.
//
// Precondition: hasLegalMove(p) (same as searchTimed's own precondition).
std::vector<RankedMove> analyzeTopMoves(const Position& p, int maxLines, int maxDepth,
                                        const TimeBudget& perLineBudget,
                                        const EvalFn& eval = evaluateDiscDifferential,
                                        const CancellationToken* cancellation = nullptr,
                                        TranspositionTable* tt = nullptr);

// Walks `tt` from the position reached by playing `firstMove` on `p`, following each ply's
// stored best move (TTEntry::bestMove), up to maxLength total plies (including firstMove
// itself). Stops early on a TT miss, a stored bestMove of -1, or game-over. Never mutates tt's
// contents - `tt` is non-const only because TranspositionTable::probe() itself is (it counts
// hits; see tt.hpp's own doc comment). A forced pass doesn't end the walk (mirrors
// GameController::advanceTurn()'s own pass-resolution): the side with no legal move simply
// doesn't consume a ply.
//
// Deliberately takes `firstMove` as an explicit parameter rather than probing `tt` for `p`
// itself: after analyzeTopMoves()'s N passes all store into the SAME tt keyed by `p`,
// TranspositionTable::store's "a same-key store always updates" rule means the entry for `p`
// reflects whichever pass ran LAST (typically the worst-ranked line), not pass 0's actual best
// move. Seeding the walk with pass 0's own already-known bestMove and reading the TT only from
// the CHILD position onward sidesteps this: child subtrees explored during pass 0 keep their own
// correct entries regardless of which pass ran last at the root.
//
// Precondition: firstMove is a legal move of p.
std::vector<int> extractPrincipalVariation(const Position& p, int firstMove, TranspositionTable& tt,
                                           int maxLength);

} // namespace reversi
