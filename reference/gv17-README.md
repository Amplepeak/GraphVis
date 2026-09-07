# GraphVis

## GraphVis 15 — native matrix fast path, split compute/render pipeline, scientific slicing

Version **2026.08.29.13** is the high-density surface-performance architecture release. Pre-gridded N×M kinetic sweeps now render through a native matrix path instead of being flattened, rediscovered and triangulated; flat XYZ Cartesian sweeps use vectorised reconstruction; scattered geometry reuses cached KD-tree/Delaunay plans; surface caches are byte-aware; numerical surface work runs on a separate compute pool; and HDF5/MAT-v7.3 matrices are lazy until selected.

Scientific field parity also expands with independent contour overlays, click-to-extract orthogonal 1-D cross sections, topology-aware solver-dropout bridging with optional imputation provenance, Clough–Tocher and monotone PCHIP estimators, Log10 response interpolation, and adjustable normalized X/Y metric anisotropy. Pure colormap/grid/surface-alpha changes can update existing artists without rebuilding the surface. See `docs/GRAPHVIS_15_PERFORMANCE_ARCHITECTURE.md`.

## GraphVis 14 — deliberate controls, project-safe deletion, MATLAB-style surface polish

Version **2026.08.29.12** is a workflow/performance pass built around large scientific projects and high-density MEC parameter sweeps.

- **Tactical apply controls:** graph-library choices and plot-setting dropdowns are staged until their adjacent `▶` button is pressed. **Apply all pending controls** commits the sidebar in one render; the Interactive strip has its own `▶ All`. This prevents accidental expensive redraws while browsing options.
- **Faster graph browsing:** selecting/searching a visualisation now scrolls the Graph Library to the result, highlights it, and stages it without rendering. Rendering occurs only after deliberate Apply.
- **Safe dataset removal:** Remove unregisters the dataset and moves the project copy from `datasets` to `detached_datasets` by default. Restore moves it back. Permanent deletion requires two explicit checkboxes and applies only to the project-local copy; GraphVis does not delete the original external import source.
- **Real Project Groups:** the group dialog supports multiple named groups with independent checklists for literature, datasets and scripts. The same file may belong to multiple groups without being moved or duplicated.
- **Simplified context ingestion:** normal literature uses **Add Literature…**; optional analysis/parameter context uses **Add Analysis Script…**. The paper+supplement bundle remains an advanced File-menu action rather than a duplicate-looking sidebar button.
- **Hover help:** sidebar controls now have concise tooltips; purpose-written X/Y/Z, scale, surface, estimator and clipping help remains more specific.
- **Noise Off:** the Interactive Noise selector includes `Off`, `Z-Score` and `IQR`.
- **Surface-domain control:** extrapolation includes convex-hull masking, nearest fill, log-aware IDW full-domain extension, linear+nearest full rectangle and edge-clamped full rectangle. Failed ODE points can use compact sample-cell, conservative local, Voronoi, or fit-through failure footprints.
- **MATLAB-style surface preset:** applies Turbo, continuous interpolated shading, compact failed-point handling, full rectangular edge fill and publication-style field presentation. Auto surface presentation adds a useful response-vs-axes title, tight field extents, outward ticks and suppresses the generic dashed grid.
- **White-hole fix:** isolated NaN/Inf solver coordinates no longer own large Voronoi-sized blank regions by default. `Sample cell only` preserves a compact failure footprint while surrounding supported surface remains interpolated.
- **Loading feedback:** startup uses real staged percentages. Long-operation progress no longer appears frozen at 20%; parsers without granular progress show a clearly approximate moving percentage until their next real checkpoint.
- **Swimming mascot:** both startup and long-operation loaders animate the otter itself over water/coral rather than rotating the whole logo. Thirty procedural variants are available and immediate repeats are avoided.
- **Windows branding:** GraphVis sets a stable Windows AppUserModelID, assigns the GraphVis icon to the QApplication/main window, ships a multi-resolution `.ico`, and install/update refreshes Desktop and Start Menu shortcuts. `Create_Desktop_Shortcut.bat` is also provided for manual refresh.

See `docs/GRAPHVIS_14_QOL_PERFORMANCE.md` for implementation details and validation boundaries.

GraphVis is a scientific visualisation and analysis desktop application with an organized source tree, a shared Python runtime, persistent project workspaces, a 286-entry graph catalogue, and project-aware literature/data workflows.



## Advanced surface estimation and MATLAB-style field rendering

