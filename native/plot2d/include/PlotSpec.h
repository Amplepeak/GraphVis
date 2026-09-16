#pragma once
// =========================================================================
// PlotSpec - a renderer-agnostic description of one 2-D scientific figure.
//
// This is the contract every catalogue engine is written against, and the only
// thing a PlotBackend is given. It deliberately mirrors GraphVis 17's
// rendering/render_core.py PlotSpec and core/publication.py PublicationProfile
// so the 2116 catalogue entries can be ported without redesigning their inputs.
//
// Nothing here refers to Qt painting, wgpu or VTK: the same spec must be
// renderable by any backend, and must survive being written to a vector PDF.
// =========================================================================
#include <QColor>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <limits>
#include <algorithm>
#include <cmath>

namespace graphvis {

inline double unsetValue() { return std::numeric_limits<double>::quiet_NaN(); }
inline bool isUnset(double v) { return v != v; }

// HOW A VALUE BECOMES A POSITION ON THE RAMP - one answer, used by both halves.
//
// A choropleth of a skewed column on a linear ramp paints almost every region
// the same colour, and quantile classing is the standard cartographic answer to
// that. The reason it was not shipped is recorded in
// graphvis-choropleth-scale-decision.md and it is the right reason:
//
//     drawColourBar takes a linear lo..hi and draws a linear ramp with numbers
//     along it - it has no way to express log or quantile classing. Shipping
//     the classing without the key would produce a figure whose colours no
//     longer mean what the key says they mean.
//
// So the key learned first. THIS FUNCTION IS THE WHOLE POINT OF THE DESIGN: the
// engine that fills a region and the bar that explains the fill both ask it
// where a value sits, so they cannot disagree about what a colour means. Two
// copies of this arithmetic is the failure this project has met more than any
// other - "one question, one answer" - and here it would be invisible, because
// a wrong key and a wrong fill look exactly like a right key and a right fill.
struct ColourScale {
    enum Kind { Linear, Log10, Quantile };
    Kind kind = Linear;
    double lo = 0.0, hi = 1.0;
    // Quantile only: the class edges, ascending, size classes()+1. Empty for a
    // continuous ramp.
    QVector<double> breaks;

