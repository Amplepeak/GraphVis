"""Assign every catalogue entry to a pack.

A pack is a FILTER over a bundled catalogue, not a separate download. Every
engine is compiled into the executable either way; what a pack decides is
whether its categories appear in the Graph Library. That distinction matters
and is stated plainly in the interface too - offering to "install" something
that is already installed would be a lie with a progress bar on it.

All packs are enabled by default, so nothing disappears from a working
installation. The point is to let someone doing bioprocess work switch off
sixty-five aviation entries and forty logistics ones, rather than to hide
things from them.

The unit is the CATEGORY, because that is the unit the library is browsed in.
An engine cannot be in a different pack from the category it is filed under
without the category listing entries that the pack filter then removes, which
reads as a miscount.

Idempotent: rerunning rewrites the same field with the same value.

    python3 tools/assign_catalogue_packs.py [config/graph_catalogue.json]
"""
import json
import pathlib
import sys

path = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "config/graph_catalogue.json")
doc = json.loads(path.read_text(encoding="utf-8"))
cats = doc["categories"]

# Base is everything a general scientific plotting tool should open with: the
# ordinary chart types, the statistics, the model diagnostics. Someone who
# never turns a pack on still has a complete, capable library.
PACKS = {
    "base": [
        "Line Plots",
        "Scatter & Bubble Charts",
        "Discrete Data Plots",
        "Data Distribution Plots",
        "Statistical & Diagnostic Plots",
        "Statistical Inference & Effect",
        "Publication & Statistical Plots",
        "Presentation & Comparison",
        "Time Series & Trend",
        "Advanced Line & Signal Plots",
        "Contour Plots",
        "Surface & Mesh Plots",
        "Polar Plots",
        "Matrix, Correlation & Multivariate",
        "Multivariate Plots",
        "Categorical & Set Views",
        "Model Evaluation",
        "Data Reduction & Interpolation",
        "Flow, Network & Composition",
        "Animation & Dynamic Display",
    ],
    "engineering": [
        "Materials & Chemistry",
        "Fluid & Field Dynamics",
        "Engineering 3D & Field",
        "Vector Fields",
        "Tensor & Structure Analysis",
        "Quality, Process & Reliability",
        "Statistical Process Control",
        "Maintenance & Reliability",
        "Logistics & Infrastructure",
        "Aviation & Flight",
        "Civil & Structural",
        "Energy & Building Services",
    ],
    "earth": [
        "Map Projections",
        "Geographic Plots",
        "Advanced Spatial & GIS",
        "Terrain & Topography",
        "Earth & Ocean Science",
        "Spatial & Specialized",
    ],
    "life": [
        "Clinical & Meta-analysis",
        "Electrochemical & Bioprocess",
        "Spectroscopy & Chromatography",
    ],
    "physics": [
        "Spectral",
        "Spectral & Signal Imaging",
        "Signal Processing Plots",
        "Signals & RF",
        "Volume Visualization",
    ],
}

owner = {}
for pack, names in PACKS.items():
    for name in names:
        if name in owner:
            raise SystemExit("category %r is in two packs: %s and %s"
                             % (name, owner[name], pack))
        owner[name] = pack

unassigned = sorted({c["name"] for c in cats} - set(owner))
if unassigned:
    # A category with no pack would vanish the moment packs were honoured,
    # which is a far worse failure than this message.
    raise SystemExit("categories with no pack: %s" % ", ".join(unassigned))

counts = {p: 0 for p in PACKS}
for c in cats:
    pack = owner[c["name"]]
    c["pack"] = pack
    for e in c["entries"]:
        e["pack"] = pack
        counts[pack] += 1

doc["packs"] = [
    {"id": "base", "name": "Core",
     "description": "The ordinary chart types, statistics, distributions and "
                    "model diagnostics. Always on."},
    {"id": "engineering", "name": "Engineering",
     "description": "Mechanical, materials, fatigue, fluids, reliability, "
                    "process control, aviation, civil and power."},
    {"id": "earth", "name": "Earth & Environment",
     "description": "Map projections, GIS, terrain, hydrology, geology and "
                    "oceanography."},
    {"id": "life", "name": "Life Sciences & Medicine",
     "description": "Clinical trials and meta-analysis, bioprocess and "
                    "electrochemistry, spectroscopy and chromatography."},
    {"id": "physics", "name": "Physics & Signals",
     "description": "Spectral analysis, signal imaging, RF and control, and "
                    "volume visualisation."},
]
for p in doc["packs"]:
    p["entryCount"] = counts[p["id"]]

doc["category_count"] = len(cats)
doc["entry_count"] = sum(len(c["entries"]) for c in cats)
doc["engine_count"] = len({e["engine"] for c in cats for e in c["entries"]})

path.write_text(json.dumps(doc, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
for p in doc["packs"]:
    print("%-12s %-28s %5d entries" % (p["id"], p["name"], p["entryCount"]))
print("total %d entries / %d engines / %d categories"
      % (doc["entry_count"], doc["engine_count"], doc["category_count"]))
