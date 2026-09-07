"""Every analysis operation, resolved and then actually run.

Two failures this suite exists to prevent, both of which had already happened.

The dispatch used to name methods that did not exist - `psd`, `spectrogram`,
`autocorrelation` on SignalEngine, `cluster`, `classify`, `regress` on
MultivariateEngine - so six operations could only ever answer "not available".
A `getattr` that misses looks exactly like a feature not yet written, so nothing
noticed. `test_every_target_resolves` makes that a failing test.

And the reply serialiser used to guess at result field names, returning
`{"ok": true}` with everything empty when it guessed wrong. So every operation
is run here for real and its reply is required to carry something.
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import numpy as np
import pandas as pd

from graphvis_science import operations as ops

passed = 0
failed = 0
skipped = 0


def ok(label, condition, detail=""):
    global passed, failed
    if condition:
        passed += 1
        print(f"  ok   {label:<40} {detail}")
    else:
        failed += 1
        print(f"  FAIL {label:<40} {detail}")


def frame():
    rng = np.random.default_rng(11)
    n = 220
    t = np.linspace(0.0, 40.0, n)
    # A saturating curve with noise: plausible for Gompertz, plateau, knee,
    # asymptote, forecast and the peak tools all at once.
    h = 280.0 * np.exp(-np.exp((2.4 * math.e / 280.0) * (9.0 - t) + 1.0))
    return pd.DataFrame({
        "x": t,
        "y": h + rng.normal(0, 2.0, n),
        "y2": h * 0.92 + rng.normal(0, 2.0, n),
        "z": np.sin(t / 3.0) * 10 + rng.normal(0, 0.4, n),
        "group": (np.arange(n) % 3).astype(float),
        "event": (rng.random(n) > 0.35).astype(float),
        "p1": rng.normal(0, 1, n),
        "p2": rng.normal(5, 2, n),
        # Three resolved peaks on a sloping baseline: the peak finder and the
        # deconvolution have something real to find. A saturating curve has no
        # peaks, and "found none" is a correct answer that reads as a failure.
        "spectrum": (40 * np.exp(-((t - 8.0) ** 2) / 2.0)
                     + 26 * np.exp(-((t - 17.0) ** 2) / 3.0)
                     + 33 * np.exp(-((t - 29.0) ** 2) / 2.5)
                     + 0.4 * t + rng.normal(0, 0.25, n)),
    })


DF = frame()

# What each operation needs, beyond the columns its `needs` already names.
EXTRA = {
    "t_test": {"y2": "y2"},
    "cohen_d": {"y2": "y2"},
    "anova": {"columns": ["y", "y2", "z"]},
    "brown_forsythe": {"columns": ["y", "y2", "z"]},
    "tukey": {"columns": ["y", "y2", "z"]},
    "pairwise": {"columns": ["y", "y2", "z"]},
    "factorial_anova": {"factors": ["group"]},
    "nonparametric": {"test": "mannwhitney", "y2": "y2"},
    "power_ttest": {"effect_size": 0.5},
    "kaplan_meier": {"x": "x", "y": "event"},
    "logrank": {"x": "x", "y": "event", "group": "group"},
    "cox": {"x": "x", "y": "event", "predictors": ["p1", "p2"]},
    "regression": {"predictors": ["p1", "p2"]},
    "logistic": {"y": "event", "predictors": ["p1", "p2"]},
    "discriminant": {"y": "group", "predictors": ["p1", "p2"]},
    "pls": {"predictors": ["p1", "p2"]},
    "decision_tree": {"y": "group", "predictors": ["p1", "p2"]},
    "sensitivity": {"predictors": ["p1", "p2"]},
    "doe_recommend": {"predictors": ["p1", "p2"]},
    "fit_model": {"model": "modified gompertz"},
    "peaks": {"y": "spectrum"},
    "deconvolve": {"y": "spectrum", "n_peaks": 3},
    "fit_overlapping_peaks": {"y": "spectrum", "n_peaks": 3},
    "fit_nonlinear": {"y": "spectrum", "model": "gaussian"},
    "als_baseline": {"y": "spectrum"},
    "polynomial_baseline": {"y": "spectrum"},
    "surface_area": {"resolution": 40},
    "volume_2d": {"resolution": 40},
    "function_curve": {"expression": "sin(x)/x", "domain": "-10,10"},
    "function_surface": {"expression": "sin(x)*cos(y)", "samples": 24},
    "parametric_curve": {"expression": "cos(t); sin(t); t/4", "domain": "0,12"},
    "implicit_field": {"expression": "x**2 + y**2 = 4", "samples": 40},
    "function_volume": {"expression": "x**2 + y**2 - z**2", "samples": 24},
    "fft_lowpass": {"keep": 0.1},
    "monte_carlo": {"model": "a * b",
                    "distributions": {"a": {"kind": "normal", "loc": 2, "scale": 0.2},
                                      "b": {"kind": "uniform", "low": 1, "high": 3}},
                    "n": 2000},
    "failure_probability": {"threshold": 150.0},
    "latin_hypercube": {"bounds": {"a": [0, 1], "b": [2, 5]}, "n": 12},
    "sobol": {"bounds": {"a": [0, 1], "b": [2, 5]}, "n": 8},
    "full_factorial": {"levels": {"a": [0, 1], "b": [2, 3]}},
    "central_composite": {"bounds": {"a": [0, 1], "b": [2, 5]}},
    "global_linear": {"datasets": [{"x": [0, 1, 2, 3], "y": [0, 2, 4, 6]},
                                   {"x": [0, 1, 2, 3], "y": [1, 3, 5, 7]}]},
    "adjust_pvalues": {"y": "p1"},
    "unit_parse": {"unit": "mA/cm2"},
    "unit_convert": {"source": "kPa", "target": "bar"},
    "unit_compatible": {"source": "kPa", "target": "bar"},
    "unit_options": {"unit": "kPa"},
    "unit_audit": {"labels": ["Pressure [kPa]", "Temperature (degC)", "plain"]},
    "propagate": {"expression": "H * exp(-k * t)",
                  "inputs": {"H": {"value": 284, "error": 12},
                             "k": {"value": 0.08, "error": 0.006},
                             "t": {"value": 10, "error": 0.2}}},
    "propagate_fit": {"parameters": {"P": {"value": 284, "stderr": 12},
                                     "Rm": {"value": 24, "stderr": 1.5}},
                      "expression": "P / Rm"},
    "find_dois": {"text": "see doi:10.1038/s41586-020-2649-2 for the method"},
    "cite": {"doi": "10.1038/s41586-020-2649-2"},
    "cite_text": {"text": "see doi:10.1038/s41586-020-2649-2"},
}

# These two ask Crossref. Their offline halves - finding a DOI in text and both
# formatters - are covered by test_extras.py, which needs no network at all; the
# sweep only checks they are reachable, and a build machine with no route out
# must not turn that into a failure.
NETWORK = {"cite", "cite_text"}

DEFAULT_COLUMN = {"x": "x", "y": "y", "z": "z", "group": "group",
                  "y2": "y2", "subject": "group"}


def request_for(op):
    req = {}
    for need in op.needs:
        if need in DEFAULT_COLUMN:
            req[need] = DEFAULT_COLUMN[need]
        elif need == "columns":
            req["columns"] = ["y", "y2", "z"]
        elif need == "predictors":
            req["predictors"] = ["p1", "p2"]
        elif need == "factors":
            req["factors"] = ["group"]
    req.update(EXTRA.get(op.name, {}))
    return req


print("=== every registry target resolves to a callable ===")
unresolved = []
for name, op in ops.REGISTRY.items():
    try:
        fn = ops._resolve(op.target)
        if not callable(fn):
            unresolved.append((name, "not callable"))
    except Exception as exc:  # noqa: BLE001
        unresolved.append((name, f"{type(exc).__name__}: {exc}"))
ok(f"all {len(ops.REGISTRY)} targets resolve", not unresolved,
   "" if not unresolved else str(unresolved[:3]))

print("\n=== every operation runs and answers with content ===")
missing_dependency = []
for name, op in sorted(ops.REGISTRY.items()):
    req = request_for(op)
    try:
        reply = ops.run(name, DF, req)
    except ops.OperationError as exc:
        text = str(exc)
        if name in NETWORK and ("crossref" in text.lower() or "internet" in text.lower()):
            skipped += 1
            print(f"  skip {name:<40} needs a network connection")
            continue
        if "not installed" in text:
            skipped += 1
            missing_dependency.append(name)
            print(f"  skip {name:<40} optional component absent")
            continue
        failed += 1
        print(f"  FAIL {name:<40} refused: {text[:52]}")
        continue
    except Exception as exc:  # noqa: BLE001
        failed += 1
        print(f"  FAIL {name:<40} {type(exc).__name__}: {str(exc)[:46]}")
        continue

    # A reply has to carry something beyond its own name.
    content = {k: v for k, v in reply.items() if k != "operation"}
    # A detector may honestly answer "none of them", and that is a reply, not
    # an empty one. Everything else has to carry something.
    detectors = {"domain", "plateau", "knee"}
    substantive = bool(content) if name in detectors else any(
        v not in (None, "", [], {}, {"columns": [], "rows": {}})
        for v in content.values()
    )
    if not substantive:
        failed += 1
        print(f"  FAIL {name:<40} replied with nothing: {list(content)[:4]}")
        continue

    # And it has to survive the pipe, which is strict JSON with no NaN.
    try:
        json.dumps(reply, allow_nan=False)
    except (ValueError, TypeError) as exc:
        failed += 1
        print(f"  FAIL {name:<40} not JSON-safe: {str(exc)[:44]}")
        continue

    passed += 1
    print(f"  ok   {name:<40} {', '.join(list(content)[:3])}")

print("\n=== bad input is refused with a sentence ===")
for label, name, req, fragment in [
    ("an unknown operation", "teleport", {}, "unknown analysis operation"),
    ("a column that is not there", "limits", {"x": "nope", "y": "y"}, "is not in this dataset"),
    ("a missing required column", "limits", {"y": "y"}, "needs a column"),
    ("no predictors", "regression", {"y": "y", "predictors": []}, "at least one column"),
]:
    try:
        ops.run(name, DF, req)
    except ops.OperationError as exc:
        ok(label, fragment.lower() in str(exc).lower(), str(exc)[:44])
    except Exception as exc:  # noqa: BLE001
        ok(label, False, f"raised {type(exc).__name__} instead")
    else:
        ok(label, False, "did not raise")

print("\n=== the catalogue the UI builds itself from ===")
cat = ops.catalogue()
ok("one entry per operation", len(cat) == len(ops.REGISTRY), f"{len(cat)}")
ok("every entry has id, label, needs, group",
   all(all(k in c for k in ("id", "label", "needs", "group")) for c in cat))
ok("the catalogue is JSON-safe", json.dumps(cat) is not None)

# The panel builds itself from this catalogue and nothing else, so anything it
# cannot express is an operation the user cannot reach. These two checks are the
# contract between the registry and AnalysisPanel.qml.
PANEL_SENDS = {"x", "y", "y2", "z", "group", "columns", "predictors", "factors"}
unsendable = sorted({(c["id"], n) for c in cat for n in c["needs"] if n not in PANEL_SENDS})
ok("every 'needs' is one the panel can send", not unsendable, str(unsendable[:3]))

ok("every required field is named in the catalogue",
   all(f.get("hint") for c in cat for f in c["fields"]),
   "a field with no hint gives the user an empty box and no idea what to type")

# And every field the panel will show has a value here, so the sweep above has
# actually exercised what the user will send.
untested = sorted({c["id"] for c in cat for f in c["fields"]
                   if f["key"] not in EXTRA.get(c["id"], {})})
ok("every required field is exercised by this suite", not untested, str(untested[:4]))

if missing_dependency:
    print(f"\nskipped (optional component absent): {', '.join(missing_dependency)}")
print(f"\n{passed} passed, {failed} failed, {skipped} skipped")
sys.exit(1 if failed else 0)
