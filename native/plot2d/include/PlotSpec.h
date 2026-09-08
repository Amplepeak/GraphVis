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
    // Numbers on the axes. On, because an axis without a scale is a picture of
    // a shape rather than a measurement - which is what the 3-D engines were
    // drawing: three named axes, a coloured surface, and no way to read a
    // single value off it. Off is for a figure whose shape is the whole point,
    // and for a thumbnail too small to read anything.
    bool scaleLabelsVisible = true;
    // Roughly how many labelled ticks each axis should aim for, and so how many
    // grid lines there are. 0 keeps the long-standing default of 7 across and 6
    // up; the tick chooser still rounds to a round number, so this is a target
    // rather than a count - asking for 12 on a range of 0 to 1 gives steps of
    // 0.1, not 0.0833.
    int gridDensity = 0;
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
    // The colour map for every engine that colours a FIELD rather than a
    // series: heatmap, contour, surface, 3-D field, vector field. Empty means
    // Viridis, which is what all of them were hard-coded to before this
    // existed. "Magma", "Inferno", "Plasma", "Cividis" and "Greys" are the
    // other sequential maps; "Coolwarm" and "BrBG" are diverging, for a field
    // with a meaningful zero. An unrecognised name falls back to Viridis
    // rather than failing, because a typo in a theme should not blank a plot.
    QString colourMap;

    // What to do with the cells of a gridded field that no sample landed in.
    //
    //   0  None     leave them empty - the background shows through
    //   1  Nearest  each empty cell takes its nearest measured cell's value
    //   2  Linear   a smooth surface fitted through the measured cells
    //   3  Cubic    the same, smoothed again, for a continuous-looking map
    //
    // Scattered measurements almost never fall one to a cell. A parameter
    // sweep dense at one end of its range and thin at the other leaves most of
    // the grid unsampled, and drawing only the cells that were hit produces a
    // field of coloured confetti on a black ground rather than a map - which
    // is what this application did, because it only ever had mode 0.
    //
    // None is honest and unreadable; the others estimate. That is the trade a
    // person makes deliberately, which is why it is a control and not a
    // constant. Density engines ignore this entirely: for a count, an empty
    // cell is a measured zero rather than an unknown.
    int fieldInterpolation = 2;

    // Cells across the gridded field. 0 chooses from the sample count. Higher
    // is smoother once the gaps are filled, and meaningless while they are not.
    int fieldResolution = 0;
};

// A note on the figure: a label at a point, optionally with a leader line back
// to it. The thing every plotting tool has and this one did not - "this is the
// breakthrough", "instrument replaced here", "n = 3 for these" - which is the
// difference between a figure someone can read and one that needs the person
// who made it standing next to it.
//
// The position is in DATA coordinates, not pixels, so the note stays on the
// point it is about when the axes are zoomed, the figure is resized or the
// same spec is exported at a different size. offsetX/offsetY move the text
// away from that point, in points (1/72 inch), so the note does not sit on top
// of the thing it is pointing at.
struct PlotAnnotation {
    double x = 0.0;
    double y = 0.0;
    QString text;
    // Where the text sits relative to the anchor, in typographic points. The
    // default puts it up and to the right, clear of a rising curve.
    double offsetX = 12.0;
    double offsetY = -18.0;
    // Draw a line from the text back to the anchor. Off when the text sits
    // close enough that a line would be clutter.
    bool leader = true;
    // Empty means the figure's foreground colour, so a note follows the theme
    // unless it was deliberately given a colour of its own.
    QColor color;
};

// Where the camera is, for the engines drawn as a projection rather than
// against a pair of axes: the 3-D family, the surfaces, the vector and volume
// fields. The 2-D engines carry their view on PlotAxis::min/max; these had
// nowhere to carry one, so the angles were written into the painter as -35 and
// 24 and the figure could not be turned.
//
// Angles are degrees and zoom is a multiplier on the fitted size, so the
// defaults are the view the figure has always had and 1.0 means "as large as
// fits". It lives on the spec, which means a rotated figure exports rotated
// and the full-resolution render agrees with the preview.
struct PlotView3D {
    double azimuth = -35.0;
    double elevation = 24.0;
    double zoom = 1.0;
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
    PlotView3D view3d;
    bool legendVisible = true;
    // Notes on the figure. Drawn last, over everything, and clipped to the plot
    // area - a note whose anchor has been zoomed out of view must not appear
    // among the axis labels.
    QVector<PlotAnnotation> annotations;
};

} // namespace graphvis
