#include "MainWindow.hpp"

#include "BoardWidget.hpp"
#include "GameController.hpp"

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

    controller_->newGame(GameController::GameMode::HumanVsHuman);
}
