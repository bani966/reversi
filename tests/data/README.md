# tests/data/

- `ffo_easy.txt` — small vendored FFO endgame test suite subset. Provenance and format
  documented in the file's own `%`-comment header.
- `dev_pattern_weights.bin` — a small, fast-to-generate pattern-eval weight file (M6),
  intentionally **not** trained on any WTHOR data — sidesteps any question about
  redistributing data derived from the WTHOR database, since the raw database itself is never
  committed to this repo (see `tools/README.md`). Generated entirely from fixed-seed engine
  self-play (fully legal by construction), reproducible exactly via:
  ```
  build/msvc/tools/wthor_extractor/Release/wthor-extractor.exe synth-dataset 40 2026 dataset.txt
  tools/.venv/Scripts/python.exe tools/train_pattern_eval.py dataset.txt --buckets 1 --l2 5.0 -o tests/data/dev_pattern_weights.bin
  ```
  This file exists only to give the pattern-eval loading/lookup mechanism a fast, routine
  correctness test (`tests/engine/pattern_eval_test.cpp`) — it has no real playing strength
  (trained on 40 random-vs-random games, 1 phase bucket) and must never be used as evidence of
  how strong the real, WTHOR-trained evaluation is. That validation is a separate, manual step
  (M6's exit criterion, `DISABLED_` by default per this project's convention for expensive
  milestone-gate tests) using real, fully-trained weights that are never committed either
  (ship as a release asset).
- `dev_opening_book.bin` — the equivalent small dev/test fixture for the opening book (M6 Phase
  2), same reasoning as `dev_pattern_weights.bin`: sourced entirely from fixed-seed engine
  self-play, not real WTHOR data, so it never raises a WTHOR-redistribution question. Exists
  only to exercise `OpeningBook`'s loading/lookup mechanism
  (`tests/engine/opening_book_test.cpp`) — with only 200 self-played games behind it, it has no
  real opening-theory value and must never be used as evidence of how good the real,
  WTHOR-trained book is (that's M6 Phase 2 step 6's job, using a real, uncommitted book built
  from real WTHOR data). Reproducible exactly via:
  ```
  build/msvc/tools/wthor_extractor/Release/wthor-extractor.exe synth-book 200 2027 20 5 tests/data/dev_opening_book.bin
  ```
- `dev_mpc_model.bin` — the equivalent small dev/test fixture for Multi-ProbCut (M7). Unlike
  the two fixtures above, MPC needs no real game data of any kind to begin with (it's fit from
  self-play search-value statistics, not real tournament outcomes) - this file's smallness is
  purely about keeping the routine test suite fast, not sidestepping a data-licensing question.
  Exists only to exercise `MpcModel`'s loading/lookup mechanism (`tests/engine/mpc_test.cpp`) -
  fit from only 20 self-played games, it has no real tuning value and must never be used as
  evidence of what a well-chosen `t`/model actually buys in strength (that's M7 step 5's job,
  using a larger self-play sample). Reproducible exactly via:
  ```
  build/msvc/tools/mpc_fitter/Release/mpc-fitter.exe generate 20 2027 2 6 dataset.txt
  build/msvc/tools/mpc_fitter/Release/mpc-fitter.exe fit dataset.txt 2 4 6 tests/data/dev_mpc_model.bin
  ```
