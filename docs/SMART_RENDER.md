# Intelligent Auto-Optimization / Smart Render

Smart Render is GraphVis's default presentation optimizer. It never mutates Arrow source data. It produces a renderer-neutral `SmartRenderPlan` and a presentation-only point/heatmap policy.

## Statistical strategy

1. **Finite-data scan** ignores NaN/Inf for presentation statistics while leaving the source dataset untouched.
2. **Sampled robust quantiles** (up to 200k deterministic observations) estimate Q1/Q5/Q25/median/Q75/Q95/Q99 without sorting multi-million-row arrays in full.
3. **MAD robust z-score** uses `z_r = (x - median)/(1.4826 MAD)`. Values beyond roughly 4.5 robust standard deviations contribute to the outlier-pressure estimate; anomalies above 3.5 receive visual emphasis.
4. **Display clipping** defaults to Q1–Q99 and tightens conservatively for outlier-heavy distributions. Clipping affects the colour mapper only; points are not deleted.
5. **Colour transform**:
   - signed response crossing zero → SymLog + CoolWarm;
   - positive multi-decade response → Log10 + Viridis;
   - strongly skewed/outlier-heavy response → empirical-CDF histogram equalization + Viridis;
   - otherwise → linear + Viridis.
6. **Heatmap interpolation policy**:
   - sparse/low-resolution → nearest;
   - dense and comparatively smooth → bicubic;
   - otherwise → bilinear.
7. **Scatter signal/noise policy** combines response prominence, robust anomaly score and spatial voxel density. Dense background clouds are spatially stratified rather than randomly sampled. X/Y/Z/response extrema are always preserved.
8. **LOD profiles**: Clarity targets up to 450k points, Balanced 280k, Performance 140k. Each profile uses deterministic spatial representatives and per-point density-aware alpha/size.

## Why voxel density instead of DBSCAN on every redraw?

DBSCAN/HDBSCAN are useful offline analysis tools but are a poor default in an interactive render loop because cost and parameter sensitivity rise quickly with millions of points and changing axis mappings. Smart Render therefore uses O(N) spatial voxel density plus robust anomaly scores for continuous interaction. A future analysis-stage clustering command can still run DBSCAN/HDBSCAN without making ordinary graph rendering depend on it.

## UX

Smart Render is enabled by default in **Variable Mapping → Intelligent Auto-Optimization**. Users choose Balanced, Clarity or Performance and click **Analyze & Apply**. The panel displays the selected contrast, palette, interpolation, robust bounds, LOD target, confidence and human-readable reasons. Turning Smart Render off restores the existing manual point-size, opacity and voxel controls.

`Invert Opacity` remains an intentional user control and works in either mode.
