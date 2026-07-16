#pragma once

#include "reversi/position.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace wthor {

// One game record from a .wtb file, byte layout empirically verified (not just researched)
// against a real downloaded file (WTH_1977.wtb, 12 games, every move replayed legally through
// reversi::applyMove/legalMoves) before this parser was written. Per-record layout: 2 bytes
// tournament id (LE u16), 2 bytes black player id (LE u16), 2 bytes white player id (LE u16),
// 1 byte "real" (actual) score, 1 byte theoretical score, then 60 move bytes (0x00 padding for
// games shorter than 60 plies - confirmed to occur in real data, not just a theoretical case).
struct GameRecord {
    unsigned tournamentId = 0;
    unsigned blackPlayerId = 0;
    unsigned whitePlayerId = 0;
    // The file's own claimed final Black disc count. Empirically confirmed to be Black's
    // ABSOLUTE final count (not mover-relative, not the winner's count) by cross-checking
    // against replayed final positions on real data. NOT used as the training target directly
    // - see moverRelativeFinalScore's doc comment for why.
    int blackFinalDiscsReported = 0;
    int theoreticalScore = 0; // reported as-is; not independently verified by this parser
    // Decoded to this project's own square-index convention (reversi::squareIndex), already
    // reindexed from the file's row/col scheme - see decodeWthorMoveByte. Stops at the first
    // 0x00 padding byte, so shorter-than-60-ply games produce a shorter vector, not one padded
    // with sentinel values.
    std::vector<int> moves;
};

// Decodes one WTHOR move byte. The file's own convention is `10*row + column`, both
// 1-indexed (a1=11, h1=18, a8=81, h8=88) - empirically confirmed against real data (the first
// move byte of a real game decoded to a square that is provably one of Othello's exactly four
// legal opening moves, and every subsequent move across 12 real games replayed legally).
// Returns nullopt for the 0x00 end-of-moves padding sentinel, otherwise a 0-63 square index in
// this project's own convention (reversi::squareIndex(file, rank)). Returns nullopt (not a
// thrown error) for any other out-of-range byte too, so a malformed file fails at the parse
// site rather than deep inside a numeric decode.
std::optional<int> decodeWthorMoveByte(std::uint8_t byte);

// Parses every game record in a .wtb file (skipping the 16-byte file header). Throws
// std::runtime_error if the file can't be opened, or if its size isn't exactly
// `16 + recordCount * 68` bytes (recordCount read from the header's own offset 4-5, little-
// endian u16) - a cheap, strong structural sanity check that would catch a wrong header
// assumption immediately, rather than silently misreading records. Does NOT validate move
// legality - that is replayGame's job, deliberately kept separate so a parse failure and a
// rules-legality failure are two different, distinguishable error classes.
std::vector<GameRecord> parseWtbFile(const std::filesystem::path& path);

// One position reached during a replayed game, immediately before the move that was actually
// played there.
struct ReplayedPosition {
    reversi::Position pos; // mover-relative, exactly as applyMove/applyPass already produce
    bool moverIsBlack;     // which absolute color pos.own refers to at this point
};

struct ReplayedGame {
    // One entry per REAL move actually played (forced passes are auto-applied and do not get
    // their own entry, matching this project's own convention elsewhere - e.g. search.cpp's
    // negamax never treats a pass as a decision worth recording/searching separately).
    std::vector<ReplayedPosition> positions;
    int finalBlackDiscs = 0;
    int finalWhiteDiscs = 0;
};

// Replays a GameRecord's move list from Position::start(), auto-applying a forced pass
// whenever the position's current mover has no legal move (silently, same as the rest of
// engine/ - see search.cpp's negamax for the identical pattern). Throws std::runtime_error if
// any move in the record is illegal at the point it is actually played - this is the real
// rules-engine stress test the M6 plan's step 8 runs at scale across the full WTHOR database;
// here, it is the mechanism, verified already against real data during this parser's
// development (12/12 games replayed with zero illegal moves).
ReplayedGame replayGame(const GameRecord& record);

// The one function responsible for converting the file's Black-absolute score convention into
// this project's mover-relative one - named and isolated specifically because this is the
// exact seam where M5's FFO score-sign bug (Black-relative vs. mover-relative) could resurface
// in a new shape. Deliberately takes the REPLAYED final disc counts (from ReplayedGame), not a
// second, independent parse of GameRecord::blackFinalDiscsReported - there is exactly one
// source of truth for "what actually happened in this game" (the replay), not two that could
// silently disagree with each other.
int moverRelativeFinalScore(bool posMoverIsBlack, int finalBlackDiscs, int finalWhiteDiscs);

} // namespace wthor
