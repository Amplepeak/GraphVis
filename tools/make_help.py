#!/usr/bin/env python3
"""Build config/help_topics.json - the manual the application ships.

Why this is generated rather than written
-----------------------------------------
The help is the one document in the project that is guaranteed to be read by
someone who cannot check it. Everywhere else, a number that has drifted is
caught by the person who knows better; in the help it is simply believed.

The first version of this help was hand-written and already wrong. It said
GraphVis ships "440 of them" as a literal in prose - true on the day it was
typed and true only by luck afterwards. The graph packs menu had the same
disease from the other direction: `packs[].entryCount` in the catalogue summed
to 2116 while `entry_count` in the same file said 2122, because one script
recomputed the headline and another recomputed the packs, and nobody compared
them. Two independent answers to one question, drifting in silence - the root
cause this project keeps meeting.

So: every number and every list in the manual comes from the thing it
describes.

  * REFERENCE topics are generated outright. The layout list is read out of
    UiLayouts.cpp, the scales out of PlotCanvas.cpp, the colour maps out of
    ColourMaps.h, the readable formats out of importer.py. A layout added to
    the table appears in the manual with no edit here.
  * WRITTEN topics are hand-authored in config/help_written.json, but any
    number in them is a {placeholder} filled from the same parsed facts. A
    placeholder this script cannot resolve is a hard error, so prose can quote
    a figure and still never be wrong.

Every extractor states what it expects to find and RAISES if it does not. That
is deliberate and it is the whole safety argument: a parser that silently
returns an empty list when a table moves does not produce a broken build, it
produces a manual quietly missing a chapter, which nobody notices for months.
A generator that stops and names the file is the only version of this worth
shipping.

    python tools/make_help.py            # write config/help_topics.json
    python tools/make_help.py --check    # fail if the file is out of date
    python tools/make_help.py --stats    # print the facts it extracted
"""
from __future__ import annotations

import argparse
import ast
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WRITTEN = ROOT / "config" / "help_written.json"
OUT = ROOT / "config" / "help_topics.json"
CATALOGUE = ROOT / "config" / "graph_catalogue.json"

SCHEMA = "graphvis.help/2"


class Stale(SystemExit):
    """A source this script reads no longer looks the way it did.

    Its own class so the message can say which file and what was expected,
    rather than surfacing as an IndexError three frames down.
    """

    def __init__(self, path: Path, expected: str, got: str = "") -> None:
        rel = path.relative_to(ROOT) if path.is_absolute() else path
        msg = f"{rel}: expected {expected}"
        if got:
            msg += f"; found {got}"
        msg += ("\n\nThe help generator reads this file. Either the table moved "
                "or its shape changed. Fix the extractor in tools/make_help.py "
                "- do NOT let the manual quietly lose a section.")
        super().__init__(msg)


# ---------------------------------------------------------------- C++ strings
_STR = re.compile(r'"((?:[^"\\]|\\.)*)"')
_ESCAPES = {"n": "\n", "t": "\t", "\\": "\\", '"': '"', "'": "'", "0": "\0"}


def _unescape(raw: str) -> str:
    out, i = [], 0
    while i < len(raw):
        c = raw[i]
        if c == "\\" and i + 1 < len(raw):
            out.append(_ESCAPES.get(raw[i + 1], raw[i + 1]))
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def _join_adjacent(blob: str) -> str:
    """Every string literal in `blob`, concatenated.

    C++ joins adjacent literals, and every long string in this codebase is
    written that way to stay inside the line width. Taking only the first would
    truncate most of the descriptions this manual is made of - which is exactly
    the failure that makes a generated document worse than a written one.
    """
    return "".join(_unescape(m.group(1)) for m in _STR.finditer(blob))


def _balanced(text: str, open_at: int) -> int:
    """Index just past the bracket opened at `open_at`, ignoring brackets in
    strings, characters and comments."""
    pairs = {"(": ")", "{": "}", "[": "]"}
    opener = text[open_at]
    closer = pairs[opener]
    depth = 0
    i = open_at
    n = len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        if c in ('"', "'"):
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == c:
                    break
                j += 1
            i = j + 1
            continue
        if c == opener:
            depth += 1
        elif c == closer:
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError("unbalanced bracket")


def _qstrings(text: str) -> list[str]:
    """Every QStringLiteral(...) / QLatin1String(...) payload, in order."""
    out = []
    for m in re.finditer(r"\b(?:QStringLiteral|QLatin1String)\s*\(", text):
        open_at = m.end() - 1
        end = _balanced(text, open_at)
        out.append(_join_adjacent(text[open_at + 1:end - 1]))
    return out


def _body_of(path: Path, signature: str) -> str:
    """The braced body of the function whose declaration matches `signature`."""
    text = path.read_text(encoding="utf-8", errors="replace")
    m = re.search(signature, text)
    if not m:
        raise Stale(path, f"a definition matching /{signature}/")
    open_at = text.index("{", m.end() - 1)
    return text[open_at:_balanced(text, open_at)]


