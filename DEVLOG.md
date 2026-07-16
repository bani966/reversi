# Dev log

Raw notes per milestone, written as work happens (or reconstructed from commit history for
M0-M6, since this file didn't exist yet). Not polished — source material for a presentation
guide later, not the guide itself.

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

## M7 — Multi-ProbCut

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
  comment and CLAUDE.md.
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

### Step 6: MoveSelectorConfig/GameController wiring

- Built: `mpcModel`/`mpcT` added to `MoveSelectorConfig`, threaded into `selectMove()`'s
  `searchTimed` branch only (never `solveExact`, which has no eval/heuristic concept at all).
  `GameController` gains `mpcModel_`, defaulted `nullptr` - same "control point exists, default
  off, no settings UI yet" pattern as `book_`. No behavior change today.
- Interesting bug caught while writing the plumbing test (not in production code - in the TEST's
  own synthetic model construction): a densely-chained "always cuts" model (every depth 1..6,
  reduction 1) produced MORE nodes with MPC than without (2708 vs 1349) at the selectMove level.
  Root cause: MPC's shallow probes always use a FULL (-inf, +inf) window internally (matching
  how training data is generated), so a probe can never cut ITSELF unless its own shallow depth
  is 0 - chaining covered depths together (deep -> shallow also covered -> shallower still
  covered...) means every probe in the chain falls through to real exploration instead of
  cutting, adding pure overhead. Fixed by using a single (deep=1, shallow=0) pair instead - the
  shallow=0 case bypasses the MPC check entirely via negamax's own depth==0 early return, so
  there's no chain to go wrong. This is the same "shallow probe overhead" lesson from step 5,
  now understood at a mechanistic level rather than just an empirical one.
- Second interesting interaction found in the same test: even with the chain issue fixed, node
  count didn't reliably DECREASE with MPC at very shallow depths - it just reliably CHANGED. An
  MPC cut inside a PVS zero-window probe returns a value sitting exactly on the window boundary,
  which can itself trigger PVS's own "the probe suggested an improvement, re-search with the
  full window" logic (search.cpp) - two independent, individually-correct techniques interacting
  in a way that isn't obviously free at shallow depth. The final plumbing test asserts node
  counts DIFFER (direction-agnostic), not that they decrease - mpc_search_test.cpp's fixed-depth
  test already proves the real, large-scale reduction at a realistic depth (7); this test's only
  job was confirming the wiring reaches search.cpp at all.
- Verified: full rebuild, full ctest (100% pass), manual GUI smoke check (human plays d3 as
  Black, AI replies legally with c3 as White) - no regression, MPC stays off by default exactly
  as designed.

M7 complete: Multi-ProbCut implemented, validated (including one real negative finding and its
fix), a conservative default t shipped with honest real numbers, and wired in off by default.

## M8 — Lazy SMP

### Step 1: SharedTranspositionTable + tests + TSan CI job

