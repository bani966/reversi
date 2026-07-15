#include "reversi/position.hpp"

#include <iostream>
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
              << "  version   Print version\n"
              << "  start     Print the initial position (toolchain smoke test)\n"
              << "  perft     (arrives with M1)\n"
              << "  bench     (arrives with M2)\n"
              << "  selfplay  (arrives with M2)\n"
              << "  solve     (arrives with M5)\n";
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
    printUsage();
    return cmd == "help" ? 0 : 1;
}
