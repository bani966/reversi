#pragma once

#include "BoardWidget.hpp"
#include "GameMode.hpp"

#include "reversi/analysis.hpp"
#include "reversi/cancellation.hpp"
#include "reversi/move_selector.hpp"
#include "reversi/position.hpp"
#include "reversi/search.hpp"
#include "reversi/solver.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace reversi {
class OpeningBook;
} // namespace reversi

// The "thin controller/view-model": owns the game state and all turn-taking/rules
// orchestration. BoardWidget only renders and reports clicks; it never touches reversi::
// rules functions itself.
class GameController : public QObject {
    Q_OBJECT

public:
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

    // M9 phase 2: undo/redo navigate history_ without ever calling advanceTurn()/
    // startAiSearch() - this is the user rewinding/replaying, not a request for the AI to move
    // again. In Human-vs-AI modes, a single call skips past the AI's own turn so it always lands
    // back on the human's turn. tt_/solverTt_ are deliberately NOT cleared (unlike newGame()) -
    // undo/redo stay within the same game's position tree, so cached entries are still valid;
    // clearing them would be a pure, avoidable performance regression, not a correctness fix.
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();

    // Save/load: this app's own JSON format (GameNotation::toSaveJson/fromSaveJson). Import/
    // export: a plain-text Othello transcript, and a 65-char board-position snapshot
    // (GameNotation::toTranscript/fromTranscript, toBoardString/fromBoardString). All six
    // return false and leave the current game completely untouched on any failure (malformed
    // file, illegal move sequence) - lastErrorMessage() then holds a human-readable reason for
    // the caller's error dialog. Unlike undo/redo, a successful load/import DOES call
    // advanceTurn() - resuming a loaded game should behave exactly like having played up to that
    // point normally, including auto-starting an AI search if it's the AI's turn.
    bool saveGame(const QString& filePath) const;
    bool loadGame(const QString& filePath);
    bool exportTranscript(const QString& filePath) const;
    bool importTranscript(const QString& filePath);
    bool exportPosition(const QString& filePath) const;
    bool importPosition(const QString& filePath);
    QString lastErrorMessage() const { return lastErrorMessage_; }

    // M9 phase 3: on-demand MultiPV analysis of the CURRENT position - works in any GameMode,
    // regardless of whose turn it is (a Human-vs-Human player must be able to inspect a position
    // just as freely as a Human-vs-AI one). `false` while an AI move-search or an already-running
    // analysis is in flight - the panel's "Analyze Position" button is disabled in that state, but
    // analyzePosition() itself also no-ops defensively rather than trusting the caller.
    bool canAnalyze() const { return !aiSearchInFlight_ && !analysisInFlight_; }
    // 3 ranked lines by default - enough to show real alternatives without the on-demand wait
    // growing past a few seconds at kAiTimeBudget's per-line cost (see GameController.cpp).
    void analyzePosition(int maxLines = 3);
    // Requests the in-flight analysis (if any) to stop and blocks until its worker thread has
    // finished. Safe to call when no analysis is running. Called from every place cancelAiSearch()
    // already is (newGame, the destructor, undo, redo, applyLoadedHistory) - any of those change
    // pos_ out from under an in-flight analysis of the old position, the same invalidation
    // reasoning cancelAiSearch() already applies to the AI's own search.
    void cancelAnalysis();

    // M9 phase 4: real settings surface for control points that previously existed but had no
    // UI (book_/mpcModel_ below, "control point exists, default off"), plus a getter for
    // lastMoveHighlightEnabled_ (so a freshly opened Settings dialog can initialize its checkbox
    // to the current state - a loaded save file may have already turned this on). Every setter/
    // loader here calls cancelAiSearch()/cancelAnalysis() first, mirroring startAiSearch()'s own
    // "cancel defensively" discipline - required for the load functions (replacing the pointed-to
    // object while a worker thread might still be reading through the old pointer would be a real
    // use-after-free), applied uniformly to the plain-value setters too for consistency. None of
    // these are persisted across app restarts or saved into a game's JSON - session-level AI
    // configuration, not per-game state (unlike lastMoveHighlightEnabled_, which already is).
    bool lastMoveHighlightEnabled() const { return lastMoveHighlightEnabled_; }

    int aiMaxSearchDepth() const { return aiMaxSearchDepth_; }
    void setAiMaxSearchDepth(int depth);
    reversi::TimeBudget aiTimeBudget() const { return aiTimeBudget_; }
    void setAiTimeBudget(int softMs, int hardMs);
    int exactSolverEmptyThreshold() const { return exactSolverEmptyThreshold_; }
    void setExactSolverEmptyThreshold(int threshold);

