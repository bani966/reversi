#include "MainWindow.hpp"

#include "BoardWidget.hpp"
#include "GameController.hpp"
#include "Palette.hpp"
#include "SettingsDialog.hpp"
#include "TitleBarWidget.hpp"

#include "reversi/analysis.hpp"
#include "reversi/position.hpp"

#include <QAbstractItemView>
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
#include <QSizePolicy>
#include <QSplitter>
#include <QStringList>
#include <QVBoxLayout>

#include <cstdint>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>
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

// M10: Qt::FramelessWindowHint clears WS_CAPTION/WS_THICKFRAME from the underlying Win32 window -
// answering WM_NCHITTEST with HTLEFT/HTRIGHT/etc (see nativeEvent() below) is not, by itself,
// enough to make the OS actually respond to a resize drag there without these style bits also
// being set (Windows' own default WM_NCHITTEST/resize-drag machinery in DefWindowProc is gated on
// them). Re-adding them here - and, critically, handling WM_NCCALCSIZE below to reserve zero
// non-client area - is the standard technique for a window that has WS_CAPTION|WS_THICKFRAME's
// OS-level behaviors (resize borders, Aero Snap) without Windows ever actually drawing the native
// caption/border chrome that would normally come with them: WM_NCCALCSIZE's job is precisely to
// tell Windows how much of the window is client area, and claiming "all of it" is what suppresses
// the native chrome while these style bits stay in effect for hit-testing purposes.
// SWP_FRAMECHANGED forces Windows to recompute the non-client area from the new style bits
// immediately, rather than only on some future geometry change.
void enableWindowsResizeFrame(QWidget* window) {
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    const LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    SetWindowLongPtr(hwnd, GWL_STYLE, style | WS_THICKFRAME | WS_CAPTION);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// M10: how many logical pixels around the window's edge count as a resize grip - see
// MainWindow::nativeEvent()'s own doc comment for why this exists at all. Not scaled by
// devicePixelRatio(): WM_NCHITTEST's lParam is already in physical screen coordinates, matching
// GetWindowRect()'s own physical-pixel result below, so no separate DPI conversion is needed here.
constexpr LONG kResizeBorderPx = 8;
#endif

// Stylesheet pass only - QMenuBar/QMenu stay real native widgets, just re-skinned to match the
// board's dark, flat aesthetic instead of default Windows menu chrome. Built from
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
    )")
        .arg(theme.windowBackground.name()) // %1
        .arg(theme.textColor.name())        // %2
        .arg(theme.panelHover.name())       // %3
        .arg(theme.popupBackground.name())  // %4
        .arg(theme.panelBorder.name());     // %5
}

// M10 phase 3: statusLabel_'s stylesheet, extracted so MainWindow::refreshTheme() and the
// constructor share exactly one definition instead of two copies that could drift apart.
QString statusLabelStyleSheet() {
    const chrome::Palette& theme = chrome::palette();
    return QStringLiteral("QLabel {"
                          "  background-color: %1;"
                          "  color: %2;"
                          "  border-radius: 6px;"
                          "  padding: 8px;"
                          "  font-family: \"Segoe UI\";"
                          "}")
        .arg(theme.popupBackground.name(), theme.textColor.name());
}

// M10 phase 3: shared by setupMoveHistoryPanel()/setupAnalysisPanel() and
// MainWindow::refreshTheme() - both panes are QFrames with an identical "one shade lighter than
// panel_'s own base" treatment (see either setup function's own comment), differing only in
// objectName.
QString shadedPaneStyleSheet(const QString& objectName) {
    return QStringLiteral("QFrame#%1 {"
                          "  background-color: %2;"
                          "  border-radius: 6px;"
                          "}")
        .arg(objectName, chrome::palette().popupBackground.name());
}

