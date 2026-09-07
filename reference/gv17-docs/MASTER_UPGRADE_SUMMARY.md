# GraphVis Professional Suite — current master summary

This build preserves the Professional Suite/MVC scientific backend and the dockable 286-graph interface while adding a project-intelligence workflow designed for thesis/research projects.

## Current headline capabilities

- Organized `src/graphvis` source tree and shared Windows runtime reused across application versions.
- Persistent project workspaces under the user's GraphVis data directory.
- **286 selectable graph definitions / 130 concrete renderer engines**.
- 15 application themes and 84 scientific colormaps.
- Broad scientific ingestion, database/cloud connectors, live telemetry, formulas/masks/pivots, hard delete and metadata tools.
- Statistics/ML/signal/fitting/survival/power/GIS/electrochemical analysis services.
- Semantic literature extraction, optical digitization, predictive recreation, `.gvfig`, publication profiles, annotations, GPU preview, 3000-DPI/vector export, workflow/batch tools and embedded consoles.

## Project Intelligence / blank startup

- GraphVis opens on a lightweight blank `Workspace` tab. No scientific figure is rendered automatically and saved graph tabs are restored only on explicit request.
- Project datasets are discovered in the background without opening plots.
- Added persistent named **Project Groups** that link thesis/papers, datasets, scripts and notes.
- `Read Literature / Thesis` stores the source inside the project, extracts/caches semantic context, and saves recovered numeric series under `datasets/literature_extracted`.
- `Add Linked Literature Bundle` clearly represents one main paper/thesis plus directly related supplements and assigns the bundle to a named group.
- `Import Script / Parameters` statically analyzes MATLAB/Python/R/Julia/plain-text workflows for assignments, plot calls and axis/title intent without executing the script.
- Smart Map Suite combines saved optional AI advice, script plot intent, offline literature/project context, dataset recommendations, compatibility checks and automatic axis mapping.
- Optional AI Project Advisor accepts a user-configured OpenAI-compatible endpoint/model/key, sends a compact project summary + schema rather than raw data rows, and does not persist the API key.
- Literature/data copy is chunked with real progress, cancellable, and uses `.graphvis.part` temporary files that are removed if cancelled.
- Loading operations use an animated GraphVis otter, rotating water-arrow arc and swimming bob/tilt motion instead of the generic spinner.

## Architecture

Persistent project context is stored by the Qt-free `ProjectContextStore`. New project context, literature and script-analysis services remain outside the View. UI actions coordinate through the existing MVC application state and scientific services; rendering remains delegated to the renderer stack and worker pool.

## Validation

- compileall: pass;
- project-context smoke: pass;
- project-intelligence static smoke: pass;
- organized/shared-runtime smoke: pass;
- native `.gvfig` round-trip: pass;
- professional backend smoke: pass;
- dockable UI/theme/library static smoke: pass;
- headless render suite: **130/130 renderer engines, 0 renderer exceptions**;
- active branding scan: 0 superseded university-brand references.

See `TEST_REPORT.md` for validation boundaries.
