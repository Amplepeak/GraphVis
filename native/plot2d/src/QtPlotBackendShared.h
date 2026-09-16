#pragma once
// ============================================================================
// QtPlotBackend's shared helpers.
//
// INTERNAL. Not installed and not part of the plot2d interface: every name here
// lived in an anonymous namespace inside QtPlotBackend.cpp until that file had
// to be split, and it is here only so the pieces can still see it.
//
// WHY THE SPLIT. QtPlotBackend.cpp was 1.78 MB in ONE translation unit and took
// 2 minutes 10 seconds to compile by itself - single-threaded, on every build,
// because a translation unit cannot be divided across cores. Any change to any
// one of 434 engines paid all of it.
//
// The contents are the seventeen anonymous-namespace blocks of that file, in
// their original order, which is also their dependency order. That was checked
// before anything moved: nothing in them calls anything defined outside them in
// that file, and nothing in them mentions QtPlotBackend. Moving them is a
// transposition, not a rewrite, and the whole gallery is byte-identical across
// it.
//
// Functions are `inline` rather than `static` deliberately. Static would give
// every including translation unit its own copy, which for the pure ones is
// only waste - but the depth counter below is not pure, and copies of THAT
// would silently disable the guard it exists to be.
// ============================================================================
#include "QtPlotBackend.h"
#include "ColourMaps.h"
#include "ColourVision.h"
#include "SurfaceEstimators.h"
#include "Expression.h"

#include <cmath>
#include <limits>

#include <QFontMetricsF>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QPainter>
#include <QPainterPath>
#include <QVector3D>
#include <QtMath>
#include <QElapsedTimer>
#include <algorithm>

namespace graphvis {

// A series' dash pattern, when it has one. Only the Monochrome colour-vision
// mode sets these: with no hue to distinguish series, the line style is what
// tells them apart, and it has to survive vector export as geometry rather than
// as a colour.
inline void applySeriesDash(QPen& pen,const PlotSeries& s){
    if(s.dashPattern.isEmpty()) return;
    pen.setDashPattern(s.dashPattern);
    pen.setCapStyle(Qt::FlatCap);
}


constexpr double kMarginLeft   = 64.0;
constexpr double kMarginRight  = 18.0;
constexpr double kMarginTop    = 34.0;
constexpr double kMarginBottom = 52.0;
constexpr double kTickLen      = 4.0;

// `finite`, `Bounds` and `Projection` moved to QtPlotBackend.h - the class
// there now declares member functions that take them. See the note beside them.

// Superscript digits let a log decade label read as 10^-1 without a full
// mathtext engine. GraphVis 17 renders these through Matplotlib mathtext;
// the visible result is the same for the exponents an axis actually uses.
inline QString superscript(int exponent){
    static const QChar digits[10]={u'⁰',u'¹',u'²',u'³',u'⁴',
                                   u'⁵',u'⁶',u'⁷',u'⁸',u'⁹'};
    QString out;
    if(exponent<0){ out+=QChar(u'⁻'); exponent=-exponent; }
    const QString plain=QString::number(exponent);
    for(const QChar c:plain) out+=digits[c.digitValue()];
    return out;
}

// "Nice" step: 1, 2, 5 or 10 times a power of ten. Standard axis rounding, and
// what makes ticks land on readable values instead of raw data extremes.
inline double niceStep(double rough){
    if(!(rough>0)) return 1.0;
    const double exp10=std::pow(10.0,std::floor(std::log10(rough)));
    const double f=rough/exp10;
    double nice;
    if(f<1.5) nice=1.0; else if(f<3.0) nice=2.0; else if(f<7.0) nice=5.0; else nice=10.0;
    return nice*exp10;
}

// A MEASURED QUANTITY, WRITTEN THE WAY SOMEONE REPORTING IT WOULD.
//
// `QString::number(v,'g',n)` leaves plain notation as soon as the value needs
// more digits before the point than n, so the threshold that decides the
// notation belongs to the FORMAT and not to the quantity. That put two
// notations in one label, in one unit, twice over: a mass haul reading
// "net 7555, most cut 1.376e+04", and a stress-strain curve reading
// "E 7e+04, UTS 410, yield 271.1" - all four numbers megapascals.
//
// It also destroyed one. An O-C diagram's epoch is a Julian date, and a Julian
// date needs seven figures before the point; at three it came out "2.45e+06",
// which names a five-thousand-day window rather than a night.
//
// This rounds to `significant` figures and then writes the result out in full,
// falling back to exponent form only where a person would use it too - beyond
// a billion, or below a ten-thousandth.
//
// DELIBERATELY NOT APPLIED EVERYWHERE. Several quantities in this file are
// always written in exponent form by the people who read them - a genome-wide
// threshold of 5e-8, an Arrhenius pre-exponential of 4.18e+09, a relative
// roughness of 1e-05, a Paris coefficient, a bit error rate of 1e-12 - and
// rewriting those as long strings of digits would be the same mistake in the
// other direction.
inline QString formatMeasured(double v,int significant=4){
    if(!finite(v)) return QStringLiteral("-");
    const double m=std::abs(v);
    if(m==0.0) return QStringLiteral("0");
    if(m<1e-4||m>=1e9) return QString::number(v,'g',significant);
    const int magnitude=int(std::floor(std::log10(m)));
    const int decimals=qBound(0,significant-1-magnitude,9);
    QString s=QString::number(v,'f',decimals);
    if(s.contains(QLatin1Char('.'))){
        while(s.endsWith(QLatin1Char('0'))) s.chop(1);
        if(s.endsWith(QLatin1Char('.'))) s.chop(1);
    }
    if(s==QLatin1String("-0")) s=QStringLiteral("0");
    return s;
}

inline QString formatTick(double v,double step){
    // Decimals needed so neighbouring ticks are distinguishable.
    int decimals=0;
    if(step>0 && step<1.0) decimals=int(std::ceil(-std::log10(step)));
    decimals=qBound(0,decimals,9);
    if(std::abs(v)>=1e5||(v!=0.0&&std::abs(v)<1e-4))
        return QString::number(v,'e',2);
    QString s=QString::number(v,'f',decimals);
    if(s==QLatin1String("-0")) s=QStringLiteral("0");
    return s;
}


inline double normalQuantile(double p);


inline QVector<double> finiteValues(const PlotSeries& s){
    QVector<double> v;
    v.reserve(s.y.size());
    for(double y:s.y) if(finite(y)) v.append(y);
    return v;
}

// Second moments of a pair of columns, computed once and shared.
//
// This two-pass loop was written out four times - Correlation Matrix,
// Covariance Matrix, Confidence Ellipse and Scatter + Marginals - with slightly
// different zero guards and different normalisation (used-1 in one place, n-1
// in another, none in a third). That is how two panels showing the same two
// columns end up reporting different numbers for r.
//
// Pairs where either value is non-finite are dropped, which is the only sound
// thing to do: a covariance computed over different subsets per column is not
// a covariance.
struct Moments2 {
    int n=0;            // finite PAIRS, not rows
    double mx=0.0, my=0.0;
    double sxx=0.0, syy=0.0, sxy=0.0;   // sums of squares, NOT divided
};

inline Moments2 momentsOf(const QVector<double>& a,const QVector<double>& b){
    Moments2 m;
    const int len=qMin(a.size(),b.size());
    double sa=0.0, sb=0.0;
    for(int i=0;i<len;++i){
        if(!finite(a[i])||!finite(b[i])) continue;
        sa+=a[i]; sb+=b[i]; ++m.n;
    }
    if(m.n<2) return m;
    m.mx=sa/double(m.n); m.my=sb/double(m.n);
    for(int i=0;i<len;++i){
        if(!finite(a[i])||!finite(b[i])) continue;
        const double da=a[i]-m.mx, db=b[i]-m.my;
        m.sxx+=da*da; m.syy+=db*db; m.sxy+=da*db;
    }
    return m;
}

// Pearson's r. A constant column has NO correlation with anything - it is not
// perfectly correlated, which dividing by zero would imply.
inline double pearson(const Moments2& m){
    if(m.n<2) return 0.0;
    const double denom=std::sqrt(m.sxx*m.syy);
    return (denom>1e-15)? m.sxy/denom : 0.0;
}

// Sample covariance, normalised by n-1 to match pearson()'s inputs.
inline double covarianceOf(const Moments2& m){
    if(m.n<2) return 0.0;
    return m.sxy/double(m.n-1);
}

// The colour for category `index`, from the figure's own categorical cycle.
//
// Every engine that lays out a whole rather than a set of series built its own
// hues with QColor::fromHsvF. Those are perfectly good hues and completely
// blind to the colour-vision setting, so switching to a dichromat palette
// changed the line charts and left the treemap, the icicle, the sunburst and
// the Sankey exactly as unreadable as they were.
//
// The fallback is the arithmetic those engines already used, so a spec built
// without a palette - by hand, or by a test - still draws what it always did.
// `saturation` and `value` apply only to that fallback: a measured palette is
// measured as it is, and re-saturating it would undo the measurement.
inline QColor categoryColour(const PlotSpec& spec,int index,
                      double fallbackHue,double saturation,double value,
                      double alpha=1.0){
    const QVector<QColor>& palette=spec.style.categoryPalette;
    QColor out;
    if(!palette.isEmpty()){
        const int n=palette.size();
        out=palette.at(((index%n)+n)%n);
    }else{
        out=QColor::fromHsvF(fallbackHue,saturation,value);
    }
    if(alpha<1.0) out.setAlphaF(alpha);
    return out;
}

inline double quantileOf(const QVector<double>& sorted,double q){
    if(sorted.isEmpty()) return 0.0;
    const double pos=q*(sorted.size()-1);
    const int i=int(pos);
    const double frac=pos-i;
    return (i+1<sorted.size())? sorted[i]*(1.0-frac)+sorted[i+1]*frac : sorted[i];
}

inline double meanOf(const QVector<double>& v){
    if(v.isEmpty()) return 0.0;
    double sum=0.0; for(double x:v) sum+=x;
    return sum/double(v.size());
}

inline double stdevOf(const QVector<double>& v){
    if(v.size()<2) return 0.0;
    const double m=meanOf(v);
    double acc=0.0; for(double x:v) acc+=(x-m)*(x-m);
    return std::sqrt(acc/double(v.size()-1));
}

// One Gaussian kernel density estimate, with Silverman's rule for the
// bandwidth. KDE Density, Violin/Raincloud and Ridgeline each carried their own
// copy of this, identical except for the grid size - which is exactly how three
// panels end up quietly disagreeing about the same column.
//
// `sorted` must be sorted ascending: the kernel cutoff below relies on it.
struct KdeCurve {
    bool valid=false;
    double h=0.0;
    QVector<double> pos, density;
    double peak=0.0;
};

inline KdeCurve kdeCurve(const QVector<double>& sorted,int grid,double padBandwidths){
    KdeCurve out;
    if(sorted.size()<3||grid<2) return out;
    const double sd=stdevOf(sorted);
    const double iqr=quantileOf(sorted,0.75)-quantileOf(sorted,0.25);
    double spread=sd;
    if(iqr>0.0) spread=qMin(sd,iqr/1.349);
    if(!(spread>0.0)) return out;
    const double h=0.9*spread*std::pow(double(sorted.size()),-0.2);
    if(!(h>0.0)) return out;

    const double lo=sorted.first()-padBandwidths*h;
    const double hi=sorted.last()+padBandwidths*h;
    if(!(hi>lo)) return out;
    const double norm=1.0/(double(sorted.size())*h*std::sqrt(2.0*3.14159265358979323846));
    out.pos.reserve(grid);
    out.density.reserve(grid);
    for(int g=0;g<grid;++g){
        const double x=lo+(hi-lo)*double(g)/double(grid-1);
        double acc=0.0;
        for(double sample:sorted){
            const double z=(x-sample)/h;
            // Beyond four bandwidths the kernel contributes less than 1e-4 of
            // its peak; skipping those turns an O(n*grid) sum into something
            // that finishes on a large sample. `sorted` makes the break valid.
            if(z>4.0) continue;
            if(z<-4.0) break;
            acc+=std::exp(-0.5*z*z);
        }
        out.pos.append(x);
        out.density.append(acc*norm);
        out.peak=qMax(out.peak,acc*norm);
    }
    out.h=h;
    out.valid=true;
    return out;
}


// Acklam's inverse normal CDF. Accurate to about 1.15e-9 across the range,
// far beyond what a plotted quantile needs, and it avoids pulling in a
// statistics library for one function.
inline double normalQuantile(double p){
    if(p<=0.0) return -std::numeric_limits<double>::infinity();
    if(p>=1.0) return std::numeric_limits<double>::infinity();
    static const double a[6]={-3.969683028665376e+01,2.209460984245205e+02,-2.759285104469687e+02,
                              1.383577518672690e+02,-3.066479806614716e+01,2.506628277459239e+00};
    static const double b[5]={-5.447609879822406e+01,1.615858368580409e+02,-1.556989798598866e+02,
                              6.680131188771972e+01,-1.328068155288572e+01};
    static const double c[6]={-7.784894002430293e-03,-3.223964580411365e-01,-2.400758277161838e+00,
                              -2.549732539343734e+00,4.374664141464968e+00,2.938163982698783e+00};
    static const double d[4]={7.784695709041462e-03,3.224671290700398e-01,2.445134137142996e+00,
                              3.754408661907416e+00};
    const double plow=0.02425, phigh=1.0-plow;
    if(p<plow){
        const double q=std::sqrt(-2.0*std::log(p));
        return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5])/
               ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    }
    if(p>phigh){
        const double q=std::sqrt(-2.0*std::log(1.0-p));
        return -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5])/
                ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    }
    const double q=p-0.5, r=q*q;
    return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q/
           (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1.0);
}

