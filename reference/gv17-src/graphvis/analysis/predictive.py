"""Predictive regression/extrapolation helpers used by literature recreation."""
from __future__ import annotations

from dataclasses import dataclass
from typing import Literal

import numpy as np
from scipy import stats


@dataclass(slots=True)
class PredictionBand:
    model: str
    x: np.ndarray
    y: np.ndarray
    lower: np.ndarray
    upper: np.ndarray
    r2: float
    original_x_max: float
    extrapolated: np.ndarray


def _r2(y: np.ndarray, pred: np.ndarray) -> float:
    ss_res = float(np.nansum((y - pred) ** 2))
    ss_tot = float(np.nansum((y - np.nanmean(y)) ** 2))
    return 1.0 - ss_res / max(ss_tot, np.finfo(float).eps)


def forecast_series(x, y, extension: float = 0.20, confidence: float = 0.95) -> PredictionBand:
    x = np.asarray(x, dtype=float).ravel(); y = np.asarray(y, dtype=float).ravel()
    n = min(x.size, y.size); x, y = x[:n], y[:n]
    ok = np.isfinite(x) & np.isfinite(y); x, y = x[ok], y[ok]
    if x.size < 4 or np.ptp(x) <= 0:
        raise ValueError("Predictive extrapolation requires at least four finite points spanning X.")
    order = np.argsort(x, kind="stable"); x, y = x[order], y[order]
    x0 = float(x.min()); xmax = float(x.max()); span = xmax - x0
    grid = np.linspace(x0, xmax + max(extension, 0.0) * span, max(240, x.size * 4))

    candidates: list[tuple[str, np.ndarray, np.ndarray, int]] = []
    for deg in (1, 2, 3):
        if x.size <= deg + 2:
            continue
        coef = np.polyfit(x, y, deg)
        pred_train = np.polyval(coef, x)
        pred_grid = np.polyval(coef, grid)
        candidates.append((f"Polynomial degree {deg}", pred_train, pred_grid, deg + 1))
    if np.all(y > 0):
        try:
            b, loga, _, _, _ = stats.linregress(x, np.log(y))
            pred_train = np.exp(loga + b * x); pred_grid = np.exp(loga + b * grid)
            candidates.append(("Exponential", pred_train, pred_grid, 2))
        except Exception:
            pass
    if not candidates:
        raise ValueError("No predictive model could be fitted.")

    def score(c):
        name, pred, _g, k = c
        rss = max(float(np.nansum((y - pred) ** 2)), np.finfo(float).eps)
        aicc = x.size * np.log(rss / x.size) + 2 * k
        if x.size > k + 1:
            aicc += 2 * k * (k + 1) / (x.size - k - 1)
        return aicc

    name, train_pred, pred_grid, k = min(candidates, key=score)
    resid = y - train_pred
    dof = max(x.size - k, 1)
    sigma = float(np.sqrt(np.nansum(resid ** 2) / dof))
    z = float(stats.t.ppf(0.5 + confidence / 2.0, dof)) if dof > 1 else 1.96
    # Conservative prediction-style band. It widens outside the observed x range.
    center = float(np.mean(x)); denom = max(float(np.sum((x - center) ** 2)), np.finfo(float).eps)
    leverage = 1.0 + 1.0 / x.size + (grid - center) ** 2 / denom
    half = z * sigma * np.sqrt(leverage)
    return PredictionBand(name, grid, pred_grid, pred_grid - half, pred_grid + half,
                          _r2(y, train_pred), xmax, grid > xmax)