# ------------------------------------------------------------------ the facts
def read_catalogue() -> dict:
    doc = json.loads(CATALOGUE.read_text(encoding="utf-8"))
    cats = doc.get("categories") or []
    if len(cats) < 20:
        raise Stale(CATALOGUE, "at least 20 categories", str(len(cats)))

    # Counted here, never taken from the header. The header is a second answer
    # to a question the entries already answer, and it was wrong by six.
    entries = [e for c in cats for e in c["entries"]]
    engines = {e["engine"] for e in entries}
    per_pack: dict[str, int] = {}
    for e in entries:
        per_pack[e.get("pack", "?")] = per_pack.get(e.get("pack", "?"), 0) + 1

    packs = []
    for p in doc.get("packs", []):
        packs.append({"id": p["id"], "name": p["name"],
                      "description": p["description"],
                      "entries": per_pack.get(p["id"], 0)})
    if not packs:
        raise Stale(CATALOGUE, "a packs list")

    categories = []
    for c in cats:
        ent = c["entries"]
        categories.append({
            "name": c["name"],
            "pack": c.get("pack", ""),
            "entries": len(ent),
            "engines": len({e["engine"] for e in ent}),
            "verified": sum(1 for e in ent if e.get("verified")),
            "examples": [e["engine"] for e in ent[:4]],
        })
    return {"categories": categories, "packs": packs,
            "entry_count": len(entries), "engine_count": len(engines),
            "category_count": len(cats),
            "verified_count": sum(1 for e in entries if e.get("verified"))}


def read_scales() -> list[dict]:
    """The axis-scale variants, from the table that applies them."""
    path = ROOT / "native" / "plot2d" / "src" / "PlotCanvas.cpp"
    text = path.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"static const Variant kVariants\[\]\s*=\s*\{", text)
    if not m:
        raise Stale(path, "the kVariants table")
    block = text[m.end() - 1:_balanced(text, m.end() - 1)]
    rows = re.findall(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*(\w+)\s*,\s*(\w+)\s*\}', block)
    if len(rows) < 5:
        raise Stale(path, "at least 5 kVariants rows", str(len(rows)))
    axis = {"AxisLinear": "linear", "AxisLog10": "log₁₀",
            "AxisLog1p": "log₁₀(1+x)", "AxisZScore": "z-score",
            "AxisQuantile": "quantile"}
    out = []
    for name, x, y in rows:
        if x not in axis or y not in axis:
            raise Stale(path, "known axis transforms", f"{x}, {y}")
        out.append({"name": _unescape(name), "x": axis[x], "y": axis[y]})
    return out


def read_layouts() -> tuple[list[dict], list[dict]]:
    path = ROOT / "app" / "src" / "UiLayouts.cpp"
    groups_body = _body_of(path, r"const QVector<UiLayoutGroup>&\s*uiLayoutGroups\s*\(")
    gstr = _qstrings(groups_body)
    if len(gstr) < 4 or len(gstr) % 2:
        raise Stale(path, "name/description pairs in uiLayoutGroups",
                    f"{len(gstr)} strings")
    groups = [{"name": gstr[i], "description": gstr[i + 1]}
              for i in range(0, len(gstr), 2)]

    body = _body_of(path, r"const QVector<UiLayout>&\s*uiLayouts\s*\(")
    # One record per `l={};` reset. Splitting on the reset rather than on
    # `l.name=` keeps the group number, which is assigned on the same line as
    # the reset and would otherwise land in the previous record.
    chunks = re.split(r"\bl\s*=\s*\{\s*\}\s*;", body)[1:]
    layouts = []
    for chunk in chunks:
        def field(name: str) -> str:
            m = re.search(r"\bl\." + name + r"\s*=\s*QStringLiteral\s*\(", chunk)
            if not m:
                return ""
            open_at = m.end() - 1
            return _join_adjacent(chunk[open_at + 1:_balanced(chunk, open_at) - 1])
        g = re.search(r"\bl\.group\s*=\s*(\d+)", chunk)
        name = field("name")
        if not name:
            continue
        layouts.append({
            "name": name,
            "icon": field("icon"),
            "from": field("from"),
            "description": field("description"),
            "group": int(g.group(1)) if g else -1,
        })
    if len(layouts) < 10:
        raise Stale(path, "at least 10 layouts", str(len(layouts)))
    for l in layouts:
        if not 0 <= l["group"] < len(groups):
            raise Stale(path, f"a valid group for {l['name']!r}", str(l["group"]))
    return groups, layouts


def read_colourmaps() -> list[dict]:
    path = ROOT / "native" / "plot2d" / "include" / "ColourMaps.h"
    body = _body_of(path, r"categories\s*\(\s*\)\s*\{")
    m = re.search(r"kCats?\b[^{]*\{|cats\s*\{", body)
    if not m:
        raise Stale(path, "the categories table")
    block = body[m.end() - 1:_balanced(body, m.end() - 1)]
    out = []
    # Each entry is {QStringLiteral("Category"),{QStringLiteral("A"),...}}.
    # Found by walking the inner braces rather than by one regex, because a
    # category name can contain an ampersand and a map name can contain a
    # space, and neither survives a pattern written to be clever.
    i = 0
    while True:
        j = block.find("{QStringLiteral(", i)
        if j < 0:
            break
        end = _balanced(block, j)
        entry = block[j:end]
        inner = entry.find("{", 1)
        if inner < 0:
            i = end
            continue
        names = _qstrings(entry[inner:_balanced(entry, inner)])
        cat = _qstrings(entry[:inner])
        if cat and names:
            out.append({"name": cat[0], "maps": names})
        i = end
    if len(out) < 4:
        raise Stale(path, "at least 4 colour-map categories", str(len(out)))
    return out


