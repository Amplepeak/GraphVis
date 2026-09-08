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

// How an axis's values are transformed before they are drawn.
//
// Log10 is the odd one out and stays that way deliberately: it is an AXIS
// property, drawn with decade ticks and 10^n labels, so the numbers on the page
// are the original data. The other three rewrite the values, because that is
// what they are for - a z-score axis is measured in standard deviations and a
// quantile axis in fractions of the sample, and showing the original units on
// either would be a label that contradicts the picture.
//
//   Linear    as measured
//   Log10     decade ticks; values must be positive
//   Log1p     log10(1 + x); the log axis for data that legitimately reaches
//             zero, where Log10 has to drop those rows entirely
//   ZScore    (x - mean) / sd; puts two columns of different units side by side
//   Quantile  each value replaced by its position in the sorted sample, 0 to 1;
//             makes a heavily skewed column readable by spreading it evenly
enum AxisTransform {
    AxisLinear = 0,
    AxisLog10 = 1,
    AxisLog1p = 2,
    AxisZScore = 3,
    AxisQuantile = 4,
};

struct PlotAxis {
    QString label;
    // Kept as its own flag because fifty-odd places in the backend read it to
    // decide tick placement, mathtext labelling and whether a non-positive
    // value can be drawn at all. transform == AxisLog10 keeps it in step.
    bool log10 = false;          // decade ticks, mathtext power labels
    double min = unsetValue();   // unset -> fit to data
    double max = unsetValue();
    // Drawn with the larger value at the origin end. Depth increases downward,
    // magnitude increases downward, rank 1 belongs at the top - and every one
    // of those was previously drawn by negating the data, which puts minus
    // signs on the ticks and makes the axis label a lie.
    bool inverted = false;
    // LAST on purpose. Thirty-odd sites in the backend build an axis with a
    // positional initialiser - PlotAxis{name, false, unsetValue(), unsetValue()}
    // - and a field inserted anywhere above this silently changes what each of
    // those numbers means. It compiled as a narrowing error here; it would not
    // have on a field of the same type.
    int transform = AxisLinear;
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
    // The SCATTERED estimator, when one is chosen. -1 keeps the grid-filling
    // path above, which is what every existing figure uses.
    //
    // The difference between the two is not a quality setting, it is where the
    // estimate comes from. Filling bins the measurements FIRST - every sample
    // in a cell becomes one number at the cell's centre - and then guesses at
    // the empty cells, so a sample near a cell edge and one at its centre are
    // indistinguishable and no amount of cleverness afterwards can recover what
    // the binning discarded. A scattered estimator uses each measurement where
    // it actually is. Filling is cheap and good enough for a dense sweep;
    // estimating is what a thin one needs. See SurfaceEstimators.h.
    int fieldEstimator = -1;        // index into graphvis::estimatorNames()
    int fieldExtrapolation = 0;     // what happens outside the measured region
    int fieldValuePolicy = 0;       // may an estimate exceed what was measured
    int fieldResponseSpace = 0;     // estimate in linear or log10 values
    int fieldNeighbours = 32;       // sample size for the local methods
    double fieldIdwPower = 2.0;
    double fieldSmoothing = 0.0;    // 0 interpolates exactly
    // What a failed run masks, which masks come back, and what a mask shows.
    // A sweep that solves an ODE at every point does not always converge, and
    // a non-finite response is an outcome rather than a missing row.
    int fieldFootprint = 0;         // how much area one failure takes out
    int fieldBridging = 1;          // which masked regions may be filled again
    int fieldBridgeMaxCells = 4;
    int fieldInvalidDisplay = 0;    // what a cell that is still masked shows
    int fieldKrigingVariogram = 0;  // 0 exponential, 1 spherical, 2 gaussian
    double fieldLoessFraction = 0.25;
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
    // The THIRD mapped column, for the engines that take one - which is most of
    // the interesting ones. It is the height on a 3-D engine and the quantity
    // the colour map runs over on a heat map, contour or surface, so what it is
    // CALLED depends on the engine; what it is remains "the third column", and
    // that is why there is one axis here rather than a zAxis and a colourAxis
    // that could never both be true at once.
    //
    // Only the transform and the label are read from it. The 3-D painters fit
    // their own bounds to the projected cube and a colour map runs 0..1 over
    // the field, so min and max here would be settings with nothing to set.
    PlotAxis zAxis;
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
