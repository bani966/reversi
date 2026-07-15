#pragma once

#include "BoardWidget.hpp"

#include "reversi/position.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

// The "thin controller/view-model": owns the game state and all turn-taking/rules
// orchestration. BoardWidget only renders and reports clicks; it never touches reversi::
// rules functions itself.
class GameController : public QObject {
    Q_OBJECT

public:
    enum class GameMode {
        HumanVsHuman,
        HumanIsBlack,
        HumanIsWhite,
    };

    explicit GameController(QObject* parent = nullptr);

    void newGame(GameMode mode);

public slots:
    void onSquareClicked(int square);

signals:
    void boardChanged(const BoardWidget::DisplayState& state);
    void statusChanged(const QString& text);

private:
    // Own/opp-relative-to-mover, same convention as reversi::Position everywhere in engine/;
    // pos_.own belongs to black iff blackToMove_, toggled in lockstep with applyMove/applyPass
    // (the same invariant engine/src/selfplay.cpp's playGame relies on).
    reversi::Position pos_;
    bool blackToMove_ = true;
    GameMode mode_ = GameMode::HumanVsHuman;

    bool isHumanTurn() const;
    void advanceTurn();
    void emitBoardState();
    void emitStatus(const QStringList& passMessages);
};
