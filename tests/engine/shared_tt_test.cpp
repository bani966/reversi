#include "reversi/shared_tt.hpp"

#include "../support/benchmark_positions.hpp"

#include <atomic>
#include <cstdint>
#include <gtest/gtest.h>
#include <random>
#include <thread>
#include <vector>

namespace reversi {
namespace {

// An independent re-implementation of the packing scheme shared_tt.cpp's own (anonymous-
// namespace, private) packData() uses - deliberately duplicated here rather than exposed from
// production code, so the torn-read test below constructs its "two different stores'" raw words
// from first principles instead of trusting shared_tt.cpp's own packing logic to also be the
// thing verifying it.
namespace independent {

std::uint64_t packData(int score, int depth, Bound bound, int bestMove) {
    std::uint64_t data = 0;
    data |= static_cast<std::uint64_t>(static_cast<std::uint32_t>(score));
    data |= static_cast<std::uint64_t>(static_cast<std::uint16_t>(depth)) << 32U;
    data |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(bound)) << 48U;
    data |=
        static_cast<std::uint64_t>(static_cast<std::uint8_t>(static_cast<std::int8_t>(bestMove)))
        << 56U;
    return data;
}

} // namespace independent

TEST(SharedTranspositionTable, ProbeMissesOnEmptyTableAndHitsAfterStore) {
    SharedTranspositionTable tt(1024);
    constexpr std::uint64_t key = 42;
    EXPECT_EQ(tt.probe(key), std::nullopt);

    tt.store(key, 5, 17, Bound::Lower, 42);
    const std::optional<TTEntry> entry = tt.probe(key);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->key, key);
    EXPECT_EQ(entry->score, 17);
    EXPECT_EQ(entry->depth, 5);
    EXPECT_EQ(entry->bound, Bound::Lower);
    EXPECT_EQ(entry->bestMove, 42);
    EXPECT_EQ(tt.hits(), std::uint64_t{1});
}

TEST(SharedTranspositionTable, RoundTripsNegativeScoresAndSentinelBestMove) {
    SharedTranspositionTable tt(1024);
    constexpr std::uint64_t key = 7;
    tt.store(key, 12, -63, Bound::Upper, -1);
    const std::optional<TTEntry> entry = tt.probe(key);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->score, -63);
    EXPECT_EQ(entry->depth, 12);
    EXPECT_EQ(entry->bound, Bound::Upper);
    EXPECT_EQ(entry->bestMove, -1);
}

// Same replacement policy as TranspositionTable, verified the same way tt_test.cpp verifies it:
// capacity 1 forces every key into the same slot, making replacement fully deterministic.
TEST(SharedTranspositionTable, IndexCollisionKeepsTheDeeperEntry) {
    SharedTranspositionTable tt(1);
    EXPECT_EQ(tt.capacity(), std::size_t{1});
    tt.store(111, 8, 5, Bound::Exact, 10);
    tt.store(222, 3, -2, Bound::Upper, 20); // shallower, different key: rejected
    ASSERT_TRUE(tt.probe(111).has_value());
    EXPECT_EQ(tt.probe(222), std::nullopt);
    tt.store(222, 9, -2, Bound::Upper, 20); // deeper, different key: evicts
    EXPECT_EQ(tt.probe(111), std::nullopt);
    ASSERT_TRUE(tt.probe(222).has_value());
}

TEST(SharedTranspositionTable, SameKeyStoreAlwaysUpdates) {
    SharedTranspositionTable tt(1024);
    tt.store(333, 7, 4, Bound::Exact, 11);
    tt.store(333, 7, 6, Bound::Exact, 12);
    const std::optional<TTEntry> entry = tt.probe(333);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->score, 6);
    EXPECT_EQ(entry->bestMove, 12);
}

TEST(SharedTranspositionTable, ClearEmptiesEverySlot) {
    SharedTranspositionTable tt(64);
    tt.store(1, 3, 1, Bound::Exact, 1);
    tt.store(2, 3, 2, Bound::Exact, 2);
    tt.clear();
    EXPECT_EQ(tt.probe(1), std::nullopt);
    EXPECT_EQ(tt.probe(2), std::nullopt);
    EXPECT_EQ(tt.hits(), std::uint64_t{0});
}

// A position with own=opp=0 legitimately hashes to Zobrist key 0 - the exact degenerate case
// shared_tt.hpp's own doc comment calls out as the reason keyXorData==0 alone can never be the
// emptiness test. Confirms probing key 0 on a genuinely-empty table is still a clean miss.
TEST(SharedTranspositionTable, ProbingKeyZeroOnAnEmptyTableIsStillAMiss) {
    SharedTranspositionTable tt(1024);
    EXPECT_EQ(tt.probe(0), std::nullopt);
}

