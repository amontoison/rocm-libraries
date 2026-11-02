#!/bin/bash
# Runs the benchmark on every DIR/*.mtx (smallest file first), lower triangle by default.
# Writes CSV to stdout; errors and timeouts go to stderr and the matrix is skipped.
#   DIR=matrices ./run_suitesparse.sh > suitesparse.csv 2> suitesparse.err
# FILLS="L U" for both triangles, TIMEOUT per run in seconds (default 1200),
# BENCH_OPTS (default "--reps-solve 20 --reps-analysis 5").
DIR="${DIR:-matrices}"
FILLS="${FILLS:-L}"
TIMEOUT="${TIMEOUT:-1200}"
O="${BENCH_OPTS:---reps-solve 20 --reps-analysis 5}"
B="$(dirname "$0")/bench_sptrsv_csc"
first=1
for f in $(ls -Sr "$DIR"/*.mtx); do
  for fill in $FILLS; do
    h="--no-header"; [ $first = 1 ] && h=""
    if out=$(timeout "$TIMEOUT" "$B" $O $h --matrix "$f" --type f64 --fill $fill); then
      echo "$out"; first=0
    else
      echo "$(basename "$f") $fill: failed or timed out" >&2
    fi
  done
done
