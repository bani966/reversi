#include "MainWindow.hpp"

#include "BoardWidget.hpp"
#include "GameController.hpp"
#include "Palette.hpp"
#include "SettingsDialog.hpp"
#include "TitleBarWidget.hpp"

#include "reversi/analysis.hpp"
#include "reversi/position.hpp"

#include <QAction>
#include <QBrush>
#include <QCloseEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QVBoxLayout>

#include <cstdint>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <dwmapi.h>
#include <windows.h>
#endif

namespace {

#ifdef Q_OS_WIN
// Spike (M3 round 2, stage 2): ask DWM to round this frameless window's corners at the OS
// compositor level. If this actually works on a frameless window, it avoids
// Qt::WA_TranslucentBackground entirely - which has documented rendering bugs at fractional
// DPI scale factors (125%/150%/175%, common on Windows laptops) and renders sliced when
// dragged between differently-scaled monitors. Windows 11 only; DwmSetWindowAttribute with
// this attribute is simply unavailable/a no-op on Windows 10, which is an acceptable
// degradation (square corners there, not a crash or a broken window).
void applyWindowsCornerRounding(QWidget* window) {
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    const DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
}
#endif

// Stylesheet pass only - QMenuBar/QMenu/QStatusBar stay real native widgets, just re-skinned
// to match the board's dark, flat aesthetic instead of default Windows menu chrome. Built from
// chrome::palette() (shared with BoardWidget's coordinate labels and TitleBarWidget) rather
// than literal hex values, so all three can't drift out of sync with each other again.
QString buildChromeStyleSheet() {
    const chrome::Palette& theme = chrome::palette();
    return QStringLiteral(R"(
        QMenuBar {
            background-color: %1;
            color: %2;
            font-family: "Segoe UI";
            font-weight: 500;
            padding: 2px 4px;
            border: none;
        }
        QMenuBar::item {
            background: transparent;
            padding: 4px 12px;
            border-radius: 4px;
        }
        QMenuBar::item:selected, QMenuBar::item:pressed {
            background-color: %3;
        }
        QMenu {
            background-color: %4;
            color: %2;
            font-family: "Segoe UI";
            border: 1px solid %5;
            padding: 4px;
        }
        QMenu::item {
            padding: 6px 28px 6px 16px;
            border-radius: 4px;
        }
        QMenu::item:selected {
            background-color: %3;
        }
        QStatusBar {
            background-color: %1;
            color: %2;
            font-family: "Segoe UI";
            font-weight: 500;
            border: none;
        }
    )")
        .arg(theme.windowBackground.name()) // %1
        .arg(theme.textColor.name())        // %2
        .arg(theme.panelHover.name())       // %3
        .arg(theme.popupBackground.name())  // %4
        .arg(theme.panelBorder.name());     // %5
}

// M9 phase 3: a light styling pass on the analysis panel's own widgets (QPushButton/QLabel/
// QPlainTextEdit aren't covered by buildChromeStyleSheet() above, which only targets
// QMenuBar/QMenu/QStatusBar) - reuses the same chrome::palette() roles so the panel reads as
// part of the app's existing dark chrome rather than default Qt widget styling. A full
// chess.com-style visual match (rounded cards, the exact reference layout) is phase 5's
// dedicated visual-parity pass, not attempted here.
QString buildAnalysisPanelStyleSheet() {
    const chrome::Palette& theme = chrome::palette();
    return QStringLiteral(R"(
        QPushButton {
            background-color: %1;
            color: %2;
            border: 1px solid %3;
            border-radius: 4px;
            padding: 8px;
            font-family: "Segoe UI";
            font-weight: 600;
        }
        QPushButton:hover:enabled {
            background-color: %4;
        }
        QPushButton:disabled {
            color: %3;
        }
        QLabel {
            color: %2;
            font-family: "Segoe UI";
        }
        QPlainTextEdit {
            background-color: %1;
            color: %2;
            border: 1px solid %3;
            border-radius: 4px;
            padding: 6px;
        }
    )")
        .arg(theme.popupBackground.name()) // %1
        .arg(theme.textColor.name())       // %2
        .arg(theme.panelBorder.name())     // %3
        .arg(theme.panelHover.name());     // %4
}

// "10094917" reads as clutter and doesn't fit the analysis panel's fixed ~300px width on one
// line at a readable font size; "10.1M" is both more compact and the conventional way engine
// node counts get displayed.
QString formatNodeCount(std::uint64_t nodes) {
    if (nodes >= 1'000'000) {
        return QStringLiteral("%1M").arg(static_cast<double>(nodes) / 1'000'000.0, 0, 'f', 1);
    }
    if (nodes >= 1'000) {
        return QStringLiteral("%1K").arg(static_cast<double>(nodes) / 1'000.0, 0, 'f', 1);
    }
    return QString::number(nodes);
}
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    setWindowTitle(QStringLiteral("Reversi"));
    resize(1020,
           796); // was 720x796; +300px (M9 phase 1) to make room for the new side panel column
    setStyleSheet(buildChromeStyleSheet());

#ifdef Q_OS_WIN
    applyWindowsCornerRounding(this);
#endif

    // QMainWindow's own menuBar()/statusBar() dock automatically above/below whatever is set
    // as the central widget - that would put them below our custom title bar, not above it. So
    // the whole stack (title bar, menu, board, status bar) is built as ordinary child widgets
    // in one layout and set as the central widget instead; menuBar_/statusBar_ stay real
    // QMenuBar/QStatusBar (round 1's QSS still applies to them identically either way), just
    // manually placed rather than using QMainWindow's docking convenience methods.
    titleBar_ = new TitleBarWidget(this);
    titleBar_->setTitle(windowTitle());

    menuBar_ = new QMenuBar(this);
    board_ = new BoardWidget(this);
    statusBar_ = new QStatusBar(this);

    // M9 phase 1: empty placeholder for the side panel phases 3-4 populate (analysis panel, move
    // history, settings). An explicit background is a correctness fix, not decoration - every
    // pixel of the window was previously covered by titleBar_/menuBar_/board_/statusBar_, so a
    // bare QWidget's default (unpainted) background has never been exercised before; without
    // this it would show through to Qt's default system palette, reading as a rendering bug next
    // to the app's otherwise uniformly dark chrome. Reuses windowBackground - the same role
    // titleBar_/menuBar_/statusBar_ already share - rather than introducing a new color.
    panel_ = new QWidget(this);
    panel_->setAutoFillBackground(true);
    QPalette panelPalette = panel_->palette();
    panelPalette.setColor(QPalette::Window, chrome::palette().windowBackground);
    panel_->setPalette(panelPalette);
    // A bare QWidget has no usable sizeHint() for layout purposes; without an explicit minimum
    // the QHBoxLayout below would collapse it to zero width. 300px is a first-pass placeholder
    // loosely sized on chess.com's own side-panel width - not load-bearing, revisited in phase 5.
    panel_->setMinimumWidth(300);

    // board_ keeps the stretch factor so it - not panel_ - absorbs extra space on resize,
    // mirroring how board_ already gets stretch 1 vertically in the outer layout below. Left
    // margin matches the 12px margin each of panel_'s own sub-panes uses internally
    // (setupMoveHistoryPanel()/setupAnalysisPanel()) so the board's left edge sits the same
    // distance from the window border as the panel's content does from the window's right
    // border - without this, the board sat flush against the left edge while the panel's content
    // had a visible 12px inset on the right, reading as unbalanced.
    auto* boardRow = new QHBoxLayout();
    boardRow->setContentsMargins(12, 0, 0, 0);
    boardRow->setSpacing(0);
    boardRow->addWidget(board_, 1);
    boardRow->addWidget(panel_);

    auto* container = new QWidget(this);
    // Same correctness fix as panel_'s own background above, for the same reason: boardRow's new
    // left margin exposes a strip of container's raw background for the first time (every pixel
    // was previously covered by a child widget) - without this it would show Qt's default system
    // palette instead of the app's chrome, and would silently drift out of sync with the rest of
    // the chrome under a future theme change, unlike a color read from chrome::palette().
    container->setAutoFillBackground(true);
    QPalette containerPalette = container->palette();
    containerPalette.setColor(QPalette::Window, chrome::palette().windowBackground);
    container->setPalette(containerPalette);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(titleBar_);
    layout->addWidget(menuBar_);
    layout->addLayout(boardRow, 1);
    layout->addWidget(statusBar_);
    setCentralWidget(container);

    connect(titleBar_, &TitleBarWidget::minimizeRequested, this, &QWidget::showMinimized);
    connect(titleBar_, &TitleBarWidget::maximizeRestoreRequested, this, [this] {
        if (isMaximized()) {
            showNormal();
        } else {
            showMaximized();
        }
    });
    connect(titleBar_, &TitleBarWidget::closeRequested, this, &QWidget::close);

    controller_ = new GameController(this);
    connect(controller_, &GameController::boardChanged, board_, &BoardWidget::setDisplayState);
    connect(board_, &BoardWidget::squareClicked, controller_, &GameController::onSquareClicked);
    connect(controller_, &GameController::statusChanged, this, [this](const QString& text) {
        statusBar_->showMessage(text);
        // canAnalyze() flips to false the instant the AI starts thinking, which happens inside
        // startAiSearch() before its own statusChanged emit (see that function's own comment) -
        // catching it here, not just on boardChanged, is what actually disables the button in
        // time rather than only after the AI's move has already completed.
        updateAnalyzeButtonEnabled();
    });

    setupPanelContent();
    createMenus();

    controller_->newGame(GameMode::HumanVsHuman);
}

void MainWindow::createMenus() {
    // File: save/load (this app's own JSON format) and import/export (plain-text transcript and
    // 65-char position formats) - all six are file-dialog-based, one consistent interaction
    // pattern rather than mixing in a separate paste-text-dialog pattern (M9 phase 2).
    QMenu* fileMenu = menuBar_->addMenu(QStringLiteral("&File"));

    QAction* saveGame = fileMenu->addAction(QStringLiteral("Save Game..."));
    saveGame->setShortcut(QKeySequence::Save);
    connect(saveGame, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save Game"), QString(), QStringLiteral("Reversi Save (*.json)"));
        if (path.isEmpty()) {
            return;
        }
        if (!controller_->saveGame(path)) {
            QMessageBox::warning(this, QStringLiteral("Save Game"),
                                 controller_->lastErrorMessage());
        }
    });

