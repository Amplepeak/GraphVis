"""The four components added on top of the analysis operations.

Units, uncertainty propagation, symbolic maths and citations. Everything that
touches the network is tested offline: `find_dois` and the two formatters take
records, not connections, so the parts that can be wrong without an internet
connection are the parts under test.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import numpy as np

from graphvis_science import citations, uncertainty, units

passed = 0
failed = 0
skipped = 0


def ok(label, condition, detail=""):
    global passed, failed
    if condition:
        passed += 1
        print(f"  ok   {label:<46} {detail}")
    else:
        failed += 1
        print(f"  FAIL {label:<46} {detail}")


def raises(label, fn, exc_type, fragment=""):
    global passed, failed
    try:
        fn()
    except exc_type as exc:
        if fragment and fragment.lower() not in str(exc).lower():
            failed += 1
            print(f"  FAIL {label:<46} wrong message: {str(exc)[:40]}")
        else:
            passed += 1
            print(f"  ok   {label:<46} {str(exc)[:44]}")
        return
    except Exception as exc:  # noqa: BLE001
        failed += 1
        print(f"  FAIL {label:<46} raised {type(exc).__name__}")
        return
    failed += 1
    print(f"  FAIL {label:<46} did not raise")


# ----------------------------------------------------------------------- units
print("\n=== units: reading what an instrument wrote ===")
try:
    units.parse("kPa")
    HAVE_PINT = True
except units.UnitError as exc:
    HAVE_PINT = "component" not in str(exc)

if not HAVE_PINT:
    skipped += 1
    print("  skip units                                     Units component absent")
else:
    ok("a unit from a label", units.unit_of("Pressure [kPa]") == "kPa",
       units.unit_of("Pressure [kPa]"))
    ok("a unit in parentheses", units.unit_of("Temperature (degC)") == "degC")
    ok("no unit is not an error", units.unit_of("plain column") == "")

    ok("kPa is a pressure",
       units.parse("kPa")["dimensionality"] == "[mass] / [length] / [time] ** 2",
       units.parse("kPa")["dimensionality"])
    ok("mA/cm2 is a compound unit",
       "[current]" in units.parse("mA/cm2")["dimensionality"],
       units.parse("mA/cm2")["dimensionality"])
    ok("a micro sign is understood",
       units.parse("µV")["dimensionality"] == units.parse("uV")["dimensionality"])
    ok("degC is not a bare 'c'", units.parse("degC")["unit"] == "degree_Celsius",
       units.parse("degC")["unit"])
    ok("mg/L/h reads as a rate", "[time]" in units.parse("mg/L/h")["dimensionality"],
       units.parse("mg/L/h")["dimensionality"])

    converted = units.convert([101.325, 202.65], "kPa", "Pa")
    ok("kPa converts to Pa", np.allclose(converted["values"], [101325.0, 202650.0]),
       f"{converted['values'][0]:.0f}")
    celsius = units.convert([0.0, 100.0], "degC", "K")
    ok("degC converts to K with its offset",
       np.allclose(celsius["values"], [273.15, 373.15]), str(celsius["values"]))

    ok("pressure and pressure are compatible", units.compatible("kPa", "bar")["compatible"])
    ok("pressure and time are not", not units.compatible("kPa", "s")["compatible"],
       units.compatible("kPa", "s")["target_dimensionality"])
    raises("converting across dimensions is refused",
           lambda: units.convert([1.0], "m", "s"), units.UnitError, "cannot convert")
    raises("a unit that is not one", lambda: units.parse("bananas"),
           units.UnitError, "not a unit")

    targets = units.conversions_for("kPa")["targets"]
    ok("a pressure offers other pressures", any("bar" in t for t in targets),
       f"{len(targets)} targets")

    report = units.audit(["Pressure [kPa]", "Temperature (degC)", "plain",
                          "Nonsense [bananas]"])
    ok("the audit finds the readable units", len(report["with_units"]) == 2,
       str(len(report["with_units"])))
    ok("and the bare column", report["without_units"] == ["plain"])
    ok("and names the unreadable one",
       report["unreadable"] and report["unreadable"][0]["unit"] == "bananas")

# ----------------------------------------------------------------- uncertainty
print("\n=== uncertainty: carrying the error through ===")
try:
    uncertainty.propagate("a + b", {"a": {"value": 1, "error": 0.1},
                                    "b": {"value": 2, "error": 0.2}})
    HAVE_UNC = True
except uncertainty.UncertaintyError as exc:
    HAVE_UNC = "component" not in str(exc)

if not HAVE_UNC:
    skipped += 1
    print("  skip uncertainty                               Uncertainty component absent")
else:
    # Independent errors add in quadrature: sqrt(0.1^2 + 0.2^2) = 0.2236
    r = uncertainty.propagate("a + b", {"a": {"value": 1.0, "error": 0.1},
                                        "b": {"value": 2.0, "error": 0.2}})
    ok("a sum adds its errors in quadrature",
       abs(r["error"] - (0.1 ** 2 + 0.2 ** 2) ** 0.5) < 1e-9, f"{r['error']:.4f}")
    ok("and the value is just the sum", abs(r["value"] - 3.0) < 1e-12)

    # A product's RELATIVE errors add in quadrature.
    r = uncertainty.propagate("a * b", {"a": {"value": 4.0, "error": 0.4},
                                        "b": {"value": 5.0, "error": 0.5}})
    expected = 20.0 * ((0.1 ** 2) + (0.1 ** 2)) ** 0.5
    ok("a product adds relative errors in quadrature",
       abs(r["error"] - expected) < 1e-9, f"{r['error']:.4f} vs {expected:.4f}")

    # The same variable twice is correlated with itself: a - a is exactly zero
    # with no error, which is the whole reason to use this rather than a
    # hand-rolled quadrature sum.
    r = uncertainty.propagate("a - a", {"a": {"value": 7.0, "error": 2.0}})
    ok("correlations are kept (a - a has no error)", r["error"] == 0.0, f"{r['error']}")

    r = uncertainty.propagate("H * exp(-k * t)",
                              {"H": {"value": 284, "error": 12},
                               "k": {"value": 0.08, "error": 0.006},
                               "t": {"value": 10, "error": 0.2}})
    ok("a non-linear model propagates", r["error"] > 0, r["formatted"])
    shares = {c["name"]: c["share"] for c in r["contributions"]}
    ok("the contributions sum to one", abs(sum(shares.values()) - 1.0) < 1e-6,
       f"{sum(shares.values()):.4f}")
    ok("and name the dominant input", max(shares, key=shares.get) == "k",
       f"k={shares['k']:.2f}")
    ok("an interval is reported",
       r["interval_95"][0] < r["value"] < r["interval_95"][1])

    fit = uncertainty.from_fit({"P": {"value": 284, "stderr": 12},
                                "Rm": {"value": 24, "stderr": 1.5}}, "P / Rm")
    ok("a fit's stderr is understood", fit["error"] > 0, fit["formatted"])
    raises("an empty input set is refused",
           lambda: uncertainty.propagate("a", {}), uncertainty.UncertaintyError,
           "at least one")
    raises("an unsafe expression is refused",
           lambda: uncertainty.propagate("__import__('os').system('x')",
                                         {"a": {"value": 1, "error": 0}}),
           Exception, "")

# ------------------------------------------------------------------- citations
print("\n=== citations: finding and formatting, without a network ===")
text = ("As reported by Chen et al. (doi:10.1016/j.watres.2019.05.001) and "
        "again in 10.1038/s41586-020-2649-2. See also "
        "https://doi.org/10.1016/j.watres.2019.05.001 for the same work.")
found = citations.find_dois(text)
ok("every DOI is found", len(found) == 2, str(found))
ok("a repeat is not counted twice", len(set(d.lower() for d in found)) == 2)
ok("a trailing full stop is not part of the DOI",
   all(not d.endswith(".") for d in found), str(found))
ok("nothing in plain prose", citations.find_dois("no identifiers here") == [])

record = {
    "doi": "10.1016/j.watres.2019.05.001",
    "title": "Hydrogen production in a microbial electrolysis cell",
    "authors": [{"family": "Bach", "given": "Wei"},
                {"family": "Okoye", "given": "Ada"}],
    "year": 2019, "journal": "Water Research", "volume": "158",
    "issue": "3", "pages": "112-121", "publisher": "Elsevier",
    "url": "https://doi.org/10.1016/j.watres.2019.05.001",
}
bib = citations.to_bibtex(record)
ok("BibTeX has a citation key", bib.startswith("@article{chen2019,"), bib[:24])
ok("BibTeX braces balance", bib.count("{") == bib.count("}"),
   f"{bib.count('{')} vs {bib.count('}')}")
ok("BibTeX keeps the title's capitals", "{Hydrogen production" in bib)
ok("BibTeX carries the DOI", "10.1016/j.watres.2019.05.001" in bib)

apa = citations.to_text(record)
ok("a prose citation names the authors", "Chen, W." in apa, apa[:38])
ok("and the year", "(2019)" in apa)
ok("and resolves to a link", "https://doi.org/" in apa)
ok("bibtex style returns BibTeX",
   citations.to_text(record, "bibtex").startswith("@article"))

sparse = citations.to_text({"doi": "10.1/x", "title": "Untitled", "authors": []})
ok("a record with no authors still formats", "Anon." in sparse, sparse[:30])

# The author rule is now per style, because to_text formats per style instead
# of ignoring the argument. APA 7 lists up to twenty authors and only then
# elides, which is the actual rule; the old assertion here ("et al." past three)
# was testing the single generic formatter that every style used to share.
many = {"doi": "10.1/x", "title": "T", "authors":
        [{"family": f"N{i}", "given": "A"} for i in range(6)]}
ok("APA lists six authors rather than eliding them",
   "et al." not in citations.to_text(many, "apa"))
ok("IEEE elides past six", "et al." in citations.to_text(
   {"doi": "10.1/x", "title": "T",
    "authors": [{"family": f"N{i}", "given": "A"} for i in range(8)]}, "ieee"))
ok("Nature elides past five", "et al." in citations.to_text(many, "nature"))
ok("Harvard elides past three", "et al." in citations.to_text(many, "harvard"))
huge = {"doi": "10.1/x", "title": "T", "authors":
        [{"family": f"N{i}", "given": "A"} for i in range(25)]}
# APA 7's rule for twenty-one or more authors is NOT "et al.": it lists the
# first nineteen, then a spaced ellipsis, then the FINAL author - ". . . Zhu, X."
# A formatter that wrote "et al." there would be wrong in the one place a
# reader checks a long reference, and the spacing is part of the rule.
_apa_huge = citations.to_text(huge, "apa")
ok("APA elides past twenty with a spaced ellipsis, not et al.",
   ". . ." in _apa_huge and "et al." not in _apa_huge, _apa_huge[-60:])
ok("APA keeps the LAST author after the ellipsis",
   _apa_huge.rstrip().split(". . . ")[-1].startswith("N24"), _apa_huge[-40:])
ok("the whole thing is JSON-safe", json.dumps(record) is not None)

print(f"\n{passed} passed, {failed} failed, {skipped} skipped")
def test_suite() -> None:
    """Report this file's result to pytest instead of aborting collection.

    These suites predate pytest: the body above runs its checks at import and
    then called sys.exit, which made `pytest tests/` abort the whole directory
    with an INTERNALERROR - so the six oldest and largest suites could only ever
    be run one file at a time, and CI had to special-case them.

    Running the file directly still behaves exactly as before.
    """
    assert failed == 0, (
        f"{failed} checks failed - run `python tests/test_extras.py` for the detail")


if __name__ == "__main__":
    sys.exit(1 if failed else 0)