Version **2026.08.29.11** adds a dedicated SciPy-backed surface engine for mixed linear/log kinetic sweeps. The **Surface estimation & noise control** panel now provides structured nearest/bilinear/bicubic/B-spline methods; Delaunay, Sibson natural-neighbour, IDW and Modified Shepard scattered estimators; TPS/multiquadric/Gaussian RBFs; Ordinary Kriging; MLS; and 2-D LOESS/LOWESS. Log10 X/Y axes are interpolated in normalized log-coordinate space while the returned plotting mesh stays in physical coordinates.

Failed NaN/Inf simulation coordinates are excluded from fitting and preserved as transparent or configurable fallback-colour masks. Colorbar clipping supports automatic/explicit `min`, `max` and `both` extensions so outliers retain terminal colormap colours. Continuous Gouraud shading and discrete `contourf` bands are both available. Native N×M matrices can use compatible MAT/HDF auxiliary axis vectors. Progressive drag previews, estimator-vs-smoothing cache separation, and cooperative cancellation reduce UI latency on heavy sweeps. PDF/SVG export now offers hybrid vector-text/raster-field output or a full-vector surface. See `docs/SURFACE_ESTIMATION_UPGRADE.md`.

## Independent X / Y / Z scaling

Version **2026.08.29.10** replaces the former global workspace scale selector with independent **Linear / Log10** controls beside the X, Y and Z/Response mapping selectors.

- X and Y can be switched independently between linear and base-10 logarithmic axes.
- Log10 axes use standard decade major ticks and logarithmic minor ticks, formatted as mathtext powers such as `10^-1`, `10^0`, `10^1`.
- On 2-D Heatmap and 2-D Contour views, Z/Response Log10 uses Matplotlib `LogNorm`: the colour gradient is logarithmic while the physical X/Y coordinates are unchanged.
- On 3-D plots, Z Log10 applies to the actual Z axis; surface colouring by Z uses matching logarithmic normalization when appropriate.
- Old saved projects and legacy `loglog`, `semilogx` and `semilogy` graph-library presets are translated to the new independent axis state when loaded.
- Turbo and Jet are explicit standard Matplotlib colormap registry entries in the GraphVis palette.

## Compact Studio interface

Version **2026.08.29.9** builds on the Compact Studio interface and introduces a substantially quieter desktop layout inspired by modern design and CAE workspaces. The scientific canvas is now the visual priority: panel chrome is 22–24 px high, side/tool bars use compact icon buttons, and heavy framed control blocks have been replaced with subtle dividers.

- **Graph Controls** and **Interactive** use an arrow + six-dot grip + pop-out control instead of a large title bar. The graph-panel titles are hidden on-canvas and available through tooltips.
- The graph action row is approximately 34 px high; secondary commands are icon-only with tooltips.
- The interactive bottom panel starts collapsed by default, can be resized with a dotted splitter grip, remembers its height, and can be floated.
- Scientific Tools and Window/Layout toolbars are 28 px icon toolbars rather than text-button stacks.
- The main controls dock migrates old oversized widths to a ~360 px compact default on first use of this release.
- The GraphVis brand block, Simple/Advanced selector, tabs, menus, fields, scrollbars and status bar all use reduced spacing/padding.

All existing graph, analysis, project-intelligence, docking and export functionality remains available.

## Start GraphVis

### First installation
Double-click **`Install_GraphVis.bat`**. It prepares the shared runtime and installs the normal recommended feature set.

If an earlier install was interrupted, use **`Repair_GraphVis.bat`**. Repair installs only the core packages required to start GraphVis, so optional scientific packages cannot block startup.

### Normal use
After installation, double-click **`Start_GraphVis.bat`**. `run_app.bat` is the underlying launcher and does not install packages.

GraphVis versions reuse the same Windows runtime:

`%LOCALAPPDATA%\GraphVis\Runtime\.venv`

Mutable research data is kept separately under:

`%USERPROFILE%\Documents\GraphVis`

This means a new GraphVis ZIP normally does not require another complete Python environment.

## Blank startup and closable workspace

GraphVis now starts on a **true empty light graph stage with no graph tab visible**. No plot is rendered and the Workspace editor is not attached until a graph is explicitly selected/generated. Closing the Workspace/last graph tab returns to the same configurable graph-background stage. Project datasets are still discovered in the background without generating plots.

To restore saved graph tabs deliberately, use **File → Restore previous graph tabs**. The View → Workspace layout menu also provides **Clear graph workspace**.



## Responsiveness and fast preview mode

Version 2026.08.29.9 retains the fast-preview work and adds another UI/render latency pass:

