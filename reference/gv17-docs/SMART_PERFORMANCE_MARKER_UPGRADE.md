# GraphVis 2026.08.29.7 — Smart Performance, Marker & UI Reliability Pass

## UI / layout

- graph-library rows have explicit breathing room while remaining compact;
- hover details are no longer pinned after the pointer leaves the graph library;
- interactive figures inherit the active GraphVis theme instead of always using a stark white canvas;
- tab-scroll controls receive explicit left/right icons and hit targets;
- splitter dragging uses non-opaque/rubber-band resizing;
- Matplotlib canvas resizing defers expensive redraw work until the resize gesture settles;
- high-frequency style/surface sliders render on release rather than while the thumb is being dragged.

## Startup and stale artifacts

- dataset loading/selection never automatically renders a graph;
- dataset switches clear the previous canvas, overlays, markers and view state;
- missing priority/canonical variables are no longer inserted as disabled hard-coded selector entries;
- startup remains a true blank stage until Preview or an explicit graph selection.

## Loading feedback

`LoadingOverlay` is now globally delayed by five seconds. Fast actions complete with only status-bar feedback; long operations automatically reveal the animated GraphVis otter/water indicator and retain cancellation support.

## Point markers

- single click: temporary nearest-visible-point red ring;
- point-picking data is cached when the figure is installed rather than rebuilt from pandas on every click;
- screen-coordinate transforms are cached for the current camera/view;
- double click: add a persistent red ring;
- double click an existing ring: remove it;
- right click a persistent ring: `Delete marker` / `Save marker`;
- marker state is stored in `PlotSpec.metadata` and therefore survives editable figure serialization.

## Smart Suite

- user thinking budget supports seconds or minutes (5–600 seconds total);
- scan breadth, sample size and pair count scale with the selected time budget;
- scan cache records the budget and is reused only when it is deep enough for the current request;
- graph recommendations include graph-specific axes, axis scale, colormap and series color;
- project-group literature context remains part of the scan fingerprint;
- previously saved optional AI Project Advisor results are folded into Smart Suite without making an automatic network request;
- Smart Suite limits repeated identical axis signatures so graph diversity is not just cosmetic.

## Performance audit

Using `complete_df_MEC_Master_Results.mat` (200,000 rows / 42 numeric columns) in the build environment:

- load: ~1.91 s;
- 4D/5D scatter preview at 10,000 displayed points: ~0.23 s;
- 2D heatmap interactive preview: ~0.18 s;
- 5-second Smart scan profile: ~1.20 s to inspect its configured 80 promising pairs and return 15 recommendations.

These are headless build-machine timings, not promises for the user's Windows hardware. They confirm that remaining perceived latency is dominated more by Qt/Matplotlib widget repaint/layout behavior than by these representative render calls.
