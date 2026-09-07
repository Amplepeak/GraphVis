# =========================================================================
# expression_engine.py - safe symbolic/numeric expression evaluation for
# GraphVis function plots (fplot/fplot3/fimplicit/fimplicit3/fcontour/fsurf/fmesh).
# =========================================================================
from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

try:
    import sympy as sp
except Exception:  # pragma: no cover
    sp = None


_ALLOWED_NAMES = {
    "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh",
    "exp", "log", "ln", "log10", "sqrt", "abs", "sign", "floor", "ceil", "pi", "E",
    "min", "max", "where", "heaviside",
}


class ExpressionError(ValueError):
    pass


def _require_sympy():
    if sp is None:
        raise ExpressionError("SymPy is required for function-expression plots. Install 'sympy'.")


def _locals(symbols: dict[str, object]) -> dict:
    _require_sympy()
    out = dict(symbols)
    out.update({
        "sin": sp.sin, "cos": sp.cos, "tan": sp.tan,
        "asin": sp.asin, "acos": sp.acos, "atan": sp.atan, "atan2": sp.atan2,
        "sinh": sp.sinh, "cosh": sp.cosh, "tanh": sp.tanh,
        "exp": sp.exp, "log": sp.log, "ln": sp.log, "log10": lambda x: sp.log(x, 10),
        "sqrt": sp.sqrt, "abs": sp.Abs, "sign": sp.sign, "floor": sp.floor, "ceil": sp.ceiling,
        "pi": sp.pi, "E": sp.E, "heaviside": sp.Heaviside,
    })
    return out


def _validate_source(source: str):
    bad = ("__", "import", "lambda", "exec", "eval", "open(", "os.", "sys.", "subprocess")
    lower = str(source).lower()
    if any(token in lower for token in bad):
        raise ExpressionError("Unsafe token in expression.")


def parse_expression(source: str, variables=("x",)):
    _require_sympy()
    source = str(source or "").strip()
    if not source:
        raise ExpressionError("Expression is empty.")
    _validate_source(source)
    syms = {name: sp.symbols(name, real=True) for name in variables}
    try:
        expr = sp.sympify(source.replace("^", "**"), locals=_locals(syms), evaluate=True)
    except Exception as exc:
        raise ExpressionError(f"Could not parse expression: {exc}") from exc
    unknown = {str(s) for s in expr.free_symbols} - set(variables)
    if unknown:
        raise ExpressionError("Unknown symbols: " + ", ".join(sorted(unknown)))
    return expr, syms


def _lambdify(expr, symbols):
    _require_sympy()
    modules = [{"Abs": np.abs, "Heaviside": np.heaviside}, "numpy"]
    return sp.lambdify(symbols, expr, modules=modules)


def parse_domain(text: str | None, dimensions: int = 1, default=(-10.0, 10.0)) -> list[tuple[float, float]]:
    """Parse domains such as '-5,5' or '-5,5; -2,8'."""
    if not text:
        return [tuple(map(float, default)) for _ in range(dimensions)]
    chunks = [c.strip() for c in str(text).split(";") if c.strip()]
    out = []
    for chunk in chunks:
        chunk = chunk.strip().strip("[]()")
        bits = [b.strip() for b in chunk.split(",") if b.strip()]
        if len(bits) != 2:
            raise ExpressionError("Domains must use 'min,max' pairs; separate dimensions with ';'.")
        lo, hi = float(bits[0]), float(bits[1])
        if not np.isfinite([lo, hi]).all() or lo == hi:
            raise ExpressionError("Domain bounds must be finite and different.")
        out.append((min(lo, hi), max(lo, hi)))
    while len(out) < dimensions:
        out.append(out[-1] if out else tuple(map(float, default)))
    return out[:dimensions]


@dataclass
class Function1DResult:
    x: np.ndarray
    y: np.ndarray


