#pragma once

#include <filesystem>
#include <optional>
#include <vector>

namespace reversi {

// Confidence multiplier for MpcConfig::t, picked from real measured data (M7 step 5) - see
// DEVLOG.md and that step's commit message for the full numbers. Two configurations were
// tried on a real fitted model (1500 self-played positions, depths 2-8): reduction=2 covering
// deep depths 4-8 (5 pairs) was a clear NET LOSS in equal-time self-play at every t tried
// (2, 3, 4, 6, 8) - the shallow-probe overhead at that many eligible nodes outweighed the
// pruning benefit, a genuine finding, not a bug (the on/off-equivalence and always/never-cuts
// unit tests all pass; this is real cost/benefit, not incorrectness). Switching to reduction=4
// covering only the deeper pairs (6, 7, 8) - fewer eligible nodes, but each shallow probe is
// cheaper relative to the subtree it replaces - recovered a real, if modest, edge: 11-9 in a
// 20-game equal-time match at t=2.5 (budget 300ms soft / 800ms hard), 16/21 move agreement
// against a fixed depth-11 ground truth. This is the shipped default: reduction=4, deep depths
// 6-8 only. A model fit with a different depth-pair range should be re-validated, not assumed
// to inherit this t.
constexpr double kDefaultMpcT = 2.5;

// Looks up fitted Multi-ProbCut coefficients for a given remaining search depth ("deep depth").
// Built by `tools/mpc_fitter fit` from self-play-generated (shallowValue, deepValue) pairs (see
// that tool's own doc comments) - never real WTHOR data, unlike PatternEvaluator/OpeningBook;
// MPC needs no real game data at all, only self-consistent search-value statistics.
//
// HARD CONSTRAINT, same risk class as PatternEvaluator's eval/terminalScore commensurability
// note (pattern_eval.hpp): a loaded MpcModel's coefficients are calibrated against exactly the
// eval function `tools/mpc_fitter generate` used to produce its training data -
// reversi::evaluateDiscDifferential, the current production default. Using a fitted model
// together with any OTHER eval function (e.g. if PatternEvaluator is ever wired into
// GameController) without regenerating data and refitting would silently miscalibrate the cut
// margins - the shallow/deep VALUES a model's a/b/sigma describe are only meaningful for the
// exact eval they were measured under.
//
// On-disk format (little-endian, dependency-free, matching this project's weight-file/book-file
// convention), produced by tools/mpc_fitter/include/mpc_fitter/fit.hpp's writeModelFile:
//   u32 pairCount
//   for each pair:
//       i32 deepDepth
//       i32 shallowDepth
//       f32 a
//       f32 b
//       f32 sigma
class MpcModel {
public:
    struct Coefficients {
        int shallowDepth;
        double a;
        double b;
        double sigma;
    };

    // Throws std::runtime_error if `file` cannot be opened, its size doesn't match
    // pairCount * 20 + 4 bytes exactly, or two entries share the same deepDepth (an ambiguous,
    // build-side bug that must fail loudly here rather than silently returning whichever entry
    // happens to be scanned first).
    explicit MpcModel(const std::filesystem::path& file);

    // Returns the fitted coefficients for `deepDepth`, or nullopt if this model has no fitted
    // pair for that exact depth (MPC is simply skipped for such nodes - see search.cpp). A
    // linear scan, not a binary search: models typically hold single-digit pair counts (e.g. 5-7
    // depths), so the machinery OpeningBook needs for its thousands of entries would be
    // unjustified complexity here.
    std::optional<Coefficients> lookup(int deepDepth) const;

private:
    struct Entry {
        int deepDepth;
        int shallowDepth;
        double a;
        double b;
        double sigma;
    };
    std::vector<Entry> entries_;
};

// Threaded through search()/searchWindow()/searchIterative()/searchTimed() (search.hpp) as a
// new trailing defaulted parameter. `model == nullptr` (the default on every existing call
// site) disables Multi-ProbCut entirely, with zero overhead - checked before any lookup is
// attempted, so this is a genuine no-cost off-switch, not merely a `t`/sigma large enough that
// cuts never trigger (a model that's present but never triggers still pays for its shallow
// probes at every eligible node - see search.cpp's negamax integration).
struct MpcConfig {
    const MpcModel* model = nullptr;
    double t = kDefaultMpcT;
};

} // namespace reversi
