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