    QAction* loadGame = fileMenu->addAction(QStringLiteral("Load Game..."));
    loadGame->setShortcut(QKeySequence::Open);
    connect(loadGame, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Load Game"), QString(), QStringLiteral("Reversi Save (*.json)"));
        if (path.isEmpty()) {
            return;
        }
        if (!controller_->loadGame(path)) {
            QMessageBox::warning(this, QStringLiteral("Load Game"),
                                 controller_->lastErrorMessage());
        }
    });

    fileMenu->addSeparator();

    QAction* exportTranscript = fileMenu->addAction(QStringLiteral("Export Transcript..."));
    connect(exportTranscript, &QAction::triggered, this, [this] {
        const QString path =
            QFileDialog::getSaveFileName(this, QStringLiteral("Export Transcript"), QString(),
                                         QStringLiteral("Text Transcript (*.txt)"));
        if (path.isEmpty()) {
            return;
        }
        if (!controller_->exportTranscript(path)) {
            QMessageBox::warning(this, QStringLiteral("Export Transcript"),
                                 controller_->lastErrorMessage());
        }
    });

    QAction* importTranscript = fileMenu->addAction(QStringLiteral("Import Transcript..."));
    connect(importTranscript, &QAction::triggered, this, [this] {
        const QString path =
            QFileDialog::getOpenFileName(this, QStringLiteral("Import Transcript"), QString(),
                                         QStringLiteral("Text Transcript (*.txt)"));
        if (path.isEmpty()) {
            return;
        }
        if (!controller_->importTranscript(path)) {
            QMessageBox::warning(this, QStringLiteral("Import Transcript"),
                                 controller_->lastErrorMessage());
        }
    });

    QAction* exportPosition = fileMenu->addAction(QStringLiteral("Export Position..."));
    connect(exportPosition, &QAction::triggered, this, [this] {
        const QString path =
            QFileDialog::getSaveFileName(this, QStringLiteral("Export Position"), QString(),
                                         QStringLiteral("Text Position (*.txt)"));
        if (path.isEmpty()) {
            return;
        }
        if (!controller_->exportPosition(path)) {
            QMessageBox::warning(this, QStringLiteral("Export Position"),
                                 controller_->lastErrorMessage());
        }
    });

    QAction* importPosition = fileMenu->addAction(QStringLiteral("Import Position..."));
    connect(importPosition, &QAction::triggered, this, [this] {
        const QString path =
            QFileDialog::getOpenFileName(this, QStringLiteral("Import Position"), QString(),
                                         QStringLiteral("Text Position (*.txt)"));
        if (path.isEmpty()) {
            return;
        }
        if (!controller_->importPosition(path)) {
            QMessageBox::warning(this, QStringLiteral("Import Position"),
                                 controller_->lastErrorMessage());
        }
    });

    fileMenu->addSeparator();

    // Settings (M9 phase 4): a fresh, non-modal SettingsDialog each time - simplest lifecycle,
    // no need to keep an instance alive/hidden between openings. Non-modal so the user can keep
    // playing/watching an AI-vs-AI game while adjusting settings (see SettingsDialog's own doc
    // comment). Placed in &File rather than its own top-level menu - one less menu-bar entry,
    // and clearer than a "Settings > Settings..." action stuttering its own menu's name.
    QAction* openSettings = fileMenu->addAction(QStringLiteral("Settings..."));
    connect(openSettings, &QAction::triggered, this, [this] {
        auto* dialog = new SettingsDialog(controller_, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });

    // Edit: undo/redo (M9 phase 2) - standard desktop placement, standard-key shortcuts so the
    // platform-correct binding (Ctrl+Y on Windows) is used automatically rather than hardcoded.
    QMenu* editMenu = menuBar_->addMenu(QStringLiteral("&Edit"));

    QAction* undo = editMenu->addAction(QStringLiteral("Undo"));
    undo->setShortcut(QKeySequence::Undo);
    connect(undo, &QAction::triggered, this, [this] { controller_->undo(); });

    QAction* redo = editMenu->addAction(QStringLiteral("Redo"));
    redo->setShortcut(QKeySequence::Redo);
    connect(redo, &QAction::triggered, this, [this] { controller_->redo(); });

    // Game: unchanged from before M9 phase 2, plus AI vs AI (M9 phase 4).
    QMenu* gameMenu = menuBar_->addMenu(QStringLiteral("&Game"));

    QAction* humanVsHuman = gameMenu->addAction(QStringLiteral("New Game: Human vs Human"));
    connect(humanVsHuman, &QAction::triggered, this,
            [this] { controller_->newGame(GameMode::HumanVsHuman); });

    QAction* humanIsBlack =
        gameMenu->addAction(QStringLiteral("New Game: Human vs AI (You play Black)"));
    connect(humanIsBlack, &QAction::triggered, this,
            [this] { controller_->newGame(GameMode::HumanIsBlack); });

    QAction* humanIsWhite =
        gameMenu->addAction(QStringLiteral("New Game: Human vs AI (You play White)"));
    connect(humanIsWhite, &QAction::triggered, this,
            [this] { controller_->newGame(GameMode::HumanIsWhite); });

    QAction* aiVsAi = gameMenu->addAction(QStringLiteral("New Game: AI vs AI"));
    connect(aiVsAi, &QAction::triggered, this, [this] { controller_->newGame(GameMode::AiVsAi); });
}