    int classes() const { return breaks.size()>=2 ? breaks.size()-1 : 0; }
    bool discrete() const { return kind==Quantile && classes()>0; }
};

// 0..1 along the ramp, or NaN when the value cannot be placed there.
//
// NaN rather than a clamp for a log scale that is handed a non-positive value:
// that value has no position on a logarithmic ramp, and colouring it as the
// bottom of the range would be inventing a reading. colourMapStyled already
// turns a non-finite position into "do not draw this", which is the honest
// picture.
inline double scalePosition(double v,const ColourScale& sc){
    if(v!=v) return std::numeric_limits<double>::quiet_NaN();
    if(sc.discrete()){
        // A class, not a gradient. The colour is constant across the class and
        // is taken from the MIDDLE of that class's share of the ramp, so a
        // five-class key is five distinct colours and not five samples that
        // happen to include both endpoints.
        const int n=sc.classes();
        for(int i=0;i<n;++i){
            const bool last=(i==n-1);
            if(v>=sc.breaks[i]&&(last ? v<=sc.breaks[i+1] : v<sc.breaks[i+1]))
                return (double(i)+0.5)/double(n);
        }
        // Outside every class - below the first break or above the last.
        return v<sc.breaks.first() ? 0.5/double(n) : (double(n)-0.5)/double(n);
    }
    if(sc.kind==ColourScale::Log10){
        if(!(v>0.0)||!(sc.lo>0.0)||!(sc.hi>0.0))
            return std::numeric_limits<double>::quiet_NaN();
        const double a=std::log10(sc.lo),b=std::log10(sc.hi);
        if(!(b>a)) return 0.5;
        return (std::log10(v)-a)/(b-a);
    }
    if(!(sc.hi>sc.lo)) return 0.5;
    return (v-sc.lo)/(sc.hi-sc.lo);
}

// The class edges for `classes` quantiles of `values`, ascending.
//
// Equal COUNT per class, which is what quantile classing means and why it
// helps: a skewed column gets as many regions in the dark class as in the pale
// one. Degenerate input - fewer values than classes, or every value the same -
// returns nothing, and the caller falls back to a linear ramp rather than
// drawing classes with identical edges.
inline QVector<double> quantileBreaks(QVector<double> values,int classes){
    QVector<double> out;
    if(classes<2) return out;
    values.erase(std::remove_if(values.begin(),values.end(),
                                [](double v){ return v!=v; }),
                 values.end());
    if(values.size()<classes) return out;
    std::sort(values.begin(),values.end());
    if(!(values.last()>values.first())) return out;
    out.reserve(classes+1);
    out.append(values.first());
    for(int i=1;i<classes;++i){
        const double pos=double(i)*double(values.size()-1)/double(classes);
        const int k=int(pos);
        const double frac=pos-double(k);
        const double a=values[qBound(0,k,values.size()-1)];
        const double b=values[qBound(0,k+1,values.size()-1)];
        out.append(a+(b-a)*frac);
    }
    out.append(values.last());
    // Ties can make two edges equal - a column that is half one value does
    // that - and two equal edges is a class nothing can fall into. Reported by
    // returning nothing, so the caller shows a linear ramp it can explain
    // rather than a key with a class of zero width on it.
    for(int i=1;i<out.size();++i)
        if(!(out[i]>out[i-1])) return QVector<double>();
    return out;
}


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
    // A CLOSED OUTLINE THAT IS A SOLID, not a loop of line.
    //
    // Two engines draw their bars as closed polylines on a Line Chart rather
    // than through drawBar, because drawBar reads each series as a GROUP and
    // stands them side by side - hand it a cumulative line and it comes back
    // as a second row of bars. The blocks that construction produced were
    // hollow: a Pareto chart of outlined rectangles, which is a chart nobody
    // draws, because the length of a bar is read from the ink in it.
    bool fillClosed = false;
    // DRAWN AGAINST THE RIGHT-HAND AXIS, not the left one.
    //
    // Several engines derive a second quantity of a completely different scale
    // from the same columns: a removal PERCENTAGE beside a concentration, the
    // POWER beside a cell voltage, a PHASE beside a gain, a cumulative SHARE
    // beside a count. Drawn against one ordinate the larger quantity takes the
    // axis and the smaller becomes a flat line along the bottom - a
    // polarisation curve whose voltage is a straight line at zero because the
    // power reaches sixty.
    //
    // This says the series belongs to a second ordinate with its own range,
    // drawn up the right-hand side. Set by the engine, because the engine is
    // what knows that the quantity it derived is not the one it was given.
    bool secondaryAxis = false;
    // NOT PART OF THE STACK IT IS DRAWN ON.
    //
    // A stacked painter adds every series it is given to the running total, and
    // that is right for every series that IS one of the parts. It is wrong for
    // a series that reports the whole: the VFA profile appends a "total VFA"
    // line whose values are the sum of the three species, and stacking that on
    // top of the three doubled the band - a peak of 1,480 mg/L drawn at 2,950,
    // with the annotation naming the true figure sitting halfway down the
    // picture beside it. The source comment claimed appending it last was
    // enough to keep it out of the stack. It was not; nothing read it.
    //
    // Clear this and the painter draws the series as a plain line in its own
    // values, and leaves the running total alone.
    bool stacked = true;
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

