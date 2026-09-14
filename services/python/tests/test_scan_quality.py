"""The catalogue scan: is the recommendation right, and is the reason honest?

"Best for this data" is advice, and advice is judged by whether it is correct
and by whether the justification under it is something the reader can argue
with. These tests use datasets whose right answer is not in doubt - a time
column, a perfect correlation, four named catalysts - and check both halves.

Three faults this suite was written against, all of which it found:

  - One column filling two roles. With two numeric columns and no declared
    response, the response fell back to a column that was already a parameter,
    so the top recommendation for a (voltage, current) table was a heatmap of
    `x=voltage, y=current, z=current` - a response surface whose height is one
    of its own axes - explained by a sentence calling 'current' both an
    independent swept parameter and the response.
  - "Strong dependency detected" on every pair, whatever the number printed
    immediately after it. Eight independent random columns produced
    recommendations reading "Strong dependency detected between 'v2' and 'v3'
    (score 0.20)". Mutual information from a finite histogram is biased upward,
    so independent columns floor at about 0.2 - the scanner was reporting its
    own estimator's noise as a finding, in the program's own voice.
  - No grouped comparison at all. The scanner ranked only over numeric columns,
    so a table of (catalyst, yield) got a histogram of yield and nothing else:
    the four groups that are the entire point of the experiment appeared
    nowhere. A measurement across a few named conditions is the commonest plot
    in science.
"""
from __future__ import annotations

import numpy as np
import pandas as pd
import pytest

from graphvis_science.analysis.intelligent_scan import column_roles, scan_dataset


class _Dataset:
    """What service.py builds for `dataset.scan`, and nothing more.

    Kept deliberately identical to the adapter in the service, so these tests
    exercise the path production actually takes rather than a better-informed
    one that never happens. When the shim gains an attribute this gains it too
    - and `test_the_test_double_still_matches_the_service_shim` below fails if
    the two drift, because a double that is quietly richer than the real thing
    tests a program nobody runs.
    """

    def __init__(self, frame: pd.DataFrame) -> None:
        self.path = "test"
        self.name = "test"
        self.df = frame
        self.numeric_columns = [str(c) for c in frame.select_dtypes("number").columns]
        self.matrices: dict = {}
        self.volumes: dict = {}
        self.parameter_columns, self.response_columns = column_roles(frame)


def recommendations(frame: pd.DataFrame, budget: float = 8.0) -> list[dict]:
    out = scan_dataset(_Dataset(frame), time_budget_seconds=budget)
    return [r if isinstance(r, dict) else r.to_dict()
            for r in out.get("recommendations", [])]


def graphs(recs: list[dict]) -> list[str]:
    return [r["graph"] for r in recs]


# ------------------------------------------------------- one column, one role
@pytest.mark.parametrize("frame", [
    pd.DataFrame({"voltage": np.linspace(0, 1, 200),
                  "current": np.linspace(0, 2, 200)}),
    pd.DataFrame({"time_h": np.linspace(0, 100, 300),
                  "concentration": np.exp(-np.linspace(0, 100, 300) / 30)}),
    pd.DataFrame({"temperature": np.linspace(20, 80, 120),
                  "pressure": np.linspace(1, 5, 120),
                  "yield_pct": np.linspace(10, 90, 120)}),
])
def test_no_recommendation_gives_one_column_two_jobs(frame: pd.DataFrame) -> None:
    for rec in recommendations(frame):
        columns = [v for v in rec["mappings"].values() if v]
        assert len(columns) == len(set(columns)), (
            f"{rec['graph']} maps one column to two roles: {rec['mappings']}"
        )


def test_two_columns_are_not_offered_a_response_surface() -> None:
    """A heatmap needs two parameters and a separate response - three columns.

    This was the visible face of the duplicate-role bug: the top suggestion for
    a two-column table was a 2D Heatmap, scoring 0.92.
    """
    frame = pd.DataFrame({"voltage": np.linspace(0, 1, 200),
                          "current": np.linspace(0, 2, 200)})
    surfaces = {"2D Heatmap", "2D Contour", "3D Topography / Surface"}
    assert not surfaces.intersection(graphs(recommendations(frame)))


def test_three_columns_still_get_the_response_surface() -> None:
    """The fix must not cost the case the feature exists for."""
    gx, gy = np.meshgrid(np.linspace(0, 1, 25), np.linspace(0, 1, 25))
    frame = pd.DataFrame({"temperature": gx.ravel(), "ph": gy.ravel(),
                          "removal_efficiency": (gx * gy).ravel()})
    top = graphs(recommendations(frame))[:3]
    assert "2D Heatmap" in top
    assert "2D Contour" in top


