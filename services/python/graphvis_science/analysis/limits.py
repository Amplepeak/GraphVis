# =========================================================================
# analysis.py — automated curve / pattern recognition and limit detection.
#
# Pure NumPy/SciPy; no Qt, no matplotlib. Safe to call from worker threads.
#
#   detect_plateau        steady-state plateau (trailing low-slope segment)
#   fit_asymptote         asymptotic limit from an exponential-saturation fit
#   confidence_envelope   upper/lower operational bounds (binned quantiles
#                         for point clouds, residual-based for single curves)
#   detect_knee           optimal trade-off / knee point (Kneedle chord method)
#   pareto_bounds         upper (Pareto) and lower (anti-Pareto) frontiers
#   analyse_limits        one call that runs whatever the options enable and
#                         returns a LimitReport of drawable features.
# =========================================================================
from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np
from scipy.ndimage import gaussian_filter1d
from scipy.optimize import curve_fit
from scipy.signal import savgol_filter

from graphvis_science.analysis.electro import pareto_frontier


@dataclass
class LimitFeature:
    kind: str                      # 'hline' | 'vline' | 'band' | 'point' | 'curve'
    label: str
    value: float | None = None     # hline y / vline x
    x: np.ndarray | None = None    # band/curve support, or point x
    lo: np.ndarray | None = None   # band lower edge
    hi: np.ndarray | None = None   # band upper edge
    y: np.ndarray | float | None = None   # curve y / point y
    group: str = ""                # 'plateau' | 'asymptote' | 'confidence' | 'knee' | 'pareto'


@dataclass
class LimitReport:
    features: list[LimitFeature] = field(default_factory=list)
    summary: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)

    def text(self) -> str:
        return "\n".join(self.summary) if self.summary else "No limits detected."


# ------------------------------------------------------------------ helpers
def _clean(x, y):
    x = np.asarray(x, float).ravel()
    y = np.asarray(y, float).ravel()
    n = min(x.size, y.size)
    x, y = x[:n], y[:n]
    ok = np.isfinite(x) & np.isfinite(y)
    return x[ok], y[ok]


def _binned_curve(x, y, bins=60):
    """Collapse a point cloud (many y per x) into a mean curve on bin centres.
    Already-functional data (≤2 points per bin on average) is passed through
    sorted instead."""
    x, y = _clean(x, y)
    if x.size == 0:
        return x, y
    order = np.argsort(x, kind="stable")
    x, y = x[order], y[order]
    if x.size <= 2 * bins or np.ptp(x) == 0:
        return x, y
    edges = np.linspace(x.min(), x.max(), bins + 1)
    idx = np.clip(np.searchsorted(edges, x, side="right") - 1, 0, bins - 1)
    counts = np.bincount(idx, minlength=bins)
    sums = np.bincount(idx, weights=y, minlength=bins)
    good = counts > 0
    centres = 0.5 * (edges[:-1] + edges[1:])
    return centres[good], sums[good] / counts[good]


def _smooth(y, window_frac=0.15):
    n = y.size
    if n < 7:
        return y.copy()
    win = max(5, int(round(n * window_frac)) | 1)
    win = min(win, n if n % 2 == 1 else n - 1)
    try:
        return savgol_filter(y, win, polyorder=2)
    except Exception:
        return gaussian_filter1d(y, sigma=max(win / 4.0, 1.0))


def _r2(y, pred):
    ss_res = float(np.sum((y - pred) ** 2))
    ss_tot = float(np.sum((y - y.mean()) ** 2))
    return 1.0 - ss_res / max(ss_tot, 1e-12)


