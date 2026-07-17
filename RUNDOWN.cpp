// ============================================================================
// RUNDOWN.cpp
//
// A simplified, presentation-oriented walkthrough of how this Reversi/Othello engine works.
// This is NOT the real production code - things are simplified and collapsed for readability
// wherever that made the idea clearer (real fast bitboard tricks, Qt's actual event handling,
// etc. are skipped in favor of plain loops and straight-line functions). See engine/ and app/ for
// the real implementation; see GUIDE.md to build it, DEVLOG.md for the full development history.
//
// Sections:
//   1. Board representation (bitboards)
//   2. Move generation + applying a move
//   3. "Input simulation" - how a click becomes a move in the real app
//   4. Evaluation (how good is a position?)
//   5. The AI: minimax with alpha-beta pruning  <-- the main event
//   6. Opening book (just the idea)
//   7. Brute-force endgame solver (just the idea)
//   8. Other ideas actually implemented (comments only - TT, iterative deepening, move
//      ordering, PVS, aspiration windows, Multi-ProbCut, Lazy SMP)
//   9. Main game loop, tying it all together
// ============================================================================

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <optional>
#include <unordered_map>
#include <vector>

// ============================================================================
// 1. Board representation (bitboards)
// ============================================================================
// The real engine stores a position as two 64-bit integers - one bit per square, one integer for
// each side's discs (engine/include/reversi/position.hpp). Bit i is the square with
// file = i % 8 (a..h), rank = i / 8 (1..8) - bit 0 is a1, bit 63 is h8.
//
// Rather than "black"/"white", the real engine (and this walkthrough) stores a position relative
// to whoever is about to move: `own` is the mover's discs, `opp` is the other side's. This sounds
// like a small detail, but it's what lets the AI section below use ONE recursive function instead
// of two mirror-image ones for "black's turn" and "white's turn" - see section 5.

using Bitboard = std::uint64_t;

struct Position {
    Bitboard own = 0;
    Bitboard opp = 0;
};

bool isBitSet(Bitboard bits, int square) {
    return (bits >> square) & 1ULL;
}
void setBit(Bitboard& bits, int square) {
    bits |= (Bitboard{1} << square);
}
void clearBit(Bitboard& bits, int square) {
    bits &= ~(Bitboard{1} << square);
}

// Standard Othello starting position: d4/e5 one color, d5/e4 the other. Black moves first, so
// `own` holds black's two discs here.
Position startingPosition() {
    Position pos;
    setBit(pos.own, 3 + 4 * 8); // d5
    setBit(pos.own, 4 + 3 * 8); // e4
    setBit(pos.opp, 3 + 3 * 8); // d4
    setBit(pos.opp, 4 + 4 * 8); // e5
    return pos;
}

// ============================================================================
// 2. Move generation + applying a move
// ============================================================================
// The one rule that makes Othello Othello: playing on a square captures every opponent disc that
// sits in an unbroken straight line between the new disc and one of your own already on the
// board - in any of the 8 directions. No captures, no legal move.

// The 8 straight-line directions from a square - every (file, rank) delta except (0, 0), the
// square itself.
constexpr std::array<std::pair<int, int>, 8> kDirections{{
    {-1, -1},
    {0, -1},
    {1, -1},
    {-1, 0},
    {1, 0},
    {-1, 1},
    {0, 1},
    {1, 1},
}};

// Every opponent disc that playing at `square` would capture - empty if the move is illegal.
// Walks each of the 8 directions in a straight line: a contiguous run of opponent discs
// immediately followed by one of the mover's own discs gets captured; anything else (an empty
// square, or hitting the board edge, before finding one's own disc) captures nothing in that
// direction.
std::vector<int> discsToFlip(const Position& pos, int square) {
    std::vector<int> flips;
    const int file0 = square % 8;
    const int rank0 = square / 8;

    for (auto [df, dr] : kDirections) {
        std::vector<int> run;
        int file = file0 + df;
        int rank = rank0 + dr;
        while (file >= 0 && file < 8 && rank >= 0 && rank < 8) {
            const int sq = rank * 8 + file;
            if (isBitSet(pos.opp, sq)) {
                run.push_back(sq);
            } else if (isBitSet(pos.own, sq) && !run.empty()) {
                flips.insert(flips.end(), run.begin(), run.end());
                break;
            } else {
                break; // empty square, or our own disc with no opponent run before it
            }
            file += df;
            rank += dr;
        }
    }
    return flips;
}

