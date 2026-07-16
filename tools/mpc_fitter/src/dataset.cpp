#include "mpc_fitter/dataset.hpp"

#include "reversi/eval.hpp"
#include "reversi/search.hpp"

#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

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

Dataset readDataset(std::istream& in) {
    Dataset dataset;
    bool sawDepthsLine = false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        if (line.front() == '%') {
            constexpr std::string_view kPrefix = "% depths:";
            if (line.compare(0, kPrefix.size(), kPrefix) == 0) {
                std::istringstream depthsIn(line.substr(kPrefix.size()));
                int depth = 0;
                while (depthsIn >> depth) {
                    dataset.depths.push_back(depth);
                }
                sawDepthsLine = true;
            }
            continue;
        }
        std::istringstream rowIn(line);
        std::vector<int> row;
        int value = 0;
        while (rowIn >> value) {
            row.push_back(value);
        }
        if (row.size() != dataset.depths.size()) {
            throw std::runtime_error("mpc dataset row has " + std::to_string(row.size()) +
                                     " values, expected " + std::to_string(dataset.depths.size()) +
                                     " (one per header-declared depth): " + line);
        }
        dataset.rows.push_back(std::move(row));
    }
    if (!sawDepthsLine) {
        throw std::runtime_error("mpc dataset is missing its '% depths:' header line");
    }
    return dataset;
}

} // namespace mpc_fitter
