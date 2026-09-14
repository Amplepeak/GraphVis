"""Tier 1: the numbers themselves, against answers worked out independently.

The self-test proves 434 engines draw. It does not prove any of them draws the
right thing, and "it produced ink" is not a claim anybody can cite. This suite
asks the other question: when an operation reports a number, is the number
correct.

Every expected value here comes from a closed form, a textbook identity, or a
construction whose answer is known before the program is run - never from what
the program currently returns. That distinction is the whole point. A test
written by pasting in today's output freezes today's bug.

The three bugs this suite was written to find, all of which it did:

  - `fit_nonlinear` ended in a bare `else` that fitted exponential saturation
    for any model name it did not recognise, and then labelled the result with
    the name that had been requested. Its registered default was "gaussian",
    a name it had no branch for - so the default invocation of the operation
    reported an exponential-saturation fit as a Gaussian one, with an R2 for
    the wrong curve. A wrong number is bad; a wrong number wearing the right
    name is quotable.
  - `full_factorial` could not read the shape its own field hint told the user
    to type. Every input failed, most of them with "TypeError: 'int' object is
    not iterable" raised from inside itertools, naming nothing the user wrote.
  - `bootstrap_ci` took a Python callable for `statistic`. Requests arrive as
    JSON, so the only thing a caller could send was a string, and a string
    raised "TypeError: 'str' object is not callable". The single working call
    was the one that omitted the argument.
"""
from __future__ import annotations

import json
import math
import re
from pathlib import Path

import numpy as np
import pandas as pd
import pytest

from graphvis_science.operations import run

ROOT = Path(__file__).resolve().parents[3]


# --------------------------------------------------------------------- helpers
def metric(result: dict, name: str) -> float:
    """One named row out of a result's metric table."""
    table = result.get("table") or {}
    rows = table.get("rows") or {}
    names = [str(n) for n in rows.get("metric", [])]
    if name not in names:
        raise AssertionError(f"no metric {name!r}; have {names}")
    return float(rows["value"][names.index(name)])


def close(got: float, want: float, tol: float = 1e-9) -> bool:
    return abs(float(got) - float(want)) <= tol * max(1.0, abs(float(want)))


# -------------------------------------------------------------------- calculus
def test_integral_is_exact_for_a_straight_line() -> None:
    """Trapezoid is exact on a linear integrand: int_0^4 (2x+1) dx = 20."""
    x = np.linspace(0.0, 4.0, 401)
    frame = pd.DataFrame({"x": x, "y": 2.0 * x + 1.0})
    got = run("integral", frame, {"y": "y", "x": "x"})
    assert close(got["y"][-1], 20.0, 1e-12)


def test_derivative_is_exact_for_a_quadratic() -> None:
    """Central differences are exact on a quadratic: d/dx x^2 = 2x."""
    x = np.linspace(0.0, 4.0, 401)
    frame = pd.DataFrame({"x": x, "y": x ** 2})
    got = run("derivative", frame, {"y": "y", "x": "x"})
    interior = np.asarray(got["y"][1:-1], dtype=float)
    assert np.abs(interior - 2.0 * x[1:-1]).max() < 1e-10


def test_polygon_area_of_a_known_triangle() -> None:
    """(0,0) (4,0) (0,3) encloses exactly 6."""
    frame = pd.DataFrame({"x": [0.0, 4.0, 0.0], "y": [0.0, 0.0, 3.0]})
    assert close(run("polygon_area", frame, {"x": "x", "y": "y"})["result"], 6.0, 1e-12)


# ------------------------------------------------------------------ statistics
def test_describe_matches_the_hand_computed_values() -> None:
    """[2,4,4,4,5,5,7,9]: mean 5, median 4.5, sample variance 32/7."""
    frame = pd.DataFrame({"y": [2.0, 4, 4, 4, 5, 5, 7, 9]})
    got = run("describe", frame, {"y": "y"})
    assert close(metric(got, "n"), 8.0)
    assert close(metric(got, "mean"), 5.0)
    assert close(metric(got, "median"), 4.5)
    assert close(metric(got, "variance"), 32 / 7)
    assert close(metric(got, "std"), math.sqrt(32 / 7))
    # Standard error is s/sqrt(n).
    assert close(metric(got, "std_error"), math.sqrt(32 / 7) / math.sqrt(8))
    assert close(metric(got, "minimum"), 2.0)
    assert close(metric(got, "maximum"), 9.0)


def test_one_sample_t_statistic_is_root_two() -> None:
    """[1..5] against mu=2: mean 3, s^2 = 2.5, se = sqrt(0.5), t = sqrt(2)."""
    frame = pd.DataFrame({"y": [1.0, 2, 3, 4, 5]})
    got = run("t_test", frame, {"y": "y", "mu": 2.0})
    assert close(got["statistic"], math.sqrt(2.0), 1e-12)


