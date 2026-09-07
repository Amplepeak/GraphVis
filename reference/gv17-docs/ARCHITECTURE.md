# GraphVis MVC architecture

GraphVis boots through a dependency-injected Model-View-Controller graph.

## Model

`models/application_model.py` is the authoritative persistent/application state boundary. It owns the active `ProjectWorkspace`, dataset registry, parameter nicknames, publication profiles, connector profiles and workflow/template stores. Dataset registration/deletion, metadata/formulas, assumptions, derived series, snapshots/templates and project changes are model mutations.

The Model contains no Qt widgets.

## View

`views/main_window.py`, `views/dialogs.py` and `views/pro_suite_dialogs.py` own Qt presentation: windows, toolbars, menus, canvas presentation, selectors, file choosers, floating windows and dialog layout. View-only state includes current selection/tab and geometry.

The View does not instantiate the application Model or Controller. `main_app.py` injects them before the event loop starts.

## Controller

`controllers/application_controller.py` validates user intentions, invokes Model operations and reports status/errors through the View protocol. It also routes the new Statistics/ML/Signal/Calculus/Fitting service calls so Analysis Hub views do not need to own persistent/domain state.

## Services

Reusable numerical/specialized logic remains outside MVC state:

- rendering: `render_core.py`, `surface_estimators.py`, `plotting_engine.py`, `gpu_backend.py`, `graph_library.py`;
- ingestion/connectivity: `data_loader.py`, `data_connectors.py`, `data_organization.py`;
- statistics/ML/signal/fitting: `statistics_engine.py`, `ml_engine.py`, `signal_engine.py`, `fitting_engine.py`;
- literature/digitization: `literature.py`, `literature_semantics.py`, `predictive.py`, `digitizer.py`;
- GIS: `gis_engine.py`;
- automation/reporting: `workflow_engine.py`, `batch_processing.py`, `embedded_console.py`;
- reproducibility/export: `native_figure.py`, `export.py`, `export_code.py`, `publication_profiles.py`;
- reliability: `app_logging.py`, `diagnostics.py`.

Long-running parsing/rendering continues to use worker tasks so the GUI event loop remains responsive. Scientific surface estimation is deliberately Qt-free: `surface_estimators.py` owns coordinate transforms, structured/scattered solvers and masks; `render_core.py` owns Matplotlib normalization/shading; `plotting_engine.py` owns worker scheduling, progressive interaction and cancellation. This separation keeps expensive numerical work headless-testable and prevents solver code from coupling to widgets.

## Project Intelligence boundary

`graphvis.core.project_context.ProjectContextStore` is Qt-free and owns the persistent mapping between named project groups, literature sources, datasets, scripts and saved advisor output. Literature extraction is handled by `graphvis.literature.extractor`, and imported scripts are statically inspected rather than executed. The View exposes group/literature/script controls and invokes Smart Map Suite; graph compatibility/axis mapping remain data/rendering services. This keeps project context persistent and testable without coupling it to the widgets.

Startup intentionally separates **dataset discovery** from **plot rendering**. The View can scan/register project datasets in background tasks while `ScientificPlotCanvas` remains in a render-free blank state until the user requests a graph.
