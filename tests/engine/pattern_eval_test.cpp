#include "reversi/pattern_eval.hpp"

#include "../support/endgame_positions.hpp"
#include "reversi/moves.hpp"
#include "reversi/pattern.hpp"
#include "reversi/search.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

// A genuinely INDEPENDENT re-parse of the weight file - deliberately not calling any of
// PatternEvaluator's own (private) byte-reading code - so this test can catch a bug in
// PatternEvaluator's own parsing/lookup logic rather than merely confirming it's internally
// self-consistent. Mirrors the same discipline as M5's FFO test (checked against independently
// -sourced published data) and M6 step 4's hand-computed ternary-index cross-check.
namespace independent {

std::uint32_t readU32LE(std::istream& in) {
    unsigned char b[4];
    in.read(reinterpret_cast<char*>(b), 4);
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8U) |
           (static_cast<std::uint32_t>(b[2]) << 16U) | (static_cast<std::uint32_t>(b[3]) << 24U);
}

float readF32LE(std::istream& in) {
    return std::bit_cast<float>(readU32LE(in));
}

// Recomputes evaluate(pos) from raw weight-file bytes, independently of PatternEvaluator.
int independentEvaluate(const std::filesystem::path& path, const Position& pos) {
    std::ifstream in(path, std::ios::binary);
    const std::uint32_t bucketCount = readU32LE(in);
    for (std::uint32_t b = 0; b < bucketCount; ++b) {
        const std::int32_t minEmpty = static_cast<std::int32_t>(readU32LE(in));
        const std::int32_t maxEmpty = static_cast<std::int32_t>(readU32LE(in));
        const float intercept = readF32LE(in);
        double sum = intercept;
        const bool inThisBucket = pos.emptyCount() >= minEmpty && pos.emptyCount() <= maxEmpty;
        for (const pattern::PatternClass& cls : pattern::allPatternClasses()) {
            const std::size_t length = cls.instances.empty() ? 0 : cls.instances.front().size();
            std::size_t states = 1;
            for (std::size_t i = 0; i < length; ++i) {
                states *= 3;
            }
            std::vector<float> weights(states);
            for (float& w : weights) {
                w = readF32LE(in);
            }
            if (inThisBucket) {
                for (const std::vector<int>& instance : cls.instances) {
                    sum += weights[static_cast<std::size_t>(pattern::ternaryIndex(pos, instance))];
                }
            }
        }
        if (inThisBucket) {
            return static_cast<int>(std::lround(sum));
        }
    }
    throw std::runtime_error("independentEvaluate: no bucket covers this position");
}

} // namespace independent

TEST(PatternEvaluator, MatchesIndependentByteLevelRecomputation) {
    const PatternEvaluator evaluator(devWeightsPath());
    for (const Position& pos :
         {Position::start(), applyMove(Position::start(), *squareFromString("f5")),
          applyMove(applyMove(Position::start(), *squareFromString("f5")),
                    *squareFromString("d6"))}) {
        EXPECT_EQ(evaluator.evaluate(pos), independent::independentEvaluate(devWeightsPath(), pos));
    }
}

// The load-bearing correctness argument from the M6 plan's Context section, verified directly:
// search(pos, pos.emptyCount(), eval) must NEVER actually invoke eval() - the game-over branch
// always fires first (the same fact M5's solveExact design relies on, see solver.hpp). This is
// what keeps evaluatePattern's differently-scaled leaf value from ever mixing with a
// terminalScore()-computed mid-tree leaf inside one search tree's alpha-beta comparisons.
TEST(PatternEvaluator, NeverInvokedWhenSearchedToExactlyEmptyCount) {
    const PatternEvaluator evaluator(devWeightsPath());
    int callCount = 0;
    const EvalFn countingEval = [&](const Position& p) {
        ++callCount;
        return evaluator.evaluate(p);
    };
    const auto sample = endgame::collectPositionsByEmptyCount(2027u, 1, 6);
    ASSERT_FALSE(sample.empty());
    for (const Position& pos : sample) {
        callCount = 0;
        search(pos, pos.emptyCount(), countingEval);
        EXPECT_EQ(callCount, 0)
            << "evaluatePattern was invoked even though depth == emptyCount should always "
               "reach the game-over branch first";
    }
}

// Positive control for the test above: confirms countingEval actually IS wired into search
// correctly (a search shallower than emptyCount must reach real leaves and call eval at least
// once) - without this, a broken counting wrapper could make the negative-control test above
// pass for the wrong reason (e.g. never actually running).
TEST(PatternEvaluator, IsInvokedWhenSearchDepthIsShallowerThanEmptyCount) {
    const PatternEvaluator evaluator(devWeightsPath());
    int callCount = 0;
    const EvalFn countingEval = [&](const Position& p) {
        ++callCount;
        return evaluator.evaluate(p);
    };
    const auto sample = endgame::collectPositionsByEmptyCount(2027u, 10, 20);
    ASSERT_FALSE(sample.empty());
    search(sample.front(), 2, countingEval); // depth 2, far shallower than emptyCount
    EXPECT_GT(callCount, 0);
}

} // namespace
} // namespace reversi
