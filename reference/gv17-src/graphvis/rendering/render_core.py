# =========================================================================
# render_core.py — thread-safe figure construction.
#
# Everything here is plain matplotlib (Agg canvas) + NumPy/SciPy. No Qt.
# build_figure(spec) is executed inside a worker thread by
# plotting_engine.ScientificPlotCanvas; the resulting Figure is then handed
# to a FigureCanvasQTAgg on the GUI thread. The same function (with a
# different dpi) backs the high-resolution export suite.
# =========================================================================
from __future__ import annotations

import copy
import datetime as _dt
import hashlib
import json
import threading
import time
from collections import OrderedDict
from dataclasses import dataclass, field, asdict

import numpy as np
import pandas as pd
from matplotlib.backends.backend_agg import FigureCanvasAgg
from matplotlib.collections import LineCollection
from matplotlib.tri import Triangulation
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
from matplotlib.colors import ListedColormap, LogNorm, Normalize, PowerNorm, SymLogNorm, to_rgba
from matplotlib.ticker import FixedLocator, FuncFormatter, LogFormatterMathtext, LogLocator, NullFormatter
from matplotlib.figure import Figure

from graphvis.analysis.limits import LimitReport, analyse_limits
from graphvis.rendering.colormaps import COLOURMAPS, resolve_colourmap
from graphvis.rendering.surface_estimators import (IMPUTATION_MODES, SurfaceGrid, canonical_estimator, impute_surface_invalid,
                                                   interpolate_surface, smooth_surface_grid, structured_surface_native)
from graphvis.analysis.lhs_pipeline import sanitize_bounds, sanitize_clipping_ranges
from graphvis.data.loader import BioprocessDataLoader, Dataset, get_pretty_label
from graphvis.rendering.decimation import adaptive_decimate_frame
from graphvis.analysis.electro import (fit_gompertz, fit_nyquist_semicircle, global_sensitivity,
                            pareto_frontier, polarisation_power)
from graphvis.analysis.expression import (ExpressionError, evaluate_1d, evaluate_2d, evaluate_3d,
                               evaluate_parametric3d, parse_domain, split_equation)

MAX_SCATTER_POINTS = 20000
FIGSIZE = (9.0, 6.5)

STYLE_ELEMENTS = ["Main Title", "X-Axis Label", "Y-Axis Label", "Z-Axis Label",
                  "Tick Labels", "Colourbar Title", "Legend"]

DEFAULT_STYLING = {
    "Main Title": {"text": "", "color": "#1F2D3D", "size": None},
    "X-Axis Label": {"text": "", "color": "#34495E", "size": None},
    "Y-Axis Label": {"text": "", "color": "#34495E", "size": None},
    "Z-Axis Label": {"text": "", "color": "#34495E", "size": None},
    "Tick Labels": {"text": "", "color": "#2C3E50", "size": None},
    "Colourbar Title": {"text": "", "color": "#34495E", "size": None},
    "Legend": {"text": "", "color": "#2C3E50", "size": None},
}

DEFAULT_LIMIT_STYLE = {"color": "#C0392B", "linestyle": "--", "linewidth": 1.4,
                       "band_alpha": 0.18, "show_labels": True}

FIFTH_AXIS_MODES = ["Size", "Opacity", "Marker geometry"]
MARKER_CYCLE = ["o", "s", "^", "D", "v", "P", "X"]

