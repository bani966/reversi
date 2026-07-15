#include "MainWindow.hpp"

#include "BoardWidget.hpp"
#include "GameController.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>

namespace {
// Stylesheet pass only - QMenuBar/QMenu/QStatusBar stay real native widgets, just re-skinned
// to match the board's dark, flat aesthetic instead of default Windows menu chrome. Colors
// are duplicated from BoardWidget's BoardPalette (windowBackground #18181A, coordinateText
// #E8E0D0) rather than shared through a header - a real cross-widget palette is M9's job, this
// is deliberately just enough consistency for the current styling pass.
const char* const kChromeStyleSheet = R"(
    QMenuBar {
        background-color: #18181A;
        color: #E8E0D0;
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
        background-color: #2A2A2E;
    }
    QMenu {
        background-color: #1E1E20;
        color: #E8E0D0;
        font-family: "Segoe UI";
        border: 1px solid #333336;
        padding: 4px;
    }
    QMenu::item {
        padding: 6px 28px 6px 16px;
        border-radius: 4px;
    }
    QMenu::item:selected {
        background-color: #2A2A2E;
    }
    QStatusBar {
        background-color: #18181A;
        color: #E8E0D0;
        font-family: "Segoe UI";
        font-weight: 500;
        border-top: 1px solid #2A2A2E;
    }
)";
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    board_ = new BoardWidget(this);
    setCentralWidget(board_);
    setWindowTitle(QStringLiteral("Reversi"));
    resize(720, 760);
    setStyleSheet(QString::fromUtf8(kChromeStyleSheet));

    controller_ = new GameController(this);
    connect(controller_, &GameController::boardChanged, board_, &BoardWidget::setDisplayState);
    connect(board_, &BoardWidget::squareClicked, controller_, &GameController::onSquareClicked);
    connect(controller_, &GameController::statusChanged, this,
            [this](const QString& text) { statusBar()->showMessage(text); });

    createGameMenu();

    controller_->newGame(GameController::GameMode::HumanVsHuman);
}

void MainWindow::createGameMenu() {
    QMenu* gameMenu = menuBar()->addMenu(QStringLiteral("&Game"));

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