def test_cohen_d_for_two_groups_of_equal_variance() -> None:
    """Means 3 and 5, pooled sd sqrt(2.5): d = -2/sqrt(2.5)."""
    frame = pd.DataFrame({"y": [1.0, 2, 3, 4, 5], "y2": [3.0, 4, 5, 6, 7]})
    got = run("cohen_d", frame, {"y": "y", "y2": "y2"})
    assert close(got["result"], -2.0 / math.sqrt(2.5), 1e-12)


def test_one_way_anova_f_is_twenty_seven() -> None:
    """Groups (1,2,3) (4,5,6) (7,8,9): SSB=54 on 2 df, SSW=6 on 6 df, F=27."""
    frame = pd.DataFrame({"a": [1.0, 2, 3], "b": [4.0, 5, 6], "c": [7.0, 8, 9]})
    got = run("anova", frame, {"columns": ["a", "b", "c"]})
    assert close(got["statistic"], 27.0, 1e-9)


def test_benjamini_hochberg_and_bonferroni_by_hand() -> None:
    """p = .01 .. .05 with n=5. Every p_i * n/rank_i is exactly 0.05."""
    frame = pd.DataFrame({"y": [0.01, 0.02, 0.03, 0.04, 0.05]})
    bh = run("adjust_pvalues", frame, {"y": "y", "method": "fdr_bh"})["result"]
    assert all(close(v, 0.05, 1e-9) for v in bh), bh
    bonf = run("adjust_pvalues", frame, {"y": "y", "method": "bonferroni"})["result"]
    for got, want in zip(bonf, [0.05, 0.10, 0.15, 0.20, 0.25]):
        assert close(got, want, 1e-9)


def test_power_ttest_matches_the_standard_sample_size() -> None:
    """d=0.5, alpha=.05, power=.8, two-sided two-sample: n = 63.77 per group."""
    got = run("power_ttest", pd.DataFrame(), {"effect_size": 0.5})
    assert close(got["details"]["n_group1"], 63.765610588, 1e-6)


def test_mann_whitney_u_is_zero_for_disjoint_groups() -> None:
    """Every value of a is below every value of b, so U = 0 exactly."""
    frame = pd.DataFrame({"y": np.arange(1.0, 11), "y2": np.arange(11.0, 21)})
    got = run("nonparametric", frame, {"name": "mannwhitney", "y": "y", "y2": "y2"})
    assert close(got["statistic"], 0.0, 1e-12)
    assert got["p_value"] < 0.001


def test_brown_forsythe_is_zero_for_identical_spreads() -> None:
    """Three groups that are translations of each other have equal variance."""
    frame = pd.DataFrame({"a": [1.0, 2, 3], "b": [4.0, 5, 6], "c": [7.0, 8, 9]})
    got = run("brown_forsythe", frame, {"columns": ["a", "b", "c"]})
    assert close(got["statistic"], 0.0, 1e-9)
    assert close(got["p_value"], 1.0, 1e-9)


def test_tukey_mean_differences_are_exact() -> None:
    """Group means 2, 5, 8: the pairwise differences are 3, 6 and 3."""
    frame = pd.DataFrame({"a": [1.0, 2, 3], "b": [4.0, 5, 6], "c": [7.0, 8, 9]})
    rows = run("tukey", frame, {"columns": ["a", "b", "c"]})["table"]["rows"]
    assert [round(v, 9) for v in rows["meandiff"]] == [3.0, 6.0, 3.0]


def test_normality_separates_normal_from_exponential() -> None:
    """Not a closed form, but a decision with a known right answer."""
    rng = np.random.default_rng(11)
    normal = run("normality", pd.DataFrame({"y": rng.normal(0, 1, 400)}), {"y": "y"})
    shapiro = next(r for r in normal["result"] if r["name"] == "Shapiro-Wilk")
    assert shapiro["p_value"] > 0.05, "a normal sample was called non-normal"

    skewed = run("normality", pd.DataFrame({"y": rng.exponential(1.0, 400)}), {"y": "y"})
    shapiro = next(r for r in skewed["result"] if r["name"] == "Shapiro-Wilk")
    assert shapiro["p_value"] < 1e-6, "an exponential sample was called normal"


# ----------------------------------------------------------------------- units
def test_unit_conversions_are_exact() -> None:
    """An inch is 2.54 cm by definition; 0 degC is 273.15 K by definition."""
    got = run("unit_convert", pd.DataFrame({"y": [1.0]}),
              {"y": "y", "source": "inch", "target": "cm"})
    assert close(got["values"][0], 2.54, 1e-12)
    got = run("unit_convert", pd.DataFrame({"y": [0.0]}),
              {"y": "y", "source": "degC", "target": "K"})
    assert close(got["values"][0], 273.15, 1e-12)


