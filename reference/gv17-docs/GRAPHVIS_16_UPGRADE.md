# GraphVis 16 — Instant Auto-Apply, Fast Preview & Responsive Rendering

This release refactors the interaction model around one principle: **the UI
always describes the figure on screen, and every change applies itself.**

## 1. Instant auto-apply (no Preview button)

- The canvas "Preview" button, the sidebar "Generate / preview" button, the
  per-dropdown `▶` play buttons, "▶ Apply selected visualisation" and both
  "Apply All" controls are hidden (widgets are retained for saved-layout and
  smoke-test compatibility, and `Ctrl+R` still forces a manual refresh).
- Dropdowns commit through debounce timers: sidebar staged combos commit after
  **420 ms** (`GraphVisWindow._staged_commit_timer` →
  `_commit_pending_staged`), canvas Interactive-strip combos after **350 ms**
  (`ScientificPlotCanvas._local_combo_debounce`). Sliders keep their existing
  drag-aware debounce (coarse progressive preview while dragging, full render
  on release). Checkboxes/spinboxes were already live.
- Graph-library selections stage as before but now auto-commit through the
  same timer, applying cached Smart-Scan mappings automatically.

## 2. Fast low-resolution preview (10-second rule)

`ScientificPlotCanvas` keeps a session-wide wall-clock duration history per
`chart_type|estimator` family (`_RENDER_DURATION_HISTORY`).

- **Predicted slow** (`> FAST_PREVIEW_THRESHOLD = 10 s`): a coarse preview
  spec (grid ≤ 48, ≤ 4000 points, 72 dpi, progressive estimator caps) renders
  first and is installed under a **3 px dashed orange border**
  (`#E67E22`) around the plot host so it is unmistakably a preview. The
  untouched full-resolution request is queued (`_queued_full_request`) and
  dispatched the moment the preview installs; when it lands, the border clears.
- **Unexpectedly slow**: any full render that crosses 10 s mid-flight is
  cooperatively cancelled by `_preview_fallback_timer`, the family is marked
  slow, a fast preview is composed immediately and the full render re-queues
  behind it.
- New changes during any of this simply bump the render sequence number: stale
  results (preview or full) are dropped, and the newest request wins.

## 3. Background-processing indicator

`LoadingOverlay` gained a `mode="corner"` variant used by every plot canvas: a
compact badge anchored to the **bottom-right** of the canvas with the animated
GraphVis otter, the current stage message, a determinate/sweeping **progress
bar** and an inline **Cancel** button. The badge occupies only its own corner,
so the figure stays fully interactive while the high-resolution render works.
Full-screen centred overlays remain for global operations (imports, OCR).

## 4. Cancellation & state reversion

- Cooperative `cancel_check` tokens now reach **every** estimator loop:
  IDW and Modified Shepard evaluate their k-NN queries in 4096-row chunks with
  per-chunk checks; monotone-PCHIP tensor interpolation checks every 32
  columns; kriging/MLS/LOESS/Sibson retain their per-row checks; the Pareto
  bootstrap checks every 16 resamples; `_Renderer.render()` checks before and
  after chart dispatch and per prepared dataset. Cancellation raises
  `RuntimeError("Cancelled")` and propagates to the worker's `cancelled`
  signal instead of painting an error figure.
- On **cancel** or **failure** the canvas restores its spec from the last
  successfully rendered snapshot, re-syncs its local controls, and emits
  `render_reverted`; the main window then re-applies the last good UI snapshot
  (`_last_good_ui_state`, captured on every successful render) without
  triggering a new render. Sliders, dropdowns and checkboxes therefore always
  roll back to the last confirmed applied state.

## 5. Panel refactor

- **Axis & Label Settings** (formerly "Label & element colour palette"): the
  per-axis scale selectors (X / Y / Z-response) live here with the new
  **Symlog** option (`AXIS_SCALE_OPTIONS = Linear | Log10 | Symlog`;
  symmetric-log axes use a data-derived `linthresh` of 1 % of the largest
  magnitude, and a Symlog Z-response maps colours through `SymLogNorm`).
  Per-element text/colour/size dialogs (Main Title, X/Y/Z labels, Tick
  Labels — tick marks share the tick colour — Colourbar Title, Legend), base
  font size and label padding are unchanged.
- **Automated pattern recognition & limit detection** now also owns data
  cleaning: `Apply outlier mask`, `Mask method` (Percentile Capping, Z-Score
  Clip, and the new **k-NN Distance Filter**) and **Min local neighbours
  (k-NN)** — the `k` used by the filter, which drops points whose k-th
  nearest-neighbour distance exceeds a robust `median + 3·1.4826·MAD` fence in
  standardised coordinates (`PlotSpec.mask_min_neighbors`, serialised and
  reproduced by exported Python scripts).
- The bottom toolbar's Interactive strip — including **Axis clipping** —
  always initialises **collapsed**; Advanced View no longer force-expands it.

## 6. Literature pipeline

- Extracted numeric tables are saved as CSVs (plus provenance side-cars) in
  the dedicated **`literature_dataset/`** folder
  (`Documents/GraphVis/Literature/literature_dataset`,
  `paths.LITERATURE_DATASET_DIR`).
- A new ingestion checkbox — *"Use literature data as datasets & overlay
  recreated plots"* — registers every extracted table and overlays the
  recreated experimental curves (with error bars where `±` columns were
  detected) on the active simulation graph for direct model-vs-literature
  validation. The existing advisory engine continues to recommend Gompertz
  kinetics, polarization curves, EIS Nyquist/Bode layouts etc. with
  scientific justifications.

## 7. Export suite

- The Export dialog's DPI field is an editable selector seeded with the
  publication tiers **100 / 150 / 300 / 600 / 900 / 1200**; any custom
  positive DPI can still be typed. PNG/JPG/TIFF/PDF/EPS/SVG remain, with
  CMYK/ICC conversion for TIFF/JPG. Fonts and line widths scale
  proportionally with DPI because export re-renders the spec from scratch.

## 8. Compatibility & branding

- Matplotlib ≥ 3.9: `boxplot(tick_labels=…)` with a legacy fallback.
- NumPy 2.0: `np.trapz` → `np.trapezoid` shim in fitting/signal/plotting.
- Windows: the SQLite connector now closes its handle so temporary `.db`
  files are deletable.
- The sidebar brand block prefers the full GraphVis logo
  (`assets/branding/graphvis_logo.png`) and will automatically pick up a
  user-supplied `logo graphvis.jpg` dropped into `assets/branding/`.
- Simple View vs Advanced View, the `vhpr`/`sCOD`/`VFA`/`applied_voltage`
  priority variables (now force-included with disabled placeholders when a
  dataset lacks them), Parula/Viridis/Plasma/Turbo colourmaps, Gaussian
  smoothing and bilinear interpolation are all preserved.

## Validation

- `tests/headless_render_smoke.py`: **130 engines, 0 errors**.
- All 24 pre-existing smoke suites pass, plus the new
  `tests/graphvis16_upgrade_smoke.py` covering auto-apply wiring, the fast
  preview pipeline, cancellation propagation, Symlog rendering, k-NN masking,
  the `literature_dataset/` layout and the DPI tier selector.