// M10 phase 3: the "Analysis" toolbar toggle's stylesheet, extracted for the same reason as
// statusLabelStyleSheet() above.
QString toolbarButtonStyleSheet() {
    const chrome::Palette& theme = chrome::palette();
    // border: none is required, not optional decoration - Qt's QSS cascade resolves PER PROPERTY
    // by specificity, not by "whichever selector matches wins entirely": the generic `QPushButton`
    // rule in chrome::panelControlsStyleSheet() (still applied via panel_'s own stylesheet, an
    // ancestor of this button) sets `border: 1px solid panelBorder`, and #toolbarButton not
    // mentioning `border` at all doesn't clear that - it just leaves the less-specific rule's
    // border property in effect. This button never had a border in the design, only in practice.
    return QStringLiteral("QPushButton#toolbarButton {"
                          "  background-color: %1;"
                          "  color: %2;"
                          "  border: none;"
                          "  border-radius: 6px;"
                          "  padding: 8px;"
                          "  font-family: \"Segoe UI\";"
                          "  font-weight: 600;"
                          "}"
                          "QPushButton#toolbarButton:hover {"
                          "  background-color: %3;"
                          "}"
                          "QPushButton#toolbarButton:checked {"
                          "  background-color: %4;"
                          "  color: %5;"
                          "}")
        .arg(theme.popupBackground.name(), theme.textColor.name(), theme.panelHover.name(),
             theme.accentColor.name(), theme.accentTextColor.name());
}

