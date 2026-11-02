#!/usr/bin/env python3
# Selects square SuiteSparse matrices with a decent number of unknowns from ssstats.csv
# (https://sparse.tamu.edu/files/ssstats.csv) and prints Group/Name, one per line.
#   ./select_suitesparse.py ssstats.csv > list.txt
# Environment: N_MIN (default 100000), NNZ_MAX (default 60000000), COUNT (default 80),
# PER_GROUP (default 3, at most that many matrices per group, for variety).
import csv, os, sys

n_min = int(os.environ.get("N_MIN", 100000))
nnz_max = int(os.environ.get("NNZ_MAX", 60000000))
count = int(os.environ.get("COUNT", 80))
per_group = int(os.environ.get("PER_GROUP", 3))

rows = list(csv.reader(open(sys.argv[1])))[2:]  # first two lines: count and date
cand = []
for r in rows:
    group, name, nrows, ncols, nnz = r[0], r[1], int(r[2]), int(r[3]), int(r[4])
    if nrows == ncols and nrows >= n_min and nnz <= nnz_max:
        cand.append((nrows, nnz, group, name, r[11] if len(r) > 11 else ""))
cand.sort()

taken, per = [], {}
step = max(1.0, len(cand) / count)
i = 0.0
while len(taken) < count and int(i) < len(cand):
    j = int(i)
    while j < len(cand) and per.get(cand[j][2], 0) >= per_group:
        j += 1
    if j < len(cand) and cand[j] not in taken:
        taken.append(cand[j])
        per[cand[j][2]] = per.get(cand[j][2], 0) + 1
    i += step

for nrows, nnz, group, name, kind in taken:
    print(f"{group}/{name}")
sys.stderr.write(f"{len(cand)} candidates, {len(taken)} selected "
                 f"(n {taken[0][0]} .. {taken[-1][0]}, nnz up to {max(t[1] for t in taken)})\n")
