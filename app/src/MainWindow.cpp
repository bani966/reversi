#include "MainWindow.hpp"

#include "BoardWidget.hpp"
#include "GameController.hpp"
#include "Palette.hpp"
#include "TitleBarWidget.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QEvent>
#include <QMenu>
#include <QMenuBar>
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
    resize(720,
           796); // was 720x760; +36px to make room for the title bar replacing native decoration
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

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(titleBar_);
    layout->addWidget(menuBar_);
    layout->addWidget(board_, 1);
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