# ----------------------------------------------------------------- uncertainty
def test_propagation_of_a_product_is_the_quadrature_sum() -> None:
    """a=2+-0.1 times b=3+-0.2: value 6, sigma sqrt(0.3^2 + 0.4^2) = 0.5 exactly.

    The contributions are worth checking too - 0.4 and 0.3 - because that
    split is the part of the answer that tells you what to measure better,
    and 0.4^2 + 0.3^2 = 0.5^2 is a check the whole calculation is consistent.
    """
    got = run("propagate", pd.DataFrame(), {
        "expression": "a*b",
        "inputs": {"a": {"value": 2.0, "error": 0.1},
                   "b": {"value": 3.0, "error": 0.2}},
    })
    assert close(got["value"], 6.0, 1e-12)
    assert close(got["error"], 0.5, 1e-12)
    shares = {c["name"]: c["contribution"] for c in got["contributions"]}
    assert close(shares["b"], 0.4, 1e-12)
    assert close(shares["a"], 0.3, 1e-12)
    assert close(shares["a"] ** 2 + shares["b"] ** 2, 0.25, 1e-12)


# --------------------------------------------------------------------- fitting
def test_linear_curve_fit_recovers_the_line_exactly() -> None:
    x = np.arange(0.0, 20.0)
    frame = pd.DataFrame({"x": x, "y": 3.0 * x + 7.0})
    got = run("curve_fit", frame, {"x": "x", "y": "y", "model": "linear"})
    assert close(got["parameters"]["slope"], 3.0, 1e-9)
    assert close(got["parameters"]["intercept"], 7.0, 1e-9)
    assert close(got["r2"], 1.0, 1e-12)


def test_curve_fit_rejects_a_model_it_does_not_have() -> None:
    x = np.arange(0.0, 20.0)
    frame = pd.DataFrame({"x": x, "y": 3.0 * x + 7.0})
    with pytest.raises(Exception, match="(?i)unsupported"):
        run("curve_fit", frame, {"x": "x", "y": "y", "model": "banana"})


def test_surface_polynomial_recovers_a_plane() -> None:
    """z = 2x + 3y + 1, so the degree-1 coefficients are exactly 1, 3 and 2."""
    gx, gy = np.meshgrid(np.linspace(0, 3, 12), np.linspace(0, 3, 12))
    frame = pd.DataFrame({"x": gx.ravel(), "y": gy.ravel(),
                          "z": 2.0 * gx.ravel() + 3.0 * gy.ravel() + 1.0})
    got = run("surface_polynomial", frame, {"x": "x", "y": "y", "z": "z", "degree": 1})
    coefficients = got["coefficients"]
    assert close(coefficients["x^0 y^0"], 1.0, 1e-9)
    assert close(coefficients["x^0 y^1"], 3.0, 1e-9)
    assert close(coefficients["x^1 y^0"], 2.0, 1e-9)
    assert close(got["r2"], 1.0, 1e-12)


# ------------------------------------------------- fit_nonlinear, the mislabel
def _gaussian_frame() -> pd.DataFrame:
    x = np.linspace(-5.0, 5.0, 400)
    return pd.DataFrame({"x": x, "y": 2.0 * np.exp(-(x - 1.0) ** 2 / (2 * 0.5 ** 2)) + 0.3})


def test_gaussian_fit_recovers_the_gaussian() -> None:
    """Amplitude 2, centre 1, sigma 0.5, offset 0.3 - and R2 of 1.

    This is the regression test for the mislabelling bug. Before the fix these
    parameters came back as `asymptote`, `amplitude` and `rate` with R2 = 0.078,
    reported as `"model": "gaussian"`.
    """
    got = run("fit_nonlinear", _gaussian_frame(), {"x": "x", "y": "y", "model": "Gaussian"})
    assert got["model"] == "Gaussian"
    assert close(got["params"]["amplitude"], 2.0, 1e-6)
    assert close(got["params"]["centre"], 1.0, 1e-6)
    assert close(got["params"]["sigma"], 0.5, 1e-6)
    assert close(got["params"]["offset"], 0.3, 1e-6)
    assert got["r2"] > 0.999999


def test_the_registered_default_model_is_one_that_exists() -> None:
    """The default used to be a name the fitter had no branch for.

    Calling with no model at all must give the same answer as asking for the
    default by name. If the default is ever changed to something the fitter
    cannot handle, this fails rather than silently fitting another curve.
    """
    frame = _gaussian_frame()
    default = run("fit_nonlinear", frame, {"x": "x", "y": "y"})
    assert default["r2"] > 0.999999
    assert default["model"] == "Gaussian"


def test_an_unknown_nonlinear_model_is_refused_not_substituted() -> None:
    frame = _gaussian_frame()
    with pytest.raises(Exception, match="(?i)unsupported nonlinear model"):
        run("fit_nonlinear", frame, {"x": "x", "y": "y", "model": "banana"})


