"""Catalogue entries for the engine expansion, batches 1 to 6.

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

    # ---- batch 2: engineering
    ("Fluid & Field Dynamics", "moody", "Moody Diagram",
     "Darcy friction factor against Reynolds number over the Colebrook-White roughness family"),
    ("Fluid & Field Dynamics", "pumpcurve", "Pump Performance Curve",
     "Head against flow with the system resistance curve and the duty point marked"),
    ("Materials & Chemistry", "stribeck", "Stribeck Curve",
     "Friction coefficient against Hersey number — boundary, mixed and hydrodynamic regimes"),
    ("Materials & Chemistry", "haigh", "Haigh Diagram",
     "Alternating against mean stress with the Goodman, Gerber and Soderberg envelopes"),
    ("Materials & Chemistry", "dadn", "Crack Growth Rate",
     "da/dN against dK on log-log axes, with the Paris exponent and coefficient fitted"),
    ("Materials & Chemistry", "strainlife", "Strain-Life Curve",
     "Strain amplitude against reversals to failure"),
    ("Materials & Chemistry", "larsonmiller", "Larson-Miller Curve",
     "Stress against the Larson-Miller parameter, collapsing creep-rupture tests onto one curve"),
    ("Advanced Line & Signal Plots", "campbell", "Campbell Diagram",
     "Natural frequencies against shaft speed with the engine-order rays"),
    ("Advanced Line & Signal Plots", "shaftorbit", "Shaft Orbit",
     "The journal centre's path from a pair of proximity probes — the shape is the diagnosis"),
    ("Spatial & Specialized", "plasticity", "Plasticity Chart",
     "Plasticity index against liquid limit over Casagrande's A-line and U-line"),
    ("Spatial & Specialized", "psd", "Particle Size Distribution",
     "Percent finer against grain size on a reversed log axis, with D10, D30 and D60"),
    ("Signals & RF", "nichols", "Nichols Chart",
     "Open-loop gain against phase, with the critical point at −180°, 0 dB"),
    ("Signals & RF", "polezero", "Pole-Zero Map",
     "Poles and zeros on the complex plane with the stability boundary"),
    ("Energy & Building Services", "pvnose", "P-V Nose Curve",
     "Bus voltage against transferred power, with the voltage-collapse point"),
    ("Aviation & Flight", "airfoilcp", "Airfoil Cp Distribution",
     "Pressure coefficient along the chord, y axis inverted as convention requires"),

    # ---- batch 3: chemistry, earth science, physics, medicine
    ("Materials & Chemistry", "lineweaver", "Lineweaver-Burk Plot",
     "1/v against 1/[S] with Vmax and Km read off the fitted line"),
    ("Materials & Chemistry", "eadie", "Eadie-Hofstee Plot",
     "v against v/[S] — the same constants with the error placed differently"),
    ("Materials & Chemistry", "haneswoolf", "Hanes-Woolf Plot",
     "[S]/v against [S] — the third rearrangement, and the best conditioned of them"),
    ("Materials & Chemistry", "scatchard", "Scatchard Plot",
     "Bound/free against bound; slope −1/Kd, x intercept the number of sites"),
    ("Spectroscopy & Chromatography", "vandeemter", "van Deemter Plot",
     "Plate height against velocity with the A + B/u + Cu fit and its optimum"),
    ("Materials & Chemistry", "bet", "BET Plot",
     "The BET transform over the 0.05–0.35 linear region, with monolayer capacity"),
    ("Materials & Chemistry", "jobplot", "Job Plot",
     "Signal against mole fraction; the maximum gives the complex stoichiometry"),
    ("Earth & Ocean Science", "gutenberg", "Gutenberg-Richter Plot",
     "Log cumulative event count against magnitude, with the b-value fitted"),
    ("Earth & Ocean Science", "doublemass", "Double-Mass Curve",
     "Cumulative station total against a reference; a slope change is an inhomogeneity"),
    ("Earth & Ocean Science", "hodograph", "Hodograph",
     "Wind u against v traced with height, with speed rings"),
    ("Clinical & Meta-analysis", "flowvolume", "Flow-Volume Loop",
     "Flow against volume as a closed loop — the shape is the diagnosis"),
    ("Clinical & Meta-analysis", "pvloop", "Pressure-Volume Loop",
     "Ventricular pressure against volume; the enclosed area is stroke work"),
    ("Signals & RF", "allan", "Allan Deviation",
     "Overlapping Allan deviation against averaging time on log-log axes"),
    ("Signals & RF", "paschen", "Paschen Curve",
     "Breakdown voltage against pressure × gap, with the Paschen minimum"),
    ("Spatial & Specialized", "phasefold", "Phase-Folded Light Curve",
     "Flux against phase after folding on a trial period, drawn over two cycles"),
    ("Spatial & Specialized", "ocdiagram", "O-C Diagram",
     "Observed minus computed event time against epoch, with the ephemeris fitted"),

    # ---- batch 4: model interpretation, diagnostics, decision analysis
    ("Model Evaluation", "pdp", "Partial Dependence Plot",
     "Average predicted response as one feature is varied"),
    ("Model Evaluation", "ice", "ICE Plot",
     "One curve per observation, with the partial dependence as their average"),
    ("Statistical & Diagnostic Plots", "influence", "Influence Plot",
     "Studentised residual against leverage, with the conventional cut-offs"),
    ("Statistical & Diagnostic Plots", "addedvar", "Added-Variable Plot",
     "Partial regression: the slope of this scatter is the coefficient in the full model"),
    ("Statistical & Diagnostic Plots", "interaction", "Interaction Plot",
     "Cell means across one factor, one line per level of a second"),
    ("Statistical Inference & Effect", "tornado", "Tornado Diagram",
     "Each input's low-to-high swing around the base case, sorted longest first"),
    ("Presentation & Comparison", "frontier", "Efficient Frontier",
     "Return against risk with the non-dominated frontier drawn through the cloud"),
    ("Time Series & Trend", "fanchart", "Fan Chart",
     "A forecast as nested probability bands widening into the future"),
    ("Time Series & Trend", "snailtrail", "Snail Trail",
     "Trailing risk against trailing return, joined in time order"),
    ("Clinical & Meta-analysis", "ceplane", "Cost-Effectiveness Plane",
     "Incremental cost against incremental effect with the willingness-to-pay ray"),
    ("Clinical & Meta-analysis", "ceac", "Acceptability Curve",
     "Probability an option is cost-effective, against willingness to pay"),
    ("Clinical & Meta-analysis", "lasagna", "Lasagna Plot",
     "A subject-by-time raster — the readable alternative to a spaghetti plot"),
    ("Clinical & Meta-analysis", "swimmer", "Swimmer Plot",
     "One lane per subject from start to stop, longest at the top"),

    # ---- batch 5: electrochemistry, bioprocess kinetics, energy, structures
    ("Electrochemical & Bioprocess", "tafel", "Tafel Plot",
     "Overpotential against log current density, each branch fitted for its slope and exchange current"),
    ("Electrochemical & Bioprocess", "cyclicvoltammogram", "Cyclic Voltammogram",
     "Current against potential as a swept loop, with the peak separation and current ratio"),
    ("Electrochemical & Bioprocess", "levich", "Levich Plot",
     "Limiting current against the square root of rotation rate, through the origin"),
    ("Electrochemical & Bioprocess", "koutecky", "Koutecky-Levich Plot",
     "Reciprocal current against reciprocal root rotation; the intercept is the kinetic current"),
    ("Electrochemical & Bioprocess", "randles", "Randles-Sevcik Plot",
     "Peak current against the square root of scan rate — diffusion control is a straight line"),
    ("Electrochemical & Bioprocess", "monod", "Monod Growth Curve",
     "Specific growth rate against substrate, with µmax and Ks fitted"),
    ("Electrochemical & Bioprocess", "haldane", "Substrate Inhibition Curve",
     "Haldane kinetics: growth rate rises, peaks and falls, with Ki and the optimum concentration"),
    ("Energy & Building Services", "ragone", "Ragone Plot",
     "Specific energy against specific power on log axes, over the constant-discharge-time diagonals"),
    ("Energy & Building Services", "ivcurve", "I-V Curve",
     "Photovoltaic current against voltage with the maximum-power rectangle and fill factor"),
    ("Energy & Building Services", "degreeday", "Degree-Day Signature",
     "Metered energy against outside temperature, with the balance point fitted by change-point search"),
    ("Energy & Building Services", "macc", "Abatement Cost Curve",
     "Measures cheapest first as blocks whose width is the saving and height the cost per unit"),
    ("Tensor & Structure Analysis", "mohr", "Mohr's Circle",
     "A stress state as a circle, with the principal stresses, maximum shear and principal angle"),
    ("Civil & Structural", "pminteraction", "P-M Interaction Diagram",
     "The axial-load and moment capacity envelope, closed, with the balanced point marked"),
    ("Civil & Structural", "pushover", "Pushover Capacity Curve",
     "Base shear against roof displacement with the equal-area bilinear idealisation and ductility"),
    ("Civil & Structural", "responsespectrum", "Response Spectrum",
     "Peak response against period on a log axis, one curve per damping ratio"),
    ("Civil & Structural", "consolidation", "Consolidation Curve",
     "Settlement against log time with Casagrande's construction and t50"),
    ("Spectral", "lombscargle", "Lomb-Scargle Periodogram",
     "Power against frequency for unevenly sampled data, which an FFT cannot take"),
    ("Categorical & Set Views", "waffle", "Waffle Chart",
     "A hundred squares apportioned by largest remainder, so the counts read exactly"),

    # ---- batch 6: regression diagnostics, signal structure, multivariate
    ("Statistical & Diagnostic Plots", "cooksdistance", "Cook's Distance Plot",
     "Influence by observation, with the 4/n screening threshold"),
    ("Statistical & Diagnostic Plots", "scalelocation", "Scale-Location Plot",
     "Root standardised residual against fitted value — flat means constant variance"),
    ("Statistical & Diagnostic Plots", "partialresidual", "Partial Residual Plot",
     "The residual with one predictor's contribution added back; curvature means the linear term is wrong"),
    ("Data Reduction & Interpolation", "savgol", "Savitzky-Golay Smoothing",
     "Polynomial smoothing over a fixed window, which keeps peak height and width"),
    ("Data Reduction & Interpolation", "lowesstrend", "LOWESS Trend",
     "A locally weighted trend through a scatter, adapting to uneven sampling"),
    ("Signal Processing Plots", "pacf", "Partial Autocorrelation",
     "Correlation at each lag with the shorter lags removed, by Durbin-Levinson"),
    ("Time Series & Trend", "stl", "Seasonal Decomposition",
     "Observed, trend, repeating cycle and remainder, with the period found or given"),
    ("Time Series & Trend", "subseries", "Seasonal Subseries Plot",
     "One line per position in the cycle, so drift within a season is visible"),
    ("Advanced Line & Signal Plots", "recurrence", "Recurrence Plot",
     "A mark wherever the record revisits an earlier state — diagonals mean deterministic structure"),
    ("Spectral & Signal Imaging", "scalogram", "Wavelet Scalogram",
     "Where the frequency content sits in time, by Morlet wavelet"),
    ("Spectral", "coherence", "Coherence Spectrum",
     "How much of one signal each frequency of another explains, averaged over Welch segments"),
    ("Signals & RF", "bode", "Bode Plot",
     "Loop gain and phase against frequency, with the 0 dB crossover"),
    ("Quality, Process & Reliability", "occurve", "Operating Characteristic Curve",
     "Probability a sampling plan accepts a lot, against how defective the lot is"),
    ("Electrochemical & Bioprocess", "cottrell", "Cottrell Plot",
     "Current against 1/sqrt(time); a straight line through the origin means diffusion control"),
    ("Electrochemical & Bioprocess", "coulombic", "Coulombic Efficiency Trend",
     "Efficiency by cycle with the cumulative mean and the trend across cycles"),
    ("Multivariate Plots", "biplot", "Biplot",
     "Observations and variables on one pair of principal components"),
    ("Multivariate Plots", "starglyph", "Star Glyph Plot",
     "One small radar per observation on a grid, so shapes can be matched by eye"),
    ("Scatter & Bubble Charts", "sunflower", "Sunflower Plot",
     "A petal per observation where points coincide, so density shows without hiding points"),
]

by_name = {c["name"]: c for c in cats}
missing = sorted({c for c, *_ in NEW if c not in by_name})
if missing:
    raise SystemExit("no such categories: %s" % ", ".join(missing))

added = 0
for category, name, engine, description in NEW:
    cat = by_name[category]
    # Replaced IN PLACE when the entry is already there, appended only when it
    # is new. Removing and re-appending would have been simpler and was wrong:
    # add_scale_variants.py appends an engine's variants to the end of the
    # category, so a rerun of this script moved the base entry down PAST its own
    # variants and the file changed again - the pipeline needed two passes to
    # stop moving, which is not what idempotent means. Nothing was lost by it;
    # a catalogue that differs from itself is just impossible to review.
    at = next((i for i, e in enumerate(cat["entries"])
               if e["engine"] == engine and e.get("scale") is None), None)
    entry = {
        "category": category,
        "name": name,
        "engine": engine,
        "scale": None,
        "advanced": False,
        "description": description,
        "preview": "",
        "thumbnail": "",
    }
    if at is None:
        cat["entries"].append(entry)
    else:
        cat["entries"][at] = entry
    added += 1

doc["category_count"] = len(cats)
doc["entry_count"] = sum(len(c["entries"]) for c in cats)
doc["engine_count"] = len({e["engine"] for c in cats for e in c["entries"]})
path.write_text(json.dumps(doc, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
print("batches 1-6: %d engines; catalogue now %d entries / %d engines / %d categories"
      % (added, doc["entry_count"], doc["engine_count"], doc["category_count"]))