// M9 phase 5: panel_'s two sections (move history, analysis) live in a vertical QSplitter so
// each list-like section (a growing move list; a growing MultiPV results list) can be resized by
// the user rather than fighting over a fixed stacked-layout split.
void MainWindow::setupPanelContent() {
    panel_->setStyleSheet(buildAnalysisPanelStyleSheet());

    auto* layout = new QVBoxLayout(panel_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Vertical, panel_);
    splitter->addWidget(setupMoveHistoryPanel());
    splitter->addWidget(setupAnalysisPanel());
    layout->addWidget(splitter);
}

// M9 phase 5: move-history list - a missed item from the original spec (alongside undo/redo and
// save/load), not new scope. Reuses GameController::jumpToHistoryIndex()/fullMoveList()/
// currentHistoryIndex() exactly - no new history tracking here, this widget only ever displays
// what GameController already owns.
QWidget* MainWindow::setupMoveHistoryPanel() {
    auto* pane = new QWidget(panel_);
    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* headerLabel = new QLabel(QStringLiteral("Move History"), pane);
    QFont headerFont = headerLabel->font();
    headerFont.setPointSize(headerFont.pointSize() + 2);
    headerFont.setBold(true);
    headerLabel->setFont(headerFont);
    layout->addWidget(headerLabel);

    moveHistoryList_ = new QListWidget(pane);
    // itemClicked (not currentRowChanged): only fires on a genuine mouse click, unlike
    // currentRowChanged, which would also fire from updateMoveHistoryList()'s own programmatic
    // setCurrentRow()-equivalent state below, creating a needless feedback loop.
    connect(moveHistoryList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const int row = moveHistoryList_->row(item);
        if (row < 0) {
            return;
        }
        // fullMoveList()[row] is history_[row + 1] - see GameController.hpp's own doc comment.
        controller_->jumpToHistoryIndex(static_cast<std::size_t>(row) + 1);
    });
    layout->addWidget(moveHistoryList_, 1);

    // history_/historyIndex_ can change from many places (a live move, undo/redo, a jump, load/
    // import) - boardChanged already fires after every one of them (traced in the plan), so no
    // new signal is needed to keep this list in sync.
    connect(controller_, &GameController::boardChanged, this,
            [this](const BoardWidget::DisplayState&) { updateMoveHistoryList(); });

    return pane;
}

