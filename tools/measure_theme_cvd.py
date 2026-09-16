#!/usr/bin/env python3
"""Measure every interface theme in Theme.qml through a dichromat simulation.

Why this exists
---------------
The 84 colour maps were measured through Machado, Oliveira & Fernandes (2009)
and the result drives what the map chooser offers. The 108 interface themes
were not. Eight of them are tagged for a deficiency, six are high-contrast, and
the other 94 carry no colour-vision information at all - so filtering the theme
picker by deficiency would have to guess about them, and guessing is what this
project measures instead of.

What "safe" means for a THEME is not what it means for a colour map
-------------------------------------------------------------------
A colour map IS the data: two values that simulate to the same colour cannot be
read apart, and that is the whole test.

A theme is chrome. Most of its colours are greys that carry no meaning - a
border is a border whatever hue it is - and their legibility is a CONTRAST
question, which is luminance only and therefore identical for every reader.
Theme.qml already states its contrast ratios and they need no simulation.

Three of a theme's colours carry meaning BY HUE, and a fourth is adjacent:

    positive    it worked                 }  these three MUST separate:
    warning     look at this              }  confusing them misreports
    danger      this will lose something  }  what happened

    accent      what is SELECTED or active - a state, not an outcome

MEASURED BOTH WAYS, because the two answers are very different and only one of
them is a defect.

Against all four colours, 23 to 33 themes fail per deficiency. Against the three
STATUS colours alone, every one of the 108 passes for every dichromacy. Almost
every "failure" in the wider reading is an accent that simulates close to the
warning colour - amber accent, amber warning - which is cosmetically poor and
misreports nothing, because the accent is not claiming an outcome.

Reporting the wider number as the verdict would have hidden thirty perfectly
usable themes from a colour-blind reader to fix a resemblance. So the VERDICT is
the status colours; the wider figure is reported as polish.

This is the colour-map lesson again: measure against the reading task the thing
exists for. One test applied to every colour map reported every diverging map as
unsafe for everyone, because a diverging map is light in the middle by
construction.

The simulation is IMPORTED from measure_colourmap_cvd.py rather than copied. Two
transforms would be two ideas of what a protanope sees, which is the fault this
codebase has now fixed three times in different places.

And the mode nobody asked about
-------------------------------
The paragraph above was written having measured the three DICHROMACIES, and it
is still true of them: all 108 themes carry protanopia, deuteranopia and
tritanopia on the status colours. That reads like a finished job, which is
exactly why nothing was ever done with it.

The program offers five colour-vision modes and one of them is Monochrome. It
was not in MODES. With hue gone, only luminance separates the three status
colours - and most palettes deliberately pick three signals of SIMILAR
lightness, so that none of them shouts. Measured:

    monochrome, status colours:  4 of 108 pass

The 104 failures put positive and warning at 2.3 dE76, which is the
just-noticeable difference - the same colour, for that reader. The four that
pass are all in the Colourblind group, and the two built for achromatopsia
score 13.4 and 17.0 against a floor of 10, which is the design working.

The distribution has a real gap where the floor sits - 2.3 ... 3.4, 7.9, 9.5 |
12.7, 13.4, 17.0, 23.1 - so this verdict is not balanced on the threshold.

Output
------
A table, and a verdict per theme per deficiency. `--write` puts the verdict on
each theme line as a `safe:` field so the picker can read it; `--check` fails
if those tags no longer match what this tool computes.

    python3 tools/measure_theme_cvd.py            # the summary
    python3 tools/measure_theme_cvd.py --verbose  # every theme, every mode
    python3 tools/measure_theme_cvd.py --write    # tag Theme.qml
    python3 tools/measure_theme_cvd.py --check    # fail if the tags are stale
"""
import re
import sys
from pathlib import Path

# The same three-way exit as the colourmap designer, and for the same reason:
# 0 up to date, 1 stale, 2 could not run. The audit used to tell these apart by
# looking for the word "stale" in the output, which made "no numpy" and "no
# verdict" indistinguishable from each other and reported both as a raw
# traceback under a heading that promised a sentence.
CANNOT_RUN = 2
try:
    import numpy as np
except ImportError as exc:                              # pragma: no cover
    print(f"cannot run: {exc}. This says nothing about the themes - "
          f"install numpy to check them.", file=sys.stderr)
    raise SystemExit(CANNOT_RUN)

sys.path.insert(0, str(Path(__file__).resolve().parent))
from measure_colourmap_cvd import to_linear, lab, simulate  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
THEME_QML = ROOT / "app" / "qml" / "Theme.qml"

