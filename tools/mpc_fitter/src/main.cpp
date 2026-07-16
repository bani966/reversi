#include "mpc_fitter/dataset.hpp"

#include "reversi/moves.hpp"
#include "reversi/players.hpp"

#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <random>
#include <string_view>
#include <vector>

namespace {

void printUsage() {
    std::cerr
        << "Usage:\n"
        << "  mpc-fitter generate <numGames> <seed> <minDepth> <maxDepth> <out.txt>\n"
        << "      Self-plays <numGames> fixed-seed games, sampling one position per real move\n"
        << "      played, and writes reversi::search(pos, d, evaluateDiscDifferential).score for\n"
        << "      every d in [minDepth, maxDepth] as one dense dataset line per position.\n";
}

int runGenerate(int numGames, unsigned seed, int minDepth, int maxDepth, const char* outputPath) {
    std::vector<int> depths;
    for (int d = minDepth; d <= maxDepth; ++d) {
        depths.push_back(d);
    }

    std::ofstream out(outputPath);
    if (!out) {
        std::cerr << "cannot open output file: " << outputPath << "\n";
        return 1;
    }
    mpc_fitter::writeDatasetHeader(depths, out);

    std::mt19937 rng(seed);
    long long positionsWritten = 0;
    for (int g = 0; g < numGames; ++g) {
        reversi::Position pos = reversi::Position::start();
        while (!reversi::isGameOver(pos)) {
            if (!reversi::hasLegalMove(pos)) {
                pos = reversi::applyPass(pos);
                continue;
            }
            mpc_fitter::writeDatasetLine(pos, depths, out);
            ++positionsWritten;
            const int square = reversi::pickRandomMove(pos, rng);
            pos = reversi::applyMove(pos, square);
        }
    }
    std::cout << numGames << " games self-played (seed " << seed << "), " << positionsWritten
              << " positions written\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }
    const std::string_view command = argv[1];
    try {
        if (command == "generate" && argc >= 7) {
            return runGenerate(std::atoi(argv[2]), static_cast<unsigned>(std::atoi(argv[3])),
                               std::atoi(argv[4]), std::atoi(argv[5]), argv[6]);
        }
        printUsage();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
