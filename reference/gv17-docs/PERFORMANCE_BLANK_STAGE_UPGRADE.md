# Performance and Blank-Stage Upgrade — 2026.08.29.6

This release focuses on perceived latency and workspace density. Startup shows no graph tab. The graph editor appears only after an explicit graph-generation action and can be closed back to a blank stage.

Graph-library PNGs remain accurate release-time renders, but sidebar icons are smaller and lazily decoded. The hover detail card is ephemeral and hidden whenever the pointer leaves the graph list.

Interactive rendering uses a preview-only quality budget (84 DPI, 10k points, 96-cell surface grid by default). These settings never modify the source dataset and are not used for publication export, which continues to use the full data and selected export DPI.

Dock animations were removed, Matplotlib motion redraws are throttled, and disk figure pickling is opt-in. Window geometry, dock/float state and theme are persisted through QSettings.
