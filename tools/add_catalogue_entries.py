"""Add the new sector engines to the GraphVis catalogue.

Idempotent: an entry is keyed by (category, name) and is replaced rather than
duplicated, so running this twice leaves the catalogue as it was after running
it once. The counts at the top of the file are recomputed rather than edited,
because a hand-edited count that disagrees with the list is worse than no count.
"""
import json, sys, pathlib

path = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "config/graph_catalogue.json")
doc = json.loads(path.read_text(encoding="utf-8"))

def entry(category, name, engine, description, scale=None, advanced=False, preview=""):
    return {"category": category, "name": name, "engine": engine, "scale": scale,
            "advanced": advanced, "description": description,
            "preview": preview, "thumbnail": ""}

new = []

# ---------------------------------------------------------------- projections
# Every geographic engine can be drawn through any of the projections; the
# catalogue is how a projection is chosen, so each pairing is an entry.
PROJECTIONS = [
    ("Mercator", "conformal cylindrical, angles true, area exaggerated toward the poles"),
    ("Web Mercator", "the tile-server projection, in metres"),
    ("Lambert Conformal Conic", "conformal conic on two standard parallels, for mid-latitude regions"),
    ("Azimuthal Equidistant", "distance and bearing true from the centre of the data"),
    ("UTM", "transverse Mercator on WGS-84, the zone taken from the data"),
]
GEO_ENGINES = [("Geo Line", "line"), ("Geo Scatter", "points"),
               ("Geo Bubble", "proportional symbols"), ("Geo Density", "density")]
for proj, why in PROJECTIONS:
    for eng, kind in GEO_ENGINES:
        new.append(entry("Map Projections", f"{proj.lower()} {kind}", eng,
                         f"Coordinates through the {proj} projection — {why}",
                         scale=proj, preview="🌐"))

# ------------------------------------------------------------------- terrain
TERRAIN = [
    ("great circle route", "Great Circle Route", "Shortest path over the sphere between waypoints", "➰"),
    ("ground track", "Ground Track", "A track over the ground, cut at the antimeridian", "🛰"),
    ("terrain profile", "Terrain Profile", "Elevation against distance travelled along a path", "⛰"),
    ("hypsometric curve", "Hypsometric Curve", "Fraction of area lying above each elevation", "📐"),
    ("slope map", "Slope Map", "Ground slope in degrees, from a height grid", "📉"),
    ("aspect map", "Aspect Map", "The compass direction each slope faces", "🧭"),
    ("hillshade", "Hillshade", "Shaded relief, sun at 315 degrees and 45 degrees up", "🌄"),
    ("elevation contour", "2D Contour", "Contours of a height field", "🗺"),
    ("elevation heat field", "2D Heatmap", "Height as a colour field", "🟩"),
]
for name, engine, desc, prev in TERRAIN:
    new.append(entry("Terrain & Topography", name, engine, desc, preview=prev))

# ------------------------------------------------------------------ aviation
AVIATION = [
    ("payload-range diagram", "Payload-Range Diagram", "Payload against range, filled to the axis", "📦"),
    ("V-n flight envelope", "V-n Flight Envelope", "Load factor against airspeed, closed, with the 1 g line", "✈"),
    ("altitude-Mach envelope", "Altitude-Mach Envelope", "The altitude and Mach the aircraft may be flown at", "🛫"),
    ("drag polar", "Drag Polar", "CL against CD with CD0, k and the best L/D fitted", "🪁"),
    ("lift curve", "Lift Curve", "CL against angle of attack, slope fitted below the stall", "📈"),
    ("flight profile", "Flight Profile", "Altitude against time or distance, top of climb marked", "🛩"),
    ("flight ground track", "Ground Track", "The path over the ground, antimeridian handled", "🗺"),
    ("great circle sector", "Great Circle Route", "Route between airports as the shortest path", "➰"),
    ("runway crosswind", "Runway Crosswind", "Crosswind and headwind resolved from wind and runway heading", "💨"),
    ("weight and balance", "Weight and Balance Envelope", "Loadings tested against the CG envelope", "⚖"),
]
for name, engine, desc, prev in AVIATION:
    new.append(entry("Aviation & Flight", name, engine, desc, preview=prev))

