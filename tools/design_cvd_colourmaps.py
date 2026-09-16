#!/usr/bin/env python3
"""Design CATEGORICAL palettes for a colour-vision deficiency.

Why only categorical
--------------------
This began as a designer for every kind of map and the measurement said no.

`tools/measure_colourmap_cvd.py` scores the eighty-four inherited maps through
each deficiency. Designing a new SEQUENTIAL ramp against that same scorer, by
walking the simulated gamut and picking the real colour nearest each wanted
point, reaches about 13 dE76 for a protanope once the audit's own
lightness-monotonicity rule is applied. Afmhot, inherited, scores 32.1.
Parula scores 30.8. The designer loses, and it loses for a reason worth
writing down: a sequential ramp's separation comes mostly from LIGHTNESS, a
dichromat keeps lightness intact, and maps drawn for normal vision already use
the full lightness range. There is no gap to fill, and shipping a worse map
with a better story would be the worst possible use of this file.

The categorical case is the opposite, and it is not close:

    deficiency   best inherited palette   designed, ten colours
    protanopia   Lines      12.4 dE76     31.0 dE76
    deuteranopia Accent     15.9 dE76     35.1 dE76
    tritanopia   Accent     25.2 dE76     35.3 dE76

The inherited palettes were chosen in normal vision and then simulated to see
what survived; exactly ONE of the thirteen passes for a protanope, so anyone
plotting six groups has a single option and no second opinion. These are
chosen IN the simulated space instead - farthest-point sampling over the
colours the named viewer can actually tell apart - which is why they roughly
treble the separation rather than improving it slightly.

Palettes are designed at several sizes, because separation falls as the count
rises and a person plotting four groups should not be handed the compromise
that ten groups needs.

Monochrome is included and is honest about its limit: with no chroma at all,
the only axis is lightness, so four greys separate well and ten do not.

Output is C++ for native/plot2d/include/ColourMapsCvd.h.

Run:  python3 tools/design_cvd_colourmaps.py [--check] [--report]
"""
from __future__ import annotations

import argparse
import pathlib
import sys

# NUMPY MAY NOT BE HERE, and that is a different answer from "the file is
# wrong".
#
# The audit runs this with --check to ask whether ColourMapsCvd.h still matches
# what the designer produces. On a machine without numpy the import failed, the
# script exited non-zero with a traceback, and the audit reported the traceback
# as the finding "the designed colour-vision palettes are not what the designer
# produces" - a statement about the file, made on the strength of a missing
# package.
#
# The exit code is what carries the difference now, rather than the audit
# reading the output and guessing: 0 up to date, 1 out of date, 2 could not run.
# The script knows which of those happened; nothing downstream should have to
# infer it.
CANNOT_RUN = 2
try:
    import numpy as np
except ImportError as exc:                              # pragma: no cover
    print(f"cannot run: {exc}. This says nothing about the file - "
          f"install numpy to check it.", file=sys.stderr)
    raise SystemExit(CANNOT_RUN)

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

# THE SAME INSTRUMENT, imported rather than rewritten. A designer that scored
# its own output with its own copy of the measurement would be free to agree
# with itself while the audit disagreed.
from measure_colourmap_cvd import (                      # noqa: E402
    MIN_MONOTONIC, MIN_SEPARATION,
    lab, simulate, to_linear,
    measure_sequential, measure_categorical,
)

STOPS = 64
OUT = ROOT / "native" / "plot2d" / "include" / "ColourMapsCvd.h"

# The deficiencies a map can be designed for. "mono" is included because a
# reader who has asked for no colour is still reading a surface, and five
# copies of the same grey ramp is not a choice.
MODES = ("protan", "deutan", "tritan", "mono")

MODE_NAMES = {"protan": "Protanopia", "deutan": "Deuteranopia",
              "tritan": "Tritanopia", "mono": "Monochrome"}

# How many categories each palette holds. Separation falls as the count rises -
# that is arithmetic, not a shortcoming - so the sizes are offered separately
# and a person plotting four groups is not handed the compromise ten needs.
#
# Monochrome stops at six. With no chroma the only axis is lightness, and ten
# greys on one axis are four dE76 apart, which is below the margin
# ColourVision.h calls "a pair a viewer would struggle to tell apart". Offering
# a ten-way grey palette would be offering something that does not work.
SIZES = {"protan": (4, 6, 8, 10, 12),
         "deutan": (4, 6, 8, 10, 12),
         "tritan": (4, 6, 8, 10, 12),
         # Monochrome stops at five, measured rather than chosen: a six-way
         # grey palette comes out at 8.5 dE76, below the 10 the audit calls
         # "a pair a viewer would struggle to tell apart". Offering it would be
         # offering something that does not work.
         "mono": (3, 4, 5)}


def gamut(step: int = 8) -> np.ndarray:
    """A dense grid of sRGB, as 0-255 floats."""
    axis = np.arange(0, 256, step, dtype=float)
    r, g, b = np.meshgrid(axis, axis, axis, indexing="ij")
    return np.stack([r.ravel(), g.ravel(), b.ravel()], axis=-1)


