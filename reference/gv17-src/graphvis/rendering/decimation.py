"""Adaptive decimation algorithms for very large GraphVis datasets."""
from __future__ import annotations

import numpy as np
import pandas as pd


def lttb_indices(x: np.ndarray, y: np.ndarray, threshold: int) -> np.ndarray:
    """Largest-Triangle-Three-Buckets indices preserving visible line shape."""
    x = np.asarray(x, dtype=float).ravel()
    y = np.asarray(y, dtype=float).ravel()
    n = min(x.size, y.size)
    if threshold >= n or threshold <= 2:
        return np.arange(n, dtype=int)
    x, y = x[:n], y[:n]
    sampled = np.empty(threshold, dtype=int)
    sampled[0], sampled[-1] = 0, n - 1
    every = (n - 2) / float(threshold - 2)
    a = 0
    for i in range(threshold - 2):
        avg_start = int(np.floor((i + 1) * every)) + 1
        avg_end = int(np.floor((i + 2) * every)) + 1
        avg_end = min(avg_end, n)
        if avg_start >= avg_end:
            avg_x, avg_y = x[min(avg_start, n - 1)], y[min(avg_start, n - 1)]
        else:
            avg_x = float(np.nanmean(x[avg_start:avg_end]))
            avg_y = float(np.nanmean(y[avg_start:avg_end]))
        range_start = int(np.floor(i * every)) + 1
        range_end = int(np.floor((i + 1) * every)) + 1
        range_end = min(range_end, n - 1)
        if range_start >= range_end:
            sampled[i + 1] = range_start
            a = range_start
            continue
        ax, ay = x[a], y[a]
        rx, ry = x[range_start:range_end], y[range_start:range_end]
        areas = np.abs((ax - avg_x) * (ry - ay) - (ax - rx) * (avg_y - ay))
        areas = np.nan_to_num(areas, nan=-1.0, posinf=-1.0, neginf=-1.0)
        idx = int(np.argmax(areas))
        a = range_start + idx
        sampled[i + 1] = a
    return np.unique(sampled)


def stratified_indices(values: np.ndarray, threshold: int, seed: int = 0) -> np.ndarray:
    """Distribution-aware subsample that keeps extrema and samples quantile bins."""
    v = np.asarray(values, dtype=float).ravel()
    n = v.size
    if threshold >= n:
        return np.arange(n, dtype=int)
    rng = np.random.default_rng(seed)
    finite = np.flatnonzero(np.isfinite(v))
    if finite.size <= threshold:
        return finite
    q = np.nanquantile(v[finite], np.linspace(0, 1, min(64, max(8, threshold // 200)) + 1))
    chosen: list[int] = [int(finite[np.nanargmin(v[finite])]), int(finite[np.nanargmax(v[finite])])]
    per = max(1, (threshold - len(chosen)) // max(len(q) - 1, 1))
    for lo, hi in zip(q[:-1], q[1:]):
        mask = finite[(v[finite] >= lo) & (v[finite] <= hi)]
        if mask.size:
            chosen.extend(rng.choice(mask, size=min(per, mask.size), replace=False).tolist())
    chosen = list(dict.fromkeys(chosen))
    if len(chosen) < threshold:
        remain = np.setdiff1d(finite, np.asarray(chosen, dtype=int), assume_unique=False)
        if remain.size:
            chosen.extend(rng.choice(remain, size=min(threshold - len(chosen), remain.size), replace=False).tolist())
    return np.sort(np.asarray(chosen[:threshold], dtype=int))


def adaptive_decimate_frame(df: pd.DataFrame, threshold: int, x_col: str | None = None,
                            y_col: str | None = None, preserve_line: bool = False) -> pd.DataFrame:
    if len(df) <= threshold:
        return df
    if preserve_line and x_col in df.columns and y_col in df.columns:
        idx = lttb_indices(df[x_col].to_numpy(float), df[y_col].to_numpy(float), threshold)
        return df.iloc[idx]
    basis = y_col if y_col in df.columns else (x_col if x_col in df.columns else None)
    if basis:
        idx = stratified_indices(df[basis].to_numpy(float), threshold)
        return df.iloc[idx]
    return df.iloc[np.linspace(0, len(df) - 1, threshold).astype(int)]
