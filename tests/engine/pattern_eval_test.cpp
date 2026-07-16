#include "reversi/pattern_eval.hpp"

#include "reversi/moves.hpp"

#include <filesystem>
#include <gtest/gtest.h>

namespace reversi {
namespace {

// tests/data/dev_pattern_weights.bin - a small, fast weight file trained entirely on
// fixed-seed engine self-play (no WTHOR data at all - see tests/data/README.md for why and
// how it's reproduced). This is a mechanism smoke test only (does loading and evaluating
// work at all); step 7's dedicated correctness tests go further (extraction matches hand-
// computed values, determinism, the eval/terminalScore commensurability regression check).
std::filesystem::path devWeightsPath() {
    return std::filesystem::path(REVERSI_TEST_DATA_DIR) / "dev_pattern_weights.bin";
}

TEST(PatternEvaluator, LoadsTheDevWeightFileWithoutThrowing) {
    EXPECT_NO_THROW({ PatternEvaluator evaluator(devWeightsPath()); });
}

TEST(PatternEvaluator, EvaluateIsDeterministicForTheSamePosition) {
    const PatternEvaluator evaluator(devWeightsPath());
    const Position pos = Position::start();
    const int first = evaluator.evaluate(pos);
    const int second = evaluator.evaluate(pos);
    EXPECT_EQ(first, second);
}

TEST(PatternEvaluator, EvaluateProducesAPlausibleDiscDifferentialScale) {
    // Trained to predict a final disc differential, which is bounded to [-64, 64] by
    // construction - a wildly out-of-range result would indicate a byte-layout or indexing
    // bug, not just an inaccurate (but plausible) prediction.
    const PatternEvaluator evaluator(devWeightsPath());
    const int score = evaluator.evaluate(Position::start());
    EXPECT_GE(score, -64);
    EXPECT_LE(score, 64);
}

TEST(PatternEvaluator, AsEvalFnProducesTheSameValueAsDirectEvaluate) {
    const PatternEvaluator evaluator(devWeightsPath());
    const EvalFn fn = evaluator.asEvalFn();
    const Position pos = Position::start();
    EXPECT_EQ(fn(pos), evaluator.evaluate(pos));
}

TEST(PatternEvaluator, ThrowsOnMissingFile) {
    EXPECT_THROW(PatternEvaluator(std::filesystem::path("does_not_exist_12345.bin")),
                std::runtime_error);
}

TEST(PatternEvaluator, DifferentPositionsProduceDifferentScores) {
    // Not a tautology: a bug that always falls back to just the intercept (e.g. an indexing
    // error that always reads weight index 0 for every pattern) would make every position
    // score identically, regardless of what's actually on the board.
    const PatternEvaluator evaluator(devWeightsPath());
    const Position start = Position::start();
    const Position afterOneMove = applyMove(start, *squareFromString("f5"));
    EXPECT_NE(evaluator.evaluate(start), evaluator.evaluate(afterOneMove));
}

} // namespace
} // namespace reversi
