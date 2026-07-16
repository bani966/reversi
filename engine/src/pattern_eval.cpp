#include "reversi/pattern_eval.hpp"

#include "reversi/pattern.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>

namespace reversi {

namespace {

// Explicit little-endian byte assembly (not a raw struct read) - matches
// tools/wthor_extractor/src/parser.cpp's readU16LE convention, portable regardless of host
// endianness rather than assuming it matches the file's.
std::uint32_t readU32LE(std::istream& in) {
    unsigned char b[4];
    in.read(reinterpret_cast<char*>(b), 4);
    if (!in) {
        throw std::runtime_error("unexpected end of weight file while reading a 4-byte field");
    }
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8U) |
           (static_cast<std::uint32_t>(b[2]) << 16U) | (static_cast<std::uint32_t>(b[3]) << 24U);
}

std::int32_t readI32LE(std::istream& in) {
    return static_cast<std::int32_t>(readU32LE(in));
}

float readF32LE(std::istream& in) {
    const std::uint32_t bits = readU32LE(in);
    return std::bit_cast<float>(bits);
}

std::size_t statesForLength(std::size_t length) {
    std::size_t states = 1;
    for (std::size_t i = 0; i < length; ++i) {
        states *= 3;
    }
    return states;
}

} // namespace

PatternEvaluator::PatternEvaluator(const std::filesystem::path& weightFile) {
    std::ifstream in(weightFile, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open pattern-eval weight file: " + weightFile.string());
    }

    const std::uint32_t bucketCount = readU32LE(in);
    const std::vector<pattern::PatternClass>& classes = pattern::allPatternClasses();

    buckets_.resize(bucketCount);
    for (Bucket& bucket : buckets_) {
        bucket.minEmpty = readI32LE(in);
        bucket.maxEmpty = readI32LE(in);
        bucket.intercept = readF32LE(in);
        bucket.weightsByShape.resize(classes.size());
        for (std::size_t shapeId = 0; shapeId < classes.size(); ++shapeId) {
            const std::size_t length =
                classes[shapeId].instances.empty() ? 0 : classes[shapeId].instances.front().size();
            const std::size_t states = statesForLength(length);
            bucket.weightsByShape[shapeId].resize(states);
            for (std::size_t i = 0; i < states; ++i) {
                bucket.weightsByShape[shapeId][i] = readF32LE(in);
            }
        }
    }

    // A strong structural check, same discipline as parseWtbFile's exact-size validation: no
    // trailing bytes left unread.
    const std::streampos afterAllBuckets = in.tellg();
    in.seekg(0, std::ios::end);
    if (in.tellg() != afterAllBuckets) {
        throw std::runtime_error("pattern-eval weight file has unexpected trailing data: " +
                                 weightFile.string());
    }
}

const PatternEvaluator::Bucket& PatternEvaluator::bucketFor(int emptyCount) const {
    for (const Bucket& bucket : buckets_) {
        if (emptyCount >= bucket.minEmpty && emptyCount <= bucket.maxEmpty) {
            return bucket;
        }
    }
    throw std::runtime_error("pattern-eval weight file has no bucket covering emptyCount=" +
                             std::to_string(emptyCount));
}

int PatternEvaluator::evaluate(const Position& p) const {
    const Bucket& bucket = bucketFor(p.emptyCount());
    double sum = bucket.intercept;
    for (const pattern::PatternClass& cls : pattern::allPatternClasses()) {
        const std::vector<float>& weights =
            bucket.weightsByShape[static_cast<std::size_t>(cls.shapeId)];
        for (const std::vector<int>& instance : cls.instances) {
            const int index = pattern::ternaryIndex(p, instance);
            sum += weights[static_cast<std::size_t>(index)];
        }
    }
    return static_cast<int>(std::lround(sum));
}

EvalFn PatternEvaluator::asEvalFn() const {
    return [this](const Position& p) { return evaluate(p); };
}

} // namespace reversi