# Which mapping slots each chart uses. None = not used (the UI greys it out).
AXIS_ROLES: dict[str, dict] = {
    "Line Chart":               {"x": "X axis", "y": "Y axis", "z": None, "w": "4th axis → line colour gradient", "v": None, "matrix": None, "gradient": None},
    "4D / 5D Scatter":          {"x": "X axis", "y": "Y axis", "z": None, "w": "4th axis → colour", "v": "5th axis → size / opacity / marker", "matrix": None, "gradient": None},
    "3D Scatter":               {"x": "X axis", "y": "Y axis", "z": "Z axis", "w": "4th axis → colour", "v": "5th axis → size / opacity / marker", "matrix": None, "gradient": None},
    "Pareto Front":             {"x": "X objective", "y": "Y objective", "z": None, "w": "4th axis → point colour", "v": "5th axis → point size", "matrix": None, "gradient": None},
    "Performance Ceiling":      {"x": "X axis", "y": "Y response", "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "2D Heatmap":               {"x": "X axis", "y": "Y axis", "z": "Z / response (colour)", "w": None, "v": None, "matrix": "Pre-gridded response matrix (optional)", "gradient": "Gradient overlay field"},
    "2D Contour":               {"x": "X axis", "y": "Y axis", "z": "Z / response (colour)", "w": None, "v": None, "matrix": "Pre-gridded response matrix (optional)", "gradient": "Gradient overlay field"},
    "3D Topography / Surface":  {"x": "X axis", "y": "Y axis", "z": "Z / height", "w": "4th axis → surface colour", "v": None, "matrix": "Pre-gridded response matrix (optional)", "gradient": None},
    "Hexbin Density":           {"x": "X axis", "y": "Y axis", "z": "Z / bin value (optional)", "w": None, "v": None, "matrix": None, "gradient": None},
    "Global Sensitivity":       {"x": None, "y": None, "z": "Response variable", "w": None, "v": None, "matrix": None, "gradient": None},
    "1D Marginal Responses":    {"x": None, "y": None, "z": "Response variable", "w": None, "v": None, "matrix": None, "gradient": None},
    "sCOD Degradation Profile": {"x": "Time vector (optional)", "y": None, "z": None, "w": None, "v": None, "matrix": "Profile matrix", "gradient": None},
    "VFA Concentration Profile": {"x": "Time vector (optional)", "y": None, "z": None, "w": None, "v": None, "matrix": "Profile matrix", "gradient": None},
    "Polarisation & Power Curve": {"x": "Current density", "y": "Cell voltage", "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "EIS: Nyquist":             {"x": "Z′ (real)", "y": "Z″ (imaginary)", "z": None, "w": "4th axis → frequency colour (optional)", "v": None, "matrix": None, "gradient": None},
    "EIS: Bode":                {"x": "Frequency [Hz]", "y": "Z′ (real)", "z": "Z″ (imaginary)", "w": None, "v": None, "matrix": None, "gradient": None},
    "Gompertz H₂ Kinetics":     {"x": "Time", "y": "Cumulative H₂", "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
}

# Additional GraphVis library renderers.  The UI uses these roles to enable only
# the mappings that each plot consumes.
AXIS_ROLES.update({
    "3D Line": {"x": "X axis", "y": "Y axis", "z": "Z axis", "w": None, "v": None, "matrix": None, "gradient": None},
    "Stairs": {"x": "X axis", "y": "Y axis", "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Error Bar": {"x": "X axis", "y": "Y axis", "z": "Error / uncertainty (optional)", "w": None, "v": None, "matrix": None, "gradient": None},
    "Area": {"x": "X axis", "y": "Y axis", "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Stacked Lines": {"x": "X axis", "y": "Primary Y (optional)", "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Swarm": {"x": "Category / X", "y": "Value / Y", "z": None, "w": "Colour (optional)", "v": None, "matrix": None, "gradient": None},
    "Plot Matrix": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Histogram": {"x": "Values", "y": None, "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "2D Histogram": {"x": "X values", "y": "Y values", "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Scatter + Marginals": {"x": "X values", "y": "Y values", "z": None, "w": "Colour (optional)", "v": None, "matrix": None, "gradient": None},
    "Box Plot": {"x": "Values", "y": None, "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Violin Plot": {"x": "Values", "y": None, "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Raincloud": {"x": "Values", "y": None, "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Bar": {"x": "X / category", "y": "Height", "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Horizontal Bar": {"x": "X / category", "y": "Length", "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "3D Bar": {"x": "X", "y": "Y", "z": "Height", "w": "Colour (optional)", "v": None, "matrix": None, "gradient": None},
    "Stem": {"x": "X", "y": "Y", "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "3D Stem": {"x": "X", "y": "Y", "z": "Z", "w": None, "v": None, "matrix": None, "gradient": None},
    "Pie": {"x": "Slice values", "y": None, "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Donut": {"x": "Slice values", "y": None, "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Parallel Coordinates": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Spy Matrix": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": "Matrix (optional)", "gradient": None},
    "Polar Line": {"x": "Angle [rad]", "y": "Radius", "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Polar Histogram": {"x": "Angle [rad]", "y": None, "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Polar Scatter": {"x": "Angle [rad]", "y": "Radius", "z": None, "w": "Colour (optional)", "v": "Size (optional)", "matrix": None, "gradient": None},
    "Compass": {"x": "U / angle", "y": "V / radius", "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Geo Line": {"x": "Longitude", "y": "Latitude", "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Geo Scatter": {"x": "Longitude", "y": "Latitude", "z": None, "w": "Colour (optional)", "v": "Size (optional)", "matrix": None, "gradient": None},
    "Geo Density": {"x": "Longitude", "y": "Latitude", "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Geo Bubble": {"x": "Longitude", "y": "Latitude", "z": None, "w": "Colour (optional)", "v": "Bubble size", "matrix": None, "gradient": None},
    "3D Contour": {"x": "X axis", "y": "Y axis", "z": "Z / response", "w": None, "v": None, "matrix": "Pre-gridded response matrix (optional)", "gradient": None},
    "Quiver Field": {"x": "X axis", "y": "Y axis", "z": "Scalar field", "w": None, "v": None, "matrix": "Pre-gridded response matrix (optional)", "gradient": "Gradient field (optional)"},
    "Stream Field": {"x": "X axis", "y": "Y axis", "z": "Scalar field", "w": None, "v": None, "matrix": "Pre-gridded response matrix (optional)", "gradient": "Gradient field (optional)"},
    "Vorticity Map": {"x": "X axis", "y": "Y axis", "z": "Scalar field", "w": None, "v": None, "matrix": None, "gradient": "Gradient field (optional)"},
    "Divergence Map": {"x": "X axis", "y": "Y axis", "z": "Scalar field", "w": None, "v": None, "matrix": None, "gradient": "Gradient field (optional)"},
    "Phase Portrait": {"x": "X axis", "y": "Y axis", "z": "Scalar field", "w": None, "v": None, "matrix": None, "gradient": "Gradient field (optional)"},
    "Flow Texture (LIC)": {"x": "X axis", "y": "Y axis", "z": "Scalar field", "w": None, "v": None, "matrix": None, "gradient": "Gradient field (optional)"},
    "Tensor Glyph Field": {"x": "X axis", "y": "Y axis", "z": "Scalar field", "w": None, "v": None, "matrix": None, "gradient": "Gradient field (optional)"},
    "3D Mesh": {"x": "X axis", "y": "Y axis", "z": "Z / height", "w": "Colour (optional)", "v": None, "matrix": "Pre-gridded response matrix (optional)", "gradient": None},
    "Surface + Contours": {"x": "X axis", "y": "Y axis", "z": "Z / height", "w": "Colour (optional)", "v": None, "matrix": "Pre-gridded response matrix (optional)", "gradient": None},
    "Waterfall": {"x": "X axis", "y": "Y axis", "z": "Z / height", "w": None, "v": None, "matrix": "Pre-gridded response matrix (optional)", "gradient": None},
    "Ribbon": {"x": "X axis", "y": "Y axis", "z": "Z / height", "w": None, "v": None, "matrix": "Pre-gridded response matrix (optional)", "gradient": None},
})

AXIS_ROLES.update({
    "Function Plot": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Function 3D Parametric": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Implicit Function": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Implicit Surface": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Function Contour": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Function Surface": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Function Mesh": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "3D Bubble": {"x": "X axis", "y": "Y axis", "z": "Z axis", "w": "Colour", "v": "Bubble size", "matrix": None, "gradient": None},
    "3D Swarm": {"x": "X axis", "y": "Y axis", "z": "Z axis", "w": "Colour", "v": None, "matrix": None, "gradient": None},
    "3D Horizontal Bar": {"x": "X", "y": "Y", "z": "Length", "w": "Colour", "v": None, "matrix": None, "gradient": None},
    "Word Cloud": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Bubble Cloud": {"x": "Weight", "y": None, "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Feather": {"x": "U component", "y": "V component", "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "3D Quiver": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": "U/V/W volume source", "gradient": None},
    "Stream Ribbon": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": "U/V/W volume source", "gradient": None},
    "Stream Tube": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": "U/V/W volume source", "gradient": None},
    "Cone Plot": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": "U/V/W volume source", "gradient": None},
    "Polar Bubble": {"x": "Angle [rad]", "y": "Radius", "z": None, "w": "Colour", "v": "Bubble size", "matrix": None, "gradient": None},
    "Isosurface": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": "3-D scalar volume", "gradient": None},
    "Isocaps": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": "3-D scalar volume", "gradient": None},
    "Isonormals": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": "3-D scalar volume", "gradient": None},
    "Volume Show": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": "3-D scalar volume", "gradient": None},
    "Patch": {"x": "X", "y": "Y", "z": "Z", "w": None, "v": None, "matrix": None, "gradient": None},
    "Volume Slice": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": "3-D scalar volume", "gradient": None},
    "Contour Slice": {"x": None, "y": None, "z": None, "w": None, "v": None, "matrix": "3-D scalar volume", "gradient": None},
    "Animated Line": {"x": "Time / X", "y": "Y", "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Comet": {"x": "X", "y": "Y", "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
    "Comet 3D": {"x": "X", "y": "Y", "z": "Z", "w": None, "v": None, "matrix": None, "gradient": None},
    "Stream Particles": {"x": "X", "y": "Y", "z": None, "w": None, "v": None, "matrix": None, "gradient": None},
})

XY_CHARTS = {"Line Chart", "Stairs", "Error Bar", "Area", "4D / 5D Scatter", "Swarm", "Pareto Front",
             "Polarisation & Power Curve", "Gompertz H₂ Kinetics", "Bar", "Horizontal Bar", "Stem"}

SURFACE_FIELD_CHARTS = {"2D Heatmap", "2D Contour", "3D Contour", "3D Topography / Surface",
                        "3D Mesh", "Surface + Contours", "Waterfall", "Ribbon", "Quiver Field", "Stream Field"}


AXIS_SCALE_OPTIONS = ("Linear", "Log10", "Ln", "Log2", "Sqrt", "Symlog", "Logit", "Date-Time", "Categorical")

# Scales whose spatial metric is logarithmic (base only changes tick labels,
# never geometry) — used by the surface interpolation distance metric.
LOG_LIKE_SCALES = {"Log10", "Ln", "Log2"}
_LOG_SCALE_BASES = {"Log10": 10.0, "Ln": float(np.e), "Log2": 2.0}

_AXIS_SCALE_ALIASES = {
    "linear": "Linear", "lin": "Linear",
    "log": "Log10", "log10": "Log10", "logarithmic": "Log10", "logarithmic scale": "Log10",
    "ln": "Ln", "loge": "Ln", "log_e": "Ln", "log e": "Ln", "natural log": "Ln",
    "natural logarithm": "Ln", "ln (natural)": "Ln",
    "log2": "Log2", "log_2": "Log2", "log 2": "Log2", "binary log": "Log2", "binary logarithm": "Log2",
    "sqrt": "Sqrt", "square root": "Sqrt", "√x": "Sqrt", "root": "Sqrt",
    "symlog": "Symlog", "sym log": "Symlog", "symmetric log": "Symlog",
    "symmetrical log": "Symlog", "symmetric logarithmic": "Symlog",
    "logit": "Logit", "probability": "Logit",
    "date-time": "Date-Time", "datetime": "Date-Time", "date": "Date-Time",
    "time": "Date-Time", "chronological": "Date-Time", "date/time": "Date-Time",
    "categorical": "Categorical", "category": "Categorical", "nominal": "Categorical",
    "discrete": "Categorical", "categorical/nominal": "Categorical",
}


def canonical_axis_scale(value: object) -> str:
    """Return the public axis-scale label used by the Qt UI."""
    raw = str(value or "Linear").strip()
    if raw in AXIS_SCALE_OPTIONS:
        return raw
    return _AXIS_SCALE_ALIASES.get(raw.lower(), "Linear")


def _signed_sqrt(x):
    x = np.asarray(x, dtype=float)
    return np.sign(x) * np.sqrt(np.abs(x))


def _signed_square(y):
    y = np.asarray(y, dtype=float)
    return np.sign(y) * np.square(y)


def legacy_scale_to_axes(scale_type: object) -> tuple[str, str, str]:
    """Translate the removed global scale selector into independent axes."""
    s = str(scale_type or "Linear Scale")
    if s == "Logarithmic Scale":
        return "Log10", "Log10", "Linear"
    if s == "Semi-Log X":
        return "Log10", "Linear", "Linear"
    if s == "Semi-Log Y":
        return "Linear", "Log10", "Linear"
    if s in ("SymLog", "Symlog Scale", "Symmetric Log"):
        return "Symlog", "Symlog", "Linear"
    return "Linear", "Linear", "Linear"


# =====================================================================
# PlotSpec
# =====================================================================
@dataclass
class PlotSpec:
    datasets: dict = field(default_factory=dict)        # name -> Dataset (not serialised)
    chart_type: str = "Line Chart"
    mappings: dict = field(default_factory=dict)        # x, y, z, w, v, matrix, gradient
    colourmap: str = "Parula"
    series_color: str = "#2980B9"
    grid_visible: bool = True
    show_legend: bool = True
    legend_fontsize: int = 9
    legend_alpha: float = 0.85
    legend_loc: object = "best"
    experimental_overlay: dict | None = None
    literature_overlays: list = field(default_factory=list)
    clipping_ranges: dict = field(default_factory=dict)
    styling: dict = field(default_factory=lambda: copy.deepcopy(DEFAULT_STYLING))
    smoothing: float = 1.2
    # Per-axis scaling.  ``scale_type`` is retained only for backwards
    # compatibility with GraphVis 2026.08.29.9 and older saved sessions.
    x_scale: str = "Linear"
    y_scale: str = "Linear"
    z_scale: str = "Linear"
    scale_type: str = "Linear Scale"
    estimator: str = "Auto (data-aware)"
    bin_statistic: str = "mean"
    grid_resolution: int = 160
    # Scientific surface-estimation controls. Distances are automatically
    # evaluated in normalized log10-space when X/Y use logarithmic axes.
    surface_neighbors: int = 32
    idw_power: float = 2.0
    loess_fraction: float = 0.25
    kriging_variogram: str = "Exponential"
    surface_value_policy: str = "Allow estimator overshoot"
    surface_extrapolation: str = "Mask outside convex hull"
    invalid_mask_policy: str = "Sample cell only"
    failure_bridge_policy: str = "Bridge isolated failures"
    failure_bridge_max_cells: int = 4
    show_imputed_cells: bool = False
    surface_response_space: str = "Linear values"
    surface_x_metric_weight: float = 1.0
    surface_y_metric_weight: float = 1.0
    invalid_data_mode: str = "Transparent"
    invalid_color: str = "#DDDDDD"
    colorbar_extend: str = "Auto (from clipping)"
    field_render_mode: str = "Continuous shading"
    contour_levels: int = 10
    contour_overlay: bool = False
    contour_overlay_levels: int = 10
    contour_overlay_color: str = "#FFFFFF"
    contour_overlay_width: float = 0.6
    contour_overlay_labels: bool = False
    progressive_rendering: bool = True
    auto_surface_presentation: bool = True
    pareto_options: dict = field(default_factory=lambda: {'constrain': True, 'maximise_y': True, 'bootstrap': True})
    font_size: int = 10
    label_padding: int = 8
    outlier_mask: bool = False
    mask_method: str = "Percentile Capping"
    mask_min_neighbors: int = 8                        # k for the k-NN distance outlier filter
    noise_filter_enabled: bool = False
    noise_filter_method: str = "Z-Score"
    noise_filter_threshold: float = 2.5
    quiver_overlay: bool = False
    quiver_type: str = "Gradient"
    fifth_axis_mode: str = "Size"
    fifth_axis_invert: bool = False                    # invert the 5th-axis opacity mapping
    density_alpha: bool = False                        # density-dependent point transparency
    auto_aggregate: bool = False                       # switch very dense 2-D scatters to hexbin
    color_gate: bool = False                           # W/V clips filter rows instead of norm-only
    point_budget: int = 0                              # per-graph decimation budget (0 = global setting)
    surface_alpha: float = 1.0                         # 3-D object opacity, 0.05 … 1.0
    assumed_alpha: float = 0.28                        # pale rendering for flagged/incomplete datasets
    pareto_only: bool = False                          # hide dominated samples on Pareto views
    expression: str = "sin(x)"                       # symbolic function source for f* plots
    expression_domain: str = "-10,10"                # x; y; z domain pairs
    volume_level: float | None = None                  # isosurface threshold (None = median)
    vector_components: dict = field(default_factory=dict)  # {'u':'name','v':'name','w':'name'}
    line_width: float = 1.8
    marker: str = ""
    secondary_y: str | None = None
    animation_speed: float = 1.0
    animation_trail: int = 80
    linked_layers: bool = False
    publication_profile: str = ""
    export_dpi: int = 300
    export_color_mode: str = "RGB"
    export_icc_profile: str = ""
    vector_surface_mode: str = "Hybrid (vector text + raster field)"
    metadata: dict = field(default_factory=dict)        # provenance / Smart Map rationale; not drawn unless requested
    limits: dict = field(default_factory=dict)          # analysis.DEFAULT_LIMIT_OPTIONS keys
    limit_style: dict = field(default_factory=lambda: dict(DEFAULT_LIMIT_STYLE))
    annotations: list = field(default_factory=list)     # {'text','x','y','coords'}
    view_state: dict | None = None                      # xlim/ylim/zlim/elev/azim (export)
    figsize: tuple = FIGSIZE

    def __post_init__(self) -> None:
        self.x_scale = canonical_axis_scale(self.x_scale)
        self.y_scale = canonical_axis_scale(self.y_scale)
        self.z_scale = canonical_axis_scale(self.z_scale)
        self.estimator = canonical_estimator(self.estimator)
        # Code that still constructs PlotSpec(scale_type=...) directly keeps
        # working during the transition, but the visible UI/state is per-axis.
        if self.scale_type != "Linear Scale" and (self.x_scale, self.y_scale, self.z_scale) == ("Linear", "Linear", "Linear"):
            self.x_scale, self.y_scale, self.z_scale = legacy_scale_to_axes(self.scale_type)

    # -- serialisation -------------------------------------------------
    def to_dict(self) -> dict:
        d = asdict(self)
        d["datasets"] = list(self.datasets.keys())
        d["figsize"] = list(self.figsize)
        for key in ("experimental_overlay",):
            ov = d.get(key)
            if ov:
                d[key] = {k: (np.asarray(v).tolist() if isinstance(v, (np.ndarray, list, tuple)) else v) for k, v in ov.items()}
        d["literature_overlays"] = [
            {k: (np.asarray(v).tolist() if isinstance(v, (np.ndarray, list, tuple)) else v) for k, v in ov.items()}
            for ov in self.literature_overlays]
        return d

    @classmethod
    def from_dict(cls, d: dict, registry: dict | None = None) -> "PlotSpec":
        d = dict(d)
        names = d.pop("datasets", []) or []
        registry = registry or {}
        # Migrate the former one-combo scale model to independent axis state.
        # New files carry x_scale/y_scale/z_scale directly; old files are
        # interpreted exactly as before.
        if not any(k in d for k in ("x_scale", "y_scale", "z_scale")):
            xs, ys, zs = legacy_scale_to_axes(d.get("scale_type", "Linear Scale"))
            d["x_scale"], d["y_scale"], d["z_scale"] = xs, ys, zs
        spec = cls(**{k: v for k, v in d.items() if k in cls.__dataclass_fields__})
        spec.x_scale = canonical_axis_scale(spec.x_scale)
        spec.y_scale = canonical_axis_scale(spec.y_scale)
        spec.z_scale = canonical_axis_scale(spec.z_scale)
        spec.datasets = {n: registry[n] for n in names if n in registry}
        spec.figsize = tuple(spec.figsize) if spec.figsize else FIGSIZE
        return spec

    def clone(self) -> "PlotSpec":
        c = copy.copy(self)
        c.mappings = dict(self.mappings)
        c.clipping_ranges = dict(self.clipping_ranges)
        c.styling = copy.deepcopy(self.styling)
        c.pareto_options = dict(self.pareto_options)
        c.vector_components = dict(self.vector_components)
        c.metadata = copy.deepcopy(self.metadata)
        c.limits = dict(self.limits)
        c.limit_style = dict(self.limit_style)
        c.annotations = [dict(a) for a in self.annotations]
        c.literature_overlays = [dict(o) for o in self.literature_overlays]
        c.view_state = dict(self.view_state) if self.view_state else None
        c.datasets = dict(self.datasets)
        return c

    def cache_key(self, dpi: int = 100) -> str:
        d = self.to_dict()
        d["dpi"] = dpi
        d["dataset_keys"] = [list(map(str, ds.cache_key())) for ds in self.datasets.values()]
        blob = json.dumps(d, sort_keys=True, default=str)
        return hashlib.sha1(blob.encode("utf-8")).hexdigest()


# =====================================================================
# caches
# =====================================================================
class _LRU:
    def __init__(self, capacity=24, *, max_bytes: int | None = None, size_func=None):
        self.capacity = int(capacity)
        self.max_bytes = int(max_bytes) if max_bytes else None
        self.size_func = size_func
        self._d: OrderedDict = OrderedDict()
        self._sizes: dict[object, int] = {}
        self._bytes = 0
        self._lock = threading.Lock()

    def get(self, key):
        with self._lock:
            if key in self._d:
                self._d.move_to_end(key)
                return self._d[key]
            return None

    def put(self, key, value):
        with self._lock:
            if key in self._d:
                self._bytes -= self._sizes.pop(key, 0)
            self._d[key] = value
            self._d.move_to_end(key)
            size = int(self.size_func(value)) if self.size_func is not None else 0
            self._sizes[key] = max(size, 0)
            self._bytes += max(size, 0)
            while self._d and (len(self._d) > self.capacity or (self.max_bytes is not None and self._bytes > self.max_bytes)):
                old_key, _ = self._d.popitem(last=False)
                self._bytes -= self._sizes.pop(old_key, 0)

    def clear(self):
        with self._lock:
            self._d.clear(); self._sizes.clear(); self._bytes = 0

    @property
    def bytes_used(self) -> int:
        with self._lock:
            return int(self._bytes)


def _surface_nbytes(surface: SurfaceGrid) -> int:
    try:
        total = int(np.asarray(surface.X).nbytes + np.asarray(surface.Y).nbytes)
        z = np.ma.asarray(surface.Z)
        total += int(np.asarray(z.data).nbytes + np.ma.getmaskarray(z).nbytes)
        for mask in (surface.invalid_mask, surface.outside_hull_mask, surface.imputed_mask):
            if mask is not None:
                total += int(np.asarray(mask).nbytes)
        return total
    except Exception:
        return 0


SURFACE_CACHE = _LRU(capacity=64, max_bytes=384 * 1024 * 1024, size_func=_surface_nbytes)     # byte-bounded surface grids
PARETO_CACHE = _LRU(capacity=24)      # pareto_frontier results


def _key(*parts) -> str:
    return hashlib.sha1(json.dumps(parts, default=str).encode()).hexdigest()


# =====================================================================
# result
# =====================================================================
@dataclass
class RenderResult:
    fig: Figure
    summary: dict = field(default_factory=dict)
    limit_report: LimitReport | None = None
    warnings: list = field(default_factory=list)
    seconds: float = 0.0


def clean_series_name(raw_name: str) -> str:
    import os
    name = os.path.basename(str(raw_name))
    for ext in ('.mat', '.csv'):
        name = name.replace(ext, '')
    return name.replace('_', ' ').strip().title()


# =====================================================================
# renderer
# =====================================================================
class _Renderer:
    def __init__(self, spec: PlotSpec, dpi: int, figsize=None, for_export=False):
        self.spec = spec
        self.dpi = dpi
        self.for_export = for_export
        self._ui_theme = dict((spec.metadata or {}).get("ui_theme") or {}) if not for_export else {}
        figure_face = self._ui_theme.get("figure", "white") if self._ui_theme else "white"
        self.fig = Figure(figsize=figsize or spec.figsize, facecolor=figure_face, dpi=dpi)
        FigureCanvasAgg(self.fig)
        self.ax = None
        self.summary: dict = {"chart": spec.chart_type, "datasets": [], "n_points": 0, "notes": [], "performance": {}}
        pre = (spec.metadata or {}).get("_surface_precompute_summary")
        if isinstance(pre, dict):
            self.summary["performance"]["surface_precompute"] = dict(pre)
        self.warnings: list[str] = []
        self.limit_report: LimitReport | None = None
        base_cmap = resolve_colourmap(spec.colourmap)
        try:
            self.cmap = base_cmap.copy()
        except Exception:
            self.cmap = copy.copy(base_cmap)
        # Explicit extremes make clipped values deterministic. Over/under
        # values use the terminal colours. Masked cells stay transparent here;
        # the 2-D field renderer paints only the *failed-simulation* mask with
        # the optional fallback colour, so an outside-convex-hull mask does not
        # incorrectly become a large grey rectangle.
        try:
            self.cmap.set_under(self.cmap(0.0))
            self.cmap.set_over(self.cmap(1.0))
            self.cmap.set_bad((0.0, 0.0, 0.0, 0.0))
        except Exception:
            pass
        self._colorbars = []

    # ------------------------------------------------------------ helpers
    def _col(self, ds: Dataset, key: str, fallback_index: int | None = None):
        c = self.spec.mappings.get(key)
        if c and c in ds.df.columns:
            return c
        if isinstance(c, str) and c.startswith("aux:"):
            # Auxiliary 1-D vectors are valid physical axes for pre-gridded
            # matrix surfaces, but are not dataframe columns for generic charts.
            return None
        if key == "gradient":
            return None
        if fallback_index is not None and len(ds.numeric_columns) > fallback_index:
            return ds.numeric_columns[fallback_index]
        return None

    def _selected_array(self, ds: Dataset, mapping_key: str = "matrix", ndim: int | None = None):
        """Resolve matrix:/volume:/tensor: mapping tokens or choose a compatible array."""
        token = self.spec.mappings.get(mapping_key)
        stores = [("matrix", getattr(ds, "matrices", {})), ("volume", getattr(ds, "volumes", {})),
                  ("tensor", getattr(ds, "tensors", {}))]
        if token:
            raw = str(token)
            prefix, name = (raw.split(":", 1) if ":" in raw else (None, raw))
            for pfx, store in stores:
                if (prefix is None or prefix == pfx) and name in store:
                    arr = np.asarray(store[name])
                    if ndim is None or arr.ndim == ndim:
                        return name, arr
        for _, store in stores:
            for name, arr0 in store.items():
                arr = np.asarray(arr0)
                if ndim is None or arr.ndim == ndim:
                    return name, arr
        return None, None

    def _vector_volumes(self, ds: Dataset):
        vols = getattr(ds, "volumes", {}) or {}
        if not vols:
            return None
        names = list(vols)
        cfg = self.spec.vector_components or {}
        def pick(role, hints):
            n = cfg.get(role)
            if n in vols:
                return n
            for cand in names:
                low = cand.lower()
                if any(h == low or h in low for h in hints):
                    return cand
            return None
        un = pick("u", ["u", "vx", "vel_x", "velocity_x", "x_velocity"])
        vn = pick("v", ["v", "vy", "vel_y", "velocity_y", "y_velocity"])
        wn = pick("w", ["w", "vz", "vel_z", "velocity_z", "z_velocity"])
        if not all((un, vn, wn)) and len(names) >= 3:
            un, vn, wn = names[:3]
        if not all((un, vn, wn)):
            return None
        u, v, w = map(lambda n: np.asarray(vols[n], float), (un, vn, wn))
        shape = tuple(min(a, b, c) for a, b, c in zip(u.shape, v.shape, w.shape))
        sl = tuple(slice(0, n) for n in shape)
        return (un, vn, wn), (u[sl], v[sl], w[sl])

    def _prepare_subset(self, ds: Dataset, cols):
        self._check_cancelled()
        cols = [c for c in cols if c and c in ds.df.columns]
        if len(cols) != len(set(cols)):
            return ds.df.iloc[0:0]
        sub = ds.df[cols].replace([np.inf, -np.inf], np.nan).dropna()
        if sub.empty:
            return sub
        sp = self.spec
        # Spatial axis clipping actively filters the plotted data range: rows
        # outside a clipped X/Y window are dropped, so histograms/statistics/
        # limit detection reflect exactly the visible window (axis limits are
        # applied as well).  Z/W/V clips deliberately stay normalization-only,
        # preserving over/under colour indicators and colorbar extends.
        clip_cols = {}
        # W/V normally stay normalization-only clips; the noise-gate option
        # turns them into hard row filters to strip low-relevance background
        # points (e.g. isolate the Pareto frontier / high-VHPR cluster).
        gate_keys = ("x", "y", "w", "v") if getattr(sp, "color_gate", False) else ("x", "y")
        for key in gate_keys:
            clip = (sp.clipping_ranges or {}).get(key)
            col = (sp.mappings or {}).get(key)
            if col and col in sub.columns and clip and clip[0] != clip[1]:
                clip_cols[col] = (float(min(clip)), float(max(clip)))
        if clip_cols:
            keep = np.ones(len(sub), dtype=bool)
            for col, (lo, hi) in clip_cols.items():
                v = sub[col].to_numpy(dtype=float)
                keep &= (v >= lo) & (v <= hi)
            dropped = int((~keep).sum())
            if keep.any():
                if dropped:
                    sub = sub.loc[keep]
                    self.summary["notes"].append(
                        f"Axis clipping active: {dropped:,} sample(s) outside the selected range were excluded."
                    )
            else:
                self.warnings.append("Axis clipping excluded every sample; showing the unclipped data instead.")
        if sp.noise_filter_enabled:
            sub = BioprocessDataLoader.apply_noise_filter(sub, cols, method=sp.noise_filter_method,
                                                          threshold=sp.noise_filter_threshold)
        if sp.outlier_mask and not sub.empty:
            if sp.mask_method == "k-NN Distance Filter":
                sub = self._knn_outlier_filter(sub, cols)
            else:
                sub = sub.copy()
                for c in cols:
                    v = sub[c].astype(float)
                    if sp.mask_method == "Z-Score Clip":
                        mu, sd = v.mean(), v.std(ddof=0)
                        if sd > 0:
                            sub[c] = v.clip(mu - 3 * sd, mu + 3 * sd)
                    else:
                        lo, hi = v.quantile(0.01), v.quantile(0.99)
                        if hi > lo:
                            sub[c] = v.clip(lo, hi)
        return sub

    def _knn_outlier_filter(self, sub, cols):
        """Drop rows whose k-th nearest-neighbour distance is anomalously large.

        Points with fewer than ``mask_min_neighbors`` local neighbours (a robust
        3-MAD fence on the k-NN distance) are treated as isolated outliers.
        """
        k = int(np.clip(getattr(self.spec, "mask_min_neighbors", 8), 1, 64))
        if len(sub) <= k + 1:
            return sub
        pts = sub[cols].to_numpy(dtype=float)
        scale = np.nanstd(pts, axis=0)
        scale[~np.isfinite(scale) | (scale == 0)] = 1.0
        try:
            from scipy.spatial import cKDTree
            tree = cKDTree(pts / scale)
            dists, _ = tree.query(pts / scale, k=k + 1)
            dk = dists[:, -1]
        except Exception:
            return sub
        med = float(np.median(dk))
        mad = float(np.median(np.abs(dk - med))) or 1e-12
        keep = dk <= med + 3.0 * 1.4826 * mad
        dropped = int((~keep).sum())
        if dropped:
            self.summary["notes"].append(
                f"k-NN outlier filter (k={k}): removed {dropped:,} isolated point(s) beyond the robust local-density fence."
            )
            return sub.loc[keep]
        return sub

    def _downsample(self, sub, x_col=None, y_col=None, preserve_line=False):
        if self.for_export:
            return sub
        budget = int(getattr(self.spec, "point_budget", 0) or 0)
        if budget > 0:
            # Per-graph thinning slider: envelope/extrema-preserving decimation
            # (LTTB / stratified) down to the user's chosen point budget.
            threshold = max(500, min(budget, MAX_SCATTER_POINTS))
        else:
            threshold = int((self.spec.metadata or {}).get("interactive_max_points", MAX_SCATTER_POINTS))
            threshold = max(2000, min(threshold, MAX_SCATTER_POINTS))
        if len(sub) > threshold:
            self.summary["notes"].append(
                f"Adaptive decimation: {len(sub):,} → {threshold:,} interactive points; full data remain available for analysis/export."
            )
            return adaptive_decimate_frame(sub, threshold, x_col=x_col, y_col=y_col, preserve_line=preserve_line)
        return sub

    def _style(self, element):
        return self.spec.styling.get(element, DEFAULT_STYLING.get(element, {}))

    def _fs(self, element, default_delta=0):
        s = self._style(element).get("size")
        return int(s) if s else self.spec.font_size + default_delta

    def _clip(self, key):
        clip = self.spec.clipping_ranges.get(key)
        if clip and clip[0] != clip[1]:
            return float(min(clip)), float(max(clip))
        return None

    def _check_cancelled(self):
        """Cooperative cancellation: raise if the owning task was cancelled."""
        cb = (self.spec.metadata or {}).get("_cancel_check")
        if cb is not None and cb():
            raise RuntimeError("Cancelled")

    def _alpha_for(self, ds: Dataset, default: float = 1.0) -> float:
        """Pale rendering for datasets explicitly flagged as assumed/incomplete."""
        if getattr(ds, "assumed", False):
            return float(np.clip(self.spec.assumed_alpha, 0.03, 1.0))
        return float(np.clip(default, 0.03, 1.0))

    def _norm_for(self, key, values):
        """Return the normalization for a colour-mapped variable.

        Masked values are excluded from limits.  Z is special in GraphVis: for
        a 2-D field it is the response/colour dimension, so ``z_scale=Log10``
        changes colour normalization but never physical X/Y coordinates.
        """
        marr = np.ma.masked_invalid(np.ma.asarray(values, dtype=float))
        finite = marr.compressed()
        finite = finite[np.isfinite(finite)]
        if finite.size == 0:
            return Normalize(vmin=0.0, vmax=1.0, clip=False)
        clip = self._clip(key)
        if key == "z" and canonical_axis_scale(self.spec.z_scale) in LOG_LIKE_SCALES:
            positive = finite[finite > 0]
            if positive.size == 0:
                self.warnings.append("Log10 Z/response colour scale needs positive values; using linear normalization.")
                lo, hi = (clip if clip else (float(np.nanmin(finite)), float(np.nanmax(finite))))
                return Normalize(vmin=lo, vmax=(hi if hi != lo else lo + 1e-9), clip=False)
            data_lo, data_hi = float(np.nanmin(positive)), float(np.nanmax(positive))
            if clip:
                lo, hi = map(float, clip)
                lo = max(lo, data_lo) if lo > 0 else data_lo
                hi = min(hi, data_hi) if hi > 0 else data_hi
                if hi <= lo:
                    lo, hi = data_lo, data_hi
            else:
                lo, hi = data_lo, data_hi
            if hi <= lo:
                hi = lo * 10.0 if lo > 0 else 1.0
            return LogNorm(vmin=lo, vmax=hi, clip=False)
        vmin, vmax = (clip if clip else (float(np.nanmin(finite)), float(np.nanmax(finite))))
        if vmin == vmax:
            vmax = vmin + 1e-9
        if key == "z":
            z_mode = canonical_axis_scale(self.spec.z_scale)
            if z_mode == "Symlog":
                linthresh = max(max(abs(vmin), abs(vmax)) * 0.01, 1e-9)
                return SymLogNorm(linthresh=linthresh, vmin=vmin, vmax=vmax, base=10, clip=False)
            if z_mode == "Sqrt" and vmin >= 0:
                return PowerNorm(gamma=0.5, vmin=vmin, vmax=vmax, clip=False)
        return Normalize(vmin=vmin, vmax=vmax, clip=False)

    @staticmethod
    def _colour_values_for_norm(values, norm):
        arr = np.ma.masked_invalid(np.ma.asarray(values, dtype=float))
        return np.ma.masked_less_equal(arr, 0.0) if isinstance(norm, LogNorm) else arr

    @staticmethod
    def _levels_for_norm(norm, count: int = 40):
        count = max(int(count), 3)
        if isinstance(norm, LogNorm):
            return np.geomspace(float(norm.vmin), float(norm.vmax), count)
        return np.linspace(float(norm.vmin), float(norm.vmax), count)

    def _colorbar_extend_for(self, values, norm) -> str:
        requested = str(getattr(self.spec, "colorbar_extend", "Auto (from clipping)") or "Auto (from clipping)").strip().lower()
        explicit = {"neither": "neither", "min": "min", "max": "max", "both": "both"}
        if requested in explicit:
            return explicit[requested]
        marr = np.ma.masked_invalid(np.ma.asarray(values, dtype=float))
        vals = marr.compressed()
        vals = vals[np.isfinite(vals)]
        if isinstance(norm, LogNorm):
            vals = vals[vals > 0]
        if vals.size == 0:
            return "neither"
        below = bool(np.any(vals < float(norm.vmin)))
        above = bool(np.any(vals > float(norm.vmax)))
        if below and above:
            return "both"
        if above:
            return "max"
        if below:
            return "min"
        return "neither"

    def _add_colorbar(self, mappable, label, ax=None, three_d=False, extend: str | None = None):
        st = self._style("Colourbar Title")
        text = st.get("text") or label
        kwargs = dict(shrink=0.65, pad=0.08) if three_d else dict(pad=0.02, fraction=0.046)
        if extend and extend != "neither":
            kwargs["extend"] = extend
        cbar = self.fig.colorbar(mappable, ax=ax or self.ax, **kwargs)
        label_color = self._theme_style_color("Colourbar Title", st.get("color"), "#34495E")
        cbar.set_label(text, color=label_color, fontsize=self._fs("Colourbar Title"))
        tick = self._style("Tick Labels")
        tick_color = self._theme_style_color("Tick Labels", tick.get("color"), "#2C3E50")
        cbar.ax.tick_params(colors=tick_color, labelsize=self._fs("Tick Labels", -1))
        if isinstance(getattr(mappable, "norm", None), LogNorm):
            # Force decade ticks and mathtext labels (10^{-1}, 10^0, 10^1 …)
            # instead of decimal exponent strings.
            cbar.locator = LogLocator(base=10.0)
            cbar.formatter = LogFormatterMathtext(base=10.0)
            cbar.update_ticks()
            cbar.ax.yaxis.set_minor_locator(LogLocator(base=10.0, subs=np.arange(2, 10) * 0.1))
            cbar.ax.yaxis.set_minor_formatter(NullFormatter())
        if self._ui_theme:
            try:
                cbar.ax.set_facecolor(self._ui_theme.get("axes", self._ui_theme.get("figure", "white")))
                cbar.outline.set_edgecolor(self._ui_theme.get("border", "#CBD5E1"))
            except Exception:
                pass
        self._colorbars.append(cbar)
        return cbar

    def _fifth_axis(self, sub, cv):
        """Return (sizes, alphas, marker_groups) from the 5th-axis column."""
        base = 22.0
        if not cv or cv not in sub.columns:
            return np.full(len(sub), base), None, None
        v = sub[cv].to_numpy(float)
        clip = self._clip("v")
        lo, hi = clip if clip else (float(np.nanmin(v)), float(np.nanmax(v)))
        if hi <= lo:
            hi = lo + 1e-9
        f = np.clip((v - lo) / (hi - lo), 0, 1)
        mode = self.spec.fifth_axis_mode
        if mode == "Opacity":
            # Invert Opacity: high values fade out so dense low-performing
            # background clouds can be de-emphasised instead (or vice versa).
            ff = 1.0 - f if getattr(self.spec, "fifth_axis_invert", False) else f
            return np.full(len(sub), base), 0.15 + 0.85 * ff, None
        if mode == "Marker geometry":
            tiers = np.minimum((f * len(MARKER_CYCLE)).astype(int), len(MARKER_CYCLE) - 1)
            edges = lo + (hi - lo) * np.arange(len(MARKER_CYCLE) + 1) / len(MARKER_CYCLE)
            return np.full(len(sub), base), None, (tiers, edges)
        return 8.0 + 90.0 * f ** 1.5, None, None

    def _scatter_4d5d(self, ax, sub, cx, cy, cw, cv, label, cz=None, alpha=1.0):
        """Shared scatter routine with optional colour (w) and size/alpha/marker (v)."""
        x, y = sub[cx].to_numpy(float), sub[cy].to_numpy(float)
        z = sub[cz].to_numpy(float) if cz else None
        sizes, alphas, groups = self._fifth_axis(sub, cv)
        if getattr(self.spec, "density_alpha", False) and len(x) > 50:
            # Density-dependent blending: points in crowded regions fade so
            # overlapping clouds read as smooth gradients, not opaque blobs.
            try:
                counts, xe, ye = np.histogram2d(x, y, bins=48)
                ix = np.clip(np.digitize(x, xe[1:-1]), 0, counts.shape[0] - 1)
                iy = np.clip(np.digitize(y, ye[1:-1]), 0, counts.shape[1] - 1)
                local = counts[ix, iy]
                cmax = float(local.max()) or 1.0
                dens = 0.10 + 0.90 * (1.0 - local / cmax) ** 1.5
                alphas = dens if alphas is None else np.asarray(alphas, float) * dens
            except Exception:
                pass
        kwargs = dict(s=sizes, linewidths=0.3, edgecolors="white", alpha=float(np.clip(alpha, 0.03, 1.0)))
        colour_mappable = None
        if cw and cw in sub.columns:
            w = sub[cw].to_numpy(float)
            norm = self._norm_for("w", w)
            kwargs.update(c=w, cmap=self.cmap, norm=norm)
        else:
            kwargs.update(color=self.spec.series_color)
        if alphas is not None:
            rgba = None
            if "c" in kwargs:
                rgba = self.cmap(kwargs["norm"](kwargs.pop("c")))
                kwargs.pop("cmap"); kwargs.pop("norm")
            else:
                rgba = np.tile(to_rgba(kwargs.pop("color")), (len(x), 1))
            rgba[:, 3] = np.clip(alphas * float(np.clip(alpha, 0.03, 1.0)), 0.03, 1.0)
            kwargs.pop("alpha", None)
            kwargs["c"] = rgba
        if groups is not None:
            tiers, edges = groups
            for t in np.unique(tiers):
                sel = tiers == t
                kw = dict(kwargs)
                kw["s"] = sizes[sel]
                if "c" in kw and not isinstance(kw["c"], str):
                    kw["c"] = np.asarray(kw["c"])[sel]
                args = (x[sel], y[sel], z[sel]) if z is not None else (x[sel], y[sel])
                sc = ax.scatter(*args, marker=MARKER_CYCLE[int(t)],
                                label=f"{label}: {get_pretty_label(cv)} {edges[t]:.3g}–{edges[t + 1]:.3g}", **kw)
                if "cmap" in kw:
                    colour_mappable = sc
        else:
            args = (x, y, z) if z is not None else (x, y)
            sc = ax.scatter(*args, label=label, **kwargs)
            if "cmap" in kwargs:
                colour_mappable = sc
            elif alphas is not None and cw and cw in sub.columns:
                # alpha-modulated colours: build a proxy mappable for the colourbar
                from matplotlib.cm import ScalarMappable
                colour_mappable = ScalarMappable(norm=self._norm_for("w", sub[cw].to_numpy(float)), cmap=self.cmap)
        if colour_mappable is not None:
            self._add_colorbar(colour_mappable, get_pretty_label(cw), ax=ax, three_d=(z is not None))
        return x, y

    # ------------------------------------------------------- overlays
    def _apply_overlays(self, ax):
        overlays = []
        if self.spec.experimental_overlay:
            overlays.append(self.spec.experimental_overlay)
        overlays.extend(self.spec.literature_overlays or [])
        palette = ["#E74C3C", "#8E44AD", "#16A085", "#D35400", "#2C3E50"]
        for i, ov in enumerate(overlays):
            if not ov or 'x' not in ov or 'y' not in ov:
                continue
            x = np.asarray(ov['x'], float)
            y = np.asarray(ov['y'], float)
            yerr = ov.get('yerr')
            yerr = np.asarray(yerr, float) if yerr is not None and len(yerr) == len(y) else None
            col = ov.get('color') or palette[i % len(palette)]
            if ov.get('kind') == 'prediction':
                lo = np.asarray(ov.get('lower'), float) if ov.get('lower') is not None else None
                hi = np.asarray(ov.get('upper'), float) if ov.get('upper') is not None else None
                ax.plot(x, y, color=col, linewidth=1.6, linestyle='--', label=ov.get('label', 'Predictive extrapolation'), zorder=5)
                if lo is not None and hi is not None and len(lo) == len(x) and len(hi) == len(x):
                    ax.fill_between(x, lo, hi, color=col, alpha=0.16, linewidth=0, label=ov.get('band_label', 'Prediction band'), zorder=2)
                cutoff = ov.get('observed_x_max')
                if cutoff is not None:
                    ax.axvline(float(cutoff), color=col, alpha=0.5, linewidth=0.9, linestyle=':')
            else:
                ax.errorbar(x, y, yerr=yerr, fmt='o', color=col, ecolor=col, elinewidth=1.2, capsize=3,
                            markersize=5, label=ov.get('label', 'Experimental data'), zorder=6)
            self.summary["notes"].append(f"Overlay '{ov.get('label', 'experimental')}' ({x.size} points).")

    def _apply_limits(self, ax, x, y):
        opts = self.spec.limits or {}
        if not any(opts.get(k) for k in ("plateau", "asymptote", "confidence", "knee", "pareto_bounds")):
            return
        opts = dict(opts)
        opts.setdefault("maximise_y", self.spec.pareto_options.get("maximise_y", True))
        rep = analyse_limits(x, y, opts)
        self.limit_report = rep
        st = {**DEFAULT_LIMIT_STYLE, **(self.spec.limit_style or {})}
        col, ls, lw = st["color"], st["linestyle"], float(st["linewidth"])
        show = st.get("show_labels", True)
        for f in rep.features:
            lab = f.label if show else None
            if f.kind == "hline":
                ax.axhline(f.value, color=col, linestyle=ls, linewidth=lw, label=lab, zorder=5)
            elif f.kind == "vline":
                ax.axvline(f.value, color=col, linestyle=":", linewidth=lw, label=lab, zorder=5)
            elif f.kind == "band":
                ax.fill_between(f.x, f.lo, f.hi, color=col, alpha=float(st["band_alpha"]), linewidth=0, label=lab, zorder=2)
            elif f.kind == "curve":
                ax.plot(f.x, f.y, color=col, linestyle="-." if f.group == "asymptote" else "-", linewidth=lw * 0.9,
                        alpha=0.9, label=lab, zorder=5)
            elif f.kind == "point":
                ax.plot([f.x], [f.y], marker="*", markersize=14, color=col, markeredgecolor="white", linestyle="none",
                        label=lab, zorder=7)
                if show:
                    ax.annotate(f"({f.x:.3g}, {f.y:.3g})", (f.x, f.y), textcoords="offset points", xytext=(8, 8),
                                fontsize=max(self.spec.font_size - 2, 6), color=col)

    def _apply_annotations(self, ax):
        from matplotlib.patches import Ellipse, FancyArrowPatch, Rectangle
        for ann_index, a in enumerate(self.spec.annotations or []):
            try:
                if not a.get("active", True):
                    continue
                created = None
                kind = str(a.get("kind", "text"))
                coords = a.get("coords", "axes")
                trans = ax.transData if coords == "data" else ax.transAxes
                x, y = float(a.get("x", 0.5)), float(a.get("y", 0.5))
                x2, y2 = float(a.get("x2", x + 0.1)), float(a.get("y2", y + 0.1))
                style = dict(a.get("style") or {})
                color = a.get("color", style.get("color", "#1F2D3D"))
                background_enabled = bool(a.get("background", True))
                bg = a.get("background_color", style.get("background", "#F1C40F")) if background_enabled else None
                bg_alpha = float(a.get("background_alpha", style.get("background_alpha", 0.55))) if bg not in (None, "", "none", "None") else 0.0
                rotation = float(a.get("rotation", 0.0))
                fontsize = float(a.get("fontsize", style.get("font_size", self.spec.font_size + (1 if coords == "axes" else 0))))
                pointer_style = a.get("pointer_style", style.get("arrowstyle", "->"))
                bbox = None if bg_alpha <= 0 else dict(boxstyle=style.get("boxstyle", "round,pad=0.4"), fc=bg, alpha=bg_alpha, ec=style.get("edgecolor", color))
                if kind in ("text", "callout"):
                    if kind == "callout" or a.get("pointer", False):
                        created = ax.annotate(a.get("text", ""), xy=(x2, y2), xytext=(x, y), xycoords=trans, textcoords=trans,
                                    color=color, fontsize=fontsize, rotation=rotation, bbox=bbox,
                                    arrowprops=dict(arrowstyle=pointer_style, color=style.get("pointer_color", color), lw=float(style.get("linewidth", 1.2))), zorder=9)
                    else:
                        created = ax.text(x, y, a.get("text", ""), transform=trans, color=color, fontsize=fontsize, rotation=rotation, bbox=bbox, zorder=9)
                elif kind in ("arrow", "bracket"):
                    p1, p2 = (x, y), (x2, y2)
                    if rotation:
                        theta = np.deg2rad(rotation)
                        cx, cy = (x + x2) / 2.0, (y + y2) / 2.0
                        vx, vy = (x2 - x) / 2.0, (y2 - y) / 2.0
                        rx = vx * np.cos(theta) - vy * np.sin(theta)
                        ry = vx * np.sin(theta) + vy * np.cos(theta)
                        p1, p2 = (cx - rx, cy - ry), (cx + rx, cy + ry)
                    arrowstyle = pointer_style if kind == "arrow" else a.get("pointer_style", style.get("arrowstyle", "]-["))
                    patch = FancyArrowPatch(p1, p2, transform=trans, arrowstyle=arrowstyle,
                                            mutation_scale=14 if kind == "arrow" else 12,
                                            color=color, linewidth=float(style.get("linewidth", 1.4)), zorder=9)
                    ax.add_patch(patch); created = patch
                elif kind in ("rectangle", "ellipse"):
                    w, h = x2 - x, y2 - y
                    cls = Rectangle if kind == "rectangle" else Ellipse
                    if cls is Rectangle:
                        patch = cls((x, y), w, h, angle=rotation, transform=trans, fill=bool(style.get("fill", False)),
                                    facecolor=style.get("fill_color", bg), alpha=float(style.get("fill_alpha", 0.15)), edgecolor=color, linewidth=float(style.get("linewidth", 1.4)), zorder=8)
                    else:
                        patch = cls((x + w/2, y + h/2), abs(w), abs(h), angle=rotation, transform=trans, fill=bool(style.get("fill", False)),
                                    facecolor=style.get("fill_color", bg), alpha=float(style.get("fill_alpha", 0.15)), edgecolor=color, linewidth=float(style.get("linewidth", 1.4)), zorder=8)
                    ax.add_patch(patch); created = patch
                if created is not None:
                    created.set_gid(f"graphvis-annotation-{ann_index}")
                    try: created.set_picker(True)
                    except Exception: pass
            except Exception:
                continue

    def _apply_data_markers(self, ax):
        """Draw persistent user markers without mixing them with annotations."""
        try:
            current_axis_index = self.fig.axes.index(ax)
        except Exception:
            current_axis_index = 0
        for idx, marker in enumerate((self.spec.metadata or {}).get("data_markers", []) or []):
            try:
                if int(marker.get("axis_index", 0) or 0) != current_axis_index:
                    continue
                x, y = float(marker["x"]), float(marker["y"])
                z = marker.get("z")
                if hasattr(ax, "get_zlim3d") and z is not None:
                    artist = ax.scatter([x], [y], [float(z)], s=72, facecolors="none", edgecolors="#E53935",
                                        linewidths=2.0, zorder=31)
                else:
                    artist, = ax.plot([x], [y], marker="o", markersize=9, markerfacecolor="none",
                                      markeredgecolor="#E53935", markeredgewidth=2.0, linestyle="none", zorder=31)
                try: artist.set_gid(f"graphvis-data-marker-{idx}")
                except Exception: pass
            except Exception:
                continue

    def _apply_interactive_theme(self, ax):
        if not self._ui_theme:
            return
        axes_bg = self._ui_theme.get("axes", self._ui_theme.get("figure", "white"))
        border = self._ui_theme.get("border", "#CBD5E1")
        muted = self._ui_theme.get("muted", "#667788")
        try:
            ax.set_facecolor(axes_bg)
            for spine in ax.spines.values():
                spine.set_color(border)
            ax.tick_params(axis="both", colors=muted)
            if hasattr(ax, "zaxis"):
                ax.tick_params(axis="z", colors=muted)
            if hasattr(ax, "xaxis") and hasattr(ax.xaxis, "pane"):
                for axis in (ax.xaxis, ax.yaxis, getattr(ax, "zaxis", None)):
                    if axis is not None and hasattr(axis, "pane"):
                        axis.pane.set_facecolor(axes_bg)
                        axis.pane.set_edgecolor(border)
        except Exception:
            pass

    def _theme_style_color(self, element: str, current: str, fallback: str) -> str:
        if not self._ui_theme:
            return current or fallback
        default = str(DEFAULT_STYLING.get(element, {}).get("color", "")).lower()
        chosen = str(current or fallback)
        if chosen.lower() != default:
            return chosen
        return self._ui_theme.get("muted" if element in ("Tick Labels", "Legend") else "text", chosen)

    # ------------------------------------------------------- styling
    def _apply_grid(self, ax):
        if self.spec.grid_visible:
            if self._ui_theme and self._ui_theme.get('grid'):
                ax.grid(True, linestyle='--', alpha=0.32, color=self._ui_theme.get('grid'))
            else:
                ax.grid(True, linestyle='--', alpha=0.4)
        else:
            ax.grid(False)

    @staticmethod
    def _format_log_axis(axis, base: float = 10.0) -> None:
        """Decade/base ticks adapted to the active logarithm base."""
        axis.set_major_locator(LogLocator(base=base))
        if abs(base - float(np.e)) < 1e-9:
            # Natural log: label the decades as powers of e.
            axis.set_major_formatter(FuncFormatter(
                lambda v, _p: rf"$e^{{{np.log(v):.0f}}}$" if v > 0 else ""))
            axis.set_minor_locator(FixedLocator([]))
        else:
            axis.set_major_formatter(LogFormatterMathtext(base=base))
            subs = np.arange(2, 10) * 0.1 if abs(base - 10.0) < 1e-9 else None
            axis.set_minor_locator(LogLocator(base=base, subs=subs) if subs is not None else FixedLocator([]))
        axis.set_minor_formatter(NullFormatter())

    def _mapped_positive_extent(self, role: str):
        col = self.spec.mappings.get(role)
        if not col:
            return None
        chunks = []
        for ds in self.spec.datasets.values():
            if col in ds.df.columns:
                vals = pd.to_numeric(ds.df[col], errors="coerce").to_numpy(float)
                vals = vals[np.isfinite(vals) & (vals > 0)]
                if vals.size:
                    chunks.append((float(np.nanmin(vals)), float(np.nanmax(vals))))
        if not chunks:
            return None
        lo, hi = min(v[0] for v in chunks), max(v[1] for v in chunks)
        if hi <= lo:
            hi = lo * 10.0
        return lo, hi

    def _apply_scale(self, ax, three_d: bool = False):
        """Apply X/Y/Z scale state independently.

        On 2-D heatmaps/contours, Z is not a spatial axis; its Log10 state is
        consumed by ``_norm_for('z', ...)`` instead.
        """
        axes = [
            ("x", canonical_axis_scale(self.spec.x_scale), ax.set_xscale, getattr(ax, "xaxis", None), ax.get_xlim, ax.set_xlim),
            ("y", canonical_axis_scale(self.spec.y_scale), ax.set_yscale, getattr(ax, "yaxis", None), ax.get_ylim, ax.set_ylim),
        ]
        if three_d and hasattr(ax, "set_zscale"):
            axes.append(("z", canonical_axis_scale(self.spec.z_scale), ax.set_zscale, getattr(ax, "zaxis", None), ax.get_zlim, ax.set_zlim))
        for role, mode, setter, axis, get_lim, set_lim in axes:
            try:
                self._apply_axis_scale_mode(role, mode, setter, axis, get_lim, set_lim)
            except Exception as exc:
                self.warnings.append(f"{role.upper()} scale not applied: {exc}")

    def _apply_axis_scale_mode(self, role, mode, setter, axis, get_lim, set_lim):
        """Apply one axis-scale mode with adaptive ticks/labels per scale."""
        if mode in LOG_LIKE_SCALES:
            current = get_lim()
            if current[0] <= 0 or current[1] <= 0:
                positive = self._mapped_positive_extent(role)
                if positive is not None:
                    set_lim(*positive)
            base = _LOG_SCALE_BASES[mode]
            setter("log", base=base)
            if axis is not None:
                self._format_log_axis(axis, base=base)
        elif mode == "Symlog":
            setter("symlog", base=10, linthresh=self._symlog_linthresh(role))
        elif mode == "Sqrt":
            # Signed square root: monotonic everywhere, so zero and negative
            # values remain representable instead of erroring out.
            setter("function", functions=(_signed_sqrt, _signed_square))
        elif mode == "Logit":
            vals = self._mapped_values(role)
            if vals is not None and vals.size and (float(np.nanmin(vals)) <= 0.0 or float(np.nanmax(vals)) >= 1.0):
                self.warnings.append(
                    f"Logit {role.upper()} scale needs values strictly between 0 and 1; using linear instead.")
                setter("linear")
            else:
                setter("logit")
        elif mode == "Date-Time":
            setter("linear")   # chronological spacing: irregular intervals keep their true gaps
            if axis is not None:
                self._format_datetime_axis(role, axis)
        elif mode == "Categorical":
            self._apply_categorical_scale(role, setter, axis)
        else:
            setter("linear")

    def _mapped_values(self, role: str):
        """All finite numeric values mapped to ``role`` across the datasets."""
        col = self.spec.mappings.get(role)
        if not col:
            return None
        chunks = []
        for ds in self.spec.datasets.values():
            if col in ds.df.columns:
                v = pd.to_numeric(ds.df[col], errors="coerce").to_numpy(float)
                v = v[np.isfinite(v)]
                if v.size:
                    chunks.append(v)
        return np.concatenate(chunks) if chunks else None

    def _apply_categorical_scale(self, role, setter, axis):
        """Evenly spaced discrete categories with value-labelled ticks."""
        vals = self._mapped_values(role)
        uniques = np.unique(vals) if vals is not None and vals.size else np.empty(0)
        if uniques.size < 2:
            self.warnings.append(
                f"Categorical {role.upper()} scale needs at least two distinct mapped values; using linear.")
            setter("linear")
            return
        idx = np.arange(uniques.size, dtype=float)

        def forward(x, _u=uniques, _i=idx):
            return np.interp(np.asarray(x, dtype=float), _u, _i)

        def inverse(y, _u=uniques, _i=idx):
            return np.interp(np.asarray(y, dtype=float), _i, _u)

        setter("function", functions=(forward, inverse))
        if axis is not None:
            step = max(1, int(np.ceil(uniques.size / 24)))   # keep tick labels legible
            axis.set_major_locator(FixedLocator(list(uniques[::step])))
            axis.set_major_formatter(FuncFormatter(lambda v, _p: f"{v:g}"))
            axis.set_minor_locator(FixedLocator([]))
            if getattr(axis, "axis_name", "") == "x" and uniques.size > 8:
                axis.set_tick_params(rotation=30)

    def _format_datetime_axis(self, role, axis):
        """Chronological tick labels; the numeric unit is inferred from magnitude.

        Supports UNIX epoch nanoseconds/milliseconds/seconds and matplotlib
        datenums.  Positions stay at the true timestamps, so irregular
        sampling (nights, weekends, missed cycles) appears as real gaps.
        """
        vals = self._mapped_values(role)
        med = float(np.nanmedian(np.abs(vals))) if vals is not None and vals.size else 0.0
        span_seconds = 0.0
        if vals is not None and vals.size > 1:
            raw_span = float(np.nanmax(vals) - np.nanmin(vals))
            if med > 1e16:
                span_seconds = raw_span / 1e9
            elif med > 1e12:
                span_seconds = raw_span / 1e3
            elif med > 1e8:
                span_seconds = raw_span
            else:
                span_seconds = raw_span * 86400.0   # matplotlib datenum (days)

        def _to_datetime(v):
            try:
                if med > 1e16:
                    return _dt.datetime.fromtimestamp(v / 1e9, tz=_dt.timezone.utc)
                if med > 1e12:
                    return _dt.datetime.fromtimestamp(v / 1e3, tz=_dt.timezone.utc)
                if med > 1e8:
                    return _dt.datetime.fromtimestamp(v, tz=_dt.timezone.utc)
                from matplotlib.dates import num2date
                return num2date(v)
            except Exception:
                return None

        if span_seconds > 3 * 86400:
            pattern = "%Y-%m-%d"
        elif span_seconds > 2 * 3600:
            pattern = "%m-%d %H:%M"
        else:
            pattern = "%H:%M:%S"

        def _fmt(v, _p):
            stamp = _to_datetime(v)
            return stamp.strftime(pattern) if stamp is not None else f"{v:g}"

        axis.set_major_formatter(FuncFormatter(_fmt))
        if getattr(axis, "axis_name", "") == "x":
            axis.set_tick_params(rotation=30)

    def _symlog_linthresh(self, role: str) -> float:
        """Linear-region half-width for a Symlog axis, derived from the data.

        1% of the largest mapped magnitude keeps the transition invisible for
        well-scaled data while still resolving values straddling zero.
        """
        extent = None
        try:
            extent = self._mapped_abs_extent(role)
        except Exception:
            extent = None
        if extent is None or extent <= 0:
            return 1.0
        return max(extent * 0.01, 1e-9)

    def _mapped_abs_extent(self, role: str) -> float | None:
        chunks = []
        col_index = {"x": 0, "y": 1, "z": 2}.get(role, 0)
        for ds in self.spec.datasets.values():
            col = self._col(ds, role, col_index)
            if col and col in getattr(ds, "df", pd.DataFrame()).columns:
                vals = np.abs(np.asarray(ds.df[col], float))
                vals = vals[np.isfinite(vals)]
                if vals.size:
                    chunks.append(float(np.nanmax(vals)))
        return max(chunks) if chunks else None

    def _safe_axis_clip(self, role: str, clip, current):
        if not clip:
            return None
        lo, hi = map(float, clip)
        scale = canonical_axis_scale(getattr(self.spec, f"{role}_scale", "Linear"))
        if scale == "Logit":
            lo, hi = max(lo, 1e-9), min(hi, 1.0 - 1e-9)
            if hi <= lo:
                self.warnings.append(f"Ignored {role.upper()} clipping range outside (0, 1) on a Logit axis.")
                return None
            return lo, hi
        if scale not in LOG_LIKE_SCALES:
            return lo, hi
        if hi <= 0:
            self.warnings.append(f"Ignored non-positive {role.upper()} clipping range on a {scale} axis.")
            return None
        if lo <= 0:
            extent = self._mapped_positive_extent(role)
            lo = extent[0] if extent is not None else (current[0] if current[0] > 0 else hi / 1e6)
        return (lo, hi) if hi > lo else None

    def _apply_clipping(self, ax, three_d=False):
        cx = self._safe_axis_clip('x', self._clip('x'), ax.get_xlim())
        cy = self._safe_axis_clip('y', self._clip('y'), ax.get_ylim())
        cz = self._safe_axis_clip('z', self._clip('z'), ax.get_zlim()) if three_d else None
        if cx:
            ax.set_xlim(*cx)
        if cy:
            ax.set_ylim(*cy)
        if three_d and cz:
            ax.set_zlim(*cz)

    def _apply_styling(self, ax, default_x="", default_y="", default_z="", default_title=""):
        sp = self.spec
        st = self._style("Main Title")
        if st.get("text") or default_title:
            ax.set_title(st.get("text") or default_title, color=self._theme_style_color("Main Title", st.get("color"), "#1F2D3D"),
                         fontsize=self._fs("Main Title", 3), pad=sp.label_padding + 4, fontfamily=st.get("font_family", None))
        sx, sy, sz = self._style("X-Axis Label"), self._style("Y-Axis Label"), self._style("Z-Axis Label")
        ax.set_xlabel(sx.get("text") or default_x, color=self._theme_style_color("X-Axis Label", sx.get("color"), "#34495E"), fontsize=self._fs("X-Axis Label", 1), labelpad=sp.label_padding, fontfamily=sx.get("font_family", None))
        ax.set_ylabel(sy.get("text") or default_y, color=self._theme_style_color("Y-Axis Label", sy.get("color"), "#34495E"), fontsize=self._fs("Y-Axis Label", 1), labelpad=sp.label_padding, fontfamily=sy.get("font_family", None))
        if hasattr(ax, "set_zlabel") and (sz.get("text") or default_z):
            ax.set_zlabel(sz.get("text") or default_z, color=self._theme_style_color("Z-Axis Label", sz.get("color"), "#34495E"), fontsize=self._fs("Z-Axis Label", 1), labelpad=sp.label_padding, fontfamily=sz.get("font_family", None))
        tick = self._style("Tick Labels")
        tick_color = self._theme_style_color("Tick Labels", tick.get("color"), "#2C3E50")
        ax.tick_params(axis='both', colors=tick_color, labelsize=self._fs("Tick Labels", -1))
        if hasattr(ax, "zaxis"):
            ax.tick_params(axis='z', colors=tick_color, labelsize=self._fs("Tick Labels", -1))

    def _legend(self, ax):
        if not self.spec.show_legend:
            return
        handles, labels = ax.get_legend_handles_labels()
        if not labels:
            return
        st = self._style("Legend")
        loc = self.spec.legend_loc if self.spec.legend_loc else "best"
        try:
            leg = ax.legend(handles=handles, labels=labels, fontsize=self._fs("Legend", -1),
                            framealpha=self.spec.legend_alpha, loc=loc)
        except Exception:
            leg = ax.legend(handles=handles, labels=labels, fontsize=self._fs("Legend", -1), framealpha=self.spec.legend_alpha)
        for t in leg.get_texts():
            t.set_color(self._theme_style_color("Legend", st.get("color"), "#2C3E50"))
        if self._ui_theme:
            try:
                leg.get_frame().set_facecolor(self._ui_theme.get("axes", "white"))
                leg.get_frame().set_edgecolor(self._ui_theme.get("border", "#CBD5E1"))
            except Exception:
                pass
        try:
            leg.set_draggable(True)
        except Exception:
            pass

    def _finish(self, ax, default_x="", default_y="", default_z="", default_title="", three_d=False, xy=None):
        if xy is not None and not three_d:
            self._apply_limits(ax, *xy)
        if not three_d:
            self._apply_overlays(ax)
        self._apply_annotations(ax)
        self._apply_data_markers(ax)
        self._apply_scale(ax, three_d=three_d)
        self._apply_clipping(ax, three_d=three_d)
        self._apply_interactive_theme(ax)
        self._apply_grid(ax)
        self._apply_styling(ax, default_x, default_y, default_z, default_title)
        self._legend(ax)

    def _empty(self, ax, msg):
        fn = ax.text2D if hasattr(ax, "text2D") else ax.text
        color = self._ui_theme.get("muted", "#7F8C8D") if self._ui_theme else "#7F8C8D"
        fn(0.5, 0.5, msg, ha='center', va='center', transform=ax.transAxes, fontsize=10, color=color)
        self.summary["notes"].append(msg)

    def _apply_view_state(self):
        vs = self.spec.view_state
        if not vs or self.ax is None:
            return
        ax = self.ax
        try:
            # Inverted/corrupted stored limits (min > max, NaN) must never
            # distort the bounding box; sanitize_bounds swaps or drops them.
            xlim = sanitize_bounds(*vs["xlim"]) if vs.get("xlim") else None
            ylim = sanitize_bounds(*vs["ylim"]) if vs.get("ylim") else None
            zlim = sanitize_bounds(*vs["zlim"]) if vs.get("zlim") else None
            if xlim: ax.set_xlim(*xlim)
            if ylim: ax.set_ylim(*ylim)
            if hasattr(ax, "set_zlim") and zlim: ax.set_zlim(*zlim)
            if hasattr(ax, "view_init") and vs.get("elev") is not None:
                ax.view_init(elev=vs.get("elev"), azim=vs.get("azim"), roll=vs.get("roll", 0))
        except Exception as exc:
            self.warnings.append(f"View state not applied: {exc}")

    # ======================================================== charts
    def render(self) -> RenderResult:
        t0 = time.perf_counter()
        c = self.spec.chart_type
        dispatch = {
            "Line Chart": self._render_line,
            "3D Line": self._render_line3d,
            "Stairs": self._render_stairs,
            "Error Bar": self._render_errorbar,
            "Area": self._render_area,
            "Stacked Lines": self._render_stacked_lines,
            "Function Plot": self._render_function_plot,
            "Function 3D Parametric": self._render_function_plot3d,
            "Implicit Function": self._render_implicit2d,
            "Implicit Surface": self._render_implicit3d,
            "Function Contour": self._render_function_contour,
            "Function Surface": self._render_function_surface,
            "Function Mesh": self._render_function_surface,
            "4D / 5D Scatter": self._render_scatter,
            "Swarm": self._render_swarm,
            "Plot Matrix": self._render_plot_matrix,
            "Histogram": self._render_histogram,
            "2D Histogram": self._render_hist2d,
            "Scatter + Marginals": self._render_scatter_marginals,
            "Box Plot": self._render_box,
            "Violin Plot": self._render_violin,
            "Raincloud": self._render_raincloud,
            "Bar": self._render_bar,
            "Horizontal Bar": self._render_bar,
            "3D Bar": self._render_bar3d,
            "3D Horizontal Bar": self._render_bar3d,
            "Stem": self._render_stem,
            "3D Stem": self._render_stem3d,
            "Pie": self._render_pie,
            "Donut": self._render_pie,
            "Word Cloud": self._render_wordcloud,
            "Bubble Cloud": self._render_bubblecloud,
            "Parallel Coordinates": self._render_parallel,
            "Spy Matrix": self._render_spy,
            "3D Scatter": self._render_scatter3d,
            "3D Bubble": self._render_scatter3d,
            "3D Swarm": self._render_scatter3d,
            "Pareto Front": self._render_pareto,
            "Performance Ceiling": self._render_performance_ceiling,
            "2D Heatmap": self._render_field,
            "2D Contour": self._render_field,
            "3D Contour": self._render_field,
            "3D Topography / Surface": self._render_field,
            "3D Mesh": self._render_field,
            "Surface + Contours": self._render_field,
            "Waterfall": self._render_field,
            "Ribbon": self._render_field,
            "Quiver Field": self._render_field,
            "Feather": self._render_feather,
            "3D Quiver": self._render_vector3d,
            "Stream Ribbon": self._render_vector3d,
            "Stream Tube": self._render_vector3d,
            "Cone Plot": self._render_vector3d,
            "Stream Field": self._render_field,
            "Hexbin Density": self._render_hexbin,
            "Polar Line": self._render_polar,
            "Polar Histogram": self._render_polar,
            "Polar Scatter": self._render_polar,
            "Polar Bubble": self._render_polar,
            "Compass": self._render_polar,
            "Geo Line": self._render_geo,
            "Geo Scatter": self._render_geo,
            "Geo Density": self._render_geo,
            "Geo Bubble": self._render_geo,
            "Isosurface": self._render_volume,
            "Isocaps": self._render_volume,
            "Isonormals": self._render_volume,
            "Volume Show": self._render_volume,
            "Patch": self._render_patch,
            "Volume Slice": self._render_volume,
            "Contour Slice": self._render_volume,
            "Animated Line": self._render_animation_static,
            "Comet": self._render_animation_static,
            "Comet 3D": self._render_animation_static,
            "Stream Particles": self._render_animation_static,
            'Step Mid': self._render_extended,
            'Connected Scatter': self._render_extended,
            'Lollipop': self._render_extended,
            'Fill Between': self._render_extended,
            'Event Plot': self._render_extended,
            'Cumulative Sum': self._render_extended,
            'Rolling Mean': self._render_extended,
            'Rolling Median': self._render_extended,
            'Moving Std': self._render_extended,
            'Derivative': self._render_extended,
            'Integral': self._render_extended,
            'Slope Graph': self._render_extended,
            'Dot Plot': self._render_extended,
            'KDE Density': self._render_extended,
            'ECDF': self._render_extended,
            'Cumulative Histogram': self._render_extended,
            'Q-Q Plot': self._render_extended,
            'Probability Plot': self._render_extended,
            'Ridgeline': self._render_extended,
            'Strip Plot': self._render_extended,
            'Beeswarm': self._render_extended,
            'Correlation Matrix': self._render_extended,
            'Covariance Matrix': self._render_extended,
            'Residual Plot': self._render_extended,
            'Bland-Altman': self._render_extended,
            'Calibration Plot': self._render_extended,
            'Confidence Ellipse': self._render_extended,
            'Spectrogram': self._render_extended,
            'Power Spectral Density': self._render_extended,
            'Autocorrelation': self._render_extended,
            'Cross Correlation': self._render_extended,
            'Lag Plot': self._render_extended,
            'Radar Chart': self._render_extended,
            'Andrews Curves': self._render_extended,
            'Ternary Scatter': self._render_extended,
            'Forest Plot': self._render_extended,
            'Volcano Plot': self._render_extended,
            'ROC Curve': self._render_extended,
            'Control Chart': self._render_extended,
            'Manhattan Plot': self._render_extended,
            'Population Pyramid': self._render_extended,
            'Treemap': self._render_extended,
            'Sunburst': self._render_extended,
            'Sankey Diagram': self._render_extended,
            'Venn Diagram': self._render_extended,
            'Piper Diagram': self._render_extended,
            'Wind Rose': self._render_polar,
            "Vorticity Map": self._render_flow_analysis,
            "Divergence Map": self._render_flow_analysis,
            "Phase Portrait": self._render_flow_analysis,
            "Flow Texture (LIC)": self._render_flow_analysis,
            "Tensor Glyph Field": self._render_flow_analysis,
            "Global Sensitivity": self._render_sensitivity,
            "1D Marginal Responses": self._render_marginals,
            "sCOD Degradation Profile": self._render_profile,
            "VFA Concentration Profile": self._render_profile,
            "Polarisation & Power Curve": self._render_polarisation,
            "EIS: Nyquist": self._render_nyquist,
            "EIS: Bode": self._render_bode,
            "Gompertz H₂ Kinetics": self._render_gompertz,
        }
        fn = dispatch.get(c, self._render_line)
        try:
            self._check_cancelled()
            fn()
            self._check_cancelled()
        except Exception as exc:  # never let a chart bug take the app down
            if str(exc) == "Cancelled":
                raise  # cooperative cancellation must propagate to the worker
            import traceback
            self.fig.clear()
            ax = self.fig.add_subplot(111)
            ax.text(0.5, 0.5, f"Render error in '{c}':\n{type(exc).__name__}: {exc}", ha='center', va='center',
                    transform=ax.transAxes, fontsize=9, color='#C0392B', wrap=True)
            self.warnings.append(traceback.format_exc())
            self.ax = ax
        self._apply_view_state()
        try:
            self.fig.tight_layout()
        except Exception:
            pass
        try:
            self.fig.canvas.draw()   # validate + warm caches off the GUI thread
        except Exception as exc:
            self.warnings.append(f"Agg pre-draw failed: {exc}")
        self.summary["datasets"] = list(self.spec.datasets.keys())
        # Scientific reproducibility: a SHA-1 over the full parameter spec plus
        # dataset revision keys uniquely identifies what produced this figure.
        try:
            self.summary["provenance_hash"] = self.spec.cache_key(dpi=int(self.dpi))
        except Exception:
            pass
        return RenderResult(self.fig, self.summary, self.limit_report, self.warnings, time.perf_counter() - t0)

    # -- symbolic function plots -----------------------------------------
    def _render_function_plot(self):
        ax = self.ax = self.fig.add_subplot(111)
        dom = parse_domain(self.spec.expression_domain, 1)[0]
        res = evaluate_1d(self.spec.expression, dom, n=max(self.spec.grid_resolution * 6, 400))
        ax.plot(res.x, res.y, color=self.spec.series_color, linewidth=self.spec.line_width,
                marker=self.spec.marker or None, markevery=max(len(res.x)//30, 1) if self.spec.marker else None,
                label=self.spec.expression)
        self.summary["expression"] = self.spec.expression
        self.summary["n_points"] = int(np.isfinite(res.y).sum())
        self._finish(ax, "x", "f(x)", default_title="Function plot", xy=(res.x, res.y))

    def _render_function_plot3d(self):
        ax = self.ax = self.fig.add_subplot(111, projection='3d')
        dom = parse_domain(self.spec.expression_domain, 1)[0]
        res = evaluate_parametric3d(self.spec.expression, dom, n=max(self.spec.grid_resolution * 5, 400))
        ax.plot(res.x, res.y, res.z, color=self.spec.series_color, linewidth=self.spec.line_width,
                label=self.spec.expression)
        self.summary["expression"] = self.spec.expression
        self.summary["n_points"] = len(res.x)
        self._finish(ax, "x(t)", "y(t)", "z(t)", "Parametric function", three_d=True)

    def _render_implicit2d(self):
        ax = self.ax = self.fig.add_subplot(111)
        dom = parse_domain(self.spec.expression_domain, 2, default=(-5, 5))
        src = split_equation(self.spec.expression)
        res = evaluate_2d(src, dom[0], dom[1], n=min(max(self.spec.grid_resolution, 80), 500))
        finite = res.Z[np.isfinite(res.Z)]
        if finite.size == 0 or not (np.nanmin(finite) <= 0 <= np.nanmax(finite)):
            self._empty(ax, "The selected domain does not contain the implicit zero level.")
        else:
            cs = ax.contour(res.X, res.Y, res.Z, levels=[0.0], colors=[self.spec.series_color], linewidths=self.spec.line_width)
            if cs.collections if hasattr(cs, 'collections') else True:
                pass
        self.summary["expression"] = self.spec.expression
        self._finish(ax, "x", "y", default_title="Implicit function")

    def _render_function_contour(self):
        ax = self.ax = self.fig.add_subplot(111)
        dom = parse_domain(self.spec.expression_domain, 2, default=(-5, 5))
        res = evaluate_2d(self.spec.expression, dom[0], dom[1], n=min(max(self.spec.grid_resolution, 80), 500))
        cs = ax.contourf(res.X, res.Y, res.Z, levels=18, cmap=self.cmap)
        ax.contour(res.X, res.Y, res.Z, levels=12, colors='k', linewidths=0.35, alpha=0.45)
        self._add_colorbar(cs, "f(x,y)", ax)
        self.summary["expression"] = self.spec.expression
        self._finish(ax, "x", "y", default_title="Function contour")

    def _render_function_surface(self):
        ax = self.ax = self.fig.add_subplot(111, projection='3d')
        dom = parse_domain(self.spec.expression_domain, 2, default=(-5, 5))
        n = min(max(self.spec.grid_resolution, 50), 260)
        res = evaluate_2d(self.spec.expression, dom[0], dom[1], n=n)
        alpha = float(np.clip(self.spec.surface_alpha, 0.05, 1.0))
        if self.spec.chart_type == "Function Mesh":
            ax.plot_wireframe(res.X, res.Y, res.Z, rstride=max(1,n//35), cstride=max(1,n//35),
                              color=self.spec.series_color, linewidth=0.55, alpha=alpha)
        else:
            surf = ax.plot_surface(res.X, res.Y, res.Z, cmap=self.cmap, linewidth=0, antialiased=True, alpha=alpha)
            self._add_colorbar(surf, "f(x,y)", ax, three_d=True)
        self.summary["expression"] = self.spec.expression
        self._finish(ax, "x", "y", "f(x,y)", "Function surface", three_d=True)

    def _render_implicit3d(self):
        ax = self.ax = self.fig.add_subplot(111, projection='3d')
        dom = parse_domain(self.spec.expression_domain, 3, default=(-3, 3))
        res = evaluate_3d(split_equation(self.spec.expression), dom[0], dom[1], dom[2],
                          n=min(max(self.spec.grid_resolution//2, 36), 90))
        finite = res.V[np.isfinite(res.V)]
        if finite.size == 0 or not (np.nanmin(finite) <= 0 <= np.nanmax(finite)):
            self._empty(ax, "The selected domain does not contain the implicit zero level.")
            self._finish(ax, "x", "y", "z", "Implicit surface", three_d=True)
            return
        try:
            from skimage.measure import marching_cubes
            V = np.nan_to_num(res.V, nan=np.nanmax(np.abs(finite))*5 if finite.size else 1.0)
            verts, faces, normals, values = marching_cubes(V, level=0.0)
            scale = np.array([(d[1]-d[0]) / max(n-1,1) for d,n in zip(dom, V.shape)])
            offset = np.array([d[0] for d in dom])
            verts = verts * scale + offset
            mesh = Poly3DCollection(verts[faces], alpha=float(np.clip(self.spec.surface_alpha,0.05,1.0)))
            mesh.set_facecolor(self.spec.series_color); mesh.set_edgecolor('none')
            ax.add_collection3d(mesh)
            ax.set_xlim(*dom[0]); ax.set_ylim(*dom[1]); ax.set_zlim(*dom[2])
            self.summary["n_faces"] = int(len(faces))
        except Exception as exc:
            self.warnings.append(f"Implicit marching cubes failed: {exc}")
            self._empty(ax, "Could not construct the implicit isosurface.")
        self.summary["expression"] = self.spec.expression
        self._finish(ax, "x", "y", "z", "Implicit surface", three_d=True)

    # -- text / categorical clouds --------------------------------------
    def _text_frequencies(self, ds: Dataset):
        from collections import Counter
        counter = Counter()
        text_data = getattr(ds, 'text_data', {}) or {}
        for values in text_data.values():
            for value in values:
                for token in str(value).replace('_',' ').split():
                    token = token.strip(".,;:!?()[]{}\"'")
                    if len(token) >= 2:
                        counter[token] += 1
        if not counter:
            for c in ds.df.columns:
                if not pd.api.types.is_numeric_dtype(ds.df[c]):
                    for value in ds.df[c].dropna().astype(str):
                        for token in value.split():
                            if len(token) >= 2: counter[token] += 1
        return counter

    def _render_wordcloud(self):
        ax = self.ax = self.fig.add_subplot(111)
        ax.axis('off')
        freq = None
        for ds in self.spec.datasets.values():
            freq = self._text_frequencies(ds)
            if freq: break
        if not freq:
            self._empty(ax, "No text/categorical tokens were found.")
            return
        try:
            from wordcloud import WordCloud
            wc = WordCloud(width=1200, height=700, background_color='white', colormap=self.cmap.name,
                           max_words=180).generate_from_frequencies(dict(freq))
            ax.imshow(wc, interpolation='bilinear')
        except Exception:
            top = freq.most_common(35)
            ax.axis('on')
            ax.barh([w for w,_ in top[::-1]], [n for _,n in top[::-1]])
            ax.set_xlabel('Frequency')
        ax.set_title(self._style('Main Title').get('text') or 'Word cloud', fontsize=self._fs('Main Title',3))
        self.summary["words"] = int(len(freq))

    def _render_bubblecloud(self):
        ax = self.ax = self.fig.add_subplot(111)
        ds = next(iter(self.spec.datasets.values()), None)
        if ds is None:
            return self._empty(ax, "No dataset loaded.")
        # labels from the first text field; weights from mapped X/first numeric column.
        labels = None
        for vals in (getattr(ds,'text_data',{}) or {}).values():
            labels = list(vals); break
        if labels is None:
            cat = next((c for c in ds.df.columns if not pd.api.types.is_numeric_dtype(ds.df[c])), None)
            labels = ds.df[cat].astype(str).tolist() if cat else None
        cw = self._col(ds,'x',0)
        if not labels or not cw:
            return self._empty(ax, "Bubble cloud needs categorical labels plus numeric weights.")
        weights = ds.df[cw].to_numpy(float)
        n = min(len(labels),len(weights),120)
        labels,weights = labels[:n],weights[:n]
        finite=np.isfinite(weights); labels=[l for l,k in zip(labels,finite) if k]; weights=weights[finite]
        if not len(weights): return self._empty(ax,"No finite bubble weights.")
        order=np.argsort(weights)[::-1][:60]; weights=weights[order]; labels=[labels[i] for i in order]
        sizes=300+2600*(np.abs(weights)/max(np.max(np.abs(weights)),1e-12))**0.8
        # deterministic golden-angle packing
        k=np.arange(len(weights)); r=np.sqrt(k+1); th=k*2.399963229728653
        x=r*np.cos(th); y=r*np.sin(th)
        sc=ax.scatter(x,y,s=sizes,c=weights,cmap=self.cmap,alpha=0.72,edgecolors='white',linewidths=1)
        for xi,yi,lab,sz in zip(x,y,labels,sizes):
            ax.text(xi,yi,str(lab)[:22],ha='center',va='center',fontsize=max(6,min(11,int(np.sqrt(sz)/7))))
        ax.set_aspect('equal'); ax.axis('off'); self._add_colorbar(sc,get_pretty_label(cw),ax)
        ax.set_title(self._style('Main Title').get('text') or 'Bubble cloud', fontsize=self._fs('Main Title',3))

    # -- additional vector engines --------------------------------------
    def _render_feather(self):
        ax = self.ax = self.fig.add_subplot(111)
        for name,ds in self.spec.datasets.items():
            cu,cv=self._col(ds,'x',0),self._col(ds,'y',1)
            if not cu or not cv: continue
            sub=self._prepare_subset(ds,[cu,cv])
            if sub.empty: continue
            u=sub[cu].to_numpy(float); v=sub[cv].to_numpy(float); x=np.arange(len(u))
            ax.quiver(x,np.zeros_like(x),u,v,angles='xy',scale_units='xy',scale=1,label=clean_series_name(name),alpha=self._alpha_for(ds))
        self._finish(ax,'Sample','Vector component',default_title='Feather plot')

    def _streamline3d(self,u,v,w,seed,steps=90,step=0.45):
        shape=np.array(u.shape,float); p=np.array(seed,float); pts=[]
        for _ in range(steps):
            idx=np.clip(np.rint(p).astype(int),0,np.array(u.shape)-1)
            vec=np.array([u[tuple(idx)],v[tuple(idx)],w[tuple(idx)]],float)
            mag=np.linalg.norm(vec)
            if not np.isfinite(mag) or mag<1e-12: break
            pts.append(p.copy()); p=p+step*vec/mag
            if np.any(p<0) or np.any(p>=shape): break
        return np.asarray(pts)

    def _render_vector3d(self):
        ax=self.ax=self.fig.add_subplot(111,projection='3d')
        ds=next(iter(self.spec.datasets.values()),None)
        if ds is None: return self._empty(ax,'No vector dataset loaded.')
        resolved=self._vector_volumes(ds)
        if resolved is None: return self._empty(ax,'Need three compatible 3-D U/V/W component arrays.')
        names,(u,v,w)=resolved; shape=u.shape
        target=10; step=tuple(max(1,int(np.ceil(n/target))) for n in shape)
        xs=np.arange(0,shape[0],step[0]); ys=np.arange(0,shape[1],step[1]); zs=np.arange(0,shape[2],step[2])
        X,Y,Z=np.meshgrid(xs,ys,zs,indexing='ij'); U=u[::step[0],::step[1],::step[2]]; V=v[::step[0],::step[1],::step[2]]; W=w[::step[0],::step[1],::step[2]]
        chart=self.spec.chart_type; alpha=float(np.clip(self.spec.surface_alpha,0.05,1.0))
        if chart in ('3D Quiver','Cone Plot'):
            mag=np.sqrt(U**2+V**2+W**2); norm=Normalize(vmin=float(np.nanmin(mag)),vmax=float(np.nanmax(mag))+1e-12)
            colors=self.cmap(norm(mag.ravel()))
            ax.quiver(X,Y,Z,U,V,W,length=max(shape)/18,normalize=True,colors=colors,alpha=alpha,
                      arrow_length_ratio=0.45 if chart=='Cone Plot' else 0.25)
        else:
            seeds=[]
            for a in np.linspace(0,shape[0]-1,4):
                for b in np.linspace(0,shape[1]-1,4):
                    seeds.append((a,b,shape[2]*0.2))
            for seed in seeds:
                pts=self._streamline3d(u,v,w,seed,steps=120)
                if len(pts)<2: continue
                lw=4.5 if chart=='Stream Tube' else 2.8
                ax.plot(pts[:,0],pts[:,1],pts[:,2],linewidth=lw,color=self.spec.series_color,alpha=alpha)
                if chart=='Stream Ribbon' and len(pts)>3:
                    ax.plot(pts[:,0]+0.18,pts[:,1],pts[:,2],linewidth=1.1,color=self.spec.series_color,alpha=alpha*0.65)
        ax.set_xlim(0,shape[0]-1); ax.set_ylim(0,shape[1]-1); ax.set_zlim(0,shape[2]-1)
        self.summary['vector_components']=names
        self._finish(ax,'X index','Y index','Z index',chart,three_d=True)

    # -- volume / voxel engines -----------------------------------------
    def _volume_data(self):
        for ds in self.spec.datasets.values():
            name,arr=self._selected_array(ds,'matrix',ndim=3)
            if arr is not None:
                return ds,name,np.asarray(arr,float)
        return None,None,None

    def _volume_level_value(self,V):
        finite=V[np.isfinite(V)]
        if finite.size==0: return 0.0
        if self.spec.volume_level is not None:
            return float(self.spec.volume_level)
        lo,hi=np.nanpercentile(finite,[35,75])
        return float(0.5*(lo+hi))

    def _render_volume(self):
        ax=self.ax=self.fig.add_subplot(111,projection='3d')
        ds,name,V=self._volume_data()
        if V is None:
            self._empty(ax,'Select/load a 3-D scalar volume.'); return
        # downsample very large arrays before interactive geometry construction
        maxdim=72
        steps=tuple(max(1,int(np.ceil(n/maxdim))) for n in V.shape)
        Vd=V[::steps[0],::steps[1],::steps[2]]
        finite=Vd[np.isfinite(Vd)]
        if finite.size==0:
            self._empty(ax,'The selected volume has no finite values.'); return
        chart=self.spec.chart_type; alpha=float(np.clip(self.spec.surface_alpha,0.05,1.0)); level=self._volume_level_value(Vd)
        if chart in ('Isosurface','Isocaps','Isonormals'):
            try:
                from skimage.measure import marching_cubes
                fill=np.nanmedian(finite)
                verts,faces,normals,vals=marching_cubes(np.nan_to_num(Vd,nan=fill),level=level)
                mesh=Poly3DCollection(verts[faces],alpha=alpha)
                mesh.set_facecolor(self.spec.series_color); mesh.set_edgecolor('none'); ax.add_collection3d(mesh)
                ax.set_xlim(0,Vd.shape[0]-1); ax.set_ylim(0,Vd.shape[1]-1); ax.set_zlim(0,Vd.shape[2]-1)
                if chart=='Isonormals' and len(verts):
                    k=np.linspace(0,len(verts)-1,min(220,len(verts))).astype(int)
                    ax.quiver(verts[k,0],verts[k,1],verts[k,2],normals[k,0],normals[k,1],normals[k,2],
                              length=max(Vd.shape)/28,color='#2C3E50',linewidth=0.5,alpha=min(alpha+0.15,1))
                if chart=='Isocaps':
                    # boundary contours on three orthogonal faces
                    try:
                        ax.contour(Vd[:,:,0].T,levels=[level],zdir='z',offset=0,colors='#34495E',linewidths=1)
                        ax.contour(Vd[:,0,:].T,levels=[level],zdir='y',offset=0,colors='#34495E',linewidths=1)
                        ax.contour(Vd[0,:,:].T,levels=[level],zdir='x',offset=0,colors='#34495E',linewidths=1)
                    except Exception: pass
                self.summary['n_faces']=int(len(faces))
            except Exception as exc:
                self.warnings.append(f'Marching cubes failed: {exc}'); self._empty(ax,'Could not build isosurface at the selected level.')
        elif chart=='Volume Show':
            # RGBA voxel rendering, aggressively downsampled for responsiveness.
            maxvox=34; st=tuple(max(1,int(np.ceil(n/maxvox))) for n in Vd.shape); A=Vd[::st[0],::st[1],::st[2]]
            f=A[np.isfinite(A)]; lo,hi=np.nanpercentile(f,[5,98]); norm=np.clip((np.nan_to_num(A,nan=lo)-lo)/max(hi-lo,1e-12),0,1)
            filled=norm>0.12; colors=self.cmap(norm); colors[...,3]=alpha*np.clip(norm**1.4,0.06,0.88)
            ax.voxels(filled,facecolors=colors,edgecolor=None)
            ax.set_xlim(0,A.shape[0]);ax.set_ylim(0,A.shape[1]);ax.set_zlim(0,A.shape[2])
        else:
            nx,ny,nz=Vd.shape; ix,iy,iz=nx//2,ny//2,nz//2
            # Orthogonal surfaces coloured by the scalar slices.
            norm=Normalize(vmin=float(np.nanmin(finite)),vmax=float(np.nanmax(finite))+1e-12)
            yy,zz=np.meshgrid(np.arange(ny),np.arange(nz),indexing='ij')
            xx=np.full_like(yy,ix,float); ax.plot_surface(xx,yy,zz,facecolors=self.cmap(norm(Vd[ix,:,:])),shade=False,alpha=alpha)
            xx2,zz2=np.meshgrid(np.arange(nx),np.arange(nz),indexing='ij'); yy2=np.full_like(xx2,iy,float)
            ax.plot_surface(xx2,yy2,zz2,facecolors=self.cmap(norm(Vd[:,iy,:])),shade=False,alpha=alpha)
            xx3,yy3=np.meshgrid(np.arange(nx),np.arange(ny),indexing='ij'); zz3=np.full_like(xx3,iz,float)
            ax.plot_surface(xx3,yy3,zz3,facecolors=self.cmap(norm(Vd[:,:,iz])),shade=False,alpha=alpha)
            if chart=='Contour Slice':
                # add contour projections near the orthogonal mid-planes
                try:
                    ax.contour(Vd[:,:,iz].T,levels=10,zdir='z',offset=iz,cmap=self.cmap,linewidths=0.7)
                except Exception: pass
            ax.set_xlim(0,nx-1);ax.set_ylim(0,ny-1);ax.set_zlim(0,nz-1)
        self.summary.update({'volume':name,'shape':tuple(map(int,V.shape)),'level':float(level)})
        self._finish(ax,'X index','Y index','Z index',f'{chart}: {name}',three_d=True)

    def _render_patch(self):
        ax=self.ax=self.fig.add_subplot(111,projection='3d')
        ds=next(iter(self.spec.datasets.values()),None)
        if ds is None: return self._empty(ax,'No patch dataset loaded.')
        topo=getattr(ds,'topology',{}) or {}
        verts=np.asarray(topo.get('vertices')) if topo.get('vertices') is not None else None
        faces=np.asarray(topo.get('faces')) if topo.get('faces') is not None else None
        if verts is not None and faces is not None and verts.ndim==2 and verts.shape[1]>=3:
            poly=Poly3DCollection(verts[faces.astype(int),:3],alpha=float(np.clip(self.spec.surface_alpha,0.05,1)))
            poly.set_facecolor(self.spec.series_color);poly.set_edgecolor('#34495E');ax.add_collection3d(poly);ax.auto_scale_xyz(verts[:,0],verts[:,1],verts[:,2])
        else:
            cx,cy,cz=self._col(ds,'x',0),self._col(ds,'y',1),self._col(ds,'z',2)
            if not all((cx,cy,cz)): return self._empty(ax,'Patch needs vertices/faces or X/Y/Z point columns.')
            sub=self._prepare_subset(ds,[cx,cy,cz]); tri=Triangulation(sub[cx].to_numpy(float),sub[cy].to_numpy(float))
            ax.plot_trisurf(tri,sub[cz].to_numpy(float),cmap=self.cmap,alpha=float(np.clip(self.spec.surface_alpha,0.05,1)))
        self._finish(ax,get_pretty_label(self.spec.mappings.get('x')) or 'X',get_pretty_label(self.spec.mappings.get('y')) or 'Y',get_pretty_label(self.spec.mappings.get('z')) or 'Z','Patch',three_d=True)

    # -- animation initial-frame rendering -------------------------------
    def _render_animation_static(self):
        chart=self.spec.chart_type; three=chart=='Comet 3D'
        ax=self.ax=self.fig.add_subplot(111,projection='3d') if three else self.fig.add_subplot(111)
        payload=None
        for name,ds in self.spec.datasets.items():
            cx,cy=self._col(ds,'x',0),self._col(ds,'y',1); cz=self._col(ds,'z',2) if three else None
            cols=[c for c in (cx,cy,cz) if c]
            if len(cols)<(3 if three else 2): continue
            sub=self._prepare_subset(ds,cols)
            if sub.empty: continue
            x=sub[cx].to_numpy(float); y=sub[cy].to_numpy(float); z=sub[cz].to_numpy(float) if three else None
            if three:
                line,=ax.plot(x[:1],y[:1],z[:1],color=self.spec.series_color,linewidth=self.spec.line_width)
                head=ax.scatter([x[0]],[y[0]],[z[0]],s=45,color=self.spec.series_color)
            else:
                line,=ax.plot(x[:1],y[:1],color=self.spec.series_color,linewidth=self.spec.line_width)
                head,=ax.plot([x[0]],[y[0]],marker='o',color=self.spec.series_color,linestyle='none')
            payload={'x':x,'y':y,'z':z,'three_d':three,'mode':chart,'line':line,'head':head,'name':name}
            # frame the complete trajectory while only drawing frame 0
            if three:
                ax.set_xlim(np.nanmin(x),np.nanmax(x));ax.set_ylim(np.nanmin(y),np.nanmax(y));ax.set_zlim(np.nanmin(z),np.nanmax(z))
            else:
                ax.set_xlim(np.nanmin(x),np.nanmax(x));ax.set_ylim(np.nanmin(y),np.nanmax(y))
            break
        if payload is None:
            self._empty(ax,'Map the required numeric series for animation.')
        else:
            self.summary['animation']={k:v for k,v in payload.items() if k not in ('line','head')}
            self.summary['n_points']=len(payload['x'])
        self._finish(ax,get_pretty_label(self.spec.mappings.get('x')) or 'X',get_pretty_label(self.spec.mappings.get('y')) or 'Y',get_pretty_label(self.spec.mappings.get('z')) or 'Z',chart,three_d=three)

    # -- line -----------------------------------------------------------
    def _render_line(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        ax2 = None
        first_xy = None
        cx_lab = cy_lab = ""
        for name, ds in sp.datasets.items():
            cx, cy, cw = self._col(ds, 'x', 0), self._col(ds, 'y', 1), self._col(ds, 'w')
            if not cx or not cy:
                continue
            cols = [cx, cy] + ([cw] if cw and cw not in (cx,cy) else [])
            if sp.secondary_y and sp.secondary_y in ds.df.columns and sp.secondary_y not in cols:
                cols.append(sp.secondary_y)
            sub = self._prepare_subset(ds, cols)
            if sub.empty:
                continue
            if not self.for_export:
                sub = self._downsample(sub, cx, cy, preserve_line=True)
            x, y = sub[cx].to_numpy(float), sub[cy].to_numpy(float)
            label = clean_series_name(name)
            if cw and cw in sub.columns and cw not in (cx, cy):
                w = sub[cw].to_numpy(float)
                pts = np.column_stack([x, y]).reshape(-1, 1, 2)
                segs = np.concatenate([pts[:-1], pts[1:]], axis=1)
                norm = self._norm_for("w", w)
                lc = LineCollection(segs, cmap=self.cmap, norm=norm, linewidth=sp.line_width, label=label,
                                    alpha=self._alpha_for(ds, 1.0))
                lc.set_array(0.5 * (w[:-1] + w[1:]))
                ax.add_collection(lc); ax.autoscale_view(); self._add_colorbar(lc, get_pretty_label(cw))
            else:
                ax.plot(x, y, color=sp.series_color, label=label, linewidth=sp.line_width,
                        marker=sp.marker or None, markersize=4 if sp.marker else None,
                        alpha=self._alpha_for(ds, 0.95))
            if sp.secondary_y and sp.secondary_y in sub.columns:
                if ax2 is None:
                    ax2 = ax.twinx()
                y2 = sub[sp.secondary_y].to_numpy(float)
                ax2.plot(x, y2, linestyle='--', linewidth=max(sp.line_width*0.9,0.6), color='#D35400',
                         alpha=self._alpha_for(ds,0.9), label=f"{label} — {get_pretty_label(sp.secondary_y)}")
                ax2.set_ylabel(get_pretty_label(sp.secondary_y), color='#D35400', fontsize=self._fs("Y-Axis Label",1))
                ax2.tick_params(axis='y', colors='#D35400')
            self.summary["n_points"] += x.size
            if first_xy is None:
                first_xy, cx_lab, cy_lab = (x, y), cx, cy
        if first_xy is None:
            self._empty(ax, "No valid data columns mapped for current view.")
        if ax2 is not None:
            h2,l2=ax2.get_legend_handles_labels()
            for h,l in zip(h2,l2):
                ax.plot([],[],linestyle='--',color='#D35400',label=l)
        self._finish(ax, get_pretty_label(cx_lab or sp.mappings.get('x')), get_pretty_label(cy_lab or sp.mappings.get('y')), xy=first_xy)

    # -- additional line/distribution/discrete views ------------------
    def _render_line3d(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111, projection='3d')
        plotted, labs = False, ("", "", "")
        for name, ds in sp.datasets.items():
            cx, cy, cz = self._col(ds, 'x', 0), self._col(ds, 'y', 1), self._col(ds, 'z', 2)
            if not (cx and cy and cz) or len({cx, cy, cz}) < 3:
                continue
            sub = self._prepare_subset(ds, [cx, cy, cz])
            if sub.empty:
                continue
            if not self.for_export:
                sub = self._downsample(sub, cx, cy, preserve_line=True)
            sub = sub.sort_values(cx, kind='stable')
            ax.plot(sub[cx], sub[cy], sub[cz], linewidth=1.8, color=sp.series_color,
                    alpha=self._alpha_for(ds, sp.surface_alpha), label=clean_series_name(name))
            self.summary["n_points"] += len(sub)
            plotted, labs = True, (cx, cy, cz)
        if not plotted:
            self._empty(ax, "3D line needs three distinct numeric mappings.")
        self._finish(ax, get_pretty_label(labs[0]), get_pretty_label(labs[1]), get_pretty_label(labs[2]), three_d=True)

    def _render_stairs(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        first_xy, labs = None, ("", "")
        for name, ds in sp.datasets.items():
            cx, cy = self._col(ds, 'x', 0), self._col(ds, 'y', 1)
            if not (cx and cy) or cx == cy:
                continue
            sub = self._prepare_subset(ds, [cx, cy]).sort_values(cx, kind='stable')
            if sub.empty:
                continue
            x, y = sub[cx].to_numpy(float), sub[cy].to_numpy(float)
            ax.step(x, y, where='mid', color=sp.series_color, linewidth=1.7,
                    alpha=self._alpha_for(ds, 0.95), label=clean_series_name(name))
            self.summary["n_points"] += len(sub)
            if first_xy is None:
                first_xy, labs = (x, y), (cx, cy)
        if first_xy is None:
            self._empty(ax, "Stairs needs X and Y mappings.")
        self._finish(ax, get_pretty_label(labs[0]), get_pretty_label(labs[1]), xy=first_xy)

    def _render_errorbar(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        first_xy, labs = None, ("", "")
        for name, ds in sp.datasets.items():
            cx, cy, ce = self._col(ds, 'x', 0), self._col(ds, 'y', 1), self._col(ds, 'z')
            if not (cx and cy) or cx == cy:
                continue
            cols = [cx, cy] + ([ce] if ce and ce not in (cx, cy) else [])
            sub = self._prepare_subset(ds, cols).sort_values(cx, kind='stable')
            if sub.empty:
                continue
            x, y = sub[cx].to_numpy(float), sub[cy].to_numpy(float)
            err = np.abs(sub[ce].to_numpy(float)) if ce and ce in sub and ce not in (cx, cy) else None
            if err is None:
                # Never invent uncertainty. When no error series is mapped, keep the
                # measured values visible but report that error bars are unavailable.
                ax.plot(x, y, 'o-', linewidth=1.2, markersize=3.5,
                        color=sp.series_color, alpha=self._alpha_for(ds, 0.95),
                        label=f"{clean_series_name(name)} — no uncertainty mapped")
                self.summary["notes"].append(
                    "No error/uncertainty column is mapped; data are shown without error bars."
                )
            else:
                ax.errorbar(x, y, yerr=err, fmt='o-', capsize=3, linewidth=1.2, markersize=3.5,
                            color=sp.series_color, alpha=self._alpha_for(ds, 0.95), label=clean_series_name(name))
            self.summary["n_points"] += len(sub)
            if first_xy is None:
                first_xy, labs = (x, y), (cx, cy)
        if first_xy is None:
            self._empty(ax, "Error bar plot needs X and Y mappings; map Z to an uncertainty column when available.")
        self._finish(ax, get_pretty_label(labs[0]), get_pretty_label(labs[1]), xy=first_xy)

    def _render_area(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        first_xy, labs = None, ("", "")
        for name, ds in sp.datasets.items():
            cx, cy = self._col(ds, 'x', 0), self._col(ds, 'y', 1)
            if not (cx and cy) or cx == cy:
                continue
            sub = self._prepare_subset(ds, [cx, cy]).sort_values(cx, kind='stable')
            if sub.empty:
                continue
            x, y = sub[cx].to_numpy(float), sub[cy].to_numpy(float)
            alpha = self._alpha_for(ds, 0.65)
            ax.fill_between(x, 0, y, color=sp.series_color, alpha=alpha, label=clean_series_name(name))
            ax.plot(x, y, color=sp.series_color, linewidth=1.2, alpha=min(1.0, alpha + 0.25))
            self.summary["n_points"] += len(sub)
            if first_xy is None:
                first_xy, labs = (x, y), (cx, cy)
        if first_xy is None:
            self._empty(ax, "Area plot needs X and Y mappings.")
        self._finish(ax, get_pretty_label(labs[0]), get_pretty_label(labs[1]), xy=first_xy)

    def _render_stacked_lines(self):
        sp = self.spec
        ds = next(iter(sp.datasets.values()), None)
        ax = self.ax = self.fig.add_subplot(111)
        if ds is None or len(ds.numeric_columns) < 3:
            self._empty(ax, "Stacked lines need at least three numeric series.")
            self._finish(ax)
            return
        cx = self._col(ds, 'x', 0)
        ys = [c for c in ds.numeric_columns if c != cx][:8]
        sub = self._prepare_subset(ds, [cx] + ys).sort_values(cx, kind='stable')
        if sub.empty:
            self._empty(ax, "No finite values available for stacked lines.")
            self._finish(ax)
            return
        x = sub[cx].to_numpy(float)
        base = np.zeros(len(sub), float)
        for c in ys:
            y = sub[c].to_numpy(float)
            ax.fill_between(x, base, base + y, alpha=self._alpha_for(ds, 0.35), label=get_pretty_label(c))
            base = base + y
        self.summary["n_points"] += len(sub) * len(ys)
        self._finish(ax, get_pretty_label(cx), "Stacked value")

    def _render_histogram(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        plotted = False
        for name, ds in sp.datasets.items():
            cx = self._col(ds, 'x', 0)
            if not cx:
                continue
            sub = self._prepare_subset(ds, [cx])
            if sub.empty:
                continue
            v = sub[cx].to_numpy(float)
            bins = int(np.clip(np.sqrt(len(v)), 8, 80))
            ax.hist(v, bins=bins, color=sp.series_color, alpha=self._alpha_for(ds, 0.75), label=clean_series_name(name))
            plotted = True; self.summary["n_points"] += len(v)
        if not plotted:
            self._empty(ax, "Histogram needs one numeric mapping.")
        self._finish(ax, get_pretty_label(sp.mappings.get('x')), "Count")

    def _render_hist2d(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        ds = next(iter(sp.datasets.values()), None)
        if ds is None:
            self._empty(ax, "No dataset selected."); self._finish(ax); return
        cx, cy = self._col(ds, 'x', 0), self._col(ds, 'y', 1)
        if not cx or not cy or cx == cy:
            self._empty(ax, "2D histogram needs distinct X and Y mappings."); self._finish(ax); return
        sub = self._prepare_subset(ds, [cx, cy])
        if sub.empty:
            self._empty(ax, "No finite data for 2D histogram."); self._finish(ax); return
        bins = int(np.clip(np.sqrt(len(sub)) / 2, 12, 80))
        h = ax.hist2d(sub[cx], sub[cy], bins=bins, cmap=self.cmap)
        self._add_colorbar(h[3], "Count", ax=ax)
        self.summary["n_points"] += len(sub)
        self._finish(ax, get_pretty_label(cx), get_pretty_label(cy))

    def _render_box(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        ds = next(iter(sp.datasets.values()), None)
        if ds is None or not ds.numeric_columns:
            self._empty(ax, "Box plot needs numeric data."); self._finish(ax); return
        cols = ([self._col(ds, 'x', 0)] if self._col(ds, 'x', 0) else [])
        cols += [c for c in ds.numeric_columns if c not in cols][:7]
        data, labels = [], []
        for c in cols:
            v = ds.df[c].to_numpy(float); v = v[np.isfinite(v)]
            if v.size:
                data.append(v); labels.append(get_pretty_label(c))
        if not data:
            self._empty(ax, "No finite values for box plot."); self._finish(ax); return
        try:
            # Matplotlib >= 3.9 renamed ``labels`` to ``tick_labels``.
            ax.boxplot(data, tick_labels=labels, showfliers=not sp.outlier_mask)
        except TypeError:
            ax.boxplot(data, labels=labels, showfliers=not sp.outlier_mask)
        self.summary["n_points"] += sum(map(len, data))
        self._finish(ax, "Series", "Value")
        ax.tick_params(axis='x', rotation=25)

    def _render_violin(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        ds = next(iter(sp.datasets.values()), None)
        if ds is None or not ds.numeric_columns:
            self._empty(ax, "Violin plot needs numeric data."); self._finish(ax); return
        cols = ds.numeric_columns[:8]
        data, labels = [], []
        for c in cols:
            v = ds.df[c].to_numpy(float); v = v[np.isfinite(v)]
            if v.size > 1:
                data.append(v); labels.append(get_pretty_label(c))
        if not data:
            self._empty(ax, "Not enough finite values for violin plot."); self._finish(ax); return
        parts = ax.violinplot(data, showmeans=True, showmedians=True, showextrema=True)
        for body in parts['bodies']:
            body.set_alpha(self._alpha_for(ds, 0.55))
        ax.set_xticks(np.arange(1, len(labels) + 1)); ax.set_xticklabels(labels, rotation=25, ha='right')
        self.summary["n_points"] += sum(map(len, data))
        self._finish(ax, "Series", "Value")

    def _render_raincloud(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        ds = next(iter(sp.datasets.values()), None)
        if ds is None:
            self._empty(ax, "No dataset selected."); self._finish(ax); return
        cx = self._col(ds, 'x', 0)
        if not cx:
            self._empty(ax, "Raincloud plot needs a numeric mapping."); self._finish(ax); return
        v = ds.df[cx].to_numpy(float); v = v[np.isfinite(v)]
        if v.size < 3:
            self._empty(ax, "Raincloud plot needs at least three finite values."); self._finish(ax); return
        parts = ax.violinplot([v], positions=[1], widths=0.65, showmeans=False, showmedians=False, showextrema=False)
        for body in parts['bodies']:
            body.set_alpha(self._alpha_for(ds, 0.4))
        rng = np.random.default_rng(0)
        jitter = 1.18 + rng.normal(0, 0.025, len(v))
        ax.scatter(jitter, v, s=10, alpha=self._alpha_for(ds, 0.45), color=sp.series_color)
        ax.boxplot([v], positions=[0.9], widths=0.12, showfliers=False)
        ax.set_xlim(0.5, 1.5); ax.set_xticks([1]); ax.set_xticklabels([get_pretty_label(cx)])
        self.summary["n_points"] += len(v)
        self._finish(ax, "", "Value")

    def _render_swarm(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        first_xy = None
        rng = np.random.default_rng(0)
        for name, ds in sp.datasets.items():
            cx, cy = self._col(ds, 'x', 0), self._col(ds, 'y', 1)
            if not cx or not cy:
                continue
            sub = self._downsample(self._prepare_subset(ds, [cx, cy]))
            if sub.empty:
                continue
            x, y = sub[cx].to_numpy(float), sub[cy].to_numpy(float)
            span = np.ptp(x) or 1.0
            xj = x + rng.normal(0, 0.006 * span, len(x))
            ax.scatter(xj, y, s=18, color=sp.series_color, alpha=self._alpha_for(ds, 0.72), label=clean_series_name(name))
            first_xy = first_xy or (x, y)
            self.summary["n_points"] += len(x)
        if first_xy is None:
            self._empty(ax, "Swarm plot needs X and Y mappings.")
        self._finish(ax, get_pretty_label(sp.mappings.get('x')), get_pretty_label(sp.mappings.get('y')), xy=first_xy)

    def _render_plot_matrix(self):
        sp = self.spec
        ds = next(iter(sp.datasets.values()), None)
        if ds is None or len(ds.numeric_columns) < 2:
            self.ax = self.fig.add_subplot(111); self._empty(self.ax, "Plot matrix needs ≥2 numeric columns."); return
        cols = ds.numeric_columns[:4]
        n = len(cols)
        for r, cr in enumerate(cols):
            for c, cc in enumerate(cols):
                ax = self.fig.add_subplot(n, n, r * n + c + 1)
                sub = self._downsample(self._prepare_subset(ds, [cc, cr])) if cc != cr else self._prepare_subset(ds, [cc])
                if cc == cr:
                    ax.hist(sub[cc], bins=18, alpha=self._alpha_for(ds, 0.7), color=sp.series_color)
                else:
                    ax.scatter(sub[cc], sub[cr], s=4, alpha=self._alpha_for(ds, 0.45), color=sp.series_color)
                if r == n - 1: ax.set_xlabel(get_pretty_label(cc), fontsize=max(sp.font_size - 2, 6))
                else: ax.set_xticklabels([])
                if c == 0: ax.set_ylabel(get_pretty_label(cr), fontsize=max(sp.font_size - 2, 6))
                else: ax.set_yticklabels([])
                ax.grid(sp.grid_visible, alpha=0.2)
        self.ax = self.fig.axes[0] if self.fig.axes else None
        self.summary["n_points"] += len(ds.df)

    def _render_scatter_marginals(self):
        sp = self.spec
        ds = next(iter(sp.datasets.values()), None)
        if ds is None:
            self.ax = self.fig.add_subplot(111); self._empty(self.ax, "No dataset selected."); return
        cx, cy = self._col(ds, 'x', 0), self._col(ds, 'y', 1)
        if not cx or not cy or cx == cy:
            self.ax = self.fig.add_subplot(111); self._empty(self.ax, "Scatter + marginals needs X and Y."); return
        sub = self._downsample(self._prepare_subset(ds, [cx, cy]))
        gs = self.fig.add_gridspec(4, 4, hspace=0.08, wspace=0.08)
        ax = self.ax = self.fig.add_subplot(gs[1:, :3])
        axx = self.fig.add_subplot(gs[0, :3], sharex=ax)
        axy = self.fig.add_subplot(gs[1:, 3], sharey=ax)
        ax.scatter(sub[cx], sub[cy], s=12, alpha=self._alpha_for(ds, 0.6), color=sp.series_color)
        axx.hist(sub[cx], bins=24, alpha=0.65, color=sp.series_color)
        axy.hist(sub[cy], bins=24, orientation='horizontal', alpha=0.65, color=sp.series_color)
        axx.tick_params(labelbottom=False); axy.tick_params(labelleft=False)
        self._apply_scale(ax); self._apply_grid(ax); self._apply_styling(ax, get_pretty_label(cx), get_pretty_label(cy))
        self.summary["n_points"] += len(sub)

    def _render_bar(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        plotted = False
        horizontal = sp.chart_type == "Horizontal Bar"
        for name, ds in sp.datasets.items():
            cx, cy = self._col(ds, 'x', 0), self._col(ds, 'y', 1)
            if not cx or not cy or cx == cy:
                continue
            sub = self._prepare_subset(ds, [cx, cy])
            if sub.empty:
                continue
            # Keep bars readable on large datasets by binning X and averaging Y.
            if len(sub) > 80:
                order = np.argsort(sub[cx].to_numpy(float), kind='stable')
                sub = sub.iloc[order]
                # Split integer positions rather than the DataFrame itself.
                # NumPy's DataFrame fallback calls deprecated swapaxes(), which
                # now emits a FutureWarning on every large bar preview.
                parts = np.array_split(np.arange(len(sub)), min(40, len(sub)))
                groups = [sub.iloc[idx] for idx in parts if len(idx)]
                x = np.array([g[cx].mean() for g in groups]); y = np.array([g[cy].mean() for g in groups])
            else:
                x, y = sub[cx].to_numpy(float), sub[cy].to_numpy(float)
            if horizontal:
                ax.barh(np.arange(len(y)), y, alpha=self._alpha_for(ds, 0.82), label=clean_series_name(name))
                ax.set_yticks(np.arange(len(y))); ax.set_yticklabels([f"{v:.3g}" for v in x])
            else:
                ax.bar(np.arange(len(y)), y, alpha=self._alpha_for(ds, 0.82), label=clean_series_name(name))
                ax.set_xticks(np.arange(len(y))); ax.set_xticklabels([f"{v:.3g}" for v in x], rotation=35, ha='right')
            plotted = True; self.summary["n_points"] += len(y)
            break
        if not plotted:
            self._empty(ax, "Bar chart needs X and Y mappings.")
        self._finish(ax, get_pretty_label(sp.mappings.get('x')), get_pretty_label(sp.mappings.get('y')))

    def _render_bar3d(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111, projection='3d')
        ds = next(iter(sp.datasets.values()), None)
        if ds is None:
            self._empty(ax, "No dataset selected."); self._finish(ax, three_d=True); return
        cx, cy, cz = self._col(ds, 'x', 0), self._col(ds, 'y', 1), self._col(ds, 'z', 2)
        if not (cx and cy and cz):
            self._empty(ax, "3D bar needs X, Y and Z."); self._finish(ax, three_d=True); return
        sub = self._downsample(self._prepare_subset(ds, [cx, cy, cz])).iloc[:800]
        x, y, z = sub[cx].to_numpy(float), sub[cy].to_numpy(float), sub[cz].to_numpy(float)
        dx = (np.ptp(x) or 1) / max(np.sqrt(len(x)), 12) * 0.6
        dy = (np.ptp(y) or 1) / max(np.sqrt(len(y)), 12) * 0.6
        ax.bar3d(x, y, np.zeros_like(z), dx, dy, z, shade=True, alpha=self._alpha_for(ds, sp.surface_alpha))
        self.summary["n_points"] += len(sub)
        self._finish(ax, get_pretty_label(cx), get_pretty_label(cy), get_pretty_label(cz), three_d=True)

    def _render_stem(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        ds = next(iter(sp.datasets.values()), None)
        if ds is None:
            self._empty(ax, "No dataset selected."); self._finish(ax); return
        cx, cy = self._col(ds, 'x', 0), self._col(ds, 'y', 1)
        if not cx or not cy:
            self._empty(ax, "Stem needs X and Y."); self._finish(ax); return
        sub = self._prepare_subset(ds, [cx, cy]).sort_values(cx, kind='stable')
        markerline, stemlines, baseline = ax.stem(sub[cx], sub[cy], linefmt='-', markerfmt='o', basefmt='-')
        markerline.set_alpha(self._alpha_for(ds, 0.95)); stemlines.set_alpha(self._alpha_for(ds, 0.75))
        self.summary["n_points"] += len(sub)
        self._finish(ax, get_pretty_label(cx), get_pretty_label(cy), xy=(sub[cx].to_numpy(float), sub[cy].to_numpy(float)))

    def _render_stem3d(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111, projection='3d')
        ds = next(iter(sp.datasets.values()), None)
        if ds is None:
            self._empty(ax, "No dataset selected."); self._finish(ax, three_d=True); return
        cx, cy, cz = self._col(ds, 'x', 0), self._col(ds, 'y', 1), self._col(ds, 'z', 2)
        if not (cx and cy and cz):
            self._empty(ax, "3D stem needs X, Y and Z."); self._finish(ax, three_d=True); return
        sub = self._downsample(self._prepare_subset(ds, [cx, cy, cz])).iloc[:2500]
        alpha = self._alpha_for(ds, sp.surface_alpha)
        for x, y, z in zip(sub[cx], sub[cy], sub[cz]):
            ax.plot([x, x], [y, y], [0, z], color=sp.series_color, alpha=alpha * 0.6, linewidth=0.7)
        ax.scatter(sub[cx], sub[cy], sub[cz], s=10, color=sp.series_color, alpha=alpha)
        self.summary["n_points"] += len(sub)
        self._finish(ax, get_pretty_label(cx), get_pretty_label(cy), get_pretty_label(cz), three_d=True)

    def _render_pie(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        ds = next(iter(sp.datasets.values()), None)
        if ds is None:
            self._empty(ax, "No dataset selected."); return
        cx = self._col(ds, 'x', 0)
        if not cx:
            self._empty(ax, "Pie chart needs a numeric value column."); return
        v = ds.df[cx].to_numpy(float); v = v[np.isfinite(v)]
        v = np.abs(v)
        if v.size == 0 or np.sum(v) <= 0:
            self._empty(ax, "Pie values must contain non-zero finite magnitudes."); return
        if v.size > 12:
            order = np.argsort(v)[::-1]
            top = v[order[:11]]; rest = float(v[order[11:]].sum()); v = np.r_[top, rest]
            labels = [f"{i + 1}" for i in range(len(top))] + ["Other"]
        else:
            labels = [f"{i + 1}" for i in range(len(v))]
        wedge = {'width': 0.48} if sp.chart_type == "Donut" else None
        ax.pie(v, labels=labels, autopct='%1.1f%%', startangle=90, wedgeprops=wedge)
        ax.set_title(get_pretty_label(cx), fontsize=self._fs("Main Title", 2))
        self.summary["n_points"] += len(v)

    def _render_parallel(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        ds = next(iter(sp.datasets.values()), None)
        if ds is None or len(ds.numeric_columns) < 3:
            self._empty(ax, "Parallel coordinates need ≥3 numeric columns."); self._finish(ax); return
        cols = ds.numeric_columns[:8]
        sub = self._downsample(self._prepare_subset(ds, cols)).iloc[:600]
        vals = sub[cols].to_numpy(float)
        lo = np.nanmin(vals, axis=0); hi = np.nanmax(vals, axis=0); span = np.where(hi > lo, hi - lo, 1.0)
        norm = (vals - lo) / span
        xx = np.arange(len(cols))
        alpha = self._alpha_for(ds, 0.15)
        for row in norm:
            ax.plot(xx, row, color=sp.series_color, alpha=alpha, linewidth=0.7)
        ax.set_xticks(xx); ax.set_xticklabels([get_pretty_label(c) for c in cols], rotation=25, ha='right')
        ax.set_ylim(0, 1)
        self.summary["n_points"] += len(sub)
        self._finish(ax, "Variables", "Normalised value")

    def _render_spy(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        ds = next(iter(sp.datasets.values()), None)
        if ds is None:
            self._empty(ax, "No dataset selected."); self._finish(ax); return
        key = sp.mappings.get('matrix')
        if key and key in ds.matrices:
            mat = np.asarray(ds.matrices[key])
        else:
            cols = ds.numeric_columns[:20]
            mat = ds.df[cols].to_numpy(float) if cols else np.empty((0, 0))
        if mat.size == 0:
            self._empty(ax, "Spy plot needs a matrix or numeric data."); self._finish(ax); return
        ax.spy(np.nan_to_num(mat) != 0, markersize=max(0.5, min(5.0, 800 / max(mat.shape))))
        self.summary["n_points"] += int(mat.size)
        self._finish(ax, "Column", "Row")

    def _render_polar(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111, projection='polar')
        ds = next(iter(sp.datasets.values()), None)
        if ds is None:
            self._empty(ax, "No dataset selected."); return
        cx, cy = self._col(ds, 'x', 0), self._col(ds, 'y', 1)
        if not cx:
            self._empty(ax, "Polar plot needs an angle mapping."); return
        if sp.chart_type in ("Polar Histogram", "Wind Rose"):
            v = ds.df[cx].to_numpy(float); v = v[np.isfinite(v)]
            ax.hist(v, bins=24, alpha=self._alpha_for(ds, 0.7))
            self.summary["n_points"] += len(v); return
        if not cy:
            self._empty(ax, "Polar line/scatter needs angle and radius mappings."); return
        sub = self._prepare_subset(ds, [cx, cy])
        th, rr = sub[cx].to_numpy(float), sub[cy].to_numpy(float)
        if sp.chart_type == "Polar Line":
            order = np.argsort(th); ax.plot(th[order], rr[order], color=sp.series_color, alpha=self._alpha_for(ds, 0.95))
        elif sp.chart_type == "Compass":
            ax.quiver(np.arctan2(rr, th), np.zeros_like(rr), np.zeros_like(rr), np.hypot(th, rr), alpha=self._alpha_for(ds, 0.75))
        else:
            sizes = np.full(len(sub), 22.0)
            if sp.chart_type == "Polar Bubble":
                cv = self._col(ds, 'v')
                if cv and cv in ds.df.columns:
                    sub = self._prepare_subset(ds, [cx, cy, cv])
                    th, rr = sub[cx].to_numpy(float), sub[cy].to_numpy(float)
                    vv = np.abs(sub[cv].to_numpy(float)); den = np.ptp(vv) or 1.0
                    sizes = 18 + 120 * (vv - vv.min()) / den
            ax.scatter(th, rr, s=sizes, color=sp.series_color, alpha=self._alpha_for(ds, 0.75))
        self.summary["n_points"] += len(sub)

    def _render_geo(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        ds = next(iter(sp.datasets.values()), None)
        if ds is None:
            self._empty(ax, "No dataset selected."); self._finish(ax); return
        cols = {str(c).lower(): c for c in ds.df.columns}
        lat = next((v for k, v in cols.items() if k in ('lat', 'latitude') or 'latitude' in k), None)
        lon = next((v for k, v in cols.items() if k in ('lon', 'lng', 'longitude') or 'longitude' in k), None)
        if lat is None or lon is None:
            self._empty(ax, "Geographic views require latitude and longitude columns."); self._finish(ax); return
        extra = self._col(ds, 'v') or self._col(ds, 'w')
        use = [lon, lat] + ([extra] if extra and extra not in (lon, lat) else [])
        sub = self._prepare_subset(ds, use)
        x, y = sub[lon].to_numpy(float), sub[lat].to_numpy(float)
        if sp.chart_type == "Geo Line":
            ax.plot(x, y, color=sp.series_color, alpha=self._alpha_for(ds, 0.9))
        elif sp.chart_type == "Geo Density":
            hb = ax.hexbin(x, y, gridsize=35, cmap=self.cmap, mincnt=1); self._add_colorbar(hb, "Count", ax=ax)
        else:
            sizes = np.full(len(sub), 28.0)
            if extra and extra in sub.columns:
                v = np.abs(sub[extra].to_numpy(float)); den = np.ptp(v) or 1.0; sizes = 16 + 90 * (v - v.min()) / den
            ax.scatter(x, y, s=sizes, color=sp.series_color, alpha=self._alpha_for(ds, 0.68), edgecolors='white', linewidths=0.3)
        ax.set_xlim(max(-180, float(np.nanmin(x)) - 1), min(180, float(np.nanmax(x)) + 1))
        ax.set_ylim(max(-90, float(np.nanmin(y)) - 1), min(90, float(np.nanmax(y)) + 1))
        self.summary["notes"].append("Geographic view uses latitude/longitude coordinates without a basemap.")
        self.summary["n_points"] += len(sub)
        self._finish(ax, "Longitude", "Latitude")

    # -- 4D/5D scatter ------------------------------------------------
    def _render_scatter(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        first_xy = None
        cx_lab = cy_lab = ""
        for name, ds in sp.datasets.items():
            cx, cy, cw, cv = self._col(ds, 'x', 0), self._col(ds, 'y', 1), self._col(ds, 'w'), self._col(ds, 'v')
            if not cx or not cy:
                continue
            cols = [cx, cy] + [c for c in (cw, cv) if c and c not in (cx, cy)]
            raw = self._prepare_subset(ds, list(dict.fromkeys(cols)))
            if getattr(sp, "auto_aggregate", False) and not self.for_export and len(raw) > 2 * max(
                    2000, int((sp.metadata or {}).get("interactive_max_points", MAX_SCATTER_POINTS))):
                # Voxel/binned aggregation: beyond readability thresholds the
                # cloud renders as a density heatmap instead of point soup.
                hx = ax.hexbin(raw[cx].to_numpy(float), raw[cy].to_numpy(float), gridsize=52,
                               cmap=self.cmap, mincnt=1, linewidths=0.0)
                self._add_colorbar(hx, "Point count", ax=ax)
                self.summary["n_points"] += len(raw)
                self.summary["notes"].append(
                    f"Auto-aggregated {len(raw):,} points into a hexbin density map (readability threshold exceeded).")
                if first_xy is None:
                    first_xy = (raw[cx].to_numpy(float), raw[cy].to_numpy(float))
                    cx_lab, cy_lab = cx, cy
                continue
            sub = self._downsample(raw)
            if sub.empty:
                continue
            x, y = self._scatter_4d5d(ax, sub, cx, cy, cw if cw not in (cx, cy) else None,
                                      cv if cv not in (cx, cy) else None, clean_series_name(name),
                                      alpha=self._alpha_for(ds, 1.0))
            self.summary["n_points"] += x.size
            if first_xy is None:
                first_xy, cx_lab, cy_lab = (x, y), cx, cy
        if first_xy is None:
            self._empty(ax, "No valid data columns mapped for current view.")
        self._finish(ax, get_pretty_label(cx_lab or sp.mappings.get('x')), get_pretty_label(cy_lab or sp.mappings.get('y')), xy=first_xy)

    def _render_scatter3d(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111, projection='3d')
        plotted = False
        labs = ("", "", "")
        for name, ds in sp.datasets.items():
            cx, cy, cz = self._col(ds, 'x', 0), self._col(ds, 'y', 1), self._col(ds, 'z', 2)
            cw, cv = self._col(ds, 'w'), self._col(ds, 'v')
            if not (cx and cy and cz):
                continue
            cols = list(dict.fromkeys([cx, cy, cz] + [c for c in (cw, cv) if c]))
            sub = self._downsample(self._prepare_subset(ds, cols))
            if sub.empty:
                continue
            self._scatter_4d5d(ax, sub, cx, cy, cw if cw not in (cx, cy, cz) else None,
                               cv if cv not in (cx, cy, cz) else None, clean_series_name(name), cz=cz,
                               alpha=self._alpha_for(ds, self.spec.surface_alpha))
            self.summary["n_points"] += len(sub)
            plotted, labs = True, (cx, cy, cz)
        if not plotted:
            self._empty(ax, "3D scatter needs distinct X, Y and Z mappings.")
        self._finish(ax, get_pretty_label(labs[0]), get_pretty_label(labs[1]), get_pretty_label(labs[2]), three_d=True)

    # -- fluid / tensor field analysis (GraphVis 16.3) -------------------
    def _binned_field(self, sub, cx, cy, cz, max_bins: int = 96):
        """Compact smoothed grid for derivative-based field analysis charts."""
        from scipy.ndimage import gaussian_filter
        from scipy.stats import binned_statistic_2d
        x = sub[cx].to_numpy(float); y = sub[cy].to_numpy(float); z = sub[cz].to_numpy(float)
        bins = int(np.clip(min(int(self.spec.grid_resolution), max_bins), 16, max_bins))
        try:
            stat, xe, ye = binned_statistic_2d(x, y, z, statistic="mean", bins=bins)[:3]
        except Exception:
            return None
        fill = float(np.nanmedian(z))
        Zi = gaussian_filter(np.nan_to_num(stat.T, nan=fill), sigma=max(float(self.spec.smoothing), 0.6))
        xc = 0.5 * (xe[:-1] + xe[1:]); yc = 0.5 * (ye[:-1] + ye[1:])
        Xi, Yi = np.meshgrid(xc, yc)
        return Xi, Yi, Zi

    def _render_flow_analysis(self):
        """Vorticity / divergence / phase-portrait / LIC flow-texture /
        tensor-glyph analysis of the gradient field of a mapped response."""
        sp = self.spec
        chart = sp.chart_type
        ax = self.ax = self.fig.add_subplot(111)
        drawn = False
        labs = ("", "")
        for name, ds in sp.datasets.items():
            cx, cy = self._col(ds, 'x', 0), self._col(ds, 'y', 1)
            zsrc = self._col(ds, 'gradient') or self._col(ds, 'z', 2)
            if not (cx and cy and zsrc) or zsrc in (cx, cy):
                continue
            sub = self._prepare_subset(ds, [cx, cy, zsrc])
            if len(sub) < 16:
                continue
            grid = self._binned_field(sub, cx, cy, zsrc)
            if grid is None:
                continue
            self._check_cancelled()
            Xi, Yi, Zi = grid
            xc, yc = Xi[0, :], Yi[:, 0]
            gy, gx = np.gradient(Zi, yc, xc)
            if chart == "Vorticity Map":
                field = np.gradient(gy, xc, axis=1) - np.gradient(gx, yc, axis=0)
                self._draw_signed_field(ax, Xi, Yi, field, gx, gy, "Vorticity ω = ∂v/∂x − ∂u/∂y")
            elif chart == "Divergence Map":
                field = np.gradient(gx, xc, axis=1) + np.gradient(gy, yc, axis=0)
                self._draw_signed_field(ax, Xi, Yi, field, gx, gy, "Divergence ∇·u (sources / sinks)")
            elif chart == "Phase Portrait":
                speed = np.hypot(gx, gy)
                strm = ax.streamplot(Xi, Yi, gx, gy, color=speed, cmap=self.cmap, density=1.4,
                                     linewidth=np.clip(1.6 * speed / (speed.max() or 1.0), 0.4, 2.4))
                ax.contour(Xi, Yi, gx, levels=[0.0], colors="#E74C3C", linewidths=1.2, linestyles="--")
                ax.contour(Xi, Yi, gy, levels=[0.0], colors="#2980B9", linewidths=1.2, linestyles=":")
                self._add_colorbar(strm.lines, "Field speed |∇z|", ax=ax)
                self.summary["notes"].append("Phase portrait: red dashed = u-nullcline, blue dotted = v-nullcline.")
            elif chart == "Flow Texture (LIC)":
                self._draw_flow_texture(ax, Xi, Yi, Zi, gx, gy)
            else:   # Tensor Glyph Field
                self._draw_tensor_glyphs(ax, Xi, Yi, Zi, gx, gy, xc, yc)
            drawn = True
            labs = (cx, cy)
            self.summary["n_points"] += len(sub)
            break   # field analysis uses the primary dataset
        if not drawn:
            self._empty(ax, "Field analysis needs X, Y and a response (Z or gradient) mapping.")
        self._finish(ax, get_pretty_label(labs[0]), get_pretty_label(labs[1]))

    def _draw_signed_field(self, ax, Xi, Yi, field, gx, gy, label):
        limit = float(np.nanmax(np.abs(field))) or 1.0
        norm = Normalize(vmin=-limit, vmax=limit)
        im = ax.pcolormesh(Xi, Yi, field, cmap=self.cmap, norm=norm, shading="gouraud",
                           rasterized=not self.for_export)
        im.set_gid("graphvis-surface-field")
        step = max(Xi.shape[0] // 16, 1)
        ax.quiver(Xi[::step, ::step], Yi[::step, ::step], gx[::step, ::step], gy[::step, ::step],
                  color="#33475B", alpha=0.55, width=0.0025)
        self._add_colorbar(im, label, ax=ax)

    def _draw_flow_texture(self, ax, Xi, Yi, Zi, gx, gy):
        """Cheap semi-Lagrangian line-integral-convolution approximation."""
        from scipy.ndimage import map_coordinates
        rng = np.random.default_rng(7)
        tex = rng.random(Zi.shape)
        mag = np.hypot(gx, gy)
        u = np.divide(gx, mag, out=np.zeros_like(gx), where=mag > 0)
        v = np.divide(gy, mag, out=np.zeros_like(gy), where=mag > 0)
        rows, cols = np.mgrid[0.0:Zi.shape[0], 0.0:Zi.shape[1]]
        acc = tex.copy(); count = np.ones_like(tex)
        for direction in (1.0, -1.0):
            r, c = rows.copy(), cols.copy()
            for _ in range(14):
                self._check_cancelled()
                du = map_coordinates(u, [r, c], order=1, mode="nearest")
                dv = map_coordinates(v, [r, c], order=1, mode="nearest")
                r = np.clip(r + direction * dv, 0, Zi.shape[0] - 1)
                c = np.clip(c + direction * du, 0, Zi.shape[1] - 1)
                acc += map_coordinates(tex, [r, c], order=1, mode="nearest")
                count += 1.0
        lic = acc / count
        lic = (lic - lic.min()) / (np.ptp(lic) or 1.0)
        ax.imshow(lic, cmap="gray", origin="lower", aspect="auto",
                  extent=[Xi.min(), Xi.max(), Yi.min(), Yi.max()], interpolation="bilinear")
        im = ax.pcolormesh(Xi, Yi, Zi, cmap=self.cmap, norm=self._norm_for("z", Zi), shading="gouraud",
                           alpha=0.45, rasterized=not self.for_export)
        im.set_gid("graphvis-surface-field")
        self._add_colorbar(im, get_pretty_label(self.spec.mappings.get("z") or "response"), ax=ax)
        self.summary["notes"].append("Flow texture: 28-step semi-Lagrangian LIC of the response gradient field.")

    def _draw_tensor_glyphs(self, ax, Xi, Yi, Zi, gx, gy, xc, yc):
        """Local structure-tensor (Hessian) ellipse glyphs over the field."""
        from matplotlib.patches import Ellipse
        im = ax.pcolormesh(Xi, Yi, Zi, cmap=self.cmap, norm=self._norm_for("z", Zi), shading="gouraud",
                           alpha=0.75, rasterized=not self.for_export)
        im.set_gid("graphvis-surface-field")
        hxx = np.gradient(gx, xc, axis=1)
        hyy = np.gradient(gy, yc, axis=0)
        hxy = 0.5 * (np.gradient(gx, yc, axis=0) + np.gradient(gy, xc, axis=1))
        step = max(Xi.shape[0] // 12, 1)
        cell_w = (xc[-1] - xc[0]) / max(len(xc) - 1, 1) * step
        cell_h = (yc[-1] - yc[0]) / max(len(yc) - 1, 1) * step
        scale_ref = max(float(np.nanpercentile(np.abs([hxx, hyy]), 98)), 1e-12)
        for i in range(0, Zi.shape[0], step):
            self._check_cancelled()
            for j in range(0, Zi.shape[1], step):
                H = np.array([[hxx[i, j], hxy[i, j]], [hxy[i, j], hyy[i, j]]], dtype=float)
                if not np.all(np.isfinite(H)):
                    continue
                evals, evecs = np.linalg.eigh(H)
                w = np.clip(np.abs(evals) / scale_ref, 0.08, 1.0)
                angle = float(np.degrees(np.arctan2(evecs[1, 1], evecs[0, 1])))
                ax.add_patch(Ellipse((Xi[i, j], Yi[i, j]), width=w[1] * cell_w, height=w[0] * cell_h,
                                     angle=angle, facecolor="none",
                                     edgecolor="#22313F" if evals[1] >= 0 else "#C0392B", linewidth=0.9))
        self._add_colorbar(im, get_pretty_label(self.spec.mappings.get("z") or "response"), ax=ax)
        self.summary["notes"].append(
            "Tensor glyphs: Hessian eigen-ellipses (dark = convex ridge, red = concave); size ∝ local curvature.")

    # -- Pareto ---------------------------------------------------------
    def _render_performance_ceiling(self):
        """Robust upper attainable envelope over a point cloud.

        The renderer bins X, takes the 99th percentile of Y in populated bins
        and then applies a running maximum.  It is intentionally distinct from
        a Pareto front: it visualises an empirical performance ceiling while
        retaining the background sample cloud.
        """
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        plotted = False
        first_x = first_y = None
        for name, ds in sp.datasets.items():
            cx, cy = self._col(ds, 'x', 0), self._col(ds, 'y', 1)
            if not cx or not cy or cx == cy:
                continue
            sub = self._prepare_subset(ds, [cx, cy])
            if sub.empty:
                continue
            x = sub[cx].to_numpy(float)
            y = sub[cy].to_numpy(float)
            show = self._downsample(sub)
            ax.scatter(show[cx], show[cy], s=5, color="#AEB6BF", alpha=0.42,
                       linewidths=0, rasterized=True,
                       label=f"{clean_series_name(name)} — samples")
            bins = max(int(sp.grid_resolution / 4), 20)
            if np.ptp(x) <= 0:
                continue
            edges = np.linspace(float(np.min(x)), float(np.max(x)), bins + 1)
            idx = np.clip(np.searchsorted(edges, x, side='right') - 1, 0, bins - 1)
            centres = 0.5 * (edges[:-1] + edges[1:])
            top = np.array([
                np.percentile(y[idx == b], 99) if np.sum(idx == b) >= 3 else np.nan
                for b in range(bins)
            ], dtype=float)
            good = np.isfinite(top)
            if good.sum() < 3:
                continue
            ceiling = np.maximum.accumulate(top[good]) if sp.pareto_options.get("maximise_y", True) else np.minimum.accumulate(top[good])
            ax.plot(centres[good], ceiling, color=sp.series_color, linewidth=max(sp.line_width, 2.4),
                    label="Performance ceiling")
            self.summary["n_points"] += len(sub)
            self.summary.setdefault("performance_ceiling", {})[name] = {
                "x": centres[good].tolist(), "y": ceiling.tolist(), "percentile": 99,
            }
            plotted = True
            if first_x is None:
                first_x, first_y = x, y
        if not plotted:
            self._empty(ax, "Performance Ceiling needs two distinct numeric mappings with enough samples per X region.")
        self._finish(ax, get_pretty_label(sp.mappings.get('x')), get_pretty_label(sp.mappings.get('y')),
                     xy=(first_x, first_y) if first_x is not None else None)

    def _render_pareto(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        first_xy = None
        cx_lab = cy_lab = ""
        for name, ds in sp.datasets.items():
            cx, cy, cw, cv = self._col(ds, 'x', 0), self._col(ds, 'y', 1), self._col(ds, 'w'), self._col(ds, 'v')
            if not cx or not cy:
                continue
            cols = list(dict.fromkeys([cx, cy] + [c for c in (cw, cv) if c]))
            sub = self._downsample(self._prepare_subset(ds, cols))
            if sub.empty:
                continue
            x, y = sub[cx].to_numpy(float), sub[cy].to_numpy(float)
            bounds = None
            if sp.pareto_options.get('constrain', True):
                xc, yc = self._clip('x'), self._clip('y')
                bounds = ((xc or (None, None)) + (yc or (None, None)))
            key = _key("pareto", ds.cache_key(), cx, cy, sp.pareto_options, bounds, sp.noise_filter_enabled,
                       sp.noise_filter_method, sp.noise_filter_threshold, sp.outlier_mask, sp.mask_method, len(sub))
            res = PARETO_CACHE.get(key)
            if res is None:
                res = pareto_frontier(x, y, maximise_y=sp.pareto_options.get('maximise_y', True), bounds=bounds,
                                      n_boot=200 if sp.pareto_options.get('bootstrap', True) else 0,
                                      cancel_check=(sp.metadata or {}).get("_cancel_check"))
                PARETO_CACHE.put(key, res)
            if not res:
                continue
            label = clean_series_name(name)
            if (cw and cw in sub.columns and cw not in (cx, cy)) or (cv and cv in sub.columns and cv not in (cx, cy)):
                self._scatter_4d5d(ax, sub, cx, cy, cw if cw not in (cx, cy) else None,
                                   cv if cv not in (cx, cy) else None, f"{label} — samples",
                                   alpha=self._alpha_for(ds, 1.0))
            else:
                if not sp.pareto_only:
                    ax.scatter(res['x'], res['y'], s=10, color='#BDC3C7', label=f"{label} — dominated", zorder=1,
                               alpha=self._alpha_for(ds, 0.55))
            ax.plot(res['x'], res['env'], color=sp.series_color, linewidth=2.5, label=f"{label} — frontier", zorder=4,
                    alpha=self._alpha_for(ds, 1.0))
            ax.scatter(res['x'][res['on_front']], res['y'][res['on_front']], s=26, color=sp.series_color,
                       edgecolors='white', linewidths=0.6, zorder=5, alpha=self._alpha_for(ds, 1.0))
            if res['band_lo'] is not None and res['grid'] is not None and sp.pareto_options.get('bootstrap', True):
                ax.fill_between(res['grid'], res['band_lo'], res['band_hi'], color=sp.series_color, alpha=0.15,
                                label="90% bootstrap band", linewidth=0)
            self.summary["n_points"] += x.size
            self.summary.setdefault("pareto", {})[name] = {"excluded": res['excluded'], "front_points": int(res['on_front'].sum())}
            if first_xy is None:
                first_xy, cx_lab, cy_lab = (x, y), cx, cy
        if first_xy is None:
            self._empty(ax, "Insufficient numeric variance for Pareto front.")
        self._finish(ax, get_pretty_label(cx_lab or sp.mappings.get('x')), get_pretty_label(cy_lab or sp.mappings.get('y')), xy=first_xy)

    # -- fields ---------------------------------------------------------
    def _fit_polynomial_surface(self, x, y, z, Xi, Yi, degree=3):
        # Retained for compatibility with old plugins; GraphVis core now routes
        # all production surface work through surface_estimators.py.
        terms = [(i, j) for i in range(degree + 1) for j in range(degree + 1 - i)]
        A = np.column_stack([(x ** i) * (y ** j) for i, j in terms])
        coeffs, *_ = np.linalg.lstsq(A, z, rcond=None)
        Zi = np.zeros_like(Xi)
        for (i, j), c in zip(terms, coeffs):
            Zi += c * (Xi ** i) * (Yi ** j)
        return Zi

    def _interpolate_surface(self, x, y, z, resolution, estimator, smoothing, geometry_key=None) -> SurfaceGrid:
        """Compatibility wrapper around the dedicated estimator router."""
        sp = self.spec
        progressive = bool((sp.metadata or {}).get("progressive_surface_preview", False)) and not self.for_export
        return interpolate_surface(
            x, y, z,
            resolution=resolution,
            estimator=estimator,
            x_log=canonical_axis_scale(sp.x_scale) in LOG_LIKE_SCALES,
            y_log=canonical_axis_scale(sp.y_scale) in LOG_LIKE_SCALES,
            smoothing=smoothing,
            neighbors=max(4, int(sp.surface_neighbors)),
            idw_power=float(sp.idw_power),
            loess_fraction=float(sp.loess_fraction),
            extrapolation=sp.surface_extrapolation,
            invalid_mask_policy=sp.invalid_mask_policy,
            failure_bridge_policy=sp.failure_bridge_policy,
            failure_bridge_max_cells=max(1, int(sp.failure_bridge_max_cells)),
            response_space=sp.surface_response_space,
            kriging_variogram=sp.kriging_variogram,
            value_policy=sp.surface_value_policy,
            duplicate_statistic=sp.bin_statistic or "mean",
            geometry_key=geometry_key,
            x_metric_weight=max(float(sp.surface_x_metric_weight), 1e-6),
            y_metric_weight=max(float(sp.surface_y_metric_weight), 1e-6),
            progressive=progressive,
            cancel_check=(sp.metadata or {}).get("_cancel_check"),
        )

    def _mapped_vector_for_matrix(self, ds: Dataset, role: str, length: int, *, exclude: set[str] | None = None):
        """Resolve an axis vector for a pre-gridded response matrix.

        Explicit X/Y mappings win.  GraphVis then looks for compatible 1-D
        vectors in dataframe columns and MAT/HDF auxiliary arrays; if none are
        available, physical index coordinates are used.
        """
        exclude = exclude or set()
        token = self.spec.mappings.get(role)
        candidates: list[tuple[str, np.ndarray]] = []
        for name, arr0 in getattr(ds, "aux", {}).items():
            arr = np.asarray(arr0, dtype=float).ravel()
            if arr.size == int(length):
                candidates.append((str(name), arr))
        for name in getattr(ds, "numeric_columns", []):
            try:
                arr = ds.df[name].to_numpy(float).ravel()
            except Exception:
                continue
            if arr.size == int(length):
                candidates.append((str(name), arr))
        if token:
            raw = str(token).split(":", 1)[-1]
            for name, arr in candidates:
                if name == raw and name not in exclude:
                    return arr, name
        # Prefer conventional axis names, then any unused compatible vector.
        role_hints = ("x", "voltage", "potential", "current", "flow", "rate") if role == "x" else ("y", "flow", "rate", "pressure", "temperature", "temp")
        for hint in role_hints:
            for name, arr in candidates:
                low = name.lower()
                if name not in exclude and (low == hint or low.startswith(hint + "_") or hint in low):
                    return arr, name
        for name, arr in candidates:
            if name not in exclude:
                return arr, name
        start = 1.0 if canonical_axis_scale(getattr(self.spec, f"{role}_scale", "Linear")) in LOG_LIKE_SCALES else 0.0
        return np.arange(start, start + int(length), dtype=float), f"{role.upper()} index"

    def _surface_from_matrix(self, ds: Dataset, matrix_name: str, matrix: np.ndarray) -> tuple[SurfaceGrid, str, str, str]:
        t_surface = time.perf_counter()
        sp = self.spec
        M = np.asarray(matrix, dtype=float)
        if M.ndim != 2 or min(M.shape) < 2:
            raise ValueError("Pre-gridded surface input must be a 2-D matrix with at least 2x2 cells.")
        ny, nx = M.shape
        xv, xname = self._mapped_vector_for_matrix(ds, "x", nx)
        yv, yname = self._mapped_vector_for_matrix(ds, "y", ny, exclude={xname})
        # If explicit mappings clearly describe the opposite matrix dimensions,
        # transpose once rather than silently falling back to index coordinates.
        xtok, ytok = sp.mappings.get("x"), sp.mappings.get("y")
        def exact_len(tok, n):
            if not tok: return False
            raw = str(tok).split(":", 1)[-1]
            if raw in getattr(ds, "aux", {}): return np.asarray(ds.aux[raw]).size == n
            if raw in getattr(ds, "df", {}).columns: return len(ds.df[raw]) == n
            return False
        if exact_len(xtok, ny) and exact_len(ytok, nx) and not (exact_len(xtok, nx) and exact_len(ytok, ny)):
            M = M.T; ny, nx = M.shape
            xv, xname = self._mapped_vector_for_matrix(ds, "x", nx)
            yv, yname = self._mapped_vector_for_matrix(ds, "y", ny, exclude={xname})
            self.summary["notes"].append(f"Transposed pre-gridded matrix '{matrix_name}' to match explicit X/Y axis vectors.")
        X0, Y0 = np.meshgrid(np.asarray(xv, float), np.asarray(yv, float))
        progressive = bool((sp.metadata or {}).get("progressive_surface_preview", False)) and not self.for_export
        base_key = _key("matrix-surf-v2", ds.cache_key(), matrix_name, M.shape, canonical_estimator(sp.estimator),
                        sp.grid_resolution, sp.x_scale, sp.y_scale, sp.surface_neighbors, sp.idw_power,
                        sp.loess_fraction, sp.kriging_variogram, sp.surface_value_policy, sp.surface_extrapolation, sp.invalid_mask_policy,
                        sp.failure_bridge_policy, sp.failure_bridge_max_cells, sp.surface_response_space, sp.surface_x_metric_weight, sp.surface_y_metric_weight, progressive, self.for_export)
        base = SURFACE_CACHE.get(base_key)
        if base is None:
            if canonical_estimator(sp.estimator) == "Auto (data-aware)":
                # A matrix is already a surface.  Do not flatten it, rebuild a
                # Cartesian topology and run Qhull merely to rediscover it.
                base = structured_surface_native(np.asarray(xv, float), np.asarray(yv, float), M,
                                                 x_log=canonical_axis_scale(sp.x_scale) in LOG_LIKE_SCALES, y_log=canonical_axis_scale(sp.y_scale) in LOG_LIKE_SCALES,
                                                 invalid_mask_policy=sp.invalid_mask_policy,
                                                 failure_bridge_policy=sp.failure_bridge_policy,
                                                 failure_bridge_max_cells=max(1, int(sp.failure_bridge_max_cells)),
                                                 progressive=progressive)
            else:
                base = interpolate_surface(X0.ravel(), Y0.ravel(), M.ravel(), resolution=sp.grid_resolution,
                                           estimator=sp.estimator, x_log=canonical_axis_scale(sp.x_scale) in LOG_LIKE_SCALES, y_log=canonical_axis_scale(sp.y_scale) in LOG_LIKE_SCALES,
                                           smoothing=0.0, neighbors=sp.surface_neighbors, idw_power=sp.idw_power,
                                           loess_fraction=sp.loess_fraction, extrapolation=sp.surface_extrapolation,
                                           invalid_mask_policy=sp.invalid_mask_policy, duplicate_statistic=sp.bin_statistic, kriging_variogram=sp.kriging_variogram,
                                           failure_bridge_policy=sp.failure_bridge_policy, failure_bridge_max_cells=max(1, int(sp.failure_bridge_max_cells)),
                                           response_space=sp.surface_response_space,
                                           x_metric_weight=max(float(sp.surface_x_metric_weight), 1e-6),
                                           y_metric_weight=max(float(sp.surface_y_metric_weight), 1e-6),
                                           value_policy=sp.surface_value_policy, progressive=progressive,
                                           geometry_key=(ds.cache_key(), matrix_name, sp.x_scale, sp.y_scale),
                                           cancel_check=(sp.metadata or {}).get("_cancel_check"))
            SURFACE_CACHE.put(base_key, base)
        sigma = max(float(sp.smoothing), 0.0)
        if sigma > 0:
            smooth_key = _key("matrix-surf-smooth-v1", base_key, round(sigma, 6))
            result = SURFACE_CACHE.get(smooth_key)
            if result is None:
                result = smooth_surface_grid(base, sigma); SURFACE_CACHE.put(smooth_key, result)
        else:
            result = base
        self.summary["notes"].extend(result.notes)
        self.summary["notes"].append(f"Pre-gridded matrix surface: {matrix_name} {M.shape[0]}x{M.shape[1]} -> {result.Z.shape[0]}x{result.Z.shape[1]} using {result.method}.")
        self.summary.setdefault("performance", {})["matrix_surface_prepare"] = round(time.perf_counter() - t_surface, 6)
        self.summary["performance"]["surface_cache_bytes"] = SURFACE_CACHE.bytes_used
        return result, xname, yname, matrix_name

    def _surface_from(self, ds, sub, cx, cy, cz) -> SurfaceGrid:
        t_surface = time.perf_counter()
        sp = self.spec
        work = sub

        # Preserve a complete Cartesian sweep for matrix algorithms.  Generic
        # point decimation destroys the N×M topology, while structured spline
        # methods are already efficient on the compact grid representation.
        structured_candidate = False
        try:
            raw_xy = ds.df[[cx, cy]].replace([np.inf, -np.inf], np.nan).dropna()
            if not raw_xy.empty:
                nx, ny = raw_xy[cx].nunique(), raw_xy[cy].nunique()
                structured_candidate = nx >= 2 and ny >= 2 and len(raw_xy.drop_duplicates()) == nx * ny
        except Exception:
            structured_candidate = False

        if not self.for_export and not structured_candidate:
            threshold = int((sp.metadata or {}).get("interactive_max_points", MAX_SCATTER_POINTS))
            if bool((sp.metadata or {}).get("progressive_surface_preview", False)):
                threshold = min(threshold, 3500)
            threshold = max(1200, min(threshold, MAX_SCATTER_POINTS))
            if len(work) > threshold:
                work = adaptive_decimate_frame(work, threshold, x_col=cx, y_col=cy, preserve_line=False)
                self.summary["notes"].append(
                    f"Surface interpolation sampled {len(sub):,} → {len(work):,} points for responsive preview; export uses full data."
                )

        # _prepare_subset() deliberately excludes NaN/Inf rows.  For surfaces
        # we additionally pass the coordinates of failed response samples back
        # to the estimator with Z=NaN so it can carve a transparent/fallback
        # failure mask instead of interpolating across an ODE solver failure.
        xvals = work[cx].to_numpy(float)
        yvals = work[cy].to_numpy(float)
        zvals = work[cz].to_numpy(float)
        failure_count = 0
        try:
            raw = ds.df[[cx, cy, cz]].replace([np.inf, -np.inf], np.nan)
            fail = raw[raw[cx].notna() & raw[cy].notna() & raw[cz].isna()]
            if not fail.empty:
                failure_count = len(fail)
                xvals = np.concatenate((xvals, fail[cx].to_numpy(float)))
                yvals = np.concatenate((yvals, fail[cy].to_numpy(float)))
                zvals = np.concatenate((zvals, np.full(len(fail), np.nan)))
        except Exception:
            pass

        progressive = bool((sp.metadata or {}).get("progressive_surface_preview", False)) and not self.for_export
        base_key = _key(
            "surf-base-v5", ds.cache_key(), cx, cy, cz, canonical_estimator(sp.estimator), sp.bin_statistic,
            sp.grid_resolution, sp.x_scale, sp.y_scale, sp.surface_neighbors, sp.idw_power,
            sp.loess_fraction, sp.kriging_variogram, sp.surface_value_policy, sp.surface_extrapolation, sp.invalid_mask_policy,
            sp.failure_bridge_policy, sp.failure_bridge_max_cells, sp.surface_response_space, sp.surface_x_metric_weight, sp.surface_y_metric_weight, progressive,
            sp.noise_filter_enabled, sp.noise_filter_method, sp.noise_filter_threshold,
            sp.outlier_mask, sp.mask_method, len(work), failure_count, self.for_export,
        )
        base = SURFACE_CACHE.get(base_key)
        if base is None:
            # Expensive geometry/statistical estimation is cached independently
            # from post-smoothing.  Dragging only the smoothing slider therefore
            # reuses Delaunay/RBF/kriging/Sibson work instead of recomputing it.
            geometry_key = (ds.cache_key(), cx, cy, sp.x_scale, sp.y_scale, len(xvals))
            base = self._interpolate_surface(xvals, yvals, zvals, sp.grid_resolution, sp.estimator, 0.0, geometry_key=geometry_key)
            SURFACE_CACHE.put(base_key, base)
        else:
            self.summary["notes"].append("Base surface estimate served from cache.")

        sigma = max(float(sp.smoothing), 0.0)
        if sigma > 0.0:
            smooth_key = _key("surf-smooth-v5", base_key, round(sigma, 6))
            res = SURFACE_CACHE.get(smooth_key)
            if res is None:
                res = smooth_surface_grid(base, sigma)
                SURFACE_CACHE.put(smooth_key, res)
            else:
                self.summary["notes"].append("Smoothed surface served from cache.")
        else:
            res = base

        if res.notes:
            self.summary["notes"].extend(res.notes)
        self.summary["notes"].append(
            f"Surface estimator: {res.method}{' (structured grid)' if res.structured else ' (scattered grid)'}; output {res.Z.shape[1]}x{res.Z.shape[0]}."
        )
        self.summary.setdefault("performance", {})["surface_prepare"] = round(time.perf_counter() - t_surface, 6)
        self.summary["performance"]["surface_cache_bytes"] = SURFACE_CACHE.bytes_used
        return res

    def _render_field(self):
        t_field = time.perf_counter()
        sp = self.spec
        is_3d = sp.chart_type in {"3D Topography / Surface", "3D Mesh", "Surface + Contours", "Waterfall", "Ribbon", "3D Contour"}
        ax = self.ax = self.fig.add_subplot(111, projection='3d') if is_3d else self.fig.add_subplot(111)
        plotted = False
        labs = ("", "", "")
        for name, ds in sp.datasets.items():
            cx, cy, cz = self._col(ds, 'x', 0), self._col(ds, 'y', 1), self._col(ds, 'z', 2)
            cw = self._col(ds, 'w') if is_3d else None
            cg = self._col(ds, 'gradient') if not is_3d else None
            sub = None
            surface = None
            point_count = 0
            labs_this = (cx or "", cy or "", cz or "")

            # Prefer explicit X/Y/Z table mappings when they form a valid
            # response surface. If those are unavailable, a selected 2-D
            # matrix is treated as a genuine pre-gridded surface rather than
            # being flattened into arbitrary table columns.
            if cx and cy and cz and len({cx, cy, cz}) >= 3:
                extra = [c for c in (cw, cg) if c and c not in (cx, cy, cz)]
                sub = self._prepare_subset(ds, [cx, cy, cz] + extra)
                if not sub.empty and sub[cx].nunique() >= 2 and sub[cy].nunique() >= 2:
                    surface = self._surface_from(ds, sub, cx, cy, cz)
                    point_count = len(sub)

            if surface is None:
                matrix_name, matrix = self._selected_array(ds, 'matrix', ndim=2)
                if matrix is not None:
                    try:
                        surface, mx, my, mz = self._surface_from_matrix(ds, matrix_name, matrix)
                        labs_this = (mx, my, mz)
                        point_count = int(np.asarray(matrix).size)
                        # Matrix surfaces have no separate table W/gradient
                        # channel unless the user maps one through X/Y/Z data.
                        cw = None; cg = None; sub = None
                    except Exception as exc:
                        self.warnings.append(f"Pre-gridded matrix surface '{matrix_name}' skipped: {exc}")
                        surface = None
            if surface is None:
                continue

            if str(sp.invalid_data_mode) in IMPUTATION_MODES:
                # Advanced solver-dropout handling: fill failed cells via the
                # selected strategy (nearest / local mean / baseline / mirror)
                # while the convex-hull mask stays untouched.
                try:
                    surface = impute_surface_invalid(surface, sp.invalid_data_mode)
                    for note in surface.notes[-1:]:
                        if "imputed" in note:
                            self.summary["notes"].append(note)
                except Exception as exc:
                    self.warnings.append(f"Invalid-data imputation ({sp.invalid_data_mode}) failed: {exc}")

            Xi, Yi, Zi = surface
            try:
                finite_surface = np.ma.asarray(Zi).compressed()
                self.summary.setdefault("surface_diagnostics", []).append({
                    "dataset": name, "method": surface.method, "shape": tuple(int(v) for v in Zi.shape),
                    "finite_cells": int(finite_surface.size),
                    "masked_cells": int(np.ma.getmaskarray(Zi).sum()),
                    "imputed_cells": int(np.asarray(surface.imputed_mask, dtype=bool).sum()) if surface.imputed_mask is not None else 0,
                    "min": float(np.nanmin(finite_surface)) if finite_surface.size else None,
                    "max": float(np.nanmax(finite_surface)) if finite_surface.size else None,
                })
            except Exception:
                pass
            z_norm = self._norm_for("z", Zi)
            z_colour = self._colour_values_for_norm(Zi, z_norm)
            z_extend = self._colorbar_extend_for(Zi, z_norm)
            response_label = labs_this[2]
            try:
                ax._graphvis_surface_payload = {
                    "dataset": name, "X": Xi, "Y": Yi, "Z": Zi,
                    "x_label": labs_this[0], "y_label": labs_this[1], "z_label": labs_this[2],
                    "x_scale": sp.x_scale, "y_scale": sp.y_scale, "z_scale": sp.z_scale,
                    "method": surface.method,
                    "imputed_mask": surface.imputed_mask, "invalid_mask": surface.invalid_mask,
                }
            except Exception:
                pass

            if is_3d:
                alpha3d = self._alpha_for(ds, sp.surface_alpha)
                if sp.chart_type == "3D Mesh":
                    ax.plot_wireframe(Xi, Yi, Zi, rstride=max(1, Xi.shape[0] // 35), cstride=max(1, Xi.shape[1] // 35),
                                      color=sp.series_color, linewidth=0.6, alpha=alpha3d)
                elif sp.chart_type == "Waterfall":
                    step = max(1, Yi.shape[0] // 24)
                    for row in range(0, Yi.shape[0], step):
                        ax.plot(Xi[row], Yi[row], Zi[row], color=sp.series_color, linewidth=1.0, alpha=alpha3d)
                elif sp.chart_type == "Ribbon":
                    step = max(1, Yi.shape[0] // 18)
                    for row in range(0, Yi.shape[0], step):
                        ax.plot(Xi[row], Yi[row], Zi[row], color=sp.series_color, linewidth=2.2, alpha=alpha3d)
                elif sp.chart_type == "3D Contour":
                    levels = self._levels_for_norm(z_norm, max(12, int(sp.contour_levels)))
                    cs = ax.contour3D(Xi, Yi, Zi, levels=levels, cmap=self.cmap, norm=z_norm, alpha=alpha3d)
                    self._add_colorbar(cs, get_pretty_label(response_label), three_d=True, extend=z_extend)
                else:
                    if cw and sub is not None and cw in sub.columns:
                        colour_surface = self._surface_from(ds, sub, cx, cy, cw)
                        _, _, Wi = colour_surface
                        norm = self._norm_for("w", Wi)
                        w_extend = self._colorbar_extend_for(Wi, norm)
                        surf = ax.plot_surface(Xi, Yi, Zi, facecolors=self.cmap(norm(Wi)), linewidth=0,
                                               antialiased=True, alpha=alpha3d, shade=False,
                                               rcount=Zi.shape[0], ccount=Zi.shape[1])
                        from matplotlib.cm import ScalarMappable
                        surf.set_gid("graphvis-surface-field")
                        self._add_colorbar(ScalarMappable(norm=norm, cmap=self.cmap), get_pretty_label(cw),
                                           three_d=True, extend=w_extend)
                        self.summary["notes"].append(f"Surface height = {get_pretty_label(response_label)}, colour = {get_pretty_label(cw)}.")
                    else:
                        surf = ax.plot_surface(Xi, Yi, Zi, cmap=self.cmap, norm=z_norm, linewidth=0,
                                               antialiased=True, alpha=alpha3d, shade=False,
                                               rcount=Zi.shape[0], ccount=Zi.shape[1])
                        surf.set_gid("graphvis-surface-field")
                        self._add_colorbar(surf, get_pretty_label(response_label), three_d=True, extend=z_extend)
                    if sp.chart_type == "Surface + Contours":
                        finite_z = np.ma.asarray(Zi).compressed()
                        if finite_z.size:
                            z0 = float(np.nanmin(finite_z))
                            ax.contour(Xi, Yi, Zi, levels=self._levels_for_norm(z_norm, max(8, int(sp.contour_levels))),
                                       zdir='z', offset=z0, cmap=self.cmap, norm=z_norm, linewidths=0.7)
                            ax.set_zlim(z0, float(np.nanmax(finite_z)))
            else:
                # Optional explicit rendering of failed-simulation cells.  The
                # interpolated response remains masked; this overlay only paints
                # the protected failed coordinates, never invents a response.
                if (str(sp.invalid_data_mode).lower().startswith("fallback") and
                        surface.invalid_mask is not None and np.any(surface.invalid_mask)):
                    failure_layer = np.ma.masked_where(~surface.invalid_mask, np.ones_like(surface.invalid_mask, dtype=float))
                    failure_artist = ax.pcolormesh(Xi, Yi, failure_layer, shading='nearest',
                                  cmap=ListedColormap([sp.invalid_color or "#DDDDDD"]), vmin=0.0, vmax=1.0,
                                  rasterized=(not self.for_export) or str(sp.vector_surface_mode).startswith("Hybrid"), zorder=0.2)
                    failure_artist.set_gid("graphvis-invalid-mask")

                discrete = str(sp.field_render_mode).lower().startswith("discrete")
                bands = max(3, int(sp.contour_levels))
                if discrete:
                    levels = self._levels_for_norm(z_norm, bands + 1)
                    cs = ax.contourf(Xi, Yi, z_colour, levels=levels, cmap=self.cmap, norm=z_norm,
                                     extend=z_extend, antialiased=True)
                    ax.contour(Xi, Yi, z_colour, levels=levels, colors='white', linewidths=0.35, alpha=0.38)
                    self._add_colorbar(cs, get_pretty_label(response_label), extend=z_extend)
                    self.summary["notes"].append(f"Discrete contour rendering: {bands} colour bands.")
                elif sp.chart_type == "2D Contour":
                    # High-density filled contours approximate continuous MATLAB
                    # shading while retaining readable iso-lines.
                    levels = self._levels_for_norm(z_norm, max(48, bands * 4))
                    cs = ax.contourf(Xi, Yi, z_colour, levels=levels, cmap=self.cmap, norm=z_norm,
                                     extend=z_extend, antialiased=True)
                    ax.contour(Xi, Yi, z_colour, levels=self._levels_for_norm(z_norm, max(8, bands)),
                               colors='white', linewidths=0.4, alpha=0.5)
                    self._add_colorbar(cs, get_pretty_label(response_label), extend=z_extend)
                else:
                    # Gouraud interpolation is the 2-D Matplotlib equivalent of
                    # MATLAB ``shading interp``.  Interactive previews rasterise
                    # the dense field for speed; SVG/PDF exports leave it vector
                    # while text/ticks always remain vector in those formats.
                    im = ax.pcolormesh(Xi, Yi, z_colour, cmap=self.cmap, norm=z_norm, shading='gouraud',
                                       alpha=max(0.05, min(1.0, float(sp.surface_alpha))),
                                       rasterized=(not self.for_export) or str(sp.vector_surface_mode).startswith("Hybrid"))
                    im.set_gid("graphvis-surface-field")
                    self._add_colorbar(im, get_pretty_label(response_label), extend=z_extend)

                if sp.show_imputed_cells and surface.imputed_mask is not None and np.any(surface.imputed_mask):
                    try:
                        imask = np.asarray(surface.imputed_mask, dtype=bool)
                        ax.scatter(Xi[imask], Yi[imask], s=18, marker='s', facecolors='none',
                                   edgecolors='#FFFFFF', linewidths=0.65, alpha=0.9, zorder=4.2,
                                   label='Interpolated solver dropout')
                        self.summary["notes"].append(f"Marked {int(imask.sum())} bridged/imputed surface cell(s).")
                    except Exception as exc:
                        self.warnings.append(f"Imputation provenance overlay skipped: {exc}")

                if sp.contour_overlay:
                    try:
                        overlay_levels = self._levels_for_norm(z_norm, max(2, int(sp.contour_overlay_levels)))
                        overlay = ax.contour(Xi, Yi, z_colour, levels=overlay_levels,
                                             colors=sp.contour_overlay_color or "#FFFFFF",
                                             linewidths=max(0.1, float(sp.contour_overlay_width)), alpha=0.88, zorder=3.5)
                        if sp.contour_overlay_labels:
                            ax.clabel(overlay, inline=True, fontsize=max(6, sp.font_size - 2), fmt="%g")
                        self.summary["notes"].append(f"Contour overlay: {len(overlay_levels)} iso-levels.")
                    except Exception as exc:
                        self.warnings.append(f"Contour overlay skipped: {exc}")

            force_vector = sp.chart_type in {"Quiver Field", "Stream Field"}
            if (sp.quiver_overlay or force_vector) and not is_3d:
                Gi = Zi
                if cg and sub is not None and cg in sub.columns:
                    grad_surface = self._surface_from(ds, sub, cx, cy, cg)
                    _, _, Gi = grad_surface
                G = np.ma.filled(np.ma.asarray(Gi), np.nan)
                # Differentiate in the same physical/log metric used for the
                # displayed axes. Array-index gradients distort arrows badly
                # across logarithmic decades.
                x_metric = np.asarray(Xi[0, :], dtype=float)
                y_metric = np.asarray(Yi[:, 0], dtype=float)
                if canonical_axis_scale(sp.x_scale) in LOG_LIKE_SCALES:
                    x_metric = np.log10(x_metric)
                if canonical_axis_scale(sp.y_scale) in LOG_LIKE_SCALES:
                    y_metric = np.log10(y_metric)
                try:
                    gy, gx = np.gradient(G, y_metric, x_metric)
                except Exception:
                    gy, gx = np.gradient(G)
                step = max(Xi.shape[0] // 18, 1)
                vector_mode = "Streamplot" if sp.chart_type == "Stream Field" else ("Quiver" if sp.chart_type == "Quiver Field" else sp.quiver_type)
                if vector_mode == "Streamplot":
                    try:
                        ax.streamplot(Xi, Yi, gx, gy, color='#1F2D3D', linewidth=0.7, density=0.9)
                    except Exception as exc:
                        self.warnings.append(f"Streamplot skipped: {exc}")
                else:
                    ax.quiver(Xi[::step, ::step], Yi[::step, ::step], gx[::step, ::step], gy[::step, ::step],
                              color='#1F2D3D', alpha=0.6)
                self.summary["notes"].append(f"Gradient overlay of {get_pretty_label(cg or response_label)}.")
            plotted, labs = True, labs_this
            self.summary["n_points"] += point_count
        if not plotted:
            self._empty(ax, "Need three distinct numeric mappings (X, Y, Z) or a 2-D pre-gridded matrix for a surface/heatmap.")
        elif sp.auto_surface_presentation and not is_3d:
            # Publication-style rectangular fields: use the data extent exactly
            # and keep the colourbar close to the plot, like MATLAB heatmaps.
            try:
                ax.margins(x=0.0, y=0.0)
                ax.set_axisbelow(True)
            except Exception:
                pass
        x_label = get_pretty_label(labs[0] or sp.mappings.get('x'))
        y_label = get_pretty_label(labs[1] or sp.mappings.get('y'))
        z_label = get_pretty_label(labs[2] or sp.mappings.get('z'))
        auto_title = ""
        if plotted and sp.auto_surface_presentation and not is_3d and x_label and y_label and z_label:
            auto_title = f"{z_label}: {x_label} vs {y_label}"
        self._finish(ax, x_label, y_label, z_label if is_3d else "", default_title=auto_title, three_d=is_3d)
        if plotted and sp.auto_surface_presentation and not is_3d:
            # MATLAB-like scientific field presentation: the field itself is
            # the visual guide, so suppress the generic dashed grid and use
            # compact outward ticks/spines. Disable this checkbox if an
            # explicit overlaid grid is desired instead.
            try:
                ax.grid(False)
                ax.tick_params(axis='both', which='major', direction='out', length=4.0, width=0.8)
                ax.tick_params(axis='both', which='minor', direction='out', length=2.5, width=0.6)
                for spine in ax.spines.values():
                    spine.set_linewidth(0.8)
            except Exception:
                pass
        self.summary.setdefault("performance", {})["field_total"] = round(time.perf_counter() - t_field, 6)

    # -- hexbin ---------------------------------------------------------
    def _render_hexbin(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        plotted = False
        cx_lab = cy_lab = ""
        for name, ds in sp.datasets.items():
            cx, cy, cz = self._col(ds, 'x', 0), self._col(ds, 'y', 1), self._col(ds, 'z')
            if not cx or not cy or cx == cy:
                continue
            cols = [cx, cy] + ([cz] if cz and cz not in (cx, cy) else [])
            sub = self._prepare_subset(ds, cols)
            if sub.empty:
                continue
            C = sub[cz].to_numpy(float) if (cz and cz in sub.columns and cz not in (cx, cy)) else None
            z_norm = self._norm_for("z", C) if C is not None else None
            hb = ax.hexbin(sub[cx].to_numpy(float), sub[cy].to_numpy(float), C=C,
                           reduce_C_function=np.mean if C is not None else np.sum,
                           gridsize=max(int(sp.grid_resolution / 4), 12), cmap=self.cmap, mincnt=1,
                           norm=z_norm, linewidths=0.2, edgecolors='face')
            self._add_colorbar(hb, get_pretty_label(cz) if C is not None else "Sample count")
            plotted, cx_lab, cy_lab = True, cx, cy
            self.summary["n_points"] += len(sub)
            break   # one density field per figure
        if not plotted:
            self._empty(ax, "Hexbin needs two distinct numeric mappings.")
        self._finish(ax, get_pretty_label(cx_lab or sp.mappings.get('x')), get_pretty_label(cy_lab or sp.mappings.get('y')))

    # -- sensitivity ----------------------------------------------------
    def _render_extended(self):
        """Concrete renderer for GraphVis' extended statistical/signal catalogue."""
        from scipy import signal, stats
        from scipy.integrate import cumulative_trapezoid
        from matplotlib.patches import Ellipse

        sp = self.spec
        chart = sp.chart_type
        ds = next(iter(sp.datasets.values()), None)
        if ds is None or not ds.numeric_columns:
            ax = self.ax = self.fig.add_subplot(111)
            self._empty(ax, "Load a numeric dataset for this plot.")
            return
        nums = list(ds.numeric_columns)
        cx = self._col(ds, 'x', 0)
        cy = self._col(ds, 'y', 1 if len(nums) > 1 else 0)
        cz = self._col(ds, 'z', 2 if len(nums) > 2 else None)
        cols = list(dict.fromkeys([c for c in (cx, cy, cz) if c]))
        sub = self._prepare_subset(ds, cols or nums[:3])
        if sub.empty:
            ax = self.ax = self.fig.add_subplot(111); self._empty(ax, "No finite mapped data."); return
        x = sub[cx].to_numpy(float) if cx in sub else np.arange(len(sub), dtype=float)
        y = sub[cy].to_numpy(float) if cy in sub else sub.iloc[:, 0].to_numpy(float)
        label = clean_series_name(ds.name)
        colour = sp.series_color

        if chart == "Radar Chart":
            ax = self.ax = self.fig.add_subplot(111, projection='polar')
            cols2 = nums[:min(12, len(nums))]
            frame = ds.df[cols2].replace([np.inf,-np.inf],np.nan)
            vals = np.array([float(frame[c].mean()) for c in cols2])
            scales = np.array([float(frame[c].std(ddof=0)) or 1.0 for c in cols2])
            z = (vals - np.array([float(frame[c].min()) for c in cols2])) / np.maximum(np.array([float(frame[c].max()-frame[c].min()) for c in cols2]), 1e-12)
            ang = np.linspace(0, 2*np.pi, len(cols2), endpoint=False); ang2=np.r_[ang,ang[0]]; z2=np.r_[z,z[0]]
            ax.plot(ang2,z2,color=colour,linewidth=sp.line_width); ax.fill(ang2,z2,color=colour,alpha=0.18)
            ax.set_xticks(ang); ax.set_xticklabels([get_pretty_label(c) for c in cols2], fontsize=max(sp.font_size-2,6)); ax.set_ylim(0,1)
            ax.set_title(sp.styling.get("Main Title",{}).get("text") or "Radar chart")
            self.summary["n_points"] = len(cols2); return
        if chart == "Ternary Scatter":
            ax = self.ax = self.fig.add_subplot(111)
            if len(nums) < 3: self._empty(ax,"Ternary scatter needs three numeric variables."); return
            dat = ds.df[nums[:3]].replace([np.inf,-np.inf],np.nan).dropna().to_numpy(float)
            dat = np.maximum(dat,0); total=dat.sum(axis=1); dat=dat[total>0]; total=total[total>0]
            if not len(dat): self._empty(ax,"Ternary values must have a positive component sum."); return
            a,b,c=(dat/total[:,None]).T; tx=b+0.5*c; ty=np.sqrt(3)/2*c
            ax.plot([0,1,0.5,0],[0,0,np.sqrt(3)/2,0],color='#7F8C8D',lw=1); ax.scatter(tx,ty,s=18,color=colour,alpha=0.7)
            ax.text(-0.03,-0.03,get_pretty_label(nums[0]),ha='right'); ax.text(1.03,-0.03,get_pretty_label(nums[1]),ha='left'); ax.text(0.5,np.sqrt(3)/2+0.03,get_pretty_label(nums[2]),ha='center')
            ax.set_aspect('equal'); ax.axis('off'); self.summary["n_points"]=len(dat); return
        if chart == "Forest Plot":
            ax = self.ax = self.fig.add_subplot(111)
            if len(nums) < 3:
                self._empty(ax, "Forest Plot needs effect, lower-CI and upper-CI numeric columns."); return
            effect_col = cx if cx in ds.df.columns else nums[0]
            remaining = [c for c in nums if c != effect_col]
            lo_col = remaining[0]; hi_col = remaining[1]
            work = ds.df[[effect_col, lo_col, hi_col]].replace([np.inf,-np.inf],np.nan).dropna()
            effect=work[effect_col].to_numpy(float); lo=work[lo_col].to_numpy(float); hi=work[hi_col].to_numpy(float)
            yidx=np.arange(len(work)); xerr=np.vstack([np.maximum(effect-lo,0),np.maximum(hi-effect,0)])
            ax.errorbar(effect,yidx,xerr=xerr,fmt='o',color=colour,ecolor='#7F8C8D',capsize=3)
            ax.axvline(0,color='#7F8C8D',ls='--',lw=1); ax.set_yticks(yidx); ax.set_yticklabels([str(i+1) for i in yidx]); ax.set_xlabel(get_pretty_label(effect_col)); ax.set_ylabel('Study / row'); ax.set_title('Forest Plot'); self.summary['n_points']=len(work); return
        if chart == "Volcano Plot":
            ax = self.ax = self.fig.add_subplot(111)
            work=ds.df[[cx,cy]].replace([np.inf,-np.inf],np.nan).dropna() if cx in ds.df and cy in ds.df else pd.DataFrame()
            if work.empty: self._empty(ax,"Volcano Plot needs effect-size X and p-value Y mappings."); return
            xv=work[cx].to_numpy(float); pv=work[cy].to_numpy(float); sig=np.where((pv>0)&(pv<=1),-np.log10(np.maximum(pv,1e-300)),pv)
            mask=(np.abs(xv)>=1)&(sig>=-np.log10(0.05)); ax.scatter(xv[~mask],sig[~mask],s=14,alpha=.45,color='#7F8C8D'); ax.scatter(xv[mask],sig[mask],s=18,alpha=.75,color=colour); ax.axvline(-1,ls='--',lw=.8,color='#BDC3C7'); ax.axvline(1,ls='--',lw=.8,color='#BDC3C7'); ax.axhline(-np.log10(.05),ls='--',lw=.8,color='#BDC3C7'); ax.set_xlabel(get_pretty_label(cx)); ax.set_ylabel('-log10(p) / significance'); ax.set_title('Volcano Plot'); self.summary['n_points']=len(work); return
        if chart == "ROC Curve":
            ax = self.ax = self.fig.add_subplot(111)
            candidates=[c for c in nums if ds.df[c].dropna().nunique()==2]
            true_col = cx if cx in candidates else (candidates[0] if candidates else None); score_col = cy if cy in nums and cy != true_col else next((c for c in nums if c!=true_col),None)
            if not true_col or not score_col: self._empty(ax,"ROC Curve needs a binary label and numeric score."); return
            work=ds.df[[true_col,score_col]].replace([np.inf,-np.inf],np.nan).dropna(); ytrue=work[true_col].to_numpy(); score=work[score_col].to_numpy(float)
            try:
                from sklearn.metrics import roc_curve, auc
                classes=np.unique(ytrue); ybin=(ytrue==classes[-1]).astype(int); fpr,tpr,_=roc_curve(ybin,score); area=float(auc(fpr,tpr))
            except Exception as exc:
                self._empty(ax,f"ROC calculation requires scikit-learn: {exc}"); return
            ax.plot(fpr,tpr,color=colour,lw=sp.line_width,label=f'AUC = {area:.3f}'); ax.plot([0,1],[0,1],ls='--',color='#7F8C8D'); ax.set_xlim(0,1); ax.set_ylim(0,1); ax.set_xlabel('False positive rate'); ax.set_ylabel('True positive rate'); ax.legend(); ax.set_title('ROC Curve'); self.summary['auc']=area; self.summary['n_points']=len(work); return
        if chart == "Control Chart":
            ax = self.ax = self.fig.add_subplot(111); order=np.argsort(x,kind='stable'); xs,ys=x[order],y[order]; mu=float(np.mean(ys)); sd=float(np.std(ys,ddof=1)) if len(ys)>1 else 0.0; ax.plot(xs,ys,'o-',ms=3,lw=1,color=colour); ax.axhline(mu,color='#34495E'); ax.axhline(mu+3*sd,color='#C0392B',ls='--'); ax.axhline(mu-3*sd,color='#C0392B',ls='--'); ax.set_title('Control Chart'); ax.set_xlabel(get_pretty_label(cx)); ax.set_ylabel(get_pretty_label(cy)); self.summary['n_points']=len(ys); return
        if chart == "Manhattan Plot":
            ax = self.ax = self.fig.add_subplot(111); pv=y; score=np.where((pv>0)&(pv<=1),-np.log10(np.maximum(pv,1e-300)),pv); ax.scatter(x,score,s=8,color=colour,alpha=.65); ax.axhline(-np.log10(5e-8),color='#C0392B',ls='--',lw=.8); ax.set_xlabel(get_pretty_label(cx)); ax.set_ylabel('-log10(p)'); ax.set_title('Manhattan Plot'); self.summary['n_points']=len(x); return
        if chart == "Population Pyramid":
            ax = self.ax = self.fig.add_subplot(111)
            if len(nums)<2: self._empty(ax,"Population Pyramid needs two numeric series."); return
            left,right=nums[:2]; work=ds.df[[left,right]].replace([np.inf,-np.inf],np.nan).dropna(); yi=np.arange(len(work)); ax.barh(yi,-np.abs(work[left].to_numpy(float)),alpha=.7,label=get_pretty_label(left)); ax.barh(yi,np.abs(work[right].to_numpy(float)),alpha=.7,label=get_pretty_label(right)); ax.axvline(0,color='#7F8C8D'); ax.legend(); ax.set_title('Population Pyramid'); self.summary['n_points']=len(work); return
        if chart == "Treemap":
            ax = self.ax = self.fig.add_subplot(111); values=np.abs(y[np.isfinite(y)]); values=values[values>0]
            if not len(values): self._empty(ax,"Treemap needs positive numeric weights."); return
            values=np.sort(values)[::-1][:40]
            try:
                import squarify
                squarify.plot(sizes=values,ax=ax,alpha=.75,pad=True,label=[f'{v:.3g}' for v in values],text_kwargs={'fontsize':7})
                ax.axis('off'); ax.set_title('Treemap')
            except Exception as exc:
                self._empty(ax,f"Treemap requires squarify: {exc}")
            self.summary['n_points']=len(values); return
        if chart == "Sunburst":
            ax = self.ax = self.fig.add_subplot(111); cats=[c for c in ds.df.columns if c not in nums]
            if cats:
                counts=ds.df[cats[0]].astype(str).value_counts().head(12); vals=counts.to_numpy(float); labs=counts.index.tolist()
            else:
                vals=np.abs(y[:min(12,len(y))]); labs=[str(i+1) for i in range(len(vals))]
            ax.pie(vals,labels=labs,wedgeprops=dict(width=.38,edgecolor='white'),textprops={'fontsize':7}); ax.set_title('Sunburst'); self.summary['n_points']=len(vals); return
        if chart == "Sankey Diagram":
            ax = self.ax = self.fig.add_subplot(111); flows=np.abs(y[np.isfinite(y)])[:8]
            if not len(flows): self._empty(ax,"Sankey Diagram needs numeric flow magnitudes."); return
            try:
                from matplotlib.sankey import Sankey
                norm=float(np.sum(flows)) or 1.0; seq=list(flows/norm)+[-1.0]; labels=[f'F{i+1}' for i in range(len(flows))]+['Total']; Sankey(ax=ax,scale=1.0,offset=.2,head_angle=120).add(flows=seq,labels=labels,orientations=[0]*len(flows)+[0]).finish(); ax.set_title('Sankey Diagram')
            except Exception as exc: self._empty(ax,f"Sankey render failed: {exc}")
            self.summary['n_points']=len(flows); return
        if chart == "Venn Diagram":
            ax = self.ax = self.fig.add_subplot(111); cats=[c for c in ds.df.columns if c not in nums]
            if len(cats)<2: self._empty(ax,"Venn Diagram needs two categorical/set columns."); return
            a=set(ds.df[cats[0]].dropna().astype(str)); b=set(ds.df[cats[1]].dropna().astype(str))
            try:
                from matplotlib_venn import venn2
                venn2([a,b],set_labels=(str(cats[0]),str(cats[1])),ax=ax); ax.set_title('Venn Diagram')
            except Exception as exc: self._empty(ax,f"Venn Diagram requires matplotlib-venn: {exc}")
            self.summary['n_points']=len(a|b); return
        if chart == "Piper Diagram":
            ax = self.ax = self.fig.add_subplot(111)
            if len(nums)<6: self._empty(ax,"Piper Diagram needs six numeric ionic components (three cations + three anions)."); return
            data=ds.df[nums[:6]].replace([np.inf,-np.inf],np.nan).dropna().to_numpy(float); data=np.maximum(data,0)
            cat=data[:,:3]; ani=data[:,3:6]; cs=np.where(cat.sum(axis=1)==0,1,cat.sum(axis=1)); ans=np.where(ani.sum(axis=1)==0,1,ani.sum(axis=1)); cat=cat/cs[:,None]; ani=ani/ans[:,None]
            h=np.sqrt(3)/2; cxp=cat[:,1]+.5*cat[:,2]; cyp=h*cat[:,2]; axp=1.25+ani[:,1]+.5*ani[:,2]; ayp=h*ani[:,2]
            ax.plot([0,1,.5,0],[0,0,h,0],color='#7F8C8D'); ax.plot([1.25,2.25,1.75,1.25],[0,0,h,0],color='#7F8C8D'); ax.scatter(cxp,cyp,s=14,color=colour,alpha=.65); ax.scatter(axp,ayp,s=14,color=colour,alpha=.65); ax.set_aspect('equal'); ax.axis('off'); ax.set_title('Piper Diagram'); self.summary['n_points']=len(data); return
        if chart == "Andrews Curves":
            ax = self.ax = self.fig.add_subplot(111)
            vals=ds.df[nums[:min(8,len(nums))]].replace([np.inf,-np.inf],np.nan).dropna().iloc[:40].to_numpy(float)
            if not len(vals): self._empty(ax,"No finite multivariate rows."); return
            vals=(vals-np.nanmean(vals,axis=0))/np.where(np.nanstd(vals,axis=0)>0,np.nanstd(vals,axis=0),1)
            t=np.linspace(-np.pi,np.pi,300)
            for row in vals:
                f=np.full_like(t,row[0]/np.sqrt(2)); idx=1; harmonic=1
                while idx < len(row):
                    f += row[idx]*np.sin(harmonic*t); idx+=1
                    if idx<len(row): f += row[idx]*np.cos(harmonic*t); idx+=1
                    harmonic+=1
                ax.plot(t,f,alpha=0.35,lw=0.8)
            self._finish(ax,"t","Andrews value",default_title="Andrews curves"); self.summary["n_points"]=len(vals); return
        if chart in ("Correlation Matrix","Covariance Matrix"):
            ax = self.ax = self.fig.add_subplot(111)
            frame=ds.df[nums[:min(30,len(nums))]].replace([np.inf,-np.inf],np.nan)
            mat=frame.corr().to_numpy(float) if chart.startswith("Correlation") else frame.cov().to_numpy(float)
            im=ax.imshow(mat,cmap=self.cmap,aspect='auto'); names=[get_pretty_label(c) for c in frame.columns]
            ax.set_xticks(range(len(names))); ax.set_yticks(range(len(names))); ax.set_xticklabels(names,rotation=70,ha='right',fontsize=max(sp.font_size-3,5)); ax.set_yticklabels(names,fontsize=max(sp.font_size-3,5)); self._add_colorbar(im,"ρ" if chart.startswith("Correlation") else "Covariance",ax=ax)
            ax.set_title(chart); self.summary["n_points"]=int(frame.notna().sum().sum()); return
        if chart == "Ridgeline":
            ax = self.ax = self.fig.add_subplot(111); cols2=nums[:min(10,len(nums))]
            for i,c in enumerate(cols2):
                v=ds.df[c].replace([np.inf,-np.inf],np.nan).dropna().to_numpy(float)
                if len(v)<3 or np.ptp(v)<=0: continue
                grid=np.linspace(v.min(),v.max(),200); den=stats.gaussian_kde(v)(grid); den/=max(den.max(),1e-12)
                ax.fill_between(grid,i,i+0.8*den,alpha=0.35); ax.plot(grid,i+0.8*den,lw=1)
            ax.set_yticks(range(len(cols2))); ax.set_yticklabels([get_pretty_label(c) for c in cols2]); ax.set_title("Ridgeline distributions"); ax.grid(False); return

        ax = self.ax = self.fig.add_subplot(111)
        order=np.argsort(x,kind='stable'); xs,ys=x[order],y[order]
        if chart == "Step Mid": ax.step(xs,ys,where='mid',color=colour,lw=sp.line_width,label=label)
        elif chart == "Connected Scatter": ax.plot(x,y,'o-',color=colour,lw=sp.line_width,ms=4,label=label)
        elif chart == "Lollipop": ax.vlines(x,0,y,color=colour,alpha=0.65,lw=max(sp.line_width*0.7,0.5)); ax.scatter(x,y,color=colour,s=20,label=label)
        elif chart == "Fill Between":
            y2=sub[cz].to_numpy(float) if cz in sub and cz!=cy else np.zeros_like(y); ax.fill_between(x,y,y2,color=colour,alpha=0.3,label=label); ax.plot(x,y,color=colour,lw=sp.line_width)
        elif chart == "Event Plot": ax.eventplot(np.sort(x),orientation='horizontal',colors=colour,lineoffsets=1,linelengths=0.8); ax.set_yticks([])
        elif chart == "Cumulative Sum": ax.plot(xs,np.cumsum(ys),color=colour,lw=sp.line_width,label=label)
        elif chart in ("Rolling Mean","Rolling Median","Moving Std"):
            win=max(3,min(len(ys)//12*2+1,101)); ser=pd.Series(ys)
            val=ser.rolling(win,center=True,min_periods=1).mean() if chart=="Rolling Mean" else ser.rolling(win,center=True,min_periods=1).median() if chart=="Rolling Median" else ser.rolling(win,center=True,min_periods=2).std().fillna(0)
            ax.plot(xs,val.to_numpy(),color=colour,lw=sp.line_width,label=f"{label} — {chart}")
        elif chart == "Derivative":
            grad=np.gradient(ys,xs); ax.plot(xs,grad,color=colour,lw=sp.line_width,label='dY/dX')
        elif chart == "Integral":
            integ=np.r_[0,cumulative_trapezoid(ys,xs)]; ax.plot(xs,integ,color=colour,lw=sp.line_width,label='∫Y dX')
        elif chart == "Slope Graph":
            cols2=nums[:min(12,len(nums))]
            for i,c in enumerate(cols2):
                v=ds.df[c].replace([np.inf,-np.inf],np.nan).dropna().to_numpy(float)
                if len(v)>=2: ax.plot([0,1],[v[0],v[-1]],marker='o',lw=1.2,label=get_pretty_label(c))
            ax.set_xticks([0,1]); ax.set_xticklabels(['Start','End'])
        elif chart == "Dot Plot": ax.scatter(np.sort(y),np.arange(len(y)),s=14,color=colour); ax.set_ylabel("Rank")
        elif chart == "KDE Density":
            v=y[np.isfinite(y)]; grid=np.linspace(v.min(),v.max(),300); den=stats.gaussian_kde(v)(grid) if len(v)>2 and np.ptp(v)>0 else np.zeros_like(grid); ax.plot(grid,den,color=colour,lw=sp.line_width); ax.fill_between(grid,0,den,color=colour,alpha=0.18)
        elif chart == "ECDF":
            v=np.sort(y[np.isfinite(y)]); ax.step(v,np.arange(1,len(v)+1)/max(len(v),1),where='post',color=colour,lw=sp.line_width); ax.set_ylim(0,1)
        elif chart == "Cumulative Histogram": ax.hist(y,bins='auto',density=True,cumulative=True,histtype='step',color=colour,lw=sp.line_width)
        elif chart in ("Q-Q Plot","Probability Plot"):
            osm,osr=stats.probplot(y[np.isfinite(y)],dist='norm',fit=False); slope,intercept,*_=stats.linregress(osm,osr); ax.scatter(osm,osr,s=14,color=colour); ax.plot(osm,intercept+slope*np.asarray(osm),color='#7F8C8D',ls='--'); ax.set_xlabel('Theoretical quantiles'); ax.set_ylabel('Ordered values')
        elif chart in ("Strip Plot","Beeswarm"):
            rng=np.random.default_rng(0); jitter=(rng.random(len(y))-0.5)*(0.16 if chart=='Strip Plot' else 0.34); ax.scatter(jitter,y,s=14,color=colour,alpha=0.65); ax.set_xlim(-0.5,0.5); ax.set_xticks([0]); ax.set_xticklabels([get_pretty_label(cy)])
        elif chart == "Residual Plot":
            coef=np.polyfit(x,y,1); pred=np.polyval(coef,x); ax.scatter(pred,y-pred,s=16,color=colour,alpha=0.7); ax.axhline(0,color='#7F8C8D',ls='--'); ax.set_xlabel('Fitted'); ax.set_ylabel('Residual')
        elif chart == "Bland-Altman":
            mean=(x+y)/2; diff=y-x; md=float(np.mean(diff)); sd=float(np.std(diff,ddof=1)); ax.scatter(mean,diff,s=16,color=colour,alpha=.7); ax.axhline(md,color='#34495E'); ax.axhline(md+1.96*sd,color='#C0392B',ls='--'); ax.axhline(md-1.96*sd,color='#C0392B',ls='--'); ax.set_xlabel('Mean'); ax.set_ylabel('Difference')
        elif chart == "Calibration Plot":
            ax.scatter(x,y,s=18,color=colour,alpha=.7); lo=min(np.nanmin(x),np.nanmin(y)); hi=max(np.nanmax(x),np.nanmax(y)); ax.plot([lo,hi],[lo,hi],ls='--',color='#7F8C8D'); ax.set_xlabel('Predicted'); ax.set_ylabel('Observed')
        elif chart == "Confidence Ellipse":
            ax.scatter(x,y,s=14,color=colour,alpha=.5); cov=np.cov(x,y); vals,vecs=np.linalg.eigh(cov); order2=vals.argsort()[::-1]; vals,vecs=vals[order2],vecs[:,order2]; ang=np.degrees(np.arctan2(*vecs[:,0][::-1])); width,height=2*1.96*np.sqrt(np.maximum(vals,0)); ell=Ellipse((np.mean(x),np.mean(y)),width,height,angle=ang,fill=False,edgecolor='#C0392B',lw=1.5); ax.add_patch(ell)
        elif chart == "Spectrogram":
            dt=float(np.nanmedian(np.diff(xs))) if len(xs)>2 else 1.0
            fs=1/max(abs(dt),1e-12)
            nfft=min(256,max(16,2**int(np.floor(np.log2(max(len(ys)//8,16))))))
            noverlap=min(max(0,nfft//2),nfft-1)
            ax.specgram(ys,NFFT=nfft,noverlap=noverlap,Fs=fs,cmap=self.cmap)
            ax.set_ylabel('Frequency'); ax.set_xlabel(get_pretty_label(cx) or 'Time')
        elif chart == "Power Spectral Density":
            dt=float(np.nanmedian(np.diff(xs))) if len(xs)>2 else 1.0; fs=1/max(abs(dt),1e-12); f,p=signal.welch(ys,fs=fs,nperseg=min(256,len(ys))); ax.semilogy(f,np.maximum(p,1e-30),color=colour,lw=sp.line_width); ax.set_xlabel('Frequency'); ax.set_ylabel('PSD')
        elif chart == "Autocorrelation":
            v=ys-np.mean(ys); corr=signal.correlate(v,v,mode='full'); corr=corr[len(v)-1:]; corr/=max(corr[0],1e-12); lags=np.arange(len(corr)); n=min(len(corr),max(50,len(corr)//4)); ax.stem(lags[:n],corr[:n],linefmt=colour,markerfmt='o',basefmt=' '); ax.set_xlabel('Lag'); ax.set_ylabel('Autocorrelation')
        elif chart == "Cross Correlation":
            a=(x-np.mean(x))/(np.std(x) or 1); b=(y-np.mean(y))/(np.std(y) or 1); corr=signal.correlate(a,b,mode='full')/len(a); lags=signal.correlation_lags(len(a),len(b),mode='full'); ax.plot(lags,corr,color=colour,lw=sp.line_width); ax.set_xlabel('Lag'); ax.set_ylabel('Cross-correlation')
        elif chart == "Lag Plot": ax.scatter(ys[:-1],ys[1:],s=16,color=colour,alpha=.65); ax.set_xlabel('Y(t-1)'); ax.set_ylabel('Y(t)')
        self.summary["n_points"] = int(len(sub))
        if chart not in ("Event Plot","Q-Q Plot","Probability Plot","Residual Plot","Bland-Altman","Calibration Plot","Strip Plot","Beeswarm","Spectrogram","Power Spectral Density","Autocorrelation","Cross Correlation","Lag Plot"):
            self._finish(ax,get_pretty_label(cx),get_pretty_label(cy),default_title=chart,xy=(x,y))
        else:
            self._apply_grid(ax); self._apply_styling(ax,ax.get_xlabel(),ax.get_ylabel(),default_title=chart)

    def _render_sensitivity(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        plotted = False
        for name, ds in sp.datasets.items():
            resp = self._col(ds, 'z') or self._col(ds, 'y') or (ds.response_columns[0] if ds.response_columns else None)
            params = ds.parameter_columns or [c for c in ds.numeric_columns if c != resp]
            table = global_sensitivity(ds.df, params, resp) if resp else None
            if table is None or table.empty:
                continue
            labels = [get_pretty_label(p) for p in table["parameter"]]
            colours = [sp.series_color if v >= 0 else "#C0392B" for v in table["SRC"]]
            ypos = np.arange(len(table))
            ax.barh(ypos, table["SRC"], color=colours, alpha=0.9, label="Standardised regression coefficient")
            ax.plot(table["spearman"], ypos, linestyle='none', marker='D', color='#2C3E50', markersize=6, label="Spearman ρ")
            ax.set_yticks(ypos)
            ax.set_yticklabels(labels)
            ax.axvline(0, color='#7F8C8D', linewidth=0.8)
            ax.set_xlim(-1.05, 1.05)
            r2 = table.attrs.get("r2", np.nan)
            short = get_pretty_label(resp)
            short = short if len(short) <= 40 else short[:38] + "…"
            self._finish(ax, "Sensitivity index (−1 … 1)", "", default_title=f"Global sensitivity of {short}\n(linear model R² = {r2:.2f})")
            self.summary["sensitivity"] = table.to_dict(orient="list")
            self.summary["n_points"] += len(ds.df)
            plotted = True
            break
        if not plotted:
            self._empty(ax, "Global sensitivity needs ≥2 sampled parameter columns and a response.")
            self._finish(ax)

    # -- marginals ------------------------------------------------------
    def _render_marginals(self):
        sp = self.spec
        ds = next(iter(sp.datasets.values()), None)
        if ds is None:
            self.ax = self.fig.add_subplot(111)
            self._empty(self.ax, "No dataset selected.")
            return
        resp = self._col(ds, 'z') or self._col(ds, 'y') or (ds.response_columns[0] if ds.response_columns else None)
        params = [p for p in (ds.parameter_columns or ds.numeric_columns) if p != resp][:8]
        if not resp or not params:
            self.ax = self.fig.add_subplot(111)
            self._empty(self.ax, "1D marginals need a response and at least one parameter column.")
            return
        n = len(params)
        ncols = 2 if n <= 4 else 3
        nrows = int(np.ceil(n / ncols))
        axes = []
        for i, p in enumerate(params, 1):
            ax = self.fig.add_subplot(nrows, ncols, i)
            sub = self._prepare_subset(ds, [p, resp])
            if sub.empty:
                continue
            x, y = sub[p].to_numpy(float), sub[resp].to_numpy(float)
            bins = 25
            edges = np.linspace(x.min(), x.max(), bins + 1)
            idx = np.clip(np.searchsorted(edges, x, side='right') - 1, 0, bins - 1)
            centres = 0.5 * (edges[:-1] + edges[1:])
            mean = np.array([y[idx == b].mean() if np.any(idx == b) else np.nan for b in range(bins)])
            std = np.array([y[idx == b].std() if np.sum(idx == b) > 1 else 0.0 for b in range(bins)])
            good = np.isfinite(mean)
            ax.fill_between(centres[good], (mean - std)[good], (mean + std)[good], color=sp.series_color, alpha=0.18, linewidth=0)
            ax.plot(centres[good], mean[good], color=sp.series_color, linewidth=2.0)
            ax.set_xlabel(get_pretty_label(p), fontsize=self._fs("X-Axis Label", -1), color=self._style("X-Axis Label").get("color"))
            if (i - 1) % ncols == 0:
                ax.set_ylabel(get_pretty_label(resp), fontsize=self._fs("Y-Axis Label", -1), color=self._style("Y-Axis Label").get("color"))
            self._apply_grid(ax)
            tick = self._style("Tick Labels")
            ax.tick_params(colors=tick.get("color", "#2C3E50"), labelsize=self._fs("Tick Labels", -2))
            axes.append(ax)
            self.summary["n_points"] += x.size
        st = self._style("Main Title")
        self.fig.suptitle(st.get("text") or f"Marginal response of {get_pretty_label(resp)} (mean ± σ)",
                          color=st.get("color", "#1F2D3D"), fontsize=self._fs("Main Title", 3))
        self.ax = axes[0] if axes else self.fig.add_subplot(111)

    # -- profiles -------------------------------------------------------
    def _profile_matrix(self, ds: Dataset, kind: str):
        m = self.spec.mappings.get('matrix')
        if m and m in ds.matrices:
            return m, ds.matrices[m]
        for k, v in ds.matrices.items():
            if kind in k.lower():
                return k, v
        return None, None

    def _time_vector(self, ds: Dataset, n_time: int):
        cx = self.spec.mappings.get('x')
        for src in (ds.aux, {c: ds.df[c].to_numpy(float) for c in ds.numeric_columns}):
            if cx and cx in src and np.asarray(src[cx]).size == n_time:
                return np.asarray(src[cx], float), get_pretty_label(cx)
        for k, v in ds.aux.items():
            if np.asarray(v).size == n_time and any(t in k.lower() for t in ('time', 't_', 'tspan', 'days', 'hours')):
                return np.asarray(v, float), get_pretty_label(k)
        return np.arange(n_time, dtype=float), "Time index"

    def _render_profile(self):
        sp = self.spec
        kind = 'scod' if 'sCOD' in sp.chart_type else 'vfa'
        ax = self.ax = self.fig.add_subplot(111)
        first_xy = None
        xlabel = "Time"
        for name, ds in sp.datasets.items():
            key, M = self._profile_matrix(ds, kind)
            label = clean_series_name(name)
            if M is not None and M.ndim == 2:
                M = np.asarray(M, float)
                if M.shape[0] < M.shape[1] and M.shape[0] <= 5:
                    M = M.T
                n_time = M.shape[1]
                t, xlabel = self._time_vector(ds, n_time)
                finite = np.isfinite(M).all(axis=1)
                M = M[finite]
                if M.size == 0:
                    continue
                med = np.nanmedian(M, axis=0)
                lo, hi = np.nanpercentile(M, [10, 90], axis=0)
                for row in M[:: max(len(M) // 20, 1)][:20]:
                    ax.plot(t, row, color=sp.series_color, alpha=0.12, linewidth=0.8)
                ax.fill_between(t, lo, hi, color=sp.series_color, alpha=0.2, linewidth=0, label=f"{label} — 10–90 % band")
                ax.plot(t, med, color=sp.series_color, linewidth=2.4, label=f"{label} — median ({key})")
                self.summary["n_points"] += M.size
                if first_xy is None:
                    first_xy = (t, med)
            else:
                col = next((c for c in ds.numeric_columns if kind in c.lower()), None)
                cx = self._col(ds, 'x', 0)
                if col and cx and cx != col:
                    sub = self._prepare_subset(ds, [cx, col])
                    if not sub.empty:
                        x, y = sub[cx].to_numpy(float), sub[col].to_numpy(float)
                        ax.plot(x, y, color=sp.series_color, linewidth=1.8, label=f"{label} — {get_pretty_label(col)}")
                        xlabel = get_pretty_label(cx)
                        self.summary["n_points"] += x.size
                        if first_xy is None:
                            first_xy = (x, y)
        if first_xy is None:
            self._empty(ax, f"No {kind.upper()} profile matrix or column found in the selected dataset(s).")
        unit = "sCOD [mg/L]" if kind == 'scod' else "VFA [mg/L]"
        self._finish(ax, xlabel, unit, xy=first_xy)

    # -- polarisation ---------------------------------------------------
    def _render_polarisation(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        ax2 = None
        first_xy = None
        for name, ds in sp.datasets.items():
            ci, cv = self._col(ds, 'x', 0), self._col(ds, 'y', 1)
            if not ci or not cv or ci == cv:
                continue
            sub = self._prepare_subset(ds, [ci, cv])
            res = polarisation_power(sub[ci], sub[cv]) if not sub.empty else None
            if not res:
                continue
            label = clean_series_name(name)
            ax.plot(res['i'], res['v'], color=sp.series_color, marker='o', markersize=3.5, linewidth=1.6, label=f"{label} — polarisation")
            if ax2 is None:
                ax2 = ax.twinx()
            ax2.plot(res['i'], res['p'], color='#D35400', linestyle='--', linewidth=1.6, label=f"{label} — power density")
            ax2.plot([res['i_peak']], [res['p_peak']], marker='*', markersize=13, color='#D35400', linestyle='none',
                     label=f"Peak power {res['p_peak']:.3g} @ {res['i_peak']:.3g}")
            self.summary.setdefault("polarisation", {})[name] = {"i_peak": res['i_peak'], "p_peak": res['p_peak'], "v_peak": res['v_peak']}
            self.summary["n_points"] += res['i'].size
            if first_xy is None:
                first_xy = (res['i'], res['v'])
        if first_xy is None:
            self._empty(ax, "Map X = current density and Y = cell voltage.")
        if ax2 is not None:
            ax2.set_ylabel("Power density [W m⁻²]", color='#D35400', fontsize=self._fs("Y-Axis Label", 1))
            ax2.tick_params(axis='y', colors='#D35400')
            h1, l1 = ax.get_legend_handles_labels()
            h2, l2 = ax2.get_legend_handles_labels()
            for h, l in zip(h2, l2):
                ax.plot([], [], color=h.get_color() if hasattr(h, 'get_color') else '#D35400',
                        linestyle=getattr(h, 'get_linestyle', lambda: '--')(), marker=getattr(h, 'get_marker', lambda: None)(), label=l)
        self._finish(ax, get_pretty_label(sp.mappings.get('x')) or "Current density [A m⁻²]",
                     get_pretty_label(sp.mappings.get('y')) or "Cell voltage [V]", xy=first_xy)

    # -- EIS ------------------------------------------------------------
    def _render_nyquist(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        plotted = False
        for name, ds in sp.datasets.items():
            cr, ci, cw = self._col(ds, 'x', 0), self._col(ds, 'y', 1), self._col(ds, 'w')
            if not cr or not ci or cr == ci:
                continue
            sub = self._prepare_subset(ds, [cr, ci] + ([cw] if cw and cw not in (cr, ci) else []))
            if sub.empty:
                continue
            zr, zi = sub[cr].to_numpy(float), sub[ci].to_numpy(float)
            zi_plot = -zi if np.nanmedian(zi) < 0 else zi
            label = clean_series_name(name)
            if cw and cw in sub.columns:
                w = sub[cw].to_numpy(float)
                wl = np.log10(np.abs(w) + 1e-12)
                sc = ax.scatter(zr, zi_plot, c=wl, cmap=self.cmap, s=22, label=f"{label} — Nyquist", edgecolors='white', linewidths=0.3)
                self._add_colorbar(sc, f"log₁₀ {get_pretty_label(cw)}")
            else:
                ax.plot(zr, zi_plot, marker='o', markersize=4, linewidth=1.2, color=sp.series_color, label=f"{label} — Nyquist")
            fit = fit_nyquist_semicircle(zr, zi_plot)
            if fit:
                ax.plot(fit['arc_x'], fit['arc_y'], linestyle='--', color='#C0392B', linewidth=1.3,
                        label=f"Semicircle fit: Rs={fit['Rs']:.3g}, Rct={fit['Rct']:.3g}")
                self.summary.setdefault("eis", {})[name] = {"Rs": fit['Rs'], "Rct": fit['Rct']}
            self.summary["n_points"] += zr.size
            plotted = True
        if not plotted:
            self._empty(ax, "Map X = Z′ (real) and Y = Z″ (imaginary).")
        try:
            ax.set_aspect('equal', adjustable='datalim')
        except Exception:
            pass
        self._finish(ax, "Z′ [Ω]", "−Z″ [Ω]")

    def _render_bode(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        ax2 = ax.twinx()
        plotted = False
        for name, ds in sp.datasets.items():
            cf, cr, ci = self._col(ds, 'x', 0), self._col(ds, 'y', 1), self._col(ds, 'z', 2)
            if not (cf and cr and ci) or len({cf, cr, ci}) < 3:
                continue
            sub = self._prepare_subset(ds, [cf, cr, ci])
            sub = sub[sub[cf] > 0]
            if sub.empty:
                continue
            f, zr, zi = sub[cf].to_numpy(float), sub[cr].to_numpy(float), sub[ci].to_numpy(float)
            order = np.argsort(f)
            f, zr, zi = f[order], zr[order], zi[order]
            mag = np.hypot(zr, zi)
            phase = np.degrees(np.arctan2(np.abs(zi), zr))
            label = clean_series_name(name)
            ax.plot(f, mag, color=sp.series_color, marker='o', markersize=3, linewidth=1.4, label=f"{label} — |Z|")
            ax2.plot(f, phase, color='#D35400', linestyle='--', linewidth=1.4, label=f"{label} — phase")
            self.summary["n_points"] += f.size
            plotted = True
        if not plotted:
            self._empty(ax, "Map X = frequency, Y = Z′ and Z = Z″.")
        ax2.set_ylabel("−Phase [°]", color='#D35400', fontsize=self._fs("Y-Axis Label", 1))
        ax2.tick_params(axis='y', colors='#D35400')
        for h, l in zip(*ax2.get_legend_handles_labels()):
            ax.plot([], [], color='#D35400', linestyle='--', label=l)
        self._finish(ax, "Frequency [Hz]", "|Z| [Ω]")

    # -- Gompertz -------------------------------------------------------
    def _render_gompertz(self):
        sp = self.spec
        ax = self.ax = self.fig.add_subplot(111)
        first_xy = None
        for name, ds in sp.datasets.items():
            ct, ch = self._col(ds, 'x', 0), self._col(ds, 'y', 1)
            if not ct or not ch or ct == ch:
                continue
            sub = self._prepare_subset(ds, [ct, ch])
            res = fit_gompertz(sub[ct], sub[ch]) if not sub.empty else None
            label = clean_series_name(name)
            if not res:
                if not sub.empty:
                    ax.plot(sub[ct], sub[ch], color='#7F8C8D', marker='o', markersize=3, linewidth=0.8, label=f"{label} — data (fit failed)")
                continue
            ax.scatter(res['t'], res['H'], color='#7F8C8D', s=16, label=f"{label} — data", zorder=3)
            ax.plot(res['t_fine'], res['fit_fine'], color=sp.series_color, linewidth=2.5,
                    label=f"{label} — Gompertz fit (R²={res['r2']:.3f})", zorder=4)
            ax.axhline(res['P'], color=sp.series_color, linestyle=':', linewidth=1.0, alpha=0.7)
            ax.annotate(f"P = {res['P']:.3g}\nRm = {res['Rm']:.3g}\nλ = {res['lag']:.3g}", xy=(0.02, 0.97), xycoords='axes fraction',
                        va='top', fontsize=max(sp.font_size - 1, 6), bbox=dict(boxstyle='round', fc='white', alpha=0.85, ec='#BDC3C7'))
            self.summary.setdefault("gompertz", {})[name] = {k: res[k] for k in ('P', 'Rm', 'lag', 'r2')}
            self.summary["n_points"] += res['t'].size
            if first_xy is None:
                first_xy = (res['t'], res['H'])
        if first_xy is None:
            self._empty(ax, "Map X = time and Y = cumulative H₂ for a Gompertz fit.")
        self._finish(ax, get_pretty_label(sp.mappings.get('x')) or "Time [days]",
                     get_pretty_label(sp.mappings.get('y')) or "Cumulative H₂ [m³ m⁻³]", xy=first_xy)


# =====================================================================
# public API
# =====================================================================
def precompute_surfaces(spec: PlotSpec, *, for_export: bool = False) -> dict:
    """Warm expensive surface caches without constructing a Matplotlib figure.

    This function is deliberately Qt/pyplot-free and can run on GraphVis'
    compute pool.  The subsequent single-threaded Matplotlib render then sees
    cache hits rather than occupying the plotting worker with TPS/Kriging/etc.
    """
    if spec.chart_type not in SURFACE_FIELD_CHARTS:
        return {"surfaces": 0, "seconds": 0.0, "notes": []}
    t0 = time.perf_counter()
    r = object.__new__(_Renderer)
    r.spec = spec
    r.for_export = bool(for_export)
    r.summary = {"chart": spec.chart_type, "datasets": [], "n_points": 0, "notes": []}
    r.warnings = []
    count = 0
    for name, ds in spec.datasets.items():
        if (spec.metadata or {}).get("_cancel_check") and spec.metadata["_cancel_check"]():
            raise RuntimeError("Cancelled")
        cx, cy, cz = r._col(ds, 'x', 0), r._col(ds, 'y', 1), r._col(ds, 'z', 2)
        sub = None
        surface = None
        if cx and cy and cz and len({cx, cy, cz}) >= 3:
            extra = []
            for role in ('w', 'gradient'):
                c = r._col(ds, role)
                if c and c not in (cx, cy, cz):
                    extra.append(c)
            sub = r._prepare_subset(ds, [cx, cy, cz] + extra)
            if not sub.empty and sub[cx].nunique() >= 2 and sub[cy].nunique() >= 2:
                surface = r._surface_from(ds, sub, cx, cy, cz)
                count += 1
                # Warm separately mapped colour/gradient surfaces as well.
                for role in ('w', 'gradient'):
                    c = r._col(ds, role)
                    if c and c in sub.columns and c not in (cx, cy, cz):
                        try:
                            r._surface_from(ds, sub, cx, cy, c); count += 1
                        except Exception:
                            pass
        if surface is None:
            matrix_name, matrix = r._selected_array(ds, 'matrix', ndim=2)
            if matrix is not None:
                r._surface_from_matrix(ds, matrix_name, matrix)
                count += 1
    return {"surfaces": count, "seconds": round(time.perf_counter() - t0, 4),
            "notes": list(r.summary.get("notes", [])), "warnings": list(r.warnings)}


def build_figure(spec: PlotSpec, dpi: int = 100, figsize=None, for_export: bool = False) -> RenderResult:
    """Build a complete matplotlib Figure for `spec`. Thread-safe (no Qt,
    no pyplot). The returned figure owns an Agg canvas; the GUI swaps in a
    Qt canvas afterwards."""
    # Central range sanitization: inverted/corrupted clipping bounds are
    # swapped or replaced with true data extents before anything renders.
    try:
        data_by_role = {}
        for role in ("x", "y", "z", "w", "v"):
            col = (spec.mappings or {}).get(role)
            for ds in spec.datasets.values():
                if col and col in getattr(ds, "df", pd.DataFrame()).columns:
                    data_by_role[role] = pd.to_numeric(ds.df[col], errors="coerce").to_numpy(float)
                    break
        clean, fixes = sanitize_clipping_ranges(spec.clipping_ranges, data_by_role)
        if fixes:
            spec = spec.clone()
            spec.clipping_ranges = clean
    except Exception:
        fixes = []
    renderer = _Renderer(spec, dpi=dpi, figsize=figsize, for_export=for_export)
    for note in fixes:
        renderer.warnings.append(note)
    return renderer.render()


def axis_roles(chart_type: str) -> dict:
    return AXIS_ROLES.get(chart_type, AXIS_ROLES["Line Chart"])
