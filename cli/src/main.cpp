#include "reversi/perft.hpp"
#include "reversi/players.hpp"
#include "reversi/position.hpp"
#include "reversi/search.hpp"
#include "reversi/selfplay.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
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
    std::cout
        << "reversi-cli " << REVERSI_VERSION << "\n"
        << "Usage: reversi-cli <command>\n\n"
        << "Commands:\n"
        << "  version                        Print version\n"
        << "  start                          Print the initial position (toolchain smoke test)\n"
        << "  perft <depth>                  Print node counts for depths 1..<depth>\n"
        << "  bench <depth>                  Fixed-depth search from the start position:\n"
        << "                                 best move, score, nodes, elapsed time, nps\n"
        << "  selfplay <a> <b> [games=100]   Play a match between two players and tally\n"
        << "                                 wins/losses/draws. Player specs: random, greedy,\n"
        << "                                 search:<depth>\n"
        << "  solve                          (arrives with M5)\n";
}

// Parses a positive integer from argv[argIndex]; returns nullopt on anything else (missing
// arg, not a number, zero, negative). Shared by perft's and bench's depth arguments.
std::optional<int> parsePositiveArg(int argc, char** argv, int argIndex) {
    if (argc <= argIndex) {
        return std::nullopt;
    }
    try {
        const int value = std::stoi(argv[argIndex]);
        return value >= 1 ? std::optional(value) : std::nullopt;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// Builds a PlayerFn from a spec string: "random", "greedy", or "search:<depth>". Returns
// nullopt on an unrecognized spec or an invalid depth.
std::optional<reversi::PlayerFn> makePlayer(std::string_view spec) {
    if (spec == "random") {
        auto rng = std::make_shared<std::mt19937>(std::random_device{}());
        return reversi::PlayerFn(
            [rng](const reversi::Position& p) { return reversi::pickRandomMove(p, *rng); });
    }
    if (spec == "greedy") {
        return reversi::PlayerFn(reversi::pickGreedyMove);
    }
    constexpr std::string_view kSearchPrefix = "search:";
    if (spec.substr(0, kSearchPrefix.size()) == kSearchPrefix) {
        try {
            const int depth = std::stoi(std::string(spec.substr(kSearchPrefix.size())));
            if (depth < 1) {
                return std::nullopt;
            }
            return reversi::PlayerFn(
                [depth](const reversi::Position& p) { return reversi::search(p, depth).bestMove; });
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
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
        const std::optional<int> depth = parsePositiveArg(argc, argv, 2);
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
    if (cmd == "bench") {
        const std::optional<int> depth = parsePositiveArg(argc, argv, 2);
        if (!depth) {
            std::cerr << "Usage: reversi-cli bench <depth>\n";
            return 1;
        }
        const reversi::Position start = reversi::Position::start();
        const auto t0 = std::chrono::steady_clock::now();
        const reversi::SearchResult result = reversi::search(start, *depth);
        const auto t1 = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(t1 - t0).count();
        const std::uint64_t nps =
            seconds > 0.0 ? static_cast<std::uint64_t>(static_cast<double>(result.nodes) / seconds)
                          : result.nodes;
        std::cout << "depth: " << *depth << "\n"
                  << "best move: " << reversi::squareToString(result.bestMove) << "\n"
                  << "score: " << result.score << "\n"
                  << "nodes: " << result.nodes << "\n"
                  << "elapsed: " << seconds << "s\n"
                  << "nps: " << nps << "\n";
        return 0;
    }
    if (cmd == "selfplay") {
        if (argc < 4) {
            std::cerr << "Usage: reversi-cli selfplay <a> <b> [games=100]\n";
            return 1;
        }
        const std::optional<reversi::PlayerFn> playerA = makePlayer(argv[2]);
        const std::optional<reversi::PlayerFn> playerB = makePlayer(argv[3]);
        if (!playerA || !playerB) {
            std::cerr << "Unknown player spec (use: random, greedy, search:<depth>)\n";
            return 1;
        }
        const int games = argc >= 5 ? parsePositiveArg(argc, argv, 4).value_or(-1) : 100;
        if (games < 1) {
            std::cerr << "games must be a positive integer\n";
            return 1;
        }
        const reversi::MatchResult result = reversi::playMatch(*playerA, *playerB, games);
        std::cout << "a (" << argv[2] << ") wins: " << result.aWins << "\n"
                  << "b (" << argv[3] << ") wins: " << result.bWins << "\n"
                  << "draws: " << result.draws << "\n";
        return 0;
    }
    printUsage();
    return cmd == "help" ? 0 : 1;
}