# The verdict: an outcome reported by hue. Confusing any two of these misreports
# what happened, which is the failure that matters.
STATUS = ["positive", "warning", "danger"]
# Polish: the accent is a STATE - what is selected - so its resembling the
# warning colour is ugly rather than wrong. Measured, reported, not a verdict.
MEANINGFUL = ["accent"] + STATUS

# MONO IS IN THIS LIST, and leaving it out was the bug.
#
# The three dichromacies were measured and every one of the 108 themes passed,
# which reads like a finished job and is why nothing was done with the result.
# But the program offers FIVE colour-vision modes and one of them is
# "Monochrome (no colour)" - `simulate` has always supported it, MODE_NAMES has
# always named it, and MODES did not contain it. So the one mode that
# discriminates was the one never asked about.
#
# It discriminates completely: 4 themes of 108 keep positive, warning and
# danger apart with hue gone, and all four are in the Colourblind group. The
# other 104 put positive and warning at 2.3 dE76 - the just-noticeable
# difference, which is to say the same colour.
#
# That is also the answer for anyone printing in greyscale, which is where a
# lot of this ends up.
MODES = ["protan", "deutan", "tritan", "mono"]
MODE_NAMES = {"protan": "protanopia", "deutan": "deuteranopia",
              "tritan": "tritanopia", "mono": "achromatopsia"}
# The name each mode carries in the theme table and in the picker.
SAFE_KEYS = {"protan": "protanopia", "deutan": "deuteranopia",
             "tritan": "tritanopia", "mono": "achromatopsia"}

# The same floor the colour maps and the series palettes use. Below this, two
# colours a reader has to tell apart are not reliably distinguishable.
FLOOR = 10.0


def parse_themes(text):
    """Every theme record in the generated catalogue."""
    out = []
    for m in re.finditer(r"\{\s*name:\s*\"([^\"]+)\"\s*,\s*group:\s*\"([^\"]+)\""
                         r"[^}]*?cvd:\s*\"([^\"]*)\"[^}]*?\}", text):
        record = {"name": m.group(1), "group": m.group(2), "cvd": m.group(3)}
        body = m.group(0)
        # Kept so --write can edit this exact record in place. Rewriting by
        # name would be a second way of finding the theme, and a theme whose
        # name appears twice in the file would then be tagged in the wrong one.
        record["_body"] = body
        hit = re.search(r'\bsafe:\s*"([^"]*)"', body)
        record["safe"] = [k for k in (hit.group(1).split(",") if hit else []) if k]
        for key in MEANINGFUL:
            hit = re.search(rf"\b{key}:\s*\"(#[0-9a-fA-F]{{6}})\"", body)
            if not hit:
                raise SystemExit(f"{record['name']}: no {key} colour found")
            record[key] = hit.group(1)
        out.append(record)
    return out


def rgb(hex_string):
    h = hex_string.lstrip("#")
    return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)], dtype=float)


def worst_pair(theme, mode, keys=None):
    """The closest two of the given colours, as this reader receives them.
    Returns the distance and which pair it was."""
    keys = keys or MEANINGFUL
    colours = np.stack([rgb(theme[k]) for k in keys])
    seen = lab(simulate(to_linear(colours), mode))
    worst, which = float("inf"), None
    for i in range(len(keys)):
        for j in range(i + 1, len(keys)):
            d = float(np.linalg.norm(seen[i] - seen[j]))
            if d < worst:
                worst, which = d, (keys[i], keys[j])
    return worst, which


def verdict(theme):
    """Which colour-vision modes this theme can carry.

    THE STATUS COLOURS DECIDE, not all four. That distinction is argued at
    length in the docstring and it is the whole reason this returns something
    usable: judged on all four, 23 to 33 themes fail per dichromacy, almost
    every one of them an amber accent sitting near an amber warning - which is
    cosmetically poor and misreports nothing, because an accent marks what is
    selected rather than claiming an outcome. Hiding thirty usable themes from
    a colour-blind reader to fix a resemblance would be the wrong trade.
    """
    return [SAFE_KEYS[m] for m in MODES
            if worst_pair(theme, m, STATUS)[0] >= FLOOR]


def write_tags(themes):
    """Put `safe:` on every theme line, next to the `cvd:` it qualifies.

    ON THE LINE, rather than in a parallel list keyed by index or by name.
    A second array that has to stay in step with this one is the shape that has
    cost this project the most time; a field on the record cannot drift from
    the record.
    """
    src = THEME_QML.read_text(encoding="utf-8")
    for theme in themes:
        body = theme["_body"]
        field = 'safe: "%s", ' % ",".join(verdict(theme))
        if 'safe: "' in body:
            new = re.sub(r'safe: "[^"]*", ', field, body)
        else:
            new = re.sub(r'(cvd: "[^"]*", )', lambda mm: mm.group(1) + field,
                         body, count=1)
        if new == body and 'safe: "' not in body:
            raise SystemExit(
                f"{theme['name']}: could not place the safe field. The theme "
                "record's shape has changed - fix this tool rather than "
                "letting it tag some themes and not others.")
        if src.count(body) != 1:
            raise SystemExit(
                f"{theme['name']}: its record appears {src.count(body)} times "
                "in Theme.qml; refusing to guess which to tag.")
        src = src.replace(body, new, 1)
    THEME_QML.write_text(src, encoding="utf-8")