// A RADIAL POSITION THAT NEVER COLLAPSES TO THE CENTRE.
//
// `Bounds::scale` maps a column's minimum to 0, which on a radial figure is
// the centre - a single point every spoke shares. So a row that is lowest on
// four of six axes has four of its vertices at the SAME place, and its outline
// draws as a line through the middle instead of a shape. Worse, it asserts
// something the data does not say: a cruise speed of 443 kt drawn at the
// origin of an axis whose others are 461 to 512 reads as none, not as least.
//
// The star glyph already reserved an inner quarter for exactly this reason and
// the radar chart, which is the same drawing, did not - one rule applied to one
// class of a pair. This is that rule, in one place, with the floor stated by
// the caller: a quarter for a star glyph, which is drawn small and in
// multiples, and a smaller reserve for a radar, which gets the whole canvas.
//
// The rings stay truthful either way: they are labelled with the plotted
// radius, which is what this returns.
inline double radialScale(const Bounds& b,double v,double floorAt){
    return floorAt+(1.0-floorAt)*b.scale(v);
}

inline Bounds boundsOf(const QVector<double>& v){
    Bounds b;
    double lo=std::numeric_limits<double>::infinity(),hi=-lo;
    for(double x:v) if(finite(x)){ lo=qMin(lo,x); hi=qMax(hi,x); }
    if(!finite(lo)||!finite(hi)) return b;
    // A flat column still needs a usable span, or every point lands on one
    // line. boundsFor used to leave it at the default [0,1] instead.
    if(qFuzzyCompare(lo,hi)) hi=lo+1.0;
    b.lo=lo; b.hi=hi; b.valid=true;
    return b;
}

// A horizontal rule across the plot, as a two-point series. Reference lines are
// what turn a scatter into a Bland-Altman or a control chart, and they cost
// nothing expressed this way.
inline PlotSeries horizontalRule(double y,double xLo,double xHi,const QColor& colour,
                          const QString& label,bool dashed){
    PlotSeries rule;
    rule.label=label;
    rule.x={xLo,xHi};
    rule.y={y,y};
    rule.color=colour;
    rule.lineWidth=0.9;
    rule.drawLine=true;
    rule.drawMarkers=false;
    if(dashed) rule.dashPattern={5,4};
    return rule;
}


// The colour map for engines that colour a FIELD.
//
// Every heat field, contour, surface and vector field in this file used to
// call viridis() directly, so the map was not a choice at all - and the colour
// map IS the reading of a field plot. GraphVis 17 offered eighty four of them
// in eight categories; the port offered one.
//
// The tables live in ColourMaps.h, generated from the reference
// implementations by tools/gen_colourmaps.py, so a map here cannot drift from
// the map of the same name anywhere else.
// ColourMapKind moved to QtPlotBackend.h - see the note there.

inline ColourMapKind colourMapFor(const QString& name){
    return colourmaps::tableFor(name);
}

inline QColor colourMap(ColourMapKind table,double t){
    return colourmaps::sample(table,t);
}

// The ramp this figure is actually drawn from: the person's chosen colours when
// they have chosen any, and the named map otherwise. One question asked in one
// place, so the figure, the colour bar and the banding cannot disagree about
// which ramp is in force.
inline QColor sampleAt(ColourMapKind table,const PlotStyle& s,double t);

// The same ramp, read through the person's own settings.
//
// Every painter that colours by value normalises to 0..1 first and then asks
// for a colour, so this is the one place a 0..1 becomes a colour and therefore
// the one place the step count has to be applied. Doing it here rather than at
// the thirteen call sites is also what keeps the COLOUR BAR honest: the key is
// drawn through this function too, so a banded figure gets a banded key without
// anybody having to remember to band it twice.
//
// The RANGE cannot be applied here, because by this point the value has already
// been divided by whatever span the engine chose. It is applied where the value
// is still a value - see rampPosition.
// A list of chosen colours, read as a ramp.
//
// Interpolated in plain sRGB rather than in a perceptual space. The generated
// maps are perceptually uniform because they were built to be; a hand-picked
// set is not going to be whatever is done between the stops, and pretending
// otherwise by interpolating in Lab would only make the result differ from the
// swatches the person actually clicked on.
inline QColor sampleStops(const QVector<QColor>& stops,double t){
    if(stops.isEmpty()) return QColor(Qt::black);
    if(stops.size()==1) return stops.first();
    const double pos=qBound(0.0,t,1.0)*double(stops.size()-1);
    const int i=qMin(int(stops.size())-2,int(pos));
    const double f=qBound(0.0,pos-double(i),1.0);
    const QColor& a=stops.at(i);
    const QColor& b=stops.at(i+1);
    return QColor::fromRgbF(a.redF()  +(b.redF()  -a.redF())  *f,
                            a.greenF()+(b.greenF()-a.greenF())*f,
                            a.blueF() +(b.blueF() -a.blueF()) *f);
}

inline QColor sampleAt(ColourMapKind table,const PlotStyle& s,double t){
    return s.customColours.isEmpty() ? colourmaps::sample(table,t)
                                     : sampleStops(s.customColours,t);
}

inline QColor colourMapStyled(ColourMapKind table,const PlotStyle& s,double t){
    // NaN reaches here only from rampPosition, and only when the person asked
    // for values outside the colour range to be dropped rather than clamped.
    // Transparent is how "do not draw this" travels through thirteen call
    // sites that each set a brush or a pen and none of which can return early:
    // a transparent brush leaves the background, which is exactly the picture
    // that was asked for.
    if(!finite(t)) return s.colourOutOfRangeDropped ? QColor(Qt::transparent)
                                                    : sampleAt(table,s,0.0);
    t=qBound(0.0,t,1.0);
    if(s.colourLevels>=2){
        // Band k of N covers [k/N,(k+1)/N). With chosen colours, band k IS
        // colour k - that is what makes "five bands, five colours, set each
        // one" land exactly. With a named map the band is drawn in the colour
        // at the MIDDLE of its slice, so the steps are even and the two ends of
        // the ramp are half-bands rather than one band each of double weight.
        const int levels=qMin(s.colourLevels,256);
        const int k=qMin(levels-1,int(t*double(levels)));
        if(!s.customColours.isEmpty())
            return s.customColours.at(k%int(s.customColours.size()));
        t=(double(k)+0.5)/double(levels);
    }
    return sampleAt(table,s,t);
}

// Where a VALUE sits on the ramp, given the range the person asked for.
//
// lo and hi are what the engine measured; colourMin and colourMax override
// either end. Clamped rather than dropped: a point above the top of the chosen
// range is drawn in the top colour, which reads as "at least this much" and is
// what a capped scale means everywhere else it is used. Filtering is the axis
// limits' job, not the colour's.
inline double rampPosition(const PlotStyle& s,double value,double lo,double hi){
    const bool capped=!isUnset(s.colourMin)||!isUnset(s.colourMax);
    double cl=isUnset(s.colourMin)?lo:s.colourMin;
    double ch=isUnset(s.colourMax)?hi:s.colourMax;
    if(!finite(cl)||!finite(ch)||!(ch>cl)){ cl=lo; ch=hi; }
    if(!(ch>cl)) return 0.0;
    // Dropping only means anything against a range somebody set. Applied to a
    // range fitted to the data it would drop the two extreme values to
    // rounding, which is a control that appears to nibble at the figure.
    if(capped&&s.colourOutOfRangeDropped&&(value<cl||value>ch))
        return std::numeric_limits<double>::quiet_NaN();
    // THROUGH THE SAME FUNCTION THE KEY IS DRAWN WITH.
    //
    // This returned (value - cl) / (ch - cl) and nothing else, so every
    // colour-mapped engine was linear whatever the person chose - the
    // ColourScale machinery existed and only the choropleth reached it.
    // Routing the last step through `scalePosition` gives all of them log and
    // quantile at once, and gives the colour bar and the figure one definition
    // of where a value sits rather than two.
    //
    // Linear is bit-for-bit what it was: `scalePosition` computes the same
    // expression for that kind, which is what lets the 440-figure gallery
    // stand as proof that no existing figure moved.
    ColourScale scale;
    scale.kind=s.colourScaleKind;
    scale.lo=cl; scale.hi=ch;
    scale.breaks=s.colourBreaks;
    const double t=scalePosition(value,scale);
    // THE CLAMP IS KEPT. This function has always returned a bounded 0..1 and
    // `scalePosition` does not clamp - it is written for a caller that has
    // already decided what to do with a value off the end of the ramp. Letting
    // an unclamped position through here would change what every existing
    // figure paints outside a stated colour range, which is not what this
    // change is about.
    //
    // NaN passes through unclamped on purpose: it is how "do not draw this"
    // travels, and qBound would turn it into a colour.
    return (t!=t) ? t : qBound(0.0,t,1.0);
}

// The person's own limits on top of what a column spans. An unset end keeps the
// measured one; a pair that does not increase is ignored rather than producing
// a cube with no depth.
inline Bounds withAxisLimits(Bounds b,const PlotAxis& a){
    if(!b.valid) return b;
    const double lo=isUnset(a.min)?b.lo:a.min;
    const double hi=isUnset(a.max)?b.hi:a.max;
    if(!finite(lo)||!finite(hi)||!(hi>lo)) return b;
    b.lo=lo; b.hi=hi;
    return b;
}



// Fitted to the frame it is given, rather than to a fraction of its shorter
// side.
//
// The old rule was `qMin(width,height)*0.34`, which is two guesses stacked: it
// assumed the projected cube is as wide as it is tall, and it left a third of
// the smaller dimension spare in case it was not. At -35 degrees the cube
// projects 1.39 units wide and 1.06 tall, so on a 780 x 590 canvas the figure
// came out 280 px across inside a 780 px frame and the rest was margin. The
// person reasonably read that as the plot being broken.
//
// So: project the eight corners, measure what they actually span, and scale to
// the space available. The cube is symmetric about the origin and the
// projection is linear, so the projected box is centred there too and only the
// half-extents are needed. `margin` is the room the caller wants kept for tick
// numbers and axis names, in pixels.
inline Projection makeProjection(const QRectF& target,double azimuthDeg,double elevationDeg,
                          double zoom=1.0,double margin=0.0,
                          double panX=0.0,double panY=0.0){
    Projection p;
    const double az=azimuthDeg*3.14159265358979323846/180.0;
    const double el=elevationDeg*3.14159265358979323846/180.0;
    p.sinAz=std::sin(az); p.cosAz=std::cos(az);
    p.sinEl=std::sin(el); p.cosEl=std::cos(el);
    p.origin=target.center();

    double halfW=0.0,halfH=0.0;
    for(int i=0;i<8;++i){
        const double x=(i&1)?0.5:-0.5;
        const double y=(i&2)?0.5:-0.5;
        const double z=(i&4)?0.5:-0.5;
        halfW=qMax(halfW,std::abs(-x*p.sinAz+y*p.cosAz));
        halfH=qMax(halfH,std::abs(-(x*p.cosAz+y*p.sinAz)*p.sinEl+z*p.cosEl));
    }
    const double availW=qMax(24.0,target.width()-2.0*margin);
    const double availH=qMax(24.0,target.height()-2.0*margin);
    p.scale=qMin(availW/(2.0*qMax(1e-6,halfW)),availH/(2.0*qMax(1e-6,halfH)));
    // Zoom is the person's multiplier on that fitted size. Bounded so a
    // runaway pinch cannot produce a scale that overflows the painter.
    p.scale*=qBound(0.05,zoom,40.0);
    // The pan, applied after the scale is known, because it is held in
    // projection units - see PlotView3D::panX. Screen y grows downward, which
    // is why this one is subtracted and `project` subtracts its own.
    p.origin+=QPointF(panX*p.scale,-panY*p.scale);
    return p;
}

