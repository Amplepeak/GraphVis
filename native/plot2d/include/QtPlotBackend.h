#pragma once

// std::optional: the engine-rewrite groups below return one.
#include <optional>
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
#include <QPointF>
// qIsInf and qBound, for `finite` and `Bounds::scale` just below.
#include <QtGlobal>
#include <QtNumeric>

class QPainter;

namespace graphvis {

// One axis tick: where it sits in data space and how it is labelled.
struct AxisTick {
    double value = 0.0;
    QString label;
    bool minor = false;
};

// MOVED UP FROM QtPlotBackendShared.h, because the class below now has member
// functions that take them.
//
// They were in the private implementation header, which is included BY this one
// - so nothing declared here could mention them, and the 3-D painter's four
// branches could not be split into functions: each needs the projection and the
// three axis spans, and a member function cannot be declared with a type its
// own header has not seen. The builders (`makeProjection`, `boundsOf`,
// `withAxisLimits`) stay in the implementation header; only the vocabulary moves.
inline bool finite(double v){ return v==v && !qIsInf(v); }

// The span of one column.
//
// This existed twice under two names with two builders - boundsOf and boundsFor.
// They measure the same thing and differ only in what they map it onto: a radar
// chart wants [0,1] clamped, a 3-D projection wants [-0.5,+0.5] unclamped. Both
// normalisations live here.
struct Bounds {
    double lo=0, hi=1;
    bool valid=false;
    // Centred on zero, for the orthographic projection: a cube from -0.5 to
    // +0.5 rotates about its own middle.
    double norm(double v) const { return hi>lo ? (v-lo)/(hi-lo)-0.5 : 0.0; }
    // Clamped to [0,1], so a radar chart can compare columns whose units
    // differ. Plotting raw values on a shared radius compares nothing.
    double scale(double v) const {
        if(!finite(v)||!(hi>lo)) return 0.0;
        return qBound(0.0,(v-lo)/(hi-lo),1.0);
    }
};

// A colour map is a table of 256 RGB triples. The alias is here rather than
// with the tables because the class below takes one as a parameter.
using ColourMapKind = const unsigned char (*)[3];

// The orthographic camera: two angles pre-resolved into sines and cosines, the
// centre of the cube on the page, and how many pixels a unit of the cube is.
struct Projection {
    double sinAz, cosAz, sinEl, cosEl;
    QPointF origin;
    double scale;
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
    // Where the last render put the legend, in the target's coordinates, or a
    // null rectangle when it drew none. A legend sits INSIDE the plot area, so
    // anything asking a question about what the DATA did near the frame has to
    // be able to exclude it - see the plot-frame property check.
    QRectF lastLegendRect() const { return lastLegendRect_; }
    // Public because the canvas decides whether to offer panning at all, and
    // the answer must be the same one render() acts on.
    static bool engineHasAxes(const QString& engine);

    // Where the last render put each annotation. Index-aligned with the spec's
    // annotations; a null rectangle means that note was not drawn, because its
    // anchor is off the plot or its text is empty. The canvas uses this to turn
    // a click into a note rather than repeating the placement rules.
    const QVector<QRectF>& annotationBoxes() const { return annotationBoxes_; }

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
    // The engines drawn in a circle, as ONE list.
    //
    // The dispatch in render() names them and so does the interface, which
    // needs to know whether to offer the angle convention. Two hand-written
    // lists is how an engine ends up drawn as a polar figure with no way to set
    // which way round it reads. Stereonet is here because it DERIVES to Polar
    // Scatter: the chosen engine is what the interface asks about.
    static bool isPolarEngine(const QString& engine);

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
    // 203 engines then catalogued - each a two-column comparison or a field whose
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