void MainWindow::updateMoveHistoryList() {
    const std::vector<int> moves = controller_->fullMoveList();
    const std::size_t currentIndex = controller_->currentHistoryIndex();

    moveHistoryList_->clear();
    QFont currentFont = moveHistoryList_->font();
    currentFont.setBold(true);
    for (std::size_t i = 0; i < moves.size(); ++i) {
        auto* item = new QListWidgetItem(QStringLiteral("%1. %2").arg(i + 1).arg(
            QString::fromStdString(reversi::squareToString(moves[i]))));
        // history_[0] is the start position (no move); fullMoveList()[i] is history_[i + 1] - so
        // this item is "current" iff currentIndex == i + 1.
        if (i + 1 == currentIndex) {
            item->setFont(currentFont);
            item->setForeground(QBrush(chrome::palette().accentColor));
        }
        moveHistoryList_->addItem(item);
    }
}

// M9 phase 3: on-demand MultiPV analysis of the CURRENT position - "Analyze Position" ranks
// candidate moves plus a principal variation for the top line. Builds and returns its own pane
// widget (M9 phase 5: previously built directly into panel_ before panel_ grew a second section).
QWidget* MainWindow::setupAnalysisPanel() {
    auto* pane = new QWidget(panel_);
    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* headerLabel = new QLabel(QStringLiteral("Analysis"), pane);
    QFont headerFont = headerLabel->font();
    headerFont.setPointSize(headerFont.pointSize() + 2);
    headerFont.setBold(true);
    headerLabel->setFont(headerFont);
    layout->addWidget(headerLabel);

    analyzeButton_ = new QPushButton(QStringLiteral("Analyze Position"), pane);
    layout->addWidget(analyzeButton_);

    analysisStatusLabel_ = new QLabel(QStringLiteral("Ready"), pane);
    layout->addWidget(analysisStatusLabel_);

    // M9 phase 5: a scroll area over a plain QVBoxLayout of row widgets - renderAnalysisResults()
    // clears and rebuilds analysisResultsLayout_'s children on every call (see that function's
    // own doc comment for why this replaced a single QPlainTextEdit).
    auto* resultsScroll = new QScrollArea(pane);
    resultsScroll->setWidgetResizable(true);
    resultsScroll->setFrameShape(QFrame::NoFrame);
    auto* resultsContent = new QWidget(resultsScroll);
    analysisResultsLayout_ = new QVBoxLayout(resultsContent);
    analysisResultsLayout_->setContentsMargins(0, 0, 0, 0);
    analysisResultsLayout_->setSpacing(8);
    auto* placeholderLabel = new QLabel(
        QStringLiteral("Click \"Analyze Position\" to rank candidate moves."), resultsContent);
    placeholderLabel->setWordWrap(true);
    analysisResultsLayout_->addWidget(placeholderLabel);
    analysisResultsLayout_->addStretch(1);
    resultsScroll->setWidget(resultsContent);
    layout->addWidget(resultsScroll, 1);

    connect(analyzeButton_, &QPushButton::clicked, this, [this] {
        controller_->analyzePosition();
        analysisStatusLabel_->setText(QStringLiteral("Analyzing..."));
        updateAnalyzeButtonEnabled();
    });
    connect(controller_, &GameController::analysisFinished, this,
            [this](const std::vector<reversi::RankedMove>& lines, const std::vector<int>& pv,
                   bool blackToMove) {
                analysisStatusLabel_->setText(QStringLiteral("Ready"));
                renderAnalysisResults(lines, pv, blackToMove);
                updateAnalyzeButtonEnabled();
            });
    // canAnalyze() depends on whether the AI is thinking, which changes every time the board
    // does (a human move can trigger an AI search) - re-check on every boardChanged, not just
    // when analysis itself starts/finishes.
    connect(controller_, &GameController::boardChanged, this,
            [this](const BoardWidget::DisplayState&) { updateAnalyzeButtonEnabled(); });

    return pane;
}

