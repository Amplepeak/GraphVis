# GraphVis Compact Studio UI

The Compact Studio pass reduces interface chrome so graphs and scientific content dominate the workspace. It follows general compact-workspace patterns used by design and engineering applications without copying any third-party application assets.

## Panel chrome

Inline panels use a 22–24 px header containing a chevron, a six-dot grip indicator and the outlined-rectangles pop-out control. Graph Controls and Interactive controls hide their title text on-canvas; hover tooltips identify the panel.

## Graph controls

The expanded graph strip is a single compact row: Preview, scale, grid, coordinate inspector, annotation/gradient/reset tools, animation controls when applicable and Export. Secondary actions are icon-only with tooltips.

## Bottom controls

Interactive controls default collapsed. A dotted splitter handle provides continuous vertical resizing. Expansion state and height are persisted. The panel can still be floated/docked.

## Toolbars

Scientific Tools and Window/Layout use 18 px vector icons inside 28 px buttons. Labels remain available through native tooltips and QAction text, preserving accessibility and menu integration.

## Sidebar

The main control dock defaults to about 360 px. A migration flag reduces previously saved oversized widths once while preserving subsequent user resizing. Sidebar panel headers share the same compact docking language.

## Theme density

All existing themes remain available, but shared QSS now uses 9 pt controls, tighter menus/tabs/inputs, 8 px scrollbars, subtle divider-only panel borders and compact status/dock chrome.