- Graph-library thumbnails use tightly cropped compact assets by default; a View option can merge their near-white background into the UI. The larger preview/details card exists only while an entry is actually hovered.
- Thumbnail icons are loaded lazily and cached in memory instead of decoding hundreds of PNGs on every graph-tree rebuild.
- Graph-library row height hints are honoured so the smaller 46×28 px previews keep visible spacing; expand/collapse animation is disabled, and unchanged capability/recommendation states no longer rebuild the graph tree.
- Qt animated docking is disabled; dock positions, floating state, main-window geometry and the last selected UI theme are persisted.
- Matplotlib mouse/touch redraws are coalesced to roughly 60 fps instead of requesting a repaint for every pointer event.
- Interactive graph renders default to 84 DPI, a 10,000-point preview budget and a 96-cell surface grid. Export continues to re-render from full data at the requested publication quality.
- Heavy cross-session Matplotlib figure pickling is now opt-in; the fast in-memory cache remains enabled.
- Settings exposes Fast interactive preview, point budget, interactive DPI and optional cross-session figure caching.

The theme catalogue now contains **30 themes**: 10 core themes and 20 colour-accent themes.

## Smart performance, loading and point markers

- Selecting/loading a dataset does **not** render automatically. Dataset changes clear stale overlays, markers and previous graph content and return the editor to a blank/manual state.
- Background operations and graph renders use a **five-second delayed** GraphVis otter/water loading overlay. Fast operations do not flash a spinner; long operations gain visible progress and cancellation where the underlying worker is cancellable.
- Interactive plot backgrounds default to a soft **Light** graph workspace and can be switched in **View -> Graph background** to White, Dark, Theme or Custom, with brightness and theme-blend controls. Publication/vector/raster export keeps its controlled publication background.
- Tab overflow controls use explicit left/right arrow icons and larger hit targets.
- Splitter motion is rubber-band/non-opaque, Matplotlib resize work is deferred until the resize gesture settles, and expensive surface/style sliders render on release rather than continuously.
- Point inspection now caches the displayed point population and display transforms. Single click shows a temporary red ring at the exact 2-D cursor/axis coordinate; double click locks/removes a persistent ring; every graph right-click menu exposes **Delete marker**, **Delete all markers**, and **Save marker** when applicable. Persistent markers are serialized in editable `.gvfig` state.

### Smart Suite thinking budget

Smart Suite and Scan Dataset include a user-selectable **Seconds / Minutes / Hours** analysis budget (up to 72 hours), with resumable project checkpoints. Short budgets sample rows/columns early for responsiveness; very long budgets progressively lift those limits. Use **Tools -> Clear Smart Suite saved scans** to discard cached scan state. The scanner expands column breadth, pair sampling and nonlinear relationship checks as the budget increases, honors cancellation/deadlines, and caches sufficiently deep results. Each recommendation has its own mapping plus suggested axis scale, colormap and series color. Saved optional AI Project Advisor results can refine the ranking without silently making a network request.

For the renderer/UI modernization plan, see `docs/FRAMEWORK_MIGRATION_RESEARCH.md`. The recommended direction is an incremental PySide6 **Qt Quick/QML** shell with renderer adapters, not a wholesale rewrite.


### Graph background and high-DPI export

Use **View -> Graph background** to switch the interactive workstation between Light (default), White, Dark, Theme and Custom colours. Brightness is adjustable from 0–100 and the graph background can be blended toward the current UI theme. Default graph labels/ticks/spines automatically switch to a readable contrast colour; explicit styling overrides are preserved.

Single export, batch export, default export settings and Publication Ready profiles accept typed positive DPI values above 3000. GraphVis does not hard-block extreme DPI requests; it reports estimated peak RAM and raw image size so the user can decide whether the output is practical.

## Intelligent graph selection and true previews

The inline Graph Library now uses **real pre-rendered GraphVis thumbnails**. During release packaging, a comprehensive reference dataset is passed through the same `render_core.build_figure()` path used by normal graphs. The resulting PNG previews are shipped under `assets\graph_previews`, so hovering a graph shows the actual 2-D/3-D/field/statistical appearance without rendering hundreds of figures at application startup.

Graph selection is single-path and cancellable: clicking a tree entry, clicking a fuzzy-search suggestion, or pressing **Enter** on the first search suggestion applies the selected engine, highlights it, loads a graph-specific cached axis mapping when available, and schedules exactly one debounced background render. Superseded renders are cancelled.

### Scan Dataset

Use **Scan Dataset** in the Graph Library section to run the deeper mapping advisor. It profiles variables, detects parameter/response roles, scores Pearson/Spearman/nonlinear dependence, checks distributions and matrices/volumes, and ranks graph-specific axis mappings instead of reusing the same X/Y pair for every recommendation. Results are cached under the active project `context\scans` folder and keyed by both dataset fingerprint and literature-context fingerprint.