    bool hasOpeningBook() const { return ownedBook_ != nullptr; }
    bool openingBookEnabled() const { return book_ != nullptr; }
    void setOpeningBookEnabled(bool enabled);
    // Loads a real OpeningBook from `filePath`, replacing any previously loaded one and enabling
    // it immediately. Returns false (and sets lastErrorMessage()) if the file can't be parsed -
    // wraps OpeningBook's own throwing constructor (opening_book.hpp) into this class's usual
    // bool+lastErrorMessage() file-operation convention.
    bool loadOpeningBook(const QString& filePath);

    bool hasMpcModel() const { return ownedMpcModel_ != nullptr; }
    bool mpcEnabled() const { return mpcModel_ != nullptr; }
    void setMpcEnabled(bool enabled);
    // Same shape as loadOpeningBook(), for MpcModel (mpc.hpp).
    bool loadMpcModel(const QString& filePath);

public slots:
    void onSquareClicked(int square);

signals:
    void boardChanged(const BoardWidget::DisplayState& state);
    void statusChanged(const QString& text);
    // `lines` is empty if analysis found nothing to report (e.g. cancelled immediately); `pv` is
    // the principal variation for lines[0] only (empty if lines is empty) - see
    // GameController.cpp's analyzePosition() for why only the top line gets a full continuation.
    // `blackToMove` is whose turn it was in the analyzed position - every RankedMove::score is
    // mover-relative (see analysis.hpp), so a fixed White-positive/Black-negative display (the
    // panel's own convention, not the engine's) needs to know which side that was in order to
    // convert.
    void analysisFinished(const std::vector<reversi::RankedMove>& lines, const std::vector<int>& pv,
                          bool blackToMove);

private:
    // One entry per fully pass-resolved resting position - i.e. exactly what advanceTurn()
    // pushes each time it's called, never a transient pre-pass state (forced passes are resolved
    // before a new entry is recorded, both here and in replayMoves() below, so history_[i]
    // always matches pos_/blackToMove_/lastMoveSquare_ exactly when historyIndex_ == i).
    struct HistoryEntry {
        reversi::Position position;
        bool blackToMove;
        int lastMoveSquare; // -1 for the start entry (index 0)
    };
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

    // history_[0] is always the game's start position (Position::start() for a normal new game,
    // or an imported board for a position import); history_[historyIndex_] always exactly
    // matches pos_/blackToMove_/lastMoveSquare_ (see HistoryEntry's doc comment above).
    std::vector<HistoryEntry> history_;
    std::size_t historyIndex_ = 0;
    // Set on any save/load/import/export failure; read by lastErrorMessage(). mutable so the
    // const export methods (saveGame/exportTranscript/exportPosition) can still report why a
    // file write failed without needing to be non-const themselves.
    mutable QString lastErrorMessage_;

    std::thread aiThread_;
    // Owned here, used by the worker thread during a search; see startAiSearch for why the
    // raw-pointer handoff is safe. unique_ptr rather than a value so the header doesn't need
    // the table's size decision (it lives with the other AI constants in the .cpp).
    std::unique_ptr<reversi::TranspositionTable> tt_;
    // A SEPARATE table for selectMove()'s solveExact branch - never the same object as tt_ (see
    // move_selector.hpp / solver.hpp's doc comments on why sharing one table between the solver
    // and heuristic search would silently corrupt the solver's exactness).
    std::unique_ptr<reversi::TranspositionTable> solverTt_;
    // Non-owning: book_/mpcModel_ never construct or destroy anything themselves - they just
    // point at ownedBook_/ownedMpcModel_ (below) once loadOpeningBook()/loadMpcModel() succeeds,
    // or nullptr (the default, and also what setOpeningBookEnabled(false)/setMpcEnabled(false)
    // restore) to disable. Off by default until the M9 phase 4 Settings dialog loads something.
    const reversi::OpeningBook* book_ = nullptr;
    const reversi::MpcModel* mpcModel_ = nullptr;
    // M9 phase 4: actually owns whatever loadOpeningBook()/loadMpcModel() last successfully
    // constructed - book_/mpcModel_ above are never reassigned to point anywhere else. Replaced
    // wholesale by a new load (never mutated in place), always after cancelAiSearch()/
    // cancelAnalysis() so no worker thread can still be reading through the pointer being
    // replaced.
    std::unique_ptr<reversi::OpeningBook> ownedBook_;
    std::unique_ptr<reversi::MpcModel> ownedMpcModel_;
    // M9 phase 4: previously compile-time constants (kAiMaxSearchDepth/kAiTimeBudget) in
    // GameController.cpp - now user-configurable via the Settings dialog, defaulted to the exact
    // same values. exactSolverEmptyThreshold_ populates MoveSelectorConfig's field of the same
    // name (move_selector.hpp), which startAiSearch() never set before this phase (silently
    // falling back to solver.hpp's own compile-time default every time).
    int aiMaxSearchDepth_ = 24;
    reversi::TimeBudget aiTimeBudget_{std::chrono::milliseconds{800},
                                      std::chrono::milliseconds{2500}};
    int exactSolverEmptyThreshold_ = reversi::kExactSolverEmptyThreshold;
    std::shared_ptr<reversi::CancellationToken> cancellation_;
    // Bumped on every new search and every cancellation, so a result that arrives after being
    // superseded (by a new search or a new game) can be recognized as stale and discarded even
    // if it otherwise looks like a normal completed result.
    int searchGeneration_ = 0;
    // True from startAiSearch() until onAiSearchFinished() runs (or cancelAiSearch() is called) -
    // NOT the same thing as aiThread_.joinable(), which stays true from spawn until join() is
    // actually called, including the window after the worker function has already returned but
    // before its queued slot has run on the GUI thread. canAnalyze() needs the precise "is a
    // result still pending" answer, not "has join() been called yet".
    bool aiSearchInFlight_ = false;

