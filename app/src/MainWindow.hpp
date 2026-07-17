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
class QLabel;
class QListWidget;
class QMenuBar;
class QPushButton;
class QStatusBar;
class QVBoxLayout;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    TitleBarWidget* titleBar_;
    QMenuBar* menuBar_;
    BoardWidget* board_;
    QWidget* panel_; // M9 placeholder side panel - phase 3 gives it its first real content
    QStatusBar* statusBar_;
    GameController* controller_;

    // M9 phase 3: analysis panel, the first real content inside panel_.
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
    // Builds panel_'s two sections (move history, analysis) into a vertical QSplitter - the
    // single entry point called from the constructor; setupMoveHistoryPanel()/
    // setupAnalysisPanel() each build and return their own pane widget for the splitter.
    void setupPanelContent();
    QWidget* setupMoveHistoryPanel();
    QWidget* setupAnalysisPanel();
    void updateMoveHistoryList();
    void updateAnalyzeButtonEnabled();
    void renderAnalysisResults(const std::vector<reversi::RankedMove>& lines,
                               const std::vector<int>& pv, bool blackToMove);
    // One "card" per ranked move: rank badge, move, score on the top line; a dimmer depth/nodes
    // line; the PV (isTopLine only) on its own dimmer line.
    QWidget* buildAnalysisLineRow(int rank, const reversi::RankedMove& line, bool isTopLine,
                                  const std::vector<int>& pv, bool blackToMove);
};