// Positive control for the torn-read test below: writing a genuinely CONSISTENT pair of words
// through the same raw-write hook must still probe as a real hit - proves the hook itself
// works and isn't just unconditionally producing misses.
TEST(SharedTranspositionTable, DebugWriteRawWordsProducesARealHitWhenConsistent) {
    SharedTranspositionTable tt(1024);
    constexpr std::uint64_t key = 100;
    const std::uint64_t data = independent::packData(10, 5, Bound::Exact, 20);
    tt.debugWriteRawWordsForTesting(key, key ^ data, data);

    const std::optional<TTEntry> entry = tt.probe(key);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->score, 10);
    EXPECT_EQ(entry->depth, 5);
    EXPECT_EQ(entry->bound, Bound::Exact);
    EXPECT_EQ(entry->bestMove, 20);
}

// The deterministic torn-read simulation: constructs the exact raw words TWO DIFFERENT real
// store() calls would have produced for the same key, then injects a MIX of one call's
// keyXorData with the OTHER call's data - exactly what a genuinely torn read (interleaved with
// a concurrent store) would observe. probe() must reject this as a miss, not return either
// entry's data (which would mean trusting a value that was never actually, atomically stored).
TEST(SharedTranspositionTable, ProbeRejectsATornCombinationOfTwoDifferentStores) {
    SharedTranspositionTable tt(1024);
    constexpr std::uint64_t key = 100;
    const std::uint64_t dataA = independent::packData(10, 5, Bound::Exact, 20);
    const std::uint64_t dataB = independent::packData(-30, 7, Bound::Lower, 5);
    ASSERT_NE(dataA, dataB);
    const std::uint64_t keyXorDataA = key ^ dataA;

    tt.debugWriteRawWordsForTesting(key, keyXorDataA, dataB);
    EXPECT_EQ(tt.probe(key), std::nullopt);
}

// The real concurrent stress test this milestone's plan calls for: many threads hammering
// store()/probe() on one shared table with a real position sample and varied depths/scores.
// Workers never call EXPECT_/ASSERT_ directly (not safe to call from arbitrary threads) - each
// worker only flips a shared atomic<bool> if it observes something structurally impossible; the
// main thread asserts that flag after joining every worker.
//
// This is inherently a "didn't observe a failure" style test - it raises confidence but cannot
// prove the absence of races (see shared_tt.hpp's own doc comment for the actual correctness
// argument, which does not depend on timing). The ci-linux-tsan CI job is the real authority on
// data-race freedom, not this test.
TEST(SharedTranspositionTable, ConcurrentStoreAndProbeNeverProducesACorruptedReadOrCrashes) {
    SharedTranspositionTable tt(std::size_t{1} << 14);
    const std::vector<Position>& positions = bench::benchmarkPositions();
    ASSERT_FALSE(positions.empty());

    constexpr int kThreadCount = 8;
    constexpr int kIterationsPerThread = 20000;
    std::atomic<bool> corruptionObserved{false};

    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);
    for (int t = 0; t < kThreadCount; ++t) {
        threads.emplace_back([&, t] {
            std::mt19937 rng(static_cast<unsigned>(t) * 7919U + 12345U);
            std::uniform_int_distribution<std::size_t> posPick(0, positions.size() - 1);
            std::uniform_int_distribution<int> depthPick(0, 30);
            std::uniform_int_distribution<int> scorePick(-64, 64);
            std::uniform_int_distribution<int> boundPick(0, 2);
            std::uniform_int_distribution<int> movePick(-1, 63);
            for (int i = 0; i < kIterationsPerThread; ++i) {
                const Position& pos = positions[posPick(rng)];
                const std::uint64_t key = zobristHash(pos);
                if (i % 2 == 0) {
                    tt.store(key, depthPick(rng), scorePick(rng),
                             static_cast<Bound>(boundPick(rng)), movePick(rng));
                } else {
                    const std::optional<TTEntry> entry = tt.probe(key);
                    if (entry.has_value()) {
                        const bool boundValid = entry->bound == Bound::Exact ||
                                                entry->bound == Bound::Lower ||
                                                entry->bound == Bound::Upper;
                        const bool scoreValid = entry->score >= -64 && entry->score <= 64;
                        const bool moveValid = entry->bestMove >= -1 && entry->bestMove < 64;
                        if (!boundValid || !scoreValid || !moveValid || entry->key != key) {
                            corruptionObserved.store(true, std::memory_order_relaxed);
                        }
                    }
                }
            }
        });
    }
    for (std::thread& th : threads) {
        th.join();
    }

    EXPECT_FALSE(corruptionObserved.load());
}

} // namespace
} // namespace reversi
