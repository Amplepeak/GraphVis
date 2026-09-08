"""Catalogue entries for batch 1 of the engine expansion.

Idempotent: keyed by (engine, scale), replaced rather than appended. Run
tools/add_scale_variants.py afterwards to give the new axis engines their scale
variants, the same way every other engine got them.

    python3 tools/add_batch1_engines.py [config/graph_catalogue.json]
"""
import json
import pathlib
import sys

path = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "config/graph_catalogue.json")
doc = json.loads(path.read_text(encoding="utf-8"))
cats = doc["categories"]

# (category, name, engine, description). Categories are existing ones: a new
# engine that needs a new category usually means it has been filed wrong.
NEW = [
    ("Statistical Inference & Effect", "lorenz", "Lorenz Curve",
     "Cumulative share of the total against cumulative share of the population, with the Gini coefficient"),
    ("Statistical Inference & Effect", "concentration", "Concentration Curve",
     "Cumulative share of an outcome against population ranked by a second variable"),
    ("Data Distribution Plots", "rankabundance", "Rank-Abundance Curve",
     "Value against rank, commonest first, on a log axis — dominance versus evenness"),
    ("Model Evaluation", "scree", "Scree Plot",
     "Eigenvalues by component with the cumulative share over the top"),
    ("Model Evaluation", "predictionerror", "Prediction Error Plot",
     "Observed against predicted with the identity line, the best fit and R²"),
    ("Model Evaluation", "learningcurve", "Learning Curve",
     "Training and validation score against training-set size — the gap is overfitting"),
    ("Model Evaluation", "validationcurve", "Validation Curve",
     "Training and validation score against one hyperparameter"),
    ("Model Evaluation", "discrimination", "Discrimination Threshold",
     "Precision, recall, F1 and queue rate against the decision threshold"),
    ("Model Evaluation", "silhouette", "Silhouette Plot",
     "One bar per observation, sorted within its cluster, with the overall mean"),
    ("Model Evaluation", "elbow", "Elbow Plot",
     "Clustering score against number of clusters, with the knee found by maximum chord distance"),
    ("Clinical & Meta-analysis", "radialplot", "Radial Plot",
     "Galbraith plot: standardised effect against precision, with the pooled slope"),
    ("Clinical & Meta-analysis", "labbe", "L'Abbé Plot",
     "Treated-arm rate against control-arm rate, one point per study"),
    ("Clinical & Meta-analysis", "caterpillar", "Caterpillar Plot",
     "A forest plot sorted by estimate, so the shape of the evidence is visible"),
    ("Clinical & Meta-analysis", "cumulativemeta", "Cumulative Meta-Analysis",
     "The pooled estimate recomputed as each study is added, with its interval"),
    ("Clinical & Meta-analysis", "nelsonaalen", "Nelson-Aalen Cumulative Hazard",
     "Cumulative hazard against time, where a constant hazard is a straight line"),
    ("Clinical & Meta-analysis", "cumincidence", "Cumulative Incidence",
     "Cumulative incidence against time, from event times and an event flag"),
    ("Clinical & Meta-analysis", "decisioncurve", "Decision Curve",
     "Net benefit against threshold probability, with treat-all and treat-none"),
    ("Maintenance & Reliability", "bathtub", "Bathtub Curve",
     "Hazard rate against age — infant mortality, useful life and wear-out"),
    ("Maintenance & Reliability", "duane", "Duane Plot",
     "Cumulative MTBF against cumulative operating time on log-log axes, with the growth slope"),
    ("Maintenance & Reliability", "mcf", "Mean Cumulative Function",
     "Mean cumulative repairs per unit against age, for repairable systems"),
]

by_name = {c["name"]: c for c in cats}
missing = sorted({c for c, *_ in NEW if c not in by_name})
if missing:
    raise SystemExit("no such categories: %s" % ", ".join(missing))

added = 0
for category, name, engine, description in NEW:
    cat = by_name[category]
    cat["entries"] = [e for e in cat["entries"]
                      if not (e["engine"] == engine and e.get("scale") is None)]
    cat["entries"].append({
        "category": category,
        "name": name,
        "engine": engine,
        "scale": None,
        "advanced": False,
        "description": description,
        "preview": "",
        "thumbnail": "",
    })
    added += 1

doc["category_count"] = len(cats)
doc["entry_count"] = sum(len(c["entries"]) for c in cats)
doc["engine_count"] = len({e["engine"] for c in cats for e in c["entries"]})
path.write_text(json.dumps(doc, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
print("batch 1: %d engines; catalogue now %d entries / %d engines / %d categories"
      % (added, doc["entry_count"], doc["engine_count"], doc["category_count"]))
