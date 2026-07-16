#include "wthor_extractor/parser.hpp"

#include "reversi/moves.hpp"

#include <fstream>
#include <stdexcept>

namespace wthor {

namespace {

constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kMovesPerRecord = 60;
constexpr std::size_t kRecordSize = 8 + kMovesPerRecord; // fixed fields + move bytes

unsigned readU16LE(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<unsigned>(bytes[offset]) | (static_cast<unsigned>(bytes[offset + 1]) << 8U);
}

} // namespace

std::optional<int> decodeWthorMoveByte(std::uint8_t byte) {
    if (byte == 0) {
        return std::nullopt;
    }
    const int row = byte / 10; // 1-indexed rank
    const int col = byte % 10; // 1-indexed file
    if (row < 1 || row > 8 || col < 1 || col > 8) {
        return std::nullopt;
    }
    return reversi::squareIndex(col - 1, row - 1);
}

std::vector<GameRecord> parseWtbFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open WTHOR file: " + path.string());
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());
    if (bytes.size() < kHeaderSize) {
        throw std::runtime_error("WTHOR file too small for even a header: " + path.string());
    }

    const unsigned recordCount = readU16LE(bytes, 4);
    const std::size_t expectedSize =
        kHeaderSize + static_cast<std::size_t>(recordCount) * kRecordSize;
    if (bytes.size() != expectedSize) {
        throw std::runtime_error("WTHOR file size mismatch for " + path.string() + ": expected " +
                                 std::to_string(expectedSize) + " bytes (16-byte header + " +
                                 std::to_string(recordCount) + " * 68-byte records), got " +
                                 std::to_string(bytes.size()));
    }

    std::vector<GameRecord> records;
    records.reserve(recordCount);
    for (unsigned i = 0; i < recordCount; ++i) {
        const std::size_t recordStart = kHeaderSize + static_cast<std::size_t>(i) * kRecordSize;
        GameRecord record;
        record.tournamentId = readU16LE(bytes, recordStart);
        record.blackPlayerId = readU16LE(bytes, recordStart + 2);
        record.whitePlayerId = readU16LE(bytes, recordStart + 4);
        record.blackFinalDiscsReported = bytes[recordStart + 6];
        record.theoreticalScore = bytes[recordStart + 7];
        for (std::size_t m = 0; m < kMovesPerRecord; ++m) {
            const std::optional<int> square = decodeWthorMoveByte(bytes[recordStart + 8 + m]);
            if (!square) {
                break; // 0x00 padding (or, defensively, a malformed byte): stop here
            }
            record.moves.push_back(*square);
        }
        records.push_back(std::move(record));
    }
    return records;
}

ReplayedGame replayGame(const GameRecord& record) {
    ReplayedGame result;
    result.positions.reserve(record.moves.size());

    reversi::Position pos = reversi::Position::start();
    bool blackToMove = true;
    for (const int square : record.moves) {
        if (!reversi::hasLegalMove(pos)) {
            pos = reversi::applyPass(pos);
            blackToMove = !blackToMove;
        }
        const reversi::Bitboard legal = reversi::legalMoves(pos);
        if ((legal & reversi::bit(square)) == 0) {
            throw std::runtime_error("illegal move in WTHOR game record: square " +
                                     std::to_string(square) + " (tournament " +
                                     std::to_string(record.tournamentId) + ")");
        }
        result.positions.push_back({pos, blackToMove});
        pos = reversi::applyMove(pos, square);
        blackToMove = !blackToMove;
    }

    result.finalBlackDiscs = blackToMove ? pos.ownCount() : pos.oppCount();
    result.finalWhiteDiscs = blackToMove ? pos.oppCount() : pos.ownCount();
    return result;
}

int moverRelativeFinalScore(bool posMoverIsBlack, int finalBlackDiscs, int finalWhiteDiscs) {
    return posMoverIsBlack ? finalBlackDiscs - finalWhiteDiscs : finalWhiteDiscs - finalBlackDiscs;
}

} // namespace wthor
