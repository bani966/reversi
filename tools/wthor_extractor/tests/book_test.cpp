#include "wthor_extractor/book.hpp"

#include "reversi/pattern.hpp"
#include "reversi/position.hpp"

#include <cstdint>
#include <gtest/gtest.h>
#include <sstream>

namespace wthor {
namespace {

int sq(const char* algebraic) {
    return *reversi::squareFromString(algebraic);
}

unsigned totalObservations(const BookAccumulator& acc) {
    unsigned total = 0;
    for (const auto& [positionKey, moveStats] : acc) {
        for (const auto& [move, stats] : moveStats) {
            total += stats.gameCount;
        }
    }
    return total;
}

// Positions arrive with strictly increasing discCount (as real replay does): 4 (ply 0), 5
// (ply 1), 6 (ply 2). With maxPly = 1, only the first two should be folded into `acc`.
TEST(AccumulateBookGame, ExcludesPositionsBeyondTheMaxPlyWindow) {
    ReplayedGame game;
    reversi::Position pos0;
    pos0.own = reversi::bit(sq("a1")) | reversi::bit(sq("b1")) | reversi::bit(sq("c1")) |
               reversi::bit(sq("d1"));
    reversi::Position pos1 = pos0;
    pos1.own |= reversi::bit(sq("e1"));
    reversi::Position pos2 = pos1;
    pos2.own |= reversi::bit(sq("f1"));
    ASSERT_EQ(pos0.discCount(), 4);
    ASSERT_EQ(pos1.discCount(), 5);
    ASSERT_EQ(pos2.discCount(), 6);

    game.positions = {{pos0, true, sq("e1")}, {pos1, true, sq("f1")}, {pos2, true, sq("g1")}};
    game.finalBlackDiscs = 40;
    game.finalWhiteDiscs = 24;

    BookAccumulator acc;
    accumulateBookGame(game, /*maxPly=*/1, acc);
    EXPECT_EQ(totalObservations(acc), 2u);
}

// Reuses the exact hand-derived example from pattern_test.cpp's Canonicalize tests: a mover
// disc alone at h1 canonicalizes to a1 via Rotate90, and a move played at g1 canonicalizes to
// a2 under that same symmetry. This is the book's "build time" half of pattern.hpp's
// directional contract - verified end to end here, not just trusted from canonicalize()'s own
// unit tests.
TEST(AccumulateBookGame, StoresTheCanonicalizedPositionAndCanonicalizedMove) {
    ReplayedGame game;
    reversi::Position pos;
    pos.own = reversi::bit(sq("h1"));
    game.positions = {{pos, /*moverIsBlack=*/true, sq("g1")}};
    game.finalBlackDiscs = 40;
    game.finalWhiteDiscs = 24;

    BookAccumulator acc;
    accumulateBookGame(game, /*maxPly=*/20, acc);

    const auto key = std::make_pair(reversi::bit(sq("a1")), reversi::Bitboard{0});
    const auto positionIt = acc.find(key);
    ASSERT_NE(positionIt, acc.end()) << "expected the canonical position (own=bit(a1)) in acc";
    const auto moveIt = positionIt->second.find(sq("a2"));
    ASSERT_NE(moveIt, positionIt->second.end()) << "expected the canonicalized move a2 in acc";
    EXPECT_EQ(moveIt->second.gameCount, 1u);
    EXPECT_EQ(moveIt->second.outcomeSum, 16); // moverRelativeFinalScore(true, 40, 24) == 16
}

TEST(FinalizeBook, SkipsCanonicalPositionsBelowMinCount) {
    BookAccumulator acc;
    const auto belowThreshold = std::make_pair(reversi::Bitboard{1}, reversi::Bitboard{0});
    acc[belowThreshold][sq("a1")] = BookMoveStats{/*gameCount=*/2, /*outcomeSum=*/10};

    const std::vector<BookEntry> entries = finalizeBook(acc, /*minCount=*/5);
    EXPECT_TRUE(entries.empty());
}

// Two candidate moves tie on gameCount (3 each); the tiebreaker is average outcome
// (outcomeSum / gameCount), not the raw outcomeSum - move B has a lower total sum (18 < 15 is
// false, so pick numbers where the raw-sum comparison would give the WRONG answer if used
// instead of the average): move A has gameCount=3, outcomeSum=15 (avg 5); move B has
// gameCount=3, outcomeSum=30 (avg 10). B must win on the higher average.
TEST(FinalizeBook, PicksHighestGameCountTiesBrokenByHigherAverageOutcome) {
    BookAccumulator acc;
    const auto key = std::make_pair(reversi::Bitboard{1}, reversi::Bitboard{0});
    acc[key][sq("a1")] = BookMoveStats{/*gameCount=*/3, /*outcomeSum=*/15};
    acc[key][sq("b1")] = BookMoveStats{/*gameCount=*/3, /*outcomeSum=*/30};

    const std::vector<BookEntry> entries = finalizeBook(acc, /*minCount=*/5);
    ASSERT_EQ(entries.size(), std::size_t{1});
    EXPECT_EQ(entries[0].move, sq("b1"));
    EXPECT_EQ(entries[0].gameCount, 3u);
    EXPECT_EQ(entries[0].outcomeSum, 30);
}

TEST(FinalizeBook, PicksHighestGameCountEvenWithALowerAverageOutcome) {
    BookAccumulator acc;
    const auto key = std::make_pair(reversi::Bitboard{1}, reversi::Bitboard{0});
    acc[key][sq("a1")] = BookMoveStats{/*gameCount=*/10, /*outcomeSum=*/10}; // avg 1
    acc[key][sq("b1")] = BookMoveStats{/*gameCount=*/2, /*outcomeSum=*/40};  // avg 20

    const std::vector<BookEntry> entries = finalizeBook(acc, /*minCount=*/5);
    ASSERT_EQ(entries.size(), std::size_t{1});
    EXPECT_EQ(entries[0].move, sq("a1")) << "gameCount is the primary key, average is only the "
                                            "tiebreaker for EQUAL gameCounts";
}

TEST(FinalizeBook, OutputIsSortedAscendingByOwnThenOpp) {
    BookAccumulator acc;
    acc[{reversi::Bitboard{5}, reversi::Bitboard{0}}][sq("a1")] = BookMoveStats{5, 0};
    acc[{reversi::Bitboard{1}, reversi::Bitboard{9}}][sq("a1")] = BookMoveStats{5, 0};
    acc[{reversi::Bitboard{1}, reversi::Bitboard{2}}][sq("a1")] = BookMoveStats{5, 0};

    const std::vector<BookEntry> entries = finalizeBook(acc, /*minCount=*/5);
    ASSERT_EQ(entries.size(), std::size_t{3});
    EXPECT_EQ(entries[0].own, reversi::Bitboard{1});
    EXPECT_EQ(entries[0].opp, reversi::Bitboard{2});
    EXPECT_EQ(entries[1].own, reversi::Bitboard{1});
    EXPECT_EQ(entries[1].opp, reversi::Bitboard{9});
    EXPECT_EQ(entries[2].own, reversi::Bitboard{5});
}

namespace independent {

std::uint32_t readU32LE(std::istream& in) {
    unsigned char b[4];
    in.read(reinterpret_cast<char*>(b), 4);
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8U) |
           (static_cast<std::uint32_t>(b[2]) << 16U) | (static_cast<std::uint32_t>(b[3]) << 24U);
}

std::uint64_t readU64LE(std::istream& in) {
    const std::uint64_t low = readU32LE(in);
    const std::uint64_t high = readU32LE(in);
    return low | (high << 32U);
}

} // namespace independent

// Independently re-parses writeBookFile's output byte-by-byte, without calling any of
// engine/'s eventual OpeningBook reader (step 3 hasn't been written yet) - same discipline as
// pattern_eval_test.cpp's MatchesIndependentByteLevelRecomputation.
TEST(WriteBookFile, MatchesIndependentByteLevelRecomputation) {
    std::vector<BookEntry> entries;
    entries.push_back({reversi::Bitboard{1}, reversi::Bitboard{2}, sq("c3"), 7u, -5});
    entries.push_back({reversi::Bitboard{100}, reversi::Bitboard{200}, sq("d4"), 12u, 30});

    std::ostringstream out(std::ios::binary);
    writeBookFile(entries, out);
    const std::string bytes = out.str();
    // u32 count + 2 * (8 + 8 + 4 + 4 + 4) bytes
    ASSERT_EQ(bytes.size(), std::size_t{4 + 2 * 28});

    std::istringstream in(bytes, std::ios::binary);
    EXPECT_EQ(independent::readU32LE(in), 2u);
    for (const BookEntry& expected : entries) {
        EXPECT_EQ(independent::readU64LE(in), expected.own);
        EXPECT_EQ(independent::readU64LE(in), expected.opp);
        EXPECT_EQ(static_cast<std::int32_t>(independent::readU32LE(in)), expected.move);
        EXPECT_EQ(independent::readU32LE(in), expected.gameCount);
        EXPECT_EQ(static_cast<std::int32_t>(independent::readU32LE(in)), expected.outcomeSum);
    }
}

} // namespace
} // namespace wthor
