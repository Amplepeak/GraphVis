# =========================================================================
# graph_library.py - complete GraphVis graph catalogue and fuzzy lookup.
#
# Every catalogue entry maps to a concrete GraphVis rendering engine. Entries
# can still be disabled by the compatibility checker when the active dataset
# lacks the required dimensionality/data model; advanced entries are never
# disabled merely because they are advanced.
# =========================================================================
from __future__ import annotations

from difflib import SequenceMatcher

GRAPH_CATEGORIES = {
    "Line Plots": [
        ("plot", "Line Chart", None, False, "2-D line plot", "╱╲__╱"),
        ("plot3", "3D Line", None, False, "3-D line trajectory", "↗⤴"),
        ("stairs", "Stairs", None, False, "Step/stair plot", "_┌─┐_"),
        ("errorbar", "Error Bar", None, False, "Line/markers with uncertainty bars", "•│•│•"),
        ("area", "Area", None, False, "Filled area under a curve", "▁▃▆█"),
        ("stackedplot", "Stacked Lines", None, False, "Multiple numeric series stacked vertically", "≈≈≈"),
        ("loglog", "Line Chart", "Logarithmic Scale", False, "Line plot with logarithmic X and Y axes", "log↗log"),
        ("semilogx", "Line Chart", "Semi-Log X", False, "Line plot with logarithmic X axis", "logx↗"),
        ("semilogy", "Line Chart", "Semi-Log Y", False, "Line plot with logarithmic Y axis", "↗logy"),
        ("fplot", "Function Plot", None, True, "Evaluate and draw y=f(x) from a symbolic expression", "f(x)"),
        ("fplot3", "Function 3D Parametric", None, True, "Evaluate x(t);y(t);z(t) from symbolic expressions", "f(t)³"),
        ("fimplicit", "Implicit Function", None, True, "Plot the zero-level contour F(x,y)=0", "F=0"),
        ("fimplicit3", "Implicit Surface", None, True, "Plot the zero-level isosurface F(x,y,z)=0", "F=0³"),
    ],
    "Scatter & Bubble Charts": [
        ("scatter", "4D / 5D Scatter", None, False, "2-D scatter with optional colour/size dimensions", "· • ·•"),
        ("scatter3", "3D Scatter", None, False, "3-D scatter cloud", "·↗•"),
        ("bubblechart", "4D / 5D Scatter", None, False, "Scatter where the fifth variable controls marker size", "o O ◎"),
        ("bubblechart3", "3D Bubble", None, True, "3-D scatter with bubble size mapping", "o↗O"),
        ("swarmchart", "Swarm", None, False, "Jittered distribution scatter", "••••"),
        ("swarmchart3", "3D Swarm", None, True, "Jittered 3-D distribution scatter", "••³"),
        ("binscatter", "Hexbin Density", None, False, "Binned 2-D density/mean relationship", "⬢⬡⬢"),
        ("plotmatrix", "Plot Matrix", None, True, "Pairwise scatter matrix for numeric variables", "▦"),
    ],
    "Data Distribution Plots": [
        ("histogram", "Histogram", None, False, "1-D frequency distribution", "▂▅█▆▃"),
        ("histogram2", "2D Histogram", None, False, "2-D binned frequency map", "▦"),
        ("scatterhistogram", "Scatter + Marginals", None, True, "Scatter plot with marginal histograms", "▥•▤"),
        ("boxchart", "Box Plot", None, False, "Box-and-whisker distribution summary", "—▣—"),
        ("violinplot", "Violin Plot", None, False, "Kernel-density distribution shape", "◖◗"),
        ("raincloudplot", "Raincloud", None, True, "Violin + box + jittered observations", "☁••"),
        ("pareto", "Pareto Front", None, False, "Non-dominated performance frontier", "⌜↗"),
    ],
    "Discrete Data Plots": [
        ("bar", "Bar", None, False, "Vertical bars", "▂▅█"),
        ("barh", "Horizontal Bar", None, False, "Horizontal bars", "▰▰"),
        ("bar3", "3D Bar", None, True, "3-D bars using X/Y/Z mappings", "▥³"),
        ("bar3h", "3D Horizontal Bar", None, True, "3-D horizontal bars", "▰³"),
        ("stem", "Stem", None, False, "Discrete stem plot", "│•│•"),
        ("stem3", "3D Stem", None, True, "3-D stem plot", "│•³"),
        ("stairs", "Stairs", None, False, "Step/stair plot", "_┌─┐_"),
        ("pie", "Pie", None, False, "Part-to-whole pie chart", "◔"),
        ("piechart", "Pie", None, False, "Part-to-whole pie chart", "◕"),
        ("donutchart", "Donut", None, False, "Part-to-whole donut chart", "◎"),
        ("wordcloud", "Word Cloud", None, True, "Word-frequency cloud from text/categorical fields", "Aa Bb"),
        ("bubblecloud", "Bubble Cloud", None, True, "Categorical bubbles sized by numeric weights", "oOo"),
        ("heatmap", "2D Heatmap", None, False, "Interpolated response heatmap", "▦"),
        ("parallelplot", "Parallel Coordinates", None, True, "Parallel-coordinate view of numeric variables", "╲│╱│"),
        ("spy", "Spy Matrix", None, True, "Matrix sparsity/nonzero pattern", "·▪··"),
    ],
    "Geographic Plots": [
        ("geoplot", "Geo Line", None, True, "Latitude/longitude line plot", "⌖╱"),
        ("geoscatter", "Geo Scatter", None, True, "Latitude/longitude scatter plot", "⌖•"),
        ("geodensityplot", "Geo Density", None, True, "Latitude/longitude density plot", "⌖▒"),
        ("geobubble", "Geo Bubble", None, True, "Latitude/longitude bubble plot", "⌖O"),
        ("geobubblechart", "Geo Bubble", None, True, "Latitude/longitude bubble plot", "⌖O"),
    ],
    "Polar Plots": [
        ("polarplot", "Polar Line", None, False, "Polar line: angle versus radius", "◌↗"),
        ("polarhistogram", "Polar Histogram", None, True, "Angular histogram", "✺"),
        ("polarscatter", "Polar Scatter", None, False, "Polar scatter: angle versus radius", "◌•"),
        ("polarbubblechart", "Polar Bubble", None, True, "Polar scatter with bubble-size mapping", "◌O"),
        ("compass", "Compass", None, True, "Polar vector arrows", "↗↘"),
        ("rose", "Polar Histogram", None, True, "Rose/angular histogram", "✾"),
    ],
    "Contour Plots": [
        ("contour", "2D Contour", None, False, "Contour lines over an interpolated field", "≋≋"),
        ("contourf", "2D Contour", None, False, "Filled contour map", "▧"),
        ("contour3", "3D Contour", None, True, "3-D contour projection", "≋³"),
        ("fcontour", "Function Contour", None, True, "Contour a symbolic f(x,y) expression", "f≋"),
        ("pcolor", "2D Heatmap", None, False, "Pseudocolour field", "▦"),
    ],
    "Vector Fields": [
        ("quiver", "Quiver Field", None, True, "2-D vector arrows from mapped fields", "↗↘↖"),
        ("quiver3", "3D Quiver", None, True, "3-D U/V/W vector arrows", "↗³"),
        ("feather", "Feather", None, True, "Vector sequence anchored on a 1-D baseline", "➤➤"),
        ("streamplot", "Stream Field", None, True, "2-D streamlines through a vector field", "〰➜"),
        ("streamline", "Stream Field", None, True, "2-D streamlines through a vector field", "〰➜"),
        ("streamslice", "Stream Field", None, True, "Streamlines on a planar field slice", "≈➜"),
        ("streamparticles", "Stream Particles", None, True, "Animated particles advancing along a mapped path", "••➜"),
    ],
    "Surface & Mesh Plots": [
        ("surf", "3D Topography / Surface", None, False, "Opaque 3-D response surface", "▱³"),
        ("surfc", "Surface + Contours", None, True, "3-D surface with contour projection", "▱≋"),
        ("surfl", "3D Topography / Surface", None, True, "Lit/shaded 3-D surface", "▱☀"),
        ("mesh", "3D Mesh", None, False, "Wireframe 3-D response mesh", "#³"),
        ("meshc", "Surface + Contours", None, True, "Wireframe mesh with contour projection", "#≋"),
        ("meshz", "3D Mesh", None, True, "Wireframe mesh with base curtain", "#⌟"),
        ("waterfall", "Waterfall", None, True, "Waterfall lines across a response surface", "≋≋³"),
        ("ribbon", "Ribbon", None, True, "Ribbon slices through a response surface", "▥"),
        ("fsurf", "Function Surface", None, True, "3-D surface z=f(x,y) from a symbolic expression", "f▱"),
        ("fmesh", "Function Mesh", None, True, "3-D wire mesh z=f(x,y) from a symbolic expression", "f#"),
    ],
    "Volume Visualization": [
        ("isosurface", "Isosurface", None, True, "Marching-cubes surface from a 3-D scalar volume", "◫³"),
        ("isocaps", "Isocaps", None, True, "Isosurface with boundary cap contours", "◫⌁"),
        ("isonormals", "Isonormals", None, True, "Isosurface with sampled surface normals", "◫↗"),
        ("volshow", "Volume Show", None, True, "Opacity-mapped voxel volume rendering", "▣³"),
        ("patch", "Patch", None, True, "Triangular patch mesh from topology or XYZ points", "△▱"),
        ("slice", "Volume Slice", None, True, "Orthogonal scalar-volume slices", "▤³"),
        ("contourslice", "Contour Slice", None, True, "Contour lines on orthogonal volume slices", "≋▤"),
        ("streamribbon", "Stream Ribbon", None, True, "Ribbon-like 3-D trajectories from U/V/W fields", "〰▰"),
        ("streamtube", "Stream Tube", None, True, "Tube-like 3-D trajectories from U/V/W fields", "〰◯"),
        ("coneplot", "Cone Plot", None, True, "3-D direction/magnitude cone-style vector field", "▲→"),
    ],
    "Animation & Dynamic Display": [
        ("animatedline", "Animated Line", None, True, "Time-progressive line playback", "↗▶"),
        ("comet", "Comet", None, True, "2-D moving head with fading trail", "☄"),
        ("comet3", "Comet 3D", None, True, "3-D moving head with fading trail", "☄³"),
    ],
}


