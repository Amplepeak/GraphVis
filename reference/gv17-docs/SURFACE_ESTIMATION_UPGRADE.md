# Advanced Surface Estimation Upgrade — 2026.08.29.11

GraphVis uses **PySide6/Qt** for the desktop UI and **Matplotlib/Agg + NumPy/SciPy** for thread-safe plotting and export. Surface/field rendering is now routed through `src/graphvis/rendering/surface_estimators.py` rather than through one generic `griddata` path.

## Estimator catalogue

The **Surface estimation & noise control** panel groups the selectable backends as follows:

- Structured Matrix Methods: Nearest Neighbor, Bilinear Interpolation, Bicubic Interpolation, Rectangular B-Spline.
- Unstructured Scattered Estimators: Delaunay Triangulation (Linear), Natural Neighbor (Sibson’s), Inverse Distance Weighting (IDW), Modified Shepard's Method.
- Radial Basis Functions: Thin Plate Spline (TPS), Multiquadric RBF, Gaussian RBF.
- Statistical & Local Regression: Ordinary Kriging, Moving Least Squares (MLS), LOESS / LOWESS.

`Auto (data-aware)` selects a structured spline for complete Cartesian sweeps, IDW for extremely large scattered clouds, and Delaunay linear interpolation otherwise. Legacy estimator names are migrated automatically.

## Log-aware spatial metric

X and Y are transformed independently according to their GraphVis axis state. A Log10 spatial axis is transformed with `log10`, then both transformed dimensions are normalized to `[0,1]`. Delaunay, Sibson, IDW, Shepard, RBF, kriging, MLS and LOESS therefore operate in the same distance geometry that is visible on mixed linear/log plots. The returned Matplotlib mesh remains in physical coordinates.

## Pre-gridded matrices

2-D matrices can be rendered directly by Heatmap, Contour, 3-D Surface/Mesh/Contour, Waterfall/Ribbon and vector-field views. Compatible 1-D MAT/HDF auxiliary vectors appear as matrix-axis choices for X/Y; otherwise GraphVis auto-detects matching vectors or uses index coordinates. Structured methods preserve matrix topology and failed NaN/Inf cells.

## Invalid simulation coordinates

Non-finite X/Y are excluded. Finite coordinates with NaN/Inf response are retained as failed-simulation locations but never fitted. A conservative nearest-failure mask prevents interpolation from artificially bridging failed ODE regions. Invalid cells can be transparent or painted with a configured fallback colour. Outside-convex-hull masking remains distinct from the failed-data mask.

## Colour clipping and discrete contours

Color normalizers use `clip=False`, with colormap under/over colours set to the terminal palette colours. `Auto (from clipping)`, `Min`, `Max`, `Both` and `Neither` colorbar extensions are supported. Thus a response clipped at 5.5 while raw values reach 150 remains painted with the terminal Turbo colour instead of becoming blank.

`Discrete contour bands` uses `contourf` with a user-selectable band count. Continuous heatmaps use Gouraud vertex shading, the Matplotlib analogue of MATLAB `shading interp`.

## Performance architecture

- Expensive estimator output is cached separately from Gaussian post-smoothing, so smoothing-slider changes reuse the same Delaunay/RBF/kriging/Sibson result.
- During an active surface slider drag, GraphVis renders a coarse progressive grid and restores full requested fidelity on release.
- Interactive scattered clouds are adaptively decimated; export uses the full dataset.
- Long natural-neighbour, kriging, MLS and LOESS loops cooperatively check the render cancellation flag so stale renders can yield the single Matplotlib worker.
- Sibson interpolation uses Voronoi stolen-area weights in normalized metric space. Dense output grids evaluate Sibson weights on a controlled native grid and bicubically refine it, preventing one estimator from freezing the desktop at 500×500 resolution.

## Scientific controls added during review

Ordinary Kriging exposes Exponential, Spherical and Gaussian variograms. An optional **Clamp to observed response range** policy prevents spline/RBF/local-regression overshoot from creating nonphysical response values. This is independent of display/colorbar clipping.

## Vector export

PDF and SVG are available through the existing export pipeline. The export dialog now exposes:

- **Hybrid (vector text + raster field)** — recommended for dense fields: axes, labels, ticks, annotations and line art remain vector while only the dense heatmap surface is rasterized.
- **Full vector surface** — preserves every surface polygon for workflows that require completely vector field geometry, at potentially much larger file size.
