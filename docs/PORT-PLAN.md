# GraphVis 17 -> 18 native port plan

Decision (6 Sep 2026): port GraphVis 17 completely to the native C++/QML/Rust
stack. This supersedes `reference/gv17-docs/FRAMEWORK_MIGRATION_RESEARCH.md`,
which recommended an incremental PySide6 + QML shell over the existing Python
domain layer instead. That trade-off is recorded here deliberately: the native
route costs far more work, and GraphVis 17's 135 engines are written
against Matplotlib's Axes API across 473 call sites in `render_core.py` and
`plotting_engine.py`. The publication profiles themselves are portable:
`core/publication.py` is pure data (dpi, font family, pt sizes, line widths,
mm figure widths), not Matplotlib logic.

> **Two catalogues, two sets of numbers.** GraphVis 17's catalogue holds 318
> entries in 30 categories drawn by 135 engines — executing
> `reference/gv17-src/graphvis/rendering/graph_library.py` gives exactly those
> three figures. GraphVis 18's has since grown to **2,116 entries in 46
> categories drawn by 434 engines**, through added domain packs and axis-scale
> variants.
>
> Those v17 figures were copied into four source comments and a menu string
> that were describing **v18** — so the program told the reader it had 318
> entries and that Qt 2-D drew "the 203 catalogue engines", both of which
> stopped being true without anyone changing them. `test_numbers.py` now
> recounts the catalogue's declared header against its own contents and fails
> if any present-tense claim in the source quotes a size the catalogue has
> outgrown. Where a figure below describes v17, it says so.

## What has to move

| Area | GraphVis 17 | Size | v18 status |
|---|---|---|---|
| Graph catalogue | `rendering/graph_library.py` | 37 KB, 318 entries / 30 categories / 135 engines | **ported and extended** — `config/graph_catalogue.json`, now 2,116 / 46 / 434 |
| Thumbnails | `assets/graph_previews*` | 318 x 3 sets | **ported** — `assets/` |
| Plotting engine | `rendering/render_core.py`, `plotting_engine.py` | 231 KB + 168 KB | not started |
| Surface estimation | `rendering/surface_estimators.py` | 59 KB | not started |
| Colormaps / decimation / GIS / GPU | `rendering/*` | ~40 KB | not started |
| Export + publication profiles | `rendering/export.py`, `export_code.py` | 27 KB | not started |
| Main UI, docking, 30 themes | `views/main_window.py`, `widgets.py`, `themes.py` | 309 + 87 + 19 KB | shell only |
| Smart Suite / Scan Dataset | `analysis/intelligent_scan.py` | 36 KB | **engine ported** to `services/python`; no UI |
| Other analysis | `analysis/*` | 12 modules | **ported** to `services/python` |
| Literature extract / digitise | `literature/*` | 61 KB | partly ported |
| Project workspaces / groups | `core/project_context.py`, `workspace.py` | 45 KB | not started |

Roughly 22,000 lines of Python have no native equivalent yet.

## Renderer decision (settled 6 Sep 2026): adapter layer, two backends

All 434 catalogue engines are written **once** against a single internal
drawing interface, `PlotBackend`. Backends implement that interface. The
existing `Renderer` selector in `TopBar.qml`, bound to
`AppController::rendererMode`, chooses which one draws.

This follows `reference/gv17-docs/FRAMEWORK_MIGRATION_RESEARCH.md`, which
recommends "a renderer adapter layer so standard high-frequency 2-D/3-D views
can use Qt Graphs or PyQtGraph, specialist GPU scientific views can continue to
use VisPy/PyVista".

| Backend | Draws | Status |
|---|---|---|
| `QtPlotBackend` | the 2-D catalogue, all axes/text/legends, and every vector export | first to ship |
| `WgpuPlotBackend` | fast path for very large 2-D layers (scatter density, hexbin) | later, opt-in |
| VTK / native viewport | 3-D, field and volume scenes | already present |

### Why Qt draws 2-D first

- wgpu draws triangles and has no text. Font shaping, mathtext for log decade
  labels (`10^-1`), tick placement, legends, dashed joins/caps, marker glyphs
  and hairline anti-aliasing all exist in Qt and would be written from scratch
  in wgpu.
