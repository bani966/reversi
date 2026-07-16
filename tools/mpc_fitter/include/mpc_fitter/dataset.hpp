#pragma once

#include "reversi/position.hpp"

#include <ostream>
#include <vector>

namespace mpc_fitter {

// Writes the dataset's self-describing header: a `%`-comment block ending in the exact list of
// depths present on every subsequent line (`% depths: <d0> <d1> ...`) - the only thing a reader
// needs to know to parse the file. Depths are never hardcoded on the reading side (step 2's
// `fit` subcommand) - this mirrors tools/wthor_extractor's dataset.hpp philosophy of deferring
// bucket/range decisions to the consuming step rather than baking them into the format.
void writeDatasetHeader(const std::vector<int>& depths, std::ostream& out);

// Writes one dataset line: reversi::search(pos, d, reversi::evaluateDiscDifferential).score for
// each d in `depths`, in the same order as the header - space-separated, one line, no trailing
// space. evaluateDiscDifferential is hardcoded, not parameterized: an MpcModel fit from this
// dataset is only valid for searches using this exact eval function (see engine/mpc.hpp's doc
// comment, added in step 3, for why a mismatched eval silently miscalibrates the cut margins).
void writeDatasetLine(const reversi::Position& pos, const std::vector<int>& depths,
                      std::ostream& out);

} // namespace mpc_fitter
