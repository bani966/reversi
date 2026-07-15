#include "reversi/moves.hpp"

#include <array>

namespace reversi {

namespace {

constexpr Bitboard kFileA = 0x0101010101010101ULL;
constexpr Bitboard kFileH = 0x8080808080808080ULL;
constexpr Bitboard kNotFileA = ~kFileA;
constexpr Bitboard kNotFileH = ~kFileH;

// bit i = rank*8 + file. North/south shifts only change rank, so they can never wrap a
// file and need no masking. Every shift with an east/west component must mask out the
// file the shift would otherwise wrap onto (e.g. shifting east from file h lands on file a
// of the next rank, which is not adjacent on the board and must be discarded).
constexpr Bitboard shiftNorth(Bitboard b) {
    return b >> 8;
}
constexpr Bitboard shiftSouth(Bitboard b) {
    return b << 8;
}
constexpr Bitboard shiftEast(Bitboard b) {
    return (b << 1) & kNotFileA;
}
constexpr Bitboard shiftWest(Bitboard b) {
    return (b >> 1) & kNotFileH;
}
constexpr Bitboard shiftNorthEast(Bitboard b) {
    return (b >> 7) & kNotFileA;
}
constexpr Bitboard shiftNorthWest(Bitboard b) {
    return (b >> 9) & kNotFileH;
}
constexpr Bitboard shiftSouthEast(Bitboard b) {
    return (b << 9) & kNotFileA;
}
constexpr Bitboard shiftSouthWest(Bitboard b) {
    return (b << 7) & kNotFileH;
}

using ShiftFn = Bitboard (*)(Bitboard);

constexpr std::array<ShiftFn, 8> kDirections = {
    shiftNorth,     shiftSouth,     shiftEast,      shiftWest,
    shiftNorthEast, shiftNorthWest, shiftSouthEast, shiftSouthWest,
};

} // namespace

Bitboard legalMoves(const Position& p) {
    const Bitboard empty = p.empty();
    Bitboard moves = 0;
    for (const ShiftFn shift : kDirections) {
        // Dumb7fill: propagate through opponent discs (at most 6 in a row on an 8-wide
        // board), then land on the empty square one step past the run, if any.
        Bitboard candidates = shift(p.own) & p.opp;
        for (int i = 0; i < 5; ++i) {
            candidates |= shift(candidates) & p.opp;
        }
        moves |= shift(candidates) & empty;
    }
    return moves;
}

bool hasLegalMove(const Position& p) {
    return legalMoves(p) != 0;
}

Position applyMove(const Position& p, int square) {
    const Bitboard placed = bit(square);
    Bitboard flips = 0;
    for (const ShiftFn shift : kDirections) {
        Bitboard candidates = shift(placed) & p.opp;
        for (int i = 0; i < 5; ++i) {
            candidates |= shift(candidates) & p.opp;
        }
        // The run only flips if it is bracketed by an own disc on the far end; a run that
        // instead ends on empty or the board edge is not a capture.
        if ((shift(candidates) & p.own) != 0) {
            flips |= candidates;
        }
    }
    const Bitboard moverOwn = p.own | placed | flips;
    const Bitboard moverOpp = p.opp & ~flips;
    return Position{.own = moverOpp, .opp = moverOwn};
}

Position applyPass(const Position& p) {
    return Position{.own = p.opp, .opp = p.own};
}

bool isGameOver(const Position& p) {
    return !hasLegalMove(p) && !hasLegalMove(applyPass(p));
}

} // namespace reversi
