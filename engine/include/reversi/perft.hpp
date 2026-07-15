#pragma once

#include "reversi/position.hpp"

#include <cstdint>

namespace reversi {

// Counts leaf positions reachable from `p` in exactly `depth` plies.
//
// Convention: a forced pass consumes one ply of depth, same as a normal move. Verified
// against https://aartbik.blogspot.com/2009/02/perft-for-reversi.html ("count 'pass' nodes
// to get the numbers"), which matches OEIS A052586. A position where neither side can move
// is terminal and counts as exactly one leaf regardless of remaining depth, since no
// further plies (moves or passes) are possible from it.
std::uint64_t perft(const Position& p, int depth);

} // namespace reversi
