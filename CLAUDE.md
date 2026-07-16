# Reversi — Claude Code project guide

Portfolio-grade Reversi/Othello desktop app: pure C++20 bitboard engine + Qt 6 Widgets GUI.
The roadmap with milestone exit criteria is in README.md. Work strictly one milestone at a
time; do not start features from later milestones without being asked.

## Layering rules (hard constraints)

- `engine/` is pure C++20. It must NEVER include Qt headers, link Qt, or call
  platform-specific APIs (no WinAPI anywhere in this repo — portability is a requirement).
- `cli/` depends only on `engine/`.
- `app/` is Qt 6 Widgets (no QML). It talks to the engine through plain C++
  interfaces/callbacks; Qt types must not leak into `engine/`.
- The board is 8x8 only. Bitboard convention (fixed): bit i = square with
  file = i % 8 (a..h), rank = i / 8 (1..8); bit 0 = a1, bit 63 = h8.
  Notation helpers live in `engine/include/reversi/position.hpp` — reuse them.
- A `TranspositionTable` must never be shared between `solveExact()` (the exact endgame
  solver, `engine/include/reversi/solver.hpp`) and `search()`/`searchIterative()`/
  `searchTimed()` (the heuristic search, `engine/include/reversi/search.hpp`). Both encode
  `Bound::Exact/Lower/Upper` the same way, but `search()`'s `Bound::Exact` only means "exact
  at the depth reached" (a lower bound on the true game value), while `solveExact()`'s means
  the actual final result — a table shared between the two would let the solver trust a
  heuristic-search entry as if it were exact, silently corrupting an otherwise-perfect solve.
  Give each mode its own table. (Full reasoning in `solver.hpp`'s doc comment on `tt`.)
- Any trained `EvalFn` (e.g. `PatternEvaluator`, `engine/include/reversi/pattern_eval.hpp`)
  must predict a position's final disc differential from the mover's perspective — exactly
  `terminalScore()`'s scale and meaning. `search.cpp`'s `negamax` returns `terminalScore()` for
  a mid-tree game-over leaf into the *same* alpha-beta/TT comparisons as every other node's
  `eval()` call; a differently-scaled eval there would silently break both. (Full reasoning in
  `pattern_eval.hpp`'s doc comment.)

## Build & test (local Windows, MSVC)

One-time setup: copy `CMakeUserPresets.json.example` to `CMakeUserPresets.json` and set
`CMAKE_PREFIX_PATH` to the local Qt install (e.g. `C:/Qt/6.8.3/msvc2022_64`).

- Configure: `cmake --preset dev`
- Build:     `cmake --build --preset dev`        (Release; `dev-debug` for Debug)
- Test:      `ctest --preset dev`

The Visual Studio generator is multi-config: builds and test runs must always carry a
configuration (the presets handle this — use them, don't call cmake ad hoc).
Do NOT switch local presets to Ninja: this shell has no MSVC environment (vcvars) loaded;
the VS generator works from any shell because MSBuild locates the toolchain itself.

On Linux/macOS (engine + CLI + tests, no GUI): use the `ci-linux` / `ci-macos` presets.

## WTHOR pipeline (`tools/`)

Opt-in, not part of the routine build: `cmake --preset dev -DREVERSI_BUILD_TOOLS=ON` (default
`OFF`; routine `ctest --preset dev` is identical either way — verified after every step that
added to this pipeline). Two pieces, deliberately different languages for different concerns:

- **`tools/wthor_extractor/`** (C++, links `reversi::engine` — same layering rule as `cli/`):
  parses `.wtb` files and replays their moves through the real engine, so there is exactly one
  implementation of game rules, not a second one in Python that could drift from the first (the
  exact risk class that caused M5's FFO score-sign bug). Subcommands: `verify <file.wtb>` (parse
  + replay, report illegal moves — the rules-engine stress test), `extract <file.wtb>
  <out.txt>` (also writes the sparse training dataset), `synth-dataset <games> <seed> <out.txt>`
  (generates a dataset from fixed-seed engine self-play instead of real WTHOR data — used for
  the committed dev/test fixture, see below). `.wtb` byte layout is documented in
  `tools/wthor_extractor/include/wthor_extractor/parser.hpp`; the dataset's sparse text format
  (self-describing `%`-header + `<score> <emptyCount> <shapeId:index>...` lines) is documented
  in `dataset.hpp`.
- **`tools/train_pattern_eval.py`** (Python venv at `tools/.venv/`, deps in
  `tools/requirements.txt`): reads one or more dataset files, fits ridge regression per
  game-phase bucket, writes a dependency-free binary weight file (format documented in the
  script's own docstring — plain `struct`-packed floats on the Python side, plain
  `std::ifstream` byte reads on the C++ side, no serialization library needed on either end).

**Pattern geometry (which squares make up each of the 12 pattern classes — lines, diagonals,
edge+2X, corner blocks) lives once, in `engine/include/reversi/pattern.hpp`**, and both the C++
extractor and the production `PatternEvaluator` call it — never redefine pattern shapes
anywhere else.

**Data**: the raw WTHOR database is never committed (no confirmed redistribution license from
the FFO). Real trained weights are likewise never committed — they ship as release assets.
`tests/data/dev_pattern_weights.bin` is the one exception: a small weight file trained purely
on synthetic self-play (`synth-dataset`, not real WTHOR data) so the loading/lookup mechanism
has a fast, routine, always-on test — it has no real playing strength and must never be treated
as evidence of the real eval's quality (see `tests/data/README.md`).

## Testing policy

- Every engine change lands together with unit tests in `tests/`.
- Rules-level code (move generation, flips) must additionally be validated by
  differential tests against a naive reference implementation and by perft counts
  (both arrive in M1 and are mandatory from then on).
- Run the full test suite and make it pass before declaring any task done.

## Style

- clang-format (config in `.clang-format`) is enforced by CI. Before committing:
  `git ls-files '*.cpp' '*.hpp' | xargs clang-format -i`
- Code must be warnings-clean at `/W4` (MSVC) and `-Wall -Wextra -Wpedantic` (GCC/Clang);
  CI builds with warnings-as-errors.

## Don'ts

- Don't add third-party dependencies without asking. So far: GoogleTest via FetchContent
  (C++, both `tests/` and `tools/wthor_extractor/`), Qt (comes from the system), and
  numpy/scipy/scikit-learn (Python, `tools/requirements.txt` — dev-time only, never linked
  into the shipped app/engine/cli).
- Don't edit `.github/workflows/` casually — green CI on all jobs is a merge requirement.
- Don't commit `CMakeUserPresets.json`, `build/`, or generated artifacts.
