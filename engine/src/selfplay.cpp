#include "reversi/selfplay.hpp"

#include "reversi/moves.hpp"

namespace reversi {

GameResult playGame(const PlayerFn& black, const PlayerFn& white) {
    Position pos = Position::start();
    // Invariant, checked at the top of every iteration: pos.own belongs to black iff
    // blackToMove, since it's toggled in lockstep with applyMove/applyPass's own/opp swap.
    bool blackToMove = true;

    while (!isGameOver(pos)) {
        if (hasLegalMove(pos)) {
            const int square = blackToMove ? black(pos) : white(pos);
            pos = applyMove(pos, square);
        } else {
            pos = applyPass(pos);
        }
        blackToMove = !blackToMove;
    }

    return blackToMove ? GameResult{pos.ownCount(), pos.oppCount()}
                        : GameResult{pos.oppCount(), pos.ownCount()};
}

MatchResult playMatch(const PlayerFn& a, const PlayerFn& b, int games) {
    MatchResult result;
    for (int i = 0; i < games; ++i) {
        const bool aPlaysBlack = i % 2 == 0;
        const GameResult game = aPlaysBlack ? playGame(a, b) : playGame(b, a);
        const int aDiscs = aPlaysBlack ? game.blackDiscs : game.whiteDiscs;
        const int bDiscs = aPlaysBlack ? game.whiteDiscs : game.blackDiscs;
        if (aDiscs > bDiscs) {
            ++result.aWins;
        } else if (bDiscs > aDiscs) {
            ++result.bWins;
        } else {
            ++result.draws;
        }
    }
    return result;
}

} // namespace reversi
