#!/usr/bin/env python3
"""Measure every colour map in ColourMaps.h through a dichromat simulation.

Why this exists
---------------
ColourVision.h says of the SERIES palettes: "these are generated and checked
rather than chosen by eye".  The continuous colour maps had no such check at
all, so choosing a colour-vision mode did nothing to a surface, a heat map, a
contour or a vector field - the very plots where the colour IS the data.  This
script supplies the missing measurement.

What "safe" means depends on what the map is FOR
------------------------------------------------
The first version of this script applied one test - monotone lightness plus
far-pair separation - to all eighty four maps, and reported every diverging map
and every cyclic map as unsafe for everyone, normal vision included.  That was
the instrument failing, not the maps: a diverging map is light in the middle by
construction, so its lightness is V-shaped and can never be monotone, and a
cyclic map joins its two ends on purpose, so its most distant pair is identical
by design.  RdBu is a standard colour-blind-safe diverging map and Twilight is a
standard cyclic one.

So each map is measured against the reading task its category exists for:

  sequential   ORDER two patches.  Needs monotone simulated L* and, for every
               pair of stops at least a quarter of the range apart, a simulated
               CIE76 difference above the margin.

  diverging    read WHICH SIDE of the centre and HOW FAR.  Each half is measured
               as its own sequential ramp, and additionally every mirrored pair
               either side of the centre must separate - that last test is the
               one RdYlGn fails, because its red and green ends simulate to the
               same colour for a protanope.

  cyclic       read an ANGLE.  Far-pair separation measured around the circle so
               the deliberate wrap is not counted as a failure; lightness
               monotonicity does not apply at all.

  categorical  tell CATEGORIES apart.  The worst pair over the distinct colours,
               which is the test ColourVision.h already applies to the series
               palettes.

  utility      not classifiable.  White is one colour by design; Flag and Prism
               repeat.  Reported unsafe for continuous data under every vision,
               which is simply true.

The simulation is Machado, Oliveira & Fernandes (2009) at severity 1.0, applied
in linear sRGB - the same family of transforms used for the series palettes.

Output is C++ for native/plot2d/include/ColourMapSafety.h.
"""
import re
import sys
import numpy as np

HEADER = "native/plot2d/include/ColourMaps.h"
# The designed palettes live in their own generated header and are registered
# in ColourMaps.h's list. Both are read, because a map that is offered and not
# measured is a map with no safety verdict at all.
DESIGNED_HEADER = "native/plot2d/include/ColourMapsCvd.h"

# Machado, Oliveira & Fernandes (2009), severity 1.0, linear sRGB.
CVD = {
    "protan": np.array([[0.152286, 1.052583, -0.204868],
                        [0.114503, 0.786281, 0.099216],
                        [-0.003882, -0.048116, 1.051998]]),
    "deutan": np.array([[0.367322, 0.860646, -0.227968],
                        [0.280085, 0.672501, 0.047413],
                        [-0.011820, 0.042940, 0.968881]]),
    "tritan": np.array([[1.255528, -0.076749, -0.178779],
                        [-0.078411, 0.930809, 0.147602],
                        [0.004733, 0.691367, 0.303900]]),
}

M_RGB2XYZ = np.array([[0.4124564, 0.3575761, 0.1804375],
                      [0.2126729, 0.7151522, 0.0721750],
                      [0.0193339, 0.1191920, 0.9503041]])
WHITE = np.array([0.95047, 1.00000, 1.08883])


def to_linear(srgb):
    c = srgb / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def lab(linear):
    xyz = linear @ M_RGB2XYZ.T / WHITE
    f = np.where(xyz > 216.0 / 24389.0, np.cbrt(xyz),
                 (24389.0 / 27.0 * xyz + 16.0) / 116.0)
    return np.stack([116.0 * f[..., 1] - 16.0,
                     500.0 * (f[..., 0] - f[..., 1]),
                     200.0 * (f[..., 1] - f[..., 2])], axis=-1)


def simulate(linear, mode):
    if mode == "normal":
        return linear
    if mode == "mono":
        # Rec.709 luma, the greyscale a monochrome reader is left with.
        y = linear @ np.array([0.2126, 0.7152, 0.0722])
        return np.stack([y, y, y], axis=-1)
    return np.clip(linear @ CVD[mode].T, 0.0, 1.0)


