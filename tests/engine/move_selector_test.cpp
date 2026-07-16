#include "reversi/move_selector.hpp"

#include "../support/endgame_positions.hpp"
#include "reversi/eval.hpp"
#include "reversi/moves.hpp"
#include "reversi/mpc.hpp"
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

// mpcModel must reach the searchTimed branch: a synthetic "always cuts" model measurably
// changes the node count relative to the identical config with mpcModel left at its default
// nullptr, forcing the search branch on both sides (a position well above
// exactSolverEmptyThreshold) so the only difference between the two calls is whether MPC is
// threaded through at all. The assertion is direction-agnostic (NE, not LT) deliberately: at
// this shallow a depth, an MPC cut inside a PVS zero-window probe returns a value sitting
// exactly on the window boundary, which can itself trigger PVS's own "the probe suggested an
// improvement, re-search with the full window" branch (search.cpp) - a real, legitimate
// interaction between two independent techniques, not a bug, but it means node count isn't
// guaranteed to strictly decrease at every depth/window combination. mpc_search_test.cpp
// already proves the strict, large-scale node reduction at a realistic fixed depth (7); this
// test's only job is confirming the wiring, not re-proving that.
//
// A SINGLE pair (deep=1, shallow=0) is used deliberately, not a densely-chained range: MPC's
// own shallow probes always use a FULL (-inf, +inf) window internally (matching how training
// data is generated - see search.cpp), so a probe can never cut ITSELF unless its shallow
// depth is 0 (which returns eval() directly, bypassing the MPC check entirely - see negamax's
// depth==0 early return). Chaining multiple covered depths together (deep -> shallow also
// covered -> shallower still covered...) makes each probe's own full-window sub-probe fall
// through to real exploration instead of cutting, adding MORE total work than no MPC at all -
// a real, surprising finding hit while writing this test, not merely a hypothetical: an
// earlier version of this test covered depths 1..6 with reduction 1 and observed MORE nodes
// WITH the "always cuts" model than without (2708 vs 1349) for exactly this reason. A single
// shallow=0 pair sidesteps that specific issue, though not the PVS-re-search interaction above.
TEST(SelectMove, MpcModelIsThreadedThroughToTheSearchBranch) {
    const Position pos = Position::start();

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "move_selector_test_mpc_always.bin";
    {
        std::ofstream out(path, std::ios::binary);
        const auto writeU32 = [&out](std::uint32_t v) {
            const std::array<char, 4> bytes = {
                static_cast<char>(v & 0xFFU), static_cast<char>((v >> 8U) & 0xFFU),
                static_cast<char>((v >> 16U) & 0xFFU), static_cast<char>((v >> 24U) & 0xFFU)};
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        };
        const auto writeI32 = [&](int v) { writeU32(static_cast<std::uint32_t>(v)); };
        const auto writeF32 = [&](float v) { writeU32(std::bit_cast<std::uint32_t>(v)); };
        writeU32(1);       // pairCount
        writeI32(1);       // deepDepth
        writeI32(0);       // shallowDepth
        writeF32(1000.0F); // a
        writeF32(0.0F);    // b
        writeF32(0.001F);  // sigma
    }
    const MpcModel model(path);

    MoveSelectorConfig withoutMpc;
    withoutMpc.exactSolverEmptyThreshold = 0; // forces the search branch
    withoutMpc.maxDepth = 2; // depth-2 iteration's ply-1 nodes have remaining depth 1 - covered
    withoutMpc.budget =
        TimeBudget{std::chrono::milliseconds{2000}, std::chrono::milliseconds{5000}};
    const SearchResult without = selectMove(pos, evaluateDiscDifferential, withoutMpc);

    MoveSelectorConfig withMpc = withoutMpc;
    withMpc.mpcModel = &model;
    withMpc.mpcT = 2.0;
    const SearchResult with = selectMove(pos, evaluateDiscDifferential, withMpc);

    EXPECT_TRUE(without.completed);
    EXPECT_TRUE(with.completed);
    EXPECT_NE(with.nodes, without.nodes);

    std::filesystem::remove(path);
}

} // namespace
} // namespace reversi
