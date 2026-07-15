#include "MainWindow.hpp"

#include "BoardWidget.hpp"
#include "GameController.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    board_ = new BoardWidget(this);
    setCentralWidget(board_);
    setWindowTitle(QStringLiteral("Reversi"));
    resize(720, 760);

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
