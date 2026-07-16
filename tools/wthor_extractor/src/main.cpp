#include "wthor_extractor/book.hpp"
#include "wthor_extractor/dataset.hpp"
#include "wthor_extractor/parser.hpp"

#include "reversi/moves.hpp"
#include "reversi/players.hpp"

#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

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
              << "      redistributing data derived from the WTHOR database.\n"
              << "  wthor-extractor build-book <path.wtb> [<path.wtb> ...] --output <book.bin>\n"
              << "                              [--max-ply N] [--min-count N]\n"
              << "      Parses and replays every input file, accumulates canonicalized opening\n"
              << "      positions within the first max-ply real moves (default 20), and writes\n"
              << "      one book entry per canonical position observed at least min-count times\n"
              << "      (default 5) - the single most-played move, ties broken by average\n"
              << "      outcome. See engine/include/reversi/opening_book.hpp for the reader.\n"
              << "  wthor-extractor synth-book <numGames> <seed> <maxPly> <minCount> <out.bin>\n"
              << "      Same accumulate/finalize/write pipeline as build-book, but sourced from\n"
              << "      fixed-seed engine self-play instead of a .wtb file - used for the small\n"
              << "      committed dev/test book fixture, same reasoning as synth-dataset.\n";
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

int runSynthBook(int numGames, unsigned seed, int maxPly, unsigned minCount,
                 const char* outputPath) {
    std::mt19937 rng(seed);
    wthor::BookAccumulator acc;
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
        wthor::accumulateBookGame(replayed, maxPly, acc);
    }

    const std::vector<wthor::BookEntry> entries = wthor::finalizeBook(acc, minCount);
    std::ofstream out(outputPath, std::ios::binary);
    if (!out) {
        std::cerr << "cannot open output file: " << outputPath << "\n";
        return 1;
    }
    wthor::writeBookFile(entries, out);
    std::cout << numGames << " games self-played (seed " << seed << "), " << acc.size()
              << " distinct canonical positions seen, " << entries.size()
              << " book entries written\n";
    return 0;
}

// Parses `build-book`'s arguments: any number of .wtb input paths plus `--output <path>`
// (required) and optional `--max-ply N` / `--min-count N` overrides, in any relative order.
int runBuildBook(const std::vector<std::string>& args) {
    std::vector<std::string> wtbPaths;
    std::string outputPath;
    int maxPly = 20;
    unsigned minCount = 5;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--output" && i + 1 < args.size()) {
            outputPath = args[++i];
        } else if (args[i] == "--max-ply" && i + 1 < args.size()) {
            maxPly = std::atoi(args[++i].c_str());
        } else if (args[i] == "--min-count" && i + 1 < args.size()) {
            minCount = static_cast<unsigned>(std::atoi(args[++i].c_str()));
        } else {
            wtbPaths.push_back(args[i]);
        }
    }
    if (wtbPaths.empty() || outputPath.empty()) {
        std::cerr << "build-book requires at least one .wtb input path and --output <path>\n";
        return 1;
    }

    wthor::BookAccumulator acc;
    int gamesOk = 0;
    int gamesFailed = 0;
    for (const std::string& wtbPath : wtbPaths) {
        const std::vector<wthor::GameRecord> records = wthor::parseWtbFile(wtbPath);
        std::cout << wtbPath << ": parsed " << records.size() << " game record(s)\n";
        for (const wthor::GameRecord& record : records) {
            try {
                const wthor::ReplayedGame replayed = wthor::replayGame(record);
                wthor::accumulateBookGame(replayed, maxPly, acc);
                ++gamesOk;
            } catch (const std::exception& e) {
                std::cerr << "  FAILED: " << e.what() << "\n";
                ++gamesFailed;
            }
        }
    }

    const std::vector<wthor::BookEntry> entries = wthor::finalizeBook(acc, minCount);
    std::ofstream out(outputPath, std::ios::binary);
    if (!out) {
        std::cerr << "cannot open output file: " << outputPath << "\n";
        return 1;
    }
    wthor::writeBookFile(entries, out);

    std::cout << gamesOk << " games replayed (" << gamesFailed << " failed), " << acc.size()
              << " distinct canonical positions seen within " << maxPly << " plies, "
              << entries.size() << " book entries written (min-count " << minCount << ") to "
              << outputPath << "\n";
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
        if (command == "build-book" && argc >= 3) {
            std::vector<std::string> args;
            for (int i = 2; i < argc; ++i) {
                args.emplace_back(argv[i]);
            }
            return runBuildBook(args);
        }
        if (command == "synth-book" && argc >= 7) {
            return runSynthBook(std::atoi(argv[2]), static_cast<unsigned>(std::atoi(argv[3])),
                                std::atoi(argv[4]), static_cast<unsigned>(std::atoi(argv[5])),
                                argv[6]);
        }
        printUsage();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