# Additional publication/statistical engines. These are concrete renderers, not
# aliases, bringing the GraphVis catalogue beyond 100 distinct plot types.
GRAPH_CATEGORIES.setdefault("Advanced Line & Signal Plots", []).extend([
    ("stepmid", "Step Mid", None, True, "Midpoint step plot", "_┐_┌_"),
    ("connectedscatter", "Connected Scatter", None, True, "Scatter observations connected in acquisition order", "•─•╱•"),
    ("lollipop", "Lollipop", None, True, "Stem-like lollipop chart", "○│○│"),
    ("fillbetween", "Fill Between", None, True, "Fill the region between two mapped series", "≈▒≈"),
    ("eventplot", "Event Plot", None, True, "Event/raster plot for event positions", "│ ││  │"),
    ("cumsum", "Cumulative Sum", None, True, "Cumulative response versus X", "↗Σ"),
    ("rollingmean", "Rolling Mean", None, True, "Rolling-mean trend", "≈μ"),
    ("rollingmedian", "Rolling Median", None, True, "Robust rolling-median trend", "≈med"),
    ("movingstd", "Moving Std", None, True, "Moving standard-deviation envelope", "σ≈"),
    ("derivative", "Derivative", None, True, "Numerical dY/dX", "dy/dx"),
    ("integral", "Integral", None, True, "Cumulative trapezoidal integral", "∫"),
    ("slopegraph", "Slope Graph", None, True, "Compare paired endpoints", "╱╲"),
    ("dotplot", "Dot Plot", None, True, "One-dimensional dot/rank plot", "••••"),
])
GRAPH_CATEGORIES.setdefault("Statistical & Diagnostic Plots", []).extend([
    ("kde", "KDE Density", None, True, "Kernel-density estimate", "⌒"),
    ("ecdf", "ECDF", None, True, "Empirical cumulative distribution", "└─┐"),
    ("cumhist", "Cumulative Histogram", None, True, "Cumulative histogram", "▂▅█"),
    ("qqplot", "Q-Q Plot", None, True, "Normal quantile-quantile diagnostic", "•╱•"),
    ("probplot", "Probability Plot", None, True, "Normal probability plot", "P↗"),
    ("ridgeline", "Ridgeline", None, True, "Stacked density ridges for numeric series", "≋≋≋"),
    ("stripplot", "Strip Plot", None, True, "Jittered strip distribution", "••••"),
    ("beeswarm", "Beeswarm", None, True, "Density-aware jittered swarm", "••••"),
    ("corrmatrix", "Correlation Matrix", None, True, "Numeric correlation matrix", "▦ρ"),
    ("covmatrix", "Covariance Matrix", None, True, "Numeric covariance matrix", "▦Σ"),
    ("residualplot", "Residual Plot", None, True, "Residuals around a fitted trend", "•—•"),
    ("blandaltman", "Bland-Altman", None, True, "Agreement plot: mean versus difference", "±Δ"),
    ("calibrationplot", "Calibration Plot", None, True, "Observed versus predicted calibration", "•╱"),
    ("confidenceellipse", "Confidence Ellipse", None, True, "Scatter with covariance confidence ellipse", "◯•"),
])
GRAPH_CATEGORIES.setdefault("Signal Processing Plots", []).extend([
    ("spectrogram", "Spectrogram", None, True, "Short-time Fourier spectrogram", "▦ƒ"),
    ("psd", "Power Spectral Density", None, True, "Welch power spectral density", "PSD"),
    ("autocorr", "Autocorrelation", None, True, "Autocorrelation versus lag", "Rxx"),
    ("crosscorr", "Cross Correlation", None, True, "Cross-correlation between X/Y series", "Rxy"),
    ("lagplot", "Lag Plot", None, True, "Y(t) versus Y(t-lag)", "t↔t-1"),
])
GRAPH_CATEGORIES.setdefault("Multivariate Plots", []).extend([
    ("radar", "Radar Chart", None, True, "Radial comparison of normalized numeric variables", "✳"),
    ("andrews", "Andrews Curves", None, True, "Andrews multivariate curves", "∿∿"),
    ("ternary", "Ternary Scatter", None, True, "Three-component simplex scatter", "△•"),
])
GRAPH_CATEGORIES.setdefault("Publication & Statistical Plots", []).extend([
    ("forest", "Forest Plot", None, False, "Effect estimates with confidence intervals", "—◆—"),
    ("volcano", "Volcano Plot", None, False, "Effect size versus -log10 significance", "∧••"),
    ("roc", "ROC Curve", None, False, "Receiver-operating characteristic with AUC", "⌜╱"),
    ("controlchart", "Control Chart", None, True, "Process series with mean and 3σ control limits", "≈┄"),
    ("manhattan", "Manhattan Plot", None, True, "Genomic/ordered position versus -log10 p-value", "▁▃▇▂"),
    ("populationpyramid", "Population Pyramid", None, True, "Mirrored paired horizontal bars", "◀▶"),
    ("treemap", "Treemap", None, True, "Hierarchical/weighted rectangle composition", "▦"),
    ("sunburst", "Sunburst", None, True, "Nested categorical composition", "◎◉"),
    ("sankey", "Sankey Diagram", None, True, "Weighted flow diagram", "⇒⇢"),
    ("venn", "Venn Diagram", None, True, "Set-overlap summary", "◯◯"),
])
GRAPH_CATEGORIES.setdefault("Spatial & Specialized", []).extend([
    ("windrose", "Wind Rose", None, True, "Direction-frequency polar histogram", "✺"),
    ("spider", "Radar Chart", None, True, "Spider/radar multivariate chart", "✳"),
    ("radial", "Polar Line", None, True, "Radial/polar line plot", "◌"),
    ("piper", "Piper Diagram", None, True, "Hydrochemical trilinear diagram", "◇△"),
    ("pie-map", "Geo Bubble", None, True, "Map-positioned proportional-symbol view", "⌖◔"),
    ("bar-map", "Geo Bubble", None, True, "Map-positioned magnitude symbols", "⌖▥"),
])



