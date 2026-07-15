#include "reversi/search.hpp"

#include "../support/benchmark_positions.hpp"
#include "../support/search_checks.hpp"
#include "reversi/cancellation.hpp"

#include <chrono>
#include <gtest/gtest.h>

namespace reversi {
namespace {

// The fail-high and fail-low paths tested EXPLICITLY, per plan - not just the happy window.
// searchWindow is the primitive searchIterative's aspiration loop is built on, so its bound
// semantics are checked directly and deterministically: the window is placed entirely below /
// around / entirely above the independently-known true score of every benchmark position.
TEST(SearchWindow, FailHighFailLowAndInWindowBoundSemantics) {
    for (const Position& pos : bench::benchmarkPositions()) {
        const int depth = 5;
        const int trueScore = search(pos, depth).score;

        // Happy path: true score strictly inside the window => exact score, optimal move.
        const SearchResult inWindow = searchWindow(pos, depth, trueScore - 4, trueScore + 4);
        EXPECT_EQ(inWindow.score, trueScore);
        EXPECT_EQ(checks::rootMoveValue(pos, inWindow.bestMove, depth), trueScore);

        // Fail high: whole window below the true score. Fail-soft must report a score at
        // least beta (that is what "failed high" means) but never above the true score (the
        // reported value must remain a valid LOWER bound - overshooting truth here is
        // exactly the "trusting a stale bound" bug class).
        const SearchResult failHigh = searchWindow(pos, depth, trueScore - 10, trueScore - 5);
        EXPECT_GE(failHigh.score, trueScore - 5);
        EXPECT_LE(failHigh.score, trueScore);

        // Fail low: whole window above the true score. Mirror-image: reported score at most
        // alpha, but never below the true score (a valid UPPER bound).
        const SearchResult failLow = searchWindow(pos, depth, trueScore + 5, trueScore + 10);
        EXPECT_LE(failLow.score, trueScore + 5);
        EXPECT_GE(failLow.score, trueScore);
    }
}

// The aspiration loop end to end: whatever mix of happy windows and re-searches a position's
// score trajectory causes across iterations, the final answer must be exactly the fixed-depth
// answer. Runs with a shared TT because that is the real configuration (and TT entries
// written by failed windows - Upper/Lower tagged - are exactly the lingering state this needs
// to digest correctly).
TEST(Aspiration, IterativeWithTTStillMatchesFixedDepthOnBenchmarkSet) {
    for (const Position& pos : bench::benchmarkPositions()) {
        const int depth = 6;
        const SearchResult fixed = search(pos, depth);
        TranspositionTable tt(std::size_t{1} << 18);
        const SearchResult iterative =
            searchIterative(pos, depth, evaluateDiscDifferential, nullptr, &tt);
        EXPECT_EQ(iterative.score, fixed.score);
        EXPECT_EQ(checks::rootMoveValue(pos, iterative.bestMove, depth), iterative.score);
    }
}

TEST(TimedSearch, ZeroSoftBudgetReturnsExactlyTheDepthOneAnswer) {
    // A soft deadline of "now" forbids starting iteration 2, so the result must be depth 1's
    // - which, with no TT and per-search-fresh ordering state, is bit-identical to a plain
    // depth-1 search.
    const Position pos = Position::start();
    const SearchResult timed = searchTimed(
        pos, 10, TimeBudget{std::chrono::milliseconds{0}, std::chrono::milliseconds{10'000}});
    const SearchResult fixed = search(pos, 1);
    EXPECT_TRUE(timed.completed);
    EXPECT_EQ(timed.depth, 1);
    EXPECT_EQ(timed.bestMove, fixed.bestMove);
    EXPECT_EQ(timed.score, fixed.score);
}

TEST(TimedSearch, HardDeadlineAbortsMidIterationAndFallsBackToACompleteAnswer) {
    // maxDepth 30 from an early-midgame position (high branching factor, so iterations get
    // expensive quickly) would take far beyond the budget; the hard deadline must abort the
    // in-flight iteration and hand back the deepest completed one, whose score must be
    // exactly the fixed-depth answer at its claimed depth (checked with the fast engine -
    // its scores are anchored by the baseline/unpruned cross-checks elsewhere - because the
    // reached depth is machine-dependent and the frozen baseline could take minutes there).
    // The generous wall-clock bound (25x the 150ms budget) only exists to prove the abort
    // mechanism fired at all without flaking on a loaded machine.
    const Position pos = bench::benchmarkPositions()[1];
    const auto start = std::chrono::steady_clock::now();
    TranspositionTable tt(std::size_t{1} << 18);
    const SearchResult timed = searchTimed(
        pos, 30, TimeBudget{std::chrono::milliseconds{150}, std::chrono::milliseconds{150}},
        evaluateDiscDifferential, nullptr, &tt);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, std::chrono::milliseconds{150 * 25});
    EXPECT_TRUE(timed.completed);
    EXPECT_GE(timed.depth, 1);
    EXPECT_LT(timed.depth, 30);
    EXPECT_EQ(timed.score, search(pos, timed.depth).score);
}

TEST(TimedSearch, PreCancelledTokenBeatsTheBudget) {
    CancellationToken token;
    token.requestStop();
    const SearchResult timed = searchTimed(
        Position::start(), 10,
        TimeBudget{std::chrono::milliseconds{10'000}, std::chrono::milliseconds{10'000}},
        evaluateDiscDifferential, &token);
    EXPECT_FALSE(timed.completed);
    EXPECT_EQ(timed.depth, 0);
    EXPECT_EQ(timed.bestMove, -1);
}

} // namespace
} // namespace reversi