void MainWindow::updateAnalyzeButtonEnabled() {
    const bool canAnalyze = controller_->canAnalyze();
    analyzeButton_->setEnabled(canAnalyze);
    // Analysis can become available again WITHOUT analysisFinished ever firing - e.g. starting a
    // new game, undo/redo, or loading/importing while an analysis was running all cancel it
    // outright (GameController::cancelAnalysis()) rather than letting it complete. Without this,
    // the "Analyzing..." text left over from the cancelled run would linger, even though the
    // button itself is correctly usable again.
    if (canAnalyze && analysisStatusLabel_->text() == QStringLiteral("Analyzing...")) {
        analysisStatusLabel_->setText(QStringLiteral("Ready"));
    }
}

// M9 phase 5: rebuilt from a single QPlainTextEdit text block into real per-line row widgets
// (buildAnalysisLineRow()) - a structural change to already-working, already-verified code, not
// a color/font restyle, per the user's own framing of this step. The signal/slot boundary this
// is called from (GameController::analysisFinished, connected in setupAnalysisPanel()) is
// completely unchanged; only what happens inside this function changed. Clears every existing
// child (row widgets, a placeholder label, the trailing stretch) and rebuilds from scratch each
// call - simplest correct way to keep a variable-length row list in sync with each new result.
void MainWindow::renderAnalysisResults(const std::vector<reversi::RankedMove>& lines,
                                       const std::vector<int>& pv, bool blackToMove) {
    while (QLayoutItem* item = analysisResultsLayout_->takeAt(0)) {
        delete item
            ->widget(); // nullptr for the trailing stretch item; delete on nullptr is a no-op
        delete item;
    }

    if (lines.empty()) {
        auto* placeholderLabel = new QLabel(QStringLiteral("No analysis result."));
        placeholderLabel->setWordWrap(true);
        analysisResultsLayout_->addWidget(placeholderLabel);
        analysisResultsLayout_->addStretch(1);
        return;
    }

    for (std::size_t i = 0; i < lines.size(); ++i) {
        analysisResultsLayout_->addWidget(
            buildAnalysisLineRow(static_cast<int>(i) + 1, lines[i], i == 0, pv, blackToMove));
    }
    analysisResultsLayout_->addStretch(1);
}

