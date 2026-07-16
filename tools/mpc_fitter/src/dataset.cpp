#include "mpc_fitter/dataset.hpp"

#include "reversi/eval.hpp"
#include "reversi/search.hpp"

namespace mpc_fitter {

void writeDatasetHeader(const std::vector<int>& depths, std::ostream& out) {
    out << "% mpc training dataset\n"
        << "% one line per sampled position: search(pos, d, evaluateDiscDifferential).score for\n"
        << "% each d in `depths`, in order, space-separated.\n"
        << "% depths:";
    for (const int depth : depths) {
        out << " " << depth;
    }
    out << "\n";
}

void writeDatasetLine(const reversi::Position& pos, const std::vector<int>& depths,
                      std::ostream& out) {
    for (std::size_t i = 0; i < depths.size(); ++i) {
        if (i > 0) {
            out << " ";
        }
        out << reversi::search(pos, depths[i], reversi::evaluateDiscDifferential).score;
    }
    out << "\n";
}

} // namespace mpc_fitter
