#include "reversi/opening_book.hpp"

#include "reversi/pattern.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>

namespace reversi {

namespace {

// Explicit little-endian byte assembly, matching tools/wthor_extractor/book.hpp's writer and
// engine/src/pattern_eval.cpp's identical convention - portable regardless of host endianness.
std::uint32_t readU32LE(std::istream& in) {
    unsigned char b[4];
    in.read(reinterpret_cast<char*>(b), 4);
    if (!in) {
        throw std::runtime_error(
            "unexpected end of opening-book file while reading a 4-byte field");
    }
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8U) |
           (static_cast<std::uint32_t>(b[2]) << 16U) | (static_cast<std::uint32_t>(b[3]) << 24U);
}

std::uint64_t readU64LE(std::istream& in) {
    const std::uint64_t low = readU32LE(in);
    const std::uint64_t high = readU32LE(in);
    return low | (high << 32U);
}

std::int32_t readI32LE(std::istream& in) {
    return static_cast<std::int32_t>(readU32LE(in));
}

constexpr std::streamoff kHeaderSize = 4;
constexpr std::streamoff kEntrySize = 8 + 8 + 4 + 4 + 4; // own, opp, move, gameCount, outcomeSum

} // namespace

OpeningBook::OpeningBook(const std::filesystem::path& bookFile) {
    std::ifstream in(bookFile, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open opening-book file: " + bookFile.string());
    }

    const std::uint32_t entryCount = readU32LE(in);

    // A strong structural check up front, same discipline as parseWtbFile's exact-size
    // validation - catches a truncated or corrupt file immediately rather than partway through
    // the read loop below.
    const std::streamoff expectedSize =
        kHeaderSize + static_cast<std::streamoff>(entryCount) * kEntrySize;
    in.seekg(0, std::ios::end);
    const std::streamoff actualSize = in.tellg();
    if (actualSize != expectedSize) {
        throw std::runtime_error("opening-book file size mismatch: " + bookFile.string());
    }
    in.seekg(kHeaderSize, std::ios::beg);

    entries_.reserve(entryCount);
    for (std::uint32_t i = 0; i < entryCount; ++i) {
        Entry entry;
        entry.own = readU64LE(in);
        entry.opp = readU64LE(in);
        entry.move = readI32LE(in);
        readU32LE(in); // gameCount - informational only, not needed for lookup
        readI32LE(in); // outcomeSum - informational only, not needed for lookup
        entries_.push_back(entry);
    }

    for (std::size_t i = 1; i < entries_.size(); ++i) {
        const Entry& prev = entries_[i - 1];
        const Entry& cur = entries_[i];
        const bool inOrder = prev.own < cur.own || (prev.own == cur.own && prev.opp < cur.opp);
        if (!inOrder) {
            throw std::runtime_error(
                "opening-book file entries are not strictly ascending by (own, opp): " +
                bookFile.string());
        }
    }
}

std::optional<int> OpeningBook::lookup(const Position& p) const {
    const pattern::Canonicalized canonical = pattern::canonicalize(p);
    const auto it = std::lower_bound(entries_.begin(), entries_.end(), canonical.position,
                                     [](const Entry& entry, const Position& target) {
                                         return entry.own < target.own ||
                                                (entry.own == target.own && entry.opp < target.opp);
                                     });
    if (it == entries_.end() || it->own != canonical.position.own ||
        it->opp != canonical.position.opp) {
        return std::nullopt;
    }
    return pattern::applySymmetry(pattern::inverse(canonical.symmetryUsed), it->move);
}

} // namespace reversi
