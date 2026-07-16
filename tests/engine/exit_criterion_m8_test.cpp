#include "reversi/search.hpp"

#include "reversi/eval.hpp"
#include "reversi/players.hpp"
#include "reversi/selfplay.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>

#include <gtest/gtest.h>

namespace reversi {
namespace {

// M8's strength half of the exit criterion, per README: "no strength regression at equal time."
// Equal-TIME (not equal-depth) self-play, matching this milestone's own point of existing -
// Lazy SMP trades redundant parallel exploration for more effective depth within the SAME wall-
// clock budget, so an equal-depth comparison would say nothing about it. Same DISABLED_-by-
// default convention as every other milestone's expensive exit-criterion test (M4/M7's own
// equal-time methodology, mirrored here) - cheap enough to always run correctness tests
// (search_lazy_smp_test.cpp) exist separately for that; this is specifically about strength.
//
// Run on demand:
//   build/msvc/tests/Release/engine_tests.exe --gtest_filter=*ExitCriterionM8* \
//     --gtest_also_run_disabled_tests
// Optionally override game count for faster iteration: REVERSI_M8_GAMES=<n>
//
// Measured results: see DEVLOG.md / the step-4 commit message for the actual numbers, alongside
// the real nps-scaling measurement (cli bench <depth> <threads>).

constexpr TimeBudget kMatchBudget{std::chrono::milliseconds{300}, std::chrono::milliseconds{800}};
constexpr int kMaxDepth = 24;
constexpr int kThreadCount = 8; // matches this dev machine's 8 physical/logical cores

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

PlayerFn lazySmpPlayer(SharedTranspositionTable& tt) {
    return [&tt](const Position& p) {
        return searchLazySmp(p, kMaxDepth, kMatchBudget, kThreadCount, evaluateDiscDifferential,
                             nullptr, &tt)
            .bestMove;
    };
}

PlayerFn singleThreadedPlayer(TranspositionTable& tt) {
    return [&tt](const Position& p) {
        return searchTimed(p, kMaxDepth, kMatchBudget, evaluateDiscDifferential, nullptr, &tt)
            .bestMove;
    };
}

TEST(DISABLED_ExitCriterionM8, EqualTimeLazySmpVsSingleThreaded) {
    int games = 20;
    if (const char* raw = getEnvOrNull("REVERSI_M8_GAMES"); raw != nullptr) {
        games = std::atoi(raw);
    }

    int lazyWins = 0;
    int singleWins = 0;
    int draws = 0;
    for (int i = 0; i < games; ++i) {
        SharedTranspositionTable lazyTt(std::size_t{1} << 20);
        TranspositionTable singleTt(std::size_t{1} << 20); // fresh per game, matches GameController
        const bool lazyIsBlack = (i % 2 == 0);
        const GameResult game =
            lazyIsBlack ? playGame(lazySmpPlayer(lazyTt), singleThreadedPlayer(singleTt))
                        : playGame(singleThreadedPlayer(singleTt), lazySmpPlayer(lazyTt));
        const int lazyDiscs = lazyIsBlack ? game.blackDiscs : game.whiteDiscs;
        const int singleDiscs = lazyIsBlack ? game.whiteDiscs : game.blackDiscs;
        if (lazyDiscs > singleDiscs) {
            ++lazyWins;
        } else if (singleDiscs > lazyDiscs) {
            ++singleWins;
        } else {
            ++draws;
        }
    }
    std::fprintf(stderr,
                 "EqualTimeLazySmpVsSingleThreaded (threads=%d): lazySmp=%d single=%d draws=%d "
                 "(of %d)\n",
                 kThreadCount, lazyWins, singleWins, draws, games);
    EXPECT_GE(lazyWins, singleWins);
}

} // namespace
} // namespace reversi
