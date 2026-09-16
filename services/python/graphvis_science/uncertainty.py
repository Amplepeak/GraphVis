"""Uncertainty propagation, backed by the `uncertainties` package.

A fit gives a parameter and a standard error, and every number computed from
that parameter inherits an uncertainty nobody usually carries through. This does
carry it: give it named values with their errors and an expression, and it
returns the result with the error propagated, plus how much of that error each
input is responsible for.

That last part is the useful one. "Hmax = 284 +/- 12 mL/g, and 80% of the 12
comes from the lag time" tells you what to measure more carefully; "284" does
not.
"""
from __future__ import annotations

from typing import Any, Mapping


class UncertaintyError(ValueError):
    """An input or an expression that cannot be propagated, said in a sentence."""


def _module():
    try:
        import uncertainties
        from uncertainties import umath  # noqa: F401 - registers the maths functions
    except ImportError as exc:  # pragma: no cover - exercised by the add-on being absent
        raise UncertaintyError(
            "Uncertainty propagation needs the Uncertainty component. "
            "Add it from Add-ons."
        ) from exc
    return uncertainties


def _values(inputs: Mapping[str, Any]):
    """Named (value, error) pairs as uncertainties' own numbers."""
    uncertainties = _module()
    if not inputs:
        raise UncertaintyError("Give at least one named value with its uncertainty.")
    out = {}
    for name, spec in inputs.items():
        if isinstance(spec, Mapping):
            value, error = spec.get("value"), spec.get("error", spec.get("std", 0.0))
        elif isinstance(spec, (list, tuple)) and len(spec) == 2:
            value, error = spec
        else:
            value, error = spec, 0.0
        try:
            out[str(name)] = uncertainties.ufloat(float(value), abs(float(error or 0.0)))
        except (TypeError, ValueError) as exc:
            raise UncertaintyError(
                f"'{name}' needs a number and an uncertainty, "
                f"like {{'value': 12.0, 'error': 0.4}}"
            ) from exc
    return out


def propagate(expression: str, inputs: Mapping[str, Any]) -> dict:
    """Evaluate an expression in named uncertain values.

    The expression is parsed by SymPy first - the same validated parser the
    function plots use - so it cannot reach an import, an attribute or the
    filesystem. It is then evaluated on `uncertainties` numbers, which do the
    first-order propagation and keep the correlations between terms.
    """
    # Called for the guard it raises when the add-on is absent, not for a
    # value: the expression below is evaluated through SymPy on numbers
    # `_values` already built, so nothing here reads the module.
    _module()
    numbers = _values(inputs)

    try:
        from graphvis_science.analysis.expression import parse_expression
    except ImportError as exc:  # pragma: no cover
        raise UncertaintyError(f"The expression parser is unavailable: {exc}") from exc

    names = list(numbers)
    parsed = parse_expression(str(expression), tuple(names))
    expr, symbols = parsed if isinstance(parsed, tuple) else (parsed, None)

    import sympy as sp

    if isinstance(symbols, dict):
        ordered = [symbols[n] for n in names]
    elif symbols is not None:
        ordered = list(symbols)
    else:
        ordered = [sp.symbols(n, real=True) for n in names]

    # lambdify against the uncertainties maths module, so sqrt, exp and log of
    # an uncertain number keep their error rather than raising.
    from uncertainties import umath

    fn = sp.lambdify(ordered, expr, modules=[{
        "sqrt": umath.sqrt, "exp": umath.exp, "log": umath.log,
        "sin": umath.sin, "cos": umath.cos, "tan": umath.tan,
        "atan": umath.atan, "asin": umath.asin, "acos": umath.acos,
        "sinh": umath.sinh, "cosh": umath.cosh, "tanh": umath.tanh,
        "Abs": abs,
    }, "math"])

    try:
        result = fn(*[numbers[n] for n in names])
    except Exception as exc:  # noqa: BLE001 - a domain error is a real answer
        raise UncertaintyError(f"Could not evaluate '{expression}': {exc}") from exc

    value = float(getattr(result, "nominal_value", result))
    error = float(getattr(result, "std_dev", 0.0))

    # Which input is responsible for how much of the error.
    # error_components() is keyed by the underlying Variable objects, and
    # ufloat() returns exactly those, so each input is matched by identity.
    contributions = []
    components = getattr(result, "error_components", None)
    if callable(components):
        by_variable = components()
        total = sum(value ** 2 for value in by_variable.values())
        for name, number in numbers.items():
            share = 0.0
            for variable, contribution in by_variable.items():
                if variable is number:
                    share = float(contribution) ** 2
                    break
            contributions.append({
                "name": name,
                "contribution": float(share ** 0.5),
                "share": float(share / total) if total else 0.0,
            })
        contributions.sort(key=lambda c: -c["share"])

    return {
        "expression": str(expression),
        "value": value,
        "error": error,
        "relative_error": float(error / value) if value else None,
        "interval_68": [value - error, value + error],
        "interval_95": [value - 1.96 * error, value + 1.96 * error],
        "formatted": f"{value:.6g} +/- {error:.3g}",
        "inputs": {name: {"value": float(number.nominal_value),
                          "error": float(number.std_dev)}
                   for name, number in numbers.items()},
        "contributions": contributions,
    }


def from_fit(parameters: Mapping[str, Any], expression: str) -> dict:
    """Propagate a fit's parameters and their standard errors.

    ``parameters`` is what a FitResult carries: name -> {value, stderr}. This is
    the case the whole module exists for - a fitted Hmax combined into a rate,
    a yield, a specific production - carrying the fit's own error through.
    """
    inputs = {}
    for name, spec in (parameters or {}).items():
        if isinstance(spec, Mapping):
            inputs[name] = {"value": spec.get("value", spec.get("estimate")),
                            "error": spec.get("error", spec.get("stderr",
                                              spec.get("std_err", 0.0)))}
        else:
            inputs[name] = {"value": spec, "error": 0.0}
    return propagate(expression, inputs)
