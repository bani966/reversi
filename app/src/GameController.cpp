#include "GameController.hpp"

#include "reversi/moves.hpp"

GameController::GameController(QObject* parent) : QObject(parent) {}

bool GameController::isHumanTurn() const {
    switch (mode_) {
    case GameMode::HumanVsHuman:
        return true;
    case GameMode::HumanIsBlack:
        return blackToMove_;
    case GameMode::HumanIsWhite:
        return !blackToMove_;
    }
    return true; // unreachable
}

void GameController::newGame(GameMode mode) {
    mode_ = mode;
    pos_ = reversi::Position::start();
    blackToMove_ = true;
    advanceTurn();
}

void GameController::onSquareClicked(int square) {
    if (reversi::isGameOver(pos_) || !isHumanTurn()) {
        return;
    }
    const reversi::Bitboard moves = reversi::legalMoves(pos_);
    if ((moves & reversi::bit(square)) == 0) {
        return; // not a legal move for the current mover: ignore, no-op
    }
    pos_ = reversi::applyMove(pos_, square);
    blackToMove_ = !blackToMove_;
    advanceTurn();
}

void GameController::advanceTurn() {
    // A pass is forced, not a choice, so there's nothing for the user to confirm - just apply
    // any consecutive forced passes and report what happened once, rather than a fleeting
    // per-pass message that a fully synchronous UI update would never actually get to show.
    QStringList passMessages;
    while (!reversi::isGameOver(pos_) && !reversi::hasLegalMove(pos_)) {
        const QString mover = blackToMove_ ? QStringLiteral("Black") : QStringLiteral("White");
        passMessages << mover + QStringLiteral(" has no legal move and passes.");
        pos_ = reversi::applyPass(pos_);
        blackToMove_ = !blackToMove_;
    }
    emitBoardState();
    emitStatus(passMessages);
}

void GameController::emitBoardState() {
    BoardWidget::DisplayState state;
    state.blackDiscs = blackToMove_ ? pos_.own : pos_.opp;
    state.whiteDiscs = blackToMove_ ? pos_.opp : pos_.own;
    state.legalMoveHighlights = (!reversi::isGameOver(pos_) && isHumanTurn())
                                    ? reversi::legalMoves(pos_)
                                    : reversi::Bitboard{0};
    emit boardChanged(state);
}

void GameController::emitStatus(const QStringList& passMessages) {
    QString text = passMessages.join(QStringLiteral(" "));
    if (!text.isEmpty()) {
        text += QStringLiteral(" ");
    }

    if (reversi::isGameOver(pos_)) {
        const int blackDiscs = blackToMove_ ? pos_.ownCount() : pos_.oppCount();
        const int whiteDiscs = blackToMove_ ? pos_.oppCount() : pos_.ownCount();
        if (blackDiscs > whiteDiscs) {
            text += QStringLiteral("Game over: Black wins %1-%2.").arg(blackDiscs).arg(whiteDiscs);
        } else if (whiteDiscs > blackDiscs) {
            text += QStringLiteral("Game over: White wins %1-%2.").arg(whiteDiscs).arg(blackDiscs);
        } else {
            text += QStringLiteral("Game over: draw %1-%2.").arg(blackDiscs).arg(whiteDiscs);
        }
    } else {
        const QString mover = blackToMove_ ? QStringLiteral("Black") : QStringLiteral("White");
        text += mover + QStringLiteral(" to move.");
    }
    emit statusChanged(text);
}
