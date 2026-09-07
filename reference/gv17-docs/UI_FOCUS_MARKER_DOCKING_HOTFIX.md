# GraphVis 2026.08.29.9 — UI Focus, Marker & Docking Hotfix

This pass addresses the Windows interaction issues visible in the 2026-08-29 screenshots.

## Startup focus
- The otter startup splash is no longer `WindowStaysOnTop`, so Alt-Tab can place other applications above it.
- If the user Alt-Tabs to another application while GraphVis is loading, the completed main window is shown without taking focus back.

## Graph-control theme isolation
- Interactive graph background colours are now applied only to the plot viewport, not to the parent `ScientificPlotCanvas` widget.
- Graph Controls and Interactive strips have explicit theme-owned panel backgrounds and text colours, so a light/white graph canvas cannot leak into dark UI controls.

## Docking
- Detachable graph panels now use a dedicated `DetachableDockWidget`.
- A dock returns inline only when explicitly closed. It no longer redocks merely because Qt emits `visibilityChanged(False)` while tabifying/minimising/changing dock state.
- Embedded panel headers can be double-clicked to pop out in addition to the existing rectangles button; floating panels can dock at any main-window edge.

## Responsiveness / visual switching
- Matplotlib canvases preserve static painted contents during resize and postpone the expensive final resize redraw until motion settles.
- New graph figures are sized to the current plot viewport before being shown and replace the old canvas atomically. This removes the brief small-then-grow visual flash when switching graph types.

## Graph library
- Compact sidebar icons are reduced to 46×28 px with 40 px rows, and the preview PNGs include transparent padding so adjacent white previews cannot touch.
- Uniform-row optimisation is disabled for the lazy tree so per-entry size hints are honoured and visible vertical breathing room remains between previews.
- Merged preview assets retain alpha transparency so theme backgrounds can show through.

## Marker behaviour
- 2-D markers use Matplotlib's exact inverse axis transform (`event.xdata`, `event.ydata`) and therefore lock at the cursor tip instead of snapping to the nearest data sample.
- The coordinate inspector shows 12 significant digits, with 17-digit values in its tooltip.
- 3-D cursor positions remain nearest-displayed-point based because a 2-D screen position does not uniquely define X/Y/Z.
- Marker hit testing first uses the rendered Matplotlib marker artist and then a 20–22 px screen-distance fallback, improving HiDPI/deferred-resize reliability.
- Every graph right-click menu now includes Delete marker, Delete all markers and Save marker actions. Line/annotation hits no longer replace those marker actions.
