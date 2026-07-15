#include "reversi/selfplay.hpp"

#include "reversi/players.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <random>

namespace reversi {
namespace {

PlayerFn randomPlayer(unsigned seed) {
    auto rng = std::make_shared<std::mt19937>(seed);
    return [rng](const Position& p) { return pickRandomMove(p, *rng); };
}

TEST(Selfplay, GameTerminatesWithAValidDiscCount) {
    const GameResult result = playGame(randomPlayer(1), randomPlayer(2));
    EXPECT_GT(result.blackDiscs, 0);
    EXPECT_GT(result.whiteDiscs, 0);
    EXPECT_LE(result.blackDiscs + result.whiteDiscs, 64);
    EXPECT_GE(result.blackDiscs + result.whiteDiscs, 4); // never below the starting 4 discs
}

TEST(Selfplay, GreedyVsRandomAlsoTerminates) {
    const GameResult result = playGame(pickGreedyMove, randomPlayer(3));
    EXPECT_LE(result.blackDiscs + result.whiteDiscs, 64);
}

TEST(Selfplay, MatchTallySumsToGameCount) {
    const MatchResult result = playMatch(randomPlayer(10), randomPlayer(11), 20);
    EXPECT_EQ(result.aWins + result.bWins + result.draws, 20);
}

} // namespace
} // namespace reversi
