# Reversi

A Reversi/Othello desktop application: a bitboard engine in pure C++20 with alpha-beta
search, a perfect-play endgame solver, a WTHOR-trained pattern evaluation with
Multi-ProbCut, and a minimalist Qt 6 Widgets GUI with live engine analysis.

**Status: M5 — endgame solver complete.** Playable HvH/HvAI in the Qt GUI (M3), with the AI
driven by iterative deepening + a transposition table + PVS + move ordering + aspiration
windows on a wall-clock time budget (M4) — a measured, large self-play gain over M2's plain
fixed-depth alpha-beta baseline (deterministic gate: depth-12 matured search vs. depth-10
baseline, 63–0; wall-clock real-config comparison across three 10-game runs: 6/10, 8+/10,
9/10, always a clear majority). M5 adds a perfect-play exact endgame solver (fastest-first +
empty-region-parity move ordering, no heuristic eval at all near the end of the game) that
matches every published score in the vendored FFO endgame test subset exactly — see
Benchmarks below. Disc-differential remains the only evaluation function outside the exact
endgame (a well-known weak Othello heuristic — it ignores mobility and positional control,
e.g. corner/edge value); addressed at M6 (WTHOR-trained pattern evaluation).

## Layout

| Directory | Contents |
|---|---|
| `engine/` | Pure C++20 static library: bitboards, search, evaluation. No Qt. |
| `cli/` | Console harness: `perft`, `bench`, `selfplay`, `solve`. |
| `app/` | Qt 6 Widgets GUI. |
| `tests/` | GoogleTest unit tests. |
| `tools/` | WTHOR pipeline: dataset extraction, eval training, opening book building. |

## Building

### Windows (primary target)

Prerequisites: Visual Studio 2022 Build Tools (C++ workload), CMake ≥ 3.24,
Qt 6.8 (`msvc2022_64` with the `qtmultimedia` module).

```
copy CMakeUserPresets.json.example CMakeUserPresets.json   (then set the Qt path inside)
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

### Linux / macOS (engine, CLI, and tests)

```
cmake --preset ci-linux            (ci-macos builds the GUI too, given Qt)
cmake --build --preset ci-linux --parallel
ctest --preset ci-linux
```

## Roadmap

| Milestone | Scope | Exit criterion |
|---|---|---|
| M0 (done) | Scaffolding, CI | CI green on Windows/macOS/Linux; formatting enforced |
| M1 (done) | Rules core | Perft matches published values; differential fuzz vs naive reference passes |
| M2 (done) | Baseline engine | Alpha-beta + simple eval; beats random and greedy 100–0 |
| M3 (done) | GUI MVP | Playable HvH/HvAI; engine on worker thread, cancelable |
| M4 (done) | Search maturity | Iterative deepening, TT, PVS, ordering, time control; large self-play gain vs M2 |
| M5 (done) | Endgame solver | FFO test positions solved with correct exact scores |
| M6 | Pattern eval + opening book | WTHOR-trained eval beats hand eval at equal depth; full-DB replay passes |
| M7 | Multi-ProbCut | Measured equal-time strength gain; toggle off by default |
| M8 | Lazy SMP | ≥3× nps on 8 threads; TSan clean; no strength regression at equal time |
| M9 | Feature complete | Undo/redo, save/load, import/export, settings, AI vs AI, analysis panel |
| M10 | Release | Animations, sound, themes, installers, v1.0 |

## Benchmarks

Exact endgame solver (`solveExact`, fastest-first + empty-region parity ordering, TT-backed),
measured on this machine — see `tests/data/ffo_easy.txt` for the vendored FFO-format positions
and their source, and `cli solve <board> <side>` to reproduce:

| Empty squares | Positions | Avg. time | Avg. nodes |
|---|---|---|---|
| 13–14 (vendored FFO subset) | 8 | 6 ms – 122 ms | up to ~313k |
| 14–15 | 2 | 126 ms | ~284k |
| 16–17 | 2 | 943 ms | ~2.1M |
| 18–19 | 2 | 52 s | ~121M |
| 20–21 | 2 | 164 s | ~391M |

Cost grows steeply with empty-square count, as expected for exhaustive search — the practical
"solves in well under a second" range currently tops out around 16–17 empties on this machine;
the 18+ rows are small samples (n=2 per bucket), indicative rather than precise.
`kExactSolverEmptyThreshold` (currently 12, in `engine/include/reversi/solver.hpp`) is
conservative relative to this data and could reasonably be raised — left as-is pending an
explicit decision on the interactivity/strength tradeoff for GUI wiring, which M5 did not
include.

## License

MIT — see [LICENSE](LICENSE).
