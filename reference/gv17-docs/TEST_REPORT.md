# GraphVis Test Report — 2026.08.29.13 Performance Architecture

- `python -m compileall -q src` — **PASS**
- `tests/graphvis15_performance_smoke.py` — **PASS**
- `tests/surface_estimators_smoke.py` — **PASS**
- `tests/surface_mask_extrapolation_smoke.py` — **PASS**
- existing UI/static/backend/project/theme regressions — **PASS**
- `tests/headless_render_smoke.py` — **130 engines / 0 renderer errors**

Synthetic 250×250 regression in the build environment: vectorised Cartesian detection ~0.01 s; native matrix preparation ~0.003 s. Absolute workstation timings will vary.

# GraphVis Test Report — 2026.08.29.12 QoL / Surface Polish

- Python `compileall` across modified source/tools: **PASS**.
- Existing non-GUI test suite: **PASS**.
- `graphvis14_qol_static_smoke.py`: **PASS** — staged apply, safe deletion, project groups, loader/icon and extrapolation UI contracts.
- `surface_mask_extrapolation_smoke.py`: **PASS** — compact failure footprint is materially smaller than Voronoi masking; conservative hull masking and all full-domain modes behave independently.
- `workspace_detach_restore_smoke.py`: **PASS** — safe detach/restore and double-confirm permanent local-copy semantics preserve the external import source.
- `surface_estimators_smoke.py`: **PASS** — estimator/log-space/masking/colorbar/vector export coverage remains clean.
- Full Agg/headless renderer sweep: **130 engines, 0 renderer errors**.
- Windows taskbar/shortcut icons, PySide6 live docking/mouse feel and animation frame pacing require target-machine validation because this build environment has no Windows Qt desktop session.

---


## 2026.08.29.11 — Advanced surface estimation verification

- `tests/surface_estimators_smoke.py` — **PASS**: all 15 estimator choices route successfully; log-aware physical grids, failed-data masks, matrix surfaces, clipped-outlier colorbar extension, discrete contouring and full-vector SVG/PDF rendering are exercised.
- `tests/surface_ui_static_smoke.py` — **PASS**: categorized estimator UI, scientific controls, progressive preview and vector-export controls are present.
- `tests/headless_render_smoke.py` — **PASS: 130/130 concrete engines, 0 renderer exceptions**.
- Existing per-axis scale, themed-marker, professional backend, native figure, UI layout, marker/docking and Smart-performance regressions — **PASS**.
- Python compilation of modified rendering/UI/export/data modules — **PASS**.

The build environment remains headless, so physical Windows PySide6 slider dragging, dock movement and native file-dialog interaction remain target-machine validation items. Mathematical/rendering behavior is exercised through the Agg backend.
# GraphVis Test Report — 2026.08.29.10 Independent Per-Axis Scale Engine

- Python `compileall` across `src`, `tests`, and `tools`: **PASS**.
- `per_axis_scale_smoke.py`: **PASS** — legacy scale migration, independent X/Y Log10 axes, 3-D Z Log10 axis, base-10 tick formatter/locators, LogNorm heatmap response/colorbar, Turbo and Jet resolution.
- `per_axis_scale_ui_static_smoke.py`: **PASS** — X/Y/Z scale selectors are adjacent to mapping controls and the former global canvas scale combo is removed.
- Full headless renderer sweep: **130 engines, 0 renderer errors**.
- Existing marker/docking, graph-background/export, Compact Studio, true-thumbnail, intelligent-scan, native-figure, project-context, professional-backend and layout regression checks used during this patch: **PASS**.
- Physical PySide6 desktop interaction remains a target-machine validation item because this build environment does not provide a Windows Qt desktop session.

---

# GraphVis Test Report — 2026.08.29.6 Responsiveness / Blank Stage

- Python `compileall`: PASS.
- `performance_blank_ui_static_smoke.py`: PASS.
- Compact Studio / UI layout / true-thumbnail static tests: PASS.
- Graph catalogue: **286 definitions**; themes: **30** (20 colour themes); colormaps: **84**.
- Headless renderer sweep: **130/130 concrete engines, 0 renderer exceptions**.
- Native `.gvfig`, intelligent scan, project context/intelligence, organized runtime and professional backend tests: PASS.
- User-supplied `complete_df_MEC_Master_Results.mat`: 200,000 rows / 42 numeric columns loaded in the build environment; fast preview benchmark was ~0.19 s for 4D/5D scatter and ~0.18 s for a 2D heatmap using the new 10k/96-grid preview path. The previous full 160-grid heatmap path measured ~0.30 s in the same environment.
- Windows/PySide6 physical mouse/dock testing remains a target-machine validation item.

