"""Dimensional analysis for column labels, backed by Pint.

The native side already reads a unit out of a label - "Pressure [kPa]" - and
converts between a table of about thirty pairs. That is enough for an axis and
deliberately lives in C++ so it works with no add-on installed.

This is the other half: real dimensional analysis. It knows compound units, so
mA/cm2 and mg/(L*h) convert as readily as kPa, and it can say that two columns
are not the same kind of quantity at all - which is the mistake that survives
peer review, because a plot of pressure against temperature looks fine.
"""
from __future__ import annotations

import re
from typing import Any

_UNIT_IN_LABEL = re.compile(r"(?:\[([^\]]+)\]|\(([^()]+)\))\s*$")

# A unit name followed by a bare exponent: cm2, m3, s-1. Not applied to a name
# that already carries ** , and not to a leading number, so "10 m" is untouched.
_EXPONENT = re.compile(r"(?<![*\d])([A-Za-z\u00b0\u03a9]+)(-?[2-9]|-1)(?![A-Za-z\d])")

# Spellings instruments produce that Pint does not accept as written.
_SPELLINGS = {
    "degc": "degC", "°c": "degC", "oc": "degC",
    "degf": "degF", "°f": "degF",
    "µ": "u", "μ": "u",
    "ohm": "ohm", "Ω": "ohm",
    "rpm": "revolutions_per_minute",
    "psu": "dimensionless",          # practical salinity is dimensionless
    "ntu": "dimensionless",          # turbidity units are instrument-defined
    "au": "dimensionless",           # absorbance / arbitrary units
    "abs": "dimensionless",
    "%": "percent",
    "v": "volt", "a": "ampere",
}


class UnitError(ValueError):
    """A unit that cannot be understood or a conversion that is not possible."""


def _registry():
    try:
        import pint
    except ImportError as exc:  # pragma: no cover - exercised by the add-on being absent
        raise UnitError(
            "Dimensional analysis needs the Units component. Add it from Add-ons."
        ) from exc
    global _REGISTRY
    if _REGISTRY is None:
        _REGISTRY = pint.UnitRegistry()
    return _REGISTRY


_REGISTRY = None


def normalise(unit: str) -> str:
    """An instrument's spelling of a unit as something Pint accepts."""
    text = str(unit or "").strip()
    if not text:
        return ""
    lowered = text.lower()
    if lowered in _SPELLINGS:
        return _SPELLINGS[lowered]
    for wrong, right in (("µ", "u"), ("μ", "u"), ("Ω", "ohm"), ("°", "deg")):
        text = text.replace(wrong, right)
    # "mg/L/h" is how a rate gets written and is ambiguous to a parser;
    # the second solidus means "per".
    parts = text.split("/")
    if len(parts) > 2:
        text = parts[0] + "/(" + "*".join(parts[1:]) + ")"

    # Instruments write exponents as trailing digits - cm2, m3, s-1, L-1 - and
    # Pint wants cm**2. This is the single most common reason a real column
    # label fails to parse: current density is almost always written mA/cm2.
    text = _EXPONENT.sub(r"\1**\2", text)
    return text


def unit_of(label: str) -> str:
    """The unit in a trailing [..] or (..), or an empty string."""
    match = _UNIT_IN_LABEL.search(str(label or ""))
    if not match:
        return ""
    return (match.group(1) or match.group(2) or "").strip()


def parse(unit: str) -> dict:
    """What a unit is, dimensionally."""
    registry = _registry()
    text = normalise(unit)
    if not text:
        raise UnitError("No unit to parse.")
    try:
        quantity = registry.Quantity(1.0, text)
    except Exception as exc:  # noqa: BLE001 - pint raises several types
        raise UnitError(f"'{unit}' is not a unit this can read: {exc}") from exc
    return {
        "unit": str(quantity.units),
        "input": str(unit),
        "dimensionality": str(quantity.dimensionality),
        "base": str(quantity.to_base_units().units),
        "base_factor": float(quantity.to_base_units().magnitude),
    }


def convert(values, source: str, target: str) -> dict:
    """A column converted from one unit to another.

    Refuses across dimensions rather than producing a number: metres to seconds
    is not a conversion, and silently returning one is worse than an error.
    """
    import numpy as np

    registry = _registry()
    src, dst = normalise(source), normalise(target)
    if not src or not dst:
        raise UnitError("Both a source and a target unit are needed.")
    try:
        quantity = registry.Quantity(np.asarray(values, dtype=float), src)
        converted = quantity.to(dst)
    except Exception as exc:  # noqa: BLE001
        raise UnitError(f"Cannot convert {source} to {target}: {exc}") from exc
    out = np.asarray(converted.magnitude, dtype=float)
    finite = np.isfinite(out)
    result = out.tolist()
    if not finite.all():
        for i in np.flatnonzero(~finite):
            result[int(i)] = None
    return {"values": result, "source": str(quantity.units),
            "target": str(converted.units),
            "dimensionality": str(converted.dimensionality)}


def compatible(source: str, target: str) -> dict:
    """Whether two units describe the same kind of quantity."""
    registry = _registry()
    try:
        a = registry.Quantity(1.0, normalise(source))
        b = registry.Quantity(1.0, normalise(target))
    except Exception as exc:  # noqa: BLE001
        raise UnitError(f"Cannot compare {source} and {target}: {exc}") from exc
    same = a.dimensionality == b.dimensionality
    return {"compatible": bool(same),
            "source_dimensionality": str(a.dimensionality),
            "target_dimensionality": str(b.dimensionality),
            "factor": float(a.to(b.units).magnitude) if same else None}


def conversions_for(unit: str, *, limit: int = 24) -> dict:
    """Units this one can be converted to, from the families it belongs to."""
    registry = _registry()
    text = normalise(unit)
    if not text:
        return {"unit": "", "targets": []}
    try:
        quantity = registry.Quantity(1.0, text)
        compatible_units = registry.get_compatible_units(quantity.dimensionality)
    except Exception as exc:  # noqa: BLE001
        raise UnitError(f"'{unit}' is not a unit this can read: {exc}") from exc
    names = sorted({str(u) for u in compatible_units})
    return {"unit": str(quantity.units),
            "dimensionality": str(quantity.dimensionality),
            "targets": names[:max(1, int(limit))]}


def audit(labels) -> dict:
    """Read every column label and report what carries a unit.

    The useful half is the mismatches: two columns with the same name and
    different units, or a label whose unit is not a unit at all.
    """
    seen: dict[str, list[str]] = {}
    parsed, unreadable, bare = [], [], []
    for label in labels or []:
        unit = unit_of(label)
        if not unit:
            bare.append(str(label))
            continue
        try:
            info = parse(unit)
        except UnitError as exc:
            unreadable.append({"column": str(label), "unit": unit, "why": str(exc)})
            continue
        parsed.append({"column": str(label), "unit": info["unit"],
                       "dimensionality": info["dimensionality"]})
        seen.setdefault(info["dimensionality"], []).append(str(label))
    return {"with_units": parsed, "without_units": bare,
            "unreadable": unreadable,
            "by_dimension": {k: v for k, v in sorted(seen.items())}}
