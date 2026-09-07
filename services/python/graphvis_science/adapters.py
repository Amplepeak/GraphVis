"""Adapters for engines whose arguments cannot cross a JSON pipe.

Most analysis engines take arrays and numbers, and a request can name columns
for those directly. A few cannot be called that way at all, and were therefore
unreachable from the application no matter how the dispatch was written:

* ``CalculusEngine.surface_area`` and ``volume_2d`` take a 2-D height grid.
  A request names columns, which are 1-D. These grid the scattered points
  first, with the same estimator the Surface panel uses, so the answer is the
  area or volume of the surface the user can already see.

* ``ReliabilityEngine.monte_carlo`` takes a Python callable and scipy frozen
  distributions. Neither survives JSON. These build both from a declaration:
  an expression string and named distributions.

* ``CurveFittingEngine.global_linear`` takes a sequence of (x, y) tuples,
  which JSON renders as nested arrays that need normalising.

The expression side is what ``analysis/expression.py`` is for. It was
unreachable - the seven Function and Implicit plot engines are evaluated by the
hand-written C++ parser in native/plot2d instead - and would have been dead code
to delete. It earns its place here: it is a SymPy front end that validates what
it parses, which is what makes "type a model and propagate uncertainty through
it" safe to offer at all.
"""
from __future__ import annotations

from typing import Any, Mapping, Sequence

# Distribution kinds a request may name, and the scipy family behind each.
# Names rather than free-form construction: a request must not be able to reach
# arbitrary scipy attributes.
_DISTRIBUTIONS = {
    "normal": ("norm", ("loc", "scale")),
    "uniform": ("uniform", ("loc", "scale")),
    "lognormal": ("lognorm", ("s", "loc", "scale")),
    "exponential": ("expon", ("loc", "scale")),
    "triangular": ("triang", ("c", "loc", "scale")),
    "weibull": ("weibull_min", ("c", "loc", "scale")),
    "beta": ("beta", ("a", "b", "loc", "scale")),
    "gamma": ("gamma", ("a", "loc", "scale")),
}


def distribution_kinds() -> list[dict]:
    """What a request may ask for, and the parameters each kind takes."""
    return [{"kind": kind, "scipy": family, "parameters": list(params)}
            for kind, (family, params) in sorted(_DISTRIBUTIONS.items())]


def _frozen(name: str, spec: Mapping[str, Any]):
    from scipy import stats

    if not isinstance(spec, Mapping):
        raise ValueError(f"'{name}' needs a distribution like "
                         f"{{'kind': 'normal', 'loc': 1, 'scale': 0.1}}")
    kind = str(spec.get("kind", "normal")).lower()
    if kind not in _DISTRIBUTIONS:
        raise ValueError(f"unknown distribution '{kind}' for '{name}'. "
                         f"Choose one of: {', '.join(sorted(_DISTRIBUTIONS))}")
    family, params = _DISTRIBUTIONS[kind]
    kwargs = {p: float(spec[p]) for p in params if p in spec}
    if kind == "uniform" and "low" in spec and "high" in spec:
        # The friendlier spelling, since scipy's uniform is loc/scale.
        kwargs = {"loc": float(spec["low"]),
                  "scale": float(spec["high"]) - float(spec["low"])}
    return getattr(stats, family)(**kwargs)


def monte_carlo_expression(model: str, distributions: Mapping[str, Any], *,
                           n: int = 10000, seed: int = 0):
    """Propagate named input distributions through an expression.

    ``model`` is an expression over the distribution names - "a * b + c" - and
    is parsed by SymPy with the same validation the function-plot engines use,
    so it cannot reach an import, an attribute or the filesystem.
    """
    import numpy as np

    from graphvis_science.analysis.expression import parse_expression, _lambdify
    from graphvis_science.analysis.reliability import ReliabilityEngine

    names = list(distributions or {})
    if not names:
        raise ValueError("Monte-Carlo needs at least one named distribution.")
    expr, symbols = _expression_and_symbols(parse_expression, model, names)
    fn = _lambdify(expr, symbols)
    frozen = {name: _frozen(name, spec) for name, spec in distributions.items()}

    def call(**samples):
        ordered = [np.asarray(samples[name], dtype=float) for name in names]
        return np.asarray(fn(*ordered), dtype=float)

    return ReliabilityEngine.monte_carlo(call, frozen, n=int(n), seed=int(seed))