// M10 phase 3: a real bug found alongside the theme audit, not introduced by it - moveHistoryList_
// never had an explicit `color` in its own stylesheet (only border/background/item padding), so
// every entry WITHOUT an explicit per-item foreground (i.e. every entry except the current one,
// which updateMoveHistoryList() sets via QListWidgetItem::setForeground(accentColor)) fell back to
// Fusion's own default QPalette::Text instead of chrome::palette().textColor - invisible against
// the light theme's own light background. Extracted so this and refreshTheme() share one
// definition.
QString moveHistoryListStyleSheet() {
    return QStringLiteral("QListWidget { border: none; background: transparent; color: %1; }"
                          "QListWidget::item { padding: 6px 4px; }")
        .arg(chrome::palette().textColor.name());
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
    // M9 phase 5: no floor existed before this - shrinking far enough (e.g. 380x300) squeezed
    // board_ down to a near-invisible sliver and clipped the analysis pane's button/results
    // entirely off-screen, found by manually testing the resize behavior with the panel's now-
    // real (phases 3-5) content, not the empty placeholder phase 1's own verification checked
    // against. 700x600 is the smallest size manually confirmed to still render every section
    // (title/menu/board/panel/status) legibly.
    setMinimumSize(700, 600);
    setStyleSheet(buildChromeStyleSheet());

#ifdef Q_OS_WIN
    applyWindowsCornerRounding(this);
    enableWindowsResizeFrame(this);
#endif

    // QMainWindow's own menuBar() docks automatically above whatever is set as the central
    // widget - that would put it below our custom title bar, not above it. So the whole stack
    // (title bar, menu, board) is built as ordinary child widgets in one layout and set as the
    // central widget instead; menuBar_ stays a real QMenuBar (round 1's QSS still applies to it
    // identically either way), just manually placed rather than using QMainWindow's docking
    // convenience methods. M10: the old QStatusBar is gone entirely, replaced by statusLabel_
    // (below), a styled label scoped to board_'s own column rather than the whole window.
    titleBar_ = new TitleBarWidget(this);
    titleBar_->setTitle(windowTitle());

    menuBar_ = new QMenuBar(this);
    board_ = new BoardWidget(this);

    // M10: replaces the old full-width QStatusBar - a styled, borderless pill matching board_'s
    // own width (see boardColumn below), not generic OS status-bar chrome spanning the whole
    // window including the panel column.
    statusLabel_ = new QLabel(this);
    statusLabel_->setAlignment(Qt::AlignCenter);
    // Expanding (not the QLabel default of Preferred): lets the label stretch to fill whatever
    // width it's given up to its maximumWidth cap below, instead of shrinking to its own text's
    // sizeHint - see the maximumWidth comment for why this, not setFixedWidth, is load-bearing.
    statusLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QFont statusFont = statusLabel_->font();
    statusFont.setPointSize(statusFont.pointSize() + 1);
    statusLabel_->setFont(statusFont);
    statusLabel_->setStyleSheet(statusLabelStyleSheet());
    // board_ itself is generally wider/taller than the square it actually draws (letterboxed to
    // stay square - see BoardWidget::recomputeBoardGeometry()), so matching board_->width() alone
    // would leave statusLabel_ wider than the visible board whenever the column isn't itself
    // square. boardWidthChanged reports the real rendered square's side length instead.
    //
    // setMaximumWidth, deliberately NOT setFixedWidth: a fixed width sets minimumWidth too, and a
    // QVBoxLayout's own minimum-size hint is the MAX of its children's minimum widths - that
    // inflated boardColumn's (and transitively the whole window's) minimum size every time the
    // board was drawn larger, and never shrank back down, since Qt won't auto-shrink an
    // already-open window just because a child's minimum decreased. The practical symptom: after
    // maximizing once, restoring back down got silently clamped to the inflated minimum -
    // "resizing" appeared to stop working entirely. maximumWidth alone caps growth without touching
    // minimumSizeHint at all, so it can't ratchet.
    connect(board_, &BoardWidget::boardWidthChanged, statusLabel_,
            [this](int boardPixels) { statusLabel_->setMaximumWidth(boardPixels); });

    // M9 phase 1: side panel (phases 3-5 populate: analysis panel, move history, and - phase 5 -
    // a real radius; M10 drops the border it briefly had, see Palette.hpp). A QFrame, not a plain
    // QWidget - see MainWindow.hpp's own comment on panel_ for why (Qt::WA_StyledBackground/
    // QFrame is what makes an explicit QSS background actually paint; a bare QWidget's own
    // background otherwise defaults to Qt's system palette, unstyled, which is what phase 1
    // originally worked around a different way, via setAutoFillBackground - the M9 phase 5
    // QFrame#sidePanel selector in chrome::panelControlsStyleSheet() replaces that palette-based
    // fill entirely).
    panel_ = new QFrame(this);
    panel_->setObjectName(QStringLiteral("sidePanel"));
    // A bare QWidget/QFrame has no usable sizeHint() for layout purposes; without an explicit
    // minimum the QHBoxLayout below would collapse it to zero width. 300px is a first-pass
    // placeholder loosely sized on chess.com's own side-panel width - not load-bearing.
    panel_->setMinimumWidth(300);

    // board_ keeps the stretch factor so it - not panel_ - absorbs extra space on resize,
    // mirroring how board_ already gets stretch 1 vertically in the outer layout below. Margins
    // on all four sides (originally left-only, matching panel_'s own 12px internal content
    // margin so the board didn't sit flush against the window while the panel's content had a
    // visible inset - see git history) now also serve a second purpose as of M9 phase 5: panel_
    // has a real radius (QFrame#sidePanel, chrome::panelControlsStyleSheet()), which is
    // otherwise invisible - flush against the window's edges and directly adjacent to board_
    // with zero spacing, there is no background visible behind any of its corners for the
    // rounding to show against.
    //
    // M10: board_ and statusLabel_ are stacked in their own column (boardColumn) so the status
    // pill sits directly under the board, matching the board's own width - not the old QStatusBar,
    // which spanned the whole window including the panel column.
    auto* boardColumn = new QVBoxLayout();
    boardColumn->setContentsMargins(0, 0, 0, 0);
    boardColumn->setSpacing(8);
    boardColumn->addWidget(board_, 1);
    // A stretch-flanked QHBoxLayout, not Qt::AlignHCenter on addWidget: AlignHCenter would size
    // statusLabel_ to its own sizeHint (its text's natural width) and ignore the Expanding size
    // policy set above entirely, defeating the whole point of letting it stretch up to
    // maximumWidth. Two stretches either side of the label center it correctly whether or not the
    // maximumWidth cap is currently active, without touching alignment/sizeHint semantics at all.
    auto* statusRow = new QHBoxLayout();
    statusRow->addStretch(1);
    statusRow->addWidget(statusLabel_);
    statusRow->addStretch(1);
    boardColumn->addLayout(statusRow);

    auto* boardRow = new QHBoxLayout();
    boardRow->setContentsMargins(12, 12, 12, 12);
    boardRow->setSpacing(12);
    boardRow->addLayout(boardColumn, 1);
    boardRow->addWidget(panel_);

    auto* container = new QWidget(this);
    // Same correctness fix as panel_'s own background above, for the same reason: boardRow's new
    // left margin exposes a strip of container's raw background for the first time (every pixel
    // was previously covered by a child widget) - without this it would show Qt's default system
    // palette instead of the app's chrome, and would silently drift out of sync with the rest of
    // the chrome under a theme change (M10 phase 3: refreshTheme() re-applies this exact line via
    // centralWidget(), reachable since container becomes exactly that below), unlike a color read
    // from chrome::palette().
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
        statusLabel_->setText(text);
        // canAnalyze() flips to false the instant the AI starts thinking, which happens inside
        // startAiSearch() before its own statusChanged emit (see that function's own comment) -
        // catching it here, not just on boardChanged, is what actually disables the button in
        // time rather than only after the AI's move has already completed.
        updateAnalyzeButtonEnabled();
    });

    setupPanelContent();
    createMenus();

    // M10 phase 3: every member refreshTheme() touches already exists past this point - connected
    // here, not called here, since each setup function above already applied its own correct
    // initial style once (calling refreshTheme() too would just be a redundant duplicate pass).
    connect(&chrome::ThemeManager::instance(), &chrome::ThemeManager::themeChanged, this,
            [this] { refreshTheme(); });

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
    QMenu* gameMenu = menuBar_->addMenu(QStringLiteral("&New Game"));

    QAction* humanVsHuman = gameMenu->addAction(QStringLiteral("Human vs Human"));
    connect(humanVsHuman, &QAction::triggered, this,
            [this] { controller_->newGame(GameMode::HumanVsHuman); });

    QAction* humanIsBlack =
        gameMenu->addAction(QStringLiteral("Human vs AI (You play Black)"));
    connect(humanIsBlack, &QAction::triggered, this,
            [this] { controller_->newGame(GameMode::HumanIsBlack); });

    QAction* humanIsWhite =
        gameMenu->addAction(QStringLiteral("Human vs AI (You play White)"));
    connect(humanIsWhite, &QAction::triggered, this,
            [this] { controller_->newGame(GameMode::HumanIsWhite); });

    QAction* aiVsAi = gameMenu->addAction(QStringLiteral("AI vs AI"));
    connect(aiVsAi, &QAction::triggered, this, [this] { controller_->newGame(GameMode::AiVsAi); });
}

