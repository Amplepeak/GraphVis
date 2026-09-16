# GraphVis passive audit — 17 standing

- 2026-09-16T17:54:34+01:00  ·  periodic
- master@e35102a
- read 48 C++ files (64,904 lines, 582,495 tokens), 61 QML (16,335 lines), 96 Python (41,529 lines)
- parsed 1718 functions, 120 types; pass took 37.8s

## New since the last pass

Nothing new. Everything below was already known.

## correctness (1)

### Component.onCompleted inside a repeated delegate  (1)

*A delegate is created and destroyed as the view scrolls, so anything onCompleted does - registering, reparenting, appending to a model - happens again on every recycle and never gets undone. This project has already had one reparenting bug of exactly this shape.*

- `app/qml/workspaces/NotebookCanvas.qml:206  FigureCell#cell`
  Component.onCompleted runs on every recycle of this Repeater delegate
      L206: Component.onCompleted: {
  → Move it to the view, or make it idempotent.

## dead-code (1)

### A module-level function nothing calls  (1)

*Code nobody runs is code nobody is testing.*

- `tools/theme-generator/gen_themes_lib.py:42  fit_lightness`
  `fit_lightness` is defined and never called
      L42: def fit_lightness(h, s, against, target, dark_text):
  → Remove it, or wire it up.

## maintainability (15)

### A function too large to hold in your head  (15)

*Length alone is weak evidence; length together with branching and nesting is the shape of a function that is hard to change without breaking something.*

- `native/plot2d/src/PlotSelfTest.cpp:53  sweepInputs`
  4455 lines, complexity 389, nesting 4
      L53: SweepInputs sweepInputs(){
  → Split out the part that can be named.
- `native/plot2d/src/PlotSelfTest.cpp:4516  runEngineSweep`
  483 lines, complexity 44, nesting 3
      L4516: bool runEngineSweep(const QString& galleryDir){
  → Split out the part that can be named.
- `native/plot2d/src/PlotSelfTest.cpp:5032  runRegressionChecks`
  1406 lines, complexity 178, nesting 5
      L5032: bool runRegressionChecks(){
  → Split out the part that can be named.
- `native/plot2d/src/PlotSelfTest.cpp:6605  runPropertyChecks`
  778 lines, complexity 88, nesting 5
      L6605: bool runPropertyChecks(bool report){
  → Split out the part that can be named.
- `app/src/AppController.cpp:41  AppController::AppController`
  321 lines, complexity 11, nesting 1
      L41: AppController::AppController(QObject* parent):QObject(parent),viewport_(){
  → Split out the part that can be named.
- `app/src/main.cpp:205  main`
  271 lines, complexity 16, nesting 4
      L205: int main(int argc, char *argv[])
  → Split out the part that can be named.
- `native/plot2d/src/Expression.cpp:85  Expression::compile`
  155 lines, complexity 74, nesting 4
      L85: bool Expression::compile(const QString& text,const QStringList& variables){
  → Split out the part that can be named.
- `native/plot2d/src/PlotCanvas.cpp:2114  PlotCanvas::rebuild`
  239 lines, complexity 40, nesting 3
      L2114: void PlotCanvas::rebuild(){
  → Split out the part that can be named.
- `native/plot2d/src/QtPlotBackend.cpp:1692  QtPlotBackend::drawLegend`
  333 lines, complexity 50, nesting 3
      L1692: void QtPlotBackend::drawLegend(QPainter* p,const Frame& f,const PlotSpec& spec) const {
  → Split out the part that can be named.
- `native/plot2d/src/QtPlotBackend.cpp:3792  QtPlotBackend::explainEmpty`
  346 lines, complexity 81, nesting 4
      L3792: QString QtPlotBackend::explainEmpty(const PlotSpec& chosen,const PlotSpec& prepared){
  → Split out the part that can be named.
- `native/plot2d/src/QtPlotBackend.cpp:4834  QtPlotBackend::drawPolar`
  235 lines, complexity 47, nesting 5
      L4834: void QtPlotBackend::drawPolar(QPainter* p,const QRectF& target,const PlotSpec& spec,
  → Split out the part that can be named.
- `native/plot2d/src/QtPlotBackend.cpp:5442  QtPlotBackend::draw3DMarks`
  261 lines, complexity 55, nesting 4
      L5442: void QtPlotBackend::draw3DMarks(QPainter* p,const Projection& proj,
  → Split out the part that can be named.
- ...and 3 more (all of them in AUDIT.json)

## What this pass did not look at

A clean report is not the same as a working program. This is static: it reads the source and runs nothing.

- Anything that needs the program running: rendering, timing, memory, and whether a button does what it says.
- Template instantiation, overload resolution and macro bodies.
- Both arms of an #ifdef are parsed; flow checks stay silent across them.
- Rust sources and the FFI boundary.
- Generated output, vendored headers and native/target/.

## Checks that could not run here

These say nothing about the code. They are checks whose tool did not run on this machine, listed so that their silence is not mistaken for a pass.

- `designed_colourmaps_stale`: cannot run: No module named 'numpy'. This says nothing about the file - install numpy to check it.
- `theme_cvd_tags_stale`: cannot run: No module named 'numpy'. This says nothing about the themes - install numpy to check them.

## For another agent

`build-reports/AUDIT.json` carries every finding with a stable id, a file and line anchor, the reasoning, the evidence, and the method each check used — including the false positives it is known to produce. Read the method before acting on the finding.

<!-- Written by tools/passive_audit.py. Static and read-only: it compiles nothing and renders nothing. -->