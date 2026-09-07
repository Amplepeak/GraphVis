# 2026-09-01 — GraphVis 16.3 hybrid architecture, adaptive UI, research-suite expansion (2026.09.01.19)

- **Hybrid architecture decision & groundwork**: `docs/ARCHITECTURE_MIGRATION_EVALUATION.md` documents the principal-engineer evaluation (full Rust, C++/Qt, Electron/WASM, status quo, hybrid) and selects the hybrid path: keep the Python/Qt shell + matplotlib publication engine, accelerate proven hot kernels via an optional Rust/PyO3 `graphvis_native` wheel. The FFI seam ships now (`core/native_accel.py`, wired into the IDW kernel with a byte-identical NumPy fallback); GPU stays on the existing vispy/pyvista path governed by Low-Power Mode.
- **Deep audit & hygiene**: audit findings (thread-safety, leaks, bottlenecks) recorded in the evaluation doc; dead `CollapsibleBox` widget deleted; stale patch-note artifacts (`*_PREVIOUS.md`, `README_LEGACY.md`) purged; fixed a latent `canonical_axis_scale` NameError in the canvas right-click scale menus.
- **Adaptive apply mode (new default)**: standard tools and light charts are fully reactive (debounced auto-apply); massive surface/volume/3-D visualizations strictly queue behind ▶ Apply so an expensive render can never hang the UI mid-tweak. Manual and Instant modes remain selectable.
- **Numeric inputs**: typed values now self-commit ~0.65 s after the last keystroke (no Enter needed, still never mid-number); embedded spinner arrows guaranteed on every box.
- **Persistent viewport**: switching visualization types, rendering modes or tool settings preserves the 3-D camera (all 3-D→3-D transitions) and axis bounds (whenever mapped axes, scales and datasets still match); only genuinely incompatible views reset. Toggleable via `ui/persist_viewport`.
- **Reset & history**: "Reset all controls to defaults" action (undoable, keeps dataset selection, clears the MATLAB preset toggle); global Undo/Redo toolbar buttons and the preset-revert toggle carried over from 16.2.
- **Data provenance logging**: every rendered figure carries a SHA-1 `provenance_hash` over the full parameter spec + dataset revision keys; each on-screen render appends a reproducibility record (hash, mappings, dataset revisions, clips, scales, timestamp) to `Logs/provenance.jsonl`, and every export writes a `.provenance.json` side-car plus embeds the hash in PNG/PDF/SVG metadata.
- **Publication-grade export fonts**: PDF/EPS now embed TrueType (Type 42) fonts and SVG keeps real selectable text (`svg.fonttype=none`) — matching journal submission requirements.
- **Workspace state serialization**: File → "Save/Restore workspace snapshot (JSON)" fully serializes the UI state, every open tab's spec, dataset source paths, literature references, active group, view mode and a bounded undo trail; restore reloads datasets by path and rebuilds the session.
- **Literature profiling & cross-linking**: literature sources now carry user-assigned profile tags (double-click a paper in Project Groups to edit; tags survive re-ingestion), and each paper's tooltip shows its full cross-link profile — the groups containing it, the simulation datasets those groups bind it to, and its extracted tables. Store API: `set_literature_tags`, `literature_tags`, `all_literature_tags`, `literature_cross_links`.
- **Simulation bridge — MATLAB Engine API**: a new in-process engine option runs `.m` scripts through MathWorks' `matlab.engine` and pulls every numeric workspace variable (scalars, vectors, N-D sweep matrices) directly into a GraphVis dataset with zero file round-trip.
- **Research-suite visualization expansion**: five genuinely new derivative-field engines — **Vorticity Map**, **Divergence Map**, **Phase Portrait** (streamlines + nullclines), **Flow Texture (LIC)** and **Tensor Glyph Field** (Hessian eigen-ellipses) — plus three new library categories (Fluid & Field Dynamics, Tensor & Structure Analysis, Spectral & Signal Imaging) growing the catalogue 286 → **318 styles / 30 categories**, with all 318 thumbnails regenerated.
- Regression: **135 headless engines / 0 errors**; all 25 smoke suites green.

# 2026-09-01 — GraphVis 16.2 tactile apply, LHS pipeline, imputation suite & simulation bridge (2026.09.01.18)