// M10 phase 3: re-applies every chrome::palette()-derived style this class owns - see the header's
// own doc comment for exactly what that is and why it's only ever connected to
// chrome::ThemeManager::themeChanged, never called at construction time.
void MainWindow::refreshTheme() {
    setStyleSheet(buildChromeStyleSheet());
    if (QWidget* container = centralWidget()) {
        QPalette containerPalette = container->palette();
        containerPalette.setColor(QPalette::Window, chrome::palette().windowBackground);
        container->setPalette(containerPalette);
    }
    statusLabel_->setStyleSheet(statusLabelStyleSheet());
    panel_->setStyleSheet(chrome::panelControlsStyleSheet());
    moveHistoryPane_->setStyleSheet(shadedPaneStyleSheet(QStringLiteral("moveHistoryPane")));
    analysisPane_->setStyleSheet(shadedPaneStyleSheet(QStringLiteral("analysisPane")));
    analysisToggleButton_->setStyleSheet(toolbarButtonStyleSheet());
    moveHistoryList_->setStyleSheet(moveHistoryListStyleSheet());
    // Refreshes moveHistoryList_'s current-row accentColor brush (and, via the stylesheet call
    // just above, every OTHER row's textColor) under the new palette - cheap, rebuilds from
    // controller_'s already-current state. NOT done for already-rendered MultiPV result cards
    // (buildAnalysisLineRow() bakes colors into each card at render time, and there's no stored
    // "last results" to replay through a refresh without adding state that has no other use) - a
    // stale card keeps its previous theme's colors until the next "Analyze Position" click, a
    // deliberate, small, accepted gap rather than one silently patched over.
    updateMoveHistoryList();
}

// M9 phase 5: panel_'s two sections (move history, analysis) live in a vertical QSplitter so
// each list-like section (a growing move list; a growing MultiPV results list) can be resized by
// the user rather than fighting over a fixed stacked-layout split. M10: a fixed bottom toolbar
// row (setupPanelToolbar()) sits below the splitter - chess.com's own flag/undo/lightbulb row -
// and analysisPane_ now starts hidden, toggled open/closed by that row's "Analysis" button rather
// than always occupying its own permanent splitter section. analysisPane_ is added to the
// splitter FIRST (top), move history second - so expanding Analysis makes it appear above the
// move list, directly over the toolbar button that opened it, not below.
void MainWindow::setupPanelContent() {
    panel_->setStyleSheet(chrome::panelControlsStyleSheet());

    auto* layout = new QVBoxLayout(panel_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Vertical, panel_);
    analysisPane_ = setupAnalysisPanel();
    analysisPane_->setVisible(false);
    splitter->addWidget(analysisPane_);
    splitter->addWidget(setupMoveHistoryPanel());
    layout->addWidget(splitter, 1);

    layout->addWidget(setupPanelToolbar());
}

