"""Add the axis-scale variants to the GraphVis catalogue.

A catalogue entry is (engine, scale). The `scale` field already carries the
variant dimension for the engines that have one - FFT windows on the spectral
family, interpolation kinds, map projections, grid resolutions - and it carries
axis scale for the rest. So a scale variant is only added to an engine whose
entries leave `scale` empty; giving Power Spectral Density a "Quantile X" entry
would mean an entry that cannot also say which window it uses.

The engines are further limited to those drawn against a pair of ordinary axes,
read from QtPlotBackend::engineHasAxes rather than guessed at. A quantile axis
on a pie chart is not a thing.

Idempotent: an entry is keyed by (engine, scale) and replaced rather than
duplicated, and the counts at the top of the file are recomputed rather than
edited - a hand-edited count that disagrees with the list is worse than none.

    python3 tools/add_scale_variants.py [config/graph_catalogue.json] \
                                        [--axes-list path]
"""
import json
import pathlib
import sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
path = pathlib.Path(args[0] if args else "config/graph_catalogue.json")

axes_path = None
if "--axes-list" in sys.argv:
    axes_path = pathlib.Path(sys.argv[sys.argv.index("--axes-list") + 1])

doc = json.loads(path.read_text(encoding="utf-8"))
cats = doc["categories"] if isinstance(doc, dict) and "categories" in doc else doc

# The engines that are drawn against axes. Produced by a probe over
# QtPlotBackend::engineHasAxes so this file and the backend cannot disagree
# about which engines have an x and a y at all.
if axes_path and axes_path.exists():
    axis_engines = {ln.strip() for ln in axes_path.read_text().splitlines() if ln.strip()}
else:
    raise SystemExit("need --axes-list from the engineHasAxes probe")

# Engines whose axes are not measurements, so a scale on them means nothing.
#
# A correlation matrix's axes are variable INDICES - 0, 1, 2 - and the log of a
# variable index is not a quantity. A confusion matrix's are class labels, a
# Gantt's are schedule rows, a candlestick's is a period number. Every one of
# these would take a scale variant and draw either the same picture or nothing,
# and a catalogue entry that cannot draw is worse than one that does not exist.
NO_SCALE = {
    "Correlation Matrix", "Covariance Matrix", "Spy Matrix", "Confusion Matrix",
    "Mosaic Plot", "UpSet Plot", "Calendar Heatmap", "Rainflow Matrix",
    "Gantt Schedule", "Availability Timeline", "Borehole Log",
    "OHLC Candlestick", "Plot Matrix", "Scatter + Marginals",
    "Parallel Coordinates", "Andrews Curves", "Population Pyramid",
    "Event Plot", "Eye Diagram", "Dendrogram",
    # Batch 5. These four draw a GEOMETRIC claim on their axes, and a
    # transformed axis makes the claim false while leaving it on the page:
    # Mohr's circle is a circle only on equal linear axes; the I-V curve's
    # rectangle has area Pmax only on linear ones; the voltammogram reports a
    # peak separation in millivolts read off the potential axis; and the
    # waffle's axes are grid positions rather than a quantity at all.
    "Mohr's Circle", "I-V Curve", "Cyclic Voltammogram", "Waffle Chart",
    # Batch 6. Same test: the axes are positions or indices rather than
    # measured quantities, so a transform on them means nothing. A star glyph's
    # axes are grid slots, a recurrence plot's are sample numbers, a biplot's
    # are principal-component scores that go negative, a sunflower's petals are
    # drawn in data units and a transformed axis bends them, and the operating
    # characteristic curve computes its own axes from a sampling plan.
    "Star Glyph Plot", "Recurrence Plot", "Biplot", "Sunflower Plot",
    "Wavelet Scalogram", "Operating Characteristic Curve",
}

# The variants, and what each is for. Ordered so the most useful come first in
# the library, since that is the order they are listed in.
VARIANTS = [
    ("Semi-Log Y",
     "logarithmic y, original units on the axis; for a response spanning decades"),
    ("Logarithmic Scale",
     "logarithmic on both axes; for a power law, which is then a straight line"),
    ("Semi-Log X",
     "logarithmic x, original units on the axis"),
    ("Semi-Log(1+x) Y",
     "log10(1 + y); the log axis for a column that legitimately reaches zero"),
    ("Standardised (Z-Score)",
     "both axes in standard deviations from their own mean, for columns in different units"),
    ("Quantile X",
     "x replaced by its position in the sorted sample, which spreads a skewed column evenly"),
]

# What each engine and category already holds, so nothing is duplicated and a
# new entry inherits the category and description of the one it varies.
existing = {}
first_entry = {}
scales_used = {}
for c in cats:
    for e in c.get("entries", []):
        existing[(e["engine"], e.get("scale"))] = e
        first_entry.setdefault(e["engine"], e)
        scales_used.setdefault(e["engine"], set()).add(e.get("scale"))

added = []
for engine, entry in sorted(first_entry.items()):
    if engine not in axis_engines or engine in NO_SCALE:
        continue
    # Only engines whose scale dimension is free. An engine already using it
    # for something else - a window, an interpolation kind, a projection -
    # cannot express both in one field.
    if scales_used[engine] != {None}:
        continue
    for scale, why in VARIANTS:
        if (engine, scale) in existing:
            continue
        added.append({
            "category": entry["category"],
            # The library shows the name; it has to say which variant this is.
            "name": "%s · %s" % (entry["name"], scale),
            "engine": engine,
            "scale": scale,
            # Marked advanced so the default library view stays the size it was.
            # 433 entries is already more than anyone scrolls; these are found
            # by searching for the engine, or by ticking Include advanced.
            "advanced": True,
            "description": "%s — %s" % (entry.get("description", engine), why),
            "preview": entry.get("preview", ""),
            "thumbnail": entry.get("thumbnail", ""),
        })

VARIANT_NAMES = {name for name, _ in VARIANTS}

# Drop any scale variant this run would no longer produce. Without this the
# script is idempotent only in the adding direction, and an engine moved into
# NO_SCALE keeps entries nothing generates any more.
for c in cats:
    c["entries"] = [e for e in c.get("entries", [])
                    if not (e.get("scale") in VARIANT_NAMES
                            and (e["engine"] in NO_SCALE or e["engine"] not in axis_engines))]

by_category = {}
for c in cats:
    by_category[c["name"]] = c
for e in added:
    cat = by_category.get(e["category"])
    if cat is None:
        continue
    # Replace rather than append, so a second run is a no-op.
    cat["entries"] = [x for x in cat["entries"]
                      if not (x["engine"] == e["engine"] and x.get("scale") == e["scale"])]
    cat["entries"].append(e)

entry_count = sum(len(c.get("entries", [])) for c in cats)
engine_count = len({e["engine"] for c in cats for e in c.get("entries", [])})
if isinstance(doc, dict):
    doc["category_count"] = len(cats)
    doc["entry_count"] = entry_count
    doc["engine_count"] = engine_count

path.write_text(json.dumps(doc, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
print("added %d entries; catalogue now %d entries / %d engines / %d categories"
      % (len(added), entry_count, engine_count, len(cats)))
