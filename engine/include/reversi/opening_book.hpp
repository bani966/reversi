#pragma once

#include "reversi/position.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace reversi {

// Looks up a precomputed best move for a position, from a book built by
// `tools/wthor_extractor build-book` (see that tool's book.hpp for how entries are chosen).
// Mirrors PatternEvaluator's shape exactly: load a file at construction, expose a lookup
// method - this is the opening book's "toggle": callers hold an OpeningBook only when they want
// book moves enabled, and pass nullptr (or skip the lookup entirely) to disable it. Never the
// other way around (no boolean flag threaded through search.hpp - see move_selector.hpp).
//
// On-disk format (little-endian, dependency-free, matching this project's weight-file
// convention), produced by tools/wthor_extractor/book.hpp's writeBookFile - documented here too
// since this is the format's other, independent half:
//   u32 entryCount
//   for each entry, ascending by (own, opp):
//       u64 own
//       u64 opp
//       i32 move
//       u32 gameCount   // informational only, not used by lookup()
//       i32 outcomeSum  // informational only, not used by lookup()
class OpeningBook {
public:
    // Throws std::runtime_error if `bookFile` cannot be opened, its size doesn't match
    // entryCount * 28 + 4 bytes exactly, or entries are not ascending by (own, opp) (lookup()
    // binary-searches assuming this order - a build regression that broke it must fail loudly
    // here, not silently return wrong moves).
    explicit OpeningBook(const std::filesystem::path& bookFile);

    // Looks up the recommended move for `p`. Canonicalizes `p` (reversi::pattern::canonicalize)
    // and binary-searches for a matching entry; on a hit, recovers the move to actually play in
    // `p` by applying the INVERSE of the symmetry canonicalize() used - not the same symmetry
    // again (see pattern.hpp's canonicalize() doc comment for the directional contract this
    // relies on). Returns nullopt on a miss.
    std::optional<int> lookup(const Position& p) const;

private:
    struct Entry {
        Bitboard own = 0;
        Bitboard opp = 0;
        int move = 0;
    };
    std::vector<Entry> entries_; // sorted ascending by (own, opp), per the file format
};

} // namespace reversi
