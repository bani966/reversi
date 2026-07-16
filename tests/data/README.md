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
