#include "mpc_fitter/fit.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace mpc_fitter {

namespace {

// Column index of `depth` within dataset.depths, or -1 if not present.
int columnFor(const Dataset& dataset, int depth) {
    for (std::size_t i = 0; i < dataset.depths.size(); ++i) {
        if (dataset.depths[i] == depth) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

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

} // namespace

std::vector<FittedPair> fitPairs(const Dataset& dataset, int reduction, int minDeep, int maxDeep,
                                 std::vector<std::string>& warnings) {
    std::vector<FittedPair> result;
    for (int deep = minDeep; deep <= maxDeep; ++deep) {
        const int shallow = deep - reduction;
        const int deepCol = columnFor(dataset, deep);
        const int shallowCol = columnFor(dataset, shallow);
        if (deepCol < 0 || shallowCol < 0) {
            warnings.push_back("skipping pair (shallow=" + std::to_string(shallow) + ", deep=" +
                               std::to_string(deep) + "): depth not present in dataset");
            continue;
        }

        std::size_t n = 0;
        double sumX = 0.0;
        double sumY = 0.0;
        for (const std::vector<int>& row : dataset.rows) {
            sumX += row[static_cast<std::size_t>(shallowCol)];
            sumY += row[static_cast<std::size_t>(deepCol)];
            ++n;
        }
        if (n < 2) {
            warnings.push_back("skipping pair (shallow=" + std::to_string(shallow) +
                               ", deep=" + std::to_string(deep) + "): fewer than 2 samples");
            continue;
        }
        const double meanX = sumX / static_cast<double>(n);
        const double meanY = sumY / static_cast<double>(n);

        double sxx = 0.0;
        double sxy = 0.0;
        for (const std::vector<int>& row : dataset.rows) {
            const double dx =
                static_cast<double>(row[static_cast<std::size_t>(shallowCol)]) - meanX;
            const double dy = static_cast<double>(row[static_cast<std::size_t>(deepCol)]) - meanY;
            sxx += dx * dx;
            sxy += dx * dy;
        }
        if (sxx == 0.0) {
            warnings.push_back("skipping pair (shallow=" + std::to_string(shallow) + ", deep=" +
                               std::to_string(deep) + "): zero variance in shallow values");
            continue;
        }
        const double b = sxy / sxx;
        const double a = meanY - b * meanX;

        double sse = 0.0;
        for (const std::vector<int>& row : dataset.rows) {
            const double predicted =
                a + b * static_cast<double>(row[static_cast<std::size_t>(shallowCol)]);
            const double residual =
                static_cast<double>(row[static_cast<std::size_t>(deepCol)]) - predicted;
            sse += residual * residual;
        }
        const double sigma = std::sqrt(sse / static_cast<double>(n - 1));

        result.push_back({deep, shallow, a, b, sigma, n});
    }
    return result;
}

void writeModelFile(const std::vector<FittedPair>& pairs, std::ostream& out) {
    writeU32LE(out, static_cast<std::uint32_t>(pairs.size()));
    for (const FittedPair& pair : pairs) {
        writeI32LE(out, pair.deepDepth);
        writeI32LE(out, pair.shallowDepth);
        writeF32LE(out, static_cast<float>(pair.a));
        writeF32LE(out, static_cast<float>(pair.b));
        writeF32LE(out, static_cast<float>(pair.sigma));
    }
}

} // namespace mpc_fitter
