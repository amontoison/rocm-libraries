#!/usr/bin/env python3
# Analysis and solve times of transposed / column solve, from the CSV of run_bench.sh.
import csv, math, sys

rows = list(csv.DictReader(open(sys.argv[1])))
cols = [("case", 40), ("m", 8), ("nnz", 9), ("ana_def", 8), ("ana_push", 8), ("ana_x", 6),
        ("sol_def", 8), ("sol_push", 8), ("sol_x", 6), ("x_1", 6), ("x_100", 6), ("resid", 8)]
print("times in ms (median); *_x = transposed / column solve (> 1: column solve faster);")
print("x_1 / x_100 = speedup of analysis + 1 solve / analysis + 100 solves\n")
print(" ".join(f"{c:>{w}}" if i else f"{c:<{w}}" for i, (c, w) in enumerate(cols)))
r_ = lambda a, b: a / max(b, 1e-9)
geo = {"ana_x": [], "sol_x": [], "x_1": [], "x_100": []}
for r in rows:
    case = f"{r['matrix']} {r['fill']}{r['diag']} {r['type']}"
    if int(r["batch"]) > 1:
        case += f" b{r['batch']}{'m' if r['batch_matrix'] == '1' else ''}"
    ad, ap = float(r["default_analysis_ms"]), float(r["push_analysis_ms"])
    sd, sp = float(r["default_solve_ms"]), float(r["push_solve_ms"])
    x = {"ana_x": r_(ad, ap), "sol_x": r_(sd, sp), "x_1": r_(ad + sd, ap + sp),
         "x_100": r_(ad + 100 * sd, ap + 100 * sp)}
    vals = [case[:40], r["m"], r["nnz"], f"{ad:.3f}", f"{ap:.3f}", f"{x['ana_x']:.2f}",
            f"{sd:.3f}", f"{sp:.3f}", f"{x['sol_x']:.2f}", f"{x['x_1']:.2f}", f"{x['x_100']:.2f}",
            f"{float(r['push_residual']):.1e}"]
    print(" ".join(f"{v:>{w}}" if i else f"{v:<{w}}" for i, (v, (_, w)) in enumerate(zip(vals, cols))))
    for k, v in x.items():
        geo[k].append(math.log(v))
if rows:
    print("\ngeometric means over %d cases:" % len(rows))
    for k, v in geo.items():
        print(f"  {k:<6} {math.exp(sum(v) / len(v)):.2f}")