bool isLegalMove(const Position& pos, int square) {
    return !isBitSet(pos.own | pos.opp, square) && !discsToFlip(pos, square).empty();
}

std::vector<int> legalMoves(const Position& pos) {
    std::vector<int> moves;
    for (int square = 0; square < 64; ++square) {
        if (isLegalMove(pos, square)) {
            moves.push_back(square);
        }
    }
    return moves;
}

// Plays `square`: places the new disc, flips every captured disc, then hands the turn to the
// other side by swapping own/opp - the position is always seen "from the mover's side," so the
// side that just moved becomes `opp` for whoever looks at the returned position next.
Position applyMove(Position pos, int square) {
    const auto flips = discsToFlip(pos, square);
    setBit(pos.own, square);
    for (int sq : flips) {
        clearBit(pos.opp, sq);
        setBit(pos.own, sq);
    }
    std::swap(pos.own, pos.opp);
    return pos;
}

// ============================================================================
// 3. "Input simulation" - how a click becomes a move in the real app
// ============================================================================
// Collapsed from BoardWidget::mousePressEvent -> GameController::onSquareClicked() ->
// commitMove() (app/src/BoardWidget.cpp, app/src/GameController.cpp) into one straight-line
// function. The real thing is spread across a Qt mouse-event handler and a controller class
// talking over signals/slots; the shape of what happens is exactly this, though.

struct Game {
    Position position = startingPosition();
    bool aiTurn = false; // whose turn "own" currently represents
};

// Stubs standing in for real app behavior not relevant to this section.
void refreshBoardDisplay(const Game&) {
    // real version repaints BoardWidget, including the piece-flip animation for captured discs
}
void playMoveSound() {
    // real version plays move-self.wav via QSoundEffect
}
void startAiSearchOnWorkerThread(Game&) {
    // see section 5 - runs on a std::thread so the GUI never blocks while the AI thinks
}

// Called when the human clicks a square on the board.
void onSquareClicked(Game& game, int clickedSquare) {
    if (game.aiTurn) {
        return; // ignore clicks while it isn't the human's turn
    }
    if (!isLegalMove(game.position, clickedSquare)) {
        return; // silently ignore, same as the real GUI
    }

    game.position = applyMove(game.position, clickedSquare);
    playMoveSound();

    if (legalMoves(game.position).empty()) {
        game.position = Position{game.position.opp, game.position.own}; // no legal move - pass
    }

    game.aiTurn = true;
    refreshBoardDisplay(game);
    startAiSearchOnWorkerThread(game);
}

// ============================================================================
// 4. Evaluation (how good is a position?)
// ============================================================================
// The simplest possible evaluation: just the disc count difference. (The real engine replaces
// this with a WTHOR-trained pattern-based evaluation for most of the game, and an exact endgame
// solver near the end - section 7 - rather than ever guessing this crudely in practice.)
int evaluate(const Position& pos) {
    return std::popcount(pos.own) - std::popcount(pos.opp);
}

int finalScore(const Position& pos) {
    return std::popcount(pos.own) - std::popcount(pos.opp); // game's over - this is the real
                                                            // final result, not an estimate
}

// ============================================================================
// 5. The AI: minimax with alpha-beta pruning
// ============================================================================
// The core idea (minimax): each side picks the move that's best FOR THEM, assuming the other
// side replies with that same assumption, all the way down the game tree. "Best for me" is
// symmetric between the two sides, so instead of writing two mirror-image functions ("black's
// turn" / "white's turn"), one function suffices: every recursive call negates the score it gets
// back, because "good for my opponent, by X" is exactly "bad for me, by X" - this is why section
// 1's own/opp convention exists.
//
// The problem this alone doesn't solve: the raw game tree is enormous. A full game runs to about
// 60 moves, branching into several legal replies at almost every ply - searching literally
// everything is hopeless past a handful of plies.
//
// Alpha-beta pruning fixes this WITHOUT changing the answer at all - it isn't an approximation,
// it just recognizes branches that provably can't affect the result and skips them:
//   alpha = the best score the side to move has already secured somewhere in this search
//   beta  = the best score the OPPONENT has already secured for themselves, one level up
// If, partway through a branch, a reply is found that's already at least as good as beta, the
// opponent would simply never let the game reach this branch in the first place (they have a
// better option already, found elsewhere) - so there's no point searching the rest of it. This
// one check is what turns an exponential search into something that runs many moves deep, fast.

constexpr int kNegativeInfinity = -1'000'000;