    // CATEGORIES, when the positions on this axis are not a scale.
    //
    // The tick chooser picks round numbers, which is right for a measurement
    // and wrong for a category: a confusion matrix came out with ticks at 0.5,
    // 1.5 and 2.5, and there is no class 1.5. The cells sit at integer indices,
    // so every label between them names something that does not exist.
    //
    // Empty means "choose ticks from the range", which is what every axis did
    // before and what nearly all of them still do. When these are set they are
    // the whole tick list: a value to place the tick at, and the text to write
    // under it, because an engine that indexes its categories 0..n-1 still has
    // to label them with what they actually were.
    //
    // Below `transform` for the same reason `transform` is last - a positional
    // initialiser must not silently acquire a new meaning - and after it
    // because these two are the fields most likely to grow.
    // Explicitly defaulted, and that matters: thirty-odd sites brace-
    // initialise this struct positionally and stop before these two, which
    // makes -Wmissing-field-initializers fire at every one of them. A member
    // with its own default initialiser is not "missing" from such a list, so
    // the = {} is what keeps the build at zero warnings.
    QVector<double> tickValues = {};
    QStringList tickLabels = {};
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
    // And the same for the vertical, separately.
    //
    // One number drove both axes, with the vertical derived as "one fewer" -
    // which is a fine DEFAULT ratio and a poor rule. The two axes of a figure
    // are rarely the same shape: a wide time series wants many ticks across and
    // few up, and a tall profile wants the reverse, and neither could be asked
    // for. 0 means "follow gridDensity, one fewer", so every figure made before
    // this existed still reads exactly as it did.
    int gridDensityY = 0;
    // HOW A PIE OR DONUT SAYS WHICH SLICE IS WHICH.
    //
    // It said nothing at all. A pie was drawn as coloured wedges with no names
    // and no key, which makes it a picture of some proportions rather than a
    // figure anybody can read - and unlike a line chart there is no axis to
    // fall back on.
    //
    // Both forms are legitimate and which is better depends on the figure, so
    // this is a choice rather than a default nobody can change: labels sit on
    // the slices and read well when there are few and their names are short; a
    // legend keeps the circle clean and copes with many slices and long names.
    //
    //   0  Legend beside the pie  (the default: it always fits)
    //   1  Labels on the slices
    //   2  Both
    //   3  Neither
    int pieLabels = 0;

    // WHICH WAY ROUND A POLAR FIGURE IS MEASURED.
    //
    // The painter drew zero at three o'clock and increased anticlockwise, which
    // is the mathematical convention and correct for a polar line or scatter.
    // It is not what a bearing means: a wind rose, a compass and a stereonet
    // are read from NORTH, clockwise, and drawn the mathematical way they come
    // out rotated a quarter turn and mirrored.
    //
    // Neither convention is right for every figure, so it is the operator's
    // choice rather than a rule this file invents. Default 0, which is exactly
    // what every figure drew before this existed.
    //
    //   0  Mathematical - 0 degrees at the right, increasing anticlockwise
    //   1  Compass      - 0 degrees at the top, increasing clockwise
    int polarConvention = 0;
    // WHAT A LEGEND DOES WITH A LABEL TOO LONG FOR ITS BOX.
    //
    // The box is capped at a third of the plot area's width, because a long
    // label unchecked makes the legend cover the figure it describes. That cap
    // is right; ELIDING to meet it is what is arguable, because these labels
    // carry the engine's computed answer - "7 categories, total 100; 3 of them
    // above the 80% line" becomes "7 categories, total 100; 3 of them ..." and
    // the finding is the part that got cut.
    //
    // There is no answer that is right for everyone: a reader comparing twenty
    // series wants short rows, and a reader quoting one number wants the
    // number. So it is offered rather than decided.
    //
    //   0  Elide  - one row per series, cut with an ellipsis. What every
    //               figure did before this existed.
    //   1  Wrap   - the row grows downward instead, so the whole label is
    //               readable and the box grows in the cheaper dimension.
    int legendLabels = 0;
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

