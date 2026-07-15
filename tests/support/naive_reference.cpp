#include "naive_reference.hpp"

namespace reversi::naive {

namespace {

constexpr int kDirections[8][2] = {
    {-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1},
};

bool inBounds(int rank, int file) {
    return rank >= 0 && rank < 8 && file >= 0 && file < 8;
}

// Walks from (rank, file) in direction (dr, df); returns the coordinates of the Own cell
// that brackets a run of one-or-more Opp cells, or nullopt if the direction is not a legal
// bracket (hits empty/edge before an Own cell, or has no Opp cells to flip).
struct Bracket {
    bool valid = false;
    int endRank = 0;
    int endFile = 0;
};

Bracket findBracket(const Board& b, int rank, int file, int dr, int df) {
    int r = rank + dr;
    int f = file + df;
    int opponentCount = 0;
    while (inBounds(r, f) && b.cells[r][f] == Cell::Opp) {
        ++opponentCount;
        r += dr;
        f += df;
    }
    if (opponentCount > 0 && inBounds(r, f) && b.cells[r][f] == Cell::Own) {
        return Bracket{true, r, f};
    }
    return Bracket{};
}

Board swapSides(const Board& b) {
    Board out;
    for (int r = 0; r < 8; ++r) {
        for (int f = 0; f < 8; ++f) {
            const Cell c = b.cells[r][f];
            out.cells[r][f] = c == Cell::Own ? Cell::Opp : c == Cell::Opp ? Cell::Own : Cell::Empty;
        }
    }
    return out;
}

bool isLegalMove(const Board& b, int rank, int file) {
    if (b.cells[rank][file] != Cell::Empty) {
        return false;
    }
    for (const auto& d : kDirections) {
        if (findBracket(b, rank, file, d[0], d[1]).valid) {
            return true;
        }
    }
    return false;
}

} // namespace

Board start() {
    return fromPosition(Position::start());
}

Board fromPosition(const Position& p) {
    Board b;
    for (int sq = 0; sq < kBoardSquares; ++sq) {
        const int rank = sq / 8;
        const int file = sq % 8;
        const Bitboard m = bit(sq);
        b.cells[rank][file] = (p.own & m) != 0   ? Cell::Own
                              : (p.opp & m) != 0 ? Cell::Opp
                                                 : Cell::Empty;
    }
    return b;
}

Position toPosition(const Board& b) {
    Position p;
    for (int sq = 0; sq < kBoardSquares; ++sq) {
        const int rank = sq / 8;
        const int file = sq % 8;
        if (b.cells[rank][file] == Cell::Own) {
            p.own |= bit(sq);
        } else if (b.cells[rank][file] == Cell::Opp) {
            p.opp |= bit(sq);
        }
    }
    return p;
}

std::vector<int> legalMoves(const Board& b) {
    std::vector<int> moves;
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            if (isLegalMove(b, rank, file)) {
                moves.push_back(squareIndex(file, rank));
            }
        }
    }
    return moves;
}

bool hasLegalMove(const Board& b) {
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            if (isLegalMove(b, rank, file)) {
                return true;
            }
        }
    }
    return false;
}

Board applyMove(const Board& b, int square) {
    const int rank = square / 8;
    const int file = square % 8;
    Board next = b;
    next.cells[rank][file] = Cell::Own;
    for (const auto& d : kDirections) {
        const Bracket br = findBracket(b, rank, file, d[0], d[1]);
        if (!br.valid) {
            continue;
        }
        int r = rank + d[0];
        int f = file + d[1];
        while (r != br.endRank || f != br.endFile) {
            next.cells[r][f] = Cell::Own;
            r += d[0];
            f += d[1];
        }
    }
    return swapSides(next);
}

Board applyPass(const Board& b) {
    return swapSides(b);
}

bool isGameOver(const Board& b) {
    return !hasLegalMove(b) && !hasLegalMove(applyPass(b));
}

} // namespace reversi::naive
