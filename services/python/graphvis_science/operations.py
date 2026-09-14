"""Every analysis operation the application can call, in one table.

Why a table rather than sixty if-blocks.

The first wiring of this dispatch was written by hand, and it advertised
operations that did not exist: `psd`, `spectrogram` and `autocorrelation` were
looked up on ``SignalEngine``, which has none of them, and `cluster`, `classify`
and `regress` were looked up on ``MultivariateEngine``, which has none of those
either. Six of the eleven operations then on offer could only ever have returned
"not available". Nothing caught it, because a `getattr` that misses looks
exactly like an operation that is merely unimplemented.

So the operations are declared once, against the real method names, and a test
asserts that every entry in this table resolves to something callable. A typo
here is a failing test rather than a button that does nothing.

The result serialiser is generic for the same reason. Each engine returns its
own dataclass - ``StatisticalResult``, ``SignalResult``, ``FitResult``,
``MLResult`` and so on - and an earlier hand-written serialiser guessed at their
field names, silently returning ``{"ok": true}`` with everything empty when it
guessed wrong. ``jsonable`` reads the fields that are actually there.
"""
from __future__ import annotations

import dataclasses
import importlib
import math
from typing import Any, Callable

# ---------------------------------------------------------------- argument kinds
#
# How one parameter of an engine method is built from the JSON request.
COLUMN = "column"        # required numeric column -> float array
COLUMN_OPT = "column?"   # optional numeric column -> float array or None
COLUMNS = "columns"      # list of column names -> list of float arrays
NAME = "name"            # a column name, passed through as a string
NAMES = "names"          # a list of column names
FRAME = "frame"          # the whole DataFrame
VALUE = "value"          # a JSON value passed straight through
FRAME_COLUMNS = "frame_columns"   # the frame narrowed to the named columns


@dataclasses.dataclass(frozen=True)
class Arg:
    kind: str
    key: str = ""          # request key; defaults to the parameter name
    default: Any = None
    required: bool = False
    hint: str = ""         # what to type, for a value the UI has to ask for


@dataclasses.dataclass(frozen=True)
class Op:
    """One callable operation.

    ``target`` is "module:attribute" or "module:Class.method", resolved on
    first use so that a missing optional dependency - statsmodels, lifelines,
    scikit-learn - fails as a message about that operation rather than at
    import time for every operation.
    """
    name: str
    target: str
    args: dict[str, Arg]
    summary: str
    needs: tuple[str, ...] = ()
    group: str = "analysis"


def _resolve(target: str) -> Callable:
    module_name, _, attr = target.partition(":")
    module = importlib.import_module(module_name)
    obj: Any = module
    for part in attr.split("."):
        obj = getattr(obj, part)
    return obj


# ------------------------------------------------------------------ serialising
def jsonable(value: Any, _depth: int = 0) -> Any:
    """Anything an engine returns, as something json.dumps can write.

    Deliberately reflective: dataclasses are read through
    ``dataclasses.fields`` rather than a hand-kept list of names, so adding a
    field to a result upstream cannot silently stop it being reported.
    """
    import numpy as np
    import pandas as pd

    if _depth > 12:
        return None
    if value is None or isinstance(value, (bool, str)):
        return value
    if isinstance(value, (int, np.integer)):
        return int(value)
    if isinstance(value, (float, np.floating)):
        v = float(value)
        return v if math.isfinite(v) else None       # JSON has no NaN
    if isinstance(value, (np.bool_,)):
        return bool(value)
    if isinstance(value, pd.DataFrame):
        return frame_out(value)
    if isinstance(value, pd.Series):
        return frame_out(value.to_frame())
    if isinstance(value, complex) or isinstance(value, np.complexfloating):
        return {"real": jsonable(value.real, _depth + 1),
                "imag": jsonable(value.imag, _depth + 1)}
    if isinstance(value, np.ndarray):
        if np.iscomplexobj(value):
            # An FFT is complex, and the phase lives in the imaginary part.
            # Casting it to float discards that silently - numpy says so with a
            # ComplexWarning nobody reads - so it is reported in full.
            return {"real": jsonable(value.real, _depth + 1),
                    "imag": jsonable(value.imag, _depth + 1),
                    "magnitude": jsonable(np.abs(value), _depth + 1),
                    "phase": jsonable(np.angle(value), _depth + 1)}
        if value.ndim == 0:
            return jsonable(value.item(), _depth + 1)
        if value.ndim == 1:
            return numbers(value)
        return [jsonable(row, _depth + 1) for row in value]
    if isinstance(value, dict):
        return {str(k): jsonable(v, _depth + 1) for k, v in value.items()}
    if isinstance(value, (list, tuple, set)):
        return [jsonable(v, _depth + 1) for v in value]
    if dataclasses.is_dataclass(value) and not isinstance(value, type):
        return {f.name: jsonable(getattr(value, f.name, None), _depth + 1)
                for f in dataclasses.fields(value)}
    if hasattr(value, "to_dict") and callable(value.to_dict):
        try:
            return jsonable(value.to_dict(), _depth + 1)
        except Exception:  # noqa: BLE001 - a to_dict that needs arguments
            pass
    # A fitted scikit-learn estimator, a scipy object, anything else: name it
    # rather than drop it, so the reply says what was produced.
    text = repr(value)
    return text[:200] + ("..." if len(text) > 200 else "")