- **Tactile Apply mode (default)**: every axis/setting dropdown regains a ▶ Apply button immediately to its right; selections and range configurations queue until Apply (or "▶ Apply all pending controls") executes them, guaranteeing intentional, reliable application. Preferences → *Dropdown apply mode* switches between Manual and the GraphVis 16 Instant debounced auto-apply; the choice applies live to the sidebar, graph picker and canvas strip.
- **Numeric input reliability**: all spin boxes now commit typed values on Enter/focus-out instead of per keystroke — intermediate values (typing "200" firing 2 → 20 → 200) can no longer corrupt min/max ranges or spam renders — and every numeric box keeps embedded increment/decrement arrows.
- **LHS sanitization & mapping pipeline** (`analysis/lhs_pipeline.py`): `sanitize_bounds`/`sanitize_clipping_ranges` guarantee `min ≤ max` everywhere (inverted bounds like 200→159 are swapped; invalid ones fall back to true data extents), `resolve_lhs_mapping` enforces the canonical X=`lhs_flow`, Y=`lhs_area`, Z=`lhs_ea`, colour=`total_VHPR_mean` assignment (new "Map LHS sweep" button in Variable mapping), and `prepare_lhs_scatter` drops NaN/non-convergent solver points and returns clean, correctly bounded 3-D coordinate arrays. `build_figure` and view-state restoration now sanitize every clipping range/limit before rendering, so a corrupted range can never distort the 3-D bounding box.
- **Invert Opacity** toggle in the 5th-axis controls: flips the opacity mapping (`alpha = 1 − normalized value`) so dense low-performing background clouds can be dimmed; redraws immediately without touching coordinates or clipping sliders.
- **Invalid / failed data strategies**: the dropdown grows from 2 to 6 options — Transparent, Fallback colour, **Nearest Neighbor Fill**, **Local Mean Imputation** (iterative valid-neighbour averaging), **Baseline Clamp** (colour-scale minimum) and **Symmetric Mirror Fill** (row+column reflection across gaps). Imputation never touches cells outside the convex hull, records provenance in the imputed-cell overlay, and cannot create boundary clipping errors.
- **Viewport freedom & dynamic bounds**: panning/zooming is now unrestricted by default (drag the scene anywhere; Preferences → "Clamp panning" restores the old cage), and axes auto-expand with configurable padding whenever plotted content (overlays, markers, ingested data) exceeds the current bounding box on an unclipped axis — deliberately tight surface axes stay tight.
- **Point-cloud readability suite** (Interactive strip): per-graph **Pts** thinning budget (envelope/extrema-preserving LTTB decimation down to 500 points), **Density α** (density-dependent transparency turns overlapping blobs into smooth gradient clouds), **Aggregate** (very dense 2-D scatters auto-switch to hexbin density maps past the readability threshold) and **Gate W/V** (the 4th/5th-axis clip sliders become hard noise gates that strip background points — isolate the Pareto frontier in one toggle).
- **MATLAB-style surface preset is now a toggle**: the second click reverts every control to the pre-preset state. Global **Undo/Redo buttons** join the quick toolbar next to the float/dock controls, sharing the existing Ctrl+Z/Ctrl+Shift+Z history stack.
- **Universal simulation bridge** (Tools menu, `automation/sim_bridge.py`): engine-agnostic execution wrappers for Python scripts, ANSYS MAPDL batch, COMSOL Multiphysics batch, AQUASIM CLI and generic commands, with editable command templates, cancellable background execution and a time limit. Fresh output files (CSV/TXT/JSON/NPY/NPZ/MAT — sweep matrices, time series, grids, point clouds) are intercepted and registered as ordinary GraphVis datasets, instantly bound to the mapping dropdowns, sliders, clipping tools and 3-D renderer.
- Regression: 130 headless engines / 0 errors; all 25 smoke suites green.

# 2026-08-31 — GraphVis 16.1 interaction fixes, full scale suite & Low-Power Mode (2026.08.31.17)

