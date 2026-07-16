#include "wthor_extractor/book.hpp"

#include "reversi/pattern.hpp"

#include <algorithm>
#include <array>

namespace wthor {

void accumulateBookGame(const ReplayedGame& game, int maxPly, BookAccumulator& acc) {
    for (const ReplayedPosition& rp : game.positions) {
        const int ply = rp.pos.discCount() - 4;
        if (ply > maxPly) {
            break; // positions arrive in increasing ply order, so nothing later qualifies either
        }
        const reversi::pattern::Canonicalized canonical = reversi::pattern::canonicalize(rp.pos);
        const int canonicalMove = reversi::pattern::applySymmetry(canonical.symmetryUsed, rp.move);
        const int outcome =
            moverRelativeFinalScore(rp.moverIsBlack, game.finalBlackDiscs, game.finalWhiteDiscs);

        BookMoveStats& stats = acc[{canonical.position.own, canonical.position.opp}][canonicalMove];
        ++stats.gameCount;
        stats.outcomeSum += outcome;
    }
}

std::vector<BookEntry> finalizeBook(const BookAccumulator& acc, unsigned minCount) {
    std::vector<BookEntry> entries;
    for (const auto& [positionKey, moveStats] : acc) {
        unsigned totalCount = 0;
        for (const auto& [move, stats] : moveStats) {
            totalCount += stats.gameCount;
        }
        if (totalCount < minCount) {
            continue;
        }

        const auto best =
            std::max_element(moveStats.begin(), moveStats.end(), [](const auto& a, const auto& b) {
                if (a.second.gameCount != b.second.gameCount) {
                    return a.second.gameCount < b.second.gameCount;
                }
                const double avgA = static_cast<double>(a.second.outcomeSum) /
                                    static_cast<double>(a.second.gameCount);
                const double avgB = static_cast<double>(b.second.outcomeSum) /
                                    static_cast<double>(b.second.gameCount);
                return avgA < avgB;
            });

        BookEntry entry;
        entry.own = positionKey.first;
        entry.opp = positionKey.second;
        entry.move = best->first;
        entry.gameCount = best->second.gameCount;
        entry.outcomeSum = best->second.outcomeSum;
        entries.push_back(entry);
    }
    return entries;
}

namespace {

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

} // namespace

void writeBookFile(const std::vector<BookEntry>& entries, std::ostream& out) {
    writeU32LE(out, static_cast<std::uint32_t>(entries.size()));
    for (const BookEntry& entry : entries) {
        writeU64LE(out, entry.own);
        writeU64LE(out, entry.opp);
        writeI32LE(out, entry.move);
        writeU32LE(out, entry.gameCount);
        writeI32LE(out, entry.outcomeSum);
    }
}

} // namespace wthor