def read_estimators() -> dict:
    path = ROOT / "native" / "plot2d" / "src" / "SurfaceEstimators.cpp"

    def names(fn: str, least: int) -> list[str]:
        got = _qstrings(_body_of(path, r"QStringList\s+" + fn + r"\s*\(\s*\)"))
        if len(got) < least:
            raise Stale(path, f"at least {least} names in {fn}", str(len(got)))
        return got

    est = names("estimatorNames", 8)
    grp = names("estimatorGroupNames", 8)
    if len(est) != len(grp):
        # Two lists indexed by the same enum. If they ever differ in length,
        # every estimator past the shorter one is labelled with the wrong
        # group - silently, because nothing else compares them.
        raise Stale(path, "estimatorNames and estimatorGroupNames to be the "
                          "same length", f"{len(est)} and {len(grp)}")

    # Which ones are actually OFFERED, by enum rather than by display name.
    #
    # estimatorImplemented() returns true for a list of cases and false for
    # everything else; one estimator is present in the name list and
    # deliberately withheld (Clough-Tocher, which fails the C1 continuity it is
    # named for - the reasoning is in that function). Listing it in the manual
    # as available would be a claim the code disproves.
    #
    # Matching on the ENUM is what makes this hold. An earlier version compared
    # the `case` labels against each other, which can only ever come out empty,
    # and reported nothing withheld - a check that cannot fail is not a check.
    header = ROOT / "native" / "plot2d" / "include" / "SurfaceEstimators.h"
    enum_body = _body_of(header, r"enum class Estimator\s*\{")
    members = [m for m in re.findall(r"^\s*(\w+)\s*,", enum_body, re.M)
               if m != "Count"]
    if len(members) != len(est):
        # The name list is indexed BY this enum. If they are different lengths
        # every name past the shorter one belongs to a different estimator.
        raise Stale(header, f"{len(est)} enumerators to match estimatorNames",
                    str(len(members)))

    impl_body = _body_of(path, r"bool\s+estimatorImplemented\s*\(")
    returns_true = impl_body.find("return true;")
    if returns_true < 0:
        raise Stale(path, "a `return true;` in estimatorImplemented")
    offered = set(re.findall(r"case\s+Estimator::(\w+)\s*:",
                             impl_body[:returns_true]))
    withheld = [n for n, e in zip(est, members) if e not in offered]

    return {
        "estimators": [{"name": n, "group": g} for n, g in zip(est, grp)],
        "withheld": withheld,
        "extrapolation": names("extrapolationNames", 3),
        "failure": names("failureFootprintNames", 3),
        "bridging": names("bridgingNames", 3),
        "invalid": names("invalidDisplayNames", 3),
        "variogram": names("krigingVariogramNames", 3),
        "values": names("valuePolicyNames", 2),
        "response": names("responseSpaceNames", 2),
    }


def read_engine_parameters() -> list[dict]:
    """Engines that declare tunable constants, with the help text they carry.

    THE RETURN TYPE IS PART OF THE ANCHOR, because a CALL to this function
    matches its name just as well as the definition does - and now there is one
    earlier in the file than the definition. `declaresMarginalPlacement` asks
    `QtPlotBackend::engineParameters(engine)` whether an engine declares a
    marginal placement, `_body_of` takes the FIRST match, and the body it
    returned was the two-line helper. No parameters were found, the whole
    "Engines with constants of their own" topic silently disappeared from the
    manual, and what the build reported was a dead cross-reference pointing at
    the missing page - a true statement about the wrong thing.

    The count guard below is the other half. A reader that finds nothing must
    say so: this manual is generated from the code precisely so it cannot drift,
    and an extractor that quietly returns an empty list is drift with a clean
    exit code.
    """
    path = ROOT / "native" / "plot2d" / "src" / "QtPlotBackend.cpp"
    body = _body_of(
        path,
        r"QVector<QtPlotBackend::EngineParameter>\s+QtPlotBackend::engineParameters\s*\(")
    out = []
    for m in re.finditer(r'engine\s*==\s*QLatin1String\s*\(\s*"((?:[^"\\]|\\.)*)"\s*\)',
                         body):
        start = body.index("{", m.end())
        block = body[start:_balanced(body, start)]
        params = []
        i = 0
        while True:
            j = block.find("{QStringLiteral(", i)
            if j < 0:
                break
            end = _balanced(block, j)
            entry = block[j:end]
            strs = _qstrings(entry)
            if len(strs) >= 3:
                # key, label, unit, help - unit is QString() for a plain number,
                # in which case only three strings are present.
                key, label = strs[0], strs[1]
                unit = strs[2] if len(strs) >= 4 else ""
                text = strs[3] if len(strs) >= 4 else strs[2]
                params.append({"key": key, "label": label, "unit": unit,
                               "help": text})
            i = end
        if params:
            out.append({"engine": _unescape(m.group(1)), "parameters": params})
    if len(out) < 3:
        raise Stale(path, "at least 3 engines declaring constants", str(len(out)))
    return out


