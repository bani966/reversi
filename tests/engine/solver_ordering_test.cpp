#include "reversi/solver.hpp"

#include "../support/baseline_solver.hpp"
#include "reversi/moves.hpp"
#include "reversi/players.hpp"
#include "reversi/position.hpp"

#include <cstdint>
#include <gtest/gtest.h>
#include <random>
#include <vector>

namespace reversi {
namespace {

// oddParitySquares is exposed specifically so its flood-fill logic gets a direct correctness
// test independent of its effect on solveExact's node counts (a flood-fill bug could easily
// produce a still-technically-valid-looking-but-wrong ordering that only shows up as a slower,
// not incorrect, search). Hand-constructed shapes with known region sizes/adjacency.
TEST(OddParitySquares, SingleIsolatedSquareIsOdd) {
    const Bitboard empty = bit(*squareFromString("d4"));
    EXPECT_EQ(oddParitySquares(empty), empty);
}

TEST(OddParitySquares, TwoOrthogonallyAdjacentSquaresAreEven) {
    const Bitboard empty = bit(*squareFromString("d4")) | bit(*squareFromString("d5"));
    EXPECT_EQ(oddParitySquares(empty), Bitboard{0});
}

TEST(OddParitySquares, TwoDiagonallyAdjacentSquaresAreOneEvenRegion) {
    // 8-directional adjacency: d4 and e5 are diagonal neighbors, so this is ONE size-2 (even)
    // region - not two separate size-1 (odd) regions. Exercises that diagonal connectivity is
    // actually implemented, not just orthogonal.
    const Bitboard empty = bit(*squareFromString("d4")) | bit(*squareFromString("e5"));
    EXPECT_EQ(oddParitySquares(empty), Bitboard{0});
}

TEST(OddParitySquares, ThreeSquareLineIsOdd) {
    const Bitboard empty =
        bit(*squareFromString("d4")) | bit(*squareFromString("d5")) | bit(*squareFromString("d6"));
    EXPECT_EQ(oddParitySquares(empty), empty);
}

TEST(OddParitySquares, TwoDisjointRegionsClassifiedIndependently) {
    // An isolated single square (a1, odd) far from an unrelated domino (d4-d5, even) - proves
    // regions are found and classified independently, not merged or cross-contaminated.
    const Bitboard oddRegion = bit(*squareFromString("a1"));
    const Bitboard evenRegion = bit(*squareFromString("d4")) | bit(*squareFromString("d5"));
    EXPECT_EQ(oddParitySquares(oddRegion | evenRegion), oddRegion);
}

TEST(OddParitySquares, EmptyInputIsEmptyOutput) {
    EXPECT_EQ(oddParitySquares(Bitboard{0}), Bitboard{0});
}

namespace {
std::vector<Position> collectPositionsByEmptyCount(unsigned seed, int minEmpty, int maxEmpty) {
    std::vector<Position> positions;
    std::mt19937 rng(seed);
    Position p = Position::start();
    while (!isGameOver(p)) {
        if (!hasLegalMove(p)) {
            p = applyPass(p);
            continue;
        }
        if (p.emptyCount() >= minEmpty && p.emptyCount() <= maxEmpty) {
            positions.push_back(p);
        }
        p = applyMove(p, pickRandomMove(p, rng));
    }
    return positions;
}
} // namespace

// The step-2 correctness contract, per plan: ordering must never change the answer. Checked
// against baseline::solveExact (tests/support/baseline_solver.*, a frozen snapshot of the
// plain-ordered M5-step-1 solver) rather than reversi::search(), since baseline::solveExact is
// the direct "before" of this exact change - solver_test.cpp's cross-check against search()
// already re-validates the same property from an independent angle on every run.
TEST(SolverOrdering, ScoreMatchesPreOrderingBaselineAndPrunesMeasurably) {
    std::vector<Position> positions;
    for (const unsigned seed : {11u, 22u, 33u, 44u}) {
        const auto sample = collectPositionsByEmptyCount(seed, 8, 12);
        positions.insert(positions.end(), sample.begin(), sample.end());
    }
    ASSERT_GE(positions.size(), std::size_t{10});

    std::uint64_t baselineNodes = 0;
    std::uint64_t orderedNodes = 0;
    for (const Position& pos : positions) {
        const baseline::BaselineResult expected = baseline::solveExact(pos);
        const SearchResult actual = solveExact(pos);
        EXPECT_EQ(actual.score, expected.score) << "emptyCount=" << pos.emptyCount();
        baselineNodes += expected.nodes;
        orderedNodes += actual.nodes;
    }
    // Measured on this sample when the ordering landed: baseline 811,072 nodes, ordered
    // 173,107 (21.3% of baseline). Asserted at 50% - well above the measured ratio, so benign
    // tweaks don't trip this, while a real ordering regression (which costs multiples, not a
    // few percent) still would.
    EXPECT_LT(orderedNodes, baselineNodes / 2);
}

} // namespace
} // namespace reversi
