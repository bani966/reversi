# Reversi

A Reversi/Othello desktop application: a bitboard engine in pure C++20 with alpha-beta
search, a perfect-play endgame solver, a WTHOR-trained pattern evaluation with
Multi-ProbCut, and a minimalist Qt 6 Widgets GUI with live engine analysis.

**Status: M9 complete — feature-complete GUI (save/load, undo/redo, import/export, AI vs AI,
on-demand MultiPV analysis, settings, move history) built and visually polished on top of M8's
Lazy SMP.**
Playable HvH/HvAI
in the Qt GUI (M3), with the AI driven by iterative deepening + a transposition table + PVS +
move ordering + aspiration windows on a wall-clock time budget (M4) — a measured, large
self-play gain over M2's plain fixed-depth alpha-beta baseline (deterministic gate: depth-12
matured search vs. depth-10 baseline, 63–0). M5 added a perfect-play exact endgame solver that
matches every published score in the vendored FFO endgame test subset exactly — see Benchmarks
below. M6 Phase 1 replaces disc-differential with a pattern-based evaluation trained on real
WTHOR tournament data (12 pattern shapes — lines, diagonals, edge+2X, corner blocks —
ternary-encoded with symmetry-shared weight tables, fit via ridge regression per game-phase
bucket): measured 20/20 in self-play against disc-differential at equal search depth, and the
`.wtb` parser/replay pipeline it's built on has been stress-tested against 8,874 real tournament
games (2016–2019) with zero illegal moves. M6 Phase 2 adds a toggleable opening book
(`OpeningBook`, off by default — no settings UI exposes it yet) built from canonicalized
positions (symmetry-deduplicated, first ~20 plies) observed at least 5 times across a real
WTHOR corpus; a book built from 8,886 real tournament games (1977, 2016–2019) produced 2,734
entries and, spot-checked against known Othello opening theory, recovers exactly the documented
main lines (e.g. 1.f5 d6, the Diagonal Opening; 1.f5 f6 e6, the Perpendicular Opening) —
including a cross-check that a symmetric-equivalent real opening (1.c4) independently
canonicalizes to and recovers the *same* underlying book entry as 1.f5, confirming the symmetry
canonicalization is correct on real data, not just in unit tests. Also new in Phase 2:
`selectMove()` (`engine/move_selector.hpp`), the book → exact-solver → heuristic-search dispatch
that GUI gameplay now goes through — previously missing entirely (the GUI called the heuristic
search directly, with no path to the exact solver at all). `tools/` (gitignored raw data,
generated weights/books ship as release assets — never committed) holds the extraction,
training, and book-building pipeline; see `tools/README.md`. M7 adds Multi-ProbCut
(`MpcModel`/`MpcConfig`, off by default, wired into `selectMove()`'s search branch): shallow-vs-
deep search value pairs are fit (closed-form OLS, `tools/mpc_fitter`) into per-depth-pair
coefficients, and `search()`'s internal nodes cut when a shallow probe's predicted deep value
clears the current alpha-beta window by a confidence margin. Real equal-time self-play
validation on a fitted model (1,500 self-played positions) found a genuinely-negative first
configuration (too many eligible nodes paying for shallow-probe overhead relative to actual
cuts taken) before landing on one with a measured, if modest, edge (11–9 in a 20-game equal-time
match) — both the finding and the fix are recorded in `DEVLOG.md`. M8 adds Lazy SMP parallel
search (`SharedTranspositionTable`, `searchLazySmp()` — engine/CLI-level only, not yet wired
into gameplay): multiple threads independently run the same iterative-deepening search, with
per-thread depth jitter, over one shared, lock-free transposition table — a "packed-atomic +
XOR checksum" design where each slot's two words are independent relaxed atomics and a probe
XORs them back together, rejecting anything that doesn't reconstruct the queried key (this
safely rejects genuine misses and torn/interleaved concurrent reads identically). Verified by a
local concurrent stress test and, more authoritatively, a dedicated ThreadSanitizer CI job added
specifically for this milestone. Measured on this dev machine (Intel i7-9700, 8 physical cores,
no hyperthreading): nps scales roughly 6.3× at 8 threads vs. 1, clearing the ≥3× target below
with real margin — an unambiguous pass. Strength is the honest exception: no statistically
significant strength difference was detected between Lazy SMP and single-threaded search at the
tested time budgets (three independent 20-game equal-time matches across two budgets, including
`GameController`'s actual production budget, 60 games combined — well within the noise a sample
this size can't distinguish from an even split). The single-position depth diagnostic confirms
the parallelism mechanism itself works (a genuinely deeper completed depth reached under an
identical time budget), but a measurable full-game strength edge was not established, nor ruled
out, at this sample size. Reported as a genuine open question rather than smoothed over — see
`DEVLOG.md` for the full investigation, including a code-confirmed (not inferred) partial
explanation: `searchLazySmp` always returns thread 0's own result by design (`search.cpp`), so any
helper thread that searches deeper than thread 0 on a given move has that extra depth discarded
except via its shared-TT contributions — a real limit on how much of the 6.3× nps figure could
ever convert into strength, and a plausible future improvement (selecting the deepest-completed
thread instead) scoped out of this milestone. Plus a second, still-speculative hypothesis: the
depth diagnostic only probes the opening position, while real games spend most of their length in
positions with less for jittered threads to usefully diverge on — left for future follow-up
rather than chased further here. M9 (five reviewed phases, no time-box) makes the GUI feature-
complete. Phase 1 restructured the layout for a side panel. Phase 2 added save/load (this app's
own JSON format), undo/redo, and plain-text transcript/position import-export. Phase 3 added an
on-demand analysis panel: single-line eval/depth/nodes/PV plus MultiPV (`analyzeTopMoves()`,
`engine/analysis.hpp` — ranks N candidate moves by repeated search with each already-ranked move
excluded, locked to the *same* completed depth per line so the ranking is genuinely comparable,
not an artifact of unequal search depth caught by manual testing during development). Phase 4
added a Settings dialog: AI vs AI game mode, and real loading for the previously-UI-less opening
book/MPC model toggles, plus AI search depth/time-budget/exact-solver-threshold tuning. Phase 5
was a visual-parity pass matching the board/title-bar's established look (flat surfaces, rounded
cards, `Palette.hpp`-routed colors) — plus a genuinely missed spec item caught and built during
that pass: a move-history list (reusing the same undo/redo restore path, not new state), and a
structural rebuild of the MultiPV display from a monospace text block into real per-line row
cards. See `DEVLOG.md` for the full per-phase writeups, including three real bugs manual GUI
testing caught (not the automated suite) during phase 3 alone.

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
| M6 (done) | Pattern eval + opening book | WTHOR-trained eval beats hand eval at equal depth; full-DB replay passes |
| M7 (done) | Multi-ProbCut | Measured equal-time strength gain; toggle off by default |
| M8 (done*) | Lazy SMP | ≥3× nps on 8 threads; TSan clean; no strength regression at equal time |
| M9 (done) | Feature complete | Undo/redo, save/load, import/export, settings, AI vs AI, analysis panel |
| M10 | Release | Animations, sound, themes, installers, v1.0 |

\* M8's engineering deliverable is complete, correct, and CI-verified (TSan-clean, nps clears the
≥3× target with real margin); the strength leg of its exit criterion is inconclusive, not
failed — no statistically significant equal-time strength difference was detected in either
direction at the sample size tested. See the M8 paragraph above and `DEVLOG.md` for the honest
numbers and the open follow-up hypothesis.

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