# Extended catalogue aliases and specialist views.  These entries deliberately
# reuse tested concrete renderer engines where the visual grammar is the same
# (for example a step-response plot uses the existing Stairs engine).  This
# gives users the terminology they expect from MATLAB/Origin/Prism/engineering
# workflows without duplicating rendering code or creating fake backends.
GRAPH_CATEGORIES.setdefault("Time Series & Trend", []).extend([
    ("time-series line", "Line Chart", None, False, "Chronological Y versus time/X line", "t↗"),
    ("step response", "Stairs", None, False, "Step response / zero-order-hold trajectory", "_┌─┐"),
    ("impulse response", "Stem", None, True, "Discrete impulse-response stems", "│•│•"),
    ("running total", "Cumulative Sum", None, True, "Running cumulative total", "Σ↗"),
    ("moving average", "Rolling Mean", None, True, "Rolling mean trend", "μ≈"),
    ("moving median", "Rolling Median", None, True, "Rolling median trend", "med≈"),
    ("moving variability", "Moving Std", None, True, "Moving standard deviation envelope", "σ≈"),
    ("rate of change", "Derivative", None, True, "Numerical rate of change", "dy/dt"),
    ("cumulative integral", "Integral", None, True, "Cumulative trapezoidal integral", "∫"),
    ("event raster", "Event Plot", None, True, "Event timing/raster visualization", "│ ││"),
    ("trend band", "Fill Between", None, True, "Trend with an uncertainty/envelope band", "≈▒≈"),
    ("paired slope", "Slope Graph", None, True, "Paired before/after slope graph", "╱╲"),
    ("connected observations", "Connected Scatter", None, True, "Observations connected in acquisition order", "•─•"),
    ("horizon-style trend", "Area", None, True, "Compact filled time-series trend", "▁▃▆"),
    ("stacked time series", "Stacked Lines", None, True, "Multiple aligned time-series panels", "≈≈≈"),
    ("log time series", "Line Chart", "Semi-Log Y", True, "Time-series with logarithmic response", "t↗log"),
    ("log-log kinetics", "Line Chart", "Logarithmic Scale", True, "Log-log kinetic/power-law view", "log↗log"),
    ("change-point overview", "Derivative", None, True, "Derivative view highlighting abrupt transitions", "Δ↗"),
])

