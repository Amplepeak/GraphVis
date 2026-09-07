# =========================================================================
# analysis_tools.py - non-linear fitting, baseline correction and peak fitting
# utilities used by the GraphVis analysis dialog and reproducibility exports.
# =========================================================================
from __future__ import annotations

from dataclasses import dataclass

import numpy as np
import pandas as pd
from scipy.optimize import least_squares
from scipy.signal import find_peaks, peak_widths


@dataclass
class FitResult:
    model: str
    params: dict
    x: np.ndarray
    y: np.ndarray
    fitted: np.ndarray
    residual: np.ndarray
    r2: float


def _clean_xy(x, y):
    x = np.asarray(x, float).ravel(); y = np.asarray(y, float).ravel()
    n = min(len(x), len(y)); x, y = x[:n], y[:n]
    ok = np.isfinite(x) & np.isfinite(y)
    x, y = x[ok], y[ok]
    order = np.argsort(x, kind="stable")
    return x[order], y[order]


def polynomial_baseline(x, y, order=2, edge_fraction=0.15):
    x, y = _clean_xy(x, y)
    if len(x) < max(6, order + 2):
        raise ValueError("Not enough points for baseline correction.")
    nedge = max(order + 2, int(round(len(x) * float(edge_fraction))))
    idx = np.r_[np.arange(min(nedge, len(x)//2)), np.arange(max(len(x)-nedge, len(x)//2), len(x))]
    coef = np.polyfit(x[idx], y[idx], int(order))
    baseline = np.polyval(coef, x)
    return x, y, baseline, y - baseline, coef


def asymmetric_least_squares(y, lam=1e5, p=0.01, niter=10):
    """Eilers-style asymmetric least-squares baseline without sparse deps."""
    y = np.asarray(y, float)
    n = len(y)
    if n < 5:
        return np.zeros_like(y)
    # dense second-difference is fine for interactive spectra up to a few k points
    D = np.diff(np.eye(n), 2, axis=0)
    w = np.ones(n)
    for _ in range(int(niter)):
        W = np.diag(w)
        Z = W + float(lam) * (D.T @ D)
        z = np.linalg.solve(Z, w * y)
        w = float(p) * (y > z) + (1.0 - float(p)) * (y < z)
    return z


def gaussian(x, amp, cen, sigma):
    sigma = max(abs(float(sigma)), 1e-12)
    return float(amp) * np.exp(-0.5 * ((x - float(cen)) / sigma) ** 2)


def lorentzian(x, amp, cen, gamma):
    gamma = max(abs(float(gamma)), 1e-12)
    return float(amp) * (gamma ** 2 / ((x - float(cen)) ** 2 + gamma ** 2))


def pseudo_voigt(x, amp, cen, width, eta):
    eta = np.clip(float(eta), 0.0, 1.0)
    return eta * lorentzian(x, amp, cen, width) + (1.0 - eta) * gaussian(x, amp, cen, width)


def _peak_model(x, params, n_peaks, kind="Gaussian", include_baseline=True):
    idx = 0
    y = np.zeros_like(x, dtype=float)
    if include_baseline:
        b0, b1 = params[0], params[1]; idx = 2
        y += b0 + b1 * (x - np.mean(x))
    for _ in range(int(n_peaks)):
        amp, cen, wid = params[idx:idx+3]; idx += 3
        if kind == "Lorentzian":
            y += lorentzian(x, amp, cen, wid)
        elif kind == "Pseudo-Voigt":
            eta = params[idx]; idx += 1
            y += pseudo_voigt(x, amp, cen, wid, eta)
        else:
            y += gaussian(x, amp, cen, wid)
    return y


def fit_overlapping_peaks(x, y, n_peaks=3, kind="Gaussian", baseline_order=1):
    x, y = _clean_xy(x, y)
    if len(x) < 10:
        raise ValueError("At least 10 finite points are required.")
    n_peaks = int(np.clip(n_peaks, 1, 12))
    span = max(float(np.ptp(x)), 1e-12)
    prominence = max(float(np.ptp(y)) * 0.03, np.finfo(float).eps)
    peaks, props = find_peaks(y, prominence=prominence, distance=max(1, len(y)//(n_peaks*4)))
    if len(peaks) < n_peaks:
        candidates = np.argsort(y)[::-1]
        chosen = list(peaks)
        for k in candidates:
            if all(abs(int(k)-int(j)) > max(2, len(y)//(n_peaks*8)) for j in chosen):
                chosen.append(int(k))
            if len(chosen) >= n_peaks:
                break
        peaks = np.asarray(chosen[:n_peaks], dtype=int)
    else:
        order = np.argsort(props.get("prominences", y[peaks]))[::-1][:n_peaks]
        peaks = peaks[order]
    peaks = np.sort(peaks)
    include_baseline = baseline_order >= 0
    p0 = []
    lower = []; upper = []
    if include_baseline:
        p0 += [float(np.percentile(y, 10)), 0.0]
        lower += [-np.inf, -np.inf]; upper += [np.inf, np.inf]
    min_width = span / max(len(x) * 2.0, 50.0)
    max_width = span
    ylow = float(np.nanmin(y)); yrange = max(float(np.ptp(y)), 1e-12)
    for pk in peaks:
        amp = max(float(y[pk] - ylow), yrange * 0.05)
        cen = float(x[pk])
        wid = span / max(n_peaks * 8.0, 16.0)
        p0 += [amp, cen, wid]
        lower += [-10*yrange, float(x.min()), min_width]
        upper += [20*yrange, float(x.max()), max_width]
        if kind == "Pseudo-Voigt":
            p0 += [0.5]; lower += [0.0]; upper += [1.0]

    def resid(p):
        return _peak_model(x, p, n_peaks, kind, include_baseline) - y

    res = least_squares(resid, np.asarray(p0), bounds=(np.asarray(lower), np.asarray(upper)), max_nfev=20000)
    fit = _peak_model(x, res.x, n_peaks, kind, include_baseline)
    ss_res = float(np.sum((y-fit)**2)); ss_tot = float(np.sum((y-y.mean())**2))
    r2 = 1.0 - ss_res/max(ss_tot, 1e-30)
    params = {"baseline_intercept": float(res.x[0]), "baseline_slope": float(res.x[1])} if include_baseline else {}
    idx = 2 if include_baseline else 0
    for i in range(n_peaks):
        params[f"peak_{i+1}_amplitude"] = float(res.x[idx]); params[f"peak_{i+1}_center"] = float(res.x[idx+1]); params[f"peak_{i+1}_width"] = float(abs(res.x[idx+2])); idx += 3
        if kind == "Pseudo-Voigt":
            params[f"peak_{i+1}_eta"] = float(res.x[idx]); idx += 1
    return FitResult(kind, params, x, y, fit, y-fit, r2)


def fit_nonlinear_model(x, y, model="Exponential saturation") -> FitResult:
    x, y = _clean_xy(x, y)
    if len(x) < 6:
        raise ValueError("At least six finite points are required.")
    xmin = float(x.min()); xs = x - xmin; span = max(float(np.ptp(xs)), 1e-12)
    if model == "Exponential decay":
        def fn(p): return p[0] + p[1]*np.exp(-np.maximum(p[2], 1e-12)*xs)
        p0 = [float(y[-1]), float(y[0]-y[-1]), 1/span]
        names = ["offset", "amplitude", "rate"]
        bounds = ([-np.inf,-np.inf,1e-12],[np.inf,np.inf,np.inf])
    elif model == "Michaelis-Menten":
        xp = xs + max(span*1e-9,1e-12)
        def fn(p): return p[0]*xp/(np.maximum(p[1],1e-12)+xp) + p[2]
        p0 = [float(np.ptp(y)), span/3, float(np.nanmin(y))]
        names = ["Vmax", "Km", "offset"]
        bounds = ([-np.inf,1e-12,-np.inf],[np.inf,np.inf,np.inf])
    elif model == "Logistic":
        def fn(p): return p[3] + p[0]/(1+np.exp(-np.clip(p[1]*(x-p[2]),-700,700)))
        p0 = [float(np.ptp(y)), 4/span, float(np.median(x)), float(np.nanmin(y))]
        names = ["amplitude", "slope", "midpoint", "offset"]
        bounds = ([-np.inf,-np.inf,float(x.min())-span,-np.inf],[np.inf,np.inf,float(x.max())+span,np.inf])
    else:
        def fn(p): return p[0] - p[1]*np.exp(-np.maximum(p[2], 1e-12)*xs)
        p0 = [float(y[-1]), float(y[-1]-y[0]), 1/span]
        names = ["asymptote", "amplitude", "rate"]
        bounds = ([-np.inf,-np.inf,1e-12],[np.inf,np.inf,np.inf])
    res = least_squares(lambda p: fn(p)-y, np.asarray(p0,float), bounds=bounds, max_nfev=15000)
    fitted = fn(res.x)
    ss_res = float(np.sum((y-fitted)**2)); ss_tot = float(np.sum((y-y.mean())**2))
    r2 = 1.0 - ss_res/max(ss_tot,1e-30)
    return FitResult(model, {n:float(v) for n,v in zip(names,res.x)}, x, y, fitted, y-fitted, r2)


def result_dataframe(result: FitResult) -> pd.DataFrame:
    return pd.DataFrame({"x": result.x, "observed": result.y, "fit": result.fitted, "residual": result.residual})
