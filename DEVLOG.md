# Dev log

Raw notes per milestone, written as work happens (or reconstructed from commit history for
M0-M6, since this file didn't exist yet). Not polished — source material for a presentation
guide later, not the guide itself.

## M7 — Multi-ProbCut (in progress)

### Step 1: tools/mpc_fitter `generate` subcommand

- Built: new tool (`tools/mpc_fitter`, opt-in via `REVERSI_BUILD_TOOLS`, same layering as
  `wthor_extractor`). `generate <games> <seed> <minDepth> <maxDepth> <out.txt>` self-plays
  fixed-seed games, samples one position per real move, and writes
  `search(pos, d, evaluateDiscDifferential).score` for every depth in range as one dense
  self-describing-header text line per position (defers which (shallow,deep) pairs to actually
  fit to the later `fit` step, rather than baking that choice into the extraction format).
- Interesting decision: unlike `wthor_extractor`/`train_pattern_eval.py`'s C++/Python split,
  both data generation AND the eventual regression fit stay in one C++ tool here — MPC's fit is
  single-predictor OLS with a closed-form solution, no need for numpy/scipy's machinery, and
  there's no real external data source to isolate the way WTHOR parsing needed isolating.
- Real finding (why this step exists as its own checkpoint before committing to a data-gen
  plan): a first real timing run — 10 games, depths 2..10 — hit a 2-minute wall-clock cap
  having written only ~330 positions (~0.36s/position average for all 9 depths combined, and
  that's skewed toward cheaper early-game positions before midgame branching-factor peaks show
  up). Confirms M4's own benchmark note that fixed-depth search cost grows fast with depth in
  the midgame. Adjusting step 5's real dataset generation to a smaller max depth (8, not 10)
  and a deliberately modest sample size given this measured rate, rather than assuming a nice
  round number would be cheap enough.

### Step 2: tools/mpc_fitter `fit` subcommand

- Built: `readDataset` (parses the header/lines `generate` writes), `fitPairs` (closed-form
  single-predictor OLS + sample-stddev residual sigma per (shallow,deep) pair, skipping
  missing-depth/too-few-samples/zero-variance pairs with a warning instead of erroring), and
  `writeModelFile` (the binary MpcModel format engine/mpc.hpp will read in step 3). `fit
  <dataset.txt> <reduction> <minDeep> <maxDeep> <out.bin>` ties it together.
- Interesting test-design note (not a bug, a discipline point): the "noisy data" fit test
  computes its expected a/b/sigma with the OLS formula written out a second time, independently,
  directly in the test file - not by calling fitPairs and checking it against itself. Same
  independent-recomputation discipline already used for every binary reader in this project
  (OpeningBook, PatternEvaluator), applied here to a piece of MATH rather than a file format.
- Smoke-tested generate+fit end to end on a tiny real sample (3 games, depths 2-6): recovered
  b in [0.86, 0.89] and sigma of a few discs across three depth pairs - directionally sane
  (shallower search should predict deeper search fairly well, deep search isn't literally
  identical to shallow so sigma isn't ~0) before trusting the mechanism for the real step-5 run.

### Step 3: engine/mpc.hpp MpcModel reader

- Built: `MpcModel` (mirrors `PatternEvaluator`/`OpeningBook`'s shape - load a file at
  construction, expose a lookup method), `MpcConfig` (the `model == nullptr` toggle, matching
  the same null-pointer-disables-it pattern used everywhere else in this codebase). Linear scan
  for `lookup()`, not binary search - deliberately, since a real model holds maybe 5-7 pairs,
  not the thousands `OpeningBook` needs to binary-search efficiently.
- Interesting decision (a new documented hard constraint, same risk class as M6's eval/
  terminalScore commensurability rule): a fitted `MpcModel`'s coefficients are only valid for
  the exact eval function its training data was generated with
  (`evaluateDiscDifferential` today) - using it with any other eval without regenerating data
  and refitting would silently miscalibrate the cut margins. Documented in `mpc.hpp`'s doc
  comment now; CLAUDE.md gets the same note once the whole milestone lands.
- Small dev/test fixture (`tests/data/dev_mpc_model.bin`) generated from only 20 self-played
  games - unlike M6's dev fixtures, this one isn't sidestepping a data-licensing question (MPC
  needs no real game data of any kind to begin with), it's purely about keeping the routine
  test suite fast.

### Step 4: search.hpp/.cpp MPC integration

- Built: `MpcConfig` threaded through `search()`/`searchWindow()`/`searchIterative()`/
  `searchTimed()` as a new trailing defaulted parameter (search.hpp's public API DOES change
  here, unlike M6 Phase 2's opening book, which deliberately stayed outside it - MPC has to run
  inside the recursive tree at arbitrary internal nodes, it can't be a root-only wrapper). The
  cut-check lives in `negamax`, right after the existing TT-probe block: a full-window shallow
  probe of the SAME position, predicted deep value compared against the current window by
  `t * sigma`, cutting (returning the window bound, never storing into the TT) on a confident
  clear.
- Interesting architectural note (worth its own callout since it's the opposite conclusion from
  M6 Phase 2): `negamax` is structurally never called for the root - `windowedSearch` iterates
  root moves directly - so the MPC check needed no extra `ply > 0` guard at all; it falls out of
  the existing control flow for free, and the root always computes a real move regardless of
  whether MPC is enabled.
- Interesting test-design finding (confirms a detail flagged in mpc.hpp's own doc comment): a
  model that's loaded and engaged but calibrated to NEVER trigger (astronomically large sigma)
  still costs MORE nodes than MPC fully off, because every eligible node still pays for its
  shallow probe even when the probe never leads to a cut. Verified directly with
  `EXPECT_GE(with.nodes, without.nodes)` - `mpcModel == nullptr` is the only genuinely free
  off-switch; a huge `t`/sigma is not equivalent to off, just very conservative.
- The "always cuts" synthetic-model test (huge intercept, tiny sigma, reduction-2 pairs chained
  down to an uncovered depth so recursion always terminates) confirms the mechanism is actually
  reachable: drastically fewer nodes across the benchmark set, still a legal, `completed` result
  every time - not just "doesn't crash," a real positive control for the cut path itself.

### Step 5: real data gen/fit + strength validation

- Generated a real dataset (25 self-played games, seed 4242, depths 2-8 -> 1500 sampled
  positions, ~40s) and fit a first real model: reduction=2 covering deep depths 4-8 (5 pairs),
  sigma 3.0-4.2 discs.
- Real negative finding (exactly the risk class the user flagged up front - "a bug doesn't
  produce an obviously wrong score, it produces a search that's silently, gradually weaker"):
  that first model was a clear NET LOSS in equal-time self-play at every t tried - 4/20 (t=2.0),
  2/8 (t=6.0) - and increasing t by 4x barely moved move-agreement (15-16/21 across t=2..8),
  which was the tell that this wasn't "not enough safety margin" (a bigger t should fix that)
  but a cost/benefit problem: with reduction=2, MPC gets a shallow-probe opportunity at nearly
  every internal node in the 4-8 depth range, and the "never triggers still costs extra nodes"
  property proven in step 4's unit test was dominating - paying for probes without enough real
  cuts to recoup the cost, so the achievable search depth under a fixed time budget actually
  went DOWN. All of this happened with the on/off-equivalence and always/never-cuts unit tests
  still fully green - correct code, bad cost/benefit tradeoff for that specific configuration,
  a genuinely different failure mode than a score bug.
- Fix (a configuration change, not new engineering, so still within the time-box): refit with
  reduction=4 covering only the deeper pairs (6, 7, 8) - fewer eligible nodes, but each shallow
  probe is cheaper relative to the subtree it replaces. Recovered a real edge: 5/8 (t=2.5,
  quick check), confirmed 11/9 in a 20-game equal-time match, 16/21 move agreement against a
  fixed depth-11 ground truth. Modest, not dramatic - expected for a linear model fit on one
  global (a, b, sigma) per depth pair with no game-phase bucketing (unlike M6's PatternEvaluator,
  which explicitly buckets by empty-square count for the same underlying reason: predicting
  from an opening position is a different problem than predicting from a near-endgame one).
- Shipped: `kDefaultMpcT = 2.5`, documented in mpc.hpp alongside the real numbers and the
  reduction=2 negative finding, so a future re-tune starts from "reduction=4, deeper-only" and
  doesn't have to rediscover the cost/benefit lesson from scratch.
- Flagged improve later: per-phase (or per-empty-count) bucketed MPC coefficients, mirroring
  PatternEvaluator's bucketing, would likely both widen safely-coverable depth range back toward
  reduction=2's broader coverage AND improve the edge size - explicitly out of scope for this
  milestone's time-box (a real engineering addition, not a config/t change).

## M0 — Scaffolding, CI

- Built: repo layout (engine/cli/app/tests/tools), CMake presets, CI on Windows/macOS/Linux.
- Interesting: CI needed a follow-up pin (windows-2022 runner, Qt 6.9 on macOS) after a
  GitHub Actions image migration (VS2026) and an AGL removal broke the original config.

## M1 — Rules core

- Built: bitboard Position/move generation, a naive 2D-array reference implementation,
  differential fuzz test (3000 seeded self-play games, bitboard vs naive, compared at every
  ply), perft.
- Interesting: differential testing against an intentionally-slow-but-obviously-correct
  reference was the actual correctness backbone here, not the perft numbers alone — perft
  matching a published count doesn't by itself prove move generation is right at every
  position, only that node counts add up.

## M2 — Baseline engine

- Built: swappable EvalFn interface, fixed-depth negamax with fail-soft alpha-beta, baseline
  players (random/greedy), self-play loop, CLI bench/selfplay, exit-criterion tests (100-0 vs
  random and greedy).
- Interesting: alpha-beta pruning was cross-checked against an unpruned reference search
  before trusting it — caught a window bug pre-merge instead of shipping a silently-wrong
  pruned score.
- Improve later (resolved same milestone): the exit-criterion smoke test originally ran at
  depth 10/100 games (~121s per ctest run). Cut to depth 6/4 games (~3.3s) after verifying that
  exact combination still cleanly wins 4/4 vs random and greedy — the DISABLED_ full-strength
  version was kept for the real exit-criterion number, the routine version just needs to catch
  a regression, not reproduce the milestone's statistic.

## M3 — GUI MVP

- Built: Qt Widgets BoardWidget + MainWindow, GameController wiring HvH and HvAI, AI search on
  a worker thread (std::thread, not QThread), CancellationToken-based cancellation, frameless
  window with DWM corner rounding, chess.com-styled visual pass, toggleable last-move highlight
  (default off — no settings UI yet, a pattern repeated later for the opening book toggle).
- Interesting: CancellationToken is a hand-rolled atomic flag, not std::stop_token/jthread —
  at the time, that required -fexperimental-library in libc++ and Apple's shipped libc++ had
  historically lagged upstream on it; too much portability risk for engine/'s public API.
  Also: DWM corner rounding (DwmSetWindowAttribute + DWMWCP_ROUND) sidesteps
  Qt::WA_TranslucentBackground's documented rendering bugs at fractional DPI / mixed-DPI
  monitor drags entirely, rather than working around them.
- Interesting bug: two "round 1" visual changes were genuinely applied and compiled but looked
  like no-ops — one was a real color change too subtle at similar luminance to read as
  different against the felt background; the other was Qt's default Windows style delegating
  QMenuBar/QStatusBar chrome painting to native Win32 theme APIs, which ignore QSS properties
  entirely (fixed by forcing the Fusion style app-wide).
- Flagged improve later: automated GUI click-through for the AI/cancellation paths was
  unreliable that session (SetForegroundWindow blocked for a background-launched process,
  clicks landing on the wrong window) — manual verification was deferred. Resolved in later
  milestones via a PrintWindow-based screenshot + SendMessage-click script instead of
  SetForegroundWindow-dependent automation.

## M4 — Search maturity

- Built: iterative deepening, Zobrist hashing + transposition table, move ordering (TT move,
  killers/history, corner bias), PVS, aspiration windows, soft/hard wall-clock time budgets.
- Interesting: the exit-criterion test went through two wrong designs before landing. First
  attempt calibrated an "equal wall-clock time" match by timing search on Position::start() —
  which turned out to be the LOWEST-branching-factor position in the whole game (4 legal
  moves), so the calibration badly underestimated the baseline's real per-move cost in a
  bushier midgame; matured lost 9/20 purely from that miscalibration, not a real regression.
  Second attempt shared one TranspositionTable across a whole 10-game match, which quietly
  degraded matured's node throughput as the table filled with unrelated games' entries (never
  wrong answers — proven elsewhere — just fewer useful hits under the depth-preferred
  replacement policy). Final design: a deterministic fixed-depth gate (matured depth 12 vs
  baseline depth 10, fresh TT, no wall clock) as the actual CI gate — 63-0 — plus a
  DISABLED_ wall-clock match using each milestone's real shipped config, reported not
  hard-gated since wall-clock timing isn't deterministic run to run.
- Flagged improve later: both M2 and M4's search are uniform brute-force to a fixed real-move
  depth, no null-move/LMR/other selective-depth extensions — noted as worth adding once a
  stronger eval (M6) exists to trust, since selective deepening pairs naturally with that.

## M5 — Endgame solver

- Built: solveExact (perfect-play negamax, no eval, terminalScore-only leaves), fastest-first +
  empty-region-parity move ordering, its own TranspositionTable usage, FFO test suite
  integration, CLI `solve` subcommand.
- Interesting bug (the flagship one): the vendored FFO test data's score field is fixed to
  Black's perspective, not mover-relative. Every White-to-move test line initially failed with
  the exact sign-flipped score; Black-to-move lines passed immediately because mover-relative
  and Black-relative coincide when Black is the mover — which is exactly what let the bug hide
  until a White-to-move line was actually checked. Fixed by negating White-to-move scores at
  parse time. This became the template risk-class referenced repeatedly in later milestones
  (M6's pattern-eval scale commensurability, M6 Phase 2's canonicalize() build/lookup symmetry
  direction) — "a silent perspective/direction flip that half the test cases can't catch."
- Interesting decision: solveExact() and search()/searchTimed() must never share a
  TranspositionTable — both encode Bound::Exact/Lower/Upper identically, but search()'s Exact
  only means "exact at the depth reached" (a lower bound on the true value), while solveExact's
  Exact means the actual final result. A shared table would let the solver trust a heuristic
  entry as if it were exact. Written up as a hard constraint in CLAUDE.md, and enforced again
  at the API level in M6 Phase 2's selectMove() (two separate tt parameters).
- Flagged improve later: `kExactSolverEmptyThreshold` (12) is deliberately conservative
  relative to the measured benchmark data (solves comfortably faster than that up to ~16-17
  empties) — left as-is pending an explicit interactivity/strength tradeoff decision for GUI
  wiring, which M5 didn't include (M6 Phase 2 later wired the dispatch itself, but didn't
  revisit the threshold value). Also: whether the vendored FFO subset's positions correspond
  1:1 with FFO's own canonical #1-79 numbering is unconfirmed (source groups by empty-count
  instead).

## M6 Phase 1 — Pattern evaluation

- Built: WTHOR .wtb parser + replay (tools/wthor_extractor, links reversi::engine — one
  implementation of game rules, not a second Python one), 12 pattern classes with
  symmetry-shared weight tables, sparse dataset extraction, Python ridge-regression trainer,
  PatternEvaluator, correctness tests.
- Interesting bug (caught before implementing, not after): the approved plan claimed all 8
  rows + 8 columns share one weight table ("a row and a column are related by 90-degree
  rotation"). Deriving the symmetry orbits by hand instead of trusting that claim showed it's
  only true for the 4 OUTER lines — the full 8-element symmetry group maps row 1 to exactly
  {row1, row8, col-a, col-h}, and no rotation/reflection reaches an inner row like row4.
  Treating all 16 lines as one class would have silently pooled squares with very different
  Othello meaning (edge stability vs. central mobility) into one weight table — wrong, not
  just inefficient. Corrected to 4 line classes by edge distance; diagonals didn't have this
  problem (length alone determines the orbit there).
- Interesting decision: any trained EvalFn must predict the position's final disc differential
  from the mover's perspective, exactly matching terminalScore()'s scale — because negamax
  feeds terminalScore() (mid-tree game-over leaves) and eval() (regular leaves) into the same
  alpha-beta/TT comparisons; a differently-scaled eval there breaks both silently. Same risk
  class as M5's FFO bug, caught by design this time instead of by a failing test.
- Interesting decision: raw WTHOR data and real trained weights are never committed (no
  confirmed FFO redistribution license, checked and found no explicit terms). Sidestepped for
  committed test fixtures by generating a synth-dataset subcommand — dev weights trained purely
  on fixed-seed engine self-play, explicitly documented as having no real playing strength.
- Measured exit criterion: 20/20 self-play vs disc-differential at equal depth (6), weights
  trained on 4 real WTHOR years (2016-2019, 8,874 games, 530,652 positions). Per-bucket train
  R² ranged 0.95 (near-endgame) down to 0.03 (opening) — expected, and the 20-0 result held up
  despite the weak opening-bucket fit.
- Flagged improve later: canonicalization doesn't fold a line's forward/reversed readings into
  the same ternary index (a further parameter-reduction some engines use) — deliberately out
  of scope, a valid future refinement.

## M6 Phase 2 — Opening book

- Built: position/move canonicalization (Symmetry::inverse, canonicalize), build-book/synth-book
  subcommands, OpeningBook reader, selectMove() (book → solveExact → searchTimed dispatch),
  GameController migration.
- Interesting gap found (not a bug — a missing piece): while confirming where the book check
  should plug in, discovered there was no shared endgame-vs-search dispatch anywhere in the
  codebase at all — GameController called searchTimed() directly and unconditionally; cli/'s
  only solveExact reference was the standalone `solve` subcommand, unrelated to any gameplay
  loop. M5's plan had explicitly deferred this wiring and nothing since had picked it up. Built
  move_selector.hpp's selectMove() to be that one composition point instead of adding the book
  check as a third independently-duplicated decision on top of an already-duplicated one.
- Interesting bug-class (same shape as M5's, different mechanism): canonicalize()'s build/lookup
  directionality — build time stores applySymmetry(symmetryUsed, move) against the canonical
  position; lookup time must recover the real move via applySymmetry(inverse(symmetryUsed),
  storedMove), the OPPOSITE direction, not the same symmetry applied twice. Got its own
  dedicated hand-verified test (not just a round-trip check) at three separate layers
  (canonicalize itself, book building, book lookup) specifically because a round-trip test
  can't distinguish "correct inverse" from "same symmetry twice" when they happen to coincide.
- Validation: built a real book from 8,886 real WTHOR games (1977, 2016-2019) → 2,734 entries.
  Spot-checked against known theory — recovers documented lines (1.f5 d6 Diagonal Opening,
  1.f5 f6 e6 Perpendicular Opening). Notably, 1.c4 (a different real recorded opening,
  symmetric-equivalent to 1.f5) independently canonicalized to and recovered the exact same
  book entry (same gameCount/outcomeSum) as 1.f5 — real cross-check that the symmetry logic
  holds on real, independently-replayed data, not just synthetic unit-test positions.
- Flagged improve later: OpeningBook's toggle in GameController (`book_`) has no loading path
  yet — no settings UI exists to point it at a real book file (that's M9). Structural wiring
  only; the book stays off by default today.
