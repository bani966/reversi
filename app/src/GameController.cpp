#include "GameController.hpp"

#include "GameNotation.hpp"

#include "reversi/eval.hpp"
#include "reversi/moves.hpp"

#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMetaObject>

#include <utility>

namespace {
// M4: the AI thinks on a wall-clock budget via iterative deepening rather than a fixed
// depth - consistent response time for the user regardless of game phase, and much deeper
// search than M2's fixed depth 10 in the same time. The depth cap is a safety net for
// trivially-solvable late endgames, not the usual stopping reason.
constexpr int kAiMaxSearchDepth = 24;
constexpr reversi::TimeBudget kAiTimeBudget{std::chrono::milliseconds{800},
                                            std::chrono::milliseconds{2500}};
// 2^20 entries (~24 MB). Reused across the AI's moves within one game (cleared on newGame):
// entries from earlier searches stay valid - keys are full positions, stored depths are
// remaining-depth - and pre-warm both cutoffs and move ordering for later searches.
constexpr std::size_t kTranspositionTableEntries = std::size_t{1} << 20;
} // namespace

GameController::GameController(QObject* parent)
    : QObject(parent),
      tt_(std::make_unique<reversi::TranspositionTable>(kTranspositionTableEntries)),
      solverTt_(std::make_unique<reversi::TranspositionTable>(kTranspositionTableEntries)) {}

GameController::~GameController() {
    cancelAiSearch();
}

bool GameController::isHumanTurn() const {
    return isHumanTurnFor(blackToMove_);
}

bool GameController::isHumanTurnFor(bool blackToMove) const {
    switch (mode_) {
    case GameMode::HumanVsHuman:
        return true;
    case GameMode::HumanIsBlack:
        return blackToMove;
    case GameMode::HumanIsWhite:
        return !blackToMove;
    }
    return true; // unreachable
}

void GameController::newGame(GameMode mode) {
    cancelAiSearch(); // joins the worker first, so clearing the tables below is race-free
    tt_->clear();     // entries describe the previous game's positions; don't carry them over
    solverTt_->clear();
    mode_ = mode;
    pos_ = reversi::Position::start();
    blackToMove_ = true;
    lastMoveSquare_ = -1;
    history_.clear();
    historyIndex_ = 0;
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
    commitMove(square);
    advanceTurn();
}

void GameController::commitMove(int square) {
    history_.resize(historyIndex_ + 1); // discard any redo tail before this new move
    pos_ = reversi::applyMove(pos_, square);
    blackToMove_ = !blackToMove_;
    lastMoveSquare_ = square;
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
    // The single place a new history_ entry is recorded during live play - always the fully
    // pass-resolved resting position (see HistoryEntry's doc comment in the header).
    history_.push_back({pos_, blackToMove_, lastMoveSquare_});
    historyIndex_ = history_.size() - 1;
    finalizeTurn(passMessages);
}

void GameController::finalizeTurn(const QStringList& passMessages) {
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

    // tt_/solverTt_ are captured raw: the worker only touches them while running, and every
    // path that mutates or destroys them (newGame, destructor) joins the worker via
    // cancelAiSearch first. book_/mpcModel_ have no mutator yet (see GameController.hpp), so
    // there is nothing to race on there either - a future loading path must preserve this same
    // join-before-mutate discipline.
    reversi::TranspositionTable* const tt = tt_.get();
    reversi::TranspositionTable* const solverTt = solverTt_.get();
    reversi::MoveSelectorConfig config;
    config.book = book_;
    config.maxDepth = kAiMaxSearchDepth;
    config.budget = kAiTimeBudget;
    config.mpcModel = mpcModel_;
    aiThread_ = std::thread([this, posCopy, myGeneration, cancellationCopy, config, tt, solverTt] {
        const reversi::SearchResult result =
            reversi::selectMove(posCopy, reversi::evaluateDiscDifferential, config,
                                cancellationCopy.get(), tt, solverTt);
        QMetaObject::invokeMethod(
            this, [this, result, myGeneration] { onAiSearchFinished(result, myGeneration); },
            Qt::QueuedConnection);
    });
}