---


## 2026.08.29.5 Compact Studio UI regression

- Python compilation: PASS.
- `compact_studio_ui_static_smoke.py`: PASS.
- `ui_layout_static_smoke.py`: PASS.
- Existing project context/intelligence, organized-runtime, native-figure, professional-backend and intelligent-scan smoke tests: PASS.
- Headless renderer sweep: 130/130 concrete engines, 0 renderer exceptions.
- Physical PySide6 mouse/dock testing remains a target-Windows validation item because PySide6 is not installed in this build container.
# GraphVis Test Report — Intelligent Scan / True Thumbnail / Docking Upgrade

## New verification in this release

- `compileall` across the organized source tree: PASS.
- True preview generation: **286/286 graph definitions received PNG assets**; **133 unique engine/scale renders**, **0 generation errors**.
- Deep-scanner synthetic test: PASS; 17 ranked recommendations with multiple distinct graph-specific mappings and context-specific cache files.
- User-supplied `complete_df_MEC_Master_Results.mat` validation: 200,000 rows, 42 numeric columns and 26 matrices loaded; deep scan completed in the build environment and produced distinct parameter/response mappings including response-surface heatmap/contour/3-D surface, sensitivity, Pareto and multivariate views.
- Graph/UI static contract: 286 preview assets present; Scan Dataset, Scan + Literature, unified selection handler, Window & Layout toolbar, pop-out title bars, vertical-splitter persistence and the requested publication profile families are present.
- Headless renderer regression: **130/130 concrete renderer engines, 0 errors**.
- Native `.gvfig`, project context, project intelligence, organized runtime and professional backend smoke tests: PASS.

## GUI validation boundary

The build environment still lacks a Windows PySide6 desktop session, so actual mouse dragging/floating, menu tear-off behavior and Windows multi-monitor placement require target-machine click testing. The code path is compiled and statically verified, and the scientific render/scan backends are exercised headlessly.

---

# GraphVis Test Report — Project Intelligence / Blank Startup

## Automated tests passed in the build environment

- Python `compileall` across `src`, `tests`, and `tools`.
- `project_context_smoke.py`:
  - project literature/scripts/extracted-data directories;
  - chunked copy progress;
  - cancellation and incomplete `.graphvis.part` cleanup;
  - project-specific literature extraction output;
  - persisted Project Groups/literature context;
  - MATLAB/script analysis and concrete GraphVis plot-engine mapping.
- `project_intelligence_static_smoke.py`:
  - blank non-rendering startup tab;
  - explicit session restore;
  - simplified literature/group/script/Smart Map UI;
  - optional AI advisor surface;
  - cancellable loading integration;
  - animated otter loader contract;
  - project-context persistence components.
- Organized/shared-runtime filesystem smoke test.
- Native `.gvfig` round-trip smoke test.
- Professional-suite backend smoke test.
- Static dockable-UI/library/theme contract test.
- Headless renderer test: **130/130 concrete renderer engines, 0 errors**.
- Graph catalogue: **286 selectable definitions**.
- Themes: **30**, including 20 colour-accent themes.
- Scientific colormaps: **84**.
- Branding scan: no superseded university-brand references in active source/docs/package files.

## Behavioral design verified statically

- Startup creates `ScientificPlotCanvas(..., autorender=False)` for the `Workspace` tab.
- Startup session restore is disabled; a File-menu action can restore previous graph tabs explicitly.
- Project dataset scan suppresses automatic plot rendering.
- Literature and script context is project-persistent and reused after restart.
- Literature numeric data is written below the project's `datasets/literature_extracted` folder and participates in recursive dataset scanning.
- File copy uses temporary `.graphvis.part` files and cancellable chunked copy.
- Literature PDF/OCR progress reports per-page checkpoints where the underlying library allows it.
- Loading overlay uses the GraphVis otter with a rotating water-arrow arc and swimming bob/tilt motion.

## Validation boundaries

The build container does not have the Windows PySide6 desktop environment, so physical mouse interaction, the actual animated loading overlay, Windows file dialogs, multi-monitor docking, and the final Windows batch launch cannot be click-tested here. `update.bat` retains the Windows off-screen GUI-construction preflight for the target machine.

The optional AI Project Advisor requires a user-configured compatible HTTP endpoint/model. No external AI endpoint was called during the build tests. The feature is provider-neutral, opt-in, and the API key is not persisted.