// Normalised coordinates in and screen point out. Depth comes back too, so the
// caller can sort back to front - without that a surface draws its far faces
// over its near ones and reads inside out.
inline QPointF project(const Projection& p,double x,double y,double z,double* depth=nullptr){
    const double sx=-x*p.sinAz+y*p.cosAz;
    const double sy=-(x*p.cosAz+y*p.sinAz)*p.sinEl+z*p.cosEl;
    if(depth) *depth=(x*p.cosAz+y*p.sinAz)*p.cosEl+z*p.sinEl;
    return QPointF(p.origin.x()+sx*p.scale,p.origin.y()-sy*p.scale);
}



inline void fftInPlace(QVector<double>& re,QVector<double>& im){
    const int n=re.size();
    if(n<2||(n&(n-1))!=0) return;
    // Bit-reversal permutation.
    for(int i=1,j=0;i<n;++i){
        int bit=n>>1;
        for(;j&bit;bit>>=1) j^=bit;
        j^=bit;
        if(i<j){ std::swap(re[i],re[j]); std::swap(im[i],im[j]); }
    }
    for(int len=2;len<=n;len<<=1){
        const double ang=-2.0*3.14159265358979323846/double(len);
        const double wr=std::cos(ang), wi=std::sin(ang);
        for(int i=0;i<n;i+=len){
            double cr=1.0, ci=0.0;
            for(int k=0;k<len/2;++k){
                const int a=i+k, b=i+k+len/2;
                const double tr=re[b]*cr-im[b]*ci;
                const double ti=re[b]*ci+im[b]*cr;
                re[b]=re[a]-tr; im[b]=im[a]-ti;
                re[a]+=tr;      im[a]+=ti;
                const double nr=cr*wr-ci*wi;
                ci=cr*wi+ci*wr; cr=nr;
            }
        }
    }
}

inline int nextPowerOfTwo(int n){
    int p=1;
    while(p<n) p<<=1;
    return p;
}

// Without a window the spectrum of anything that does not fit a whole number of
// periods into the record is dominated by leakage from the edges, which looks
// like broadband noise that is not there. Hann is the default because it is the
// right answer most of the time, but it is not the right answer every time and
// the choice used to be unavailable:
//
//   Rectangular  no window at all. The narrowest possible main lobe and the
//                worst leakage. Correct, and only correct, when the record
//                holds a whole number of periods - a synchronously sampled
//                gear order, a simulation with a chosen record length.
//   Hann         the default. Fast-decaying sidelobes, moderate main lobe.
//   Hamming      lower first sidelobe than Hann, but the far sidelobes decay
//                more slowly. Better for separating two tones of similar size
//                that are close together.
//   Blackman     wider main lobe, much lower sidelobes - a small tone next to
//                a large one at some distance.
//   Blackman-Harris  the four-term form: -92 dB sidelobes, the widest main
//                lobe here. For dynamic range, when resolution is spare.
//   Bartlett     triangular. Cheap, and its transform is non-negative.
//   Welch        parabolic. The classic choice for Welch's periodogram.
//   Flat Top     designed so a tone lands at the right AMPLITUDE regardless of
//                where it falls between bins - about 0.01 dB of scalloping
//                loss against Hann's 1.4 dB. Use it to measure how big a peak
//                is, never to measure how many peaks there are.
//
// Every one of these is normalised the same way by the callers (dividing by the
// sum of w squared), so the PSD stays calibrated whichever is chosen and the
// numbers on the y axis remain comparable between them.
enum class SpectralWindow { Rectangular, Hann, Hamming, Blackman, BlackmanHarris,
                            Bartlett, Welch, FlatTop };

inline SpectralWindow spectralWindowFor(const QString& variant){
    const QString v=variant.trimmed();
    if(v.compare(QLatin1String("Rectangular"),Qt::CaseInsensitive)==0
     ||v.compare(QLatin1String("None"),Qt::CaseInsensitive)==0)        return SpectralWindow::Rectangular;
    if(v.compare(QLatin1String("Hamming"),Qt::CaseInsensitive)==0)     return SpectralWindow::Hamming;
    if(v.compare(QLatin1String("Blackman"),Qt::CaseInsensitive)==0)    return SpectralWindow::Blackman;
    if(v.compare(QLatin1String("Blackman-Harris"),Qt::CaseInsensitive)==0
     ||v.compare(QLatin1String("Blackman Harris"),Qt::CaseInsensitive)==0)
                                                                       return SpectralWindow::BlackmanHarris;
    if(v.compare(QLatin1String("Bartlett"),Qt::CaseInsensitive)==0
     ||v.compare(QLatin1String("Triangular"),Qt::CaseInsensitive)==0)  return SpectralWindow::Bartlett;
    if(v.compare(QLatin1String("Welch"),Qt::CaseInsensitive)==0)       return SpectralWindow::Welch;
    if(v.compare(QLatin1String("Flat Top"),Qt::CaseInsensitive)==0
     ||v.compare(QLatin1String("Flat-Top"),Qt::CaseInsensitive)==0)    return SpectralWindow::FlatTop;
    return SpectralWindow::Hann;
}

inline double spectralWindow(SpectralWindow kind,int i,int n){
    if(n<2) return 1.0;
    constexpr double kPi=3.14159265358979323846;
    const double d=double(n-1);
    const double a=2.0*kPi*double(i)/d;      // 0 .. 2*pi across the record
    switch(kind){
    case SpectralWindow::Rectangular:
        return 1.0;
    case SpectralWindow::Hann:
        return 0.5*(1.0-std::cos(a));
    case SpectralWindow::Hamming:
        // 0.54/0.46, the classic constants rather than the optimised
        // 0.53836/0.46164: the difference is below a tenth of a dB and these
        // are the ones every reference and every other tool prints.
        return 0.54-0.46*std::cos(a);
    case SpectralWindow::Blackman:
        return 0.42-0.5*std::cos(a)+0.08*std::cos(2.0*a);
    case SpectralWindow::BlackmanHarris:
        return 0.35875-0.48829*std::cos(a)+0.14128*std::cos(2.0*a)-0.01168*std::cos(3.0*a);
    case SpectralWindow::Bartlett:
        return 1.0-std::abs((double(i)-d/2.0)/(d/2.0));
    case SpectralWindow::Welch: {
        const double u=(double(i)-d/2.0)/(d/2.0);
        return 1.0-u*u;
    }
    case SpectralWindow::FlatTop:
        // The five-term SRS/ISO form, left unnormalised: it goes negative in
        // its side regions, which is what flattens the passband, and the
        // callers' sum-of-squares normalisation handles the scale.
        return 0.21557895-0.41663158*std::cos(a)+0.277263158*std::cos(2.0*a)
              -0.083578947*std::cos(3.0*a)+0.006947368*std::cos(4.0*a);
    }
    return 0.5*(1.0-std::cos(a));
}



struct EdgeList {
    QVector<double> ids;                 // distinct node identifiers, in first-seen order
    QVector<int> from,to;                // indices into ids
    QVector<double> weight;
    QVector<double> strength;            // total weight at each node
    bool valid=false;
};

inline EdgeList edgesFrom(const PlotSpec& spec){
    EdgeList e;
    if(spec.series.size()<2) return e;
    const QVector<double>& a=spec.series.at(0).y;
    const QVector<double>& b=spec.series.at(1).y;
    const bool weighted=spec.series.size()>=3;
    const QVector<double> w=weighted?spec.series.at(2).y:QVector<double>();
    const int n=qMin(a.size(),b.size());
    QHash<double,int> index;
    for(int i=0;i<n;++i){
        if(!finite(a[i])||!finite(b[i])) continue;
        double weightValue=1.0;
        if(weighted&&i<w.size()){
            if(!finite(w[i])||w[i]<=0) continue;
            weightValue=w[i];
        }
        const auto seat=[&index,&e](double id){
            auto it=index.find(id);
            if(it!=index.end()) return it.value();
            const int at=e.ids.size();
            e.ids.append(id); e.strength.append(0.0);
            index.insert(id,at);
            return at;
        };
        const int u=seat(a[i]),v=seat(b[i]);
        e.from.append(u); e.to.append(v); e.weight.append(weightValue);
        e.strength[u]+=weightValue; e.strength[v]+=weightValue;
    }
    // One node is not a graph. There is no upper limit here: a large graph gets
    // a cheaper layout rather than no picture - an engine that silently draws
    // nothing on data it could have drawn is the failure this whole pass exists
    // to remove.
    e.valid=(e.ids.size()>=2&&!e.from.isEmpty());
    return e;
}



// Saturation vapour pressure over water, in hPa, for a temperature in Celsius.
// Bolton (1980) equation 10: better than a tenth of a per cent from -35 to
// +35 C, which is the whole range a sounding spends its time in.
inline double saturationPressure(double celsius){
    return 6.112*std::exp(17.67*celsius/(celsius+243.5));
}

// The inverse, used to place a line of constant mixing ratio: given a vapour
// pressure, the temperature at which the air would be saturated.
inline double dewpointFrom(double vapourPressure){
    if(!(vapourPressure>0.0)) return -273.15;
    const double logged=std::log(vapourPressure/6.112);
    const double denom=17.67-logged;
    if(!(std::abs(denom)>1e-12)) return -273.15;
    return 243.5*logged/denom;
}

// Mass of water vapour per unit mass of dry air, saturated, in g/kg.
inline double saturationMixingRatio(double celsius,double hPa){
    const double e=saturationPressure(celsius);
    // A pressure at or below the saturation vapour pressure is not a state the
    // atmosphere is in; guarding it stops a negative mixing ratio propagating
    // into the moist adiabat's denominator.
    if(!(hPa>e+1e-6)) return 1000.0;
    return 1000.0*0.622*e/(hPa-e);
}

// Temperature on a dry adiabat: potential temperature is conserved, so
// T = theta (p/1000)^(R/cp). Kelvin in, Kelvin out.
inline double dryAdiabatAt(double theta,double hPa){
    return theta*std::pow(hPa/1000.0,0.2854);
}

// Equivalent potential temperature of a SATURATED parcel, Bolton (1980)
// equation 39. This is what a saturated adiabat conserves, and it is the
// definition the curves below are built from.
inline double equivalentPotential(double celsius,double hPa){
    const double kelvin=celsius+273.15;
    const double ratio=saturationMixingRatio(celsius,hPa)/1000.0;   // kg/kg
    const double theta=kelvin*std::pow(1000.0/qMax(1e-6,hPa),0.2854*(1.0-0.28*ratio));
    return theta*std::exp((3036.0/kelvin-1.78)*ratio*(1.0+0.448*ratio));
}

// The temperature at which a saturated parcel of the given equivalent
// potential temperature sits at this pressure. Found by bisection, because
// theta-e rises monotonically with temperature at fixed pressure.
//
// This INVERTS the usual approach and it is worth saying why. The first version
// integrated the pseudoadiabatic lapse rate downward in pressure with a
// constant latent heat, and checking the result against this same theta-e
// formula showed the curves drifting off theta-e conservation by up to six
// kelvin on the warmest adiabat over eight hundred hectopascals - around two
// and a half degrees of temperature error at 200 hPa, which is half the spacing
// between adjacent adiabats on the chart. A temperature-dependent latent heat
// improved the warm curves and made the cold ones worse. Solving for theta-e
// instead makes the residual 1e-13 K, which is to say exact: a saturated
// adiabat IS a curve of constant theta-e, so it should be drawn as one rather
// than approached by integration.
inline double saturatedTemperatureAt(double target,double hPa){
    double lo=-120.0,hi=60.0;
    for(int i=0;i<60;++i){
        const double mid=0.5*(lo+hi);
        if(equivalentPotential(mid,hPa)<target) lo=mid; else hi=mid;
    }
    return 0.5*(lo+hi);
}

// The lifting condensation level, Bolton (1980) equation 22 for the
// temperature and Poisson's equation for the pressure. Kelvin in.
struct Condensation { double kelvin=0.0; double hPa=0.0; bool ok=false; };
inline Condensation liftingCondensation(double kelvin,double dewKelvin,double hPa){
    Condensation out;
    if(!(kelvin>0.0)||!(dewKelvin>0.0)||!(hPa>0.0)) return out;
    // The dewpoint cannot exceed the temperature. Saturated air condenses at
    // the surface, and the formula's 1/(Td - 56) is not the thing that should
    // decide that.
    const double dew=qMin(dewKelvin,kelvin);
    const double denom=1.0/(dew-56.0)+std::log(kelvin/dew)/800.0;
    if(!(std::abs(denom)>1e-12)) return out;
    out.kelvin=1.0/denom+56.0;
    out.hPa=hPa*std::pow(out.kelvin/kelvin,3.504);
    out.ok=finite(out.kelvin)&&finite(out.hPa)&&out.hPa>0.0;
    return out;
}