def parse_tables(text):
    """Pull every kName[kStops][3] table out of the generated header."""
    tables = {}
    # Either spelling of the stop count. The inherited header writes
    # `[kStops][3]`; the designed one is included before that constant is
    # declared and writes `[64][3]`.
    for m in re.finditer(
            r"static const unsigned char (k\w+)\[(?:kStops|64)\]\[3\]=\{(.*?)\};",
            text, re.S):
        nums = [int(n) for n in re.findall(r"\d+", m.group(2))]
        if len(nums) != 64 * 3:
            raise SystemExit(f"{m.group(1)}: {len(nums)} numbers, expected 192")
        tables[m.group(1)] = np.array(nums, dtype=float).reshape(64, 3)
    return tables


def parse_registry(text):
    """The {"Display Name",kSymbol} list, in offer order."""
    body = text.split("inline const QVector<Entry>& all()")[1]
    body = body.split("return entries;")[0]
    return re.findall(r'\{"([^"]+)",(k\w+)\}', body)


def parse_categories(text):
    body = text.split("inline const QVector<QPair<QString,QStringList>>& categories()")[1]
    body = body.split("return cats;")[0]
    cats = {}
    for m in re.finditer(r'\{QStringLiteral\("([^"]+)"\),\{(.*?)\}\}', body, re.S):
        cats[m.group(1)] = re.findall(r'QStringLiteral\("([^"]+)"\)', m.group(2))
    return cats


# A quarter of the range apart: the coarsest reading anyone does off a ramp is
# "this patch against that one", and patches closer than this are a judgement
# no ramp is expected to support.
FAR = 0.25
# dE76.  Below 10 is the margin ColourVision.h already calls "a pair a viewer
# would struggle to tell apart"; the same number is used here so the two
# measurements are on one scale.
MIN_SEPARATION = 10.0
# A ramp may wobble, but if a fifth of its steps run backwards in lightness the
# reader can no longer order by lightness where hue has collapsed.
MIN_MONOTONIC = 0.80


def de76(L):
    return np.sqrt(((L[:, None, :] - L[None, :, :]) ** 2).sum(-1))


def monotonicity(L):
    steps = np.diff(L[:, 0])
    up = float((steps > 0).sum())
    down = float((steps < 0).sum())
    total = up + down
    return (max(up, down) / total) if total else 1.0


def measure_sequential(L, far=FAR):
    u = np.linspace(0.0, 1.0, len(L))
    mask = np.abs(u[:, None] - u[None, :]) >= far
    if not mask.any():
        return 0.0, 0.0
    return float(de76(L)[mask].min()), monotonicity(L)


def measure_diverging(L):
    half = len(L) // 2
    lo, mo_lo = measure_sequential(L[:half])
    hi, mo_hi = measure_sequential(L[half:])
    # Mirrored pairs: the same distance either side of the centre must not
    # simulate to the same colour, or the sign of the deviation is lost.
    n = min(half, len(L) - half)
    off = np.arange(n)
    keep = off >= int(0.15 * len(L))
    a = L[half - 1 - off][keep]
    b = L[half + off][keep]
    across = float(np.sqrt(((a - b) ** 2).sum(-1)).min()) if keep.any() else 0.0
    return min(lo, hi, across), min(mo_lo, mo_hi)


def measure_cyclic(L):
    u = np.linspace(0.0, 1.0, len(L))
    d = np.abs(u[:, None] - u[None, :])
    circular = np.minimum(d, 1.0 - d)
    mask = circular >= FAR
    if not mask.any():
        return 0.0, 1.0
    # Monotonicity is meaningless on a cycle, so it is reported as passing and
    # the separation alone decides.
    return float(de76(L)[mask].min()), 1.0


def measure_categorical(rgb, L):
    # The stored table is a step function; collapse it back to the categories.
    _, first = np.unique(rgb, axis=0, return_index=True)
    idx = np.sort(first)
    if len(idx) < 2:
        return 0.0, 1.0
    C = L[idx]
    d = de76(C)
    np.fill_diagonal(d, np.inf)
    return float(d.min()), 1.0


ROLE = {
    "Perceptually Uniform": "sequential",
    "Scientific Sequential": "sequential",
    "Diverging": "diverging",
    "Cyclic & Angular": "cyclic",
    "Seasonal & Shading": "sequential",
    "Terrain, Ocean & Field": "sequential",
    "Categorical": "categorical",
    "Specialized & Utility": "utility",
    # The designed palettes are a mixed category by construction - grey ramps
    # and categorical palettes in one list - so the role comes from the name
    # rather than from the group. See ROLE_OVERRIDE below.
    "Colour-blind (designed)": "categorical",
}