// Best achievable score for the side to move, from THEIR perspective, searching `depth` plies
// ahead. `alpha`/`beta` start as (-infinity, +infinity) at the root and narrow as the search
// proceeds - see the explanation above.
int alphaBeta(const Position& pos, int depth, int alpha, int beta) {
    const auto moves = legalMoves(pos);

    if (moves.empty()) {
        const Position passed{pos.opp, pos.own}; // no legal move: turn passes
        if (legalMoves(passed).empty()) {
            return finalScore(pos); // neither side can move - game over
        }
        return -alphaBeta(passed, depth, -beta, -alpha);
    }

    if (depth == 0) {
        return evaluate(pos);
    }

    int best = kNegativeInfinity;
    for (int move : moves) {
        const Position child = applyMove(pos, move);
        const int score = -alphaBeta(child, depth - 1, -beta, -alpha);

        best = std::max(best, score);
        alpha = std::max(alpha, score);
        if (alpha >= beta) {
            break; // beta cutoff - the opponent would never allow this branch, stop searching it
        }
    }
    return best;
}

int chooseAiMove(const Position& pos, int depth) {
    int bestMove = -1;
    int bestScore = kNegativeInfinity;
    for (int move : legalMoves(pos)) {
        const Position child = applyMove(pos, move);
        const int score = -alphaBeta(child, depth - 1, kNegativeInfinity, -kNegativeInfinity);
        if (score > bestScore) {
            bestScore = score;
            bestMove = move;
        }
    }
    return bestMove;
}

// ============================================================================
// 6. Opening book (just the idea)
// ============================================================================
// Pre-compute, offline, from real tournament games, the known-good move for early-game positions
// - then the AI doesn't search AT ALL for the first ~20 moves, it just plays from memorized
// theory, the same way a chess opening book works.

std::uint64_t roughPositionHash(const Position& pos) {
    return pos.own ^ (pos.opp * 0x9E3779B97F4A7C15ULL); // stand-in only - not a real hash function
}

struct OpeningBook {
    std::unordered_map<std::uint64_t, int> moveByPositionHash;

    std::optional<int> lookup(const Position& pos) const {
        // The real book (engine/include/reversi/opening_book.hpp) also checks all 8 board
        // symmetries (rotations/reflections) of `pos` before giving up, so a mirrored
        // transposition of a known opening still hits the same book entry - see
        // engine/include/reversi/pattern.hpp's canonicalize().
        const auto it = moveByPositionHash.find(roughPositionHash(pos));
        return it != moveByPositionHash.end() ? std::optional(it->second) : std::nullopt;
    }
};

// ============================================================================
// 7. Brute-force endgame solver (just the idea)
// ============================================================================
// Once few enough squares are empty, it's cheap enough to search all the way to the true end of
// the game and know the EXACT final result - no evaluation-function guessing needed at all. This
// is exactly alphaBeta() above with two changes: it always recurses to the real end of the game
// (no depth limit), and the only "evaluation" is the actual final disc count.
int solveExact(const Position& pos, int alpha, int beta) {
    const auto moves = legalMoves(pos);
    if (moves.empty()) {
        const Position passed{pos.opp, pos.own};
        if (legalMoves(passed).empty()) {
            return finalScore(pos); // this IS the real, exact result - not an estimate
        }
        return -solveExact(passed, -beta, -alpha);
    }

    int best = kNegativeInfinity;
    for (int move : moves) {
        const int score = -solveExact(applyMove(pos, move), -beta, -alpha);
        best = std::max(best, score);
        alpha = std::max(alpha, score);
        if (alpha >= beta) {
            break;
        }
    }
    return best;
    // The real solver (engine/include/reversi/solver.hpp) adds fastest-first move ordering and
    // parity-based empty-region ordering on top of this - without them, this exact idea is
    // correct but far too slow to finish a search with ~15+ empty squares remaining.
}