// Which of the four thermodynamic diagrams. They plot the SAME sounding and
// differ only in where a (temperature, pressure) pair lands on the page, so
// everything below - the four families of background curve, the parcel lift,
// the condensation level, the energy integrals, the chrome - is written once
// and the transform is the parameter.
//
// The differences are not cosmetic, which is why all four are offered rather
// than one of them:
//
//   Skew-T    isotherms at 45 degrees, so the angle between an isotherm and a
//             dry adiabat is wide and stability is easy to read.
//   Emagram   isotherms vertical. The oldest of the four and the easiest to
//             read a temperature off, at the cost of that angle.
//   Stuve     pressure to the power R/cp, which makes the DRY ADIABATS
//             STRAIGHT. That is the whole reason it exists.
//   Tephigram temperature against log potential temperature, rotated. Area on
//             it is proportional to energy, so CAPE is a region whose size can
//             be judged by eye rather than a number to be trusted.
enum class SoundingChart { SkewT, Emagram, Stuve, Tephigram };

// Where a (temperature, pressure) pair sits, in the chart's own units. The
// caller fits whatever box these produce to the page, so the units only have to
// be self-consistent within one chart.
inline QPointF placeOn(SoundingChart chart,double celsius,double hPa){
    const double kelvin=celsius+273.15;
    switch(chart){
    case SoundingChart::Emagram:
        return QPointF(celsius,std::log(1000.0/qMax(1e-6,hPa)));
    case SoundingChart::Stuve:
        // Pressure to the power R/cp, decreasing upward. A dry adiabat is
        // T = theta (p/1000)^0.2854, which is linear in exactly this quantity -
        // so it comes out a straight line through the origin.
        return QPointF(celsius,-std::pow(qMax(1e-6,hPa)/1000.0,0.2854));
    case SoundingChart::Tephigram: {
        // Temperature against the log of potential temperature, then rotated
        // through 45 degrees so the isobars run roughly across the page. The
        // 160 scales the log axis into the same range as the temperature one;
        // it is a choice of aspect and carries no physics.
        const double theta=kelvin*std::pow(1000.0/qMax(1e-6,hPa),0.2854);
        const double x=celsius;
        const double y=160.0*std::log(theta/273.15);
        const double c=std::sqrt(0.5);
        return QPointF(c*(x+y),c*(y-x));
    }
    case SoundingChart::SkewT:
    default:
        // The skew is applied by the caller, which is the only place that knows
        // the box's aspect - an isotherm is meant to be at 45 degrees ON THE
        // PAGE, and that cannot be decided in chart units.
        return QPointF(celsius,std::log(1000.0/qMax(1e-6,hPa)));
    }
}



struct TreeNode { double id=0.0,value=0.0; int parent=-1,depth=0; QVector<int> kids; };
struct Tree {
    QVector<TreeNode> nodes;
    QVector<int> roots;
    QVector<int> preorder;      // parents always before their children
    int deepest=0;
    bool valid=false;
};

inline Tree treeFrom(const PlotSpec& spec){
    Tree tree;
    if(spec.series.size()<3) return tree;
    const QVector<double>& nodeId=spec.series.at(0).y;
    const QVector<double>& parentId=spec.series.at(1).y;
    const QVector<double>& value=spec.series.at(2).y;
    const int rows=qMin(nodeId.size(),qMin(parentId.size(),value.size()));

    QVector<double> rawParent;
    QVector<bool> hasParent;
    QHash<double,int> index;
    for(int i=0;i<rows;++i){
        if(!finite(nodeId[i])||!finite(value[i])) continue;
        if(index.contains(nodeId[i])) continue;      // the first row for an id wins
        TreeNode node;
        node.id=nodeId[i];
        node.value=value[i];
        index.insert(node.id,tree.nodes.size());
        tree.nodes.append(node);
        hasParent.append(finite(parentId[i]));
        rawParent.append(finite(parentId[i])?parentId[i]:0.0);
    }
    if(tree.nodes.isEmpty()) return tree;

    // Parents in a second pass, because a table is free to name a parent before
    // the row that defines it.
    for(int i=0;i<tree.nodes.size();++i){
        if(!hasParent[i]) continue;
        const int up=index.value(rawParent[i],-1);
        if(up>=0&&up!=i) tree.nodes[i].parent=up;
    }
    // A parent column is data, and data can be wrong. A cycle in it would make
    // the walk below run forever, so any node that cannot reach a root in as
    // many steps as there are nodes is cut loose and made a root of its own -
    // the picture is then odd, which is a better failure than a hung renderer.
    for(int i=0;i<tree.nodes.size();++i){
        int walk=tree.nodes[i].parent,steps=0;
        while(walk>=0&&steps<=tree.nodes.size()){ walk=tree.nodes[walk].parent; ++steps; }
        if(steps>tree.nodes.size()) tree.nodes[i].parent=-1;
    }
    for(int i=0;i<tree.nodes.size();++i){
        if(tree.nodes[i].parent<0) tree.roots.append(i);
        else tree.nodes[tree.nodes[i].parent].kids.append(i);
    }
    if(tree.roots.isEmpty()) return tree;

    // An explicit stack rather than recursion: the depth is the user's data and
    // a pathological table should not be able to overflow the call stack.
    QVector<int> stack=tree.roots;
    while(!stack.isEmpty()){
        const int at=stack.takeLast();
        tree.preorder.append(at);
        tree.deepest=qMax(tree.deepest,tree.nodes[at].depth);
        for(int kid:tree.nodes[at].kids){
            tree.nodes[kid].depth=tree.nodes[at].depth+1;
            stack.append(kid);
        }
    }
    tree.valid=true;
    return tree;
}



struct TextureClass { const char* name; int count; const double (*vertex)[2]; };

// (clay, silt) per cent. Sand is what is left.
const double kUkClay[]        ={100,0, 55,0, 35,20, 35,45, 55,45};
const double kUkSandyClay[]   ={55,0, 30,0, 30,20, 35,20};
const double kUkSiltyClay[]   ={55,45, 35,45, 35,65};
const double kUkClayLoam[]    ={35,20, 30,20, 18,32, 18,62, 35,45};
const double kUkSiltyClayLo[] ={35,45, 18,62, 18,82, 35,65};
const double kUkSandyClayLo[] ={30,0, 18,0, 18,32, 30,20};
const double kUkSandyLoam[]   ={18,0, 15,0, 0,30, 0,50, 18,32};
const double kUkSandySiltLo[] ={18,32, 0,50, 0,80, 18,62};
const double kUkSiltLoam[]    ={18,62, 0,80, 0,100, 18,82};
const double kUkLoamySand[]   ={15,0, 10,0, 0,15, 0,30};
const double kUkSand[]        ={10,0, 0,0, 0,15};

const double kUsSand[]        ={0,15, 0,0, 10,0};
const double kUsLoamySand[]   ={0,30, 0,15, 10,0, 15,0};
const double kUsSandyLoamA[]  ={0,48, 0,30, 15,0, 20,0, 20,28};
const double kUsSandyLoamB[]  ={0,50, 0,48, 7,41, 7,50};
const double kUsLoam[]        ={7,50, 7,41, 20,28, 27,28, 27,50};
const double kUsSiltLoamA[]   ={27,73, 12,88, 12,50, 27,50};
const double kUsSiltLoamB[]   ={12,80, 0,80, 0,50, 12,50};
const double kUsSilt[]        ={12,88, 0,100, 0,80, 12,80};
const double kUsSandyClayLo[] ={20,28, 20,0, 35,0, 35,20, 27,28};
const double kUsClayLoam[]    ={27,53, 27,28, 40,15, 40,40};
const double kUsSiltyClayLo[] ={40,60, 27,73, 27,53, 40,40};
const double kUsSandyClay[]   ={35,20, 35,0, 55,0};
const double kUsSiltyClay[]   ={60,40, 40,60, 40,40};
const double kUsClay[]        ={100,0, 60,40, 40,40, 40,15, 55,0};

struct Region { const char* name; const double* points; int count; };

const Region kUk[]={
    {"clay",kUkClay,5},{"sandy clay",kUkSandyClay,4},{"silty clay",kUkSiltyClay,3},
    {"clay loam",kUkClayLoam,5},{"silty clay loam",kUkSiltyClayLo,4},
    {"sandy clay loam",kUkSandyClayLo,4},{"sandy loam",kUkSandyLoam,5},
    {"sandy silt loam",kUkSandySiltLo,4},{"silt loam",kUkSiltLoam,4},
    {"loamy sand",kUkLoamySand,4},{"sand",kUkSand,3}};

const Region kUsda[]={
    {"sand",kUsSand,3},{"loamy sand",kUsLoamySand,4},
    {"sandy loam",kUsSandyLoamA,5},{"sandy loam",kUsSandyLoamB,4},
    {"loam",kUsLoam,5},{"silt loam",kUsSiltLoamA,4},{"silt loam",kUsSiltLoamB,4},
    {"silt",kUsSilt,4},{"sandy clay loam",kUsSandyClayLo,5},
    {"clay loam",kUsClayLoam,4},{"silty clay loam",kUsSiltyClayLo,4},
    {"sandy clay",kUsSandyClay,3},{"silty clay",kUsSiltyClay,3},{"clay",kUsClay,5}};

// Even-odd, in the (clay, silt) plane the regions are defined in - so the test
// asks the same question the boundaries answer, rather than one about pixels.
inline bool insideRegion(const Region& region,double clay,double silt){
    bool in=false;
    for(int i=0,j=region.count-1;i<region.count;j=i++){
        const double ci=region.points[i*2],si=region.points[i*2+1];
        const double cj=region.points[j*2],sj=region.points[j*2+1];
        if((si>silt)!=(sj>silt)){
            const double at=ci+(silt-si)*(cj-ci)/(sj-si);
            if(clay<at) in=!in;
        }
    }
    return in;
}



// A field: the band it sits in, the plagioclase-ratio range across it, and its
// name. `foid` selects the lower triangle, where `lo`/`hi` are F rather than Q.
struct QapfField { bool foid; double lo,hi,p0,p1; const char* number; const char* name; };

const QapfField kQapfPlutonic[]={
    // Q, upper triangle.
    {false, 90,100,   0,100, "1a", "quartzolite"},
    {false, 60, 90,   0,100, "1b", "quartz-rich granitoid"},
    {false, 20, 60,   0, 10, "2", "alkali feldspar granite"},
    {false, 20, 60,  10, 35, "3a", "syenogranite"},
    {false, 20, 60,  35, 65, "3b", "monzogranite"},
    {false, 20, 60,  65, 90, "4", "granodiorite"},
    {false, 20, 60,  90,100, "5", "tonalite"},
    {false,  5, 20,   0, 10, "6*", "quartz alkali feldspar syenite"},
    {false,  5, 20,  10, 35, "7*", "quartz syenite"},
    {false,  5, 20,  35, 65, "8*", "quartz monzonite"},
    {false,  5, 20,  65, 90, "9*", "quartz monzodiorite / quartz monzogabbro"},
    {false,  5, 20,  90,100, "10*", "quartz diorite / quartz gabbro / quartz anorthosite"},
    {false,  0,  5,   0, 10, "6", "alkali feldspar syenite"},
    {false,  0,  5,  10, 35, "7", "syenite"},
    {false,  0,  5,  35, 65, "8", "monzonite"},
    {false,  0,  5,  65, 90, "9", "monzodiorite / monzogabbro"},
    {false,  0,  5,  90,100, "10", "diorite / gabbro / anorthosite"},
    // F, lower triangle.
    {true,   0, 10,   0, 10, "6'", "foid-bearing alkali feldspar syenite"},
    {true,   0, 10,  10, 35, "7'", "foid-bearing syenite"},
    {true,   0, 10,  35, 65, "8'", "foid-bearing monzonite"},
    {true,   0, 10,  65, 90, "9'", "foid-bearing monzodiorite / monzogabbro"},
    {true,   0, 10,  90,100, "10'", "foid-bearing diorite / gabbro / anorthosite"},
    // Four fields across, not five, and divided at 50 rather than at 35 and 65.
    {true,  10, 60,   0, 10, "11", "foid syenite"},
    {true,  10, 60,  10, 50, "12", "foid monzosyenite"},
    {true,  10, 60,  50, 90, "13", "foid monzodiorite / foid monzogabbro"},
    {true,  10, 60,  90,100, "14", "foid diorite / foid gabbro"},
    {true,  60,100,   0,100, "15", "foidolite"}};