- Publication output must be vector PDF/SVG with embedded fonts; GraphVis 17
  sets `pdf.fonttype = 42` for exactly this. wgpu is a rasteriser, so a wgpu
  path needs a second, independent scene-to-vector exporter kept
  pixel-consistent with the screen. `QPainter` draws to the screen and to
  `QPdfWriter`/`QSvgGenerator` from the same code.
- wgpu's advantage is million-point throughput, but the 2-D interactive path is
  already decimated to a 10,000-point preview budget and most of the catalogue
  is bars, boxes, violins, histograms, contours and error bars.

### Rules

1. The interface is designed to Qt's capabilities, not to the lowest common
   denominator of Qt and wgpu.
2. Vector export always goes through `QtPlotBackend`, whatever the user has
   selected. The selector governs interaction only.
3. A heavy 2-D layer may be rendered by the native viewport with Qt drawing
   axes, labels and legend over the top. That is a per-engine choice, not a
   second implementation of the engine.

### Cost accepted

Two backends must be implemented and kept visually consistent. The toggle adds
work rather than saving it; it buys large-data performance later without
rewriting engines.

## Order of work

**Phase 1 — catalogue and library UI. Done.**
`config/graph_catalogue.json` (318 entries at the time; 2,116 now), all
thumbnails, `AppController`
catalogue loader with a port of `graph_library.fuzzy_score`, and a
`GraphLibrary.qml` panel with search, categories, thumbnails and the
GraphVis 17 stage-then-Apply behaviour.

**Phase 2 — the engine tiers. Done: every catalogue entry draws.** That was
318 entries and 135 engines when the phase closed; the catalogue has since
grown to 2,116 entries and 434 engines, and the sweep below covers all of
them.

Every engine in the GraphVis 17 catalogue draws, and every one is verified to
put data on the page by an automated sweep that runs on each build. Nothing in
the Graph Library reports itself unsupported any more.

Almost all of them are rewrites in `prepareSpec` rather than draw functions of
their own, because each is a transformation of the data followed by geometry
that already exists: an ECDF is a staircase, a Q-Q plot is a scatter with a
reference line, a control chart is a line with three rules across it. Rewriting
the spec and retargeting `spec.engine` means `render()` dispatches to drawing
code that is already tested. The alternative - a bespoke draw function per
catalogue entry - is how a plotting library ends up with forty subtly different
ways to draw a line.

