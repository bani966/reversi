#include "reversi/search.hpp"

#include "../support/benchmark_positions.hpp"
#include "../support/search_checks.hpp"
#include "reversi/moves.hpp"

#include <chrono>
#include <gtest/gtest.h>

namespace reversi {
namespace {

// The load-bearing correctness test for this milestone: extends tt_test.cpp's own
// "TTSearch.SearchWithTTMatchesSearchWithoutOnBenchmarkSet" argument ("a correct table is a
// pure time-saver, never changes the score") to the concurrent case. Any TT entry that passes
// SharedTranspositionTable::probe()'s validation is a genuinely-computed true result for its
// exact position/depth, from WHATEVER thread computed it (see shared_tt.hpp's own doc comment)
// - trusting it can only save work, never change the answer. A generous time budget (30s soft /
// 60s hard) at small fixed depths means the budget never actually triggers early - every thread
// deterministically completes exactly through `depth`, so this is a clean depth-for-depth
// comparison, not muddied by wall-clock timing.
TEST(SearchLazySmp, ScoreAtAFixedDepthMatchesSingleThreadedSearchOnBenchmarkSet) {
    constexpr TimeBudget kGenerousBudget{std::chrono::seconds{30}, std::chrono::seconds{60}};
    for (const Position& pos : bench::benchmarkPositions()) {
        for (int depth = 1; depth <= 5; ++depth) {
            const SearchResult baseline = search(pos, depth);
            SharedTranspositionTable sharedTt(std::size_t{1} << 16);
            const SearchResult lazy = searchLazySmp(pos, depth, kGenerousBudget, /*threadCount=*/4,
                                                    evaluateDiscDifferential, nullptr, &sharedTt);
            EXPECT_TRUE(lazy.completed) << "depth " << depth;
            EXPECT_EQ(lazy.score, baseline.score) << "depth " << depth;
            EXPECT_EQ(checks::rootMoveValue(pos, lazy.bestMove, depth), lazy.score)
                << "depth " << depth;
        }
    }
}

// threadCount <= 1 must behave deterministically and reasonably (no threading machinery
// exercised at all - see search.hpp's own doc comment) - same fixed-depth comparison as above,
// specifically for the degenerate single-thread path.
TEST(SearchLazySmp, ThreadCountOneMatchesSingleThreadedSearchOnBenchmarkSet) {
    constexpr TimeBudget kGenerousBudget{std::chrono::seconds{30}, std::chrono::seconds{60}};
    for (const Position& pos : bench::benchmarkPositions()) {
        for (const int depth : {1, 3, 5}) {
            const SearchResult baseline = search(pos, depth);
            SharedTranspositionTable sharedTt(std::size_t{1} << 16);
            const SearchResult lazy =
                searchLazySmp(pos, depth, kGenerousBudget,
                              /*threadCount=*/1, evaluateDiscDifferential, nullptr, &sharedTt);
            EXPECT_TRUE(lazy.completed) << "depth " << depth;
            EXPECT_EQ(lazy.score, baseline.score) << "depth " << depth;
        }
    }
}

// No-crash/repeatability check across several thread counts under a real, short wall-clock
// budget (the realistic usage shape - iterative deepening to whatever depth the budget allows,
// not a fixed small depth). Reuses one shared table across positions/runs (clear()'d between
// each, matching how a real caller would reuse one table across a game) to also exercise the
// table under genuinely repeated concurrent use, not just a single call.
TEST(SearchLazySmp, RunsRepeatablyAcrossSeveralThreadCountsWithoutCrashing) {
    constexpr TimeBudget kShortBudget{std::chrono::milliseconds{20}, std::chrono::milliseconds{60}};
    SharedTranspositionTable sharedTt(std::size_t{1} << 16);
    for (const int threadCount : {2, 4, 8}) {
        for (int run = 0; run < 2; ++run) {
            for (const Position& pos : bench::benchmarkPositions()) {
                sharedTt.clear();
                const SearchResult result =
                    searchLazySmp(pos, 60, kShortBudget, threadCount, evaluateDiscDifferential,
                                  nullptr, &sharedTt);
                EXPECT_TRUE(result.completed) << "threadCount " << threadCount;
                ASSERT_NE(result.bestMove, -1) << "threadCount " << threadCount;
                EXPECT_NE((legalMoves(pos) & bit(result.bestMove)), Bitboard{0})
                    << "threadCount " << threadCount;
            }
        }
    }
}

// result.nodes for threadCount > 1 is the SUM across all threads (see search.hpp's own doc
// comment on why) - must be strictly greater than what a single thread alone would report for
// the same position/budget, since every additional thread contributes real, non-zero work.
TEST(SearchLazySmp, NodesAreSummedAcrossThreadsNotJustThread0s) {
    constexpr TimeBudget kBudget{std::chrono::milliseconds{100}, std::chrono::milliseconds{300}};
    const Position& pos = bench::benchmarkPositions().front();

    SharedTranspositionTable soloTt(std::size_t{1} << 16);
    const SearchResult solo = searchLazySmp(pos, 60, kBudget, /*threadCount=*/1,
                                            evaluateDiscDifferential, nullptr, &soloTt);

    SharedTranspositionTable groupTt(std::size_t{1} << 16);
    const SearchResult group = searchLazySmp(pos, 60, kBudget, /*threadCount=*/8,
                                             evaluateDiscDifferential, nullptr, &groupTt);

    EXPECT_GT(group.nodes, solo.nodes);
}

} // namespace
} // namespace reversi