- Built: `engine/include/reversi/shared_tt.hpp` + `.cpp`, `SharedTranspositionTable` - a
  genuinely new class (not a retrofit of `TranspositionTable`, which stays completely
  untouched, still used unsynchronized by every existing single-threaded caller). Packed-atomic
  entries: `TTEntry` is exactly 16 bytes (two u64 words), so each slot stores `data` (packed
  score/depth/bound/bestMove) and `keyXorData` (`key XOR data`) as two separate
  `std::atomic<uint64_t>` words, all loads/stores at `memory_order_relaxed`. `probe()` XORs the
  two words back together and rejects the entry (safe miss) unless the result matches the
  queried key - this single check handles a genuine miss and a torn/interleaved read
  identically, with the same negligible false-positive probability as an ordinary 64-bit hash
  collision (a risk class already accepted everywhere Zobrist hashing is used in this project).
  Added a new `tsan` CI job + `ci-linux-tsan` preset (mirrors `ci-linux-sanitize`'s structure;
  a separate job since ASan/UBSan and TSan can't be combined in one binary).
- Interesting decision (the actual design fork this step hinged on): keeping
  `TranspositionTable`'s existing pointer-returning `probe()` API for the single-threaded case
  wasn't just conservative, it was closer to necessary - handing out a raw pointer into memory
  another thread can concurrently overwrite is a real time-of-check/time-of-use gap even when
  the writes themselves are individually safe. A genuinely concurrent-safe probe has to return
  a validated COPY, which is a different contract, so `SharedTranspositionTable` had to be a
  different type rather than a modification of the existing one - which turned out to make the
  "existing tests pass unmodified" requirement trivially true for this step (zero existing
  files touched at all, only new additive ones).
- Interesting test-design problem and its resolution: a real concurrent stress test can't
  reliably reproduce a torn read on demand (races are timing-dependent and may never manifest
  locally, which is exactly why TSan-in-CI exists as the real authority). To get a
  *deterministic* test of the checksum-rejection path specifically, added a small, clearly-
  labeled test-only method (`debugWriteRawWordsForTesting`) that writes raw, possibly-
  inconsistent words directly into a slot, bypassing `store()`'s normal packing - lets a test
  construct the exact byte-for-byte scenario a real torn read would produce (one real store's
  `keyXorData` mixed with a different real store's `data`) and confirm `probe()` rejects it,
  with certainty rather than luck. Same justification precedent as `solver.hpp`'s
  `oddParitySquares` being exposed specifically so an easy-to-get-wrong piece of logic gets a
  direct test independent of the rest of the class's behavior.
- The real concurrent stress test (8 threads, 20000 iterations each, hammering `store()`/
  `probe()` on one shared table using the real benchmark position set) passed cleanly, but its
  own doc comment states plainly that this is inherently a "didn't observe a failure" style
  test - it raises confidence but cannot prove the absence of races. Ran it 5 additional times
  locally for extra confidence; the actual authority is the new `tsan` CI job, verified by
  pushing (the one piece of this step's verification that can't be done on this Windows
  dev machine at all - no local GCC/Clang/TSan toolchain). CI confirmed green, including the
  new `tsan` job, on the actual push.

### Step 2: TtView adapter + searchLazySmp + Threads::Threads linkage

- Built: an internal `TtView` (type-erased, two raw function pointers + a `void*`, no
  `std::function`) so `negamax`/`windowedSearch`/`iterativeDriver` stay agnostic to which table
  type they're talking to, without templating `search.cpp` or changing either table class's own
  public API. The four existing public functions (`search`/`searchWindow`/`searchIterative`/
  `searchTimed`) keep their exact pre-M8 signatures - each just builds a `TtView` from its
  `TranspositionTable*` argument internally before calling into the (now `TtView`-based) shared
  internals. `iterativeDriver` gained a `startDepth = 1` trailing parameter (default preserves
  every pre-M8 call site) so Lazy SMP's per-thread jitter reuses the exact same iterative-
  deepening loop, not a second copy of it. New `searchLazySmp()`: `threadCount <= 1` runs
  inline on the calling thread (no `std::thread` spawned at all); `threadCount > 1` spawns all
  `threadCount` threads (including thread 0), each with its own `SearchContext` (own killers/
  history/nodes - cheap, no value in sharing) but the same `TtView` over one shared table,
  joins them, and returns thread 0's own result with `.nodes` overwritten to the sum across all
  threads (needed for nps-scaling measurement, step 4).
- Interesting bug caught mid-implementation (not in review - while writing the `startDepth`
  change itself): `iterativeDriver`'s soft-deadline check used a hardcoded `depth > 1` to mean
  "always let the first iteration start, regardless of the soft budget." With a jittered start
  depth, `depth > 1` no longer identifies "the first iteration for this thread" - a helper
  thread starting at depth 4 would have its very first iteration skipped if the soft deadline
  happened to already be past by the time it started. Fixed to `depth > startDepth`, which
  degrades to the exact original behavior when `startDepth == 1` (every pre-M8 call site).
- The whole `TtView` refactor - a real, if mechanical, change to already-tested `negamax`/
  `windowedSearch`/`iterativeDriver` internals - compiled clean on the first attempt and the
  FULL pre-existing test suite (171 tests) passed unmodified immediately after, with zero test
  file edits needed anywhere. This is the concrete confirmation of this milestone's core design
  claim (point 3 of the plan): routing single-threaded callers through one extra layer of
  indirection that resolves to the exact same `TranspositionTable::probe()`/`store()` calls
  changes nothing observable.
- New tests (`search_lazy_smp_test.cpp`): score-at-fixed-depth exactly matches plain `search()`
  across the benchmark set (the "TT never changes the score" argument extended to the
  concurrent case, using a generous time budget so the comparison isn't muddied by wall-clock
  timing), the same for `threadCount == 1` specifically, a repeatability/no-crash check across
  thread counts {2, 4, 8} under a real short time budget, and a check that `.nodes` really is
  the sum across all threads (not just thread 0's) - all green, plus a manual GUI smoke check
  confirming `app/`'s build/link still works with `engine/` now depending on `Threads::Threads`
  directly.

### Step 3: cli bench thread-count arg + DISABLED_ExitCriterionM8

- Built: `cli bench <depth> [threads]` - `threads` defaults to 1 (byte-identical to the pre-M8
  `search()` call it already made); `threads > 1` switches to `searchLazySmp()` with a generous
  (1h/2h) time budget so `bench` keeps its existing fixed-DEPTH contract rather than becoming
  time-budgeted. This is what step 4's real nps-scaling measurement will drive. New
  `DISABLED_ExitCriterionM8` test (`exit_criterion_m8_test.cpp`), same convention as every
  earlier milestone's expensive gate test: equal-TIME (not equal-depth) self-play, Lazy SMP at
  8 threads vs. single-threaded, `REVERSI_M8_GAMES` override for fast local iteration, defaults
  to 20 games.
- Interesting finding, investigated rather than dismissed: a quick 2-game manual run of the new
  test showed Lazy SMP losing 0-2, which would contradict "no strength regression at equal
  time." Rather than accept a 2-game sample or wave it off as noise, built a throwaway
  diagnostic comparing `searchTimed()` (solo) against `searchLazySmp()` (8 threads) under an
  *identical* wall-clock budget (800ms/2500ms) on the start position, 3 trials: solo
  consistently reached depth 14 (2-10M nodes), lazy8 consistently reached depth 15 (22-35M
  nodes) - confirming the mechanism is working exactly as designed, reaching a genuinely deeper
  effective search in the same time. The differing scores at those different depths (-2 vs 3)
  is expected eval variance between different search depths, not corruption - same-depth score
  identity is what `search_lazy_smp_test.cpp`'s unit tests already prove rigorously, and that
  invariant held throughout. A follow-up 6-game manual run trended to 3-3. Conclusion: small-
  sample noise around real parity, not a regression - but this is provisional; the real 20-game
  `DISABLED_ExitCriterionM8` run (step 4) is what actually settles it, and the honest result
  gets reported either way.
- Flagged improve later: `bench`'s `threads > 1` path allocates a fresh 1M-entry
  `SharedTranspositionTable` per invocation rather than letting the caller size/reuse it - fine
  for a CLI benchmarking tool, would need revisiting if `searchLazySmp` ever got a persistent-TT
  caller.

### Step 4: real nps scaling + strength validation, M8 done

- Measured on this dev machine (Intel i7-9700, 8 physical cores / 8 logical processors, no
  hyperthreading - checked directly, not assumed): `cli bench 14 <threads>` at 1/2/4/8 threads,
  3 trials each for stability (depth 12 was too fast, sub-100ms, to trust the timing). Average
  nps: 1 thread ~5.48M, 2 threads ~9.27M, 4 threads ~18.36M, 8 threads ~34.34M - roughly **6.3x**
  at 8 threads vs. 1, clearing the README's >=3x target with real margin. This is sum-of-all-
  threads nps (deliberately, per `searchLazySmp`'s own doc comment), so it counts Lazy SMP's
  redundant parallel exploration as real work, which is the honest framing for this design (no
  explicit work-splitting, unlike YBWC) - the shared TT keeps that redundancy from being total
  waste, which is the whole point of the scheme. **This half of the exit criterion is a clear,
  unambiguous pass.**
- Strength validation turned into the real investigation of this step. First 20-game
  `DISABLED_ExitCriterionM8` run (original 300ms/800ms match budget, mirrored from M7's own
  equal-time test): **lazySmp=9, single=10, draws=1**. A repeat run at the same budget:
  **lazySmp=9, single=11, draws=0** - two runs both leaning the same way was enough to investigate
  properly rather than accept a single sample at face value.
- A depth diagnostic at that same 300ms/800ms budget explained a real mechanism: completing depth
  14 from the start position takes ~520ms on this machine, already past a 300ms soft deadline, so
  neither solo nor Lazy SMP's thread 0 ever gets to *start* a depth-15 iteration - Lazy SMP's only
  route to a same-budget edge (shared-TT entries from other threads letting thread 0 blow through
  its own iterations fast enough to buy spare time for one more ply before the soft deadline)
  needs slack a 300ms soft deadline doesn't leave. This looked like a real bug in the test's
  methodology: it used a short budget picked for fast CI iteration (M7's own precedent), not
  `GameController`'s actual production budget (`kAiTimeBudget`, 800ms/2500ms) - a mismatch between
  what's being validated and how the feature would actually be used.
- Switched the test to the real production budget and re-ran the full 20 games: **lazySmp=9,
  single=11, draws=0** again. Three independent 20-game runs across two different budgets,
  **27/32/1 across 60 games total**.
- **Statistical read (caught before finalizing this writeup, not before it was first drafted -
  worth recording as its own correction)**: at n=60, the win-rate standard error is
  `sqrt(0.5*0.5/60) ≈ 6.5` percentage points. The observed gap from an even 50/50 split (Lazy SMP
  at ~45.8% including the draw as a half-point) is well inside one standard error - i.e. **not
  statistically distinguishable from noise at this sample size**. The correct conclusion is not
  "single-threaded is stronger" but "a full-game strength edge for Lazy SMP was not established,
  nor ruled out, at this sample size." An earlier draft of this entry and of README.md read the
  three same-direction runs as a real negative-leaning effect - a reasonable instinct given three
  runs agreeing, but wrong: three 20-game samples from the same underlying noise distribution will
  often agree on direction purely by chance when the true gap is this close to zero, and a formal
  standard-error check is what actually distinguishes "same direction three times" from "real
  effect," not agreement-by-eye. Corrected in both places before shipping this milestone's
  writeup, not after.
- Reconciling the inconclusive match result against step 3's depth diagnostic (which *did* show a
  clear benefit - depth 15 vs. 14, same 800ms/2500ms budget, same start position) means explaining
  why a real, confirmed per-position mechanism doesn't reliably show up as a measurable full-game
  edge. Two candidate explanations, one confirmed directly from the code, one still a hypothesis:
  - **Confirmed from the code, not inferred from behavior** - result-aggregation logic, explicitly
    checked (a good question to ask given this is exactly the bug class, "quietly returning the
    wrong thread's result," that could hide inside an underwhelming-but-not-obviously-broken
    outcome like this one): re-read `search.cpp`'s `searchLazySmp` directly rather than trusting
    the earlier summary of it. Confirmed: it unconditionally returns `results[0]` (thread 0's own
    `SearchResult` - `search.cpp`, the `finalResult = results[0]` line), never "whichever thread
    completed the deepest iteration." This is a fixed thread index by design, documented in both
    `search.cpp`'s inline comment and `search.hpp`'s doc comment on `searchLazySmp` (helper
    threads 1..N-1's own results are discarded entirely; their only contribution is the TT
    entries they leave behind for thread 0 to benefit from). Not a bug - but worth stating plainly
    since the earlier depth diagnostic (step 3) only ever observed the *returned* result's depth,
    which this confirms really is thread 0's own completed depth, not some other thread's
    substituted in silently. The direct, mechanistic consequence for the strength gap: on any
    given move, if a helper thread happens to search deeper than thread 0, that extra depth is
    thrown away entirely except for whatever it left behind in the shared TT - so the 6.3x nps
    figure measures total work done, not work that can possibly reach the move actually played.
    This is a real, code-confirmed limit on how much of Lazy SMP's raw throughput gain could ever
    convert into strength here, independent of which specific positions a game visits.
  - **Still a hypothesis, not confirmed** - that diagnostic only ever probed the single opening
    position, repeatedly, while a real game spends most of its length past the opening, in the
    mid/endgame, where fewer legal moves mean less for jittered threads to usefully diverge on in
    the first place. Confirming this would mean repeating the depth diagnostic across a spread of
    mid/endgame positions, not just the opening - left for follow-up, not chased now, per the
    time-box note.
- **Scoped out, not attempted here (time-box)**: having thread 0's own iteration always be the
  returned result is a real, if partial, explanation for the strength gap above, which raises the
  obvious follow-up question of selecting whichever thread completed the deepest iteration instead
  of always thread 0. That's a plausible **future improvement**, not a bug fix - correctness is
  unaffected either way, since `search_lazy_smp_test.cpp` already proves any completed depth's
  score exactly matches single-threaded search at that depth, regardless of which thread produced
  it. It would need its own design pass (e.g. tracking each thread's own depth/score/bestMove
  alongside a max-reduction step, still cheap since threads already join before returning) and
  isn't attempted in this milestone, per the same time-box note guiding every other stopping point
  here.
- Left the test's assertion (`lazyWins + draws >= singleWins`, i.e. "not an outright regression",
  looser than requiring a win) in place but **failing** on this machine's data, deliberately not
  loosened further to force green - a threshold tuned to always pass would be exactly as
  uninformative as the noisy binary result it currently produces. The test's own header comment
  states the standard-error read plainly so a future single failing run isn't mistaken for a
  confirmed regression.
- **Net conclusion**: M8's engineering deliverable (concurrent design, TSan-clean, correctly
  reaching greater effective search depth in isolation on the position tested, strong nps scaling,
  result-aggregation logic verified as intentional and correct) is solid. The "no strength
  regression at equal time" leg of the exit criterion is **inconclusive, not failed**: no
  statistically significant equal-time strength difference was detected between Lazy SMP and
  single-threaded search at the tested budgets, in either direction, at this sample size. Flagged
  as the milestone's one open item rather than swept under the rug or overstated as a clear
  regression - a real follow-up would need either a much larger sample (hundreds of games, real
  minutes-to-hours of runtime) or the mid/endgame-position hypothesis tested directly, both
  deliberately left for later rather than chased now, per the time-box note.
- M8 marked done in `README.md`'s roadmap table and status line - the engineering work is
  complete, tested, and CI-verified - but the status paragraph states the strength result exactly
  as measured (inconclusive at this sample size), not as a clean win or a confirmed regression.

## M9 — Feature complete (in progress)

M9 is the last major milestone before M10 polish/release, so unlike M7/M8 it carries no
time-box: full attention, done in 5 reviewed phases with a checkpoint after each (layout, game
data features, analysis panel, settings panel, visual parity pass).

### Phase 1: layout restructuring for a side panel

- Built: `MainWindow`'s board row is now a `QHBoxLayout` (`board_` at stretch 1, plus a new
  `panel_` member - a currently-empty placeholder `QWidget` with a 300px minimum width) nested
  inside the existing outer `QVBoxLayout`, in place of the old flat `addWidget(board_, 1)`.
  Default window size grew from 720x796 to 1020x796 (+300px) to fit the new column. Title bar/
  menu bar/status bar are unaffected - still full-width above and below the board row.
- Interesting: the request's premise cited a prior design confirmation from "GameController's
  author... months ago" as the basis for this change. Checked directly before writing the plan -
  `GameController.hpp`/`.cpp` has zero references to a side panel, and neither does `DEVLOG.md`
  (which didn't exist for most of the project's history anyway) or anywhere else in the repo; the
  only trace is a one-line README roadmap entry. No such confirmation exists on record. The
  underlying technical claim turned out to be true anyway, independently verified from the
  current code: `BoardWidget` enforces its square aspect entirely by itself in
  `resizeEvent()`/`recomputeBoardGeometry()` (`std::min(width(), height())` + letterboxing), with
  no `heightForWidth()` override and no explicit `QSizePolicy` - since it only ever looks at its
  own allocated rect, it doesn't care whether a `QVBoxLayout` or `QHBoxLayout` handed that rect
  to it. So the plan proceeded as specified, just on verified grounds rather than a repo
  precedent that doesn't actually exist - worth recording so this doesn't get cited as a real
  precedent later.
- Interesting (smaller): giving the empty placeholder panel real background styling (reusing
  `chrome::palette().windowBackground`, the same role the title/menu/status bars already use)
  turned out to be a correctness fix, not decoration - every pixel of the window was previously
  covered by an existing widget, so a bare `QWidget`'s unpainted default background had never
  been exercised before. Without it, the new panel would have shown Qt's default system palette
  through, reading as a rendering bug next to the app's otherwise uniformly dark chrome.
- Verified via screenshots at three window sizes (default 1020x796, wide 1400x900, narrow
  820x650) plus a real click test: the board grows/shrinks and stays square at every size (the
  specific thing this phase was asked to confirm, not assume), the panel holds its ~300px
  minimum width without stretching, and clicking a legal move (d3) still correctly plays the
  move and flips discs - confirming `GameController`'s existing signal/slot wiring is unaffected.
  At the wider size, the board's own letterbox margin and the panel are visually indistinguishable
  (same unstyled dark color, no border between them yet) - expected and out of scope until the
  phase 5 visual parity pass adds real panel styling.

### Phase 2: game data features (save/load, undo/redo, import/export)

- Built: `GameController::HistoryEntry`-backed `history_`/`historyIndex_` (one entry per fully
  pass-resolved resting position) powering `undo()`/`redo()`; `GameNotation.hpp/.cpp` (new,
  QtCore-only, zero `QWidget`/GUI coupling so `app_tests` can exercise it without a
  `QApplication`) providing pure text/JSON <-> (mode, move-list, settings) conversions for three
  independent file formats - this app's own versioned JSON save format, the standard Othello
  transcript convention (concatenated lowercase squares, no separators/pass markers), and a
  self-contained 65-char board-position snapshot (64-char row-major board + trailing side-to-move
  character). `GameController` gained `saveGame`/`loadGame`/`exportTranscript`/`importTranscript`/
  `exportPosition`/`importPosition`, all routed through `replayMoves()` (the one shared
  move-legality validation path) and `applyLoadedHistory()` (the one shared commit path,
  resetting `tt_`/`solverTt_` since a loaded/imported game is a different position tree - unlike
  undo/redo, which deliberately keep the tables since they stay within the same tree). `MainWindow`
  gained a File menu (Save/Load/Export Transcript/Import Transcript/Export Position/Import
  Position) and an Edit menu (Undo/Redo, standard-key shortcuts) - all six file operations are
  file-dialog-based with `QMessageBox::warning(lastErrorMessage())` on failure, one consistent
  interaction pattern.
- Interesting (a new file, not a modification): `GameMode` was split out of `GameController.hpp`
  into its own `GameMode.hpp` specifically so `GameNotation.hpp` (needing the enum for its
  save-file schema) doesn't have to include `GameController.hpp` itself - that header pulls in
  `BoardWidget.hpp` (`QWidget`), which would force `GameNotation`'s dependents (`app_tests`) into
  linking `Qt6::Widgets` for a module that only needs `QtCore`. A one-line enum move avoided a
  much larger, wrong-shaped test-linkage dependency.
- Interesting bug class avoided by design (same "one implementation of rules" principle as the
  WTHOR pipeline): `GameNotation` only knows TEXT/JSON *shape* - it does not validate Othello move
  legality itself, so a malformed or hand-edited file can still produce a `LoadedGame` with an
  illegal move list. `GameController::replayMoves()` is the one place that actually replays a
  loaded move list through the real engine and rejects it - `loadGame`/`importTranscript` both go
  through it, so there is exactly one legality-checking path, not two that could silently drift.
- Local build/test friction (environment, not code): both `ctest --preset dev`'s `app_tests`
  discovery step and directly launching `reversi-app.exe` failed with a DLL-not-found error
  (`STATUS_DLL_NOT_FOUND` / a native "Qt6Widgets.dll was not found" dialog) when Qt's `bin/`
  directory wasn't on `PATH` - unrelated to this phase's code, just this dev machine's shell not
  having Qt on `PATH` by default the way the IDE/build tooling does. Fixed locally by prepending
  `C:/Qt/6.8.3/msvc2022_64/bin` to `PATH` before both `ctest` and launching the app; not a project
  config change, since CMakeUserPresets.json already points at the right Qt install for the build
  itself.
- Manual GUI verification of all six file operations (this phase's own exit check, beyond the
  automated `app_tests` suite), driven by real mouse/keyboard input (not `SendMessage`/focus-
  independent posting) since Qt's native file dialogs and menus need genuine OS input to behave
  like a real user's: played a short HvH game (d3, c5), then round-tripped Export Transcript ->
  file contents exactly `"d3c5"`, Export Position -> exact 65-char string matching the on-screen
  board (`"---X----" `/`"---XX---"`/`"--OOO---"` rows + trailing `B`), Import Transcript with a
  different sequence (`"f5d6"`) actually changing the displayed board, Import Position round-
  tripping the exported position file back onto the board exactly, and a malformed-transcript
  file producing the exact `lastErrorMessage_` text in a warning dialog with the board left
  untouched underneath - all five confirmed against real file contents/screenshots, not just "the
  dialog closed without crashing."
- Interesting automation-methodology finding, worth recording since it cost real time: driving a
  native Windows Save/Open dialog via `SendKeys` after `SetForegroundWindow(dialogHwnd)` is not
  reliable - `SetForegroundWindow` silently no-ops when called from a background process without
  a preceding real input event (Windows' foreground-lock-timeout protection), so the keystrokes
  went to whatever window genuinely had OS focus instead (in one case, this very terminal
  session). The fix that actually works: a real mouse click (via `SetCursorPos` + `mouse_event`,
  genuine hardware input, not `PostMessage`) on the target control itself, which triggers Windows'
  normal click-to-activate behavior - confirmed via `GetForegroundWindow()` equality right after
  the click, every time, before sending any keystrokes. Also: typing a full absolute path directly
  into a save/open dialog's filename field is fragile (its combo-box autocomplete can silently
  substitute a different matching MRU entry, once landing in a completely wrong prior session's
  scratch folder); navigating via the dialog's breadcrumb bar instead (click its empty space to
  get an editable address field, type the target *folder* only, confirm, then type just the
  filename in the File name field) is the robust pattern and is what the verification above
  actually used.

