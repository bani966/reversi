#include "wthor_extractor/dataset.hpp"
#include "wthor_extractor/parser.hpp"

#include "reversi/moves.hpp"
#include "reversi/players.hpp"

#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <random>
#include <string_view>

namespace {

void printUsage() {
    std::cerr << "Usage:\n"
              << "  wthor-extractor verify <path.wtb>\n"
              << "      Parses every game and replays its moves through the real engine,\n"
              << "      reporting how many games replayed with every move legal. No output\n"
              << "      file - this is the rules-engine stress test mode.\n"
              << "  wthor-extractor extract <path.wtb> <output-dataset.txt>\n"
              << "      Parses, replays, and writes one sparse pattern-feature dataset line\n"
              << "      per sampled position to the output file.\n"
              << "  wthor-extractor synth-dataset <numGames> <seed> <output-dataset.txt>\n"
              << "      Generates a dataset from fixed-seed engine self-play (fully legal by\n"
              << "      construction, zero dependency on WTHOR data) - used for the small\n"
              << "      committed dev/test weight fixture, sidestepping any question about\n"
              << "      redistributing data derived from the WTHOR database.\n";
}

int runVerify(const char* wtbPath) {
    const std::vector<wthor::GameRecord> records = wthor::parseWtbFile(wtbPath);
    std::cout << "parsed " << records.size() << " game record(s)\n";

    int ok = 0;
    int failed = 0;
    for (const wthor::GameRecord& record : records) {
        try {
            const wthor::ReplayedGame replayed = wthor::replayGame(record);
            ++ok;
            (void)replayed;
        } catch (const std::exception& e) {
            std::cerr << "  FAILED: " << e.what() << "\n";
            ++failed;
        }
    }
    std::cout << ok << " games replayed cleanly, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}

int runExtract(const char* wtbPath, const char* outputPath) {
    const std::vector<wthor::GameRecord> records = wthor::parseWtbFile(wtbPath);
    std::cout << "parsed " << records.size() << " game record(s)\n";

    std::ofstream out(outputPath);
    if (!out) {
        std::cerr << "cannot open output file: " << outputPath << "\n";
        return 1;
    }
    wthor::writeDatasetHeader(out);

    int gamesOk = 0;
    int gamesFailed = 0;
    long long positionsWritten = 0;
    for (const wthor::GameRecord& record : records) {
        try {
            const wthor::ReplayedGame replayed = wthor::replayGame(record);
            wthor::writeDatasetLines(replayed, out);
            positionsWritten += static_cast<long long>(replayed.positions.size());
            ++gamesOk;
        } catch (const std::exception& e) {
            std::cerr << "  FAILED: " << e.what() << "\n";
            ++gamesFailed;
        }
    }
    std::cout << gamesOk << " games extracted (" << positionsWritten << " positions), "
              << gamesFailed << " failed\n";
    return gamesFailed == 0 ? 0 : 1;
}

int runSynthDataset(int numGames, unsigned seed, const char* outputPath) {
    std::ofstream out(outputPath);
    if (!out) {
        std::cerr << "cannot open output file: " << outputPath << "\n";
        return 1;
    }
    wthor::writeDatasetHeader(out);

    std::mt19937 rng(seed);
    long long positionsWritten = 0;
    for (int g = 0; g < numGames; ++g) {
        wthor::GameRecord record;
        reversi::Position pos = reversi::Position::start();
        while (!reversi::isGameOver(pos)) {
            if (!reversi::hasLegalMove(pos)) {
                pos = reversi::applyPass(pos);
                continue;
            }
            const int square = reversi::pickRandomMove(pos, rng);
            record.moves.push_back(square);
            pos = reversi::applyMove(pos, square);
        }
        const wthor::ReplayedGame replayed = wthor::replayGame(record);
        wthor::writeDatasetLines(replayed, out);
        positionsWritten += static_cast<long long>(replayed.positions.size());
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
        if (command == "verify" && argc >= 3) {
            return runVerify(argv[2]);
        }
        if (command == "extract" && argc >= 4) {
            return runExtract(argv[2], argv[3]);
        }
        if (command == "synth-dataset" && argc >= 5) {
            return runSynthDataset(std::atoi(argv[2]), static_cast<unsigned>(std::atoi(argv[3])),
                                   argv[4]);
        }
        printUsage();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