# --------------------------------------------------- the reason must be honest
def test_independent_columns_produce_no_claimed_relationship() -> None:
    """Eight independent normals. There is nothing to find, so nothing is said.

    The mutual-information estimate for independent columns is not zero - a
    finite histogram biases it upward to about 0.2 - so this is the case that
    decides whether the scanner reports its own noise floor as a discovery.
    """
    rng = np.random.default_rng(0)
    frame = pd.DataFrame({f"v{i}": rng.normal(0, 1, 200) for i in range(8)})
    for rec in recommendations(frame):
        assert "relationship between" not in rec["reason"], rec["reason"]
        assert "dependency detected" not in rec["reason"].lower(), rec["reason"]


def test_a_real_relationship_is_reported_with_its_statistics() -> None:
    """A perfect correlation is called strong, and the numbers are shown."""
    x = np.linspace(0, 10, 300)
    frame = pd.DataFrame({"voltage": x, "current": 2.0 * x})
    reasons = [r["reason"] for r in recommendations(frame)
               if "relationship between" in r["reason"]]
    assert reasons, "a perfect correlation was not reported at all"
    best = reasons[0]
    assert "strong" in best.lower()
    # The justification has to carry the evidence, not just an adjective.
    assert "Pearson" in best and "Spearman" in best


def test_the_adjective_tracks_the_evidence() -> None:
    """Weak structure must not be described in the words used for strong.

    Built with a deliberately noisy relationship, so the dependency lands
    somewhere in the middle of the scale rather than at either end.
    """
    rng = np.random.default_rng(3)
    x = rng.normal(0, 1, 400)
    frame = pd.DataFrame({"driver": x, "measured": x + rng.normal(0, 2.2, 400)})
    for rec in recommendations(frame):
        reason = rec["reason"]
        if "relationship between" not in reason:
            continue
        strong = "a strong relationship" in reason.lower()
        # Pearson is printed in the sentence; parse it back and check the word
        # chosen matches the number shown, whichever way it came out.
        shown = float(reason.split("Pearson ")[1].split(",")[0])
        assert strong == (abs(shown) >= 0.8), (reason, shown)


# ------------------------------------------------------ grouped comparisons
def test_a_categorical_column_earns_a_grouped_comparison() -> None:
    """(catalyst, yield) is a group comparison, not a histogram of yield."""
    rng = np.random.default_rng(0)
    catalyst = rng.choice(list("ABCD"), 200)
    frame = pd.DataFrame({
        "catalyst": catalyst,
        "yield_pct": rng.normal(70, 8, 200) + np.where(catalyst == "A", 15.0, 0.0),
    })
    recs = recommendations(frame)
    names = graphs(recs)
    assert "Box Plot" in names
    assert "Violin Plot" in names
    assert "Bar Chart" in names
    # And it should be the headline, not an afterthought below the histogram.
    assert names[0] == "Box Plot", names[:4]

    box = next(r for r in recs if r["graph"] == "Box Plot")
    assert box["mappings"] == {"x": "catalyst", "y": "yield_pct"}
    assert "4 groups" in box["reason"]
    # A real effect, so the reason should say the comparison is worth drawing.
    assert box["diagnostics"]["eta_squared"] > 0.2
    assert "worth drawing" in box["reason"]


def test_a_grouping_that_explains_nothing_is_ranked_as_such() -> None:
    """The same graph, honestly demoted when the groups overlap.

    A scanner that recommends a box plot whenever a text column exists is not
    advice, it is a schema reader. The effect size is what separates the two,
    so a grouping explaining almost no variance has to rank below the plain
    distribution views and say why.
    """
    rng = np.random.default_rng(5)
    frame = pd.DataFrame({"batch": rng.choice(list("XYZ"), 200),
                          "yield_pct": rng.normal(70, 8, 200)})
    recs = recommendations(frame)
    box = next((r for r in recs if r["graph"] == "Box Plot"), None)
    assert box is not None, "the comparison should still be offered"
    assert box["diagnostics"]["eta_squared"] < 0.05
    assert "expect the groups to overlap" in box["reason"]

    order = graphs(recs)
    assert order.index("Histogram") < order.index("Box Plot"), order


def test_a_continuous_column_is_not_treated_as_a_grouping() -> None:
    """Twelve distinct readings are not twelve experimental conditions."""
    rng = np.random.default_rng(7)
    frame = pd.DataFrame({"sensor_mv": rng.normal(0, 1, 400),
                          "reading": rng.normal(5, 2, 400)})
    for rec in recommendations(frame):
        if rec["graph"] in ("Box Plot", "Violin Plot", "Bar Chart"):
            raise AssertionError(f"grouped a continuous column: {rec['mappings']}")


# ------------------------------------------------------------------ unchanged
def test_a_time_column_still_leads_to_a_line_chart() -> None:
    t = np.linspace(0, 100, 300)
    frame = pd.DataFrame({"time_h": t, "concentration": 5 * np.exp(-t / 30)})
    assert graphs(recommendations(frame))[0] == "Line Chart"


def test_latitude_and_longitude_still_lead_to_a_map() -> None:
    rng = np.random.default_rng(1)
    frame = pd.DataFrame({"latitude": rng.uniform(50, 55, 200),
                          "longitude": rng.uniform(-5, 0, 200),
                          "rainfall": rng.gamma(2, 3, 200)})
    assert graphs(recommendations(frame))[0] == "Geo Scatter"


