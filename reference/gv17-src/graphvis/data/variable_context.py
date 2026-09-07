# =========================================================================
# variable_context.py - variable-name context parsing, units and conversions.
# =========================================================================
from __future__ import annotations

import re
from dataclasses import dataclass


_UNIT_RE = re.compile(r"(?:\[([^\]]+)\]|\(([^()]+)\))\s*$")


def extract_unit(label: str) -> str | None:
    m = _UNIT_RE.search(str(label or ""))
    if not m:
        return None
    unit = (m.group(1) or m.group(2) or "").strip()
    return unit or None


def strip_unit(label: str) -> str:
    return _UNIT_RE.sub("", str(label or "")).strip()


def parse_variable_context(text: str) -> dict[str, dict]:
    """Parse lightweight Python/MATLAB/comment definitions.

    Accepted examples:
      T = 298.15  # Temperature [K]
      T = ... % Temperature (degC)
      T: Temperature [K]
      # T - Temperature [K]
      Temperature (T) [K]
    """
    out: dict[str, dict] = {}
    for raw in str(text or "").splitlines():
        line = raw.strip()
        if not line:
            continue
        line = re.sub(r"^\s*(#|//|%)\s*", "", line)
        # Main name (short) [unit]
        m = re.match(r"(.+?)\s*\(([A-Za-z_]\w*)\)\s*(?:\[([^\]]+)\])?\s*$", line)
        if m and len(m.group(1).strip()) > 1:
            short = m.group(2)
            out[short] = {"name": m.group(1).strip(), "unit": (m.group(3) or "").strip()}
            continue
        # short : long or short - long
        m = re.match(r"([A-Za-z_]\w*)\s*(?::|\s+-\s+)\s*(.+)$", line)
        if m:
            short, desc = m.group(1), m.group(2).strip()
            unit = extract_unit(desc) or ""
            out[short] = {"name": strip_unit(desc), "unit": unit}
            continue
        # assignment plus trailing comment
        m = re.match(r"([A-Za-z_]\w*)\s*=.*?(?:#|//|%)\s*(.+)$", raw)
        if m:
            short, desc = m.group(1), m.group(2).strip()
            unit = extract_unit(desc) or ""
            out[short] = {"name": strip_unit(desc), "unit": unit}
            continue
    return out


def infer_display(raw: str, context: dict[str, dict] | None = None) -> tuple[str, str, str | None]:
    """Return (main_name, short_name, unit)."""
    raw = str(raw)
    context = context or {}
    if raw in context:
        rec = context[raw]
        return rec.get("name") or raw, raw, rec.get("unit") or extract_unit(raw)
    base = strip_unit(raw)
    unit = extract_unit(raw)
    pretty = base.replace("lhs_", "").replace("_", " ").strip()
    pretty = re.sub(r"\s+", " ", pretty).title() or raw
    return pretty, raw, unit


@dataclass(frozen=True)
class UnitConversion:
    source: str
    target: str
    factor: float
    offset: float = 0.0

    def apply(self, value):
        return value * self.factor + self.offset


# value_target = value_source * factor + offset
_CONVERSIONS = [
    UnitConversion("mv", "V", 1e-3), UnitConversion("V", "mV", 1e3),
    UnitConversion("uv", "V", 1e-6), UnitConversion("V", "uV", 1e6),
    UnitConversion("kv", "V", 1e3), UnitConversion("V", "kV", 1e-3),
    UnitConversion("ma", "A", 1e-3), UnitConversion("A", "mA", 1e3),
    UnitConversion("ua", "A", 1e-6), UnitConversion("A", "uA", 1e6),
    UnitConversion("ms", "s", 1e-3), UnitConversion("s", "ms", 1e3),
    UnitConversion("min", "s", 60.0), UnitConversion("s", "min", 1/60.0),
    UnitConversion("h", "s", 3600.0), UnitConversion("s", "h", 1/3600.0),
    UnitConversion("mm", "m", 1e-3), UnitConversion("m", "mm", 1e3),
    UnitConversion("um", "m", 1e-6), UnitConversion("m", "um", 1e6),
    UnitConversion("nm", "m", 1e-9), UnitConversion("m", "nm", 1e9),
    UnitConversion("kpa", "Pa", 1e3), UnitConversion("Pa", "kPa", 1e-3),
    UnitConversion("mpa", "Pa", 1e6), UnitConversion("Pa", "MPa", 1e-6),
    UnitConversion("bar", "Pa", 1e5), UnitConversion("Pa", "bar", 1e-5),
    UnitConversion("c", "K", 1.0, 273.15), UnitConversion("degc", "K", 1.0, 273.15),
    UnitConversion("°c", "K", 1.0, 273.15), UnitConversion("K", "°C", 1.0, -273.15),
    UnitConversion("g/l", "mg/L", 1000.0), UnitConversion("mg/l", "g/L", 1e-3),
    UnitConversion("mg/L", "g/L", 1e-3), UnitConversion("g/L", "mg/L", 1000.0),
]


def _norm_unit(unit: str | None) -> str:
    if unit is None:
        return ""
    return str(unit).strip().replace("μ", "u").replace("µ", "u")


def conversions_for(unit: str | None) -> list[UnitConversion]:
    u = _norm_unit(unit)
    low = u.lower()
    out = []
    for c in _CONVERSIONS:
        if _norm_unit(c.source).lower() == low:
            out.append(c)
    return out


def replace_unit(label: str, new_unit: str) -> str:
    base = strip_unit(label)
    return f"{base} [{new_unit}]"
