#pragma once

#include <bit>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace reversi {

// Board representation convention, fixed for the whole project:
// bit i corresponds to the square with file = i % 8 (a..h) and rank = i / 8 (1..8),
// so bit 0 = a1 and bit 63 = h8.
using Bitboard = std::uint64_t;

constexpr int kBoardSquares = 64;

constexpr Bitboard bit(int square) {
    return Bitboard{1} << square;
}

constexpr int squareIndex(int file, int rank) {
    return rank * 8 + file;
}

// A position is stored relative to the side to move: `own` holds the mover's discs.
// This is the convention the search operates in; colors are a GUI-level concept.
struct Position {
    Bitboard own = 0;
    Bitboard opp = 0;

    // Standard Othello opening position, black to move (black = own).
    static Position start();

    // Parses a 64-character row-wise board diagram (a1,b1,...,h1,a2,...,h8 - matching this
    // project's own square-index convention exactly, so no reindexing happens here) with
    // black discs as 'X'/'x'/'*', white discs as 'O'/'o'/'0', and empty squares as '-'/'.': a
    // common interchange convention for Othello positions (used by, e.g., the FFO endgame test
    // format - see tests/support/ffo.hpp). Returns nullopt if `board` isn't exactly 64
    // characters. Unrecognized characters are treated as empty rather than rejected, so this
    // stays lenient the same way squareFromString isn't picky about case.
    static std::optional<Position> fromBoardString(std::string_view board, bool blackToMove);

    int ownCount() const { return std::popcount(own); }
    int oppCount() const { return std::popcount(opp); }
    int discCount() const { return ownCount() + oppCount(); }
    Bitboard occupied() const { return own | opp; }
    Bitboard empty() const { return ~occupied(); }
    // The exact endgame solver's search depth: search(pos, emptyCount(), ...) already reaches
    // every leaf via the game-over branch (see solver.hpp), never the heuristic eval fallback.
    int emptyCount() const { return std::popcount(empty()); }
};

constexpr bool operator==(const Position& a, const Position& b) {
    return a.own == b.own && a.opp == b.opp;
}

// Converts a square index to lowercase algebraic notation ("a1".."h8").
std::string squareToString(int square);

// Parses algebraic notation (case-insensitive); returns nullopt on invalid input.
std::optional<int> squareFromString(std::string_view s);

} // namespace reversi
