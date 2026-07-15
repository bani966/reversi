#include "reversi/search.hpp"

#include "../support/benchmark_positions.hpp"
#include "reversi/cancellation.hpp"
#include "reversi/position.hpp"

#include <cstdint>
#include <gtest/gtest.h>

namespace reversi {
namespace {

// The M4-step-1 correctness contract: iterative deepening is a scheduling change, not an
// algorithmic one, so at a given final depth its chosen move and score must exactly match
// plain fixed-depth alpha-beta on the same position (the M4 analogue of M2's
// alpha-beta-vs-unpruned-negamax cross-check). Strict move equality (not just score) is
// deliberate and remains valid even after M4 step 3's move ordering: killers/history are
// rebuilt fresh inside every search() call, so without a TT nothing carries over between
// iterations and ID's final iteration is literally the same invocation as the fixed-depth
// call. (With a shared TT, hints DO carry over and shift tie-breaks - that path is covered
// with the appropriately relaxed check in tt_test.cpp instead.)
TEST(IterativeSearch, MatchesFixedDepthExactlyOnBenchmarkSet) {
    ASSERT_GE(bench::benchmarkPositions().size(), std::size_t{15});
    for (const Position& pos : bench::benchmarkPositions()) {
        for (int depth = 1; depth <= 5; ++depth) {
            const SearchResult fixed = search(pos, depth);
            const SearchResult iterative = searchIterative(pos, depth);
            EXPECT_EQ(iterative.bestMove, fixed.bestMove) << "depth " << depth;
            EXPECT_EQ(iterative.score, fixed.score) << "depth " << depth;
            EXPECT_EQ(iterative.depth, depth);
            EXPECT_TRUE(iterative.completed);
        }
    }
}

TEST(IterativeSearch, NodesAccumulateAcrossIterations) {
    const SearchResult fixed = search(Position::start(), 5);
    const SearchResult iterative = searchIterative(Position::start(), 5);
    // The final iteration alone visits exactly `fixed.nodes`; the warm-up iterations must
    // account for strictly more on top (no TT yet, so no work is shared between iterations).
    EXPECT_GT(iterative.nodes, fixed.nodes);
}

TEST(IterativeSearch, PreCancelledSearchReportsNoCompletedDepth) {
    CancellationToken token;
    token.requestStop();
    const SearchResult result =
        searchIterative(Position::start(), 6, evaluateDiscDifferential, &token);
    EXPECT_FALSE(result.completed);
    EXPECT_EQ(result.depth, 0);
    EXPECT_EQ(result.bestMove, -1);
}

// Deterministic mid-search cancellation, no thread/timing flakiness: first measure how many
// eval calls the depth-1 and depth-2 iterations make, then arm an eval that requests a stop
// as soon as the count enters the depth-3 iteration. Depths 1-2 complete untouched, depth 3
// aborts, and the driver must hand back the full depth-2 answer - the graceful-degradation
// property that is iterative deepening's whole reason to exist.
TEST(IterativeSearch, MidIterationCancellationFallsBackToDeepestCompletedDepth) {
    const Position pos = Position::start();
    const auto countEvals = [&pos](int depth) {
        std::uint64_t count = 0;
        const EvalFn counting = [&count](const Position& p) {
            ++count;
            return evaluateDiscDifferential(p);
        };
        search(pos, depth, counting);
        return count;
    };
    const std::uint64_t evalsThroughDepth2 = countEvals(1) + countEvals(2);

    CancellationToken token;
    std::uint64_t count = 0;
    const EvalFn tripping = [&count, &token, evalsThroughDepth2](const Position& p) {
        if (++count > evalsThroughDepth2) {
            token.requestStop();
        }
        return evaluateDiscDifferential(p);
    };
    const SearchResult result = searchIterative(pos, 6, tripping, &token);
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.depth, 2);
    const SearchResult depth2 = search(pos, 2);
    EXPECT_EQ(result.bestMove, depth2.bestMove);
    EXPECT_EQ(result.score, depth2.score);
}

} // namespace
} // namespace reversi