### Phase 3: analysis panel (single-line eval/depth/nodes/PV + on-demand MultiPV)

- Built: `engine/include/reversi/analysis.hpp`/`.cpp` (new module) - `analyzeTopMoves()` (ranks up
  to N candidate moves) and `extractPrincipalVariation()` (walks a TT's stored best moves into a
  real move sequence - no PV mechanism existed anywhere in the engine before this). Two new
  additive sibling functions in `search.hpp`/`.cpp`, `searchExcludingMoves`/
  `searchTimedExcludingMoves` - `windowedSearch`/`iterativeDriver` (internal) gained a defaulted
  `excludedRootMoves` parameter that skips those squares entirely at the root; every existing
  public function (`search`/`searchWindow`/`searchIterative`/`searchTimed`/`searchLazySmp`) is
  byte-identical, unchanged. `GameController` gained a dedicated `analysisThread_`/`analysisTt_`/
  `analysisCancellation_` (mirroring the AI-search worker's shape exactly, but never sharing the
  live game's `tt_`/`solverTt_` - a plain `TranspositionTable` isn't safe for two threads at once)
  and `canAnalyze()`/`analyzePosition()`/`cancelAnalysis()`. `MainWindow`'s `panel_` (empty since
  phase 1) got its first real content: an "Analyze Position" button, status label, and a
  monospace read-only results view.
