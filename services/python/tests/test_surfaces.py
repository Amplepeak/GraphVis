"""Surface estimators: every estimator runs, and the numbers are right.

The sixteen estimators ported from GraphVis 17 are measured against a known
analytic surface, so "it produced a grid" is not mistaken for "it produced the
right grid" - which is exactly the failure this file was written after. On the
first run through, Multiquadric RBF scored RMSE 18.05 and Gaussian RBF 9.67
against a surface whose own range is 3.0. Both produced a full, plausible-looking
grid. Only measuring caught it.

Run with:  python tests/test_surfaces.py
"""
import os, sys, warnings
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
warnings.filterwarnings('ignore')

import numpy as np
from graphvis_science.analysis.surfaces import interpolate_surface, ALL_ESTIMATORS


def truth(a, b):
    return np.sin(a) * np.cos(b) + 0.3 * a


def scattered(n=400, seed=11):
    r = np.random.default_rng(seed)
    x = r.uniform(-3, 3, n); y = r.uniform(-3, 3, n)
    return x, y, truth(x, y)


def clustered(n=400, seed=11):
    """Non-uniform density - the realistic survey case, and the one that broke
    the RBF conditioning when epsilon was scaled to point spacing."""
    r = np.random.default_rng(seed)
    x = np.concatenate([r.normal(-1, 0.5, n // 2), r.uniform(-3, 3, n - n // 2)]).clip(-3, 3)
    y = np.concatenate([r.normal(1, 0.5, n // 2), r.uniform(-3, 3, n - n // 2)]).clip(-3, 3)
    return x, y, truth(x, y)


def rmse(estimator, x, y, z, resolution=60):
    g = interpolate_surface(x, y, z, resolution=resolution, estimator=estimator)
    Z = np.ma.filled(g.Z.astype(float), np.nan)
    X = np.asarray(g.X); Y = np.asarray(g.Y)
    m = np.isfinite(Z)
    if not m.any():
        return float('nan'), g
    return float(np.sqrt(np.nanmean((Z[m] - truth(X[m], Y[m])) ** 2))), g


ok = fail = 0


def check(name, condition, detail=""):
    global ok, fail
    if condition:
        print(f"  ok   {name:44s} {detail}"); ok += 1
    else:
        print(f"  FAIL {name:44s} {detail}"); fail += 1


print("=== every estimator runs and is accurate on scattered data ===")
x, y, z = scattered()
for est in ALL_ESTIMATORS:
    try:
        r, g = rmse(est, x, y, z)
        # The surface spans about 3.0; 0.2 is a generous ceiling that still
        # catches divergence by three orders of magnitude.
        check(est, np.isfinite(r) and r < 0.2, f"RMSE {r:.4f}")
    except Exception as exc:
        check(est, False, f"{type(exc).__name__}: {exc}")

print("\n=== RBF conditioning: the regression that motivated this file ===")
for est, ceiling in (("Multiquadric RBF", 0.05), ("Gaussian RBF", 0.05)):
    worst = 0.0
    for gen in (scattered, clustered):
        for n in (200, 400):
            for seed in (11, 12, 13):
                r, _ = rmse(est, *gen(n, seed))
                worst = max(worst, r if np.isfinite(r) else 1e9)
    check(f"{est} stays conditioned", worst < ceiling, f"worst RMSE {worst:.4f} over 12 datasets")

print("\n=== structured methods detect a grid and use it ===")
g1 = np.linspace(-3, 3, 25)
GX, GY = np.meshgrid(g1, g1)
sx, sy = GX.ravel(), GY.ravel()
sz = truth(sx, sy)
scores = {}
for est in ("Nearest Neighbor", "Bilinear Interpolation", "Bicubic Interpolation",
            "Rectangular B-Spline", "Monotone PCHIP (Structured)"):
    r, g = rmse(est, sx, sy, sz)
    scores[est] = r
    check(f"{est} structured", g.structured, f"RMSE {r:.5f}")
# Order matters: a higher-order method on a grid must beat nearest-neighbour,
# or the method selection is not reaching the estimator at all.
check("bicubic beats nearest on a grid",
      scores["Bicubic Interpolation"] < scores["Nearest Neighbor"],
      f"{scores['Bicubic Interpolation']:.5f} < {scores['Nearest Neighbor']:.5f}")

print("\n=== degenerate inputs are refused, not crashed on ===")
for name, args in (("all points identical", (np.zeros(20), np.zeros(20), np.zeros(20))),
                   ("collinear points", (np.linspace(0, 1, 20), np.zeros(20), np.linspace(0, 1, 20))),
                   ("fewer than 3 points", (np.array([0.0, 1.0]), np.array([0.0, 1.0]), np.array([0.0, 1.0])))):
    try:
        interpolate_surface(*args, resolution=20, estimator="Delaunay Triangulation (Linear)")
        check(name, True, "handled")
    except Exception as exc:
        # A clear exception is an acceptable answer; a crash in native Qhull is not.
        check(name, isinstance(exc, (ValueError, RuntimeError)), f"{type(exc).__name__}")

print(f"\n{ok} passed, {fail} failed")
sys.exit(1 if fail else 0)
