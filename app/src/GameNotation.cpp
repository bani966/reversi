#include "GameNotation.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonValue>

namespace {

QString modeToString(GameMode mode) {
    switch (mode) {
    case GameMode::HumanVsHuman:
        return QStringLiteral("HumanVsHuman");
    case GameMode::HumanIsBlack:
        return QStringLiteral("HumanIsBlack");
    case GameMode::HumanIsWhite:
        return QStringLiteral("HumanIsWhite");
    case GameMode::AiVsAi:
        return QStringLiteral("AiVsAi");
    }
    return QStringLiteral("HumanVsHuman"); // unreachable
}

std::optional<GameMode> modeFromString(const QString& s) {
    if (s == QStringLiteral("HumanVsHuman")) {
        return GameMode::HumanVsHuman;
    }
    if (s == QStringLiteral("HumanIsBlack")) {
        return GameMode::HumanIsBlack;
    }
    if (s == QStringLiteral("HumanIsWhite")) {
        return GameMode::HumanIsWhite;
    }
    if (s == QStringLiteral("AiVsAi")) {
        return GameMode::AiVsAi;
    }
    return std::nullopt;
}

} // namespace

namespace notation {

QJsonObject toSaveJson(GameMode mode, const std::vector<int>& moves,
                       bool lastMoveHighlightEnabled) {
    QJsonArray movesArray;
    for (int square : moves) {
        movesArray.append(QString::fromStdString(reversi::squareToString(square)));
    }

    QJsonObject settings;
    settings[QStringLiteral("lastMoveHighlightEnabled")] = lastMoveHighlightEnabled;

    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("reversi-save");
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("savedAt")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root[QStringLiteral("mode")] = modeToString(mode);
    root[QStringLiteral("settings")] = settings;
    root[QStringLiteral("moves")] = movesArray;
    return root;
}

std::optional<LoadedGame> fromSaveJson(const QJsonObject& json) {
    if (json.value(QStringLiteral("format")).toString() != QStringLiteral("reversi-save")) {
        return std::nullopt;
    }
    if (json.value(QStringLiteral("version")).toInt(-1) != 1) {
        return std::nullopt;
    }
    const std::optional<GameMode> mode =
        modeFromString(json.value(QStringLiteral("mode")).toString());
    if (!mode) {
        return std::nullopt;
    }
    if (!json.value(QStringLiteral("moves")).isArray()) {
        return std::nullopt;
    }

    const QJsonArray movesArray = json.value(QStringLiteral("moves")).toArray();
    std::vector<int> moves;
    moves.reserve(static_cast<std::size_t>(movesArray.size()));
    for (const QJsonValue& value : movesArray) {
        if (!value.isString()) {
            return std::nullopt;
        }
        const std::optional<int> square = reversi::squareFromString(value.toString().toStdString());
        if (!square) {
            return std::nullopt;
        }
        moves.push_back(*square);
    }

    const bool lastMoveHighlightEnabled = json.value(QStringLiteral("settings"))
                                              .toObject()
                                              .value(QStringLiteral("lastMoveHighlightEnabled"))
                                              .toBool(false);

    LoadedGame result;
    result.mode = *mode;
    result.moves = std::move(moves);
    result.lastMoveHighlightEnabled = lastMoveHighlightEnabled;
    return result;
}

QString toTranscript(const std::vector<int>& moves) {
    QString result;
    result.reserve(static_cast<int>(moves.size()) * 2);
    for (int square : moves) {
        result += QString::fromStdString(reversi::squareToString(square));
    }
    return result;
}

std::optional<std::vector<int>> fromTranscript(const QString& transcript) {
    if (transcript.size() % 2 != 0) {
        return std::nullopt;
    }
    std::vector<int> moves;
    moves.reserve(static_cast<std::size_t>(transcript.size() / 2));
    for (int i = 0; i < transcript.size(); i += 2) {
        const std::optional<int> square =
            reversi::squareFromString(transcript.mid(i, 2).toStdString());
        if (!square) {
            return std::nullopt;
        }
        moves.push_back(*square);
    }
    return moves;
}

QString toBoardString(const reversi::Position& position, bool blackToMove) {
    const reversi::Bitboard black = blackToMove ? position.own : position.opp;
    const reversi::Bitboard white = blackToMove ? position.opp : position.own;

    QString result;
    result.reserve(reversi::kBoardSquares + 1);
    for (int square = 0; square < reversi::kBoardSquares; ++square) {
        const reversi::Bitboard mask = reversi::bit(square);
        if ((black & mask) != 0) {
            result += QLatin1Char('X');
        } else if ((white & mask) != 0) {
            result += QLatin1Char('O');
        } else {
            result += QLatin1Char('-');
        }
    }
    result += blackToMove ? QLatin1Char('B') : QLatin1Char('W');
    return result;
}

std::optional<std::pair<reversi::Position, bool>> fromBoardString(const QString& board) {
    if (board.size() != reversi::kBoardSquares + 1) {
        return std::nullopt;
    }

    const QChar sideChar = board.at(reversi::kBoardSquares);
    bool blackToMove = false;
    if (sideChar == QLatin1Char('B') || sideChar == QLatin1Char('b')) {
        blackToMove = true;
    } else if (sideChar == QLatin1Char('W') || sideChar == QLatin1Char('w')) {
        blackToMove = false;
    } else {
        return std::nullopt;
    }

    const std::optional<reversi::Position> position = reversi::Position::fromBoardString(
        board.left(reversi::kBoardSquares).toStdString(), blackToMove);
    if (!position) {
        return std::nullopt;
    }
    return std::make_pair(*position, blackToMove);
}

} // namespace notation
