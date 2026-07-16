#pragma once

#include "reversi/eval.hpp"
#include "reversi/position.hpp"

#include <filesystem>
#include <vector>

namespace reversi {

// Loads and evaluates positions using a WTHOR-trained pattern-based evaluation (M6). The
// weight file format is produced by tools/train_pattern_eval.py (see that script's own doc
// comment for the exact byte layout) - engine/ only ever reads this generated artifact, never
// the training pipeline or raw WTHOR data (a hard build-time dependency boundary: this class
// has no code path that touches tools/ or a .wtb file at all).
//
// Design note, load-bearing for search correctness (not just eval accuracy): the weight file
// is trained to predict a position's FINAL DISC DIFFERENTIAL from the mover's perspective -
// exactly terminalScore()'s scale and meaning, by construction (see
// tools/wthor_extractor/dataset.hpp's target-score convention). This keeps evaluate()'s output
// commensurable with terminalScore() when search.cpp's negamax returns terminalScore() for a
// mid-tree game-over leaf into the exact same alpha-beta comparisons as this eval's own
// non-terminal leaves - mixing two differently-scaled values there would silently break
// alpha-beta and TT-bound correctness (see engine/src/search.cpp and the M6 plan's Context
// section for the full reasoning this class relies on without re-deriving it here).
class PatternEvaluator {
public:
    // Throws std::runtime_error if `weightFile` cannot be opened, or its contents don't match
    // the expected size for the bucket/pattern-class layout declared in its own header (a
    // strong structural check, same discipline as wthor_extractor's parseWtbFile).
    explicit PatternEvaluator(const std::filesystem::path& weightFile);

    // The trained prediction for `p`, rounded to the nearest int (matching EvalFn's int return
    // type and terminalScore()'s own integral scale).
    int evaluate(const Position& p) const;

    // Returns an EvalFn closing over `this` by reference - the evaluator must outlive any
    // EvalFn obtained from it. Loading a weight file is expensive enough (potentially many
    // megabytes) that copying it per EvalFn would be wasteful; searchers are expected to hold
    // one PatternEvaluator for the lifetime of the search(es) using it, exactly like how a
    // TranspositionTable is held by the caller, not by search() itself.
    EvalFn asEvalFn() const;

private:
    struct Bucket {
        int minEmpty = 0;
        int maxEmpty = 0;
        float intercept = 0.0F;
        // weightsByShape[shapeId][ternaryIndex], shapeId matching
        // reversi::pattern::allPatternClasses()'s fixed order.
        std::vector<std::vector<float>> weightsByShape;
    };
    std::vector<Bucket> buckets_;

    const Bucket& bucketFor(int emptyCount) const;
};

} // namespace reversi