# Cubehelix sits in the cyclic group by name but is a monotone-lightness ramp -
# that is the whole point of it - so it is measured as what it is.
ROLE_OVERRIDE = {"Cubehelix": "sequential"}
# The designed grey ramps are ramps; everything else in that category is a
# palette. Named by prefix because the designer names them that way and the
# audit fails if it stops doing so.
ROLE_OVERRIDE.update({name: "sequential" for name in
                      ("Monochrome Linear", "Monochrome Low detail",
                       "Monochrome High detail", "Monochrome High contrast")})


def measure(table, mode, role):
    linear = to_linear(table)
    sim = simulate(linear, mode)
    L = lab(sim)
    if role == "sequential":
        return measure_sequential(L)
    if role == "diverging":
        return measure_diverging(L)
    if role == "cyclic":
        return measure_cyclic(L)
    if role == "categorical":
        return measure_categorical(np.round(sim * 255).astype(int), L)
    return 0.0, 0.0


def parse_designed(text):
    """(display name, symbol) for every designed palette, in offer order.

    Read from ColourMapsCvd.h rather than from ColourMaps.h's registry: the
    designed maps are appended there by a LOOP, not written as literals, so
    the registry parser cannot see them. A map that is offered by the program
    and not measured here would have no safety verdict at all - it would be
    hidden from every colour-vision mode, which for palettes designed for
    those very modes would be the whole feature failing silently.
    """
    body = text.split("inline const QVector<CvdEntry>& designed()")[1]
    body = body.split("return entries;")[0]
    return re.findall(r'\{"([^"]+)",(k\w+),', body)


def main():
    text = open(HEADER, encoding="utf-8").read()
    designed_text = open(DESIGNED_HEADER, encoding="utf-8").read()
    tables = parse_tables(text)
    tables.update(parse_tables(designed_text))
    registry = parse_registry(text)
    designed_registry = parse_designed(designed_text)
    registry = registry + [row for row in designed_registry
                           if row not in registry]
    cats = parse_categories(text)
    cat_of = {n: c for c, names in cats.items() for n in names}
    for name, _sym in designed_registry:
        cat_of.setdefault(name, "Colour-blind (designed)")

    modes = ["protan", "deutan", "tritan", "mono"]
    rows = []
    for name, sym in registry:
        t = tables[sym]
        cat = cat_of.get(name, "?")
        role = ROLE_OVERRIDE.get(name, ROLE.get(cat, "utility"))
        rec = {"name": name, "cat": cat, "role": role}
        for m in ["normal"] + modes:
            rec[m] = measure(t, m, role)
        rows.append(rec)

    if "--report" in sys.argv:
        hdr = f"{'map':<18}{'role':<13}" + "".join(f"{m:>16}" for m in ["normal"] + modes)
        print(hdr)
        print("-" * len(hdr))
        for r in rows:
            line = f"{r['name']:<18}{r['role']:<13}"
            for m in ["normal"] + modes:
                w, mo = r[m]
                flag = " " if (w >= MIN_SEPARATION and mo >= MIN_MONOTONIC) else "x"
                line += f"{w:7.1f}/{mo:4.2f}{flag:>4}"
            print(line)
        return

    emit(rows, modes, tables, registry)


ROLE_ENUM = {"sequential": "Sequential", "diverging": "Diverging",
             "cyclic": "Cyclic", "categorical": "Categorical",
             "utility": "Utility"}


def safe(r, m):
    w, mo = r[m]
    return w >= MIN_SEPARATION and mo >= MIN_MONOTONIC


# How many maps the "Best for this reader" group offers per role. Six is
# enough to cover the preferred tier - the maps designed for the job - plus a
# little of what the measurement turns up behind them, and short enough to read
# without scrolling.
RECOMMEND_PER_ROLE = 6