- **Axis clipping sliders fixed**: the bottom-drawer range sliders' clips were silently discarded by every sidebar-driven spec rebuild; canvas clips now persist through `build_spec` (explicit sidebar spin-box clips still win per axis). X/Y clipping additionally *filters the plotted data subset* — histograms, box plots, statistics and limit detection now reflect exactly the visible window — while Z/W/V clips remain colour/size normalization bounds so over/under-range colorbar indicators keep working.
- **Persistent state on visualization change**: switching graph types no longer stomps user-selected axes or scales. `populate_axes` re-applies the heuristic suggestion only to roles whose previous selection no longer exists; the graph picker's cached/suggested mappings fill only genuinely unmapped roles; scale presets (EIS Bode, PSD) apply only while all three axis scales are still at their Linear default.
- **Marker deletion orphan fixed**: the coordinate-inspector's temporary circle was styled nearly identically to a permanent marker and was never removed by any deletion path — the "one marker always remains" bug. It is now cleared on every marker add/delete/delete-all, reset when a new figure installs, and restyled as a blue crosshair so inspection never masquerades as a marker.
- **Show nearest point on click**: new checkable right-click menu entry. When ticked, 2-D clicks snap to the closest displayed data point (3-D always snapped) and a cursor tooltip shows the point's exact coordinates, source dataset and screen distance; the inspector label/tooltip carries full 17-digit precision.
- **Smooth 3-D dragging**: matplotlib's Axes3D issues one uncoalesced full redraw per mouse-move; GraphVis now detects 3-D camera drags, routes those redraws through the ~60 fps frame coalescer, and — when the last full-quality draw took >50 ms — temporarily hides collections/lines with >1500 elements (>400 in Low-Power Mode) until release, followed by one full-quality redraw. 3-D mouse rotation also finally emits `view_changed`, so linked views and pick caches stay in sync.
- **Low-Power Mode** (Preferences → Low-Power Mode: Auto/On/Off): Auto activates on ≤4 logical cores or <7.5 GB RAM. When active: interactive DPI ≤72, point budget ≤4000, progressive/interactive grids ≤32/64, fast-preview fallback threshold halved to 5 s, mascot animation ~11 fps, 30 fps drag coalescing and the aggressive 3-D drag degradation. Export quality is never reduced.
- **Comprehensive per-axis scale suite**: every axis (X/Y/Z-response) now independently supports Linear, Log10, **Ln** (natural, e-power ticks), **Log2** (binary), **Sqrt** (signed square root — zero/negative safe), **Symlog**, **Logit** (validated to (0,1), linear fallback with a warning), **Date-Time** (epoch s/ms/ns and matplotlib datenums auto-detected; chronological spacing preserves irregular gaps; span-adaptive tick formats) and **Categorical** (distinct values evenly spaced with value-labelled ticks, capped at ~24 labels). Tick generation, label formatting, colour normalization (LogNorm/SymLogNorm/PowerNorm), clip guards and the log-space surface-interpolation metric (Log10/Ln/Log2) all adapt per scale. The canvas right-click X/Y/Z scale submenus expose the full suite with the active scale ticked.
- Regression: 130 headless engines / 0 errors; all 25 smoke suites green.

# 2026-08-30 — GraphVis 16 instant auto-apply & responsive rendering (2026.08.30.16)

- Instant auto-apply everywhere: the manual Preview / Generate buttons, the per-dropdown `▶` play buttons and both Apply All controls are retired. Every dropdown, slider, checkbox and graph-library selection now commits automatically through short debounce timers (sidebar 420 ms, canvas strip 350 ms) and issues exactly one render.
- Automatic fast low-resolution preview: chart/estimator families whose last render exceeded 10 s render a coarse preview first (≤48 grid, ≤4000 points, 72 dpi) with a dashed orange border around the canvas, while the full high-resolution render queues behind it and replaces it when ready. A render that unexpectedly crosses 10 s mid-flight is cooperatively cancelled, falls back to the fast preview, and re-queues itself.
- The loading indicator moved to a compact bottom-right badge over the canvas with a live progress bar (determinate or sweeping) and an inline Cancel button; the rest of the figure stays interactive during background processing.
- Granular cooperative cancellation extended into IDW, Modified Shepard (chunked k-NN evaluation), monotone-PCHIP tensor loops, structured interpolation and the Pareto bootstrap; the renderer checks the cancel token between dispatch stages and cancellation propagates cleanly instead of drawing an error figure.
- Cancelled or failed renders automatically roll every pending sidebar/canvas input (dropdowns, sliders, checkboxes) back to the last successfully rendered state, so the controls always describe the figure on screen.
- "Label & element colour palette" is now "Axis & Label Settings" and hosts explicit per-axis scale selectors (Linear / Log10 / **Symlog**, new SymLogNorm colour normalization included) alongside per-element text/colour/size, base font size and label padding.
- Data cleaning (`Apply outlier mask`, `Mask method`) moved into "Automated pattern recognition & limit detection", joined by a new **k-NN Distance Filter** mask method with a "Min local neighbours (k-NN)" threshold control (robust 3-MAD fence on the k-th neighbour distance).
- The bottom Interactive strip (including Axis clipping) now always initialises collapsed; Advanced View no longer force-expands it.
- Canonical thesis variables (`vhpr`, `sCOD`, `VFA`, `applied_voltage`) are force-included in every mapping dropdown; datasets missing one show a disabled "not present in this dataset" entry instead of silently omitting it.
- Literature extraction now writes extracted numeric tables into the dedicated `literature_dataset/` folder, and a new explicit "Use literature data as datasets & overlay recreated plots" checkbox recreates experimental curves as overlays on the active simulation graph after reading a paper.
- Export dialog resolution is now an editable tier selector (100 / 150 / 300 / 600 / 900 / 1200 DPI) that still accepts any custom value; fonts/line widths continue to scale proportionally with DPI.
- Compatibility fixes: Matplotlib ≥ 3.9 `boxplot(tick_labels=…)`, NumPy 2.0 `trapezoid`, and SQLite connector handles no longer lock `.db` files on Windows.
- Full regression: 130 headless render engines / 0 errors; 24 smoke suites green, plus the new `graphvis16_upgrade_smoke.py`.

