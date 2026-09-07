"""Context-aware literature interpretation for GraphVis.

The engine intentionally has a deterministic heuristic core so it works offline.
If a local Hugging Face zero-shot model is configured through
``GRAPHVIS_TRANSFORMERS_MODEL``, a Transformers enhancer is used to refine the
plot-class prediction without downloading models at runtime.
"""
from __future__ import annotations

import os
import re
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Iterable, Sequence

import numpy as np
import pandas as pd

from graphvis.core.logging import get_logger

LOG = get_logger("literature_semantics")

_NUM = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"


@dataclass(slots=True)
class SemanticContext:
    abstract: str = ""
    captions: list[str] = field(default_factory=list)
    conditions: dict[str, dict[str, str]] = field(default_factory=dict)
    relationships: list[str] = field(default_factory=list)
    inferred_plot_type: str = "Line Chart"
    x_variable: str | None = None
    y_variable: str | None = None
    z_variable: str | None = None
    confidence: float = 0.0
    backend: str = "heuristic"
    notes: list[str] = field(default_factory=list)

    def to_dict(self) -> dict:
        return asdict(self)


_PLOT_KEYWORDS: list[tuple[str, tuple[str, ...]]] = [
    ("EIS: Nyquist", ("nyquist", "z'", "z′", "impedance semicircle")),
    ("EIS: Bode", ("bode", "phase angle", "impedance magnitude")),
    ("Polarisation & Power Curve", ("polarization curve", "polarisation curve", "power density", "current density")),
    ("Gompertz H₂ Kinetics", ("gompertz", "cumulative hydrogen", "cumulative h2", "hydrogen production kinetics")),
    ("2D Heatmap", ("heat map", "heatmap", "colour map", "color map")),
    ("2D Contour", ("contour", "iso-line", "isoline")),
    ("3D Topography / Surface", ("response surface", "3d surface", "surface plot", "topography")),
    ("Histogram", ("histogram", "frequency distribution")),
    ("Box Plot", ("box plot", "box-and-whisker")),
    ("Violin Plot", ("violin plot",)),
    ("Pareto Front", ("pareto", "non-dominated", "trade-off frontier")),
    ("4D / 5D Scatter", ("scatter plot", "scatterplot", "correlation plot")),
    ("Line Chart", ("time series", "trajectory", "profile", "versus", " vs ", "as a function of")),
]

_CONDITION_PATTERNS: dict[str, tuple[str, ...]] = {
    "temperature": (rf"(?:temperature|temp\.?|T)\s*(?:=|of|at)?\s*({_NUM})\s*(°?\s*[CK])",),
    "pH": (rf"\bpH\s*(?:=|of|at)?\s*({_NUM})",),
    "voltage": (rf"(?:applied\s+)?voltage\s*(?:=|of|at)?\s*({_NUM})\s*(m?V)\b",),
    "current_density": (rf"current\s+density\s*(?:=|of|at)?\s*({_NUM})\s*([munµ]?A\s*(?:cm|m)[-−⁻]?2)",),
    "time": (rf"(?:time|duration|reaction\s+time|HRT)\s*(?:=|of|at)?\s*({_NUM})\s*(s|min|h|hr|hours?|days?)\b",),
    "pressure": (rf"(?:pressure)\s*(?:=|of|at)?\s*({_NUM})\s*(Pa|kPa|MPa|bar|atm)\b",),
    "concentration": (rf"(?:concentration|loading|feed)\s*(?:=|of|at)?\s*({_NUM})\s*(mg/L|g/L|mol/L|mM|µM|uM)\b",),
    "flow_rate": (rf"(?:flow\s*rate)\s*(?:=|of|at)?\s*({_NUM})\s*([A-Za-zµμ0-9^/ .-]+)",),
}


def _clean_text(text: str) -> str:
    return re.sub(r"[ \t]+", " ", str(text or "")).replace("\x00", " ")


def extract_abstract(text: str, max_chars: int = 5000) -> str:
    t = _clean_text(text)
    m = re.search(r"(?is)\babstract\b\s*[:\-]?\s*(.*?)(?=\n\s*(?:keywords?|introduction|1\.?\s+introduction)\b)", t)
    if m:
        return re.sub(r"\s+", " ", m.group(1)).strip()[:max_chars]
    # Fall back to the first substantial prose block.
    paras = [re.sub(r"\s+", " ", p).strip() for p in re.split(r"\n\s*\n", t)]
    return next((p[:max_chars] for p in paras if len(p) > 160 and not p.lower().startswith(("figure", "table"))), "")


def extract_captions(text: str, max_items: int = 40) -> list[str]:
    lines = [re.sub(r"\s+", " ", s).strip() for s in str(text or "").splitlines()]
    out: list[str] = []
    i = 0
    while i < len(lines) and len(out) < max_items:
        s = lines[i]
        if re.match(r"(?i)^(?:fig(?:ure)?\.?|table)\s*[sS]?\d+[A-Za-z]?\s*[:.\-]", s):
            parts = [s]
            j = i + 1
            while j < len(lines) and lines[j] and not re.match(r"(?i)^(?:fig(?:ure)?\.?|table)\s*", lines[j]) and len(" ".join(parts)) < 1200:
                if len(lines[j]) > 5:
                    parts.append(lines[j])
                j += 1
            out.append(" ".join(parts))
            i = j
        else:
            i += 1
    return out


def extract_conditions(text: str) -> dict[str, dict[str, str]]:
    t = _clean_text(text)
    out: dict[str, dict[str, str]] = {}
    for key, patterns in _CONDITION_PATTERNS.items():
        for pat in patterns:
            m = re.search(pat, t, flags=re.I)
            if not m:
                continue
            unit = m.group(2).strip() if m.lastindex and m.lastindex >= 2 else ""
            out[key] = {"value": m.group(1), "unit": unit, "source": m.group(0)[:180]}
            break
    return out