def read_profiles() -> list[dict]:
    """The built-in publication profiles - BOTH ways they are written.

    Four are built field by field (`PublicationProfile screen; screen.dpi=...`)
    and the other sixty-one go through an `add(name, widthMm, ...)` lambda
    declared halfway down the function. Reading only the first shape found four
    profiles and lost the entire list of publishers, and the "at least 3" guard
    below happily passed - which is the exact failure mode this generator is
    built to refuse. The guard is now a floor near the real count, so losing a
    shape stops the build instead of shortening the manual.
    """
    path = ROOT / "native" / "plot2d" / "src" / "PublicationProfile.cpp"
    body = _body_of(path, r"QVector<PublicationProfile>\s+builtinProfiles\s*\(")
    chunks = re.split(r"\bPublicationProfile\s+(\w+)\s*;", body)
    out = []
    for k in range(1, len(chunks), 2):
        var, chunk = chunks[k], chunks[k + 1]

        def sfield(name: str) -> str:
            m = re.search(re.escape(var) + r"\." + name + r"\s*=\s*QStringLiteral\s*\(",
                          chunk)
            if not m:
                return ""
            open_at = m.end() - 1
            return _join_adjacent(chunk[open_at + 1:_balanced(chunk, open_at) - 1])

        def nfield(name: str) -> float | None:
            m = re.search(re.escape(var) + r"\." + name + r"\s*=\s*([-\d./*+ ]+?)\s*;",
                          chunk)
            if not m:
                return None
            try:                      # `160.0/25.4` is written as the division
                return float(eval(m.group(1), {"__builtins__": {}}, {}))
            except Exception:
                return None

        name = sfield("name")
        if not name:
            continue
        width_in = nfield("figureWidthIn")
        out.append({
            "name": name,
            "dpi": nfield("dpi"),
            "width_mm": round(width_in * 25.4) if width_in else None,
            "base_font": nfield("baseFontSize"),
            "notes": sfield("notes"),
            "publisher": False,
        })

    # add(name, widthMm, heightMm, dpi, base, title, axis, tick, legend,
    #     lineWidth, markerSize, font, notes)
    for m in re.finditer(r"\badd\s*\(", body):
        open_at = m.end() - 1
        call = body[open_at:_balanced(body, open_at)]
        strs = _qstrings(call)
        nums = re.findall(r"(?<![\w.])(\d+(?:\.\d+)?)(?![\w.])", call)
        if len(strs) < 3 or len(nums) < 4:
            continue                      # not the profile-adding lambda
        out.append({
            "name": strs[0],
            "dpi": float(nums[2]),
            "width_mm": float(nums[0]),
            "base_font": float(nums[3]),
            "notes": strs[-1],
            "publisher": True,
        })

    if len(out) < 20:
        raise Stale(path, "at least 20 built-in profiles - both the "
                          "field-by-field ones and the add() calls",
                    str(len(out)))
    return out


def read_colour_vision() -> list[str]:
    path = next(ROOT.rglob("ColourVision.h"), None)
    if path is None:
        raise Stale(Path("ColourVision.h"), "the header to exist")
    got = _qstrings(_body_of(path, r"QStringList\s+colourVisionNames\s*\(\s*\)"))
    if len(got) < 3:
        raise Stale(path, "at least 3 colour-vision names", str(len(got)))
    return got


def read_formats() -> tuple[dict[str, list[str]], list[str]]:
    """Readable extensions by group, and the ones that need no add-on.

    Parsed rather than imported: this has to run on a machine with neither
    pandas nor the add-on installed, and importing the module to ask it would
    require both.
    """
    path = ROOT / "services" / "python" / "graphvis_science" / "data" / "importer.py"
    tree = ast.parse(path.read_text(encoding="utf-8", errors="replace"))
    groups: dict[str, list[str]] = {}
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        if not (isinstance(node.func, ast.Name) and node.func.id == "_register"):
            continue
        if len(node.args) != 3:
            continue
        exts, group = node.args[0], node.args[2]
        if not isinstance(group, ast.Constant) or not isinstance(exts, (ast.Tuple, ast.List)):
            continue
        for el in exts.elts:
            if isinstance(el, ast.Constant) and isinstance(el.value, str):
                groups.setdefault(group.value, []).append(el.value)
    if len(groups) < 4:
        raise Stale(path, "at least 4 format groups", str(len(groups)))

    # The formats the executable reads with no add-on at all.
    #
    # From nativeReadsExtension(), which is the function that ROUTES an import:
    # true means the native core opens the file, false means it goes to the
    # Python importer. That makes it the only honest answer to "what works
    # without the add-on".
    #
    # NOT from importNameFilters(): those are the file-dialog filters and they
    # list all 149 extensions, add-on included. Reading them here produced a
    # manual that told someone with no add-on installed that GraphVis reads
    # DICOM - the file dialog would have offered it and the import would have
    # failed, which is precisely the kind of confident wrong answer a generated
    # manual is supposed to make impossible.
    native = ROOT / "app" / "src" / "AppController.cpp"
    body = _body_of(native, r"bool\s+nativeReadsExtension\s*\(")
    builtin = sorted("." + e for e in _qstrings(body))
    if not 2 <= len(builtin) <= 12:
        raise Stale(native, "a short list of natively-read extensions",
                    f"{len(builtin)}")
    return {g: sorted(set(v)) for g, v in sorted(groups.items())}, builtin


def gather() -> dict:
    cat = read_catalogue()
    groups, layouts = read_layouts()
    cmaps = read_colourmaps()
    fmt_groups, builtin = read_formats()
    return {
        "catalogue": cat,
        "scales": read_scales(),
        "layout_groups": groups,
        "layouts": layouts,
        "colourmaps": cmaps,
        "field": read_estimators(),
        "engine_parameters": read_engine_parameters(),
        "profiles": read_profiles(),
        "colour_vision": read_colour_vision(),
        "formats": fmt_groups,
        "builtin_formats": builtin,
    }


