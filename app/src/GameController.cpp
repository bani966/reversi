#include "GameController.hpp"

#include "reversi/eval.hpp"
#include "reversi/moves.hpp"

#include <QMetaObject>

namespace {
// Matches M2's measured/validated depth (100/100 vs greedy, 97/100 vs random in self-play).
constexpr int kAiSearchDepth = 10;
} // namespace

GameController::GameController(QObject* parent) : QObject(parent) {}

GameController::~GameController() {
    cancelAiSearch();
}

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
    cancelAiSearch();
    mode_ = mode;
    pos_ = reversi::Position::start();
    blackToMove_ = true;
    lastMoveSquare_ = -1;
    advanceTurn();
}

void GameController::setLastMoveHighlightEnabled(bool enabled) {
    lastMoveHighlightEnabled_ = enabled;
    emitBoardState(); // reflect the change immediately rather than waiting for the next move
}

void GameController::cancelAiSearch() {
    if (cancellation_) {
        cancellation_->requestStop();
    }
    ++searchGeneration_; // even a result that completes anyway is now stale
    if (aiThread_.joinable()) {
        aiThread_.join();
    }
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
    lastMoveSquare_ = square;
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
    if (!reversi::isGameOver(pos_) && !isHumanTurn()) {
        startAiSearch();
    }
}

void GameController::startAiSearch() {
    const QString mover = blackToMove_ ? QStringLiteral("Black") : QStringLiteral("White");
    emit statusChanged(mover + QStringLiteral(" to move. (AI thinking...)"));

    cancelAiSearch(); // defensive: keeps "at most one aiThread_ alive" airtight even if a
                      // future mode ever dispatches AI twice in a row
    ++searchGeneration_;
    const int myGeneration = searchGeneration_;
    cancellation_ = std::make_shared<reversi::CancellationToken>();
    const std::shared_ptr<reversi::CancellationToken> cancellationCopy = cancellation_;
    const reversi::Position posCopy = pos_;

    aiThread_ = std::thread([this, posCopy, myGeneration, cancellationCopy] {
        const reversi::SearchResult result = reversi::search(
            posCopy, kAiSearchDepth, reversi::evaluateDiscDifferential, cancellationCopy.get());
        QMetaObject::invokeMethod(
            this, [this, result, myGeneration] { onAiSearchFinished(result, myGeneration); },
            Qt::QueuedConnection);
    });
}

void GameController::onAiSearchFinished(const reversi::SearchResult& result, int generation) {
    if (generation != searchGeneration_ || !result.completed) {
        return; // superseded by a new game/search, or this one was cancelled: discard
    }
    pos_ = reversi::applyMove(pos_, result.bestMove);
    blackToMove_ = !blackToMove_;
    lastMoveSquare_ = result.bestMove;
    advanceTurn();
}

void GameController::emitBoardState() {
    BoardWidget::DisplayState state;
    state.blackDiscs = blackToMove_ ? pos_.own : pos_.opp;
    state.whiteDiscs = blackToMove_ ? pos_.opp : pos_.own;
    state.legalMoveHighlights = (!reversi::isGameOver(pos_) && isHumanTurn())
                                    ? reversi::legalMoves(pos_)
                                    : reversi::Bitboard{0};
    state.lastMoveSquare = lastMoveHighlightEnabled_ ? lastMoveSquare_ : -1;
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
