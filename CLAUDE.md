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

- Don't add third-party dependencies without asking (GoogleTest via FetchContent is the
  only one so far; Qt comes from the system).
- Don't edit `.github/workflows/` casually — green CI on all jobs is a merge requirement.
- Don't commit `CMakeUserPresets.json`, `build/`, or generated artifacts.