# -------------------------------------------------------------- detectors
def detect_plateau(x, y, slope_tol=0.03, min_frac=0.12) -> dict:
    """Find a trailing steady-state plateau.

    The curve is normalised to the unit square, smoothed, and the longest
    *trailing* run whose |dy/dx| stays below `slope_tol` (in normalised
    units) is taken as the plateau. It must span at least `min_frac` of the
    x-range to count.
    """
    xc, yc = _binned_curve(x, y)
    if xc.size < 8 or np.ptp(xc) == 0 or np.ptp(yc) == 0:
        return {"found": False}
    xn = (xc - xc.min()) / np.ptp(xc)
    yn = (yc - yc.min()) / np.ptp(yc)
    ys = _smooth(yn)
    # noise-aware tolerance: a plateau is the trailing run whose smoothed
    # level stays within `slope_tol` (fraction of the y-range) of the end level
    noise = float(np.std(yn - ys)) if yn.size > 5 else 0.0
    tol = max(float(slope_tol), 2.0 * noise)
    n_tail = max(3, int(round(0.1 * ys.size)))
    end_level = float(np.median(ys[-n_tail:]))
    flat = np.abs(ys - end_level) <= tol
    i = flat.size - 1
    while i >= 0 and flat[i]:
        i -= 1
    start = i + 1
    if start >= flat.size - 2:
        return {"found": False}
    span = xn[-1] - xn[start]
    if span < min_frac:
        return {"found": False}
    # reject if the "plateau" is really the whole curve (no rise before it)
    if start == 0 and np.ptp(ys) <= 3 * tol:
        return {"found": False}
    seg_y = yc[start:]
    level = float(np.median(seg_y))
    spread = float(np.std(seg_y))
    return {
        "found": True,
        "level": level,
        "x_start": float(xc[start]),
        "x_end": float(xc[-1]),
        "spread": spread,
        "n_points": int(seg_y.size),
        "frac_of_range": float(span),
    }


def _exp_sat(x, a, b, k):
    return a - b * np.exp(-k * x)


def _michaelis(x, a, b):
    return a * x / (b + x)


def fit_asymptote(x, y) -> dict:
    """Estimate the asymptotic limit of a saturating response.

    Tries an exponential-saturation model y = a − b·exp(−k·x) first, then a
    Michaelis–Menten/Monod-type model y = a·x/(b + x). Returns the model with
    the higher R²; `found` is False if neither fits better than R² = 0.5.
    """
    xc, yc = _binned_curve(x, y)
    if xc.size < 6 or np.ptp(xc) == 0 or np.ptp(yc) == 0:
        return {"found": False}
    x0 = xc.min()
    xs = xc - x0
    span = float(np.ptp(xs)) or 1.0
    candidates = []
    increasing = yc[-1] >= yc[0]
    a0 = float(yc.max() if increasing else yc.min())
    b0 = float(a0 - yc[0])
    try:
        popt, _ = curve_fit(_exp_sat, xs, yc, p0=[a0, b0, 3.0 / span],
                            bounds=([-np.inf, -np.inf, 1e-9 / span], [np.inf, np.inf, 1e3 / span]),
                            maxfev=4000)
        pred = _exp_sat(xs, *popt)
        candidates.append(("exp_sat", float(popt[0]), _r2(yc, pred), pred, popt))
    except Exception:
        # a fit that does not converge is not a candidate; the others still stand
        pass
    if increasing and np.all(yc >= 0):
        try:
            popt, _ = curve_fit(_michaelis, xs + 1e-12, yc, p0=[a0, span / 4.0],
                                bounds=([1e-12, 1e-12], [np.inf, np.inf]), maxfev=4000)
            pred = _michaelis(xs + 1e-12, *popt)
            candidates.append(("michaelis", float(popt[0]), _r2(yc, pred), pred, popt))
        except Exception:
            # as above - a Michaelis fit that fails is simply not offered
            pass
    if not candidates:
        return {"found": False}
    model, a, r2, pred, popt = max(candidates, key=lambda c: c[2])
    if not np.isfinite(a) or r2 < 0.5:
        return {"found": False, "r2": float(r2)}
    # x at which the fit reaches 95 % of the way to the asymptote
    x95 = None
    if model == "exp_sat" and popt[2] > 0 and popt[1] != 0:
        x95 = float(x0 + np.log(20.0) / popt[2])
    elif model == "michaelis":
        x95 = float(x0 + 19.0 * popt[1])
    return {"found": True, "asymptote": float(a), "r2": float(r2), "model": model,
            "x": xc, "fit": pred, "x95": x95}