def test_each_nonlinear_model_is_actually_a_different_model() -> None:
    """Distinct names must give distinct fits.

    The failure being guarded against is several names quietly resolving to one
    branch, which is exactly what used to happen. On Gaussian data the Gaussian
    fit is perfect and the others are not; if any two of these ever agree to
    machine precision, one of them is not being fitted.
    """
    frame = _gaussian_frame()
    scores = {}
    for name in ("Gaussian", "Lorentzian", "Exponential saturation",
                 "Exponential decay", "Michaelis-Menten", "Logistic"):
        scores[name] = run("fit_nonlinear", frame,
                           {"x": "x", "y": "y", "model": name})["r2"]
    assert scores["Gaussian"] > 0.999999
    assert scores["Lorentzian"] < scores["Gaussian"]
    assert scores["Exponential saturation"] < 0.5
    rounded = [round(v, 9) for v in scores.values()]
    assert len(set(rounded)) > 1, f"models are not distinct: {scores}"


# ---------------------------------------------------------------------- signal
def test_fft_puts_the_peak_at_the_signal_frequency() -> None:
    """A 5 Hz sine sampled for exactly one second peaks in the 5 Hz bin."""
    t = np.linspace(0.0, 1.0, 200, endpoint=False)
    frame = pd.DataFrame({"t": t, "y": np.sin(2 * np.pi * 5 * t)})
    got = run("fft", frame, {"y": "y", "x": "t"})
    frequency = np.asarray(got["x"], dtype=float)
    magnitude = np.asarray(got["y"], dtype=float)
    assert close(frequency[int(np.argmax(magnitude))], 5.0, 1e-6)


def test_hilbert_envelope_of_a_unit_sine_is_one() -> None:
    t = np.linspace(0.0, 1.0, 512, endpoint=False)
    frame = pd.DataFrame({"t": t, "y": np.sin(2 * np.pi * 8 * t)})
    got = run("hilbert", frame, {"y": "y", "x": "t"})
    # The transform rings at the ends, as it must; the interior is the test.
    envelope = np.asarray(got["y"], dtype=float)[20:-20]
    assert np.abs(envelope - 1.0).max() < 1e-6


def test_smoothers_leave_a_straight_line_alone() -> None:
    """Savitzky-Golay and LOWESS both reproduce a polynomial of low order."""
    t = np.linspace(0.0, 1.0, 200)
    frame = pd.DataFrame({"t": t, "y": 3.0 * t + 1.0})
    for op in ("savgol", "lowess"):
        got = np.asarray(run(op, frame, {"y": "y", "x": "t"})["y"], dtype=float)
        assert np.abs(got - (3.0 * t + 1.0)).max() < 1e-6, op


# ---------------------------------------------------------------------- limits
def test_asymptote_of_a_saturating_exponential() -> None:
    """y = 10 - 8 exp(-x/2) approaches exactly 10."""
    x = np.linspace(0.1, 10.0, 60)
    frame = pd.DataFrame({"x": x, "y": 10.0 - 8.0 * np.exp(-0.5 * x)})
    got = run("asymptote", frame, {"x": "x", "y": "y"})
    assert got["found"] is True
    assert close(got["asymptote"], 10.0, 1e-6)
    assert got["r2"] > 0.999999


# ----------------------------------------------------------------- reliability
def test_failure_probability_counts_the_threshold_as_a_failure() -> None:
    """The boundary is inclusive: reaching the limit counts as having failed.

    1..100 with a threshold of 70 gives 70..100, which is 31 values, not the 30
    that "above 70" would suggest. That is the defensible engineering
    convention and it is what the code does - this test exists to pin it
    deliberately, because a silent change of the comparison would move a
    reported probability by one part in n with nothing to catch it.

    Both directions are inclusive, so the two probabilities sum to more than 1
    by exactly the share of values sitting on the threshold. That is inherent
    to an inclusive boundary rather than a fault, and is asserted here so the
    behaviour is written down somewhere.
    """
    frame = pd.DataFrame({"y": np.arange(1.0, 101)})
    high = run("failure_probability", frame, {"y": "y", "threshold": 70.0})
    assert close(high["probability"], 0.31, 1e-12)
    assert high["n"] == 100
    # Standard error of a proportion: sqrt(p(1-p)/n).
    assert close(high["standard_error"], math.sqrt(0.31 * 0.69 / 100), 1e-12)

    low = run("failure_probability", frame,
              {"y": "y", "threshold": 70.0, "higher_is_failure": False})
    assert close(low["probability"], 0.70, 1e-12)
    assert close(high["probability"] + low["probability"], 1.01, 1e-12)

    # A threshold that lands between observations has no such overlap.
    between = run("failure_probability", frame, {"y": "y", "threshold": 70.5})
    assert close(between["probability"], 0.30, 1e-12)


