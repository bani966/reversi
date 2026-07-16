#include "mpc_fitter/dataset.hpp"

#include "reversi/eval.hpp"
#include "reversi/moves.hpp"
#include "reversi/position.hpp"
#include "reversi/search.hpp"

#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

namespace mpc_fitter {
namespace {

TEST(WriteDatasetHeader, ListsExactlyTheGivenDepthsInOrder) {
    std::ostringstream out;
    writeDatasetHeader({2, 3, 5, 8}, out);
    const std::string text = out.str();
    EXPECT_NE(text.find("% depths: 2 3 5 8\n"), std::string::npos) << text;
}

TEST(WriteDatasetHeader, EveryLineStartsWithAPercentComment) {
    std::ostringstream out;
    writeDatasetHeader({1, 2}, out);
    std::istringstream in(out.str());
    std::string line;
    while (std::getline(in, line)) {
        EXPECT_EQ(line.front(), '%') << line;
    }
}

TEST(WriteDatasetLine, EmitsExactlyOneTokenPerDepthInOrder) {
    std::ostringstream out;
    writeDatasetLine(reversi::Position::start(), {1, 2, 3}, out);
    std::istringstream in(out.str());
    int a = 0;
    int b = 0;
    int c = 0;
    ASSERT_TRUE(static_cast<bool>(in >> a >> b >> c));
    int extra;
    EXPECT_FALSE(static_cast<bool>(in >> extra)) << "expected exactly 3 tokens";
}

// Direct cross-check: the written values must equal calling reversi::search() by hand at each
// depth - not just "some numbers came out."
TEST(WriteDatasetLine, ValuesMatchDirectSearchCallsForEachDepth) {
    const reversi::Position pos =
        reversi::applyMove(reversi::Position::start(), *reversi::squareFromString("f5"));
    const std::vector<int> depths = {1, 2, 4};

    std::ostringstream out;
    writeDatasetLine(pos, depths, out);
    std::istringstream in(out.str());

    for (const int depth : depths) {
        int value = 0;
        ASSERT_TRUE(static_cast<bool>(in >> value)) << "depth " << depth;
        const int expected = reversi::search(pos, depth, reversi::evaluateDiscDifferential).score;
        EXPECT_EQ(value, expected) << "depth " << depth;
    }
}

TEST(WriteDatasetLine, DifferentPositionsCanProduceDifferentValues) {
    const reversi::Position start = reversi::Position::start();
    const reversi::Position afterF5 = reversi::applyMove(start, *reversi::squareFromString("f5"));

    std::ostringstream startOut;
    writeDatasetLine(start, {3}, startOut);
    std::ostringstream afterOut;
    writeDatasetLine(afterF5, {3}, afterOut);
    EXPECT_NE(startOut.str(), afterOut.str());
}

} // namespace
} // namespace mpc_fitter