def _expression_and_symbols(parse, source: str, variables: Sequence[str]):
    """The parsed expression plus its symbols, in the order given.

    parse_expression returns ``(expression, {name: symbol})``. lambdify needs a
    sequence, and the ORDER matters - it decides which sampled array binds to
    which name, so a dict passed straight through would silently transpose the
    inputs even where it did not fail outright.
    """
    parsed = parse(source, tuple(variables))
    expr, symbols = parsed if isinstance(parsed, tuple) else (parsed, None)
    if isinstance(symbols, dict):
        return expr, [symbols[name] for name in variables]
    if symbols is not None:
        return expr, list(symbols)
    import sympy as sp
    return expr, [sp.symbols(v, real=True) for v in variables]


def _grid_from_points(x, y, z, *, resolution: int, estimator: str):
    from graphvis_science.analysis.surfaces import interpolate_surface
    return interpolate_surface(x, y, z, resolution=int(resolution),
                               estimator=estimator)


def surface_area_from_points(x, y, z, *, resolution: int = 120,
                             estimator: str = "Auto (data-aware)") -> dict:
    """Surface area of the surface through scattered points."""
    from graphvis_science.analysis.signal import CalculusEngine

    grid = _grid_from_points(x, y, z, resolution=resolution, estimator=estimator)
    area = CalculusEngine.surface_area(grid.Z, grid.X, grid.Y)
    return {"area": float(area), "method": grid.method,
            "resolution": int(resolution), "notes": list(grid.notes or [])}


def volume_from_points(x, y, z, *, resolution: int = 120,
                       estimator: str = "Auto (data-aware)") -> dict:
    """Volume under the surface through scattered points."""
    import numpy as np

    from graphvis_science.analysis.signal import CalculusEngine

    grid = _grid_from_points(x, y, z, resolution=resolution, estimator=estimator)
    # The axes are the grid's own, taken from its first row and column: the
    # engine integrates over spacing, so passing indices would report a volume
    # in cells rather than in the data's units.
    xs = np.asarray(grid.X)[0, :] if np.asarray(grid.X).ndim == 2 else np.asarray(grid.X)
    ys = np.asarray(grid.Y)[:, 0] if np.asarray(grid.Y).ndim == 2 else np.asarray(grid.Y)
    volume = CalculusEngine.volume_2d(np.nan_to_num(grid.Z, nan=0.0), xs, ys)
    return {"volume": float(volume), "method": grid.method,
            "resolution": int(resolution), "notes": list(grid.notes or [])}


def global_linear_pairs(datasets: Sequence[Any], *, shared_slope: bool = True):
    """A global linear fit from JSON-shaped datasets.

    Accepts either [[xs, ys], ...] or [{"x": [...], "y": [...]}, ...]; both are
    what a caller naturally writes, and the engine wants tuples.
    """
    from graphvis_science.analysis.fitting import CurveFittingEngine

    pairs = []
    for i, entry in enumerate(datasets or []):
        if isinstance(entry, Mapping):
            xs, ys = entry.get("x"), entry.get("y")
        elif isinstance(entry, (list, tuple)) and len(entry) == 2:
            xs, ys = entry
        else:
            raise ValueError(f"dataset {i} should be [x, y] or "
                             f"{{'x': [...], 'y': [...]}}")
        if xs is None or ys is None:
            raise ValueError(f"dataset {i} is missing x or y")
        pairs.append((list(xs), list(ys)))
    if not pairs:
        raise ValueError("A global fit needs at least one dataset.")
    return CurveFittingEngine.global_linear(pairs, shared_slope=bool(shared_slope))


