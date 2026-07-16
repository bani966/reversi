#include "reversi/mpc.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <vector>

namespace reversi {
namespace {

std::filesystem::path devModelPath() {
    return std::filesystem::path(REVERSI_TEST_DATA_DIR) / "dev_mpc_model.bin";
}

// A local, independent binary writer - deliberately NOT calling tools/mpc_fitter's own
// writeModelFile (engine/ tests must not depend on tools/, which is opt-in via
// REVERSI_BUILD_TOOLS). Also serves the same purpose as OpeningBook/PatternEvaluator's own
// tests: MpcModel (the production reader) is tested against hand-assembled bytes, not just
// against its own writer's output.
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

TEST(MpcModel, LoadsTheDevModelFileWithoutThrowing) {
    EXPECT_NO_THROW({ MpcModel model(devModelPath()); });
}

TEST(MpcModel, ThrowsOnMissingFile) {
    EXPECT_THROW(MpcModel{std::filesystem::path("does_not_exist_54321.bin")}, std::runtime_error);
}

TEST(MpcModel, ThrowsOnSizeMismatch) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "mpc_test_truncated.bin";
    {
        std::ofstream out(path, std::ios::binary);
        testmodel::writeU32LE(out, 2); // claims 2 entries but writes zero
    }
    EXPECT_THROW(MpcModel{path}, std::runtime_error);
    std::filesystem::remove(path);
}

TEST(MpcModel, ThrowsOnDuplicateDeepDepth) {
    const std::vector<testmodel::RawEntry> entries = {
        {/*deepDepth=*/6, /*shallowDepth=*/4, 1.0F, 0.9F, 3.0F},
        {/*deepDepth=*/6, /*shallowDepth=*/3, 0.5F, 0.8F, 2.5F}, // duplicate deepDepth=6
    };
    const std::filesystem::path path = testmodel::writeFile(entries, "mpc_test_duplicate.bin");
    EXPECT_THROW(MpcModel{path}, std::runtime_error);
    std::filesystem::remove(path);
}

TEST(MpcModel, LookupReturnsNulloptOnMiss) {
    const std::vector<testmodel::RawEntry> entries = {
        {/*deepDepth=*/6, /*shallowDepth=*/4, 1.0F, 0.9F, 3.0F},
    };
    const std::filesystem::path path = testmodel::writeFile(entries, "mpc_test_miss.bin");
    const MpcModel model(path);
    EXPECT_EQ(model.lookup(8), std::nullopt);
    std::filesystem::remove(path);
}

TEST(MpcModel, LookupReturnsTheStoredCoefficientsOnHit) {
    const std::vector<testmodel::RawEntry> entries = {
        {/*deepDepth=*/6, /*shallowDepth=*/4, 1.5F, 0.9F, 3.25F},
    };
    const std::filesystem::path path = testmodel::writeFile(entries, "mpc_test_hit.bin");
    const MpcModel model(path);

    const std::optional<MpcModel::Coefficients> result = model.lookup(6);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->shallowDepth, 4);
    EXPECT_FLOAT_EQ(static_cast<float>(result->a), 1.5F);
    EXPECT_FLOAT_EQ(static_cast<float>(result->b), 0.9F);
    EXPECT_FLOAT_EQ(static_cast<float>(result->sigma), 3.25F);
    std::filesystem::remove(path);
}

TEST(MpcModel, MultipleEntriesEachLookUpIndependently) {
    const std::vector<testmodel::RawEntry> entries = {
        {4, 2, 0.1F, 0.7F, 2.0F},
        {6, 4, 0.2F, 0.8F, 3.0F},
        {8, 6, 0.3F, 0.9F, 4.0F},
    };
    const std::filesystem::path path = testmodel::writeFile(entries, "mpc_test_multiple.bin");
    const MpcModel model(path);

    for (const testmodel::RawEntry& expected : entries) {
        const std::optional<MpcModel::Coefficients> result = model.lookup(expected.deepDepth);
        ASSERT_TRUE(result.has_value()) << "deepDepth " << expected.deepDepth;
        EXPECT_EQ(result->shallowDepth, expected.shallowDepth);
        EXPECT_FLOAT_EQ(static_cast<float>(result->a), expected.a);
        EXPECT_FLOAT_EQ(static_cast<float>(result->b), expected.b);
        EXPECT_FLOAT_EQ(static_cast<float>(result->sigma), expected.sigma);
    }
    std::filesystem::remove(path);
}

} // namespace
} // namespace reversi
