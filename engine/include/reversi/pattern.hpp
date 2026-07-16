#pragma once

#include "reversi/position.hpp"

#include <string>
#include <vector>

namespace reversi::pattern {

// The 8-element dihedral symmetry group of the square board (D4): identity, the 3 non-trivial
// rotations, and the 4 reflections (2 axis-aligned, 2 diagonal).
enum class Symmetry {
    Identity,
    Rotate90,
    Rotate180,
    Rotate270,
    ReflectHorizontal, // flip rank: (file, 7-rank)
    ReflectVertical,   // flip file: (7-file, rank)
    ReflectMainDiag,   // swap file/rank: (rank, file)
    ReflectAntiDiag,   // (7-rank, 7-file)
};

// Applies one symmetry to a single square index.
int applySymmetry(Symmetry sym, int square);

// The group inverse: applySymmetry(inverse(sym), applySymmetry(sym, square)) == square for
// every square. The 6 reflections and Rotate180 are each their own inverse; Rotate90 and
// Rotate270 are each other's inverse.
Symmetry inverse(Symmetry sym);

// A position together with the symmetry that maps the ORIGINAL position onto it.
struct Canonicalized {
    Position position;
    Symmetry symmetryUsed;
};

// Canonicalizes `p`: applies all 8 symmetries to `p`'s occupied squares and returns whichever
// image sorts lexicographically smallest as an (own, opp) pair, alongside the symmetry that
// produced it.
//
// Directional contract (easy to get backwards - see opening_book.hpp for the concrete
// consequence): if canonicalize(p) == {p', s}, then p' is `s` applied to `p`. A move `m`
// legal in `p` corresponds to move `applySymmetry(s, m)` in `p'`. Conversely, given a stored
// move `m'` that belongs to canonical position `q'` where canonicalize(q) == {q', s_q}, the
// move to actually play in `q` is `applySymmetry(inverse(s_q), m')` - NOT `applySymmetry(s_q,
// m')` again.
Canonicalized canonicalize(const Position& p);

// One symmetry-equivalence class of pattern instances: every instance in `instances` shares
// exactly one weight table of size 3^length (length = instances[i].size(), equal for all i).
struct PatternClass {
    int shapeId;
    std::string name;
    std::vector<std::vector<int>> instances; // each: an ordered square list (ascending index -
                                             // see allPatternClasses' doc comment for why)
};

// Every pattern class used by the WTHOR-trained evaluation. Verified by explicit coordinate-
// transform derivation (not assumed) which concrete pattern instances are actually related by
// board symmetry - this matters because it is easy to get wrong in a way that still "works"
// (extracts *some* index) while silently pooling strategically unrelated squares:
//
// - Lines (rows/columns, length 8): NOT one shared class. Applying the symmetry group to row 1
//   (rank 0) produces the orbit {row1, row8, col-a, col-h} - the 4 OUTER lines - and no
//   sequence of rotations/reflections reaches an inner row like row4. So lines split into 4
//   classes by distance from the nearest edge (0 = outer edge, 1, 2, 3 = the two central
//   lines), 4 instances each. This also matches Othello strategy: edge lines and central lines
//   have very different meaning (edge stability vs. central mobility) and should not share a
//   weight table even if they happened to be symmetry-related, which they are not.
// - Diagonals (length 3-8): here length alone determines the orbit - all diagonals of a given
//   length, in both diagonal directions, ARE related by the symmetry group (verified: a
//   diagonal in one direction rotates into a diagonal in the other direction of the same
//   length). 6 classes (lengths 3,4,5,6,7,8); the length-8 class has 2 instances (the two main
//   diagonals), lengths 3-7 have 4 instances each.
// - Edge+2X (10 squares: an edge's 8 squares plus the 2 X-squares diagonally adjacent to that
//   edge's corners): 1 class, 4 instances (one per edge), confirmed related by rotation.
// - Corner 3x3 block (9 squares): 1 class, 4 instances (one per corner), confirmed related by
//   rotation.
//
// Total: 12 classes, 46 concrete pattern instances (16 lines + 22 diagonals + 4 edge+2X +
// 4 corner blocks).
//
// Canonicalization scope, deliberately simple: within an instance, squares are always listed
// in ascending square-index order - a fixed, mechanical, easy-to-verify convention. This is
// what makes instances of the same class poolable into one weight table (same ordering
// convention applied consistently), but it does NOT additionally fold a line's "forward" and
// "reversed" readings into the same ternary index (a further parameter-reduction technique
// some engines use). That is a valid future refinement, not implemented here - out of scope
// for this milestone's exit criterion (a decisive, not maximally parameter-efficient, win over
// disc-differential).
const std::vector<PatternClass>& allPatternClasses();

// Ternary index for one concrete pattern instance on position `p`: for instanceSquares[i],
// digit = 0 (empty) / 1 (own) / 2 (opp), index = sum(digit_i * 3^i). Order matters and must be
// exactly the order the instance's squares are listed in (ascending index, per
// allPatternClasses' convention) - this function does not re-sort or otherwise canonicalize
// its input, it just reads whatever order it's given.
int ternaryIndex(const Position& p, const std::vector<int>& instanceSquares);

} // namespace reversi::pattern