def check_tags(themes):
    stale = [t["name"] for t in themes if sorted(t["safe"]) != sorted(verdict(t))]
    if stale:
        print(f"Theme.qml colour-vision tags are stale for {len(stale)} theme(s), "
              f"first: {stale[0]}.\nRun tools/measure_theme_cvd.py --write",
              file=sys.stderr)
        return 1
    print(f"theme colour-vision tags up to date: {len(themes)} themes")
    return 0


def main():
    verbose = "--verbose" in sys.argv
    themes = parse_themes(THEME_QML.read_text(encoding="utf-8"))
    if not themes:
        raise SystemExit("no themes parsed - has the Theme.qml record shape changed?")

    if "--check" in sys.argv:
        return check_tags(themes)

    print(f"{len(themes)} themes, four meaning-carrying colours each, "
          f"measured against a floor of {FLOOR:.0f} dE76\n")

    status_failures = {m: [] for m in MODES}
    failures = {m: [] for m in MODES}
    for theme in themes:
        scores = {m: worst_pair(theme, m) for m in ["normal"] + MODES}
        for m in MODES:
            if scores[m][0] < FLOOR:
                failures[m].append((theme, scores[m]))
            s_score = worst_pair(theme, m, STATUS)
            if s_score[0] < FLOOR:
                status_failures[m].append((theme, s_score))
        if verbose:
            line = f"  {theme['name']:<28} {theme['group']:<14}"
            for m in ["normal"] + MODES:
                line += f" {m} {scores[m][0]:5.1f}"
            print(line)

    if verbose:
        print()

    print("=== THE VERDICT: positive / warning / danger ===")
    print("Confusing two of these misreports what happened.\n")
    for m in MODES:
        bad = status_failures[m]
        print(f"{MODE_NAMES[m]}: {len(themes) - len(bad)} of {len(themes)} usable")
        for theme, (score, pair) in sorted(bad, key=lambda r: r[1][0]):
            print(f"    {theme['name']:<28} {score:5.1f}  {pair[0]} / {pair[1]}"
                  + (f"   [tagged {theme['cvd']}]" if theme["cvd"] else ""))
    print()
    print("=== POLISH: the accent as well ===")
    print("An accent resembling the warning colour is ugly, not wrong - the")
    print("accent marks what is selected, it does not claim an outcome.\n")
    for m in MODES:
        bad = failures[m]
        print(f"{MODE_NAMES[m]}: {len(themes) - len(bad)} of {len(themes)} clean")
        for theme, (score, pair) in sorted(bad, key=lambda r: r[1][0]):
            print(f"    {theme['name']:<28} {score:5.1f}  "
                  f"{pair[0]} / {pair[1]}"
                  + (f"   [tagged {theme['cvd']}]" if theme["cvd"] else ""))
        print()

    # The tagged themes must pass the deficiency they are named for. If one does
    # not, the tag is a claim the colours do not support, which is worse than no
    # tag at all - a person picks it BECAUSE of the name.
    broken_promises = []
    for theme in themes:
        if not theme["cvd"]:
            continue
        for m in MODES:
            if MODE_NAMES[m] == theme["cvd"] and worst_pair(theme, m)[0] < FLOOR:
                broken_promises.append((theme["name"], m))
    if broken_promises:
        print("THEMES THAT FAIL THE DEFICIENCY THEY ARE NAMED FOR:")
        for name, m in broken_promises:
            print(f"    {name}  fails {MODE_NAMES[m]}")
        return 1
    print("Every tagged theme passes the deficiency it is named for.")

    # What the picker will do with this.
    print("\n=== WHAT THE PICKER HIDES ===")
    for m in MODES:
        kind = SAFE_KEYS[m]
        carry = [t for t in themes if kind in verdict(t)]
        hidden = len(themes) - len(carry)
        if hidden == 0:
            print(f"  {MODE_NAMES[m]:14s} nothing hidden - all {len(themes)} carry it")
        else:
            print(f"  {MODE_NAMES[m]:14s} {hidden} hidden, {len(carry)} offered: "
                  + ", ".join(t["name"] for t in carry))

    if "--write" in sys.argv:
        write_tags(themes)
        print(f"\nwrote safe tags into {THEME_QML.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
