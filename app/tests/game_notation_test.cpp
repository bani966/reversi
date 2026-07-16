#include "GameNotation.hpp"

#include "reversi/position.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <gtest/gtest.h>

namespace {

TEST(SaveJson, RoundTripsModeMovesAndSettings) {
    const std::vector<int> moves = {reversi::squareIndex(5, 3), reversi::squareIndex(3, 4),
                                    reversi::squareIndex(2, 2)};
    const QJsonObject json = notation::toSaveJson(GameMode::HumanIsBlack, moves, true);

    const std::optional<notation::LoadedGame> loaded = notation::fromSaveJson(json);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->mode, GameMode::HumanIsBlack);
    EXPECT_EQ(loaded->moves, moves);
    EXPECT_TRUE(loaded->lastMoveHighlightEnabled);
}

TEST(SaveJson, RoundTripsAiVsAiMode) {
    const QJsonObject json = notation::toSaveJson(GameMode::AiVsAi, {}, false);

    const std::optional<notation::LoadedGame> loaded = notation::fromSaveJson(json);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->mode, GameMode::AiVsAi);
}

TEST(SaveJson, RoundTripsEmptyMoveListAndFalseSettings) {
    const QJsonObject json = notation::toSaveJson(GameMode::HumanVsHuman, {}, false);
    const std::optional<notation::LoadedGame> loaded = notation::fromSaveJson(json);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->mode, GameMode::HumanVsHuman);
    EXPECT_TRUE(loaded->moves.empty());
    EXPECT_FALSE(loaded->lastMoveHighlightEnabled);
}

TEST(SaveJson, RejectsWrongFormatField) {
    QJsonObject json = notation::toSaveJson(GameMode::HumanVsHuman, {}, false);
    json[QStringLiteral("format")] = QStringLiteral("not-a-reversi-save");
    EXPECT_FALSE(notation::fromSaveJson(json).has_value());
}

TEST(SaveJson, RejectsWrongVersion) {
    QJsonObject json = notation::toSaveJson(GameMode::HumanVsHuman, {}, false);
    json[QStringLiteral("version")] = 2;
    EXPECT_FALSE(notation::fromSaveJson(json).has_value());
}

TEST(SaveJson, RejectsUnknownMode) {
    QJsonObject json = notation::toSaveJson(GameMode::HumanVsHuman, {}, false);
    json[QStringLiteral("mode")] = QStringLiteral("HumanVsRobot");
    EXPECT_FALSE(notation::fromSaveJson(json).has_value());
}

TEST(SaveJson, RejectsNonStringMoveEntry) {
    QJsonObject json = notation::toSaveJson(GameMode::HumanVsHuman, {}, false);
    QJsonArray moves;
    moves.append(42); // not a string
    json[QStringLiteral("moves")] = moves;
    EXPECT_FALSE(notation::fromSaveJson(json).has_value());
}

TEST(SaveJson, RejectsGarbageMoveString) {
    QJsonObject json = notation::toSaveJson(GameMode::HumanVsHuman, {}, false);
    QJsonArray moves;
    moves.append(QStringLiteral("zz"));
    json[QStringLiteral("moves")] = moves;
    EXPECT_FALSE(notation::fromSaveJson(json).has_value());
}

TEST(SaveJson, MissingSettingsObjectDefaultsHighlightToFalse) {
    QJsonObject json = notation::toSaveJson(GameMode::HumanVsHuman, {}, true);
    json.remove(QStringLiteral("settings"));
    const std::optional<notation::LoadedGame> loaded = notation::fromSaveJson(json);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_FALSE(loaded->lastMoveHighlightEnabled);
}

TEST(Transcript, RoundTripsMoveList) {
    const std::vector<int> moves = {reversi::squareIndex(5, 3), reversi::squareIndex(3, 4),
                                    reversi::squareIndex(2, 2), reversi::squareIndex(3, 2)};
    const QString transcript = notation::toTranscript(moves);
    EXPECT_EQ(transcript, QStringLiteral("f4d5c3d3"));

    const std::optional<std::vector<int>> parsed = notation::fromTranscript(transcript);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, moves);
}

TEST(Transcript, EmptyTranscriptRoundTripsToEmptyMoveList) {
    const QString transcript = notation::toTranscript({});
    EXPECT_TRUE(transcript.isEmpty());
    const std::optional<std::vector<int>> parsed = notation::fromTranscript(transcript);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->empty());
}

TEST(Transcript, RejectsOddLength) {
    EXPECT_FALSE(notation::fromTranscript(QStringLiteral("f5d")).has_value());
}

TEST(Transcript, RejectsInvalidSquareChunk) {
    EXPECT_FALSE(notation::fromTranscript(QStringLiteral("f5zz")).has_value());
}

TEST(BoardString, RoundTripsBlackToMove) {
    const reversi::Position start = reversi::Position::start();
    const QString board = notation::toBoardString(start, true);
    EXPECT_EQ(board.size(), 65);
    EXPECT_EQ(board.back(), QLatin1Char('B'));

    const std::optional<std::pair<reversi::Position, bool>> parsed =
        notation::fromBoardString(board);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->first, start);
    EXPECT_TRUE(parsed->second);
}

TEST(BoardString, RoundTripsWhiteToMove) {
    const reversi::Position start = reversi::Position::start();
    const QString board = notation::toBoardString(start, false);
    EXPECT_EQ(board.back(), QLatin1Char('W'));

    const std::optional<std::pair<reversi::Position, bool>> parsed =
        notation::fromBoardString(board);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->first, start);
    EXPECT_FALSE(parsed->second);
}

TEST(BoardString, LowercaseSideCharIsAccepted) {
    const reversi::Position start = reversi::Position::start();
    QString board = notation::toBoardString(start, true);
    board[64] = QLatin1Char('b');
    const std::optional<std::pair<reversi::Position, bool>> parsed =
        notation::fromBoardString(board);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->second);
}

TEST(BoardString, RejectsWrongLength) {
    EXPECT_FALSE(notation::fromBoardString(QStringLiteral("tooShort")).has_value());
    const reversi::Position start = reversi::Position::start();
    const QString board = notation::toBoardString(start, true);
    EXPECT_FALSE(notation::fromBoardString(board + QLatin1Char('X')).has_value());
    EXPECT_FALSE(notation::fromBoardString(board.left(64)).has_value());
}

TEST(BoardString, RejectsInvalidSideChar) {
    const reversi::Position start = reversi::Position::start();
    QString board = notation::toBoardString(start, true);
    board[64] = QLatin1Char('?');
    EXPECT_FALSE(notation::fromBoardString(board).has_value());
}

} // namespace
