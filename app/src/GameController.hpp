#pragma once

#include "BoardWidget.hpp"

#include "reversi/cancellation.hpp"
#include "reversi/position.hpp"
#include "reversi/search.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>
#include <thread>

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
    ~GameController() override;

    void newGame(GameMode mode);

    // Requests the in-flight AI search (if any) to stop and blocks until its worker thread
    // has finished. Safe to call when no search is running. Must be called before this object
    // (and the Position it captured for the worker thread) is destroyed - this is the concrete
    // mechanism behind "closing the window while the AI is thinking must not hang or crash".
    void cancelAiSearch();

    // Off by default: no Settings UI exists yet to expose this (that's M9's job), so the
    // control point is here now but nothing calls it yet. lastMoveSquare_ is still tracked
    // unconditionally either way - only whether it's exposed via DisplayState is gated.
    void setLastMoveHighlightEnabled(bool enabled);

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
    // Square of the most recently played move, -1 before the first move of a game. Not reset
    // on a forced pass (a pass doesn't change the board, so the last real move stays the most
    // relevant thing to highlight) - only newGame() and a fresh applyMove() touch this.
    int lastMoveSquare_ = -1;
    bool lastMoveHighlightEnabled_ = false;

    std::thread aiThread_;
    std::shared_ptr<reversi::CancellationToken> cancellation_;
    // Bumped on every new search and every cancellation, so a result that arrives after being
    // superseded (by a new search or a new game) can be recognized as stale and discarded even
    // if it otherwise looks like a normal completed result.
    int searchGeneration_ = 0;

    bool isHumanTurn() const;
    void advanceTurn();
    void emitBoardState();
    void emitStatus(const QStringList& passMessages);

    void startAiSearch();
    void onAiSearchFinished(const reversi::SearchResult& result, int generation);
};
