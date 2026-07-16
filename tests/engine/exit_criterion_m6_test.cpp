#include "reversi/pattern_eval.hpp"

#include "reversi/eval.hpp"
#include "reversi/search.hpp"
#include "reversi/selfplay.hpp"

#include <cstdio>
#include <cstdlib>
#include <gtest/gtest.h>

namespace reversi {
namespace {

// M6's exit criterion, per README: "WTHOR-trained eval beats hand eval at equal depth." Both
// sides use reversi::search() at the SAME fixed depth, isolating the eval-quality gain -
// mirrors M4's exit criterion using the same eval on both sides to isolate search improvement,
// just inverted (same search, different eval, here).
//
// This test needs REAL, fully WTHOR-trained weights - not the small committed dev/test file
// (tests/data/dev_pattern_weights.bin, trained on 40 synthetic self-play games purely to
// exercise the loading mechanism, with no real playing strength). Real weights are never
// committed (no confirmed WTHOR redistribution license - see the M6 plan's Context section),
// so this is DISABLED_ by default, run on demand with a local weight file path:
//   REVERSI_M6_REAL_WEIGHTS=<path> build/msvc/tests/Release/engine_tests.exe
//     --gtest_filter=*PatternEvalVsDiscDifferential* --gtest_also_run_disabled_tests
//
// Measured result: see the commit message for the actual numbers and how the weights used to
// produce them were trained (dataset size, source years, bucket/regularization settings).
TEST(DISABLED_ExitCriterionM6, PatternEvalVsDiscDifferential) {
    // std::getenv is a read-only lookup here (never written into), so MSVC's "unsafe, use
    // _dupenv_s" deprecation warning doesn't apply to this usage - suppressed locally rather
    // than project-wide (_CRT_SECURE_NO_WARNINGS would silence genuinely-unsafe uses too).
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* weightsPath = std::getenv("REVERSI_M6_REAL_WEIGHTS");
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    ASSERT_NE(weightsPath, nullptr)
        << "set REVERSI_M6_REAL_WEIGHTS to a real trained weight file path to run this test";

    const PatternEvaluator patternEval(weightsPath);
    constexpr int kDepth = 6;
    const PlayerFn patternPlayer = [&patternEval](const Position& p) {
        return search(p, kDepth, patternEval.asEvalFn()).bestMove;
    };
    const PlayerFn discDiffPlayer = [](const Position& p) {
        return search(p, kDepth, evaluateDiscDifferential).bestMove;
    };

    constexpr int kGames = 20;
    int patternWins = 0;
    int discDiffWins = 0;
    int draws = 0;
    for (int i = 0; i < kGames; ++i) {
        const bool patternIsBlack = (i % 2 == 0);
        const GameResult game = patternIsBlack ? playGame(patternPlayer, discDiffPlayer)
                                               : playGame(discDiffPlayer, patternPlayer);
        const int patternDiscs = patternIsBlack ? game.blackDiscs : game.whiteDiscs;
        const int discDiffDiscs = patternIsBlack ? game.whiteDiscs : game.blackDiscs;
        if (patternDiscs > discDiffDiscs) {
            ++patternWins;
        } else if (discDiffDiscs > patternDiscs) {
            ++discDiffWins;
        } else {
            ++draws;
        }
    }
    std::fprintf(stderr, "PatternEvalVsDiscDifferential: pattern=%d discDiff=%d draws=%d (of %d)\n",
                 patternWins, discDiffWins, draws, kGames);
    EXPECT_GT(patternWins, discDiffWins);
}

} // namespace
} // namespace reversi
