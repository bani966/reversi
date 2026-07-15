#include "ffo.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace reversi::endgame {

std::vector<FfoPosition> loadFfoPositions(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open FFO data file: " + path.string());
    }
    std::vector<FfoPosition> positions;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '%') {
            continue;
        }
        std::istringstream lineStream(line);
        std::string board;
        std::string side;
        int bestMove = 0;
        int score = 0;
        if (!(lineStream >> board >> side >> bestMove >> score)) {
            continue;
        }
        const std::optional<Position> pos = Position::fromBoardString(board, side == "Black");
        if (!pos) {
            continue;
        }
        // This source's score field is fixed to Black's perspective (a common convention for
        // an absolute-color board diagram format), not mover-relative - confirmed empirically
        // while vendoring ffo_easy.txt: every "White to move" line's published score was the
        // exact negation of solveExact's mover-relative result, every "Black to move" line
        // matched directly (mover == Black there, so the two conventions coincide and the
        // discrepancy was invisible until a White-to-move line was checked). Converted to
        // mover-relative here so downstream code never needs to know about this quirk - see
        // FfoPosition::score's doc comment for the promised convention.
        if (side == "White") {
            score = -score;
        }
        positions.push_back({*pos, bestMove, score});
    }
    return positions;
}

} // namespace reversi::endgame