Cancellation is immediate during GraphVis' own chunked copy loops and at literature-processing checkpoints. A third-party PDF/OCR function already executing inside one library call cannot be forcibly interrupted by a Python thread; cancellation takes effect at the next checkpoint.


## 2026.08.29.7 — Smart Performance / Marker / UI reliability verification

Regression run on the release working tree:

- `python -m compileall -q .` — **PASS**
- `tests/modern_smart_performance_static_smoke.py` — **PASS**
- `tests/themed_marker_render_smoke.py` — **PASS**
- `tests/intelligent_scan_smoke.py` — **PASS** (`17` recommendations; cache/diagnostic checks included)
- `tests/performance_blank_ui_static_smoke.py` — **PASS**
- `tests/compact_studio_ui_static_smoke.py` — **PASS**
- `tests/advisor_thumbnail_ui_static_smoke.py` — **PASS** (`286` graph definitions, `0` missing thumbnails)
- `tests/ui_layout_static_smoke.py` — **PASS** (`30` themes, `84` colormaps)
- `tests/project_intelligence_static_smoke.py` — **PASS**
- `tests/project_context_smoke.py` — **PASS**
- `tests/native_figure_smoke.py` — **PASS**
- `tests/pro_suite_backend_smoke.py` — **PASS**
- `tests/organized_layout_smoke.py` — **PASS**
- `tests/headless_render_smoke.py` — **PASS: 130/130 concrete engines, 0 exceptions**

Representative benchmark with `complete_df_MEC_Master_Results.mat` (200,000 rows, 42 numeric columns) on the build machine:

- data load: ~1.91 s;
- 4D/5D interactive scatter preview (10,000 displayed points, 84 DPI): ~0.23 s;
- 2D heatmap interactive preview: ~0.18 s;
- 5-second Smart scan profile: ~1.20 s, examining the configured 80 promising pairs and returning 15 ranked recommendations.

These timings are diagnostic build-machine measurements, not guaranteed Windows timings. PySide6 is not installed in the build container, so physical mouse/dock/resize/marker interaction and the animated >5 s overlay require final validation on the target Windows desktop. Static Qt-facing source contracts, compilation, renderer output and backend behavior are covered by the tests above.


## 2026.08.29.8 — Graph background / deep scan / export-DPI verification

Current working-tree regression run:

- `python -m compileall -q src/graphvis tools/prepare_graph_previews.py` — **PASS**
- `tests/background_checkpoint_export_static_smoke.py` — **PASS**
- `tests/intelligent_scan_smoke.py` — **PASS** (`17` recommendations, checkpoint save/load/clear, and wide-table budgeted-profile checks)
- `tests/project_context_smoke.py` — **PASS** (excerpt-only startup load + compressed full-text retrieval)
- `tests/themed_marker_render_smoke.py` — **PASS**
- `tests/pro_suite_backend_smoke.py` — **PASS**
- `tests/native_figure_smoke.py` — **PASS**
- `tests/organized_layout_smoke.py` — **PASS**
- all UI/static contract tests listed for 2026.08.29.7 above — **PASS** after updating their expected compact/lazy behavior.
- graph preview assets — **286 original + 286 compact + 286 merged**, with no missing original definitions.

The exhaustive `tests/headless_render_smoke.py` sweep was attempted during this upgrade but exceeded the build-container run window before completing; no renderer exception was reported before that stop. The targeted renderer/backend regressions above passed. The earlier 2026.08.29.7 full-engine result remains a historical result for that release, not a claim that the full sweep was rerun to completion for 2026.08.29.8.

PySide6 is not installed in this build container, so real Windows mouse/splitter interaction cannot be physically click-tested here. The Qt-facing changes compile and are covered by static contracts; final live resize feel should be verified on the target Windows machine.


## 2026.08.29.9 — focus / docking / marker / toolbar verification

Current working-tree regression run:

- `python -m compileall -q src/graphvis` — **PASS**
- all `tests/*static_smoke.py` contracts — **PASS**, including the new `ui_marker_docking_regression_static_smoke.py`
- `tests/intelligent_scan_smoke.py` — **PASS**
- `tests/project_context_smoke.py` — **PASS**
- `tests/pro_suite_backend_smoke.py` — **PASS**
- `tests/native_figure_smoke.py` — **PASS**
- `tests/themed_marker_render_smoke.py` — **PASS**
- `tests/organized_layout_smoke.py` — **PASS**

PySide6 is not installed in the build container, so native Windows Alt-Tab behaviour, live QDockWidget dragging, and physical mouse marker hit-testing cannot be exercised interactively here. The affected Qt-facing paths compile and are covered by static contracts; renderer-side marker persistence/theme tests pass.
