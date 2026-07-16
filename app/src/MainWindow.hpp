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
class QMenuBar;
class QPlainTextEdit;
class QPushButton;
class QStatusBar;

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
    QPlainTextEdit* analysisResultsView_;

    void createMenus();
    void setupAnalysisPanel();
    void updateAnalyzeButtonEnabled();
    void renderAnalysisResults(const std::vector<reversi::RankedMove>& lines,
                               const std::vector<int>& pv, bool blackToMove);
};