GRAPH_CATEGORIES.setdefault("Spectroscopy & Chromatography", []).extend([
    ("spectrum", "Line Chart", None, False, "Generic intensity spectrum", "⌒⌒"),
    ("mass spectrum", "Stem", None, False, "Mass-to-charge peaks as stems", "│•│•"),
    ("chromatogram", "Line Chart", None, False, "Signal intensity versus retention time", "⌒⌒"),
    ("absorbance spectrum", "Line Chart", None, False, "Absorbance versus wavelength/frequency", "A(λ)"),
    ("emission spectrum", "Line Chart", None, False, "Emission intensity spectrum", "I(λ)"),
    ("raman spectrum", "Line Chart", None, False, "Raman intensity versus shift", "I(Δν)"),
    ("ftir spectrum", "Line Chart", None, False, "FTIR absorbance/transmittance spectrum", "IR⌒"),
    ("xrd pattern", "Line Chart", None, False, "Diffraction intensity versus 2θ", "I(2θ)"),
    ("peak-stick spectrum", "Stem", None, True, "Stick representation of detected peaks", "│││"),
    ("spectral heatmap", "2D Heatmap", None, True, "Spectral intensity as a two-dimensional field", "▦λ"),
    ("spectral contour", "2D Contour", None, True, "Contour view of a spectral field", "≋λ"),
    ("waterfall spectra", "Waterfall", None, True, "Offset family of spectra", "≋≋³"),
    ("stacked spectra", "Stacked Lines", None, True, "Multiple spectra in vertically stacked panels", "≈≈"),
    ("peak area view", "Integral", None, True, "Cumulative/peak integration view", "∫peak"),
    ("peak derivative", "Derivative", None, True, "Derivative spectroscopy view", "dI/dx"),
])

