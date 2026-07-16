#include "MainWindow.hpp"

#include "BoardWidget.hpp"
#include "GameController.hpp"
#include "Palette.hpp"
#include "TitleBarWidget.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QPalette>
#include <QStatusBar>
#include <QVBoxLayout>

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
    // mirroring how board_ already gets stretch 1 vertically in the outer layout below.
    auto* boardRow = new QHBoxLayout();
    boardRow->setContentsMargins(0, 0, 0, 0);
    boardRow->setSpacing(0);
    boardRow->addWidget(board_, 1);
    boardRow->addWidget(panel_);

    auto* container = new QWidget(this);
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
    connect(controller_, &GameController::statusChanged, this,
            [this](const QString& text) { statusBar_->showMessage(text); });

    createGameMenu();

    controller_->newGame(GameController::GameMode::HumanVsHuman);
}

void MainWindow::createGameMenu() {
    QMenu* gameMenu = menuBar_->addMenu(QStringLiteral("&Game"));

    QAction* humanVsHuman = gameMenu->addAction(QStringLiteral("New Game: Human vs Human"));
    connect(humanVsHuman, &QAction::triggered, this,
            [this] { controller_->newGame(GameController::GameMode::HumanVsHuman); });

    QAction* humanIsBlack =
        gameMenu->addAction(QStringLiteral("New Game: Human vs AI (You play Black)"));
    connect(humanIsBlack, &QAction::triggered, this,
            [this] { controller_->newGame(GameController::GameMode::HumanIsBlack); });

    QAction* humanIsWhite =
        gameMenu->addAction(QStringLiteral("New Game: Human vs AI (You play White)"));
    connect(humanIsWhite, &QAction::triggered, this,
            [this] { controller_->newGame(GameController::GameMode::HumanIsWhite); });
}

void MainWindow::closeEvent(QCloseEvent* event) {
    controller_->cancelAiSearch();
    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        titleBar_->setMaximized(isMaximized());
    }
}