def infer_relationships(text: str, max_items: int = 24) -> list[str]:
    sentences = re.split(r"(?<=[.!?])\s+|\n+", _clean_text(text))
    cues = (" as a function of ", " versus ", " vs. ", " vs ", " plotted against ", " dependence of ",
            " increased with ", " decreased with ", " correlated with ", " relationship between ")
    out: list[str] = []
    for s in sentences:
        low = f" {s.lower()} "
        if any(c in low for c in cues) and 20 <= len(s) <= 500:
            out.append(s.strip())
            if len(out) >= max_items:
                break
    return out


def _column_score(name: str, role: str) -> float:
    low = str(name).lower()
    if role == "x":
        keys = ("time", "temp", "voltage", "current", "frequency", "concentration", "pressure", "ph", "distance", "wavelength", "potential")
    elif role == "y":
        keys = ("response", "yield", "rate", "intensity", "absorb", "signal", "current", "voltage", "vhpr", "conversion", "removal", "efficiency", "power")
    else:
        keys = ("z", "height", "response", "intensity", "density")
    return max([1.0 if k == low else 0.78 if k in low else 0.0 for k in keys] + [0.0])


def infer_axes(columns: Sequence[str], context_text: str = "") -> tuple[str | None, str | None, str | None]:
    cols = [str(c) for c in columns]
    if not cols:
        return None, None, None
    text = context_text.lower()
    explicit_x = re.search(r"(?:x[- ]?axis|abscissa)\s*(?:is|=|:)?\s*([A-Za-z][A-Za-z0-9_ /%°µμ^-]{1,80})", text)
    explicit_y = re.search(r"(?:y[- ]?axis|ordinate)\s*(?:is|=|:)?\s*([A-Za-z][A-Za-z0-9_ /%°µμ^-]{1,80})", text)

    def closest(explicit, role: str, exclude: set[str]) -> str | None:
        if explicit:
            phrase = explicit.group(1).lower()
            hits = sorted((sum(tok in phrase for tok in re.findall(r"[a-z0-9]+", c.lower())), c) for c in cols if c not in exclude)
            if hits and hits[-1][0] > 0:
                return hits[-1][1]
        ranked = sorted((_column_score(c, role), -i, c) for i, c in enumerate(cols) if c not in exclude)
        if ranked and ranked[-1][0] > 0:
            return ranked[-1][2]
        return next((c for c in cols if c not in exclude), None)

    x = closest(explicit_x, "x", set())
    y = closest(explicit_y, "y", {x} if x else set())
    z = closest(None, "z", {c for c in (x, y) if c}) if len(cols) >= 3 else None
    return x, y, z


def infer_plot_type(text: str, columns: Sequence[str] = ()) -> tuple[str, float]:
    low = f" {_clean_text(text).lower()} "
    scores: list[tuple[int, str]] = []
    for chart, keys in _PLOT_KEYWORDS:
        score = sum(2 if k in low else 0 for k in keys)
        scores.append((score, chart))
    best_score, best = max(scores, default=(0, "Line Chart"))
    if best_score == 0:
        n = len(columns)
        best = "Line Chart" if n <= 2 else "4D / 5D Scatter"
    return best, min(0.98, 0.45 + 0.08 * best_score)


def _transformer_refine(context: SemanticContext, text: str) -> SemanticContext:
    model_ref = os.environ.get("GRAPHVIS_TRANSFORMERS_MODEL", "").strip()
    if not model_ref:
        return context
    model_path = Path(model_ref).expanduser()
    if not model_path.exists():
        context.notes.append("GRAPHVIS_TRANSFORMERS_MODEL is set but does not point to a local model; heuristic NLP used.")
        return context
    try:
        from transformers import AutoModelForSequenceClassification, AutoTokenizer, pipeline
        labels = [x[0] for x in _PLOT_KEYWORDS]
        tok = AutoTokenizer.from_pretrained(str(model_path), local_files_only=True)
        mdl = AutoModelForSequenceClassification.from_pretrained(str(model_path), local_files_only=True)
        clf = pipeline("zero-shot-classification", model=mdl, tokenizer=tok, device=-1)
        result = clf(text[:5000], labels, multi_label=False)
        if result.get("labels"):
            context.inferred_plot_type = str(result["labels"][0])
            context.confidence = float(result["scores"][0])
            context.backend = "transformers+heuristic"
    except Exception as exc:
        LOG.warning("Transformer semantic enhancer unavailable: %s", exc)
        context.notes.append(f"Transformer enhancer unavailable: {type(exc).__name__}")
    return context


def analyse_literature_context(text: str, datasets: Iterable[pd.DataFrame] = ()) -> SemanticContext:
    abstract = extract_abstract(text)
    captions = extract_captions(text)
    relationships = infer_relationships(text)
    conditions = extract_conditions("\n".join([abstract, *captions, *relationships, text[:15000]]))
    cols: list[str] = []
    for df in datasets:
        if df is not None:
            cols.extend(map(str, df.columns))
    # Preserve order while removing duplicates.
    cols = list(dict.fromkeys(cols))
    evidence = "\n".join([abstract, *captions, *relationships]) or text[:10000]
    chart, confidence = infer_plot_type(evidence, cols)
    x, y, z = infer_axes(cols, evidence)
    ctx = SemanticContext(abstract=abstract, captions=captions, conditions=conditions,
                          relationships=relationships, inferred_plot_type=chart,
                          x_variable=x, y_variable=y, z_variable=z,
                          confidence=confidence, backend="heuristic")
    return _transformer_refine(ctx, evidence)