def choose_substitutes(rows, modes, tables, registry):
    """Pick the map to offer instead of an unsafe one, per role and vision.

    Highest score is the wrong rule on its own.  Ranked by separation alone the
    substitute for a sequential ramp came out as Afmhot - a black-red-yellow
    heat ramp that scores well because it is violently contrasty - ahead of
    Cividis, which exists for precisely this purpose.  So a preferred tier is
    consulted first and the score only orders within it.

    Monochrome is not a safety substitution at all, it is a rendering request:
    someone who asks for no colour must GET no colour, whatever the measurement
    says about a colourful map surviving a greyscale print.  So it is answered
    from the maps that are achromatic by construction.
    """
    achromatic = set()
    for name, sym in registry:
        t = tables[sym]
        if np.all(t[:, 0] == t[:, 1]) and np.all(t[:, 1] == t[:, 2]):
            achromatic.add(name)

    # Consulted before the score.  These are the maps designed for the job.
    #
    # A PALETTE DESIGNED FOR THIS DEFICIENCY OUTRANKS ANY INHERITED ONE, and
    # only in the categorical role, because that is the only role where the
    # measurement says it wins: the designed palettes reach 31-71 dE76 for a
    # protanope against the best inherited palette's 12.4, while a designed
    # sequential ramp loses to Afmhot by more than two to one. Preferring them
    # everywhere would be preferring the story over the numbers.
    PREFERRED = {
        "sequential": ["Cividis", "Viridis", "Magma", "Inferno", "Plasma"],
        "diverging": ["PuOr", "RdBu", "RdYlBu", "PRGn", "BrBG"],
        "cyclic": ["Twilight", "Twilight Shifted", "Phase"],
        "categorical": ["Accent", "Dark2", "Set2", "Tab10"],
    }
    DESIGNED_FOR = {"protan": "Protanopia", "deutan": "Deuteranopia",
                    "tritan": "Tritanopia", "mono": "Monochrome"}

    best = {}
    recommended = {}
    for role in ["sequential", "diverging", "cyclic", "categorical"]:
        for m in modes:
            pool = [r for r in rows if r["role"] == role and safe(r, m)
                    and safe(r, "normal")]
            if m == "mono":
                # Achromatic first; only if none exists does the measurement
                # get a say, and then the caller is told what was lost.
                grey = [r for r in rows if r["name"] in achromatic
                        and safe(r, "mono")]
                if role == "sequential" or not pool:
                    pool = grey or pool
                else:
                    pool = grey
            if not pool:
                continue
            pref = PREFERRED.get(role, [])
            designed_prefix = DESIGNED_FOR.get(m, "")

            def rank(r, pref=pref, m=m, role=role, prefix=designed_prefix):
                # Tier -1: designed for this very deficiency, categorical only.
                if (role == "categorical" and prefix
                        and r["name"].startswith(prefix)):
                    return (-1, -r[m][0])
                tier = pref.index(r["name"]) if r["name"] in pref else len(pref)
                return (tier, -r[m][0])

            best[(role, m)] = min(pool, key=rank)["name"]
            # The same ordering, kept whole rather than reduced to its first
            # element. One substitute per role is enough to make an unreadable
            # figure readable; it is not enough to answer "which should I
            # pick", and it cannot offer a reader something RESEMBLING the map
            # they chose. The interface takes the nearest entry of this list to
            # the chosen map, so a person who picked a blue-white-red diverging
            # map gets a blue-white-red one back.
            recommended[(role, m)] = [r["name"] for r in sorted(pool, key=rank)][:RECOMMEND_PER_ROLE]
    return best, achromatic, recommended


