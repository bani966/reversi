#pragma once

#include "reversi/position.hpp"

#include <filesystem>
#include <vector>

namespace reversi::endgame {

struct FfoPosition {
    Position pos;
    int bestMove; // published best move, as a 0-63 square index (this project's own convention)
    int score;    // published exact score, from the mover's perspective (matches
                  // SearchResult::score's convention, so no sign-flipping is needed to compare)
};

// Parses the small text format used by the vendored FFO-style endgame test data
// (tests/data/ffo_easy.txt has the exact grammar and provenance in its own header comment):
// one position per line, blank lines and lines starting with '%' are comments/skipped.
// Line grammar: <64-char board, row-major a1..h8, X/O/-> <Black|White> <bestMove> <score>.
// Throws std::runtime_error if `path` cannot be opened. Malformed individual lines are skipped
// rather than throwing - defensive for a small, hand-curated data file, not a hardening
// requirement.
std::vector<FfoPosition> loadFfoPositions(const std::filesystem::path& path);

} // namespace reversi::endgame