# 2026-08-29 — GraphVis 15 surface performance architecture (2026.08.29.13)

- Added native pre-gridded matrix fast path and vectorised Cartesian sweep detection.
- Split scientific surface precomputation from the single Matplotlib composition worker.
- Added reusable content-safe KD-tree/Delaunay geometry plans and byte-aware surface caching.
- Added lazy HDF5/MAT-v7.3 matrix proxies and dataset revision-based cache invalidation.
- Added independent contour overlays, heatmap cross-section slicing, topology-aware NaN bridging/provenance, Clough–Tocher, monotone PCHIP, Log10 response interpolation and metric anisotropy controls.
- Added fast artist-only colormap/grid/surface-alpha updates and expanded Performance Inspector diagnostics.
- Full headless graph-library regression: 130 engines, 0 renderer errors.

# 2026-08-29 — QoL / tactical apply / MATLAB surface polish (2026.08.29.12)

- Added staged per-dropdown `▶` application and Apply All paths so browsing controls/visualisations does not trigger accidental expensive renders.
- Graph search now centers/highlights the selected library item and stages the renderer until explicit Apply.
- Added safe dataset detach/restore workflow with project-local permanent-delete double confirmation and external-source protection.
- Expanded Project Groups into many-to-many literature/dataset/script checklists with multiple named groups.
- Simplified literature/script ingestion labels and moved the linked bundle workflow to an advanced File-menu action.
- Added concise fallback hover help across sidebar controls and an explicit Noise `Off` mode.
- Added compact failed-sample masks plus conservative/Voronoi/fit-through policies and five convex-hull/full-domain extrapolation choices.
- Added a MATLAB-style surface preset and automatic publication-style field title/ticks/grid treatment.
- Added staged startup progress, moving approximate parser progress, 30 procedural swimming-otter/coral loader variants, Windows AppUserModelID branding and GraphVis `.ico` shortcut refresh.
- Full headless render regression remains 130 engines / 0 renderer errors.

# 2026-08-29 — Advanced surface estimation / topological field rendering (2026.08.29.11)

- Replaced the legacy generic surface interpolator with a dedicated Qt-free SciPy estimator backend.
- Added structured nearest, bilinear, bicubic and rectangular B-spline interpolation.
- Added Delaunay linear, Sibson natural-neighbour, IDW, Modified Shepard, TPS/multiquadric/Gaussian RBF, Ordinary Kriging, MLS and LOESS/LOWESS estimators.
- Added normalized Log10 spatial distance metrics for independently logarithmic X/Y axes.
- Added direct N×M matrix surfaces with compatible MAT/HDF auxiliary axis vectors.
- Added NaN/Inf failed-simulation masks, transparent/fallback rendering, convex-hull extrapolation policy, colorbar min/max/both extensions, and discrete `contourf` bands.
- Added kriging variogram selection and optional estimator-overshoot clamping.
- Split expensive estimator caching from post-smoothing and added cooperative cancellation for long local estimators.
- Added progressive coarse slider previews with full-resolution restoration on release.
- Added hybrid and full-vector field modes for PDF/SVG export.
- Added `surface_estimators_smoke.py` and `surface_ui_static_smoke.py`; the full 130-engine headless sweep passes with 0 renderer errors.

