#include "reversi/mpc.hpp"
#include "reversi/search.hpp"

#include "../support/benchmark_positions.hpp"
#include "../support/search_checks.hpp"
#include "reversi/moves.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <vector>

namespace reversi {
namespace {

// A local, independent binary writer for synthetic MpcModel fixtures - same discipline as
// tests/engine/mpc_test.cpp's own testmodel namespace (kept separate per-file rather than
// shared, matching this project's established repeated-per-file pattern for these small
// writers).
namespace testmodel {

struct RawEntry {
    int deepDepth;
    int shallowDepth;
    float a;
    float b;
    float sigma;
};

void writeU32LE(std::ostream& out, std::uint32_t value) {
    const std::array<char, 4> bytes = {
        static_cast<char>(value & 0xFFU), static_cast<char>((value >> 8U) & 0xFFU),
        static_cast<char>((value >> 16U) & 0xFFU), static_cast<char>((value >> 24U) & 0xFFU)};
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeI32LE(std::ostream& out, int value) {
    writeU32LE(out, static_cast<std::uint32_t>(value));
}

void writeF32LE(std::ostream& out, float value) {
    writeU32LE(out, std::bit_cast<std::uint32_t>(value));
}

std::filesystem::path writeFile(const std::vector<RawEntry>& entries, const char* name) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path, std::ios::binary);
    writeU32LE(out, static_cast<std::uint32_t>(entries.size()));
    for (const RawEntry& e : entries) {
        writeI32LE(out, e.deepDepth);
        writeI32LE(out, e.shallowDepth);
        writeF32LE(out, e.a);
        writeF32LE(out, e.b);
        writeF32LE(out, e.sigma);
    }
    return path;
}

} // namespace testmodel

// The load-bearing anchor: `mpc` defaults to nullptr on every existing call site, and the
// integration is structurally skipped whenever it's null - this is the exact same "toggle =
// null pointer, structurally identical code path when off" pattern already proven for
// TranspositionTable*/OpeningBook*/CancellationToken* (see tt_test.cpp's own on/off equivalence
// test, which this mirrors). Explicitly passing mpc=nullptr must be indistinguishable from not
// passing it at all - score AND node count identical, not just score (a real MPC engagement
// would cost extra nodes even where it never cuts - see the never-triggers test below).
TEST(MpcSearch, ExplicitNullptrMpcMatchesTheDefaultOnBenchmarkSet) {
    for (const Position& pos : bench::benchmarkPositions()) {
        for (int depth = 1; depth <= 5; ++depth) {
            const SearchResult withoutArg = search(pos, depth);
            const SearchResult explicitNull =
                search(pos, depth, evaluateDiscDifferential, nullptr, nullptr, nullptr);
            EXPECT_EQ(explicitNull.score, withoutArg.score) << "depth " << depth;
            EXPECT_EQ(explicitNull.nodes, withoutArg.nodes) << "depth " << depth;
        }
    }
}

// A model covering realistic depths with an ASTRONOMICALLY large sigma can never clear any
// real alpha-beta window (disc differentials live in [-64, 64], so even a huge `t` times a huge
// sigma dwarfs any window), so it must never actually cut - but it's still ENGAGED (a real
// MpcConfig with a non-null model), meaning every eligible node pays for a shallow probe. This
// proves two things at once: (1) engaging-but-never-triggering MPC cannot corrupt the score,
// and (2) it is NOT free - node counts are expected to be >= MPC-off, documented explicitly so
// nobody mistakes "large t" for a genuine off-switch (only mpcModel == nullptr truly is).
TEST(MpcSearch, ModelThatNeverTriggersMatchesScoreButCostsAtLeastAsManyNodes) {
    std::vector<testmodel::RawEntry> entries;
    for (int deep = 2; deep <= 7; ++deep) {
        entries.push_back({deep, deep - 2, 0.0F, 1.0F, 1e9F});
    }
    const std::filesystem::path path = testmodel::writeFile(entries, "mpc_search_test_never.bin");
    const MpcModel model(path);
    const MpcConfig config{&model, /*t=*/2.0};

    for (const Position& pos : bench::benchmarkPositions()) {
        for (const int depth : {3, 5, 6}) {
            const SearchResult without = search(pos, depth);
            const SearchResult with =
                search(pos, depth, evaluateDiscDifferential, nullptr, nullptr, &config);
            EXPECT_EQ(with.score, without.score) << "depth " << depth;
            EXPECT_EQ(checks::rootMoveValue(pos, with.bestMove, depth), with.score)
                << "depth " << depth;
            EXPECT_GE(with.nodes, without.nodes) << "depth " << depth;
        }
    }
    std::filesystem::remove(path);
}

// A model with a huge intercept and tiny sigma clears any real window immediately, so it MUST
// cut, drastically shrinking the node count and proving the cut path is actually reachable and
// doesn't crash/hang - not just that it stays inert when it shouldn't fire. shallowDepth =
// deep - 2 for every configured pair (deep 2..7), so a chain of recursive shallow probes always
// bottoms out at an uncovered depth (1) and terminates - no infinite regress.
TEST(MpcSearch, ModelThatAlwaysTriggersDrasticallyReducesNodesAndStillReturnsALegalMove) {
    std::vector<testmodel::RawEntry> entries;
    for (int deep = 2; deep <= 7; ++deep) {
        entries.push_back({deep, deep - 2, 1000.0F, 0.0F, 0.001F});
    }
    const std::filesystem::path path = testmodel::writeFile(entries, "mpc_search_test_always.bin");
    const MpcModel model(path);
    const MpcConfig config{&model, /*t=*/2.0};

    std::uint64_t nodesWithout = 0;
    std::uint64_t nodesWith = 0;
    for (const Position& pos : bench::benchmarkPositions()) {
        constexpr int kDepth = 7;
        const SearchResult without = search(pos, kDepth);
        const SearchResult with =
            search(pos, kDepth, evaluateDiscDifferential, nullptr, nullptr, &config);
        nodesWithout += without.nodes;
        nodesWith += with.nodes;
        EXPECT_TRUE(with.completed);
        EXPECT_NE(with.bestMove, -1);
        EXPECT_NE((legalMoves(pos) & bit(with.bestMove)), Bitboard{0})
            << "MPC-on search must still return a legal move";
    }
    EXPECT_LT(nodesWith, nodesWithout);
    std::filesystem::remove(path);
}

} // namespace
} // namespace reversi
