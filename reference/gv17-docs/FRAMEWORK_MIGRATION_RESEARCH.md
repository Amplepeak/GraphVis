# GraphVis UI / Rendering Framework Migration Research

## Recommendation

GraphVis should **not** be rewritten wholesale.  The safest premium-UI path is an incremental migration that keeps the Python MVC/domain layer and publication renderer, while replacing the outer Qt Widgets shell with **PySide6 + Qt Quick/QML + Qt Quick Controls**.  For interactive charts, add a renderer adapter layer so standard high-frequency 2-D/3-D views can use **Qt Graphs** or **PyQtGraph**, specialist GPU scientific views can continue to use **VisPy/PyVista**, and Matplotlib remains the deterministic publication/export backend.

This avoids throwing away the existing loaders, project context, Smart Suite, statistics, fitting, `.gvfig`, export, and 286-graph registry.

## Candidates reviewed

### 1. PySide6 + Qt Quick/QML — preferred UI shell

Qt for Python is the official Python binding for Qt. Qt Quick/QML is Qt's declarative UI stack, designed around a visual scene, animation, input, models/views, and Qt Quick Controls. It is the most natural migration because GraphVis already uses PySide6 and can expose the existing controllers/models directly to QML.

**Why it fits GraphVis**

- incremental migration rather than a rewrite;
- fluid, GPU-friendly interface chrome and transitions;
- good fit for Figma/SimScale-like compact panels;
- retains native Qt windows, multi-monitor behavior, settings, file dialogs, accessibility, and packaging;
- Python backend remains authoritative.

### 2. Qt Graphs — preferred Qt-native standard chart renderer

Qt Graphs is the current Qt direction for 2-D/3-D data visualization and is built for Qt Quick/Qt Quick 3D.  It is suitable for line/scatter/bar/area/pie and standard 3-D scatter/surface/bar views, especially where pan/zoom/resizing must remain fluid.

Qt Charts and Qt Data Visualization should not be new GraphVis targets because Qt has deprecated both in favor of Qt Graphs.

### 3. PyQtGraph — preferred ultra-fast 2-D telemetry/engineering renderer

PyQtGraph remains a strong option for high-frequency 2-D scientific interaction.  It supports view clipping and downsampling and is a good fit for live telemetry, large time-series, fast point inspection, and ROI gadgets.

### 4. VisPy / PyVista — retain for specialist GPU scientific rendering

VisPy is designed for high-performance interactive scientific 2-D/3-D data using OpenGL.  GraphVis already has a VisPy/PyVista path; retaining it for large point clouds, vector/volume scenes and specialist GPU renderers avoids forcing every scientific plot through a single technology.

### 5. Dear PyGui — fast, but not preferred for this migration

Dear PyGui is GPU-accelerated and well suited to dynamic engineering tools.  A complete migration would, however, require rebuilding GraphVis's current Qt docking, dialogs, model/view bindings, menus, accessibility and many desktop conventions.  It is therefore better as a reference for interaction density than as the near-term application shell.

### 6. Slint — attractive, but higher risk today

Slint has a polished declarative UI model and Python integration, but the Python integration is currently beta.  It would impose a much larger boundary rewrite than QML while providing less reuse of GraphVis's current PySide6 infrastructure.

## Proposed migration phases

### Phase A — renderer-neutral interfaces (now / low risk)

1. Keep `PlotSpec` as the canonical visualization contract.
2. Introduce a `RendererBackend` protocol: `set_spec`, `set_view`, `pick`, `render`, `export_snapshot`.
3. Keep Matplotlib as `PublicationRenderer`.
4. Add capability metadata to graph-library entries: `matplotlib`, `qtgraphs`, `pyqtgraph`, `vispy`, `pyvista`.

### Phase B — QML studio shell

1. Rebuild only the main workspace chrome in QML: sidebar, tabs, command bar, search, inspector, status/loading layer.
2. Expose the existing `ApplicationModel` and `ApplicationController` as QObject/QAbstractItemModel adapters.
3. Keep existing QWidget dialogs as transitional windows where useful.
4. Replace heavy dock-title widgets with QML drawers/panels and lightweight pop-out `Window`s.

### Phase C — accelerated 2-D path

1. Move Line/Scatter/Area/Bar/Time Series/Telemetry/ROI to Qt Graphs or PyQtGraph.
2. Reuse the existing LTTB/stratified decimation service.
3. Make picking operate on the displayed decimated indices and preserve exact source-row references.
4. Keep publication export routed to Matplotlib from the same `PlotSpec`.

### Phase D — accelerated 3-D path

1. Standard surface/scatter/bar: Qt Graphs 3D.
2. Volume/vector/mesh/specialist scientific 3-D: VisPy/PyVista.
3. Keep a single GraphVis camera/view-state format so `.gvfig` does not depend on the renderer.

### Phase E — retire QWidget shell selectively

Only after feature parity is proven should the old main QWidget shell be removed.  The model, controller, analysis engines, project format and export path should not need a simultaneous rewrite.

## Decision

**Target architecture:** PySide6/QML shell + renderer adapters + Qt Graphs/PyQtGraph/VisPy/PyVista interactive backends + Matplotlib publication backend.

This gives GraphVis a credible path toward a highly polished, low-latency scientific studio without risking a multi-year all-at-once rewrite.
