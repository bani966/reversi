#include "mpc_fitter/fit.hpp"

#include "mpc_fitter/dataset.hpp"
#include "reversi/moves.hpp"
#include "reversi/position.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <utility>

namespace mpc_fitter {
namespace {

TEST(ReadDataset, RoundTripsWhatWriteDatasetHeaderAndLineProduce) {
    std::ostringstream out;
    writeDatasetHeader({2, 4, 6}, out);
    writeDatasetLine(reversi::Position::start(), {2, 4, 6}, out);
    const reversi::Position afterF5 =
        reversi::applyMove(reversi::Position::start(), *reversi::squareFromString("f5"));
    writeDatasetLine(afterF5, {2, 4, 6}, out);

    std::istringstream in(out.str());
    const Dataset dataset = readDataset(in);
    EXPECT_EQ(dataset.depths, (std::vector<int>{2, 4, 6}));
    ASSERT_EQ(dataset.rows.size(), std::size_t{2});
    EXPECT_EQ(dataset.rows[0].size(), std::size_t{3});
    EXPECT_EQ(dataset.rows[1].size(), std::size_t{3});
}

TEST(ReadDataset, ThrowsWhenDepthsHeaderIsMissing) {
    std::istringstream in("1 2 3\n");
    EXPECT_THROW(readDataset(in), std::runtime_error);
}

TEST(ReadDataset, ThrowsWhenARowsTokenCountDoesNotMatchTheHeader) {
    std::istringstream in("% depths: 2 4\n1 2 3\n");
    EXPECT_THROW(readDataset(in), std::runtime_error);
}

// Hand-constructed, perfectly linear data: deepValue = 2*shallowValue + 3 exactly, for every
// row. The fit must recover a=3, b=2, sigma=0 (up to floating-point rounding) - the strongest
// possible correctness check, not just "some numbers came out."
TEST(FitPairs, RecoversExactCoefficientsForPerfectlyLinearData) {
    Dataset dataset;
    dataset.depths = {2, 4};
    for (const int shallow : {0, 1, 2, 3, 10, -5}) {
        dataset.rows.push_back({shallow, 2 * shallow + 3});
    }

    std::vector<std::string> warnings;
    const std::vector<FittedPair> pairs = fitPairs(dataset, /*reduction=*/2, /*minDeep=*/4,
                                                   /*maxDeep=*/4, warnings);
    EXPECT_TRUE(warnings.empty());
    ASSERT_EQ(pairs.size(), std::size_t{1});
    EXPECT_EQ(pairs[0].deepDepth, 4);
    EXPECT_EQ(pairs[0].shallowDepth, 2);
    EXPECT_NEAR(pairs[0].a, 3.0, 1e-9);
    EXPECT_NEAR(pairs[0].b, 2.0, 1e-9);
    EXPECT_NEAR(pairs[0].sigma, 0.0, 1e-9);
}

// A noisy case, checked against coefficients computed independently in THIS test (plain mean/
// covariance/variance arithmetic written out directly), not by calling fitPairs' own internals
// a second time - mirrors the independent-recomputation discipline used for every binary reader
// elsewhere in this project (OpeningBook, PatternEvaluator).
TEST(FitPairs, MatchesIndependentlyComputedOlsForNoisyData) {
    Dataset dataset;
    dataset.depths = {3, 5};
    const std::vector<std::pair<int, int>> samples = {{1, 4}, {2, 5}, {3, 9}, {4, 10}, {5, 15}};
    for (const auto& [shallow, deep] : samples) {
        dataset.rows.push_back({shallow, deep});
    }

    double sumX = 0.0;
    double sumY = 0.0;
    for (const auto& [x, y] : samples) {
        sumX += x;
        sumY += y;
    }
    const double n = static_cast<double>(samples.size());
    const double meanX = sumX / n;
    const double meanY = sumY / n;
    double sxx = 0.0;
    double sxy = 0.0;
    for (const auto& [x, y] : samples) {
        sxx += (x - meanX) * (x - meanX);
        sxy += (x - meanX) * (y - meanY);
    }
    const double expectedB = sxy / sxx;
    const double expectedA = meanY - expectedB * meanX;
    double sse = 0.0;
    for (const auto& [x, y] : samples) {
        const double residual = y - (expectedA + expectedB * x);
        sse += residual * residual;
    }
    const double expectedSigma = std::sqrt(sse / (n - 1));

    std::vector<std::string> warnings;
    const std::vector<FittedPair> pairs = fitPairs(dataset, /*reduction=*/2, /*minDeep=*/5,
                                                   /*maxDeep=*/5, warnings);
    ASSERT_EQ(pairs.size(), std::size_t{1});
    EXPECT_NEAR(pairs[0].a, expectedA, 1e-9);
    EXPECT_NEAR(pairs[0].b, expectedB, 1e-9);
    EXPECT_NEAR(pairs[0].sigma, expectedSigma, 1e-9);
}

TEST(FitPairs, SkipsPairsWhoseDepthsAreNotInTheDataset) {
    Dataset dataset;
    dataset.depths = {2, 4};
    dataset.rows = {{1, 2}, {3, 4}};

    std::vector<std::string> warnings;
    const std::vector<FittedPair> pairs =
        fitPairs(dataset, /*reduction=*/2, /*minDeep=*/6, /*maxDeep=*/6, warnings);
    EXPECT_TRUE(pairs.empty());
    EXPECT_EQ(warnings.size(), std::size_t{1});
}

TEST(FitPairs, SkipsPairsWithFewerThanTwoSamples) {
    Dataset dataset;
    dataset.depths = {2, 4};
    dataset.rows = {{1, 2}};

    std::vector<std::string> warnings;
    const std::vector<FittedPair> pairs =
        fitPairs(dataset, /*reduction=*/2, /*minDeep=*/4, /*maxDeep=*/4, warnings);
    EXPECT_TRUE(pairs.empty());
    EXPECT_EQ(warnings.size(), std::size_t{1});
}

TEST(FitPairs, SkipsPairsWithZeroVarianceInShallowValues) {
    Dataset dataset;
    dataset.depths = {2, 4};
    dataset.rows = {{5, 1}, {5, 2}, {5, 3}}; // every shallow value identical

    std::vector<std::string> warnings;
    const std::vector<FittedPair> pairs =
        fitPairs(dataset, /*reduction=*/2, /*minDeep=*/4, /*maxDeep=*/4, warnings);
    EXPECT_TRUE(pairs.empty());
    EXPECT_EQ(warnings.size(), std::size_t{1});
}

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

} // namespace independent

TEST(WriteModelFile, MatchesIndependentByteLevelRecomputation) {
    const std::vector<FittedPair> pairs = {
        {4, 2, 1.5, 2.25, 3.5, 100},
        {6, 4, -2.0, 0.9, 4.1, 250},
    };
    std::ostringstream out(std::ios::binary);
    writeModelFile(pairs, out);
    const std::string bytes = out.str();
    ASSERT_EQ(bytes.size(), std::size_t{4 + 2 * (4 + 4 + 4 + 4 + 4)});

    std::istringstream in(bytes, std::ios::binary);
    EXPECT_EQ(independent::readU32LE(in), 2u);
    for (const FittedPair& expected : pairs) {
        EXPECT_EQ(static_cast<std::int32_t>(independent::readU32LE(in)), expected.deepDepth);
        EXPECT_EQ(static_cast<std::int32_t>(independent::readU32LE(in)), expected.shallowDepth);
        EXPECT_FLOAT_EQ(independent::readF32LE(in), static_cast<float>(expected.a));
        EXPECT_FLOAT_EQ(independent::readF32LE(in), static_cast<float>(expected.b));
        EXPECT_FLOAT_EQ(independent::readF32LE(in), static_cast<float>(expected.sigma));
    }
}

} // namespace
} // namespace mpc_fitter