    // WHAT RANGE the colour map runs over, and in how many steps.
    //
    // The map has always been stretched across whatever the data happened to
    // span, which is the right default and the wrong only-option: one outlier
    // at 160 puts the whole body of a distribution into the bottom eighth of
    // the ramp and every point that matters comes out the same dark blue. The
    // 3-D scatter that prompted this was exactly that picture.
    //
    // Unset means "fit to the data", which is what it did before.
    double colourMin = unsetValue();
    double colourMax = unsetValue();
    // What happens to a value OUTSIDE that range, which is a real choice and
    // not a detail:
    //
    //   false  clamp - it is drawn in the colour at the nearer end, which reads
    //          as "at least this much" and keeps every point on screen. The
    //          default, and what a capped scale means in most publications.
    //   true   drop  - it is not drawn at all, so the figure shows only the
    //          band that was asked for and the rest is background.
    //
    // Neither is right in general. Clamping keeps the shape of the cloud and
    // saturates its tail into one colour; dropping shows exactly the population
    // in range and silently removes the rest, which is honest in a figure with
    // a caption saying so and misleading in one without. The person chooses,
    // beside the range itself.
    bool colourOutOfRangeDropped = false;
    // HOW A VALUE IS PLACED ON THE RAMP, for every colour-mapped engine and not
    // only the choropleth.
    //
    // `ColourScale`, `scalePosition` and the classing-aware `drawColourBar`
    // were all built for the choropleth and all worked; the other ten
    // colour-bar call sites still passed a linear low/high pair, so a heat map
    // of a quantity spanning four orders of magnitude had no way to be read
    // except as "nearly all of it is the bottom colour".
    //
    // Linear is the default and `scalePosition` returns exactly
    // (v - lo) / (hi - lo) for it, so every existing figure is unchanged - the
    // 440-figure gallery is the proof of that, not an assumption.
    //
    // Log10 needs nothing but the range, so it reaches every mapped engine
    // through `rampPosition`. Quantile needs the distribution of the values
    // being coloured, which only the painter holds, so an engine opts into it
    // by filling `colourBreaks` - see `quantileBreaks`.
    ColourScale::Kind colourScaleKind = ColourScale::Linear;
    // Quantile only: the class edges, ascending. Empty means the continuous
    // ramp, whatever `colourScaleKind` says, because a quantile scale with no
    // edges is not a scale.
    QVector<double> colourBreaks;
    // How many classes to ask for when the person chooses quantile. The
    // painter turns this into `colourBreaks` from its own values.
    int colourClasses = 5;
    // 0 is the continuous ramp. Two or more turns it into that many discrete
    // bands - the difference between a photograph and a contour map, and what
    // lets a reader say "that region is the third band" at all. The colour bar
    // is drawn through the same function, so the key and the figure agree.
    int colourLevels = 0;

    // The person's OWN colours, when none of the eighty-four maps is the one
    // they want.
    //
    // Empty is every figure that has ever been drawn: the named map for a
    // field, the colour-vision palette for a set of series. Non-empty overrides
    // both, and it is deliberately ONE list rather than two, because "the
    // colours this figure uses, in order" is one idea wearing two hats:
    //
    //   a ramp        the stops, interpolated - or, when colourLevels is set,
    //                 band k is exactly colour k, so five bands and five
    //                 colours means picking each band by hand
    //   a set of      the cycle - series 0, 1, 2..., pie sector 0, 1, 2...,
    //   categories    taken in order and wrapped when there are more
    //                 categories than colours
    //
    // Which hat it wears is decided by the engine on screen, so the same list
    // follows a person from a heat map to a pie chart instead of being set up
    // twice.
    //
    // Nothing checks these for colour-vision safety, and that is the trade:
    // the generated palettes are measured through a dichromat projection and a
    // hand-picked set cannot be. The interface says so where the choice is
    // made rather than here.
    QVector<QColor> customColours;