def numbers(values) -> list:
    """A 1-D numeric array as a JSON list, with non-finite entries as null.

    Vectorised: written per-element this was 94% of the cost of a 200k-row PCA.
    """
    import numpy as np

    if values is None:
        return []
    values = np.asarray(values)
    if np.iscomplexobj(values):
        # Reached only if a caller asks for a complex array as plain numbers.
        # Magnitude is the sensible scalar; the full complex value is available
        # through jsonable, which reports real, imaginary, magnitude and phase.
        values = np.abs(values)
    arr = np.asarray(values, dtype=float).ravel()
    out = arr.tolist()
    finite = np.isfinite(arr)
    if not finite.all():
        for i in np.flatnonzero(~finite):
            out[int(i)] = None
    return out


def frame_out(frame) -> dict:
    """A DataFrame as its column names plus column-wise rows."""
    import numpy as np
    import pandas as pd

    if frame is None or not isinstance(frame, pd.DataFrame) or frame.empty:
        return {"columns": [], "rows": {}}
    rows: dict[str, Any] = {}
    for column in frame.columns:
        series = frame[column]
        if pd.api.types.is_numeric_dtype(series):
            rows[str(column)] = numbers(series.to_numpy())
        else:
            values = series.astype(str).tolist()
            missing = series.isna().to_numpy()
            if missing.any():
                for i in np.flatnonzero(missing):
                    values[int(i)] = None
            rows[str(column)] = values
    return {"columns": [str(c) for c in frame.columns], "rows": rows}


# --------------------------------------------------------------------- binding
class OperationError(ValueError):
    """A request that cannot be turned into a call, said in a sentence."""


def _column(frame, name, *, required):
    import numpy as np

    if name in (None, ""):
        if required:
            raise OperationError("this operation needs a column to be chosen")
        return None
    if name not in frame.columns:
        raise OperationError(f"column '{name}' is not in this dataset")
    return frame[name].to_numpy(dtype=float)


def bind(op: Op, frame, req: dict) -> dict:
    """The keyword arguments for one call, built from the request."""
    import numpy as np

    kwargs: dict[str, Any] = {}
    for param, spec in op.args.items():
        key = spec.key or param
        raw = req.get(key, None)

        if spec.kind == FRAME:
            kwargs[param] = frame
            continue
        if spec.kind == FRAME_COLUMNS:
            names = raw or list(frame.columns)
            missing = [n for n in names if n not in frame.columns]
            if missing:
                raise OperationError(f"columns not in this dataset: {', '.join(missing)}")
            kwargs[param] = frame[names]
            continue
        if spec.kind == COLUMN:
            kwargs[param] = _column(frame, raw, required=True)
            continue
        if spec.kind == COLUMN_OPT:
            value = _column(frame, raw, required=False)
            if value is not None:
                kwargs[param] = value
            continue
        if spec.kind == COLUMNS:
            names = raw or []
            if not names:
                raise OperationError(f"'{key}' needs at least one column")
            kwargs[param] = [_column(frame, n, required=True) for n in names]
            continue
        if spec.kind == NAME:
            if raw in (None, ""):
                if spec.required:
                    raise OperationError(f"'{key}' needs a column to be chosen")
                continue
            if raw not in frame.columns:
                raise OperationError(f"column '{raw}' is not in this dataset")
            kwargs[param] = raw
            continue
        if spec.kind == NAMES:
            names = raw or []
            if not names and spec.required:
                raise OperationError(f"'{key}' needs at least one column")
            missing = [n for n in names if n not in frame.columns]
            if missing:
                raise OperationError(f"columns not in this dataset: {', '.join(missing)}")
            kwargs[param] = list(names)
            continue
        # VALUE
        if raw is None:
            if spec.default is not None:
                kwargs[param] = spec.default
            elif spec.required:
                raise OperationError(f"'{key}' is required for this operation")
            continue
        kwargs[param] = raw
    return kwargs


def run(name: str, frame, req: dict) -> dict:
    """Run one operation and return a JSON-safe reply."""
    op = REGISTRY.get(name)
    if op is None:
        raise OperationError(f"unknown analysis operation '{name}'")
    try:
        call = _resolve(op.target)
    except (ImportError, AttributeError) as exc:
        raise OperationError(
            f"'{name}' needs a component that is not installed ({exc}). "
            f"Add it from Add-ons."
        ) from exc
    try:
        result = call(**bind(op, frame, req))
    except (ImportError, ModuleNotFoundError) as exc:
        raise OperationError(_missing(name, exc)) from exc
    except RuntimeError as exc:
        # Several engines report an absent optional package as a RuntimeError
        # ("Kaplan-Meier analysis requires lifelines."). Without this they
        # surfaced as a bare failure with no route to fixing it.
        text = str(exc)
        if "requires" in text.lower() or "not installed" in text.lower():
            raise OperationError(_missing(name, exc)) from exc
        raise
    except OperationError:
        raise
    except ValueError as exc:
        # UnitError, UncertaintyError, CitationError and the engines' own
        # ValueErrors are all sentences written for the user - "Cannot convert
        # kPa to s", "Could not reach Crossref". Re-raised as an OperationError
        # they reach the panel as that sentence; left alone they reach it as a
        # traceback and a failed request with no reason attached.
        raise OperationError(str(exc)) from exc
    except (OverflowError, ZeroDivisionError, FloatingPointError,
            IndexError, AttributeError) as exc:
        # The numeric-rubbish family, reported as a sentence rather than as a
        # stack trace about an array the user never saw.
        #
        # Found by sweeping every operation with frames of NaN, inf, 1e308 and
        # nothing: `anova` raised OverflowError("Numerical result out of
        # range"), `discriminant` raised "index 0 is out of bounds for axis 0
        # with size 0". Guarding each engine separately is endless - there are
        # eighty-eight of them and any library update can add another - so the
        # boundary that already turns ValueError into a message handles these
        # too.
        #
        # The original type and text are kept. A user gets a sentence they can
        # act on; a developer reading a log still sees exactly what failed,
        # which is what makes this different from swallowing it.
        raise OperationError(
            f"'{name}' could not run on the selected data "
            f"({type(exc).__name__}: {exc}). This usually means too few rows, "
            f"a constant column, or values too large to compute with."
        ) from exc
    payload = jsonable(result)
    if not isinstance(payload, dict):
        payload = {"result": payload}
    payload.setdefault("operation", name)
    return payload