def test_bootstrap_ci_accepts_a_named_statistic() -> None:
    """The regression test for 'str' object is not callable.

    A request is JSON, so a statistic can only arrive as a string. Taking a
    callable meant every value the application could send was a crash.
    """
    frame = pd.DataFrame({"y": np.arange(1.0, 101)})
    for name, want in (("mean", 50.5), ("median", 50.5), ("max", 100.0), ("min", 1.0)):
        got = run("bootstrap_ci", frame,
                  {"y": "y", "statistic": name, "seed": 1, "resamples": 300})
        assert close(got["estimate"], want, 1e-9), name
        assert got["low"] <= got["estimate"] <= got["high"], name

    with pytest.raises(Exception, match="(?i)unknown bootstrap statistic"):
        run("bootstrap_ci", frame, {"y": "y", "statistic": "banana"})


def test_weibull_recovers_the_shape_and_scale_it_was_given() -> None:
    """Statistical rather than exact, so the sample is large and the seed fixed."""
    rng = np.random.default_rng(4)
    sample = 10.0 * rng.weibull(2.0, 20000)
    got = run("weibull", pd.DataFrame({"y": sample}), {"y": "y"})
    assert abs(got["details"]["shape"] - 2.0) < 0.05
    assert abs(got["details"]["scale"] - 10.0) < 0.15


# -------------------------------------------------------- design of experiments
def test_full_factorial_takes_explicit_levels() -> None:
    got = run("full_factorial", pd.DataFrame(),
              {"levels": {"temperature": [20, 30, 40], "pH": [6, 7]}})
    rows = got["design"]["rows"]
    assert len(rows["temperature"]) == 6
    assert sorted(set(rows["temperature"])) == [20.0, 30.0, 40.0]
    assert sorted(set(rows["pH"])) == [6.0, 7.0]


def test_full_factorial_takes_a_level_count() -> None:
    """The shape the field hint asks for, which used to raise from itertools."""
    got = run("full_factorial", pd.DataFrame(), {"levels": {"temperature": 3, "pH": 2}})
    rows = got["design"]["rows"]
    assert len(rows["temperature"]) == 6
    assert sorted(set(rows["temperature"])) == [-1.0, 0.0, 1.0]
    assert sorted(set(rows["pH"])) == [-1.0, 1.0]


def test_full_factorial_says_what_is_wrong() -> None:
    for bad, pattern in (({"a": 1}, "at least 2"),
                         ({"a": "xyz"}, "level count"),
                         ({}, "No factor levels")):
        with pytest.raises(Exception, match=f"(?i){pattern}"):
            run("full_factorial", pd.DataFrame(), {"levels": bad})


def test_designs_have_the_run_count_they_promise() -> None:
    bounds = {"a": [0.0, 1.0], "b": [0.0, 1.0]}
    lhs = run("latin_hypercube", pd.DataFrame(),
              {"bounds": bounds, "n": 16, "seed": 3})
    assert len(lhs["design"]["rows"]["a"]) == 16
    # Central composite on k=2: 4 corners + 4 axial + 5 centre = 13.
    ccd = run("central_composite", pd.DataFrame(), {"bounds": bounds})
    assert ccd["diagnostics"]["runs"] == 13
    assert close(ccd["diagnostics"]["alpha"], math.sqrt(2.0), 1e-9)


# ----------------------------------------------------------------- multivariate
def test_pca_of_perfectly_collinear_columns_is_one_component() -> None:
    """b = 2a exactly, so PC1 must carry all of the variance."""
    rng = np.random.default_rng(2)
    a = rng.normal(0.0, 1.0, 300)
    frame = pd.DataFrame({"a": a, "b": 2.0 * a})
    got = run("pca", frame, {"columns": ["a", "b"]})
    ratios = got["table"]["rows"]["explained_variance_ratio"]
    assert close(ratios[0], 1.0, 1e-9), ratios


def test_peak_detection_finds_the_centre_it_was_given() -> None:
    x = np.linspace(-5.0, 5.0, 1001)
    frame = pd.DataFrame({"x": x, "y": np.exp(-(x - 1.0) ** 2 / (2 * 0.5 ** 2))})
    rows = run("peaks", frame, {"x": "x", "y": "y"})["rows"]
    assert len(rows["x"]) == 1
    assert close(rows["x"][0], 1.0, 1e-2)
    assert close(rows["height"][0], 1.0, 1e-6)
    # FWHM of a Gaussian is 2 sqrt(2 ln 2) sigma = 2.3548 * 0.5.
    assert abs(rows["FWHM"][0] - 2.0 * math.sqrt(2 * math.log(2)) * 0.5) < 0.02


# ------------------------------------------------------------------ regression
def test_multiple_regression_recovers_exact_coefficients() -> None:
    """y = 2*x1 + 3*x2 + 5, noiseless, so the coefficients are determined."""
    frame = pd.DataFrame({"x1": [1.0, 2, 3, 4, 5, 6], "x2": [1.0, 0, 2, 1, 3, 2]})
    frame["y"] = 2.0 * frame.x1 + 3.0 * frame.x2 + 5.0
    rows = run("regression", frame, {"predictors": ["x1", "x2"], "y": "y"})["coefficients"]["rows"]
    coefficients = dict(zip(rows["term"], rows["coefficient"]))
    assert close(coefficients["Intercept"], 5.0, 1e-9)
    assert close(coefficients["x1"], 2.0, 1e-9)
    assert close(coefficients["x2"], 3.0, 1e-9)


