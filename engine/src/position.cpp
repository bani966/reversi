#include "reversi/position.hpp"

namespace reversi {

namespace {

constexpr int kD4 = squareIndex(3, 3);
constexpr int kE4 = squareIndex(4, 3);
constexpr int kD5 = squareIndex(3, 4);
constexpr int kE5 = squareIndex(4, 4);

} // namespace

Position Position::start() {
    // Standard Othello opening: black on d5 and e4, white on d4 and e5; black moves first.
    Position p;
    p.own = bit(kD5) | bit(kE4);
    p.opp = bit(kD4) | bit(kE5);
    return p;
}

std::optional<Position> Position::fromBoardString(std::string_view board, bool blackToMove) {
    if (board.size() != kBoardSquares) {
        return std::nullopt;
    }
    Bitboard black = 0;
    Bitboard white = 0;
    for (int i = 0; i < kBoardSquares; ++i) {
        const char c = board[static_cast<std::size_t>(i)];
        if (c == 'X' || c == 'x' || c == '*') {
            black |= bit(i);
        } else if (c == 'O' || c == 'o' || c == '0') {
            white |= bit(i);
        }
    }
    return blackToMove ? Position{black, white} : Position{white, black};
}

std::string squareToString(int square) {
    const char file = static_cast<char>('a' + square % 8);
    const char rank = static_cast<char>('1' + square / 8);
    return std::string{file, rank};
}

std::optional<int> squareFromString(std::string_view s) {
    if (s.size() != 2) {
        return std::nullopt;
    }
    char file = s[0];
    if (file >= 'A' && file <= 'H') {
        file = static_cast<char>(file - 'A' + 'a');
    }
    if (file < 'a' || file > 'h' || s[1] < '1' || s[1] > '8') {
        return std::nullopt;
    }
    return squareIndex(file - 'a', s[1] - '1');
}

} // namespace reversi