# 2026-08-29 — Independent per-axis scale engine (2026.08.29.10)

- Replaced the global graph scale selector with independent X, Y and Z/Response `Linear` / `Log10` state in the left variable-mapping panel.
- Added migration for old `Logarithmic Scale`, `Semi-Log X` and `Semi-Log Y` saved specs and graph-library presets.
- Added base-10 `LogLocator` major/minor tick spacing and `LogFormatterMathtext` scientific labels on logarithmic X/Y/Z axes.
- Added logarithmic Z/Response colour normalization with `LogNorm` for 2-D heatmap/contour fields without transforming X/Y coordinates.
- Applied matching logarithmic normalization to Z-coloured 3-D surfaces and Z-valued hexbin fields.
- Made Turbo and Jet explicit Matplotlib colormap registry mappings.
- Updated Python/MATLAB code export to reproduce independent axis scaling.
- Added `per_axis_scale_smoke.py` and `per_axis_scale_ui_static_smoke.py`; full 130-engine headless renderer sweep completed with 0 renderer errors.

# 2026-08-29 — Focus / dock / marker / toolbar hotfix (2026.08.29.9)

- Startup otter splash no longer stays on top; completing startup does not steal focus if the user Alt-Tabs to another application.
- Scoped interactive graph background palettes to the plot viewport so light/white graph backgrounds no longer wash out Graph Controls or Interactive toolbar text. Added explicit theme styling for both strips.
- Replaced visibility-driven detachable-panel redocking with an explicit-close dock shell, allowing Graph Controls/Interactive panels to remain genuinely docked/tabbed; double-clicking an embedded panel header also pops it out.
- Pre-sized new Matplotlib canvases to the existing viewport and atomically replace the old canvas, removing the brief small-then-grow flash when changing visual types.
- Added static-content resize handling and a longer resize-settle debounce to reduce remaining splitter/dock paint churn.
- Reduced compact graph-library previews to 46×28 px, use 40 px rows, and add transparent padding so thumbnails retain a visible gap.
- Changed 2-D marker placement/inspection to exact cursor-axis coordinates, strengthened HiDPI marker hit testing, and made Delete marker/Delete all/Save marker available from every graph right-click menu.
- Added `tests/ui_marker_docking_regression_static_smoke.py`.

# 2026-08-29 — Graph background / deep scan / export-DPI upgrade

- Added independent interactive graph-background modes: Light (default), White, Dark, Theme and Custom, with 0–100 brightness plus optional UI-theme blending.
- Added contrast-aware default ticks, axes, spines, grids, labels, legends and colourbars so dark graph backgrounds remain readable without overwriting explicit user styling.
- Added compact/tightly cropped and transparent-merge graph-library preview assets; graph categories are now populated lazily to reduce startup work.
- Removed transient white canvas exposure during splitter resizing and defer Matplotlib resize work until a drag settles.
- Fixed an interactive render-cache key mismatch that caused avoidable rerenders.
- Expanded Smart Suite/Scan Dataset budgets to Seconds/Minutes/Hours (up to 72 h), made column/row sampling budget-aware, and added retained resumable profile/pair-metric checkpoints plus an explicit clear-cache command.
- Added per-file literature scan budgets, deadline-aware PDF/OCR extraction, removal of the old fixed OCR page ceiling, and compressed full-text project caching with excerpt-only startup reads.
- Removed the 3000-DPI ceiling from single export, batch export, default export settings and publication profiles; high values remain permitted with RAM/raw-image-size warnings.
- Added new static/runtime regression coverage for background controls, checkpoints, full-text cache behavior and unrestricted DPI.

# 2026-08-29 — Responsiveness / blank-stage / theme expansion

- Startup now displays a true empty dark stage with no visible graph tab; the reusable Workspace editor is attached only when a graph is explicitly generated. Closing the Workspace or final graph returns to the blank stage.
- Reduced GraphPicker entry thumbnails from 112×70 to 74×46 and hover preview from 240×150 to 160×100. Hover details now disappear on pointer leave and are no longer pinned by selection.
- Added lazy process-wide thumbnail icon caching, uniform graph-tree rows, disabled tree/dock animation, and no-op guards that avoid rebuilding the 286-entry picker when capabilities/recommendations did not change.
- Added fast interactive rendering defaults: 84 DPI, 10k point budget, 96-cell surface grid, preview-only surface decimation, and full-data export preservation.
- Made cross-session Matplotlib figure pickling opt-in while retaining per-canvas in-memory caching.
- Coalesced canvas pan/touch redraw requests to ~60 fps and added slow-render timing to `graphvis.log` under the PERF tag.
- Main-window geometry, Qt dock/toolbar state, popped-out sidebar sections and UI theme are now persisted.
- Expanded themes from 15 to 30: 10 core themes plus 20 colour-accent themes.
- Added performance controls to Settings.

