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
// The match budget below is deliberately GameController::kAiTimeBudget (app/src/GameController.
// cpp), not the short budget (300ms/800ms) M7's own equal-time test used and this test originally
// mirrored - switched after that shorter budget's own 20-game runs kept leaning toward single-
// threaded, and a direct depth diagnostic showed why: completing depth 14 from the start position
// already takes ~520ms on this machine, already past a 300ms soft deadline, so neither solo nor
// Lazy SMP's thread 0 ever gets to START a depth-15 iteration there - Lazy SMP's only mechanism
// for a same-budget edge (other threads' shared-TT entries letting thread 0 blow through its own
// iterations faster, buying it spare time before the soft deadline to attempt one more ply) needs
// slack a 300ms soft deadline doesn't leave.
//
// Switching to the real production budget did NOT resolve the gap, though - three independent
// 20-game runs across both budgets (300/800ms twice, then the corrected 800/2500ms) all landed
// single-threaded ahead (9/10/1, 9/11/0, 9/11/0 - 27/32/1 combined across 60 games). The depth
// diagnostic's clear per-position benefit (depth 15 vs 14, same start position, same 800/2500ms
// budget - see DEVLOG.md's step-3 entry) is real but evidently doesn't generalize into a net
// full-game edge: that diagnostic only ever probed the opening position, while a real game spends
// most of its length in the mid/endgame, where fewer legal moves mean less for jittered threads to
// usefully diverge on and shared-TT contention has less redundant work to amortize against. This
// is an honest, currently-unresolved negative-leaning finding, not swept under the rug - see
// DEVLOG.md's step-4 entry for the full writeup and the real nps-scaling numbers (which ARE a
// clear, unambiguous win, measured via `cli bench <depth> <threads>`).

constexpr TimeBudget kMatchBudget =
    TimeBudget{std::chrono::milliseconds{800}, std::chrono::milliseconds{2500}};
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
    // Counting a draw as a non-loss states the actual bar this test wants ("not a clear
    // regression", not "must outright win"): it fails only if single-threaded wins an outright
    // majority over Lazy SMP's wins-plus-draws combined. As of step 4 this assertion is known to
    // FAIL on this machine (9 + 0 draws = 9 < 11) - left failing deliberately rather than loosened
    // further to force green, since the measured result is a real, reproducible finding (see the
    // file header comment and DEVLOG.md), not sampling noise this bar should be tolerating away.
    EXPECT_GE(lazyWins + draws, singleWins);
}

} // namespace
} // namespace reversi