def simulated_lab(rgb: np.ndarray, mode: str) -> np.ndarray:
    return lab(simulate(to_linear(rgb), mode))


# A DESIGNED SEQUENTIAL RAMP WAS TRIED AND REMOVED, not forgotten.
#
# Walking the simulated gamut and picking the real colour nearest each wanted
# point reaches about 13 dE76 for a protanope once the audit's own
# lightness-monotonicity rule is applied. Inherited Afmhot scores 32.1 and
# Parula 30.8. The designer loses by more than two to one, and it loses for a
# reason worth keeping written down: a sequential ramp's separation comes
# mostly from LIGHTNESS, a dichromat keeps lightness intact, and the inherited
# maps already use the full lightness range. There is no gap to fill.
#
# The code is gone rather than kept behind a flag. An unused designer is a
# thing that silently stops matching the measurement it is scored by, and this
# file's whole claim is that its numbers come from the same instrument the
# audit uses.


# A DESIGNED DIVERGING MAP WAS TRIED AND REMOVED, not forgotten. Built out of
# two designed one-sided ramps it reached about 20 dE76 for a protanope, where
# inherited PRGn scores 18.5 and PuOr 18.4 - a dead heat, for a map nobody has
# ever seen and cannot recognise. The same argument as for sequential ramps
# applies and the code is gone rather than kept as an unused alternative: an
# unused designer is a thing that silently stops matching the measurement.


def design_categorical(mode: str, count: int = 10) -> tuple:
    """Colours that stay apart FOR THIS VIEWER, by farthest-point sampling.

    Chosen in the simulated space, not chosen in normal vision and checked
    afterwards. That is the whole difference, and it is why this can offer a
    protanope ten distinguishable categories at 31 dE76 where the inherited
    catalogue offers one palette at 12.

    Seeded from the two most distant colours rather than from the lightest, so
    the result does not depend on which corner of the gamut happens to be
    brightest - a seed that changes the answer is a measurement that changes
    with the weather.
    """
    rgb = gamut(6)
    sim = simulated_lab(rgb, mode)
    # Only colours a figure can actually use: nothing so dark or so pale that
    # it disappears against a background, whichever background it is.
    keep = (sim[:, 0] > 22.0) & (sim[:, 0] < 90.0)
    rgb, sim = rgb[keep], sim[keep]

    # The two ends of the longest diagonal, found on a thinned sample because
    # the exact pair does not matter and the full pairwise distance over
    # seventy thousand colours does not fit in memory.
    coarse = sim[::37]
    coarse_rgb = rgb[::37]
    d = ((coarse[:, None, :] - coarse[None, :, :]) ** 2).sum(-1)
    a, b = np.unravel_index(int(np.argmax(d)), d.shape)
    picked = [coarse_rgb[a], coarse_rgb[b]]
    picked_sim = [coarse[a], coarse[b]]

    while len(picked) < count:
        far = np.min(((sim[:, None, :] - np.array(picked_sim)[None, :, :]) ** 2)
                     .sum(-1), axis=1)
        k = int(np.argmax(far))
        picked.append(rgb[k])
        picked_sim.append(sim[k])
    picked = np.array(picked[:count])

    # Ordered light to dark, so a legend reads in a sensible order and the
    # palette still carries some information to a monochrome reader.
    order = np.argsort([-p[0] for p in np.array(picked_sim[:count])])
    picked = picked[order]

    # Stored as a step function over the 64 stops, which is how every
    # categorical map in the catalogue is stored.
    table = np.empty((STOPS, 3))
    for i in range(STOPS):
        table[i] = picked[min(count - 1, int(i * count / STOPS))]
    score, _ = measure_categorical(
        np.round(simulate(to_linear(table), mode) * 255).astype(int),
        simulated_lab(table, mode))
    return score, table


def cpp_table(name: str, table: np.ndarray) -> str:
    rows = []
    for i in range(0, STOPS, 4):
        chunk = table[i:i + 4]
        rows.append("    " + " ".join(
            "{%3d,%3d,%3d}," % tuple(int(round(v)) for v in c) for c in chunk))
    body = "\n".join(rows).rstrip(",")
    # A LITERAL 64, not kStops. This header is included at the top of
    # ColourMaps.h, before kStops is declared - putting it after the constant
    # instead would nest this file's namespaces inside that one's.
    return (f"static const unsigned char {name}[{STOPS}][3]={{\n{body}\n}};\n")


# Grey ramps that differ in WHERE THEY SPEND THEIR CONTRAST, not in colour.
#
# Monochrome is the one deficiency where the catalogue has nothing to rank:
# Gray, Binary, Gist Gray and Gist Yarg are the same linear ramp forwards and
# backwards, so "five options" is one option shown five times. A tone curve is
# a real difference and a useful one - a surface with a long tail is unreadable
# on a linear grey ramp and fine on one that spends its range at the bottom -
# and it is the only axis a reader with no colour has left.
TONE_CURVES = (
    ("Linear", lambda u: u,
     "Even grey. What every other grey ramp in the catalogue already is."),
    ("Low detail", lambda u: u ** 0.45,
     "Most of the range spent on the dark end, for data with a long bright "
     "tail - the equivalent of a log scale drawn in grey."),
    ("High detail", lambda u: u ** 2.2,
     "Most of the range spent on the bright end, for data that crowds near "
     "the top."),
    ("High contrast", lambda u: np.clip((u - 0.15) / 0.7, 0.0, 1.0),
     "The middle stretched and both ends clipped, for a figure that has to "
     "survive photocopying."),
)


