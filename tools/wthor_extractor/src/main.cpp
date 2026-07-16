#include "wthor_extractor/parser.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: wthor-extractor <path-to.wtb>\n"
                  << "Parses every game in the file and replays its moves through the real "
                     "engine, reporting how many games replayed with every move legal.\n";
        return 1;
    }

    try {
        const std::vector<wthor::GameRecord> records = wthor::parseWtbFile(argv[1]);
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
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
