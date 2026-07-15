#include "reversi/tt.hpp"

#include <array>
#include <bit>
#include <random>

namespace reversi {

namespace {

// keys[0][sq] tags an own disc on sq, keys[1][sq] an opp disc. Generated once, lazily, from
// a fixed seed (see tt.hpp for why that is deterministic everywhere).
const std::array<std::array<std::uint64_t, kBoardSquares>, 2>& zobristKeys() {
    static const auto kKeys = [] {
        std::array<std::array<std::uint64_t, kBoardSquares>, 2> keys{};
        std::mt19937_64 rng(20260715);
        for (auto& side : keys) {
            for (std::uint64_t& key : side) {
                key = rng();
            }
        }
        return keys;
    }();
    return kKeys;
}

} // namespace

std::uint64_t zobristHash(const Position& p) {
    const auto& keys = zobristKeys();
    std::uint64_t hash = 0;
    for (Bitboard b = p.own; b != 0; b &= b - 1) {
        hash ^= keys[0][std::countr_zero(b)];
    }
    for (Bitboard b = p.opp; b != 0; b &= b - 1) {
        hash ^= keys[1][std::countr_zero(b)];
    }
    return hash;
}

TranspositionTable::TranspositionTable(std::size_t entryCount) {
    const std::size_t rounded = std::bit_ceil(entryCount == 0 ? std::size_t{1} : entryCount);
    entries_.resize(rounded);
    mask_ = rounded - 1;
}

const TTEntry* TranspositionTable::probe(std::uint64_t key) {
    // depth >= 0 distinguishes a genuinely stored entry from a default-constructed empty slot
    // (a real position could hash to the all-zero key, however unlikely, so key alone is not
    // a valid emptiness test).
    const TTEntry& entry = entries_[key & mask_];
    if (entry.depth < 0 || entry.key != key) {
        return nullptr;
    }
    ++hits_;
    return &entry;
}

void TranspositionTable::store(std::uint64_t key, int depth, int score, Bound bound, int bestMove) {
    TTEntry& slot = entries_[key & mask_];
    // Depth-preferred on index collisions only: a different position's deeper entry survives,
    // but a same-key store always updates (equal-or-deeper results for the same position are
    // at least as fresh and at least as informative).
    if (slot.depth >= 0 && slot.key != key && slot.depth > depth) {
        return;
    }
    slot.key = key;
    slot.score = score;
    slot.depth = static_cast<std::int16_t>(depth);
    slot.bound = bound;
    slot.bestMove = static_cast<std::int8_t>(bestMove);
}

void TranspositionTable::clear() {
    const std::size_t count = entries_.size();
    entries_.assign(count, TTEntry{});
    hits_ = 0;
}

} // namespace reversi
