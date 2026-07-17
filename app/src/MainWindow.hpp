#pragma once

#include <QMainWindow>

#include <vector>

namespace reversi {
struct RankedMove;
} // namespace reversi

class BoardWidget;
class GameController;
class TitleBarWidget;
class QCloseEvent;
class QFrame;
class QLabel;
class QListWidget;
class QMenuBar;
class QPushButton;
class QVBoxLayout;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    // M10: frameless windows (Qt::FramelessWindowHint) lose the OS's own resize-border
    // hit-testing along with the rest of the native frame - this restores it on Windows by
    // answering WM_NCHITTEST directly (see MainWindow.cpp's own doc comment on the
    // implementation). A no-op passthrough to the base class on any other platform.
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    TitleBarWidget* titleBar_;
    QMenuBar* menuBar_;
    BoardWidget* board_;
    // M9 phase 5: QFrame, not QWidget - a plain QWidget doesn't paint its own QSS background by
    // default (Qt::WA_StyledBackground would be the other way to opt in; QFrame does it
    // automatically, the same reason the MultiPV row cards and the move-history/analysis panes
    // are QFrames too), needed now that panel_ has a real background+radius via QFrame#sidePanel
    // in chrome::panelControlsStyleSheet() (M10: background-color + radius only, no border).
    QFrame* panel_;
    // M10: replaces the old QStatusBar, which spanned the whole window (board + panel) with
    // generic OS status-bar chrome. This sits only under board_ (its own column in boardRow), a
    // styled, borderless pill matching the board's own width - see MainWindow.cpp's boardColumn
    // comment.
    QLabel* statusLabel_;
    GameController* controller_;

    // M9 phase 3: analysis panel, the first real content inside panel_. M10: starts hidden -
    // toggled by the "Analysis" pill button in the panel's bottom toolbar (setupPanelToolbar()) -
    // so analysisPane_ is now a stored member (previously setupAnalysisPanel()'s return value was
    // only ever handed straight to the QSplitter and never needed again).
    QWidget* analysisPane_;
    // M10 phase 3: the move-history pane needs to be reachable outside setupMoveHistoryPanel() too
    // - refreshTheme() re-applies its stylesheet on a theme switch, the same reason analysisPane_
    // is already a member rather than a local.
    QFrame* moveHistoryPane_;
    // M10 phase 3: same reasoning - the toolbar's "Analysis" toggle bakes chrome::palette() colors
    // into its own stylesheet directly (not just inherited from panel_'s cascade), so it needs its
    // own re-application on theme change too.
    QPushButton* analysisToggleButton_;
    QPushButton* analyzeButton_;
    QLabel* analysisStatusLabel_;
    // M9 phase 5: MultiPV results rebuilt from a single QPlainTextEdit text block into real
    // per-line row widgets - a structural change, not a restyle (see MainWindow.cpp's own doc
    // comment on renderAnalysisResults()). Holds zero or more row widgets plus a placeholder
    // label when there are no results; renderAnalysisResults() clears and rebuilds its children
    // (including the placeholder, recreated fresh each time - not a persistent member, since
    // nothing outside renderAnalysisResults() ever needs to reach it again) on every call.
    QVBoxLayout* analysisResultsLayout_;
    // M9 phase 5: move-history list - a missed item from the original spec (alongside undo/redo
    // and save/load), not new scope; built alongside the rest of this phase's visual pass but
    // its own reviewed step, same as MultiPV got its own section in phase 3.
    QListWidget* moveHistoryList_;

    void createMenus();
    // Builds panel_'s two sections (move history, analysis) into a vertical QSplitter plus a
    // fixed bottom toolbar row (setupPanelToolbar()) - the single entry point called from the
    // constructor; setupMoveHistoryPanel()/setupAnalysisPanel() each build and return their own
    // pane widget for the splitter.
    void setupPanelContent();
    QWidget* setupMoveHistoryPanel();
    QWidget* setupAnalysisPanel();
    // M10: the "Analysis" pill toggle (chess.com's own flag/undo/lightbulb bottom-row convention)
    // - room for more toolbar buttons later, though only Analysis exists today.
    QWidget* setupPanelToolbar();
    void updateMoveHistoryList();
    void updateAnalyzeButtonEnabled();
    // M10 phase 3: re-applies every chrome::palette()-derived stylesheet/QPalette this class owns
    // (the window's own chrome QSS, centralWidget()'s QPalette, panel_, the move-history/analysis
    // panes, the toolbar toggle) and refreshes moveHistoryList_'s current-row color - called once
    // from the constructor's chrome::ThemeManager::themeChanged connection, never at construction
    // time itself (each setup function already applies its own correct initial style once).
    // BoardWidget/TitleBarWidget/SettingsDialog each own their own equivalent independently rather
    // than being reached into from here - see their own themeChanged connections.
    void refreshTheme();
    void renderAnalysisResults(const std::vector<reversi::RankedMove>& lines,
                               const std::vector<int>& pv, bool blackToMove);
    // One "card" per ranked move: rank badge, move, score on the top line; a dimmer depth/nodes
    // line; the PV (isTopLine only) on its own dimmer line.
    QWidget* buildAnalysisLineRow(int rank, const reversi::RankedMove& line, bool isTopLine,
                                  const std::vector<int>& pv, bool blackToMove);
};