# 2026-08-29 — Compact Studio UI / non-intrusive workspace

- Reworked GraphVis chrome around a compact studio layout inspired by modern design/CAE applications: the canvas remains dominant and controls stay visually quiet until hovered/focused.
- Replaced the large graph-control headers with 22 px arrow/grip/pop-out chrome; graph panel title text is hidden on-canvas.
- Added six-dot grip affordances and a custom 7 px dotted splitter handle for precise graph/bottom-panel resizing.
- Converted Graph Controls secondary actions to compact icon-only tools with descriptive tooltips and reduced the expanded top strip to roughly 56 px total including the collapsible header.
- Interactive controls now default collapsed, use a 22 px header, remember the last expanded height, and retain pop-out/docking.
- Converted Scientific Tools and Window/Layout to 28 px icon-only floating/dockable toolbars.
- Reduced sidebar default width from 520 px to 360 px and migrate legacy oversized saved widths once.
- Reduced logo/header footprint, Simple/Advanced button height, menu/tab/input spacing, scrollbar thickness and status-bar height.
- Removed heavy panel/dock framing in favour of subtle one-pixel dividers and hover/focus emphasis.
- Added `GripDots`, `CompactSplitter`, compact vector tool icons and static regression coverage for the new UI density contract.

# 2026-08-29 — Intelligent Scan / true thumbnails / docking fix

- Fixed graph selection to use one atomic selection path: tree clicks, fuzzy-search suggestions, and Enter-to-select all keep the item highlighted and schedule one debounced render instead of double-queuing.
- Preserved background render cancellation so rapid graph changes supersede older work rather than freezing the UI.
- Added 286 release-time thumbnails rendered through the real GraphVis plotting engine from a comprehensive reference dataset; 3-D/volume/vector/statistical entries now show representative real plots instead of generic 2-D icons.
- Added `Scan Dataset` deep mapping advisor with variable profiling, semantic role detection, Pearson/Spearman/nonlinear dependency scoring, graph-specific axis mappings and local cache persistence.
- Scan caches are keyed by both dataset fingerprint and literature-context fingerprint, so the same dataset can keep different mappings for different thesis/paper groups.
- Added `Scan + Literature…` to combine PDF/text semantic context with dataset structure before ranking visualizations.
- Added duplicate/alias suppression and word-aware semantic matching to avoid false recommendations such as treating `eval_time_sec` as an energy/frequency axis.
- Fixed graph/bottom-controls vertical resizing: the interactive panel can shrink to its header, its height is remembered, and it remains collapsible/detachable.
- Added explicit stacked-rectangle pop-out controls to dock panels, programmatic pop-out controls for toolbars, tear-off menus, and stronger Reset Layout behavior.
- Expanded Publication Ready built-ins to Nature single/double, Science/AAAS compact, IEEE single/double, Elsevier line-art baseline and APA 7 figure profiles.

# 2026-08-29 — Project Intelligence / blank-startup upgrade

- Startup now opens a blank `Workspace` tab and does not automatically render or restore graph tabs.
- Project datasets are discovered in the background without creating plots; previous tabs can be restored explicitly from the File menu.
- Added named persistent Project Groups linking thesis/papers, datasets, scripts and notes.
- Added simplified `Read Literature / Thesis`, `Add Dataset`, `Project Groups`, `Import Script / Parameters`, `Add Linked Literature Bundle`, and `Smart Map Suite from Active Group` workflow.
- Literature/theses are stored under each project, semantic context is cached for future sessions, and extracted numeric datasets are saved to `datasets/literature_extracted`.
- Added static MATLAB/Python/R/Julia plot/parameter parsing without executing imported code.
- Added optional user-configured OpenAI-compatible API Project Advisor; raw dataset rows and API keys are not persisted/sent by default.
- Smart Map Suite now combines saved AI advice, script plot hints, offline literature/project context, dataset recommendations, compatibility checks and automatic axis mapping.
- Added chunked cancellable project copying with real progress and `.graphvis.part` cleanup.
- Added page-level literature/OCR progress checkpoints and Cancel integration.
- Replaced the generic loading circle with the animated GraphVis otter, rotating water-arrow arc and swimming bob/tilt motion.
- Object Manager now exposes named Project Context groups and linked literature/dataset/script entries.

