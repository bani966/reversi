#include "wthor_extractor/parser.hpp"

#include "reversi/position.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <vector>

namespace wthor {
namespace {

// decodeWthorMoveByte: the file's own square encoding is `10*row + column`, both 1-indexed
// (a1=11, h1=18, a8=81, h8=88) - empirically confirmed against a real downloaded file
// (WTH_1977.wtb) during this parser's development: the first move byte of a real game decoded
// to a square that is provably one of Othello's exactly four legal opening moves (not a
// coincidence - 4 out of 60 possible byte values), and all 720 real moves across 12 real games
// (including 9 games with at least one forced pass) replayed legally.
TEST(DecodeWthorMoveByte, CornersMatchKnownEncoding) {
    EXPECT_EQ(decodeWthorMoveByte(11), *reversi::squareFromString("a1"));
    EXPECT_EQ(decodeWthorMoveByte(18), *reversi::squareFromString("h1"));
    EXPECT_EQ(decodeWthorMoveByte(81), *reversi::squareFromString("a8"));
    EXPECT_EQ(decodeWthorMoveByte(88), *reversi::squareFromString("h8"));
}

TEST(DecodeWthorMoveByte, RealValidatedOpeningMoveDecodesToF5) {
    // 0x38 = 56 decimal: the actual first move byte of WTH_1977.wtb's first game, confirmed
    // during development to be exactly f5 - one of Othello's four legal opening moves.
    EXPECT_EQ(decodeWthorMoveByte(56), *reversi::squareFromString("f5"));
}

TEST(DecodeWthorMoveByte, ZeroByteIsEndOfMovesPadding) {
    EXPECT_EQ(decodeWthorMoveByte(0), std::nullopt);
}

TEST(DecodeWthorMoveByte, OutOfRangeRowOrColumnReturnsNullopt) {
    EXPECT_EQ(decodeWthorMoveByte(99), std::nullopt); // row 9: invalid
    EXPECT_EQ(decodeWthorMoveByte(19), std::nullopt); // col 9: invalid
    EXPECT_EQ(decodeWthorMoveByte(90), std::nullopt); // col 0: invalid
}

// The exact bug class this function exists to prevent (M5's FFO score-sign mismatch,
// recurring in a new shape): the same absolute final result must produce OPPOSITE signs
// depending on which color the position's mover actually is.
TEST(MoverRelativeFinalScore, SignFlipsWithMoverColor) {
    EXPECT_EQ(moverRelativeFinalScore(/*posMoverIsBlack=*/true, /*finalBlack=*/40,
                                      /*finalWhite=*/24),
              16);
    EXPECT_EQ(moverRelativeFinalScore(/*posMoverIsBlack=*/false, /*finalBlack=*/40,
                                      /*finalWhite=*/24),
              -16);
}

TEST(MoverRelativeFinalScore, DrawIsZeroRegardlessOfMoverColor) {
    EXPECT_EQ(moverRelativeFinalScore(true, 32, 32), 0);
    EXPECT_EQ(moverRelativeFinalScore(false, 32, 32), 0);
}

namespace {

// Builds a minimal synthetic .wtb file: 16-byte header (recordCount=1) + one 68-byte record
// using 4 REAL, already-validated moves from WTH_1977.wtb's first game (f5, d6, c3, f3 -
// bytes 0x38, 0x40, 0x21, 0x24), padded with 0x00 for the remaining 56 move bytes. Raw .wtb
// files are never committed to this repo (no confirmed redistribution license - see the M6
// plan's Context section), so tests construct their own tiny fixture in-memory rather than
// depending on a vendored binary blob.
std::filesystem::path writeSyntheticWtbFile(const std::filesystem::path& path) {
    std::vector<std::uint8_t> bytes(16 + 68, 0);
    // Header: only recordCount (offset 4-5, LE) matters for parsing; other fields left zero.
    bytes[4] = 1;
    bytes[5] = 0;

    constexpr std::size_t kRecordStart = 16;
    // tournament id = 7
    bytes[kRecordStart + 0] = 7;
    bytes[kRecordStart + 1] = 0;
    // black player id = 1
    bytes[kRecordStart + 2] = 1;
    bytes[kRecordStart + 3] = 0;
    // white player id = 2
    bytes[kRecordStart + 4] = 2;
    bytes[kRecordStart + 5] = 0;
    // real/theoretical score: arbitrary: this parser's tests never trust these for the
    // training target (see moverRelativeFinalScore's doc comment) - only replayGame's own
    // final disc counts matter for that.
    bytes[kRecordStart + 6] = 4;
    bytes[kRecordStart + 7] = 4;
    // moves: f5, d6, c3, f3, then implicit 0x00 padding (already zero-initialized)
    const std::array<std::uint8_t, 4> moves = {0x38, 0x40, 0x21, 0x24};
    for (std::size_t i = 0; i < moves.size(); ++i) {
        bytes[kRecordStart + 8 + i] = moves[i];
    }

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return path;
}

} // namespace

TEST(ParseWtbFile, ParsesSyntheticSingleGameFile) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "wthor_extractor_test_single_game.wtb";
    writeSyntheticWtbFile(path);