GRAPH_CATEGORIES.setdefault("Statistical Inference & Effect", []).extend([
    ("effect-size forest", "Forest Plot", None, False, "Effect estimates and confidence intervals", "—◆—"),
    ("caterpillar plot", "Forest Plot", None, True, "Sorted estimates with confidence intervals", "—◆—"),
    ("funnel plot", "Volcano Plot", None, True, "Effect versus precision-style diagnostic", "▽••"),
    ("mean confidence plot", "Error Bar", None, False, "Means with confidence/error bars", "•│•"),
    ("median interval plot", "Error Bar", None, True, "Median estimates with intervals", "◆│"),
    ("estimation plot", "Bland-Altman", None, True, "Difference/estimation style comparison", "Δ•"),
    ("agreement plot", "Bland-Altman", None, False, "Bland-Altman agreement view", "±Δ"),
    ("normal Q-Q", "Q-Q Plot", None, False, "Quantile-quantile normality diagnostic", "•╱•"),
    ("P-P probability", "Probability Plot", None, True, "Probability diagnostic plot", "P↗"),
    ("empirical CDF", "ECDF", None, False, "Empirical cumulative distribution", "└─┐"),
    ("density curve", "KDE Density", None, False, "Kernel-density estimate", "⌒"),
    ("cumulative frequency", "Cumulative Histogram", None, True, "Cumulative histogram/frequency", "▂▅█"),
    ("half violin", "Violin Plot", None, True, "Compact violin distribution view", "◖"),
    ("boxen-style summary", "Box Plot", None, True, "Quantile-rich box-style summary", "▣▣"),
    ("jitter plot", "Strip Plot", None, True, "Jittered individual observations", "••••"),
    ("beeswarm distribution", "Beeswarm", None, True, "Density-aware observation swarm", "••••"),
    ("raincloud distribution", "Raincloud", None, False, "Violin + box + points", "☁••"),
    ("volcano significance", "Volcano Plot", None, False, "Effect versus -log10 significance", "∧••"),
    ("ROC diagnostic", "ROC Curve", None, False, "Receiver-operating characteristic", "⌜╱"),
    ("calibration curve", "Calibration Plot", None, True, "Observed versus predicted calibration", "•╱"),
    ("residual diagnostic", "Residual Plot", None, True, "Residuals around fitted trend", "•—•"),
    ("confidence ellipse scatter", "Confidence Ellipse", None, True, "Scatter with covariance ellipse", "◯•"),
])

GRAPH_CATEGORIES.setdefault("Matrix, Correlation & Multivariate", []).extend([
    ("correlogram", "Correlation Matrix", None, False, "Correlation matrix heatmap", "▦ρ"),
    ("covariance map", "Covariance Matrix", None, True, "Covariance matrix heatmap", "▦Σ"),
    ("pair plot", "Plot Matrix", None, True, "Pairwise scatter/histogram matrix", "▦•"),
    ("scatter matrix", "Plot Matrix", None, True, "Pairwise scatter matrix", "▦•"),
    ("parallel coordinates", "Parallel Coordinates", None, True, "Parallel-coordinate multivariate view", "╲│╱"),
    ("radar multivariate", "Radar Chart", None, True, "Normalized radial comparison", "✳"),
    ("spider multivariate", "Radar Chart", None, True, "Spider/radar variable comparison", "✳"),
    ("Andrews curves", "Andrews Curves", None, True, "Andrews multivariate curves", "∿∿"),
    ("PCA-style scores", "4D / 5D Scatter", None, True, "Score-like multivariate scatter", "PC•"),
    ("PCA-style loadings", "Quiver Field", None, True, "Loading-vector style arrows", "PC↗"),
    ("cluster heatmap", "2D Heatmap", None, True, "Matrix heatmap for clustered variables/samples", "▦"),
    ("distance matrix", "2D Heatmap", None, True, "Pairwise-distance matrix", "▦d"),
    ("confusion matrix", "2D Heatmap", None, True, "Classification confusion matrix", "▦C"),
    ("sparsity matrix", "Spy Matrix", None, True, "Nonzero/sparsity pattern", "·▪··"),
    ("simplex composition", "Ternary Scatter", None, True, "Three-component ternary composition", "△•"),
])

GRAPH_CATEGORIES.setdefault("Quality, Process & Reliability", []).extend([
    ("Shewhart chart", "Control Chart", None, False, "Process values with control limits", "≈┄"),
    ("individuals control chart", "Control Chart", None, True, "Individuals process-control chart", "I┄"),
    ("run chart", "Line Chart", None, False, "Process metric in run order", "run↗"),
    ("process capability histogram", "Histogram", None, True, "Process distribution/capability view", "▂▅█"),
    ("quality Pareto", "Pareto Front", None, False, "Ranked/upper-bound Pareto view", "⌜↗"),
    ("variability plot", "Box Plot", None, True, "Grouped variability summary", "—▣—"),
    ("multi-vari plot", "Stacked Lines", None, True, "Multiple factor/series variation view", "≈≈≈"),
    ("reliability survival", "ECDF", None, True, "Cumulative reliability/failure distribution", "R(t)"),
    ("failure-rate trend", "Derivative", None, True, "Rate-of-change/failure-rate view", "λ(t)"),
    ("cumulative failures", "Cumulative Sum", None, True, "Cumulative failure count", "Σfail"),
    ("process density", "KDE Density", None, True, "Smooth process distribution", "⌒"),
    ("process correlation", "Connected Scatter", None, True, "Connected relationship in process order", "•─•"),
])

