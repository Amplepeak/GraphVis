# Intelligent Advisor, True Thumbnails, Docking & Publication Upgrade

## Graph selection
All graph-library selection surfaces converge on the same handler. The hidden compatibility combo is updated with signals blocked, cached graph-specific mappings are applied, and only one debounced render is requested. Search Enter selects the top fuzzy suggestion.

## True preview assets
`tools/generate_graph_thumbnails.py` builds a comprehensive reference `Dataset` and invokes `render_core.build_figure()` for every graph engine/scale combination. The 240×150 previews are stored in `assets/graph_previews`. Alias entries that map to the same concrete renderer reuse the corresponding real rendering while retaining entry-specific asset filenames.

## Deep Scan Dataset advisor
`analysis/intelligent_scan.py` is Qt-free and profiles variable semantics/statistics, bounded pairwise correlations and mutual information, parameter/response structure, distributions, matrices/volumes, and optional literature semantic variables. Recommendations contain explicit per-graph mappings and reasons. Cache filenames include the dataset and literature fingerprints.

## Docking
Control and Object Manager docks use explicit pop-out title bars. Detachable sections create real `QDockWidget` instances. Graph top/bottom controls remain collapsible and detachable, while the graph/bottom divider persists a user-selected height. Toolbars can be moved or popped out and Reset Layout re-docks temporary panels.

## Publication Ready
Built-in profiles cover Nature, Science/AAAS, IEEE, Elsevier and APA 7 baselines. Profiles remain user-editable/persistable; target journal instructions take precedence over generic publisher baselines.
