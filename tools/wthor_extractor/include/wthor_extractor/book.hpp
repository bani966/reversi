#pragma once

#include "wthor_extractor/parser.hpp"

#include "reversi/position.hpp"

#include <cstdint>
#include <map>
#include <ostream>
#include <utility>
#include <vector>

namespace wthor {

// Aggregated observations for one candidate move at one canonical position.
struct BookMoveStats {
    unsigned gameCount = 0;
    int outcomeSum = 0; // sum of mover-relative outcomes (moverRelativeFinalScore) observed
};

// canonical (own, opp) -> move -> aggregated stats. A std::map (not unordered_map) so that
// iterating it during finalizeBook already visits entries in ascending (own, opp) order,
// matching the on-disk format's required sort order for free.
using BookAccumulator =
    std::map<std::pair<reversi::Bitboard, reversi::Bitboard>, std::map<int, BookMoveStats>>;

// Folds one replayed game's positions into `acc`. Only positions within the opening-book ply
// window are considered: ply = pos.discCount() - 4 (discCount starts at 4, at Position::start(),
// and increases by exactly 1 per real move played, so this is literally "how many real moves
// have been played so far") must be <= maxPly. Both the position and the move actually played
// from it are canonicalized via reversi::pattern::canonicalize/applySymmetry BEFORE being folded
// into `acc` - this is the book's "build time" half of the directional contract documented in
// pattern.hpp's canonicalize() doc comment: store applySymmetry(symmetryUsed, move) against the
// canonical position, never the raw move against the raw position. Uses zero new WTHOR-reading
// code - `game` must already come from wthor::replayGame.
void accumulateBookGame(const ReplayedGame& game, int maxPly, BookAccumulator& acc);

// One finalized opening-book entry: the single best move recorded for one canonical position.
struct BookEntry {
    reversi::Bitboard own = 0;
    reversi::Bitboard opp = 0;
    int move = 0;
    unsigned gameCount = 0; // how many games agreed on THIS move (not total observations)
    int outcomeSum = 0;     // sum of mover-relative outcomes for THIS move, for future tuning
};

// Finalizes an accumulator into book entries, ascending by (own, opp) (BookAccumulator's own
// iteration order already guarantees this - see its doc comment). Skips any canonical position
// whose TOTAL observation count (summed across all candidate moves) is below `minCount`. Among
// a kept position's candidate moves, picks the one maximizing gameCount; ties are broken by
// higher average outcome (outcomeSum / gameCount, as a real division - not the raw sum, since a
// move played in more low-margin games can otherwise unfairly outrank a move played in fewer
// high-margin ones).
std::vector<BookEntry> finalizeBook(const BookAccumulator& acc, unsigned minCount);

// Writes `entries` (which MUST already be sorted ascending by (own, opp) - finalizeBook's own
// output already is) to the on-disk binary opening-book format, dependency-free (no external
// serialization library), matching engine/include/reversi/opening_book.hpp's reader exactly:
//   u32 entryCount
//   for each entry, ascending by (own, opp):
//       u64 own
//       u64 opp
//       i32 move
//       u32 gameCount
//       i32 outcomeSum
// All integers little-endian, matching this project's existing weight-file format convention
// (tools/train_pattern_eval.py / engine/pattern_eval.cpp).
void writeBookFile(const std::vector<BookEntry>& entries, std::ostream& out);

} // namespace wthor