    // M9 phase 3: MultiPV analysis worker, mirroring aiThread_/cancellation_/searchGeneration_'s
    // shape exactly, but kept fully separate - analysisTt_ is dedicated and NEVER shared with
    // tt_/solverTt_ (a plain TranspositionTable, unlike M8's SharedTranspositionTable, is not
    // safe for concurrent access from two threads at once; see analysis.hpp's own doc comment on
    // why analyzeTopMoves() still safely reuses one table across its own N sequential passes).
    std::thread analysisThread_;
    std::unique_ptr<reversi::TranspositionTable> analysisTt_;
    std::shared_ptr<reversi::CancellationToken> analysisCancellation_;
    int analysisGeneration_ = 0;
    bool analysisInFlight_ = false;

    bool isHumanTurn() const;
    bool isHumanTurnFor(bool blackToMove) const;
    void advanceTurn();
    void emitBoardState();
    void emitStatus(const QStringList& passMessages);

    void startAiSearch();
    void onAiSearchFinished(const reversi::SearchResult& result, int generation);
    void onAnalysisFinished(std::vector<reversi::RankedMove> lines, std::vector<int> pv,
                            bool blackToMove, int generation);

    // Shared by onSquareClicked/onAiSearchFinished: applies a real move to pos_/blackToMove_/
    // lastMoveSquare_ and discards any redo tail. Does NOT touch history_ itself - advanceTurn()
    // (called right after, by both callers) is the single place that resolves forced passes and
    // records the resulting resting position, so it's the only thing that ever pushes to
    // history_ during live play.
    void commitMove(int square);
    // Shared by undo/redo: restores pos_/blackToMove_/lastMoveSquare_ from history_[index] and
    // emits the resulting state, without calling advanceTurn()/startAiSearch() (see the public
    // undo()/redo() doc comment above for why).
    void restoreFromHistory(std::size_t index);
    // Replays `moves` from (start, startBlackToMove), resolving forced passes exactly like
    // advanceTurn() does, into a full HistoryEntry sequence. Returns nullopt (without partially
    // building anything usable) the first time a move isn't legal for the position it would be
    // played from - the one shared validation path for loadGame/importTranscript, so a
    // corrupted/hand-edited file fails cleanly rather than desyncing state.
    std::optional<std::vector<HistoryEntry>> replayMoves(const reversi::Position& start,
                                                         bool startBlackToMove,
                                                         const std::vector<int>& moves) const;
    // Shared commit path for loadGame/importTranscript/importPosition: resets tt_/solverTt_
    // (this genuinely is a different position tree, unlike undo/redo - see the public API doc
    // comment), installs the given history wholesale, and calls advanceTurn()'s non-history-
    // pushing counterpart to emit state and possibly start an AI search.
    void applyLoadedHistory(GameMode mode, std::vector<HistoryEntry> history,
                            bool lastMoveHighlightEnabled);
    // The move list for history_[1..historyIndex_] - shared by saveGame/exportTranscript, which
    // both export "what's currently on screen", not any undone redo tail.
    std::vector<int> currentMoveList() const;
    // The non-history-pushing half of advanceTurn(): emits boardChanged/statusChanged and starts
    // an AI search if appropriate. Factored out so applyLoadedHistory() can reuse it without
    // advanceTurn() pushing a second, duplicate history_ entry on top of the one replayMoves()
    // already built.
    void finalizeTurn(const QStringList& passMessages);
};
