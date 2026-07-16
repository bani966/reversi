#include "reversi/move_selector.hpp"

#include "../support/endgame_positions.hpp"
#include "reversi/eval.hpp"
#include "reversi/moves.hpp"
#include "reversi/opening_book.hpp"
#include "reversi/pattern.hpp"
#include "reversi/solver.hpp"
#include "reversi/tt.hpp"

#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace reversi {
namespace {

// A local, independent binary writer for a single-entry opening book - same discipline as
// tests/engine/opening_book_test.cpp's testbook namespace (engine/ tests must not depend on
// tools/, which is opt-in). Stores the CANONICAL image of `pos`/`move` (pattern::canonicalize +
// applySymmetry), so this works regardless of which symmetry `pos` happens to canonicalize
// through - the test doesn't need `pos` to already be canonical.
std::filesystem::path writeSingleEntryBook(const Position& pos, int move, const char* name) {
    const pattern::Canonicalized canonical = pattern::canonicalize(pos);
    const int canonicalMove = pattern::applySymmetry(canonical.symmetryUsed, move);

    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path, std::ios::binary);
    const auto writeU32 = [&out](std::uint32_t v) {
        const std::array<char, 4> bytes = {
            static_cast<char>(v & 0xFFU), static_cast<char>((v >> 8U) & 0xFFU),
            static_cast<char>((v >> 16U) & 0xFFU), static_cast<char>((v >> 24U) & 0xFFU)};
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    };
    const auto writeU64 = [&](std::uint64_t v) {
        writeU32(static_cast<std::uint32_t>(v & 0xFFFFFFFFULL));
        writeU32(static_cast<std::uint32_t>((v >> 32U) & 0xFFFFFFFFULL));
    };
    writeU32(1); // entryCount
    writeU64(canonical.position.own);
    writeU64(canonical.position.opp);
    writeU32(static_cast<std::uint32_t>(canonicalMove)); // move (i32, always non-negative here)
    writeU32(1);                                         // gameCount
    writeU32(0);                                         // outcomeSum
    return path;
}

// A book hit must short-circuit BEFORE the threshold check: picks a low-empty-count position
// that would otherwise definitely take the solveExact branch (well under the default
// threshold), stores a book entry for it, and confirms selectMove returns the book's move
// untouched - with the documented book-hit signature (depth=0, nodes=0) and WITHOUT ever
// invoking `eval` (a counting wrapper - same technique as pattern_eval_test.cpp's
// commensurability tests). Neither solveExact nor searchTimed take an eval callback for the
// solver branch, so pairing the counting eval with a position that would otherwise hit the
// solver branch closes both gaps at once.
TEST(SelectMove, BookHitShortCircuitsBeforeSolverOrSearchAndNeverInvokesEval) {
    const auto sample = endgame::collectPositionsByEmptyCount(4242u, 8, 10);
    ASSERT_FALSE(sample.empty());
    const Position pos = sample.front();
    const int legalMove = std::countr_zero(legalMoves(pos));

    const std::filesystem::path bookPath =
        writeSingleEntryBook(pos, legalMove, "move_selector_test_book_hit.bin");
    const OpeningBook book(bookPath);

    int callCount = 0;
    const EvalFn countingEval = [&](const Position& p) {
        ++callCount;
        return evaluateDiscDifferential(p);
    };

    MoveSelectorConfig config;
    config.book = &book;
    ASSERT_LE(pos.emptyCount(), config.exactSolverEmptyThreshold)
        << "test setup: this position must be within the solver's normal range for the "
           "short-circuit-before-threshold claim to mean anything";

    const SearchResult result = selectMove(pos, countingEval, config);
    EXPECT_EQ(result.bestMove, legalMove);
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.depth, 0);
    EXPECT_EQ(result.nodes, std::uint64_t{0});
    EXPECT_EQ(callCount, 0);

    std::filesystem::remove(bookPath);
}

// Same position, threshold flipped across its emptyCount() boundary: no book, so this is a
// pure test of the emptyCount() <= exactSolverEmptyThreshold dispatch. solveExact's own
// contract (solver.cpp) sets depth == emptyCount() exactly when completed; searchTimed with
// maxDepth=1 can never report depth > 1. These are unambiguous, non-overlapping signatures.
TEST(SelectMove, DispatchesToSolverAtOrBelowThresholdAndToSearchAboveIt) {
    const auto sample = endgame::collectPositionsByEmptyCount(4242u, 8, 8);
    ASSERT_FALSE(sample.empty());
    const Position pos = sample.front();
    ASSERT_EQ(pos.emptyCount(), 8);

    MoveSelectorConfig solverConfig;
    solverConfig.exactSolverEmptyThreshold = 8; // >= emptyCount(): solver branch
    solverConfig.maxDepth = 1;                  // would betray a search-branch bug immediately
    const SearchResult solved = selectMove(pos, evaluateDiscDifferential, solverConfig);
    EXPECT_TRUE(solved.completed);
    EXPECT_EQ(solved.depth, 8);

    MoveSelectorConfig searchConfig;
    searchConfig.exactSolverEmptyThreshold = 7; // < emptyCount(): search branch
    searchConfig.maxDepth = 1;
    searchConfig.budget =
        TimeBudget{std::chrono::milliseconds{500}, std::chrono::milliseconds{2000}};
    const SearchResult searched = selectMove(pos, evaluateDiscDifferential, searchConfig);
    EXPECT_TRUE(searched.completed);
    EXPECT_LE(searched.depth, 1);
}

// searchTt and solverTt really are used independently: after forcing the solver branch, the
// root position's entry must land in solverTt (solveExact explicitly stores its root result -
// see solver.cpp) and searchTt must remain completely untouched (still a miss) - and
// symmetrically for the search branch. Directly probing each table for the root's own hash is
// a stronger check than merely trusting the two calls "didn't crash": it proves which table
// actually received data.
TEST(SelectMove, SearchTtAndSolverTtAreUsedIndependently) {
    const auto sample = endgame::collectPositionsByEmptyCount(4242u, 8, 8);
    ASSERT_FALSE(sample.empty());
    const Position pos = sample.front();
    const std::uint64_t rootHash = zobristHash(pos);

    {
        TranspositionTable searchTt(1024);
        TranspositionTable solverTt(1024);
        MoveSelectorConfig config;
        config.exactSolverEmptyThreshold = 8; // forces the solver branch for this position
        selectMove(pos, evaluateDiscDifferential, config, /*cancellation=*/nullptr, &searchTt,
                   &solverTt);
        EXPECT_NE(solverTt.probe(rootHash), nullptr)
            << "solveExact must store the root in solverTt";
        EXPECT_EQ(searchTt.probe(rootHash), nullptr)
            << "searchTt must stay untouched by the solver";
    }
    {
        TranspositionTable searchTt(1024);
        TranspositionTable solverTt(1024);
        MoveSelectorConfig config;
        config.exactSolverEmptyThreshold = 7; // forces the search branch for this position
        config.maxDepth = 2;
        config.budget = TimeBudget{std::chrono::milliseconds{500}, std::chrono::milliseconds{2000}};
        selectMove(pos, evaluateDiscDifferential, config, /*cancellation=*/nullptr, &searchTt,
                   &solverTt);
        EXPECT_NE(searchTt.probe(rootHash), nullptr)
            << "searchTimed must store the root in searchTt";
        EXPECT_EQ(solverTt.probe(rootHash), nullptr)
            << "solverTt must stay untouched by the search";
    }
}

} // namespace
} // namespace reversi