def test_global_linear_shares_one_slope_across_datasets() -> None:
    """Two lines of slope 2 with intercepts 1 and 2: one slope, two intercepts."""
    got = run("global_linear", pd.DataFrame(), {"datasets": [
        {"x": [0, 1, 2], "y": [1, 3, 5]},
        {"x": [0, 1, 2], "y": [2, 4, 6]},
    ]})
    parameters = got["parameters"]
    assert close(parameters["shared_slope"], 2.0, 1e-9)
    assert close(parameters["intercept_1"], 1.0, 1e-9)
    assert close(parameters["intercept_2"], 2.0, 1e-9)


# ------------------------------------------------------------- electrochemistry
def test_modified_gompertz_recovers_its_own_parameters() -> None:
    """Data generated from the model itself, so P, Rm and lag are known exactly.

    H(t) = P exp(-exp(Rm e / P (lag - t) + 1)) with P=300, Rm=20, lag=5.
    """
    t = np.linspace(0.0, 48.0, 40)
    P, rate, lag = 300.0, 20.0, 5.0
    h = P * np.exp(-np.exp(rate * np.e / P * (lag - t) + 1.0))
    got = run("gompertz", pd.DataFrame({"t": t, "H": h}), {"x": "t", "y": "H"})
    assert close(got["P"], P, 1e-6)
    assert close(got["Rm"], rate, 1e-6)
    assert close(got["lag"], lag, 1e-6)
    assert got["r2"] > 0.999999


def test_nyquist_semicircle_recovers_the_resistances() -> None:
    """A semicircle centred at Rs + Rct/2 with radius Rct/2, for Rs=5, Rct=20."""
    theta = np.linspace(0.0, np.pi, 40)
    rs, rct = 5.0, 20.0
    frame = pd.DataFrame({"re": rs + rct / 2 + (rct / 2) * np.cos(theta),
                          "im": -(rct / 2) * np.sin(theta)})
    got = run("nyquist", frame, {"x": "re", "y": "im"})
    assert close(got["Rs"], rs, 1e-6)
    assert close(got["Rct"], rct, 1e-6)


def test_polarisation_peak_power_is_where_the_product_peaks() -> None:
    """V = 1 - I, so P = I(1-I), which peaks at exactly 0.25 when I = 0.5."""
    current = np.linspace(0.0, 1.0, 101)
    frame = pd.DataFrame({"i": current, "v": 1.0 - current})
    got = run("polarisation", frame, {"x": "i", "y": "v"})
    assert close(got["i_peak"], 0.5, 1e-12)
    assert close(got["p_peak"], 0.25, 1e-12)
    assert close(got["v_peak"], 0.5, 1e-12)


# ------------------------------------------------------------------ monte carlo
def test_monte_carlo_sum_of_two_normals() -> None:
    """a + b with both N(0,1) is N(0, sqrt(2)).

    Tolerances are set from the sampling error of the estimates themselves -
    the standard error of an sd is sd/sqrt(2n) - rather than picked to fit.
    """
    n = 200_000
    got = run("monte_carlo", pd.DataFrame(), {
        "model": "a+b",
        "distributions": {"a": {"kind": "normal", "loc": 0, "scale": 1},
                          "b": {"kind": "normal", "loc": 0, "scale": 1}},
        "n": n, "seed": 1,
    })["details"]
    expected_sd = math.sqrt(2.0)
    assert abs(got["mean"]) < 5 * expected_sd / math.sqrt(n)
    assert abs(got["std"] - expected_sd) < 5 * expected_sd / math.sqrt(2 * n)


# ---------------------------------------------------------------------- limits
def test_plateau_is_found_exactly_where_the_curve_flattens() -> None:
    x = np.arange(0.0, 21.0)
    frame = pd.DataFrame({"x": x, "y": np.where(x < 10, x, 10.0)})
    got = run("plateau", frame, {"x": "x", "y": "y"})
    assert got["found"] is True
    assert close(got["level"], 10.0, 1e-9)
    assert close(got["x_start"], 10.0, 1e-9)


