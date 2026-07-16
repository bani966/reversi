#include "reversi/shared_tt.hpp"

#include <bit>

namespace reversi {

namespace {

struct UnpackedData {
    int score;
    int depth;
    Bound bound;
    int bestMove;
};

// Packs (score, depth, bound, bestMove) into one 64-bit word: score in bits 0-31, depth in bits
// 32-47, bound in bits 48-55, bestMove in bits 56-63. Same narrowing-cast idiom
// TranspositionTable's own store() already uses for depth/bestMove (tt.cpp), just made
// explicit and extended to a full manual bit-pack since this word must be a single atomic unit.
constexpr std::uint64_t packData(int score, int depth, Bound bound, int bestMove) {
    std::uint64_t data = 0;
    data |= static_cast<std::uint64_t>(static_cast<std::uint32_t>(score));
    data |= static_cast<std::uint64_t>(static_cast<std::uint16_t>(depth)) << 32U;
    data |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(bound)) << 48U;
    data |=
        static_cast<std::uint64_t>(static_cast<std::uint8_t>(static_cast<std::int8_t>(bestMove)))
        << 56U;
    return data;
}

constexpr UnpackedData unpackData(std::uint64_t data) {
    UnpackedData result{};
    result.score = static_cast<int>(
        static_cast<std::int32_t>(static_cast<std::uint32_t>(data & 0xFFFFFFFFULL)));
    result.depth = static_cast<int>(
        static_cast<std::int16_t>(static_cast<std::uint16_t>((data >> 32U) & 0xFFFFULL)));
    result.bound = static_cast<Bound>(static_cast<std::uint8_t>((data >> 48U) & 0xFFULL));
    result.bestMove = static_cast<int>(
        static_cast<std::int8_t>(static_cast<std::uint8_t>((data >> 56U) & 0xFFULL)));
    return result;
}

// The packed representation of an empty slot: depth=-1 is the same "never stored" sentinel
// TranspositionTable's own TTEntry::depth uses. A position with own=opp=0 legitimately hashes
// to Zobrist key 0 (zobristHash XORs zero keys together for zero discs), so keyXorData==0 alone
// is never a valid emptiness test - only the unpacked depth<0 check is (same discipline
// tt.cpp's own probe() doc comment already documents for the single-threaded table).
constexpr std::uint64_t kEmptyData = packData(0, -1, Bound::Exact, -1);

} // namespace

SharedTranspositionTable::SharedTranspositionTable(std::size_t entryCount)
    : slots_(std::bit_ceil(entryCount == 0 ? std::size_t{1} : entryCount)),
      mask_(slots_.size() - 1) {
    clear();
}

std::optional<TTEntry> SharedTranspositionTable::probe(std::uint64_t key) const {
    const Slot& slot = slots_[key & mask_];
    const std::uint64_t keyXorData = slot.keyXorData.load(std::memory_order_relaxed);
    const std::uint64_t data = slot.data.load(std::memory_order_relaxed);
    if ((keyXorData ^ data) != key) {
        return std::nullopt; // miss or a torn/inconsistent read - see class doc comment
    }
    const UnpackedData unpacked = unpackData(data);
    if (unpacked.depth < 0) {
        return std::nullopt; // the validly-checksummed "empty" sentinel, not a real entry
    }
    hits_.fetch_add(1, std::memory_order_relaxed);
    TTEntry entry;
    entry.key = key;
    entry.score = unpacked.score;
    entry.depth = static_cast<std::int16_t>(unpacked.depth);
    entry.bound = unpacked.bound;
    entry.bestMove = static_cast<std::int8_t>(unpacked.bestMove);
    return entry;
}

void SharedTranspositionTable::store(std::uint64_t key, int depth, int score, Bound bound,
                                     int bestMove) {
    Slot& slot = slots_[key & mask_];
    // Depth-preferred replacement, best-effort under a relaxed race - see the class doc comment
    // for why an occasionally-stale read here is safe (a benign race on a caching heuristic, not
    // a correctness-critical value).
    const std::uint64_t oldKeyXorData = slot.keyXorData.load(std::memory_order_relaxed);
    const std::uint64_t oldData = slot.data.load(std::memory_order_relaxed);
    const UnpackedData oldUnpacked = unpackData(oldData);
    const bool oldIsDifferentKey = (oldKeyXorData ^ oldData) != key;
    if (oldUnpacked.depth >= 0 && oldIsDifferentKey && oldUnpacked.depth > depth) {
        return;
    }

    const std::uint64_t newData = packData(score, depth, bound, bestMove);
    // Order between these two stores doesn't matter for correctness (see class doc comment) -
    // any reader that observes one new word and one old word fails the XOR check and safely
    // treats the slot as a miss, regardless of which word it saw "first".
    slot.data.store(newData, std::memory_order_relaxed);
    slot.keyXorData.store(key ^ newData, std::memory_order_relaxed);
}

void SharedTranspositionTable::debugWriteRawWordsForTesting(std::uint64_t key,
                                                            std::uint64_t keyXorData,
                                                            std::uint64_t data) {
    Slot& slot = slots_[key & mask_];
    slot.keyXorData.store(keyXorData, std::memory_order_relaxed);
    slot.data.store(data, std::memory_order_relaxed);
}

void SharedTranspositionTable::clear() {
    for (Slot& slot : slots_) {
        slot.data.store(kEmptyData, std::memory_order_relaxed);
        slot.keyXorData.store(kEmptyData, std::memory_order_relaxed);
    }
    hits_.store(0, std::memory_order_relaxed);
}

} // namespace reversi
