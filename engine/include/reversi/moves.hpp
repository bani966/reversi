#pragma once

#include "reversi/position.hpp"

namespace reversi {

// Mask of squares where the side to move (own) has a legal move.
Bitboard legalMoves(const Position& p);

bool hasLegalMove(const Position& p);

// Places own's disc at `square`, flips all bracketed opp discs, and returns the resulting
// position with own/opp swapped so it is relative to the next player to move.
// Precondition: `square` is a set bit in legalMoves(p).
Position applyMove(const Position& p, int square);

// Swaps own/opp without changing the board (forced pass).
Position applyPass(const Position& p);

// True when neither side to move nor its opponent has a legal move.
bool isGameOver(const Position& p);

} // namespace reversi