// The same construction for fine-grained rocks: identical boundaries, different
// names, read from Figure 19 of the same report ("Classification and
// nomenclature of fine-grained crystalline rocks according to their modal
// mineral contents using the QAPF diagram", after Streckeisen 1978).
//
// Three things differ from the coarse-grained table above, and all three are
// differences in the FIGURE rather than simplifications made here:
//
//   - fields 3a and 3b are one field in the volcanic diagram: rhyolite runs the
//     whole way from 10 to 65, where the plutonic diagram splits that same span
//     into syenogranite and monzogranite;
//   - fields 9 and 10 - and their starred and primed forms - carry one name
//     between them. QAPF does not separate basalt from andesite; that is done
//     on colour index and silica (the report's Figure 21), neither of which is
//     a modal mineral and so neither of which this engine is given. Naming a
//     field 9 rock "andesite" from its position alone would be an answer the
//     data does not support, so both fields say what the figure says;
//   - fields 1a and 1b have no approved volcanic name at all. Rocks that
//     silica-rich and that poor in feldspar are vanishingly rare as lavas and
//     the scheme leaves the region unnamed rather than inventing one. The
//     engine reports the field number and says so.
//
// Field 15 is left whole. The figure divides it into phonolitic foidite,
// tephritic (basanitic) foidite and foidite, but the dividing ratios were not
// legible in the source, and a boundary guessed from the shape of the picture
// would be a boundary invented here. An unsubdivided field 15 names a rock
// "foidite", which is true of everything in it.
const QapfField kQapfVolcanic[]={
    // Q, upper triangle.
    {false, 90,100,   0,100, "1a", ""},
    {false, 60, 90,   0,100, "1b", ""},
    {false, 20, 60,   0, 10, "2", "alkali feldspar rhyolite"},
    {false, 20, 60,  10, 65, "3", "rhyolite"},
    {false, 20, 60,  65, 90, "4", "dacite"},
    {false, 20, 60,  90,100, "5", "dacite"},
    {false,  5, 20,   0, 10, "6*", "quartz alkali feldspar trachyte"},
    {false,  5, 20,  10, 35, "7*", "quartz trachyte"},
    {false,  5, 20,  35, 65, "8*", "quartz latite"},
    {false,  5, 20,  65, 90, "9*", "quartz-bearing basalt / andesite"},
    {false,  5, 20,  90,100, "10*", "quartz-bearing basalt / andesite"},
    {false,  0,  5,   0, 10, "6", "alkali feldspar trachyte"},
    {false,  0,  5,  10, 35, "7", "trachyte"},
    {false,  0,  5,  35, 65, "8", "latite"},
    {false,  0,  5,  65, 90, "9", "basalt / andesite"},
    {false,  0,  5,  90,100, "10", "basalt / andesite"},
    // F, lower triangle.
    {true,   0, 10,   0, 10, "6'", "foid-bearing alkali feldspar trachyte"},
    {true,   0, 10,  10, 35, "7'", "foid-bearing trachyte"},
    {true,   0, 10,  35, 65, "8'", "foid-bearing latite"},
    {true,   0, 10,  65, 90, "9'", "foid-bearing basalt / andesite"},
    {true,   0, 10,  90,100, "10'", "foid-bearing basalt / andesite"},
    {true,  10, 60,   0, 10, "11", "phonolite"},
    {true,  10, 60,  10, 50, "12", "tephritic phonolite"},
    {true,  10, 60,  50, 90, "13", "phonolitic tephrite / phonolitic basanite"},
    // Olivine, not feldspar, is what separates these two: over about ten per
    // cent of it and the rock is a basanite. QAPF does not carry olivine.
    {true,  10, 60,  90,100, "14", "tephrite / basanite"},
    {true,  60,100,   0,100, "15", "foidite"}};


inline void fnvBytes(quint64& h,const void* data,size_t bytes){
    const auto* p=static_cast<const unsigned char*>(data);
    for(size_t i=0;i<bytes;++i){ h^=p[i]; h*=0x100000001b3ULL; }
}
inline void fnvDouble(quint64& h,double v){
    // Normalise the two zeros and every NaN payload so that data which draws
    // identically also fingerprints identically.
    if(v==0.0) v=0.0;
    else if(v!=v){ const quint64 nan=0x7ff8000000000000ULL; fnvBytes(h,&nan,sizeof(nan)); return; }
    fnvBytes(h,&v,sizeof(v));
}
inline void fnvString(quint64& h,const QString& s){
    const QByteArray b=s.toUtf8();
    fnvBytes(h,b.constData(),size_t(b.size()));
    fnvBytes(h,"\x1f",1);
}
inline void fnvColour(quint64& h,const QColor& c){
    const quint32 rgba=c.rgba();
    fnvBytes(h,&rgba,sizeof(rgba));
}

// The caller's limits, notes and CAMERA, put back on top of a rewrite that set
// its own.
//
// All three are deliberately absent from specFingerprint, for the same reason:
// no rewrite in prepareSpec reads any of them, and all three change while
// someone is interacting. Hashing the limits meant dragging a violin plot
// re-derived the violin sixty times a second; hashing the notes would mean
// re-deriving it on every keystroke while a note is being typed; hashing the
// camera would mean re-preparing a 200,000-point surface on every frame of a
// drag, which is the cost the draft path exists to avoid.
//
// This is not an optimisation, it is a correctness fix, and it has now been the
// same fix three times. Without the notes line a note added to a figure whose
// data had not changed hit the cache, which had been filled before the note
// existed, and simply did not appear. Without the camera lines TURNING A 3-D
// FIGURE DID NOTHING: the painter draws the PREPARED spec, the prepared spec
// came out of a cache filled at the previous angle, and every frame of a drag
// redrew the same picture. It looked like the figure snapping between a few
// fixed faces, because the only thing that invalidated the cache was the point
// count changing when draft thinning switched off at the end of the gesture -
// so the figure moved once, on release, and not at all while being dragged.
// Zoom was the same fault seen from the other side: a wheel notch changed a
// number nothing read.
inline PlotSpec& applyLimits(PlotSpec& out,const PlotSpec& in){
    if(!isUnset(in.xAxis.min)) out.xAxis.min=in.xAxis.min;
    if(!isUnset(in.xAxis.max)) out.xAxis.max=in.xAxis.max;
    if(!isUnset(in.yAxis.min)) out.yAxis.min=in.yAxis.min;
    if(!isUnset(in.yAxis.max)) out.yAxis.max=in.yAxis.max;
    // The THIRD axis too, now that it has limits worth carrying. draw3D fits
    // its cube to these, so leaving them behind here would be the rotation bug
    // again in a new place: the person types a ceiling of ten, the painter
    // draws the cached spec, and the number they typed changes nothing.
    if(!isUnset(in.zAxis.min)) out.zAxis.min=in.zAxis.min;
    if(!isUnset(in.zAxis.max)) out.zAxis.max=in.zAxis.max;
    // And the colour scale, for the same reason. No rewrite in prepareSpec
    // reads any of these - they are painting decisions, so they are absent
    // from the fingerprint - which is exactly why they have to be copied on
    // afterwards or the cached spec's defaults win.
    out.style.colourMin=in.style.colourMin;
    out.style.colourMax=in.style.colourMax;
    out.style.colourLevels=in.style.colourLevels;
    out.style.colourOutOfRangeDropped=in.style.colourOutOfRangeDropped;
    out.style.customColours=in.style.customColours;
    // HOW A VALUE IS PLACED ON THE RAMP - linear, log10 or quantile - which is
    // the same kind of decision as the four above and belongs in the same
    // place for the same reason.
    //
    // It was first written into prepareSpec, and that was wrong in a way worth
    // recording: `preparedCached` does not call prepareSpec, it inlines the
    // same steps, so the choice reached a figure prepared directly and never
    // one that went through the cache - which is every figure the application
    // actually draws. Two implementations of one question, and the change
    // landed in the half nothing uses. It rendered pixel-for-pixel identical at
    // every setting and looked, from the outside, exactly like a control that
    // does nothing.
    //
    // Here it cannot miss: both cache paths and prepareSpec end at this
    // function, which is the whole reason it exists.
    //
    // Read off the CALLER's parameters and applied whatever the engine is: an
    // engine that does not colour by value never calls rampPosition, so
    // carrying the choice costs it nothing, and asking "is this a field" here
    // would need the backend's own answer in a header that cannot see it.
    {
        const int choice=int(in.parameter(QStringLiteral("valueScale"),0.0)+0.5);
        out.style.colourClasses=
            qBound(2,int(in.parameter(QStringLiteral("classes"),5.0)+0.5),9);
        out.style.colourScaleKind= choice==1 ? ColourScale::Log10
                                 : choice==2 ? ColourScale::Quantile
                                             : ColourScale::Linear;
    }
    // The caller's notes, refreshed; the engine's own, kept. See
    // PlotAnnotation::derived - this line used to be a plain assignment, which
    // discarded every note a rewrite had just produced.
    {
        QVector<PlotAnnotation> fromEngine;
        for(const PlotAnnotation& note:out.annotations)
            if(note.derived) fromEngine.append(note);
        out.annotations=in.annotations;
        out.annotations+=fromEngine;
    }
    out.view3d=in.view3d;
    // And the in-frame view, for exactly the reason the line above it exists.
    //
    // The prepared spec is cached on a fingerprint that deliberately ignores
    // the way you are LOOKING at the figure, so a gesture that only changes
    // this would leave the painter drawing the cached copy and the number the
    // gesture moved would be read by nobody. That is precisely the bug the 3-D
    // camera had - rotating did nothing for a whole session because
    // `out.view3d=in.view3d` was missing - and it would present identically
    // here: dragging a treemap would move it once, when something else
    // happened to invalidate the cache, and not at all in between.
    out.frameView=in.frameView;
    return out;
}

// Translate and scale the painter so the figure is drawn moved within `frame`.
//
// Clipped to the frame, so a magnified figure cannot spill over the title, the
// margins or - on an export - the edge of the page.
//
// The zoom is about the frame's CENTRE, and the pan is what makes any other
// point reachable: zoomFrameAt converts "keep the pointer still" into the pan
// that achieves it, which keeps this function a pure transform with nothing to
// get wrong. Doing it the other way - zooming about the pointer here - would
// mean the export and the preview needed the pointer position to agree.
inline void applyFrameView(QPainter* p,const QRectF& frame,const PlotSpec& spec){
    const PlotFrameView& v=spec.frameView;
    if(!v.active()) return;
    // A zoom that reaches zero cannot be undone by any gesture and one that
    // reaches infinity stops being drawable. Both are refused here as well as
    // at the gesture, because a spec can arrive from a saved figure.
    if(!(v.zoom>0.0)||!std::isfinite(v.zoom)) return;
    if(!std::isfinite(v.panX)||!std::isfinite(v.panY)) return;
    p->setClipRect(frame);
    const QPointF centre=frame.center();
    p->translate(centre.x()+v.panX*frame.width(),
                 centre.y()+v.panY*frame.height());
    p->scale(v.zoom,v.zoom);
    p->translate(-centre.x(),-centre.y());
}

// TWO SERIES THE SAME COLOUR, WITH A LEGEND NAMING THEM BOTH.
//
// PlotSeries' colour has a default - the one blue - so a caller that hands over
// three series without setting any of them gets three identical blue series.
// A grouped bar chart came out as eight pairs of identical bars above a legend
// naming "measured" and "predicted": the legend said two series and the picture
// said one. The same silence painted a pie as a single flat disc.
//
// The application never showed it because PlotCanvas::buildPlotSeries assigns
// from the palette before handing anything over. That is exactly the shape of
// the scatter's marker flag: the backend must not depend on its caller having
// filled something in, because the next caller will not.
//
// So a series still carrying the DEFAULT takes one from the palette by its
// position. A series whose colour was chosen keeps it - a rewrite's grey
// reference line, a person's own colour - because this fills in what nobody
// filled in and overrides nothing. The palette is the figure's own category
// palette where it has one, which is the colour-vision-corrected set, and the
// standard palette otherwise.
inline void assignDefaultSeriesColours(PlotSpec& spec){
    if(spec.series.size()<2) return;
    const QColor fallback=PlotSeries().color;
    bool anyDefault=false;
    for(const PlotSeries& s:spec.series) if(s.color==fallback){ anyDefault=true; break; }
    if(!anyDefault) return;
    const QVector<QColor> palette=spec.style.categoryPalette.isEmpty()
                                  ?seriesPalette(ColourVision::Standard)
                                  :spec.style.categoryPalette;
    if(palette.isEmpty()) return;
    for(int i=0;i<spec.series.size();++i)
        if(spec.series[i].color==fallback)
            spec.series[i].color=palette.at(i%palette.size());
}