// M9 phase 5: move-history list - a missed item from the original spec (alongside undo/redo and
// save/load), not new scope. Reuses GameController::jumpToHistoryIndex()/fullMoveList()/
// currentHistoryIndex() exactly - no new history tracking here, this widget only ever displays
// what GameController already owns.
QWidget* MainWindow::setupMoveHistoryPanel() {
    // M10: a QFrame with its own subtle background (one shade lighter than panel_'s own base),
    // not a plain transparent QWidget - "sections separated by shading, not borders/blocky
    // elements" (chess.com's own panel convention, per the user's own framing) is delivered by
    // this shading contrast alone, no border anywhere.
    moveHistoryPane_ = new QFrame(panel_);
    moveHistoryPane_->setObjectName(QStringLiteral("moveHistoryPane"));
    moveHistoryPane_->setStyleSheet(shadedPaneStyleSheet(QStringLiteral("moveHistoryPane")));
    auto* layout = new QVBoxLayout(moveHistoryPane_);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* headerLabel = new QLabel(QStringLiteral("Move History"), moveHistoryPane_);
    QFont headerFont = headerLabel->font();
    headerFont.setPointSize(headerFont.pointSize() + 2);
    headerFont.setBold(true);
    headerLabel->setFont(headerFont);
    layout->addWidget(headerLabel);

    moveHistoryList_ = new QListWidget(moveHistoryPane_);
    // M10: bigger rows, per the user's request - both a larger base font and taller per-item
    // padding (the font bump alone doesn't grow the row height without also giving it more
    // padding to sit in). Bold too (every entry, not just the current one) - the base font here is
    // what every non-current entry ends up using; updateMoveHistoryList() takes the current entry
    // one size larger still on top of this.
    QFont listFont = moveHistoryList_->font();
    listFont.setPointSize(listFont.pointSize() + 3);
    listFont.setBold(true);
    moveHistoryList_->setFont(listFont);
    moveHistoryList_->setStyleSheet(moveHistoryListStyleSheet());
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

    return moveHistoryPane_;
}

void MainWindow::updateMoveHistoryList() {
    const std::vector<int> moves = controller_->fullMoveList();
    const std::size_t currentIndex = controller_->currentHistoryIndex();

    moveHistoryList_->clear();
    // The base list font (moveHistoryList_->font(), set in setupMoveHistoryPanel()) is already
    // bold - this is bold too (redundant but explicit) and one size larger still, so the current
    // entry reads as bigger than every other bold entry, not just differently colored.
    QFont currentFont = moveHistoryList_->font();
    currentFont.setBold(true);
    currentFont.setPointSize(currentFont.pointSize() + 1);
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

    // M10: always scroll to keep the current entry visible, rather than special-casing "a move
    // was added" vs. "a move was undone" separately - a live move appends and the new current
    // entry is the new last one (scrolls down), an undo/jump lands earlier in the list (scrolls
    // back up if it had fallen out of view), and this one rule covers both correctly.
    // fullMoveList()[currentIndex - 1] is the current entry's row (see the loop above); at
    // currentIndex == 0 (the start position) there's no item to scroll to, so just show the top.
    if (currentIndex > 0) {
        if (QListWidgetItem* currentItem =
                moveHistoryList_->item(static_cast<int>(currentIndex) - 1)) {
            moveHistoryList_->scrollToItem(currentItem, QAbstractItemView::EnsureVisible);
        }
    } else {
        moveHistoryList_->scrollToTop();
    }
}

