#include "reversi/eval.hpp"

#include <gtest/gtest.h>

namespace reversi {
namespace {

TEST(Eval, StartPositionIsBalanced) {
    EXPECT_EQ(evaluateDiscDifferential(Position::start()), 0);
    EXPECT_EQ(terminalScore(Position::start()), 0);
}

TEST(Eval, DiscDifferentialMatchesAsymmetricPosition) {
    Position p;
    p.own = bit(0) | bit(1) | bit(2); // 3 own discs
    p.opp = bit(10);                  // 1 opp disc
    EXPECT_EQ(evaluateDiscDifferential(p), 2);
    EXPECT_EQ(terminalScore(p), 2);
}

TEST(Eval, EvalFnIsCallableThroughTheAlias) {
    const EvalFn eval = evaluateDiscDifferential;
    EXPECT_EQ(eval(Position::start()), 0);
}

} // namespace
} // namespace reversi