// ===========================================================================
// Data reduction and interpolation.
//
// Two operations that every other plotting tool has and this one did not: make
// a curve smaller without changing its shape, and read a curve between the
// points it was measured at. They are opposites and they share a file because
// they are asked in the same breath - one throws away points nobody needs, the
// other invents points nobody measured, and both are lies of a controlled size.
// ===========================================================================
namespace {

// Ramer-Douglas-Peucker. Keeps the first and last point, finds the point
// furthest from the line between them, and if that distance is over the
// tolerance splits there and recurses. What survives is the set of points that
// no straight line can stand in for.
//
// Iterative rather than recursive, on an explicit stack. The recursive form is
// two lines shorter and blows the stack on the input this engine exists for:
// a monotone series of a hundred thousand points recurses a hundred thousand
// deep, because every split puts one point on one side.
//
// sx and sy scale the axes to a common footing before the distance is measured;
// the tolerance is in that scaled space.
inline QVector<int> douglasPeucker(const QVector<double>& xs,const QVector<double>& ys,
                            double sx,double sy,double tolerance){
    const int n=xs.size();
    QVector<int> out;
    if(n<3){ for(int i=0;i<n;++i) out.append(i); return out; }

    QVector<bool> keep(n,false);
    keep[0]=true; keep[n-1]=true;
    QVector<QPair<int,int>> stack;
    stack.append({0,n-1});
    const double tol2=tolerance*tolerance;

    while(!stack.isEmpty()){
        const QPair<int,int> seg=stack.takeLast();
        const int a=seg.first, b=seg.second;
        if(b<=a+1) continue;
        const double ax=xs[a]*sx, ay=ys[a]*sy;
        const double bx=xs[b]*sx, by=ys[b]*sy;
        const double ex=bx-ax, ey=by-ay;
        const double len2=ex*ex+ey*ey;
        int worst=-1;
        double worstD2=0.0;
        for(int i=a+1;i<b;++i){
            const double px=xs[i]*sx-ax, py=ys[i]*sy-ay;
            double d2;
            if(len2>0.0){
                // Distance to the SEGMENT, not the infinite line: a closed loop
                // has a==b in scaled space, and projecting onto its zero-length
                // direction would keep nothing.
                double t=(px*ex+py*ey)/len2;
                t=qBound(0.0,t,1.0);
                const double qx=px-t*ex, qy=py-t*ey;
                d2=qx*qx+qy*qy;
            }else{
                d2=px*px+py*py;
            }
            if(d2>worstD2){ worstD2=d2; worst=i; }
        }
        if(worst>=0&&worstD2>tol2){
            keep[worst]=true;
            stack.append({a,worst});
            stack.append({worst,b});
        }
    }
    for(int i=0;i<n;++i) if(keep[i]) out.append(i);
    return out;
}

enum class InterpolationKind { Linear, CubicSpline, Akima, Cosine, Nearest };

inline InterpolationKind interpolationFor(const QString& variant){
    const QString v=variant.trimmed();
    if(v.compare(QLatin1String("Linear"),Qt::CaseInsensitive)==0)  return InterpolationKind::Linear;
    if(v.compare(QLatin1String("Cubic Spline"),Qt::CaseInsensitive)==0
     ||v.compare(QLatin1String("Spline"),Qt::CaseInsensitive)==0)  return InterpolationKind::CubicSpline;
    if(v.compare(QLatin1String("Cosine"),Qt::CaseInsensitive)==0)  return InterpolationKind::Cosine;
    if(v.compare(QLatin1String("Nearest"),Qt::CaseInsensitive)==0
     ||v.compare(QLatin1String("Step"),Qt::CaseInsensitive)==0)    return InterpolationKind::Nearest;
    return InterpolationKind::Akima;
}

inline QString interpolationName(InterpolationKind k){
    switch(k){
    case InterpolationKind::Linear:      return QStringLiteral("linear");
    case InterpolationKind::CubicSpline: return QStringLiteral("cubic spline");
    case InterpolationKind::Cosine:      return QStringLiteral("cosine");
    case InterpolationKind::Nearest:     return QStringLiteral("nearest");
    case InterpolationKind::Akima:       break;
    }
    return QStringLiteral("Akima");
}

// The index of the interval containing x, by binary search. Linear scanning
// here is what turns a thousand-sample grid over a hundred-thousand-point
// series into a hundred million comparisons.
inline int intervalFor(const QVector<double>& xs,double x){
    int lo=0, hi=xs.size()-1;
    if(x<=xs[0]) return 0;
    if(x>=xs[hi]) return hi-1;
    while(hi-lo>1){
        const int mid=(lo+hi)/2;
        if(xs[mid]<=x) lo=mid; else hi=mid;
    }
    return lo;
}

// Natural cubic spline second derivatives, by the standard tridiagonal solve.
// "Natural" means the curve is straight at both ends - the second derivative is
// set to zero there - which is the choice that adds no information the data did
// not contain.
inline QVector<double> naturalSplineM(const QVector<double>& xs,const QVector<double>& ys){
    const int n=xs.size();
    QVector<double> m(n,0.0);
    if(n<3) return m;
    QVector<double> c(n,0.0),d(n,0.0);
    for(int i=1;i<n-1;++i){
        const double h0=xs[i]-xs[i-1], h1=xs[i+1]-xs[i];
        if(!(h0>0.0)||!(h1>0.0)) return QVector<double>(n,0.0);
        const double a=h0, b=2.0*(h0+h1), cc=h1;
        const double r=6.0*((ys[i+1]-ys[i])/h1-(ys[i]-ys[i-1])/h0);
        const double denom=b-a*c[i-1];
        if(!(std::abs(denom)>1e-300)) return QVector<double>(n,0.0);
        c[i]=cc/denom;
        d[i]=(r-a*d[i-1])/denom;
    }
    for(int i=n-2;i>=1;--i) m[i]=d[i]-c[i]*m[i+1];
    return m;
}

// Akima slopes. The slope at a point is a weighted blend of the two secants
// either side, weighted by how much the OTHER two secants disagree - which is
// what makes it local, and what makes a step in the data stay a step instead of
// ringing. Akima's 1970 construction, with his end conditions.
inline QVector<double> akimaSlopes(const QVector<double>& xs,const QVector<double>& ys){
    const int n=xs.size();
    QVector<double> t(n,0.0);
    if(n<2) return t;
    if(n==2){
        const double h=xs[1]-xs[0];
        const double s=(h>0.0)?(ys[1]-ys[0])/h:0.0;
        t[0]=t[1]=s;
        return t;
    }
    // Secants, with two extrapolated at each end so the interior formula can be
    // used everywhere. m is indexed from -2, so it is stored shifted by 2.
    QVector<double> m(n+3,0.0);
    for(int i=0;i<n-1;++i){
        const double h=xs[i+1]-xs[i];
        m[i+2]=(h>0.0)?(ys[i+1]-ys[i])/h:0.0;
    }
    m[1]=2.0*m[2]-m[3];
    m[0]=2.0*m[1]-m[2];
    m[n+1]=2.0*m[n]-m[n-1];
    m[n+2]=2.0*m[n+1]-m[n];
    for(int i=0;i<n;++i){
        const double w1=std::abs(m[i+3]-m[i+2]);
        const double w2=std::abs(m[i+1]-m[i]);
        if(w1+w2>1e-300) t[i]=(w1*m[i+1]+w2*m[i+2])/(w1+w2);
        else             t[i]=0.5*(m[i+1]+m[i+2]);   // four collinear secants
    }
    return t;
}

// One evaluation. The spline and Akima coefficient arrays are rebuilt per call
// only if the caller is careless; the engine below builds them once per series
// through interpolatorFor and passes them in.
struct Interpolator {
    InterpolationKind kind=InterpolationKind::Akima;
    QVector<double> m;      // spline second derivatives
    QVector<double> t;      // Akima slopes
};

inline Interpolator interpolatorFor(InterpolationKind kind,const QVector<double>& xs,const QVector<double>& ys){
    Interpolator it; it.kind=kind;
    if(kind==InterpolationKind::CubicSpline) it.m=naturalSplineM(xs,ys);
    else if(kind==InterpolationKind::Akima)  it.t=akimaSlopes(xs,ys);
    return it;
}

inline double interpolateAt(const Interpolator& it,const QVector<double>& xs,const QVector<double>& ys,double x){
    const int n=xs.size();
    if(n==0) return std::numeric_limits<double>::quiet_NaN();
    if(n==1) return ys[0];
    const int i=intervalFor(xs,x);
    const double h=xs[i+1]-xs[i];
    if(!(h>0.0)) return ys[i];
    const double u=(x-xs[i])/h;
    switch(it.kind){
    case InterpolationKind::Nearest:
        return (u<0.5)?ys[i]:ys[i+1];
    case InterpolationKind::Linear:
        return ys[i]+(ys[i+1]-ys[i])*u;
    case InterpolationKind::Cosine: {
        const double w=(1.0-std::cos(u*3.14159265358979323846))/2.0;
        return ys[i]*(1.0-w)+ys[i+1]*w;
    }
    case InterpolationKind::CubicSpline: {
        if(it.m.size()!=n) return ys[i]+(ys[i+1]-ys[i])*u;
        const double a=xs[i+1]-x, b=x-xs[i];
        return (a*a*a-h*h*a)*it.m[i]/(6.0*h)
             + (b*b*b-h*h*b)*it.m[i+1]/(6.0*h)
             + (a*ys[i]+b*ys[i+1])/h;
    }
    case InterpolationKind::Akima: {
        if(it.t.size()!=n) return ys[i]+(ys[i+1]-ys[i])*u;
        // Hermite on the Akima slopes.
        const double u2=u*u, u3=u2*u;
        const double h00= 2.0*u3-3.0*u2+1.0;
        const double h10=     u3-2.0*u2+u;
        const double h01=-2.0*u3+3.0*u2;
        const double h11=     u3-    u2;
        return h00*ys[i]+h10*h*it.t[i]+h01*ys[i+1]+h11*h*it.t[i+1];
    }
    }
    return ys[i]+(ys[i+1]-ys[i])*u;
}

} // namespace

// Every derived engine opens the same way: keep the incoming spec so the title,
// the axes, the style and the publication profile all survive; swap in the
// engine that will actually be drawn; and start with no series, because a
// derivation builds its own. This was written out at thirty-two sites, which
// is thirty-two places for the rule to drift apart.
inline PlotSpec derivedAs(const PlotSpec& in,const QString& engine){
    PlotSpec out=in;
    out.engine=engine;
    out.series.clear();
    return out;
}

// Least squares through paired values, in whatever space the caller has already
// transformed them into.
//
// Six engines fit a straight line and read a physical parameter off it: the
// drag polar's CD0 and k, the lift-curve slope, the Weibull shape and scale,
// the Crow-AMSAA growth, Basquin's exponent, and the free-flow speed in a
// fundamental diagram. Each had written out the same four sums and the same
// determinant, which is six places for a sign to be wrong in one of them.
struct LineFit {
    double slope=0.0;
    double intercept=0.0;
    bool ok=false;
};

inline LineFit fitLine(const QVector<double>& x,const QVector<double>& y,int upTo=-1){
    const int n=(upTo<0)?qMin(x.size(),y.size()):qMin(upTo,qMin(x.size(),y.size()));
    double sx=0,sy=0,sxx=0,sxy=0; int m=0;
    for(int i=0;i<n;++i){
        if(!finite(x[i])||!finite(y[i])) continue;
        sx+=x[i]; sy+=y[i]; sxx+=x[i]*x[i]; sxy+=x[i]*y[i]; ++m;
    }
    LineFit fit;
    if(m<3) return fit;
    const double denom=double(m)*sxx-sx*sx;
    if(std::abs(denom)<1e-18) return fit;
    fit.slope=(double(m)*sxy-sx*sy)/denom;
    fit.intercept=(sy-fit.slope*sx)/double(m);
    fit.ok=finite(fit.slope)&&finite(fit.intercept);
    return fit;
}

// THE BEST STRAIGHT WINDOW of a curve, by the slope of the fit.
//
// Three engines want this and each had written it out: the Tauc plot's
// steepest rise, the Kubelka-Munk absorption edge, and the creep curve's
// flattest secondary stage. Each copied the window out with `mid` at every
// start position and fitted it fresh - so the same quadratic sat in the
// catalogue three times, and had to be found three times. (The growth curve
// has a fourth copy of the shape but scores its windows by straightness as
// well as steepness, so it keeps its own loop.)
//
// Here the search slides. A least-squares fit comes from five running
// numbers, and moving the window one place forward adds a point at the
// leading edge and drops one at the trailing edge - the same five sums
// `fitLine` above accumulates, in the same order within each window.
//
// A TIE GOES TO THE EARLIEST WINDOW, deliberately. A genuinely straight
// stretch of curve is equally straight everywhere along it, so its windows
// tie exactly, and the winner would otherwise be settled by the last bits of
// the accumulation - which moves the drawn tangent along the curve whenever
// the sums are accumulated in a different order.
struct WindowFit {
    LineFit fit;
    int at=-1;
};

