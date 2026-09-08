#pragma once

// Estimating a field from scattered measurements.
//
// A heat map, a contour or a surface needs a value at every node of a regular
// grid, and a sweep gives you values wherever the runs happened to land. What
// happens in between is not a detail: it is the whole picture. GraphVis 17 knew
// that and offered seventeen estimators in five families, five extrapolation
// policies, and a set of masking rules for solver failures; this port shipped
// four hole-filling passes over an already-binned grid, which is a different and
// much weaker thing.
//
// The difference is worth stating plainly, because it is the reason this file
// exists rather than another mode in estimateEmptyCells(). Binning first throws
// the measurements away: every sample in a cell becomes one number at the cell's
// centre before anything is estimated, so a sample near a cell edge and one at
// its centre are indistinguishable, and no estimator can recover what the
// binning discarded. Estimating from the SCATTERED POINTS uses each measurement
// where it actually is.
//
// Everything here is verified against the Python it replaces
// (reference/gv17-src/graphvis/rendering/surface_estimators.py) by recovering
// known fields: an estimator that interpolates must return a measured value at
// a measured point, and every one of these is checked for that at machine
// precision.

#include <QString>
#include <QStringList>
#include <QVector>

namespace graphvis {

// One measurement, in the space the axes are drawn in - so a logarithmic axis
// hands over log10 of the coordinate, and distances mean what they look like
// on the page rather than what they mean in the raw units.
struct ScatterPoint {
    double x = 0.0;
    double y = 0.0;
    double v = 0.0;
};

// A regular grid of estimates, and what is known about each node.
struct EstimatedField {
    int nx = 0, ny = 0;
    double xLo = 0, xHi = 1, yLo = 0, yHi = 1;
    QVector<double> value;      // NaN where nothing is known
    QVector<bool> outsideHull;  // node lies outside the measured region
    QVector<bool> estimated;    // node had no measurement of its own
    bool valid = false;
};

// How the field between the measurements is estimated.
//
// The order and the names are GraphVis 17's, so a saved figure and a person's
// habits both carry over. Auto is not a seventeenth method - it chooses among
// the others from the shape of the data, which is what makes it the default.
enum class Estimator {
    Auto,
    // Structured: for measurements that already lie on a complete rectangular
    // grid, where the general scattered machinery is both slower and worse.
    NearestNeighbour,
    Bilinear,
    Bicubic,
    RectangularBSpline,
    MonotonePchip,
    // Unstructured: the general case.
    DelaunayLinear,
    CloughTocher,
    NaturalNeighbour,
    InverseDistance,
    ModifiedShepard,
    // Radial basis functions: one global solve, smooth everywhere.
    ThinPlateSpline,
    Multiquadric,
    GaussianRbf,
    // Statistical and local regression.
    OrdinaryKriging,
    MovingLeastSquares,
    Loess,
    Count
};

// What happens beyond the measured region.
//
// The default masks it, and that is a scientific position rather than a
// cosmetic one: the convex hull of the samples is the boundary of what was
// measured, and colouring beyond it presents an extrapolation in the same ink
// as a measurement. The full-domain modes are offered because sometimes the
// rectangle IS the experiment and the corners simply failed.
enum class Extrapolation {
    MaskOutsideHull,
    NearestOutsideHull,
    IdwFullDomain,
    LinearThenNearest,
    EdgeClamped,
    Count
};

// Whether an estimator may return a value outside the range that was measured.
// A spline through noisy data overshoots, and an overshoot on a colour scale
// reads as a peak nobody recorded.
enum class ValuePolicy { AllowOvershoot, ClampToObserved, Count };

// The space the RESPONSE is estimated in, which is not the same question as
// what the axes do. A quantity spanning four decades interpolates badly in
// linear space: halfway between 1 and 10000 is 5000, and on a log response it
// is 100.
enum class ResponseSpace { Linear, Log10, Count };

struct EstimatorSettings {
    Estimator estimator = Estimator::Auto;
    Extrapolation extrapolation = Extrapolation::MaskOutsideHull;
    ValuePolicy valuePolicy = ValuePolicy::AllowOvershoot;
    ResponseSpace responseSpace = ResponseSpace::Linear;
    int resolution = 160;        // nodes across the longer side
    int neighbours = 32;         // for the local methods
    double idwPower = 2.0;       // exponent in the inverse-distance weight
    double loessFraction = 0.25; // share of the sample in each local fit
    double smoothing = 0.0;      // RBF regularisation; 0 interpolates exactly
    double xWeight = 1.0;        // per-axis distance weights, so a sweep that is
    double yWeight = 1.0;        // dense in x and thin in y is not smeared
};

// The names, in the order and the words GraphVis 17 used, so the interface and
// a saved figure can both round-trip through them.
QStringList estimatorNames();
// Which of the seventeen this build actually computes.
//
// The list of names is the full GraphVis 17 set deliberately - a saved figure
// naming an estimator has to round-trip through it - but an estimator that is
// named and not implemented is worse than one that is absent: it draws
// something under a name that means something else, which is how a plotting
// library ends up with forty subtly different ways to draw the same line. An
// unimplemented choice falls back to Auto and says so, and the interface offers
// only what this returns true for.
bool estimatorImplemented(Estimator e);
QStringList estimatorGroupNames();      // one group per estimator, parallel list
QStringList extrapolationNames();
QStringList valuePolicyNames();
QStringList responseSpaceNames();

// Estimate the field. Returns an invalid field if there is nothing to work
// from; never throws, and never returns a non-finite value where `value` is
// marked finite.
EstimatedField estimateField(const QVector<ScatterPoint>& points,
                             int nx, int ny,
                             double xLo, double xHi, double yLo, double yHi,
                             const EstimatorSettings& settings);

// ---------------------------------------------------------------------------
// The geometry the estimators are built on, exposed because the masking
// policies and the tests both need it.

// Convex hull of the measured points, anticlockwise, by monotone chain.
QVector<int> convexHull(const QVector<ScatterPoint>& points);
// True when (x,y) is inside or on the hull.
bool insideHull(const QVector<ScatterPoint>& points, const QVector<int>& hull,
                double x, double y);

struct Triangle { int a=0,b=0,c=0; };
// Delaunay triangulation by Bowyer-Watson. Three of the estimators and the
// hull masking all read it, so it is built once and shared.
QVector<Triangle> delaunay(const QVector<ScatterPoint>& points);

} // namespace graphvis
