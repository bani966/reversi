#pragma once

#include "reversi/position.hpp"

#include <array>
#include <vector>

namespace reversi::naive {

// Plain, obviously-correct reference implementation of Reversi rules, used only to
// differentially test the bitboard engine. Deliberately avoids any bit tricks: legality
// and flips are found by walking the 2D grid one square at a time.
//
// Mirrors Position's convention: cells are relative to the side to move (Own/Opp), not
// absolute colors, so a Board converts directly to/from a Position.
enum class Cell { Empty, Own, Opp };

struct Board {
    std::array<std::array<Cell, 8>, 8> cells{}; // cells[rank][file]
};

Board start();

Board fromPosition(const Position& p);
Position toPosition(const Board& b);

// Square indices (rank * 8 + file) where Own has a legal move.
std::vector<int> legalMoves(const Board& b);
bool hasLegalMove(const Board& b);

// Places Own's disc at `square`, flips all bracketed Opp discs, then swaps Own/Opp so the
// returned board is relative to the next player to move. `square` must be a legal move.
Board applyMove(const Board& b, int square);

// Swaps Own/Opp without changing the board (forced pass).
Board applyPass(const Board& b);

// True when neither side to move nor its opponent has a legal move.
bool isGameOver(const Board& b);

} // namespace reversi::naive