# --------------------------------------------------------------- reliability
RELIABILITY = [
    ("Weibull probability plot", "Weibull Probability Plot", "Median ranks with the shape and scale fitted", "📊"),
    ("reliability growth", "Reliability Growth", "Crow-AMSAA: cumulative failures against cumulative time", "📉"),
    ("MTBF trend", "MTBF Trend", "Cumulative and rolling mean time between failures", "⏱"),
    ("maintenance calendar", "Calendar Heatmap", "Daily values as a grid of weeks", "📅"),
    ("CUSUM chart", "CUSUM Chart", "Cumulative sum with the standard slack and decision interval", "📶"),
    ("EWMA chart", "EWMA Chart", "Exponentially weighted moving average with widening limits", "〰"),
    ("S-N fatigue curve", "S-N Fatigue Curve", "Stress against cycles with Basquin's law fitted", "🔩"),
    ("rainflow matrix", "Rainflow Matrix", "Closed hysteresis cycles by range and mean, ASTM E1049", "🌧"),
    ("availability timeline", "Availability Timeline", "Uptime and downtime spans per asset", "🟦"),
    ("failure Pareto", "Pareto Front", "Failure modes ranked by contribution", "📶"),
]
for name, engine, desc, prev in RELIABILITY:
    new.append(entry("Maintenance & Reliability", name, engine, desc, preview=prev))

# ------------------------------------------------------ logistics and assets
LOGISTICS = [
    ("Gantt schedule", "Gantt Schedule", "One row per task, a bar from start to end", "📋"),
    ("network graph", "Network Graph", "Nodes and links, laid out force-directed and deterministically", "🕸"),
    ("chord diagram", "Chord Diagram", "An origin-destination matrix as arcs and ribbons", "🔄"),
    ("origin-destination flow", "Origin-Destination Flow", "Flows between places as great circles, width by volume", "🔀"),
    ("cumulative flow diagram", "Cumulative Flow", "Stacked bands showing work in each stage over time", "📚"),
    ("duration curve", "Duration Curve", "Values sorted against the fraction of time exceeded", "📉"),
    ("inventory sawtooth", "Inventory Sawtooth", "Stock over time with reorder points and the mean level", "🪚"),
    ("borehole log", "Borehole Log", "Intervals by depth, one column per hole", "🕳"),
    ("traffic fundamental diagram", "Fundamental Diagram", "Flow against density with Greenshields fitted", "🚗"),
    ("OHLC candlestick", "OHLC Candlestick", "Open, high, low and close per period", "🕯"),
]
for name, engine, desc, prev in LOGISTICS:
    new.append(entry("Logistics & Infrastructure", name, engine, desc, preview=prev))

# ============================ GAP SEARCH ==================================
# The second wave: engines found by the specialised-engine gap search and by
# the sci-draw / LabPlot feature analysis. Same shape as everything above, so
# the merge below treats them identically; they are separated only so it stays
# clear which pass each entry came from.
# ==========================================================================

MATERIALS = [
    ("stress-strain curve", "Stress-Strain Curve", "Engineering stress against strain, with the 0.2% offset yield, UTS and modulus marked", "\U0001F4CF"),
    ("Arrhenius plot", "Arrhenius Plot", "ln(k) against 1/T with the activation energy fitted", "\U0001F321"),
    ("titration curve", "Titration Curve", "pH against titrant volume, equivalence point at the steepest slope", "\U0001F9EA"),
    ("calibration curve", "Calibration Curve", "Signal against known concentration, with the limit of detection", "\U0001F4C8"),
    ("Michaelis-Menten", "Michaelis-Menten", "Reaction rate against substrate, Vmax and Km fitted", "\U0001F9EC"),
    ("dose-response curve", "Dose-Response Curve", "Four-parameter logistic against log dose, EC50 marked", "\U0001F489"),
]
for name, engine, desc, prev in MATERIALS:
    new.append(entry("Materials & Chemistry", name, engine, desc, preview=prev))

CLINICAL = [
    ("Kaplan-Meier survival", "Kaplan-Meier Survival", "Survival probability as a step function, censoring ticked", "\U0001FA7A"),
    ("funnel plot", "Funnel Plot", "Effect size against precision, with the pseudo-confidence funnel", "\U0001F53B"),
]
for name, engine, desc, prev in CLINICAL:
    new.append(entry("Clinical & Meta-analysis", name, engine, desc, preview=prev))

SPC = [
    ("X-bar and R chart", "X-bar and R Chart", "Subgroup means and ranges with Shewhart A2, D3 and D4 limits", "\U0001F4CA"),
    ("process capability", "Process Capability", "The distribution against the specification, with Cp, Cpk and Pp", "\U0001F3AF"),
    ("p-chart", "p-Chart", "Defective fraction, limits widening as the subgroup shrinks", "\U0001F4C9"),
    ("np-chart", "np-Chart", "Count of defectives for a constant subgroup size", "\U0001F522"),
    ("c-chart", "c-Chart", "Defects per unit, Poisson limits", "\U0001F535"),
    ("u-chart", "u-Chart", "Defects per unit area, limits varying with the area inspected", "\U0001F7E3"),
]
for name, engine, desc, prev in SPC:
    new.append(entry("Statistical Process Control", name, engine, desc, preview=prev))