// M9 phase 3: on-demand MultiPV analysis of the CURRENT position - "Analyze Position" ranks
// candidate moves plus a principal variation for the top line. Builds and returns its own pane
// widget (M9 phase 5: previously built directly into panel_ before panel_ grew a second section).
// M10: starts hidden - toggled by the "Analysis" pill in setupPanelToolbar() (see
// setupPanelContent()) - everything below is otherwise completely unchanged from M9.
QWidget* MainWindow::setupAnalysisPanel() {
    // Same shaded-QFrame treatment as the move-history pane - see that function's own comment.
    auto* pane = new QFrame(panel_);
    pane->setObjectName(QStringLiteral("analysisPane"));
    pane->setStyleSheet(shadedPaneStyleSheet(QStringLiteral("analysisPane")));
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
    // M10: QScrollArea (and its internal viewport widget, which needs its OWN stylesheet - setting
    // one on the QScrollArea alone doesn't reach it) paints an opaque Fusion-style palette
    // background by default, which doesn't match analysisPane_'s popupBackground fill - flagged as
    // a visibly mismatched background around the ranked-move cards. Making both transparent lets
    // the pane's own shading show through instead.
    auto* resultsScroll = new QScrollArea(pane);
    resultsScroll->setWidgetResizable(true);
    resultsScroll->setFrameShape(QFrame::NoFrame);
    resultsScroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
    resultsScroll->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));
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