    // A constant an engine needs that is not per-row data - a decay's parent
    // mass, a reference pressure, a sample rate.
    //
    // Declared rather than hand-wired. The eight field-estimator settings are
    // each threaded through six places (PlotSpec, a Q_PROPERTY and a clamping
    // setter, a settings key, a QML control, the fingerprint), which is fine
    // for one subsystem and hopeless as a rule: "any engine may declare
    // constants" would mean six edits per constant forever. An engine adds a
    // row to the table in engineParameters() and the interface builds the
    // control from what the row says.
    //
    // Everything needed to draw and validate a control is in the row, the
    // range included - a control that accepts a parent mass lighter than its
    // own daughters is a control that produces a figure with no boundary and
    // no explanation of why.
    struct EngineParameter {
        QString key;            // short ASCII name; the key in PlotSpec
        QString label;          // what the control is called on screen
        QString unit;           // shown after the value; may be empty
        QString help;           // one line, for the tooltip
        double defaultValue = 0.0;
        double minimum = 0.0;
        double maximum = 1.0;
        int decimals = 3;
    };
    // Empty for all but a few engines: the interface hides the whole group on
    // an empty list rather than showing an empty box.
    static QVector<EngineParameter> engineParameters(const QString& engine);
    // The declared defaults as a map, for filling a spec that has none.
    static QMap<QString,double> engineParameterDefaults(const QString& engine);

private:
    struct Frame {
        QRectF plotArea;      // where series are drawn
        double xLo=0, xHi=1;  // data-space bounds, already log10 if log
        double yLo=0, yHi=1;
        bool xLog=false, yLog=false;
        // The right-hand ordinate, when some series asked for one. See
        // PlotSeries::secondaryAxis: measured only from the series that set
        // it, so the two quantities stop fighting over one range.
        double y2Lo=0, y2Hi=1;
        bool y2Log=false;
        bool hasY2=false;
    };
    Frame computeFrame(QPainter* p, const QRectF& target, const PlotSpec& spec,
                       QVector<AxisTick>& xTicks, QVector<AxisTick>& yTicks) const;
    // The data-space half of computeFrame, with no painter and no chrome.
    Frame computeRange(const PlotSpec& spec) const;
    QPointF toDevice(const Frame& f, double x, double y) const;
    // The same, for a series drawn against the right-hand ordinate. Falls back
    // to the left-hand one when the frame has no second axis, so a caller that
    // passes `true` on a figure without one is drawn rather than dropped.
    QPointF toDeviceOn(const Frame& f, double x, double y, bool secondary) const;
    // The right-hand ordinate's ticks. Computed from the frame rather than
    // returned alongside the other two, because both the margin measurement in
    // computeFrame and the drawing in drawChrome need them and a second copy
    // of the choosing would let the two disagree about where they are.
    QVector<AxisTick> secondaryTicks(const PlotSpec& spec, const Frame& f) const;
    void drawChrome(QPainter* p, const Frame& f, const PlotSpec& spec,
                    const QVector<AxisTick>& xTicks, const QVector<AxisTick>& yTicks) const;
    void drawLegend(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    // The key to a colour-mapped figure: a strip of the map with the value
    // range on it. Drawn in the backend so it reaches the PDF and the SVG too.
    // Hexagonal binning for "Hexbin Density", which until now shared the 2-D
    // histogram's square-celled painter and therefore drew the same picture.
    void drawHexbin(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    // The style to colour with, once the painter knows its values.
    //
    // A quantile scale needs the DISTRIBUTION of what is being coloured, and
    // only the painter holds that - a heat map has its cells, a hexbin its
    // counts. Linear and log need nothing but the range, which is why those two
    // reach every mapped engine through rampPosition alone and this exists only
    // for the third.
    //
    // Returns a copy rather than mutating the spec: a painter takes a const
    // PlotSpec&, and it should stay that way - a painter that edits the figure
    // it is drawing is how two renders of one spec come out different.
    static PlotStyle scaledStyle(const PlotSpec& spec, const QVector<double>& values);

    void drawColourBar(QPainter* p, const Frame& f, const PlotSpec& spec,
                       double lo, double hi, const QString& caption) const;
    // The same key, for a ramp that is not linear in the value.
    //
    // A separate overload rather than a defaulted argument, so the eight
    // existing callers are untouched and the two that pass a scale say so at
    // the call site. The scale here MUST be the one the engine coloured with -
    // scalePosition is shared for exactly that reason.
    void drawColourBar(QPainter* p, const Frame& f, const PlotSpec& spec,
                       const ColourScale& scale, const QString& caption) const;
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
    // The joint scatter plus the two marginal strips. NOT drawScatter
    // under a second name - see the note on the definition.
    void drawScatterMarginals(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    void drawBar(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    void drawArea(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    // The band between adjacent curves. NOT drawArea under a second name -
    // see the note on the definition for what that alias cost.
    void drawFillBetween(QPainter* p, const Frame& f, const PlotSpec& spec) const;
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
    // The mainstream shapes the catalogue was missing. Each carries its own
    // note where it is defined, in QtPlotBackendEngines6.cpp.
    //
    // Gauge, Bullet and Marimekko take a target rectangle rather than a Frame:
    // none of them has a coordinate to put on an axis, so all three are in
    // engineHasAxes' exclusion list for the same reason Pie is. Dumbbell does
    // have axes - its x IS the measured value - and takes a Frame.
    void drawDumbbell(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    void drawGauge(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    void drawBullet(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    void drawMarimekko(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    void drawChoropleth(QPainter* p, const Frame& f, const PlotSpec& spec) const;
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
    // The cloud, the box and the RAIN. Not drawViolin under a second
    // name - see the note on the definition.
    void drawRaincloud(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    void drawPolar(QPainter* p, const QRectF& target, const PlotSpec& spec, bool markersOnly) const;
    void drawForest(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    // Orthographic projection rather than the VTK viewport: a catalogue entry
    // that only works when an optional plugin is loaded usually does not work.
    void draw3D(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    // Quiver, streamlines, and the scalars derived from a field. All share one
    // gridding step: a field measured at scattered points has to be regular
    // before anything can be differentiated or integrated through it.
    // The four pictures draw3D dispatches between. The camera, the cube, its
    // axes and the depth-sort are written once in draw3D because every one of
    // them needs all four; what differs is the mark, and that is these.
    //
    // The last of them was the `else` of a four-arm chain, and nine engines
    // were drawn as a scatter because they landed in it - a 3-D bar as a cloud
    // of dots, a 3-D stem with no stalks. A named function is harder to fall
    // into by accident than an `else`.
    void draw3DSurface(QPainter* p, const Projection& proj, const PlotSpec& spec,
                       const Bounds& bz, ColourMapKind cmap, bool limited,
                       bool contoured, bool wireframe) const;
    void draw3DContourStack(QPainter* p, const Projection& proj, const PlotSpec& spec,
                            const Bounds& bz, ColourMapKind cmap) const;
    void draw3DLine(QPainter* p, const Projection& proj, const PlotSpec& spec,
                    const Bounds& bx, const Bounds& by, const Bounds& bz,
                    bool limited) const;
    void draw3DMarks(QPainter* p, const Projection& proj, const PlotSpec& spec,
                     const Bounds& bx, const Bounds& by, const Bounds& bz,
                     ColourMapKind cmap, bool limited) const;
    // The three parts render() was made of. It was 318 lines and complexity
    // 83, three quarters of which was two dispatch chains - thirty engine
    // names in one and forty in the other - burying the part that actually
    // decides anything: the draft thinning, the frame, the order the chrome
    // goes on in.
    bool thinForDraft(const PlotSpec& prepared, PlotSpec& out) const;
    void drawAxislessEngine(QPainter* painter, const QRectF& target,
                            const PlotSpec& spec) const;
    void drawFramedEngine(QPainter* painter, const Frame& f,
                          const PlotSpec& spec) const;
    void drawVectorField(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    // The five pictures drawVectorField dispatches between, one function each.
    // They were one 482-line body sharing a scope, which is how the feather
    // branch spent a release drawing the quiver's gridded arrows: the comment
    // said the two differed and the code did not, and nothing in the shape of
    // the function made that visible. Separate functions cannot share a branch
    // by accident.
    //
    // Each takes the gridded field rather than the raw columns, because that
    // gridding is the one step all of them except the feather have in common.
    void drawFeatherPlot(QPainter* p, const Frame& f, const PlotSpec& spec) const;
    void drawDerivedField(QPainter* p, const Frame& f, const PlotSpec& spec,
                          const ValueGrid& gu, const ValueGrid& gv,
                          bool vorticity) const;
    void drawStreamlines(QPainter* p, const Frame& f, const PlotSpec& spec,
                         const ValueGrid& gu, const ValueGrid& gv,
                         double magMax) const;
    void drawPhasePortraitPoints(QPainter* p, const Frame& f, const PlotSpec& spec,
                                 const ValueGrid& gu, const ValueGrid& gv) const;
    void drawQuiverArrows(QPainter* p, const Frame& f, const PlotSpec& spec,
                          const ValueGrid& gu, const ValueGrid& gv,
                          double magMax) const;
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
    // Every mapped column against every other. Draws its own panels and its
    // own frames, so it takes the target rather than a Frame.
    void drawPlotMatrix(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    // The network graph's edge list, laid out on a line.
    void drawArc(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    // Q, A, P and F against the IUGS igneous rock fields.
    void drawQapf(QPainter* p, const QRectF& target, const PlotSpec& spec,
                  bool volcanic) const;
    // Sand, silt and clay against the named texture classes. uk selects the
    // Soil Survey of England and Wales regions rather than the USDA ones.
    void drawSoilTexture(QPainter* p, const QRectF& target, const PlotSpec& spec,
                         bool uk) const;
    // Three parallel scales, with a check that the middle one is single-valued.
    void drawNomogram(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    // Segments of different lengths around a ring, linked at positions inside
    // them rather than at the segments themselves.
    void drawCircos(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
    // Chromosomes to one scale, with their banding and their centromeres.
    void drawKaryotype(QPainter* p, const QRectF& target, const PlotSpec& spec) const;
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
    // Two invariant masses with the kinematic boundary that confines them.
    void drawDalitz(QPainter* p, const Frame& f, const PlotSpec& spec) const;
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
    // The engine-rewrite chain, in six pieces - one per translation unit.
    //
    // prepareSpecCore was 19,600 lines inside a 1.78 MB source file that took
    // 2 minutes 10 seconds to compile by itself. A translation unit cannot be
    // divided across cores, so every build paid all of it for a change to any
    // one of 434 engines.
    //
    // Each returns the rewritten spec if it recognised the engine and nothing
    // if it did not, so prepareSpecCore can try them in order. std::optional
    // rather than a bool with an out parameter, because every block inside
    // them still says `return out;` exactly as it did - and so do the many
    // lambdas in there, which a bool signature would have meant telling apart
    // by hand, 312 times.
    //
    // They are called in the order they are numbered, and that order is part
    // of the behaviour: several engines are matched by a `startsWith` that a
    // later and more specific test would also match.
    std::optional<PlotSpec> prepareEngineGroup1(const PlotSpec& spec) const;
    std::optional<PlotSpec> prepareEngineGroup2(const PlotSpec& spec) const;
    std::optional<PlotSpec> prepareEngineGroup3(const PlotSpec& spec) const;
    std::optional<PlotSpec> prepareEngineGroup4(const PlotSpec& spec) const;
    std::optional<PlotSpec> prepareEngineGroup5(const PlotSpec& spec) const;
    std::optional<PlotSpec> prepareEngineGroup6(const PlotSpec& spec) const;

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
    mutable QRectF lastLegendRect_;
    // The rectangle the last frame was fitted to, so the colour bar can tell
    // whether it would fall off the edge of a very narrow figure.
    mutable QRectF lastTarget_;
    // Where the last render put each annotation, index-aligned with
    // spec.annotations and null for one that was not drawn. Filled by
    // drawAnnotations; read by the canvas to turn a click into a note.
    mutable QVector<QRectF> annotationBoxes_;
    mutable PreparedCache prepCache_;
    // 64-bit FNV-1a over everything prepareSpec can read. Cheap enough to run
    // per repaint (it is a linear pass over data that render already copies)
    // and specific enough that a changed sample, colour or axis bound misses.
    static quint64 specFingerprint(const PlotSpec& spec);

    const PlotSpec& preparedCached(const PlotSpec& spec) const;
public:
    // WHERE THE 3-D CUBE IS DRAWN, AND HOW BIG.
    //
    // Public so a caller can anchor a zoom on the pointer. The 2-D figures do
    // that from their own axis limits; the 3-D ones have no axis limits to read
    // and their framing comes out of makeProjection, which is internal. Asking
    // for the answer is the alternative to a second copy of that arithmetic in
    // PlotCanvas - and a second copy is exactly how the frame and the picture
    // come to disagree about where the figure is.
    //
    // `origin` is the screen point the cube's centre projects to and `scale`
    // converts projection units to pixels, which is everything needed to turn
    // "keep this screen point still" into a pan.
    struct CameraFrame {
        QPointF origin;
        double scale = 0.0;
        bool valid = false;
    };
    // No paint device: the margin is font metrics, and the item and the
    // projection both work in logical pixels, which is the space
    // QFontMetricsF measures in without one.
    CameraFrame cameraFrameFor(const PlotSpec& spec,const QRectF& target) const;
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

    // Will this engine draw a mapped series against the right-hand axis when
    // the mapping asks it to? Answered by asking the engine rather than by
    // listing names - see the definition, which explains why the probe is run
    // twice.
    bool honoursSecondaryAxis(const QString& engine) const;
private:
    // Notes on the figure, drawn last and clipped to the plot area. Real text,
    // so a PDF export carries words rather than outlines.
    void drawAnnotations(QPainter* p,const Frame& f,const PlotSpec& spec) const;
    QFont font(const PlotSpec& spec, double pointSize) const;
};

} // namespace graphvis
