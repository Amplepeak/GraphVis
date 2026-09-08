#pragma once
// =========================================================================
// PlotSpec - a renderer-agnostic description of one 2-D scientific figure.
//
// This is the contract every catalogue engine is written against, and the only
// thing a PlotBackend is given. It deliberately mirrors GraphVis 17's
// rendering/render_core.py PlotSpec and core/publication.py PublicationProfile
// so the 318 catalogue entries can be ported without redesigning their inputs.
//
// Nothing here refers to Qt painting, wgpu or VTK: the same spec must be
// renderable by any backend, and must survive being written to a vector PDF.
// =========================================================================
#include <QColor>
#include <QString>
#include <QVector>
#include <limits>

namespace graphvis {

inline double unsetValue() { return std::numeric_limits<double>::quiet_NaN(); }
inline bool isUnset(double v) { return v != v; }

// One drawn series. x and y are parallel and already cleaned of non-finite
// pairs by the caller, because a backend must never have to decide what a
// failed solver point means - GraphVis 17 treats that as a data decision.
struct PlotSeries {
    QString label;
    QVector<double> x;
    QVector<double> y;
    QColor color = QColor(0x4f, 0x9d, 0xf7);
    double lineWidth = 1.2;
    bool drawLine = true;
    bool drawMarkers = false;
    double markerSize = 4.0;
    double opacity = 1.0;
    // Set when a caller has deliberately chosen a width or a marker size, so a
    // publication profile knows what it may override. PublicationProfile used
    // to detect "untouched" by comparing against the defaults above, but
    // PlotCanvas always sets markerSize to 4.5 or 3.0 and never 4.0 - so the
    // journal's marker size could never apply, and the line width stopped
    // applying the moment anyone changed it on screen.
    bool lineWidthExplicit = false;
    bool markerSizeExplicit = false;
    // Set only by the Monochrome colour-vision mode, where colour cannot carry
    // the series on its own. Empty means a solid line. Units are pen widths, so
    // the pattern scales with lineWidth and survives PDF export unchanged.
    QVector<qreal> dashPattern;
};

struct PlotAxis {
    QString label;
    bool log10 = false;          // decade ticks, mathtext power labels
    double min = unsetValue();   // unset -> fit to data
    double max = unsetValue();
    // Drawn with the larger value at the origin end. Depth increases downward,
    // magnitude increases downward, rank 1 belongs at the top - and every one
    // of those was previously drawn by negating the data, which puts minus
    // signs on the ticks and makes the axis label a lie.
    bool inverted = false;
};

// Typography and geometry, taken straight from a GraphVis 17 publication
// profile (core/publication.py). Sizes are points, widths are inches, so a
// backend can honour "Nature single-column: 89 mm, 6 pt, 0.75 pt lines"
// exactly whether it is drawing to a screen or to a PDF.
struct PlotStyle {
    QString fontFamily = QStringLiteral("Arial");
    double baseFontSize = 8.0;
    double titleSize = 10.0;
    double axisLabelSize = 8.0;
    double tickSize = 7.0;
    double legendSize = 7.0;
    double lineWidth = 1.2;
    bool gridVisible = true;
    int dpi = 100;
    double figureWidthIn = unsetValue();
    double figureHeightIn = unsetValue();
    QColor background = QColor(0x11, 0x18, 0x20);
    QColor foreground = QColor(0xdb, 0xe6, 0xf0);
    QColor gridColor = QColor(0x26, 0x38, 0x4f);
    // Semantic colours, for the engines where up and down are the finding -
    // a slope graph, a waterfall, a residual - rather than a series identity.
    // Defaults match the Theme tokens the interface uses.
    QColor positive = QColor(0x61, 0xd1, 0x95);
    QColor warning  = QColor(0xe2, 0xad, 0x50);
    QColor danger   = QColor(0xde, 0x62, 0x54);
};

struct PlotSpec {
    QString engine;   // catalogue engine key, e.g. "Line Chart", "3D Scatter"
    QString variant;  // catalogue scale variant, e.g. "Semi-Log X"
    QString title;
    PlotAxis xAxis;
    PlotAxis yAxis;
    QVector<PlotSeries> series;
    // The formula, for the Function and Implicit engines. Those catalogue
    // entries plot an expression rather than a dataset, so the text has to
    // travel with the spec; every other engine ignores it.
    QString expression;
    PlotStyle style;
    bool legendVisible = true;
};

} // namespace graphvis
