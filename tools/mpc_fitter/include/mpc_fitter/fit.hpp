#pragma once

#include "mpc_fitter/dataset.hpp"

#include <ostream>
#include <string>
#include <vector>

namespace mpc_fitter {

struct FittedPair {
    int deepDepth;
    int shallowDepth;
    double a;
    double b;
    double sigma;
    std::size_t sampleCount; // informational, printed by the CLI for inspectability
};

// For each `deep` in [minDeep, maxDeep], pairs it with `shallow = deep - reduction` and fits
// deepValue ~ a + b*shallowValue by closed-form ordinary least squares (single predictor - a
// direct mean/variance/covariance computation, no external linear-algebra library needed,
// unlike M6's ridge regression over a large pattern-feature space). sigma is the SAMPLE
// (N-1 denominator) standard deviation of the residuals deepValue - (a + b*shallowValue).
// A pair is skipped (not included in the result) if `shallow` or `deep` isn't present in
// `dataset.depths`, if fewer than 2 matching rows exist (can't compute a variance), or if every
// row has the identical shallow value (zero variance, b undefined) - each case appended to
// `warnings` as a human-readable message rather than thrown, so a too-ambitious range degrades
// gracefully instead of aborting the whole fit.
std::vector<FittedPair> fitPairs(const Dataset& dataset, int reduction, int minDeep, int maxDeep,
                                 std::vector<std::string>& warnings);

// Writes the binary MpcModel format (little-endian, dependency-free - matches the weight-file/
// book-file convention used elsewhere in this project; independently re-read by
// engine/include/reversi/mpc.hpp, added in step 3):
//   u32 pairCount
//   for each pair, in the order given: i32 deepDepth, i32 shallowDepth, f32 a, f32 b, f32 sigma
void writeModelFile(const std::vector<FittedPair>& pairs, std::ostream& out);

} // namespace mpc_fitter