EARTH = [
    ("T-S diagram", "T-S Diagram", "Temperature against salinity with density contours drawn behind", "\U0001F30A"),
    ("CTD profile", "CTD Profile", "Depth downward against temperature, salinity and density", "\U0001F321"),
    ("rating curve", "Rating Curve", "Discharge against stage, with the power law fitted", "\U0001F30A"),
    ("drawdown curve", "Drawdown Curve", "Well drawdown against log time, Cooper-Jacob transmissivity fitted", "\U0001F573"),
    ("stereonet", "Stereonet", "Poles and great circles on an equal-area lower hemisphere", "\U0001F52E"),
]
for name, engine, desc, prev in EARTH:
    new.append(entry("Earth & Ocean Science", name, engine, desc, preview=prev))

ENERGY = [
    ("wind power curve", "Wind Power Curve", "Turbine power against wind speed with cut-in, rated and cut-out", "\U0001F4A8"),
    ("psychrometric chart", "Psychrometric Chart", "Humidity ratio against dry-bulb, with the saturation and RH curves", "\U0001F4A7"),
]
for name, engine, desc, prev in ENERGY:
    new.append(entry("Energy & Building Services", name, engine, desc, preview=prev))

CIVIL = [
    ("mass haul diagram", "Mass Haul Diagram", "Cumulative cut and fill along a chainage, balance points marked", "\U0001F69C"),
    ("shear and moment", "Shear and Moment", "Shear force and bending moment along a beam", "\U0001F3D7"),
]
for name, engine, desc, prev in CIVIL:
    new.append(entry("Civil & Structural", name, engine, desc, preview=prev))

SIGNALS = [
    ("eye diagram", "Eye Diagram", "Every symbol period overlaid, with the eye height and width", "\U0001F441"),
    ("Smith chart", "Smith Chart", "Normalised impedance on the constant-R and constant-X circles", "\U0001F4E1"),
]
for name, engine, desc, prev in SIGNALS:
    new.append(entry("Signals & RF", name, engine, desc, preview=prev))

CATEGORICAL = [
    ("mosaic plot", "Mosaic Plot", "A contingency table as tiles sized by count, shaded by residual", "\U0001F9E9"),
    ("UpSet plot", "UpSet Plot", "Set intersections as a bar chart with a membership matrix", "\U0001F53C"),
    ("dendrogram", "Dendrogram", "Hierarchical clustering drawn as a tree of merge heights", "\U0001F333"),
    ("rug plot", "Rug Plot", "Every observation as a tick on the axis, under the main series", "\U0001F4CF"),
]
for name, engine, desc, prev in CATEGORICAL:
    new.append(entry("Categorical & Set Views", name, engine, desc, preview=prev))

MODEL_EVAL = [
    ("precision-recall curve", "Precision-Recall Curve", "Precision against recall with the average precision", "\U0001F3AF"),
    ("confusion matrix", "Confusion Matrix", "Predicted against actual as a shaded table of counts", "\U0001F7E6"),
    ("MA plot", "MA Plot", "Log ratio against mean intensity, with a loess trend", "\U0001F9EA"),
]
for name, engine, desc, prev in MODEL_EVAL:
    new.append(entry("Model Evaluation", name, engine, desc, preview=prev))

# ------------------------------------------------------------------- merging
by_name = {c["name"]: c for c in doc["categories"]}
order = [c["name"] for c in doc["categories"]]
added = replaced = 0
for item in new:
    cat = item["category"]
    if cat not in by_name:
        by_name[cat] = {"name": cat, "entries": []}
        order.append(cat)
    entries = by_name[cat]["entries"]
    for i, existing in enumerate(entries):
        if existing.get("name") == item["name"]:
            entries[i] = item
            replaced += 1
            break
    else:
        entries.append(item)
        added += 1

doc["categories"] = [by_name[n] for n in order]
doc["category_count"] = len(doc["categories"])
doc["entry_count"] = sum(len(c["entries"]) for c in doc["categories"])
doc["engine_count"] = len({e["engine"] for c in doc["categories"] for e in c["entries"]})
path.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
print(f"added {added}, replaced {replaced}")
print(f"categories {doc['category_count']}, entries {doc['entry_count']}, engines {doc['engine_count']}")