inline WindowFit slidingBestFit(const QVector<double>& xs,const QVector<double>& ys,
                         int window,bool steepest){
    WindowFit out;
    const int n=qMin(xs.size(),ys.size());
    if(window<3||window>n) return out;
    double sx=0,sy=0,sxx=0,sxy=0; int m=0;
    const auto take=[&](int i,double sign){
        if(!finite(xs[i])||!finite(ys[i])) return;
        sx+=sign*xs[i]; sy+=sign*ys[i];
        sxx+=sign*xs[i]*xs[i]; sxy+=sign*xs[i]*ys[i];
        m+=int(sign);
    };
    for(int i=0;i<window;++i) take(i,1.0);
    double bestSlope=0.0;
    for(int start=0;start+window<=n;++start){
        if(start>0){ take(start-1,-1.0); take(start+window-1,1.0); }
        if(m<3) continue;
        const double denom=double(m)*sxx-sx*sx;
        if(std::abs(denom)<1e-18) continue;
        LineFit f;
        f.slope=(double(m)*sxy-sx*sy)/denom;
        f.intercept=(sy-f.slope*sx)/double(m);
        f.ok=finite(f.slope)&&finite(f.intercept);
        if(!f.ok) continue;
        if(steepest){
            // A RISING window, and strictly steeper than the incumbent: the
            // tangent these two engines draw is an absorption edge, and an
            // edge that falls is not one.
            if(!(f.slope>bestSlope+1e-12*std::abs(bestSlope))) continue;
        }else{
            // The flattest, with no floor - a creep rate can be anything, and
            // the first fittable window has to be able to win, or the search
            // starts from a number rather than from the data.
            if(out.at>=0&&!(f.slope<bestSlope-1e-12*std::abs(bestSlope))) continue;
        }
        bestSlope=f.slope; out.fit=f; out.at=start;
    }
    return out;
}

// Least squares of a parabola, for the engines whose answer is a TURNING
// POINT rather than a slope: the optimum moisture of a compaction test, the
// waist of a beam caustic. Both used to be read off whichever sample happened
// to be highest, and the peak of a smooth curve almost never is one.
//
// Solved as a 3x3 normal-equation system by Gauss-Jordan with partial
// pivoting. Three unknowns, so the cost is irrelevant and the stability is
// not: the powers of x span six orders of magnitude on a caustic measured in
// millimetres, and the unpivoted elimination loses the quadratic term.
struct QuadFit {
    double a=0.0,b=0.0,c=0.0;
    bool ok=false;
};

inline QuadFit fitQuadratic(const QVector<double>& x,const QVector<double>& y){
    QuadFit fit;
    const int n=qMin(x.size(),y.size());
    double s[3][4]={};
    int used=0;
    // Centred on the mean of x before fitting and shifted back afterwards.
    // A caustic measured from a bench datum has x around 500 and a spread of
    // 20, and the uncentred normal matrix for that is numerically singular
    // long before the data is uninformative.
    double centre=0.0;
    for(int i=0;i<n;++i) if(finite(x[i])&&finite(y[i])){ centre+=x[i]; ++used; }
    if(used<4) return fit;
    centre/=double(used);
    for(int i=0;i<n;++i){
        if(!finite(x[i])||!finite(y[i])) continue;
        const double u=x[i]-centre;
        const double basis[3]={1.0,u,u*u};
        for(int r=0;r<3;++r){
            for(int c=0;c<3;++c) s[r][c]+=basis[r]*basis[c];
            s[r][3]+=basis[r]*y[i];
        }
    }
    for(int col=0;col<3;++col){
        int pivot=col;
        for(int r=col+1;r<3;++r)
            if(std::abs(s[r][col])>std::abs(s[pivot][col])) pivot=r;
        if(std::abs(s[pivot][col])<1e-14) return fit;
        if(pivot!=col) for(int c=0;c<4;++c) std::swap(s[col][c],s[pivot][c]);
        const double d=s[col][col];
        for(int c=0;c<4;++c) s[col][c]/=d;
        for(int r=0;r<3;++r){
            if(r==col) continue;
            const double f=s[r][col];
            for(int c=0;c<4;++c) s[r][c]-=f*s[col][c];
        }
    }
    // y = A + B u + C u^2 with u = x - centre, expanded back to x.
    const double A=s[0][3],B=s[1][3],C=s[2][3];
    fit.a=C;
    fit.b=B-2.0*C*centre;
    fit.c=A-B*centre+C*centre*centre;
    fit.ok=finite(fit.a)&&finite(fit.b)&&finite(fit.c);
    return fit;
}

// ======================================================================
// Geography
//
// Every geographic engine used to be longitude on x and latitude on y, and
// nothing else. That is an equirectangular projection, and it is wrong in a
// way that gets worse the further from the equator you work: at 55 degrees
// north a degree of longitude is 64 km, not 111, so a country is drawn half
// again too wide, a circle of range is drawn as an ellipse, and the shortest
// path between two airports - a great circle - is drawn as a curve while a
// straight line on the picture is not the shortest path at all.
//
// So the projection became a catalogue variant. "Equirectangular" is still the
// default and still exactly what it was, so every figure already saved is
// unchanged; the others are the standard formulas, on a sphere of radius
// 6378137 m for the cylindricals and on WGS-84 for UTM, which is the ellipsoid
// every survey and infrastructure dataset is already in.
//
// Same anonymous namespace as derivedAs above: these are helpers for the
// rewrites, not part of the backend's interface.

constexpr double kEarthRadius=6378137.0;            // WGS-84 semi-major axis, metres
constexpr double kFlattening=1.0/298.257223563;     // WGS-84
constexpr double kDegToRad=M_PI/180.0;
constexpr double kRadToDeg=180.0/M_PI;

// Named MapProjection because Projection above is the 3-D orthographic one:
// two different things called the same thing in one file is how the wrong one
// gets used.
enum class MapProjection { Equirectangular, Mercator, WebMercator, LambertConformal,
                        AzimuthalEquidistant, UTM };

inline MapProjection mapProjectionFor(const QString& variant){
    const QString v=variant.trimmed();
    if(v.compare(QLatin1String("Mercator"),Qt::CaseInsensitive)==0) return MapProjection::Mercator;
    if(v.compare(QLatin1String("Web Mercator"),Qt::CaseInsensitive)==0) return MapProjection::WebMercator;
    if(v.compare(QLatin1String("Lambert Conformal Conic"),Qt::CaseInsensitive)==0) return MapProjection::LambertConformal;
    if(v.compare(QLatin1String("Azimuthal Equidistant"),Qt::CaseInsensitive)==0) return MapProjection::AzimuthalEquidistant;
    if(v.compare(QLatin1String("UTM"),Qt::CaseInsensitive)==0) return MapProjection::UTM;
    return MapProjection::Equirectangular;
}

// A projection needs a place to be centred on, and the honest choice is the
// data's own middle: a Lambert cone fitted to Norway and used for Chile is a
// worse picture than no projection at all. The standard parallels follow
// Kavrayskiy's rule, at a sixth in from each edge of the latitude span.
struct GeoFrame {
    MapProjection kind=MapProjection::Equirectangular;
    double lon0=0.0, lat0=0.0, lat1=0.0, lat2=0.0;
    int utmZone=31;
    bool northern=true;
};

inline GeoFrame geoFrameFor(MapProjection kind,const QVector<double>& lon,const QVector<double>& lat){
    GeoFrame f; f.kind=kind;
    double lonLo=std::numeric_limits<double>::infinity(),lonHi=-lonLo;
    double latLo=lonLo,latHi=-lonLo;
    const int n=qMin(lon.size(),lat.size());
    for(int i=0;i<n;++i){
        if(!finite(lon[i])||!finite(lat[i])) continue;
        lonLo=qMin(lonLo,lon[i]); lonHi=qMax(lonHi,lon[i]);
        latLo=qMin(latLo,lat[i]); latHi=qMax(latHi,lat[i]);
    }
    // Every bound, not just the two lows. A column of 1e308 gives a finite low
    // and an infinite high, the midpoint is then infinite, and casting that to
    // an int for the UTM zone is undefined behaviour - which the sanitiser
    // catches and a release build does not.
    if(!finite(lonLo)||!finite(lonHi)||!finite(latLo)||!finite(latHi)) return f;
    f.lon0=0.5*(lonLo+lonHi);
    f.lat0=0.5*(latLo+latHi);
    if(!finite(f.lon0)||!finite(f.lat0)) return f;
    const double span=latHi-latLo;
    f.lat1=latLo+span/6.0;
    f.lat2=latHi-span/6.0;
    if(std::abs(f.lat2-f.lat1)<1e-6){ f.lat1=f.lat0-1.0; f.lat2=f.lat0+1.0; }
    f.utmZone=qBound(1,int(std::floor((f.lon0+180.0)/6.0))+1,60);
    f.northern=(f.lat0>=0.0);
    return f;
}

// Transverse Mercator on the WGS-84 ellipsoid, Krueger series to fourth order.
// Sub-millimetre inside a zone, which is far past what any plot resolves, and
// it is the projection that survey, cadastral and infrastructure data arrives
// in - so a UTM easting and northing can be plotted against the coordinates
// they were measured in rather than converted by hand first.
inline QPointF utmForward(double lonDeg,double latDeg,int zone,bool northern){
    const double k0=0.9996;
    const double e2=kFlattening*(2.0-kFlattening);
    const double ep2=e2/(1.0-e2);
    const double lon0=double((zone-1)*6-180+3)*kDegToRad;
    const double lat=latDeg*kDegToRad;
    const double lon=lonDeg*kDegToRad;
    const double sinLat=std::sin(lat),cosLat=std::cos(lat),tanLat=std::tan(lat);
    const double N=kEarthRadius/std::sqrt(1.0-e2*sinLat*sinLat);
    const double T=tanLat*tanLat;
    const double C=ep2*cosLat*cosLat;
    const double A=(lon-lon0)*cosLat;
    const double M=kEarthRadius*((1.0-e2/4.0-3.0*e2*e2/64.0-5.0*e2*e2*e2/256.0)*lat
                   -(3.0*e2/8.0+3.0*e2*e2/32.0+45.0*e2*e2*e2/1024.0)*std::sin(2.0*lat)
                   +(15.0*e2*e2/256.0+45.0*e2*e2*e2/1024.0)*std::sin(4.0*lat)
                   -(35.0*e2*e2*e2/3072.0)*std::sin(6.0*lat));
    const double easting=k0*N*(A+(1.0-T+C)*A*A*A/6.0
                   +(5.0-18.0*T+T*T+72.0*C-58.0*ep2)*A*A*A*A*A/120.0)+500000.0;
    double northing=k0*(M+N*tanLat*(A*A/2.0+(5.0-T+9.0*C+4.0*C*C)*A*A*A*A/24.0
                   +(61.0-58.0*T+T*T+600.0*C-330.0*ep2)*A*A*A*A*A*A/720.0));
    if(!northern) northing+=10000000.0;
    return QPointF(easting,northing);
}

// One longitude and latitude as a point on the picture. Non-finite in, non-
// finite out: a point that cannot be projected - a pole in Mercator, a place on
// the far side of the globe in an azimuthal projection - is dropped by the
// caller rather than clamped to an edge it does not belong on.
inline QPointF projectPoint(const GeoFrame& f,double lonDeg,double latDeg){
    if(!finite(lonDeg)||!finite(latDeg)) return QPointF(qQNaN(),qQNaN());
    switch(f.kind){
    case MapProjection::Equirectangular:
        return QPointF(lonDeg,latDeg);
    case MapProjection::Mercator: {
        // The poles are infinitely far away in Mercator; 85.05 degrees is where
        // the projection is conventionally cut, and it is exactly square there.
        if(std::abs(latDeg)>85.0511) return QPointF(qQNaN(),qQNaN());
        const double y=kRadToDeg*std::log(std::tan(M_PI/4.0+latDeg*kDegToRad/2.0));
        return QPointF(lonDeg,y);
    }
    case MapProjection::WebMercator: {
        if(std::abs(latDeg)>85.0511) return QPointF(qQNaN(),qQNaN());
        const double x=kEarthRadius*lonDeg*kDegToRad;
        const double y=kEarthRadius*std::log(std::tan(M_PI/4.0+latDeg*kDegToRad/2.0));
        return QPointF(x,y);
    }
    case MapProjection::LambertConformal: {
        const double lat1=f.lat1*kDegToRad,lat2=f.lat2*kDegToRad;
        const double lat=latDeg*kDegToRad,lat0=f.lat0*kDegToRad;
        const double cosLat1=std::cos(lat1);
        const double t=std::tan(M_PI/4.0+lat/2.0);
        const double t1=std::tan(M_PI/4.0+lat1/2.0);
        const double t2=std::tan(M_PI/4.0+lat2/2.0);
        const double t0=std::tan(M_PI/4.0+lat0/2.0);
        if(!(t>0)||!(t1>0)||!(t2>0)||!(t0>0)) return QPointF(qQNaN(),qQNaN());
        // Two standard parallels, unless they coincide - then the tangent case.
        const double n=(std::abs(lat1-lat2)<1e-9)
            ? std::sin(lat1)
            : std::log(cosLat1/std::cos(lat2))/std::log(t2/t1);
        if(!finite(n)||std::abs(n)<1e-12) return QPointF(qQNaN(),qQNaN());
        const double F=cosLat1*std::pow(t1,n)/n;
        const double rho=kEarthRadius*F/std::pow(t,n);
        const double rho0=kEarthRadius*F/std::pow(t0,n);
        const double theta=n*(lonDeg-f.lon0)*kDegToRad;
        if(!finite(rho)||!finite(rho0)) return QPointF(qQNaN(),qQNaN());
        return QPointF(rho*std::sin(theta),rho0-rho*std::cos(theta));
    }
    case MapProjection::AzimuthalEquidistant: {
        // Distance and bearing from the centre are both true, so a great
        // circle through the centre is a straight line and a range ring is a
        // circle. This is the projection a route or a coverage map wants.
        const double lat=latDeg*kDegToRad,lat0=f.lat0*kDegToRad;
        const double dLon=(lonDeg-f.lon0)*kDegToRad;
        const double cosC=std::sin(lat0)*std::sin(lat)+std::cos(lat0)*std::cos(lat)*std::cos(dLon);
        const double c=std::acos(qBound(-1.0,cosC,1.0));
        if(!finite(c)) return QPointF(qQNaN(),qQNaN());
        // The antipode has no unique direction, so it is dropped rather than
        // drawn at an arbitrary bearing.
        if(std::abs(M_PI-c)<1e-6) return QPointF(qQNaN(),qQNaN());
        const double k=(std::abs(c)<1e-12)?1.0:(c/std::sin(c));
        const double x=kEarthRadius*k*std::cos(lat)*std::sin(dLon);
        const double y=kEarthRadius*k*(std::cos(lat0)*std::sin(lat)
                                       -std::sin(lat0)*std::cos(lat)*std::cos(dLon));
        return QPointF(x,y);
    }
    case MapProjection::UTM:
        return utmForward(lonDeg,latDeg,f.utmZone,f.northern);
    }
    return QPointF(lonDeg,latDeg);
}