    // The categorical cycle this figure should use, in order, already chosen
    // for the reader's colour vision.
    //
    // PlotCanvas computes seriesPalette(vision) and gives it to every series it
    // builds - but the engines that lay out a WHOLE rather than a set of series
    // never see a PlotSeries to take a colour from. Treemap, icicle, sunburst,
    // Sankey, chord and the rest generated their own hues with
    // QColor::fromHsvF, so the colour-vision setting did nothing on any of
    // them: someone switching to the Deuteranopia palette watched those engines
    // carry on drawing the same rainbow they could not read.
    //
    // Filled by PlotCanvas - from customColours when the user has picked their
    // own, otherwise from the measured palette. Empty means nobody set one, and
    // the painters fall back to the hue arithmetic they used before, so a spec
    // built by hand still draws.
    QVector<QColor> categoryPalette;

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
    // LINES ACROSS THE WIREFRAME, which is not the same thing as cells across
    // the grid.
    //
    // 3D Mesh and 3D Topography / Surface were the same picture at any
    // resolution a real dataset produces. They build the identical grid and
    // differ by one boolean, and the "mesh" was every quad of that grid
    // outlined - so at the 160 to 360 cells a survey gives you, the wireframe's
    // own strokes cover the gaps between them and it draws as a solid block.
    // The two engines were distinguishable only by zooming in far enough to see
    // a single cell.
    //
    // A mesh is a grid of lines you can see through, and how many lines that is
    // has nothing to do with how finely the surface underneath was sampled. So
    // this counts LINES, the geometry still follows every cell, and the surface
    // keeps its own resolution. 0 follows the grid, which is what it did
    // before.
    int meshDensity = 0;
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
    // PUT THERE BY THE ENGINE, not by the person.
    //
    // applyLimits refreshes the painting decisions on a cached prepared spec
    // from the caller's, annotations among them, because none of them is in
    // the fingerprint. That was written when only a caller could make one. Four
    // rewrites now do - the ternary corners, the VFA peak, the waffle
    // percentages and the animated line's step numbers - and every one of them
    // was being overwritten by the caller's list, which is normally empty. The
    // engines were producing notes that reached nothing.
    //
    // So the two are told apart: a derived note survives the refresh, a
    // caller's is replaced by the caller's current list.
    bool derived = false;
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
    // WHERE THE CUBE SITS, so a zoom can be about the pointer.
    //
    // The 2-D figures zoom about the cursor - zoomAt anchors on it - and the
    // 3-D ones scaled about the centre of the canvas, because zoom3DBy takes a
    // factor and no position. Zooming into the corner of a surface meant
    // zooming into the middle and then having nowhere to go.
    //
    // Held in the projection's own units, which is the space `project` works in
    // before it multiplies by the fitted scale. That makes it independent of
    // the canvas size: the same figure exported at another size, or the
    // full-resolution render beside the preview, frames identically. A pan in
    // pixels would not survive either.
    double panX = 0.0;
    double panY = 0.0;
};

