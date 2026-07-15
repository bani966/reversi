#pragma once

#include "reversi/position.hpp"

#include <QPoint>
#include <QWidget>

// Pure renderer + click detector for the 8x8 board. Knows nothing about game rules — it just
// draws whatever DisplayState it's given and reports which square was clicked; GameController
// decides whether a click is legal and what state to show next.
class BoardWidget : public QWidget {
    Q_OBJECT

public:
    struct DisplayState {
        reversi::Bitboard blackDiscs = 0;
        reversi::Bitboard whiteDiscs = 0;
        reversi::Bitboard legalMoveHighlights = 0; // empty when it isn't a human's turn to click
    };

    explicit BoardWidget(QWidget* parent = nullptr);

    void setDisplayState(const DisplayState& state);

    QSize sizeHint() const override;

signals:
    void squareClicked(int square);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    DisplayState state_;
    // Recomputed on resize: side length of one cell in pixels, and the top-left pixel of the
    // board, so an 8x8 square board stays centered and undistorted in a non-square widget.
    int cellSize_ = 0;
    QPoint boardOrigin_;

    void recomputeBoardGeometry();
    int squareAt(const QPoint& localPos) const; // -1 if outside the board
};
