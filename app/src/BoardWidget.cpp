#include "BoardWidget.hpp"

#include "Palette.hpp"

#include <QEasingCurve>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
constexpr int kBoardSquares = 8;
// M10 phase 1: within the requested ~120-150ms range - fast enough to stay out of the way of a
// human clicking through moves quickly, slow enough to actually read as a flip rather than a
// flicker.
constexpr int kFlipAnimationDurationMs = 140;
// M10: a small rounding on the board's own outer edge, chess.com-style, kept equal by hand to
// Palette.cpp's panelControlsStyleSheet() container radius (QFrame#sidePanel/QGroupBox) so the
// board and the side panel read as one consistent "container" tier - see that function's own doc
// comment in Palette.hpp for the cross-reference.
constexpr int kCornerRadius = 6;

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
    // Continuous-field Othello board (not a checkerboard of alternating squares - this is
    // Othello, not chess), but tuned to chess.com's actual green board identity: boardColor is
    // chess.com's own dark-square green (#769656), not an invented felt tone. Gridlines darkened
    // to a mossy green-black rather than near-white, since a near-white line read as intended
    // contrast against the old darker/more desaturated green but washes out against this lighter,
    // more saturated one. Off-white/near-black discs (not pure #fff/#000) each with a subtle
    // border for definition, unchanged.
    static const BoardPalette kPalette{
        .windowBackground = chrome::palette().windowBackground,
        .boardColor = QColor(118, 150, 86),
        .gridLineColor = QColor(40, 66, 40, 90),
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

    // One shared clock for every currently-flipping square - see the header's doc comment on
    // flipProgress_ for why a single animation, not one per square, is both correct and what
    // makes re-triggering mid-flight a clean supersede rather than a blend.
    flipAnimation_ = new QVariantAnimation(this);
    flipAnimation_->setDuration(kFlipAnimationDurationMs);
    flipAnimation_->setStartValue(0.0);
    flipAnimation_->setEndValue(1.0);
    flipAnimation_->setEasingCurve(QEasingCurve::InOutQuad);
    connect(flipAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        flipProgress_ = value.toDouble();
        update();
    });
}

void BoardWidget::setDisplayState(const DisplayState& state) {
    // Diffed against state_ (what's still on screen right now) BEFORE it's overwritten below. A
    // square only counts as "flipping" if it held a disc in both the old and new state with a
    // different color - an empty->disc placement (the just-played square itself) isn't a flip,
    // it pops in immediately, matching the real rule that only captured discs flip.
    flippingSquares_ = state.animate ? (state_.blackDiscs & state.whiteDiscs) |
                                           (state_.whiteDiscs & state.blackDiscs)
                                     : reversi::Bitboard{0};
    previousBlackDiscs_ = state_.blackDiscs;
    previousWhiteDiscs_ = state_.whiteDiscs;
    state_ = state;

    if (flippingSquares_ != 0) {
        // Explicit stop() + setCurrentTime(0) + start(), rather than relying on start()'s own
        // (undocumented here) rewind behavior - a flip re-triggered mid-flight (rapid undo/redo
        // clicks, fast back-to-back AI moves at a low configured time budget) must always restart
        // cleanly from progress 0, never resume or blend with a stale in-flight animation. This is
        // the "a new animation always supersedes the old one, no exception" requirement, satisfied
        // structurally. flipProgress_ is also set synchronously so the very next paintEvent (from
        // the update() below) already shows the correct first frame, without waiting for the
        // animation's own timer to tick.
        flipAnimation_->stop();
        flipAnimation_->setCurrentTime(0);
        flipProgress_ = 0.0;
        flipAnimation_->start();
    } else {
        flipAnimation_->stop();
        flipProgress_ = 1.0;
    }
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
    emit boardWidthChanged(boardPixels);
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

    // M10: clip everything drawn inside boardRect (felt, gridlines, discs) to a small rounded
    // rect - the four corner cells get a subtle rounded cut at the very edge without touching any
    // of the per-cell drawing logic below, matching chess.com's own board corners and this app's
    // side-panel radius (kCornerRadius, kept equal by hand - see its own doc comment above).
    QPainterPath boardClipPath;
    boardClipPath.addRoundedRect(boardRect, kCornerRadius, kCornerRadius);
    painter.setClipPath(boardClipPath);

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

            if ((flippingSquares_ & mask) != 0 && flipProgress_ < 1.0) {
                // M10 phase 1: a captured disc mid-flip. Standard 2D "coin flip" trick - the
                // disc's width shrinks to 0 (edge-on) at the animation's halfway point, then
                // grows back; the from/to color swap happens exactly there, when the disc is
                // momentarily invisible, so the swap itself is imperceptible.
                const bool wasBlack = (previousBlackDiscs_ & mask) != 0;
                const bool firstHalf = flipProgress_ < 0.5;
                // firstHalf shows the square's previous color (wasBlack); the second half shows
                // its new color (the opposite one - only genuinely flipped squares reach here).
                const bool showBlack = firstHalf ? wasBlack : !wasBlack;
                const QColor& fill = showBlack ? theme.blackDiscFill : theme.whiteDiscFill;
                const QColor& border = showBlack ? theme.blackDiscBorder : theme.whiteDiscBorder;
                const double widthScale = std::abs(std::cos(std::numbers::pi * flipProgress_));
                const QRect discRect =
                    cell.adjusted(discMargin, discMargin, -discMargin, -discMargin);
                const int squashedWidth =
                    std::max(1, static_cast<int>(discRect.width() * widthScale));
                const QRect flippedRect(discRect.center().x() - squashedWidth / 2, discRect.top(),
                                        squashedWidth, discRect.height());
                painter.setPen(QPen(border, discBorderWidth));
                painter.setBrush(fill);
                painter.drawEllipse(flippedRect);
            } else if ((state_.blackDiscs & mask) != 0) {
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