Use **Scan + Literature…** to attach/read a PDF/text source first. Literature-inferred variables and plot intent are matched against real dataset columns and used to boost or refine the scan.

## Docking and graph-area resizing

The graph canvas and interactive-controls area use a true vertical splitter. The bottom controls may be dragged down to a compact header height, collapsed, or popped out. The chosen bottom-panel height is remembered. Control/Object panels use explicit stacked-rectangle pop-out controls; Graph Controls and Interactive Controls use the same pop-out convention. The Scientific Tools and Window & Layout toolbars can also be popped out, and application menus are tear-off enabled.

**View → Workspace layout → Reset docks & panels** returns temporary panel docks and toolbars to the default Analysis workspace.

## Publication Ready built-ins

Publication Ready ships with editable built-in baselines for **Nature**, **Science/AAAS**, **IEEE**, **Elsevier**, and **APA 7**, including single/double-column variants where useful. They configure dimensions, font hierarchy, line weights, raster DPI and color-space defaults; journal-specific current author instructions should always take precedence.

## Project intelligence workflow

The recommended workflow for a thesis or research project is:

1. **Read Literature / Thesis…** – GraphVis copies the document into the active project, extracts/caches its text, captions, experimental context and any recoverable numeric series. The document is remembered on later starts.
2. **Add Dataset…** – import a dataset once. It is copied to the project dataset folder and is automatically discovered on future starts.
3. **Project Groups…** – link the thesis/papers, datasets and optional scripts that belong to the same study or experiment. Groups can be named, for example `Thesis main study`, `Experiment 2`, or `Validation cohort`.
4. **Smart Map Suite from Active Group** – GraphVis combines dataset structure, literature context, imported analysis scripts and saved optional AI advice to recommend and open compatible graphs.

### Linked literature bundles

**Add Linked Literature Bundle…** is for *one* paper/thesis plus files that directly supplement it. Select the related files together and assign the bundle to a named Project Group. Use separate Project Groups when files belong to different experiments/studies.

### MATLAB / Python / R / Julia scripts

Use **Import Script / Parameters…** to paste or import analysis scripts. GraphVis does **not execute the script**. It reads assignments, comments, plotting calls and axis/title labels to infer analysis/plot intent for Smart Map Suite.

### Optional AI Project Advisor

**Tools → AI Project Advisor…** can send a compact project summary and dataset schema to a user-configured OpenAI-compatible chat-completions endpoint. It is optional. Raw dataset rows are not sent by this feature, the API key is not persisted, and GraphVis still has offline project-context recommendations when no AI service is configured.

## Cancellable literature/data ingestion

Long file copies, literature extraction, OCR and background imports display the GraphVis **swimming otter** progress overlay. The water-arrow arc rotates behind the otter while the mascot gently bobs/tilts. A **Cancel** button is available for cancellable operations.

Project copies are written to a temporary `.graphvis.part` file and committed only after the copy completes. Cancelling a copy removes the incomplete temporary file.

## Project storage

Each project has explicit subfolders, for example:

```text
Documents\GraphVis\Projects\MyProject\
├── datasets\
│   └── literature_extracted\
├── documents\
│   ├── literature\
│   └── scripts\
├── digitized\
├── figures\
├── exports\
├── reports\
├── snapshots\
├── templates\
├── workflows\
├── connectors\
├── publication_profiles\
├── settings\
└── context\
```

Dataset discovery recursively scans the project's `datasets` tree, including literature-extracted datasets. Literature and script context is indexed in `context`, so those sources do not need to be selected again each session.

## Source tree

```text
GraphVis\
├── GraphVis.py
├── Start_GraphVis.bat
├── Install_GraphVis.bat
├── Repair_GraphVis.bat
├── src\graphvis\
│   ├── analysis\
│   ├── automation\
│   ├── controllers\
│   ├── core\
│   ├── data\
│   ├── literature\
│   ├── models\
│   ├── rendering\
│   └── views\
├── assets\branding\
├── requirements\
├── docs\
├── tests\
└── tools\
```

See `docs/INSTALLATION_AND_LAYOUT.md` and `docs/PROJECT_INTELLIGENCE_WORKFLOW.md` for more detail.

## Useful maintenance shortcuts

- `debug.bat` – inspect the shared runtime and persistent logs.
- `tools\install_full_features.bat` – install optional GPU/GIS/AI/database/instrument dependencies.
- `tools\open_user_data.bat` – open the shared GraphVis research-data folder.
- `tools\open_logs.bat` – open persistent logs.
- `tools\archive_version.bat` – archive the current application version.
- `tools\backup_user_data.bat` – back up research/project data separately from the recreatable runtime.