def function_curve(source: str, *, domain: str | None = None,
                   samples: int = 1200) -> dict:
    """y = f(x) over a domain, as a curve the plot catalogue can draw.

    The native C++ parser already evaluates the Function engines for drawing.
    This is the symbolic route: it returns the points AND the exact derivative,
    which the C++ parser cannot produce.
    """
    from graphvis_science.analysis.expression import (evaluate_1d, parse_domain,
                                                      parse_expression)

    bounds = parse_domain(domain, 1, (-10.0, 10.0))[0]
    result = evaluate_1d(source, tuple(bounds), int(samples))
    payload = {"x": list(map(float, result.x)), "y": list(map(float, result.y)),
               "domain": [float(bounds[0]), float(bounds[1])],
               "expression": str(source)}
    try:
        import sympy as sp

        expr, symbols = _expression_and_symbols(parse_expression, source, ("x",))
        payload["derivative"] = str(sp.diff(expr, symbols[0]))
        payload["simplified"] = str(sp.simplify(expr))
    except Exception:  # noqa: BLE001 - the curve is the answer; symbolic extras are a bonus
        pass
    return payload


def parametric_curve(source: str, *, domain: str | None = None,
                     samples: int = 1200) -> dict:
    """x(t); y(t); z(t) over a range of t, as a three-dimensional curve.

    The native parser draws y = f(x) and nothing else, so a parametric curve -
    a helix, a Lissajous figure, an orbit - had no route to a plot at all even
    though the evaluator for it was written.
    """
    from graphvis_science.analysis.expression import evaluate_parametric3d, parse_domain

    bounds = parse_domain(domain, 1, (-10.0, 10.0))[0]
    result = evaluate_parametric3d(source, tuple(bounds), int(samples))
    return {"x": list(map(float, result.x)), "y": list(map(float, result.y)),
            "z": list(map(float, result.z)),
            "domain": [float(bounds[0]), float(bounds[1])],
            "expression": str(source)}


def implicit_field(source: str, *, x_domain: str | None = None,
                   y_domain: str | None = None, samples: int = 120) -> dict:
    """f(x, y) for an implicit equation, as a grid whose zero contour is the curve.

    ``x**2 + y**2 = 4`` is rearranged to ``(x**2 + y**2) - (4)`` and evaluated,
    so the shape the equation describes is the level set at zero. Written as an
    equation it is what a reader recognises; written as a difference it is what
    can be drawn.
    """
    from graphvis_science.analysis.expression import (evaluate_2d, parse_domain,
                                                      split_equation)

    difference = split_equation(source)
    xb = parse_domain(x_domain, 1, (-5.0, 5.0))[0]
    yb = parse_domain(y_domain, 1, (-5.0, 5.0))[0]
    result = evaluate_2d(difference, tuple(xb), tuple(yb), int(samples))
    return {"x": list(map(float, result.x)), "y": list(map(float, result.y)),
            "Z": [[float(v) for v in row] for row in result.Z],
            "equation": str(source), "difference": difference,
            "contour": 0.0}


def function_volume(source: str, *, x_domain: str | None = None,
                    y_domain: str | None = None, z_domain: str | None = None,
                    samples: int = 32) -> dict:
    """v = f(x, y, z) on a regular grid, for a volume or an isosurface.

    Deliberately coarse by default: the grid is cubic, so 32 is already 32,768
    values and 64 would be a quarter of a million crossing a JSON pipe.
    """
    from graphvis_science.analysis.expression import evaluate_3d, parse_domain

    xb = parse_domain(x_domain, 1, (-3.0, 3.0))[0]
    yb = parse_domain(y_domain, 1, (-3.0, 3.0))[0]
    zb = parse_domain(z_domain, 1, (-3.0, 3.0))[0]
    # The engine's own floor is 24 per axis; asking for fewer silently gets 24,
    # so the request is clamped here and the reply reports what was used.
    n = max(24, min(int(samples), 48))
    result = evaluate_3d(source, tuple(xb), tuple(yb), tuple(zb), n)
    return {"x": list(map(float, result.x)), "y": list(map(float, result.y)),
            "z": list(map(float, result.z)),
            "V": [[[float(v) for v in column] for column in plane] for plane in result.V],
            "shape": [int(s) for s in result.V.shape],
            "expression": str(source)}


