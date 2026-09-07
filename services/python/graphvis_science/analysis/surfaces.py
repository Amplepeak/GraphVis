"""Scientific surface interpolation, ported from GraphVis 17.

Sixteen estimators - structured matrix methods, scattered estimators, radial
basis functions and local regression - reached from the desktop through the
`surface.estimate` service operation.

This lives in the science service rather than in C++ deliberately. Every one of
these is built on scipy: Delaunay and Voronoi from Qhull, CloughTocher2DInterpolator,
cKDTree, distance_transform_edt, gaussian_filter. Re-implementing computational
geometry that the application already ships a library for would be a great deal
of unverified C++ to avoid a dependency that is already present.

The native side keeps its own binning-plus-gap-closing surface (QtPlotBackend's
gridFromSeries), so a surface still draws with no add-on installed; these
estimators are the accurate version when it is.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Iterable
from collections import OrderedDict
import hashlib
import math
import threading

import numpy as np
from scipy.interpolate import (CloughTocher2DInterpolator, LinearNDInterpolator,
                               NearestNDInterpolator, RBFInterpolator,
                               RectBivariateSpline, RegularGridInterpolator, PchipInterpolator)
from scipy.ndimage import distance_transform_edt, gaussian_filter, label as ndi_label
from scipy.spatial import Delaunay, QhullError, cKDTree, distance


ESTIMATOR_CATEGORIES: tuple[tuple[str, tuple[str, ...]], ...] = (
    ("Recommended", (
        "Auto (data-aware)",
    )),
    ("Structured Matrix Methods", (
        "Nearest Neighbor",
        "Bilinear Interpolation",
        "Bicubic Interpolation",
        "Rectangular B-Spline",
        "Monotone PCHIP (Structured)",
    )),
    ("Unstructured Scattered Estimators", (
        "Delaunay Triangulation (Linear)",
        "Clough–Tocher C1 Smooth",
        "Natural Neighbor (Sibson’s)",
        "Inverse Distance Weighting (IDW)",
        "Modified Shepard's Method",
    )),
    ("Radial Basis Functions (RBF)", (
        "Thin Plate Spline (TPS)",
        "Multiquadric RBF",
        "Gaussian RBF",
    )),
    ("Statistical & Local Regression", (
        "Ordinary Kriging",
        "Moving Least Squares (MLS)",
        "LOESS / LOWESS",
    )),
)

ALL_ESTIMATORS: tuple[str, ...] = tuple(v for _, values in ESTIMATOR_CATEGORIES for v in values)

_ESTIMATOR_ALIASES = {
    "auto": "Auto (data-aware)",
    "auto (data-aware)": "Auto (data-aware)",
    "scattered interpolation": "Delaunay Triangulation (Linear)",
    "rbf kriging": "Multiquadric RBF",
    "polynomial spline": "Thin Plate Spline (TPS)",
    "delaunay": "Delaunay Triangulation (Linear)",
    "clough tocher": "Clough–Tocher C1 Smooth",
    "clough-tocher": "Clough–Tocher C1 Smooth",
    "cloughtocher": "Clough–Tocher C1 Smooth",
    "pchip": "Monotone PCHIP (Structured)",
    "monotone": "Monotone PCHIP (Structured)",
    "linear": "Delaunay Triangulation (Linear)",
    "idw": "Inverse Distance Weighting (IDW)",
    "modified shepard": "Modified Shepard's Method",
    "natural neighbor": "Natural Neighbor (Sibson’s)",
    "natural neighbor (sibson)": "Natural Neighbor (Sibson’s)",
    "natural neighbor (sibson's)": "Natural Neighbor (Sibson’s)",
    "natural neighbor (sibson’s)": "Natural Neighbor (Sibson’s)",
    "sibson": "Natural Neighbor (Sibson’s)",
    "tps": "Thin Plate Spline (TPS)",
    "multiquadric": "Multiquadric RBF",
    "gaussian": "Gaussian RBF",
    "kriging": "Ordinary Kriging",
    "mls": "Moving Least Squares (MLS)",
    "loess": "LOESS / LOWESS",
    "lowess": "LOESS / LOWESS",
}


def canonical_estimator(value: object) -> str:
    raw = str(value or "Auto (data-aware)").strip()
    if raw in ALL_ESTIMATORS:
        return raw
    return _ESTIMATOR_ALIASES.get(raw.lower(), "Auto (data-aware)")


@dataclass
class SurfaceGrid:
    """Physical grid plus a masked response and diagnostic metadata."""

    X: np.ndarray
    Y: np.ndarray
    Z: np.ma.MaskedArray
    method: str
    structured: bool = False
    invalid_mask: np.ndarray | None = None
    outside_hull_mask: np.ndarray | None = None
    imputed_mask: np.ndarray | None = None
    notes: list[str] = field(default_factory=list)

    def __iter__(self):
        # Backwards-compatible tuple unpacking in existing renderer call sites.
        yield self.X
        yield self.Y
        yield self.Z


@dataclass
class CoordinateTransform:
    """Map physical X/Y into a normalised interpolation metric."""

    x_log: bool
    y_log: bool
    x0: float
    x1: float
    y0: float
    y1: float

    @classmethod
    def fit(cls, x: np.ndarray, y: np.ndarray, x_log: bool, y_log: bool) -> "CoordinateTransform":
        tx = np.log10(x) if x_log else x.astype(float, copy=False)
        ty = np.log10(y) if y_log else y.astype(float, copy=False)
        x0, x1 = float(np.nanmin(tx)), float(np.nanmax(tx))
        y0, y1 = float(np.nanmin(ty)), float(np.nanmax(ty))
        if not np.isfinite(x0 + x1 + y0 + y1):
            raise ValueError("Surface coordinates contain no finite range.")
        if x1 <= x0:
            x1 = x0 + 1.0
        if y1 <= y0:
            y1 = y0 + 1.0
        return cls(bool(x_log), bool(y_log), x0, x1, y0, y1)

    def transform(self, x: np.ndarray, y: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        tx = np.log10(x) if self.x_log else np.asarray(x, dtype=float)
        ty = np.log10(y) if self.y_log else np.asarray(y, dtype=float)
        return (tx - self.x0) / (self.x1 - self.x0), (ty - self.y0) / (self.y1 - self.y0)

    def inverse_x(self, u: np.ndarray) -> np.ndarray:
        raw = self.x0 + np.asarray(u, dtype=float) * (self.x1 - self.x0)
        return np.power(10.0, raw) if self.x_log else raw

    def inverse_y(self, v: np.ndarray) -> np.ndarray:
        raw = self.y0 + np.asarray(v, dtype=float) * (self.y1 - self.y0)
        return np.power(10.0, raw) if self.y_log else raw


@dataclass
class GeometryPlan:
    """Reusable metric-space geometry for one X/Y mapping.

    The same KD-tree/Delaunay triangulation can service multiple response
    variables, contour settings and grid resolutions.  Keeping this geometry
    separate from Z values removes repeated Qhull work during UI interaction.
    """
    points: np.ndarray
    tree: cKDTree
    triangulation: Delaunay | None


_GEOMETRY_CACHE_LOCK = threading.Lock()
_GEOMETRY_CACHE: "OrderedDict[str, GeometryPlan]" = OrderedDict()
_GEOMETRY_CACHE_CAPACITY = 24


def _points_digest(points: np.ndarray) -> str:
    arr = np.ascontiguousarray(np.asarray(points, dtype=np.float64))
    h = hashlib.blake2b(digest_size=16)
    h.update(str(arr.shape).encode("ascii"))
    h.update(memoryview(arr).cast("B"))
    return h.hexdigest()


def geometry_plan(points: np.ndarray, cache_key: object | None = None) -> GeometryPlan:
    """Return cached KD-tree/Qhull geometry for normalized X/Y points."""
    pts = np.ascontiguousarray(np.asarray(points, dtype=float))
    digest = _points_digest(pts)
    key = f"{cache_key!s}:{digest}" if cache_key is not None else digest
    with _GEOMETRY_CACHE_LOCK:
        hit = _GEOMETRY_CACHE.get(key)
        if hit is not None and hit.points.shape == pts.shape:
            _GEOMETRY_CACHE.move_to_end(key)
            return hit
    tri = None
    if len(pts) >= 3:
        try:
            tri = Delaunay(pts, qhull_options="Qbb Qc Qz Q12")
        except QhullError:
            tri = None
    plan = GeometryPlan(points=pts, tree=cKDTree(pts), triangulation=tri)
    with _GEOMETRY_CACHE_LOCK:
        _GEOMETRY_CACHE[key] = plan
        _GEOMETRY_CACHE.move_to_end(key)
        while len(_GEOMETRY_CACHE) > _GEOMETRY_CACHE_CAPACITY:
            _GEOMETRY_CACHE.popitem(last=False)
    return plan


def clear_geometry_cache() -> None:
    with _GEOMETRY_CACHE_LOCK:
        _GEOMETRY_CACHE.clear()


# ---------------------------------------------------------------------------
# Geometry / masking helpers
# ---------------------------------------------------------------------------
def _spatial_valid(x: np.ndarray, y: np.ndarray, x_log: bool, y_log: bool) -> np.ndarray:
    mask = np.isfinite(x) & np.isfinite(y)
    if x_log:
        mask &= x > 0
    if y_log:
        mask &= y > 0
    return mask


def _aggregate_duplicates(points: np.ndarray, values: np.ndarray, statistic: str = "mean") -> tuple[np.ndarray, np.ndarray]:
    """Collapse exact duplicate metric coordinates before Qhull/local solvers."""
    if len(points) == 0:
        return points, values
    # Rounding removes floating noise introduced by log/normalisation while
    # preserving far more precision than the interpolation needs.
    rounded = np.round(np.asarray(points, float), 14)
    uniq, inv = np.unique(rounded, axis=0, return_inverse=True)
    if len(uniq) == len(points):
        return np.asarray(points, float), np.asarray(values, float)
    out = np.empty(len(uniq), dtype=float)
    stat = str(statistic or "mean").lower()
    for i in range(len(uniq)):
        vals = np.asarray(values, float)[inv == i]
        vals = vals[np.isfinite(vals)]
        if vals.size == 0:
            out[i] = np.nan
        elif stat == "median":
            out[i] = float(np.nanmedian(vals))
        elif stat == "std":
            out[i] = float(np.nanstd(vals))
        elif stat == "count":
            out[i] = float(vals.size)
        else:
            out[i] = float(np.nanmean(vals))
    good = np.isfinite(out)
    return uniq[good], out[good]


def _outside_hull(points: np.ndarray, queries: np.ndarray, tri: Delaunay | None = None) -> np.ndarray:
    if len(points) < 3:
        return np.ones(len(queries), dtype=bool)
    if tri is None:
        try:
            tri = Delaunay(points, qhull_options="Qbb Qc Qz Q12")
        except QhullError:
            return np.zeros(len(queries), dtype=bool)
    try:
        return tri.find_simplex(queries) < 0
    except Exception:
        return np.zeros(len(queries), dtype=bool)


def _invalid_region_mask(valid_points: np.ndarray, invalid_points: np.ndarray,
                         queries: np.ndarray, resolution: int,
                         policy: str = "Sample cell only",
                         valid_tree: cKDTree | None = None) -> np.ndarray:
    """Protect failed ODE coordinates without creating artificial white islands.

    ``Sample cell only`` is deliberately the default for dense parameter sweeps:
    a failed solver coordinate remains visible, but its Voronoi territory is not
    removed from an otherwise well-supported interpolated field.  Stronger modes
    are available when a failed simulation should invalidate its neighbourhood.
    """
    if invalid_points is None or len(invalid_points) == 0 or len(valid_points) == 0:
        return np.zeros(len(queries), dtype=bool)
    valid_tree = valid_tree or cKDTree(valid_points)
    invalid_tree = cKDTree(invalid_points)
    dv = valid_tree.query(queries, k=1, workers=1)[0]
    di = invalid_tree.query(queries, k=1, workers=1)[0]
    if len(valid_points) >= 2:
        nn = valid_tree.query(valid_points, k=2, workers=1)[0][:, 1]
        nn = nn[np.isfinite(nn) & (nn > 0)]
        spacing = float(np.nanmedian(nn)) if nn.size else 1.0 / max(int(resolution), 2)
    else:
        spacing = 1.0 / max(int(resolution), 2)
    pixel = 1.0 / max(int(resolution) - 1, 2)
    mode = str(policy or "Sample cell only").strip().lower()
    if mode.startswith("none"):
        return np.zeros(len(queries), dtype=bool)
    if mode.startswith("voronoi"):
        radius = max(0.55 * spacing, 0.65 * pixel)
        return (di <= radius) | (di < 0.92 * dv)
    if mode.startswith("conservative"):
        radius = max(0.42 * spacing, 0.55 * pixel)
        return (di <= radius) | (di < 0.60 * dv)
    # A compact footprint approximating the source cell itself.  This is the
    # least visually destructive option and closely matches MATLAB-style sweep
    # plots where isolated failed ODE points should not excavate large holes.
    radius = max(0.23 * spacing, 0.42 * pixel)
    return di <= radius


def _fill_nan_nearest_grid(z: np.ndarray) -> np.ndarray:
    arr = np.asarray(z, dtype=float)
    invalid = ~np.isfinite(arr)
    if not invalid.any():
        return arr
    if invalid.all():
        return np.zeros_like(arr)
    indices = distance_transform_edt(invalid, return_distances=False, return_indices=True)
    return arr[tuple(indices)]


def _weighted_gaussian(z: np.ndarray, sigma: float, protected_mask: np.ndarray) -> np.ndarray:
    if sigma <= 0:
        return np.asarray(z, float)
    arr = np.asarray(z, float)
    valid = np.isfinite(arr) & ~np.asarray(protected_mask, bool)
    if not valid.any():
        return arr
    num = gaussian_filter(np.where(valid, arr, 0.0), sigma=float(sigma), mode="nearest")
    den = gaussian_filter(valid.astype(float), sigma=float(sigma), mode="nearest")
    out = np.divide(num, den, out=np.full_like(num, np.nan), where=den > 1e-12)
    out[np.asarray(protected_mask, bool)] = np.nan
    return out


def _detect_structured(u_all: np.ndarray, v_all: np.ndarray, z_all: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray] | None:
    """Vectorised complete-Cartesian sweep detection.

    The older nested row/column boolean scans were O(N * Nx * Ny) and could
    spend several seconds merely rediscovering a 250x250 matrix.  This version
    maps each sample to one integer cell and uses bincount aggregation.
    """
    if len(u_all) < 4:
        return None
    ur = np.round(np.asarray(u_all, dtype=float), 13)
    vr = np.round(np.asarray(v_all, dtype=float), 13)
    zz = np.asarray(z_all, dtype=float)
    ux, ui = np.unique(ur, return_inverse=True)
    vy, vi = np.unique(vr, return_inverse=True)
    if len(ux) < 2 or len(vy) < 2:
        return None
    ncells = int(len(ux) * len(vy))
    cell = vi.astype(np.int64) * int(len(ux)) + ui.astype(np.int64)
    if np.unique(cell).size != ncells:
        return None
    good = np.isfinite(zz)
    sums = np.bincount(cell[good], weights=zz[good], minlength=ncells)
    counts = np.bincount(cell[good], minlength=ncells)
    flat = np.full(ncells, np.nan, dtype=float)
    np.divide(sums, counts, out=flat, where=counts > 0)
    return ux.astype(float), vy.astype(float), flat.reshape(len(vy), len(ux))


def _classify_bridgeable_mask(mask: np.ndarray, policy: str, max_cells: int) -> tuple[np.ndarray, np.ndarray]:
    """Split failed grid cells into bridgeable interior holes and preserved regions."""
    bad = np.asarray(mask, dtype=bool)
    bridge = np.zeros_like(bad)
    preserve = bad.copy()
    mode = str(policy or "Preserve all failures").lower()
    if not bad.any() or mode.startswith("preserve") or mode.startswith("none"):
        return bridge, preserve
    labels, count = ndi_label(bad)
    unlimited = mode.startswith("bridge all interior")
    isolated_only = mode.startswith("bridge isolated")
    limit = 1 if isolated_only else max(1, int(max_cells))
    for region in range(1, int(count) + 1):
        component = labels == region
        size = int(component.sum())
        touches = bool(component[0].any() or component[-1].any() or component[:, 0].any() or component[:, -1].any())
        if not touches and (unlimited or size <= limit):
            bridge |= component
            preserve[component] = False
    return bridge, preserve


def structured_surface_native(x_axis: np.ndarray, y_axis: np.ndarray, z_grid: np.ndarray, *,
                              x_log: bool = False, y_log: bool = False,
                              invalid_mask_policy: str = "Sample cell only",
                              failure_bridge_policy: str = "Bridge isolated failures",
                              failure_bridge_max_cells: int = 4,
                              progressive: bool = False) -> SurfaceGrid:
    """Return a pre-gridded matrix without rebuilding/interpolating its topology.

    This is the high-throughput path for kinetic parameter sweeps.  A 250x250
    response matrix is already a surface; GraphVis therefore preserves it and
    only performs optional LOD decimation and failure-mask classification.
    """
    xa = np.asarray(x_axis, dtype=float).ravel()
    ya = np.asarray(y_axis, dtype=float).ravel()
    z = np.asarray(z_grid, dtype=float)
    if z.shape != (ya.size, xa.size):
        raise ValueError(f"Native structured matrix shape {z.shape} does not match axes {(ya.size, xa.size)}.")
    if x_log and np.any(xa <= 0):
        raise ValueError("Log10 X axes require strictly positive matrix coordinates.")
    if y_log and np.any(ya <= 0):
        raise ValueError("Log10 Y axes require strictly positive matrix coordinates.")
    # During slider motion use topology-preserving stride decimation rather
    # than flattening/random sampling.  Full fidelity returns automatically.
    if progressive and max(z.shape) > 64:
        sy = max(1, int(math.ceil(z.shape[0] / 64)))
        sx = max(1, int(math.ceil(z.shape[1] / 64)))
        ya = ya[::sy]; xa = xa[::sx]; z = z[::sy, ::sx]
    invalid = ~np.isfinite(z)
    bridge, preserve = _classify_bridgeable_mask(invalid, failure_bridge_policy, failure_bridge_max_cells)
    z_work = z.copy()
    if bridge.any():
        filled = _fill_nan_nearest_grid(z_work)
        z_work[bridge] = filled[bridge]
    # Existing footprint modes remain meaningful: "None" allows the filled
    # surface through all failed cells, while other modes preserve failures.
    if str(invalid_mask_policy or "").lower().startswith("none"):
        preserve[:] = False
        if invalid.any():
            z_work[invalid] = _fill_nan_nearest_grid(z_work)[invalid]
    X, Y = np.meshgrid(xa, ya)
    final_mask = preserve | ~np.isfinite(z_work)
    notes = [f"Native structured matrix fast path: {z.shape[1]}x{z.shape[0]} (no Delaunay/Qhull rebuild)."]
    if bridge.any():
        notes.append(f"Bridged {int(bridge.sum())} interior failed cell(s); preserved {int(preserve.sum())} failure cell(s).")
    if x_log or y_log:
        axes = "/".join(a for a, on in (("X", x_log), ("Y", y_log)) if on)
        notes.append(f"Native matrix axes retain physical coordinates; Log10 {axes} is applied by the plot axis without spatial regridding.")
    return SurfaceGrid(X=X, Y=Y, Z=np.ma.array(z_work, mask=final_mask, copy=False),
                       method="Native structured matrix", structured=True,
                       invalid_mask=preserve, outside_hull_mask=np.zeros_like(final_mask),
                       imputed_mask=bridge, notes=notes)


# ---------------------------------------------------------------------------
# Structured estimators
# ---------------------------------------------------------------------------
def _structured_interpolate(method: str, u_axis: np.ndarray, v_axis: np.ndarray, z_grid: np.ndarray,
                            U: np.ndarray, V: np.ndarray, cancel_check=None) -> np.ndarray:
    z_work = _fill_nan_nearest_grid(z_grid)
    q = np.column_stack((V.ravel(), U.ravel()))
    if method == "Nearest Neighbor":
        interp = RegularGridInterpolator((v_axis, u_axis), z_work, method="nearest", bounds_error=False, fill_value=np.nan)
        return interp(q).reshape(U.shape)
    if method == "Bilinear Interpolation":
        interp = RegularGridInterpolator((v_axis, u_axis), z_work, method="linear", bounds_error=False, fill_value=np.nan)
        return interp(q).reshape(U.shape)
    if method == "Monotone PCHIP (Structured)":
        # Shape-preserving tensor interpolation: first interpolate every source
        # row along U, then every target column along V.  This avoids the
        # overshoot common to unconstrained bicubic splines on kinetic sweeps.
        u_target = U[0, :]
        v_target = V[:, 0]
        row_values = np.vstack([PchipInterpolator(u_axis, row, extrapolate=False)(u_target) for row in z_work])
        out = np.empty((len(v_target), len(u_target)), dtype=float)
        for i in range(len(u_target)):
            if cancel_check is not None and i % 32 == 0 and cancel_check():
                raise RuntimeError("Cancelled")
            out[:, i] = PchipInterpolator(v_axis, row_values[:, i], extrapolate=False)(v_target)
        return out

    # Cubic tensor-product splines need at least 4 knots per direction.  For
    # smaller matrices, reduce the order rather than failing the render.
    ky = min(3, max(1, len(v_axis) - 1))
    kx = min(3, max(1, len(u_axis) - 1))
    if method == "Bicubic Interpolation":
        spl = RectBivariateSpline(v_axis, u_axis, z_work, kx=ky, ky=kx, s=0.0)
    else:  # Rectangular B-Spline: slight regularising spline for noisy grids.
        variance = float(np.nanvar(z_work))
        smooth = max(0.0, 1e-6 * z_work.size * variance)
        spl = RectBivariateSpline(v_axis, u_axis, z_work, kx=ky, ky=kx, s=smooth)
    return spl.ev(V.ravel(), U.ravel()).reshape(U.shape)


# ---------------------------------------------------------------------------
# Scattered estimators
# ---------------------------------------------------------------------------
def _delaunay_linear(points: np.ndarray, values: np.ndarray, queries: np.ndarray, tri: Delaunay | None = None) -> np.ndarray:
    interp = LinearNDInterpolator(tri if tri is not None else points, values, fill_value=np.nan, rescale=False)
    return np.asarray(interp(queries), dtype=float)

def _clough_tocher(points: np.ndarray, values: np.ndarray, queries: np.ndarray, tri: Delaunay | None = None) -> np.ndarray:
    # SciPy accepts either point coordinates or an existing Delaunay object.
    interp = CloughTocher2DInterpolator(tri if tri is not None else points, values, fill_value=np.nan, rescale=False)
    return np.asarray(interp(queries), dtype=float)


def _knn_blocks(points: np.ndarray, queries: np.ndarray, k: int, tree, cancel_check):
    """Yield (slice, distances, indices) for the queries, 4096 at a time.

    Both inverse-distance estimators walked the query set in the same blocks,
    for the same two reasons: a k-NN query over a whole 160x160 grid is one
    uninterruptible call, and the distance matrix for it is large. Chunking
    keeps cancellation responsive and the working set small. The weighting is
    what differs between them, so only the weighting is written twice.
    """
    tree = tree or cKDTree(points)
    step = 4096
    for start in range(0, len(queries), step):
        if cancel_check is not None and cancel_check():
            raise RuntimeError("Cancelled")
        block = queries[start:start + step]
        d, idx = tree.query(block, k=k, workers=1)
        if k == 1:
            d, idx = d[:, None], idx[:, None]
        yield slice(start, start + len(block)), d, idx


def _idw(points: np.ndarray, values: np.ndarray, queries: np.ndarray, *, power: float, neighbors: int,
         tree: cKDTree | None = None, cancel_check=None) -> np.ndarray:
    k = max(1, min(int(neighbors), len(points)))
    pred = np.empty(len(queries), dtype=float)
    for where, d, idx in _knn_blocks(points, queries, k, tree, cancel_check):
        exact = d[:, 0] <= 1e-14
        weights = 1.0 / np.maximum(d, 1e-12) ** max(float(power), 0.05)
        out = np.sum(weights * values[idx], axis=1) / np.maximum(np.sum(weights, axis=1), 1e-30)
        out[exact] = values[idx[exact, 0]]
        pred[where] = out
    return pred


def _modified_shepard(points: np.ndarray, values: np.ndarray, queries: np.ndarray, *, neighbors: int,
                      tree: cKDTree | None = None, cancel_check=None) -> np.ndarray:
    k = max(2, min(int(neighbors), len(points)))
    pred = np.empty(len(queries), dtype=float)
    for where, d, idx in _knn_blocks(points, queries, k, tree, cancel_check):
        exact = d[:, 0] <= 1e-14
        # Franke/Nielson compact Shepard weights: ((R-d)/(R*d))^2 for d<R.
        R = np.maximum(d[:, -1:], 1e-12) * 1.0000001
        w = np.where(d < R, ((R - d) / np.maximum(R * d, 1e-12)) ** 2, 0.0)
        denom = np.sum(w, axis=1)
        out = np.sum(w * values[idx], axis=1) / np.maximum(denom, 1e-30)
        out[exact] = values[idx[exact, 0]]
        pred[where] = out
    return pred


def _rbf(points: np.ndarray, values: np.ndarray, queries: np.ndarray, *, kernel: str,
         neighbors: int, smoothing: float = 0.0, tree: cKDTree | None = None) -> np.ndarray:
    k = min(len(points), max(16, int(neighbors) * 2))
    kwargs: dict = {"kernel": kernel, "smoothing": max(float(smoothing), 0.0), "neighbors": k}
    if kernel in {"multiquadric", "gaussian", "inverse_multiquadric", "inverse_quadratic"}:
        # The shape parameter is a LENGTH SCALE: scipy's multiquadric is
        # -sqrt((r/epsilon)^2 + 1) and its gaussian exp(-(r/epsilon)^2).
        #
        # This used to be the median nearest-neighbour distance, which makes
        # each basis function about as wide as the gap between two samples.
        # That is the severely ill-conditioned end of the multiquadric family,
        # and it did not merely lose accuracy - it diverged. Measured against a
        # known analytic surface, worst case over three seeds, three sample
        # counts and both uniform and clustered densities:
        #
        #     median-NN epsilon, no smoothing : RMSE  72 (mq)   407 (gauss)
        #     0.6 x diagonal, smoothing 1e-9  : RMSE 0.007 (mq) 0.029 (gauss)
        #
        # A flat kernel relative to the domain is the well-conditioned regime,
        # and a floor under `smoothing` regularises the solve without visibly
        # smoothing the result. Scaling to the domain rather than to the point
        # spacing also keeps this stable as sample density changes.
        span = np.asarray(points).max(axis=0) - np.asarray(points).min(axis=0)
        diagonal = float(np.sqrt(np.sum(np.square(span))))
        kwargs["epsilon"] = max(diagonal * 0.6, 1e-4)
        kwargs["smoothing"] = max(float(smoothing), 1e-9)
    interp = RBFInterpolator(points, values, **kwargs)
    return np.asarray(interp(queries), dtype=float).ravel()


def _variogram(h: np.ndarray, sill: float, range_: float, nugget: float, model: str = "Exponential") -> np.ndarray:
    """Standard isotropic semivariogram families in normalized metric space."""
    h = np.asarray(h, dtype=float)
    r = np.maximum(h / max(float(range_), 1e-8), 0.0)
    mode = str(model or "Exponential").strip().lower()
    if mode.startswith("spher"):
        core = np.where(r < 1.0, 1.5 * r - 0.5 * r ** 3, 1.0)
    elif mode.startswith("gauss"):
        core = 1.0 - np.exp(-3.0 * r ** 2)
    else:
        # Exponential semivariogram reaches ~95% of sill at h=range_.
        core = 1.0 - np.exp(-3.0 * r)
    return nugget + sill * core


def _ordinary_kriging(points: np.ndarray, values: np.ndarray, queries: np.ndarray, *, neighbors: int, variogram: str = "Exponential", cancel_check=None, tree: cKDTree | None = None) -> np.ndarray:
    tree = tree or cKDTree(points)
    k = min(len(points), max(3, min(int(neighbors), len(points))))
    d_query, idx_query = tree.query(queries, k=k, workers=1)
    if k == 1:
        d_query, idx_query = d_query[:, None], idx_query[:, None]
    # Robust global variogram parameters from a capped subset; local solves use
    # these same parameters and therefore retain an ordinary-kriging mean.
    sample = points
    if len(sample) > 700:
        take = np.linspace(0, len(sample) - 1, 700).astype(int)
        sample = sample[take]
    pdist = distance.pdist(sample)
    pdist = pdist[np.isfinite(pdist) & (pdist > 0)]
    range_ = float(np.nanquantile(pdist, 0.65)) if pdist.size else 0.35
    sill = max(float(np.nanvar(values)), 1e-12)
    nugget = max(sill * 1e-10, 1e-14)
    out = np.empty(len(queries), dtype=float)
    for row in range(len(queries)):
        if cancel_check is not None and row % 32 == 0 and cancel_check():
            raise RuntimeError("Cancelled")
        ids = np.atleast_1d(idx_query[row]).astype(int)
        ds = np.atleast_1d(d_query[row]).astype(float)
        if ds[0] <= 1e-14:
            out[row] = values[ids[0]]
            continue
        p = points[ids]
        G = _variogram(distance.cdist(p, p), sill, range_, nugget, variogram)
        # Exact diagonal semivariance is zero; nugget belongs to off-diagonal
        # model uncertainty, not self-distance.
        np.fill_diagonal(G, 0.0)
        A = np.empty((len(ids) + 1, len(ids) + 1), dtype=float)
        A[:-1, :-1] = G
        A[:-1, -1] = 1.0
        A[-1, :-1] = 1.0
        A[-1, -1] = 0.0
        b = np.r_[_variogram(ds, sill, range_, nugget, variogram), 1.0]
        try:
            sol = np.linalg.solve(A, b)
        except np.linalg.LinAlgError:
            sol = np.linalg.lstsq(A, b, rcond=None)[0]
        out[row] = float(np.dot(sol[:-1], values[ids]))
    return out


def _local_polynomial(points: np.ndarray, values: np.ndarray, queries: np.ndarray, *, neighbors: int,
                      mode: str, loess_fraction: float, cancel_check=None, tree: cKDTree | None = None) -> np.ndarray:
    n = len(points)
    if mode == "loess":
        k = int(math.ceil(np.clip(float(loess_fraction), 0.02, 1.0) * n))
        k = min(n, max(6, min(k, max(int(neighbors) * 3, 18), n)))
    else:
        k = min(n, max(6, min(int(neighbors), n)))
    tree = tree or cKDTree(points)
    d_all, idx_all = tree.query(queries, k=k, workers=1)
    if k == 1:
        d_all, idx_all = d_all[:, None], idx_all[:, None]
    out = np.empty(len(queries), dtype=float)
    for row, q in enumerate(queries):
        if cancel_check is not None and row % 32 == 0 and cancel_check():
            raise RuntimeError("Cancelled")
        d = np.atleast_1d(d_all[row]).astype(float)
        ids = np.atleast_1d(idx_all[row]).astype(int)
        if d[0] <= 1e-14:
            out[row] = values[ids[0]]
            continue
        dx = points[ids, 0] - q[0]
        dy = points[ids, 1] - q[1]
        if mode == "loess":
            h = max(float(d[-1]), 1e-12)
            r = np.clip(d / h, 0.0, 1.0)
            w = (1.0 - r ** 3) ** 3
        else:
            h = max(float(np.nanmedian(d[d > 0])) if np.any(d > 0) else float(d[-1]), 1e-12)
            w = np.exp(-((d / h) ** 2))
        # Local quadratic fit; intercept is the estimate exactly at q.
        A = np.column_stack((np.ones_like(dx), dx, dy, dx * dx, dx * dy, dy * dy))
        sw = np.sqrt(np.maximum(w, 1e-12))
        Aw = A * sw[:, None]
        zw = values[ids] * sw
        try:
            beta = np.linalg.lstsq(Aw, zw, rcond=None)[0]
            out[row] = beta[0]
        except np.linalg.LinAlgError:
            out[row] = float(np.average(values[ids], weights=np.maximum(w, 1e-12)))
    return out


# ---------------------------------------------------------------------------
# Local Sibson natural-neighbour implementation
# ---------------------------------------------------------------------------
def _polygon_area(poly: np.ndarray) -> float:
    if poly is None or len(poly) < 3:
        return 0.0
    x, y = poly[:, 0], poly[:, 1]
    return abs(float(np.dot(x, np.roll(y, -1)) - np.dot(y, np.roll(x, -1)))) * 0.5


def _clip_halfplane(poly: np.ndarray, normal: np.ndarray, bound: float, *, eps: float = 1e-12) -> np.ndarray:
    """Clip a convex polygon by ``normal dot x <= bound``.

    Sibson coordinates can be constructed entirely from Voronoi half-planes.
    Doing the clipping directly is substantially faster than rebuilding a
    Qhull Voronoi diagram for every output pixel.
    """
    poly = np.asarray(poly, dtype=float)
    if len(poly) == 0:
        return poly.reshape(0, 2)
    out: list[np.ndarray] = []
    prev = poly[-1]
    prev_f = float(np.dot(normal, prev) - bound)
    prev_inside = prev_f <= eps
    for cur in poly:
        cur_f = float(np.dot(normal, cur) - bound)
        cur_inside = cur_f <= eps
        if cur_inside != prev_inside:
            den = prev_f - cur_f
            if abs(den) > 1e-15:
                t = prev_f / den
                out.append(prev + t * (cur - prev))
        if cur_inside:
            out.append(cur)
        prev, prev_f, prev_inside = cur, cur_f, cur_inside
    return np.asarray(out, dtype=float).reshape(-1, 2) if out else np.empty((0, 2), dtype=float)


def _voronoi_cell(site: np.ndarray, competitors: np.ndarray, seed: np.ndarray) -> np.ndarray:
    """Return a Voronoi cell clipped to ``seed`` using distance half-planes.

    For a site p and competitor r, points x belonging to p satisfy
    ``2(r-p) dot x <= |r|^2-|p|^2``.  Starting from the query cell rather than
    an unbounded global box gives exactly the stolen-area polygon required by
    Sibson interpolation and avoids constructing irrelevant Voronoi regions.
    """
    poly = np.asarray(seed, dtype=float)
    p2 = float(np.dot(site, site))
    for other in competitors:
        if np.allclose(other, site, rtol=0.0, atol=1e-14):
            continue
        normal = 2.0 * (other - site)
        bound = float(np.dot(other, other) - p2)
        poly = _clip_halfplane(poly, normal, bound)
        if len(poly) < 3:
            break
    return poly


def _query_voronoi_cell(q: np.ndarray, local: np.ndarray) -> np.ndarray:
    # Coordinates are normalised to approximately [0,1].  The query cell for
    # a point inside the source hull is bounded; this generous box merely seeds
    # half-plane clipping and never changes the finite cell.
    span = 4.0
    poly = np.array([[-span, -span], [1.0 + span, -span],
                     [1.0 + span, 1.0 + span], [-span, 1.0 + span]], dtype=float)
    q2 = float(np.dot(q, q))
    for site in local:
        normal = 2.0 * (site - q)
        bound = float(np.dot(site, site) - q2)
        poly = _clip_halfplane(poly, normal, bound)
        if len(poly) < 3:
            break
    return poly


def _natural_neighbor_sibson(points: np.ndarray, values: np.ndarray, queries: np.ndarray, *, neighbors: int, cancel_check=None, tree: cKDTree | None = None, tri: Delaunay | None = None) -> np.ndarray:
    """Sibson natural-neighbour interpolation using stolen Voronoi areas.

    The source Delaunay hull is built once.  For each query inside the hull we
    select a local nearest-neighbour set, form the inserted query Voronoi cell
    by direct half-plane clipping, then measure how much of that cell overlaps
    each original local Voronoi cell.  Those stolen areas are the Sibson
    barycentric weights. The candidate set is bounded to the nearest source
    points because a 2-D natural-neighbour stencil is local; within that
    candidate set the Sibson stolen-area weights are computed exactly.
    """
    n = len(points)
    if n < 3:
        return _idw(points, values, queries, power=2.0, neighbors=max(1, n))
    # Natural neighbours are local by definition; a 24-point candidate cap
    # comfortably exceeds the typical 2-D Voronoi stencil while avoiding
    # O(N^2) polygon clipping at every output pixel.
    k = min(n, max(6, min(int(neighbors), 24, n)))
    tree = tree or cKDTree(points)
    if k == n:
        idx_all = np.tile(np.arange(n, dtype=int), (len(queries), 1))
        d_all = distance.cdist(queries, points)
        order = np.argsort(d_all, axis=1)
        idx_all = np.take_along_axis(idx_all, order, axis=1)
        d_all = np.take_along_axis(d_all, order, axis=1)
    else:
        d_all, idx_all = tree.query(queries, k=k, workers=1)
        if k == 1:
            d_all, idx_all = d_all[:, None], idx_all[:, None]

    try:
        hull = tri if tri is not None else Delaunay(points, qhull_options="Qbb Qc Qz Q12")
        inside_global = hull.find_simplex(queries) >= 0
    except QhullError:
        inside_global = np.ones(len(queries), dtype=bool)
    fallback = CloughTocher2DInterpolator(points, values, fill_value=np.nan, rescale=False)
    out = np.full(len(queries), np.nan, dtype=float)

    for row, q in enumerate(queries):
        if cancel_check is not None and row % 32 == 0 and cancel_check():
            raise RuntimeError("Cancelled")
        d = np.atleast_1d(d_all[row]).astype(float)
        ids = np.atleast_1d(idx_all[row]).astype(int)
        if d.size and d[0] <= 1e-14:
            out[row] = values[ids[0]]
            continue
        if not inside_global[row]:
            continue
        # Deduplicate the local set while keeping stable nearest-first order.
        seen: set[tuple[float, float]] = set()
        keep: list[int] = []
        for ident in ids:
            key = tuple(np.round(points[ident], 14))
            if key not in seen:
                seen.add(key); keep.append(int(ident))
        local = points[keep]
        local_vals = values[keep]
        if len(local) < 3:
            out[row] = _idw(local, local_vals, q[None, :], power=2.0, neighbors=len(local))[0]
            continue

        q_cell = _query_voronoi_cell(q, local)
        q_area = _polygon_area(q_cell)
        if q_area <= 1e-15:
            val = fallback(q[None, :])[0]
            out[row] = float(val) if np.isfinite(val) else _idw(local, local_vals, q[None, :], power=2.0, neighbors=len(local))[0]
            continue

        weights = np.zeros(len(local), dtype=float)
        # V_i(old) intersect V_q(new) is the area stolen from source i.
        for i, site in enumerate(local):
            stolen = _voronoi_cell(site, local, q_cell)
            weights[i] = _polygon_area(stolen)
        total = float(np.sum(weights))
        if not np.isfinite(total) or total <= 1e-15:
            val = fallback(q[None, :])[0]
            out[row] = float(val) if np.isfinite(val) else _idw(local, local_vals, q[None, :], power=2.0, neighbors=len(local))[0]
        else:
            out[row] = float(np.dot(weights / total, local_vals))
    return out




IMPUTATION_MODES = ("Nearest Neighbor Fill", "Local Mean Imputation",
                    "Baseline Clamp (colour-scale minimum)", "Symmetric Mirror Fill")


def _local_mean_fill(z: np.ndarray, target: np.ndarray, max_iters: int = 64) -> np.ndarray:
    """Iteratively replace ``target`` cells with the mean of valid neighbours."""
    from scipy.ndimage import uniform_filter
    out = np.asarray(z, dtype=float).copy()
    pending = np.asarray(target, bool) | ~np.isfinite(out)
    for _ in range(max_iters):
        if not pending.any():
            break
        valid = np.isfinite(out) & ~pending
        if not valid.any():
            break
        num = uniform_filter(np.where(valid, out, 0.0), size=3, mode="nearest")
        den = uniform_filter(valid.astype(float), size=3, mode="nearest")
        ready = pending & (den > 1e-9)
        if not ready.any():
            break
        out[ready] = num[ready] / den[ready]
        pending &= ~ready
    if pending.any():   # isolated interior holes with no reachable neighbours
        out[pending] = _fill_nan_nearest_grid(np.where(pending, np.nan, out))[pending]
    return out


def _mirror_fill(z: np.ndarray, target: np.ndarray) -> np.ndarray:
    """Reflect valid neighbouring values across each gap (rows then columns),
    averaging both directions to keep the topography continuous."""
    base = np.where(np.asarray(target, bool), np.nan, np.asarray(z, dtype=float))

    def _fill_axis(arr: np.ndarray) -> np.ndarray:
        out = arr.copy()
        for i in range(out.shape[0]):
            row = out[i]
            bad = ~np.isfinite(row)
            if not bad.any() or bad.all():
                continue
            idx = np.flatnonzero(bad)
            splits = np.split(idx, np.flatnonzero(np.diff(idx) > 1) + 1)
            for run in splits:
                a, b = run[0], run[-1]
                left = row[max(0, a - (b - a + 1)):a][::-1]      # mirrored from the left edge
                right = row[b + 1:b + 2 + (b - a)]               # mirrored from the right edge
                vals = np.full(run.size, np.nan)
                if left.size:
                    vals[:left.size] = left[:run.size]
                if right.size:
                    rv = right[::-1]
                    tailed = rv[:run.size][::-1]
                    merge = np.where(np.isfinite(vals[-tailed.size:]),
                                     0.5 * (vals[-tailed.size:] + tailed), tailed)
                    vals[-tailed.size:] = merge
                row[run] = vals
        return out

    by_rows = _fill_axis(base)
    by_cols = _fill_axis(base.T).T
    stacked = np.nanmean(np.stack([by_rows, by_cols]), axis=0)
    remaining = ~np.isfinite(stacked)
    if remaining.any():
        stacked[remaining] = _fill_nan_nearest_grid(np.where(remaining, np.nan, stacked))[remaining]
    return stacked


def impute_surface_invalid(surface: SurfaceGrid, mode: str) -> SurfaceGrid:
    """Fill failed/invalid response cells using the selected strategy.

    Only genuinely failed simulation cells are imputed — cells masked because
    they lie outside the convex hull remain masked, so extrapolation policy is
    unchanged and no boundary clipping artefacts appear.  Imputed cells are
    recorded in ``imputed_mask`` for the provenance overlay.
    """
    mode = str(mode or "")
    if mode not in IMPUTATION_MODES:
        return surface
    mask = np.ma.getmaskarray(surface.Z)
    outside = np.asarray(surface.outside_hull_mask, bool) if surface.outside_hull_mask is not None else np.zeros_like(mask)
    target = mask & ~outside
    if not target.any():
        return surface
    raw = np.ma.filled(np.ma.asarray(surface.Z, dtype=float), np.nan)
    valid_vals = raw[np.isfinite(raw) & ~mask]
    if valid_vals.size == 0:
        return surface
    if mode == "Nearest Neighbor Fill":
        filled = _fill_nan_nearest_grid(np.where(target, np.nan, raw))
    elif mode == "Local Mean Imputation":
        filled = _local_mean_fill(raw, target)
    elif mode == "Symmetric Mirror Fill":
        filled = _mirror_fill(raw, target)
    else:   # Baseline Clamp
        filled = raw.copy()
        filled[target] = float(np.nanmin(valid_vals))
    out = raw.copy()
    out[target] = filled[target]
    new_mask = (mask & ~target) | ~np.isfinite(out)
    imputed = np.asarray(surface.imputed_mask, bool).copy() if surface.imputed_mask is not None else np.zeros_like(mask)
    imputed |= target & ~new_mask
    return SurfaceGrid(
        X=surface.X, Y=surface.Y, Z=np.ma.array(out, mask=new_mask, copy=False),
        method=surface.method, structured=surface.structured,
        invalid_mask=surface.invalid_mask, outside_hull_mask=surface.outside_hull_mask,
        imputed_mask=imputed,
        notes=list(surface.notes) + [f"{mode}: {int(target.sum())} failed cell(s) imputed; hull mask preserved."],
    )


def smooth_surface_grid(surface: SurfaceGrid, sigma: float) -> SurfaceGrid:
    """Apply mask-aware Gaussian smoothing to an existing estimated grid.

    This deliberately operates *after* the expensive estimator so GraphVis can
    cache triangulation/RBF/kriging results and update only the inexpensive
    display smoothing while a user drags the sigma slider.  Existing failure
    and hull masks are immutable.
    """
    sigma = max(float(sigma), 0.0)
    if sigma <= 0.0:
        return surface
    mask = np.ma.getmaskarray(surface.Z).copy()
    raw = np.ma.filled(np.ma.asarray(surface.Z, dtype=float), np.nan)
    smoothed = _weighted_gaussian(raw, sigma, mask)
    z = np.ma.array(smoothed, mask=mask | ~np.isfinite(smoothed), copy=False)
    return SurfaceGrid(
        X=surface.X, Y=surface.Y, Z=z, method=surface.method, structured=surface.structured,
        invalid_mask=surface.invalid_mask, outside_hull_mask=surface.outside_hull_mask,
        imputed_mask=surface.imputed_mask,
        notes=list(surface.notes) + [f"Mask-aware Gaussian post-smoothing applied (sigma={sigma:.3g})."],
    )


# ---------------------------------------------------------------------------
# Public router
# ---------------------------------------------------------------------------
def interpolate_surface(x: Iterable[float], y: Iterable[float], z: Iterable[float], *,
                        resolution: int = 160, estimator: str = "Auto (data-aware)",
                        x_log: bool = False, y_log: bool = False, smoothing: float = 0.0,
                        neighbors: int = 32, idw_power: float = 2.0, loess_fraction: float = 0.25,
                        extrapolation: str = "Mask outside convex hull", invalid_mask_policy: str = "Sample cell only",
                        duplicate_statistic: str = "mean",
                        kriging_variogram: str = "Exponential", value_policy: str = "Allow estimator overshoot",
                        failure_bridge_policy: str = "Bridge isolated failures", failure_bridge_max_cells: int = 4,
                        response_space: str = "Linear values", geometry_key: object | None = None,
                        x_metric_weight: float = 1.0, y_metric_weight: float = 1.0,
                        progressive: bool = False, cancel_check=None) -> SurfaceGrid:
    """Route a surface request to the selected scientific estimator.

    X/Y can contain failed samples as long as their coordinates remain finite;
    non-finite Z values are retained as a failure mask rather than silently
    filled into the fit.  Non-positive X/Y values are likewise excluded when
    their corresponding axis is logarithmic.
    """
    if cancel_check is not None and cancel_check():
        raise RuntimeError("Cancelled")
    x = np.asarray(x, dtype=float).ravel()
    y = np.asarray(y, dtype=float).ravel()
    z = np.asarray(z, dtype=float).ravel()
    if not (len(x) == len(y) == len(z)):
        raise ValueError("X, Y and Z must have the same number of samples.")
    resolution = max(12, int(resolution))
    if progressive:
        resolution = min(resolution, 48)
        neighbors = min(int(neighbors), 20)

    spatial = _spatial_valid(x, y, bool(x_log), bool(y_log))
    response_log = str(response_space or "Linear values").lower().startswith("log")
    response_valid = np.isfinite(z)
    if response_log:
        response_valid &= z > 0
    valid = spatial & response_valid
    invalid = spatial & ~response_valid
    z_fit_all = np.where(valid, np.log10(z) if response_log else z, np.nan)
    if valid.sum() < 3:
        raise ValueError("Surface interpolation needs at least three finite X/Y/Z samples.")

    # The output domain follows every finite spatial sample, including
    # coordinates whose ODE response failed. Otherwise an all-failed extreme
    # voltage/flow boundary would be silently cropped away instead of appearing
    # as a transparent/fallback failure region.
    transform = CoordinateTransform.fit(x[spatial], y[spatial], bool(x_log), bool(y_log))
    xw = max(float(x_metric_weight), 1e-6)
    yw = max(float(y_metric_weight), 1e-6)
    u_valid, v_valid = transform.transform(x[valid], y[valid])
    valid_points, valid_values = _aggregate_duplicates(np.column_stack((u_valid * xw, v_valid * yw)), z_fit_all[valid], duplicate_statistic)
    if len(valid_points) < 3:
        raise ValueError("Surface interpolation needs at least three distinct coordinate pairs.")
    centred = valid_points - np.nanmean(valid_points, axis=0, keepdims=True)
    if np.linalg.matrix_rank(centred, tol=1e-12) < 2:
        raise ValueError("Surface interpolation needs X/Y coordinates that span a two-dimensional domain; the valid samples are collinear.")

    # Include failed-Z coordinates in structured-grid detection so a single ODE
    # failure does not make an otherwise complete N×M sweep look scattered.
    u_spatial, v_spatial = transform.transform(x[spatial], y[spatial])
    structured = _detect_structured(u_spatial * xw, v_spatial * yw, z_fit_all[spatial])

    requested = canonical_estimator(estimator)
    notes: list[str] = []
    # Auto on a complete Cartesian sweep should not spend seconds rebuilding
    # a surface that already exists.  Preserve the native matrix topology and
    # let Matplotlib/GPU shading provide continuous presentation.
    if requested == "Auto (data-aware)" and structured is not None:
        ux, vy, z_grid = structured
        physical_x = transform.inverse_x(ux / xw)
        physical_y = transform.inverse_y(vy / yw)
        native = structured_surface_native(physical_x, physical_y, z_grid,
                                           x_log=x_log, y_log=y_log, invalid_mask_policy=invalid_mask_policy,
                                           failure_bridge_policy=failure_bridge_policy,
                                           failure_bridge_max_cells=failure_bridge_max_cells, progressive=progressive)
        native.method = "Auto → Native structured matrix"
        if response_log:
            raw = np.ma.filled(native.Z, np.nan)
            raw = np.power(10.0, raw)
            native.Z = np.ma.array(raw, mask=np.ma.getmaskarray(native.Z), copy=False)
            native.notes.append("Response values interpolated in Log10 space and transformed back to physical units.")
        if float(smoothing) > 0:
            native = smooth_surface_grid(native, float(smoothing))
        return native

    u = np.linspace(0.0, 1.0, resolution)
    v = np.linspace(0.0, 1.0, resolution)
    U0, V0 = np.meshgrid(u, v)
    U, V = U0 * xw, V0 * yw
    X, Y = np.meshgrid(transform.inverse_x(u), transform.inverse_y(v))
    queries = np.column_stack((U.ravel(), V.ravel()))

    invalid_points = np.empty((0, 2), dtype=float)
    if invalid.any():
        ui, vi = transform.transform(x[invalid], y[invalid])
        inside = np.isfinite(ui) & np.isfinite(vi) & (ui >= -1e-9) & (ui <= 1 + 1e-9) & (vi >= -1e-9) & (vi <= 1 + 1e-9)
        invalid_points = np.column_stack((ui[inside] * xw, vi[inside] * yw))

    method = requested
    if requested == "Auto (data-aware)":
        if structured is not None:
            ux, vy, _ = structured
            method = "Bicubic Interpolation" if len(ux) >= 4 and len(vy) >= 4 else "Bilinear Interpolation"
        elif len(valid_points) >= 40000:
            method = "Inverse Distance Weighting (IDW)"
        else:
            method = "Delaunay Triangulation (Linear)"
        notes.append(f"Auto selected {method}.")

    structured_methods = {"Nearest Neighbor", "Bilinear Interpolation", "Bicubic Interpolation", "Rectangular B-Spline", "Monotone PCHIP (Structured)"}
    if method in structured_methods and structured is None:
        notes.append(f"{method} requires a complete Cartesian X×Y sweep; using Delaunay Triangulation (Linear) for this scattered dataset.")
        method = "Delaunay Triangulation (Linear)"

    plan = None if method in structured_methods else geometry_plan(valid_points, geometry_key)
    tree = plan.tree if plan is not None else cKDTree(valid_points)
    tri = plan.triangulation if plan is not None else None

    try:
        if method in structured_methods:
            ux, vy, z_grid = structured  # type: ignore[misc]
            Zi = _structured_interpolate(method, ux, vy, z_grid, U, V, cancel_check=cancel_check)
        elif method == "Delaunay Triangulation (Linear)":
            Zi = _delaunay_linear(valid_points, valid_values, queries, tri=tri).reshape(U.shape)
        elif method == "Clough–Tocher C1 Smooth":
            Zi = _clough_tocher(valid_points, valid_values, queries, tri=tri).reshape(U.shape)
        elif method == "Natural Neighbor (Sibson’s)":
            # Exact/local Sibson construction is substantially more expensive
            # than Delaunay/IDW because every target needs a Voronoi stolen-area
            # solve.  Evaluate it on a controlled metric-space grid and, only
            # when the requested display grid is denser, bicubically resample
            # that Sibson field.  This keeps interactive frame times bounded
            # without changing the selected estimator or the physical axes.
            native_cap = 16 if progressive else 30
            native_res = min(int(resolution), native_cap)
            if native_res < int(resolution):
                un = np.linspace(0.0, xw, native_res)
                vn = np.linspace(0.0, yw, native_res)
                Un, Vn = np.meshgrid(un, vn)
                qn = np.column_stack((Un.ravel(), Vn.ravel()))
                Zn = _natural_neighbor_sibson(valid_points, valid_values, qn, neighbors=neighbors, cancel_check=cancel_check, tree=tree, tri=tri).reshape(Un.shape)
                # The final convex-hull and failed-simulation masks are applied
                # at full resolution below, so nearest filling here is only a
                # numerical aid for the tensor spline and never exposes invalid
                # cells in the rendered result.
                Zn_work = _fill_nan_nearest_grid(Zn)
                ky = min(3, max(1, native_res - 1)); kx = ky
                spl = RectBivariateSpline(vn, un, Zn_work, kx=ky, ky=kx, s=0.0)
                Zi = spl.ev(V.ravel(), U.ravel()).reshape(U.shape)
                notes.append(f"Sibson weights evaluated on a {native_res}x{native_res} native grid and bicubically refined to {resolution}x{resolution} for responsive rendering.")
            else:
                Zi = _natural_neighbor_sibson(valid_points, valid_values, queries, neighbors=neighbors, cancel_check=cancel_check, tree=tree, tri=tri).reshape(U.shape)
        elif method == "Inverse Distance Weighting (IDW)":
            Zi = _idw(valid_points, valid_values, queries, power=idw_power, neighbors=neighbors, tree=tree, cancel_check=cancel_check).reshape(U.shape)
        elif method == "Modified Shepard's Method":
            Zi = _modified_shepard(valid_points, valid_values, queries, neighbors=neighbors, tree=tree, cancel_check=cancel_check).reshape(U.shape)
        elif method == "Thin Plate Spline (TPS)":
            Zi = _rbf(valid_points, valid_values, queries, kernel="thin_plate_spline", neighbors=neighbors, tree=tree).reshape(U.shape)
        elif method == "Multiquadric RBF":
            Zi = _rbf(valid_points, valid_values, queries, kernel="multiquadric", neighbors=neighbors, tree=tree).reshape(U.shape)
        elif method == "Gaussian RBF":
            Zi = _rbf(valid_points, valid_values, queries, kernel="gaussian", neighbors=neighbors, tree=tree).reshape(U.shape)
        elif method == "Ordinary Kriging":
            Zi = _ordinary_kriging(valid_points, valid_values, queries, neighbors=neighbors, variogram=kriging_variogram, cancel_check=cancel_check, tree=tree).reshape(U.shape)
            notes.append(f"Ordinary kriging variogram: {kriging_variogram}.")
        elif method == "Moving Least Squares (MLS)":
            Zi = _local_polynomial(valid_points, valid_values, queries, neighbors=neighbors, mode="mls", loess_fraction=loess_fraction, cancel_check=cancel_check, tree=tree).reshape(U.shape)
        elif method == "LOESS / LOWESS":
            Zi = _local_polynomial(valid_points, valid_values, queries, neighbors=neighbors, mode="loess", loess_fraction=loess_fraction, cancel_check=cancel_check, tree=tree).reshape(U.shape)
        else:
            Zi = _delaunay_linear(valid_points, valid_values, queries, tri=tri).reshape(U.shape)
            method = "Delaunay Triangulation (Linear)"
    except Exception as exc:
        if str(exc) == "Cancelled":
            raise
        # A scientific plotting application should still produce a diagnostic
        # figure when a specialised solver is ill-conditioned.  Linear
        # Delaunay is the conservative fallback and leaves extrapolated cells
        # masked rather than inventing values.
        notes.append(f"{method} failed ({exc}); fell back to Delaunay Triangulation (Linear).")
        if plan is None:
            plan = geometry_plan(valid_points, geometry_key)
            tree, tri = plan.tree, plan.triangulation
        Zi = _delaunay_linear(valid_points, valid_values, queries, tri=tri).reshape(U.shape)
        method = "Delaunay Triangulation (Linear)"

    if cancel_check is not None and cancel_check():
        raise RuntimeError("Cancelled")
    if str(value_policy or "").lower().startswith("clamp"):
        lo_src = float(np.nanmin(valid_values)); hi_src = float(np.nanmax(valid_values))
        Zi = np.clip(Zi, lo_src, hi_src)
        notes.append(f"Estimator overshoot clamped to observed response range [{lo_src:.6g}, {hi_src:.6g}].")

    if structured is not None and method in structured_methods:
        outside = np.zeros(U.shape, dtype=bool)
    else:
        outside = _outside_hull(valid_points, queries, tri=tri).reshape(U.shape)
    invalid_mask = _invalid_region_mask(valid_points, invalid_points, queries, resolution, invalid_mask_policy, valid_tree=tree).reshape(U.shape)
    extrap = str(extrapolation or "Mask outside convex hull")
    extrap_low = extrap.strip().lower()
    fill_mask = ~np.isfinite(Zi) | outside
    if extrap_low.startswith("nearest") or extrap_low.startswith("edge-clamped"):
        if fill_mask.any():
            nearest = NearestNDInterpolator(valid_points, valid_values)(queries[fill_mask.ravel()])
            flat = Zi.ravel(); flat[fill_mask.ravel()] = nearest; Zi = flat.reshape(U.shape)
        outside_mask = np.zeros_like(outside)
        notes.append("Extended the estimated field across the full rectangular domain using nearest/edge-clamped values.")
    elif extrap_low.startswith("idw") or extrap_low.startswith("smooth idw"):
        if fill_mask.any():
            fill_values = _idw(valid_points, valid_values, queries[fill_mask.ravel()],
                               power=max(float(idw_power), 0.25), neighbors=max(4, min(int(neighbors), 64)), tree=tree)
            flat = Zi.ravel(); flat[fill_mask.ravel()] = fill_values; Zi = flat.reshape(U.shape)
        outside_mask = np.zeros_like(outside)
        notes.append("Extended the estimated field across the full rectangular domain with IDW in the normalized spatial metric.")
    elif extrap_low.startswith("linear + nearest") or extrap_low.startswith("full rectangular"):
        if fill_mask.any():
            nearest = NearestNDInterpolator(valid_points, valid_values)(queries[fill_mask.ravel()])
            flat = Zi.ravel(); flat[fill_mask.ravel()] = nearest; Zi = flat.reshape(U.shape)
        outside_mask = np.zeros_like(outside)
        notes.append("Filled only unsupported edge cells after the primary interpolator to produce a complete rectangular surface.")
    else:
        outside_mask = outside

    # Zi already excludes failed source points from fitting, so clearing a
    # small enclosed failure mask reveals a scientifically explicit local
    # interpolation rather than leaving permanent white holes. Boundary or
    # large connected failure regions remain masked.
    imputed_mask, invalid_mask = _classify_bridgeable_mask(invalid_mask, failure_bridge_policy, failure_bridge_max_cells)
    if imputed_mask.any():
        notes.append(f"Bridged {int(imputed_mask.sum())} small enclosed failure-grid cell(s) using the selected estimator; larger/boundary failures remain masked.")

    if response_log:
        Zi = np.power(10.0, Zi)
        notes.append("Response values interpolated in Log10 space and transformed back to physical units.")

    protected = invalid_mask | outside_mask | ~np.isfinite(Zi)
    if float(smoothing) > 0:
        Zi = _weighted_gaussian(Zi, float(smoothing), protected)
    final_mask = protected | ~np.isfinite(Zi)
    Zm = np.ma.array(Zi, mask=final_mask, copy=False)

    if x_log or y_log:
        axes = "/".join(a for a, on in (("X", x_log), ("Y", y_log)) if on)
        notes.append(f"Interpolation distances computed in normalized Log10 {axes} coordinate space.")
    if abs(xw - 1.0) > 1e-12 or abs(yw - 1.0) > 1e-12:
        notes.append(f"Interpolation metric anisotropy: X weight={xw:.4g}, Y weight={yw:.4g}.")
    if invalid_points.size:
        notes.append(f"Masked {int(invalid.sum())} failed/non-finite response coordinate(s) using '{invalid_mask_policy}'.")
    return SurfaceGrid(X=X, Y=Y, Z=Zm, method=method, structured=structured is not None,
                       invalid_mask=invalid_mask, outside_hull_mask=outside_mask, imputed_mask=imputed_mask, notes=notes)