# ------------------------------------------------------------- the placeholders
def numbers(f: dict) -> dict[str, str]:
    """What a written topic may quote. Everything here is counted, not typed."""
    cat = f["catalogue"]
    field = f["field"]
    return {
        "engines": str(cat["engine_count"]),
        "entries": f"{cat['entry_count']:,}",
        "categories": str(cat["category_count"]),
        "verified": str(cat["verified_count"]),
        "unverified": str(cat["entry_count"] - cat["verified_count"]),
        "packs": str(len(cat["packs"])),
        "layouts": str(len(f["layouts"])),
        "layout_groups": str(len(f["layout_groups"])),
        "scales": str(len(f["scales"])),
        "colourmaps": str(sum(len(c["maps"]) for c in f["colourmaps"])),
        "colourmap_groups": str(len(f["colourmaps"])),
        "estimators": str(sum(1 for e in field["estimators"]
                              if e["name"] not in field["withheld"])),
        "formats": str(sum(len(v) for v in f["formats"].values())),
        "format_groups": str(len(f["formats"])),
        "builtin_formats": ", ".join(f["builtin_formats"]),
        "profiles": str(len(f["profiles"])),
        "publisher_profiles": str(sum(1 for p in f["profiles"] if p["publisher"])),
        "colour_vision_modes": str(len(f["colour_vision"])),
        "withheld_estimators": ", ".join(f["field"]["withheld"]) or "none",
    }


_PLACE = re.compile(r"\{([a-z_]+)\}")


def fill(text: str, facts: dict[str, str], where: str) -> str:
    def one(m):
        key = m.group(1)
        if key not in facts:
            raise SystemExit(
                f"{where}: unknown placeholder {{{key}}}.\n"
                f"Known: {', '.join(sorted(facts))}\n"
                "A number in the manual must come from the program, so every "
                "placeholder has to resolve. Add it to numbers() or write the "
                "number out only if it can never change.")
        return facts[key]
    return _PLACE.sub(one, text)


# -------------------------------------------------------------- the reference
def _rows(pairs: list[tuple[str, str]]) -> str:
    """A definition list. StyledText has no tables, so this is the shape that
    survives: a bold term and its explanation, one per line."""
    return "<br>".join(f"<b>{t}</b> &mdash; {d}" for t, d in pairs)


def _escaped(value):
    """Every string in the extracted facts, safe to drop into markup.

    Done once, over the whole structure, rather than at each of the ninety
    places a name is interpolated below. Category names carry ampersands
    (`Scatter & Bubble Charts`, `Terrain, Ocean & Field`), and so does at least
    one publisher (`Taylor & Francis`); a bare & in StyledText is the start of
    an entity, so `Scatter & Bubble` renders as `Scatter` followed by whatever
    the parser makes of the rest. Escaping at the choke point means a name that
    gains a character later cannot reintroduce the bug.
    """
    if isinstance(value, str):
        return value.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    if isinstance(value, list):
        return [_escaped(v) for v in value]
    if isinstance(value, dict):
        return {k: _escaped(v) for k, v in value.items()}
    return value