# Shared Runtime Fingerprint Hotfix

- Fixed Windows requirements fingerprint failure for application paths containing spaces.
- Replaced fragile `FOR /F` command substitution with a temporary-file hash handoff.
- Added stale-stamp validation and automatic repair of incomplete shared environments.
- Added `Install_GraphVis.bat`, `Repair_GraphVis.bat`, and `Start_GraphVis.bat` wrappers.
- Clarified that `run_app.bat` launches only and `update.bat` owns installation.

# GraphVis changelog — Advanced UI merge + startup repair

## Advanced interface merge
- Kept the full Professional Suite/MVC backend while adopting the uploaded advanced GraphVis interface patterns for graph selection and sidebar presentation.
- Added an inline categorized `GraphPicker` with native expand/collapse arrows, live typo-tolerant suggestions, dataset compatibility states and recommendation grouping.
- Removed the purple/orange hard-coded literature button styling and document/book emoji; literature actions now follow the active application theme.
- Increased the sidebar GraphVis logo to approximately twice its former size.
- Removed the checkerboard pixels from the logo and saved genuine alpha transparency so the artwork follows any theme background.

## Theme repair
- Added `GraphVis Light` as the new default while retaining Scientific Light, Glassmorphism, Neon Dark and High Contrast.
- Rebuilt theme QSS around complete input/view/menu/tab/dock/scrollbar states and installed a matching `QPalette`.
- Removed several hard-coded dark text colours from collapsible controls/coordinate inspector that made text unreadable in dark themes.

## Startup repair
- Added `startup_bootstrap.py`, which runs before PySide6 is imported when `main_app.py` is launched directly.
- `main_app.py`, `GraphVis.py` and `run_app.bat` all converge on the local `.venv` interpreter.
- `run_app.bat` now repairs an incomplete environment and launches `main_app.py` directly.
- `update.bat` / `update.sh` support `--no-launch` for bootstrap repairs without nested application instances.

## Literature workflow
- Restored the advanced-interface option to auto-configure and open reconstructed graph tabs after batch literature ingestion.

## Data and connectivity
- Added ASCII/DAT, XLSM/all-sheet workbook, WAV/FLAC/OGG, MGF/mzML/mzXML and FCS support around the existing scientific loader stack.
- Added SQLite, MySQL, Oracle, SQL Server/ODBC, generic ODBC, Windows ADO/OLEDB, HTTP(S) and fsspec cloud/network connectors.
- Added per-connector refresh interval tied to Live Telemetry.
- Added formula columns, conditional masks, pivot dataset creation, metadata editor and callback-backed data-operation Undo/Redo.

## Analysis
- Added typed statistics, multivariate/ML, survival/power, signal-processing, numerical-calculus, curve/peak and surface-fitting engines.
- Added Tukey plus Bonferroni/FDR/Fisher-LSD/Scheffe post-hoc helpers.
- Added Predictive Modeling Advisor.

## Workflow and UI
- Expanded Object Manager to group workbook worksheets and graph tabs.
- Added multi-sheet workbook loader, Data Connector dialog, Analysis Hub, Batch Processor and Workflow Builder.
- Added embedded Python and optional R consoles and detachable graph windows.
- Enhanced ROI gadget with local statistics/integral, export and apply-as-filter actions.
- Enhanced optical digitizer with Cartesian, polar and ternary calibration.

## Visualization
- Expanded catalogue to 140 selectable entries / 121 concrete render engines.
- Added publication/statistical and spatial engines including forest, volcano, ROC, control, Manhattan, population pyramid, Treemap, Sunburst, Sankey, Venn, Piper and Wind Rose.
- Retained adaptive data decimation and optional VisPy/PyVista GPU preview/PBR path.