def design_mono_ramps() -> list:
    """The grey ramps, with the separation each one actually achieves."""
    out = []
    for name, curve, _why in TONE_CURVES:
        u = np.linspace(0.0, 1.0, STOPS)
        level = np.clip(curve(u), 0.0, 1.0) * 255.0
        table = np.stack([level, level, level], axis=-1)
        score, mono = measure_sequential(simulated_lab(table, "mono"))
        if mono < MIN_MONOTONIC or score < MIN_SEPARATION:
            continue
        out.append((f"kCvdMonoRamp{name.replace(' ', '')}",
                    f"Monochrome {name}", "sequential", "mono", 0, score, table))
    return out


def build() -> list:
    """Every designed palette, with the score the audit's own scorer gives it."""
    maps = list(design_mono_ramps())
    # (cpp_name, display name, role, mode, count, score, table)
    for mode in MODES:
        pretty = MODE_NAMES[mode]
        for count in SIZES[mode]:
            score, table = design_categorical(mode, count)
            maps.append((f"kCvd{pretty[:6]}{count}",
                         f"{pretty} {count}",
                         "categorical", mode, count, score, table))
    return maps


def header(maps) -> str:
    lines = [
        '#pragma once',
        '',
        '// =========================================================================',
        '// ColourMapsCvd.h - GENERATED by tools/design_cvd_colourmaps.py.',
        '// Do not edit by hand; re-run the script.',
        '//',
        '// Categorical palettes DESIGNED for a colour-vision deficiency, as against',
        '// the inherited ones that were chosen in normal vision and then checked.',
        '//',
        '// Exactly one of the thirteen inherited categorical palettes survives',
        '// protanopia, so anyone plotting six groups has had a single option and no',
        '// second opinion. These are chosen IN the simulated space - farthest-point',
        '// sampling over the colours the named viewer can actually tell apart - and',
        '// roughly treble the separation of the best inherited palette rather than',
        '// improving on it slightly.',
        '//',
        '// Only categorical. A designed SEQUENTIAL ramp was tried and lost to the',
        '// inherited maps by a wide margin, because a sequential ramp\'s separation',
        '// comes mostly from lightness, a dichromat keeps lightness intact, and the',
        '// inherited maps already use the full range. See the script for the numbers.',
        '//',
        '// Scored by the same function tools/measure_colourmap_cvd.py scores the',
        '// inherited palettes with, so these numbers are directly comparable with',
        '// the ones in ColourMapSafety.h.',
        '// =========================================================================',
        '',
        '#include <QVector>',
        '',
        'namespace graphvis {',
        'namespace colourmaps {',
        '',
    ]
    for cpp, name, role, mode, count, score, table in maps:
        lines.append(
            f"// {name}: {count} categories, worst pair {score:.1f} dE76 "
            f"simulated through {mode}." if count else
            f"// {name}: a grey ramp, worst separation {score:.1f} dE76.")
        lines.append(cpp_table(cpp, table))
    lines += [
        '// The designed palettes: display name, table, how many categories it',
        '// holds, the deficiency it was designed for, and the worst pairwise',
        '// separation measured under that deficiency.',
        'struct CvdEntry {',
        '    const char* name;',
        '    const unsigned char (*table)[3];',
        '    int categories;',
        '    const char* mode;',
        '    double separation;',
        '};',
        '',
        'inline const QVector<CvdEntry>& designed(){',
        '    static const QVector<CvdEntry> entries{',
    ]
    rows = [f'        {{"{name}",{cpp},{count},"{mode}",{score:.1f}}}'
            for cpp, name, role, mode, count, score, table in maps]
    lines.append(",\n".join(rows))
    lines += [
        '    };',
        '    return entries;',
        '}',
        '',
        '} // namespace colourmaps',
        '} // namespace graphvis',
    ]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--report", action="store_true",
                        help="print each designed map's score and stop")
    args = parser.parse_args()

    maps = build()
    if args.report:
        for _, name, role, mode, count, score, _ in maps:
            what = f"{count:3} categories" if count else "    ramp      "
            print(f"{name:28} {what}  {mode:7} {score:6.1f} dE76")
        return 0
    text = header(maps)
    if args.check:
        if not OUT.exists() or OUT.read_text(encoding="utf-8") != text:
            print(f"{OUT} is out of date with the designer")
            return 1
        print(f"{OUT} is up to date ({len(maps)} designed maps)")
        return 0
    OUT.write_text(text, encoding="utf-8")
    print(f"wrote {OUT} ({len(maps)} designed maps)")
    for _, name, role, mode, count, score, _ in maps:
        what = f"{count:3} categories" if count else "    ramp      "
        print(f"  {name:28} {what}  {mode:7} {score:6.1f} dE76")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