void GameController::onAiSearchFinished(const reversi::SearchResult& result, int generation) {
    if (generation != searchGeneration_ || !result.completed) {
        return; // superseded by a new game/search, or this one was cancelled: discard
    }
    commitMove(result.bestMove);
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

bool GameController::canUndo() const {
    return historyIndex_ > 0;
}

bool GameController::canRedo() const {
    return historyIndex_ + 1 < history_.size();
}

void GameController::undo() {
    if (!canUndo()) {
        return;
    }
    cancelAiSearch();
    do {
        --historyIndex_;
    } while (historyIndex_ > 0 && !isHumanTurnFor(history_[historyIndex_].blackToMove));
    restoreFromHistory(historyIndex_);
}

void GameController::redo() {
    if (!canRedo()) {
        return;
    }
    cancelAiSearch();
    do {
        ++historyIndex_;
    } while (historyIndex_ + 1 < history_.size() &&
             !isHumanTurnFor(history_[historyIndex_].blackToMove));
    restoreFromHistory(historyIndex_);
}

void GameController::restoreFromHistory(std::size_t index) {
    const HistoryEntry& entry = history_[index];
    pos_ = entry.position;
    blackToMove_ = entry.blackToMove;
    lastMoveSquare_ = entry.lastMoveSquare;
    emitBoardState();
    emitStatus({}); // no pass messages - this is a restore, not a live pass sequence
    // Deliberately no advanceTurn()/startAiSearch() call here - see the public undo()/redo()
    // doc comment in the header for why.
}

std::optional<std::vector<GameController::HistoryEntry>>
GameController::replayMoves(const reversi::Position& start, bool startBlackToMove,
                            const std::vector<int>& moves) const {
    std::vector<HistoryEntry> history;
    history.reserve(moves.size() + 1);
    reversi::Position pos = start;
    bool blackToMove = startBlackToMove;
    int lastSquare = -1;

    const auto resolvePasses = [&pos, &blackToMove] {
        while (!reversi::isGameOver(pos) && !reversi::hasLegalMove(pos)) {
            pos = reversi::applyPass(pos);
            blackToMove = !blackToMove;
        }
    };

    // The start position itself might require an immediate pass (e.g. an imported position
    // where the recorded side-to-move has no legal move) - resolve that before recording index 0
    // too, so every entry (including the first) is a fully pass-resolved resting position, same
    // invariant advanceTurn() maintains for live play.
    resolvePasses();
    history.push_back({pos, blackToMove, lastSquare});

    for (int square : moves) {
        const reversi::Bitboard legal = reversi::legalMoves(pos);
        if (square < 0 || square >= reversi::kBoardSquares || (legal & reversi::bit(square)) == 0) {
            return std::nullopt;
        }
        pos = reversi::applyMove(pos, square);
        blackToMove = !blackToMove;
        lastSquare = square;
        resolvePasses();
        history.push_back({pos, blackToMove, lastSquare});
    }
    return history;
}

void GameController::applyLoadedHistory(GameMode mode, std::vector<HistoryEntry> history,
                                        bool lastMoveHighlightEnabled) {
    cancelAiSearch();
    tt_->clear(); // a different position tree than whatever was loaded before - see the header's
    solverTt_->clear(); // undo/redo-vs-load doc comment for why this differs from undo/redo.
    mode_ = mode;
    history_ = std::move(history);
    historyIndex_ = history_.size() - 1;
    const HistoryEntry& current = history_[historyIndex_];
    pos_ = current.position;
    blackToMove_ = current.blackToMove;
    lastMoveSquare_ = current.lastMoveSquare;
    lastMoveHighlightEnabled_ = lastMoveHighlightEnabled;
    finalizeTurn({}); // no pass messages to report for a freshly loaded/imported game
}

std::vector<int> GameController::currentMoveList() const {
    std::vector<int> moves;
    moves.reserve(historyIndex_);
    for (std::size_t i = 1; i <= historyIndex_; ++i) {
        moves.push_back(history_[i].lastMoveSquare);
    }
    return moves;
}

bool GameController::saveGame(const QString& filePath) const {
    const QJsonObject json =
        notation::toSaveJson(mode_, currentMoveList(), lastMoveHighlightEnabled_);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        lastErrorMessage_ = QStringLiteral("Could not open file for writing.");
        return false;
    }
    file.write(QJsonDocument(json).toJson());
    return true;
}

bool GameController::loadGame(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        lastErrorMessage_ = QStringLiteral("Could not open file.");
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        lastErrorMessage_ = QStringLiteral("Not a valid save file.");
        return false;
    }
    const std::optional<notation::LoadedGame> loaded = notation::fromSaveJson(doc.object());
    if (!loaded) {
        lastErrorMessage_ =
            QStringLiteral("Save file is malformed or from an incompatible version.");
        return false;
    }
    const std::optional<std::vector<HistoryEntry>> history =
        replayMoves(reversi::Position::start(), true, loaded->moves);
    if (!history) {
        lastErrorMessage_ = QStringLiteral("Save file contains an illegal move sequence.");
        return false;
    }
    applyLoadedHistory(loaded->mode, *history, loaded->lastMoveHighlightEnabled);
    return true;
}

bool GameController::exportTranscript(const QString& filePath) const {
    const QString transcript = notation::toTranscript(currentMoveList());
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        lastErrorMessage_ = QStringLiteral("Could not open file for writing.");
        return false;
    }
    file.write(transcript.toUtf8());
    return true;
}

bool GameController::importTranscript(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        lastErrorMessage_ = QStringLiteral("Could not open file.");
        return false;
    }
    const QString text = QString::fromUtf8(file.readAll()).trimmed();
    const std::optional<std::vector<int>> moves = notation::fromTranscript(text);
    if (!moves) {
        lastErrorMessage_ =
            QStringLiteral("Not a valid transcript (must be an even-length sequence of square "
                           "names, e.g. \"f5d6c3\").");
        return false;
    }
    const std::optional<std::vector<HistoryEntry>> history =
        replayMoves(reversi::Position::start(), true, *moves);
    if (!history) {
        lastErrorMessage_ = QStringLiteral("Transcript contains an illegal move sequence.");
        return false;
    }
    applyLoadedHistory(mode_, *history,
                       lastMoveHighlightEnabled_); // preserves current mode/settings
    return true;
}

bool GameController::exportPosition(const QString& filePath) const {
    const QString board = notation::toBoardString(pos_, blackToMove_);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        lastErrorMessage_ = QStringLiteral("Could not open file for writing.");
        return false;
    }
    file.write(board.toUtf8());
    return true;
}

bool GameController::importPosition(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        lastErrorMessage_ = QStringLiteral("Could not open file.");
        return false;
    }
    const QString text = QString::fromUtf8(file.readAll()).trimmed();
    const std::optional<std::pair<reversi::Position, bool>> parsed =
        notation::fromBoardString(text);
    if (!parsed) {
        lastErrorMessage_ = QStringLiteral("Not a valid 65-character position string.");
        return false;
    }
    // A fresh single-entry history: there's no move list leading up to an arbitrary imported
    // board, so nothing before it to undo into. mode_ is preserved, not reset to HumanVsHuman.
    std::vector<HistoryEntry> history;
    history.push_back({parsed->first, parsed->second, -1});
    applyLoadedHistory(mode_, std::move(history), lastMoveHighlightEnabled_);
    return true;
}