// M10: panel_'s fixed bottom row, mirroring the board's own statusLabel_ (MainWindow.cpp's
// constructor) rather than chess.com's small icon-toolbar buttons - a full-width, centered bar
// styled identically to that status pill, "matching the info panel below the board" per the
// explicit feedback that prompted this revision. Today this only holds the "Analysis" toggle
// (analysisPane_'s show/hide switch, wired here); a plain QHBoxLayout leaves room for more
// buttons alongside it later without restructuring anything.
QWidget* MainWindow::setupPanelToolbar() {
    auto* bar = new QWidget(panel_);
    auto* barLayout = new QHBoxLayout(bar);
    // M10 phase 3: bottom margin 0, not 12 - panel_'s own outer spacing already comes from
    // boardRow's shared 12px margin (the same one board_/statusLabel_ get), so a nonzero bottom
    // margin here was PURELY EXTRA, asymmetric padding with nothing on the board's own side to
    // match - it read as the Analysis button (and the panel's own visible content) stopping
    // noticeably short of the panel's true bottom edge, not lining up with statusLabel_'s own
    // bottom the way the board's own content does. Confirmed by temporarily bordering panel_/the
    // splitter/this bar directly and screenshotting: panel_'s own bounding box already closely
    // matches boardColumn's height - this margin was the entire visible gap, not a real layout bug.
    barLayout->setContentsMargins(12, 8, 12, 0);
    barLayout->setSpacing(8);

    // M10 (revised): a full-width bar spanning the panel, styled identically to statusLabel_
    // (same popupBackground fill, same 6px radius) rather than a small standalone pill off to one
    // side - "shouldn't be standalone... match the info panel below the board" was the explicit
    // feedback on the first version of this control. :checked still gets an accentColor fill so
    // the open/closed state is visible at a glance (the same "one thing gets emphasis" accentColor
    // already uses elsewhere - MultiPV's top-line badge, the move-history current-row indicator).
    analysisToggleButton_ = new QPushButton(QStringLiteral("Analysis"), bar);
    analysisToggleButton_->setObjectName(QStringLiteral("toolbarButton"));
    analysisToggleButton_->setCheckable(true);
    analysisToggleButton_->setStyleSheet(toolbarButtonStyleSheet());
    // Only toggles visibility - GameController's analysisFinished/canAnalyze() wiring inside
    // analysisPane_ (setupAnalysisPanel(), above) is completely untouched; clicking "Analyze
    // Position" inside the revealed section still triggers the real analysis exactly as before.
    connect(analysisToggleButton_, &QPushButton::toggled, this,
            [this](bool checked) { analysisPane_->setVisible(checked); });
    // Stretch 1, no trailing addStretch: fills the toolbar row's full width (panel_'s own width,
    // matching the ask directly) instead of shrinking to its own text.
    barLayout->addWidget(analysisToggleButton_, 1);

    return bar;
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
    // M10 phase 3: no border - matching the rest of the app's "sections separated by shading, not
    // borders/blocky elements" convention (the side panel, its own move-history/analysis panes).
    // panelHover, not popupBackground: analysisPane_ (the card's own parent) already uses
    // popupBackground for ITS fill, so the cards need a genuinely different shade to read as
    // distinct rows via shading alone - panelHover reads as a gentle lift, consistent with its use
    // elsewhere as "slightly more prominent than the base" state.
    card->setStyleSheet(QStringLiteral("QFrame#analysisLineCard {"
                                       "  background-color: %1;"
                                       "  border-radius: 8px;"
                                       "}")
                            .arg(theme.panelHover.name()));
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
    // M10 phase 3: accentTextColor, not theme.windowBackground - text drawn on top of the solid
    // accentColor fill needs a color that stays legible against accentColor specifically, which is
    // theme-invariant (see Palette.hpp's own doc comment); windowBackground flips from dark to
    // light between themes and would have produced near-white text on gold in the light theme.
    const QString badgeText = isTopLine ? theme.accentTextColor.name() : theme.textColor.name();
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
    // M10 phase 3: secondaryTextColor, not theme.panelHover.lighter(160) - lightening an
    // already-dark panelHover happened to produce a visible dim gray in the dark theme, but the
    // same transform on an already-light panelHover (the light theme) washed this text out to
    // near-invisible instead. secondaryTextColor has its own correct value per theme.
    metaLabel->setStyleSheet(QStringLiteral("color: %1;").arg(theme.secondaryTextColor.name()));
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
        pvLabel->setStyleSheet(QStringLiteral("color: %1;").arg(theme.secondaryTextColor.name()));
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

// M10: Qt::FramelessWindowHint (set in the constructor, for the custom title bar + DWM corner
// rounding) removes the OS's own native frame entirely, resize borders included - this window
// could never actually be drag-resized by a real user since M3, only programmatically. Restores
// it the same way the title bar's own drag-to-move already works around the analogous problem,
// but via the lower-level WM_NCHITTEST message rather than QWindow::startSystemMove(): mouse
// clicks near the window's edges normally land on titleBar_/board_/panel_/menuBar_ (they cover
// the entire window), not on MainWindow itself, so a mouse-event-based approach would need to
// special-case which child widget the click actually landed on. Answering WM_NCHITTEST instead
// works at the OS message level, before Qt's own widget dispatch, so it applies uniformly
// regardless of which child widget nominally occupies those edge pixels - the OS then drives the
// actual resize drag itself (proper cursor icons, Aero snap, etc. all included for free).
// Windows-only; a plain passthrough to the base class on every other platform, same "acceptable
// degradation, not a broken window" posture as applyWindowsCornerRounding()'s own Windows 10 note.
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG") {
        auto* msg = static_cast<MSG*>(message);
        // WM_NCCALCSIZE: paired with enableWindowsResizeFrame()'s WS_CAPTION|WS_THICKFRAME - not
        // adjusting the proposed rect (msg->wParam == TRUE means it's already the candidate new
        // window rect) means the ENTIRE window is treated as client area, so the native
        // caption/border these style bits would normally draw never actually appears. Returning 0
        // (not calling the base implementation) is what signals that to Windows.
        if (msg->message == WM_NCCALCSIZE && msg->wParam == TRUE) {
            *result = 0;
            return true;
        }
        if (msg->message == WM_NCHITTEST) {
            RECT windowRect;
            GetWindowRect(reinterpret_cast<HWND>(winId()), &windowRect);
            const int x = GET_X_LPARAM(msg->lParam);
            const int y = GET_Y_LPARAM(msg->lParam);

            const bool onLeft = x < windowRect.left + kResizeBorderPx;
            const bool onRight = x >= windowRect.right - kResizeBorderPx;
            const bool onTop = y < windowRect.top + kResizeBorderPx;
            const bool onBottom = y >= windowRect.bottom - kResizeBorderPx;

            if (onTop && onLeft) {
                *result = HTTOPLEFT;
                return true;
            }
            if (onTop && onRight) {
                *result = HTTOPRIGHT;
                return true;
            }
            if (onBottom && onLeft) {
                *result = HTBOTTOMLEFT;
                return true;
            }
            if (onBottom && onRight) {
                *result = HTBOTTOMRIGHT;
                return true;
            }
            if (onLeft) {
                *result = HTLEFT;
                return true;
            }
            if (onRight) {
                *result = HTRIGHT;
                return true;
            }
            if (onTop) {
                *result = HTTOP;
                return true;
            }
            if (onBottom) {
                *result = HTBOTTOM;
                return true;
            }
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}