def fft_lowpass(y, x=None, *, keep: float = 0.1) -> dict:
    """Rebuild a signal from the lowest fraction of its spectrum.

    Forward transform, discard everything above ``keep`` of the frequency
    range, inverse transform. It is the smoothing that does not shift a peak or
    round a step the way a moving average does - and it is the only sane way to
    reach the inverse transform from a panel that can only offer columns, since
    a complex spectrum cannot be typed into a text box.
    """
    import numpy as np

    from graphvis_science.analysis.signal import SignalEngine

    spectrum = SignalEngine.fft(y, x)
    complex_spectrum = np.asarray(spectrum.details["complex_spectrum"], dtype=complex)
    fraction = float(min(max(keep, 1e-4), 1.0))
    kept = max(1, int(round(complex_spectrum.size * fraction)))
    filtered = complex_spectrum.copy()
    filtered[kept:] = 0.0
    rebuilt = SignalEngine.ifft(filtered, float(spectrum.details["sample_spacing"]))

    values = np.asarray(rebuilt.y, dtype=float)
    original = np.asarray(y, dtype=float).ravel()
    # irfft returns an even-length signal; an odd-length input comes back one
    # sample short, and a residual needs the two to be the same length.
    length = min(values.size, original.size)
    residual = original[:length] - values[:length]
    return {"x": [float(v) for v in np.asarray(rebuilt.x, dtype=float)[:length]],
            "y": [float(v) for v in values[:length]],
            "residual": [float(v) for v in residual],
            "components_kept": int(kept),
            "components_total": int(complex_spectrum.size),
            "cutoff_fraction": fraction,
            "residual_rms": float(np.sqrt(np.mean(residual ** 2))) if length else 0.0}


def function_surface(source: str, *, x_domain: str | None = None,
                     y_domain: str | None = None, samples: int = 80) -> dict:
    """z = f(x, y) over a rectangle, as a grid."""
    from graphvis_science.analysis.expression import evaluate_2d, parse_domain

    xb = parse_domain(x_domain, 1, (-5.0, 5.0))[0]
    yb = parse_domain(y_domain, 1, (-5.0, 5.0))[0]
    result = evaluate_2d(source, tuple(xb), tuple(yb), int(samples))
    return {"x": list(map(float, result.x)), "y": list(map(float, result.y)),
            "Z": [[float(v) for v in row] for row in result.Z],
            "expression": str(source)}


# The analyses inside analyse_limits are all OFF in DEFAULT_LIMIT_OPTIONS -
# a reasonable library default, and the wrong one for a button labelled
# "plateau, asymptote, knee". Wired straight through, that button returned an
# empty report every single time, which looked like data with no features
# rather than a request that asked for nothing.
#
# The three cheap analyses are on here. The two that bootstrap - the confidence
# envelope and the Pareto bounds - stay off unless asked, because they cost
# hundreds of resamples and have their own operations.
LIMIT_DEFAULTS = {"plateau": True, "asymptote": True, "knee": True,
                  "confidence": False, "pareto_bounds": False}


def limits_report(x, y, *, options: Mapping[str, Any] | None = None):
    from graphvis_science.analysis.limits import analyse_limits

    merged = dict(LIMIT_DEFAULTS)
    if options:
        merged.update({k: v for k, v in options.items() if v is not None})
    return analyse_limits(x, y, merged)


def unit_audit(frame, labels=None) -> dict:
    """Audit the labels named, or the dataset's own column labels.

    The engine takes a list. A request that names none should still get an
    answer about the dataset in front of the user rather than a TypeError, so
    the frame's columns are the default.
    """
    from graphvis_science import units

    # Not `columns or []`: a pandas Index has no truth value, and asking for
    # one raises "The truth value of a Index is ambiguous" - which is how this
    # failed the first time it was run for real rather than read.
    if labels:
        names = [str(label) for label in labels]
    else:
        columns = getattr(frame, "columns", None)
        names = [] if columns is None else [str(label) for label in columns]
    return units.audit(names)


def advisor_recommendations(dataset, literature=None) -> dict:
    """Graph recommendations as named pairs.

    The engine returns a list of (engine, why) tuples, which JSON renders as
    nested arrays - readable by a machine, awkward for a panel. Naming the two
    halves means the UI can show them without knowing the order.
    """
    from graphvis_science.analysis.electro import intelligent_visualization_advisor

    pairs = intelligent_visualization_advisor(dataset, literature) or []
    return {"recommendations": [{"engine": str(a), "why": str(b)}
                                for a, b in pairs]}