def _missing(name: str, exc: Exception) -> str:
    return (f"'{name}' needs a component that is not installed ({exc}). "
            f"Add it from Add-ons.")


def catalogue() -> list[dict]:
    """Every operation, for the UI to build itself from.

    ``needs`` names the column choices an operation reads; ``fields`` names the
    values it cannot run without - an expression, a unit, a DOI. The panel
    builds a text box per field and keeps the button disabled until each is
    filled, which is the difference between an operation the user can reach and
    one that always answers "'expression' is required for this operation".
    """
    out = []
    for op in REGISTRY.values():
        fields = [{"key": spec.key or param,
                   "hint": spec.hint,
                   "required": True}
                  for param, spec in op.args.items()
                  if spec.kind == VALUE and spec.required]
        out.append({"id": op.name, "label": op.summary, "needs": list(op.needs),
                    "group": op.group, "fields": fields})
    return out


# -------------------------------------------------------------------- registry
_A = "graphvis_science.analysis"


def _ops() -> list[Op]:
    out: list[Op] = []
    add = out.append

    # ------------------------------------------------------------- descriptive
    add(Op("describe", f"{_A}.statistics:StatisticalEngine.descriptive",
           {"values": Arg(COLUMN, "y"), "confidence": Arg(VALUE)},
           "Descriptive statistics with a confidence interval", ("y",), "Statistics"))
    add(Op("normality", f"{_A}.statistics:StatisticalEngine.normality",
           {"values": Arg(COLUMN, "y")},
           "Normality test", ("y",), "Statistics"))
    add(Op("t_test", f"{_A}.statistics:StatisticalEngine.t_test",
           {"a": Arg(COLUMN, "y"), "b": Arg(COLUMN_OPT, "y2"),
            "paired": Arg(VALUE), "equal_var": Arg(VALUE), "mu": Arg(VALUE)},
           "t-test between two columns", ("y",), "Statistics"))
    add(Op("anova", f"{_A}.statistics:StatisticalEngine.one_way_anova",
           {"groups": Arg(COLUMNS, "columns"), "labels": Arg(VALUE),
            "welch": Arg(VALUE)},
           "One-way ANOVA across columns", ("columns",), "Statistics"))
    add(Op("factorial_anova", f"{_A}.statistics:StatisticalEngine.factorial_anova",
           {"df": Arg(FRAME), "response": Arg(NAME, "y", required=True),
            "factors": Arg(NAMES, "factors", required=True),
            "repeated_subject": Arg(NAME, "subject")},
           "Factorial ANOVA", ("y", "factors"), "Statistics"))
    add(Op("brown_forsythe", f"{_A}.statistics:StatisticalEngine.brown_forsythe",
           {"groups": Arg(COLUMNS, "columns")},
           "Brown-Forsythe equal-variance test", ("columns",), "Statistics"))
    add(Op("nonparametric", f"{_A}.statistics:StatisticalEngine.nonparametric",
           {"name": Arg(VALUE, "test", default="mannwhitney", required=True,
                          hint="mannwhitney, wilcoxon, kruskal or friedman"),
            "a": Arg(COLUMN, "y"), "b": Arg(COLUMN_OPT, "y2"),
            "groups": Arg(VALUE)},
           "Non-parametric test", ("y",), "Statistics"))
    add(Op("tukey", f"{_A}.statistics:StatisticalEngine.tukey",
           {"groups": Arg(COLUMNS, "columns"), "labels": Arg(VALUE),
            "alpha": Arg(VALUE)},
           "Tukey post-hoc comparison", ("columns",), "Statistics"))
    add(Op("pairwise", f"{_A}.statistics:StatisticalEngine.pairwise_comparisons",
           {"groups": Arg(COLUMNS, "columns"), "labels": Arg(VALUE),
            "method": Arg(VALUE), "alpha": Arg(VALUE)},
           "All pairwise comparisons, corrected", ("columns",), "Statistics"))
    add(Op("adjust_pvalues", f"{_A}.statistics:StatisticalEngine.adjust_pvalues",
           {"pvalues": Arg(COLUMN, "y"), "method": Arg(VALUE)},
           "Multiple-comparison correction", ("y",), "Statistics"))
    add(Op("power_ttest", f"{_A}.statistics:StatisticalEngine.power_ttest",
           {"effect_size": Arg(VALUE, "effect_size", required=True,
                                  hint="Cohen's d, e.g. 0.5"),
            "alpha": Arg(VALUE), "power": Arg(VALUE), "ratio": Arg(VALUE),
            "alternative": Arg(VALUE)},
           "Sample size for a t-test", (), "Statistics"))
    add(Op("cohen_d", f"{_A}.statistics:cohen_d",
           {"a": Arg(COLUMN, "y"), "b": Arg(COLUMN, "y2"), "paired": Arg(VALUE)},
           "Cohen's d effect size", ("y", "y2"), "Statistics"))
    add(Op("kaplan_meier", f"{_A}.statistics:StatisticalEngine.kaplan_meier",
           {"time": Arg(COLUMN, "x"), "event": Arg(COLUMN, "y"),
            "group": Arg(COLUMN_OPT, "group")},
           "Kaplan-Meier survival curve", ("x", "y"), "Survival"))
    add(Op("logrank", f"{_A}.statistics:StatisticalEngine.logrank",
           {"time": Arg(COLUMN, "x"), "event": Arg(COLUMN, "y"),
            "group": Arg(COLUMN, "group")},
           "Log-rank test between groups", ("x", "y", "group"), "Survival"))
    add(Op("cox", f"{_A}.statistics:StatisticalEngine.cox",
           {"df": Arg(FRAME), "duration_col": Arg(NAME, "x", required=True),
            "event_col": Arg(NAME, "y", required=True),
            "covariates": Arg(NAMES, "predictors", required=True)},
           "Cox proportional hazards", ("x", "y", "predictors"), "Survival"))

    # ------------------------------------------------------------------ signal
    for method, label in (("fft", "FFT spectrum"),
                          ("stft", "Short-time Fourier transform"),
                          ("hilbert", "Hilbert envelope and phase"),
                          ("savgol", "Savitzky-Golay smoothing"),
                          ("lowess", "LOWESS smoothing")):
        add(Op(method, f"{_A}.signal:SignalEngine.{method}",
               {"y": Arg(COLUMN, "y"), "x": Arg(COLUMN_OPT, "x")},
               label, ("y",), "Signal"))
    # The inverse transform, reachable. SignalEngine.ifft takes a complex
    # spectrum, which no panel can offer, so it sat unused; a forward transform,
    # a truncation and an inverse is the smoothing it is actually wanted for.
    add(Op("fft_lowpass", "graphvis_science.adapters:fft_lowpass",
           {"y": Arg(COLUMN, "y"), "x": Arg(COLUMN_OPT, "x"), "keep": Arg(VALUE)},
           "Smooth by rebuilding from the lowest frequencies", ("y",), "Signal"))
    add(Op("iir_filter", f"{_A}.signal:SignalEngine.iir_filter",
           {"y": Arg(COLUMN, "y"), "x": Arg(COLUMN_OPT, "x"),
            "cutoff": Arg(VALUE), "order": Arg(VALUE), "kind": Arg(VALUE),
            "fs": Arg(VALUE)},
           "IIR filter (low, high or band pass)", ("y",), "Signal"))
    add(Op("baseline_subtract", f"{_A}.signal:SignalEngine.baseline_subtract",
           {"y": Arg(COLUMN, "y"), "x": Arg(COLUMN_OPT, "x"),
            "method": Arg(VALUE), "degree": Arg(VALUE), "lam": Arg(VALUE),
            "p": Arg(VALUE)},
           "Baseline subtraction", ("y",), "Signal"))
    add(Op("derivative", f"{_A}.signal:CalculusEngine.derivative",
           {"y": Arg(COLUMN, "y"), "x": Arg(COLUMN_OPT, "x"), "order": Arg(VALUE)},
           "Numerical derivative", ("y",), "Calculus"))
    add(Op("integral", f"{_A}.signal:CalculusEngine.integral",
           {"y": Arg(COLUMN, "y"), "x": Arg(COLUMN_OPT, "x")},
           "Cumulative integral", ("y",), "Calculus"))
    add(Op("polygon_area", f"{_A}.signal:CalculusEngine.polygon_area",
           {"x": Arg(COLUMN, "x"), "y": Arg(COLUMN, "y")},
           "Enclosed area of a closed curve", ("x", "y"), "Calculus"))
    # Through an adapter: the engines take a 2-D height grid, and a request
    # names columns. The adapter grids the scattered points with the same
    # estimator the Surface panel uses, so the answer describes the surface the
    # user can already see rather than a reindexed column.
    add(Op("surface_area", "graphvis_science.adapters:surface_area_from_points",
           {"x": Arg(COLUMN, "x"), "y": Arg(COLUMN, "y"), "z": Arg(COLUMN, "z"),
            "resolution": Arg(VALUE), "estimator": Arg(VALUE)},
           "Surface area through scattered points", ("x", "y", "z"), "Calculus"))
    add(Op("volume_2d", "graphvis_science.adapters:volume_from_points",
           {"x": Arg(COLUMN, "x"), "y": Arg(COLUMN, "y"), "z": Arg(COLUMN, "z"),
            "resolution": Arg(VALUE), "estimator": Arg(VALUE)},
           "Volume under scattered points", ("x", "y", "z"), "Calculus"))

    # ----------------------------------------------------------------- fitting
    add(Op("curve_fit", f"{_A}.fitting:CurveFittingEngine.fit",
           {"x": Arg(COLUMN, "x"), "y": Arg(COLUMN, "y"), "model": Arg(VALUE),
            "degree": Arg(VALUE), "initial": Arg(VALUE)},
           "Fit a named model to x and y", ("x", "y"), "Fitting"))
    add(Op("fit_models", f"{_A}.fitting:available_models", {},
           "List the models curve fitting can use", (), "Fitting"))
    add(Op("fit_model", f"{_A}.fitting:fit_registered_model",
           {"x": Arg(COLUMN, "x"), "y": Arg(COLUMN, "y"),
            "model": Arg(VALUE, "model", required=True,
                                hint="a model name, e.g. logistic or modified gompertz"),
            "initial": Arg(VALUE),
            "bounds": Arg(VALUE), "bootstrap": Arg(VALUE),
            "random_state": Arg(VALUE)},
           "Fit one registered model, with bootstrap intervals", ("x", "y"), "Fitting"))
    add(Op("global_linear", "graphvis_science.adapters:global_linear_pairs",
           {"datasets": Arg(VALUE, "datasets", required=True,
                                hint='[{"x": [...], "y": [...]}, ...]'),
            "shared_slope": Arg(VALUE)},
           "Global linear fit across datasets", (), "Fitting"))
    add(Op("peaks", f"{_A}.fitting:PeakEngine.detect",
           {"x": Arg(COLUMN, "x"), "y": Arg(COLUMN, "y"),
            "prominence": Arg(VALUE), "distance": Arg(VALUE)},
           "Find peaks with widths and areas", ("x", "y"), "Peaks"))
    add(Op("deconvolve", f"{_A}.fitting:PeakEngine.deconvolve",
           {"x": Arg(COLUMN, "x"), "y": Arg(COLUMN, "y"), "n_peaks": Arg(VALUE),
            "profile": Arg(VALUE), "prominence": Arg(VALUE)},
           "Separate overlapping peaks", ("x", "y"), "Peaks"))
    add(Op("surface_polynomial", f"{_A}.fitting:SurfaceFittingEngine.polynomial",
           {"x": Arg(COLUMN, "x"), "y": Arg(COLUMN, "y"), "z": Arg(COLUMN, "z"),
            "degree": Arg(VALUE)},
           "Polynomial surface fit", ("x", "y", "z"), "Fitting"))

    # ------------------------------------------------------------- peak tools
    add(Op("polynomial_baseline", f"{_A}.tools:polynomial_baseline",
           {"x": Arg(COLUMN, "x"), "y": Arg(COLUMN, "y"), "order": Arg(VALUE),
            "edge_fraction": Arg(VALUE)},
           "Polynomial baseline", ("x", "y"), "Peaks"))
    add(Op("als_baseline", f"{_A}.tools:asymmetric_least_squares",
           {"y": Arg(COLUMN, "y"), "lam": Arg(VALUE), "p": Arg(VALUE),
            "niter": Arg(VALUE)},
           "Asymmetric least-squares baseline", ("y",), "Peaks"))
    add(Op("fit_overlapping_peaks", f"{_A}.tools:fit_overlapping_peaks",
           {"x": Arg(COLUMN, "x"), "y": Arg(COLUMN, "y"), "n_peaks": Arg(VALUE),
            "kind": Arg(VALUE), "baseline_order": Arg(VALUE)},
           "Fit overlapping Gaussian/Lorentzian/Voigt peaks", ("x", "y"), "Peaks"))
    # The default was "gaussian", which fit_nonlinear_model had no branch for,
    # so it fell through to exponential saturation and reported that fit under
    # the name "gaussian". Gaussian is a real model now, and an unrecognised
    # name is an error rather than a different curve wearing the asked-for
    # label.
    add(Op("fit_nonlinear", f"{_A}.tools:fit_nonlinear_model",
           {"x": Arg(COLUMN, "x"), "y": Arg(COLUMN, "y"),
            "model": Arg(VALUE, "model", default="Gaussian",
                         hint="Gaussian, Lorentzian, Exponential saturation, "
                              "Exponential decay, Michaelis-Menten or Logistic")},
           "Fit a single non-linear peak model", ("x", "y"), "Peaks"))

    # -------------------------------------------------------- electrochemistry
    add(Op("gompertz", f"{_A}.electro:fit_gompertz",
           {"t": Arg(COLUMN, "x"), "H": Arg(COLUMN, "y")},
           "Modified Gompertz fit (Hmax, Rm, lag)", ("x", "y"), "Electrochemistry"))
    add(Op("nyquist", f"{_A}.electro:fit_nyquist_semicircle",
           {"z_re": Arg(COLUMN, "x"), "z_im": Arg(COLUMN, "y")},
           "EIS semicircle fit (Rs, Rct)", ("x", "y"), "Electrochemistry"))
    add(Op("polarisation", f"{_A}.electro:polarisation_power",
           {"current": Arg(COLUMN, "x"), "voltage": Arg(COLUMN, "y")},
           "Polarisation curve and peak power", ("x", "y"), "Electrochemistry"))
    add(Op("sensitivity", f"{_A}.electro:global_sensitivity",
           {"df": Arg(FRAME), "params": Arg(NAMES, "predictors", required=True),
            "response": Arg(NAME, "y", required=True)},
           "Global sensitivity of a response to its parameters",
           ("y", "predictors"), "Electrochemistry"))
    add(Op("pareto", f"{_A}.electro:pareto_frontier",
           {"x": Arg(COLUMN, "x"), "y": Arg(COLUMN, "y"),
            "maximise_y": Arg(VALUE), "bounds": Arg(VALUE),
            "n_boot": Arg(VALUE), "seed": Arg(VALUE)},
           "Pareto frontier with a bootstrap band", ("x", "y"), "Electrochemistry"))
    add(Op("advisor", "graphvis_science.adapters:advisor_recommendations",
           {"dataset": Arg(FRAME), "literature": Arg(VALUE)},
           "Recommend graphs for this data", (), "Advice"))
    add(Op("domain", f"{_A}.electro:detect_domain_data",
           {"dataset": Arg(FRAME)},
           "Detect electrochemical columns", (), "Advice"))

    # -------------------------------------------------------------- limits etc.
    add(Op("limits", "graphvis_science.adapters:limits_report",
           {"x": Arg(COLUMN, "x"), "y": Arg(COLUMN, "y"), "options": Arg(VALUE)},
           "Limits: plateau, asymptote, knee", ("x", "y"), "Limits"))
    add(Op("plateau", f"{_A}.limits:detect_plateau",
           {"x": Arg(COLUMN, "x"), "y": Arg(COLUMN, "y"),
            "slope_tol": Arg(VALUE), "min_frac": Arg(VALUE)},
           "Plateau detection", ("x", "y"), "Limits"))
    add(Op("asymptote", f"{_A}.limits:fit_asymptote",
           {"x": Arg(COLUMN, "x"), "y": Arg(COLUMN, "y")},
           "Asymptote fit", ("x", "y"), "Limits"))
    add(Op("knee", f"{_A}.limits:detect_knee",
           {"x": Arg(COLUMN, "x"), "y": Arg(COLUMN, "y")},
           "Knee / elbow point", ("x", "y"), "Limits"))
    add(Op("confidence_envelope", f"{_A}.limits:confidence_envelope",
           {"x": Arg(COLUMN, "x"), "y": Arg(COLUMN, "y"), "level": Arg(VALUE),
            "bins": Arg(VALUE), "smooth_sigma": Arg(VALUE),
            "min_neighbors": Arg(VALUE)},
           "Confidence envelope around a scatter", ("x", "y"), "Limits"))
    add(Op("pareto_bounds", f"{_A}.limits:pareto_bounds",
           {"x": Arg(COLUMN, "x"), "y": Arg(COLUMN, "y"),
            "maximise_y": Arg(VALUE), "n_boot": Arg(VALUE)},
           "Pareto bounds with a bootstrap band", ("x", "y"), "Limits"))
    add(Op("forecast", f"{_A}.predictive:forecast_series",
           {"x": Arg(COLUMN, "x"), "y": Arg(COLUMN, "y"),
            "extension": Arg(VALUE), "confidence": Arg(VALUE)},
           "Forecast with a confidence band", ("x", "y"), "Limits"))

    # ----------------------------------------------------------- multivariate
    add(Op("pca", f"{_A}.ml:MultivariateEngine.pca",
           {"df": Arg(FRAME), "columns": Arg(NAMES, "columns"),
            "n_components": Arg(VALUE, "components"), "scale": Arg(VALUE)},
           "Principal components (PCA)", (), "Multivariate"))
    add(Op("kmeans", f"{_A}.ml:MultivariateEngine.kmeans",
           {"df": Arg(FRAME), "columns": Arg(NAMES, "columns"),
            "n_clusters": Arg(VALUE, "clusters"), "scale": Arg(VALUE),
            "random_state": Arg(VALUE)},
           "k-means clustering", (), "Multivariate"))
    add(Op("hierarchical", f"{_A}.ml:MultivariateEngine.hierarchical",
           {"df": Arg(FRAME), "columns": Arg(NAMES, "columns"),
            "method": Arg(VALUE), "metric": Arg(VALUE)},
           "Hierarchical clustering", (), "Multivariate"))
    add(Op("discriminant", f"{_A}.ml:MultivariateEngine.discriminant",
           {"df": Arg(FRAME), "features": Arg(NAMES, "predictors", required=True),
            "target": Arg(NAME, "y", required=True)},
           "Linear discriminant analysis", ("y", "predictors"), "Multivariate"))
    add(Op("pls", f"{_A}.ml:MultivariateEngine.pls",
           {"df": Arg(FRAME), "features": Arg(NAMES, "predictors", required=True),
            "target": Arg(NAME, "y", required=True),
            "n_components": Arg(VALUE, "components")},
           "Partial least squares", ("y", "predictors"), "Multivariate"))
    add(Op("decision_tree", f"{_A}.ml:MultivariateEngine.decision_tree",
           {"df": Arg(FRAME), "features": Arg(NAMES, "predictors", required=True),
            "target": Arg(NAME, "y", required=True), "task": Arg(VALUE),
            "max_depth": Arg(VALUE), "random_state": Arg(VALUE)},
           "Decision tree", ("y", "predictors"), "Multivariate"))
    add(Op("predictive_advisor", f"{_A}.ml:MultivariateEngine.predictive_advisor",
           {"df": Arg(FRAME), "target": Arg(NAME, "y")},
           "Suggest a predictive approach for this data", (), "Advice"))

    # ------------------------------------------------------------- regression
    add(Op("regression", f"{_A}.regression:RegressionEngine.linear",
           {"df": Arg(FRAME), "response": Arg(NAME, "y", required=True),
            "predictors": Arg(NAMES, "predictors", required=True),
            "robust": Arg(VALUE), "add_intercept": Arg(VALUE)},
           "Linear regression", ("y", "predictors"), "Regression"))
    add(Op("logistic", f"{_A}.regression:RegressionEngine.logistic",
           {"df": Arg(FRAME), "response": Arg(NAME, "y", required=True),
            "predictors": Arg(NAMES, "predictors", required=True)},
           "Logistic regression", ("y", "predictors"), "Regression"))

    # ------------------------------------------------------------ reliability
    add(Op("weibull", f"{_A}.reliability:ReliabilityEngine.weibull",
           {"values": Arg(COLUMN, "y"), "confidence": Arg(VALUE)},
           "Weibull reliability", ("y",), "Reliability"))
    add(Op("bootstrap_ci", f"{_A}.reliability:ReliabilityEngine.bootstrap_ci",
           {"values": Arg(COLUMN, "y"),
            "statistic": Arg(VALUE, "statistic", default="mean",
                             hint="mean, median, std, var, min or max"),
            "confidence": Arg(VALUE), "resamples": Arg(VALUE), "seed": Arg(VALUE)},
           "Bootstrap confidence interval", ("y",), "Reliability"))
    add(Op("failure_probability", f"{_A}.reliability:ReliabilityEngine.failure_probability",
           {"values": Arg(COLUMN, "y"),
            "threshold": Arg(VALUE, "threshold", required=True,
                                 hint="the value counted as a failure, e.g. 12.5"),
            "higher_is_failure": Arg(VALUE)},
           "Probability of exceeding a threshold", ("y",), "Reliability"))
    # Also through an adapter: the engine wants a Python callable and scipy
    # frozen distributions, neither of which survives JSON. The adapter builds
    # both from an expression and named distributions.
    add(Op("monte_carlo", "graphvis_science.adapters:monte_carlo_expression",
           {"model": Arg(VALUE, "model", required=True,
                                hint="an expression in the input names, e.g. a * b + c"),
            "distributions": Arg(VALUE, "distributions", required=True,
                                 hint='{"a": {"kind": "normal", "loc": 1, "scale": 0.1}}'),
            "n": Arg(VALUE), "seed": Arg(VALUE)},
           "Monte-Carlo uncertainty propagation", (), "Reliability"))
    add(Op("distribution_kinds", "graphvis_science.adapters:distribution_kinds", {},
           "Distributions Monte-Carlo can sample", (), "Reliability"))

    # ------------------------------------------------------------- symbolic
    # The symbolic route into analysis/expression.py. The seven Function and
    # Implicit plot engines are drawn by the C++ parser; this is what the C++
    # parser cannot do - exact derivatives and simplification.
    add(Op("function_curve", "graphvis_science.adapters:function_curve",
           {"source": Arg(VALUE, "expression", required=True,
                              hint="y = f(x), e.g. sin(x) / x"),
            "domain": Arg(VALUE), "samples": Arg(VALUE)},
           "Plot y = f(x), with its exact derivative", (), "Symbolic"))
    add(Op("function_surface", "graphvis_science.adapters:function_surface",
           {"source": Arg(VALUE, "expression", required=True,
                              hint="z = f(x, y), e.g. sin(x) * cos(y)"),
            "x_domain": Arg(VALUE), "y_domain": Arg(VALUE),
            "samples": Arg(VALUE)},
           "Plot z = f(x, y) over a rectangle", (), "Symbolic"))
    add(Op("parametric_curve", "graphvis_science.adapters:parametric_curve",
           {"source": Arg(VALUE, "expression", required=True,
                          hint="x(t); y(t); z(t), e.g. cos(t); sin(t); t/4"),
            "domain": Arg(VALUE), "samples": Arg(VALUE)},
           "Plot a parametric curve x(t), y(t), z(t)", (), "Symbolic"))
    add(Op("implicit_field", "graphvis_science.adapters:implicit_field",
           {"source": Arg(VALUE, "expression", required=True,
                          hint="an equation, e.g. x**2 + y**2 = 4"),
            "x_domain": Arg(VALUE), "y_domain": Arg(VALUE),
            "samples": Arg(VALUE)},
           "Solve an implicit equation as a field to contour", (), "Symbolic"))
    add(Op("function_volume", "graphvis_science.adapters:function_volume",
           {"source": Arg(VALUE, "expression", required=True,
                          hint="v = f(x, y, z), e.g. x**2 + y**2 - z**2"),
            "x_domain": Arg(VALUE), "y_domain": Arg(VALUE),
            "z_domain": Arg(VALUE), "samples": Arg(VALUE)},
           "Evaluate v = f(x, y, z) on a volume grid", (), "Symbolic"))

    # -------------------------------------------------------------------- DOE
    for method, label in (("latin_hypercube", "Latin hypercube design"),
                          ("sobol", "Sobol sequence design")):
        add(Op(method, f"{_A}.doe:DOEEngine.{method}",
               {"bounds": Arg(VALUE, "bounds", required=True,
                                 hint='{"temperature": [20, 60], "pH": [5, 9]}'),
                "n": Arg(VALUE, "n", default=16), "seed": Arg(VALUE)},
               label, (), "Design of experiments"))
    add(Op("full_factorial", f"{_A}.doe:DOEEngine.full_factorial",
           {"levels": Arg(VALUE, "levels", required=True,
                             hint='{"temperature": [20, 30, 40], "pH": [6, 7]}, '
                                  'or a count per factor: {"temperature": 3, "pH": 2}')},
           "Full factorial design", (), "Design of experiments"))
    add(Op("central_composite", f"{_A}.doe:DOEEngine.central_composite",
           {"bounds": Arg(VALUE, "bounds", required=True,
                             hint='{"temperature": [20, 60], "pH": [5, 9]}'),
            "alpha": Arg(VALUE),
            "center_points": Arg(VALUE)},
           "Central composite design", (), "Design of experiments"))
    add(Op("doe_recommend", f"{_A}.doe:DOEEngine.recommend",
           {"existing": Arg(FRAME),
            "parameter_columns": Arg(NAMES, "predictors", required=True),
            "response": Arg(NAME, "y")},
           "Recommend the next experiments", ("predictors",), "Design of experiments"))

    # ------------------------------------------------------------------ units
    # Dimensional analysis. The native side reads a unit out of a label and
    # converts about thirty pairs with no add-on at all; this is the rest -
    # compound units, and whether two columns are even the same kind of
    # quantity, which is the mistake that survives review.
    add(Op("unit_parse", "graphvis_science.units:parse",
           {"unit": Arg(VALUE, "unit", required=True, hint="a unit, e.g. mA/cm2")},
           "What a unit is, dimensionally", (), "Units"))
    add(Op("unit_convert", "graphvis_science.units:convert",
           {"values": Arg(COLUMN, "y"),
            "source": Arg(VALUE, "source", required=True,
                          hint="the unit the column is in, e.g. kPa"),
            "target": Arg(VALUE, "target", required=True,
                          hint="the unit to convert to, e.g. bar")},
           "Convert a column between units", ("y",), "Units"))
    add(Op("unit_compatible", "graphvis_science.units:compatible",
           {"source": Arg(VALUE, "source", required=True, hint="a unit, e.g. kPa"),
            "target": Arg(VALUE, "target", required=True, hint="a unit, e.g. s")},
           "Are two units the same kind of quantity?", (), "Units"))
    add(Op("unit_options", "graphvis_science.units:conversions_for",
           {"unit": Arg(VALUE, "unit", required=True, hint="a unit, e.g. kPa"),
            "limit": Arg(VALUE)},
           "Units this one converts to", (), "Units"))
    # Audits the dataset's own labels when none are named, so the button needs
    # no input at all - which is the only way it is ever going to be pressed.
    add(Op("unit_audit", "graphvis_science.adapters:unit_audit",
           {"frame": Arg(FRAME), "labels": Arg(VALUE, "labels")},
           "Read every column label and report its units", (), "Units"))

    # ------------------------------------------------------------ uncertainty
    add(Op("propagate", "graphvis_science.uncertainty:propagate",
           {"expression": Arg(VALUE, "expression", required=True,
                                  hint="an expression in the input names, e.g. H * exp(-k * t)"),
            "inputs": Arg(VALUE, "inputs", required=True,
                              hint='{"H": {"value": 284, "error": 12}}')},
           "Propagate uncertainty through an expression", (), "Uncertainty"))
    add(Op("propagate_fit", "graphvis_science.uncertainty:from_fit",
           {"parameters": Arg(VALUE, "parameters", required=True,
                                  hint='{"P": {"value": 284, "stderr": 12}}'),
            "expression": Arg(VALUE, "expression", required=True,
                                  hint="an expression in the parameter names, e.g. P / Rm")},
           "Propagate a fit's parameters and their errors", (), "Uncertainty"))

    # -------------------------------------------------------------- citations
    # No optional component: one HTTPS GET and one JSON parse, both standard
    # library. The thing that records where a number came from should not be
    # able to be missing.
    add(Op("latex_figure", "graphvis_science.latex:figure_block",
           {"image_path": Arg(VALUE, "image_path", required=True,
                              hint="path of the image you already exported"),
            "caption": Arg(VALUE, "caption"),
            "style": Arg(VALUE, "style", default="apa",
                         hint="apa, ieee, nature or harvard"),
            "label": Arg(VALUE), "width": Arg(VALUE), "placement": Arg(VALUE),
            "note": Arg(VALUE), "citation": Arg(VALUE), "dataset": Arg(VALUE)},
           "LaTeX figure block for an exported image", (), "Citations"))
    add(Op("cite", "graphvis_science.citations:cite",
           {"doi": Arg(VALUE, "doi", required=True,
                           hint="a DOI, e.g. 10.1038/s41586-020-2649-2"),
            "style": Arg(VALUE),
            "timeout": Arg(VALUE)},
           "Resolve a DOI to a citation", (), "Citations"))
    add(Op("cite_text", "graphvis_science.citations:cite_many",
           {"text": Arg(VALUE, "text", required=True,
                            hint="text containing DOIs, e.g. a reference list"),
            "style": Arg(VALUE),
            "timeout": Arg(VALUE), "limit": Arg(VALUE)},
           "Resolve every DOI in a block of text", (), "Citations"))
    add(Op("find_dois", "graphvis_science.citations:find_dois",
           {"text": Arg(VALUE, "text", required=True,
                            hint="text to search for DOIs")},
           "List the DOIs in a block of text", (), "Citations"))

    return out


REGISTRY: dict[str, Op] = {op.name: op for op in _ops()}