- Real bug caught by manual GUI testing, not by unit tests (worth recording in full): the first
  working version time-budgeted every MultiPV pass independently via `searchTimedExcludingMoves`.
  Manually clicking "Analyze Position" on the start position showed rank 1 (the supposed BEST
  move) scoring *worse* than ranks 2 and 3 - an impossible ranking if all three were searched at
  equal strength. Root cause: excluding moves shrinks the root's branching factor, so under a
  wall-clock budget alone, a later (narrower) pass's iterative deepening reached depth 15 in the
  same time pass 0's full branching factor only reached depth 14 in - and search scores at
  different depths simply aren't comparable (the same "differing depths give differing scores,
  that's not corruption" lesson M8's DEVLOG entry already recorded, but here it silently broke a
  correctness-shaped guarantee - "rank 1 is the best line" - instead of just being an expected
  variance). Fixed by making pass 0 the only time-budgeted call; every later pass is locked to
  EXACTLY pass 0's own achieved depth via the fixed-depth `searchExcludingMoves`, guaranteeing
  every ranked line is genuinely comparable. Added a dedicated regression test
  (`AnalyzeTopMoves.AllRanksSearchToTheSameDepth`) that encodes this invariant directly, since the
  existing "scores are non-increasing" test alone hadn't caught it (a quick fixed low depth in the
  unit test never hit the asymmetric-branching-factor conditions that surfaced it live). The
  `extractPrincipalVariation` doc comment separately documents a related but distinct pitfall: the
  shared analysis TT's entry for the root position itself reflects whichever pass ran LAST (not
  pass 0's real best move), so the PV walk deliberately seeds its first ply from pass 0's
  already-known `bestMove` rather than probing the TT for the root position directly.