def reference_topics(facts: dict) -> list[dict]:
    f = _escaped(facts)
    cat = f["catalogue"]
    out: list[dict] = []

    # --- every graph category ------------------------------------------
    lines = []
    for c in cat["categories"]:
        eg = ", ".join(c["examples"])
        lines.append(
            f"<b>{c['name']}</b> &mdash; {c['entries']} entries drawn by "
            f"{c['engines']} engine{'s' if c['engines'] != 1 else ''} "
            f"({c['pack']} pack). e.g. {eg}.")
    out.append({
        "id": "ref-categories", "section": "reference", "kind": "reference",
        "title": f"Every graph category ({cat['category_count']})",
        "keywords": "catalogue category list all graphs kinds types index",
        "body": ("The graph library groups its "
                 f"{cat['entry_count']:,} entries into {cat['category_count']} "
                 "categories. An <i>entry</i> is an engine at one scale, so "
                 f"the {cat['engine_count']} engines produce far more entries "
                 "than there are engines. The pack in brackets is the one that "
                 "has to be enabled for the category to appear.<br><br>"
                 + "<br><br>".join(lines))})

    # --- packs ----------------------------------------------------------
    out.append({
        "id": "ref-packs", "section": "reference", "kind": "reference",
        "title": "Graph packs",
        "keywords": "pack packs core engineering earth life physics enable subset",
        "body": ("Packs decide which graphs the library offers. Turning one "
                 "off does not uninstall anything - it shortens the list, "
                 "which is the point when you are looking for a box plot among "
                 f"{cat['entry_count']:,} entries. <b>Options ▸ Graph "
                 "packs.</b><br><br>"
                 + "<br><br>".join(
                     f"<b>{p['name']}</b> ({p['entries']} entries) &mdash; "
                     f"{p['description']}" for p in cat["packs"]))})

    # --- scales ---------------------------------------------------------
    out.append({
        "id": "ref-scales", "section": "reference", "kind": "reference",
        "title": f"Axis scales ({len(f['scales'])})",
        "keywords": "scale variant log logarithmic semilog z-score quantile axis transform",
        "body": ("Most catalogue entries are one engine at one of these "
                 "scales. Choosing a scale changes how the axis is "
                 "<i>marked</i>, not what your data is - nothing on disk is "
                 "touched and the figure can be put back.<br><br>"
                 + _rows([(s["name"], f"x: {s['x']}, y: {s['y']}")
                          for s in f["scales"]])
                 + "<br><br><b>Reading these.</b> A log axis cannot show zero "
                 "or a negative number, and points at zero simply vanish; "
                 "log(1+x) exists for exactly that case. A z-score axis is "
                 "standard deviations from the mean, which is how two columns "
                 "in different units are compared on one pair of axes. A "
                 "quantile axis is position in the sorted sample, which is how "
                 "a column that is mostly one value stops being one pixel "
                 "wide.")})

    # --- colour maps ----------------------------------------------------
    total = sum(len(c["maps"]) for c in f["colourmaps"])
    out.append({
        "id": "ref-colourmaps", "section": "reference", "kind": "reference",
        "title": f"Colour maps ({total})",
        "keywords": "colour color map colormap viridis jet parula list ramp",
        "body": (f"{total} maps in {len(f['colourmaps'])} groups. A map turns "
                 "a number into a colour; the group tells you what it is fit "
                 "for.<br><br>"
                 + "<br><br>".join(
                     f"<b>{c['name']}</b> ({len(c['maps'])})<br>"
                     + ", ".join(c["maps"]) for c in f["colourmaps"])
                 + "<br><br><b>Which group to use.</b> Perceptually uniform "
                 "for a quantity you want read off the figure - equal steps in "
                 "the number are equal steps in the colour, which is what "
                 "makes a gradient in the picture mean a gradient in the data. "
                 "Diverging for a quantity with a meaningful middle, such as "
                 "an anomaly about zero. Categorical for labels, never for "
                 "numbers. Terrain, ocean and rainbow maps are here because "
                 "conventions in some fields expect them, not because they "
                 "read well: Jet in particular invents a bright band in the "
                 "middle of the range that is not in your data.")})

    # --- layouts --------------------------------------------------------
    by_group: dict[int, list[dict]] = {}
    for l in f["layouts"]:
        by_group.setdefault(l["group"], []).append(l)
    chunks = []
    for gi, g in enumerate(f["layout_groups"]):
        members = by_group.get(gi, [])
        if not members:
            continue
        chunks.append(
            f"<b>{g['name']}</b><br><i>{g['description']}</i><br>"
            + "<br>".join(
                f"&nbsp;&nbsp;{l['icon']} <b>{l['name']}</b>"
                + (f" (after {l['from']})" if l["from"] else "")
                + f" &mdash; {l['description']}" for l in members))
    out.append({
        "id": "ref-layouts", "section": "reference", "kind": "reference",
        "title": f"Window layouts ({len(f['layouts'])})",
        "keywords": "layout window arrangement rail inspector zen dock shape interface",
        "body": ("<b>View ▸ Layout.</b> A layout rearranges the whole "
                 "window: which panels exist, where they sit, and whether "
                 "there is a bar across the top at all. It changes nothing "
                 "about your data or your figure, so trying one costs "
                 "nothing.<br><br>"
                 "If a layout hides a control you want, <b>Ctrl+K</b> reaches "
                 "everything from any layout, and <b>View ▸ Layout</b> is "
                 "always on the menu.<br><br>" + "<br><br>".join(chunks))})

    # --- field estimation ----------------------------------------------
    field = f["field"]
    groups: dict[str, list[str]] = {}
    for e in field["estimators"]:
        if e["name"] in field["withheld"]:
            continue
        groups.setdefault(e["group"], []).append(e["name"])
    out.append({
        "id": "ref-estimators", "section": "reference", "kind": "reference",
        "title": "Field estimators",
        "keywords": "estimator interpolation kriging idw spline loess surface grid field scattered",
        "body": ("<b>View ▸ Estimating a field ▸ Method.</b> A "
                 "contour, surface or heat map needs a value everywhere; "
                 "measurements exist only where you measured. An estimator is "
                 "the rule that fills in between, and the choice is a "
                 "statement about your data, not a preference.<br><br>"
                 + "<br><br>".join(
                     f"<b>{g}</b><br>&nbsp;&nbsp;" + "<br>&nbsp;&nbsp;".join(m)
                     for g, m in groups.items())
                 + "<br><br><b>Auto</b> looks at whether your points fall on a "
                 "grid and picks accordingly, and is the right answer until "
                 "you have a reason it is not.<br><br>"
                 "<b>The options beside it.</b><br><br>"
                 + _rows([
                     ("Beyond the measurements",
                      "what happens outside the area you sampled: "
                      + "; ".join(field["extrapolation"])),
                     ("Values",
                      "whether the estimate may exceed what you observed: "
                      + "; ".join(field["values"])
                      + " &mdash; and the space it is fitted in: "
                      + "; ".join(field["response"])),
                     ("Kriging variogram",
                      "how quickly the response is assumed to decorrelate: "
                      + "; ".join(field["variogram"])),
                     ("Failed runs",
                      "how much of the surface one failure invalidates ("
                      + "; ".join(field["failure"])
                      + "), whether holes are bridged ("
                      + "; ".join(field["bridging"])
                      + "), and how a masked area is drawn ("
                      + "; ".join(field["invalid"]) + ")"),
                 ])
                 + "<br><br>The distinction that matters most: <b>masking is "
                 "honesty and bridging is convenience</b>. A masked area says "
                 "“not measured”. Anything that fills it in is the "
                 "program's guess wearing the same colours as your data.")})

    # --- engine constants -----------------------------------------------
    if f["engine_parameters"]:
        chunks = []
        for ep in f["engine_parameters"]:
            chunks.append(
                f"<b>{ep['engine']}</b><br>"
                + "<br>".join(
                    f"&nbsp;&nbsp;<b>{p['label']}</b>"
                    + (f" [{p['unit']}]" if p["unit"] else "")
                    + f" &mdash; {p['help']}" for p in ep["parameters"]))
        out.append({
            "id": "ref-engine-parameters", "section": "reference",
            "kind": "reference",
            "title": "Engines with constants of their own",
            "keywords": "parameter constant dalitz forest pooled mass setting engine",
            "body": ("Almost every engine gets everything it needs from your "
                     "columns. A few need a number your data cannot supply - a "
                     "particle mass, or which kind of effect measure a column "
                     "holds - and those appear as extra controls when the "
                     "engine is chosen.<br><br>" + "<br><br>".join(chunks))})

    # --- publication profiles -------------------------------------------
    def prow(p: dict) -> str:
        bits = []
        if p["width_mm"]:
            bits.append(f"{p['width_mm']:g} mm wide")
        if p["dpi"]:
            bits.append(f"{int(p['dpi'])} dpi")
        if p["base_font"]:
            bits.append(f"{p['base_font']:g} pt text")
        return (f"<b>{p['name']}</b> &mdash; {', '.join(bits)}.<br>"
                f"&nbsp;&nbsp;<i>{p['notes']}</i>")

    working = [p for p in f["profiles"] if not p["publisher"]]
    publishers = [p for p in f["profiles"] if p["publisher"]]
    out.append({
        "id": "ref-profiles", "section": "reference", "kind": "reference",
        "title": f"Publication profiles ({len(f['profiles'])})",
        "keywords": ("journal profile publication export dpi width millimetres "
                     "submit nature science elsevier ieee wiley springer plos"),
        "body": ("A profile is a figure specification: a column width in "
                 "millimetres, a text size in points <i>at final size</i>, a "
                 "minimum line weight and a resolution floor. The figure is "
                 "<b>re-rendered</b> at those measurements rather than scaled "
                 "to them, which is the whole point - shrinking a screen "
                 "figure to 89 mm takes its 8 pt labels down to about 3 pt, "
                 "and that is what comes back from a copy editor.<br><br>"
                 "<b>Working profiles</b><br><br>"
                 + "<br><br>".join(prow(p) for p in working)
                 + f"<br><br><b>Publishers ({len(publishers)})</b><br><br>"
                 "Every number in these is what that publisher's own author "
                 "guidelines state. Where a publisher does not publish a "
                 "figure, the profile's note says so rather than quietly "
                 "filling one in - a profile that looks authoritative and is "
                 "not is the one thing here nobody would think to check. "
                 "<b>Check the current instructions before you submit</b>; "
                 "these were correct when they were written.<br><br>"
                 + "<br><br>".join(prow(p) for p in publishers))})

    # --- formats ---------------------------------------------------------
    total_fmt = sum(len(v) for v in f["formats"].values())
    out.append({
        "id": "ref-formats", "section": "reference", "kind": "reference",
        "title": f"File formats it can read ({total_fmt})",
        "keywords": "format file extension csv excel matlab hdf5 netcdf read import supported",
        "body": ("<b>Without any add-on</b> GraphVis reads "
                 + ", ".join(f["builtin_formats"])
                 + " directly in the application.<br><br>"
                 f"<b>With the Science add-on</b> a further {total_fmt} "
                 "extensions are converted on import. The file is read, "
                 "converted to Arrow and handed to the same code that loads a "
                 "CSV - so what plots is identical whichever door it came "
                 "in by. A format whose Python package is missing says "
                 "exactly what to install rather than failing "
                 "quietly.<br><br>"
                 + "<br><br>".join(
                     f"<b>{g}</b><br>&nbsp;&nbsp;" + " ".join(v)
                     for g, v in f["formats"].items()))})

    # --- colour vision ---------------------------------------------------
    out.append({
        "id": "ref-colour-vision-modes", "section": "reference",
        "kind": "reference",
        "title": "Colour-vision modes",
        "keywords": "colourblind colorblind protanopia deuteranopia tritanopia monochrome accessible",
        "body": ("<b>View ▸ Colour vision</b> chooses the palette the "
                 "series are drawn in. This changes the figure itself, so a "
                 "figure exported in one of these modes carries that palette "
                 "wherever it goes.<br><br>"
                 + _rows([(n, "") for n in f["colour_vision"]]).replace(" &mdash; ", "")
                 + "<br><br><b>Standard is safe for all three deficiencies</b> "
                 "and is the right choice unless you are designing for one in "
                 "particular. A palette tuned for one deficiency is <i>not</i> "
                 "automatically safe for another - measured true of protanopia "
                 "and deuteranopia, false of tritanopia - and GraphVis says so "
                 "on the menu when the one you picked has that "
                 "problem.<br><br>Separately, <b>Preview as this reader sees "
                 "it</b> simulates the deficiency over the whole figure "
                 "without changing it. That is a check, not a setting: it "
                 "shows you what somebody else sees.")})
    return out


