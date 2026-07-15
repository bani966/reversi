#pragma once

#include "reversi/position.hpp"

#include <functional>

namespace reversi {

// Called only when hasLegalMove(p) is true — the game loop applies forced passes itself, so
// player implementations never need to handle the no-legal-move case.
using PlayerFn = std::function<int(const Position&)>;

struct GameResult {
    int blackDiscs = 0;
    int whiteDiscs = 0;
};

// Plays one game from the start position, `black` and `white` are asked for a move only on
// their own turn and only when they have one; forced passes are applied automatically.
GameResult playGame(const PlayerFn& black, const PlayerFn& white);

struct MatchResult {
    int aWins = 0;
    int bWins = 0;
    int draws = 0;
};

// Plays `games` games between `a` and `b`, alternating who plays black each game so neither
// side is favored by the first-move advantage.
MatchResult playMatch(const PlayerFn& a, const PlayerFn& b, int games);

} // namespace reversi
