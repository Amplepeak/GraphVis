"""Deep, cacheable dataset-to-visualisation mapping for GraphVis.

The scanner is deliberately Qt-free. It profiles variables, evaluates pairwise
relationships, considers variable-name semantics and optional literature
semantic context, then ranks concrete GraphVis engines together with explicit
axis mappings. Results are persisted under ``project/context/scans`` and keyed
by both the dataset fingerprint and literature context fingerprint.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
import re
import time
from dataclasses import dataclass, asdict
from difflib import SequenceMatcher
from pathlib import Path
from typing import Any, Iterable

import numpy as np
import pandas as pd
from scipy import stats


_TIME_TOKENS = ("time", "hour", "minute", "second", "day", "week", "month", "year", "elapsed", "tspan")
_FREQ_TOKENS = ("freq", "frequency", "hz", "wavenumber", "raman", "wavelength", "energy", "ev")
_RESPONSE_TOKENS = (
    "response", "output", "target", "result", "yield", "rate", "power", "efficiency",
    "conversion", "removal", "activity", "signal", "intensity", "density", "score",
)
_PARAMETER_TOKENS = (
    "parameter", "input", "factor", "feature", "predictor", "control", "dose", "flow", "area",
    "voltage", "current", "temperature", "pressure", "ph", "concentration", "loading", "speed",
    "frequency", "time", "potential", "position",
)
_GEO_X = ("longitude", "lon", "lng")
_GEO_Y = ("latitude", "lat")

_COMPUTE_TIME_TOKENS = {"eval", "evaluation", "runtime", "execution", "wall", "cpu", "benchmark"}


def _semantic_token_match(text: str, tokens: Iterable[str]) -> bool:
    """Word-aware semantic match that avoids short-token false positives.

    For example, the electrochemical unit token ``eV`` must not make
    ``eval_time_sec`` look like a frequency/energy axis merely because the
    letters ``ev`` occur inside ``eval``.
    """
    words = set(_norm(text).split())
    for raw in tokens:
        token = _norm(raw)
        if not token:
            continue
        if token in words:
            return True
        if len(token) >= 5 and any(w.startswith(token) or token.startswith(w) for w in words if len(w) >= 4):
            return True
    return False


def _redundant_pair(a: str, b: str, metrics: dict[str, float]) -> bool:
    """Reject obvious aliases/duplicate columns from graph recommendations."""
    pear = abs(float(metrics.get("pearson", 0.0)))
    spear = abs(float(metrics.get("spearman", 0.0)))
    if max(pear, spear) < 0.9995:
        return False
    ta, tb = _tokens(a), _tokens(b)
    overlap = len(ta & tb) / max(min(len(ta), len(tb)), 1)
    return _name_similarity(a, b) >= 0.60 or overlap >= 0.75


def _norm(text: Any) -> str:
    text = str(text or "").lower().replace("−", "-")
    text = re.sub(r"\[[^\]]+\]|\([^\)]*\)", " ", text)
    return re.sub(r"[^a-z0-9]+", " ", text).strip()


def _tokens(text: Any) -> set[str]:
    return {t for t in _norm(text).split() if len(t) > 1}


def _name_similarity(a: str, b: str) -> float:
    na, nb = _norm(a), _norm(b)
    if not na or not nb:
        return 0.0
    if na == nb:
        return 1.0
    ta, tb = _tokens(na), _tokens(nb)
    jacc = len(ta & tb) / max(len(ta | tb), 1)
    substring = 0.9 if na in nb or nb in na else 0.0
    return max(SequenceMatcher(None, na, nb).ratio(), jacc, substring)


def _finite_numeric(series: pd.Series, max_points: int = 12000) -> np.ndarray:
    arr = pd.to_numeric(series, errors="coerce").to_numpy(float)
    arr = arr[np.isfinite(arr)]
    if arr.size > max_points:
        idx = np.linspace(0, arr.size - 1, max_points).astype(int)
        arr = arr[idx]
    return arr


def _mutual_information(x: np.ndarray, y: np.ndarray, bins: int = 20) -> float:
    """Small dependency score in [0, 1] without requiring scikit-learn."""
    n = min(x.size, y.size)
    if n < 20:
        return 0.0
    x, y = x[:n], y[:n]
    ok = np.isfinite(x) & np.isfinite(y)
    x, y = x[ok], y[ok]
    if x.size < 20 or np.ptp(x) == 0 or np.ptp(y) == 0:
        return 0.0
    b = max(6, min(int(math.sqrt(x.size)), bins))
    hist, _, _ = np.histogram2d(x, y, bins=b)
    pxy = hist / max(hist.sum(), 1.0)
    px = pxy.sum(axis=1, keepdims=True)
    py = pxy.sum(axis=0, keepdims=True)
    expected = px @ py
    mask = (pxy > 0) & (expected > 0)
    mi = float(np.sum(pxy[mask] * np.log(pxy[mask] / expected[mask])))
    hx = float(-np.sum(px[px > 0] * np.log(px[px > 0])))
    hy = float(-np.sum(py[py > 0] * np.log(py[py > 0])))
    denom = max(min(hx, hy), 1e-12)
    return max(0.0, min(1.0, mi / denom))


def dataset_fingerprint(dataset: Any) -> str:
    path = str(getattr(dataset, "path", "") or "")
    stat = None
    try:
        st = os.stat(path)
        stat = (int(st.st_size), int(st.st_mtime_ns))
    except OSError:
        stat = (0, 0)
    payload = {
        "path": os.path.abspath(path) if path else "",
        "stat": stat,
        "rows": int(len(getattr(dataset, "df", []))),
        "columns": [str(c) for c in getattr(getattr(dataset, "df", None), "columns", [])],
        "matrix_shapes": sorted((str(k), list(np.asarray(v).shape)) for k, v in getattr(dataset, "matrices", {}).items()),
        "volume_shapes": sorted((str(k), list(np.asarray(v).shape)) for k, v in getattr(dataset, "volumes", {}).items()),
    }
    return hashlib.sha256(json.dumps(payload, sort_keys=True, default=str).encode("utf-8")).hexdigest()


def literature_fingerprint(literature: Iterable[Any] | None) -> str:
    compact: list[dict[str, Any]] = []
    for ext in literature or []:
        compact.append({
            "path": str(getattr(ext, "path", "") or ""),
            "title": str(getattr(ext, "title", "") or ""),
            "semantic": getattr(ext, "semantic_context", {}) or {},
            "parameters": getattr(ext, "parameters", {}) or {},
        })
    return hashlib.sha256(json.dumps(compact, sort_keys=True, default=str).encode("utf-8")).hexdigest()


def _context_variables(literature: Iterable[Any] | None) -> list[tuple[str, str, str]]:
    """Return (role, textual variable, source title)."""
    out: list[tuple[str, str, str]] = []
    for ext in literature or []:
        title = str(getattr(ext, "title", "Literature") or "Literature")
        sc = getattr(ext, "semantic_context", {}) or {}
        for role, key in (("x", "x_variable"), ("y", "y_variable"), ("z", "z_variable")):
            value = sc.get(key)
            if value:
                out.append((role, str(value), title))
        for key in (sc.get("conditions") or {}):
            out.append(("condition", str(key), title))
    return out


def _literature_plot_hints(literature: Iterable[Any] | None) -> list[tuple[str, str]]:
    out: list[tuple[str, str]] = []
    for ext in literature or []:
        title = str(getattr(ext, "title", "Literature") or "Literature")
        sc = getattr(ext, "semantic_context", {}) or {}
        chart = sc.get("inferred_plot_type")
        if chart:
            out.append((str(chart), title))
    return out


def _profile_column(name: str, series: pd.Series, max_points: int = 12000) -> dict[str, Any]:
    n = int(len(series))
    sampled = series
    # Sample before numeric conversion / nunique so a short Smart Suite budget
    # does not still pay an O(N) pandas-cleaning cost for every wide-table
    # column.  max_points=0 is the explicit deep-scan/full-data mode.
    if max_points > 0 and n > max_points:
        idx = np.linspace(0, n - 1, max_points).astype(int)
        sampled = series.iloc[idx]
    arr = pd.to_numeric(sampled, errors="coerce").to_numpy(float)
    finite = np.isfinite(arr)
    x = arr[finite]
    out: dict[str, Any] = {
        "name": str(name), "n": n, "sampled_n": int(arr.size),
        "finite_fraction": float(finite.mean()) if arr.size else 0.0,
        "unique": int(pd.Series(x).nunique()) if x.size else 0,
        "unique_is_sampled": bool(max_points > 0 and n > max_points),
    }
    if x.size == 0:
        return out
    sample = x
    out.update({
        "min": float(np.min(sample)), "max": float(np.max(sample)), "mean": float(np.mean(sample)),
        "std": float(np.std(sample)), "median": float(np.median(sample)),
        "skew": float(stats.skew(sample, bias=False)) if sample.size >= 8 else 0.0,
        "nonnegative": bool(np.min(sample) >= 0),
        "unique_fraction": float(len(np.unique(sample)) / max(sample.size, 1)),
    })
    if sample.size >= 8 and np.ptp(sample) > 0:
        idx = np.arange(sample.size, dtype=float)
        try:
            out["order_spearman"] = float(stats.spearmanr(idx, sample).statistic)
        except Exception:
            out["order_spearman"] = 0.0
        q1, q3 = np.percentile(sample, [25, 75]); iqr = q3 - q1
        out["outlier_fraction"] = float(np.mean((sample < q1 - 1.5 * iqr) | (sample > q3 + 1.5 * iqr))) if iqr > 0 else 0.0
    else:
        out["order_spearman"] = 0.0; out["outlier_fraction"] = 0.0
    low = _norm(name)
    words = set(low.split())
    computational_time = bool(words & _COMPUTE_TIME_TOKENS)
    out["time_like"] = _semantic_token_match(low, _TIME_TOKENS) and not computational_time
    out["frequency_like"] = _semantic_token_match(low, _FREQ_TOKENS)
    out["response_like"] = _semantic_token_match(low, _RESPONSE_TOKENS)
    out["parameter_like"] = _semantic_token_match(low, _PARAMETER_TOKENS)
    out["computational_time"] = computational_time
    out["longitude_like"] = low in _GEO_X or any(f" {t} " in f" {low} " for t in _GEO_X)
    out["latitude_like"] = low in _GEO_Y or any(f" {t} " in f" {low} " for t in _GEO_Y)
    return out


def _pair_metrics(df: pd.DataFrame, a: str, b: str, max_points: int = 8000) -> dict[str, float]:
    pair = df[[a, b]]
    # Pre-sample raw rows before pandas numeric conversion/dropna.  Oversample
    # slightly so sparse/NaN columns still have a chance to contribute the
    # requested number of finite observations.
    if max_points > 0 and len(pair) > max_points * 2:
        idx = np.linspace(0, len(pair) - 1, max_points * 2).astype(int)
        pair = pair.iloc[idx]
    pair = pair.apply(pd.to_numeric, errors="coerce").replace([np.inf, -np.inf], np.nan).dropna()
    if max_points > 0 and len(pair) > max_points:
        idx = np.linspace(0, len(pair) - 1, max_points).astype(int); pair = pair.iloc[idx]
    if len(pair) < 8:
        return {"pearson": 0.0, "spearman": 0.0, "mutual_info": 0.0, "n": float(len(pair))}
    x = pair[a].to_numpy(float); y = pair[b].to_numpy(float)
    if np.ptp(x) == 0 or np.ptp(y) == 0:
        return {"pearson": 0.0, "spearman": 0.0, "mutual_info": 0.0, "n": float(len(pair))}
    try: pear = float(stats.pearsonr(x, y).statistic)
    except Exception: pear = 0.0
    try: spear = float(stats.spearmanr(x, y).statistic)
    except Exception: spear = 0.0
    return {"pearson": pear, "spearman": spear, "mutual_info": _mutual_information(x, y), "n": float(len(pair))}


@dataclass(slots=True)
class ScanRecommendation:
    graph: str
    score: float
    mappings: dict[str, str | None]
    reason: str
    source: str = "dataset"
    diagnostics: dict[str, Any] | None = None

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


def _best_column_match(columns: list[str], textual: str) -> tuple[str | None, float]:
    scored = sorted(((_name_similarity(c, textual), c) for c in columns), reverse=True)
    return (scored[0][1], scored[0][0]) if scored else (None, 0.0)


def _add(rec_list: list[ScanRecommendation], graph: str, score: float, mappings: dict[str, str | None], reason: str,
         source: str = "dataset", diagnostics: dict[str, Any] | None = None) -> None:
    mappings = {k: v for k, v in mappings.items() if v}
    key = (graph, tuple(sorted(mappings.items())))
    for i, old in enumerate(rec_list):
        old_key = (old.graph, tuple(sorted((k, v) for k, v in old.mappings.items() if v)))
        if old_key == key:
            if score > old.score:
                rec_list[i] = ScanRecommendation(graph, min(float(score), 1.0), mappings, reason, source, diagnostics)
            return
    rec_list.append(ScanRecommendation(graph, min(float(score), 1.0), mappings, reason, source, diagnostics))


def _pair_cache_key(a: str, b: str) -> str:
    return json.dumps(sorted((str(a), str(b))), ensure_ascii=False, separators=(",", ":"))


def scan_dataset(dataset: Any, literature: Iterable[Any] | None = None, *, progress=None, cancelled=None,
                 time_budget_seconds: float = 30.0, resume_state: dict[str, Any] | None = None,
                 checkpoint=None) -> dict[str, Any]:
    """Deep scan a GraphVis Dataset and return graph-specific axis mappings.

    ``time_budget_seconds`` is a soft analysis budget.  Small budgets reduce
    the pairwise search/sample size; larger budgets inspect more columns and
    more observations.  The scanner always returns the best result found so
    far before the budget expires.
    """
    if dataset is None or getattr(dataset, "df", None) is None or dataset.df.empty:
        return {"recommendations": [], "profiles": {}, "pair_metrics": [], "generated": time.time()}
    df = dataset.df
    budget = max(3.0, min(float(time_budget_seconds or 30.0), 259200.0))
    started = time.monotonic()
    deadline = started + budget
    # Scale scan breadth with the user supplied thinking budget.  Long budgets
    # deliberately remove the old 10-minute ceiling and can inspect every
    # numeric column / every row.  The deadline still guarantees a soft stop.
    if budget <= 8:
        max_columns, profile_sample, pair_sample, max_pairs = 18, 12000, 2200, 80
    elif budget <= 20:
        max_columns, profile_sample, pair_sample, max_pairs = 28, 12000, 5000, 180
    elif budget <= 60:
        max_columns, profile_sample, pair_sample, max_pairs = 42, 16000, 9000, 420
    elif budget <= 300:
        max_columns, profile_sample, pair_sample, max_pairs = 96, 30000, 30000, 3000
    elif budget <= 1800:
        max_columns, profile_sample, pair_sample, max_pairs = 192, 60000, 60000, 12000
    elif budget <= 7200:
        max_columns, profile_sample, pair_sample, max_pairs = 384, 120000, 120000, 50000
    elif budget <= 21600:
        max_columns, profile_sample, pair_sample, max_pairs = 10**9, 250000, 250000, 10**9
    else:
        # Six hours or more is explicit "deep scan everything" territory.
        # max_points=0 means no row sampling in the profile/pair helpers.
        max_columns, profile_sample, pair_sample, max_pairs = 10**9, 0, 0, 10**9
    numeric = [str(c) for c in getattr(dataset, "numeric_columns", []) if c in df.columns]
    if not numeric:
        return {"recommendations": [], "profiles": {}, "pair_metrics": [], "generated": time.time()}
    current_dfp = dataset_fingerprint(dataset)
    current_lfp = literature_fingerprint(literature)
    resume = dict(resume_state or {})
    valid_resume = (resume.get("dataset_fingerprint") == current_dfp and resume.get("literature_fingerprint") == current_lfp)
    profiles = dict(resume.get("profiles") or {}) if valid_resume else {}
    pair_cache: dict[str, dict[str, float]] = dict(resume.get("pair_cache") or {}) if valid_resume else {}
    budget_exhausted = False
    # Prioritise declared experiment inputs/responses, then fill the remaining
    # budget with numeric columns in source order.  Earlier builds profiled every
    # numeric column even for a five-second scan, which could dominate runtime on
    # very wide tables before pair scoring started.  Six-hour+ scans still use the
    # effectively-unbounded max_columns setting and therefore profile everything.
    parameter_hints = [c for c in numeric if c in getattr(dataset, "parameter_columns", [])]
    response_hints = [c for c in numeric if c in getattr(dataset, "response_columns", [])]
    profile_columns = list(dict.fromkeys(parameter_hints + response_hints + numeric))[:max_columns]
    if progress:
        suffix = f" (resuming {len(profiles)} profiles / {len(pair_cache)} pairs)" if valid_resume and (profiles or pair_cache) else ""
        progress("Profiling dataset variables…" + suffix, 8)
    for idx, c in enumerate(profile_columns):
        if c not in profiles:
            profiles[c] = _profile_column(c, df[c], max_points=profile_sample)
        if cancelled and cancelled():
            if checkpoint:
                checkpoint({"version": 1, "dataset_fingerprint": current_dfp, "literature_fingerprint": current_lfp,
                            "profiles": profiles, "pair_cache": pair_cache, "updated": time.time()})
            raise RuntimeError("Cancelled")
        if checkpoint and idx and idx % 32 == 0:
            checkpoint({"version": 1, "dataset_fingerprint": current_dfp, "literature_fingerprint": current_lfp,
                        "profiles": profiles, "pair_cache": pair_cache, "updated": time.time()})
        if time.monotonic() >= deadline:
            budget_exhausted = True
            if checkpoint:
                checkpoint({"version": 1, "dataset_fingerprint": current_dfp, "literature_fingerprint": current_lfp,
                            "profiles": profiles, "pair_cache": pair_cache, "updated": time.time()})
            break
    if cancelled and cancelled(): raise RuntimeError("Cancelled")
    profiled_numeric = [c for c in profile_columns if c in profiles]
    if not profiled_numeric:
        return {
            "version": 2, "dataset_name": str(getattr(dataset, "name", "dataset")),
            "dataset_fingerprint": current_dfp, "literature_fingerprint": current_lfp,
            "generated": time.time(), "analysis_seconds": round(time.monotonic() - started, 4),
            "time_budget_seconds": budget, "budget_exhausted": True, "examined_pairs": 0,
            "checkpoint_pair_count": int(len(pair_cache)), "profiles": profiles, "pair_metrics": [],
            "literature_matches": {}, "recommendations": [],
        }

    # Literature-to-dataset variable matching.
    lit_matches: dict[str, tuple[str, float, str]] = {}
    for role, text, title in _context_variables(literature):
        col, score = _best_column_match(numeric, text)
        if col and score >= 0.42 and (role not in lit_matches or score > lit_matches[role][1]):
            lit_matches[role] = (col, score, title)

    # Evaluate a budgeted set of promising pair relationships.  This is O(p^2),
    # so short scans use the same profiled subset while explicit long scans can
    # progressively reach every numeric column.
    if progress: progress("Scoring variable relationships…", 24)
    params = [c for c in parameter_hints if c in profiles]
    responses = [c for c in response_hints if c in profiles]
    ranked_cols = sorted(profiled_numeric, key=lambda c: (
        1 if profiles[c].get("time_like") else 0,
        1 if profiles[c].get("parameter_like") else 0,
        1 if profiles[c].get("response_like") else 0,
        float(profiles[c].get("std", 0.0)) > 0,
    ), reverse=True)
    candidates = list(dict.fromkeys(params + responses + ranked_cols))[:max_columns]
    total_candidate_pairs = len(candidates) * max(len(candidates) - 1, 0) // 2
    max_pairs = min(int(max_pairs), total_candidate_pairs)
    pairs: list[dict[str, Any]] = []
    examined_pairs = 0
    pair_limit_reached = False
    new_pairs_since_checkpoint = 0
    last_checkpoint = time.monotonic()

    def write_checkpoint(force: bool = False) -> None:
        nonlocal new_pairs_since_checkpoint, last_checkpoint
        if not checkpoint:
            return
        now = time.monotonic()
        # Full checkpoint JSON grows with the pair cache.  Saving every few
        # hundred pairs becomes an O(n^2) I/O tax on very deep scans, so keep a
        # useful recovery cadence without dominating the analysis itself.
        if force or new_pairs_since_checkpoint >= 1000 or now - last_checkpoint >= 20.0:
            checkpoint({"version": 1, "dataset_fingerprint": current_dfp, "literature_fingerprint": current_lfp,
                        "profiles": profiles, "pair_cache": pair_cache, "updated": time.time()})
            new_pairs_since_checkpoint = 0
            last_checkpoint = now

    for i, a in enumerate(candidates):
        if budget_exhausted:
            break
        if cancelled and cancelled():
            write_checkpoint(True); raise RuntimeError("Cancelled")
        if time.monotonic() >= deadline:
            budget_exhausted = True; break
        for b in candidates[i + 1:]:
            if cancelled and cancelled():
                write_checkpoint(True); raise RuntimeError("Cancelled")
            if examined_pairs >= max_pairs or time.monotonic() >= deadline:
                pair_limit_reached = examined_pairs >= max_pairs
                budget_exhausted = time.monotonic() >= deadline
                break
            cache_key = _pair_cache_key(a, b)
            met = pair_cache.get(cache_key)
            if met is None:
                met = _pair_metrics(df, a, b, max_points=pair_sample)
                pair_cache[cache_key] = met
                new_pairs_since_checkpoint += 1
            examined_pairs += 1
            if _redundant_pair(a, b, met):
                write_checkpoint()
                continue
            dep = max(abs(met["pearson"]), abs(met["spearman"]), met["mutual_info"])
            if dep >= 0.18:
                pairs.append({"a": a, "b": b, "dependency": float(dep), **met})
            write_checkpoint()
        if budget_exhausted or pair_limit_reached:
            break
    if budget_exhausted:
        write_checkpoint(True)
    pairs.sort(key=lambda d: d["dependency"], reverse=True)

    recs: list[ScanRecommendation] = []
    if progress: progress("Matching graph mathematics to variable patterns…", 52)

    # Explicit literature plot intent has priority.
    #
    # A recommendation still has to be DRAWABLE. The paper often names only some
    # of the axes - an x and a z but no y is common, because that is how the
    # caption read - and such a mapping used to be emitted exactly as matched.
    # The old fallback only fired when the mapping was completely empty, so a
    # partial match went out with no y column, and a plot with no y draws
    # nothing: clicking the recommendation looked like a dead control.
    #
    # Missing axes are filled from the numeric columns and SAID so, rather than
    # left out silently or the whole recommendation dropped. "Never invent axes"
    # was the right instinct and the wrong conclusion: what matters is that the
    # person can see which axes came from the paper and which did not.
    lit_map = {role: val[0] for role, val in lit_matches.items() if role in ("x", "y", "z")}
    for graph, title in _literature_plot_hints(literature):
        mappings = {role: col for role, col in lit_map.items() if col}
        from_paper = sorted(mappings)
        filled: list[str] = []
        for role in ("x", "y"):
            if mappings.get(role):
                continue
            for candidate in numeric:
                if candidate not in mappings.values():
                    mappings[role] = candidate
                    filled.append(role)
                    break
        if not mappings.get("x") or not mappings.get("y"):
            # Fewer than two numeric columns: there is no two-axis plot to
            # recommend, and offering one that cannot be drawn is worse than
            # offering none.
            continue
        if from_paper and filled:
            reason = (f"Literature '{title}' explicitly indicates this plot. "
                      f"{'/'.join(from_paper).upper()} matched to the data; "
                      f"{'/'.join(filled).upper()} chosen from the numeric columns.")
        elif from_paper:
            reason = (f"Literature '{title}' explicitly indicates this "
                      f"plot/relationship; its variables were matched to the data.")
        else:
            reason = (f"Literature '{title}' indicates this plot type. No variable "
                      f"in it matched a column, so the axes are the first two "
                      f"numeric columns.")
        _add(recs, graph, 0.98 if from_paper else 0.72, mappings, reason, "literature")

    # Named/time/frequency structures.
    time_cols = [c for c in profiled_numeric if profiles[c].get("time_like")]
    freq_cols = [c for c in profiled_numeric if profiles[c].get("frequency_like")]
    if time_cols:
        x = time_cols[0]
        y_choices = [c for c in responses + numeric if c != x]
        if y_choices:
            y = y_choices[0]
            _add(recs, "Line Chart", 0.94, {"x": x, "y": y}, f"'{x}' is time-like, so a time-series view against '{y}' preserves acquisition order.")
            _add(recs, "Rolling Mean", 0.79, {"x": x, "y": y}, "A rolling mean exposes slower trends in the detected time series.")
    if freq_cols:
        x = freq_cols[0]; y = next((c for c in responses + numeric if c != x), None)
        if y:
            _add(recs, "Power Spectral Density", 0.90, {"x": x, "y": y}, f"Frequency-like variable '{x}' detected; spectral density is mathematically appropriate.")
            _add(recs, "Line Chart", 0.82, {"x": x, "y": y}, "Frequency-domain response curve.")

    # Parameter-response combinations are the core scientific sweep use case.
    if not params:
        params = [c for c in profiled_numeric if profiles[c].get("parameter_like")][:8]
    if not responses:
        responses = [c for c in profiled_numeric if profiles[c].get("response_like") and c not in params][:8]
    if not responses:
        responses = [profiled_numeric[-1]]
    if params and responses:
        x, y = params[0], responses[0]
        best = next((p for p in pairs if {p["a"], p["b"]} == {x, y}), None)
        dep = float(best["dependency"]) if best else 0.35
        _add(recs, "4D / 5D Scatter", 0.72 + 0.18 * dep, {"x": x, "y": y,
             "w": (params[1] if len(params) > 1 else None), "v": (responses[1] if len(responses) > 1 else None)},
             f"Parameter '{x}' and response '{y}' form a natural experiment-response pairing; extra variables are encoded only when available.")
        _add(recs, "Connected Scatter", 0.62 + 0.18 * dep, {"x": x, "y": y}, "Shows the parameter-response relationship while preserving sample order.")
        if len(params) >= 2:
            z = responses[0]
            _add(recs, "2D Heatmap", 0.92, {"x": params[0], "y": params[1], "z": z},
                 f"Two independent/swept parameters ('{params[0]}', '{params[1]}') and response '{z}' support a response-surface heatmap.")
            _add(recs, "2D Contour", 0.89, {"x": params[0], "y": params[1], "z": z}, "Iso-response contours reveal operating windows and optima.")
            _add(recs, "3D Topography / Surface", 0.86, {"x": params[0], "y": params[1], "z": z}, "Three-dimensional response surface for the two-parameter sweep.")
            _add(recs, "Global Sensitivity", 0.81, {"z": z}, "Multiple parameter columns allow standardized sensitivity ranking against the response.")
            _add(recs, "1D Marginal Responses", 0.78, {"z": z}, "Marginal response panels reveal monotone and nonlinear parameter effects.")

    # Strongest empirical relationships get their own graph-specific mappings.
    for rank, p in enumerate(pairs[:8]):
        a, b, dep = p["a"], p["b"], float(p["dependency"])
        score = 0.67 + 0.25 * dep - 0.015 * rank
        _add(recs, "4D / 5D Scatter", score, {"x": a, "y": b},
             f"Strong dependency detected between '{a}' and '{b}' (|ρ/Pearson|/MI score {dep:.2f}).", diagnostics=p)
        if abs(float(p.get("spearman", 0))) >= 0.72:
            _add(recs, "Line Chart", score - 0.04, {"x": a, "y": b},
                 f"'{a}' and '{b}' show a strong monotonic relationship (Spearman ρ={p['spearman']:.2f}).", diagnostics=p)

    # Multivariate structure.
    if len(profiled_numeric) >= 4:
        _add(recs, "Correlation Matrix", 0.80, {"x": profiled_numeric[0], "y": profiled_numeric[1], "z": profiled_numeric[2]}, "Four or more profiled numeric variables support a correlation structure overview.")
        _add(recs, "Plot Matrix", 0.73, {"x": profiled_numeric[0], "y": profiled_numeric[1], "z": profiled_numeric[2]}, "Pairwise multivariate overview for the most informative profiled columns.")
    if len(responses) >= 2:
        _add(recs, "Pareto Front", 0.79, {"x": responses[0], "y": responses[1]}, "Two response/objective variables support a non-dominated trade-off view.")

    # Distribution diagnostics.
    distribution_col = max(profiled_numeric, key=lambda c: abs(float(profiles[c].get("skew", 0.0))) + 3.0 * float(profiles[c].get("outlier_fraction", 0.0)))
    _add(recs, "Histogram", 0.62, {"x": distribution_col}, f"Distribution summary for '{distribution_col}', selected from skew/outlier diagnostics.")
    _add(recs, "KDE Density", 0.60, {"x": distribution_col}, f"Smooth density estimate for '{distribution_col}'.")
    _add(recs, "ECDF", 0.58, {"x": distribution_col}, f"Distribution-free cumulative view for '{distribution_col}'.")

    # Geographic pair.
    lon = next((c for c in profiled_numeric if profiles[c].get("longitude_like")), None)
    lat = next((c for c in profiled_numeric if profiles[c].get("latitude_like")), None)
    if lon and lat:
        _add(recs, "Geo Scatter", 0.96, {"x": lon, "y": lat}, "Latitude/longitude fields detected.")

    # Matrix/volume-aware recommendations.
    matrices = getattr(dataset, "matrices", {}) or {}
    volumes = getattr(dataset, "volumes", {}) or {}
    if matrices:
        m = next(iter(matrices))
        _add(recs, "2D Heatmap", 0.76, {"matrix": f"matrix:{m}"}, f"2-D matrix '{m}' can be visualized directly as a heatmap.")
    if volumes:
        vname = next(iter(volumes))
        _add(recs, "Volume Slice", 0.86, {"matrix": f"volume:{vname}"}, f"3-D scalar volume '{vname}' detected.")
        _add(recs, "Isosurface", 0.80, {"matrix": f"volume:{vname}"}, f"3-D scalar volume '{vname}' supports isosurface extraction.")

    # Recommend presentation choices as part of the same scientific decision,
    # rather than applying one arbitrary colour/scale to every graph.
    def choose_axis_scales(mapping: dict[str, str | None]) -> dict[str, str]:
        scales = {"x": "Linear", "y": "Linear", "z": "Linear"}
        for role in ("x", "y", "z"):
            c = mapping.get(role)
            pr = profiles.get(c or "", {})
            lo, hi = pr.get("min"), pr.get("max")
            if lo is not None and hi is not None and float(lo) > 0 and float(hi) / max(float(lo), 1e-15) >= 1e3:
                scales[role] = "Log10"
        return scales


    for rec in recs:
        graph = rec.graph.lower()
        zcol = rec.mappings.get("z")
        zprof = profiles.get(zcol or "", {})
        diverging = bool(zprof and float(zprof.get("min", 0.0)) < 0 < float(zprof.get("max", 0.0)))
        if any(k in graph for k in ("correlation", "covariance", "residual", "difference", "bland")) or diverging:
            cmap = "coolwarm"; series_colour = "#C44E52"
        elif any(k in graph for k in ("heatmap", "contour", "surface", "density", "volume", "iso")):
            cmap = "viridis"; series_colour = "#2A9D8F"
        elif any(k in graph for k in ("pareto", "scatter", "bubble", "point")):
            cmap = "turbo"; series_colour = "#2E86C1"
        elif any(k in graph for k in ("time", "line", "rolling", "trend")):
            cmap = "parula"; series_colour = "#3A7D44"
        else:
            cmap = "parula"; series_colour = "#2E6F9E"
        diag = dict(rec.diagnostics or {})
        diag["recommended_colourmap"] = cmap
        diag["recommended_series_color"] = series_colour
        axis_scales = choose_axis_scales(rec.mappings)
        diag["recommended_axis_scales"] = axis_scales
        rec.diagnostics = diag

    # Boost recommendations whose mapping agrees with literature variable roles.
    if lit_map:
        for rec in recs:
            matches = sum(1 for role in ("x", "y", "z") if lit_map.get(role) and rec.mappings.get(role) == lit_map.get(role))
            if matches:
                rec.score = min(1.0, rec.score + 0.05 * matches)
                rec.reason += f" Literature-context mapping agrees on {matches} axis/axes."
                rec.source = "dataset+literature"

    recs.sort(key=lambda r: r.score, reverse=True)
    # Keep graph diversity while permitting a second mapping for the same graph
    # only when it is materially different and high-scoring.
    final: list[ScanRecommendation] = []
    per_graph: dict[str, int] = {}
    for rec in recs:
        if per_graph.get(rec.graph, 0) >= 2:
            continue
        final.append(rec); per_graph[rec.graph] = per_graph.get(rec.graph, 0) + 1
        if len(final) >= 18:
            break
    if progress: progress("Finalizing intelligent mappings…", 92)
    # Keep the final reusable work state even when this budget finished before
    # its deadline.  A later, larger budget can continue from these profiles
    # and pair metrics instead of recomputing them from scratch.
    write_checkpoint(True)

    result = {
        "version": 2,
        "dataset_name": str(getattr(dataset, "name", "dataset")),
        "dataset_fingerprint": current_dfp,
        "literature_fingerprint": current_lfp,
        "generated": time.time(),
        "analysis_seconds": round(time.monotonic() - started, 4),
        "time_budget_seconds": budget,
        "budget_exhausted": bool(budget_exhausted),
        "examined_pairs": int(examined_pairs),
        "checkpoint_pair_count": int(len(pair_cache)),
        "profiles": profiles,
        "pair_metrics": pairs[:40],
        "literature_matches": {k: {"column": v[0], "score": v[1], "source": v[2]} for k, v in lit_matches.items()},
        "recommendations": [r.to_dict() for r in final],
    }
    if progress: progress("Dataset scan complete", 100)
    return result


# The finished-scan cache used to live here as cache_path/save_scan/
# load_cached_scan, and nothing called any of the three: the application caches
# a finished scan itself, in AppController::scanDataset, keyed on the Arrow
# file's size and modification time, the budget and the literature block, and
# stored in the project folder when a project is open. Two caches for one answer
# is one cache too many, and the one that was reachable is the one that knows
# about projects - so this half is gone rather than duplicated.
#
# The checkpoint below is a different thing and is wired: it is what lets a long
# scan - the budget goes up to three days - resume instead of starting again.
def checkpoint_path(cache_dir: str | Path, dataset: Any, literature_fp: str | None = None) -> Path:
    root = Path(cache_dir); root.mkdir(parents=True, exist_ok=True)
    lfp = str(literature_fp or literature_fingerprint(None))
    return root / f"{dataset_fingerprint(dataset)}_{lfp[:16]}.checkpoint.json"


def save_scan_checkpoint(state: dict[str, Any], cache_dir: str | Path, dataset: Any,
                         literature: Iterable[Any] | None = None) -> Path:
    """Atomically persist resumable deep-scan work."""
    path = checkpoint_path(cache_dir, dataset, literature_fingerprint(literature))
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(state, ensure_ascii=False, default=str), encoding="utf-8")
    os.replace(tmp, path)
    return path


def load_scan_checkpoint(dataset: Any, cache_dir: str | Path,
                         literature: Iterable[Any] | None = None) -> dict[str, Any] | None:
    path = checkpoint_path(cache_dir, dataset, literature_fingerprint(literature))
    if not path.exists():
        return None
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return None
    if data.get("dataset_fingerprint") != dataset_fingerprint(dataset):
        return None
    if data.get("literature_fingerprint") != literature_fingerprint(literature):
        return None
    return data


def clear_scan_checkpoint(dataset: Any, cache_dir: str | Path,
                          literature: Iterable[Any] | None = None) -> None:
    try:
        checkpoint_path(cache_dir, dataset, literature_fingerprint(literature)).unlink(missing_ok=True)
    except Exception:
        pass


# The panel picks a recommendation itself - ScanPanel.qml reads the mappings
# straight off the row the user tapped - so best_mapping_for_graph, which asked
# the service to re-find the highest-scoring row of a list the UI already holds,
# was a round trip for an answer already in hand. Removed rather than wired.