Beyond the rewrites, seven pieces of real geometry carry the rest: the heatmap family (a grid of binned cells,
Viridis, count-aggregated for densities and mean-aggregated for measured
fields), contours (marching squares over that grid, gaps closed first because
scattered samples leave most cells unknown and a cell with any unknown corner
cannot be contoured), violins (mirrored kernel density), polar (its own radial
projection, no rectangular frame), and 3-D (orthographic projection with a
bounding cube and painter's-algorithm depth sorting).

The domain tier computes what it plots rather than trusting a column to have
been kept in step: power from I and V, removal from the starting concentration,
and the modified Gompertz parameters from an actual Gauss-Newton fit done
natively - the science add-on has the same fit in scipy, but a graph that will
not draw without an optional Python install is not much of a graph.

The advanced tiers added: a vector-field renderer (quiver, streamlines by RK2
midpoint integration, and divergence and vorticity by central differences on the
gridded field); composition layouts that divide a whole rather than plot a
coordinate (treemap, sunburst, Venn, word and bubble clouds, Sankey); a 3-D field
renderer for quiver, cones, stream tubes and ribbons, tensor glyphs and the
volume family; a radix-2 FFT behind the power spectral density, the spectrogram
and cross-correlation; and an expression evaluator - tokeniser, shunting-yard
parse to RPN, stack evaluation - for the Function and Implicit engines, which
plot a formula rather than a dataset.

Two engines are honest about what a still figure cannot do. Comet and Animated
Line animate in GraphVis 17; here they draw the whole trace with the head
marked, which is the state they settle into. Venn diagrams size their circles by
the data but not their intersections, because a Venn diagram with true
proportional intersections is not generally constructible and drawing one to
scale would be a lie with a ruler against it. Geographic engines project
longitude and latitude equirectangularly and label the axes as coordinates,
because there is no basemap and pretending otherwise would be worse than
plotting honest numbers.

**The engine sweep.** `--selftest-plot` now renders every supported engine and
fails the build if one draws nothing. This matters because an engine written as
a data rewrite fails silently: a wrong transformation produces an empty plot,
not a compile error, and nobody notices until they open that catalogue entry.
It earned its keep immediately and then taught two lessons about testing:

- A fixed ink threshold reported five working engines as broken. A single thin
  curve puts far less ink on the page than four dense ones.
- A shared empty-frame baseline then reported the polar engines as broken. They
  have no rectangular frame, so they draw *less* chrome than the engine the
  baseline came from. Each engine is now measured against its own empty frame.

It found four real faults: contours drew nothing because the grid was finer
than the sample count could fill; 2-D histograms were flat because the grid
averaged a column of ones instead of counting them; polar histograms were drawn
as sixteen dots rather than wedges; and ternary plots targeted the scatter draw,
which marks points and ignores lines, so the triangle that makes the projection
legible never appeared.

And it caught a third bad test of mine: the "empty frame" probe cleared the
input series, which does not stop a formula engine - it synthesised its default
function anyway, on a different domain, and reported more ink than the real
render. The probe now clears the PREPARED spec, which runs the final engine and
has nothing left to invent.

**Phase 2 (historical) — first engine tier.**

Landed and verified on screen:

- `native/plot2d` — `PlotSpec`/`PlotBackend` (the adapter interface),
  `QtPlotBackend` (axes, nice ticks, log decades with real superscript powers,
  grid, legend, clipping), `ArrowTable` (Arrow IPC to double columns),
  `PlotCanvas` (`QQuickPaintedItem`, `QML_ELEMENT`).
- Linked directly into the executable rather than behind a lazily-loaded QML
  plugin. 2-D plotting is the core of a graph editor, not a specialist view,
  so the indirection the VTK backend needs would buy nothing here.
- `Qt 2-D` added to the Renderer selector; applying a catalogue entry switches
  to it and draws.
- Engines (13 of the 82 non-advanced entries): Line Chart, Stairs, Area,
  Error Bar, 4D / 5D Scatter, Bar, Horizontal Bar, Stem, Stacked Lines,
  Histogram, Box Plot, Pie, Donut. Histogram and Box Plot rewrite the spec into
  drawable geometry before the axes are computed (Freedman-Diaconis binning;
  quartiles with 1.5 IQR whiskers), so the generic axis code needs no special
  cases. Pie and Donut skip the frame entirely.
  Catalogue entries whose engine is not yet ported stay visible and report
  themselves unsupported rather than drawing something wrong.
- `--selftest-plot <out.pdf>` renders a Nature single-column figure through the
  backend into a real `QPdfWriter` and asserts the result is a vector PDF with
  font resources and no raster image XObject. `tools/Full-Diagnose.ps1` runs it
  on every build.

Still to do in this phase: the remaining non-advanced engines (82 of the 318
entries are non-advanced and cover ordinary scientific work — histogram, box,
violin, heatmap, contour, surface), plus a capability check that disables
entries the active dataset cannot support, as GraphVis 17 does.

**Phase 3 — Smart Suite / Scan Dataset. Built, awaiting verification.**

- `services/.../service.py` gained a `dataset.scan` op. `intelligent_scan.
  scan_dataset` expects a GraphVis 17 Dataset object, so a small adapter
  supplies the four attributes it actually reads (`df`, `numeric_columns`,
  `path`, `name`) from the Arrow file the native core already owns.
- `AppController::scanDataset(budgetSeconds)` plus `scanRecommendations`,
  `scanSummary` and `scanning`. The service reply handler now routes by the
  requested op: before this there was only one operation and every reply was
  assumed to be a literature extraction, which would have mis-filed scan
  results into `literatureAnalysis`.
- `ScanPanel.qml` under the Graph Library: thinking budget (8 s to 2 h,
  matching v17's soft-deadline behaviour), ranked recommendations with score,
  chosen mapping and the scanner's stated reason.
- Choosing a recommendation applies the graph **and** its axis mapping in one
  action, which is the point of the scan.
- The `dataset.scan` op already accepts an optional `literature` argument, so
  Phase 4 is wiring rather than new plumbing.

Not yet done in this phase: the project-local scan cache keyed by dataset and
literature fingerprint, and per-recommendation colormap/scale.

**Phase 4 — literature to recommendation. Built, awaiting a real paper.**

`intelligent_scan` reads literature entries with `getattr(.title,
.semantic_context)`, so the JSON returned by `literature.extract` is wrapped
into objects before being handed to the scanner. When a paper is open and
analysed, "Scan Dataset" becomes "Scan + Literature", the paper's x/y/z
variables and inferred plot type steer the ranking, and literature-sourced
recommendations are badged. Untested against an actual PDF, which needs the
optional science add-on installed.

**Phase 5 — surface estimation, export and publication profiles, themes,
project workspaces and groups.**

**Data import. Done, and well past what GraphVis 17 read.**

`services/python/graphvis_science/data/importer.py` is a registry of 149 file
extensions across 32 groups. Each reader is one function, registered by
extension, and every third-party package is imported lazily - a format whose
package is missing reports what to install and never blocks the formats that
do work. Non-native files are converted to Arrow IPC by the science service
and handed to the Rust core, which is exactly the "optional scientific IO
plugin that emits Arrow IPC" the core's own error message asks for.

Groups: tables and text, spreadsheets (including Apple Numbers), columnar,
HDF5/NetCDF, statistics packages (SPSS, Stata, SAS, R), arrays and databases
(including DuckDB), MATLAB, business and accounting (dBase, Access), finance
(OFX/QFX, QIF, SWIFT MT940), reports (tables lifted out of PDF and Word),
instruments and audio, mass spec, flow cytometry, spectroscopy, astronomy,
particle physics, seismic, physiology, electrophysiology, neuroscience,
medical imaging, geospatial, GPS tracks, marine and oceanography, diving,
genomics and molecular structures.

About a third of the readers are written here rather than delegated, because
no maintained package reads them: Sea-Bird CTD `.cnv`/`.btl`, Ocean Data View,
NMEA 0183, GPX, KML/KMZ, bathymetric and molecular `.xyz`, the four
dive-computer log formats (UDDF, Subsurface, DAN DL7, Suunto SML), QIF,
fixed-width text, VCF/BED/GFF/GTF/SAM, FASTA/FASTQ, PDB, mmCIF and MDL
molfiles. `services/python/tests/test_importers.py` round-trips every one of
them through `import_to_arrow` against a small real-shaped fixture.

Two extensions are claimed by two unrelated formats and are resolved by
content, not by name: `.fit` is both FITS and Garmin/Shearwater dive logs
(a FIT file carries `.FIT` at byte 8), and `.xyz` is both a molecular
structure and a bathymetric sounding grid (a molecular file opens with an
integer atom count).

**Window and display modes. Done.**

`AppController` owns `displayMode` (windowed / maximised / fullscreen /
borderless), persisted with the theme, plus `preferredWindowGeometry`,
`targetScreenGeometry` and `targetWorkAreaGeometry`. Qt centres a fixed-size
window on the *virtual* desktop, which on a two-monitor machine opens it
straddling the bezel; all three pick one real screen - the one under the
pointer - and stay inside its work area. A windowed session's geometry is
remembered, but only if the screen it was saved on still exists and still
contains it.

Both fullscreen modes are frameless windows, not `Window.FullScreen`. True
fullscreen asks for an exclusive surface, which on a hybrid-graphics laptop is
a display mode change: the monitor blanks while the GPU renegotiates, and on
this hardware it did not reliably come back. A frameless window sized to the
screen looks identical, is what games call borderless fullscreen, and never
touches the display mode.

## Reference material

`reference/gv17-src`, `reference/gv17-docs`, `reference/gv17-config` are the
GraphVis 17 tree, kept as the source of truth for the port. They are reference
only and are not built.