- Second real bug caught by manual GUI testing: `startAiSearch()` set `aiSearchInFlight_ = true`
  *after* its own `emit statusChanged(...)` call, and `MainWindow` only re-checked `canAnalyze()`
  on `boardChanged`, not `statusChanged` - so the "Analyze Position" button stayed clickable for
  the AI's entire thinking window instead of disabling immediately. Caught by clicking a move in
  Human-vs-AI mode and screenshotting with minimal delay (deliberately racing the AI's own
  sub-second response) to catch the transient "AI thinking..." state. Fixed by setting the flag
  before the emit (a direct/same-thread Qt connection runs its slot synchronously inside `emit`,
  so ordering here is load-bearing) and wiring `updateAnalyzeButtonEnabled()` into the existing
  `statusChanged` connection too, not just `boardChanged`.
- Third, smaller gap caught the same way: cancelling an in-flight analysis by starting a new game
  mid-analysis correctly re-enabled the button (`cancelAnalysis()` resets `analysisInFlight_`) but
  left the "Analyzing..." status label stuck, since cancellation never fires `analysisFinished`.
  Fixed by having `updateAnalyzeButtonEnabled()` also reset the label to "Ready" whenever
  `canAnalyze()` is true and the label still reads "Analyzing..." - covers every path that can
  make analysis available again (completion, or any of the cancellation call sites) uniformly.
