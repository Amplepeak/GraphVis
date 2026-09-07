# =========================================================================
# lhs_pipeline.py — robust sanitization + canonical mapping for Latin
# Hypercube simulation sweeps (DF + MEC hydrogen-production studies).
#
# Pure NumPy/pandas: safe from worker threads.  Fixes the class of bugs where
# axis limits / clipping bounds arrive inverted or corrupted (e.g. min=200,
# max=159), which distorts the 3-D bounding box and breaks coordinate
# mapping, and filters non-convergent solver points before rendering.
# =========================================================================
from __future__ import annotations

import numpy as np
import pandas as pd

# Canonical LHS sweep roles for this thesis: X = flow rate, Y = anode area,
# Z = applied voltage, colour/response = volumetric H2 production rate.
LHS_CANONICAL_ROLES: dict[str, tuple[str, ...]] = {
    "x": ("lhs_flow", "flow", "q_in", "flow_rate"),
    "y": ("lhs_area", "area", "anode_area", "a_an"),
    "z": ("lhs_ea", "applied_voltage", "e_app", "eapp", "ea", "v_app"),
    "w": ("total_VHPR_mean", "vhpr", "total_vhpr", "vhpr_mean", "mec_VHPR_mean"),
}


def sanitize_bounds(lo, hi, data=None) -> tuple[float, float] | None:
    """Return a valid ``(min, max)`` pair with ``min <= max`` guaranteed.

    - Inverted bounds (min > max) are swapped, never rejected.
    - Non-finite or degenerate (min == max) bounds fall back to the true
      data min/max when ``data`` is supplied, else ``None`` (meaning "no
      constraint" — callers should use autoscale).
    """
    try:
        lo_f, hi_f = float(lo), float(hi)
    except (TypeError, ValueError):
        lo_f, hi_f = np.nan, np.nan
    if np.isfinite(lo_f) and np.isfinite(hi_f) and lo_f != hi_f:
        return (lo_f, hi_f) if lo_f < hi_f else (hi_f, lo_f)
    if data is not None:
        arr = np.asarray(data, dtype=float)
        arr = arr[np.isfinite(arr)]
        if arr.size >= 2:
            d_lo, d_hi = float(arr.min()), float(arr.max())
            if d_hi > d_lo:
                return d_lo, d_hi
    return None


def sanitize_clipping_ranges(clips: dict | None, data_by_role: dict | None = None) -> tuple[dict, list[str]]:
    """Validate a role→(lo, hi) clipping dict; returns (clean dict, fix notes)."""
    clean: dict[str, tuple[float, float]] = {}
    notes: list[str] = []
    for role, pair in (clips or {}).items():
        if not pair or len(pair) < 2:
            continue
        data = (data_by_role or {}).get(role)
        fixed = sanitize_bounds(pair[0], pair[1], data)
        if fixed is None:
            notes.append(f"Dropped invalid {str(role).upper()} clipping range {pair!r}.")
            continue
        if (float(pair[0]), float(pair[1])) != fixed:
            notes.append(f"Corrected {str(role).upper()} clipping range {pair!r} → ({fixed[0]:g}, {fixed[1]:g}).")
        clean[role] = fixed
    return clean, notes


def resolve_lhs_mapping(columns) -> dict[str, str | None]:
    """Match dataframe columns to the canonical LHS roles (case-insensitive,
    exact name first, then substring)."""
    cols = list(columns)
    lowered = {str(c).strip().lower(): c for c in cols}
    mapping: dict[str, str | None] = {}
    for role, aliases in LHS_CANONICAL_ROLES.items():
        found = None
        for alias in aliases:
            if alias.lower() in lowered:
                found = lowered[alias.lower()]
                break
        if found is None:
            for alias in aliases:
                for low, original in lowered.items():
                    if alias.lower() in low:
                        found = original
                        break
                if found is not None:
                    break
        mapping[role] = found
    return mapping


def prepare_lhs_scatter(df: pd.DataFrame, mapping: dict | None = None,
                        clips: dict | None = None) -> dict:
    """Return clean, correctly bounded coordinate arrays for 3-D rendering.

    - Enforces the canonical X=lhs_flow, Y=lhs_area, Z=lhs_ea,
      colour=total_VHPR_mean assignment (overridable via ``mapping``).
    - Drops non-convergent / failed samples (NaN or ±inf in any mapped
      column) so solver dropouts cannot corrupt the bounding box.
    - Sanitizes every clipping range (min <= max, data-derived fallback)
      and filters spatial axes to the clipped window.
    - Returns per-axis bounds ready for ``set_xlim3d``-style calls.
    """
    roles = dict(mapping or resolve_lhs_mapping(df.columns))
    missing = [r for r in ("x", "y", "z") if not roles.get(r) or roles[r] not in df.columns]
    if missing:
        raise ValueError(f"LHS mapping incomplete — no column found for role(s): {', '.join(missing)}")
    use_cols = [roles[r] for r in ("x", "y", "z") if roles.get(r)]
    if roles.get("w") and roles["w"] in df.columns:
        use_cols.append(roles["w"])
    frame = df[list(dict.fromkeys(use_cols))].apply(pd.to_numeric, errors="coerce")
    frame = frame.replace([np.inf, -np.inf], np.nan)
    total = len(frame)
    frame = frame.dropna()
    dropped = total - len(frame)
    if len(frame) < 3:
        raise ValueError("Fewer than three convergent LHS samples remain after removing failed points.")

    data_by_role = {r: frame[roles[r]].to_numpy(float) for r in ("x", "y", "z", "w")
                    if roles.get(r) and roles[r] in frame.columns}
    clean_clips, notes = sanitize_clipping_ranges(clips, data_by_role)
    keep = np.ones(len(frame), dtype=bool)
    for role in ("x", "y", "z"):
        clip = clean_clips.get(role)
        if clip:
            v = data_by_role[role]
            keep &= (v >= clip[0]) & (v <= clip[1])
    if keep.any() and not keep.all():
        frame = frame.loc[keep]
        notes.append(f"Clipped {int((~keep).sum()):,} sample(s) outside the sanitized axis windows.")
        data_by_role = {r: frame[roles[r]].to_numpy(float) for r in data_by_role}
    elif not keep.any():
        notes.append("Clipping windows excluded every sample; returning the unclipped convergent set instead.")

    bounds = {}
    for role, values in data_by_role.items():
        clip = clean_clips.get(role)
        bounds[role] = clip if clip else sanitize_bounds(np.nan, np.nan, values)

    return {
        "mapping": roles,
        "x": data_by_role.get("x"),
        "y": data_by_role.get("y"),
        "z": data_by_role.get("z"),
        "color": data_by_role.get("w"),
        "bounds": bounds,
        "dropped_failed": int(dropped),
        "notes": notes,
    }
