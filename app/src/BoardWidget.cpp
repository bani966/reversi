#include "BoardWidget.hpp"

#include "Palette.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <algorithm>

namespace {
constexpr int kBoardSquares = 8;

// Named color roles, defined once here rather than as literals scattered through paintEvent.
// windowBackground/coordinateTextColor/lastMoveHighlightColor come from the shared
// chrome::palette() (also used by MainWindow's menu/status/title bar) so they can't drift out
// of sync; the rest are board-specific (felt green, disc fills, grid lines) and have no other
// consumer, so they stay local. M9's theme toggle will turn this into a swappable light/dark
// pair instead of a fixed constant; nothing below needs to change except where these values
// come from.
struct BoardPalette {
    QColor windowBackground; // letterbox color when the widget isn't square
    QColor boardColor;
    QColor gridLineColor;
    QColor coordinateTextColor;
    QColor blackDiscFill;
    QColor blackDiscBorder;
    QColor whiteDiscFill;
    QColor whiteDiscBorder;
    QColor legalMoveHighlightColor;
    QColor lastMoveHighlightColor;
};

const BoardPalette& boardPalette() {
    // Traditional solid-green Othello felt table, chess.com-style panel typography and
    // coordinate placement: a richer, slightly desaturated felt green (not neon), thin light
    // gridlines on top of one continuous field, off-white/near-black discs (not pure #fff/
    // #000) each with a subtle border for definition.
    static const BoardPalette kPalette{
        .windowBackground = chrome::palette().windowBackground,
        .boardColor = QColor(66, 116, 72),
        .gridLineColor = QColor(235, 238, 230, 50),
        .coordinateTextColor = chrome::palette().textColor,
        .blackDiscFill = QColor(26, 26, 28),
        .blackDiscBorder = QColor(58, 58, 62),
        .whiteDiscFill = QColor(242, 236, 224),
        .whiteDiscBorder = QColor(178, 168, 150),
        .legalMoveHighlightColor = QColor(255, 255, 255, 70),
        .lastMoveHighlightColor = chrome::palette().lastMoveHighlightColor,
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
    // One continuous felt-green field, with thin light gridlines drawn on top - not a
    // checkerboard of alternating squares (this is Othello, not chess).
    painter.fillRect(boardRect, theme.boardColor);

    painter.setPen(QPen(theme.gridLineColor, 1));
    for (int i = 0; i <= kBoardSquares; ++i) {
        const int x = boardOrigin_.x() + i * cellSize_;
        const int y = boardOrigin_.y() + i * cellSize_;
        painter.drawLine(x, boardOrigin_.y(), x, boardOrigin_.y() + boardPixels);
        painter.drawLine(boardOrigin_.x(), y, boardOrigin_.x() + boardPixels, y);
    }

    // Coordinate labels: chess.com's placement - files a-h inset in the bottom-right corner of
    // the bottom row, ranks 1-8 inset in the top-left corner of the left column. The two only
    // ever share one cell (the bottom-left corner of the board), where they land in different
    // corners of that cell and don't collide. This project's orientation is fixed (a1
    // top-left, rank increases downward - see reversi/position.hpp), so "bottom row" here is
    // the last row regardless of which rank number is printed on it, matching where a
    // chess.com-style board prints its own bottom edge.
    QFont labelFont(QStringLiteral("Segoe UI")); // Qt substitutes a system sans-serif if absent
    labelFont.setPixelSize(std::max(9, cellSize_ / 6));
    labelFont.setWeight(QFont::Bold); // demibold wasn't a clear enough jump from regular weight
    painter.setFont(labelFont);
    painter.setPen(theme.coordinateTextColor);
    const int labelInset = std::max(2, cellSize_ / 14);
    const int bottomRow = kBoardSquares - 1;
    for (int file = 0; file < kBoardSquares; ++file) {
        const QRect cell(boardOrigin_.x() + file * cellSize_,
                         boardOrigin_.y() + bottomRow * cellSize_, cellSize_, cellSize_);
        const QRect labelRect = cell.adjusted(labelInset, labelInset, -labelInset, -labelInset);
        painter.drawText(labelRect, Qt::AlignBottom | Qt::AlignRight, QString(QChar('a' + file)));
    }
    for (int rank = 0; rank < kBoardSquares; ++rank) {
        const QRect cell(boardOrigin_.x(), boardOrigin_.y() + rank * cellSize_, cellSize_,
                         cellSize_);
        const QRect labelRect = cell.adjusted(labelInset, labelInset, -labelInset, -labelInset);
        painter.drawText(labelRect, Qt::AlignTop | Qt::AlignLeft, QString::number(rank + 1));
    }

    // Consistent padding so discs never touch gridlines, plus a deliberate border (not a flat
    // undecorated circle): white gets a border a shade darker than its fill for definition
    // against the green; black is already darkest, so its border is a shade lighter instead -
    // otherwise a "darker than near-black" border would be indistinguishable from the fill.
    const int discMargin = std::max(2, static_cast<int>(cellSize_ * 0.16));
    const int discBorderWidth = std::clamp(cellSize_ / 35, 2, 3) + 1;
    const int highlightMargin = cellSize_ * 3 / 8;
    // Diagram orientation matches the CLI's: a1 top-left, rank increases downward, file
    // increases rightward.
    for (int rank = 0; rank < kBoardSquares; ++rank) {
        for (int file = 0; file < kBoardSquares; ++file) {
            const int square = reversi::squareIndex(file, rank);
            const reversi::Bitboard mask = reversi::bit(square);
            const QRect cell(boardOrigin_.x() + file * cellSize_,
                             boardOrigin_.y() + rank * cellSize_, cellSize_, cellSize_);

            // Drawn before the disc/highlight below so it sits underneath as a tint on the
            // square, not a shape competing with the disc for attention.
            if (square == state_.lastMoveSquare) {
                painter.fillRect(cell, theme.lastMoveHighlightColor);
            }

            if ((state_.blackDiscs & mask) != 0) {
                painter.setPen(QPen(theme.blackDiscBorder, discBorderWidth));
                painter.setBrush(theme.blackDiscFill);
                painter.drawEllipse(
                    cell.adjusted(discMargin, discMargin, -discMargin, -discMargin));
            } else if ((state_.whiteDiscs & mask) != 0) {
                painter.setPen(QPen(theme.whiteDiscBorder, discBorderWidth));
                painter.setBrush(theme.whiteDiscFill);
                painter.drawEllipse(
                    cell.adjusted(discMargin, discMargin, -discMargin, -discMargin));
            } else if ((state_.legalMoveHighlights & mask) != 0) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(theme.legalMoveHighlightColor);
                painter.drawEllipse(cell.adjusted(highlightMargin, highlightMargin,
                                                  -highlightMargin, -highlightMargin));
            }
        }
    }
}