// MOVING A FIGURE THAT HAS NO AXES TO MOVE.
//
// The 2-D pan and zoom work by changing the axis range: the data slides under a
// frame that stays put. That is the right model for a figure whose axes mean
// something, and it is no model at all for the sixty-six engines where
// engineHasAxes is false - a treemap, a sunburst, a Sankey, a chord diagram, a
// network graph, a word cloud, a flame graph, an UpSet plot. viewInteractive()
// is false for every one of them, so until now they could not be moved or
// magnified at all - and they are precisely the dense figures somebody most
// wants to get into.
//
// So this is the other kind of view: the drawing itself is translated and
// scaled INSIDE its frame, clipped to it. No data changes and nothing is
// recomputed. It is a magnifying glass held over the picture.
//
// HELD AS FRACTIONS OF THE FRAME rather than in pixels, for the same reason
// PlotView3D holds its pan in projection units: the same figure exported at
// another size, and the full-resolution render beside the preview, must frame
// identically. A pan in pixels would put the magnified part somewhere else in
// each of them.
struct PlotFrameView {
    double panX = 0.0;   // fraction of the frame's width, positive is right
    double panY = 0.0;   // fraction of the frame's height, positive is down
    double zoom = 1.0;
    // Compared exactly rather than with a tolerance: resetView writes literal
    // 0 and 1, so "has this been touched" has an exact answer, and a gesture
    // that lands a hair from the origin should still count as touched.
    bool active() const { return zoom != 1.0 || panX != 0.0 || panY != 0.0; }
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
    // The in-frame view, for the engines that have no axis range to slide.
    // Beside view3d because it is the same kind of thing: a way of looking at
    // the figure rather than a statement about the data, carried on the spec
    // so that the export and the full-resolution render show what is on
    // screen.
    PlotFrameView frameView;
    bool legendVisible = true;
    // Whether a RECTANGULAR FRAME belongs round this figure at all.
    //
    // `engineHasAxes` answers that for an engine drawn by its own painter - a
    // pie, a polar plot, a Piper diagram. It cannot answer it for an engine
    // that REWRITES onto one that does have axes: a ternary scatter becomes a
    // Line Chart so that drawLineChart can draw its triangle and its points,
    // and the frame decision was then taken on the name "Line Chart". The
    // triangle came out inside a box ruled 0.0 to 1.0 on both sides, measuring
    // nothing that appears in the diagram.
    //
    // So a rewrite that produces its OWN coordinate system says so here, and
    // the flag travels with the spec through however many derivations follow.
    bool framed = true;
    // Whether one unit on x must be the same length as one unit on y.
    //
    // Mohr's circle is a CIRCLE: its radius is the maximum shear stress and
    // you read it by looking at how round it is. Both axes carry the same
    // quantity in the same unit, so a frame that fits each to its own range
    // draws it as an ellipse - and there is nothing in the picture to say the
    // distortion is the frame's rather than the material's. The same is true
    // of an impedance locus, a shaft orbit, a hodograph and a Poincare plot.
    //
    // Set by the engine, applied in computeRange by widening whichever range
    // is short of the other. Widening rather than cropping, because cropping
    // would hide data to keep a shape.
    bool equalAspect = false;
    // The right-hand ordinate: its label, and whether it is logarithmic.
    // Its RANGE is measured from the series that asked for it, exactly as the
    // left-hand one is measured from the series that did not. Unused - and no
    // axis drawn - unless some series sets `secondaryAxis`.
    PlotAxis y2Axis;
    // Notes on the figure. Drawn last, over everything, and clipped to the plot
    // area - a note whose anchor has been zoomed out of view must not appear
    // among the axis labels.
    QVector<PlotAnnotation> annotations;

    // One line under the figure saying what this picture CANNOT show.
    //
    // Unlike an annotation it is not anchored to a point and is not about the
    // data: it is about the figure itself - a colour map substituted for a
    // colour-vision mode that can no longer mark a diverging centre, an
    // estimator whose assumption the data does not meet, a projection that had
    // to drop a dimension. Limits like those were recorded only in the
    // catalogue entry, where a reader holding the exported PDF never sees them.
    //
    // Exported with the figure, deliberately, for that same reason.
    QString figureNote;

    // Constants an engine needs that are NOT per-row data.
    //
    // A Dalitz plot needs the parent mass and three daughter masses; those are
    // properties of the decay, not of an event, and the only way to give them
    // to an engine before this was a column repeating one number down every
    // row. That works and is what the first Dalitz shipped as, but it costs
    // four column slots to carry four numbers, it cannot express a value the
    // dataset does not already contain, and every engine wanting a constant
    // would have cost four more.
    //
    // Keyed by a short ASCII name the engine chooses, declared through
    // QtPlotBackend::engineParameters() so the interface can build controls
    // for them without knowing what any of them mean. Absent means "use the
    // declared default", which is why this is a map and not a fixed struct:
    // an engine that gains a parameter must not invalidate every figure made
    // before it had one.
    QMap<QString,double> parameters;
    double parameter(const QString& key,double fallback) const {
        const auto it=parameters.constFind(key);
        if(it==parameters.constEnd()) return fallback;
        return isUnset(*it)?fallback:*it;
    }
};

} // namespace graphvis
