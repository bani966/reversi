#include "reversi/perft.hpp"

#include <cstdint>
#include <gtest/gtest.h>

namespace reversi {
namespace {

// Published values for depths 1-8 from the start position, verified against
// https://aartbik.blogspot.com/2009/02/perft-for-reversi.html (matches OEIS A052586).
// See perft.hpp for the forced-pass-consumes-a-ply convention these numbers assume.
TEST(Perft, MatchesPublishedCountsFromStartPosition) {
    constexpr std::uint64_t kExpected[] = {4, 12, 56, 244, 1396, 8200, 55092, 390216};
    for (int depth = 1; depth <= 8; ++depth) {
        EXPECT_EQ(perft(Position::start(), depth), kExpected[depth - 1]) << "depth " << depth;
    }
}

TEST(Perft, DepthZeroIsOneLeaf) {
    EXPECT_EQ(perft(Position::start(), 0), std::uint64_t{1});
}

} // namespace
} // namespace reversi