def test_knee_is_found_near_the_corner_with_a_known_smoothing_bias() -> None:
    """The reported knee is pulled toward the middle of the range.

    detect_knee smooths with a Savitzky-Golay window of 15% of the samples
    before taking the point of maximum distance from the chord. On a perfectly
    sharp corner - the worst case for any smoother - that displaces the answer
    toward the centre of the x range by up to about 2.5% of it. Measured, not
    assumed: corners at 3, 5, 8, 12 and 15 over 0..20 come back at 3.5, 5.5,
    8.17, 11.83 and 14.5.

    This is a property of smoothing rather than a defect, and on the noisy data
    the smoothing exists for it is worth the bias. It is asserted here so that
    the bias stays bounded: if it ever grows past 5% of the range, something has
    changed that nobody decided.
    """
    span = 20.0
    for corner in (3.0, 5.0, 8.0, 12.0, 15.0):
        x = np.linspace(0.0, span, 201)
        y = np.where(x <= corner, x * 4.0, corner * 4.0 + (x - corner) * 0.2)
        got = run("knee", pd.DataFrame({"x": x, "y": y}), {"x": "x", "y": "y"})
        assert got["found"] is True, corner
        assert abs(got["x"] - corner) < 0.05 * span, (corner, got["x"])


# -------------------------------------------------------------------- symbolic
def test_function_curve_evaluates_the_expression() -> None:
    """x^2 on [0,2], checked at every returned point rather than at the ends."""
    got = run("function_curve", pd.DataFrame(),
              {"expression": "x^2", "domain": [0, 2], "samples": 200})
    x = np.asarray(got["x"], dtype=float)
    y = np.asarray(got["y"], dtype=float)
    assert close(x[0], 0.0, 1e-12) and close(x[-1], 2.0, 1e-12)
    assert np.abs(y - x ** 2).max() < 1e-12


# --------------------------------------------------------- degenerate inputs
#
# Found by firing six degenerate frames at all 85 non-network operations - 510
# calls - and looking for exceptions that were not OperationError or a plain
# ValueError. Seven came back, across three operations. These are the two that
# were real defects rather than an engine's own clear complaint.
def test_derivative_refuses_fewer_than_two_points() -> None:
    """np.gradient on an empty array says nothing the user can act on.

    A single row, or a column that is entirely NaN, reaches np.gradient empty
    because the non-finite rows have already been dropped - and it raises
    "index 0 is out of bounds for axis 0 with size 0", which is a true statement
    about an array the user never saw.
    """
    frame = pd.DataFrame({"x": [1.0], "y": [2.0]})
    with pytest.raises(Exception, match="(?i)at least two points"):
        run("derivative", frame, {"y": "y", "x": "x"})

    nan_frame = pd.DataFrame({"x": [1.0, 2, 3], "y": [float("nan")] * 3})
    with pytest.raises(Exception, match="(?i)at least two points"):
        run("derivative", nan_frame, {"y": "y", "x": "x"})


def test_factorial_anova_refuses_duplicate_column_choices() -> None:
    """The response cannot also be a factor, and a factor cannot repeat.

    Selecting a column twice makes `df[[...]]` return repeated names, so
    `df[name]` is a DataFrame rather than a Series and patsy fails with
    "'DataFrame' object has no attribute 'dtype'" - naming neither the response
    nor the factor. Both mistakes are one click away in a multi-select.
    """
    frame = pd.DataFrame({"x": list("aabb") * 3,
                          "g": list("cd") * 6,
                          "y": np.arange(12.0)})
    with pytest.raises(Exception, match="(?i)cannot also be a factor"):
        run("factorial_anova", frame, {"y": "y", "factors": ["x", "y"]})
    with pytest.raises(Exception, match="(?i)only be listed once"):
        run("factorial_anova", frame, {"y": "y", "factors": ["x", "x"]})
    # And the valid case still works, which is the point of guarding rather
    # than widening.
    ok = run("factorial_anova", frame, {"y": "y", "factors": ["x", "g"]})
    assert "ANOVA" in ok["name"]


def test_a_pickle_is_not_opened_without_being_asked_twice() -> None:
    """Opening a .pkl runs the code inside it.

    `pd.read_pickle` is an interpreter, not a parser, and there is no safe mode
    - which is why the .npy and .npz readers beside it pass allow_pickle=False.
    GraphVis exists to open files that came from a paper or a collaborator, so
    "plot this .pkl for me" is both an ordinary request and an effective way to
    run code on someone's machine.
    """
    import pickle
    import tempfile

    from graphvis_science.data.importer import ImportError_, import_to_arrow

    with tempfile.NamedTemporaryFile(suffix=".pkl", delete=False) as handle:
        pickle.dump(pd.DataFrame({"a": [1, 2, 3]}), handle)
        path = handle.name

    with pytest.raises(ImportError_, match="(?i)runs any code stored inside it"):
        import_to_arrow(path, path + ".arrow")