GRAPH_CATEGORIES.setdefault("Flow, Network & Composition", []).extend([
    ("alluvial flow", "Sankey Diagram", None, True, "Alluvial weighted-flow view", "⇒⇢"),
    ("flow diagram", "Sankey Diagram", None, True, "Weighted source-to-target flow", "⇒"),
    ("energy flow", "Sankey Diagram", None, True, "Energy/material flow balance", "E⇒"),
    ("material balance flow", "Sankey Diagram", None, True, "Mass-balance flow diagram", "m⇒"),
    ("hierarchical treemap", "Treemap", None, True, "Weighted hierarchical rectangles", "▦"),
    ("nested sunburst", "Sunburst", None, True, "Nested hierarchical composition", "◎◉"),
    ("set overlap", "Venn Diagram", None, True, "Set-overlap summary", "◯◯"),
    ("composition pie", "Pie", None, False, "Part-to-whole pie chart", "◔"),
    ("composition donut", "Donut", None, False, "Part-to-whole donut chart", "◎"),
    ("bubble composition", "Bubble Cloud", None, True, "Weighted bubble composition", "oOo"),
    ("word frequency", "Word Cloud", None, True, "Word-frequency cloud", "Aa Bb"),
])

GRAPH_CATEGORIES.setdefault("Advanced Spatial & GIS", []).extend([
    ("latitude-longitude line", "Geo Line", None, True, "Coordinate-based geographic line", "⌖╱"),
    ("latitude-longitude scatter", "Geo Scatter", None, True, "Coordinate-based geographic scatter", "⌖•"),
    ("spatial density", "Geo Density", None, True, "Geographic density map", "⌖▒"),
    ("proportional symbol map", "Geo Bubble", None, True, "Geographic bubble/proportional symbols", "⌖O"),
    ("point map", "Geo Scatter", None, True, "Geographic point map", "⌖·"),
    ("track map", "Geo Line", None, True, "Geographic trajectory/track", "⌖↗"),
    ("map heat field", "2D Heatmap", None, True, "Coordinate heat field", "⌖▦"),
    ("map contour field", "2D Contour", None, True, "Coordinate contour field", "⌖≋"),
    ("direction rose", "Wind Rose", None, True, "Direction-frequency wind rose", "✺"),
    ("radial direction plot", "Polar Histogram", None, True, "Angular frequency/radial distribution", "✾"),
    ("hydrochemical Piper", "Piper Diagram", None, True, "Piper hydrochemical trilinear view", "◇△"),
    ("geospatial bubble layer", "Geo Bubble", None, True, "Magnitude-encoded geographic layer", "⌖O"),
])

GRAPH_CATEGORIES.setdefault("Engineering 3D & Field", []).extend([
    ("3D trajectory", "3D Line", None, False, "Three-dimensional trajectory", "↗⤴"),
    ("3D point cloud", "3D Scatter", None, False, "Three-dimensional point cloud", "·↗•"),
    ("3D bubble cloud", "3D Bubble", None, True, "3-D bubble-size mapped scatter", "o↗O"),
    ("3D response surface", "3D Topography / Surface", None, False, "Interpolated XYZ response surface", "▱³"),
    ("3D wireframe", "3D Mesh", None, True, "Wireframe response mesh", "#³"),
    ("surface with contours", "Surface + Contours", None, True, "Surface plus base contours", "▱≋"),
    ("3D waterfall field", "Waterfall", None, True, "Waterfall slices through response surface", "≋≋³"),
    ("3D ribbon field", "Ribbon", None, True, "Ribbon response slices", "▥³"),
    ("3D vector arrows", "3D Quiver", None, True, "U/V/W vector arrows", "↗³"),
    ("3D stream tube", "Stream Tube", None, True, "Tube-like vector trajectories", "〰◯"),
    ("3D stream ribbon", "Stream Ribbon", None, True, "Ribbon-like vector trajectories", "〰▰"),
    ("3D cone field", "Cone Plot", None, True, "Cone-style direction/magnitude vectors", "▲→"),
    ("scalar isosurface", "Isosurface", None, True, "Constant-value surface from scalar volume", "◫³"),
    ("orthogonal volume slices", "Volume Slice", None, True, "Orthogonal scalar-volume slices", "▤³"),
    ("voxel volume", "Volume Show", None, True, "Opacity-mapped voxel volume", "▣³"),
    ("surface normals", "Isonormals", None, True, "Isosurface with normal vectors", "◫↗"),
])