def emit(rows, modes, tables, registry):
    # The substitute keeps the SAME role: swapping a diverging map for a
    # sequential one would throw away the centre the diverging map exists to
    # show, which is a change to what the figure claims, not to how it looks.
    best, achromatic_names, recommended = choose_substitutes(rows, modes, tables, registry)

    out = []
    w = out.append
    w("#pragma once")
    w("#include <QString>")
    w('#include "ColourMaps.h"')
    w('#include "ColourVision.h"')
    w("")
    w("// =========================================================================")
    w("// ColourMapSafety.h - GENERATED by tools/measure_colourmap_cvd.py.")
    w("// Do not edit by hand; re-run the script.")
    w("//")
    w("// Choosing a colour-vision mode used to change the SERIES palette and")
    w("// nothing else. On a surface, a heat map, a contour or a vector field there")
    w("// are no series - the colour map IS the data - so on exactly the plots where")
    w("// the setting matters most it appeared to do nothing at all. This table is")
    w("// what lets the setting reach them.")
    w("//")
    w("// Every map was simulated through Machado, Oliveira & Fernandes (2009) at")
    w("// severity 1.0 and measured against the reading task its category exists")
    w("// for - ordering for a sequential ramp, side-and-distance for a diverging")
    w("// one, angle for a cyclic one, telling apart for a categorical one. Pass is")
    w(f"// a worst separation of {MIN_SEPARATION:.0f} dE76 with at least")
    w(f"// {MIN_MONOTONIC:.2f} of steps running the same way in lightness, the same")
    w("// margin ColourVision.h uses for the series palettes.")
    w("// =========================================================================")
    w("")
    w("namespace graphvis {")
    w("namespace colourmaps {")
    w("")
    w("enum class MapRole { Sequential, Diverging, Cyclic, Categorical, Utility };")
    w("")
    w("struct SafetyEntry {")
    w("    const char* name;")
    w("    MapRole role;")
    w("    // Bit 0 protanopia, 1 deuteranopia, 2 tritanopia, 3 monochrome,")
    w("    // 4 normal vision. Set means the map is right for that viewer.")
    w("    //")
    w("    // Bits 0-2 and 4 are the measurement: does the map survive being")
    w("    // simulated through that deficiency. Bit 3 is NOT - monochrome is a")
    w("    // request for no colour rather than a question about legibility, and")
    w("    // answering it with the measurement is a bug that shipped: Plasma")
    w("    // scores 16.9 in greyscale, so \"safe in monochrome\" said yes and a")
    w("    // person who had asked for no colour kept a full-colour surface. Bit 3")
    w("    // is set only for the maps that are grey by construction.")
    w("    unsigned char passes;")
    w("};")
    w("")
    w("inline const QVector<SafetyEntry>& safety(){")
    w("    static const QVector<SafetyEntry> entries{")
    for r in rows:
        bits = 0
        for i, m in enumerate(modes):
            if m == "mono":
                # Monochrome asks a different question from the three
                # dichromacies, and answering it with the measurement was a bug
                # worth recording: Plasma scores 16.9 in greyscale, comfortably
                # above the margin, so "is it safe in monochrome" said yes and a
                # person who had asked for no colour went on looking at a full
                # colour surface. Surviving a greyscale print is not the same as
                # being grey. So this bit is set only for the maps that are
                # achromatic by construction.
                if r["name"] in achromatic_names:
                    bits |= (1 << i)
                continue
            if safe(r, m):
                bits |= (1 << i)
        if safe(r, "normal"):
            bits |= (1 << 4)
        scores = "  ".join(f"{m} {r[m][0]:.1f}" for m in ["normal"] + modes)
        w(f'        {{"{r["name"]}",MapRole::{ROLE_ENUM[r["role"]]},0x{bits:02x}}},'
          f"  // {scores}")
    w("    };")
    w("    return entries;")
    w("}")
    w("")
    w("inline const SafetyEntry* safetyFor(const QString& name){")
    w("    // Empty is Viridis - the same rule tableFor() follows, and it has to")
    w("    // be the same rule or the default map would be judged as an unknown")
    w("    // one and pass everything by omission. That is how the default figure")
    w("    // stayed in colour after a request for monochrome.")
    w("    const QString wanted=name.isEmpty()?QStringLiteral(\"Viridis\"):name;")
    w("    for(const SafetyEntry& e:safety())")
    w("        if(wanted.compare(QLatin1String(e.name),Qt::CaseInsensitive)==0) return &e;")
    w("    return nullptr;")
    w("}")
    w("")
    w("inline MapRole roleOf(const QString& name){")
    w("    const SafetyEntry* e=safetyFor(name);")
    w("    return e?e->role:MapRole::Sequential;")
    w("}")
    w("")
    w("// Bit index for a vision mode, or -1 for Standard - which asks no question,")
    w("// so no map can fail it.")
    w("inline int visionBit(ColourVision mode){")
    w("    switch(mode){")
    w("    case ColourVision::Protanopia:  return 0;")
    w("    case ColourVision::Deuteranopia:return 1;")
    w("    case ColourVision::Tritanopia:  return 2;")
    w("    case ColourVision::Monochrome:  return 3;")
    w("    default: return -1;")
    w("    }")
    w("}")
    w("")
    w("inline bool mapIsSafeFor(const QString& name,ColourVision mode){")
    w("    const int bit=visionBit(mode);")
    w("    if(bit<0) return true;")
    w("    const SafetyEntry* e=safetyFor(name);")
    w("    return e?((e->passes>>bit)&1)!=0:true;")
    w("}")
    w("")
    w("// The map a reader with this vision should get instead, keeping the role so")
    w("// the substitution cannot change what the figure claims about the data.")
    w("inline QString safeSubstituteFor(const QString& name,ColourVision mode){")
    w("    switch(mode){")
    for m, label in [("protan", "Protanopia"), ("deutan", "Deuteranopia"),
                     ("tritan", "Tritanopia"), ("mono", "Monochrome")]:
        w(f"    case ColourVision::{label}:")
        w("        switch(roleOf(name)){")
        for role in ["sequential", "diverging", "cyclic", "categorical"]:
            pick = best.get((role, m))
            if pick:
                w(f'        case MapRole::{ROLE_ENUM[role]}: '
                  f'return QStringLiteral("{pick}");')
        w("        default: break;")
        w("        }")
        w("        break;")
    w("    default: break;")
    w("    }")
    w("    return name;")
    w("}")
    w("")
    w("// The maps this reader should be OFFERED, best first, per role.")
    w("//")
    w("// safeSubstituteFor answers \"what do I draw instead of this unreadable")
    w("// map\" with one name per role. That is enough to rescue a figure and it")
    w("// is not enough for two other things the interface needs:")
    w("//")
    w("//   - a \"Best for this reader\" group in the chooser. Hiding the maps a")
    w("//     reader cannot use says which are ruled out; it never says which to")
    w("//     pick, and of the maps that pass, some clear the bar by a point and")
    w("//     some by fifteen.")
    w("//   - a substitute that RESEMBLES the chosen map. With one name per role,")
    w("//     everyone who picked any unreadable diverging map got the same one.")
    w("//     With a list, the interface can take the nearest entry to what was")
    w("//     chosen, so a blue-white-red map comes back blue-white-red.")
    w("//")
    w("// Ordered by the same rule as the substitute above: the preferred tier")
    w("// first - the maps designed for the job - and the measured score only")
    w("// ordering within it. Ranked by score alone this list starts with Afmhot,")
    w("// a black-red-yellow heat ramp that scores well because it is violently")
    w("// contrasty, ahead of Cividis, which exists for precisely this purpose.")
    w("inline QStringList recommendedMaps(MapRole role,ColourVision mode){")
    w("    switch(mode){")
    for m, label in [("protan", "Protanopia"), ("deutan", "Deuteranopia"),
                     ("tritan", "Tritanopia"), ("mono", "Monochrome")]:
        w(f"    case ColourVision::{label}:")
        w("        switch(role){")
        for role in ["sequential", "diverging", "cyclic", "categorical"]:
            names = recommended.get((role, m)) or []
            if names:
                joined = ",".join(f'QStringLiteral("{n}")' for n in names)
                w(f'        case MapRole::{ROLE_ENUM[role]}: return {{{joined}}};')
        w("        default: break;")
        w("        }")
        w("        break;")
    w("    default: break;")
    w("    }")
    w("    return {};")
    w("}")
    w("")
    w("// What the substitution costs, or an empty string when it costs nothing.")
    w("// A substitution that loses something has to SAY so - on the figure as")
    w("// well as in the setting - or the reader is looking at a picture that")
    w("// quietly stopped showing part of what it used to.")
    w("inline QString substitutionNote(const QString& name,ColourVision mode){")
    w("    if(mapIsSafeFor(name,mode)) return QString();")
    w("    if(mode==ColourVision::Monochrome){")
    w("        switch(roleOf(name)){")
    w("        case MapRole::Diverging:")
    w("            return QStringLiteral(\"Monochrome: no grey ramp can mark a \"")
    w("                                  \"centre, so the two sides now read as \"")
    w("                                  \"one scale.\");")
    w("        case MapRole::Categorical:")
    w("            return QStringLiteral(\"Monochrome: categories are separated by \"")
    w("                                  \"lightness alone, which carries about six \"")
    w("                                  \"before they merge.\");")
    w("        case MapRole::Cyclic:")
    w("            return QStringLiteral(\"Monochrome: a grey ramp cannot close a \"")
    w("                                  \"cycle, so the wrap point is no longer \"")
    w("                                  \"visible.\");")
    w("        default: return QString();")
    w("        }")
    w("    }")
    w("    return QString();")
    w("}")
    w("")
    w("} // namespace colourmaps")
    w("} // namespace graphvis")
    print("\n".join(out))


if __name__ == "__main__":
    main()