# ------------------------------------------------ the catalogue's own counts
#
# Comments and menu text quote how big the catalogue is. It grew four times
# over this port - 318 entries, then 433, then 1,769, then 2,116 - and each
# round left the previous figure in whichever file had quoted it. By the end
# the tree asserted, in the present tense and in four places, that there were
# 318 entries in 30 categories; the Renderer menu told the user that Qt 2-D
# draws "the 203 catalogue engines"; and a search cap written to mean "all of
# them" had been raised from 400 to 2,000 against a catalogue of 2,116, so it
# was already truncating again.
#
# A stale count is not cosmetic here. Each of those numbers is the argument
# for a design decision - why the categories are collapsed, why the search is
# grouped by engine, why the sidebar is a SplitView - and a reader who checks
# one, finds it wrong, and has no way to tell which of the others still hold
# has lost the use of all of them.
#
# Two checks. The first is the one that can be exact: the catalogue file
# states three counts in its header, and they are recounted from its own
# contents. The second is deliberately a denylist rather than a parser.
# Deciding from English whether "33 engines have more than one entry" is a
# claim about the whole catalogue is a losing game - a first attempt at it
# flagged that line, a QStringLiteral format string reading "%1 categories",
# and a sentence that had said "then catalogued" one word past where the
# scanner stopped reading. What can be decided exactly is whether a figure is
# a size this catalogue has ALREADY outgrown, which is the failure that keeps
# happening. When the catalogue next grows, its old size joins this list.
_CATALOGUE = ROOT / "config" / "graph_catalogue.json"

_SUPERSEDED = {
    "entries": (318, 433, 1359, 1769),
    "engines": (135, 203, 318, 395, 433),
    "categories": (30,),
}

# A figure may be quoted as history. The sentence has to say so, and these are
# the words that say it - matched over the comment block rather than the line,
# because a wrapped comment routinely puts "then catalogued" on the next line
# from the number it qualifies.
_HISTORICAL = ("then catalogued", "when the catalogue held", "was set when",
               "used to", "has grown", "had grown", "that was the state",
               "returned", "then held", "as it stood")


@pytest.mark.skipif(not _CATALOGUE.exists(), reason="run from a source tree")
def test_the_catalogue_header_matches_the_catalogue() -> None:
    """The file's declared counts, recounted from its own contents.

    The header is what everything else quotes, so it is the one number that
    has to be checked against the thing itself rather than against another
    copy of the claim.
    """
    catalogue = json.loads(_CATALOGUE.read_text(encoding="utf-8"))
    entries = [e for c in catalogue["categories"] for e in c.get("entries", [])]
    assert len(catalogue["categories"]) == catalogue["category_count"]
    assert len(entries) == catalogue["entry_count"]
    assert len({e["engine"] for e in entries}) == catalogue["engine_count"]


@pytest.mark.skipif(not _CATALOGUE.exists(), reason="run from a source tree")
def test_no_source_text_quotes_a_size_the_catalogue_has_outgrown() -> None:
    catalogue = json.loads(_CATALOGUE.read_text(encoding="utf-8"))
    current = {"entries": catalogue["entry_count"],
               "engines": catalogue["engine_count"],
               "categories": catalogue["category_count"]}

    pattern = re.compile(
        r"([0-9][0-9,]*)\s+(?:catalogue\s+|graph\s+)?(entries|engines|categories)\b")
    # Code, plus the README - the two places a reader meets a count without
    # any surrounding discussion of which catalogue is meant.
    #
    # docs/PORT-PLAN.md is deliberately out of scope. Its subject IS the
    # difference between the two catalogues, so it quotes v17's 318 / 30 / 135
    # and v18's 2,116 / 46 / 434 on adjacent lines, correctly, and every
    # v17 figure in it trips a denylist that cannot tell them apart. A check
    # that has to be argued with on every run stops being read; that document
    # carries the explanation instead, which is the right home for it.
    roots = ["app/src", "app/qml", "native/plot2d/src", "native/plot2d/include",
             "README.md"]

    stale: list[str] = []
    for target in roots:
        path = ROOT / target
        sources = [path] if path.is_file() else [
            p for p in path.rglob("*")
            if p.suffix in {".cpp", ".h", ".qml", ".md"}]
        for source in sources:
            lines = source.read_text(encoding="utf-8", errors="ignore").splitlines()
            for index, line in enumerate(lines):
                for match in pattern.finditer(line):
                    value = int(match.group(1).replace(",", ""))
                    noun = match.group(2)
                    if value == current[noun]:
                        continue
                    if value not in _SUPERSEDED[noun]:
                        continue
                    # The surrounding lines as one sentence. Comment markers
                    # are stripped before joining, because a wrapped comment
                    # puts "// " between two words of the phrase being looked
                    # for - which is how "engines then / catalogued" read as a
                    # present-tense claim on the first attempt.
                    window = lines[max(0, index - 1):index + 3]
                    context = " ".join(
                        re.sub(r"^\s*(?://+|#+|\*)\s?", "", text)
                        for text in window).lower()
                    if any(word in context for word in _HISTORICAL):
                        continue
                    stale.append(
                        f"{source.relative_to(ROOT)}:{index + 1}: {line.strip()}")

    assert not stale, (
        f"the catalogue holds {current['entries']} entries / "
        f"{current['engines']} engines / {current['categories']} categories. "
        "These quote a size it has outgrown, without saying they mean the "
        "past:\n  " + "\n  ".join(stale[:20]))