# ------------------------------------------------------------------- markup
# What Qt's Text.StyledText actually understands. It is a small subset of HTML,
# and - this is the reason for the check - it fails SILENTLY: an unknown tag or
# an unclosed <b> is not an error, it just renders wrongly or swallows the rest
# of the paragraph. Over a hundred kilobytes of hand-written markup, a typo
# that eats half an article would be found by a reader, months later.
STYLED_TAGS = {"b", "i", "u", "br", "font", "big", "small", "sub", "sup",
               "a", "s", "strike", "em", "strong", "p", "ol", "ul", "li",
               "h1", "h2", "h3", "h4", "h5", "h6", "pre", "center", "img"}
VOID_TAGS = {"br", "img"}
_TAG = re.compile(r"<\s*(/?)\s*([a-zA-Z0-9]+)([^>]*)>")
_ENTITY = re.compile(r"&(#\d+|#x[0-9a-fA-F]+|[a-zA-Z][a-zA-Z0-9]*);")
KNOWN_ENTITIES = {"amp", "lt", "gt", "quot", "apos", "nbsp", "mdash", "ndash",
                  "ldquo", "rdquo", "lsquo", "rsquo", "hellip", "times",
                  "deg", "middot", "plusmn", "frac12", "copy", "reg"}


def _check_markup(body: str, where: str) -> None:
    stack: list[str] = []
    for m in _TAG.finditer(body):
        closing, name, rest = m.group(1), m.group(2).lower(), m.group(3)
        if name not in STYLED_TAGS:
            raise SystemExit(
                f"{where}: <{name}> is not a tag Text.StyledText understands. "
                f"It will not render, and it will not report an error either.\n"
                f"Allowed: {', '.join(sorted(STYLED_TAGS))}")
        if name in VOID_TAGS or rest.rstrip().endswith("/"):
            continue
        if closing:
            if not stack or stack[-1] != name:
                raise SystemExit(
                    f"{where}: </{name}> closes "
                    + (f"<{stack[-1]}>" if stack else "nothing")
                    + ". StyledText will swallow text rather than complain.")
            stack.pop()
        else:
            stack.append(name)
    if stack:
        raise SystemExit(
            f"{where}: <{stack[-1]}> is never closed. Everything after it "
            "renders in that style, to the end of the article.")

    for m in _ENTITY.finditer(body):
        name = m.group(1)
        if not name.startswith("#") and name not in KNOWN_ENTITIES:
            raise SystemExit(
                f"{where}: &{name}; is not an entity to rely on. Use a "
                "numeric reference, or the character itself - the file is "
                "UTF-8.")

    # A bare & that is not an entity renders as itself in StyledText, but it is
    # nearly always a half-typed entity, and `&mdash` without its semicolon is
    # the exact typo this catches.
    for m in re.finditer(r"&(?![a-zA-Z#])|&[a-zA-Z#][a-zA-Z0-9#]*(?![a-zA-Z0-9#;])",
                         body):
        raise SystemExit(
            f"{where}: {m.group(0)!r} looks like an entity missing its "
            "semicolon. Write &amp; for a literal ampersand.")