## Setup/reliability
- Kept explicit MVC bootstrap and routed new analysis service calls through the Controller.
- Kept rotating `graphvis.log` for persistent error history.
- Added/updated `GraphVis.py`, `update.bat`, `update.sh`, dependency manifests and reproducible smoke tests.
## Installer / startup hotfix
- Removed all environment repair/bootstrap logic from `main_app.py` and `GraphVis.py`.
- `update.bat` is now the sole Windows dependency manager.
- Standardised Windows setup on Python 3.12 for broad scientific-wheel compatibility.
- Core NumPy/Pandas/SciPy/h5py/Matplotlib/PySide6 installation is binary-wheel-only; GraphVis no longer requires a C/C++ compiler for core setup.
- Added persistent `logs/update.log` and pip `logs/pip-install.log`.
- Added `launch_graphvis.py` and `logs/launcher.log` to capture startup/import tracebacks that occur before the GUI logger exists.
- `run_app.bat` no longer installs or repairs dependencies; it only launches an already prepared `.venv`.
- `debug_run.py` now reports installer/startup/application log tails before dependency checks.

## 2026-08-29 — Dockable UI / 286-graph library upgrade
- Replaced the custom constrained control-sidebar splitter with a native Qt dock. The divider now uses normal Qt resizing and the complete control sidebar can float to another window/monitor.
- Added individually detachable/collapsible sidebar sections for ingestion, graph selection, mappings, filtering, surface controls, limits, styling, aliases and actions.
- Added detachable/collapsible per-graph top controls and interactive bottom controls. A vertical graph/control splitter now allows continuous height resizing; long button/control rows scroll horizontally rather than disappearing off-screen.
- Normalized Scientific Tools buttons to the same dimensions and border geometry. The toolbar remains movable, floatable and hideable.
- Preserved movable graph tabs and added workspace layout presets: Analysis, Focus Graph, Presentation, and Reset.
- Added Ctrl+K to reveal/focus fuzzy graph search and persisted native Qt dock/toolbar layout between sessions.
- GraphPicker now starts with every category collapsed, shows live fuzzy suggestions while typing, and provides an immediate hover preview card with an example glyph, description, category, renderer and current availability.
- Expanded the selectable graph catalogue to 286 definitions across 27 categories mapped to 130 concrete renderer engines. The complete catalogue remains searchable in Simple View; Simple/Advanced now controls editing complexity instead of hiding graph types.
- Added the missing concrete Performance Ceiling renderer (binned 99th-percentile empirical envelope).
- Consolidated GraphVis Light / Scientific Light into one `Light` theme. Added Glass, Graphite, Midnight, High Contrast plus 10 colour-accent themes (Red, Orange, Yellow, Lime, Green, Teal, Cyan, Blue, Purple, Magenta).
- Expanded the colormap library to 84 palettes, including additional sequential, diverging, cyclic, terrain/ocean, categorical and ColorBrewer-style palettes.

## Startup runtime hotfix
- Fixed a fatal `NameError` during `GraphVisWindow` construction: `QSizePolicy` was used by the Advanced UI layout but was not imported.
- Added `startup_preflight.py`, an off-screen MVC/Qt construction test invoked by `update.bat` after dependency installation.
- `GraphVis.py` now delegates to the persistent logging launcher and pauses on Windows startup failure, so double-click failures do not disappear immediately.
- Dependency installation remains exclusively in `update.bat`; no updater logic was added to `main_app.py`.

## Runtime installer / launch hotfix
- Core-only `Repair_GraphVis.bat` so optional dependencies cannot break repair.
- Recommended/full optional groups no longer abort GraphVis core installation.
- Removed required `netCDF4` C-extension from the recommended profile; h5netcdf/SciPy are primary NetCDF backends.
- Deferred heavy MVC/UI imports until after the otter splash is visible.
- Added explicit launcher startup-stage messages.

## 2026.08.29.7 — Smart Performance / Marker / UI reliability

- Added true five-second delayed otter loading feedback for background tasks and renders.
- Removed missing-variable hard-coded selector placeholders and clear stale data-specific visuals on dataset change.
- Added cached, precise point picking plus temporary/permanent data markers with save/delete context menu.
- Added theme-integrated interactive figure backgrounds while preserving publication/export behavior.
- Added explicit tab navigation arrows and graph-library row spacing; removed pinned graph-detail panel.
- Deferred Matplotlib resize redraws and made compact splitters non-opaque for smoother panel movement.
- Prevented heavy slider drags from continuously starting renders.
- Extended Smart Suite with seconds/minutes budgets, scan-depth scaling, graph-specific color/scale decisions, cached AI-advice integration, and mapping diversity.
- Added framework migration research recommending an incremental PySide6 Qt Quick/QML + renderer-adapter architecture.
