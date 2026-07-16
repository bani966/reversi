#include "reversi/opening_book.hpp"

#include "reversi/pattern.hpp"
#include "reversi/position.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <vector>

namespace reversi {
namespace {

int sq(const char* algebraic) {
    return *squareFromString(algebraic);
}

std::filesystem::path devBookPath() {
    return std::filesystem::path(REVERSI_TEST_DATA_DIR) / "dev_opening_book.bin";
}

// A local, independent binary writer - deliberately NOT calling tools/wthor_extractor's
// writeBookFile (engine/ tests must not depend on tools/, which is opt-in via
// REVERSI_BUILD_TOOLS - see CLAUDE.md's layering rule). Also serves the same purpose as
// pattern_eval_test.cpp's independent byte-level recomputation: OpeningBook (the production
// reader) is tested against hand-assembled bytes, not just against its own writer's output.
namespace testbook {

struct RawEntry {
    Bitboard own;
    Bitboard opp;
    int move;
    unsigned gameCount = 1;
    int outcomeSum = 0;
};

void writeU32LE(std::ostream& out, std::uint32_t value) {
    const std::array<char, 4> bytes = {
        static_cast<char>(value & 0xFFU), static_cast<char>((value >> 8U) & 0xFFU),
        static_cast<char>((value >> 16U) & 0xFFU), static_cast<char>((value >> 24U) & 0xFFU)};
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeU64LE(std::ostream& out, std::uint64_t value) {
    writeU32LE(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
    writeU32LE(out, static_cast<std::uint32_t>((value >> 32U) & 0xFFFFFFFFULL));
}

void writeI32LE(std::ostream& out, int value) {
    writeU32LE(out, static_cast<std::uint32_t>(value));
}

std::filesystem::path writeFile(const std::vector<RawEntry>& entries, const char* name) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path, std::ios::binary);
    writeU32LE(out, static_cast<std::uint32_t>(entries.size()));
    for (const RawEntry& e : entries) {
        writeU64LE(out, e.own);
        writeU64LE(out, e.opp);
        writeI32LE(out, e.move);
        writeU32LE(out, e.gameCount);
        writeI32LE(out, e.outcomeSum);
    }
    return path;
}

} // namespace testbook

TEST(OpeningBook, LoadsTheDevBookFileWithoutThrowing) {
    EXPECT_NO_THROW({ OpeningBook book(devBookPath()); });
}

TEST(OpeningBook, ThrowsOnMissingFile) {
    EXPECT_THROW(OpeningBook(std::filesystem::path("does_not_exist_98765.bin")),
                 std::runtime_error);
}

TEST(OpeningBook, ThrowsOnSizeMismatch) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "opening_book_test_truncated.bin";
    {
        std::ofstream out(path, std::ios::binary);
        testbook::writeU32LE(out, 3); // claims 3 entries but writes zero
    }
    EXPECT_THROW(OpeningBook{path}, std::runtime_error);
    std::filesystem::remove(path);
}

TEST(OpeningBook, ThrowsWhenEntriesAreNotAscendingByOwnThenOpp) {
    const std::vector<testbook::RawEntry> entries = {
        {/*own=*/5, /*opp=*/0, /*move=*/sq("a1")},
        {/*own=*/1, /*opp=*/0, /*move=*/sq("b1")}, // out of order: own decreases
    };
    const std::filesystem::path path =
        testbook::writeFile(entries, "opening_book_test_unsorted.bin");
    EXPECT_THROW(OpeningBook{path}, std::runtime_error);
    std::filesystem::remove(path);
}

TEST(OpeningBook, LookupReturnsNulloptOnMiss) {
    const std::vector<testbook::RawEntry> entries = {
        {/*own=*/reversi::bit(sq("c3")), /*opp=*/0, /*move=*/sq("a1")},
    };
    const std::filesystem::path path = testbook::writeFile(entries, "opening_book_test_miss.bin");
    const OpeningBook book(path);

    Position query; // own = opp = 0, not in the book and not related by symmetry to the entry
    EXPECT_EQ(book.lookup(query), std::nullopt);
    std::filesystem::remove(path);
}

// Identity case: querying the EXACT canonical position stored in the book must return the
// stored move unchanged (canonicalize(p) for an already-canonical p uses Symmetry::Identity,
// whose inverse is itself).
TEST(OpeningBook, LookupReturnsTheStoredMoveForAnAlreadyCanonicalPosition) {
    Position canonicalPos;
    canonicalPos.own = reversi::bit(sq("a1"));
    ASSERT_EQ(pattern::canonicalize(canonicalPos).symmetryUsed, pattern::Symmetry::Identity);

    const std::vector<testbook::RawEntry> entries = {
        {canonicalPos.own, canonicalPos.opp, /*move=*/sq("a2")},
    };
    const std::filesystem::path path =
        testbook::writeFile(entries, "opening_book_test_identity.bin");
    const OpeningBook book(path);

    const std::optional<int> result = book.lookup(canonicalPos);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, sq("a2"));
    std::filesystem::remove(path);
}

// The canonicalization-aware lookup test the M6 plan specifically calls for: a query position
// related to a stored entry by a NON-self-inverse symmetry (Rotate90/Rotate270), verified with
// the exact same hand-derived example as pattern_test.cpp's Canonicalize tests and
// book_test.cpp's AccumulateBookGame test - one consistent worked example used across all three
// layers (canonicalize, book building, book lookup).
//
// Book entry: canonical position own=bit(a1) (this is h1's canonical image via Rotate90) has
// stored move a2 (this is g1's canonical image via that same Rotate90). Querying with the
// REAL position (own=bit(h1)) must recover g1 - applying inverse(Rotate90)=Rotate270 to a2 -
// NOT b8, which is what applying Rotate90 to a2 a second time (the backwards, buggy direction)
// would incorrectly produce.
TEST(OpeningBook, LookupAppliesTheInverseSymmetryNotTheSameSymmetryAgain) {
    const std::vector<testbook::RawEntry> entries = {
        {/*own=*/reversi::bit(sq("a1")), /*opp=*/0, /*move=*/sq("a2")},
    };
    const std::filesystem::path path =
        testbook::writeFile(entries, "opening_book_test_inverse_symmetry.bin");
    const OpeningBook book(path);

    Position query;
    query.own = reversi::bit(sq("h1"));
    ASSERT_EQ(pattern::canonicalize(query).symmetryUsed, pattern::Symmetry::Rotate90);

    const std::optional<int> result = book.lookup(query);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, sq("g1"));
    EXPECT_NE(*result, sq("b8")) << "b8 is what the WRONG (repeat-the-same-symmetry) direction "
                                    "would incorrectly produce";
    std::filesystem::remove(path);
}

} // namespace
} // namespace reversi