# ------------------------------------------------------------------ assembly
def build() -> dict:
    facts = gather()
    nums = numbers(facts)

    written = json.loads(WRITTEN.read_text(encoding="utf-8"))
    sections = [dict(s) for s in written["sections"]]
    for s in sections:
        s["blurb"] = fill(s["blurb"], nums, f"section {s['id']!r}")
    topics: list[dict] = []
    for t in written["topics"]:
        t = dict(t)
        where = f"help_written.json topic {t['id']!r}"
        t["title"] = fill(t["title"], nums, where)
        t["body"] = fill(t["body"], nums, where)
        t["keywords"] = fill(t.get("keywords", ""), nums, where)
        topics.append(t)
    topics.extend(reference_topics(facts))

    # ---- the checks that make this safe to ship ------------------------
    for t in topics:
        _check_markup(t["body"], f"topic {t['id']!r}")

    seen: dict[str, str] = {}
    known_sections = {s["id"] for s in sections}
    for t in topics:
        if t["id"] in seen:
            raise SystemExit(f"two topics share the id {t['id']!r}")
        seen[t["id"]] = t["title"]
        if t["section"] not in known_sections:
            raise SystemExit(
                f"topic {t['id']!r} is in section {t['section']!r}, which is "
                f"not declared. Sections: {', '.join(sorted(known_sections))}")
        for field in ("title", "body", "keywords"):
            if not t.get(field, "").strip():
                raise SystemExit(f"topic {t['id']!r} has an empty {field}")
    for t in topics:
        for ref in t.get("see_also", []):
            if ref not in seen:
                raise SystemExit(
                    f"topic {t['id']!r} points at {ref!r}, which does not "
                    "exist. A dead cross-reference is worse than none: it "
                    "sends the reader nowhere and looks like their mistake.")
    for s in sections:
        if not any(t["section"] == s["id"] for t in topics):
            raise SystemExit(f"section {s['id']!r} has no topics")

    # Topics in section order, so the browser can group without sorting.
    order = {s["id"]: i for i, s in enumerate(sections)}
    topics.sort(key=lambda t: order[t["section"]])

    return {
        "schema": SCHEMA,
        "note": ("Generated by tools/make_help.py. Do not edit: the written "
                 "half lives in config/help_written.json and the reference "
                 "half is read out of the program's own tables, so a number "
                 "here cannot disagree with the program. Run the script."),
        "counts": {"topics": len(topics), "sections": len(sections),
                   "written": len(written["topics"]),
                   "generated": len(topics) - len(written["topics"])},
        "sections": sections,
        "topics": topics,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="build the GraphVis manual")
    ap.add_argument("--check", action="store_true",
                    help="exit non-zero if the written file is out of date")
    ap.add_argument("--stats", action="store_true",
                    help="print what was extracted, and stop")
    args = ap.parse_args()

    if args.stats:
        facts = gather()
        for k, v in numbers(facts).items():
            print(f"{k:22s} {v}")
        return 0

    doc = build()
    text = json.dumps(doc, indent=1, ensure_ascii=False) + "\n"

    if args.check:
        current = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
        if current != text:
            print("config/help_topics.json is out of date - run "
                  "tools/make_help.py", file=sys.stderr)
            return 1
        print(f"help up to date: {doc['counts']['topics']} topics")
        return 0

    OUT.write_text(text, encoding="utf-8")
    c = doc["counts"]
    print(f"wrote {OUT.relative_to(ROOT)}: {c['topics']} topics in "
          f"{c['sections']} sections ({c['written']} written, "
          f"{c['generated']} generated)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
