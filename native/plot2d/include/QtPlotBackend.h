#pragma once
// =========================================================================
// QtPlotBackend - draws the 2-D catalogue with QPainter.
//
// QPainter draws to the screen, to QPdfWriter and to QSvgGenerator from this
// same code, so publication output is true vector with embedded fonts and no
// second scene-to-vector path has to be kept consistent. That is the reason
// this is the first backend (docs/PORT-PLAN.md).
// =========================================================================
#include "PlotBackend.h"
#include <QFont>

class QPainter;

namespace graphvis {

// One axis tick: where it sits in data space and how it is labelled.
struct AxisTick {
    double value = 0.0;
    QString label;
    bool minor = false;
};

class QtPlotBackend final : public PlotBackend {
public:
    QString name() const override { return QStringLiteral("Qt 2-D"); }
    QStringList supportedEngines() const override;
    bool supports(const QString& engine) const override;
    bool supportsVectorOutput() const override { return true; }
    void render(QPainter* painter, const QRectF& target, const PlotSpec& spec) override;

    // Draft mode: the picture drawn WHILE a figure is being turned, panned or
    // zoomed, as against the one left on screen when it stops.
    //
    // This exists because of a measurement, not a hunch. Antialiasing a stroked
    // line whose segments cross the whole frame is pathologically expensive in
    // Qt's raster engine: a thousand-point 3-D line took 6,667 ms a frame, the
    // cube and its axes took 0.6 ms of that, and the identical polyline with
    // antialiasing off took 54 ms. So rotation had been working all along and
    // each frame was taking most of a second, which on screen looks exactly
    // like a figure snapping between a few fixed faces rather than turning.
    //
    // Draft turns antialiasing off and thins very dense series. It is for the
    // live preview only - the full-resolution render that lands when the drag
    // stops is never in draft, and neither is any export - so nothing that
    // leaves this application is drawn at draft quality.
    void setDraft(bool on){ draft_=on; }
    bool draft() const { return draft_; }
    // Above this many points in one series, draft mode strides over the rest.
    // A preview is replaced within a fifth of a second by the real thing, so
    // the honest trade is a thinner line now against a stalled one.
    //
    // ADAPTIVE, because a fixed cap cannot be right. What a segment costs
    // depends on how LONG it is, not how many there are: six thousand points
    // of a smooth curve cost about 15 ms and six thousand points of a line
    // that crosses the whole frame between every pair cost 167. Any constant
    // is therefore either needlessly coarse on ordinary data or still too slow
    // on the awkward kind. So each draft frame times itself and moves the cap
    // toward a 40 ms target, which settles within two or three frames of a
    // drag starting - before the eye can follow it.
    static constexpr int kDraftTargetMs=40;
    static constexpr int kDraftCapMin=600;
    static constexpr int kDraftCapMax=40000;

    // Exposed for testing and for reuse by future backends that want Qt to
    // draw the chrome over their own data layer.
    static QVector<AxisTick> linearTicks(double lo, double hi, int wanted);
    static QVector<AxisTick> logTicks(double lo, double hi);

    // What one figure currently spans, and whether it is drawn against axes at
    // all. Both are for the canvas's interactive pan and zoom: a zoom starts
    // from the range the figure already has, and a pie has nothing to zoom.
    struct DataRange {
        double xLo=0, xHi=1, yLo=0, yHi=1;
        bool xLog=false, yLog=false;
    };
    DataRange rangeFor(const PlotSpec& spec) const;
    // Where the last render put the plot area, in the target's coordinates. An
    // interactive zoom has to be about the point under the finger, and the
    // point under the finger is only meaningful against the drawn area - which
    // is inset by however wide the y labels turned out to be. Written by
    // computeFrame on whichever thread drew; the full-resolution render builds
    // its own backend, so this is never touched from two threads at once.
    QRectF lastPlotArea() const { return lastPlotArea_; }
    // Public because the canvas decides whether to offer panning at all, and
    // the answer must be the same one render() acts on.
    static bool engineHasAxes(const QString& engine);

    // Why did this engine draw nothing? Empty when it drew, or when the reason
    // is not one of the understood ones.
    //
    // A figure that comes out empty is the single most confusing thing this
    // application can do, and it used to say only "2D Heatmap - 20000 points",
    // which is true and tells you nothing. A heat map needs THREE mapped
    // columns - x, y and the value - and given two it returns before drawing a
    // single cell while the axes are still computed from the data, so the frame
    // and the labels appear and the plot area does not.
    //
    // Takes BOTH specs. The requirement belongs to the engine the person
    // chose - a Ternary Scatter that got two columns has already been rewritten
    // into something else by the time the painter sees it, so asking the
    // prepared engine what it needed gives the wrong answer or none. What was
    // actually produced has to come from the prepared one.
    static QString explainEmpty(const PlotSpec& chosen,const PlotSpec& prepared);