GRAPH_CATEGORIES.setdefault("Electrochemical & Bioprocess", []).extend([
    ("polarization curve", "Polarisation & Power Curve", None, False, "Cell voltage/current with power density", "V-I"),
    ("power density curve", "Polarisation & Power Curve", None, False, "Power-density performance curve", "P-I"),
    ("Nyquist impedance", "EIS: Nyquist", None, False, "Complex-plane impedance arc", "Z′◡Z″"),
    ("Bode impedance", "EIS: Bode", None, False, "Magnitude/phase versus frequency", "|Z|ƒ"),
    ("Gompertz hydrogen kinetics", "Gompertz H₂ Kinetics", None, False, "Modified Gompertz cumulative-H2 kinetics", "H₂⌒"),
    ("sCOD profile", "sCOD Degradation Profile", None, False, "sCOD degradation trajectory/profile", "COD↘"),
    ("VFA profile", "VFA Concentration Profile", None, False, "VFA concentration profile", "VFA↗"),
    ("global sensitivity map", "Global Sensitivity", None, False, "SRC and Spearman sensitivity view", "SRC"),
    ("marginal response panel", "1D Marginal Responses", None, False, "Robust parameter-response marginals", "μ±"),
    ("performance envelope", "Performance Ceiling", None, False, "Upper attainable performance envelope", "⌜"),
    ("operating Pareto front", "Pareto Front", None, False, "Non-dominated operating frontier", "⌜↗"),
    ("parameter heatmap", "2D Heatmap", None, False, "Response field over two parameters", "▦"),
    ("parameter contour", "2D Contour", None, False, "Response contours over two parameters", "≋"),
])

GRAPH_CATEGORIES.setdefault("Presentation & Comparison", []).extend([
    ("grouped columns", "Bar", None, False, "Grouped/vertical comparison bars", "▂▅█"),
    ("horizontal comparison", "Horizontal Bar", None, False, "Horizontal comparison bars", "▰▰"),
    ("lollipop comparison", "Lollipop", None, True, "Lollipop magnitude comparison", "○│○"),
    ("dot comparison", "Dot Plot", None, True, "Compact dot/rank comparison", "••••"),
    ("before-after", "Slope Graph", None, True, "Before/after paired comparison", "╱╲"),
    ("small multiples", "Stacked Lines", None, True, "Aligned small-multiple line panels", "≈≈≈"),
    ("filled comparison", "Area", None, True, "Filled area comparison", "▁▃▆"),
    ("ranked Pareto", "Pareto Front", None, True, "Ranked performance frontier", "⌜↗"),
    ("scatter with marginals", "Scatter + Marginals", None, True, "Scatter with marginal distributions", "▥•▤"),
    ("density scatter", "Hexbin Density", None, True, "High-density relationship view", "⬢⬡"),
    ("bubble comparison", "4D / 5D Scatter", None, True, "Bubble-size/colour encoded comparison", "o O ◎"),
    ("parallel comparison", "Parallel Coordinates", None, True, "Multivariate parallel-axis comparison", "╲│╱"),
])

# =====================================================================
# GraphVis 16.3 scientific research-suite expansion.
# Five genuinely new derivative-field engines (Vorticity Map, Divergence
# Map, Phase Portrait, Flow Texture (LIC), Tensor Glyph Field) plus a broad
# set of domain-terminology styles routed onto validated engines, following
# the library's established alias architecture.
# =====================================================================
GRAPH_CATEGORIES.setdefault("Fluid & Field Dynamics", []).extend([
    ("vorticity map", "Vorticity Map", None, True, "Curl of the response gradient: rotational structure of the field", "↻↺"),
    ("swirl strength", "Vorticity Map", None, True, "Signed swirl intensity with flow arrows", "↻±"),
    ("divergence map", "Divergence Map", None, True, "Sources and sinks of the response gradient field", "✳"),
    ("source-sink field", "Divergence Map", None, True, "Positive/negative flux regions of the field", "◉◎"),
    ("phase portrait", "Phase Portrait", None, True, "Streamlines with u/v nullclines of the gradient system", "∮"),
    ("dynamical nullclines", "Phase Portrait", None, True, "Nullcline intersections locating stationary points", "✕∮"),
    ("flow texture", "Flow Texture (LIC)", None, True, "Line-integral-convolution flow imaging over the response", "≈≋"),
    ("LIC streaks", "Flow Texture (LIC)", None, True, "Dense directional streak texture of the vector field", "〰"),
    ("velocity vectors", "Quiver Field", None, True, "Discrete velocity/force arrows over the domain", "↗↘"),
    ("flow streamlines", "Stream Field", None, True, "Continuous streamlines of the gradient flow", "S≈"),
    ("stream function view", "Stream Field", None, True, "Streamline topology of a 2-D flow", "ψ≈"),
    ("3D velocity field", "3D Quiver", None, True, "Volumetric velocity arrows (u, v, w components)", "⇗⇘"),
    ("3D streamtubes", "Stream Tube", None, True, "Flux-carrying tubes along 3-D streamlines", "◎≈"),
    ("3D stream ribbons", "Stream Ribbon", None, True, "Twist-revealing ribbons along 3-D streamlines", "~≋"),
    ("cone velocity glyphs", "Cone Plot", None, True, "Direction cones for 3-D vector data", "▲▲"),
    ("advected particles", "Stream Particles", None, True, "Animated particles advected by the field", "···→"),
    ("gradient magnitude", "2D Heatmap", None, True, "Scalar magnitude of the response gradient", "▤∇"),
    ("potential field", "2D Contour", None, True, "Equipotential contours of the response", "◌◌"),
])

