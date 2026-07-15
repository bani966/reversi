#include "BoardWidget.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <algorithm>

namespace {
constexpr int kBoardSquares = 8;

// Named color roles, defined once here rather than as literals scattered through paintEvent.
// M9's theme toggle will turn this into a swappable light/dark pair instead of a fixed
// constant; nothing below needs to change except where this struct's values come from.
struct BoardPalette {
    QColor windowBackground; // letterbox color when the widget isn't square
    QColor boardColor;
    QColor gridColor;
    QColor blackDiscColor;
    QColor blackDiscBorderColor;
    QColor whiteDiscColor;
    QColor whiteDiscBorderColor;
    QColor highlightColor;
    QColor labelColor;
};

const BoardPalette& boardPalette() {
    // Apple-minimalist: muted neutral board, generous disc whitespace, high-contrast but not
    // garish discs (near-black/near-white rather than pure #000/#fff, each with a subtle
    // border for definition against the board and against each other).
    static const BoardPalette kPalette{
        .windowBackground = QColor(24, 24, 26),
        .boardColor = QColor(74, 124, 89),
        .gridColor = QColor(60, 102, 73),
        .blackDiscColor = QColor(30, 30, 32),
        .blackDiscBorderColor = QColor(70, 70, 74),
        .whiteDiscColor = QColor(245, 245, 243),
        .whiteDiscBorderColor = QColor(200, 200, 196),
        .highlightColor = QColor(255, 255, 255, 80),
        .labelColor = QColor(222, 234, 224, 175),
    };
    return kPalette;
}
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
    const BoardPalette& theme = boardPalette();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), theme.windowBackground);

    if (cellSize_ <= 0) {
        return;
    }

    const int boardPixels = cellSize_ * kBoardSquares;
    const QRect boardRect(boardOrigin_, QSize(boardPixels, boardPixels));
    painter.fillRect(boardRect, theme.boardColor);

    painter.setPen(theme.gridColor);
    for (int i = 0; i <= kBoardSquares; ++i) {
        const int x = boardOrigin_.x() + i * cellSize_;
        const int y = boardOrigin_.y() + i * cellSize_;
        painter.drawLine(x, boardOrigin_.y(), x, boardOrigin_.y() + boardPixels);
        painter.drawLine(boardOrigin_.x(), y, boardOrigin_.x() + boardPixels, y);
    }

    // Coordinate labels: files a-h inset in the top-left corner of the top row, ranks 1-8
    // inset in the bottom-left corner of the left column - lichess/chess.com's placement,
    // chosen because it's a proven, unobtrusive pattern (the two only ever share a corner in
    // a1, where they land in different corners of that one cell and don't collide). Matches
    // this project's fixed a1-top-left, rank-increases-downward orientation (see
    // reversi/position.hpp) rather than flipping to a rank-1-at-bottom layout.
    QFont labelFont = painter.font();
    labelFont.setPixelSize(std::max(9, cellSize_ / 6));
    painter.setFont(labelFont);
    painter.setPen(theme.labelColor);
    const int labelInset = std::max(2, cellSize_ / 14);
    for (int file = 0; file < kBoardSquares; ++file) {
        const QRect cell(boardOrigin_.x() + file * cellSize_, boardOrigin_.y(), cellSize_,
                         cellSize_);
        const QRect labelRect = cell.adjusted(labelInset, labelInset, -labelInset, -labelInset);
        painter.drawText(labelRect, Qt::AlignTop | Qt::AlignLeft, QString(QChar('a' + file)));
    }
    for (int rank = 0; rank < kBoardSquares; ++rank) {
        const QRect cell(boardOrigin_.x(), boardOrigin_.y() + rank * cellSize_, cellSize_,
                         cellSize_);
        const QRect labelRect = cell.adjusted(labelInset, labelInset, -labelInset, -labelInset);
        painter.drawText(labelRect, Qt::AlignBottom | Qt::AlignLeft, QString::number(rank + 1));
    }

    // Generous whitespace around each disc rather than filling the cell edge-to-edge, plus a
    // subtle border on both colors for definition (a flat fill alone reads as an undecorated
    // circle, not a deliberately designed piece).
    const int discMargin = std::max(2, static_cast<int>(cellSize_ * 0.16));
    const int discBorderWidth = std::max(1, cellSize_ / 40);
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
                painter.setPen(QPen(theme.blackDiscBorderColor, discBorderWidth));
                painter.setBrush(theme.blackDiscColor);
                painter.drawEllipse(
                    cell.adjusted(discMargin, discMargin, -discMargin, -discMargin));
            } else if ((state_.whiteDiscs & mask) != 0) {
                painter.setPen(QPen(theme.whiteDiscBorderColor, discBorderWidth));
                painter.setBrush(theme.whiteDiscColor);
                painter.drawEllipse(
                    cell.adjusted(discMargin, discMargin, -discMargin, -discMargin));
            } else if ((state_.legalMoveHighlights & mask) != 0) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(theme.highlightColor);
                painter.drawEllipse(cell.adjusted(highlightMargin, highlightMargin,
                                                  -highlightMargin, -highlightMargin));
            }
        }
    }
}
