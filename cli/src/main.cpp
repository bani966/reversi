#include "reversi/perft.hpp"
#include "reversi/position.hpp"

#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace {

// Othello diagram orientation: a1 top-left, ranks increase downward.
// At the start position it is black to move, so own = X (black), opp = O (white).
void printBoard(const reversi::Position& p) {
    std::cout << "  a b c d e f g h\n";
    for (int rank = 0; rank < 8; ++rank) {
        std::cout << rank + 1 << ' ';
        for (int file = 0; file < 8; ++file) {
            const reversi::Bitboard b = reversi::bit(reversi::squareIndex(file, rank));
            const char c = (p.own & b) != 0 ? 'X' : (p.opp & b) != 0 ? 'O' : '.';
            std::cout << c << ' ';
        }
        std::cout << '\n';
    }
}

void printUsage() {
    std::cout << "reversi-cli " << REVERSI_VERSION << "\n"
              << "Usage: reversi-cli <command>\n\n"
              << "Commands:\n"
              << "  version         Print version\n"
              << "  start           Print the initial position (toolchain smoke test)\n"
              << "  perft <depth>   Print node counts for depths 1..<depth> from the start position\n"
              << "  bench           (arrives with M2)\n"
              << "  selfplay        (arrives with M2)\n"
              << "  solve           (arrives with M5)\n";
}

// Parses a positive perft depth; returns nullopt on anything else (missing arg, not a
// number, zero, negative).
std::optional<int> parsePerftDepth(int argc, char** argv) {
    if (argc < 3) {
        return std::nullopt;
    }
    try {
        const int depth = std::stoi(argv[2]);
        return depth >= 1 ? std::optional(depth) : std::nullopt;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::string_view cmd = argc > 1 ? argv[1] : "help";
    if (cmd == "version") {
        std::cout << "reversi-cli " << REVERSI_VERSION << '\n';
        return 0;
    }
    if (cmd == "start") {
        printBoard(reversi::Position::start());
        return 0;
    }
    if (cmd == "perft") {
        const std::optional<int> depth = parsePerftDepth(argc, argv);
        if (!depth) {
            std::cerr << "Usage: reversi-cli perft <depth>\n";
            return 1;
        }
        const reversi::Position start = reversi::Position::start();
        for (int d = 1; d <= *depth; ++d) {
            std::cout << d << ": " << reversi::perft(start, d) << "\n";
        }
        return 0;
    }
    printUsage();
    return cmd == "help" ? 0 : 1;
}
