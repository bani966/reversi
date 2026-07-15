# tools/

Arrives with M6: the WTHOR pipeline.

- `.wtb` parser and dataset extractor (position, final disc differential), with
  symmetry canonicalization and deduplication
- Pattern-evaluation weight trainer (ridge-regularized linear regression)
- Opening book builder
- Multi-ProbCut cut-model fitter (M7)

The raw WTHOR database is not committed; fetch/build scripts will live here and
generated weights/book ship as release assets.
