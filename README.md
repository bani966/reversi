# Reversi

A Reversi/Othello desktop application: a bitboard engine in pure C++20 with alpha-beta
search, a perfect-play endgame solver, a WTHOR-trained pattern evaluation with
Multi-ProbCut, and a minimalist Qt 6 Widgets GUI with live engine analysis.

**Status: v1.0.0 — released.** Feature-complete GUI (save/load, undo/redo, import/export, AI vs
AI, on-demand MultiPV analysis, settings, move history, piece-flip animation, sound, light/dark
theme), packaged as a native Windows installer and macOS DMG. See `DEVLOG.md` for the full
milestone-by-milestone development history (engine architecture, measured strength gains at each
step, and every real bug found along the way).

## Screenshots

<!-- TODO: add screenshots/demo GIF — board (light + dark theme), analysis panel, settings
     dialog. -->

## Layout

| Directory | Contents |
|---|---|
| `engine/` | Pure C++20 static library: bitboards, search, evaluation. No Qt. |
| `cli/` | Console harness: `perft`, `bench`, `selfplay`, `solve`. |
| `app/` | Qt 6 Widgets GUI. |
| `tests/` | GoogleTest unit tests. |
| `tools/` | WTHOR pipeline: dataset extraction, eval training, opening book building. |

## Installing

Prebuilt packages for both platforms are produced by every green CI run, downloadable as build
artifacts: open the repo's **Actions** tab, click the latest successful **CI** run, and scroll to
the **Artifacts** section at the bottom of that run's summary page (GitHub always wraps each
artifact in its own `.zip`, regardless of the file type inside).

### Windows

Download either `reversi-windows-installer` (an installer — Start Menu shortcut, uninstaller) or
`reversi-windows-portable` (unzip anywhere, run `bin\reversi-app.exe`). Both are **unsigned** (no
code-signing certificate) — Windows SmartScreen will show "Windows protected your PC" on first
run. This is expected, not a bug: click "More info" → "Run anyway" to proceed.

### macOS

Download `reversi-macos-dmg`, open the `.dmg` inside, and drag Reversi to Applications. The app is
ad-hoc code-signed (no paid Apple Developer ID, so it's still **not notarized**) — Gatekeeper will
refuse to open it normally on first launch. Right-click (or Control-click) the app → "Open" →
"Open" in the confirmation dialog; this is only needed once. If that still reports the app as
"damaged and can't be opened" rather than showing the override dialog (a harder Gatekeeper block
that a from-the-web quarantine flag can trigger even on an ad-hoc-signed app), clear the
quarantine flag manually instead: `xattr -cr /Applications/Reversi.app` in Terminal.

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
| M10 (done) | Release | Piece-flip animation, sound, light/dark theme, packaged installers, v1.0.0 |

\* M8's engineering deliverable is complete, correct, and CI-verified (TSan-clean, nps clears the
≥3× target with real margin); the strength leg of its exit criterion is inconclusive, not
failed — no statistically significant equal-time strength difference was detected in either
direction at the sample size tested. See the M8 paragraph above and `DEVLOG.md` for the honest
numbers and the open follow-up hypothesis.

## Benchmarks

All numbers below are measured on this dev machine (Intel i7-9700, 8 physical cores, no
hyperthreading) unless noted otherwise; see `DEVLOG.md` for the full per-milestone writeups this
section summarizes.

### Rules-core correctness (M1)

Perft from the start position (`tests/engine/perft_test.cpp`), matching published values —
verified against https://aartbik.blogspot.com/2009/02/perft-for-reversi.html, which matches
OEIS A052586:

| Depth | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| Nodes | 4 | 12 | 56 | 244 | 1,396 | 8,200 | 55,092 | 390,216 |

Also validated by differential fuzz testing against a naive reference move generator (mandatory
from M1 on, per the testing policy above), not perft counts alone.

### Search maturity (M4)

A fixed-depth self-play gate: depth-12 matured search (iterative deepening + TT + PVS + move
ordering + aspiration windows) vs. depth-10 plain alpha-beta baseline (M2) — **63–0**, a
large, unambiguous gain, deterministic (fixed depths, not a time-budget match).

### Exact endgame solver (M5)

`solveExact` (fastest-first + empty-region parity ordering, TT-backed) matches every published
score in the vendored FFO endgame test subset exactly — see `tests/data/ffo_easy.txt` for the
positions and their source, and `cli solve <board> <side>` to reproduce:

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

### Pattern evaluation + opening book (M6)

The WTHOR-trained pattern evaluation (12 pattern shapes, ridge regression per game-phase bucket)
measured **20/20** in self-play against disc-differential eval at equal search depth. Its
`.wtb` parser/replay pipeline was stress-tested against 8,874 real tournament games (2016–2019)
with zero illegal moves. The opening book built from 8,886 real tournament games (1977,
2016–2019) produced **2,734 entries** and, spot-checked against known theory, recovers the
documented main lines exactly (e.g. 1.f5 d6, the Diagonal Opening) — including a symmetry
cross-check (1.c4 canonicalizes to the same book entry as 1.f5) confirmed correct on real data,
not just unit tests.

### Multi-ProbCut (M7)

A fitted MPC model (1,500 self-played sampled positions, closed-form OLS per depth pair) measured
a modest but real edge in a 20-game equal-time self-play match: **11–9** against MPC disabled.
The first fitted configuration tested was actually a regression (too many eligible cut nodes
relative to actual cuts taken) before landing on this one — recorded in `DEVLOG.md` rather than
only reporting the number that worked.

### Lazy SMP (M8)

nps scaling by thread count (`cli bench 14 <threads>`, engine/CLI-level `searchLazySmp()`, not yet
wired into GUI gameplay; sum-of-all-threads nps, so it counts Lazy SMP's redundant parallel
exploration as real work — the honest framing for a design with no explicit work-splitting):

| Threads | 1 | 2 | 4 | 8 |
|---|---|---|---|---|
| Avg. nps | ~5.48M | ~9.27M | ~18.36M | ~34.34M |

Roughly **6.3×** at 8 threads vs. 1, clearing the ≥3× target with real margin. Strength is the
honest exception: no statistically significant strength difference was detected between Lazy SMP
and single-threaded search across three independent 20-game equal-time matches (60 games
combined) — reported as genuinely inconclusive at this sample size, not a pass, and not a
regression either. See the M8 paragraph above and `DEVLOG.md` for the full investigation.

## License

MIT — see [LICENSE](LICENSE).

## Third-Party Notices

- **Qt 6** (LGPLv3) — used under dynamic linking: the packaged builds ship Qt's DLLs/frameworks
  as separate files alongside the executable (via `qt_generate_deploy_app_script()`, see
  `app/CMakeLists.txt`), never statically linked, satisfying LGPLv3's relinking requirement.
- **GoogleTest** (BSD-3-Clause) — test-only dependency (`tests/`, `app/tests/`,
  `tools/wthor_extractor/`, `tools/mpc_fitter/`), fetched via CMake's `FetchContent`; never
  linked into the shipped app, CLI, or engine.
