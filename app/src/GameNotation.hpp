#pragma once

#include "GameMode.hpp"

#include "reversi/position.hpp"

#include <QJsonObject>
#include <QString>

#include <optional>
#include <utility>
#include <vector>

// Pure text/JSON <-> (mode, move-list, settings) conversion for GameController's save/load and
// import/export features (M9 phase 2). No QWidget/GUI coupling - only QtCore types - so this is
// independently unit-testable (app_tests) without a QApplication. This module only knows about
// TEXT/JSON *shape*; it does not validate Othello move legality itself (a malformed file can
// still produce a `LoadedGame` with an illegal move list) - GameController is responsible for
// replaying the returned move list through the real engine rules and rejecting it there, exactly
// once, in one place (the same "one implementation of game rules" principle the WTHOR pipeline
// follows for the same reason).
namespace notation {

struct LoadedGame {
    GameMode mode = GameMode::HumanVsHuman;
    std::vector<int> moves; // squares in play order; forced passes are never encoded
    bool lastMoveHighlightEnabled = false;
};

// Save/load: this app's own JSON format ("reversi-save", versioned from the start).
QJsonObject toSaveJson(GameMode mode, const std::vector<int>& moves,
                       bool lastMoveHighlightEnabled);
std::optional<LoadedGame> fromSaveJson(const QJsonObject& json);

// Transcript: the standard Othello convention - concatenated lowercase two-character squares in
// play order, no separators, no pass markers (e.g. "f5d6c3d3c4...").
QString toTranscript(const std::vector<int>& moves);
std::optional<std::vector<int>> fromTranscript(const QString& transcript);

// Position (board) snapshot: the existing 64-character row-major a1..h8 board convention
// (X=black/O=white/-=empty, matching reversi::Position::fromBoardString exactly) plus one
// trailing side-to-move character (B/W) - 65 characters total. A self-contained sibling to the
// existing space-separated "<64char> <Black|White>" FFO format used by `cli solve`/
// tests/data/ffo_easy.txt, packed into one portable line instead of two tokens.
QString toBoardString(const reversi::Position& position, bool blackToMove);
std::optional<std::pair<reversi::Position, bool>> fromBoardString(const QString& board);

} // namespace notation
