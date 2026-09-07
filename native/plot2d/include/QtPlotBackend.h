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

    // Exposed for testing and for reuse by future backends that want Qt to
    // draw the chrome over their own data layer.
    static QVector<AxisTick> linearTicks(double lo, double hi, int wanted);
    static QVector<AxisTick> logTicks(double lo, double hi);

private:
    struct Frame {
        QRectF plotArea;      // where series are drawn
        double xLo=0, xHi=1;  // data-space bounds, already log10 if log
        double yLo=0, yHi=1;
        bool xLog=false, yLog=false;
    };
    Frame computeFrame(QPainter* p, const QRectF& target, const PlotSpec& spec,
                       QVector<AxisTick>& xTicks, QVector<AxisTick>& yTicks) const;
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
    // Quiver, cones, stream tubes and ribbons, tensor glyphs and the volume
    // family: six mapped columns for a vector field, four for a scalar volume.
    void draw3DField(QPainter* p, const QRectF& target, const PlotSpec& spec) const;

    // Some engines are not a direct plot of the mapped columns: a histogram
    // draws the distribution of one column, a box plot draws its quartiles.
    // They rewrite the spec into drawable geometry before the frame and axes
    // are computed, so the generic axis code needs no special cases.
    PlotSpec prepareSpec(const PlotSpec& spec) const;

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
private:
    static bool engineHasAxes(const QString& engine);

    QFont font(const PlotSpec& spec, double pointSize) const;
};

} // namespace graphvis
