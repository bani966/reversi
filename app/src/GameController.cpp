#include "GameController.hpp"

#include "GameNotation.hpp"

#include "reversi/eval.hpp"
#include "reversi/moves.hpp"
#include "reversi/mpc.hpp"
#include "reversi/opening_book.hpp"

#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMetaObject>

#include <filesystem>
#include <stdexcept>
#include <utility>

namespace {
// 2^20 entries (~24 MB). Reused across the AI's moves within one game (cleared on newGame):
// entries from earlier searches stay valid - keys are full positions, stored depths are
// remaining-depth - and pre-warm both cutoffs and move ordering for later searches.
constexpr std::size_t kTranspositionTableEntries = std::size_t{1} << 20;
// M9 phase 3: PV length cap for the analysis panel's top line - long enough to show a real
// continuation, short enough that a shallow analysis depth (aiMaxSearchDepth_ aside, a per-line
// budget can still land at a modest completed depth) doesn't display a misleadingly long line
// propped up by TT entries left over from unrelated exploration.
constexpr int kMaxPvLength = 10;
} // namespace

GameController::GameController(QObject* parent)
    : QObject(parent),
      tt_(std::make_unique<reversi::TranspositionTable>(kTranspositionTableEntries)),
      solverTt_(std::make_unique<reversi::TranspositionTable>(kTranspositionTableEntries)),
      analysisTt_(std::make_unique<reversi::TranspositionTable>(kTranspositionTableEntries)) {}

GameController::~GameController() {
    cancelAiSearch();
    cancelAnalysis();
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
    case GameMode::AiVsAi:
        return false;
    }
    return true; // unreachable
}

void GameController::newGame(GameMode mode) {
    cancelAiSearch(); // joins the worker first, so clearing the tables below is race-free
    cancelAnalysis(); // a new game invalidates any in-flight analysis of the old position too
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
    aiSearchInFlight_ = false;
}

void GameController::cancelAnalysis() {
    if (analysisCancellation_) {
        analysisCancellation_->requestStop();
    }
    ++analysisGeneration_; // even a result that completes anyway is now stale
    if (analysisThread_.joinable()) {
        analysisThread_.join();
    }
    analysisInFlight_ = false;
}

void GameController::setAiMaxSearchDepth(int depth) {
    cancelAiSearch();
    cancelAnalysis();
    aiMaxSearchDepth_ = depth;
}

void GameController::setAiTimeBudget(int softMs, int hardMs) {
    cancelAiSearch();
    cancelAnalysis();
    aiTimeBudget_ =
        reversi::TimeBudget{std::chrono::milliseconds{softMs}, std::chrono::milliseconds{hardMs}};
}

void GameController::setExactSolverEmptyThreshold(int threshold) {
    cancelAiSearch();
    cancelAnalysis();
    exactSolverEmptyThreshold_ = threshold;
}

void GameController::setOpeningBookEnabled(bool enabled) {
    if (!hasOpeningBook()) {
        return; // nothing loaded to enable
    }
    cancelAiSearch();
    cancelAnalysis();
    book_ = enabled ? ownedBook_.get() : nullptr;
}

bool GameController::loadOpeningBook(const QString& filePath) {
    cancelAiSearch(); // join-before-mutate: ownedBook_ is about to be destroyed and replaced,
    cancelAnalysis(); // and a worker thread could still be reading through book_'s old value
    try {
        auto book =
            std::make_unique<reversi::OpeningBook>(std::filesystem::path(filePath.toStdString()));
        ownedBook_ = std::move(book);
        book_ = ownedBook_.get(); // loading it is why you loaded it: enable immediately
        return true;
    } catch (const std::runtime_error& e) {
        // OpeningBook's constructor throws rather than returning bool/optional (opening_book.hpp)
        // - this is the one place that convention needs bridging into this class's usual
        // bool+lastErrorMessage() file-operation contract.
        lastErrorMessage_ = QStringLiteral("Could not load opening book: %1").arg(e.what());
        return false;
    }
}

void GameController::setMpcEnabled(bool enabled) {
    if (!hasMpcModel()) {
        return; // nothing loaded to enable
    }
    cancelAiSearch();
    cancelAnalysis();
    mpcModel_ = enabled ? ownedMpcModel_.get() : nullptr;
}

