# tools/

Opt-in build (`REVERSI_BUILD_TOOLS`, default `OFF`) — not part of the routine developer/CI
build; only needed when regenerating trained artifacts. Two pipelines:

- **`wthor_extractor/`** (M6): `.wtb` parser and dataset extractor (position, final disc
  differential, symmetry canonicalization/deduplication), pattern-evaluation weight trainer
  (`train_pattern_eval.py`, ridge-regularized linear regression), and opening book builder
  (`build-book`/`synth-book`).
- **`mpc_fitter/`** (M7): Multi-ProbCut data generation (`generate`, self-play only — no
  external data needed) and cut-model fitter (`fit`, closed-form OLS).

The raw WTHOR database is not committed; generated weights/books/MPC models ship as release
assets, never committed either. See `CLAUDE.md`'s "WTHOR pipeline" and "Multi-ProbCut" sections
for subcommand details and file formats.