QWidget* MainWindow::buildAnalysisLineRow(int rank, const reversi::RankedMove& line, bool isTopLine,
                                          const std::vector<int>& pv, bool blackToMove) {
    const chrome::Palette& theme = chrome::palette();

    auto* card = new QFrame();
    card->setObjectName(QStringLiteral("analysisLineCard"));
    card->setStyleSheet(QStringLiteral("QFrame#analysisLineCard {"
                                       "  background-color: %1;"
                                       "  border: 1px solid %2;"
                                       "  border-radius: 8px;"
                                       "}")
                            .arg(theme.popupBackground.name(), theme.panelBorder.name()));
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(10, 8, 10, 8);
    cardLayout->setSpacing(4);

    // Top line: rank badge, move (bold), score.
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(8);

    auto* rankBadge = new QLabel(QString::number(rank));
    rankBadge->setAlignment(Qt::AlignCenter);
    rankBadge->setFixedSize(20, 20);
    // Rank 1 (the best line) gets the solid accent color; the rest share a neutral, muted badge -
    // the same "one thing gets emphasis" principle accentColor's own Palette.hpp doc comment
    // describes.
    const QString badgeBackground = (isTopLine ? theme.accentColor : theme.panelBorder).name();
    const QString badgeText = isTopLine ? theme.windowBackground.name() : theme.textColor.name();
    rankBadge->setStyleSheet(QStringLiteral("background-color: %1; color: %2; border-radius: 10px; "
                                            "font-weight: 600;")
                                 .arg(badgeBackground, badgeText));
    topRow->addWidget(rankBadge);

    auto* moveLabel = new QLabel(QString::fromStdString(reversi::squareToString(line.move)));
    QFont moveFont = moveLabel->font();
    moveFont.setBold(true);
    moveLabel->setFont(moveFont);
    topRow->addWidget(moveLabel);
    topRow->addStretch(1);

    // RankedMove::score is mover-relative (analysis.hpp); the panel's own display convention is
    // fixed instead, chess.com-style: positive favors White, negative favors Black, regardless of
    // whose turn was actually analyzed.
    const int displayScore = blackToMove ? -line.score : line.score;
    const QString scoreText =
        displayScore > 0 ? QStringLiteral("+%1").arg(displayScore) : QString::number(displayScore);
    auto* scoreLabel = new QLabel(scoreText);
    QFont scoreFont = scoreLabel->font();
    scoreFont.setBold(true);
    scoreLabel->setFont(scoreFont);
    topRow->addWidget(scoreLabel);

    cardLayout->addLayout(topRow);

    // Dimmer meta line: depth/nodes.
    auto* metaLabel = new QLabel(
        QStringLiteral("depth %1 · %2 nodes").arg(line.depth).arg(formatNodeCount(line.nodes)));
    QFont metaFont = metaLabel->font();
    metaFont.setPointSize(qMax(metaFont.pointSize() - 1, 1));
    metaLabel->setFont(metaFont);
    metaLabel->setStyleSheet(
        QStringLiteral("color: %1;").arg(theme.panelHover.lighter(160).name()));
    cardLayout->addWidget(metaLabel);

    // PV, top line only, its own dimmer line.
    if (isTopLine && !pv.empty()) {
        QStringList pvSquares;
        for (int move : pv) {
            pvSquares << QString::fromStdString(reversi::squareToString(move));
        }
        auto* pvLabel =
            new QLabel(QStringLiteral("PV: %1").arg(pvSquares.join(QStringLiteral(" "))));
        pvLabel->setWordWrap(true);
        QFont pvFont = pvLabel->font();
        pvFont.setPointSize(qMax(pvFont.pointSize() - 1, 1));
        pvLabel->setFont(pvFont);
        pvLabel->setStyleSheet(
            QStringLiteral("color: %1;").arg(theme.panelHover.lighter(160).name()));
        cardLayout->addWidget(pvLabel);
    }

    return card;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    controller_->cancelAiSearch();
    controller_->cancelAnalysis();
    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        titleBar_->setMaximized(isMaximized());
    }
}