    // What one engine wants from the mapping, in the only terms the canvas
    // needs: how many columns, and how they arrive.
    //
    // There are two conventions in this backend and the difference is not
    // cosmetic. An ORDINARY engine takes x from the frame and draws one series
    // per y column, which is what makes three mapped y columns draw three
    // lines. A COLUMN engine reads series i as column i - a heat map's x, y and
    // value; a network's from, to and weight; a Bland-Altman's two methods -
    // and returns without drawing if it has fewer series than it reads.
    //
    // Getting a number here wrong is invisible except as a blank figure,
    // because the mapping composes exactly as many roles as this asks for: an
    // engine whose requirement is under-stated can never be given enough
    // columns by any action the person takes. That was the state of 53 of the
    // 203 engines - every one of them a two-column comparison or a field whose
    // requirement lives in its data rewrite rather than in its painter, which
    // is the half of the code the first version of this function did not read.
    struct ColumnPlan {
        int minimum = 2;        // fewest mapped columns that draw anything
        int maximum = 2;        // most it reads; 0 means every column mapped
        bool asSeries = false;  // series i is column i, rather than x + one y each
    };
    static ColumnPlan columnPlan(const QString& engine);

    // The minimum on its own, for the callers that only need the number.
    static int columnsRequired(const QString& engine);

private:
    struct Frame {
        QRectF plotArea;      // where series are drawn
        double xLo=0, xHi=1;  // data-space bounds, already log10 if log
        double yLo=0, yHi=1;
        bool xLog=false, yLog=false;
    };
    Frame computeFrame(QPainter* p, const QRectF& target, const PlotSpec& spec,
                       QVector<AxisTick>& xTicks, QVector<AxisTick>& yTicks) const;
    // The data-space half of computeFrame, with no painter and no chrome.
    Frame computeRange(const PlotSpec& spec) const;
    QPointF toDevice(const Frame& f, double x, double y) const;
    void drawChrome(QPainter* p, const Frame& f, const PlotSpec& spec,
                    const QVector<AxisTick>& xTicks, const QVector<AxisTick>& yTicks) const;
    void drawLegend(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    // The title, for the engines that have no axis frame to hang it on: pie,
    // donut, the composition family and both 3-D paths. Written out four times
    // identically before this.
    void drawFloatingTitle(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    // Bar width from the closest neighbouring pair of slots, so unevenly
    // spaced categories never overlap. Shared by drawBar, drawHorizontalBar
    // and drawFloatingBar.
    static double slotWidthFrom(QVector<double> centres, double fallback, double limit);
    // Half a mark's width along the slot axis, shared by the three floating
    // painters. alongY measures rows rather than columns; fraction is how much
    // of the slot the mark fills. Returns 0 when there is nothing to measure.
    double halfSlotAcross(const Frame& f, const PlotSpec& spec, bool alongY,
                          double minimum, double fraction = 0.36) const;
    // One series' markers. drawLineChart, drawScatter and drawStem each had
    // their own copy of this loop, guards and radius included. Deliberately
    // does NOT test s.drawMarkers: for the Scatter engine the markers ARE the
    // plot and are drawn whatever the flag says, so the caller decides.
    void drawSeriesMarkers(QPainter* p, const Frame& f, const PlotSeries& s,
                           double minRadius = 0.5) const;

    void drawLineChart(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    void drawScatter(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    void drawBar(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    void drawArea(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    void drawStairs(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    void drawStem(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    void drawHorizontalBar(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    void drawStackedLines(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    void drawErrorBar(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    void drawBoxPlot(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    // A bar that does not start at zero. Waterfall, range and Gantt-shaped
    // engines all need it, and none of the existing draw functions can express
    // it: drawBar is anchored to the baseline and drawErrorBar reads only the
    // first two series, so a waterfall rewritten onto it drew a single point.
    // One series per bar: x = {slot}, y = {low, high}.
    void drawFloatingBar(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    // The same bar transposed: y = {row}, x = {start, end}. A schedule, an
    // availability timeline and a borehole log are one shape drawn three ways.
    void drawFloatingRow(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    // Open/high/low/close. One series per period, y = {o, h, l, c}.
    void drawCandlestick(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    void drawPie(QPainter* p, const QRectF& target, const PlotSpec& spec, bool donut) const;
    // A scalar field sampled onto a regular lattice. Declared here rather than
    // in the .cpp so it can be cached across repaints - see gridCache_.
    struct ValueGrid {
        int nx=0, ny=0;
        double xLo=0, xHi=1, yLo=0, yHi=1;
        double vLo=0, vHi=1;
        QVector<double> cells;      // nx*ny, NaN where no sample landed
        bool valid=false;
    };
    enum class GridAggregate { Mean, Count };
    static ValueGrid gridFromSeries(const PlotSpec& spec, int requested,
                                    GridAggregate how = GridAggregate::Mean);
    // Fill the cells no sample landed in. See PlotStyle::fieldInterpolation for
    // the modes and the implementation for why a pyramid rather than a search.
    static void estimateEmptyCells(ValueGrid& g, int mode);
    static void closeGaps(ValueGrid& g, int passes);

    // Gridding is the other half of the per-repaint cost that prepareSpec was.
    // A heatmap re-binned every row on every paint; a contour then ran ten
    // marching-squares sweeps over the result; a quiver field built TWO grids
    // and closed the gaps in both. None of that changes between repaints.
    //
    // Three slots because drawVectorField needs two grids live at once, and
    // keeping a third means a heatmap and a contour of the same data do not
    // evict each other.
    struct GridCacheEntry {
        bool valid=false;
        quint64 hash=0;
        int aggregate=0;
        int gapPasses=0;
        ValueGrid grid;
    };
    mutable GridCacheEntry gridCache_[3];
    const ValueGrid& cachedGrid(const PlotSpec& gridInput, GridAggregate how,
                                int gapPasses, int slot) const;

    // Field engines. These need geometry of their own rather than a rewrite:
    // a heatmap is a grid of cells, a contour traces level crossings through
    // that grid, and neither can be expressed as a series of points.
    void drawHeatmap(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    void drawContour(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    void drawViolin(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    void drawPolar(QPainter* p, const QRectF& target, const PlotSpec& spec, bool markersOnly) const;
    void drawForest(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    // Orthographic projection rather than the VTK viewport: a catalogue entry
    // that only works when an optional plugin is loaded usually does not work.
    void draw3D(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    // Quiver, streamlines, and the scalars derived from a field. All share one
    // gridding step: a field measured at scattered points has to be regular
    // before anything can be differentiated or integrated through it.
    void drawVectorField(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    // Treemap, sunburst, Venn, word cloud, Sankey. All divide a whole rather
    // than plot a coordinate, so none of them uses the axis frame.
    void drawComposition(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    // Nodes and the links between them: a grid, a pipeline network, a route
    // map. Both read an edge list - source, target, weight - and neither has
    // axes, because a layout is not a coordinate system.
    void drawNetwork(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    void drawChord(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    // Three more that are their own coordinate system: the reflection plane
    // with its impedance grid, a contingency table drawn to scale, and set
    // intersections as bars over a membership matrix.
    void drawSmith(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    // The four thermodynamic diagrams. They draw their own frame, chrome and
    // background curves, so they take the target rectangle rather than a
    // Frame; chartKind selects the transform (0 Skew-T, 1 emagram, 2 Stuve,
    // 3 tephigram) and everything else is shared.
    void drawSounding(QPainter* p, const QRectF& target, const PlotSpec& spec,
                      int chartKind) const;
    // Two ternary triangles and the diamond both project into.
    void drawPiper(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    // The Piper's six columns again, drawn so one water is one shape.
    void drawStiff(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    // And a third time, projected into a square where mixing is a straight line.
    void drawDurov(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    // Period against pseudo-velocity on log-log, with the constant displacement
    // and constant acceleration families drawn over it. An axis engine: the two
    // plotted quantities are ordinary coordinates.
    void drawTripartite(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    // The same edge list on three radial axes, placed by degree.
    void drawHive(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    // The same edge list as a flow through computed stages.
    void drawAlluvial(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    // The network graph's edge list, laid out on a line.
    void drawArc(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    // Three components and a value over them, contoured on the Delaunay
    // triangulation of the samples themselves.
    void drawTernaryContour(QPainter* p, const QRectF& target,
                            const PlotSpec& spec) const;
    // Stacked bands on a free baseline; every mapped column is one band.
    void drawStream(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    // The same node/parent/value columns read as a tree with branch lengths.
    void drawCladogram(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    // A hierarchy as nested bars, from node/parent/own-value columns. upward
    // draws it as a profiler does - deepest at the top, siblings sorted - and
    // the two engines differ in nothing else.
    void drawIcicle(QPainter* p, const QRectF& target, const PlotSpec& spec,
                    bool upward) const;
    // Potential against pH, with water's own stability field over the top.
    void drawPourbaix(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    // One stack of letters per position, the stack height in bits.
    void drawSequenceLogo(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    // Nearest-site regions. Takes a Frame rather than the target rectangle:
    // unlike the painters above it is drawn against ordinary axes, because the
    // positions are the data and a reader needs to know what they are.
    void drawVoronoi(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    void drawMosaic(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    void drawUpSet(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    // Quiver, cones, stream tubes and ribbons, tensor glyphs and the volume
    // family: six mapped columns for a vector field, four for a scalar volume.
    void draw3DField(QPainter* p, const QRectF& target, const PlotSpec& spec) const;

    // Some engines are not a direct plot of the mapped columns: a histogram
    // draws the distribution of one column, a box plot draws its quartiles.
    // They rewrite the spec into drawable geometry before the frame and axes
    // are computed, so the generic axis code needs no special cases.
    PlotSpec prepareSpec(const PlotSpec& spec) const;
    // The rewrite itself. prepareSpec wraps it so that axis limits set by the
    // caller - the canvas's pan and zoom - survive an engine that sets its own.
    PlotSpec prepareSpecCore(const PlotSpec& spec) const;

    // prepareSpec is where the statistical engines actually live: a KDE sums a
    // kernel over every sample for every output point, a periodogram runs an
    // FFT, a growth-curve fit runs thirty Gauss-Newton iterations. render() was
    // calling it on every single repaint, so hovering the mouse over a violin
    // plot re-derived the violin. The inputs almost never change between
    // repaints, so the result is cached against a fingerprint of them.
    struct PreparedCache {
        bool valid=false;
        QString engine, variant, expression;
        int seriesCount=0;
        qsizetype pointCount=0;
        quint64 hash=0;
        PlotSpec prepared;
    };
    // The network layout, in a unit square. Same reasoning as gridCache_: it
    // depends only on the edges, and recomputing it per repaint was measured at
    // half a second for a 400-node graph.
    struct NetworkLayout {
        bool valid=false;
        quint64 hash=0;
        QVector<QPointF> pos;
    };
    mutable NetworkLayout networkCache_;
    bool draft_=false;
    mutable int draftCap_=6000;
    mutable QRectF lastPlotArea_;
    mutable PreparedCache prepCache_;
    // 64-bit FNV-1a over everything prepareSpec can read. Cheap enough to run
    // per repaint (it is a linear pass over data that render already copies)
    // and specific enough that a changed sample, colour or axis bound misses.
    static quint64 specFingerprint(const PlotSpec& spec);
    const PlotSpec& preparedCached(const PlotSpec& spec) const;
public:
    // The rewritten spec, for the engine sweep. An engine expressed as a data
    // rewrite fails by producing no series rather than by failing to compile,
    // so the test needs to see what the rewrite actually produced.
    PlotSpec preparedFor(const PlotSpec& spec) const { return prepareSpec(spec); }

    // The gridded field, for the tests. An estimator that alters a measured
    // cell is not an estimator but a filter, and that can only be checked by
    // comparing the numbers - the picture looks equally plausible either way.
    struct FieldGrid { int nx=0,ny=0; QVector<double> cells; bool valid=false; };
    static FieldGrid fieldFor(const PlotSpec& spec){
        const ValueGrid g=gridFromSeries(spec,0);
        return {g.nx,g.ny,g.cells,g.valid};
    }

    // Does this engine colour a FIELD - a heat map, a contour, a surface, a
    // vector field - rather than a set of series? Only those read
    // PlotStyle::colourMap, so only for those should the interface offer the
    // choice; on the other hundred and eighty it would be a control that does
    // nothing, which is worse than no control.
    //
    // Asked of the PREPARED engine, not the catalogue one: a Spectrogram is a
    // 2-D heatmap by the time anything is drawn and a Correlation Matrix is
    // too, so answering from the catalogue name would miss both.
    static bool usesColourMap(const QString& preparedEngine);
private:
    // Notes on the figure, drawn last and clipped to the plot area. Real text,
    // so a PDF export carries words rather than outlines.
    void drawAnnotations(QPainter* p,const Frame& f,const PlotSpec& spec) const;
    QFont font(const PlotSpec& spec, double pointSize) const;
};

} // namespace graphvis
