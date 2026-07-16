#include "reversi/mpc.hpp"

#include <bit>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>

namespace reversi {

namespace {

// Explicit little-endian byte assembly, matching tools/mpc_fitter/src/fit.cpp's writer and
// engine/src/pattern_eval.cpp's/opening_book.cpp's identical convention.
std::uint32_t readU32LE(std::istream& in) {
    unsigned char b[4];
    in.read(reinterpret_cast<char*>(b), 4);
    if (!in) {
        throw std::runtime_error("unexpected end of MPC model file while reading a 4-byte field");
    }
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8U) |
           (static_cast<std::uint32_t>(b[2]) << 16U) | (static_cast<std::uint32_t>(b[3]) << 24U);
}

std::int32_t readI32LE(std::istream& in) {
    return static_cast<std::int32_t>(readU32LE(in));
}

float readF32LE(std::istream& in) {
    return std::bit_cast<float>(readU32LE(in));
}

constexpr std::streamoff kHeaderSize = 4;
constexpr std::streamoff kEntrySize = 4 + 4 + 4 + 4 + 4; // deepDepth, shallowDepth, a, b, sigma

} // namespace

MpcModel::MpcModel(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open MPC model file: " + file.string());
    }

    const std::uint32_t pairCount = readU32LE(in);

    // A strong structural check up front, same discipline as OpeningBook/parseWtbFile's exact-
    // size validation - catches a truncated or corrupt file immediately.
    const std::streamoff expectedSize =
        kHeaderSize + static_cast<std::streamoff>(pairCount) * kEntrySize;
    in.seekg(0, std::ios::end);
    const std::streamoff actualSize = in.tellg();
    if (actualSize != expectedSize) {
        throw std::runtime_error("MPC model file size mismatch: " + file.string());
    }
    in.seekg(kHeaderSize, std::ios::beg);

    entries_.reserve(pairCount);
    for (std::uint32_t i = 0; i < pairCount; ++i) {
        Entry entry;
        entry.deepDepth = readI32LE(in);
        entry.shallowDepth = readI32LE(in);
        entry.a = readF32LE(in);
        entry.b = readF32LE(in);
        entry.sigma = readF32LE(in);
        entries_.push_back(entry);
    }

    for (std::size_t i = 0; i < entries_.size(); ++i) {
        for (std::size_t j = i + 1; j < entries_.size(); ++j) {
            if (entries_[i].deepDepth == entries_[j].deepDepth) {
                throw std::runtime_error("MPC model file has two entries for the same deepDepth (" +
                                         std::to_string(entries_[i].deepDepth) +
                                         "): " + file.string());
            }
        }
    }
}

std::optional<MpcModel::Coefficients> MpcModel::lookup(int deepDepth) const {
    for (const Entry& entry : entries_) {
        if (entry.deepDepth == deepDepth) {
            return Coefficients{entry.shallowDepth, entry.a, entry.b, entry.sigma};
        }
    }
    return std::nullopt;
}

} // namespace reversi