def confidence_envelope(x, y, level=95.0, bins=40, smooth_sigma=1.0, min_neighbors=3, cancel_check=None) -> dict:
    """Upper / lower operational bounds.

    * Point clouds (many y per x) → binned percentile envelope at `level` %
      (e.g. 5th–95th for 90 %), lightly smoothed.
    * Functional data (≈ one y per x) → a smoothed trend ± z·σ of residuals,
      i.e. a prediction-style band.
    """
    x, y = _clean(x, y)
    if x.size < 10 or np.ptp(x) == 0:
        return {"found": False}
    level = float(np.clip(level, 50.0, 99.9))
    q_lo, q_hi = (100.0 - level) / 2.0, 100.0 - (100.0 - level) / 2.0
    order = np.argsort(x, kind="stable")
    x, y = x[order], y[order]
    per_bin = x.size / float(bins)
    if per_bin >= 4:
        edges = np.linspace(x.min(), x.max(), bins + 1)
        idx = np.clip(np.searchsorted(edges, x, side="right") - 1, 0, bins - 1)
        centres = 0.5 * (edges[:-1] + edges[1:])
        lo, hi, med = [], [], []
        keep = []
        min_neighbors = max(2, int(min_neighbors or 2))
        for b in range(bins):
            if cancel_check is not None and b % 8 == 0 and cancel_check():
                raise RuntimeError("Cancelled")
            sel = y[idx == b]
            if sel.size >= min_neighbors:
                lo.append(np.percentile(sel, q_lo))
                hi.append(np.percentile(sel, q_hi))
                med.append(np.median(sel))
                keep.append(centres[b])
        if len(keep) < 4:
            return {"found": False}
        lo, hi, med, grid = map(np.asarray, (lo, hi, med, keep))
        if smooth_sigma > 0 and grid.size > 5:
            lo, hi, med = (gaussian_filter1d(v, smooth_sigma) for v in (lo, hi, med))
        return {"found": True, "grid": grid, "lo": lo, "hi": hi, "median": med,
                "level": level, "method": "binned_quantiles"}
    # functional data
    from scipy.stats import norm
    trend = _smooth(y)
    resid = y - trend
    sigma = float(np.std(resid, ddof=1)) if resid.size > 2 else 0.0
    z = float(norm.ppf(q_hi / 100.0))
    return {"found": True, "grid": x, "lo": trend - z * sigma, "hi": trend + z * sigma,
            "median": trend, "level": level, "method": "residual_band", "sigma": sigma}


def detect_knee(x, y) -> dict:
    """Kneedle-style knee/elbow: point of maximum perpendicular distance from
    the chord joining the ends of the normalised, monotone-sorted curve."""
    xc, yc = _binned_curve(x, y)
    if xc.size < 5 or np.ptp(xc) == 0 or np.ptp(yc) == 0:
        return {"found": False}
    xn = (xc - xc.min()) / np.ptp(xc)
    yn = (yc - yc.min()) / np.ptp(yc)
    ys = _smooth(yn) if yn.size >= 7 else yn
    p0, p1 = np.array([xn[0], ys[0]]), np.array([xn[-1], ys[-1]])
    chord = p1 - p0
    norm_c = np.linalg.norm(chord)
    if norm_c == 0:
        return {"found": False}
    pts = np.column_stack([xn, ys]) - p0
    dist = np.abs(pts[:, 0] * chord[1] - pts[:, 1] * chord[0]) / norm_c
    i = int(np.argmax(dist))
    if dist[i] < 0.02 or i in (0, xn.size - 1):
        return {"found": False}
    return {"found": True, "x": float(xc[i]), "y": float(yc[i]), "strength": float(dist[i])}


def pareto_bounds(x, y, maximise_y=True, n_boot=200, cancel_check=None) -> dict:
    """Upper (Pareto) frontier and lower (anti-Pareto) frontier of a cloud."""
    x, y = _clean(x, y)
    if x.size < 5:
        return {"found": False}
    upper = pareto_frontier(x, y, maximise_y=maximise_y, n_boot=max(0, int(n_boot)), cancel_check=cancel_check)
    if cancel_check is not None and cancel_check():
        raise RuntimeError("Cancelled")
    lower = pareto_frontier(x, y, maximise_y=not maximise_y, n_boot=0, cancel_check=cancel_check)
    if not upper or not lower:
        return {"found": False}
    return {"found": True, "x": upper["x"], "upper": upper["env"], "lower": lower["env"],
            "maximise_y": maximise_y}


# ------------------------------------------------------------ orchestration
DEFAULT_LIMIT_OPTIONS = {
    "plateau": False,
    "asymptote": False,
    "confidence": False,
    "knee": False,
    "pareto_bounds": False,
    "level": 95.0,
    "slope_tol": 0.03,
    "min_neighbors": 5,
    "pareto_bootstrap": 200,
    "maximise_y": True,
}


