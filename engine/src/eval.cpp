#include "reversi/eval.hpp"

namespace reversi {

int evaluateDiscDifferential(const Position& p) {
    return p.ownCount() - p.oppCount();
}

int terminalScore(const Position& p) {
    return p.ownCount() - p.oppCount();
}

} // namespace reversi
