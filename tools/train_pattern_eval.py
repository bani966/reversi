#!/usr/bin/env python3
"""Ridge-regression trainer for the WTHOR-derived pattern evaluation (M6).

Reads a sparse dataset file written by tools/wthor_extractor's `extract` command (format
documented in that tool's own dataset.hpp / in each dataset file's own '%'-comment header),
fits one ridge-regularized linear model per game-phase bucket (bucketed by empty-square count),
and writes a single dependency-free binary weight file that engine/'s evaluatePattern (M6 step
6) loads at runtime.

Weight file format (little-endian, matching this project's other binary format - see
engine/src/tt.cpp's own byte-level conventions for precedent): written with Python's built-in
`struct` module, read in C++ via plain std::ifstream byte reads - no serialization library
needed on either side.

    u32 bucketCount
    for each bucket, ascending by minEmpty:
        i32 minEmpty, i32 maxEmpty   (inclusive range this bucket covers)
        f32 intercept
        for each of the 12 pattern classes, in reversi::pattern::allPatternClasses() order
        (shapeId 0..11 - NOT re-stored here; the order itself is the encoding, exactly
        matching how the dataset file's own header lists them):
            f32[3^length] weights

Usage:
    train_pattern_eval.py <dataset.txt> [<dataset2.txt> ...] --buckets B --l2 LAMBDA -o weights.bin
"""

import argparse
import re
import struct
import sys
from dataclasses import dataclass

import numpy as np
from scipy.sparse import csr_matrix
from sklearn.linear_model import Ridge

HEADER_CLASS_RE = re.compile(r"^%\s+(\d+)\s+(\S+)\s+(\d+)\s+(\d+)\s*$")


@dataclass
class PatternClassMeta:
    shape_id: int
    name: str
    length: int
    states: int


def parse_header(lines):
    """Extracts the shapeId -> (name, length, states) table from the dataset's own '%' header.
    Order matters: classes must be returned sorted by shapeId, since that ascending order IS
    the column-offset encoding both this script and the C++ loader rely on.
    """
    classes = {}
    for line in lines:
        if not line.startswith("%"):
            break
        match = HEADER_CLASS_RE.match(line)
        if match:
            shape_id, name, length, states = match.groups()
            classes[int(shape_id)] = PatternClassMeta(int(shape_id), name, int(length), int(states))
    if not classes:
        raise ValueError("dataset header contained no pattern-class lines - wrong file format?")
    ordered = [classes[i] for i in sorted(classes)]
    if [c.shape_id for c in ordered] != list(range(len(ordered))):
        raise ValueError("pattern-class shapeIds in header are not a contiguous 0..N-1 range")
    return ordered


def column_offsets(classes):
    """Cumulative column-start offset per shapeId in the (sparse) global design matrix, so
    (shapeId, canonicalIndex) maps to one global column = offsets[shapeId] + canonicalIndex.
    """
    offsets = []
    total = 0
    for cls in classes:
        offsets.append(total)
        total += cls.states
    return offsets, total


def load_dataset(paths):
    """Returns (classes, rows) where rows is a list of (target, emptyCount, [(shapeId, idx), ...])."""
    classes = None
    rows = []
    for path in paths:
        with open(path, "r", encoding="ascii") as f:
            lines = f.readlines()
        file_classes = parse_header(lines)
        if classes is None:
            classes = file_classes
        elif [(c.shape_id, c.length) for c in classes] != [(c.shape_id, c.length) for c in file_classes]:
            raise ValueError(f"{path}: pattern-class layout does not match earlier dataset file(s)")
        for line in lines:
            if line.startswith("%") or not line.strip():
                continue
            tokens = line.split()
            target = int(tokens[0])
            empty_count = int(tokens[1])
            features = []
            for tok in tokens[2:]:
                shape_id_str, idx_str = tok.split(":")
                features.append((int(shape_id_str), int(idx_str)))
            rows.append((target, empty_count, features))
    return classes, rows


def build_bucket_matrix(rows, offsets, total_columns):
    """Builds a sparse (n_positions x total_columns) design matrix and target vector from a
    list of (target, emptyCount, [(shapeId, idx), ...]) rows already filtered to one bucket.
    """
    indptr = [0]
    indices = []
    data = []
    y = []
    for target, _empty_count, features in rows:
        for shape_id, idx in features:
            indices.append(offsets[shape_id] + idx)
            data.append(1.0)
        indptr.append(len(indices))
        y.append(target)
    x = csr_matrix((data, indices, indptr), shape=(len(rows), total_columns), dtype=np.float64)
    return x, np.array(y, dtype=np.float64)


def make_buckets(rows, bucket_count):
    """Splits rows into `bucket_count` contiguous empty-count ranges covering the full 0..60
    span (not just the range present in this dataset), so a small dev dataset still produces a
    weight file whose bucket boundaries are meaningful for positions the training data never
    happened to sample - untrained buckets just get whatever ridge fits from an empty/near-
    empty design matrix (effectively all-zero weights plus a zero intercept), which is a
    correct, if unhelpful, fallback rather than an undefined gap.
    """
    edges = np.linspace(0, 61, bucket_count + 1, dtype=int)
    buckets = []
    for i in range(bucket_count):
        lo, hi = int(edges[i]), int(edges[i + 1]) - 1
        if i == bucket_count - 1:
            hi = 60
        buckets.append((lo, hi))
    bucketed_rows = [[] for _ in range(bucket_count)]
    for target, empty_count, features in rows:
        for i, (lo, hi) in enumerate(buckets):
            if lo <= empty_count <= hi:
                bucketed_rows[i].append((target, empty_count, features))
                break
    return buckets, bucketed_rows


def train(classes, rows, bucket_count, l2):
    offsets, total_columns = column_offsets(classes)
    buckets, bucketed_rows = make_buckets(rows, bucket_count)
    results = []
    for (lo, hi), bucket_data in zip(buckets, bucketed_rows):
        if not bucket_data:
            print(f"  bucket [{lo},{hi}]: 0 positions - writing all-zero weights", file=sys.stderr)
            results.append((lo, hi, 0.0, np.zeros(total_columns, dtype=np.float32)))
            continue
        x, y = build_bucket_matrix(bucket_data, offsets, total_columns)
        model = Ridge(alpha=l2, fit_intercept=True)
        model.fit(x, y)
        print(f"  bucket [{lo},{hi}]: {len(bucket_data)} positions, "
              f"train R^2={model.score(x, y):.4f}", file=sys.stderr)
        results.append((lo, hi, float(model.intercept_), model.coef_.astype(np.float32)))
    return results


def write_weight_file(path, results):
    with open(path, "wb") as f:
        f.write(struct.pack("<I", len(results)))
        for lo, hi, intercept, weights in results:
            f.write(struct.pack("<iif", lo, hi, intercept))
            f.write(weights.tobytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("datasets", nargs="+", help="one or more dataset files from wthor-extractor extract")
    parser.add_argument("--buckets", type=int, default=4, help="number of game-phase buckets (default 4)")
    parser.add_argument("--l2", type=float, default=1.0, help="ridge L2 regularization strength (default 1.0)")
    parser.add_argument("-o", "--output", required=True, help="output weight file path")
    args = parser.parse_args()

    classes, rows = load_dataset(args.datasets)
    print(f"loaded {len(rows)} positions, {len(classes)} pattern classes, "
          f"{sum(c.states for c in classes)} total features", file=sys.stderr)
    results = train(classes, rows, args.buckets, args.l2)
    write_weight_file(args.output, results)
    print(f"wrote {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