bool GameController::loadMpcModel(const QString& filePath) {
    cancelAiSearch();
    cancelAnalysis();
    try {
        auto model =
            std::make_unique<reversi::MpcModel>(std::filesystem::path(filePath.toStdString()));
        ownedMpcModel_ = std::move(model);
        mpcModel_ = ownedMpcModel_.get();
        return true;
    } catch (const std::runtime_error& e) {
        lastErrorMessage_ = QStringLiteral("Could not load MPC model: %1").arg(e.what());
        return false;
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
    cancelAiSearch(); // defensive: keeps "at most one aiThread_ alive" airtight even if a
                      // future mode ever dispatches AI twice in a row
    cancelAnalysis(); // analysis of the current position is redundant once the AI is about to
                      // move there anyway, and canAnalyze() disables the panel button meanwhile
    ++searchGeneration_;
    const int myGeneration = searchGeneration_;
    // Set BEFORE emitting statusChanged below: a direct (same-thread) Qt connection runs its
    // slot synchronously inside emit, so canAnalyze() must already reflect "AI thinking" by the
    // time any slot observes this signal - MainWindow's panel re-checks canAnalyze() exactly
    // here (see MainWindow.cpp's statusChanged connection), not just on boardChanged.
    aiSearchInFlight_ = true;
    const QString mover = blackToMove_ ? QStringLiteral("Black") : QStringLiteral("White");
    emit statusChanged(mover + QStringLiteral(" to move. (AI thinking...)"));

    cancellation_ = std::make_shared<reversi::CancellationToken>();
    const std::shared_ptr<reversi::CancellationToken> cancellationCopy = cancellation_;
    const reversi::Position posCopy = pos_;

    // tt_/solverTt_ are captured raw: the worker only touches them while running, and every
    // path that mutates or destroys them (newGame, destructor) joins the worker via
    // cancelAiSearch first. config.book/config.mpcModel below are snapshotted BY VALUE into
    // `config` (which the lambda then captures by value too) - a subsequent
    // setOpeningBookEnabled()/setMpcEnabled() toggle can't race with this, but
    // loadOpeningBook()/loadMpcModel() replacing (destroying) the owned object book_/mpcModel_
    // point at absolutely could, if they didn't also join first - see those functions' own
    // "join-before-mutate" comments.
    reversi::TranspositionTable* const tt = tt_.get();
    reversi::TranspositionTable* const solverTt = solverTt_.get();
    reversi::MoveSelectorConfig config;
    config.book = book_;
    config.maxDepth = aiMaxSearchDepth_;
    config.budget = aiTimeBudget_;
    config.mpcModel = mpcModel_;
    config.exactSolverEmptyThreshold = exactSolverEmptyThreshold_;
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
    aiSearchInFlight_ = false;
    commitMove(result.bestMove);
    advanceTurn();
}

void GameController::analyzePosition(int maxLines) {
    if (!canAnalyze()) {
        return; // an AI move-search or another analysis is already in flight
    }
    cancelAnalysis(); // defensive: keeps "at most one analysisThread_ alive" airtight
    ++analysisGeneration_;
    const int myGeneration = analysisGeneration_;
    analysisInFlight_ = true;
    analysisCancellation_ = std::make_shared<reversi::CancellationToken>();
    const std::shared_ptr<reversi::CancellationToken> cancellationCopy = analysisCancellation_;
    analysisTt_->clear(); // a fresh analysis of a (possibly different) position each time -
                          // entries from the previous call's position tree don't carry over
    const reversi::Position posCopy = pos_;
    const bool blackToMoveCopy = blackToMove_; // so onAnalysisFinished can convert the (mover-
                                               // relative) score to the panel's fixed White/Black
                                               // display convention - see the header's doc comment
    // Snapshotted here (not read directly from `this` inside the thread lambda below) for the
    // same reason startAiSearch()'s `config` is captured by value: the Settings dialog can change
    // these live on the GUI thread while this worker is running, and reading `this->...` directly
    // from another thread mid-search would be a real data race, not just a "which value did it
    // use" ambiguity.
    const int maxDepthCopy = aiMaxSearchDepth_;
    const reversi::TimeBudget budgetCopy = aiTimeBudget_;

    // analysisTt_ is captured raw: the worker only touches it while running, and every path that
    // mutates or destroys it (cancelAnalysis, the destructor) joins the worker first - the exact
    // same "join-before-mutate" discipline startAiSearch()'s own comment describes for tt_/
    // solverTt_.
    reversi::TranspositionTable* const tt = analysisTt_.get();
    analysisThread_ = std::thread([this, posCopy, myGeneration, cancellationCopy, maxLines, tt,
                                   blackToMoveCopy, maxDepthCopy, budgetCopy] {
        const std::vector<reversi::RankedMove> lines =
            reversi::analyzeTopMoves(posCopy, maxLines, maxDepthCopy, budgetCopy,
                                     reversi::evaluateDiscDifferential, cancellationCopy.get(), tt);
        // The PV is only ever extracted for the top-ranked line - a chess-analysis-UI
        // convention, and each additional line's PV would need its own walk from a different
        // child position (see analysis.hpp's own doc comment on extractPrincipalVariation).
        std::vector<int> pv;
        if (!lines.empty()) {
            pv = reversi::extractPrincipalVariation(posCopy, lines[0].move, *tt, kMaxPvLength);
        }
        QMetaObject::invokeMethod(
            this,
            [this, lines, pv, blackToMoveCopy, myGeneration] {
                onAnalysisFinished(lines, pv, blackToMoveCopy, myGeneration);
            },
            Qt::QueuedConnection);
    });
}

void GameController::onAnalysisFinished(std::vector<reversi::RankedMove> lines, std::vector<int> pv,
                                        bool blackToMove, int generation) {
    if (generation != analysisGeneration_) {
        // Superseded by a newer analysis or a position change: discard without touching
        // analysisInFlight_, which may already correctly reflect a newer, still-running pass.
        return;
    }
    analysisInFlight_ = false;
    emit analysisFinished(lines, pv, blackToMove);
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
    cancelAnalysis();
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
    cancelAnalysis();
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
    cancelAnalysis();
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
