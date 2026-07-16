#include "reversi/pattern.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <set>
#include <string>
#include <utility>

namespace reversi::pattern {

namespace {

// (file, rank) coordinate transforms for each of D4's 8 elements. Derived and cross-checked
// by hand before implementing (not copied from an unverified source): Rotate90 composed with
// itself gives exactly Rotate180's formula, and composed a third time gives exactly
// Rotate270's formula, confirming these 8 formulas are mutually consistent as a single group
// rather than 8 independently-guessed transforms.
std::pair<int, int> transformCoords(Symmetry sym, int file, int rank) {
    switch (sym) {
    case Symmetry::Identity:
        return {file, rank};
    case Symmetry::Rotate90:
        return {rank, 7 - file};
    case Symmetry::Rotate180:
        return {7 - file, 7 - rank};
    case Symmetry::Rotate270:
        return {7 - rank, file};
    case Symmetry::ReflectHorizontal:
        return {file, 7 - rank};
    case Symmetry::ReflectVertical:
        return {7 - file, rank};
    case Symmetry::ReflectMainDiag:
        return {rank, file};
    case Symmetry::ReflectAntiDiag:
        return {7 - rank, 7 - file};
    }
    return {file, rank}; // unreachable
}

constexpr std::array<Symmetry, 8> kAllSymmetries = {
    Symmetry::Identity,        Symmetry::Rotate90,          Symmetry::Rotate180,
    Symmetry::Rotate270,       Symmetry::ReflectHorizontal, Symmetry::ReflectVertical,
    Symmetry::ReflectMainDiag, Symmetry::ReflectAntiDiag,
};

std::vector<int> transformInstance(const std::vector<int>& seed, Symmetry sym) {
    std::vector<int> out;
    out.reserve(seed.size());
    for (const int square : seed) {
        const int file = square % 8;
        const int rank = square / 8;
        const auto [nf, nr] = transformCoords(sym, file, rank);
        out.push_back(squareIndex(nf, nr));
    }
    std::sort(out.begin(), out.end()); // ascending-index canonical order, per pattern.hpp
    return out;
}

// Generates a class's full instance set by applying all 8 symmetries to one hand-picked seed
// instance and deduplicating identical results (an instance can be its own image under some
// symmetries - e.g. a corner block under the diagonal reflection through its own corner - so
// the orbit size is not always exactly 8).
PatternClass buildClass(int shapeId, std::string name, const std::vector<int>& seed) {
    std::set<std::vector<int>> uniqueInstances;
    for (const Symmetry sym : kAllSymmetries) {
        uniqueInstances.insert(transformInstance(seed, sym));
    }
    PatternClass cls{shapeId, std::move(name), {}};
    cls.instances.assign(uniqueInstances.begin(), uniqueInstances.end());
    return cls;
}

// One seed square-list per line, chosen as the outermost representative of each edge-distance
// class (rank 0..3, all files) - see pattern.hpp's doc comment for why lines split into 4
// classes (by edge distance) rather than 1.
std::vector<int> lineSeed(int rankFromEdge) {
    std::vector<int> squares;
    for (int file = 0; file < 8; ++file) {
        squares.push_back(squareIndex(file, rankFromEdge));
    }
    return squares;
}

// One seed square-list per diagonal length (3..8), using the main-diagonal-relative (d =
// rank - file) family; d = 8 - length. See pattern.hpp for why length alone determines the
// orbit here (unlike lines).
std::vector<int> diagonalSeed(int length) {
    const int d = 8 - length; // rank = file + d, file ranges 0..(7-d)
    std::vector<int> squares;
    for (int file = 0; file <= 7 - d; ++file) {
        squares.push_back(squareIndex(file, file + d));
    }
    return squares;
}

std::vector<int> edgePlus2xSeed() {
    std::vector<int> squares;
    for (int file = 0; file < 8; ++file) {
        squares.push_back(squareIndex(file, 0)); // the top edge, rank 0
    }
    squares.push_back(squareIndex(1, 1)); // X-square adjacent to the a1 corner
    squares.push_back(squareIndex(6, 1)); // X-square adjacent to the h1 corner
    std::sort(squares.begin(), squares.end());
    return squares;
}

std::vector<int> corner3x3Seed() {
    std::vector<int> squares;
    for (int file = 0; file < 3; ++file) {
        for (int rank = 0; rank < 3; ++rank) {
            squares.push_back(squareIndex(file, rank));
        }
    }
    std::sort(squares.begin(), squares.end());
    return squares;
}

} // namespace

int applySymmetry(Symmetry sym, int square) {
    const auto [nf, nr] = transformCoords(sym, square % 8, square / 8);
    return squareIndex(nf, nr);
}

const std::vector<PatternClass>& allPatternClasses() {
    static const std::vector<PatternClass> kClasses = [] {
        std::vector<PatternClass> classes;
        int shapeId = 0;
        for (int edgeDistance = 0; edgeDistance < 4; ++edgeDistance) {
            classes.push_back(buildClass(shapeId++, "line_edgeDist" + std::to_string(edgeDistance),
                                         lineSeed(edgeDistance)));
        }
        for (int length = 3; length <= 8; ++length) {
            classes.push_back(
                buildClass(shapeId++, "diag" + std::to_string(length), diagonalSeed(length)));
        }
        classes.push_back(buildClass(shapeId++, "edge2x", edgePlus2xSeed()));
        classes.push_back(buildClass(shapeId++, "corner3x3", corner3x3Seed()));
        return classes;
    }();
    return kClasses;
}

int ternaryIndex(const Position& p, const std::vector<int>& instanceSquares) {
    int index = 0;
    int power = 1;
    for (const int square : instanceSquares) {
        const Bitboard b = bit(square);
        const int digit = (p.own & b) != 0 ? 1 : (p.opp & b) != 0 ? 2 : 0;
        index += digit * power;
        power *= 3;
    }
    return index;
}

} // namespace reversi::pattern