GRAPH_CATEGORIES.setdefault("Tensor & Structure Analysis", []).extend([
    ("tensor glyph field", "Tensor Glyph Field", None, True, "Hessian eigen-ellipse glyphs revealing local curvature structure", "◬◭"),
    ("curvature ellipses", "Tensor Glyph Field", None, True, "Principal-curvature ellipses over the response surface", "⬭⬬"),
    ("stress-tensor view", "Tensor Glyph Field", None, True, "2×2 symmetric tensor ellipses (stress/strain style)", "σ⬭"),
    ("anisotropy field", "Tensor Glyph Field", None, True, "Local anisotropy orientation and magnitude", "⇱⇲"),
    ("principal directions", "Tensor Glyph Field", None, True, "Eigenvector orientation field of the Hessian", "✚⤫"),
    ("ridge-valley structure", "Tensor Glyph Field", None, True, "Convex ridges vs concave valleys via eigenvalue sign", "∧∨"),
])

GRAPH_CATEGORIES.setdefault("Spectral & Signal Imaging", []).extend([
    ("spectral heatmap", "Spectrogram", None, True, "Time–frequency magnitude imaging of a signal", "▦♪"),
    ("time-frequency map", "Spectrogram", None, True, "Short-time spectral evolution heatmap", "▦t"),
    ("power spectrum", "Power Spectral Density", "Semi-Log Y", True, "Welch power spectral density", "⌒⌄"),
    ("harmonic content", "Power Spectral Density", "Semi-Log Y", True, "Harmonic/spectral peak inspection", "♪▁"),
    ("autocorrelogram", "Autocorrelation", None, True, "Lag correlation structure of a series", "≡⇢"),
    ("cross-correlogram", "Cross Correlation", None, True, "Lagged coupling between two series", "≡≡"),
    ("impedance spectrum", "EIS: Bode", "Logarithmic Scale", False, "Frequency-resolved impedance magnitude/phase", "|Z|∠"),
    ("complex-plane impedance", "EIS: Nyquist", None, False, "Nyquist complex-impedance arc with Rs/Rct", "⌒Ω"),
])


def all_entries():
    out = []
    for category, items in GRAPH_CATEGORIES.items():
        for name, engine, scale, advanced, description, preview in items:
            out.append({
                "category": category, "name": name, "engine": engine, "scale": scale,
                "advanced": bool(advanced), "description": description, "preview": preview,
            })
    return out


GRAPH_LIBRARY = all_entries()


def entry_by_name(name: str):
    low = str(name).strip().lower()
    return next((e for e in GRAPH_LIBRARY if e["name"].lower() == low), None)


def fuzzy_score(query: str, entry: dict) -> float:
    q = str(query or "").strip().lower()
    if not q:
        return 1.0
    name = entry["name"].lower()
    desc = entry["description"].lower()
    cat = entry["category"].lower()
    if q in name:
        return 1.0
    if q in desc or q in cat:
        return 0.9
    tokens = [name] + name.replace("/", " ").replace("3", " 3").split() + desc.split()[:8]
    return max([SequenceMatcher(None, q, t).ratio() for t in tokens] + [SequenceMatcher(None, q, name).ratio()])


def search_entries(query: str, advanced: bool = True, threshold: float = 0.38, include_advanced: bool | None = None):
    if include_advanced is not None:
        advanced = bool(include_advanced)
    scored = []
    for e in GRAPH_LIBRARY:
        if e["advanced"] and not advanced:
            continue
        score = fuzzy_score(query, e)
        if not query or score >= threshold:
            scored.append((score, e))
    scored.sort(key=lambda p: (-p[0], p[1]["category"], p[1]["name"]))
    return [e for _, e in scored]

# ---------------------------------------------------------------- preview assets
import hashlib as _hashlib
import re as _re
from pathlib import Path as _Path


def preview_asset_name(entry: dict) -> str:
    """Stable filename for an entry-specific pre-rendered thumbnail."""
    raw = f"{entry.get('category','')}|{entry.get('name','')}|{entry.get('engine','')}|{entry.get('scale','')}"
    stem = _re.sub(r"[^A-Za-z0-9._-]+", "_", str(entry.get("name") or entry.get("engine") or "plot")).strip("_")[:48] or "plot"
    digest = _hashlib.sha1(raw.encode("utf-8")).hexdigest()[:10]
    return f"{stem}_{digest}.png"


def preview_asset_path(entry: dict) -> _Path:
    from graphvis.core.paths import GRAPH_PREVIEW_DIR
    return GRAPH_PREVIEW_DIR / preview_asset_name(entry)
