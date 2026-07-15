# Reversi

A Reversi/Othello desktop application: a bitboard engine in pure C++20 with alpha-beta
search, a perfect-play endgame solver, a WTHOR-trained pattern evaluation with
Multi-ProbCut, and a minimalist Qt 6 Widgets GUI with live engine analysis.

**Status: M1 — rules core complete.** Perft verified against published values through ply 8,
differential fuzzing vs. a naive reference implementation passes, all CI jobs green. Nothing
playable yet — no search or GUI board interaction.

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
| M2 | Baseline engine | Alpha-beta + simple eval; beats random and greedy 100–0 |
| M3 | GUI MVP | Playable HvH/HvAI; engine on worker thread, cancelable |
| M4 | Search maturity | Iterative deepening, TT, PVS, ordering, time control; large self-play gain vs M2 |
| M5 | Endgame solver | FFO test positions solved with correct exact scores |
| M6 | Pattern eval + opening book | WTHOR-trained eval beats hand eval at equal depth; full-DB replay passes |
| M7 | Multi-ProbCut | Measured equal-time strength gain; toggle off by default |
| M8 | Lazy SMP | ≥3× nps on 8 threads; TSan clean; no strength regression at equal time |
| M9 | Feature complete | Undo/redo, save/load, import/export, settings, AI vs AI, analysis panel |
| M10 | Release | Animations, sound, themes, installers, v1.0 |

## License

MIT — see [LICENSE](LICENSE).
