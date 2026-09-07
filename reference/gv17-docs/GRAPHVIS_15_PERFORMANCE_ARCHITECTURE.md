# GraphVis 15 — Surface Performance Architecture

Release: **2026.08.29.13**

GraphVis 15 restructures the high-density scientific-field pipeline around the fact that a pre-gridded kinetic sweep is already a surface. The main target is mixed linear/log parameter sweeps such as Applied Voltage x Flow Rate -> MEC VHPR.

## Performance architecture

- Native N x M matrices use a direct `structured_surface_native()` path. GraphVis no longer flattens an already-gridded matrix, rediscovers its Cartesian topology, constructs a Delaunay hull and interpolates it back onto another grid when Auto is selected.
- Flat XYZ tables use vectorised Cartesian-grid reconstruction based on integer cell IDs and `numpy.bincount`, replacing nested per-row/per-column boolean scans.
- Scattered X/Y geometry is cached separately from response values. One content-safe `GeometryPlan` stores a KD-tree and Delaunay triangulation and can be reused by several Z responses and render resolutions.
- Surface cache is now both entry-limited and byte-limited (384 MiB default) rather than retaining an arbitrary count of potentially huge matrices.
- Dataset cache identity contains an in-memory revision counter, preventing derived-column edits from reusing stale surfaces.
- Surface numerical precomputation runs on a dedicated Qt compute pool before the single Matplotlib composition worker. Stale compute tasks are cancelled cooperatively where the estimator permits it.
- Matplotlib-only style changes for colormap, grid visibility and primary surface alpha update existing artists without rebuilding the scientific surface.
- Existing progressive rendering remains topology-preserving: native matrices are stride-decimated to approximately 64 cells per axis during active surface-slider interaction and return to full fidelity on release.
- HDF5 / MATLAB v7.3 2-D and higher arrays are represented by `HDFArrayProxy` objects and remain on disk until selected. Small 1-D coordinate vectors remain eager so mapping controls still populate quickly.
- Optional modules including the raster digitizer, peak-analysis helpers, predictive model code, GPU preview backend and scripting consoles are imported on demand rather than at main-window module import time.

## Scientific interpolation and failure handling

- Normalized Log10 X/Y coordinates remain the native distance metric whenever an axis is logarithmic.
- Optional X/Y metric weights add controlled scientific anisotropy after normalization without changing the physical plot coordinates.
- `Clough-Tocher C1 Smooth` and `Monotone PCHIP (Structured)` are available in addition to the existing estimator family.
- Response interpolation can operate in physical values or Log10 response space.
- Solver failure handling distinguishes isolated/enclosed dropouts from boundary-connected failure/washout regions:
  - Preserve all failures
  - Bridge isolated failures
  - Bridge small enclosed holes
  - Bridge all interior holes
- Boundary-connected failed regions are preserved by the topology-aware bridge classifier.
- `Mark bridged cells` optionally outlines inferred cells so imputation provenance remains visible.

## MATLAB / Origin-style field tools

- Independent contour-line overlay can be placed on top of a continuous heatmap, with configurable level count, line colour, width and labels.
- The cross-section tool reads the already-rendered `SurfaceGrid`; clicking a heatmap creates orthogonal response-vs-X and response-vs-Y profiles without re-running interpolation.
- Gradient/quiver derivatives use physical/log coordinate spacing instead of array-index spacing.
- Existing SVG/PDF/EPS hybrid/full-vector export remains available. Hybrid export keeps text/ticks/contours vector while rasterising only a dense field when requested.
- The Performance Inspector reports surface-precompute time, surface preparation, Matplotlib field time, cache memory and masked/imputed-cell diagnostics.

## Synthetic 250 x 250 benchmark

On the build environment, a mixed linear/log 250 x 250 sweep produced approximately:

- Native structured surface preparation: ~0.003 s
- Flat-table Auto reconstruction + native surface: ~0.07 s
- Flat-table Bilinear path: ~0.11 s
- Vectorised Cartesian detection alone: ~0.01 s

These values are regression/engineering measurements, not guaranteed workstation timings; Windows hardware, antivirus, Python wheels and estimator choice affect absolute results.

## Deliberate backend split

GraphVis retains Matplotlib for normal publication rendering/export. The existing VisPy/PyVista GPU Preview remains available for workloads where GPU rasterisation is materially useful, particularly large 3-D meshes and volumes. A 250 x 250 2-D field is not GPU-bound after the topology/preprocessing fixes, so GraphVis 15 avoids adding a second embedded renderer to the normal heatmap path purely to mask unnecessary CPU preprocessing.