def test_every_recommendation_carries_a_reason() -> None:
    """An unexplained ranking is a number nobody can check."""
    rng = np.random.default_rng(2)
    catalyst = rng.choice(list("AB"), 150)
    frame = pd.DataFrame({
        "time_h": np.linspace(0, 10, 150),
        "catalyst": catalyst,
        "yield_pct": np.linspace(0, 50, 150) + np.where(catalyst == "A", 8.0, 0.0),
    })
    for rec in recommendations(frame):
        assert rec["reason"].strip(), rec["graph"]
        assert len(rec["reason"]) > 25, (rec["graph"], rec["reason"])
        assert 0.0 <= rec["score"] <= 1.0


# --------------------------------------------------- column roles and advice
def test_the_test_double_still_matches_the_service_shim() -> None:
    """`_Dataset` above must expose exactly what `_ArrowDataset` does.

    The shim lives inside a function body in service.py, so it cannot be
    imported; this reads the attributes it assigns. The point is narrow and
    worth the awkwardness: every scan test in this file runs against the
    double, so a double that has an attribute production lacks turns the whole
    suite into a test of a program that is never shipped. That is precisely
    what had happened - the shim declared no roles at all while the scanner
    asked for them - and it is what this stops recurring silently.
    """
    import re
    from pathlib import Path

    service = (Path(__file__).resolve().parents[1] / "graphvis_science"
               / "service.py").read_text(encoding="utf-8")
    lines = service.splitlines()
    start = next((i for i, line in enumerate(lines)
                  if line.strip() == "class _ArrowDataset:"), None)
    assert start is not None, "the _ArrowDataset shim has moved or been renamed"
    # To the next line at or outside the class's own indentation. Stopping at
    # the first blank line instead would make a blank line inside the class
    # look like a shim that had lost half its attributes.
    indent = len(lines[start]) - len(lines[start].lstrip())
    end = start + 1
    while end < len(lines):
        line = lines[end]
        if line.strip() and (len(line) - len(line.lstrip())) <= indent:
            break
        end += 1
    # Tuple unpacking assigns two names on one line; the pattern sees both.
    shim = set(re.findall(r"self\.(\w+)\s*(?:[,=]|:\s*\w+\s*=)",
                          "\n".join(lines[start:end])))
    double = set(vars(_Dataset(pd.DataFrame({"x": [1.0, 2.0]}))))
    assert shim == double, (
        "the scan test double and the service shim have drifted apart, so "
        "these tests no longer describe the program that ships.\n"
        f"  only in service.py: {sorted(shim - double) or 'none'}\n"
        f"  only in the double: {sorted(double - shim) or 'none'}")


def test_column_roles_separates_swept_inputs_from_measured_outputs() -> None:
    frame = pd.DataFrame({"temperature": [1.0], "flow_rate_input": [1.0],
                          "yield_pct": [1.0], "label": ["a"]})
    parameters, responses = column_roles(frame)
    assert "temperature" in parameters
    assert "yield_pct" in responses
    # Non-numeric columns are not roles, and no column holds two.
    assert "label" not in parameters and "label" not in responses
    assert not set(parameters) & set(responses)


def test_a_two_parameter_sweep_is_advised_a_response_surface() -> None:
    """The advisor's whole reason for existing, on a bare DataFrame.

    `intelligent_visualization_advisor` recommends a heatmap/contour/surface
    when a dataset has two or more swept parameters and a response. It counted
    parameters through `getattr(dataset, 'parameter_columns', ())`, which a
    plain DataFrame does not have - and a plain DataFrame is the only thing
    v18 ever passes it. So the count was zero for every dataset the shipped
    program can produce, and this branch never once fired.

    Reverting `nparams` to that getattr makes this test fail, which is the
    check that it is testing the fix rather than the weather.
    """
    from graphvis_science.analysis.electro import intelligent_visualization_advisor

    rng = np.random.default_rng(7)
    n = 120
    temperature = rng.uniform(300, 400, n)
    pressure = rng.uniform(1, 10, n)
    frame = pd.DataFrame({
        "temperature": temperature,
        "pressure": pressure,
        "conversion_efficiency": 0.4 * temperature - 2.0 * pressure + rng.normal(0, 1, n),
    })
    engines = [engine for engine, _ in intelligent_visualization_advisor(frame)]
    assert "2D Heatmap" in engines, engines


def test_the_advisor_does_not_invent_a_sweep_that_is_not_there() -> None:
    """The negative case: two measured outputs are not two swept parameters."""
    from graphvis_science.analysis.electro import intelligent_visualization_advisor

    rng = np.random.default_rng(8)
    frame = pd.DataFrame({"signal_a": rng.normal(size=120),
                          "signal_b": rng.normal(size=120)})
    engines = [engine for engine, _ in intelligent_visualization_advisor(frame)]
    assert "2D Heatmap" not in engines, engines