- Mid-session correction, per explicit user feedback after seeing a first working version: scores
  were originally shown mover-relative with a "positive = good for the side to move" caption.
  Changed to a fixed chess.com-style convention instead - positive always favors White, negative
  always favors Black, regardless of whose turn was analyzed - which needed `analysisFinished` to
  carry the analyzed position's `blackToMove` alongside the ranked lines, since `RankedMove::score`
  itself stays mover-relative (matching `SearchResult`'s own convention) and only the panel's
  display layer converts.
- Scope boundaries decided up front (confirmed with the user before implementation): AI-vs-AI game
  mode stays deferred to phase 4 (zero scaffolding for it exists anywhere yet, and it reads as a
  settings/mode-selection concern, not an analysis-display one); `analyzeTopMoves` never consults
  the opening book or exact endgame solver, always ranking via heuristic search regardless of
  `emptyCount()` - blending book/solver awareness into MultiPV ranking is a real future
  enhancement, not attempted here. A full chess.com-style visual reskin (rounded cards, the exact
  reference layout) stays phase 5's job; this phase's own visual cleanup (panel styling via
  `chrome::palette()`, a monospace results font, compact node-count formatting like "10.1M" instead
  of "10094917") was a light, contained pass, not a preview of phase 5.
- Verified: 203/203 automated tests green (10 new: 4 `SearchExcludingMoves`/1
  `SearchTimedExcludingMoves` engine tests, 5 `AnalyzeTopMoves`/`ExtractPrincipalVariation` tests
  including the depth-equality regression test), plus the manual GUI pass above that actually
  found the three real bugs this entry describes - a reminder that the automated suite and manual
  verification catch genuinely different failure classes here, same as M8's Lazy SMP strength
  investigation.

### Phase 4: settings dialog (AI vs AI, toggles, opening book/MPC loading, tuning)

- Built: `GameMode::AiVsAi` (both sides AI-driven - `isHumanTurnFor()` returns false
  unconditionally; `finalizeTurn()`'s existing `if (!isGameOver && !isHumanTurn()) startAiSearch();`
  already chained AI moves back-to-back with zero further changes, confirmed structurally correct
  by exploration before writing any code); a real loading path for `book_`/`mpcModel_`
  (`loadOpeningBook`/`loadMpcModel`, wrapping `OpeningBook`/`MpcModel`'s throwing constructors into
  this class's usual `bool` + `lastErrorMessage()` file-operation convention) plus
  `setOpeningBookEnabled`/`setMpcEnabled` toggles; `aiMaxSearchDepth_`/`aiTimeBudget_`/
  `exactSolverEmptyThreshold_` as real `GameController` members (previously compile-time constants,
  or in the threshold's case, an existing `MoveSelectorConfig` field `GameController` never
  populated at all) with getters/setters. `SettingsDialog` (new `QDialog` - the first one in this
  app; no prior `QTabWidget`/`QDialog` precedent existed anywhere) with Board/Opening Book/
  Multi-ProbCut/AI Strength `QGroupBox` sections, every control applying live (no OK/Apply/Cancel),
  non-modal (`show()`, not `exec()`) so the user can keep watching an AI-vs-AI game while adjusting
  settings.
- Scope confirmed with the user before implementation: settings live behind a menu action, not
  tabs inside `panel_` (kept the analysis panel's placeholder simple, no visual overhaul this
  phase); Lazy SMP thread count stayed out of scope entirely - `GameController` doesn't use
  `searchLazySmp` at all today (only single-threaded `selectMove()`/`searchTimed()`), and wiring it
  up would mean switching `tt_`/`solverTt_` to the concurrent-safe `SharedTranspositionTable`, a
  real architecture change, not a settings field. Nothing here persists across app restarts or
  saves into a game's JSON - session-level AI configuration, not per-game state, unlike
  `lastMoveHighlightEnabled_` (which already does both).
- Interesting bug caught before it ever reached the UI (found while re-reading the diff, not by a
  test or manual click): the first draft of `analyzePosition()`'s worker-thread lambda read
  `aiMaxSearchDepth_`/`aiTimeBudget_` directly off `this` at execution time on the analysis
  thread - fine as long as nothing else could change them mid-search, but the whole point of this
  phase is a live Settings dialog that can. Reading a plain (non-atomic) member from one thread
  while the GUI thread's setter could be writing it at the same moment is a real data race, not
  just an ambiguity about which value wins. Fixed by snapshotting both into local `const` copies
  before spawning the thread and capturing those instead - exactly the same reasoning
  `startAiSearch()`'s own `config` (captured by value) already relies on, just not yet applied
  to `analyzePosition()`'s two newly-configurable values.
- Menu placement revised after a first pass shipped it as its own top-level `&Settings` menu: on
  review this read as "Settings > Settings..." stuttering its own menu's name for one action, and
  added a fourth top-level menu for a single item. Moved into `&File` instead (after Import
  Position, its own separator) - one less menu-bar entry, clearer, and consistent with `&File`
  already being the catch-all for the app's other dialog-triggering actions (save/load/import/
  export).
- Real layout bug caught by the user, not by me, immediately after a follow-up visual tweak: asked
  to pad the board's left edge to visually match the panel's own internal right margin (so the
  board/panel pair reads as centered rather than the board sitting flush against the window's left
  edge), the first fix simply added a left `QHBoxLayout` margin - which exposed a strip of the
  window's `container` widget background for the first time (every pixel of `container` had
  always been covered by a child widget before this margin existed), and `container` had never
  been given explicit background styling, unlike `titleBar_`/`menuBar_`/`statusBar_`/`panel_` -
  exactly the same "unstyled QWidget shows Qt's default system palette" failure phase 1's own
  DEVLOG entry already named, reintroduced by accident via a margin instead of an uncovered
  widget. Fixed the same way phase 1 fixed it for `panel_`: `setAutoFillBackground(true)` +
  `chrome::palette().windowBackground`, not a hardcoded color - keeps this new fill in sync with
  every other chrome surface automatically if a future theme pass changes what that function
  returns, rather than becoming a fourth place a theme color could drift out of sync.
- Verified: 204/204 automated tests green (1 new: `GameNotation`'s `AiVsAi` mode round-trip, fully
  QApplication-free per this file's existing convention). Manual GUI verification (handed to the
  user partway through, per their request) covered: AI vs AI playing unattended to completion with
  the analyze button correctly disabled throughout; the last-move highlight toggle applying
  immediately; both `tests/data/dev_opening_book.bin` and `dev_mpc_model.bin` loading successfully
  with their status labels/checkboxes updating correctly and a subsequent AI move still completing
  normally with both active simultaneously.
