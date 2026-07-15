#include "BoardWidget.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <algorithm>

namespace {
constexpr int kBoardSquares = 8;
} // namespace

BoardWidget::BoardWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(false);
}

void BoardWidget::setDisplayState(const DisplayState& state) {
    state_ = state;
    update();
}

QSize BoardWidget::sizeHint() const {
    return QSize(640, 640);
}

void BoardWidget::recomputeBoardGeometry() {
    const int side = std::min(width(), height());
    cellSize_ = side / kBoardSquares;
    const int boardPixels = cellSize_ * kBoardSquares;
    boardOrigin_ = QPoint((width() - boardPixels) / 2, (height() - boardPixels) / 2);
}

int BoardWidget::squareAt(const QPoint& localPos) const {
    if (cellSize_ <= 0) {
        return -1;
    }
    const QPoint relative = localPos - boardOrigin_;
    if (relative.x() < 0 || relative.y() < 0) {
        return -1;
    }
    const int file = relative.x() / cellSize_;
    const int rank = relative.y() / cellSize_;
    if (file >= kBoardSquares || rank >= kBoardSquares) {
        return -1;
    }
    return reversi::squareIndex(file, rank);
}

void BoardWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    recomputeBoardGeometry();
}

void BoardWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const int square = squareAt(event->pos());
    if (square >= 0) {
        emit squareClicked(square);
    }
}

void BoardWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(30, 30, 30)); // letterbox color when the widget isn't square

    if (cellSize_ <= 0) {
        return;
    }

    const int boardPixels = cellSize_ * kBoardSquares;
    const QRect boardRect(boardOrigin_, QSize(boardPixels, boardPixels));
    painter.fillRect(boardRect, QColor(0, 128, 0));

    painter.setPen(QColor(0, 60, 0));
    for (int i = 0; i <= kBoardSquares; ++i) {
        const int x = boardOrigin_.x() + i * cellSize_;
        const int y = boardOrigin_.y() + i * cellSize_;
        painter.drawLine(x, boardOrigin_.y(), x, boardOrigin_.y() + boardPixels);
        painter.drawLine(boardOrigin_.x(), y, boardOrigin_.x() + boardPixels, y);
    }

    const int discMargin = cellSize_ / 10;
    const int highlightMargin = cellSize_ * 3 / 8;
    // Diagram orientation matches the CLI's: a1 top-left, rank increases downward, file
    // increases rightward.
    for (int rank = 0; rank < kBoardSquares; ++rank) {
        for (int file = 0; file < kBoardSquares; ++file) {
            const int square = reversi::squareIndex(file, rank);
            const reversi::Bitboard mask = reversi::bit(square);
            const QRect cell(boardOrigin_.x() + file * cellSize_,
                             boardOrigin_.y() + rank * cellSize_, cellSize_, cellSize_);
            if ((state_.blackDiscs & mask) != 0) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(Qt::black);
                painter.drawEllipse(
                    cell.adjusted(discMargin, discMargin, -discMargin, -discMargin));
            } else if ((state_.whiteDiscs & mask) != 0) {
                painter.setPen(QColor(60, 60, 60));
                painter.setBrush(Qt::white);
                painter.drawEllipse(
                    cell.adjusted(discMargin, discMargin, -discMargin, -discMargin));
            } else if ((state_.legalMoveHighlights & mask) != 0) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(255, 255, 255, 90));
                painter.drawEllipse(cell.adjusted(highlightMargin, highlightMargin,
                                                  -highlightMargin, -highlightMargin));
            }
        }
    }
}