def analyse_limits(x, y, options: dict | None = None) -> LimitReport:
    opts = dict(DEFAULT_LIMIT_OPTIONS)
    if options:
        opts.update({k: v for k, v in options.items() if v is not None})
    rep = LimitReport()
    cancel_check = opts.get("_cancel_check")
    def checkpoint():
        if cancel_check is not None and cancel_check():
            raise RuntimeError("Cancelled")
    checkpoint()
    x, y = _clean(x, y)
    if x.size < 5:
        rep.notes.append("Too few finite points for pattern analysis.")
        return rep

    if opts.get("plateau"):
        checkpoint()
        p = detect_plateau(x, y, slope_tol=float(opts.get("slope_tol", 0.03)))
        if p.get("found"):
            rep.features.append(LimitFeature("hline", f"Steady-state plateau ≈ {p['level']:.4g}",
                                             value=p["level"], group="plateau"))
            rep.features.append(LimitFeature("vline", f"Plateau onset x ≈ {p['x_start']:.4g}",
                                             value=p["x_start"], group="plateau"))
            rep.summary.append(f"Plateau: level {p['level']:.4g} (±{p['spread']:.2g}) from x ≈ "
                               f"{p['x_start']:.4g}, covering {p['frac_of_range']*100:.0f}% of the x-range.")
        else:
            rep.notes.append("No steady-state plateau found (slope never settles below tolerance).")

    if opts.get("asymptote"):
        checkpoint()
        a = fit_asymptote(x, y)
        if a.get("found"):
            rep.features.append(LimitFeature("hline", f"Asymptotic limit ≈ {a['asymptote']:.4g} ({a['model']}, R²={a['r2']:.3f})",
                                             value=a["asymptote"], group="asymptote"))
            rep.features.append(LimitFeature("curve", "Saturation fit", x=a["x"], y=a["fit"], group="asymptote"))
            line = f"Asymptote: {a['asymptote']:.4g} from {a['model']} fit (R² = {a['r2']:.3f})"
            if a.get("x95") is not None:
                line += f"; 95% of limit reached at x ≈ {a['x95']:.4g}"
            rep.summary.append(line + ".")
        else:
            rep.notes.append("No convergent saturation fit (R² too low or data not saturating).")

    if opts.get("confidence"):
        checkpoint()
        c = confidence_envelope(x, y, level=float(opts.get("level", 95.0)),
                                min_neighbors=int(opts.get("min_neighbors", 5)), cancel_check=cancel_check)
        if c.get("found"):
            rep.features.append(LimitFeature("band", f"{c['level']:.0f}% operational envelope",
                                             x=c["grid"], lo=c["lo"], hi=c["hi"], group="confidence"))
            rep.features.append(LimitFeature("curve", "Envelope median" if c["method"] == "binned_quantiles" else "Trend",
                                             x=c["grid"], y=c["median"], group="confidence"))
            width = float(np.median(c["hi"] - c["lo"]))
            rep.summary.append(f"{c['level']:.0f}% envelope ({c['method'].replace('_', ' ')}): median band width {width:.4g}; "
                               f"upper bound peaks at {float(np.max(c['hi'])):.4g}, lower bound floors at {float(np.min(c['lo'])):.4g}.")
        else:
            rep.notes.append("Confidence envelope needs ≥10 points spread over x.")

    if opts.get("knee"):
        checkpoint()
        k = detect_knee(x, y)
        if k.get("found"):
            rep.features.append(LimitFeature("point", f"Knee / optimal trade-off ({k['x']:.4g}, {k['y']:.4g})",
                                             x=k["x"], y=k["y"], group="knee"))
            rep.summary.append(f"Knee point at x = {k['x']:.4g}, y = {k['y']:.4g} (chord distance {k['strength']:.2f}).")
        else:
            rep.notes.append("No distinct knee (curve is close to linear).")

    if opts.get("pareto_bounds"):
        checkpoint()
        pb = pareto_bounds(x, y, maximise_y=bool(opts.get("maximise_y", True)),
                           n_boot=int(opts.get("pareto_bootstrap", 200)), cancel_check=cancel_check)
        if pb.get("found"):
            rep.features.append(LimitFeature("curve", "Pareto upper bound", x=pb["x"], y=pb["upper"], group="pareto"))
            rep.features.append(LimitFeature("curve", "Anti-Pareto lower bound", x=pb["x"], y=pb["lower"], group="pareto"))
            rep.features.append(LimitFeature("band", "Attainable region", x=pb["x"],
                                             lo=np.minimum(pb["upper"], pb["lower"]),
                                             hi=np.maximum(pb["upper"], pb["lower"]), group="pareto"))
            rep.summary.append(f"Pareto bounds: best attainable y = {float(np.max(pb['upper']) if pb['maximise_y'] else np.min(pb['upper'])):.4g}; "
                               f"worst = {float(np.min(pb['lower']) if pb['maximise_y'] else np.max(pb['lower'])):.4g}.")
        else:
            rep.notes.append("Pareto bounds need ≥5 finite points.")

    return rep