    const std::vector<GameRecord> records = parseWtbFile(path);
    std::filesystem::remove(path);

    ASSERT_EQ(records.size(), std::size_t{1});
    EXPECT_EQ(records[0].tournamentId, 7u);
    EXPECT_EQ(records[0].blackPlayerId, 1u);
    EXPECT_EQ(records[0].whitePlayerId, 2u);
    ASSERT_EQ(records[0].moves.size(), std::size_t{4});
    EXPECT_EQ(records[0].moves[0], *reversi::squareFromString("f5"));
    EXPECT_EQ(records[0].moves[1], *reversi::squareFromString("d6"));
    EXPECT_EQ(records[0].moves[2], *reversi::squareFromString("c3"));
    EXPECT_EQ(records[0].moves[3], *reversi::squareFromString("f3"));
}

TEST(ParseWtbFile, ThrowsOnSizeMismatch) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "wthor_extractor_test_truncated.wtb";
    {
        std::ofstream out(path, std::ios::binary);
        const std::vector<std::uint8_t> tooShort(16 + 68 - 1, 0); // one byte short
        out.write(reinterpret_cast<const char*>(tooShort.data()),
                  static_cast<std::streamsize>(tooShort.size()));
    }
    EXPECT_THROW(parseWtbFile(path), std::runtime_error);
    std::filesystem::remove(path);
}

TEST(ReplayGame, ReplaysFourRealValidatedMovesWithCorrectMoverTracking) {
    GameRecord record;
    record.moves = {*reversi::squareFromString("f5"), *reversi::squareFromString("d6"),
                    *reversi::squareFromString("c3"), *reversi::squareFromString("f3")};

    const ReplayedGame replayed = replayGame(record);

    ASSERT_EQ(replayed.positions.size(), std::size_t{4});
    // Black moves first (ply 0), then strictly alternates since none of these 4 real plies
    // included a forced pass (confirmed during development against the source game).
    EXPECT_TRUE(replayed.positions[0].moverIsBlack);
    EXPECT_FALSE(replayed.positions[1].moverIsBlack);
    EXPECT_TRUE(replayed.positions[2].moverIsBlack);
    EXPECT_FALSE(replayed.positions[3].moverIsBlack);
    EXPECT_EQ(replayed.finalBlackDiscs + replayed.finalWhiteDiscs, 4 + 4); // 4 plies played
}

TEST(ReplayGame, ThrowsOnIllegalMove) {
    GameRecord record;
    // f5 is legal from the start; d3 is NOT legal for White immediately after Black's f5 (it
    // is one of Black's OWN opening options, already taken by a different move here).
    record.moves = {*reversi::squareFromString("f5"), *reversi::squareFromString("d3")};
    EXPECT_THROW(replayGame(record), std::runtime_error);
}

} // namespace
} // namespace wthor
