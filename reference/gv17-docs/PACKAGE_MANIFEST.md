# GraphVis Package Manifest — 2026.08.29.13

GraphVis 15 performance architecture release. New release-specific engineering notes: `docs/GRAPHVIS_15_PERFORMANCE_ARCHITECTURE.md`; regression: `tests/graphvis15_performance_smoke.py`.

# GraphVis Package Manifest — 2026.08.29.9

Files (excluding Python caches): **996**

## Top-level groups
- `GraphVis.py`: 1 file(s)
- `Install_GraphVis.bat`: 1 file(s)
- `README.md`: 1 file(s)
- `Repair_GraphVis.bat`: 1 file(s)
- `Start_GraphVis.bat`: 1 file(s)
- `VERSION`: 1 file(s)
- `archive`: 1 file(s)
- `assets`: 861 file(s)
- `config`: 1 file(s)
- `debug.bat`: 1 file(s)
- `docs`: 24 file(s)
- `examples`: 1 file(s)
- `requirements`: 13 file(s)
- `run_app.bat`: 1 file(s)
- `src`: 57 file(s)
- `tests`: 16 file(s)
- `tools`: 12 file(s)
- `update.bat`: 1 file(s)
- `update.sh`: 1 file(s)

## Graph / UI assets
- `assets/graph_previews/`: 286 original render-engine thumbnails.
- `assets/graph_previews_compact/`: 286 tightly cropped graph-library previews.
- `assets/graph_previews_merged/`: 286 cropped RGBA previews whose near-white outer background can merge with the UI.
- Graph catalogue: 286 selectable definitions / 130 concrete render engines.
- Themes: 30.
- Colormaps: 84.

## 2026.08.29.9 release-specific files / tests
- `docs/UI_FOCUS_MARKER_DOCKING_HOTFIX.md`: startup focus, theme isolation, true detachable docking, preview spacing, canvas-swap and marker reliability notes.
- `docs/GRAPH_BACKGROUND_DEEP_SCAN_UPGRADE.md`: graph-background, deep-scan, literature-cache, resize and unrestricted-DPI implementation notes.
- `docs/SMART_PERFORMANCE_MARKER_UPGRADE.md`: prior latency, loading, marker and Smart Suite changes.
- `docs/FRAMEWORK_MIGRATION_RESEARCH.md`: researched incremental UI/rendering migration plan.
- `tests/ui_marker_docking_regression_static_smoke.py`: startup focus, dock lifecycle, toolbar theme isolation, exact marker and canvas-swap regression.
- `tests/background_checkpoint_export_static_smoke.py`: graph-background, resumable scan, literature-cache and unrestricted-DPI regression.
- `tests/modern_smart_performance_static_smoke.py`: UI/performance/Smart Suite/marker contract regression.
- `tests/themed_marker_render_smoke.py`: interactive theme background and persistent marker renderer regression.

Runtime caches, `__pycache__`, `.pyc`, user projects, logs and the shared `.venv` are excluded from the release archive.
