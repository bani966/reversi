#pragma once

#include "reversi/position.hpp"

#include <QPoint>
#include <QVariantAnimation>
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
        int lastMoveSquare = -1; // most recently played square, or -1 before the first move
        // M10 phase 1: true for a continuous move-by-move update (a live move, undo/redo, or a
        // history-list jump) - BoardWidget flips whichever squares changed color. False for a
        // discontinuous position swap (a new game, or a loaded/imported game) where the on-screen
        // discs bear no meaningful relationship to the new position - render immediately, no
        // flip. Defaults true since most callers are continuous; GameController sets it false
        // only at the two discontinuous sites (see GameController.cpp's newGame/
        // applyLoadedHistory).
        bool animate = true;
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

    // M10 phase 1: piece-flip animation. previousBlackDiscs_/previousWhiteDiscs_ are just enough
    // of the prior DisplayState to compute the flip diff and look up each flipping square's
    // "from" color - no need to keep a whole previous DisplayState around. flippingSquares_ is
    // the diff computed once per setDisplayState() call; flipProgress_ (0 = fully the previous
    // state, 1 = fully the current one) is driven by flipAnimation_, one shared clock for every
    // currently-flipping square since they all started at the same instant - this is also what
    // makes a re-triggered flip (setDisplayState() called again mid-animation) cleanly supersede
    // the in-flight one: stop()+start() always resets progress to 0, never blends two flips.
    reversi::Bitboard previousBlackDiscs_ = 0;
    reversi::Bitboard previousWhiteDiscs_ = 0;
    reversi::Bitboard flippingSquares_ = 0;
    double flipProgress_ = 1.0;
    QVariantAnimation* flipAnimation_;

    void recomputeBoardGeometry();
    int squareAt(const QPoint& localPos) const; // -1 if outside the board
};
