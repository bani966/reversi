#include "reversi/solver.hpp"

#include "../support/ffo.hpp"

#include <filesystem>
#include <gtest/gtest.h>

namespace reversi {
namespace {

// M5's actual exit criterion, per README: "FFO test positions solved with correct exact
// scores." tests/data/ffo_easy.txt has the full provenance/format details in its own header
// comment - a small, hand-vendored subset (8 positions, fetched directly rather than
// reconstructed from memory) of genuine FFO-format Othello endgame test data with published
// exact scores.
TEST(Ffo, SolveExactMatchesPublishedScores) {
    const std::filesystem::path dataFile =
        std::filesystem::path(REVERSI_TEST_DATA_DIR) / "ffo_easy.txt";
    const std::vector<endgame::FfoPosition> positions = endgame::loadFfoPositions(dataFile);
    ASSERT_GE(positions.size(), std::size_t{5}) << "expected the vendored ffo_easy.txt subset";
    for (const endgame::FfoPosition& ffo : positions) {
        const SearchResult result = solveExact(ffo.pos);
        EXPECT_TRUE(result.completed);
        EXPECT_EQ(result.score, ffo.score) << "emptyCount=" << ffo.pos.emptyCount();
    }
}

} // namespace
} // namespace reversi
