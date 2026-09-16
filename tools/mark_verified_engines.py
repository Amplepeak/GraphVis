#!/usr/bin/env python3
"""Stamp each catalogue entry with whether its engine has been VERIFIED.

Why this exists
---------------
Five engines were added to the catalogue in one afternoon and the only thing
standing between them and a user was the sweep, which proves an engine draws
and draws a picture no other engine draws. That is not verification. A bump
chart that ranked backwards, a dial whose sweep ignored its scale and a
Marimekko whose columns were all the same width would each have passed it
comfortably — and the first drafts of two of those three were wrong in exactly
that way.

So an engine is offered as known-good only when something MEASURES it, and this
computes that rather than trusting a flag somebody typed. A hand-edited
`verified` in the catalogue is caught by the guard in
services/python/tests/test_reachable.py, which runs this and compares.

What counts as verified
-----------------------
1. It has a measured regression check in PlotSelfTest.cpp — data whose answer
   is arithmetic, asserted against what the engine produces or draws; or
2. it is in the audited baseline: the engines that went through the 22-pass
   audit — every engine drawing, no two alike, 434 figures judged by eye, by
   measurement and by what they report.

Anything else is `verified: false`, and the graph library draws it in red and
says so, rather than offering it as though it were known to be right.

    python3 tools/mark_verified_engines.py            # write the catalogue
    python3 tools/mark_verified_engines.py --check    # report, change nothing
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOGUE = ROOT / "config" / "graph_catalogue.json"
SELFTEST = ROOT / "native" / "plot2d" / "src" / "PlotSelfTest.cpp"
BASELINE = ROOT / "config" / "audited_engines.json"

# The block of PlotSelfTest.cpp that measures newly added engines. Bounded, so
# an engine name mentioned anywhere else in that very large file — a fixture, a
# comment, a sweep entry — cannot be mistaken for a check.
BLOCK_START = "11. NEW ENGINES"
BLOCK_END = "if(!failures.isEmpty()){"


def measured_engines() -> set[str]:
    """Engines with a measured check, read out of the check block itself."""
    text = SELFTEST.read_text(encoding="utf-8")
    try:
        start = text.index(BLOCK_START)
        end = text.index(BLOCK_END, start)
    except ValueError:
        raise SystemExit(
            f"{SELFTEST.name}: cannot find the verification block "
            f"({BLOCK_START!r} .. {BLOCK_END!r}). If it was renamed, rename it here too — "
            "a guard that cannot find what it guards passes vacuously."
        )
    block = text[start:end]
    # An engine is measured when the block BUILDS a spec for it. Mentioning its
    # name in a failure message is not a check.
    names = set(re.findall(r'whiteSpec\(QStringLiteral\("([^"]+)"\)\)', block))
    if not names:
        raise SystemExit(
            f"{SELFTEST.name}: the verification block builds no specs, so nothing is "
            "measured. Either it is empty or the way checks are written has changed."
        )
    return names


def audited_engines() -> set[str]:
    if not BASELINE.exists():
        raise SystemExit(
            f"{BASELINE} is missing. It records the engines that went through the "
            "22-pass audit; without it every engine would be reported unverified."
        )
    return set(json.loads(BASELINE.read_text(encoding="utf-8"))["engines"])


def verdict_for(engines, measured, audited) -> dict[str, bool]:
    """The rule itself, separated from the files so it can be tested.

    An engine is verified when something measures it, or when it is in the
    audited baseline. Nothing else counts - not being in the catalogue, not
    having a fixture, not drawing successfully.
    """
    ok = set(measured) | set(audited)
    return {name: (name in ok) for name in sorted(engines)}


def verdicts() -> dict[str, bool]:
    catalogue = json.loads(CATALOGUE.read_text(encoding="utf-8"))
    engines = {e["engine"] for c in catalogue["categories"] for e in c["entries"]}
    return verdict_for(engines, measured_engines(), audited_engines())


def main() -> int:
    check_only = "--check" in sys.argv
    want = verdicts()
    catalogue = json.loads(CATALOGUE.read_text(encoding="utf-8"))

    wrong = []
    for category in catalogue["categories"]:
        for entry in category["entries"]:
            expected = want[entry["engine"]]
            if entry.get("verified") != expected:
                wrong.append((entry["name"], entry["engine"], entry.get("verified"), expected))
            entry["verified"] = expected

    unverified = sorted(n for n, ok in want.items() if not ok)
    print(f"{len(want)} engines; {len(unverified)} unverified")
    for name in unverified:
        print(f"    {name}")

    if check_only:
        if wrong:
            print(f"\n{len(wrong)} catalogue entr(ies) carry the wrong verified flag:")
            for name, engine, was, expected in wrong[:20]:
                print(f"    {name} [{engine}]: {was!r} should be {expected!r}")
            return 1
        print("every catalogue entry's verified flag matches the evidence")
        return 0

    CATALOGUE.write_text(
        json.dumps(catalogue, indent=1, ensure_ascii=False), encoding="utf-8")
    print(f"wrote {CATALOGUE.name} ({len(wrong)} entr(ies) changed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
