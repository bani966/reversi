#include "MainWindow.hpp"

#include "BoardWidget.hpp"

#include "reversi/position.hpp"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    board_ = new BoardWidget(this);
    setCentralWidget(board_);
    setWindowTitle(QStringLiteral("Reversi"));
    resize(720, 760);

    const reversi::Position start = reversi::Position::start();
    BoardWidget::DisplayState state;
    state.blackDiscs = start.own; // black moves first, so own == black at the start position
    state.whiteDiscs = start.opp;
    board_->setDisplayState(state);
}
