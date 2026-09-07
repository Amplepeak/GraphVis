# GraphVis Dockable UI & Graph Library Upgrade

This build focuses on the layout problems visible in the supplied screenshots.

## Layout changes
The former hand-clamped sidebar splitter has been replaced by a native `QDockWidget` control sidebar. It can be resized with the normal Qt divider, moved to either side, floated, closed, and restored. Every major sidebar section is additionally wrapped by a detachable/collapsible panel, so an individual Graph Library, Mapping, Styling, Limits, etc. section can be moved to a separate window.

Inside each graph tab, Graph Controls and Interactive Controls are independent collapsible/detachable panels. The graph and lower controls use a vertical `QSplitter`, making the boundary genuinely draggable. Both the top action row and lower live-control row use horizontal scroll areas on small windows so controls are never silently clipped.

The graph tab bar itself remains movable and is never removed by Focus Graph or Presentation layout presets.

## Graph selection
The catalogue contains 286 selectable definitions mapped to 130 concrete render engines. All categories begin closed. Typing in the graph search field produces fuzzy suggestions immediately. Hovering a graph entry displays an on-screen preview card containing the visual hint, scientific description, concrete renderer, and compatibility status.

## Themes
Core themes: Light, Glass, Graphite, Midnight, High Contrast.

Colour themes: Red, Orange, Yellow, Lime, Green, Teal, Cyan, Blue, Purple, Magenta.

The old duplicate GraphVis Light / Scientific Light names migrate automatically to Light.

## Colormaps
The palette menu now contains 84 named maps covering perceptually uniform, sequential, diverging, cyclic, seasonal/shading, terrain/ocean/field, categorical and specialized families.
