#include "reversi/pattern.hpp"

#include "reversi/position.hpp"

#include <algorithm>
#include <gtest/gtest.h>

namespace reversi::pattern {
namespace {

int sq(const char* algebraic) {
    return *squareFromString(algebraic);
}

std::vector<int> sorted(std::vector<int> v) {
    std::sort(v.begin(), v.end());
    return v;
}

// Corner cycle under repeated 90-degree rotation, hand-derived from transformCoords' formula
// before writing the implementation: a1 -> a8 -> h8 -> h1 -> a1.
TEST(ApplySymmetry, Rotate90CyclesCornersInExpectedOrder) {
    EXPECT_EQ(applySymmetry(Symmetry::Rotate90, sq("a1")), sq("a8"));
    EXPECT_EQ(applySymmetry(Symmetry::Rotate90, sq("a8")), sq("h8"));
    EXPECT_EQ(applySymmetry(Symmetry::Rotate90, sq("h8")), sq("h1"));
    EXPECT_EQ(applySymmetry(Symmetry::Rotate90, sq("h1")), sq("a1"));
}

TEST(ApplySymmetry, IdentityIsANoOp) {
    for (int square = 0; square < kBoardSquares; ++square) {
        EXPECT_EQ(applySymmetry(Symmetry::Identity, square), square);
    }
}

TEST(ApplySymmetry, EveryTransformIsABijectionOverAllSixtyFourSquares) {
    for (const Symmetry sym :
         {Symmetry::Identity, Symmetry::Rotate90, Symmetry::Rotate180, Symmetry::Rotate270,
          Symmetry::ReflectHorizontal, Symmetry::ReflectVertical, Symmetry::ReflectMainDiag,
          Symmetry::ReflectAntiDiag}) {
        std::vector<int> images;
        for (int square = 0; square < kBoardSquares; ++square) {
            images.push_back(applySymmetry(sym, square));
        }
        std::sort(images.begin(), images.end());
        for (int i = 0; i < kBoardSquares; ++i) {
            EXPECT_EQ(images[i], i) << "symmetry did not permute all 64 squares bijectively";
        }
    }
}

// The corrected design (see pattern.hpp): lines split into 4 classes by edge distance, NOT
// one shared class for all 16 rows+columns - verified here against hand-derived orbits, not
// just trusted from the implementation.
TEST(AllPatternClasses, HasExactlyTwelveClassesWithExpectedInstanceCounts) {
    const std::vector<PatternClass>& classes = allPatternClasses();
    ASSERT_EQ(classes.size(), std::size_t{12});

    // 4 line classes (edge distance 0..3), 4 instances each.
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(classes[static_cast<std::size_t>(i)].instances.size(), std::size_t{4})
            << "line class " << i;
    }
    // 6 diagonal classes (length 3..8): length 8 has 2 instances, lengths 3-7 have 4 each.
    EXPECT_EQ(classes[4].instances.size(), std::size_t{4});  // length 3
    EXPECT_EQ(classes[5].instances.size(), std::size_t{4});  // length 4
    EXPECT_EQ(classes[6].instances.size(), std::size_t{4});  // length 5
    EXPECT_EQ(classes[7].instances.size(), std::size_t{4});  // length 6
    EXPECT_EQ(classes[8].instances.size(), std::size_t{4});  // length 7
    EXPECT_EQ(classes[9].instances.size(), std::size_t{2});  // length 8: only 2 main diagonals
    EXPECT_EQ(classes[10].instances.size(), std::size_t{4}); // edge2x
    EXPECT_EQ(classes[11].instances.size(), std::size_t{4}); // corner3x3