def evaluate_1d(source: str, domain=(-10.0, 10.0), n=1200) -> Function1DResult:
    expr, syms = parse_expression(source, ("x",))
    x = np.linspace(float(domain[0]), float(domain[1]), int(max(n, 50)))
    fn = _lambdify(expr, syms["x"])
    with np.errstate(all="ignore"):
        y = np.asarray(fn(x), dtype=float)
    if y.ndim == 0:
        y = np.full_like(x, float(y))
    y = np.array(np.broadcast_to(y, x.shape), dtype=float, copy=True)
    y[~np.isfinite(y)] = np.nan
    return Function1DResult(x, y)


@dataclass
class Parametric3DResult:
    x: np.ndarray
    y: np.ndarray
    z: np.ndarray


def evaluate_parametric3d(source: str, domain=(-10.0, 10.0), n=1200) -> Parametric3DResult:
    """Evaluate `x(t); y(t); z(t)` for fplot3."""
    parts = [p.strip() for p in str(source).split(";") if p.strip()]
    if len(parts) != 3:
        raise ExpressionError("fplot3 expects three expressions separated by semicolons: x(t); y(t); z(t).")
    t = np.linspace(float(domain[0]), float(domain[1]), int(max(n, 50)))
    values = []
    for src in parts:
        expr, syms = parse_expression(src, ("t",))
        fn = _lambdify(expr, syms["t"])
        with np.errstate(all="ignore"):
            val = np.asarray(fn(t), dtype=float)
        if val.ndim == 0:
            val = np.full_like(t, float(val))
        val = np.array(np.broadcast_to(val, t.shape), dtype=float, copy=True)
        val[~np.isfinite(val)] = np.nan
        values.append(val)
    return Parametric3DResult(*values)


@dataclass
class Field2DResult:
    x: np.ndarray
    y: np.ndarray
    X: np.ndarray
    Y: np.ndarray
    Z: np.ndarray


def evaluate_2d(source: str, x_domain=(-5.0, 5.0), y_domain=(-5.0, 5.0), n=240) -> Field2DResult:
    expr, syms = parse_expression(source, ("x", "y"))
    x = np.linspace(*map(float, x_domain), int(max(n, 40)))
    y = np.linspace(*map(float, y_domain), int(max(n, 40)))
    X, Y = np.meshgrid(x, y)
    fn = _lambdify(expr, (syms["x"], syms["y"]))
    with np.errstate(all="ignore"):
        Z = np.asarray(fn(X, Y), dtype=float)
    if Z.ndim == 0:
        Z = np.full_like(X, float(Z))
    Z = np.array(np.broadcast_to(Z, X.shape), dtype=float, copy=True)
    Z[~np.isfinite(Z)] = np.nan
    return Field2DResult(x, y, X, Y, Z)


@dataclass
class Field3DResult:
    x: np.ndarray
    y: np.ndarray
    z: np.ndarray
    X: np.ndarray
    Y: np.ndarray
    Z: np.ndarray
    V: np.ndarray


def evaluate_3d(source: str, x_domain=(-3.0, 3.0), y_domain=(-3.0, 3.0), z_domain=(-3.0, 3.0), n=64) -> Field3DResult:
    expr, syms = parse_expression(source, ("x", "y", "z"))
    n = int(np.clip(n, 24, 120))
    x = np.linspace(*map(float, x_domain), n)
    y = np.linspace(*map(float, y_domain), n)
    z = np.linspace(*map(float, z_domain), n)
    X, Y, Z = np.meshgrid(x, y, z, indexing="ij")
    fn = _lambdify(expr, (syms["x"], syms["y"], syms["z"]))
    with np.errstate(all="ignore"):
        V = np.asarray(fn(X, Y, Z), dtype=float)
    if V.ndim == 0:
        V = np.full_like(X, float(V))
    V = np.array(np.broadcast_to(V, X.shape), dtype=float, copy=True)
    V[~np.isfinite(V)] = np.nan
    return Field3DResult(x, y, z, X, Y, Z, V)


def split_equation(source: str) -> str:
    """Turn `lhs = rhs` into `(lhs) - (rhs)` for implicit plots."""
    source = str(source or "").strip()
    if "=" in source and "==" not in source:
        lhs, rhs = source.split("=", 1)
        return f"({lhs})-({rhs})"
    return source
