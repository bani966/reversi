#include "wthor_extractor/dataset.hpp"

#include "reversi/pattern.hpp"

namespace wthor {

void writeDatasetHeader(std::ostream& out) {
    out << "% WTHOR-derived pattern-eval training dataset.\n"
        << "% One position per line: <target score, mover-relative> <emptySquareCount> "
        << "<shapeId:canonicalIndex> ...\n"
        << "% Target score matches reversi::SearchResult::score's convention (mover's own\n"
        << "% perspective, matching terminalScore's scale exactly - see the M6 plan's\n"
        << "% eval/terminalScore commensurability note).\n"
        << "% Pattern classes (shapeId name length statesCount = 3^length):\n";
    for (const reversi::pattern::PatternClass& cls : reversi::pattern::allPatternClasses()) {
        const std::size_t length = cls.instances.empty() ? 0 : cls.instances.front().size();
        int states = 1;
        for (std::size_t i = 0; i < length; ++i) {
            states *= 3;
        }
        out << "%   " << cls.shapeId << " " << cls.name << " " << length << " " << states << "\n";
    }
}

void writeDatasetLine(const reversi::Position& pos, bool posMoverIsBlack, int finalBlackDiscs,
                      int finalWhiteDiscs, std::ostream& out) {
    const int target = moverRelativeFinalScore(posMoverIsBlack, finalBlackDiscs, finalWhiteDiscs);
    out << target << " " << pos.emptyCount();
    for (const reversi::pattern::PatternClass& cls : reversi::pattern::allPatternClasses()) {
        for (const std::vector<int>& instance : cls.instances) {
            const int index = reversi::pattern::ternaryIndex(pos, instance);
            out << " " << cls.shapeId << ":" << index;
        }
    }
    out << "\n";
}

void writeDatasetLines(const ReplayedGame& game, std::ostream& out) {
    for (const ReplayedPosition& rp : game.positions) {
        writeDatasetLine(rp.pos, rp.moverIsBlack, game.finalBlackDiscs, game.finalWhiteDiscs, out);
    }
}

} // namespace wthor