// ============================================================================
// 8. Other ideas actually implemented (comments only - each is a refinement ON TOP of the
//    alpha-beta idea above, not a replacement for it; full details + real bugs found while
//    building each are in DEVLOG.md)
// ============================================================================
//
// - Transposition table (TT): a big cache from "position already searched" to "the score found
//   last time, and how deep that search went." The same position is often reached by different
//   move orders (move A then B gives the same board as move B then A) - the TT means the engine
//   only has to actually search a given position once.
//
// - Iterative deepening: search depth 1, then depth 2, then depth 3, ... until the time budget
//   runs out, instead of jumping straight to some fixed depth. Sounds wasteful (redoing shallow
//   work every pass) but isn't in practice: each shallow pass fills the TT with move-ordering
//   information the next, deeper pass reuses, and there's ALWAYS a usable move ready the instant
//   time runs out, rather than nothing until the search finishes.
//
// - Move ordering: alpha-beta prunes best when the strongest replies are tried FIRST (an earlier
//   cutoff skips searching everything after it) - so candidate moves are sorted by "most likely
//   to be good" (the TT's remembered best move first, corners prioritized, etc.) before
//   searching, not searched in raw board order.
//
// - PVS (Principal Variation Search): after the first move at a node is searched properly, every
//   other move first gets a cheap "null window" search that only answers "better or worse than
//   what I already have," not "exactly how much better." Only a "better" answer earns a full,
//   expensive re-search - most moves aren't better, so most of the tree only pays the cheap price.
//
// - Aspiration windows: each iterative-deepening pass starts its alpha-beta window narrow,
//   centered on the previous pass's score (instead of the full -infinity..+infinity), since the
//   score rarely swings wildly between one depth and the next. A narrower window prunes more; it
//   only costs a re-search on the rare pass where the guess was wrong.
//
// - Multi-ProbCut (MPC): if a much SHALLOWER search already predicts, with high statistical
//   confidence, that a branch will fail high or low, skip the expensive deep search on that
//   branch entirely. The prediction is fit from real search data (shallow-depth score vs.
//   true deep-depth score, linear regression) rather than guessed at.
//
// - Lazy SMP: run several threads at once, each doing basically the same iterative-deepening
//   search but with a slightly different starting depth ("jitter"), all sharing ONE
//   transposition table. No thread is explicitly assigned a part of the tree - the jitter alone
//   makes them naturally explore somewhat different parts, and every thread benefits from every
//   other thread's TT entries along the way.

// ============================================================================
// 9. Main game loop, tying it all together
// ============================================================================
// The real app runs this same shape (make a move, check for game over, switch sides, repeat) but
// spread across GameController/BoardWidget with Qt signals/slots instead of a straight-line loop,
// and with the AI's search running on a worker thread so the GUI never freezes while it thinks
// (see app/src/GameController.cpp). This CLI stand-in plays the same game end to end.

void printBoard(const Position& pos, bool ownIsBlack) {
    for (int rank = 7; rank >= 0; --rank) {
        for (int file = 0; file < 8; ++file) {
            const int sq = rank * 8 + file;
            char c = '.';
            if (isBitSet(pos.own, sq)) {
                c = ownIsBlack ? 'X' : 'O';
            } else if (isBitSet(pos.opp, sq)) {
                c = ownIsBlack ? 'O' : 'X';
            }
            std::cout << c << ' ';
        }
        std::cout << '\n';
    }
}

// Real version: BoardWidget::mousePressEvent turns pixel coordinates into a square index, then
// routes it through onSquareClicked() (section 3 above), which silently ignores an illegal
// square and just waits for the next click. This stand-in reads from stdin instead of a mouse
// click, so it re-prompts instead - same legality check either way (isLegalMove(), section 2) -
// and returns -1 on EOF so main() can end the game gracefully instead of looping forever.
int readHumanMove(const Position& pos) {
    while (true) {
        std::cout << "Your move (square 0-63, a1=0 .. h8=63): ";
        int square = -1;
        if (!(std::cin >> square)) {
            return -1;
        }
        if (isLegalMove(pos, square)) {
            return square;
        }
        std::cout << "Illegal move, try again.\n";
    }
}

int main() {
    Position pos = startingPosition();
    bool ownIsBlack = true; // black moves first

    while (true) {
        if (legalMoves(pos).empty()) {
            const Position passed{pos.opp, pos.own};
            if (legalMoves(passed).empty()) {
                break; // neither side can move - game over
            }
            pos = passed;
            ownIsBlack = !ownIsBlack;
            continue;
        }

        printBoard(pos, ownIsBlack);
        const int move = ownIsBlack ? readHumanMove(pos) : chooseAiMove(pos, /*depth=*/6);
        if (move < 0) {
            break; // stdin exhausted (EOF) - end the game instead of looping forever
        }
        pos = applyMove(pos, move);
        ownIsBlack = !ownIsBlack;
    }

    const int blackDiscs = ownIsBlack ? std::popcount(pos.own) : std::popcount(pos.opp);
    const int whiteDiscs = ownIsBlack ? std::popcount(pos.opp) : std::popcount(pos.own);
    std::cout << "Game over. Black: " << blackDiscs << "  White: " << whiteDiscs << '\n';
    return 0;
}