    std::size_t total = 0;
    for (const PatternClass& cls : classes) {
        total += cls.instances.size();
    }
    EXPECT_EQ(total, std::size_t{46});
}

// Hand-derived orbit for the outermost line class: rank1's full symmetry orbit is exactly
// {row1, row8, col-a, col-h} - the 4 OUTER lines - confirmed by explicit coordinate-transform
// derivation before implementing (an inner row like row4 is NOT reachable from row1 by any
// combination of rotations/reflections, so it must be a different class - checked below too).
TEST(AllPatternClasses, OuterLineClassContainsExactlyTheFourEdgeLines) {
    const std::vector<int> row1 = {sq("a1"), sq("b1"), sq("c1"), sq("d1"),
                                   sq("e1"), sq("f1"), sq("g1"), sq("h1")};
    const std::vector<int> row8 = {sq("a8"), sq("b8"), sq("c8"), sq("d8"),
                                   sq("e8"), sq("f8"), sq("g8"), sq("h8")};
    const std::vector<int> colA = {sq("a1"), sq("a2"), sq("a3"), sq("a4"),
                                   sq("a5"), sq("a6"), sq("a7"), sq("a8")};
    const std::vector<int> colH = {sq("h1"), sq("h2"), sq("h3"), sq("h4"),
                                   sq("h5"), sq("h6"), sq("h7"), sq("h8")};

    // allPatternClasses' seed for edge distance 0 is exactly row1 (rank 0), so it must be
    // class index 0.
    const PatternClass& outer = allPatternClasses()[0];
    EXPECT_EQ(outer.name, "line_edgeDist0");

    std::vector<std::vector<int>> expected = {sorted(row1), sorted(row8), sorted(colA),
                                              sorted(colH)};
    std::sort(expected.begin(), expected.end());
    std::vector<std::vector<int>> actual = outer.instances;
    std::sort(actual.begin(), actual.end());
    EXPECT_EQ(actual, expected);
}

// row4 (rank 3) must NOT be reachable from row1's class - it belongs to a different,
// central-lines orbit (edge distance 3).
TEST(AllPatternClasses, InnerLineIsNotInTheOuterLineClass) {
    const std::vector<int> row4 =
        sorted({sq("a4"), sq("b4"), sq("c4"), sq("d4"), sq("e4"), sq("f4"), sq("g4"), sq("h4")});
    const PatternClass& outer = allPatternClasses()[0];
    for (const std::vector<int>& instance : outer.instances) {
        EXPECT_NE(sorted(instance), row4);
    }
}

// The main diagonal (length 8) class has exactly the two main diagonals, hand-computed before
// implementing: a1-h8 and a8-h1.
TEST(AllPatternClasses, Length8DiagonalClassIsExactlyTheTwoMainDiagonals) {
    const std::vector<int> mainDiag =
        sorted({sq("a1"), sq("b2"), sq("c3"), sq("d4"), sq("e5"), sq("f6"), sq("g7"), sq("h8")});
    const std::vector<int> antiDiag =
        sorted({sq("a8"), sq("b7"), sq("c6"), sq("d5"), sq("e4"), sq("f3"), sq("g2"), sq("h1")});

    const PatternClass& diag8 = allPatternClasses()[9];
    EXPECT_EQ(diag8.name, "diag8");
    std::vector<std::vector<int>> expected = {mainDiag, antiDiag};
    std::sort(expected.begin(), expected.end());
    std::vector<std::vector<int>> actual = diag8.instances;
    std::sort(actual.begin(), actual.end());
    EXPECT_EQ(actual, expected);
}

// Every pattern class must be internally consistent: all instances the same length, and every
// instance's squares listed in strictly ascending order (the canonicalization convention
// pattern.hpp documents).
TEST(AllPatternClasses, EveryInstanceIsSortedAscendingAndClassIsLengthConsistent) {
    for (const PatternClass& cls : allPatternClasses()) {
        ASSERT_FALSE(cls.instances.empty()) << cls.name;
        const std::size_t length = cls.instances.front().size();
        for (const std::vector<int>& instance : cls.instances) {
            EXPECT_EQ(instance.size(), length) << cls.name;
            EXPECT_TRUE(std::is_sorted(instance.begin(), instance.end())) << cls.name;
        }
    }
}

TEST(TernaryIndex, EmptyBoardIsAlwaysZero) {
    const Position empty; // own = opp = 0
    EXPECT_EQ(ternaryIndex(empty, {sq("a1"), sq("b1"), sq("c1")}), 0);
}

TEST(TernaryIndex, DigitsAreBase3PositionallyWeighted) {
    // squares[0]=own (digit 1), squares[1]=opp (digit 2), squares[2]=empty (digit 0).
    // index = 1*3^0 + 2*3^1 + 0*3^2 = 1 + 6 + 0 = 7.
    Position p;
    p.own = bit(sq("a1"));
    p.opp = bit(sq("b1"));
    const std::vector<int> instance = {sq("a1"), sq("b1"), sq("c1")};
    EXPECT_EQ(ternaryIndex(p, instance), 7);
}

TEST(TernaryIndex, OrderOfInstanceSquaresChangesTheIndex) {
    Position p;
    p.own = bit(sq("a1"));
    // Same two squares, reversed order, must give a different index unless symmetric by luck -
    // own-then-empty vs empty-then-own are genuinely different base-3 numbers here.
    const int forward = ternaryIndex(p, {sq("a1"), sq("b1")});
    const int backward = ternaryIndex(p, {sq("b1"), sq("a1")});
    EXPECT_NE(forward, backward);
}

} // namespace
} // namespace reversi::pattern
