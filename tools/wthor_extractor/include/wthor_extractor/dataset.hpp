#pragma once

#include "wthor_extractor/parser.hpp"

#include <ostream>

namespace wthor {

// Writes the dataset file's self-describing header: one `%`-comment line per pattern class
// (shapeId, name, length, and 3^length state count) from reversi::pattern::allPatternClasses().
// This is the ONLY thing a reader (this project's own Python trainer, or anyone else) needs to
// know about pattern geometry - which squares make up a shape is never re-derived from the
// dataset, only consumed as already-computed indices. Call once, before any writeDatasetLine
// calls, on a freshly-opened output stream.
void writeDatasetHeader(std::ostream& out);

// Writes one sparse dataset line for a single sampled position:
//   <target score, mover-relative> <emptySquareCount> <shapeId:canonicalIndex>
//   <shapeId:canonicalIndex> ...
// Target score is computed via moverRelativeFinalScore from the position's own mover color and
// the game's true replayed final disc counts - never a second, independent read of the file's
// own reported score field (see moverRelativeFinalScore's doc comment for why). Empty-square
// count is written directly (not a pre-decided "phase bucket" index), deferring the bucket-
// boundary decision to training/eval time - re-bucketing later never requires re-extraction.
void writeDatasetLine(const reversi::Position& pos, bool posMoverIsBlack, int finalBlackDiscs,
                      int finalWhiteDiscs, std::ostream& out);

// Convenience: writes one dataset line per sampled position in `game` (every ReplayedPosition -
// one per real move actually played, matching ReplayedGame's own convention of not giving
// forced passes their own entry).
void writeDatasetLines(const ReplayedGame& game, std::ostream& out);

} // namespace wthor
