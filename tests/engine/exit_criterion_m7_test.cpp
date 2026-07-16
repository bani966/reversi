#include "reversi/mpc.hpp"

#include "../support/benchmark_positions.hpp"
#include "reversi/eval.hpp"
#include "reversi/players.hpp"
#include "reversi/search.hpp"
#include "reversi/selfplay.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include <gtest/gtest.h>

namespace reversi {
namespace {

// M7's validation strategy (per the plan): equal-TIME self-play MPC-on vs MPC-off (not equal
// depth - the whole point of MPC is trading a small accuracy risk for more effective depth in
// the same wall-clock budget), plus a separate move-agreement measurement against a much
// deeper/slower ground-truth search. Both need a REAL, non-trivial MpcModel - the small
// committed dev fixture (tests/data/dev_mpc_model.bin, fit from 20 self-played games) has no
// real tuning value, same reasoning as M6's dev_pattern_weights.bin/dev_opening_book.bin. So
// these are DISABLED_ by default, run on demand with a real model file path:
//   REVERSI_M7_REAL_MODEL=<path> [REVERSI_M7_T=<t>] \
//     build/msvc/tests/Release/engine_tests.exe --gtest_filter=*ExitCriterionM7* \
//     --gtest_also_run_disabled_tests
//
// Measured results: see DEVLOG.md / the step-5 commit message for the actual numbers and the
// candidate t values tried before picking kDefaultMpcT.

constexpr TimeBudget kMatchBudget{std::chrono::milliseconds{300}, std::chrono::milliseconds{800}};
constexpr int kMaxDepth = 24;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv is read-only here, no _dupenv_s needed
#endif
const char* getEnvOrNull(const char* name) {
    return std::getenv(name);
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

double resolveT() {
    if (const char* raw = getEnvOrNull("REVERSI_M7_T"); raw != nullptr) {
        return std::atof(raw);
    }
    return kDefaultMpcT;
}

PlayerFn mpcOnPlayer(const MpcModel& model, double t, TranspositionTable& tt) {
    return [&model, t, &tt](const Position& p) {
        const MpcConfig config{&model, t};
        return searchTimed(p, kMaxDepth, kMatchBudget, evaluateDiscDifferential, nullptr, &tt,
                           &config)
            .bestMove;
    };
}

PlayerFn mpcOffPlayer(TranspositionTable& tt) {
    return [&tt](const Position& p) {
        return searchTimed(p, kMaxDepth, kMatchBudget, evaluateDiscDifferential, nullptr, &tt)
            .bestMove;
    };
}

TEST(DISABLED_ExitCriterionM7, EqualTimeMpcVsNoMpc) {
    const char* modelPath = getEnvOrNull("REVERSI_M7_REAL_MODEL");
    ASSERT_NE(modelPath, nullptr)
        << "set REVERSI_M7_REAL_MODEL to a real fitted MPC model file path to run this test";
    const MpcModel model(modelPath);
    const double t = resolveT();

    int kGames = 20;
    if (const char* rawGames = getEnvOrNull("REVERSI_M7_GAMES"); rawGames != nullptr) {
        kGames = std::atoi(rawGames);
    }
    int mpcOnWins = 0;
    int mpcOffWins = 0;
    int draws = 0;
    for (int i = 0; i < kGames; ++i) {
        TranspositionTable onTt(std::size_t{1} << 20); // fresh per game, matching GameController
        TranspositionTable offTt(std::size_t{1} << 20);
        const bool mpcOnIsBlack = (i % 2 == 0);
        const GameResult game = mpcOnIsBlack
                                    ? playGame(mpcOnPlayer(model, t, onTt), mpcOffPlayer(offTt))
                                    : playGame(mpcOffPlayer(offTt), mpcOnPlayer(model, t, onTt));
        const int mpcOnDiscs = mpcOnIsBlack ? game.blackDiscs : game.whiteDiscs;
        const int mpcOffDiscs = mpcOnIsBlack ? game.whiteDiscs : game.blackDiscs;
        if (mpcOnDiscs > mpcOffDiscs) {
            ++mpcOnWins;
        } else if (mpcOffDiscs > mpcOnDiscs) {
            ++mpcOffWins;
        } else {
            ++draws;
        }
    }
    std::fprintf(stderr, "EqualTimeMpcVsNoMpc (t=%.2f): mpcOn=%d mpcOff=%d draws=%d (of %d)\n", t,
                 mpcOnWins, mpcOffWins, draws, kGames);
    EXPECT_GE(mpcOnWins, mpcOffWins);
}

TEST(DISABLED_ExitCriterionM7, MoveAgreementVsDeeperGroundTruth) {
    const char* modelPath = getEnvOrNull("REVERSI_M7_REAL_MODEL");
    ASSERT_NE(modelPath, nullptr)
        << "set REVERSI_M7_REAL_MODEL to a real fitted MPC model file path to run this test";
    const MpcModel model(modelPath);
    const double t = resolveT();
    const MpcConfig config{&model, t};

    constexpr int kGroundTruthDepth = 11; // deliberately deeper than the match budget reaches
    int agree = 0;
    int total = 0;
    for (const Position& pos : bench::benchmarkPositions()) {
        TranspositionTable mpcTt(std::size_t{1} << 20);
        const int mpcMove = searchTimed(pos, kMaxDepth, kMatchBudget, evaluateDiscDifferential,
                                        nullptr, &mpcTt, &config)
                                .bestMove;
        const int groundTruthMove =
            search(pos, kGroundTruthDepth, evaluateDiscDifferential).bestMove;
        if (mpcMove == groundTruthMove) {
            ++agree;
        }
        ++total;
    }
    std::fprintf(stderr, "MoveAgreementVsDeeperGroundTruth (t=%.2f): %d/%d agree\n", t, agree,
                 total);
    EXPECT_GT(total, 0);
}

} // namespace
} // namespace reversi