inline QString projectionXLabel(const GeoFrame& f){
    switch(f.kind){
    case MapProjection::Equirectangular: return QStringLiteral("longitude (deg E)");
    case MapProjection::Mercator:        return QStringLiteral("longitude (deg E, Mercator)");
    case MapProjection::WebMercator:     return QStringLiteral("easting (m, Web Mercator)");
    case MapProjection::LambertConformal:return QStringLiteral("easting (m, Lambert conformal)");
    case MapProjection::AzimuthalEquidistant: return QStringLiteral("easting (m, azimuthal equidistant)");
    case MapProjection::UTM:             return QStringLiteral("easting (m)");
    }
    return QStringLiteral("x");
}

inline QString projectionYLabel(const GeoFrame& f){
    switch(f.kind){
    case MapProjection::Equirectangular: return QStringLiteral("latitude (deg N)");
    case MapProjection::Mercator:        return QStringLiteral("latitude (deg N, Mercator)");
    case MapProjection::WebMercator:     return QStringLiteral("northing (m, Web Mercator)");
    case MapProjection::LambertConformal:return QStringLiteral("northing (m, Lambert conformal)");
    case MapProjection::AzimuthalEquidistant: return QStringLiteral("northing (m, azimuthal equidistant)");
    case MapProjection::UTM:             return QStringLiteral("northing (m, zone %1%2")
                                          .arg(f.utmZone).arg(f.northern?QStringLiteral("N)"):QStringLiteral("S)"));
    }
    return QStringLiteral("y");
}

// Great-circle distance, on a sphere. The haversine form, because the simple
// spherical cosine loses all its precision on the short legs - and a taxi-out
// or an approach leg is exactly where a track has its shortest legs.
inline double greatCircleMetres(double lon1,double lat1,double lon2,double lat2){
    const double p1=lat1*kDegToRad,p2=lat2*kDegToRad;
    const double dp=(lat2-lat1)*kDegToRad,dl=(lon2-lon1)*kDegToRad;
    const double a=std::sin(dp/2)*std::sin(dp/2)
                  +std::cos(p1)*std::cos(p2)*std::sin(dl/2)*std::sin(dl/2);
    return 2.0*kEarthRadius*std::asin(std::sqrt(qBound(0.0,a,1.0)));
}

// A point a fraction of the way along the great circle between two others.
// Spherical interpolation on the unit vectors, which stays well behaved when
// the two ends are nearly coincident and when they are nearly antipodal.
inline void greatCirclePoint(double lon1,double lat1,double lon2,double lat2,
                      double t,double& lon,double& lat){
    const double p1=lat1*kDegToRad,l1=lon1*kDegToRad;
    const double p2=lat2*kDegToRad,l2=lon2*kDegToRad;
    const double x1=std::cos(p1)*std::cos(l1),y1=std::cos(p1)*std::sin(l1),z1=std::sin(p1);
    const double x2=std::cos(p2)*std::cos(l2),y2=std::cos(p2)*std::sin(l2),z2=std::sin(p2);
    const double dot=qBound(-1.0,x1*x2+y1*y2+z1*z2,1.0);
    const double omega=std::acos(dot);
    double a=1.0-t,b=t;
    if(omega>1e-9){
        const double s=std::sin(omega);
        a=std::sin((1.0-t)*omega)/s;
        b=std::sin(t*omega)/s;
    }
    const double x=a*x1+b*x2,y=a*y1+b*y2,z=a*z1+b*z2;
    lat=std::atan2(z,std::sqrt(x*x+y*y))*kRadToDeg;
    lon=std::atan2(y,x)*kRadToDeg;
}



// One axis's worth of values, transformed.
//
// Applied AFTER the engine's own rewrite, so it acts on what will actually be
// drawn rather than on what was mapped: z-scoring a histogram's counts is then
// z-scoring the counts, which is what the axis says it is doing. Applied before
// the limits go back on, so a zoom set on transformed values stays where it was
// put.
//
// Log10 is not here. It is drawn as a log AXIS - decade ticks, 10^n labels,
// original numbers on the page - and rewriting the values as well would put the
// data through the transform twice.
inline void transformValues(QVector<double>& v,int mode){
    if(mode==AxisLinear||mode==AxisLog10||v.isEmpty()) return;

    if(mode==AxisLog1p){
        // log10(1 + x). The log axis for a column that legitimately reaches
        // zero - counts, concentrations, failures per cycle - where a plain log
        // axis has to drop every one of those rows. Below -1 there is no answer,
        // so those become gaps rather than a silently shifted value.
        for(double& d:v) d=(finite(d)&&d>-1.0)?std::log10(1.0+d)
                                             :std::numeric_limits<double>::quiet_NaN();
        return;
    }

    if(mode==AxisZScore){
        // Two passes rather than the sum-of-squares shortcut: on a column whose
        // values are large and whose spread is small - a temperature in kelvin,
        // a timestamp - E[x^2] - E[x]^2 subtracts two nearly equal large numbers
        // and can come out negative, which is a standard deviation that cannot
        // exist and a plot full of NaN.
        double sum=0.0; int n=0;
        for(double d:v) if(finite(d)){ sum+=d; ++n; }
        if(n<2) return;
        const double mean=sum/double(n);
        double ss=0.0;
        for(double d:v) if(finite(d)){ const double e=d-mean; ss+=e*e; }
        const double sd=std::sqrt(ss/double(n-1));
        if(!(sd>0.0)||!finite(sd)) return;   // a constant column has no z-score
        for(double& d:v) if(finite(d)) d=(d-mean)/sd;
        return;
    }

    if(mode==AxisQuantile){
        // Each value replaced by its position in the sorted sample, 0 to 1.
        // Ties share the midpoint of the range they span, so a column that is
        // 97% zeros - which is exactly the shape that made a real dataset draw
        // as a flat line on the axis - does not have its zeros spread across
        // 97% of the axis as if they differed.
        QVector<QPair<double,int>> order;
        order.reserve(v.size());
        for(int i=0;i<v.size();++i) if(finite(v[i])) order.append({v[i],i});
        const int n=order.size();
        if(n<2) return;
        std::sort(order.begin(),order.end(),
                  [](const QPair<double,int>& a,const QPair<double,int>& b){
                      return a.first<b.first;
                  });
        int i=0;
        while(i<n){
            int j=i;
            while(j+1<n&&order[j+1].first==order[i].first) ++j;
            const double q=((double(i)+double(j))/2.0)/double(n-1);
            for(int k=i;k<=j;++k) v[order[k].second]=q;
            i=j+1;
        }
        for(double& d:v) if(!finite(d)) d=std::numeric_limits<double>::quiet_NaN();
        return;
    }
}

// What the axis is now measured in. A z-score axis labelled with the column's
// original name and unit is a label that contradicts its own numbers.
inline QString transformedLabel(const QString& label,int mode){
    switch(mode){
    case AxisLog1p:    return QStringLiteral("log10(1 + %1)").arg(label);
    case AxisZScore:   return label.isEmpty()?QStringLiteral("z-score")
                                             :QStringLiteral("%1 (z-score)").arg(label);
    case AxisQuantile: return label.isEmpty()?QStringLiteral("quantile")
                                             :QStringLiteral("quantile of %1").arg(label);
    default:           return label;
    }
}


constexpr int kMaxRewriteDepth = 8;
// ONE COUNTER, SHARED BY EVERY TRANSLATION UNIT THAT REWRITES.
//
// This was a file-scope thread_local while the backend was one translation
// unit. Split across several, a plain definition in a header gives each .cpp
// its OWN counter - and the recursion it guards crosses them, because a
// rewrite in one engine group calls prepareSpec on a spec that can land in
// another. Every counter would see a depth of one and the guard would never
// fire, which is the failure it was written to prevent.
//
// A function-local static inside an inline function is ONE object across the
// whole program. That is the property needed here, and it is the reason this
// is a function and not a variable.
inline int& rewriteDepthCounter(){ static thread_local int depth=0; return depth; }

struct RewriteDepthGuard {
    bool tooDeep;
    RewriteDepthGuard() : tooDeep(rewriteDepthCounter()>=kMaxRewriteDepth) {
        if(!tooDeep) ++rewriteDepthCounter();
    }
    ~RewriteDepthGuard() { if(!tooDeep) --rewriteDepthCounter(); }
    RewriteDepthGuard(const RewriteDepthGuard&)=delete;
    RewriteDepthGuard& operator=(const RewriteDepthGuard&)=delete;
};

// ---------------------------------------------------------------------------
// Two helpers that were file-scope in QtPlotBackend.cpp rather than inside one
// of its anonymous namespaces, and are called from the engine-rewrite chain.
// They moved here with the rest for the same reason: the chain is now six
// translation units and all six have to see them.
// ---------------------------------------------------------------------------
// The variable names down the side of a square matrix.
//
// A correlation, covariance or spy matrix is k columns against the same k
// columns, and every one of them numbered its axes 0, 1, 2 ... The correlation
// matrix went one worse and joined all five names into the x AXIS LABEL - a
// single line of text under the plot reading "signal, paired, p_value, label,
// cost", which is the information present and unusable: nothing said which
// name went with which row.
//
// The resolution matters as much as the names. These carry exactly k by k
// cells and nothing else; without saying so the heatmap chose its own grid and
// resampled a 5 by 5 matrix onto it, so the cells came out at sizes that do
// not correspond to any pair of variables. The same fault, and the same fix,
// as the confusion matrix.
inline void labelMatrixAxes(PlotSpec& out,const QStringList& names){
    const int k=names.size();
    if(k<=0) return;
    out.style.fieldResolution=k;
    out.xAxis.tickValues.clear(); out.xAxis.tickLabels.clear();
    out.yAxis.tickValues.clear(); out.yAxis.tickLabels.clear();
    for(int i=0;i<k;++i){
        out.xAxis.tickValues.append(double(i)); out.xAxis.tickLabels.append(names.at(i));
        out.yAxis.tickValues.append(double(i)); out.yAxis.tickLabels.append(names.at(i));
    }
}

// The floor a p-value of exactly zero is drawn at.
//
// Both -log10(p) engines clamped to 1e-300, which puts a zero at 300 on an
// axis whose real content lies between 0 and about 8. The threshold line a
// Manhattan plot exists to be read against - 5e-8, at y = 7.3 - was then a
// pixel above the baseline, and every genuine peak with it. The comment said
// "clamp rather than plot infinity"; 300 is infinity as far as the axis is
// concerned.
//
// A zero p-value is a rounding artefact of whatever produced it. Drawn a
// decade below the smallest one that IS representable in the data, it stays
// the most extreme point on the figure - which is honest - without deciding
// the scale for everything else. With no non-zero p at all there is no scale
// to preserve, so a plain small number will do.
inline double negLogFloor(const QVector<double>& values){
    double smallest=std::numeric_limits<double>::infinity();
    for(double p:values) if(finite(p)&&p>0.0&&p<=1.0) smallest=qMin(smallest,p);
    if(!finite(smallest)) return 1e-10;
    return qMax(1e-300,smallest*0.1);
}

} // namespace graphvis
