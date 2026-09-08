#include "QtPlotBackend.h"
#include "ColourMaps.h"
#include "Expression.h"

#include <cmath>
#include <limits>

#include <QFontMetricsF>
#include <QHash>
#include <QSet>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <algorithm>

namespace graphvis {

namespace {
// A series' dash pattern, when it has one. Only the Monochrome colour-vision
// mode sets these: with no hue to distinguish series, the line style is what
// tells them apart, and it has to survive vector export as geometry rather than
// as a colour.
inline void applySeriesDash(QPen& pen,const PlotSeries& s){
    if(s.dashPattern.isEmpty()) return;
    pen.setDashPattern(s.dashPattern);
    pen.setCapStyle(Qt::FlatCap);
}
} // namespace

namespace {

constexpr double kMarginLeft   = 64.0;
constexpr double kMarginRight  = 18.0;
constexpr double kMarginTop    = 34.0;
constexpr double kMarginBottom = 52.0;
constexpr double kTickLen      = 4.0;

bool finite(double v){ return v==v && !qIsInf(v); }

// Superscript digits let a log decade label read as 10^-1 without a full
// mathtext engine. GraphVis 17 renders these through Matplotlib mathtext;
// the visible result is the same for the exponents an axis actually uses.
QString superscript(int exponent){
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
double niceStep(double rough){
    if(!(rough>0)) return 1.0;
    const double exp10=std::pow(10.0,std::floor(std::log10(rough)));
    const double f=rough/exp10;
    double nice;
    if(f<1.5) nice=1.0; else if(f<3.0) nice=2.0; else if(f<7.0) nice=5.0; else nice=10.0;
    return nice*exp10;
}

QString formatTick(double v,double step){
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

} // namespace

QStringList QtPlotBackend::supportedEngines() const {
    // The first engine tier. Every other catalogue entry stays visible in the
    // Graph Library but is reported unsupported, so nothing silently draws the
    // wrong picture. Entries are added here as their engines are ported.
    //
    // Built once. This used to construct a fresh 135-element QStringList on
    // every call, and supports() calls it for each engine the graph library
    // asks about.
    static const QStringList kEngines{
        QStringLiteral("Line Chart"),
        QStringLiteral("Stairs"),
        QStringLiteral("Area"),
        QStringLiteral("Error Bar"),
        QStringLiteral("4D / 5D Scatter"),
        QStringLiteral("Bar"),
        QStringLiteral("Horizontal Bar"),
        QStringLiteral("Stem"),
        QStringLiteral("Stacked Lines"),
        QStringLiteral("Histogram"),
        QStringLiteral("Box Plot"),
        QStringLiteral("Pie"),
        QStringLiteral("Donut"),
        // Statistical tier. Each is a rewrite in prepareSpec onto geometry
        // above rather than a draw function of its own - see the comment there.
        QStringLiteral("ECDF"),
        QStringLiteral("Q-Q Plot"),
        QStringLiteral("KDE Density"),
        QStringLiteral("Bland-Altman"),
        QStringLiteral("Control Chart"),
        QStringLiteral("ROC Curve"),
        QStringLiteral("Volcano Plot"),
        QStringLiteral("Pareto Front"),
        QStringLiteral("Performance Ceiling"),
        QStringLiteral("Global Sensitivity"),
        QStringLiteral("Swarm"),
        // Domain tier. These are the engines the bioelectrochemical work
        // actually needs, and they compute what they plot rather than trusting
        // a column to have been kept in step - power from I and V, removal from
        // the starting concentration, the Gompertz parameters from a real fit.
        QStringLiteral("Gompertz H₂ Kinetics"),
        QStringLiteral("sCOD Degradation Profile"),
        QStringLiteral("VFA Concentration Profile"),
        QStringLiteral("Polarisation & Power Curve"),
        QStringLiteral("EIS: Nyquist"),
        QStringLiteral("EIS: Bode"),
        QStringLiteral("1D Marginal Responses"),
        // Field tier. These have geometry of their own - a grid of cells, level
        // crossings through that grid, a mirrored density, a radial projection.
        QStringLiteral("2D Heatmap"),
        QStringLiteral("2D Contour"),
        QStringLiteral("2D Histogram"),
        QStringLiteral("Hexbin Density"),
        QStringLiteral("Correlation Matrix"),
        QStringLiteral("Violin Plot"),
        QStringLiteral("Raincloud"),
        QStringLiteral("Forest Plot"),
        QStringLiteral("Polar Line"),
        QStringLiteral("Polar Scatter"),
        // 3-D tier, orthographic. The VTK viewport remains the interactive,
        // lit, rotatable option; these are the ones that export as vectors.
        QStringLiteral("3D Line"),
        QStringLiteral("3D Scatter"),
        QStringLiteral("3D Topography / Surface"),
        QStringLiteral("3D Mesh"),
        // Advanced tier, signal transforms and marker variants. Each is one
        // operation on the mapped series, drawn with geometry already here.
        QStringLiteral("Derivative"),
        QStringLiteral("Integral"),
        QStringLiteral("Cumulative Sum"),
        QStringLiteral("Rolling Mean"),
        QStringLiteral("Rolling Median"),
        QStringLiteral("Moving Std"),
        QStringLiteral("Autocorrelation"),
        QStringLiteral("Lag Plot"),
        QStringLiteral("Cumulative Histogram"),
        QStringLiteral("Dot Plot"),
        QStringLiteral("Strip Plot"),
        QStringLiteral("Beeswarm"),
        QStringLiteral("Connected Scatter"),
        QStringLiteral("Lollipop"),
        QStringLiteral("Step Mid"),
        QStringLiteral("Fill Between"),
        QStringLiteral("Event Plot"),
        QStringLiteral("Residual Plot"),
        QStringLiteral("Calibration Plot"),
        QStringLiteral("Probability Plot"),
        QStringLiteral("Manhattan Plot"),
        // 3-D and polar variants: the same projection with a different mark.
        QStringLiteral("3D Bar"),
        QStringLiteral("3D Horizontal Bar"),
        QStringLiteral("3D Stem"),
        QStringLiteral("3D Bubble"),
        QStringLiteral("3D Swarm"),
        QStringLiteral("3D Contour"),
        QStringLiteral("Surface + Contours"),
        QStringLiteral("Comet 3D"),
        QStringLiteral("Ribbon"),
        QStringLiteral("Polar Histogram"),
        QStringLiteral("Wind Rose"),
        QStringLiteral("Radar Chart"),
        QStringLiteral("Compass"),
        QStringLiteral("Polar Bubble"),
        QStringLiteral("Covariance Matrix"),
        QStringLiteral("Spy Matrix"),
        // Vector fields. Four mapped columns: x, y and the two components.
        QStringLiteral("Quiver Field"),
        QStringLiteral("Feather"),
        QStringLiteral("Stream Field"),
        QStringLiteral("Stream Particles"),
        QStringLiteral("Phase Portrait"),
        QStringLiteral("Flow Texture (LIC)"),
        QStringLiteral("Divergence Map"),
        QStringLiteral("Vorticity Map"),
        // Geographic. Longitude and latitude are an equirectangular projection
        // and nothing more - there is no basemap, and pretending otherwise
        // would be worse than plotting honest coordinates.
        QStringLiteral("Geo Scatter"),
        QStringLiteral("Geo Line"),
        QStringLiteral("Geo Bubble"),
        QStringLiteral("Geo Density"),
        // The projection is a catalogue variant, so these four gained
        // Mercator, Web Mercator, Lambert conformal conic, azimuthal
        // equidistant and UTM without becoming twenty entries.
        QStringLiteral("Great Circle Route"),
        QStringLiteral("Ground Track"),
        QStringLiteral("Terrain Profile"),
        QStringLiteral("Hypsometric Curve"),
        QStringLiteral("Slope Map"),
        QStringLiteral("Aspect Map"),
        QStringLiteral("Hillshade"),
        // Flight and aviation. Each computes what its chart is read for - the
        // fitted drag coefficients, the lift-curve slope below the stall, the
        // resolved wind components, whether the loading is inside the envelope.
        QStringLiteral("Payload-Range Diagram"),
        QStringLiteral("V-n Flight Envelope"),
        QStringLiteral("Altitude-Mach Envelope"),
        QStringLiteral("Drag Polar"),
        QStringLiteral("Lift Curve"),
        QStringLiteral("Flight Profile"),
        QStringLiteral("Runway Crosswind"),
        QStringLiteral("Weight and Balance Envelope"),
        // Maintenance and reliability. Each reports its fitted parameter in the
        // legend rather than leaving a slope to be measured off the picture.
        QStringLiteral("Weibull Probability Plot"),
        QStringLiteral("Reliability Growth"),
        QStringLiteral("MTBF Trend"),
        QStringLiteral("Calendar Heatmap"),
        QStringLiteral("CUSUM Chart"),
        QStringLiteral("EWMA Chart"),
        QStringLiteral("S-N Fatigue Curve"),
        QStringLiteral("Rainflow Matrix"),
        // Logistics and infrastructure.
        QStringLiteral("Gantt Schedule"),
        QStringLiteral("Availability Timeline"),
        QStringLiteral("Borehole Log"),
        QStringLiteral("OHLC Candlestick"),
        QStringLiteral("Network Graph"),
        QStringLiteral("Chord Diagram"),
        QStringLiteral("Origin-Destination Flow"),
        QStringLiteral("Cumulative Flow"),
        QStringLiteral("Duration Curve"),
        QStringLiteral("Inventory Sawtooth"),
        QStringLiteral("Fundamental Diagram"),
        // Chemistry, materials and the laboratory.
        QStringLiteral("Stress-Strain Curve"),
        QStringLiteral("Arrhenius Plot"),
        QStringLiteral("Titration Curve"),
        QStringLiteral("Calibration Curve"),
        QStringLiteral("Michaelis-Menten"),
        QStringLiteral("Dose-Response Curve"),
        // Medicine, epidemiology and quality.
        QStringLiteral("Kaplan-Meier Survival"),
        QStringLiteral("Funnel Plot"),
        QStringLiteral("X-bar and R Chart"),
        QStringLiteral("Process Capability"),
        // Ocean, hydrology, energy, civil works and the specialised charts.
        QStringLiteral("T-S Diagram"),
        QStringLiteral("CTD Profile"),
        QStringLiteral("Rating Curve"),
        QStringLiteral("Wind Power Curve"),
        QStringLiteral("Drawdown Curve"),
        QStringLiteral("Mass Haul Diagram"),
        QStringLiteral("Shear and Moment"),
        QStringLiteral("Eye Diagram"),
        QStringLiteral("Stereonet"),
        QStringLiteral("Psychrometric Chart"),
        QStringLiteral("Smith Chart"),
        QStringLiteral("Mosaic Plot"),
        QStringLiteral("UpSet Plot"),
        // The gaps a comparison against LabPlot and sci-draw.com turned up.
        QStringLiteral("Dendrogram"),
        QStringLiteral("Precision-Recall Curve"),
        QStringLiteral("Confusion Matrix"),
        QStringLiteral("MA Plot"),
        QStringLiteral("p-Chart"),
        QStringLiteral("np-Chart"),
        QStringLiteral("c-Chart"),
        QStringLiteral("u-Chart"),
        QStringLiteral("Rug Plot"),
        // Data reduction and interpolation. One makes a curve smaller without
        // changing its shape; the other reads it between the measured points.
        QStringLiteral("Data Reduction"),
        QStringLiteral("Interpolation"),
        // Spectral. One radix-2 FFT serves all three; the catalogue variant
        // chooses the window, Hann when nothing asks for another.
        QStringLiteral("Power Spectral Density"),
        QStringLiteral("Spectrogram"),
        QStringLiteral("Cross Correlation"),
        QStringLiteral("Parallel Coordinates"),
        QStringLiteral("Andrews Curves"),
        QStringLiteral("Slope Graph"),
        QStringLiteral("Waterfall"),
        QStringLiteral("Confidence Ellipse"),
        // Composition and hierarchy. These divide a whole rather than plot a
        // coordinate, so they have no axes at all.
        QStringLiteral("Treemap"),
        QStringLiteral("Sunburst"),
        QStringLiteral("Venn Diagram"),
        QStringLiteral("Word Cloud"),
        QStringLiteral("Bubble Cloud"),
        QStringLiteral("Sankey Diagram"),
        QStringLiteral("Ridgeline"),
        QStringLiteral("Population Pyramid"),
        QStringLiteral("Ternary Scatter"),
        QStringLiteral("Piper Diagram"),
        QStringLiteral("Patch"),
        QStringLiteral("Comet"),
        QStringLiteral("Animated Line"),
        QStringLiteral("Scatter + Marginals"),
        QStringLiteral("Plot Matrix"),
        // 3-D fields. VTK stays the right tool for a rotatable, lit,
        // million-cell volume; these are the ones that export as vectors.
        QStringLiteral("3D Quiver"),
        QStringLiteral("Cone Plot"),
        QStringLiteral("Stream Tube"),
        QStringLiteral("Stream Ribbon"),
        QStringLiteral("Tensor Glyph Field"),
        QStringLiteral("Volume Show"),
        QStringLiteral("Volume Slice"),
        QStringLiteral("Isosurface"),
        QStringLiteral("Isonormals"),
        QStringLiteral("Isocaps"),
        QStringLiteral("Contour Slice"),
        // Formula engines. These plot an expression rather than a dataset;
        // see Expression.h for what the parser accepts.
        QStringLiteral("Function Plot"),
        QStringLiteral("Function Contour"),
        QStringLiteral("Function Surface"),
        QStringLiteral("Function Mesh"),
        QStringLiteral("Function 3D Parametric"),
        QStringLiteral("Implicit Function"),
        QStringLiteral("Implicit Surface"),
    };
    return kEngines;
}

// A set built from the same list, so supports() is a hash lookup rather than a
// linear scan of 135 strings.
bool QtPlotBackend::supports(const QString& engine) const {
    static const QSet<QString> kEngineSet=[]{
        QSet<QString> out;
        for(const QString& e:QtPlotBackend().supportedEngines()) out.insert(e);
        return out;
    }();
    return kEngineSet.contains(engine);
}

QFont QtPlotBackend::font(const PlotSpec& spec,double pointSize) const {
    QFont f(spec.style.fontFamily);
    f.setPointSizeF(qMax(1.0,pointSize));
    return f;
}

QVector<AxisTick> QtPlotBackend::linearTicks(double lo,double hi,int wanted){
    QVector<AxisTick> ticks;
    if(!(finite(lo)&&finite(hi))||hi<=lo) return ticks;
    const double step=niceStep((hi-lo)/qMax(1,wanted));
    const double first=std::ceil(lo/step)*step;
    for(double v=first;v<=hi+step*1e-9;v+=step){
        const double snapped=(std::abs(v)<step*1e-9)?0.0:v;
        ticks.append({snapped,formatTick(snapped,step),false});
        if(ticks.size()>512) break;
    }
    return ticks;
}

QVector<AxisTick> QtPlotBackend::logTicks(double lo,double hi){
    // lo/hi are already log10 values. Majors on decades, minors on 2..9.
    QVector<AxisTick> ticks;
    if(!(finite(lo)&&finite(hi))||hi<=lo) return ticks;
    const int first=int(std::floor(lo)), last=int(std::ceil(hi));
    if(last-first>60) return linearTicks(lo,hi,6);
    for(int e=first;e<=last;++e){
        const double major=double(e);
        if(major>=lo-1e-9&&major<=hi+1e-9)
            ticks.append({major,QStringLiteral("10")+superscript(e),false});
        for(int m=2;m<=9;++m){
            const double minor=e+std::log10(double(m));
            if(minor>=lo&&minor<=hi) ticks.append({minor,QString(),true});
        }
    }
    return ticks;
}

// The data range one figure occupies, before any of the chrome is measured.
//
// Split out of computeFrame so the canvas can ask what it is currently showing
// without a painter: an interactive zoom has to start from the range the
// figure already has, and re-deriving that in the canvas would be a second
// copy of these rules - the explicit limits, the flat-series padding, the
// engines that must include zero - that could drift from the drawn one.
//
// Values come back in the space the axis is drawn in, so log10 when the axis
// is logarithmic. That is the space a zoom should be uniform in.
QtPlotBackend::Frame QtPlotBackend::computeRange(const PlotSpec& spec) const {
    Frame f;
    f.xLog=spec.xAxis.log10;
    f.yLog=spec.yAxis.log10;

    double xLo=std::numeric_limits<double>::infinity(),xHi=-xLo;
    double yLo=xLo,yHi=xHi;
    for(const PlotSeries& s:spec.series){
        const int n=qMin(s.x.size(),s.y.size());
        for(int i=0;i<n;++i){
            double x=s.x[i],y=s.y[i];
            if(f.xLog){ if(!(x>0)) continue; x=std::log10(x); }
            if(f.yLog){ if(!(y>0)) continue; y=std::log10(y); }
            if(!finite(x)||!finite(y)) continue;
            xLo=qMin(xLo,x); xHi=qMax(xHi,x);
            yLo=qMin(yLo,y); yHi=qMax(yHi,y);
        }
    }
    if(!finite(xLo)||!finite(xHi)){ xLo=0; xHi=1; }
    if(!finite(yLo)||!finite(yHi)){ yLo=0; yHi=1; }

    // Explicit limits win; GraphVis 17 treats an unset limit as "fit to data".
    if(!isUnset(spec.xAxis.min)) xLo=f.xLog?std::log10(qMax(1e-300,spec.xAxis.min)):spec.xAxis.min;
    if(!isUnset(spec.xAxis.max)) xHi=f.xLog?std::log10(qMax(1e-300,spec.xAxis.max)):spec.xAxis.max;
    if(!isUnset(spec.yAxis.min)) yLo=f.yLog?std::log10(qMax(1e-300,spec.yAxis.min)):spec.yAxis.min;
    if(!isUnset(spec.yAxis.max)) yHi=f.yLog?std::log10(qMax(1e-300,spec.yAxis.max)):spec.yAxis.max;

    // A flat series must still produce a readable axis.
    if(qFuzzyCompare(xLo,xHi)){ const double pad=qMax(0.5,std::abs(xLo)*0.05); xLo-=pad; xHi+=pad; }
    if(qFuzzyCompare(yLo,yHi)){ const double pad=qMax(0.5,std::abs(yLo)*0.05); yLo-=pad; yHi+=pad; }

    // Anything drawn from a zero baseline is read against zero, so the value
    // axis must include it. Only "Bar" did, and Histogram dispatches to drawBar
    // too: a histogram whose bins all held between 100 and 110 counts got an
    // axis starting near 100, so bars were drawn from there and a 10% spread
    // looked like a 2:1 one. Stem and Lollipop draw stalks from the same
    // baseline. Horizontal Bar draws along x, so it is the x axis that must
    // contain zero.
    const bool zeroOnY = spec.engine==QLatin1String("Bar")
                      || spec.engine==QLatin1String("Histogram")
                      || spec.engine==QLatin1String("Cumulative Histogram")
                      || spec.engine==QLatin1String("Stem")
                      || spec.engine==QLatin1String("Lollipop");
    if(zeroOnY&&!f.yLog){ yLo=qMin(yLo,0.0); yHi=qMax(yHi,0.0); }
    if(spec.engine==QLatin1String("Horizontal Bar")&&!f.xLog){ xLo=qMin(xLo,0.0); xHi=qMax(xHi,0.0); }

    if(!f.yLog){
        // Scaled BEFORE subtracting, not after. (yHi - yLo) * 0.05 overflows to
        // infinity on a column holding both 1e308 and -1e308, and then yLo and
        // yHi become -inf and +inf - so two perfectly finite bounds were turned
        // into infinities by the act of padding them, every coordinate came out
        // NaN, and Qt drew markers from numbers that were not numbers.
        // yHi*0.05 - yLo*0.05 is the same value wherever both forms are
        // representable, and stays representable where the other does not.
        const double pad=yHi*0.05-yLo*0.05;
        if(std::isfinite(pad)){ yLo-=pad; yHi+=pad; }
    }

    // An inverted axis is the same range, mapped the other way round. Doing it
    // by swapping the bounds means every engine, every tick and every export
    // gets it for free: toDevice divides by (hi - lo), which is simply negative
    // now, and nothing else has to know.
    if(spec.xAxis.inverted) std::swap(xLo,xHi);
    if(spec.yAxis.inverted) std::swap(yLo,yHi);

    // Last line of defence. Everything above is meant to keep the bounds
    // finite, and every future addition to it is another chance to lose that -
    // padding, a zero baseline, an explicit limit read from a saved figure.
    // A non-finite bound is not a slightly wrong picture, it is NaN in every
    // coordinate and nothing drawn, so it is caught here rather than trusted.
    if(!finite(xLo)||!finite(xHi)){ xLo=0; xHi=1; }
    if(!finite(yLo)||!finite(yHi)){ yLo=0; yHi=1; }

    f.xLo=xLo; f.xHi=xHi; f.yLo=yLo; f.yHi=yHi;
    return f;
}

// What the canvas asks. prepareSpec first, because for a histogram or a violin
// the drawn range belongs to the rewritten geometry rather than to the mapped
// column - a histogram of values 0 to 1 is drawn against counts.
QtPlotBackend::DataRange QtPlotBackend::rangeFor(const PlotSpec& spec) const {
    const Frame f=computeRange(preparedCached(spec));
    return DataRange{f.xLo,f.xHi,f.yLo,f.yHi,f.xLog,f.yLog};
}

QtPlotBackend::Frame QtPlotBackend::computeFrame(QPainter* p,const QRectF& target,const PlotSpec& spec,
                                                 QVector<AxisTick>& xTicks,QVector<AxisTick>& yTicks) const {
    Frame f=computeRange(spec);
    const double xLo=f.xLo,xHi=f.xHi,yLo=f.yLo,yHi=f.yHi;

    // Ticks are chosen over the range in its natural order; which end of the
    // picture each one lands on is toDevice's business.
    const double xFrom=qMin(xLo,xHi),xTo=qMax(xLo,xHi);
    const double yFrom=qMin(yLo,yHi),yTo=qMax(yLo,yHi);
    xTicks=f.xLog?logTicks(xFrom,xTo):linearTicks(xFrom,xTo,7);
    yTicks=f.yLog?logTicks(yFrom,yTo):linearTicks(yFrom,yTo,6);

    // Widen the left margin so the longest y label always fits.
    const QFontMetricsF fm(font(spec,spec.style.tickSize),p->device());
    double widest=0;
    for(const AxisTick& t:yTicks) if(!t.minor) widest=qMax(widest,fm.horizontalAdvance(t.label));
    const double left=qMax(kMarginLeft,widest+kTickLen+14.0+fm.height());

    f.plotArea=QRectF(target.left()+left,target.top()+kMarginTop,
                      qMax(10.0,target.width()-left-kMarginRight),
                      qMax(10.0,target.height()-kMarginTop-kMarginBottom));
    lastPlotArea_=f.plotArea;
    return f;
}

QPointF QtPlotBackend::toDevice(const Frame& f,double x,double y) const {
    if(f.xLog) x=(x>0)?std::log10(x):f.xLo;
    if(f.yLog) y=(y>0)?std::log10(y):f.yLo;
    // Where a value sits in [lo, hi], as a fraction. Two things have to be got
    // right, and both were found by looking at what was drawn rather than by
    // reading this code:
    //
    // The span is SIGNED. An inverted axis has hi below lo, and clamping the
    // span up to a positive floor - which qMax(1e-300, span) does - turns
    // every coordinate into an infinity and draws nothing at all. Only a span
    // of exactly zero needs the floor.
    //
    // The span can also OVERFLOW. A column holding both 1e308 and -1e308 has a
    // range wider than a double can represent: hi - lo is +inf, (v - lo) is
    // +inf as well, and inf/inf is NaN - so every coordinate became NaN and Qt
    // drew shapes out of numbers that were not numbers ("QPainterPath::arcTo:
    // Adding arc where a parameter is NaN", once per marker). Halving all
    // three is an exact fix rather than an approximate one: division by two is
    // exact in binary floating point, and the ratio (v-lo)/(hi-lo) is
    // unchanged by scaling v, lo and hi together.
    const auto fraction=[](double v,double lo,double hi){
        double span=hi-lo;
        for(int halvings=0;!std::isfinite(span)&&halvings<8;++halvings){
            v*=0.5; lo*=0.5; hi*=0.5;
            span=hi-lo;
        }
        return (v-lo)/((std::abs(span)>1e-300)?span:1e-300);
    };
    const double tx=fraction(x,f.xLo,f.xHi);
    const double ty=fraction(y,f.yLo,f.yHi);
    return QPointF(f.plotArea.left()+tx*f.plotArea.width(),
                   f.plotArea.bottom()-ty*f.plotArea.height());
}

void QtPlotBackend::drawChrome(QPainter* p,const Frame& f,const PlotSpec& spec,
                               const QVector<AxisTick>& xTicks,const QVector<AxisTick>& yTicks) const {
    p->save();
    const QColor fg=spec.style.foreground;

    if(spec.style.gridVisible){
        QPen grid(spec.style.gridColor); grid.setWidthF(0.6); p->setPen(grid);
        for(const AxisTick& t:xTicks){ if(t.minor) continue;
            const double dx=toDevice(f,f.xLog?std::pow(10.0,t.value):t.value,f.yLo).x();
            p->drawLine(QPointF(dx,f.plotArea.top()),QPointF(dx,f.plotArea.bottom())); }
        for(const AxisTick& t:yTicks){ if(t.minor) continue;
            const double dy=toDevice(f,f.xLo,f.yLog?std::pow(10.0,t.value):t.value).y();
            p->drawLine(QPointF(f.plotArea.left(),dy),QPointF(f.plotArea.right(),dy)); }
    }

    QPen axis(fg); axis.setWidthF(qMax(0.5,spec.style.lineWidth*0.8)); p->setPen(axis);
    p->drawRect(f.plotArea);

    const QFont tickFont=font(spec,spec.style.tickSize);
    const QFontMetricsF fm(tickFont,p->device());
    p->setFont(tickFont);

    for(const AxisTick& t:xTicks){
        const double dx=toDevice(f,f.xLog?std::pow(10.0,t.value):t.value,f.yLo).x();
        const double len=t.minor?kTickLen*0.5:kTickLen;
        p->drawLine(QPointF(dx,f.plotArea.bottom()),QPointF(dx,f.plotArea.bottom()+len));
        if(t.minor||t.label.isEmpty()) continue;
        p->drawText(QRectF(dx-45,f.plotArea.bottom()+len+2,90,fm.height()),
                    Qt::AlignHCenter|Qt::AlignTop,t.label);
    }
    for(const AxisTick& t:yTicks){
        const double dy=toDevice(f,f.xLo,f.yLog?std::pow(10.0,t.value):t.value).y();
        const double len=t.minor?kTickLen*0.5:kTickLen;
        p->drawLine(QPointF(f.plotArea.left()-len,dy),QPointF(f.plotArea.left(),dy));
        if(t.minor||t.label.isEmpty()) continue;
        p->drawText(QRectF(f.plotArea.left()-len-8-200,dy-fm.height()/2,200,fm.height()),
                    Qt::AlignRight|Qt::AlignVCenter,t.label);
    }

    p->setFont(font(spec,spec.style.axisLabelSize));
    if(!spec.xAxis.label.isEmpty())
        p->drawText(QRectF(f.plotArea.left(),f.plotArea.bottom()+kTickLen+fm.height()+6,
                           f.plotArea.width(),fm.height()+6),
                    Qt::AlignHCenter|Qt::AlignTop,spec.xAxis.label);
    if(!spec.yAxis.label.isEmpty()){
        p->save();
        p->translate(f.plotArea.left()-kMarginLeft+12,f.plotArea.center().y());
        p->rotate(-90);
        p->drawText(QRectF(-f.plotArea.height()/2,-fm.height(),f.plotArea.height(),fm.height()*1.6),
                    Qt::AlignHCenter|Qt::AlignVCenter,spec.yAxis.label);
        p->restore();
    }
    if(!spec.title.isEmpty()){
        p->setFont(font(spec,spec.style.titleSize));
        const QFontMetricsF tfm(p->font(),p->device());
        p->drawText(QRectF(f.plotArea.left(),f.plotArea.top()-tfm.height()-8,
                           f.plotArea.width(),tfm.height()+4),
                    Qt::AlignHCenter|Qt::AlignVCenter,spec.title);
    }
    p->restore();
}

void QtPlotBackend::drawLegend(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    if(!spec.legendVisible||spec.series.size()<2) return;
    p->save();
    const QFont lf=font(spec,spec.style.legendSize);
    p->setFont(lf);
    const QFontMetricsF fm(lf,p->device());
    double widest=0;
    for(const PlotSeries& s:spec.series) widest=qMax(widest,fm.horizontalAdvance(s.label));
    const double rowH=fm.height()+3, boxW=widest+34, boxH=rowH*spec.series.size()+8;
    const QRectF box(f.plotArea.right()-boxW-8,f.plotArea.top()+8,boxW,boxH);
    QColor panel=spec.style.background; panel.setAlpha(215);
    p->setBrush(panel);
    p->setPen(QPen(spec.style.gridColor,0.6));
    p->drawRoundedRect(box,4,4);
    double y=box.top()+4;
    for(const PlotSeries& s:spec.series){
        QPen swatch(s.color); swatch.setWidthF(qMax(1.0,s.lineWidth)); applySeriesDash(swatch,s); p->setPen(swatch);
        p->drawLine(QPointF(box.left()+7,y+rowH/2),QPointF(box.left()+25,y+rowH/2));
        p->setPen(spec.style.foreground);
        p->drawText(QRectF(box.left()+30,y,widest+4,rowH),Qt::AlignLeft|Qt::AlignVCenter,s.label);
        y+=rowH;
    }
    p->restore();
}

void QtPlotBackend::drawLineChart(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    for(const PlotSeries& s:spec.series){
        const int n=qMin(s.x.size(),s.y.size());
        if(n==0) continue;
        QPainterPath path; bool started=false;
        for(int i=0;i<n;++i){
            const double x=s.x[i],y=s.y[i];
            if(!finite(x)||!finite(y)||(f.xLog&&x<=0)||(f.yLog&&y<=0)){ started=false; continue; }
            const QPointF pt=toDevice(f,x,y);
            if(!started){ path.moveTo(pt); started=true; } else path.lineTo(pt);
        }
        p->save();
        p->setOpacity(qBound(0.0,s.opacity,1.0));
        if(s.drawLine){
            QPen pen(s.color); pen.setWidthF(qMax(0.2,s.lineWidth)); applySeriesDash(pen,s);
            pen.setJoinStyle(Qt::RoundJoin); pen.setCapStyle(Qt::RoundCap);
            p->setPen(pen); p->setBrush(Qt::NoBrush); p->drawPath(path);
        }
        if(s.drawMarkers){
            p->setPen(Qt::NoPen); p->setBrush(s.color);
            drawSeriesMarkers(p,f,s);
        }
        p->restore();
    }
}

void QtPlotBackend::drawScatter(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    for(const PlotSeries& s:spec.series){
        p->save();
        p->setOpacity(qBound(0.0,s.opacity,1.0));
        // Not guarded on s.drawMarkers: for this engine the markers are the
        // plot, whatever the point budget decided about the line style.
        p->setPen(Qt::NoPen); p->setBrush(s.color);
        drawSeriesMarkers(p,f,s);
        p->restore();
    }
}

void QtPlotBackend::drawArea(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    const double baseline=f.yLog?f.yLo:qBound(f.yLo,0.0,f.yHi);
    for(const PlotSeries& s:spec.series){
        const int n=qMin(s.x.size(),s.y.size());
        if(n<2) continue;
        QPainterPath path; bool started=false; double firstX=0,lastX=0;
        for(int i=0;i<n;++i){
            const double x=s.x[i],y=s.y[i];
            if(!finite(x)||!finite(y)||(f.xLog&&x<=0)||(f.yLog&&y<=0)) continue;
            const QPointF pt=toDevice(f,x,y);
            if(!started){ path.moveTo(pt); firstX=pt.x(); started=true; } else path.lineTo(pt);
            lastX=pt.x();
        }
        if(!started) continue;
        const double baseY=f.plotArea.bottom()-((baseline-f.yLo)/qMax(1e-300,f.yHi-f.yLo))*f.plotArea.height();
        path.lineTo(QPointF(lastX,baseY));
        path.lineTo(QPointF(firstX,baseY));
        path.closeSubpath();
        QColor fill=s.color; fill.setAlphaF(0.35);
        p->save();
        // In Monochrome colour-vision mode the dash pattern is the only thing
        // separating one series from another, so a draw function that drops it
        // renders every series as the same grey outline.
        QPen pen(s.color,qMax(0.2,s.lineWidth)); applySeriesDash(pen,s);
        p->setPen(pen);
        p->setBrush(fill);
        p->drawPath(path);
        p->restore();
    }
}

void QtPlotBackend::drawStairs(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    for(const PlotSeries& s:spec.series){
        const int n=qMin(s.x.size(),s.y.size());
        if(n<2) continue;
        QPainterPath path; bool started=false; QPointF prev;
        for(int i=0;i<n;++i){
            const double x=s.x[i],y=s.y[i];
            if(!finite(x)||!finite(y)||(f.xLog&&x<=0)||(f.yLog&&y<=0)){ started=false; continue; }
            const QPointF pt=toDevice(f,x,y);
            if(!started){ path.moveTo(pt); started=true; }
            else { path.lineTo(QPointF(pt.x(),prev.y())); path.lineTo(pt); }
            prev=pt;
        }
        QPen pen(s.color); pen.setWidthF(qMax(0.2,s.lineWidth)); pen.setJoinStyle(Qt::MiterJoin); applySeriesDash(pen,s);
        p->save(); p->setPen(pen); p->setBrush(Qt::NoBrush); p->drawPath(path); p->restore();
    }
}

void QtPlotBackend::drawFloatingTitle(QPainter* p,const QRectF& target,
                                      const PlotSpec& spec) const {
    if(spec.title.isEmpty()) return;
    p->setPen(spec.style.foreground);
    p->setFont(font(spec,spec.style.titleSize));
    p->drawText(QRectF(target.left(),target.top()+6,target.width(),26),
                Qt::AlignHCenter|Qt::AlignTop,spec.title);
}

double QtPlotBackend::slotWidthFrom(QVector<double> centres,double fallback,double limit){
    if(centres.isEmpty()) return qBound(1.0,fallback,qMax(1.0,limit));
    std::sort(centres.begin(),centres.end());
    double minGap=std::numeric_limits<double>::infinity();
    for(int i=1;i<centres.size();++i){
        const double d=centres[i]-centres[i-1];
        // Sub-pixel gaps are ignored: duplicated slots are a grouped category,
        // not a narrower bar.
        if(d>0.5) minGap=qMin(minGap,d);
    }
    const double slot=finite(minGap)?minGap:fallback;
    return qBound(1.0,slot,qMax(1.0,limit));
}

void QtPlotBackend::drawSeriesMarkers(QPainter* p,const Frame& f,const PlotSeries& s,
                                      double minRadius) const {
    const int n=qMin(s.x.size(),s.y.size());
    const double r=qMax(minRadius,s.markerSize/2.0);
    for(int i=0;i<n;++i){
        const double x=s.x[i],y=s.y[i];
        if(!finite(x)||!finite(y)||(f.xLog&&x<=0)||(f.yLog&&y<=0)) continue;
        p->drawEllipse(toDevice(f,x,y),r,r);
    }
}

void QtPlotBackend::drawBar(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    int total=0;
    for(const PlotSeries& s:spec.series) total=qMax(total,qMin(s.x.size(),s.y.size()));
    if(total<=0) return;
    const int groups=qMax(1,spec.series.size());

    // A bar belongs at its own x value, not at its position in the array. The
    // axis underneath is scaled to the data, so spacing bars evenly draws them
    // under labels they do not correspond to - and for a histogram, whose bin
    // centres are the entire content of the x column, every bar lands wrong.
    QVector<double> centres;
    centres.reserve(total*groups);
    for(const PlotSeries& s:spec.series){
        const int n=qMin(s.x.size(),s.y.size());
        for(int i=0;i<n;++i){
            const double x=s.x[i];
            if(!finite(x)||(f.xLog&&x<=0)) continue;
            centres.push_back(toDevice(f,x,f.yLo).x());
        }
    }
    if(centres.isEmpty()) return;

    const double slot=slotWidthFrom(centres,f.plotArea.width()/double(total),f.plotArea.width());
    const double barW=qMax(1.0,(slot*0.78)/double(groups));
    const double baseY=f.plotArea.bottom()-((qBound(f.yLo,0.0,f.yHi)-f.yLo)/qMax(1e-300,f.yHi-f.yLo))*f.plotArea.height();

    for(int g=0;g<spec.series.size();++g){
        const PlotSeries& s=spec.series[g];
        const int n=qMin(s.x.size(),s.y.size());
        p->save(); p->setPen(Qt::NoPen); p->setBrush(s.color);
        p->setOpacity(qBound(0.0,s.opacity,1.0));
        for(int i=0;i<n;++i){
            const double x=s.x[i],y=s.y[i];
            if(!finite(x)||!finite(y)||(f.xLog&&x<=0)||(f.yLog&&y<=0)) continue;
            const QPointF pt=toDevice(f,x,y);
            const double left=pt.x()-(groups*barW)/2.0+g*barW;
            p->drawRect(QRectF(left,qMin(pt.y(),baseY),barW,std::abs(baseY-pt.y())));
        }
        p->restore();
    }
}


// ---------------------------------------------------------------------------
// Statistical engines.
//
// Every engine below is expressed as a rewrite in prepareSpec rather than as a
// new draw function, because each one is a transformation of the data followed
// by geometry that already exists. An ECDF is a staircase; a Q-Q plot is a
// scatter with a reference line; a control chart is a line with three rules
// across it. Retargeting spec.engine after the rewrite means render() dispatches
// to drawing code that is already tested, and the only new thing here is
// arithmetic.
//
// The alternative - a bespoke draw function per catalogue entry - is how a
// plotting library ends up with forty subtly different ways to draw a line.
namespace {

QVector<double> finiteValues(const PlotSeries& s){
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

Moments2 momentsOf(const QVector<double>& a,const QVector<double>& b){
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
double pearson(const Moments2& m){
    if(m.n<2) return 0.0;
    const double denom=std::sqrt(m.sxx*m.syy);
    return (denom>1e-15)? m.sxy/denom : 0.0;
}

// Sample covariance, normalised by n-1 to match pearson()'s inputs.
double covarianceOf(const Moments2& m){
    if(m.n<2) return 0.0;
    return m.sxy/double(m.n-1);
}

double quantileOf(const QVector<double>& sorted,double q){
    if(sorted.isEmpty()) return 0.0;
    const double pos=q*(sorted.size()-1);
    const int i=int(pos);
    const double frac=pos-i;
    return (i+1<sorted.size())? sorted[i]*(1.0-frac)+sorted[i+1]*frac : sorted[i];
}

double meanOf(const QVector<double>& v){
    if(v.isEmpty()) return 0.0;
    double sum=0.0; for(double x:v) sum+=x;
    return sum/double(v.size());
}

double stdevOf(const QVector<double>& v){
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

KdeCurve kdeCurve(const QVector<double>& sorted,int grid,double padBandwidths){
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
double normalQuantile(double p){
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

// The span of one column.
//
// This existed twice under two names - Bounds and Bounds - with two builders,
// boundsOf and boundsFor. They measure the same thing and differ only in what
// they map it onto: a radar chart wants [0,1] clamped, a 3-D projection wants
// [-0.5,+0.5] unclamped. Both normalisations live here now.
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

Bounds boundsOf(const QVector<double>& v){
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
PlotSeries horizontalRule(double y,double xLo,double xHi,const QColor& colour,
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

} // namespace

// ---------------------------------------------------------------------------
// Field engines.
//
// A heatmap and a contour both read a value over a 2-D domain, and both get it
// here the same way: series[0] is x, series[1] is y, series[2] is the value,
// and the points are binned onto a regular grid. Requiring a pre-gridded matrix
// would be the easier implementation and the less useful one - measurements
// arrive as scattered triples far more often than as a rectangle.
// ValueGrid and GridAggregate are declared in the class so a built grid can be
// cached across repaints; these two are therefore members rather than local
// helpers. Everything below them is still file-local.
QtPlotBackend::ValueGrid QtPlotBackend::gridFromSeries(const PlotSpec& spec,int requested,
                                                       GridAggregate how){
    ValueGrid g;
    if(spec.series.size()<3) return g;
    const QVector<double>& xs=spec.series.at(0).y;
    const QVector<double>& ys=spec.series.at(1).y;
    const QVector<double>& vs=spec.series.at(2).y;
    const int n=qMin(xs.size(),qMin(ys.size(),vs.size()));
    if(n<4) return g;

    double xLo=std::numeric_limits<double>::infinity(),xHi=-xLo;
    double yLo=xLo,yHi=-xLo;
    for(int i=0;i<n;++i){
        if(!finite(xs[i])||!finite(ys[i])||!finite(vs[i])) continue;
        xLo=qMin(xLo,xs[i]); xHi=qMax(xHi,xs[i]);
        yLo=qMin(yLo,ys[i]); yHi=qMax(yHi,ys[i]);
    }
    if(!finite(xLo)||!finite(yLo)||!(xHi>xLo)||!(yHi>yLo)) return g;

    // Grid resolution follows the sample count: enough cells to show structure,
    // few enough that most of them get a sample. sqrt(n)/2 is the usual
    // compromise and behaves for anything from a hundred points to a million.
    const int side=qBound(12,requested>0?requested:int(std::sqrt(double(n))/2.0),160);
    g.nx=side; g.ny=side;
    g.xLo=xLo; g.xHi=xHi; g.yLo=yLo; g.yHi=yHi;
    g.cells.fill(std::numeric_limits<double>::quiet_NaN(),side*side);
    QVector<int> counts(side*side,0);
    QVector<double> sums(side*side,0.0);
    for(int i=0;i<n;++i){
        if(!finite(xs[i])||!finite(ys[i])||!finite(vs[i])) continue;
        const int cx=qBound(0,int((xs[i]-xLo)/(xHi-xLo)*side),side-1);
        const int cy=qBound(0,int((ys[i]-yLo)/(yHi-yLo)*side),side-1);
        const int k=cy*side+cx;
        sums[k]+=vs[i]; counts[k]+=1;
    }
    double vLo=std::numeric_limits<double>::infinity(),vHi=-vLo;
    for(int k=0;k<side*side;++k){
        if(counts[k]==0){
            // For a density the absence of samples IS zero; for a measured
            // field it is unknown, and colouring it would invent data.
            if(how==GridAggregate::Count){ g.cells[k]=0.0; vLo=qMin(vLo,0.0); vHi=qMax(vHi,0.0); }
            continue;
        }
        g.cells[k]=(how==GridAggregate::Count)?double(counts[k]):sums[k]/double(counts[k]);
        vLo=qMin(vLo,g.cells[k]); vHi=qMax(vHi,g.cells[k]);
    }
    if(!finite(vLo)||!finite(vHi)) return g;
    if(qFuzzyCompare(vLo,vHi)) vHi=vLo+1.0;
    g.vLo=vLo; g.vHi=vHi;
    g.valid=true;
    return g;
}

namespace {
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
using ColourMapKind = const unsigned char (*)[3];

ColourMapKind colourMapFor(const QString& name){
    return colourmaps::tableFor(name);
}

inline QColor colourMap(ColourMapKind table,double t){
    return colourmaps::sample(table,t);
}
} // namespace

// Fills unknown cells from their finite neighbours, a few passes outward.
// Marching squares needs all four corners of a cell, so a grid built from
// scattered measurements has to be closed before it can be contoured. This is
// nearest-neighbour smoothing, not interpolation with any claim to accuracy -
// it exists so the contour follows the data that IS there rather than vanishing.
void QtPlotBackend::closeGaps(ValueGrid& g,int passes){
    for(int pass=0;pass<passes;++pass){
        QVector<double> next=g.cells;
        bool changed=false;
        for(int cy=0;cy<g.ny;++cy){
            for(int cx=0;cx<g.nx;++cx){
                const int k=cy*g.nx+cx;
                if(finite(g.cells[k])) continue;
                double sum=0.0; int used=0;
                for(int dy=-1;dy<=1;++dy){
                    for(int dx=-1;dx<=1;++dx){
                        const int nx=cx+dx, ny=cy+dy;
                        if(nx<0||ny<0||nx>=g.nx||ny>=g.ny) continue;
                        const double v=g.cells[ny*g.nx+nx];
                        if(finite(v)){ sum+=v; ++used; }
                    }
                }
                if(used>0){ next[k]=sum/double(used); changed=true; }
            }
        }
        g.cells=next;
        if(!changed) break;
    }
}

const QtPlotBackend::ValueGrid& QtPlotBackend::cachedGrid(const PlotSpec& gridInput,
                                                          GridAggregate how,
                                                          int gapPasses,int slot) const {
    GridCacheEntry& entry=gridCache_[qBound(0,slot,2)];
    const quint64 hash=specFingerprint(gridInput);
    if(entry.valid&&entry.hash==hash
       &&entry.aggregate==int(how)&&entry.gapPasses==gapPasses)
        return entry.grid;

    entry.grid=gridFromSeries(gridInput,0,how);
    if(gapPasses>0&&entry.grid.valid) closeGaps(entry.grid,gapPasses);
    entry.hash=hash;
    entry.aggregate=int(how);
    entry.gapPasses=gapPasses;
    entry.valid=true;
    return entry.grid;
}

// The list is kept beside the dispatch above rather than in QML, so a new field
// engine cannot be added to one and forgotten in the other. Every name here
// reaches drawHeatmap, drawContour, drawVectorField, draw3D or draw3DField in
// render() - and every branch of render() that reaches one of those is here.
bool QtPlotBackend::usesColourMap(const QString& preparedEngine){
    static const QSet<QString> kFieldEngines{
        // Flat fields.
        QStringLiteral("2D Heatmap"),QStringLiteral("2D Histogram"),
        QStringLiteral("Hexbin Density"),QStringLiteral("Correlation Matrix"),
        QStringLiteral("Covariance Matrix"),QStringLiteral("Spy Matrix"),
        QStringLiteral("2D Contour"),
        // Vector fields.
        QStringLiteral("Quiver Field"),QStringLiteral("Feather"),
        QStringLiteral("Stream Field"),QStringLiteral("Stream Particles"),
        QStringLiteral("Phase Portrait"),QStringLiteral("Flow Texture (LIC)"),
        QStringLiteral("Divergence Map"),QStringLiteral("Vorticity Map"),
        // Surfaces and volumes.
        QStringLiteral("Surface + Contours"),QStringLiteral("Comet 3D"),
        QStringLiteral("Ribbon"),QStringLiteral("Stream Ribbon"),
        QStringLiteral("Tensor Glyph Field"),QStringLiteral("Volume Show"),
        QStringLiteral("Volume Slice"),QStringLiteral("Isosurface"),
        QStringLiteral("Isonormals"),QStringLiteral("Isocaps"),
        QStringLiteral("Contour Slice")};
    // Everything drawn by draw3D, which is selected by prefix rather than by
    // name - "3D Surface", "3D Scatter" and the rest.
    if(preparedEngine.startsWith(QLatin1String("3D "))) return true;
    return kFieldEngines.contains(preparedEngine);
}

// ---------------------------------------------------------------------------
// Annotations.
//
// Drawn last, over everything including the legend, because a note is about
// the figure rather than part of it.
//
// Real text, not a path: this goes through the same painter as the screen, so
// an exported PDF carries a selectable, searchable string in an embedded font
// rather than an outline. The whole point of the vector export is that the
// words in it are words.
//
// Positions are DATA coordinates and offsets are typographic points, so the
// note follows its point through a zoom and keeps its distance from it at any
// figure size - a note placed at 900x650 and exported at 89 mm must not end up
// half a plot away from the thing it is pointing at.
void QtPlotBackend::drawAnnotations(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    if(spec.annotations.isEmpty()) return;
    p->save();
    // Clipped to the plot area. A note whose anchor has been zoomed out of view
    // must not reappear among the axis labels, which is where an unclipped one
    // ends up as soon as the offset carries it past the frame.
    p->setClipRect(f.plotArea.adjusted(-1,-1,1,1));
    const QFont base=font(spec,spec.style.baseFontSize);
    QFontMetricsF metrics(base);
    p->setFont(base);
    // Points to device pixels, the same conversion the rest of the typography
    // uses, so an offset given in points is the same physical distance as a
    // font size given in points.
    const double scale=double(qMax(1,spec.style.dpi))/72.0;

    for(const PlotAnnotation& note:spec.annotations){
        if(note.text.isEmpty()) continue;
        if(!finite(note.x)||!finite(note.y)) continue;
        if(f.xLog&&note.x<=0) continue;
        if(f.yLog&&note.y<=0) continue;
        const QPointF anchor=toDevice(f,note.x,note.y);
        if(!finite(anchor.x())||!finite(anchor.y())) continue;
        // The ANCHOR decides whether the note exists at all. A note about a
        // point that has been zoomed or panned out of view is not about
        // anything on screen, and the nudge below would otherwise drag it back
        // into the frame and draw it beside data it has nothing to do with.
        // The clip alone is not enough: it removes the label but leaves the
        // leader line, and the nudge would keep the label too.
        if(!f.plotArea.contains(anchor)) continue;
        const QPointF at=anchor+QPointF(note.offsetX*scale,note.offsetY*scale);

        const QColor ink=note.color.isValid()?note.color:spec.style.foreground;
        QRectF text=metrics.boundingRect(note.text).translated(at);
        QRectF plate=text.adjusted(-4,-2,4,2);

        // Pushed back inside the frame if the offset carried it out.
        //
        // The clip above is for a note whose ANCHOR has been zoomed away, which
        // should vanish. This is the different case: the anchor is on the plot
        // and only the label overhangs, and clipping that leaves a leader line
        // pointing at nothing and a note nobody can read. A default offset near
        // an edge does it immediately - the very first test figure had a note
        // at the bottom of the range whose text landed under the x axis.
        // qBound(lo, 0, hi) is not safe here: when the label is WIDER than the
        // plot area - a long note on a narrow figure, or a paragraph pasted
        // into one - lo exceeds hi and qBound asserts. Pinning to the leading
        // edge instead keeps the beginning of the text readable, which is the
        // half worth having when the whole of it cannot fit.
        const auto slide=[](double lo,double hi){
            if(lo>hi) return lo;              // too big to fit: show the start
            return qBound(lo,0.0,hi);
        };
        const QPointF nudge(slide(f.plotArea.left()+2-plate.left(),
                                  f.plotArea.right()-2-plate.right()),
                            slide(f.plotArea.top()+2-plate.top(),
                                  f.plotArea.bottom()-2-plate.bottom()));
        if(!nudge.isNull()){
            text.translate(nudge);
            plate.translate(nudge);
        }

        // A backing plate, because a note over a dense plot is unreadable
        // otherwise, and because "put it somewhere empty" is not available when
        // the note has to sit next to the point it is about.
        QColor behind=spec.style.background;
        behind.setAlphaF(0.78f);
        p->setPen(Qt::NoPen);
        p->setBrush(behind);
        p->drawRoundedRect(plate,3,3);

        if(note.leader){
            // From the edge of the plate nearest the anchor, not from the text
            // itself, so the line does not run underneath its own label.
            const QPointF from(qBound(plate.left(),anchor.x(),plate.right()),
                               qBound(plate.top(),anchor.y(),plate.bottom()));
            QPen leader(ink);
            leader.setWidthF(qMax(0.5,spec.style.lineWidth*0.6));
            p->setPen(leader);
            p->setBrush(Qt::NoBrush);
            p->drawLine(from,anchor);
            // A dot on the anchor: without it the line ends in mid-air and it
            // is not clear which point of several the note is about.
            p->setPen(Qt::NoPen);
            p->setBrush(ink);
            p->drawEllipse(anchor,qMax(1.2,spec.style.lineWidth),qMax(1.2,spec.style.lineWidth));
        }

        p->setPen(ink);
        p->setBrush(Qt::NoBrush);
        p->drawText(at+nudge,note.text);
    }
    p->restore();
}

// The mapped-column requirement each family actually enforces, kept beside the
// code that enforces it. Every number here is the guard in the corresponding
// painter or grid builder, not a guess at what the engine "should" need.
// The tables here are the guards in the painters and grid builders, read out
// of that code rather than guessed at. Anything not listed takes x plus one y,
// which is every ordinary series engine.
int QtPlotBackend::columnsRequired(const QString& engine){
    static const QSet<QString> kThree{
        // gridFromSeries: x, y, value.
        QStringLiteral("2D Heatmap"),QStringLiteral("2D Contour"),
        QStringLiteral("2D Histogram"),QStringLiteral("Hexbin Density"),
        // Edge lists: from, to, weight.
        QStringLiteral("Network Graph"),QStringLiteral("Chord Diagram"),
        // Contingency and set membership.
        QStringLiteral("Mosaic Plot"),QStringLiteral("UpSet Plot"),
        // Three corners.
        QStringLiteral("Ternary Scatter"),QStringLiteral("Piper Diagram"),
        // Surfaces and 3-D: x, y, z.
        QStringLiteral("Surface + Contours"),QStringLiteral("Comet 3D"),
        QStringLiteral("Ribbon")};
    // NOT here, and each was tried and removed after reading what the engine
    // does with its input:
    //   Spectrogram      takes ONE signal column and PRODUCES three series.
    //                    Demanding three would have made it read the x column
    //                    as the signal.
    //   4D / 5D Scatter  is an ordinary series engine - drawScatter iterates
    //                    spec.series and takes x from the frame. Its extra
    //                    dimensions are marker size and colour, not columns.
    // Both drew correctly before this function existed, and listing them here
    // would have broken them in the name of fixing something else.
    static const QSet<QString> kFour{
        // x, y and the two vector components.
        QStringLiteral("Quiver Field"),QStringLiteral("Feather"),
        QStringLiteral("Stream Field"),QStringLiteral("Stream Particles"),
        QStringLiteral("Phase Portrait"),QStringLiteral("Flow Texture (LIC)"),
        QStringLiteral("Divergence Map"),QStringLiteral("Vorticity Map"),
        // Origin and destination are two coordinate PAIRS.
        QStringLiteral("Origin-Destination Flow")};
    if(kFour.contains(engine)) return 4;
    if(kThree.contains(engine)) return 3;
    if(engine.startsWith(QLatin1String("3D "))) return 3;
    return 2;
}

QString QtPlotBackend::explainEmpty(const PlotSpec& chosen,const PlotSpec& prepared){
    const QString e=chosen.engine;
    // How many columns the person actually mapped, which is what the
    // requirements below are about. The prepared spec is used only to decide
    // whether anything came out.
    const int n=int(chosen.series.size());

    // How many it needed comes from columnsRequired(), the one place that
    // knows; the sets below only choose the WORDING, because "x, y and the
    // value it colours by" and "x, y and z" are the same requirement described
    // to different people. Deriving the number here as well is how the message
    // and the mapping drift apart.
    const int needed=columnsRequired(e);

    // gridFromSeries: series 0 is x, 1 is y, 2 is the value.
    static const QSet<QString> kGridEngines{
        QStringLiteral("2D Heatmap"),QStringLiteral("2D Contour"),
        QStringLiteral("2D Histogram"),QStringLiteral("Hexbin Density")};
    if(kGridEngines.contains(e)&&n<needed){
        return QStringLiteral(
            "%1 needs three mapped columns - x, y and the value it colours by - "
            "and %2 %3 mapped. The axes come from the data, which is why the "
            "frame is drawn and the plot area is empty.")
            .arg(e).arg(n).arg(n==1?QStringLiteral("is"):QStringLiteral("are"));
    }

    // Vector fields read two more columns for the components.
    static const QSet<QString> kVectorEngines{
        QStringLiteral("Quiver Field"),QStringLiteral("Feather"),
        QStringLiteral("Stream Field"),QStringLiteral("Stream Particles"),
        QStringLiteral("Phase Portrait"),QStringLiteral("Flow Texture (LIC)"),
        QStringLiteral("Divergence Map"),QStringLiteral("Vorticity Map")};
    if(kVectorEngines.contains(e)&&n<needed){
        return QStringLiteral(
            "%1 needs four mapped columns - x, y and the two vector components - "
            "and %2 mapped.").arg(e).arg(n);
    }

    if(e.startsWith(QLatin1String("3D "))
       ||e==QLatin1String("Surface + Contours")
       ||e==QLatin1String("Ribbon")){
        if(n<needed) return QStringLiteral(
            "%1 needs three mapped columns - x, y and z - and %2 mapped.").arg(e).arg(n);
    }

    if(e==QLatin1String("Ternary Scatter")&&n<needed)
        return QStringLiteral("Ternary Scatter needs three mapped columns, one per corner.");

    // Anything else that wants more than it got. Without this an engine added
    // to columnsRequired but not to a wording set above says nothing at all,
    // which is the failure this function exists to prevent.
    //
    // Only for the multi-column engines, and the reason is that `n` counts
    // SERIES rather than mapped columns, and the two are the same number only
    // above three. An ordinary engine takes x from the frame and one series per
    // y column, so a perfectly mapped Line Chart has n == 1 and needed == 2 -
    // and the first version of this said "Line Chart needs 2 mapped columns and
    // 1 is mapped" over a figure drawing two hundred thousand points quite
    // happily. A warning on a working plot is worse than no warning at all.
    if(needed>=3&&n<needed&&n>0)
        return QStringLiteral("%1 needs %2 mapped columns and %3 %4 mapped.")
            .arg(e).arg(needed).arg(n)
            .arg(n==1?QStringLiteral("is"):QStringLiteral("are"));

    if(n==0)
        return QStringLiteral(
            "No column produced any drawable values. Check that the mapped "
            "columns are numeric and are not entirely blank.");

    // Columns were mapped, and the rewrite still produced nothing.
    bool anyPoints=false;
    for(const PlotSeries& s:prepared.series) if(!s.y.isEmpty()){ anyPoints=true; break; }
    if(!anyPoints)
        return QStringLiteral(
            "%1 produced no points from the mapped columns. Its own guards "
            "rejected the data - most often too few rows, or values it cannot "
            "use such as zero or negative numbers on a log axis.").arg(e);

    // DEGENERATE, which is not the same as empty and looks identical.
    //
    // A plot can draw every one of its points and still show nothing, because
    // they all landed in the same few pixels. That is what a 200,000-row
    // parameter sweep did: its x column was a five-value solver flag with one
    // stray outlier, so the axis stretched across 200,000 while every point sat
    // at one end - and 97% of the y column was within 1% of zero, so they sat
    // on the bottom axis too. The line WAS drawn, exactly on the gridline, and
    // the only honest reading of the picture was that the application was
    // broken.
    //
    // Measured on the data, not on the pixels: the renderer must not have to
    // look at its own output, and the answer has to be the same in a PDF at any
    // size as it is on screen.
    const auto spread=[](bool useX,const PlotSpec& p,QString& why){
        double lo=std::numeric_limits<double>::infinity(),hi=-lo;
        int finite=0;
        QSet<double> levels;
        for(const PlotSeries& s:p.series){
            const QVector<double>& v=useX?s.x:s.y;
            for(double d:v){
                if(!std::isfinite(d)) continue;
                ++finite;
                lo=qMin(lo,d); hi=qMax(hi,d);
                if(levels.size()<8) levels.insert(d);
            }
        }
        if(finite<8||!(hi>lo)) return false;
        // How much of the span the middle 98% of the values actually occupy.
        // One outlier can own the axis while everything else shares a pixel.
        QVector<double> all;
        all.reserve(finite);
        for(const PlotSeries& s:p.series){
            const QVector<double>& v=useX?s.x:s.y;
            for(double d:v) if(std::isfinite(d)) all.append(d);
        }
        std::sort(all.begin(),all.end());
        const double p01=all.at(int(all.size()*0.01));
        const double p99=all.at(qMin(int(all.size())-1,int(all.size()*0.99)));
        const double span=hi-lo;
        const double used=p99-p01;
        const QString axis=useX?QStringLiteral("x"):QStringLiteral("y");
        if(levels.size()<=5&&int(all.size())>200){
            why=QStringLiteral(
                "The %1 column has only %2 distinct values across %3 points, so every "
                "point is stacked onto %2 positions. It is probably a flag or a "
                "category rather than an axis - choose a measured column, or an "
                "engine that expects categories.")
                .arg(axis).arg(levels.size()).arg(all.size());
            return true;
        }
        if(used<span*0.02){
            why=QStringLiteral(
                // A single %, not %%: QString::arg is not printf and does not
                // collapse a doubled one, so "%%" reaches the user verbatim.
                // Safe because neither % here is followed by a digit.
                "99% of the %1 values lie within %2% of the axis range - one or "
                "two outliers are holding the axis open and everything else is drawn "
                "into a couple of pixels. Try a log %1 axis, or zoom, or plot the "
                "distribution instead.")
                .arg(axis).arg(100.0*used/span,0,'f',2);
            return true;
        }
        return false;
    };
    QString why;
    if(spread(true,prepared,why))  return why;
    if(spread(false,prepared,why)) return why;

    return QString();
}

void QtPlotBackend::drawHeatmap(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    // The field colour map, read once. Viridis unless the style asks for
    // another - see PlotStyle::colourMap.
    const ColourMapKind cmap=colourMapFor(spec.style.colourMap);
    const bool density=spec.engine==QLatin1String("2D Histogram")
                     ||spec.engine==QLatin1String("Hexbin Density");
    const ValueGrid& g=cachedGrid(spec,density?GridAggregate::Count:GridAggregate::Mean,0,0);
    if(!g.valid) return;
    const double cellW=f.plotArea.width()/double(g.nx);
    const double cellH=f.plotArea.height()/double(g.ny);
    p->save();
    p->setPen(Qt::NoPen);
    for(int cy=0;cy<g.ny;++cy){
        for(int cx=0;cx<g.nx;++cx){
            const double v=g.cells[cy*g.nx+cx];
            if(!finite(v)) continue;     // leave the background showing through
            const double t=(v-g.vLo)/(g.vHi-g.vLo);
            p->setBrush(colourMap(cmap,t));
            // Half a pixel of overlap, or antialiasing leaves seams between
            // cells that read as a grid pattern in the data.
            p->drawRect(QRectF(f.plotArea.left()+cx*cellW,
                               f.plotArea.bottom()-(cy+1)*cellH,
                               cellW+0.5,cellH+0.5));
        }
    }
    p->restore();
}

void QtPlotBackend::drawContour(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    // The field colour map, read once. Viridis unless the style asks for
    // another - see PlotStyle::colourMap.
    const ColourMapKind cmap=colourMapFor(spec.style.colourMap);
    // Auto resolution, same as the heatmap: a grid fine enough to show
    // structure and coarse enough that its cells actually contain samples.
    const ValueGrid& g=cachedGrid(spec,GridAggregate::Mean,3,0);
    if(!g.valid) return;

    // Marching squares, the two-segment cases only. Saddle cells are drawn as
    // both crossings, which is the standard resolution and cannot mislead at
    // the level a contour plot is read.
    const double cellW=f.plotArea.width()/double(g.nx-1);
    const double cellH=f.plotArea.height()/double(g.ny-1);
    auto at=[&](int cx,int cy){ return g.cells[qBound(0,cy,g.ny-1)*g.nx+qBound(0,cx,g.nx-1)]; };
    auto pointFor=[&](double cx,double cy){
        return QPointF(f.plotArea.left()+cx*cellW,f.plotArea.bottom()-cy*cellH);
    };

    constexpr int kLevels=10;
    p->save();
    for(int level=1;level<=kLevels;++level){
        const double frac=double(level)/double(kLevels+1);
        const double iso=g.vLo+(g.vHi-g.vLo)*frac;
        QPen pen(colourMap(cmap,frac));
        pen.setWidthF(qMax(0.6,spec.style.lineWidth));
        p->setPen(pen);
        for(int cy=0;cy<g.ny-1;++cy){
            for(int cx=0;cx<g.nx-1;++cx){
                const double v00=at(cx,cy), v10=at(cx+1,cy), v11=at(cx+1,cy+1), v01=at(cx,cy+1);
                if(!finite(v00)||!finite(v10)||!finite(v11)||!finite(v01)) continue;
                // Corner values above the level, as a four-bit case index.
                const int code=(v00>iso?1:0)|(v10>iso?2:0)|(v11>iso?4:0)|(v01>iso?8:0);
                if(code==0||code==15) continue;
                auto lerp=[&](double va,double vb,double a,double b){
                    const double d=vb-va;
                    return std::abs(d)<1e-15? a : a+(b-a)*((iso-va)/d);
                };
                const QPointF bottom=pointFor(lerp(v00,v10,cx,cx+1),cy);
                const QPointF right =pointFor(cx+1,lerp(v10,v11,cy,cy+1));
                const QPointF top   =pointFor(lerp(v01,v11,cx,cx+1),cy+1);
                const QPointF left  =pointFor(cx,lerp(v00,v01,cy,cy+1));
                switch(code){
                case 1: case 14: p->drawLine(left,bottom); break;
                case 2: case 13: p->drawLine(bottom,right); break;
                case 3: case 12: p->drawLine(left,right); break;
                case 4: case 11: p->drawLine(right,top); break;
                case 6: case 9:  p->drawLine(bottom,top); break;
                case 7: case 8:  p->drawLine(left,top); break;
                case 5:  p->drawLine(left,bottom); p->drawLine(right,top); break;
                case 10: p->drawLine(left,top); p->drawLine(bottom,right); break;
                default: break;
                }
            }
        }
    }
    p->restore();
}

// A violin is the kernel density of a distribution, mirrored about its slot.
// The box plot inside it is what makes it readable as a summary rather than as
// a shape: the median and the quartiles are the numbers people quote.
void QtPlotBackend::drawViolin(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    const double slotW=qMin(f.plotArea.width()/qMax(1.0,double(spec.series.size()))*0.72,120.0);
    for(const PlotSeries& s:spec.series){
        // prepareSpec packs each violin as: x = slot, y = density profile, with
        // the profile's sample values carried in x beyond the first entry.
        if(s.x.size()<4||s.y.size()!=s.x.size()) continue;
        const double slot=s.x.first();
        double peak=0.0;
        for(int i=1;i<s.y.size();++i) peak=qMax(peak,s.y[i]);
        if(!(peak>0.0)) continue;

        QPainterPath body;
        bool started=false;
        for(int i=1;i<s.x.size();++i){
            const double halfWidth=(s.y[i]/peak)*slotW*0.5;
            const QPointF centre=toDevice(f,slot,s.x[i]);
            const QPointF right(centre.x()+halfWidth,centre.y());
            if(!started){ body.moveTo(right); started=true; } else body.lineTo(right);
        }
        for(int i=s.x.size()-1;i>=1;--i){
            const double halfWidth=(s.y[i]/peak)*slotW*0.5;
            const QPointF centre=toDevice(f,slot,s.x[i]);
            body.lineTo(QPointF(centre.x()-halfWidth,centre.y()));
        }
        body.closeSubpath();

        p->save();
        QColor fill=s.color; fill.setAlphaF(0.45);
        p->setBrush(fill);
        QPen edge(s.color); edge.setWidthF(qMax(0.6,s.lineWidth));
        p->setPen(edge);
        p->drawPath(body);
        p->restore();
    }
}

// A forest plot is one estimate per row with its confidence interval, and a
// rule at the null value. Horizontal because the rows are named studies.
void QtPlotBackend::drawForest(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    p->save();
    for(const PlotSeries& s:spec.series){
        // x = row position, y = {estimate, low, high}
        if(s.x.isEmpty()||s.y.size()<3) continue;
        const double row=s.x.first();
        const QPointF estimate=toDevice(f,s.y[0],row);
        const QPointF low=toDevice(f,s.y[1],row);
        const QPointF high=toDevice(f,s.y[2],row);

        QPen line(s.color);
        line.setWidthF(qMax(0.8,s.lineWidth));
        p->setPen(line);
        p->drawLine(low,high);
        // End caps, so a wide interval cannot be mistaken for a ruled line.
        p->drawLine(QPointF(low.x(),low.y()-3.5),QPointF(low.x(),low.y()+3.5));
        p->drawLine(QPointF(high.x(),high.y()-3.5),QPointF(high.x(),high.y()+3.5));

        p->setPen(Qt::NoPen);
        p->setBrush(s.color);
        const double box=qMax(3.0,s.markerSize);
        p->drawRect(QRectF(estimate.x()-box/2.0,estimate.y()-box/2.0,box,box));
    }
    p->restore();
}

// Polar plots get their own projection rather than an axis frame: the chrome is
// concentric rings and radial spokes, and the data is drawn in the same call
// because there is no rectangular plot area to hand to a normal draw function.
void QtPlotBackend::drawPolar(QPainter* p,const QRectF& target,const PlotSpec& spec,
                              bool markersOnly) const {
    const QPointF centre=target.center();
    const double radius=qMin(target.width(),target.height())*0.5-46.0;
    if(radius<20.0) return;

    double rMax=0.0;
    for(const PlotSeries& s:spec.series)
        for(double v:s.y) if(finite(v)) rMax=qMax(rMax,std::abs(v));
    if(!(rMax>0.0)) rMax=1.0;

    p->save();
    // Rings and spokes.
    QPen grid(spec.style.gridColor);
    grid.setWidthF(0.6);
    p->setPen(grid);
    p->setBrush(Qt::NoBrush);
    for(int ring=1;ring<=4;++ring){
        const double r=radius*double(ring)/4.0;
        p->drawEllipse(centre,r,r);
    }
    for(int spoke=0;spoke<12;++spoke){
        const double angle=spoke*(2.0*3.14159265358979323846/12.0);
        p->drawLine(centre,centre+QPointF(std::cos(angle)*radius,-std::sin(angle)*radius));
    }
    p->setPen(spec.style.foreground);
    p->setFont(font(spec,spec.style.tickSize));
    for(int ring=1;ring<=4;++ring){
        const double r=radius*double(ring)/4.0;
        p->drawText(QRectF(centre.x()+4,centre.y()-r-8,60,14),Qt::AlignLeft|Qt::AlignVCenter,
                    QString::number(rMax*double(ring)/4.0,'g',3));
    }

    // A binned distribution is drawn as wedges, not as points. Sixteen dots
    // around a circle is technically the data and visually nothing; the wedge
    // is what makes a rose readable, and its area is the quantity.
    const bool wedges=spec.engine==QLatin1String("Polar Histogram")
                    ||spec.engine==QLatin1String("Wind Rose");
    const bool spokes=spec.engine==QLatin1String("Compass");
    if(wedges||spokes){
        const int sectors=spec.series.isEmpty()?1:qMax(1,int(spec.series.first().x.size()));
        const double sweep=360.0/double(qMax(1,sectors));
        for(const PlotSeries& s:spec.series){
            const int n=qMin(s.x.size(),s.y.size());
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                const double r=std::abs(s.y[i])/rMax*radius;
                if(!(r>0.0)) continue;
                if(spokes){
                    const double angle=s.x[i]*3.14159265358979323846/180.0;
                    QPen arrow(s.color);
                    arrow.setWidthF(qMax(1.0,s.lineWidth*1.3));
                    p->setPen(arrow); p->setBrush(Qt::NoBrush);
                    const QPointF tip=centre+QPointF(std::cos(angle)*r,-std::sin(angle)*r);
                    p->drawLine(centre,tip);
                    // A head, so direction is readable without following the
                    // line back to the origin.
                    const double head=qMin(9.0,r*0.28);
                    for(const double turn:{2.5,-2.5}){
                        const double a=angle+turn;
                        p->drawLine(tip,tip+QPointF(std::cos(a)*head,-std::sin(a)*head));
                    }
                }else{
                    QColor fill=s.color; fill.setAlphaF(0.72);
                    p->setBrush(fill);
                    QPen edge(s.color.darker(120)); edge.setWidthF(0.6);
                    p->setPen(edge);
                    QPainterPath wedge;
                    wedge.moveTo(centre);
                    // Qt measures arcs in sixteenths of a degree, anticlockwise.
                    wedge.arcTo(QRectF(centre.x()-r,centre.y()-r,2*r,2*r),
                                s.x[i]-sweep/2.0,sweep);
                    wedge.closeSubpath();
                    p->drawPath(wedge);
                }
            }
        }
        p->restore();
        return;
    }

    for(const PlotSeries& s:spec.series){
        const int n=qMin(s.x.size(),s.y.size());
        if(n<1) continue;
        QPainterPath path;
        bool started=false;
        p->setPen(Qt::NoPen);
        p->setBrush(s.color);
        for(int i=0;i<n;++i){
            if(!finite(s.x[i])||!finite(s.y[i])) continue;
            // x is the angle in degrees, y the radius. Degrees because that is
            // what instruments report and what people type.
            const double angle=s.x[i]*3.14159265358979323846/180.0;
            const double r=std::abs(s.y[i])/rMax*radius;
            const QPointF pt=centre+QPointF(std::cos(angle)*r,-std::sin(angle)*r);
            if(markersOnly){
                p->drawEllipse(pt,qMax(1.5,s.markerSize/2.0),qMax(1.5,s.markerSize/2.0));
            }else{
                if(!started){ path.moveTo(pt); started=true; } else path.lineTo(pt);
            }
        }
        if(!markersOnly&&started){
            QPen pen(s.color);
            pen.setWidthF(qMax(0.8,s.lineWidth));
            if(!s.dashPattern.isEmpty()) pen.setDashPattern(s.dashPattern);
            p->setPen(pen);
            p->setBrush(Qt::NoBrush);
            p->drawPath(path);
        }
    }
    p->restore();
}

// ---------------------------------------------------------------------------
// 3-D engines, drawn by orthographic projection.
//
// These could have been left to the VTK viewport, and for a rotatable, lit,
// million-triangle surface they should be. But a catalogue entry that only
// works when an optional plugin is loaded is a catalogue entry that usually
// does not work, and a projected wireframe is what a paper figure needs anyway:
// it exports as vectors, it prints, and it does not depend on a GPU.
//
// Orthographic rather than perspective, because a perspective 3-D plot makes
// equal intervals look unequal, and the whole point of the axes is that they
// can be read.
namespace {

struct Projection {
    double sinAz, cosAz, sinEl, cosEl;
    QPointF origin;
    double scale;
};

Projection makeProjection(const QRectF& target,double azimuthDeg,double elevationDeg){
    Projection p;
    const double az=azimuthDeg*3.14159265358979323846/180.0;
    const double el=elevationDeg*3.14159265358979323846/180.0;
    p.sinAz=std::sin(az); p.cosAz=std::cos(az);
    p.sinEl=std::sin(el); p.cosEl=std::cos(el);
    p.origin=target.center();
    p.scale=qMin(target.width(),target.height())*0.34;
    return p;
}

// Normalised coordinates in and screen point out. Depth comes back too, so the
// caller can sort back to front - without that a surface draws its far faces
// over its near ones and reads inside out.
QPointF project(const Projection& p,double x,double y,double z,double* depth=nullptr){
    const double sx=-x*p.sinAz+y*p.cosAz;
    const double sy=-(x*p.cosAz+y*p.sinAz)*p.sinEl+z*p.cosEl;
    if(depth) *depth=(x*p.cosAz+y*p.sinAz)*p.cosEl+z*p.sinEl;
    return QPointF(p.origin.x()+sx*p.scale,p.origin.y()-sy*p.scale);
}

} // namespace

// The unit bounding cube, drawn faintly first so a projection reads as a
// projection. Both 3-D entry points - draw3D and the vector/volume family -
// opened with the same twelve edges, the same 0.6 pen and the same corner
// table; the two copies had already drifted apart in whether the loop body
// carried braces.
void drawBoundingCube(QPainter* p,const Projection& proj,const QColor& gridColor){
    QPen box(gridColor);
    box.setWidthF(0.6);
    p->setPen(box);
    const double c[8][3]={{-0.5,-0.5,-0.5},{0.5,-0.5,-0.5},{0.5,0.5,-0.5},{-0.5,0.5,-0.5},
                          {-0.5,-0.5, 0.5},{0.5,-0.5, 0.5},{0.5,0.5, 0.5},{-0.5,0.5, 0.5}};
    const int edges[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for(const auto& e:edges)
        p->drawLine(project(proj,c[e[0]][0],c[e[0]][1],c[e[0]][2]),
                    project(proj,c[e[1]][0],c[e[1]][1],c[e[1]][2]));
}

void QtPlotBackend::draw3D(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    // The field colour map, read once. Viridis unless the style asks for
    // another - see PlotStyle::colourMap.
    const ColourMapKind cmap=colourMapFor(spec.style.colourMap);
    if(spec.series.size()<3) return;
    const QVector<double>& xs=spec.series.at(0).y;
    const QVector<double>& ys=spec.series.at(1).y;
    const QVector<double>& zs=spec.series.at(2).y;
    const int n=qMin(xs.size(),qMin(ys.size(),zs.size()));
    if(n<2) return;

    const Bounds bx=boundsOf(xs), by=boundsOf(ys), bz=boundsOf(zs);
    if(!bx.valid||!by.valid||!bz.valid) return;

    const Projection proj=makeProjection(target,-35.0,24.0);

    p->save();

    drawBoundingCube(p,proj,spec.style.gridColor);

    p->setFont(font(spec,spec.style.axisLabelSize));
    p->setPen(spec.style.foreground);
    p->drawText(QRectF(project(proj,0.0,-0.5,-0.5)+QPointF(-40,6),QSizeF(80,14)),
                Qt::AlignCenter,spec.series.at(0).label);
    p->drawText(QRectF(project(proj,0.5,0.0,-0.5)+QPointF(-40,6),QSizeF(80,14)),
                Qt::AlignCenter,spec.series.at(1).label);
    p->drawText(QRectF(project(proj,-0.5,-0.5,0.0)+QPointF(-84,-7),QSizeF(80,14)),
                Qt::AlignRight|Qt::AlignVCenter,spec.series.at(2).label);

    const bool surface=spec.engine==QLatin1String("3D Topography / Surface")
                     ||spec.engine==QLatin1String("3D Mesh");
    const bool wireframe=spec.engine==QLatin1String("3D Mesh");

    if(surface){
        // A grid, so a scattered survey can still be drawn as a surface. Quads
        // are sorted by the depth of their centre and painted back to front.
        const ValueGrid& g=cachedGrid(spec,GridAggregate::Mean,4,0);
        if(g.valid){
            struct Quad { double depth; QPolygonF shape; double value; };
            QVector<Quad> quads;
            quads.reserve((g.nx-1)*(g.ny-1));
            for(int cy=0;cy<g.ny-1;++cy){
                for(int cx=0;cx<g.nx-1;++cx){
                    const double v00=g.cells[cy*g.nx+cx],       v10=g.cells[cy*g.nx+cx+1];
                    const double v11=g.cells[(cy+1)*g.nx+cx+1], v01=g.cells[(cy+1)*g.nx+cx];
                    if(!finite(v00)||!finite(v10)||!finite(v11)||!finite(v01)) continue;
                    auto corner=[&](int ix,int iy,double v,double* d){
                        const double nx=double(ix)/double(g.nx-1)-0.5;
                        const double ny=double(iy)/double(g.ny-1)-0.5;
                        return project(proj,nx,ny,bz.norm(v),d);
                    };
                    double d0,d1,d2,d3;
                    QPolygonF shape;
                    shape<<corner(cx,cy,v00,&d0)<<corner(cx+1,cy,v10,&d1)
                         <<corner(cx+1,cy+1,v11,&d2)<<corner(cx,cy+1,v01,&d3);
                    quads.append({(d0+d1+d2+d3)/4.0,shape,(v00+v10+v11+v01)/4.0});
                }
            }
            std::sort(quads.begin(),quads.end(),
                      [](const Quad& a,const Quad& b){ return a.depth<b.depth; });
            for(const Quad& q:quads){
                const double t=(q.value-g.vLo)/qMax(1e-12,g.vHi-g.vLo);
                if(wireframe){
                    p->setBrush(Qt::NoBrush);
                    QPen mesh(colourMap(cmap,t)); mesh.setWidthF(qMax(0.4,spec.style.lineWidth*0.7));
                    p->setPen(mesh);
                }else{
                    p->setBrush(colourMap(cmap,t));
                    QPen edge(colourMap(cmap,t).darker(115)); edge.setWidthF(0.3);
                    p->setPen(edge);
                }
                p->drawPolygon(q.shape);
            }
        }
    }else if(spec.engine==QLatin1String("3D Line")){
        QPainterPath path;
        bool started=false;
        for(int i=0;i<n;++i){
            if(!finite(xs[i])||!finite(ys[i])||!finite(zs[i])) continue;
            const QPointF pt=project(proj,bx.norm(xs[i]),by.norm(ys[i]),bz.norm(zs[i]));
            if(!started){ path.moveTo(pt); started=true; } else path.lineTo(pt);
        }
        QPen pen(spec.series.at(2).color);
        pen.setWidthF(qMax(0.9,spec.style.lineWidth*1.2));
        p->setPen(pen); p->setBrush(Qt::NoBrush);
        p->drawPath(path);
    }else{
        // 3D Scatter. Painter's algorithm again, and the marker shrinks with
        // depth so the far side of the cloud recedes.
        struct Dot { double depth; QPointF at; double value; };
        QVector<Dot> dots;
        dots.reserve(n);
        for(int i=0;i<n;++i){
            if(!finite(xs[i])||!finite(ys[i])||!finite(zs[i])) continue;
            double d=0;
            const QPointF pt=project(proj,bx.norm(xs[i]),by.norm(ys[i]),bz.norm(zs[i]),&d);
            dots.append({d,pt,zs[i]});
        }
        std::sort(dots.begin(),dots.end(),[](const Dot& a,const Dot& b){ return a.depth<b.depth; });
        p->setPen(Qt::NoPen);
        const double base=qMax(2.0,spec.series.at(2).markerSize);
        for(const Dot& dot:dots){
            const double t=(dot.value-bz.lo)/qMax(1e-12,bz.hi-bz.lo);
            p->setBrush(colourMap(cmap,t));
            const double r=base*(0.62+0.38*(dot.depth+0.9));
            p->drawEllipse(dot.at,qMax(0.8,r/2.0),qMax(0.8,r/2.0));
        }
    }

    drawFloatingTitle(p,target,spec);
    p->restore();
}

// ---------------------------------------------------------------------------
// Vector fields.
//
// Four mapped columns: x, y and the two components of the vector at each point.
// Quiver draws the vectors themselves; streamlines integrate through the field;
// divergence and vorticity draw a scalar derived from it. All four share the
// same gridding, because a field measured at scattered points has to be regular
// before anything can be differentiated or followed through it.
void QtPlotBackend::drawVectorField(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    // The field colour map, read once. Viridis unless the style asks for
    // another - see PlotStyle::colourMap.
    const ColourMapKind cmap=colourMapFor(spec.style.colourMap);
    if(spec.series.size()<4) return;
    const QVector<double>& xs=spec.series.at(0).y;
    const QVector<double>& ys=spec.series.at(1).y;
    const QVector<double>& us=spec.series.at(2).y;
    const QVector<double>& vs=spec.series.at(3).y;
    const int n=qMin(qMin(xs.size(),ys.size()),qMin(us.size(),vs.size()));
    if(n<4) return;

    // Grid both components onto the same lattice.
    PlotSpec uSpec=spec; uSpec.series={spec.series.at(0),spec.series.at(1),spec.series.at(2)};
    PlotSpec vSpec=spec; vSpec.series={spec.series.at(0),spec.series.at(1),spec.series.at(3)};
    const ValueGrid& gu=cachedGrid(uSpec,GridAggregate::Mean,4,1);
    const ValueGrid& gv=cachedGrid(vSpec,GridAggregate::Mean,4,2);
    if(!gu.valid||!gv.valid) return;

    const QString& engine=spec.engine;
    const bool derived=engine==QLatin1String("Divergence Map")
                     ||engine==QLatin1String("Vorticity Map");
    const bool streaming=engine.startsWith(QLatin1String("Stream"))
                       ||engine==QLatin1String("Flow Texture (LIC)")
                       ||engine==QLatin1String("Phase Portrait");

    const double cellX=(gu.xHi-gu.xLo)/qMax(1,gu.nx-1);
    const double cellY=(gu.yHi-gu.yLo)/qMax(1,gu.ny-1);
    auto sampleU=[&](int cx,int cy){ return gu.cells[qBound(0,cy,gu.ny-1)*gu.nx+qBound(0,cx,gu.nx-1)]; };
    auto sampleV=[&](int cx,int cy){ return gv.cells[qBound(0,cy,gv.ny-1)*gv.nx+qBound(0,cx,gv.nx-1)]; };

    if(derived){
        // Divergence is du/dx + dv/dy, vorticity is dv/dx - du/dy. Central
        // differences on the grid, then drawn as the scalar field it is.
        const bool vorticity=engine==QLatin1String("Vorticity Map");
        ValueGrid out=gu;
        double lo=std::numeric_limits<double>::infinity(),hi=-lo;
        for(int cy=0;cy<gu.ny;++cy){
            for(int cx=0;cx<gu.nx;++cx){
                const double dudx=(sampleU(cx+1,cy)-sampleU(cx-1,cy))/(2.0*qMax(1e-12,cellX));
                const double dudy=(sampleU(cx,cy+1)-sampleU(cx,cy-1))/(2.0*qMax(1e-12,cellY));
                const double dvdx=(sampleV(cx+1,cy)-sampleV(cx-1,cy))/(2.0*qMax(1e-12,cellX));
                const double dvdy=(sampleV(cx,cy+1)-sampleV(cx,cy-1))/(2.0*qMax(1e-12,cellY));
                const double value=vorticity?(dvdx-dudy):(dudx+dvdy);
                out.cells[cy*gu.nx+cx]=value;
                if(finite(value)){ lo=qMin(lo,value); hi=qMax(hi,value); }
            }
        }
        if(!finite(lo)||!finite(hi)) return;
        if(qFuzzyCompare(lo,hi)) hi=lo+1.0;
        out.vLo=lo; out.vHi=hi;
        const double cw=f.plotArea.width()/double(out.nx);
        const double ch=f.plotArea.height()/double(out.ny);
        p->save();
        p->setPen(Qt::NoPen);
        for(int cy=0;cy<out.ny;++cy){
            for(int cx=0;cx<out.nx;++cx){
                const double value=out.cells[cy*out.nx+cx];
                if(!finite(value)) continue;
                p->setBrush(colourMap(cmap,(value-out.vLo)/(out.vHi-out.vLo)));
                p->drawRect(QRectF(f.plotArea.left()+cx*cw,
                                   f.plotArea.bottom()-(cy+1)*ch,cw+0.5,ch+0.5));
            }
        }
        p->restore();
        return;
    }

    double magMax=0.0;
    for(int k=0;k<gu.cells.size()&&k<gv.cells.size();++k){
        const double u=gu.cells[k], v=gv.cells[k];
        if(finite(u)&&finite(v)) magMax=qMax(magMax,std::hypot(u,v));
    }
    if(!(magMax>0.0)) return;

    p->save();
    if(streaming){
        // Streamlines: seed a lattice and integrate with RK2, which is stable
        // enough on a gridded field and does not curl inward the way Euler does
        // on a rotational flow.
        const int seeds=qBound(6,gu.nx/2,18);
        for(int sy=0;sy<seeds;++sy){
            for(int sx=0;sx<seeds;++sx){
                double px=gu.xLo+(gu.xHi-gu.xLo)*(double(sx)+0.5)/double(seeds);
                double py=gu.yLo+(gu.yHi-gu.yLo)*(double(sy)+0.5)/double(seeds);
                QPainterPath path;
                bool started=false;
                double carried=0.0;
                const double step=qMin(cellX,cellY)*0.6;
                for(int iter=0;iter<160;++iter){
                    const int cx=int((px-gu.xLo)/qMax(1e-12,gu.xHi-gu.xLo)*(gu.nx-1)+0.5);
                    const int cy=int((py-gu.yLo)/qMax(1e-12,gu.yHi-gu.yLo)*(gu.ny-1)+0.5);
                    if(cx<0||cy<0||cx>=gu.nx||cy>=gu.ny) break;
                    const double u=sampleU(cx,cy), v=sampleV(cx,cy);
                    if(!finite(u)||!finite(v)) break;
                    const double mag=std::hypot(u,v);
                    if(!(mag>1e-12)) break;
                    carried=qMax(carried,mag);
                    // Midpoint step.
                    const double hx=px+u/mag*step*0.5, hy=py+v/mag*step*0.5;
                    const int mx=int((hx-gu.xLo)/qMax(1e-12,gu.xHi-gu.xLo)*(gu.nx-1)+0.5);
                    const int my=int((hy-gu.yLo)/qMax(1e-12,gu.yHi-gu.yLo)*(gu.ny-1)+0.5);
                    const double u2=sampleU(mx,my), v2=sampleV(mx,my);
                    const double m2=std::hypot(u2,v2);
                    if(!finite(m2)||!(m2>1e-12)) break;
                    px+=u2/m2*step; py+=v2/m2*step;
                    const QPointF pt=toDevice(f,px,py);
                    if(!started){ path.moveTo(pt); started=true; } else path.lineTo(pt);
                }
                if(!started) continue;
                QPen pen(colourMap(cmap,qBound(0.0,carried/magMax,1.0)));
                pen.setWidthF(qMax(0.5,spec.style.lineWidth*0.9));
                p->setPen(pen); p->setBrush(Qt::NoBrush);
                p->drawPath(path);
            }
        }
    }else{
        // Quiver, and Feather, which is the same arrows anchored on a line.
        const int stride=qMax(1,gu.nx/22);
        const double arrowMax=qMin(f.plotArea.width()/double(gu.nx),
                                   f.plotArea.height()/double(gu.ny))*stride*1.6;
        for(int cy=0;cy<gu.ny;cy+=stride){
            for(int cx=0;cx<gu.nx;cx+=stride){
                const double u=sampleU(cx,cy), v=sampleV(cx,cy);
                if(!finite(u)||!finite(v)) continue;
                const double mag=std::hypot(u,v);
                if(!(mag>0.0)) continue;
                const double gx=gu.xLo+(gu.xHi-gu.xLo)*double(cx)/double(qMax(1,gu.nx-1));
                const double gy=gu.yLo+(gu.yHi-gu.yLo)*double(cy)/double(qMax(1,gu.ny-1));
                const QPointF from=toDevice(f,gx,gy);
                const double length=arrowMax*(mag/magMax);
                const QPointF dir(u/mag,-v/mag);
                const QPointF to=from+dir*length;
                QPen pen(colourMap(cmap,mag/magMax));
                pen.setWidthF(qMax(0.5,spec.style.lineWidth));
                p->setPen(pen);
                p->drawLine(from,to);
                // Head as two short strokes; a filled polygon at this size is
                // a blob rather than an arrow.
                const double head=qMin(6.0,length*0.34);
                const double angle=std::atan2(dir.y(),dir.x());
                for(const double turn:{2.6,-2.6}){
                    const double a=angle+turn;
                    p->drawLine(to,to+QPointF(std::cos(a)*head,std::sin(a)*head));
                }
            }
        }
    }
    p->restore();
}

// Radix-2 FFT, iterative, in place. Written here rather than pulled in because
// it is forty lines and the alternative is a dependency for two engines.
// Sequences are zero-padded to the next power of two, which is standard and
// costs only frequency resolution, not correctness.
namespace {

void fftInPlace(QVector<double>& re,QVector<double>& im){
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

int nextPowerOfTwo(int n){
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

SpectralWindow spectralWindowFor(const QString& variant){
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

double spectralWindow(SpectralWindow kind,int i,int n){
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

} // namespace

// ---------------------------------------------------------------------------
// Composition and hierarchy.
//
// Treemap, sunburst, Venn, word cloud and Sankey all lay out a whole rather
// than plot a coordinate, so none of them uses the axis frame. They share this
// one function because they share the same input shape - a list of labelled
// magnitudes - and differ only in how the area is divided.
// ======================================================================
// Networks
//
// A grid, a pipeline, a route map and a supply chain are all the same object:
// nodes and the links between them. Nothing in the catalogue could draw one -
// a Sankey is a flow through stages, not a graph - so a network arrived as a
// table of pairs and stayed a table of pairs.
//
// Both of these read an edge list: the first three mapped columns are source,
// target and weight, with the weight optional. Node identifiers are numbers,
// because that is what a mapped column can carry.
// ======================================================================
namespace {

struct EdgeList {
    QVector<double> ids;                 // distinct node identifiers, in first-seen order
    QVector<int> from,to;                // indices into ids
    QVector<double> weight;
    QVector<double> strength;            // total weight at each node
    bool valid=false;
};

EdgeList edgesFrom(const PlotSpec& spec){
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

} // namespace

void QtPlotBackend::drawNetwork(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    const EdgeList e=edgesFrom(spec);
    drawFloatingTitle(p,target,spec);
    if(!e.valid) return;
    const int n=e.ids.size();

    // The layout is cached, and computed in a unit square rather than in device
    // coordinates so that a resize reuses it.
    //
    // Measured before caching: 400 nodes took 542 ms per repaint, and a repaint
    // happens on every hover, resize and theme change. The layout depends only
    // on the edges, so recomputing it when nothing about the graph changed was
    // half a second of arithmetic to produce the picture already on screen.
    const quint64 hash=specFingerprint(spec);
    if(!networkCache_.valid||networkCache_.hash!=hash||networkCache_.pos.size()!=n){
        QVector<QPointF> pos(n),disp(n);
        for(int i=0;i<n;++i){
            const double angle=2.0*M_PI*double(i)/double(n);
            pos[i]=QPointF(0.5+0.42*std::cos(angle),0.5+0.42*std::sin(angle));
        }
        // Fruchterman-Reingold, seeded from that circle and run for a fixed
        // number of iterations. Deliberately deterministic: a figure that lays
        // itself out differently every time cannot be exported, compared or
        // published, and a random seed is exactly how that happens.
        //
        // The repulsion step is every pair against every other, so the layout
        // costs iterations x n^2. Past a few hundred nodes that is seconds for a
        // picture nobody can read anyway, so a large graph keeps the circle it
        // was seeded with - the edges still show the structure, and the figure
        // still appears rather than silently not drawing.
        const int iterations=(n<=400)?150:0;
        const double k=std::sqrt(1.0/double(n));
        double temperature=0.10;
        for(int step=0;step<iterations;++step){
            for(int i=0;i<n;++i) disp[i]=QPointF(0,0);
            for(int i=0;i<n;++i){
                for(int j=i+1;j<n;++j){
                    QPointF d=pos[i]-pos[j];
                    double len=std::hypot(d.x(),d.y());
                    if(len<1e-9){
                        d=QPointF(1e-6*double(i+1),1e-6*double(j+1));
                        len=std::hypot(d.x(),d.y());
                    }
                    const QPointF push=d/len*(k*k/len);
                    disp[i]+=push; disp[j]-=push;
                }
            }
            for(int m=0;m<e.from.size();++m){
                const int i=e.from[m],j=e.to[m];
                if(i==j) continue;
                const QPointF d=pos[i]-pos[j];
                const double len=qMax(1e-9,std::hypot(d.x(),d.y()));
                const QPointF pull=d/len*(len*len/k);
                disp[i]-=pull; disp[j]+=pull;
            }
            for(int i=0;i<n;++i){
                const double len=std::hypot(disp[i].x(),disp[i].y());
                if(len>1e-12) pos[i]+=disp[i]/len*qMin(len,temperature);
                pos[i].setX(qBound(0.0,pos[i].x(),1.0));
                pos[i].setY(qBound(0.0,pos[i].y(),1.0));
            }
            temperature*=0.955;
        }
        // Fit what the layout actually used to the whole unit square, so a
        // graph that settled into a corner still fills the figure.
        double xLo=1.0,xHi=0.0,yLo=1.0,yHi=0.0;
        for(const QPointF& q:pos){
            xLo=qMin(xLo,q.x()); xHi=qMax(xHi,q.x());
            yLo=qMin(yLo,q.y()); yHi=qMax(yHi,q.y());
        }
        const double spanX=qMax(1e-6,xHi-xLo),spanY=qMax(1e-6,yHi-yLo);
        for(QPointF& q:pos)
            q=QPointF((q.x()-xLo)/spanX,(q.y()-yLo)/spanY);
        networkCache_.pos=pos;
        networkCache_.hash=hash;
        networkCache_.valid=true;
    }

    const QRectF area=target.adjusted(target.width()*0.12,target.height()*0.12,
                                      -target.width()*0.12,-target.height()*0.12);
    QVector<QPointF> at(n);
    for(int i=0;i<n;++i)
        at[i]=QPointF(area.left()+networkCache_.pos[i].x()*area.width(),
                      area.top()+networkCache_.pos[i].y()*area.height());

    double heaviest=0.0,strongest=0.0;
    for(double w:e.weight) heaviest=qMax(heaviest,w);
    for(double w:e.strength) strongest=qMax(strongest,w);
    if(!(heaviest>0)) heaviest=1.0;
    if(!(strongest>0)) strongest=1.0;

    p->save();
    for(int m=0;m<e.from.size();++m){
        QColor line=spec.style.gridColor;
        line.setAlphaF(0.85);
        QPen pen(line);
        pen.setWidthF(qBound(0.4,3.0*e.weight[m]/heaviest,4.0));
        p->setPen(pen);
        p->drawLine(at[e.from[m]],at[e.to[m]]);
    }
    const QColor node=spec.series.at(0).color;
    p->setPen(QPen(spec.style.foreground,0.8));
    const QFont label=font(spec,spec.style.tickSize);
    p->setFont(label);
    const QFontMetricsF fm(label,p->device());
    for(int i=0;i<n;++i){
        const double radius=qBound(2.5,3.0+6.0*e.strength[i]/strongest,11.0);
        p->setBrush(node);
        p->drawEllipse(at[i],radius,radius);
        // Labels only when they can be read. Forty nodes of overlapping text is
        // less informative than none.
        if(n<=40){
            const QString text=QString::number(e.ids[i],'g',6);
            p->drawText(QRectF(at[i].x()-40,at[i].y()-radius-fm.height()-1,80,fm.height()),
                        Qt::AlignHCenter|Qt::AlignBottom,text);
        }
    }
    p->restore();
}

// A chord diagram: the same edge list read as a matrix of flows between places.
// Each node gets an arc in proportion to everything entering and leaving it,
// and each flow a ribbon between two arcs. It is the one picture that shows an
// origin-destination matrix as a whole rather than as a grid of numbers.
void QtPlotBackend::drawChord(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    const EdgeList e=edgesFrom(spec);
    drawFloatingTitle(p,target,spec);
    if(!e.valid||e.ids.size()>60) return;
    const int n=e.ids.size();

    double grand=0.0;
    for(double w:e.strength) grand+=w;
    if(!(grand>0)||!finite(grand)) return;

    const QPointF centre=target.center();
    const double radius=0.36*qMin(target.width(),target.height());
    const double inner=radius*0.90;
    // A gap between arcs, so two adjacent nodes are two arcs and not one.
    const double gap=qMin(0.03,0.6/double(n));
    const double usable=2.0*M_PI*(1.0-gap*double(n));

    QVector<double> start(n),extent(n);
    double angle=-M_PI/2.0;
    for(int i=0;i<n;++i){
        extent[i]=usable*e.strength[i]/grand;
        start[i]=angle;
        angle+=extent[i]+gap*2.0*M_PI;
    }
    // Where the next ribbon leaving each node begins, so ribbons sit side by
    // side inside their node's arc instead of on top of each other.
    QVector<double> cursor=start;

    p->save();
    const QFont label=font(spec,spec.style.tickSize);
    p->setFont(label);
    for(int m=0;m<e.from.size();++m){
        const int i=e.from[m],j=e.to[m];
        const double share=e.weight[m]/qMax(1e-12,grand)*usable;
        // Dropping the sub-pixel ribbons was tried here and reverted: 5.4x
        // faster at sixty nodes (295 ms to 54 ms) and 16.4% of the pixels
        // changed, because a ribbon that is thin where it meets the ring is
        // wide in the middle where it bundles with its neighbours. The speed
        // was real and so was the different picture.
        const double a0=cursor[i],a1=cursor[i]+share;
        const double b0=cursor[j],b1=cursor[j]+share;
        cursor[i]=a1; cursor[j]=b1;
        const auto at=[centre,inner](double t){
            return QPointF(centre.x()+inner*std::cos(t),centre.y()+inner*std::sin(t));
        };
        QPainterPath ribbon(at(a0));
        ribbon.arcTo(QRectF(centre.x()-inner,centre.y()-inner,inner*2,inner*2),
                     -a0*180.0/M_PI,-(a1-a0)*180.0/M_PI);
        ribbon.quadTo(centre,at(b0));
        ribbon.arcTo(QRectF(centre.x()-inner,centre.y()-inner,inner*2,inner*2),
                     -b0*180.0/M_PI,-(b1-b0)*180.0/M_PI);
        ribbon.quadTo(centre,at(a0));
        QColor fill=spec.series.at(0).color;
        // Hue by source, so every flow out of one place reads as one family.
        fill=QColor::fromHsvF(std::fmod(double(i)/double(n)+0.55,1.0),0.55,0.85);
        fill.setAlphaF(0.45);
        p->setBrush(fill);
        p->setPen(Qt::NoPen);
        p->drawPath(ribbon);
    }
    const QFontMetricsF fm(label,p->device());
    for(int i=0;i<n;++i){
        QColor arc=QColor::fromHsvF(std::fmod(double(i)/double(n)+0.55,1.0),0.6,0.95);
        p->setBrush(arc);
        p->setPen(Qt::NoPen);
        QPainterPath band;
        band.moveTo(centre.x()+inner*std::cos(start[i]),centre.y()+inner*std::sin(start[i]));
        band.arcTo(QRectF(centre.x()-inner,centre.y()-inner,inner*2,inner*2),
                   -start[i]*180.0/M_PI,-extent[i]*180.0/M_PI);
        band.arcTo(QRectF(centre.x()-radius,centre.y()-radius,radius*2,radius*2),
                   -(start[i]+extent[i])*180.0/M_PI,extent[i]*180.0/M_PI);
        band.closeSubpath();
        p->drawPath(band);

        const double mid=start[i]+extent[i]*0.5;
        const QPointF where(centre.x()+(radius+6.0)*std::cos(mid),
                            centre.y()+(radius+6.0)*std::sin(mid));
        p->setPen(spec.style.foreground);
        const QString text=QString::number(e.ids[i],'g',6);
        p->drawText(QRectF(where.x()-40,where.y()-fm.height()*0.5,80,fm.height()),
                    Qt::AlignHCenter|Qt::AlignVCenter,text);
    }
    p->restore();
}

// A Smith chart: normalised impedance on the reflection-coefficient plane.
//
// The chart IS its grid. Constant-resistance circles and constant-reactance
// arcs are what turn a dot inside a circle into a reading, so they are drawn
// here rather than assumed to be on the paper underneath - and a match at the
// centre, an open at the right and a short at the left are labelled, because
// those three points are how the chart is oriented.
//
// Two mapped columns: normalised resistance and normalised reactance.
void QtPlotBackend::drawSmith(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    drawFloatingTitle(p,target,spec);
    const double radius=0.42*qMin(target.width(),target.height());
    const QPointF centre=target.center();
    const auto at=[centre,radius](double re,double im){
        return QPointF(centre.x()+re*radius,centre.y()-im*radius);
    };

    p->save();
    QPen grid(spec.style.gridColor); grid.setWidthF(0.7);
    p->setPen(grid);
    p->setBrush(Qt::NoBrush);
    // Constant resistance: circles centred at r/(1+r) with radius 1/(1+r).
    for(const double r:{0.0,0.2,0.5,1.0,2.0,5.0}){
        const double cx=r/(1.0+r),rr=1.0/(1.0+r);
        p->drawEllipse(at(cx,0.0),rr*radius,rr*radius);
    }
    // Constant reactance: arcs centred at 1 + i/x with radius 1/|x|, clipped to
    // the unit circle - outside it they are not part of the chart.
    p->save();
    QPainterPath unit;
    unit.addEllipse(centre,radius,radius);
    p->setClipPath(unit);
    for(const double x:{0.2,0.5,1.0,2.0,5.0}){
        for(const double sign:{1.0,-1.0}){
            const double rr=1.0/std::abs(x);
            p->drawEllipse(at(1.0,sign/x),rr*radius,rr*radius);
        }
    }
    p->restore();
    QPen axis(spec.style.foreground); axis.setWidthF(0.9);
    p->setPen(axis);
    p->drawEllipse(centre,radius,radius);
    p->drawLine(at(-1.0,0.0),at(1.0,0.0));

    const QFont label=font(spec,spec.style.tickSize);
    p->setFont(label);
    const QFontMetricsF fm(label,p->device());
    const auto tag=[&](const QPointF& where,const QString& text,int flags){
        p->drawText(QRectF(where.x()-45,where.y()-fm.height()*0.5,90,fm.height()),flags,text);
    };
    tag(at(0.0,0.0)+QPointF(0,-6),QStringLiteral("match"),Qt::AlignHCenter|Qt::AlignVCenter);
    tag(at(1.0,0.0)+QPointF(-14,-10),QStringLiteral("open"),Qt::AlignRight|Qt::AlignVCenter);
    tag(at(-1.0,0.0)+QPointF(14,-10),QStringLiteral("short"),Qt::AlignLeft|Qt::AlignVCenter);

    if(spec.series.size()>=2){
        const QVector<double>& resistance=spec.series.at(0).y;
        const QVector<double>& reactance=spec.series.at(1).y;
        const int n=qMin(resistance.size(),reactance.size());
        QPen trace(spec.series.at(0).color);
        trace.setWidthF(qMax(1.0,spec.style.lineWidth));
        p->setPen(trace);
        p->setBrush(spec.series.at(0).color);
        QPointF previous;
        bool have=false;
        for(int i=0;i<n;++i){
            if(!finite(resistance[i])||!finite(reactance[i])) continue;
            // Gamma = (z - 1) / (z + 1), with z = r + jx.
            const double ar=resistance[i]-1.0,ai=reactance[i];
            const double br=resistance[i]+1.0,bi=reactance[i];
            const double denom=br*br+bi*bi;
            // finite(denom), not just denom > 0. An impedance near the top of
            // the double range squares to infinity, and infinity over infinity
            // is NaN - which reaches QPainter as a NaN coordinate and is drawn
            // as something undefined. Qt says so out loud
            // ("QPainterPath::arcTo: Adding arc where a parameter is NaN") and
            // the pathological-data sweep is where it said it.
            if(!(denom>1e-12)||!finite(denom)) continue;
            const QPointF here=at((ar*br+ai*bi)/denom,(ai*br-ar*bi)/denom);
            if(!finite(here.x())||!finite(here.y())) continue;
            p->drawEllipse(here,2.6,2.6);
            if(have&&finite(previous.x())&&finite(previous.y())) p->drawLine(previous,here);
            previous=here; have=true;
        }
    }
    p->restore();
}

// A mosaic plot: a contingency table drawn to scale. Column widths are the
// column totals and each column is divided by its own proportions, so an
// association shows as tiles that fail to line up across columns - which is
// exactly what a chi-squared test is testing and what a grid of numbers hides.
//
// Three mapped columns: the column category, the row category, and the count.
void QtPlotBackend::drawMosaic(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    drawFloatingTitle(p,target,spec);
    if(spec.series.size()<2) return;
    const QVector<double>& columnOf=spec.series.at(0).y;
    const QVector<double>& rowOf=spec.series.at(1).y;
    const bool counted=spec.series.size()>=3;
    const QVector<double> weight=counted?spec.series.at(2).y:QVector<double>();
    const int n=qMin(columnOf.size(),rowOf.size());

    QVector<double> columns,rows;
    QHash<double,int> columnIndex,rowIndex;
    QVector<QVector<double>> table;
    for(int i=0;i<n;++i){
        if(!finite(columnOf[i])||!finite(rowOf[i])) continue;
        double amount=1.0;
        if(counted){
            if(i>=weight.size()||!finite(weight[i])||weight[i]<=0) continue;
            amount=weight[i];
        }
        if(!columnIndex.contains(columnOf[i])){
            columnIndex.insert(columnOf[i],columns.size());
            columns.append(columnOf[i]);
            for(QVector<double>& r:table) r.append(0.0);
        }
        if(!rowIndex.contains(rowOf[i])){
            rowIndex.insert(rowOf[i],rows.size());
            rows.append(rowOf[i]);
            table.append(QVector<double>(columns.size(),0.0));
        }
        table[rowIndex.value(rowOf[i])][columnIndex.value(columnOf[i])]+=amount;
    }
    if(columns.isEmpty()||rows.isEmpty()||columns.size()>40||rows.size()>40) return;

    QVector<double> columnTotal(columns.size(),0.0);
    double grand=0.0;
    for(const QVector<double>& row:table)
        for(int c=0;c<row.size();++c){ columnTotal[c]+=row[c]; grand+=row[c]; }
    if(!(grand>0)) return;

    const QRectF area=target.adjusted(target.width()*0.08,target.height()*0.12,
                                      -target.width()*0.04,-target.height()*0.10);
    const double gap=qMin(3.0,area.width()/double(qMax(1,columns.size()))*0.06);
    const QFont label=font(spec,spec.style.tickSize);
    p->save();
    p->setFont(label);
    const QFontMetricsF fm(label,p->device());
    double x=area.left();
    for(int c=0;c<columns.size();++c){
        const double width=(area.width()-gap*double(columns.size()-1))*columnTotal[c]/grand;
        double y=area.top();
        for(int r=0;r<rows.size();++r){
            const double share=(columnTotal[c]>0)?table[r][c]/columnTotal[c]:0.0;
            const double height=area.height()*share;
            if(height>0){
                QColor fill=QColor::fromHsvF(std::fmod(double(r)/double(qMax(1,rows.size()))+0.08,1.0),
                                             0.5,0.9);
                fill.setAlphaF(0.85);
                p->setBrush(fill);
                p->setPen(QPen(spec.style.background,0.8));
                p->drawRect(QRectF(x,y,width,height));
                if(height>fm.height()+2&&width>fm.horizontalAdvance(QStringLiteral("00"))+4){
                    p->setPen(spec.style.background);
                    p->drawText(QRectF(x,y,width,height),Qt::AlignCenter,
                                QString::number(table[r][c],'g',4));
                }
            }
            y+=height;
        }
        p->setPen(spec.style.foreground);
        p->drawText(QRectF(x,area.bottom()+2,width,fm.height()),
                    Qt::AlignHCenter|Qt::AlignTop,QString::number(columns[c],'g',6));
        x+=width+gap;
    }
    p->restore();
}

// An UpSet plot: how big each combination of sets is, as bars, with a matrix of
// dots underneath saying which combination each bar is.
//
// A Venn diagram stops being readable at four sets and cannot be drawn at all
// past five; this is the standard answer to that, and the combinations are
// sorted by size so the ones that matter are on the left.
//
// One mapped column per set, holding 1 for a member and 0 for a non-member.
void QtPlotBackend::drawUpSet(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    drawFloatingTitle(p,target,spec);
    const int sets=spec.series.size();
    if(sets<2||sets>12) return;
    int rows=std::numeric_limits<int>::max();
    for(const PlotSeries& s:spec.series) rows=qMin(rows,int(s.y.size()));
    if(rows<=0) return;

    QHash<quint32,int> tally;
    for(int i=0;i<rows;++i){
        quint32 mask=0;
        bool usable=true;
        for(int k=0;k<sets;++k){
            const double v=spec.series.at(k).y[i];
            if(!finite(v)){ usable=false; break; }
            if(v>=0.5) mask|=(1u<<k);
        }
        if(!usable||mask==0) continue;      // a row in no set is not an intersection
        tally[mask]+=1;
    }
    if(tally.isEmpty()) return;
    QVector<QPair<quint32,int>> ordered;
    for(auto it=tally.constBegin();it!=tally.constEnd();++it) ordered.append({it.key(),it.value()});
    std::sort(ordered.begin(),ordered.end(),
              [](const QPair<quint32,int>& a,const QPair<quint32,int>& b){
                  return a.second>b.second; });
    if(ordered.size()>24) ordered.resize(24);

    const QFont label=font(spec,spec.style.tickSize);
    p->save();
    p->setFont(label);
    const QFontMetricsF fm(label,p->device());
    double widest=0;
    for(const PlotSeries& s:spec.series) widest=qMax(widest,fm.horizontalAdvance(s.label));
    const double left=target.left()+qMin(target.width()*0.32,widest+14.0);
    const double matrixHeight=qMin(target.height()*0.42,double(sets)*18.0);
    const QRectF bars(left,target.top()+22.0,
                      target.right()-left-8.0,
                      qMax(20.0,target.height()-matrixHeight-52.0));
    const double column=bars.width()/double(ordered.size());
    int biggest=0;
    for(const auto& entry:ordered) biggest=qMax(biggest,entry.second);
    if(biggest<=0){ p->restore(); return; }

    for(int i=0;i<ordered.size();++i){
        const double height=bars.height()*double(ordered[i].second)/double(biggest);
        const QRectF bar(bars.left()+column*double(i)+column*0.15,
                         bars.bottom()-height,column*0.7,height);
        p->setBrush(spec.series.at(0).color);
        p->setPen(Qt::NoPen);
        p->drawRect(bar);
        p->setPen(spec.style.foreground);
        p->drawText(QRectF(bar.left()-6,bar.top()-fm.height()-1,bar.width()+12,fm.height()),
                    Qt::AlignHCenter|Qt::AlignBottom,QString::number(ordered[i].second));
    }

    const double rowHeight=matrixHeight/double(sets);
    for(int k=0;k<sets;++k){
        const double y=bars.bottom()+14.0+rowHeight*(double(k)+0.5);
        p->setPen(spec.style.foreground);
        p->drawText(QRectF(target.left()+4,y-fm.height()*0.5,left-target.left()-10,fm.height()),
                    Qt::AlignRight|Qt::AlignVCenter,spec.series.at(k).label);
        // A band behind alternate rows, so a dot can be traced back to its set.
        if(k%2==0){
            QColor band=spec.style.gridColor; band.setAlphaF(0.35);
            p->fillRect(QRectF(left,y-rowHeight*0.5,bars.width(),rowHeight),band);
        }
    }
    for(int i=0;i<ordered.size();++i){
        const double cx=bars.left()+column*(double(i)+0.5);
        double firstY=-1,lastY=-1;
        for(int k=0;k<sets;++k){
            const double y=bars.bottom()+14.0+rowHeight*(double(k)+0.5);
            const bool member=(ordered[i].first&(1u<<k))!=0;
            p->setPen(Qt::NoPen);
            QColor dot=member?spec.series.at(0).color:spec.style.gridColor;
            if(!member) dot.setAlphaF(0.55);
            p->setBrush(dot);
            p->drawEllipse(QPointF(cx,y),qMin(4.0,rowHeight*0.3),qMin(4.0,rowHeight*0.3));
            if(member){ if(firstY<0) firstY=y; lastY=y; }
        }
        // The line joining the members is what makes a combination readable as
        // one thing rather than as a column of unrelated dots.
        if(firstY>=0&&lastY>firstY){
            QPen join(spec.series.at(0).color);
            join.setWidthF(1.6);
            p->setPen(join);
            p->drawLine(QPointF(cx,firstY),QPointF(cx,lastY));
        }
    }
    p->restore();
}

void QtPlotBackend::drawComposition(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    // Each mapped series contributes its total as one part of the whole.
    struct Part { QString label; double value; QColor colour; };
    QVector<Part> parts;
    for(const PlotSeries& s:spec.series){
        double total=0.0;
        for(double v:s.y) if(finite(v)) total+=std::abs(v);
        // finite(total), not just total>0. Each value can be finite while the
        // SUM overflows to infinity - 240 values near 1e308 does it - and an
        // infinite total then divides by itself below to produce NaN, which
        // reaches QPainter as a NaN radius. Qt says so out loud
        // ("QPainterPath::arcTo: Adding arc where a parameter is NaN") and
        // draws something undefined.
        if(total>0.0&&finite(total)) parts.append({s.label,total,s.color});
    }
    if(parts.isEmpty()) return;
    std::sort(parts.begin(),parts.end(),
              [](const Part& a,const Part& b){ return a.value>b.value; });
    double grand=0.0;
    for(const Part& part:parts) grand+=part.value;
    if(!(grand>0.0)) return;

    const QRectF area=target.adjusted(28,44,-28,-28);
    p->save();
    p->setFont(font(spec,spec.style.legendSize));

    const QString& engine=spec.engine;

    if(engine==QLatin1String("Treemap")){
        // Slice-and-dice, alternating direction. Not squarified - that needs a
        // second pass and buys aspect ratio, not correctness - but the areas
        // are exactly proportional, which is the property that matters.
        QRectF remaining=area;
        double left=grand;
        bool horizontal=remaining.width()>=remaining.height();
        for(int i=0;i<parts.size();++i){
            const double fraction=parts[i].value/qMax(1e-12,left);
            QRectF cell;
            if(i==parts.size()-1){
                cell=remaining;
            }else if(horizontal){
                const double w=remaining.width()*fraction;
                cell=QRectF(remaining.left(),remaining.top(),w,remaining.height());
                remaining.setLeft(remaining.left()+w);
            }else{
                const double h=remaining.height()*fraction;
                cell=QRectF(remaining.left(),remaining.top(),remaining.width(),h);
                remaining.setTop(remaining.top()+h);
            }
            left-=parts[i].value;
            horizontal=remaining.width()>=remaining.height();
            p->setBrush(parts[i].colour);
            QPen edge(spec.style.background); edge.setWidthF(1.5);
            p->setPen(edge);
            p->drawRect(cell);
            if(cell.width()>52&&cell.height()>18){
                p->setPen(spec.style.background.lightness()<128?Qt::white:Qt::black);
                p->drawText(cell.adjusted(5,4,-5,-4),Qt::AlignLeft|Qt::AlignTop|Qt::TextWordWrap,
                            QStringLiteral("%1\n%2%").arg(parts[i].label)
                                .arg(parts[i].value/grand*100.0,0,'f',1));
            }
        }
    }else if(engine==QLatin1String("Sunburst")){
        // One ring, because a flat list of series has one level of hierarchy.
        // Drawing a second ring would invent a structure the data does not have.
        const QPointF centre=area.center();
        const double outer=qMin(area.width(),area.height())*0.46;
        const double inner=outer*0.42;
        double start=90.0;
        for(const Part& part:parts){
            const double sweep=-360.0*part.value/grand;
            QPainterPath ring;
            ring.arcMoveTo(QRectF(centre.x()-outer,centre.y()-outer,2*outer,2*outer),start);
            ring.arcTo(QRectF(centre.x()-outer,centre.y()-outer,2*outer,2*outer),start,sweep);
            ring.arcTo(QRectF(centre.x()-inner,centre.y()-inner,2*inner,2*inner),start+sweep,-sweep);
            ring.closeSubpath();
            p->setBrush(part.colour);
            QPen edge(spec.style.background); edge.setWidthF(1.2);
            p->setPen(edge);
            p->drawPath(ring);
            start+=sweep;
        }
    }else if(engine==QLatin1String("Venn Diagram")){
        // Two or three circles, sized by their totals and overlapped by a fixed
        // amount. The overlap is not computed from the data - a Venn diagram
        // with true proportional intersections is not generally constructible,
        // and pretending otherwise would be a lie drawn to scale.
        const QPointF centre=area.center();
        const double r=qMin(area.width(),area.height())*0.26;
        const int count=qMin(3,parts.size());
        const double offsets[3][2]={{-0.55,0.0},{0.55,0.0},{0.0,0.9}};
        for(int i=0;i<count;++i){
            // parts is sorted descending and every value is finite and
            // positive, so the ratio is in (0,1] - but the division is guarded
            // anyway, because a NaN here becomes a NaN circle radius and the
            // failure is silent apart from a Qt warning nobody reads.
            const double largest=parts[0].value;
            const double share=largest>0.0?parts[i].value/largest:1.0;
            const double scale=0.7+0.6*(finite(share)?qBound(0.0,share,1.0):1.0);
            QColor fill=parts[i].colour; fill.setAlphaF(0.45);
            p->setBrush(fill);
            QPen edge(parts[i].colour); edge.setWidthF(1.2);
            p->setPen(edge);
            const QPointF at=centre+QPointF(offsets[i][0]*r,offsets[i][1]*r*0.8);
            p->drawEllipse(at,r*scale,r*scale);
            p->setPen(spec.style.foreground);
            p->drawText(QRectF(at.x()-r,at.y()-r*scale-16,2*r,14),Qt::AlignCenter,parts[i].label);
        }
    }else if(engine==QLatin1String("Word Cloud")||engine==QLatin1String("Bubble Cloud")){
        // Size by magnitude. Placement is a spiral with overlap rejection,
        // which is what every word cloud does and is honest about being
        // arbitrary: position carries no meaning here, only size does.
        QVector<QRectF> placed;
        const QPointF centre=area.center();
        for(const Part& part:parts){
            // Same guard as the Venn circles: this weight sets a font size
            // and a bubble radius, and NaN in either is undefined behaviour
            // inside QPainter rather than a visible mistake.
            const double biggest=parts.first().value;
            const double ratio=biggest>0.0?part.value/biggest:1.0;
            const double weight=finite(ratio)?qBound(0.0,ratio,1.0):1.0;
            const bool bubble=(engine==QLatin1String("Bubble Cloud"));
            QFont f=font(spec,spec.style.legendSize);
            f.setPointSizeF(qBound(7.0,spec.style.legendSize*(0.9+2.8*weight),34.0));
            f.setBold(weight>0.55);
            p->setFont(f);
            const QFontMetricsF fm(f,p->device());
            const double w=bubble?qMax(18.0,60.0*weight):fm.horizontalAdvance(part.label)+8.0;
            const double h=bubble?w:fm.height();
            QRectF box;
            bool ok=false;
            for(double t=0.0;t<64.0;t+=0.28){
                const double radius=6.0*t;
                const QPointF at=centre+QPointF(std::cos(t)*radius,std::sin(t)*radius*0.62);
                box=QRectF(at.x()-w/2.0,at.y()-h/2.0,w,h);
                if(!area.contains(box)) continue;
                bool clash=false;
                for(const QRectF& other:placed) if(box.intersects(other)){ clash=true; break; }
                if(!clash){ ok=true; break; }
            }
            if(!ok) continue;
            placed.append(box);
            if(bubble){
                QColor fill=part.colour; fill.setAlphaF(0.7);
                p->setBrush(fill); p->setPen(Qt::NoPen);
                p->drawEllipse(box);
                p->setPen(spec.style.foreground);
                p->drawText(box,Qt::AlignCenter,part.label);
            }else{
                p->setPen(part.colour);
                p->drawText(box,Qt::AlignCenter,part.label);
            }
        }
    }else{
        // Sankey. One stage, because a flat list of series is a split rather
        // than a network: a single source flowing into one band per part, with
        // the band's thickness the magnitude.
        const double leftX=area.left()+area.width()*0.14;
        const double rightX=area.right()-area.width()*0.18;
        double y=area.top();
        const double sourceTop=area.top();
        double sourceCursor=sourceTop;
        for(const Part& part:parts){
            const double thickness=area.height()*(part.value/grand)*0.92;
            QPainterPath band;
            const double midX=(leftX+rightX)/2.0;
            band.moveTo(leftX,sourceCursor);
            band.cubicTo(midX,sourceCursor,midX,y,rightX,y);
            band.lineTo(rightX,y+thickness);
            band.cubicTo(midX,y+thickness,midX,sourceCursor+thickness,leftX,sourceCursor+thickness);
            band.closeSubpath();
            QColor fill=part.colour; fill.setAlphaF(0.62);
            p->setBrush(fill); p->setPen(Qt::NoPen);
            p->drawPath(band);
            p->setPen(spec.style.foreground);
            p->drawText(QRectF(rightX+6,y+thickness/2.0-8,area.width()*0.17,16),
                        Qt::AlignLeft|Qt::AlignVCenter,part.label);
            sourceCursor+=thickness;
            y+=thickness+area.height()*0.02;
        }
        p->setBrush(spec.style.foreground);
        p->setPen(Qt::NoPen);
        p->drawRect(QRectF(leftX-10,sourceTop,10,sourceCursor-sourceTop));
    }

    drawFloatingTitle(p,target,spec);
    p->restore();
}

// ---------------------------------------------------------------------------
// 3-D fields: quiver, cones, stream tubes and ribbons, tensor glyphs, and the
// volume family.
//
// Six mapped columns for a vector field - x, y, z and the three components -
// or four for a scalar volume. Everything is projected by the same orthographic
// transform as the other 3-D engines and painted back to front, because a glyph
// drawn out of depth order reads as a field pointing the wrong way.
//
// VTK remains the right tool for a rotatable, lit, million-cell volume. This is
// the version that exports as vectors and does not need a plugin.
void QtPlotBackend::draw3DField(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    // The field colour map, read once. Viridis unless the style asks for
    // another - see PlotStyle::colourMap.
    const ColourMapKind cmap=colourMapFor(spec.style.colourMap);
    const int columns=spec.series.size();
    if(columns<4) return;
    const QVector<double>& xs=spec.series.at(0).y;
    const QVector<double>& ys=spec.series.at(1).y;
    const QVector<double>& zs=spec.series.at(2).y;
    const int n=qMin(xs.size(),qMin(ys.size(),zs.size()));
    if(n<2) return;

    const bool haveVector=(columns>=6);
    const QVector<double>& us=spec.series.at(qMin(3,columns-1)).y;
    const QVector<double>& vs=spec.series.at(qMin(4,columns-1)).y;
    const QVector<double>& ws=spec.series.at(qMin(5,columns-1)).y;
    // With only four columns the fourth is a scalar - the volume family - and
    // there is no direction to draw, only a magnitude.
    const QVector<double>& scalar=spec.series.at(3).y;

    const Bounds bx=boundsOf(xs), by=boundsOf(ys), bz=boundsOf(zs);
    if(!bx.valid||!by.valid||!bz.valid) return;

    const Projection proj=makeProjection(target,-35.0,24.0);

    p->save();
    drawBoundingCube(p,proj,spec.style.gridColor);

    const QString& engine=spec.engine;
    const bool tensor=engine==QLatin1String("Tensor Glyph Field");
    const bool cones=engine==QLatin1String("Cone Plot");
    const bool tubes=engine==QLatin1String("Stream Tube")
                   ||engine==QLatin1String("Stream Ribbon");
    const bool volume=engine.startsWith(QLatin1String("Volume"))
                    ||engine.startsWith(QLatin1String("Iso"))
                    ||engine==QLatin1String("Contour Slice");

    // Magnitude for colour and length, from whichever channels exist.
    double magMax=0.0;
    QVector<double> magnitude(n,0.0);
    for(int i=0;i<n;++i){
        if(haveVector&&i<us.size()&&i<vs.size()&&i<ws.size()
           &&finite(us[i])&&finite(vs[i])&&finite(ws[i])){
            magnitude[i]=std::sqrt(us[i]*us[i]+vs[i]*vs[i]+ws[i]*ws[i]);
        }else if(i<scalar.size()&&finite(scalar[i])){
            magnitude[i]=std::abs(scalar[i]);
        }
        magMax=qMax(magMax,magnitude[i]);
    }
    if(!(magMax>0.0)) magMax=1.0;

    // Depth-sorted, painter's algorithm. Without it a glyph behind another is
    // drawn over it and the field reads inside out.
    struct Glyph { double depth; int index; QPointF at; };
    QVector<Glyph> glyphs;
    glyphs.reserve(n);
    // A field of ten thousand arrows is a grey rectangle. Thin to a readable
    // count, evenly through the record so the sampling is not biased to one end.
    const int budget=volume?2200:900;
    const int stride=qMax(1,n/budget);
    for(int i=0;i<n;i+=stride){
        if(!finite(xs[i])||!finite(ys[i])||!finite(zs[i])) continue;
        double d=0;
        const QPointF at=project(proj,bx.norm(xs[i]),by.norm(ys[i]),bz.norm(zs[i]),&d);
        glyphs.append({d,i,at});
    }
    std::sort(glyphs.begin(),glyphs.end(),
              [](const Glyph& a,const Glyph& b){ return a.depth<b.depth; });

    for(const Glyph& g:glyphs){
        const int i=g.index;
        const double t=qBound(0.0,magnitude[i]/magMax,1.0);
        const QColor colour=colourMap(cmap,t);

        if(volume){
            // Points shaded and sized by the scalar, with the low end faded
            // out: an isosurface without the surface, which is honest about
            // being a projection of samples rather than a reconstructed mesh.
            QColor fill=colour;
            fill.setAlphaF(0.25+0.65*t);
            p->setPen(Qt::NoPen); p->setBrush(fill);
            const double r=qMax(0.9,2.2+3.4*t);
            p->drawEllipse(g.at,r,r);
            continue;
        }

        if(!haveVector) continue;
        const double u=us.value(i), v=vs.value(i), w=ws.value(i);
        if(!finite(u)||!finite(v)||!finite(w)) continue;
        const double mag=magnitude[i];
        if(!(mag>0.0)) continue;

        // The vector's own tip, projected: the direction on screen is the
        // projection of the direction in space, not an angle guessed in 2-D.
        const QPointF tip=project(proj,
                                  bx.norm(xs[i])+u/magMax*0.16,
                                  by.norm(ys[i])+v/magMax*0.16,
                                  bz.norm(zs[i])+w/magMax*0.16);
        const QPointF along=tip-g.at;
        const double len=std::hypot(along.x(),along.y());
        if(!(len>0.4)) continue;
        const QPointF dir=along/len;
        const QPointF normal(-dir.y(),dir.x());

        if(tensor){
            // A tensor glyph shows magnitude and orientation together: an
            // ellipse whose long axis is the vector and whose short axis is
            // what is left of the magnitude across it.
            p->save();
            QColor fill=colour; fill.setAlphaF(0.7);
            p->setBrush(fill);
            p->setPen(QPen(colour.darker(130),0.4));
            p->translate(g.at);
            p->rotate(std::atan2(dir.y(),dir.x())*180.0/3.14159265358979323846);
            p->drawEllipse(QPointF(0,0),qMax(1.2,len*0.5),qMax(0.7,len*0.22));
            p->restore();
        }else if(cones){
            QPolygonF cone;
            cone<<tip
                <<(g.at+normal*qMax(1.0,len*0.28))
                <<(g.at-normal*qMax(1.0,len*0.28));
            p->setPen(Qt::NoPen); p->setBrush(colour);
            p->drawPolygon(cone);
        }else if(tubes){
            // A tube in projection is a thick line; a ribbon is a flat one. The
            // width carries the magnitude either way.
            QPen pen(colour);
            pen.setWidthF(qMax(0.8,(engine==QLatin1String("Stream Tube"))?2.0+3.0*t:1.0+1.4*t));
            pen.setCapStyle(Qt::RoundCap);
            p->setPen(pen);
            p->drawLine(g.at,tip);
        }else{
            // 3D Quiver.
            QPen pen(colour);
            pen.setWidthF(qMax(0.5,spec.style.lineWidth));
            p->setPen(pen);
            p->drawLine(g.at,tip);
            const double head=qMin(5.0,len*0.34);
            p->drawLine(tip,tip-dir*head+normal*head*0.5);
            p->drawLine(tip,tip-dir*head-normal*head*0.5);
        }
    }

    drawFloatingTitle(p,target,spec);
    p->restore();
}

bool QtPlotBackend::engineHasAxes(const QString& engine){
    // Polar joins pie and donut: a rectangular frame would be meaningless
    // chrome around a radial projection.
    return engine!=QLatin1String("Pie")
        && engine!=QLatin1String("Donut")
        && engine!=QLatin1String("Polar Line")
        && engine!=QLatin1String("Polar Scatter")
        // 3-D draws its own projected cube; a 2-D frame around it would be
        // chrome that means nothing.
        && engine!=QLatin1String("3D Line")
        && engine!=QLatin1String("3D Scatter")
        && engine!=QLatin1String("3D Topography / Surface")
        && engine!=QLatin1String("3D Mesh")
        && !engine.startsWith(QLatin1String("3D "))
        && engine!=QLatin1String("Surface + Contours")
        && engine!=QLatin1String("Comet 3D")
        && engine!=QLatin1String("Ribbon")
        && engine!=QLatin1String("Polar Histogram")
        && engine!=QLatin1String("Wind Rose")
        && engine!=QLatin1String("Radar Chart")
        && engine!=QLatin1String("Compass")
        && engine!=QLatin1String("Polar Bubble")
        && engine!=QLatin1String("Treemap")
        && engine!=QLatin1String("Sunburst")
        && engine!=QLatin1String("Venn Diagram")
        && engine!=QLatin1String("Word Cloud")
        && engine!=QLatin1String("Bubble Cloud")
        && engine!=QLatin1String("Sankey Diagram")
        // A force-directed layout and a ring of arcs are both positions this
        // backend invented, not coordinates the data has. An axis around
        // either would be chrome with numbers on it that mean nothing.
        && engine!=QLatin1String("Network Graph")
        && engine!=QLatin1String("Chord Diagram")
        && engine!=QLatin1String("Smith Chart")
        && engine!=QLatin1String("Mosaic Plot")
        && engine!=QLatin1String("UpSet Plot")
        && engine!=QLatin1String("3D Quiver")
        && engine!=QLatin1String("Cone Plot")
        && engine!=QLatin1String("Stream Tube")
        && engine!=QLatin1String("Stream Ribbon")
        && engine!=QLatin1String("Tensor Glyph Field")
        && engine!=QLatin1String("Volume Show")
        && engine!=QLatin1String("Volume Slice")
        && engine!=QLatin1String("Isosurface")
        && engine!=QLatin1String("Isonormals")
        && engine!=QLatin1String("Isocaps")
        && engine!=QLatin1String("Contour Slice");
}

// Histogram and Box Plot describe the distribution of the mapped column rather
// than plotting it against X, so they are rewritten into drawable geometry
// before the axes are computed.
// ------------------------------------------------------------ prepared cache
namespace {
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
} // namespace

quint64 QtPlotBackend::specFingerprint(const PlotSpec& spec){
    quint64 h=0xcbf29ce484222325ULL;
    fnvString(h,spec.engine);
    fnvString(h,spec.variant);
    fnvString(h,spec.expression);
    fnvString(h,spec.title);
    for(const PlotAxis* a:{&spec.xAxis,&spec.yAxis}){
        fnvString(h,a->label);
        const unsigned char lg=(a->log10?1:0)|(a->inverted?2:0); fnvBytes(h,&lg,1);
        // Deliberately NOT the limits. No rewrite reads them - the only code
        // that touches them copies them onto the result - and they now change
        // on every frame of a pan or a pinch. Hashing them meant dragging a
        // violin plot re-derived the violin sixty times a second.
    }
    for(const PlotSeries& s:spec.series){
        fnvString(h,s.label);
        fnvColour(h,s.color);
        fnvDouble(h,s.lineWidth);
        fnvDouble(h,s.markerSize);
        fnvDouble(h,s.opacity);
        const unsigned char flags=(s.drawLine?1:0)|(s.drawMarkers?2:0);
        fnvBytes(h,&flags,1);
        const qsizetype nx=s.x.size(), ny=s.y.size();
        fnvBytes(h,&nx,sizeof(nx)); fnvBytes(h,&ny,sizeof(ny));
        // The bulk of the work, and a straight linear scan of memory that
        // render() is about to copy anyway.
        for(double v:s.x) fnvDouble(h,v);
        for(double v:s.y) fnvDouble(h,v);
        for(qreal v:s.dashPattern) fnvDouble(h,double(v));
    }
    // Only the style fields a rewrite in prepareSpec can read. Font sizes and
    // grid visibility are drawing decisions and must not invalidate the cache.
    fnvColour(h,spec.style.positive);
    fnvColour(h,spec.style.warning);
    fnvColour(h,spec.style.danger);
    fnvColour(h,spec.style.foreground);
    fnvColour(h,spec.style.background);
    return h;
}

namespace {
// The caller's limits and notes, put back on top of a rewrite that set its own.
//
// Both are deliberately absent from specFingerprint, for the same reason: no
// rewrite in prepareSpec reads either, and both change while someone is
// interacting. Hashing the limits meant dragging a violin plot re-derived the
// violin sixty times a second; hashing the notes would mean re-deriving it on
// every keystroke while a note is being typed.
//
// This is not an optimisation, it is a correctness fix. Without the second
// line a note added to a figure whose data had not changed hit the cache,
// which had been filled before the note existed, and simply did not appear -
// and one that was deleted stayed on screen.
PlotSpec& applyLimits(PlotSpec& out,const PlotSpec& in){
    if(!isUnset(in.xAxis.min)) out.xAxis.min=in.xAxis.min;
    if(!isUnset(in.xAxis.max)) out.xAxis.max=in.xAxis.max;
    if(!isUnset(in.yAxis.min)) out.yAxis.min=in.yAxis.min;
    if(!isUnset(in.yAxis.max)) out.yAxis.max=in.yAxis.max;
    out.annotations=in.annotations;
    return out;
}
} // namespace

const PlotSpec& QtPlotBackend::preparedCached(const PlotSpec& in) const {
    int seriesCount=in.series.size();
    qsizetype pointCount=0;
    for(const PlotSeries& s:in.series) pointCount+=s.x.size()+s.y.size();
    const quint64 hash=specFingerprint(in);

    if(prepCache_.valid
       && prepCache_.hash==hash
       && prepCache_.seriesCount==seriesCount
       && prepCache_.pointCount==pointCount
       && prepCache_.engine==in.engine
       && prepCache_.variant==in.variant
       && prepCache_.expression==in.expression)
        return applyLimits(prepCache_.prepared,in);

    // The core, not the wrapper: the wrapper's only job is to put the caller's
    // limits back on top, and that is done below on every call, hit or miss.
    prepCache_.prepared=prepareSpecCore(in);
    prepCache_.engine=in.engine;
    prepCache_.variant=in.variant;
    prepCache_.expression=in.expression;
    prepCache_.seriesCount=seriesCount;
    prepCache_.pointCount=pointCount;
    prepCache_.hash=hash;
    prepCache_.valid=true;
    return applyLimits(prepCache_.prepared,in);
}

namespace {
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
QVector<int> douglasPeucker(const QVector<double>& xs,const QVector<double>& ys,
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

InterpolationKind interpolationFor(const QString& variant){
    const QString v=variant.trimmed();
    if(v.compare(QLatin1String("Linear"),Qt::CaseInsensitive)==0)  return InterpolationKind::Linear;
    if(v.compare(QLatin1String("Cubic Spline"),Qt::CaseInsensitive)==0
     ||v.compare(QLatin1String("Spline"),Qt::CaseInsensitive)==0)  return InterpolationKind::CubicSpline;
    if(v.compare(QLatin1String("Cosine"),Qt::CaseInsensitive)==0)  return InterpolationKind::Cosine;
    if(v.compare(QLatin1String("Nearest"),Qt::CaseInsensitive)==0
     ||v.compare(QLatin1String("Step"),Qt::CaseInsensitive)==0)    return InterpolationKind::Nearest;
    return InterpolationKind::Akima;
}

QString interpolationName(InterpolationKind k){
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
int intervalFor(const QVector<double>& xs,double x){
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
QVector<double> naturalSplineM(const QVector<double>& xs,const QVector<double>& ys){
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
QVector<double> akimaSlopes(const QVector<double>& xs,const QVector<double>& ys){
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

Interpolator interpolatorFor(InterpolationKind kind,const QVector<double>& xs,const QVector<double>& ys){
    Interpolator it; it.kind=kind;
    if(kind==InterpolationKind::CubicSpline) it.m=naturalSplineM(xs,ys);
    else if(kind==InterpolationKind::Akima)  it.t=akimaSlopes(xs,ys);
    return it;
}

double interpolateAt(const Interpolator& it,const QVector<double>& xs,const QVector<double>& ys,double x){
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
PlotSpec derivedAs(const PlotSpec& in,const QString& engine){
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

LineFit fitLine(const QVector<double>& x,const QVector<double>& y,int upTo=-1){
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

MapProjection mapProjectionFor(const QString& variant){
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

GeoFrame geoFrameFor(MapProjection kind,const QVector<double>& lon,const QVector<double>& lat){
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
QPointF utmForward(double lonDeg,double latDeg,int zone,bool northern){
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
QPointF projectPoint(const GeoFrame& f,double lonDeg,double latDeg){
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

QString projectionXLabel(const GeoFrame& f){
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

QString projectionYLabel(const GeoFrame& f){
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
double greatCircleMetres(double lon1,double lat1,double lon2,double lat2){
    const double p1=lat1*kDegToRad,p2=lat2*kDegToRad;
    const double dp=(lat2-lat1)*kDegToRad,dl=(lon2-lon1)*kDegToRad;
    const double a=std::sin(dp/2)*std::sin(dp/2)
                  +std::cos(p1)*std::cos(p2)*std::sin(dl/2)*std::sin(dl/2);
    return 2.0*kEarthRadius*std::asin(std::sqrt(qBound(0.0,a,1.0)));
}

// A point a fraction of the way along the great circle between two others.
// Spherical interpolation on the unit vectors, which stays well behaved when
// the two ends are nearly coincident and when they are nearly antipodal.
void greatCirclePoint(double lon1,double lat1,double lon2,double lat2,
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

} // namespace

// A caller's axis limits outrank the engine's own.
//
// Many of the rewrites below set out.xAxis/yAxis themselves, because a
// histogram is drawn against counts and a correlation matrix against variable
// indices - ranges the mapped columns know nothing about. Those assignments are
// unconditional, so once the canvas gained an interactive zoom, every one of
// those engines would have thrown the user's zoom away on the next repaint and
// snapped back to the full range. The limits that arrive on the spec are the
// ones the user is looking at, and they win.
PlotSpec QtPlotBackend::prepareSpec(const PlotSpec& in) const {
    PlotSpec out=prepareSpecCore(in);
    return applyLimits(out,in);
}

PlotSpec QtPlotBackend::prepareSpecCore(const PlotSpec& in) const {
    // ----------------------------------------------------------------- ECDF
    // The empirical distribution: every observation contributes one step of
    // 1/n. Drawn as a staircase because that is what it is - joining the points
    // with straight lines would claim values between observations that were
    // never measured.
    if(in.engine==QLatin1String("ECDF")){
        PlotSpec out=derivedAs(in,QStringLiteral("Stairs"));
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<2) continue;
            std::sort(v.begin(),v.end());
            PlotSeries step;
            step.label=s.label; step.color=s.color; step.dashPattern=s.dashPattern;
            step.lineWidth=s.lineWidth; step.drawLine=true; step.drawMarkers=false;
            for(int i=0;i<v.size();++i){
                step.x.append(v[i]);
                step.y.append(double(i+1)/double(v.size()));
            }
            out.series.append(step);
        }
        out.xAxis=PlotAxis{in.yAxis.label.isEmpty()?QStringLiteral("value"):in.yAxis.label,
                           in.yAxis.log10,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("cumulative fraction"),false,0.0,1.0};
        return out;
    }

    // ------------------------------------------------------------ Q-Q Plot
    // Sample quantiles against the normal quantiles they would have if the
    // sample were normal. The 45-degree line is the hypothesis; departure from
    // it is the finding, so it is drawn rather than left to be imagined.
    if(in.engine==QLatin1String("Q-Q Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        double lo=std::numeric_limits<double>::infinity(),hi=-lo;
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<3) continue;
            std::sort(v.begin(),v.end());
            const double mean=meanOf(v), sd=stdevOf(v);
            PlotSeries pts;
            pts.label=s.label; pts.color=s.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            for(int i=0;i<v.size();++i){
                // Blom's plotting position: the standard choice for a normal
                // probability plot, and unbiased for the expected order
                // statistic in a way that (i+1)/n is not.
                const double p=(double(i)+1.0-0.375)/(double(v.size())+0.25);
                const double theoretical=normalQuantile(p);
                if(!finite(theoretical)) continue;
                pts.x.append(theoretical);
                pts.y.append(sd>0.0?(v[i]-mean)/sd:0.0);
                lo=qMin(lo,qMin(theoretical,pts.y.last()));
                hi=qMax(hi,qMax(theoretical,pts.y.last()));
            }
            out.series.append(pts);
        }
        if(finite(lo)&&finite(hi)&&hi>lo){
            PlotSeries diagonal;
            diagonal.label=QStringLiteral("normal");
            diagonal.x={lo,hi}; diagonal.y={lo,hi};
            diagonal.color=in.style.gridColor;
            diagonal.lineWidth=0.9; diagonal.drawLine=true; diagonal.drawMarkers=false;
            diagonal.dashPattern={5,4};
            out.series.append(diagonal);
        }
        out.xAxis=PlotAxis{QStringLiteral("theoretical quantiles"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("standardised sample quantiles"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------- KDE Density
    // A Gaussian kernel density estimate, bandwidth by Silverman's rule of
    // thumb. A histogram's shape depends on where the bin edges happen to fall;
    // this does not, which is the whole reason to prefer it.
    if(in.engine==QLatin1String("KDE Density")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<3) continue;
            std::sort(v.begin(),v.end());
            const KdeCurve kde=kdeCurve(v,256,3.0);
            if(!kde.valid) continue;
            PlotSeries curve;
            curve.label=s.label; curve.color=s.color; curve.dashPattern=s.dashPattern;
            curve.lineWidth=qMax(1.2,s.lineWidth);
            curve.drawLine=true; curve.drawMarkers=false;
            curve.x=kde.pos;
            curve.y=kde.density;
            out.series.append(curve);
        }
        out.xAxis=PlotAxis{in.yAxis.label.isEmpty()?QStringLiteral("value"):in.yAxis.label,
                           false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("density"),false,0.0,unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Bland-Altman
    // Agreement between two methods measuring the same thing: the difference
    // against the mean, with the bias and the 95% limits of agreement drawn.
    // Needs exactly two mapped columns, because that is what the method is.
    if(in.engine==QLatin1String("Bland-Altman")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const PlotSeries& a=in.series.at(0);
            const PlotSeries& b=in.series.at(1);
            const int n=qMin(a.y.size(),b.y.size());
            PlotSeries pts;
            pts.label=QStringLiteral("%1 vs %2").arg(a.label,b.label);
            pts.color=a.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            QVector<double> diffs;
            double xLo=std::numeric_limits<double>::infinity(),xHi=-xLo;
            for(int i=0;i<n;++i){
                if(!finite(a.y[i])||!finite(b.y[i])) continue;
                const double mean=(a.y[i]+b.y[i])/2.0;
                const double diff=a.y[i]-b.y[i];
                pts.x.append(mean); pts.y.append(diff);
                diffs.append(diff);
                xLo=qMin(xLo,mean); xHi=qMax(xHi,mean);
            }
            if(!diffs.isEmpty()){
                out.series.append(pts);
                const double bias=meanOf(diffs), sd=stdevOf(diffs);
                out.series.append(horizontalRule(bias,xLo,xHi,in.style.foreground,
                                                 QStringLiteral("bias"),false));
                out.series.append(horizontalRule(bias+1.96*sd,xLo,xHi,in.style.gridColor,
                                                 QStringLiteral("+1.96 SD"),true));
                out.series.append(horizontalRule(bias-1.96*sd,xLo,xHi,in.style.gridColor,
                                                 QStringLiteral("-1.96 SD"),true));
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("mean of the two methods"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("difference"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ----------------------------------------------------- Control Chart
    // An individuals chart: the series in order, with the centre line and the
    // three-sigma limits. Sigma is estimated from the mean moving range rather
    // than from the standard deviation of all the points, because a process
    // that has already shifted would inflate the latter and hide the shift the
    // chart exists to reveal.
    if(in.engine==QLatin1String("Control Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<4) continue;
            PlotSeries line;
            line.label=s.label; line.color=s.color;
            line.lineWidth=qMax(1.1,s.lineWidth);
            line.drawLine=true; line.drawMarkers=true; line.markerSize=3.2;
            for(int i=0;i<v.size();++i){ line.x.append(double(i+1)); line.y.append(v[i]); }
            out.series.append(line);

            double movingRange=0.0;
            for(int i=1;i<v.size();++i) movingRange+=std::abs(v[i]-v[i-1]);
            movingRange/=double(v.size()-1);
            // d2 for a moving range of two consecutive observations.
            const double sigma=movingRange/1.128;
            const double centre=meanOf(v);
            const double xLo=1.0, xHi=double(v.size());
            out.series.append(horizontalRule(centre,xLo,xHi,in.style.foreground,
                                             QStringLiteral("centre"),false));
            out.series.append(horizontalRule(centre+3.0*sigma,xLo,xHi,in.style.gridColor,
                                             QStringLiteral("UCL"),true));
            out.series.append(horizontalRule(centre-3.0*sigma,xLo,xHi,in.style.gridColor,
                                             QStringLiteral("LCL"),true));
            break;   // one chart, one characteristic
        }
        out.xAxis=PlotAxis{QStringLiteral("observation"),false,unsetValue(),unsetValue()};
        if(out.yAxis.label.isEmpty()&&!in.series.isEmpty()) out.yAxis.label=in.series.first().label;
        return out;
    }

    // -------------------------------------------------------- ROC Curve
    // Two mapped columns: a score and a binary label. Sweeping the threshold
    // down the sorted scores traces the curve, and the diagonal is the
    // no-information line a classifier has to beat to be worth anything.
    if(in.engine==QLatin1String("ROC Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const PlotSeries& score=in.series.at(0);
            const PlotSeries& truth=in.series.at(1);
            const int n=qMin(score.y.size(),truth.y.size());
            QVector<QPair<double,bool>> rows;
            rows.reserve(n);
            // Anything above the midpoint of the label column counts as
            // positive, so 0/1, 1/2 and true/false all behave.
            double labelLo=std::numeric_limits<double>::infinity(),labelHi=-labelLo;
            for(int i=0;i<n;++i){
                if(!finite(truth.y[i])) continue;
                labelLo=qMin(labelLo,truth.y[i]); labelHi=qMax(labelHi,truth.y[i]);
            }
            const double cut=(finite(labelLo)&&finite(labelHi))?(labelLo+labelHi)/2.0:0.5;
            for(int i=0;i<n;++i){
                if(!finite(score.y[i])||!finite(truth.y[i])) continue;
                rows.append({score.y[i],truth.y[i]>cut});
            }
            std::sort(rows.begin(),rows.end(),[](const QPair<double,bool>& a,const QPair<double,bool>& b){
                return a.first>b.first;
            });
            int positives=0,negatives=0;
            for(const auto& r:rows){ if(r.second) ++positives; else ++negatives; }
            if(positives>0&&negatives>0){
                PlotSeries curve;
                curve.label=QStringLiteral("ROC");
                curve.color=score.color;
                curve.lineWidth=qMax(1.4,score.lineWidth);
                curve.drawLine=true; curve.drawMarkers=false;
                int tp=0,fp=0;
                curve.x.append(0.0); curve.y.append(0.0);
                double auc=0.0; double prevFpr=0.0,prevTpr=0.0;
                for(const auto& r:rows){
                    if(r.second) ++tp; else ++fp;
                    const double fpr=double(fp)/double(negatives);
                    const double tpr=double(tp)/double(positives);
                    auc+=(fpr-prevFpr)*(tpr+prevTpr)/2.0;
                    prevFpr=fpr; prevTpr=tpr;
                    curve.x.append(fpr); curve.y.append(tpr);
                }
                curve.label=QStringLiteral("ROC (AUC %1)").arg(auc,0,'f',3);
                out.series.append(curve);

                PlotSeries chance;
                chance.label=QStringLiteral("chance");
                chance.x={0.0,1.0}; chance.y={0.0,1.0};
                chance.color=in.style.gridColor;
                chance.lineWidth=0.9; chance.drawLine=true; chance.drawMarkers=false;
                chance.dashPattern={5,4};
                out.series.append(chance);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("false positive rate"),false,0.0,1.0};
        out.yAxis=PlotAxis{QStringLiteral("true positive rate"),false,0.0,1.0};
        return out;
    }

    // ------------------------------------------------------ Volcano Plot
    // Effect size against significance. Two mapped columns: a fold change and
    // a p-value. Significance is plotted as -log10(p) so the interesting points
    // are at the top, and the two threshold rules are drawn because a volcano
    // plot without them is just a scatter.
    if(in.engine==QLatin1String("Volcano Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const PlotSeries& effect=in.series.at(0);
            const PlotSeries& pvalue=in.series.at(1);
            const int n=qMin(effect.y.size(),pvalue.y.size());
            PlotSeries below,above;
            below.label=QStringLiteral("not significant");
            below.color=in.style.gridColor;
            below.drawLine=false; below.drawMarkers=true; below.markerSize=3.4;
            above.label=QStringLiteral("significant");
            above.color=effect.color;
            above.drawLine=false; above.drawMarkers=true; above.markerSize=3.8;
            double xLo=std::numeric_limits<double>::infinity(),xHi=-xLo;
            for(int i=0;i<n;++i){
                const double x=effect.y[i];
                double p=pvalue.y[i];
                if(!finite(x)||!finite(p)) continue;
                // A p-value of exactly zero is a rounding artefact of whatever
                // produced it, not a certainty; clamp rather than plot infinity.
                p=qBound(1e-300,p,1.0);
                const double y=-std::log10(p);
                xLo=qMin(xLo,x); xHi=qMax(xHi,x);
                const bool significant=(p<0.05)&&(std::abs(x)>1.0);
                PlotSeries& target=significant?above:below;
                target.x.append(x); target.y.append(y);
            }
            if(!below.x.isEmpty()) out.series.append(below);
            if(!above.x.isEmpty()) out.series.append(above);
            if(finite(xLo)&&finite(xHi)){
                out.series.append(horizontalRule(-std::log10(0.05),xLo,xHi,in.style.gridColor,
                                                 QStringLiteral("p = 0.05"),true));
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("log2 fold change"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("-log10 p"),false,0.0,unsetValue()};
        return out;
    }

    // ------------------------------------------------------- Pareto Front
    // Every solution as a point, with the non-dominated set joined. Assumes
    // both objectives are being minimised, which is the convention the rest of
    // the optimisation literature uses; a maximised objective is negated by the
    // person who mapped it.
    if(in.engine==QLatin1String("Pareto Front")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const PlotSeries& f1=in.series.at(0);
            const PlotSeries& f2=in.series.at(1);
            const int n=qMin(f1.y.size(),f2.y.size());
            QVector<QPointF> points;
            points.reserve(n);
            for(int i=0;i<n;++i)
                if(finite(f1.y[i])&&finite(f2.y[i])) points.append(QPointF(f1.y[i],f2.y[i]));

            PlotSeries all;
            all.label=QStringLiteral("solutions");
            all.color=in.style.gridColor;
            all.drawLine=false; all.drawMarkers=true; all.markerSize=3.2;
            for(const QPointF& pt:points){ all.x.append(pt.x()); all.y.append(pt.y()); }
            if(!all.x.isEmpty()) out.series.append(all);

            // Sort by the first objective, then sweep: a point is on the front
            // if nothing seen so far was already better on the second.
            QVector<QPointF> sorted=points;
            std::sort(sorted.begin(),sorted.end(),[](const QPointF& a,const QPointF& b){
                return a.x()!=b.x()? a.x()<b.x() : a.y()<b.y();
            });
            PlotSeries front;
            front.label=QStringLiteral("Pareto front");
            front.color=f1.color;
            front.lineWidth=qMax(1.5,f1.lineWidth);
            front.drawLine=true; front.drawMarkers=true; front.markerSize=4.2;
            double best=std::numeric_limits<double>::infinity();
            for(const QPointF& pt:sorted){
                if(pt.y()<best){
                    best=pt.y();
                    front.x.append(pt.x()); front.y.append(pt.y());
                }
            }
            if(!front.x.isEmpty()) out.series.append(front);
            out.xAxis.label=f1.label;
            out.yAxis.label=f2.label;
        }
        return out;
    }

    // -------------------------------------------------- Performance Ceiling
    // The upper envelope of what has actually been achieved: points binned
    // along x, the best value in each bin joined. It answers "how good has
    // anything been at this setting", which a scatter alone does not.
    if(in.engine==QLatin1String("Performance Ceiling")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<4) continue;
            PlotSeries cloud;
            cloud.label=s.label;
            cloud.color=in.style.gridColor;
            cloud.drawLine=false; cloud.drawMarkers=true; cloud.markerSize=3.0;
            double lo=std::numeric_limits<double>::infinity(),hi=-lo;
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                cloud.x.append(s.x[i]); cloud.y.append(s.y[i]);
                lo=qMin(lo,s.x[i]); hi=qMax(hi,s.x[i]);
            }
            if(cloud.x.isEmpty()||!(hi>lo)) continue;
            out.series.append(cloud);

            const int bins=qBound(8,int(std::sqrt(double(cloud.x.size()))*1.5),40);
            QVector<double> best(bins,-std::numeric_limits<double>::infinity());
            for(int i=0;i<cloud.x.size();++i){
                const int b=qBound(0,int((cloud.x[i]-lo)/(hi-lo)*bins),bins-1);
                best[b]=qMax(best[b],cloud.y[i]);
            }
            PlotSeries ceiling;
            ceiling.label=QStringLiteral("ceiling");
            ceiling.color=s.color;
            ceiling.lineWidth=qMax(1.5,s.lineWidth);
            ceiling.drawLine=true; ceiling.drawMarkers=false;
            for(int b=0;b<bins;++b){
                if(!finite(best[b])) continue;   // an empty bin is not a zero
                ceiling.x.append(lo+(hi-lo)*(double(b)+0.5)/double(bins));
                ceiling.y.append(best[b]);
            }
            if(ceiling.x.size()>=2) out.series.append(ceiling);
            break;
        }
        return out;
    }

    // ---------------------------------------------------- Global Sensitivity
    // One bar per input, sorted by influence. Horizontal because the labels are
    // variable names and reading those rotated is a needless tax.
    if(in.engine==QLatin1String("Global Sensitivity")){
        PlotSpec out=derivedAs(in,QStringLiteral("Horizontal Bar"));
        out.legendVisible=false;
        QVector<QPair<double,PlotSeries>> ranked;
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.isEmpty()) continue;
            // The index is whatever the mapped column holds; if a whole column
            // was mapped rather than a single figure, its mean is the summary.
            ranked.append({std::abs(meanOf(v)),s});
        }
        std::sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){ return a.first<b.first; });
        int slot=1;
        for(const auto& entry:ranked){
            PlotSeries bar;
            bar.label=entry.second.label;
            bar.color=entry.second.color;
            // Horizontal Bar reads the value from x and the category row from
            // y. These were the other way round, which is what put every bar
            // in one band and scaled its length against the wrong axis.
            bar.x={entry.first};
            bar.y={double(slot++)};
            bar.drawLine=false;
            out.series.append(bar);
        }
        out.xAxis=PlotAxis{QStringLiteral("sensitivity index"),false,0.0,unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("input"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------ Gompertz H2 Kinetics
    // The modified Gompertz equation is the standard model for cumulative
    // biogas from a batch fermentation:
    //
    //     H(t) = P * exp(-exp(Rm*e/P * (lambda - t) + 1))
    //
    // with P the ultimate yield, Rm the maximum production rate and lambda the
    // lag before production starts. Those three numbers are usually the point
    // of the experiment, so they are fitted here and put in the legend rather
    // than left for the reader to estimate off the curve.
    //
    // Fitted natively by Gauss-Newton on the residuals. The science add-on has
    // a fit.modified_gompertz op that does the same thing with scipy, but a
    // graph that will not draw without an optional Python install is not much
    // of a graph.
    if(in.engine==QLatin1String("Gompertz H₂ Kinetics")
       ||in.engine==QLatin1String("Gompertz H2 Kinetics")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            QVector<double> t,h;
            for(int i=0;i<n;++i)
                if(finite(s.x[i])&&finite(s.y[i])){ t.append(s.x[i]); h.append(s.y[i]); }
            if(t.size()<4) continue;

            PlotSeries measured;
            measured.label=s.label.isEmpty()?QStringLiteral("measured"):s.label;
            measured.color=s.color;
            measured.drawLine=false; measured.drawMarkers=true; measured.markerSize=4.2;
            measured.x=t; measured.y=h;
            out.series.append(measured);

            // Starting values taken from the data itself: the plateau, the
            // steepest observed slope, and the first time anything happened.
            double P=*std::max_element(h.begin(),h.end());
            if(!(P>0.0)) continue;
            double Rm=0.0;
            for(int i=1;i<t.size();++i){
                const double dt=t[i]-t[i-1];
                if(dt>0.0) Rm=qMax(Rm,(h[i]-h[i-1])/dt);
            }
            if(!(Rm>0.0)) Rm=P/qMax(1e-9,t.last()-t.first());
            double lag=t.first();
            for(int i=0;i<h.size();++i){ if(h[i]>0.05*P){ lag=t[i]; break; } }

            auto model=[](double tt,double p,double rm,double lam){
                const double e=2.718281828459045;
                const double inner=rm*e/qMax(1e-12,p)*(lam-tt)+1.0;
                // exp of a large positive number overflows to inf; the model
                // value there is zero, which is what the clamp expresses.
                if(inner>700.0) return 0.0;
                return p*std::exp(-std::exp(inner));
            };

            // Gauss-Newton with a numerical Jacobian and a damping term. Thirty
            // iterations is far more than this three-parameter problem needs and
            // still costs nothing at plot time.
            for(int iter=0;iter<30;++iter){
                double JtJ[3][3]={{0,0,0},{0,0,0},{0,0,0}};
                double Jtr[3]={0,0,0};
                const double step[3]={qMax(1e-6,P*1e-4),qMax(1e-6,Rm*1e-4),1e-4};
                double params[3]={P,Rm,lag};
                for(int i=0;i<t.size();++i){
                    const double residual=h[i]-model(t[i],P,Rm,lag);
                    double J[3];
                    for(int k=0;k<3;++k){
                        double bumped[3]={params[0],params[1],params[2]};
                        bumped[k]+=step[k];
                        J[k]=(model(t[i],bumped[0],bumped[1],bumped[2])
                              -model(t[i],P,Rm,lag))/step[k];
                    }
                    for(int a=0;a<3;++a){
                        Jtr[a]+=J[a]*residual;
                        for(int b=0;b<3;++b) JtJ[a][b]+=J[a]*J[b];
                    }
                }
                for(int k=0;k<3;++k) JtJ[k][k]*=1.0+1e-3;   // Levenberg damping

                // 3x3 solve by Cramer's rule; the matrix is tiny and symmetric.
                const double det=
                    JtJ[0][0]*(JtJ[1][1]*JtJ[2][2]-JtJ[1][2]*JtJ[2][1])
                   -JtJ[0][1]*(JtJ[1][0]*JtJ[2][2]-JtJ[1][2]*JtJ[2][0])
                   +JtJ[0][2]*(JtJ[1][0]*JtJ[2][1]-JtJ[1][1]*JtJ[2][0]);
                if(std::abs(det)<1e-18) break;
                auto solve=[&](int col){
                    double m[3][3];
                    for(int r=0;r<3;++r) for(int c=0;c<3;++c) m[r][c]=(c==col)?Jtr[r]:JtJ[r][c];
                    return (m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])
                           -m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])
                           +m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]))/det;
                };
                const double dP=solve(0), dRm=solve(1), dLag=solve(2);
                if(!finite(dP)||!finite(dRm)||!finite(dLag)) break;
                P+=dP; Rm+=dRm; lag+=dLag;
                P=qMax(1e-9,P); Rm=qMax(1e-12,Rm);
                if(std::abs(dP)<1e-9*qMax(1.0,P)&&std::abs(dRm)<1e-9*qMax(1.0,Rm)
                   &&std::abs(dLag)<1e-9) break;
            }

            PlotSeries fit;
            fit.label=QStringLiteral("P %1  ·  Rm %2  ·  λ %3")
                          .arg(P,0,'g',3).arg(Rm,0,'g',3).arg(lag,0,'g',3);
            fit.color=s.color;
            fit.lineWidth=qMax(1.6,s.lineWidth);
            fit.drawLine=true; fit.drawMarkers=false;
            const double t0=t.first(), t1=t.last();
            for(int g=0;g<200;++g){
                const double tt=t0+(t1-t0)*double(g)/199.0;
                fit.x.append(tt);
                fit.y.append(model(tt,P,Rm,lag));
            }
            out.series.append(fit);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("time");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("cumulative H₂");
        return out;
    }

    // ------------------------------------------- sCOD Degradation Profile
    // Substrate concentration against time, with removal as a percentage of the
    // starting value on the same picture. Removal is what gets reported, and
    // deriving it here means the two can never disagree.
    if(in.engine==QLatin1String("sCOD Degradation Profile")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<2) continue;
            PlotSeries conc;
            conc.label=s.label.isEmpty()?QStringLiteral("sCOD"):s.label;
            conc.color=s.color;
            conc.lineWidth=qMax(1.4,s.lineWidth);
            conc.drawLine=true; conc.drawMarkers=true; conc.markerSize=3.6;
            double first=std::numeric_limits<double>::quiet_NaN();
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                if(!finite(first)) first=s.y[i];
                conc.x.append(s.x[i]); conc.y.append(s.y[i]);
            }
            if(conc.x.size()<2) continue;
            out.series.append(conc);

            if(finite(first)&&std::abs(first)>1e-12){
                PlotSeries removal;
                removal.label=QStringLiteral("%1 removal (%)").arg(conc.label);
                removal.color=s.color;
                removal.lineWidth=1.0;
                removal.drawLine=true; removal.drawMarkers=false;
                removal.dashPattern={5,4};
                for(int i=0;i<conc.x.size();++i){
                    removal.x.append(conc.x[i]);
                    removal.y.append((first-conc.y[i])/first*100.0);
                }
                out.series.append(removal);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("time");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("sCOD / removal %");
        return out;
    }

    // ------------------------------------------- VFA Concentration Profile
    // Volatile fatty acids accumulate and are consumed in sequence, so what
    // matters is the composition over time rather than any one species. Stacked
    // so the total is readable as the top of the band.
    if(in.engine==QLatin1String("VFA Concentration Profile")){
        PlotSpec out=in;
        out.engine=QStringLiteral("Stacked Lines");
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("time");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("VFA concentration");
        return out;
    }

    // --------------------------------------------- Polarisation & Power Curve
    // The standard electrochemical characterisation: voltage falling with
    // current, and the power it implies. Power is computed rather than mapped,
    // because P = IV is not something the reader should have to take on trust
    // from a separate column that may or may not have been kept in step.
    if(in.engine==QLatin1String("Polarisation & Power Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<2) continue;
            PlotSeries voltage;
            voltage.label=s.label.isEmpty()?QStringLiteral("voltage"):s.label;
            voltage.color=s.color;
            voltage.lineWidth=qMax(1.4,s.lineWidth);
            voltage.drawLine=true; voltage.drawMarkers=true; voltage.markerSize=3.6;
            PlotSeries power;
            power.label=QStringLiteral("power");
            power.color=s.color;
            power.lineWidth=1.1;
            power.drawLine=true; power.drawMarkers=false;
            power.dashPattern={5,4};
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                voltage.x.append(s.x[i]); voltage.y.append(s.y[i]);
                power.x.append(s.x[i]);   power.y.append(s.x[i]*s.y[i]);
            }
            if(voltage.x.size()<2) continue;
            out.series.append(voltage);
            out.series.append(power);
            break;   // one cell, one curve pair
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("current density");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("voltage / power");
        return out;
    }

    // ------------------------------------------------------- EIS: Nyquist
    // Impedance in the complex plane: -Z″ against Z′, so a capacitive
    // semicircle appears above the axis where everyone expects to read it.
    // Two mapped columns, real and imaginary.
    if(in.engine==QLatin1String("EIS: Nyquist")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const PlotSeries& re=in.series.at(0);
            const PlotSeries& im=in.series.at(1);
            const int n=qMin(re.y.size(),im.y.size());
            PlotSeries arc;
            arc.label=QStringLiteral("impedance");
            arc.color=re.color;
            arc.lineWidth=qMax(1.4,re.lineWidth);
            arc.drawLine=true; arc.drawMarkers=true; arc.markerSize=3.4;
            for(int i=0;i<n;++i){
                if(!finite(re.y[i])||!finite(im.y[i])) continue;
                arc.x.append(re.y[i]);
                // Negated by convention. If the column already holds -Z″ this
                // would flip it, so the sign of the majority decides.
                arc.y.append(im.y[i]);
            }
            int negative=0;
            for(double v:arc.y) if(v<0) ++negative;
            if(negative>arc.y.size()/2) for(double& v:arc.y) v=-v;
            if(!arc.x.isEmpty()) out.series.append(arc);
        }
        out.xAxis=PlotAxis{QStringLiteral("Z′ (real)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("−Z″ (imaginary)"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ---------------------------------------------------------- EIS: Bode
    // Magnitude and phase against frequency, on a log frequency axis because
    // impedance spectra span decades and a linear axis wastes most of the plot
    // on the top one.
    if(in.engine==QLatin1String("EIS: Bode")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const PlotSeries& freq=in.series.at(0);
            const int n=freq.y.size();
            for(int k=1;k<in.series.size();++k){
                const PlotSeries& channel=in.series.at(k);
                PlotSeries curve;
                curve.label=channel.label;
                curve.color=channel.color;
                curve.dashPattern=channel.dashPattern;
                curve.lineWidth=qMax(1.3,channel.lineWidth);
                curve.drawLine=true; curve.drawMarkers=true; curve.markerSize=3.2;
                const int m=qMin(n,channel.y.size());
                for(int i=0;i<m;++i){
                    if(!finite(freq.y[i])||!finite(channel.y[i])||freq.y[i]<=0) continue;
                    curve.x.append(freq.y[i]);
                    curve.y.append(channel.y[i]);
                }
                if(!curve.x.isEmpty()) out.series.append(curve);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("frequency"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("|Z| / phase"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------ 1D Marginal Responses
    // How the output moves as each input is swept on its own. The inputs have
    // different units and ranges, so each response is scaled to 0-1 over its own
    // sweep; the shape is the finding, not the magnitude.
    if(in.engine==QLatin1String("1D Marginal Responses")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<2) continue;
            double lo=std::numeric_limits<double>::infinity(),hi=-lo;
            for(int i=0;i<n;++i) if(finite(s.y[i])){ lo=qMin(lo,s.y[i]); hi=qMax(hi,s.y[i]); }
            if(!finite(lo)||!finite(hi)) continue;
            const double span=hi-lo;
            PlotSeries curve;
            curve.label=s.label;
            curve.color=s.color; curve.dashPattern=s.dashPattern;
            curve.lineWidth=qMax(1.3,s.lineWidth);
            curve.drawLine=true; curve.drawMarkers=false;
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                curve.x.append(s.x[i]);
                // A flat response is drawn at the midline rather than dividing
                // by zero and vanishing.
                curve.y.append(span>0.0?(s.y[i]-lo)/span:0.5);
            }
            out.series.append(curve);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("input");
        out.yAxis=PlotAxis{QStringLiteral("scaled response"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------------------- Swarm
    // Every observation shown, displaced sideways only as far as it must be to
    // stop points overlapping. Unlike a jitter plot the offset is deterministic,
    // so the same data draws the same picture every time - which matters when
    // the picture goes in a paper.
    if(in.engine==QLatin1String("Swarm")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        out.legendVisible=in.series.size()>1;
        int slot=1;
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.isEmpty()){ ++slot; continue; }
            std::sort(v.begin(),v.end());
            PlotSeries pts;
            pts.label=s.label; pts.color=s.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;

            // Points close in value share a row and are fanned out around the
            // slot centre. The tolerance is a fraction of the whole spread, so
            // it scales with the data rather than assuming units.
            const double tolerance=(v.last()-v.first())*0.012;
            int i=0;
            while(i<v.size()){
                int j=i+1;
                while(j<v.size()&&tolerance>0.0&&(v[j]-v[i])<=tolerance) ++j;
                const int count=j-i;
                for(int k=0;k<count;++k){
                    const double offset=(count==1)?0.0
                        :((double(k)-(count-1)/2.0)/qMax(1.0,double(count-1)))*0.34;
                    pts.x.append(double(slot)+offset);
                    pts.y.append(v[i+k]);
                }
                i=j;
            }
            out.series.append(pts);
            ++slot;
        }
        out.xAxis=PlotAxis{QStringLiteral("group"),false,0.4,double(slot)-0.4};
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("value");
        return out;
    }

    // ------------------------------------------------------- Violin Plot
    // The distribution's shape rather than its summary. Packed for drawViolin
    // as: x[0] = the slot, then the sample positions; y[0] unused, then the
    // density at each position. Carrying both in the existing two vectors keeps
    // PlotSeries from growing a field that only one engine would ever use.
    if(in.engine==QLatin1String("Violin Plot")||in.engine==QLatin1String("Raincloud")){
        PlotSpec out=in;
        out.series.clear();
        out.legendVisible=in.series.size()>1;
        int slot=1;
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<5){ ++slot; continue; }
            std::sort(v.begin(),v.end());
            // No padding: a violin is drawn across the observed range, not
            // three bandwidths past either end of it.
            const KdeCurve kde=kdeCurve(v,96,0.0);
            if(!kde.valid){ ++slot; continue; }

            PlotSeries violin;
            violin.label=s.label;
            violin.color=s.color;
            violin.lineWidth=qMax(0.8,s.lineWidth);
            violin.markerSize=s.markerSize;
            // The slot marker first, then the density profile - the packing
            // drawViolin reads.
            violin.x.append(double(slot));
            violin.y.append(0.0);
            violin.x+=kde.pos;
            violin.y+=kde.density;
            out.series.append(violin);
            ++slot;
        }
        out.xAxis=PlotAxis{QStringLiteral("group"),false,0.4,double(slot)-0.4};
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("value");
        return out;
    }

    // ------------------------------------------------------- Forest Plot
    // One row per estimate. Three mapped columns - estimate, lower, upper - or
    // one column, in which case the interval is the standard error of the mean
    // and the plot still says something rather than refusing to draw.
    if(in.engine==QLatin1String("Forest Plot")){
        PlotSpec out=in;
        out.series.clear();
        out.legendVisible=false;
        int row=1;
        if(in.series.size()>=3){
            const PlotSeries& est=in.series.at(0);
            const PlotSeries& lo=in.series.at(1);
            const PlotSeries& hi=in.series.at(2);
            const int n=qMin(est.y.size(),qMin(lo.y.size(),hi.y.size()));
            for(int i=0;i<n&&i<40;++i){
                if(!finite(est.y[i])||!finite(lo.y[i])||!finite(hi.y[i])) continue;
                PlotSeries entry;
                entry.label=QStringLiteral("%1").arg(i+1);
                entry.color=est.color;
                entry.x={double(row++)};
                entry.y={est.y[i],lo.y[i],hi.y[i]};
                entry.markerSize=6.0;
                out.series.append(entry);
            }
        }else{
            for(const PlotSeries& s:in.series){
                QVector<double> v=finiteValues(s);
                if(v.size()<2) continue;
                const double mean=meanOf(v);
                const double se=stdevOf(v)/std::sqrt(double(v.size()));
                PlotSeries entry;
                entry.label=s.label;
                entry.color=s.color;
                entry.x={double(row++)};
                entry.y={mean,mean-1.96*se,mean+1.96*se};
                entry.markerSize=6.0;
                out.series.append(entry);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("effect");
        out.yAxis=PlotAxis{QStringLiteral("study"),false,0.4,double(row)-0.4};
        return out;
    }

    // --------------------------------------------------- Correlation Matrix
    // Every pair of mapped columns, as Pearson r on a grid. Packed as the three
    // columns the field engines read: x index, y index, value.
    if(in.engine==QLatin1String("Correlation Matrix")){
        PlotSpec out=in;
        out.series.clear();
        out.legendVisible=false;
        const int k=in.series.size();
        if(k>=2){
            QVector<QVector<double>> cols;
            QStringList names;
            for(const PlotSeries& s:in.series){ cols.append(s.y); names.append(s.label); }
            PlotSeries xi,yi,vi;
            for(int a=0;a<k;++a){
                for(int b=0;b<k;++b){
                    const Moments2 m=momentsOf(cols[a],cols[b]);
                    if(m.n<2) continue;
                    xi.y.append(double(a)); yi.y.append(double(b)); vi.y.append(pearson(m));
                }
            }
            xi.label=QStringLiteral("i"); yi.label=QStringLiteral("j"); vi.label=QStringLiteral("r");
            out.series={xi,yi,vi};
            out.xAxis=PlotAxis{names.join(QStringLiteral(", ")),false,-0.5,double(k)-0.5};
            out.yAxis=PlotAxis{QStringLiteral("variable"),false,-0.5,double(k)-0.5};
        }
        return out;
    }

    // -------------------------------- 2D Heatmap / 2D Histogram / Hexbin
    // The heatmap family reads x, y and a value from the first three mapped
    // columns. A 2-D histogram has no value column - the count IS the value -
    // so one is synthesised, which is also what makes hexbin density work.
    if(in.engine==QLatin1String("2D Histogram")||in.engine==QLatin1String("Hexbin Density")){
        PlotSpec out=in;
        out.legendVisible=false;
        if(in.series.size()>=2){
            PlotSeries ones;
            ones.label=QStringLiteral("count");
            ones.y.fill(1.0,qMin(in.series.at(0).y.size(),in.series.at(1).y.size()));
            out.series={in.series.at(0),in.series.at(1),ones};
            out.xAxis.label=in.series.at(0).label;
            out.yAxis.label=in.series.at(1).label;
        }
        return out;
    }
    if(in.engine==QLatin1String("2D Heatmap")||in.engine==QLatin1String("2D Contour")){
        PlotSpec out=in;
        out.legendVisible=false;
        if(in.series.size()>=2){
            out.xAxis.label=in.series.at(0).label;
            out.yAxis.label=in.series.at(1).label;
        }
        return out;
    }

    // ------------------------------------------------- Polar Line / Scatter
    // Angle in degrees against radius. The projection happens in drawPolar;
    // this only names the axes so the legend and any export read correctly.
    if(in.engine==QLatin1String("Polar Line")||in.engine==QLatin1String("Polar Scatter")){
        PlotSpec out=in;
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("angle (deg)");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("radius");
        return out;
    }

    // ---------------------------------------------------------------------
    // Signal transforms.
    //
    // Each of these is one operation applied to every mapped series, then drawn
    // as a line or a scatter. Expressing them as a table of transforms rather
    // than as twenty near-identical if-blocks keeps the differences visible:
    // what actually distinguishes a rolling mean from a rolling median is one
    // line, and it should look like one line.
    {
        enum class Transform { None, Derivative, Integral, CumulativeSum, RollingMean,
                               RollingMedian, MovingStd, Autocorrelation, Lag,
                               CumulativeHistogram };
        Transform how=Transform::None;
        QString yLabel;
        QString xLabel;
        QString drawAs=QStringLiteral("Line Chart");

        if(in.engine==QLatin1String("Derivative")){        how=Transform::Derivative;       yLabel=QStringLiteral("d/dx"); }
        else if(in.engine==QLatin1String("Integral")){     how=Transform::Integral;         yLabel=QStringLiteral("cumulative integral"); }
        else if(in.engine==QLatin1String("Cumulative Sum")){how=Transform::CumulativeSum;   yLabel=QStringLiteral("cumulative sum"); }
        else if(in.engine==QLatin1String("Rolling Mean")){ how=Transform::RollingMean;      yLabel=QStringLiteral("rolling mean"); }
        else if(in.engine==QLatin1String("Rolling Median")){how=Transform::RollingMedian;   yLabel=QStringLiteral("rolling median"); }
        else if(in.engine==QLatin1String("Moving Std")){   how=Transform::MovingStd;        yLabel=QStringLiteral("moving std"); }
        else if(in.engine==QLatin1String("Autocorrelation")){how=Transform::Autocorrelation;
            yLabel=QStringLiteral("autocorrelation"); xLabel=QStringLiteral("lag"); drawAs=QStringLiteral("Stem"); }
        else if(in.engine==QLatin1String("Lag Plot")){     how=Transform::Lag;
            yLabel=QStringLiteral("value at t"); xLabel=QStringLiteral("value at t−1");
            drawAs=QStringLiteral("4D / 5D Scatter"); }
        else if(in.engine==QLatin1String("Cumulative Histogram")){how=Transform::CumulativeHistogram;
            yLabel=QStringLiteral("cumulative count"); drawAs=QStringLiteral("Stairs"); }

        if(how!=Transform::None){
            PlotSpec out=derivedAs(in,drawAs);
            // A window that scales with the series: too small and a rolling
            // statistic is the original with noise, too large and it is a
            // straight line. A twentieth of the sample is the usual compromise.
            for(const PlotSeries& s:in.series){
                const int n=qMin(s.x.size(),s.y.size());
                if(n<3) continue;
                const int window=qBound(3,n/20,201);
                PlotSeries out_s;
                out_s.label=s.label; out_s.color=s.color; out_s.dashPattern=s.dashPattern;
                out_s.lineWidth=qMax(1.2,s.lineWidth);
                out_s.drawLine=(drawAs!=QLatin1String("4D / 5D Scatter"));
                out_s.drawMarkers=(drawAs==QLatin1String("4D / 5D Scatter"));
                out_s.markerSize=3.8;

                switch(how){
                case Transform::Derivative:
                    // Central differences inside, one-sided at the ends: a
                    // forward difference throughout would shift the whole curve
                    // half a sample to the left.
                    for(int i=0;i<n;++i){
                        const int a=qMax(0,i-1), b=qMin(n-1,i+1);
                        const double dx=s.x[b]-s.x[a];
                        if(!finite(dx)||std::abs(dx)<1e-15) continue;
                        out_s.x.append(s.x[i]);
                        out_s.y.append((s.y[b]-s.y[a])/dx);
                    }
                    break;
                case Transform::Integral: {
                    // Trapezoidal, which is exact for the straight segments the
                    // line chart is already drawing between the samples.
                    double acc=0.0;
                    for(int i=0;i<n;++i){
                        if(i>0&&finite(s.y[i])&&finite(s.y[i-1]))
                            acc+=(s.y[i]+s.y[i-1])/2.0*(s.x[i]-s.x[i-1]);
                        out_s.x.append(s.x[i]); out_s.y.append(acc);
                    }
                    break;
                }
                case Transform::CumulativeSum: {
                    double acc=0.0;
                    for(int i=0;i<n;++i){
                        if(finite(s.y[i])) acc+=s.y[i];
                        out_s.x.append(s.x[i]); out_s.y.append(acc);
                    }
                    break;
                }
                case Transform::RollingMean:
                case Transform::RollingMedian:
                case Transform::MovingStd: {
                    for(int i=0;i<n;++i){
                        const int lo=qMax(0,i-window/2), hi=qMin(n-1,i+window/2);
                        QVector<double> w;
                        for(int k=lo;k<=hi;++k) if(finite(s.y[k])) w.append(s.y[k]);
                        if(w.isEmpty()) continue;
                        double value=0.0;
                        if(how==Transform::RollingMean) value=meanOf(w);
                        else if(how==Transform::MovingStd) value=stdevOf(w);
                        else { std::sort(w.begin(),w.end()); value=quantileOf(w,0.5); }
                        out_s.x.append(s.x[i]); out_s.y.append(value);
                    }
                    break;
                }
                case Transform::Autocorrelation: {
                    QVector<double> v=finiteValues(s);
                    if(v.size()<8) break;
                    const double m=meanOf(v);
                    double denom=0.0;
                    for(double x:v) denom+=(x-m)*(x-m);
                    if(!(denom>0.0)) break;
                    const int maxLag=qMin(v.size()/2,60);
                    for(int lag=0;lag<=maxLag;++lag){
                        double acc=0.0;
                        for(int i=lag;i<v.size();++i) acc+=(v[i]-m)*(v[i-lag]-m);
                        out_s.x.append(double(lag));
                        out_s.y.append(acc/denom);
                    }
                    break;
                }
                case Transform::Lag: {
                    QVector<double> v=finiteValues(s);
                    for(int i=1;i<v.size();++i){ out_s.x.append(v[i-1]); out_s.y.append(v[i]); }
                    break;
                }
                case Transform::CumulativeHistogram: {
                    QVector<double> v=finiteValues(s);
                    if(v.size()<2) break;
                    std::sort(v.begin(),v.end());
                    for(int i=0;i<v.size();++i){ out_s.x.append(v[i]); out_s.y.append(double(i+1)); }
                    break;
                }
                default: break;
                }
                if(out_s.x.size()>=2) out.series.append(out_s);
            }
            if(!xLabel.isEmpty()) out.xAxis=PlotAxis{xLabel,false,unsetValue(),unsetValue()};
            out.yAxis=PlotAxis{yLabel,false,unsetValue(),unsetValue()};
            return out;
        }
    }

    // ------------------------------------------- marker and shape variants
    // These differ from an engine already ported only in how the same points
    // are marked. Saying so here is honest and one line each; giving each its
    // own draw function would be four copies of the same loop.
    if(in.engine==QLatin1String("Dot Plot")||in.engine==QLatin1String("Strip Plot")
       ||in.engine==QLatin1String("Beeswarm")){
        PlotSpec out=in;
        // Beeswarm is the deterministic fan-out already written for Swarm;
        // strip and dot plots are the same points without it.
        out.engine=(in.engine==QLatin1String("Beeswarm"))
                       ? QStringLiteral("Swarm") : QStringLiteral("4D / 5D Scatter");
        if(out.engine==QLatin1String("Swarm")){
            PlotSpec relabelled=out;
            relabelled.engine=QStringLiteral("Swarm");
            return prepareSpec(relabelled);
        }
        for(PlotSeries& s:out.series){ s.drawLine=false; s.drawMarkers=true; s.markerSize=4.2; }
        return out;
    }
    // ------------------------------------------------------- Data Reduction
    //
    // The same curve with fewer points in it. A logger that wrote a sample a
    // second for a week is two hundred thousand points describing a shape that
    // needs two hundred, and the extra ones cost redraw time, file size and -
    // once the line is thicker than the spacing between them - legibility.
    //
    // Both curves are drawn: the original faint underneath, the reduced one on
    // top, and the legend says how many points each has. A reduction you cannot
    // see the error of is a reduction you should not trust, and the whole point
    // of the picture is to let the tolerance be judged by eye.
    //
    // The tolerance is a fraction of the data's own bounding diagonal rather
    // than an absolute number, because the alternative is a figure that empties
    // itself the moment the y axis is in millivolts instead of volts. Measured
    // on three periods of a noisy sine at 2,000 points, on an 800x550 plot:
    //
    //     variant     fraction   points kept   worst error on screen
    //     "Fine"        0.05%        25.4%          0.5 px
    //     default       0.25%         3.1%          2.4 px
    //     "Coarse"      1.0%          1.0%          8.9 px
    //
    // The default keeps a thirtieth of the points and moves the line by under
    // three pixels, which is why it is the default. That error is measured
    // PERPENDICULAR to the curve rather than vertically, which is also how the
    // eye reads it: on a steep segment the same two pixels amount to a large
    // change in y and are invisible.
    if(in.engine==QLatin1String("Data Reduction")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        const QString v=in.variant.trimmed();
        const double fraction=(v.compare(QLatin1String("Fine"),Qt::CaseInsensitive)==0)   ? 0.0005
                             :(v.compare(QLatin1String("Coarse"),Qt::CaseInsensitive)==0) ? 0.01
                                                                                          : 0.0025;
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            QVector<double> xs,ys;
            xs.reserve(n); ys.reserve(n);
            for(int i=0;i<n;++i)
                if(finite(s.x[i])&&finite(s.y[i])){ xs.append(s.x[i]); ys.append(s.y[i]); }
            if(xs.size()<3) continue;

            double xLo=xs[0],xHi=xs[0],yLo=ys[0],yHi=ys[0];
            for(int i=1;i<xs.size();++i){
                xLo=qMin(xLo,xs[i]); xHi=qMax(xHi,xs[i]);
                yLo=qMin(yLo,ys[i]); yHi=qMax(yHi,ys[i]);
            }
            const double dx=xHi-xLo, dy=yHi-yLo;
            const double diagonal=std::sqrt(dx*dx+dy*dy);
            if(!(diagonal>0.0)) continue;
            // Perpendicular distance is measured in the plot's own units, so x
            // and y are put on comparable footing first - otherwise a series in
            // seconds against microvolts is reduced entirely along one axis.
            const double sx=(dx>0.0)?1.0/dx:0.0, sy=(dy>0.0)?1.0/dy:0.0;
            const double tolerance=fraction*std::sqrt(2.0);

            const QVector<int> keep=douglasPeucker(xs,ys,sx,sy,tolerance);

            PlotSeries original=s;
            original.x=xs; original.y=ys;
            original.drawLine=true; original.drawMarkers=false;
            original.lineWidth=qMax(0.7,s.lineWidth*0.6);
            // Neutral, not the series colour: the whole picture is a
            // comparison, and two curves in the same hue with only a width
            // between them is not one. The original recedes; the reduced curve
            // keeps the series identity and is drawn over it.
            original.color=in.style.foreground;
            original.color.setAlphaF(0.45f);
            original.label=QStringLiteral("%1 — %2 points").arg(s.label).arg(xs.size());

            PlotSeries reduced;
            reduced.color=s.color.isValid()?s.color:in.style.warning;
            reduced.drawLine=true; reduced.drawMarkers=keep.size()<=200;
            reduced.markerSize=3.0;
            reduced.lineWidth=qMax(1.4,s.lineWidth);
            reduced.label=QStringLiteral("reduced — %1 points (%2%)")
                              .arg(keep.size())
                              .arg(100.0*double(keep.size())/double(xs.size()),0,'f',1);
            for(int i:keep){ reduced.x.append(xs[i]); reduced.y.append(ys[i]); }

            out.series.append(original);
            out.series.append(reduced);
        }
        out.xAxis=in.xAxis;
        out.yAxis=in.yAxis;
        return out;
    }

    // -------------------------------------------------------- Interpolation
    //
    // A curve through the points, evaluated on a fine grid, so a handful of
    // measurements can be read at values nobody measured. The variant picks the
    // method, and the methods differ in ways that matter:
    //
    //   "Linear"        straight between neighbours. Never overshoots, never
    //                   smooth. The safe answer.
    //   "Cubic Spline"  natural cubic spline: the smoothest curve through the
    //                   points, second derivative continuous. It CAN overshoot
    //                   - a step in the data becomes a ring around the step -
    //                   and that overshoot is real information about the fit,
    //                   not an artefact to hide.
    //   "Akima"         (the default) local, so a step stays a step: the
    //                   coefficients depend on two neighbours each side rather
    //                   than on the whole series. Much less ringing than a
    //                   spline; slightly less smooth.
    //   "Cosine"        smooth between each pair with no dependence beyond
    //                   them. Cheap, monotone between neighbours.
    //   "Nearest"       piecewise constant. For a categorical or quantised
    //                   quantity, where a value between two samples is a lie.
    //
    // The source points are drawn on top as markers, because the one question
    // asked of an interpolation is where it passed through what was measured.
    if(in.engine==QLatin1String("Interpolation")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        const InterpolationKind how=interpolationFor(in.variant);
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<2) continue;
            // Sorted and de-duplicated by x: every method here needs a
            // single-valued function of x, and a logger that repeats a
            // timestamp would otherwise divide by a zero interval.
            QVector<QPair<double,double>> pairs;
            pairs.reserve(n);
            for(int i=0;i<n;++i)
                if(finite(s.x[i])&&finite(s.y[i])) pairs.append({s.x[i],s.y[i]});
            std::sort(pairs.begin(),pairs.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){return a.first<b.first;});
            QVector<double> xs,ys;
            for(const auto& pr:pairs){
                if(!xs.isEmpty()&&!(pr.first>xs.last())){
                    // Repeated x: average, rather than pick one arbitrarily.
                    ys.last()=(ys.last()+pr.second)/2.0;
                    continue;
                }
                xs.append(pr.first); ys.append(pr.second);
            }
            if(xs.size()<2) continue;

            // Enough samples to look continuous at any figure width, and no
            // more: a thousand points is already finer than the pixels.
            const int grid=qBound(200,xs.size()*8,1000);
            PlotSeries curve;
            curve.color=s.color; curve.drawLine=true; curve.drawMarkers=false;
            curve.lineWidth=qMax(1.3,s.lineWidth);
            curve.label=QStringLiteral("%1 — %2").arg(s.label,interpolationName(how));
            // Coefficients once per series, not once per evaluated point: the
            // spline solve is O(n) and the grid is a thousand samples, so doing
            // it inside the loop would be a thousand solves for one curve.
            const Interpolator it=interpolatorFor(how,xs,ys);
            const double x0=xs.first(), x1=xs.last();
            for(int i=0;i<grid;++i){
                const double x=x0+(x1-x0)*double(i)/double(grid-1);
                const double y=interpolateAt(it,xs,ys,x);
                if(!finite(y)) continue;
                curve.x.append(x); curve.y.append(y);
            }

            PlotSeries knots;
            knots.x=xs; knots.y=ys;
            knots.color=s.color; knots.drawLine=false; knots.drawMarkers=true;
            knots.markerSize=4.5;
            knots.label=QStringLiteral("%1 — measured").arg(s.label);

            if(curve.x.size()>=2) out.series.append(curve);
            out.series.append(knots);
        }
        out.xAxis=in.xAxis;
        out.yAxis=in.yAxis;
        return out;
    }

    if(in.engine==QLatin1String("Connected Scatter")){
        PlotSpec out=in;
        out.engine=QStringLiteral("Line Chart");
        for(PlotSeries& s:out.series){ s.drawLine=true; s.drawMarkers=true; s.markerSize=4.0; }
        return out;
    }
    if(in.engine==QLatin1String("Lollipop")){
        PlotSpec out=in;
        out.engine=QStringLiteral("Stem");
        for(PlotSeries& s:out.series){ s.drawMarkers=true; s.markerSize=5.0; }
        return out;
    }
    if(in.engine==QLatin1String("Step Mid")){
        PlotSpec out=in;
        // The step changes half way between samples rather than at one, which
        // is the honest reading when a value is a measurement at a point rather
        // than a level that held until the next one.
        out.engine=QStringLiteral("Stairs");
        out.series.clear();
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<2) continue;
            PlotSeries mid=s;
            mid.x.clear(); mid.y.clear();
            for(int i=0;i<n;++i){
                const double half=(i==0)?(s.x[1]-s.x[0])/2.0:(s.x[i]-s.x[i-1])/2.0;
                mid.x.append(s.x[i]-half);
                mid.y.append(s.y[i]);
            }
            mid.x.append(s.x[n-1]+(s.x[n-1]-s.x[n-2])/2.0);
            mid.y.append(s.y[n-1]);
            out.series.append(mid);
        }
        return out;
    }
    if(in.engine==QLatin1String("Fill Between")){
        PlotSpec out=in;
        out.engine=QStringLiteral("Area");
        return out;
    }
    if(in.engine==QLatin1String("Event Plot")){
        PlotSpec out=in;
        // One raster row per series: every event a tick at its own height, which
        // is how spike trains and log events are read.
        out.engine=QStringLiteral("4D / 5D Scatter");
        out.series.clear();
        int row=1;
        for(const PlotSeries& s:in.series){
            PlotSeries ticks;
            ticks.label=s.label; ticks.color=s.color;
            ticks.drawLine=false; ticks.drawMarkers=true; ticks.markerSize=3.0;
            for(int i=0;i<s.x.size();++i){
                if(!finite(s.x[i])) continue;
                ticks.x.append(s.x[i]); ticks.y.append(double(row));
            }
            if(!ticks.x.isEmpty()) out.series.append(ticks);
            ++row;
        }
        out.yAxis=PlotAxis{QStringLiteral("series"),false,0.4,double(row)-0.4};
        return out;
    }

    // ----------------------------------------------- regression diagnostics
    // Residual, calibration and probability plots all compare a measurement to
    // what a model says it should be, and all three want the reference line
    // drawn or they are just scatters.
    if(in.engine==QLatin1String("Residual Plot")||in.engine==QLatin1String("Calibration Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const PlotSeries& observed=in.series.at(0);
            const PlotSeries& predicted=in.series.at(1);
            const int n=qMin(observed.y.size(),predicted.y.size());
            const bool residual=(in.engine==QLatin1String("Residual Plot"));
            PlotSeries pts;
            pts.label=residual?QStringLiteral("residual"):QStringLiteral("observed vs predicted");
            pts.color=observed.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            double lo=std::numeric_limits<double>::infinity(),hi=-lo;
            for(int i=0;i<n;++i){
                if(!finite(observed.y[i])||!finite(predicted.y[i])) continue;
                pts.x.append(predicted.y[i]);
                pts.y.append(residual?(observed.y[i]-predicted.y[i]):observed.y[i]);
                lo=qMin(lo,qMin(predicted.y[i],pts.y.last()));
                hi=qMax(hi,qMax(predicted.y[i],pts.y.last()));
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                if(residual){
                    out.series.append(horizontalRule(0.0,lo,hi,in.style.foreground,
                                                     QStringLiteral("zero"),false));
                }else if(finite(lo)&&finite(hi)&&hi>lo){
                    PlotSeries ideal;
                    ideal.label=QStringLiteral("perfect agreement");
                    ideal.x={lo,hi}; ideal.y={lo,hi};
                    ideal.color=in.style.gridColor;
                    ideal.lineWidth=0.9; ideal.drawLine=true; ideal.dashPattern={5,4};
                    out.series.append(ideal);
                }
            }
            out.xAxis=PlotAxis{QStringLiteral("predicted"),false,unsetValue(),unsetValue()};
            out.yAxis=PlotAxis{residual?QStringLiteral("residual"):QStringLiteral("observed"),
                               false,unsetValue(),unsetValue()};
        }
        return out;
    }
    if(in.engine==QLatin1String("Probability Plot")){
        // Same construction as the Q-Q plot already ported; the name differs by
        // discipline, not by method.
        PlotSpec relabelled=in;
        relabelled.engine=QStringLiteral("Q-Q Plot");
        return prepareSpec(relabelled);
    }
    if(in.engine==QLatin1String("Manhattan Plot")){
        // Genome-wide significance: -log10(p) along an ordinal axis, with the
        // 5e-8 threshold that the field uses drawn on.
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        for(const PlotSeries& s:in.series){
            PlotSeries pts;
            pts.label=s.label; pts.color=s.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=3.0;
            const int n=qMin(s.x.size(),s.y.size());
            for(int i=0;i<n;++i){
                if(!finite(s.y[i])) continue;
                const double p=qBound(1e-300,s.y[i],1.0);
                pts.x.append(finite(s.x[i])?s.x[i]:double(i));
                pts.y.append(-std::log10(p));
            }
            if(!pts.x.isEmpty()) out.series.append(pts);
        }
        if(!out.series.isEmpty()){
            double lo=out.series.first().x.first(), hi=lo;
            for(const PlotSeries& s:out.series)
                for(double x:s.x){ lo=qMin(lo,x); hi=qMax(hi,x); }
            out.series.append(horizontalRule(-std::log10(5e-8),lo,hi,in.style.gridColor,
                                             QStringLiteral("genome-wide 5e-8"),true));
        }
        out.yAxis=PlotAxis{QStringLiteral("-log10 p"),false,0.0,unsetValue()};
        return out;
    }

    // --------------------------------------------------------- 3-D variants
    // All of these are the projected cube with a different mark on it, so they
    // are named here and drawn by draw3D rather than duplicating the
    // projection, the depth sorting and the bounding box four times over.
    if(in.engine==QLatin1String("3D Bar")||in.engine==QLatin1String("3D Horizontal Bar")
       ||in.engine==QLatin1String("3D Stem")||in.engine==QLatin1String("3D Bubble")
       ||in.engine==QLatin1String("3D Swarm")||in.engine==QLatin1String("Comet 3D")
       ||in.engine==QLatin1String("3D Contour")||in.engine==QLatin1String("Surface + Contours")
       ||in.engine==QLatin1String("Ribbon")){
        PlotSpec out=in;
        if(in.series.size()>=3){
            out.xAxis.label=in.series.at(0).label;
            out.yAxis.label=in.series.at(1).label;
        }
        return out;
    }

    // ------------------------------------------------------- polar variants
    // Angle-and-radius data with different marks and one genuine difference:
    // the histogram family bins the angles first, because a wind rose is a
    // distribution of directions rather than a list of them.
    if(in.engine==QLatin1String("Polar Histogram")||in.engine==QLatin1String("Wind Rose")){
        PlotSpec out=derivedAs(in,QStringLiteral("Polar Histogram"));
        constexpr int kSectors=16;
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            QVector<double> weight(kSectors,0.0);
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])) continue;
                // Degrees, wrapped, so a bearing of 370 lands with 10.
                double angle=std::fmod(s.x[i],360.0);
                if(angle<0) angle+=360.0;
                const int sector=qBound(0,int(angle/360.0*kSectors),kSectors-1);
                // A wind rose weights by magnitude; a plain polar histogram
                // counts. Both are "how much came from this direction".
                weight[sector]+=(in.engine==QLatin1String("Wind Rose")&&finite(s.y[i]))
                                    ? std::abs(s.y[i]) : 1.0;
            }
            PlotSeries sectors;
            sectors.label=s.label; sectors.color=s.color;
            for(int k=0;k<kSectors;++k){
                sectors.x.append((double(k)+0.5)*360.0/double(kSectors));
                sectors.y.append(weight[k]);
            }
            out.series.append(sectors);
        }
        out.xAxis=PlotAxis{QStringLiteral("direction (deg)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{in.engine==QLatin1String("Wind Rose")?QStringLiteral("magnitude")
                                                               :QStringLiteral("count"),
                           false,unsetValue(),unsetValue()};
        return out;
    }
    if(in.engine==QLatin1String("Radar Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("Radar Chart"));
        // One spoke per mapped column, one closed outline per row. Values are
        // scaled per axis, because a radar chart of raw values with different
        // units compares nothing.
        const int axes=in.series.size();
        if(axes>=3){
            QVector<Bounds> spans;
            for(const PlotSeries& s:in.series) spans.append(boundsOf(s.y));
            int rows=std::numeric_limits<int>::max();
            for(const PlotSeries& s:in.series) rows=qMin(rows,int(s.y.size()));
            rows=qMin(rows,8);          // more outlines than this is unreadable
            for(int r=0;r<rows;++r){
                PlotSeries outline;
                outline.label=QStringLiteral("row %1").arg(r+1);
                outline.color=in.series.at(r%axes).color;
                outline.lineWidth=1.4;
                for(int a=0;a<axes;++a){
                    outline.x.append(double(a)*360.0/double(axes));
                    outline.y.append(spans[a].scale(in.series.at(a).y.value(r)));
                }
                // Closed: the first spoke repeated, or the outline is a fan.
                outline.x.append(0.0);
                outline.y.append(spans[0].scale(in.series.at(0).y.value(r)));
                out.series.append(outline);
            }
        }
        out.yAxis=PlotAxis{QStringLiteral("scaled value"),false,0.0,1.0};
        return out;
    }
    if(in.engine==QLatin1String("Compass")||in.engine==QLatin1String("Polar Bubble")){
        PlotSpec out=in;
        // Compass draws each observation as a spoke from the origin; polar
        // bubble marks it. Both are the polar projection with a different mark.
        return out;
    }

    // ------------------------------------------------------ matrix variants
    if(in.engine==QLatin1String("Covariance Matrix")||in.engine==QLatin1String("Spy Matrix")){
        PlotSpec out=in;
        out.legendVisible=false;
        const int k=in.series.size();
        if(k>=2){
            const bool spy=(in.engine==QLatin1String("Spy Matrix"));
            QVector<QVector<double>> cols;
            for(const PlotSeries& s:in.series) cols.append(s.y);
            PlotSeries xi,yi,vi;
            for(int a=0;a<k;++a){
                for(int b=0;b<k;++b){
                    if(spy){
                        const int n=qMin(cols[a].size(),cols[b].size());
                        // The sparsity pattern: is this pair ever jointly
                        // non-zero. What a spy plot is for is structure, not
                        // magnitude.
                        double marked=0.0;
                        for(int i=0;i<n;++i)
                            if(finite(cols[a][i])&&finite(cols[b][i])
                               &&std::abs(cols[a][i])>1e-12&&std::abs(cols[b][i])>1e-12){
                                marked=1.0; break;
                            }
                        xi.y.append(double(a)); yi.y.append(double(b)); vi.y.append(marked);
                        continue;
                    }
                    const Moments2 m=momentsOf(cols[a],cols[b]);
                    if(m.n<2) continue;
                    xi.y.append(double(a)); yi.y.append(double(b));
                    vi.y.append(covarianceOf(m));
                }
            }
            xi.label=QStringLiteral("i"); yi.label=QStringLiteral("j");
            vi.label=spy?QStringLiteral("non-zero"):QStringLiteral("covariance");
            out.series={xi,yi,vi};
            out.xAxis=PlotAxis{QStringLiteral("variable"),false,-0.5,double(k)-0.5};
            out.yAxis=PlotAxis{QStringLiteral("variable"),false,-0.5,double(k)-0.5};
        }
        return out;
    }

    // ---------------------------------------------------------- geographic
    // Longitude east, latitude north, through a real projection.
    //
    // There is still no basemap and this still does not pretend to be one: the
    // axes are labelled with what they carry so nobody reads a country outline
    // that is not there. What changed is that the coordinates are projected
    // rather than plotted raw. Raw longitude against latitude IS a projection -
    // equirectangular - and it is the one that gets a route, a range ring and
    // the width of a country wrong everywhere except the equator. It remains
    // the default, so every figure already saved is drawn exactly as before,
    // and the catalogue variant chooses another.
    if(in.engine.startsWith(QLatin1String("Geo "))
       ||in.engine==QLatin1String("Great Circle Route")
       ||in.engine==QLatin1String("Ground Track")){
        if(in.series.size()<2) return in;
        const PlotSeries& lonSeries=in.series.at(0);
        const PlotSeries& latSeries=in.series.at(1);
        const MapProjection kind=mapProjectionFor(in.variant);
        const GeoFrame frame=geoFrameFor(kind,lonSeries.y,latSeries.y);
        const int n=qMin(lonSeries.y.size(),latSeries.y.size());

        // A route is the great circle between the points, not the straight line
        // between them: London to Tokyo over the pole is 9,600 km and the line
        // an equirectangular plot draws across Asia is 11,300. Each leg is
        // densified before projection, so the curve is the projection's own.
        QVector<double> lon,lat;
        if(in.engine==QLatin1String("Great Circle Route")){
            for(int i=0;i+1<n;++i){
                if(!finite(lonSeries.y[i])||!finite(latSeries.y[i])) continue;
                if(!finite(lonSeries.y[i+1])||!finite(latSeries.y[i+1])) continue;
                // Steps sized to the leg: a 40 km hop needs no subdivision at
                // all, a transpolar one needs plenty.
                const double metres=greatCircleMetres(lonSeries.y[i],latSeries.y[i],
                                                      lonSeries.y[i+1],latSeries.y[i+1]);
                const int steps=qBound(2,int(metres/25000.0),256);
                for(int k=0;k<steps;++k){
                    double gl=0,gp=0;
                    greatCirclePoint(lonSeries.y[i],latSeries.y[i],
                                     lonSeries.y[i+1],latSeries.y[i+1],
                                     double(k)/double(steps),gl,gp);
                    lon.append(gl); lat.append(gp);
                }
            }
            if(n>0&&finite(lonSeries.y[n-1])&&finite(latSeries.y[n-1])){
                lon.append(lonSeries.y[n-1]); lat.append(latSeries.y[n-1]);
            }
        }else{
            for(int i=0;i<n;++i){ lon.append(lonSeries.y[i]); lat.append(latSeries.y[i]); }
        }

        if(in.engine==QLatin1String("Geo Density")){
            PlotSpec out=derivedAs(in,QStringLiteral("2D Histogram"));
            PlotSeries px,py;
            px.label=projectionXLabel(frame); py.label=projectionYLabel(frame);
            for(int i=0;i<lon.size();++i){
                const QPointF q=projectPoint(frame,lon[i],lat[i]);
                if(!finite(q.x())||!finite(q.y())) continue;
                px.y.append(q.x()); py.y.append(q.y());
            }
            out.series={px,py};
            return prepareSpecCore(out);
        }

        PlotSpec out=derivedAs(in,(in.engine==QLatin1String("Geo Line")
                                   ||in.engine==QLatin1String("Great Circle Route")
                                   ||in.engine==QLatin1String("Ground Track"))
                                      ? QStringLiteral("Line Chart")
                                      : QStringLiteral("4D / 5D Scatter"));
        const bool asLine=(out.engine==QLatin1String("Line Chart"));
        // A ground track crosses the antimeridian, and a polyline that does is
        // drawn straight back across the whole picture unless it is cut there.
        // The cut is a new series rather than a gap, because a series is
        // documented as already cleaned of non-finite pairs - a NaN break would
        // be relying on behaviour nothing promises.
        const bool cutAtWrap=(in.engine==QLatin1String("Ground Track"));
        PlotSeries run;
        // "position", not the longitude column's name: the series is a place,
        // and labelling it "Longitude" describes half of it.
        run.label=QStringLiteral("position");
        run.color=lonSeries.color;
        run.drawLine=asLine;
        run.drawMarkers=!asLine;
        run.markerSize=(in.engine==QLatin1String("Geo Bubble"))?5.5:3.6;
        run.lineWidth=qMax(1.2,lonSeries.lineWidth);
        double previousLon=qQNaN();
        for(int i=0;i<lon.size();++i){
            if(cutAtWrap&&finite(previousLon)&&finite(lon[i])
               &&std::abs(lon[i]-previousLon)>180.0){
                if(run.x.size()>1) out.series.append(run);
                run.x.clear(); run.y.clear();
                run.label.clear();          // one legend entry for the track
            }
            if(finite(lon[i])) previousLon=lon[i];
            const QPointF q=projectPoint(frame,lon[i],lat[i]);
            if(!finite(q.x())||!finite(q.y())) continue;
            run.x.append(q.x()); run.y.append(q.y());
        }
        if(!run.x.isEmpty()) out.series.append(run);
        out.xAxis=PlotAxis{projectionXLabel(frame),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{projectionYLabel(frame),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ terrain profile
    // Elevation along a path: longitude, latitude and height, drawn against the
    // distance actually travelled rather than against a row index. The distance
    // is the great-circle sum leg by leg, so a profile of a flight, a pipeline
    // or a transect is in kilometres on the ground.
    if(in.engine==QLatin1String("Terrain Profile")){
        PlotSpec out=derivedAs(in,QStringLiteral("Area"));
        if(in.series.size()>=3){
            const QVector<double>& lon=in.series.at(0).y;
            const QVector<double>& lat=in.series.at(1).y;
            const QVector<double>& elevation=in.series.at(2).y;
            const int n=qMin(lon.size(),qMin(lat.size(),elevation.size()));
            PlotSeries profile;
            profile.label=in.series.at(2).label.isEmpty()
                             ? QStringLiteral("elevation") : in.series.at(2).label;
            profile.color=in.series.at(2).color;
            profile.drawLine=true;
            double travelled=0.0;
            double lastLon=qQNaN(),lastLat=qQNaN();
            for(int i=0;i<n;++i){
                if(!finite(lon[i])||!finite(lat[i])||!finite(elevation[i])) continue;
                if(finite(lastLon))
                    travelled+=greatCircleMetres(lastLon,lastLat,lon[i],lat[i])/1000.0;
                lastLon=lon[i]; lastLat=lat[i];
                profile.x.append(travelled);
                profile.y.append(elevation[i]);
            }
            if(!profile.x.isEmpty()) out.series.append(profile);
        }
        out.xAxis=PlotAxis{QStringLiteral("distance along path (km)"),false,unsetValue(),unsetValue()};
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("elevation");
        return out;
    }

    // ------------------------------ hypsometric and duration curves
    // One operation: sort the values from largest to smallest and plot them
    // against the fraction of the whole that reaches at least that value. It is
    // called a hypsometric curve when the whole is an area and the values are
    // heights, and a duration curve when the whole is a year and the values are
    // a load or a flow - and it was written out twice here before that was
    // noticed.
    //
    // What each says is the same thing about a different quantity: a convex
    // curve is a young incised landscape or a peaky demand, a concave one is a
    // worn basin or a steady baseload. Neither is visible in a histogram of the
    // same numbers.
    if(in.engine==QLatin1String("Hypsometric Curve")
       ||in.engine==QLatin1String("Duration Curve")){
        const bool hypsometric=(in.engine==QLatin1String("Hypsometric Curve"));
        const double fullScale=hypsometric?1.0:100.0;
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<3) continue;
            std::sort(v.begin(),v.end(),std::greater<double>());
            PlotSeries curve;
            curve.label=s.label; curve.color=s.color; curve.drawLine=true;
            curve.lineWidth=qMax(1.2,s.lineWidth);
            for(int i=0;i<v.size();++i){
                curve.x.append(fullScale*double(i+1)/double(v.size()));
                curve.y.append(v[i]);
            }
            out.series.append(curve);
        }
        out.xAxis=PlotAxis{hypsometric?QStringLiteral("fraction of area above")
                                      :QStringLiteral("per cent of time exceeded"),
                           false,0.0,fullScale};
        if(out.yAxis.label.isEmpty())
            out.yAxis.label=hypsometric?QStringLiteral("elevation"):QStringLiteral("value");
        return out;
    }

    // ------------------------------------------- slope / aspect / hillshade
    // The three things a height grid is actually read for. All from the same
    // gridded surface and the same central differences, so they cannot disagree
    // with each other about which way the ground faces.
    //
    // Aspect is a compass bearing, and the discontinuity at north is real: a
    // slope facing 359 degrees and one facing 1 degree are neighbours on the
    // ground and at opposite ends of the colour scale. That is a property of
    // the quantity, not a fault in the drawing, and smoothing it away would be
    // the dishonest choice.
    if(in.engine==QLatin1String("Slope Map")||in.engine==QLatin1String("Aspect Map")
       ||in.engine==QLatin1String("Hillshade")){
        PlotSpec out=derivedAs(in,QStringLiteral("2D Heatmap"));
        out.legendVisible=false;
        // Resolution from the sample count, the same rule every other field
        // engine uses. Asking for a fixed grid would read a sparse survey at a
        // detail it does not have, and a dense one at less than it does.
        const ValueGrid g=gridFromSeries(in,0,GridAggregate::Mean);
        if(!g.valid||g.nx<3||g.ny<3) return out;

        ValueGrid filled=g;
        closeGaps(filled,2);
        const double dx=(filled.xHi-filled.xLo)/qMax(1,filled.nx-1);
        const double dy=(filled.yHi-filled.yLo)/qMax(1,filled.ny-1);
        PlotSeries xi,yi,vi;
        xi.label=QStringLiteral("x"); yi.label=QStringLiteral("y");
        vi.label=in.engine==QLatin1String("Slope Map") ? QStringLiteral("slope (deg)")
                : in.engine==QLatin1String("Aspect Map") ? QStringLiteral("aspect (deg from N)")
                                                         : QStringLiteral("shading");
        // Sun at 315 degrees and 45 degrees up: the convention every relief map
        // is lit by, and the one people read as terrain rather than as a hole.
        const double sunAzimuth=(360.0-315.0+90.0)*kDegToRad;
        const double sunAltitude=45.0*kDegToRad;
        for(int j=1;j<filled.ny-1;++j){
            for(int i=1;i<filled.nx-1;++i){
                const double zL=filled.cells[j*filled.nx+i-1];
                const double zR=filled.cells[j*filled.nx+i+1];
                const double zD=filled.cells[(j-1)*filled.nx+i];
                const double zU=filled.cells[(j+1)*filled.nx+i];
                if(!finite(zL)||!finite(zR)||!finite(zD)||!finite(zU)) continue;
                const double gxv=(zR-zL)/(2.0*qMax(1e-12,dx));
                const double gyv=(zU-zD)/(2.0*qMax(1e-12,dy));
                double value=0.0;
                if(in.engine==QLatin1String("Slope Map")){
                    value=std::atan(std::hypot(gxv,gyv))*kRadToDeg;
                }else if(in.engine==QLatin1String("Aspect Map")){
                    if(std::abs(gxv)<1e-15&&std::abs(gyv)<1e-15) continue;  // flat has no aspect
                    // Downslope direction, as a bearing clockwise from north.
                    double bearing=std::atan2(-gxv,gyv)*kRadToDeg;
                    if(bearing<0) bearing+=360.0;
                    value=bearing;
                }else{
                    const double slope=std::atan(std::hypot(gxv,gyv));
                    double aspect=std::atan2(gyv,-gxv);
                    value=std::cos(sunAltitude)*std::cos(slope)
                         +std::sin(sunAltitude)*std::sin(slope)*std::cos(sunAzimuth-aspect);
                    value=qBound(0.0,value,1.0);
                }
                xi.y.append(filled.xLo+double(i)*dx);
                yi.y.append(filled.yLo+double(j)*dy);
                vi.y.append(value);
            }
        }
        out.series={xi,yi,vi};
        out.xAxis=PlotAxis{in.series.size()>0?in.series.at(0).label:QStringLiteral("x"),
                           false,filled.xLo,filled.xHi};
        out.yAxis=PlotAxis{in.series.size()>1?in.series.at(1).label:QStringLiteral("y"),
                           false,filled.yLo,filled.yHi};
        return out;
    }

    // =====================================================================
    // Flight and aviation
    //
    // These are the charts an aircraft is actually specified and operated by,
    // and none of them is a plot of the mapped columns as they stand: a drag
    // polar is read for the two coefficients fitted through it, a lift curve
    // for its slope and where it stops being a line, a crosswind chart for
    // components that have to be resolved from a wind and a runway heading,
    // and a weight-and-balance chart for whether the loaded point is inside the
    // envelope or outside it. Drawing the columns and leaving the reader to do
    // that arithmetic is what a general-purpose line chart already does.
    // =====================================================================

    // Payload against range: the four-cornered envelope every aircraft is sold
    // on. Filled to the axis, because the area under it is the useful part -
    // every payload/range pair inside it is a flight the aircraft can do.
    if(in.engine==QLatin1String("Payload-Range Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("Area"));
        for(const PlotSeries& s:in.series){
            PlotSeries leg=s;
            leg.drawLine=true;
            leg.drawMarkers=true;
            leg.markerSize=qMax(5.0,s.markerSize);
            leg.markerSizeExplicit=true;
            out.series.append(leg);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("range");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("payload");
        return out;
    }

    // A flight envelope is a closed shape, and a polyline that is not closed
    // reads as a boundary with a hole in it. Both of these close it and mark
    // the reference the envelope is read against: one g for the V-n diagram,
    // where the whole point is how far above and below one g the aircraft may
    // be flown.
    if(in.engine==QLatin1String("V-n Flight Envelope")
       ||in.engine==QLatin1String("Altitude-Mach Envelope")){
        const bool vn=(in.engine==QLatin1String("V-n Flight Envelope"));
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries loop;
            loop.label=s.label; loop.color=s.color; loop.drawLine=true;
            loop.drawMarkers=true; loop.markerSize=4.0;
            loop.lineWidth=qMax(1.6,s.lineWidth); loop.lineWidthExplicit=true;
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                loop.x.append(s.x[i]); loop.y.append(s.y[i]);
            }
            if(loop.x.size()<3) continue;
            // Close it, unless the data already did.
            if(!qFuzzyCompare(loop.x.first(),loop.x.last())
               ||!qFuzzyCompare(loop.y.first(),loop.y.last())){
                loop.x.append(loop.x.first());
                loop.y.append(loop.y.first());
            }
            out.series.append(loop);
        }
        if(vn&&!out.series.isEmpty()){
            double lo=std::numeric_limits<double>::infinity(),hi=-lo;
            for(const PlotSeries& s:out.series)
                for(double v:s.x){ lo=qMin(lo,v); hi=qMax(hi,v); }
            if(finite(lo)&&finite(hi)&&hi>lo){
                PlotSeries level;
                level.label=QStringLiteral("1 g");
                level.color=in.style.foreground;
                level.drawLine=true; level.lineWidth=0.8; level.lineWidthExplicit=true;
                level.dashPattern={4.0,3.0};
                level.x={lo,hi}; level.y={1.0,1.0};
                out.series.append(level);
            }
        }
        if(out.xAxis.label.isEmpty())
            out.xAxis.label=vn?QStringLiteral("equivalent airspeed"):QStringLiteral("Mach number");
        if(out.yAxis.label.isEmpty())
            out.yAxis.label=vn?QStringLiteral("load factor n (g)"):QStringLiteral("altitude");
        return out;
    }

    // The drag polar, with the two numbers it exists to give: the zero-lift
    // drag and the induced-drag factor of CD = CD0 + k CL^2, fitted by least
    // squares on CL^2, and the best lift-to-drag ratio that pair implies. The
    // measured points, the fitted parabola and the tangent point are drawn
    // together so a departure from the parabola is visible rather than averaged
    // into a number.
    if(in.engine==QLatin1String("Drag Polar")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries pts;
            pts.label=s.label; pts.color=s.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.5;
            // CD = CD0 + k CL^2 is a straight line in CL^2, so that is the
            // space the fit is done in.
            QVector<double> squared,drag;
            for(int i=0;i<n;++i){
                const double cd=s.x[i],cl=s.y[i];
                if(!finite(cd)||!finite(cl)) continue;
                pts.x.append(cd); pts.y.append(cl);
                squared.append(cl*cl); drag.append(cd);
            }
            if(pts.x.isEmpty()) continue;
            out.series.append(pts);
            const LineFit fit=fitLine(squared,drag);
            const double k=fit.slope,cd0=fit.intercept;
            if(!fit.ok||!(k>0)||!(cd0>0)) continue;

            double clLo=std::numeric_limits<double>::infinity(),clHi=-clLo;
            for(double cl:pts.y){ clLo=qMin(clLo,cl); clHi=qMax(clHi,cl); }
            PlotSeries curve;
            const double clBest=std::sqrt(cd0/k);
            const double ldMax=1.0/(2.0*std::sqrt(cd0*k));
            curve.label=QStringLiteral("CD0 %1, k %2, (L/D)max %3")
                            .arg(cd0,0,'f',4).arg(k,0,'f',4).arg(ldMax,0,'f',1);
            curve.color=s.color; curve.drawLine=true; curve.drawMarkers=false;
            curve.lineWidth=1.4; curve.lineWidthExplicit=true;
            for(int i=0;i<=120;++i){
                const double cl=clLo+(clHi-clLo)*double(i)/120.0;
                curve.x.append(cd0+k*cl*cl);
                curve.y.append(cl);
            }
            out.series.append(curve);
            if(clBest>=clLo&&clBest<=clHi){
                PlotSeries best;
                best.label=QStringLiteral("best L/D at CL %1").arg(clBest,0,'f',3);
                best.color=in.style.positive;
                best.drawLine=false; best.drawMarkers=true;
                best.markerSize=8.0; best.markerSizeExplicit=true;
                best.x={cd0+k*clBest*clBest}; best.y={clBest};
                out.series.append(best);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("drag coefficient CD");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("lift coefficient CL");
        return out;
    }

    // The lift curve, with the slope of its straight part and the stall it ends
    // at. The slope is fitted only below the maximum, because fitting through
    // the stall is what makes a lift-curve slope wrong - and quietly, since the
    // fit still returns a number.
    if(in.engine==QLatin1String("Lift Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries pts;
            pts.label=s.label; pts.color=s.color;
            pts.drawLine=true; pts.drawMarkers=true; pts.markerSize=4.0;
            int peak=-1; double peakCl=-std::numeric_limits<double>::infinity();
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                pts.x.append(s.x[i]); pts.y.append(s.y[i]);
                if(s.y[i]>peakCl){ peakCl=s.y[i]; peak=pts.x.size()-1; }
            }
            if(pts.x.isEmpty()) continue;

            // Only up to the peak: the linear region is what a lift-curve
            // slope means, and fitting through the stall quietly returns a
            // smaller number that still looks like an answer.
            const LineFit fit0=fitLine(pts.x,pts.y,(peak>=0)?peak+1:-1);
            {
                const double slope=fit0.slope,intercept=fit0.intercept;
                if(fit0.ok){
                    pts.label=QStringLiteral("%1 (a %2 /deg, %3 /rad, CLmax %4)")
                                  .arg(s.label).arg(slope,0,'f',4)
                                  .arg(slope*180.0/M_PI,0,'f',3).arg(peakCl,0,'f',3);
                    PlotSeries fit;
                    fit.label=QStringLiteral("linear region");
                    fit.color=in.style.warning;
                    fit.drawLine=true; fit.drawMarkers=false;
                    fit.lineWidth=1.0; fit.lineWidthExplicit=true;
                    fit.dashPattern={5.0,3.0};
                    const double a0=pts.x.first(),a1=pts.x.at(peak>=0?peak:pts.x.size()-1);
                    fit.x={a0,a1};
                    fit.y={intercept+slope*a0,intercept+slope*a1};
                    out.series.append(pts);
                    out.series.append(fit);
                    if(peak>=0){
                        PlotSeries stall;
                        stall.label=QStringLiteral("stall");
                        stall.color=in.style.danger;
                        stall.drawLine=false; stall.drawMarkers=true;
                        stall.markerSize=8.0; stall.markerSizeExplicit=true;
                        stall.x={pts.x.at(peak)}; stall.y={pts.y.at(peak)};
                        out.series.append(stall);
                    }
                    continue;
                }
            }
            out.series.append(pts);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("angle of attack (deg)");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("lift coefficient CL");
        return out;
    }

    // Altitude against time or distance, filled, with the top of climb marked.
    // The apex is where the profile stops being a climb and starts being a
    // cruise, and finding it by eye off a line is exactly the sort of reading
    // that goes wrong on a compressed x axis.
    if(in.engine==QLatin1String("Flight Profile")){
        PlotSpec out=derivedAs(in,QStringLiteral("Area"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<2) continue;
            PlotSeries leg=s;
            leg.drawLine=true;
            out.series.append(leg);
            int apex=-1; double top=-std::numeric_limits<double>::infinity();
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                if(s.y[i]>top){ top=s.y[i]; apex=i; }
            }
            if(apex>=0){
                PlotSeries mark;
                mark.label=QStringLiteral("top of climb %1").arg(top,0,'f',0);
                mark.color=in.style.positive;
                mark.drawLine=false; mark.drawMarkers=true;
                mark.markerSize=7.0; mark.markerSizeExplicit=true;
                mark.x={s.x[apex]}; mark.y={s.y[apex]};
                out.series.append(mark);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("time or distance");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("altitude");
        return out;
    }

    // Crosswind and headwind, resolved. Three mapped columns - runway heading,
    // wind direction and wind speed - and the components computed from them,
    // because that is the arithmetic the chart exists to save and the one that
    // gets done wrong in a hurry. Sign matters and is kept: a negative
    // headwind is a tailwind and is a different decision.
    if(in.engine==QLatin1String("Runway Crosswind")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=3){
            const QVector<double>& runway=in.series.at(0).y;
            const QVector<double>& from=in.series.at(1).y;
            const QVector<double>& speed=in.series.at(2).y;
            const int n=qMin(runway.size(),qMin(from.size(),speed.size()));
            PlotSeries pts;
            pts.label=QStringLiteral("wind");
            pts.color=in.series.at(2).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.5;
            double worstCross=0.0;
            for(int i=0;i<n;++i){
                if(!finite(runway[i])||!finite(from[i])||!finite(speed[i])) continue;
                const double delta=(from[i]-runway[i])*kDegToRad;
                const double cross=speed[i]*std::sin(delta);
                const double head=speed[i]*std::cos(delta);
                pts.x.append(cross); pts.y.append(head);
                worstCross=qMax(worstCross,std::abs(cross));
            }
            if(!pts.x.isEmpty()){
                pts.label=QStringLiteral("wind (max crosswind %1)").arg(worstCross,0,'f',1);
                out.series.append(pts);
                // The line where a headwind becomes a tailwind. Everything
                // below it is a downwind landing.
                PlotSeries zero;
                zero.label=QStringLiteral("no headwind");
                zero.color=in.style.warning;
                zero.drawLine=true; zero.drawMarkers=false;
                zero.lineWidth=0.8; zero.lineWidthExplicit=true;
                zero.dashPattern={4.0,3.0};
                zero.x={-worstCross,worstCross}; zero.y={0.0,0.0};
                out.series.append(zero);
            }
        }
        if(out.xAxis.label.isEmpty())
            out.xAxis.label=QStringLiteral("crosswind component (right positive)");
        if(out.yAxis.label.isEmpty())
            out.yAxis.label=QStringLiteral("headwind component (tailwind negative)");
        return out;
    }

    // Weight and balance. The first mapped column pair is the envelope; every
    // pair after it is a loading to test against it, and each is drawn in the
    // colour of its answer. Inside or outside is a ray-crossing test on the
    // polygon rather than a judgement by eye, because a point a millimetre
    // outside the aft limit looks exactly like one a millimetre inside.
    if(in.engine==QLatin1String("Weight and Balance Envelope")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        if(!in.series.isEmpty()){
            const PlotSeries& env=in.series.at(0);
            QVector<double> ex,ey;
            const int n=qMin(env.x.size(),env.y.size());
            for(int i=0;i<n;++i){
                if(!finite(env.x[i])||!finite(env.y[i])) continue;
                ex.append(env.x[i]); ey.append(env.y[i]);
            }
            if(ex.size()>=3){
                PlotSeries loop;
                loop.label=env.label.isEmpty()?QStringLiteral("envelope"):env.label;
                loop.color=env.color; loop.drawLine=true; loop.drawMarkers=false;
                loop.lineWidth=qMax(1.6,env.lineWidth); loop.lineWidthExplicit=true;
                loop.x=ex; loop.y=ey;
                if(!qFuzzyCompare(loop.x.first(),loop.x.last())
                   ||!qFuzzyCompare(loop.y.first(),loop.y.last())){
                    loop.x.append(loop.x.first()); loop.y.append(loop.y.first());
                }
                out.series.append(loop);

                const auto inside=[&ex,&ey](double px,double py){
                    bool in=false;
                    const int m=ex.size();
                    for(int i=0,j=m-1;i<m;j=i++){
                        const bool straddles=((ey[i]>py)!=(ey[j]>py));
                        if(!straddles) continue;
                        const double t=(py-ey[i])/(ey[j]-ey[i]);
                        if(px<ex[i]+t*(ex[j]-ex[i])) in=!in;
                    }
                    return in;
                };
                for(int k=1;k<in.series.size();++k){
                    const PlotSeries& load=in.series.at(k);
                    PlotSeries ok,bad;
                    ok.color=in.style.positive; bad.color=in.style.danger;
                    ok.drawLine=false; bad.drawLine=false;
                    ok.drawMarkers=true; bad.drawMarkers=true;
                    ok.markerSize=7.0; bad.markerSize=7.0;
                    ok.markerSizeExplicit=true; bad.markerSizeExplicit=true;
                    const int ln=qMin(load.x.size(),load.y.size());
                    for(int i=0;i<ln;++i){
                        if(!finite(load.x[i])||!finite(load.y[i])) continue;
                        PlotSeries& target=inside(load.x[i],load.y[i])?ok:bad;
                        target.x.append(load.x[i]); target.y.append(load.y[i]);
                    }
                    if(!ok.x.isEmpty()){
                        ok.label=QStringLiteral("%1 within limits (%2)")
                                     .arg(load.label).arg(ok.x.size());
                        out.series.append(ok);
                    }
                    if(!bad.x.isEmpty()){
                        bad.label=QStringLiteral("%1 OUTSIDE limits (%2)")
                                      .arg(load.label).arg(bad.x.size());
                        out.series.append(bad);
                    }
                }
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("centre of gravity");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("weight");
        return out;
    }

    // =====================================================================
    // Maintenance and reliability
    //
    // These are read for a fitted parameter, not for their shape: a Weibull
    // plot for the shape that says whether things are failing early, randomly
    // or by wear-out; a growth plot for whether the fixes are working; a CUSUM
    // for the moment a process shifted. Every one of them puts its numbers in
    // the legend, because a chart that makes you get a ruler out is a chart
    // that gets read wrong.
    // =====================================================================

    // The Weibull probability plot. Median ranks on the doubly-logarithmic
    // scale where a Weibull is a straight line, then a least-squares fit for
    // the shape and the scale.
    //
    // The shape is the whole point: below 1 the failures are early-life and a
    // burn-in is indicated; near 1 they are random and preventive replacement
    // buys nothing; above 1 they are wear-out and a life limit does. That
    // reading is not available from a histogram of the same times.
    if(in.engine==QLatin1String("Weibull Probability Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        for(const PlotSeries& s:in.series){
            QVector<double> t=finiteValues(s);
            // A failure at or before zero has no logarithm and is not a life.
            QVector<double> lives;
            for(double v:t) if(v>0) lives.append(v);
            if(lives.size()<3) continue;
            std::sort(lives.begin(),lives.end());
            const int n=lives.size();
            PlotSeries pts;
            pts.color=s.color; pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.5;
            for(int i=0;i<n;++i){
                // Bernard's approximation to the median rank. The plotting
                // position matters at small n, which is where reliability data
                // usually is.
                const double f=(double(i+1)-0.3)/(double(n)+0.4);
                const double x=std::log(lives[i]);
                const double y=std::log(-std::log(1.0-f));
                if(!finite(x)||!finite(y)) continue;
                pts.x.append(x); pts.y.append(y);
            }
            if(pts.x.size()<3) continue;
            {
                const LineFit line=fitLine(pts.x,pts.y);
                const double beta=line.slope;
                const double intercept=line.intercept;
                const double eta=std::exp(-intercept/qMax(1e-12,beta));
                if(line.ok&&finite(eta)&&beta>0){
                    pts.label=QStringLiteral("%1 (beta %2, eta %3, %4)")
                        .arg(s.label).arg(beta,0,'f',2).arg(eta,0,'f',3)
                        .arg(beta<0.95?QStringLiteral("early-life")
                            :beta<1.05?QStringLiteral("random")
                                      :QStringLiteral("wear-out"));
                    PlotSeries fit;
                    fit.label=QStringLiteral("Weibull fit");
                    fit.color=in.style.warning;
                    fit.drawLine=true; fit.drawMarkers=false;
                    fit.lineWidth=1.2; fit.lineWidthExplicit=true;
                    fit.x={pts.x.first(),pts.x.last()};
                    fit.y={intercept+beta*pts.x.first(),intercept+beta*pts.x.last()};
                    out.series.append(pts);
                    out.series.append(fit);
                    continue;
                }
            }
            pts.label=s.label;
            out.series.append(pts);
        }
        out.xAxis=PlotAxis{QStringLiteral("ln(life)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("ln(-ln(1-F))"),false,unsetValue(),unsetValue()};
        return out;
    }

    // Reliability growth. Cumulative failures against cumulative time, fitted
    // as N = lambda t^beta on log-log, which is the Crow-AMSAA model and the
    // Duane plot's straight line.
    //
    // Beta is the answer: below one the intervals between failures are getting
    // longer and the fixes are working, above one they are getting shorter and
    // the fleet is getting worse. The number is reported rather than left to be
    // eyeballed off a slope, because on log-log a slope of 0.8 and one of 1.2
    // look much the same.
    if(in.engine==QLatin1String("Reliability Growth")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        for(const PlotSeries& s:in.series){
            QVector<double> times=finiteValues(s);
            QVector<double> valid;
            for(double v:times) if(v>0) valid.append(v);
            if(valid.size()<3) continue;
            std::sort(valid.begin(),valid.end());
            PlotSeries pts;
            pts.color=s.color; pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.5;
            QVector<double> logTime,logCount;
            for(int i=0;i<valid.size();++i){
                const double x=std::log(valid[i]);
                const double y=std::log(double(i+1));
                if(!finite(x)||!finite(y)) continue;
                pts.x.append(valid[i]); pts.y.append(double(i+1));
                logTime.append(x); logCount.append(y);
            }
            if(logTime.size()<3) continue;
            {
                const LineFit line=fitLine(logTime,logCount);
                const double beta=line.slope;
                const double lambda=std::exp(line.intercept);
                if(line.ok&&finite(lambda)){
                    pts.label=QStringLiteral("%1 (beta %2, lambda %3, %4)")
                        .arg(s.label).arg(beta,0,'f',3).arg(lambda,0,'g',3)
                        .arg(beta<0.95?QStringLiteral("improving")
                            :beta>1.05?QStringLiteral("deteriorating")
                                      :QStringLiteral("steady"));
                    PlotSeries fit;
                    fit.label=QStringLiteral("Crow-AMSAA fit");
                    fit.color=in.style.warning;
                    fit.drawLine=true; fit.drawMarkers=false;
                    fit.lineWidth=1.2; fit.lineWidthExplicit=true;
                    for(int i=0;i<=60;++i){
                        const double t=valid.first()
                            +(valid.last()-valid.first())*double(i)/60.0;
                        if(!(t>0)) continue;
                        fit.x.append(t);
                        fit.y.append(lambda*std::pow(t,beta));
                    }
                    out.series.append(pts);
                    out.series.append(fit);
                    continue;
                }
            }
            pts.label=s.label;
            out.series.append(pts);
        }
        out.xAxis=PlotAxis{QStringLiteral("cumulative operating time"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("cumulative failures"),true,unsetValue(),unsetValue()};
        return out;
    }

    // Mean time between failures, as it moves. The column is failure times; the
    // intervals between them are what MTBF is the mean of, and a running mean
    // of those intervals is the only form of this chart that can show a trend
    // rather than a single number that hides one.
    if(in.engine==QLatin1String("MTBF Trend")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            QVector<double> times=finiteValues(s);
            if(times.size()<3) continue;
            std::sort(times.begin(),times.end());
            QVector<double> gaps;
            for(int i=1;i<times.size();++i) gaps.append(times[i]-times[i-1]);
            if(gaps.isEmpty()) continue;
            // A window that adapts: five points is noise on a long history and
            // all of the history on a short one.
            const int window=qBound(3,gaps.size()/6,25);
            PlotSeries cumulative,rolling;
            cumulative.label=QStringLiteral("%1 cumulative MTBF").arg(s.label);
            cumulative.color=s.color; cumulative.drawLine=true;
            rolling.label=QStringLiteral("last %1 intervals").arg(window);
            rolling.color=in.style.warning; rolling.drawLine=true;
            // Not dashed. A dash pattern over a curve of thousands of points is
            // both unreadable and slow - Qt's dash iterator runs per segment,
            // and this one series took a 36,000-point figure from 59 ms to
            // 4.7 SECONDS per repaint. Dashes are for the two-point reference
            // lines; a data curve is told apart by its colour.

            // A running sum, not a sum over the window at every step. The
            // window grows with the history - a sixth of it - so re-adding it
            // per point is quadratic, and it measured at 1,026 ms per repaint
            // on a few thousand failures. Subtracting the interval that leaves
            // and adding the one that arrives gives the same numbers in one
            // pass.
            // One point per failure is more than a trend chart can show. A
            // figure is a couple of thousand pixels wide at most, and both of
            // these are smoothed summaries - a running mean has no detail
            // between neighbouring points to lose. Striding to about two
            // thousand points was measured against the undecimated picture
            // before being kept.
            //
            // It matters because a noisy curve is expensive to draw: this one
            // series took 1,077 ms per repaint at three thousand points while
            // the smooth one beside it took 51.
            const int stride=qMax(1,int(gaps.size()/2000));
            double running=0.0,windowed=0.0;
            for(int i=0;i<gaps.size();++i){
                running+=gaps[i];
                windowed+=gaps[i];
                if(i>=window) windowed-=gaps[i-window];
                const bool keep=(i%stride==0)||(i==gaps.size()-1);
                if(keep){
                    cumulative.x.append(times[i+1]);
                    cumulative.y.append(running/double(i+1));
                }
                if(i+1>=window&&keep){
                    rolling.x.append(times[i+1]);
                    rolling.y.append(windowed/double(window));
                }
            }
            out.series.append(cumulative);
            if(!rolling.x.isEmpty()) out.series.append(rolling);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("operating time");
        out.yAxis=PlotAxis{QStringLiteral("mean time between failures"),false,unsetValue(),unsetValue()};
        return out;
    }

    // A year of daily values as a grid of weeks. The pattern a calendar shows
    // and a time series does not is the weekly one: a maintenance backlog that
    // clears every Friday, a fleet that flies at weekends. x is the week, y is
    // the day within it, and the value is the colour.
    if(in.engine==QLatin1String("Calendar Heatmap")){
        PlotSpec out=derivedAs(in,QStringLiteral("2D Heatmap"));
        out.legendVisible=false;
        if(!in.series.isEmpty()){
            const PlotSeries& s=in.series.at(0);
            const int n=qMin(s.x.size(),s.y.size());
            double first=std::numeric_limits<double>::infinity();
            for(int i=0;i<n;++i) if(finite(s.x[i])) first=qMin(first,s.x[i]);
            if(finite(first)){
                PlotSeries wx,wy,wv;
                for(int i=0;i<n;++i){
                    if(!finite(s.x[i])||!finite(s.y[i])) continue;
                    // The x column is a date in days. Anything with a fixed
                    // daily step works - a day number, a Julian day, a serial
                    // date - because only the difference is used.
                    const double days=std::floor(s.x[i]-first);
                    if(!(days>=0)) continue;
                    wx.y.append(std::floor(days/7.0));
                    wy.y.append(days-std::floor(days/7.0)*7.0);
                    wv.y.append(s.y[i]);
                }
                wx.label=QStringLiteral("week"); wy.label=QStringLiteral("day");
                wv.label=s.label;
                double weeks=0.0;
                for(double v:wx.y) weeks=qMax(weeks,v);
                out.series={wx,wy,wv};
                // Explicit, because these series carry values in .y and nothing
                // in .x: left to fit itself the frame would read the union of
                // week, weekday and value, and label the axis 0 to 1.
                out.xAxis=PlotAxis{QStringLiteral("week from first date"),false,-0.5,weeks+0.5};
                out.yAxis=PlotAxis{QStringLiteral("day within week"),false,-0.5,6.5};
                return out;
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("week from first date"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("day within week"),false,-0.5,6.5};
        return out;
    }

    // CUSUM. The tabular form, with the standard slack of half a sigma and a
    // decision interval of five: a chart that answers "when did it shift" for
    // shifts a Shewhart chart never triggers on, because a run of small
    // deviations all one way accumulates here and does not there.
    if(in.engine==QLatin1String("CUSUM Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<4) continue;
            const double mean=meanOf(v);
            const double sigma=stdevOf(v);
            if(!(sigma>0)) continue;
            const double slack=0.5*sigma,limit=5.0*sigma;
            PlotSeries up,down,upper,lower;
            up.label=QStringLiteral("%1 C+").arg(s.label);
            up.color=s.color; up.drawLine=true;
            down.label=QStringLiteral("%1 C-").arg(s.label);
            down.color=in.style.warning; down.drawLine=true;
            double cp=0,cm=0; int alarms=0;
            for(int i=0;i<v.size();++i){
                cp=qMax(0.0,cp+(v[i]-mean)-slack);
                cm=qMin(0.0,cm+(v[i]-mean)+slack);
                if(cp>limit||cm<-limit) ++alarms;
                up.x.append(double(i+1)); up.y.append(cp);
                down.x.append(double(i+1)); down.y.append(cm);
            }
            up.label=QStringLiteral("%1 C+ (%2 point%3 beyond h)")
                         .arg(s.label).arg(alarms).arg(alarms==1?QString():QStringLiteral("s"));
            upper.label=QStringLiteral("h = 5 sigma");
            upper.color=in.style.danger; upper.drawLine=true;
            upper.lineWidth=0.8; upper.lineWidthExplicit=true;
            upper.dashPattern={4.0,3.0};
            upper.x={1.0,double(v.size())}; upper.y={limit,limit};
            lower=upper; lower.label.clear();
            lower.y={-limit,-limit};
            out.series.append(up);
            out.series.append(down);
            out.series.append(upper);
            out.series.append(lower);
        }
        out.xAxis=PlotAxis{QStringLiteral("observation"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("cumulative sum of deviations"),false,unsetValue(),unsetValue()};
        return out;
    }

    // EWMA, with the limits that widen from the first point to their asymptote
    // rather than the asymptote drawn flat across the start. Early points are
    // the ones a flat limit judges wrongly, and they are the ones you have when
    // a process has just been changed.
    if(in.engine==QLatin1String("EWMA Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<4) continue;
            const double mean=meanOf(v),sigma=stdevOf(v);
            if(!(sigma>0)) continue;
            const double lambda=0.2,L=3.0;
            PlotSeries z,upper,lower,centre;
            z.label=QStringLiteral("%1 EWMA (lambda 0.2)").arg(s.label);
            z.color=s.color; z.drawLine=true; z.drawMarkers=true; z.markerSize=3.0;
            upper.label=QStringLiteral("3 sigma limits");
            upper.color=in.style.danger; upper.drawLine=true;
            upper.lineWidth=0.8; upper.lineWidthExplicit=true;
            lower=upper; lower.label.clear();
            centre.label=QStringLiteral("mean");
            centre.color=in.style.foreground; centre.drawLine=true;
            centre.lineWidth=0.6; centre.lineWidthExplicit=true;
            centre.dashPattern={4.0,4.0};
            double value=mean;
            // The centre is a constant: two points draw it exactly, and dashing
            // two points costs nothing while dashing one per observation is the
            // same cliff the MTBF rolling line fell off.
            centre.x={1.0,double(v.size())};
            centre.y={mean,mean};
            int alarms=0;
            for(int i=0;i<v.size();++i){
                value=lambda*v[i]+(1.0-lambda)*value;
                const double spread=sigma*std::sqrt((lambda/(2.0-lambda))
                                     *(1.0-std::pow(1.0-lambda,2.0*double(i+1))));
                const double hi=mean+L*spread,lo=mean-L*spread;
                if(value>hi||value<lo) ++alarms;
                z.x.append(double(i+1)); z.y.append(value);
                upper.x.append(double(i+1)); upper.y.append(hi);
                lower.x.append(double(i+1)); lower.y.append(lo);
            }
            z.label=QStringLiteral("%1 EWMA (%2 out of control)").arg(s.label).arg(alarms);
            out.series.append(z);
            out.series.append(upper);
            out.series.append(lower);
            out.series.append(centre);
        }
        out.xAxis=PlotAxis{QStringLiteral("observation"),false,unsetValue(),unsetValue()};
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("EWMA");
        return out;
    }

    // The S-N curve, with Basquin's law fitted through it: S = A N^b on log-log,
    // which is the form fatigue life is quoted in and the one that lets a life
    // be read off at a stress the test never ran at.
    if(in.engine==QLatin1String("S-N Fatigue Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries pts;
            pts.color=s.color; pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.5;
            QVector<double> logCycles,logStress;
            for(int i=0;i<n;++i){
                const double cycles=s.x[i],stress=s.y[i];
                if(!(cycles>0)||!(stress>0)) continue;
                pts.x.append(cycles); pts.y.append(stress);
                logCycles.append(std::log(cycles)); logStress.append(std::log(stress));
            }
            if(logCycles.size()<3) continue;
            {
                const LineFit line=fitLine(logCycles,logStress);
                const double b=line.slope;
                const double A=std::exp(line.intercept);
                if(line.ok&&finite(A)){
                    pts.label=QStringLiteral("%1 (S = %2 N^%3)")
                                  .arg(s.label).arg(A,0,'g',4).arg(b,0,'f',4);
                    PlotSeries fit;
                    fit.label=QStringLiteral("Basquin fit");
                    fit.color=in.style.warning;
                    fit.drawLine=true; fit.drawMarkers=false;
                    fit.lineWidth=1.2; fit.lineWidthExplicit=true;
                    double lo=std::numeric_limits<double>::infinity(),hi=-lo;
                    for(double c:pts.x){ lo=qMin(lo,c); hi=qMax(hi,c); }
                    for(int i=0;i<=60;++i){
                        const double c=std::exp(std::log(lo)
                                     +(std::log(hi)-std::log(lo))*double(i)/60.0);
                        fit.x.append(c); fit.y.append(A*std::pow(c,b));
                    }
                    out.series.append(pts);
                    out.series.append(fit);
                    continue;
                }
            }
            pts.label=s.label;
            out.series.append(pts);
        }
        out.xAxis=PlotAxis{QStringLiteral("cycles to failure N"),true,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("stress amplitude S"),true,unsetValue(),unsetValue()};
        return out;
    }

    // Rainflow counting, ASTM E1049 four-point, on a load history.
    //
    // A fatigue life cannot be computed from a load history directly: the
    // damage is done by closed hysteresis loops, and finding them means pairing
    // a reversal with the one it closes against, which is what rainflow does
    // and what counting peaks does not. The result is the cycles by range and
    // mean, as a grid - the form a damage sum is taken over.
    if(in.engine==QLatin1String("Rainflow Matrix")){
        PlotSpec out=derivedAs(in,QStringLiteral("2D Heatmap"));
        out.legendVisible=false;
        if(!in.series.isEmpty()){
            const QVector<double> raw=finiteValues(in.series.at(0));
            // Reversals only: the points where the load changes direction. The
            // straight runs between them do no damage and would otherwise be
            // counted as cycles of zero range.
            QVector<double> turns;
            for(int i=0;i<raw.size();++i){
                if(i==0||i==raw.size()-1){ turns.append(raw[i]); continue; }
                const double a=raw[i]-raw[i-1],b=raw[i+1]-raw[i];
                if((a>0&&b<0)||(a<0&&b>0)) turns.append(raw[i]);
            }
            QVector<double> ranges,means,counts;
            QVector<double> stack;
            const auto record=[&ranges,&means,&counts](double a,double b,double count){
                ranges.append(std::abs(a-b));
                means.append(0.5*(a+b));
                counts.append(count);
            };
            for(double point:turns){
                stack.append(point);
                // Four-point: the inner range closes when it is no larger than
                // the range on either side of it.
                while(stack.size()>=4){
                    const int k=stack.size();
                    const double r1=std::abs(stack[k-3]-stack[k-4]);
                    const double r2=std::abs(stack[k-2]-stack[k-3]);
                    const double r3=std::abs(stack[k-1]-stack[k-2]);
                    if(r2<=r1&&r2<=r3){
                        record(stack[k-3],stack[k-2],1.0);
                        stack.remove(k-3,2);
                    }else break;
                }
            }
            // Whatever is left never closed. Those are half cycles, and
            // dropping them would under-count the largest ranges - which are
            // the ones that do the damage.
            for(int i=0;i+1<stack.size();++i) record(stack[i],stack[i+1],0.5);

            if(!ranges.isEmpty()){
                PlotSeries rx,my,cv;
                rx.y=means; my.y=ranges; cv.y=counts;
                rx.label=QStringLiteral("mean");
                my.label=QStringLiteral("range");
                cv.label=QStringLiteral("cycles");
                out.series={rx,my,cv};
                double meanLo=std::numeric_limits<double>::infinity(),meanHi=-meanLo;
                double rangeHi=0.0;
                for(double v:means){ meanLo=qMin(meanLo,v); meanHi=qMax(meanHi,v); }
                for(double v:ranges) rangeHi=qMax(rangeHi,v);
                if(finite(meanLo)&&finite(meanHi)){
                    const double pad=qMax(1e-9,(meanHi-meanLo)*0.05);
                    out.xAxis=PlotAxis{QStringLiteral("cycle mean"),false,meanLo-pad,meanHi+pad};
                    out.yAxis=PlotAxis{QStringLiteral("cycle range"),false,0.0,rangeHi*1.05};
                    return out;
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("cycle mean"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("cycle range"),false,unsetValue(),unsetValue()};
        return out;
    }

    // =====================================================================
    // Logistics and infrastructure
    // =====================================================================

    // A schedule, an availability timeline and a borehole log: three names for
    // one shape, which is a row per thing and a bar from a start to an end.
    // Three mapped columns - row, start, end - and one series per bar, because
    // that is what the floating-row painter reads.
    if(in.engine==QLatin1String("Gantt Schedule")
       ||in.engine==QLatin1String("Availability Timeline")
       ||in.engine==QLatin1String("Borehole Log")){
        PlotSpec out=derivedAs(in,QStringLiteral("Floating Row"));
        out.legendVisible=false;
        if(in.series.size()>=3){
            const QVector<double>& row=in.series.at(0).y;
            const QVector<double>& from=in.series.at(1).y;
            const QVector<double>& to=in.series.at(2).y;
            const int n=qMin(row.size(),qMin(from.size(),to.size()));
            double rowLo=std::numeric_limits<double>::infinity(),rowHi=-rowLo;
            for(int i=0;i<n;++i){
                if(!finite(row[i])||!finite(from[i])||!finite(to[i])) continue;
                PlotSeries bar;
                bar.label=QStringLiteral("%1").arg(row[i]);
                // Alternating hues by row, so neighbouring rows are separable
                // without a legend - which a hundred-row schedule cannot have.
                bar.color=QColor::fromHsvF(std::fmod(std::abs(row[i])*0.17,1.0),0.45,0.9);
                bar.y={row[i]};
                bar.x={qMin(from[i],to[i]),qMax(from[i],to[i])};
                out.series.append(bar);
                rowLo=qMin(rowLo,row[i]); rowHi=qMax(rowHi,row[i]);
            }
            if(finite(rowLo)&&finite(rowHi))
                out.yAxis=PlotAxis{in.series.at(0).label.isEmpty()
                                       ?QStringLiteral("row"):in.series.at(0).label,
                                   false,rowLo-0.6,rowHi+0.6};
        }
        if(out.xAxis.label.isEmpty())
            out.xAxis.label=(in.engine==QLatin1String("Borehole Log"))
                                ? QStringLiteral("depth") : QStringLiteral("time");
        return out;
    }

    // Open, high, low, close. Four mapped columns and one series per period,
    // which is what the candlestick painter reads.
    if(in.engine==QLatin1String("OHLC Candlestick")){
        PlotSpec out=derivedAs(in,QStringLiteral("Candlestick"));
        out.legendVisible=false;
        if(in.series.size()>=4){
            const PlotSeries& first=in.series.at(0);
            const QVector<double>& open=in.series.at(0).y;
            const QVector<double>& high=in.series.at(1).y;
            const QVector<double>& low=in.series.at(2).y;
            const QVector<double>& close=in.series.at(3).y;
            const int n=qMin(qMin(open.size(),high.size()),qMin(low.size(),close.size()));
            for(int i=0;i<n;++i){
                if(!finite(open[i])||!finite(high[i])||!finite(low[i])||!finite(close[i]))
                    continue;
                PlotSeries candle;
                // The period's own x when one was mapped, otherwise its order.
                candle.x={(i<first.x.size()&&finite(first.x[i]))?first.x[i]:double(i)};
                candle.y={open[i],high[i],low[i],close[i]};
                out.series.append(candle);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("period");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("price");
        return out;
    }

    // Flows between places, on a map. Five mapped columns - origin longitude
    // and latitude, destination longitude and latitude, and the weight - drawn
    // as great circles whose width is the flow, through the same projection
    // every other geographic engine uses.
    if(in.engine==QLatin1String("Origin-Destination Flow")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        out.legendVisible=false;
        if(in.series.size()>=4){
            const QVector<double>& oLon=in.series.at(0).y;
            const QVector<double>& oLat=in.series.at(1).y;
            const QVector<double>& dLon=in.series.at(2).y;
            const QVector<double>& dLat=in.series.at(3).y;
            const bool weighted=in.series.size()>=5;
            const QVector<double> w=weighted?in.series.at(4).y:QVector<double>();
            const int n=qMin(qMin(oLon.size(),oLat.size()),qMin(dLon.size(),dLat.size()));

            QVector<double> allLon,allLat;
            for(int i=0;i<n;++i){ allLon.append(oLon[i]); allLat.append(oLat[i]);
                                  allLon.append(dLon[i]); allLat.append(dLat[i]); }
            const GeoFrame frame=geoFrameFor(mapProjectionFor(in.variant),allLon,allLat);

            double heaviest=0.0;
            for(int i=0;i<n&&weighted;++i) if(i<w.size()&&finite(w[i]))
                heaviest=qMax(heaviest,std::abs(w[i]));
            if(!(heaviest>0)) heaviest=1.0;

            for(int i=0;i<n;++i){
                if(!finite(oLon[i])||!finite(oLat[i])||!finite(dLon[i])||!finite(dLat[i]))
                    continue;
                const double flow=(weighted&&i<w.size()&&finite(w[i]))?std::abs(w[i]):1.0;
                PlotSeries arc;
                arc.color=in.series.at(0).color;
                arc.drawLine=true; arc.drawMarkers=false;
                arc.lineWidth=qBound(0.5,4.0*flow/heaviest,5.0);
                arc.lineWidthExplicit=true;
                arc.opacity=0.85;
                const double metres=greatCircleMetres(oLon[i],oLat[i],dLon[i],dLat[i]);
                // Detail per arc, scaled by how many arcs there are. A dozen
                // routes deserve a smooth curve; three thousand of them are a
                // few pixels each, and drawing 128 points per arc cost 800 ms
                // per repaint to produce a picture indistinguishable from this
                // one. The budget is about forty thousand points in total.
                const int perArc=qBound(2,40000/qMax(1,n),128);
                const int steps=qBound(2,qMin(perArc,int(metres/50000.0)),128);
                for(int k=0;k<=steps;++k){
                    double gl=0,gp=0;
                    greatCirclePoint(oLon[i],oLat[i],dLon[i],dLat[i],
                                     double(k)/double(steps),gl,gp);
                    const QPointF q=projectPoint(frame,gl,gp);
                    if(!finite(q.x())||!finite(q.y())) continue;
                    arc.x.append(q.x()); arc.y.append(q.y());
                }
                if(arc.x.size()>1) out.series.append(arc);
            }
            out.xAxis=PlotAxis{projectionXLabel(frame),false,unsetValue(),unsetValue()};
            out.yAxis=PlotAxis{projectionYLabel(frame),false,unsetValue(),unsetValue()};
        }
        return out;
    }

    // The cumulative flow diagram. Bands stacked in the order the work moves
    // through them, so the vertical distance between two lines is how much is
    // sitting in that stage and the horizontal distance is how long it stays
    // there. Drawn largest first, because each band is filled to the baseline
    // and the later ones have to paint over the earlier.
    if(in.engine==QLatin1String("Cumulative Flow")){
        PlotSpec out=derivedAs(in,QStringLiteral("Area"));
        const int k=in.series.size();
        if(k>0){
            const int n=in.series.at(0).y.size();
            for(int band=k-1;band>=0;--band){
                PlotSeries area;
                area.label=in.series.at(band).label;
                area.color=in.series.at(band).color;
                area.drawLine=true; area.drawMarkers=false;
                area.opacity=0.85;
                for(int i=0;i<n;++i){
                    double total=0.0; bool ok=true;
                    for(int j=0;j<=band;++j){
                        if(i>=in.series.at(j).y.size()||!finite(in.series.at(j).y[i])){ ok=false; break; }
                        total+=in.series.at(j).y[i];
                    }
                    if(!ok) continue;
                    const QVector<double>& xs=in.series.at(band).x;
                    area.x.append((i<xs.size()&&finite(xs[i]))?xs[i]:double(i));
                    area.y.append(total);
                }
                if(!area.x.isEmpty()) out.series.append(area);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("time");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("cumulative count");
        return out;
    }

    // Stock over time, with the reorder points marked and the average level
    // stated. The sawtooth is read for two things - how low it gets and how
    // often it is refilled - and both are marked rather than counted by eye.
    if(in.engine==QLatin1String("Inventory Sawtooth")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries level=s;
            level.drawLine=true; level.drawMarkers=false;
            PlotSeries troughs;
            troughs.label=QStringLiteral("reorder");
            troughs.color=in.style.danger;
            troughs.drawLine=false; troughs.drawMarkers=true;
            troughs.markerSize=6.0; troughs.markerSizeExplicit=true;
            double sum=0.0; int counted=0; double lowest=std::numeric_limits<double>::infinity();
            for(int i=0;i<n;++i){
                if(!finite(s.y[i])) continue;
                sum+=s.y[i]; ++counted;
                lowest=qMin(lowest,s.y[i]);
                if(i==0||i==n-1) continue;
                if(!finite(s.y[i-1])||!finite(s.y[i+1])) continue;
                // A trough: the level falls into this point and rises out of
                // it. That is a replenishment, whatever the interval.
                if(s.y[i]<s.y[i-1]&&s.y[i+1]>s.y[i]){
                    troughs.x.append(s.x[i]); troughs.y.append(s.y[i]);
                }
            }
            if(counted>0){
                const double mean=sum/double(counted);
                level.label=QStringLiteral("%1 (mean %2, minimum %3, %4 reorders)")
                                .arg(s.label).arg(mean,0,'g',4).arg(lowest,0,'g',4)
                                .arg(troughs.x.size());
                out.series.append(level);
                if(!troughs.x.isEmpty()) out.series.append(troughs);
                PlotSeries average;
                average.label=QStringLiteral("mean level");
                average.color=in.style.warning;
                average.drawLine=true; average.lineWidth=0.8; average.lineWidthExplicit=true;
                average.dashPattern={4.0,3.0};
                average.x={s.x.first(),s.x.last()};
                average.y={mean,mean};
                out.series.append(average);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("time");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("stock on hand");
        return out;
    }

    // The fundamental diagram of traffic: flow against density, with
    // Greenshields fitted through it. The two numbers it gives - free-flow
    // speed and jam density - are what a road's capacity is computed from, and
    // the capacity itself is the apex of the parabola, which is easy to
    // mis-read off scattered points and exact from the fit.
    if(in.engine==QLatin1String("Fundamental Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<4) continue;
            PlotSeries pts;
            pts.label=s.label; pts.color=s.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            // q = vf k - (vf/kj) k^2, fitted through the origin as
            // q/k = vf - (vf/kj) k, which is a straight line in speed.
            QVector<double> densities,speeds;
            double densest=0.0;
            for(int i=0;i<n;++i){
                const double density=s.x[i],flow=s.y[i];
                if(!finite(density)||!finite(flow)||!(density>0)) continue;
                pts.x.append(density); pts.y.append(flow);
                densities.append(density); speeds.append(flow/density);
                densest=qMax(densest,density);
            }
            if(pts.x.isEmpty()) continue;
            out.series.append(pts);
            const LineFit line=fitLine(densities,speeds);
            const double slope=line.slope,freeFlow=line.intercept;
            if(!line.ok||!(freeFlow>0)||!(slope<0)) continue;
            const double jam=-freeFlow/slope;
            const double capacity=freeFlow*jam/4.0;
            PlotSeries curve;
            curve.label=QStringLiteral("Greenshields (vf %1, kj %2, capacity %3)")
                            .arg(freeFlow,0,'f',1).arg(jam,0,'f',1).arg(capacity,0,'f',0);
            curve.color=in.style.warning;
            curve.drawLine=true; curve.drawMarkers=false;
            curve.lineWidth=1.3; curve.lineWidthExplicit=true;
            for(int i=0;i<=80;++i){
                const double k=qMax(densest,jam)*double(i)/80.0;
                curve.x.append(k);
                curve.y.append(freeFlow*k+slope*k*k);
            }
            out.series.append(curve);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("density (veh/km)");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("flow (veh/h)");
        return out;
    }

    // =====================================================================
    // Chemistry, materials and the laboratory
    //
    // Every one of these is read for a number rather than for its shape - a
    // modulus, an activation energy, a pKa, a detection limit, a Km, an EC50 -
    // and every one of those numbers comes from a construction the reader is
    // otherwise expected to do with a ruler and a calculator.
    // =====================================================================

    // Stress against strain, with the three numbers a tensile test is run for:
    // the elastic modulus, the 0.2 per cent offset yield and the tensile
    // strength. The modulus is fitted over the first tenth of the strain range
    // rather than over everything, because a fit through the plastic region
    // returns a smaller modulus that still looks like an answer.
    if(in.engine==QLatin1String("Stress-Strain Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<5) continue;
            PlotSeries curve;
            curve.label=s.label; curve.color=s.color; curve.drawLine=true;
            double strainHi=0.0,peak=-std::numeric_limits<double>::infinity();
            int peakAt=-1;
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                curve.x.append(s.x[i]); curve.y.append(s.y[i]);
                strainHi=qMax(strainHi,s.x[i]);
                if(s.y[i]>peak){ peak=s.y[i]; peakAt=curve.x.size()-1; }
            }
            if(curve.x.size()<5) continue;

            // The elastic region: everything below a tenth of the strain range.
            int elastic=0;
            while(elastic<curve.x.size()&&curve.x[elastic]<=strainHi*0.10) ++elastic;
            const LineFit line=fitLine(curve.x,curve.y,qMax(3,elastic));
            if(line.ok&&line.slope>0){
                // The 0.2% offset line, and where the curve crosses it - which
                // is the definition of yield for anything without a sharp one.
                const double offset=0.002;
                PlotSeries offsetLine;
                offsetLine.label=QStringLiteral("0.2%% offset");
                offsetLine.color=in.style.warning;
                offsetLine.drawLine=true; offsetLine.lineWidth=0.9;
                offsetLine.lineWidthExplicit=true; offsetLine.dashPattern={5.0,3.0};
                offsetLine.x={offset,strainHi};
                offsetLine.y={line.slope*(offset-offset)+line.intercept,
                              line.slope*(strainHi-offset)+line.intercept};
                double yieldStress=qQNaN(),yieldStrain=qQNaN();
                for(int i=1;i<curve.x.size();++i){
                    const double model=line.slope*(curve.x[i]-offset)+line.intercept;
                    const double before=line.slope*(curve.x[i-1]-offset)+line.intercept;
                    if((curve.y[i-1]-before)>0&&(curve.y[i]-model)<=0){
                        yieldStrain=curve.x[i]; yieldStress=curve.y[i];
                        break;
                    }
                }
                curve.label=QStringLiteral("%1 (E %2, UTS %3%4)")
                    .arg(s.label).arg(line.slope,0,'g',4).arg(peak,0,'g',4)
                    .arg(finite(yieldStress)?QStringLiteral(", yield %1").arg(yieldStress,0,'g',4)
                                            :QString());
                out.series.append(curve);
                out.series.append(offsetLine);
                if(finite(yieldStress)){
                    PlotSeries mark;
                    mark.label=QStringLiteral("yield");
                    mark.color=in.style.positive;
                    mark.drawLine=false; mark.drawMarkers=true;
                    mark.markerSize=8.0; mark.markerSizeExplicit=true;
                    mark.x={yieldStrain}; mark.y={yieldStress};
                    out.series.append(mark);
                }
            }else{
                out.series.append(curve);
            }
            if(peakAt>=0){
                PlotSeries uts;
                uts.label=QStringLiteral("tensile strength");
                uts.color=in.style.danger;
                uts.drawLine=false; uts.drawMarkers=true;
                uts.markerSize=8.0; uts.markerSizeExplicit=true;
                uts.x={curve.x.at(peakAt)}; uts.y={curve.y.at(peakAt)};
                out.series.append(uts);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("strain");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("stress");
        return out;
    }

    // ln k against 1/T. The slope is -Ea/R, and the activation energy is the
    // reason the plot is drawn at all - so it is computed here rather than left
    // as a slope on a picture with a reciprocal axis.
    if(in.engine==QLatin1String("Arrhenius Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        constexpr double kGasConstant=8.314462618;   // J/(mol K)
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries pts;
            pts.color=s.color; pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.5;
            QVector<double> inverse,logRate;
            for(int i=0;i<n;++i){
                const double temperature=s.x[i],rate=s.y[i];
                if(!(temperature>0)||!(rate>0)) continue;
                const double x=1000.0/temperature;      // 1000/T, the usual axis
                const double y=std::log(rate);
                pts.x.append(x); pts.y.append(y);
                inverse.append(x); logRate.append(y);
            }
            if(pts.x.size()<3) continue;
            const LineFit line=fitLine(inverse,logRate);
            if(line.ok){
                // The x axis is 1000/T, so the slope is -Ea/(1000 R).
                const double activation=-line.slope*kGasConstant*1000.0;
                pts.label=QStringLiteral("%1 (Ea %2 kJ/mol, A %3)")
                              .arg(s.label).arg(activation/1000.0,0,'f',1)
                              .arg(std::exp(line.intercept),0,'g',3);
                PlotSeries fit;
                fit.label=QStringLiteral("Arrhenius fit");
                fit.color=in.style.warning;
                fit.drawLine=true; fit.lineWidth=1.2; fit.lineWidthExplicit=true;
                double lo=std::numeric_limits<double>::infinity(),hi=-lo;
                for(double v:pts.x){ lo=qMin(lo,v); hi=qMax(hi,v); }
                fit.x={lo,hi};
                fit.y={line.intercept+line.slope*lo,line.intercept+line.slope*hi};
                out.series.append(pts);
                out.series.append(fit);
                continue;
            }
            pts.label=s.label;
            out.series.append(pts);
        }
        out.xAxis=PlotAxis{QStringLiteral("1000/T (1/K)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("ln k"),false,unsetValue(),unsetValue()};
        return out;
    }

    // A titration curve, with the equivalence point taken as the steepest part
    // of the curve rather than as the middle of the visible jump, and the pKa
    // read at half that volume. Both are constructions done by hand on a
    // printout, and both are done wrong when the jump is not symmetrical.
    if(in.engine==QLatin1String("Titration Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<5) continue;
            PlotSeries curve;
            curve.label=s.label; curve.color=s.color; curve.drawLine=true;
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                curve.x.append(s.x[i]); curve.y.append(s.y[i]);
            }
            if(curve.x.size()<5) continue;
            int steepest=-1; double slope=0.0;
            for(int i=1;i<curve.x.size();++i){
                const double dx=curve.x[i]-curve.x[i-1];
                if(!(std::abs(dx)>1e-12)) continue;
                const double gradient=std::abs((curve.y[i]-curve.y[i-1])/dx);
                if(gradient>slope){ slope=gradient; steepest=i; }
            }
            out.series.append(curve);
            if(steepest>0){
                const double volume=0.5*(curve.x[steepest]+curve.x[steepest-1]);
                PlotSeries mark;
                mark.label=QStringLiteral("equivalence at %1").arg(volume,0,'g',4);
                mark.color=in.style.danger;
                mark.drawLine=false; mark.drawMarkers=true;
                mark.markerSize=8.0; mark.markerSizeExplicit=true;
                mark.x={volume}; mark.y={0.5*(curve.y[steepest]+curve.y[steepest-1])};
                out.series.append(mark);
                // Half-equivalence: for a weak acid the pH there is the pKa.
                const double half=volume*0.5;
                double pka=qQNaN();
                for(int i=1;i<curve.x.size();++i){
                    if(curve.x[i-1]<=half&&half<=curve.x[i]){
                        const double span=curve.x[i]-curve.x[i-1];
                        const double t=(std::abs(span)>1e-12)?(half-curve.x[i-1])/span:0.0;
                        pka=curve.y[i-1]+t*(curve.y[i]-curve.y[i-1]);
                        break;
                    }
                }
                if(finite(pka)){
                    PlotSeries halfMark;
                    halfMark.label=QStringLiteral("half-equivalence, pKa %1").arg(pka,0,'f',2);
                    halfMark.color=in.style.positive;
                    halfMark.drawLine=false; halfMark.drawMarkers=true;
                    halfMark.markerSize=7.0; halfMark.markerSizeExplicit=true;
                    halfMark.x={half}; halfMark.y={pka};
                    out.series.append(halfMark);
                }
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("titrant added");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("pH");
        return out;
    }

    // A calibration curve with the two limits every analytical result depends
    // on: the detection limit at 3.3 residual standard deviations over the
    // slope, and the quantitation limit at 10. Reporting a concentration below
    // the first is reporting noise, and it is done constantly because the
    // limits are not on the chart.
    if(in.engine==QLatin1String("Calibration Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries pts;
            pts.color=s.color; pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=5.0;
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                pts.x.append(s.x[i]); pts.y.append(s.y[i]);
            }
            if(pts.x.size()<3) continue;
            const LineFit line=fitLine(pts.x,pts.y);
            if(!line.ok||!(std::abs(line.slope)>0)){ pts.label=s.label; out.series.append(pts); continue; }
            double ssResidual=0.0,ssTotal=0.0,mean=0.0;
            for(double v:pts.y) mean+=v;
            mean/=double(pts.y.size());
            for(int i=0;i<pts.x.size();++i){
                const double predicted=line.intercept+line.slope*pts.x[i];
                ssResidual+=(pts.y[i]-predicted)*(pts.y[i]-predicted);
                ssTotal+=(pts.y[i]-mean)*(pts.y[i]-mean);
            }
            const int dof=pts.x.size()-2;
            const double sigma=(dof>0)?std::sqrt(ssResidual/double(dof)):0.0;
            const double r2=(ssTotal>0)?1.0-ssResidual/ssTotal:0.0;
            const double lod=3.3*sigma/std::abs(line.slope);
            const double loq=10.0*sigma/std::abs(line.slope);
            pts.label=QStringLiteral("%1 (slope %2, R2 %3, LOD %4, LOQ %5)")
                          .arg(s.label).arg(line.slope,0,'g',4).arg(r2,0,'f',4)
                          .arg(lod,0,'g',3).arg(loq,0,'g',3);
            out.series.append(pts);
            double lo=std::numeric_limits<double>::infinity(),hi=-lo;
            for(double v:pts.x){ lo=qMin(lo,v); hi=qMax(hi,v); }
            PlotSeries fit;
            fit.label=QStringLiteral("least squares");
            fit.color=in.style.warning; fit.drawLine=true;
            fit.lineWidth=1.2; fit.lineWidthExplicit=true;
            fit.x={lo,hi};
            fit.y={line.intercept+line.slope*lo,line.intercept+line.slope*hi};
            out.series.append(fit);
            for(const auto& limit:{qMakePair(lod,QStringLiteral("LOD")),
                                   qMakePair(loq,QStringLiteral("LOQ"))}){
                if(!finite(limit.first)||limit.first<lo||limit.first>hi) continue;
                PlotSeries mark;
                mark.label=limit.second;
                mark.color=(limit.second==QLatin1String("LOD"))?in.style.danger:in.style.positive;
                mark.drawLine=true; mark.drawMarkers=false;
                mark.lineWidth=0.8; mark.lineWidthExplicit=true;
                mark.dashPattern={3.0,3.0};
                double yLo=std::numeric_limits<double>::infinity(),yHi=-yLo;
                for(double v:pts.y){ yLo=qMin(yLo,v); yHi=qMax(yHi,v); }
                mark.x={limit.first,limit.first};
                mark.y={yLo,yHi};
                out.series.append(mark);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("concentration");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("response");
        return out;
    }

    // Michaelis-Menten, with Vmax and Km from the Lineweaver-Burk
    // linearisation. The double-reciprocal plot is drawn as well as the
    // hyperbola, because that is where a departure from the model shows -
    // inhibition bends the reciprocal line and barely moves the hyperbola.
    if(in.engine==QLatin1String("Michaelis-Menten")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries pts;
            pts.color=s.color; pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.5;
            QVector<double> reciprocalS,reciprocalV;
            for(int i=0;i<n;++i){
                const double substrate=s.x[i],rate=s.y[i];
                if(!(substrate>0)||!(rate>0)) continue;
                pts.x.append(substrate); pts.y.append(rate);
                reciprocalS.append(1.0/substrate); reciprocalV.append(1.0/rate);
            }
            if(pts.x.size()<3) continue;
            const LineFit line=fitLine(reciprocalS,reciprocalV);
            if(line.ok&&line.intercept>0){
                const double vmax=1.0/line.intercept;
                const double km=line.slope*vmax;
                pts.label=QStringLiteral("%1 (Vmax %2, Km %3)")
                              .arg(s.label).arg(vmax,0,'g',4).arg(km,0,'g',4);
                PlotSeries curve;
                curve.label=QStringLiteral("Michaelis-Menten fit");
                curve.color=in.style.warning; curve.drawLine=true;
                curve.lineWidth=1.2; curve.lineWidthExplicit=true;
                double hi=0.0;
                for(double v:pts.x) hi=qMax(hi,v);
                for(int i=0;i<=120;++i){
                    const double substrate=hi*double(i)/120.0;
                    curve.x.append(substrate);
                    curve.y.append(vmax*substrate/qMax(1e-12,km+substrate));
                }
                out.series.append(pts);
                out.series.append(curve);
                continue;
            }
            pts.label=s.label;
            out.series.append(pts);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("substrate concentration");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("initial rate");
        return out;
    }

    // A dose-response curve with its EC50, from the Hill linearisation: the
    // logit of the normalised response against log dose is a straight line
    // whose slope is the Hill coefficient and whose root is the EC50. The
    // plateau is taken from the data rather than assumed to be one, because a
    // response normalised to a plateau that was never reached puts the EC50 in
    // the wrong place.
    if(in.engine==QLatin1String("Dose-Response Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<4) continue;
            PlotSeries pts;
            pts.color=s.color; pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.5;
            double top=-std::numeric_limits<double>::infinity();
            double bottom=std::numeric_limits<double>::infinity();
            for(int i=0;i<n;++i){
                if(!(s.x[i]>0)||!finite(s.y[i])) continue;
                pts.x.append(s.x[i]); pts.y.append(s.y[i]);
                top=qMax(top,s.y[i]); bottom=qMin(bottom,s.y[i]);
            }
            if(pts.x.size()<4||!(top>bottom)) continue;
            QVector<double> logDose,logit;
            for(int i=0;i<pts.x.size();++i){
                const double fraction=(pts.y[i]-bottom)/(top-bottom);
                if(!(fraction>0.02)||!(fraction<0.98)) continue;   // the plateaus carry no slope
                logDose.append(std::log10(pts.x[i]));
                logit.append(std::log10(fraction/(1.0-fraction)));
            }
            const LineFit line=fitLine(logDose,logit);
            if(line.ok&&std::abs(line.slope)>1e-9){
                const double ec50=std::pow(10.0,-line.intercept/line.slope);
                pts.label=QStringLiteral("%1 (EC50 %2, Hill %3)")
                              .arg(s.label).arg(ec50,0,'g',4).arg(line.slope,0,'f',2);
                PlotSeries curve;
                curve.label=QStringLiteral("Hill fit");
                curve.color=in.style.warning; curve.drawLine=true;
                curve.lineWidth=1.2; curve.lineWidthExplicit=true;
                double lo=std::numeric_limits<double>::infinity(),hi=-lo;
                for(double v:pts.x){ lo=qMin(lo,v); hi=qMax(hi,v); }
                for(int i=0;i<=120;++i){
                    const double dose=std::pow(10.0,std::log10(lo)
                                     +(std::log10(hi)-std::log10(lo))*double(i)/120.0);
                    const double ratio=std::pow(dose/qMax(1e-300,ec50),line.slope);
                    curve.x.append(dose);
                    curve.y.append(bottom+(top-bottom)*ratio/(1.0+ratio));
                }
                out.series.append(pts);
                out.series.append(curve);
                PlotSeries mark;
                mark.label=QStringLiteral("EC50");
                mark.color=in.style.positive;
                mark.drawLine=false; mark.drawMarkers=true;
                mark.markerSize=8.0; mark.markerSizeExplicit=true;
                mark.x={ec50}; mark.y={0.5*(top+bottom)};
                out.series.append(mark);
                continue;
            }
            pts.label=s.label;
            out.series.append(pts);
        }
        out.xAxis=PlotAxis{in.xAxis.label.isEmpty()?QStringLiteral("dose"):in.xAxis.label,
                           true,unsetValue(),unsetValue()};
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("response");
        return out;
    }

    // =====================================================================
    // Medicine, epidemiology and quality
    // =====================================================================

    // Kaplan-Meier. Two mapped columns - the time, and 1 for an event and 0 for
    // a censored observation - and the product-limit estimate over them, with
    // the censored observations ticked on the curve.
    //
    // An ECDF of the same times is not this: it treats a subject who left the
    // study alive as one who died, which biases survival down by exactly the
    // amount of follow-up that was lost.
    if(in.engine==QLatin1String("Kaplan-Meier Survival")){
        PlotSpec out=derivedAs(in,QStringLiteral("Stairs"));
        if(in.series.size()>=2){
            const QVector<double>& times=in.series.at(0).y;
            const QVector<double>& events=in.series.at(1).y;
            const int n=qMin(times.size(),events.size());
            QVector<QPair<double,bool>> subjects;
            for(int i=0;i<n;++i){
                if(!finite(times[i])||!finite(events[i])) continue;
                subjects.append({times[i],events[i]>=0.5});
            }
            std::sort(subjects.begin(),subjects.end(),
                      [](const QPair<double,bool>& a,const QPair<double,bool>& b){
                          return a.first<b.first; });
            if(subjects.size()>=2){
                PlotSeries curve,censored;
                curve.label=in.series.at(0).label;
                curve.color=in.series.at(0).color;
                curve.drawLine=true; curve.drawMarkers=false;
                censored.label=QStringLiteral("censored");
                censored.color=in.style.warning;
                censored.drawLine=false; censored.drawMarkers=true;
                censored.markerSize=5.0; censored.markerSizeExplicit=true;
                double survival=1.0;
                int atRisk=subjects.size();
                int deaths=0;
                double median=qQNaN();
                curve.x.append(subjects.first().first); curve.y.append(1.0);
                for(int i=0;i<subjects.size();){
                    const double t=subjects[i].first;
                    int eventsHere=0,leaving=0;
                    while(i<subjects.size()&&subjects[i].first==t){
                        if(subjects[i].second) ++eventsHere;
                        ++leaving; ++i;
                    }
                    if(eventsHere>0&&atRisk>0){
                        survival*=(1.0-double(eventsHere)/double(atRisk));
                        deaths+=eventsHere;
                        curve.x.append(t); curve.y.append(survival);
                        if(!finite(median)&&survival<=0.5) median=t;
                    }else if(leaving>0){
                        censored.x.append(t); censored.y.append(survival);
                    }
                    atRisk-=leaving;
                }
                curve.label=QStringLiteral("%1 (%2 events of %3%4)")
                    .arg(in.series.at(0).label).arg(deaths).arg(subjects.size())
                    .arg(finite(median)?QStringLiteral(", median %1").arg(median,0,'g',4)
                                       :QStringLiteral(", median not reached"));
                out.series.append(curve);
                if(!censored.x.isEmpty()) out.series.append(censored);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("time");
        out.yAxis=PlotAxis{QStringLiteral("surviving fraction"),false,0.0,1.02};
        return out;
    }

    // A funnel plot: effect against precision, with the funnel a symmetrical
    // literature would fall inside. Asymmetry at the wide end is the signal -
    // small studies reporting only large effects - and it is invisible in a
    // forest plot of the same studies.
    if(in.engine==QLatin1String("Funnel Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& effect=in.series.at(0).y;
            const QVector<double>& error=in.series.at(1).y;
            const int n=qMin(effect.size(),error.size());
            PlotSeries pts;
            pts.label=QStringLiteral("studies");
            pts.color=in.series.at(0).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=5.0;
            double weighted=0.0,weight=0.0,widest=0.0;
            for(int i=0;i<n;++i){
                if(!finite(effect[i])||!(error[i]>0)) continue;
                pts.x.append(effect[i]); pts.y.append(error[i]);
                const double w=1.0/(error[i]*error[i]);
                weighted+=effect[i]*w; weight+=w;
                widest=qMax(widest,error[i]);
            }
            if(!pts.x.isEmpty()&&weight>0){
                const double summary=weighted/weight;
                pts.label=QStringLiteral("studies (summary %1, %2 of them)")
                              .arg(summary,0,'g',4).arg(pts.x.size());
                out.series.append(pts);
                PlotSeries centre,left,right;
                centre.label=QStringLiteral("fixed-effect summary");
                centre.color=in.style.warning;
                centre.drawLine=true; centre.lineWidth=1.0; centre.lineWidthExplicit=true;
                centre.x={summary,summary}; centre.y={0.0,widest};
                left.label=QStringLiteral("95%% funnel");
                left.color=in.style.foreground;
                left.drawLine=true; left.lineWidth=0.8; left.lineWidthExplicit=true;
                left.dashPattern={4.0,3.0};
                right=left; right.label.clear();
                left.x={summary-1.96*widest,summary}; left.y={widest,0.0};
                right.x={summary+1.96*widest,summary}; right.y={widest,0.0};
                out.series.append(centre);
                out.series.append(left);
                out.series.append(right);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("effect size");
        // Precision upward is the convention, and the axis is inverted by
        // giving it a maximum of zero at the top.
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("standard error");
        return out;
    }

    // X-bar and R. Subgroup means with limits from the average range, which is
    // how a control chart is actually set up: the within-subgroup range
    // estimates the process spread, and using the overall standard deviation
    // instead gives limits that are too wide to catch anything.
    if(in.engine==QLatin1String("X-bar and R Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        // Shewhart's constants, by subgroup size, from the standard tables.
        static const double kA2[11]={0,0,1.880,1.023,0.729,0.577,0.483,0.419,0.373,0.337,0.308};
        static const double kD3[11]={0,0,0.000,0.000,0.000,0.000,0.000,0.076,0.136,0.184,0.223};
        static const double kD4[11]={0,0,3.267,2.574,2.282,2.114,2.004,1.924,1.864,1.816,1.777};
        const int subgroup=qBound(2,in.series.size(),10);
        if(in.series.size()>=2){
            int rows=std::numeric_limits<int>::max();
            for(const PlotSeries& s:in.series) rows=qMin(rows,int(s.y.size()));
            PlotSeries means,ranges;
            means.label=QStringLiteral("subgroup mean");
            means.color=in.series.at(0).color;
            means.drawLine=true; means.drawMarkers=true; means.markerSize=3.5;
            ranges.label=QStringLiteral("subgroup range");
            ranges.color=in.style.warning;
            ranges.drawLine=true; ranges.drawMarkers=true; ranges.markerSize=3.0;
            double grand=0.0,averageRange=0.0; int counted=0;
            for(int r=0;r<rows;++r){
                double sum=0.0,lo=std::numeric_limits<double>::infinity(),hi=-lo;
                bool ok=true;
                for(int k=0;k<subgroup;++k){
                    const double v=in.series.at(k).y[r];
                    if(!finite(v)){ ok=false; break; }
                    sum+=v; lo=qMin(lo,v); hi=qMax(hi,v);
                }
                if(!ok) continue;
                const double mean=sum/double(subgroup);
                means.x.append(double(r+1)); means.y.append(mean);
                ranges.x.append(double(r+1)); ranges.y.append(hi-lo);
                grand+=mean; averageRange+=hi-lo; ++counted;
            }
            if(counted>0){
                grand/=double(counted); averageRange/=double(counted);
                const double a2=kA2[subgroup],d3=kD3[subgroup],d4=kD4[subgroup];
                const double upper=grand+a2*averageRange,lower=grand-a2*averageRange;
                int beyond=0;
                for(double v:means.y) if(v>upper||v<lower) ++beyond;
                means.label=QStringLiteral("subgroup mean (n %1, %2 beyond the limits)")
                                .arg(subgroup).arg(beyond);
                out.series.append(means);
                out.series.append(ranges);
                const auto level=[&out,&means](const QString& label,double value,const QColor& colour,
                                               bool dashed){
                    if(!finite(value)||means.x.isEmpty()) return;
                    PlotSeries line;
                    line.label=label; line.color=colour;
                    line.drawLine=true; line.lineWidth=0.8; line.lineWidthExplicit=true;
                    if(dashed) line.dashPattern={4.0,3.0};
                    line.x={means.x.first(),means.x.last()};
                    line.y={value,value};
                    out.series.append(line);
                };
                level(QStringLiteral("centre"),grand,in.style.foreground,false);
                level(QStringLiteral("mean limits"),upper,in.style.danger,true);
                level(QString(),lower,in.style.danger,true);
                level(QStringLiteral("range limits"),d4*averageRange,in.style.positive,true);
                if(d3>0) level(QString(),d3*averageRange,in.style.positive,true);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("subgroup"),false,unsetValue(),unsetValue()};
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("value");
        return out;
    }

    // Process capability: the distribution against the specification, with Cp
    // and Cpk. Cp alone says the spread would fit; Cpk says whether it fits
    // where it actually sits, and a process with Cp 2.0 and Cpk 0.6 is one that
    // could be capable and is not.
    //
    // Three mapped columns: the measurements, then the lower and upper limits,
    // read from the first finite value of each.
    if(in.engine==QLatin1String("Process Capability")){
        PlotSpec out=derivedAs(in,QStringLiteral("Histogram"));
        if(!in.series.isEmpty()){
            PlotSeries values=in.series.at(0);
            const QVector<double> v=finiteValues(values);
            double lsl=qQNaN(),usl=qQNaN();
            const auto firstFinite=[](const PlotSeries& s){
                for(double x:s.y) if(finite(x)) return x;
                return qQNaN();
            };
            if(in.series.size()>=2) lsl=firstFinite(in.series.at(1));
            if(in.series.size()>=3) usl=firstFinite(in.series.at(2));
            out.series.append(values);
            if(v.size()>=4){
                const double mean=meanOf(v),sigma=stdevOf(v);
                if(sigma>0&&finite(lsl)&&finite(usl)&&usl>lsl){
                    const double cp=(usl-lsl)/(6.0*sigma);
                    const double cpk=qMin(usl-mean,mean-lsl)/(3.0*sigma);
                    values.label=QStringLiteral("%1 (Cp %2, Cpk %3, mean %4, sigma %5)")
                        .arg(in.series.at(0).label).arg(cp,0,'f',2).arg(cpk,0,'f',2)
                        .arg(mean,0,'g',4).arg(sigma,0,'g',3);
                    out.series[0]=values;
                }
            }
            for(const auto& limit:{qMakePair(lsl,QStringLiteral("LSL")),
                                   qMakePair(usl,QStringLiteral("USL"))}){
                if(!finite(limit.first)) continue;
                PlotSeries line;
                line.label=limit.second;
                line.color=in.style.danger;
                line.drawLine=true; line.drawMarkers=false;
                line.lineWidth=1.0; line.lineWidthExplicit=true;
                line.dashPattern={4.0,3.0};
                // A histogram rewrite reads only the first series, so the
                // limits travel as their own and are drawn over it.
                line.x={limit.first,limit.first};
                line.y={0.0,1.0};
                out.series.append(line);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("measurement");
        return out;
    }

    // =====================================================================
    // Ocean, hydrology, energy and civil works
    // =====================================================================

    // Temperature against salinity, with the density contours a water mass is
    // actually identified by. Two samples at the same density and different
    // temperatures are different water masses, and on a plot without isopycnals
    // they are two dots with nothing to say so.
    //
    // Density is sigma-t from the UNESCO one-atmosphere equation of state,
    // which is the form oceanography quotes and is good to a hundredth of a
    // kg/m3 over the range any CTD returns.
    if(in.engine==QLatin1String("T-S Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& salinity=in.series.at(0).y;
            const QVector<double>& temperature=in.series.at(1).y;
            const int n=qMin(salinity.size(),temperature.size());
            PlotSeries pts;
            pts.label=QStringLiteral("samples");
            pts.color=in.series.at(0).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            double sLo=std::numeric_limits<double>::infinity(),sHi=-sLo;
            double tLo=sLo,tHi=-sLo;
            for(int i=0;i<n;++i){
                if(!finite(salinity[i])||!finite(temperature[i])) continue;
                pts.x.append(salinity[i]); pts.y.append(temperature[i]);
                sLo=qMin(sLo,salinity[i]); sHi=qMax(sHi,salinity[i]);
                tLo=qMin(tLo,temperature[i]); tHi=qMax(tHi,temperature[i]);
            }
            if(!pts.x.isEmpty()&&finite(sLo)&&sHi>sLo&&tHi>tLo){
                out.series.append(pts);
                // sigma-t: density at one atmosphere, minus 1000.
                const auto sigmaT=[](double S,double T){
                    const double rw=999.842594+6.793952e-2*T-9.095290e-3*T*T
                                   +1.001685e-4*T*T*T-1.120083e-6*T*T*T*T+6.536332e-9*T*T*T*T*T;
                    const double A=8.24493e-1-4.0899e-3*T+7.6438e-5*T*T
                                  -8.2467e-7*T*T*T+5.3875e-9*T*T*T*T;
                    const double B=-5.72466e-3+1.0227e-4*T-1.6546e-6*T*T;
                    const double C=4.8314e-4;
                    return rw+A*S+B*S*std::sqrt(qMax(0.0,S))+C*S*S-1000.0;
                };
                double densityLo=std::numeric_limits<double>::infinity(),densityHi=-densityLo;
                for(const double sv:{sLo,sHi})
                    for(const double tv:{tLo,tHi}){
                        const double d=sigmaT(sv,tv);
                        densityLo=qMin(densityLo,d); densityHi=qMax(densityHi,d);
                    }
                // Contours on a round interval, at most a dozen of them.
                const double span=densityHi-densityLo;
                double step=0.5;
                for(const double candidate:{0.1,0.2,0.5,1.0,2.0,5.0})
                    if(span/candidate<=12.0){ step=candidate; break; }
                bool labelled=false;
                for(double level=std::ceil(densityLo/step)*step;level<=densityHi;level+=step){
                    PlotSeries isopycnal;
                    isopycnal.label=labelled?QString()
                                            :QStringLiteral("sigma-t contours, %1 apart").arg(step);
                    labelled=true;
                    isopycnal.color=in.style.gridColor;
                    isopycnal.drawLine=true; isopycnal.drawMarkers=false;
                    isopycnal.lineWidth=0.7; isopycnal.lineWidthExplicit=true;
                    // Solve for the temperature at this density for each
                    // salinity, by bisection: sigma-t falls monotonically with
                    // temperature over the oceanographic range.
                    for(int i=0;i<=60;++i){
                        const double S=sLo+(sHi-sLo)*double(i)/60.0;
                        double lo=tLo-5.0,hi=tHi+5.0;
                        if((sigmaT(S,lo)-level)*(sigmaT(S,hi)-level)>0) continue;
                        for(int k=0;k<40;++k){
                            const double mid=0.5*(lo+hi);
                            if((sigmaT(S,lo)-level)*(sigmaT(S,mid)-level)<=0) hi=mid; else lo=mid;
                        }
                        isopycnal.x.append(S); isopycnal.y.append(0.5*(lo+hi));
                    }
                    if(isopycnal.x.size()>1) out.series.append(isopycnal);
                }
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("salinity");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("temperature");
        return out;
    }

    // A profile down through the water column, or down a borehole: the property
    // on x and depth on y, increasing downward. Depth is negated so the surface
    // is at the top, which is the only orientation this is ever read in.
    if(in.engine==QLatin1String("CTD Profile")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& depth=in.series.at(0).y;
            for(int k=1;k<in.series.size();++k){
                const PlotSeries& property=in.series.at(k);
                const int n=qMin(depth.size(),property.y.size());
                PlotSeries trace;
                trace.label=property.label; trace.color=property.color;
                trace.drawLine=true; trace.drawMarkers=false;
                for(int i=0;i<n;++i){
                    if(!finite(depth[i])||!finite(property.y[i])) continue;
                    trace.x.append(property.y[i]);
                    trace.y.append(std::abs(depth[i]));
                }
                if(!trace.x.isEmpty()) out.series.append(trace);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("measured property");
        // Depth as a positive number, on an axis that runs downward. Negating
        // the data was the way to do this before PlotAxis could be inverted,
        // and it put minus signs on every tick of an axis labelled "depth".
        out.yAxis=PlotAxis{QStringLiteral("depth"),false,unsetValue(),unsetValue(),true};
        return out;
    }

    // Stage against discharge, with the power law fitted through it. That
    // relation is what turns a level gauge - which is all most rivers have -
    // into a flow record, so its two coefficients are the useful output.
    if(in.engine==QLatin1String("Rating Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries pts;
            pts.color=s.color; pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.5;
            QVector<double> logStage,logFlow;
            for(int i=0;i<n;++i){
                const double stage=s.x[i],flow=s.y[i];
                if(!(stage>0)||!(flow>0)) continue;
                pts.x.append(stage); pts.y.append(flow);
                logStage.append(std::log(stage)); logFlow.append(std::log(flow));
            }
            if(pts.x.size()<3) continue;
            const LineFit line=fitLine(logStage,logFlow);
            if(line.ok){
                const double coefficient=std::exp(line.intercept);
                pts.label=QStringLiteral("%1 (Q = %2 h^%3)")
                              .arg(s.label).arg(coefficient,0,'g',4).arg(line.slope,0,'f',3);
                PlotSeries fit;
                fit.label=QStringLiteral("rating");
                fit.color=in.style.warning; fit.drawLine=true;
                fit.lineWidth=1.2; fit.lineWidthExplicit=true;
                double lo=std::numeric_limits<double>::infinity(),hi=-lo;
                for(double v:pts.x){ lo=qMin(lo,v); hi=qMax(hi,v); }
                for(int i=0;i<=80;++i){
                    const double stage=lo+(hi-lo)*double(i)/80.0;
                    fit.x.append(stage);
                    fit.y.append(coefficient*std::pow(stage,line.slope));
                }
                out.series.append(pts);
                out.series.append(fit);
                continue;
            }
            pts.label=s.label;
            out.series.append(pts);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("stage");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("discharge");
        return out;
    }

    // A turbine's power curve, with the three speeds that define it found from
    // the data: where it starts generating, where it reaches rated power, and
    // where it shuts down. A warranty is written against those three numbers.
    if(in.engine==QLatin1String("Wind Power Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Connected Scatter"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<4) continue;
            QVector<QPair<double,double>> points;
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                points.append({s.x[i],s.y[i]});
            }
            if(points.size()<4) continue;
            std::sort(points.begin(),points.end(),
                      [](const QPair<double,double>& a,const QPair<double,double>& b){
                          return a.first<b.first; });
            PlotSeries curve;
            curve.color=s.color; curve.drawLine=true; curve.drawMarkers=true; curve.markerSize=3.0;
            double rated=-std::numeric_limits<double>::infinity();
            for(const auto& pt:points){
                curve.x.append(pt.first); curve.y.append(pt.second);
                rated=qMax(rated,pt.second);
            }
            const double threshold=rated*0.01;
            double cutIn=qQNaN(),ratedSpeed=qQNaN(),cutOut=qQNaN();
            for(const auto& pt:points){
                if(!finite(cutIn)&&pt.second>threshold) cutIn=pt.first;
                if(!finite(ratedSpeed)&&pt.second>=rated*0.99) ratedSpeed=pt.first;
            }
            for(int i=points.size()-1;i>=0;--i)
                if(points[i].second>threshold){ cutOut=points[i].first; break; }
            curve.label=QStringLiteral("%1 (cut-in %2, rated %3 at %4, cut-out %5)")
                .arg(s.label)
                .arg(finite(cutIn)?QString::number(cutIn,'g',3):QStringLiteral("-"))
                .arg(rated,0,'g',4)
                .arg(finite(ratedSpeed)?QString::number(ratedSpeed,'g',3):QStringLiteral("-"))
                .arg(finite(cutOut)?QString::number(cutOut,'g',3):QStringLiteral("-"));
            out.series.append(curve);
            PlotSeries marks;
            marks.label=QStringLiteral("cut-in, rated, cut-out");
            marks.color=in.style.positive;
            marks.drawLine=false; marks.drawMarkers=true;
            marks.markerSize=7.0; marks.markerSizeExplicit=true;
            for(const double speed:{cutIn,ratedSpeed,cutOut}){
                if(!finite(speed)) continue;
                for(const auto& pt:points)
                    if(pt.first==speed){ marks.x.append(pt.first); marks.y.append(pt.second); break; }
            }
            if(!marks.x.isEmpty()) out.series.append(marks);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("wind speed");
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("power");
        return out;
    }

    // Drawdown: how far below its own running peak a series has fallen, and the
    // worst of those. A rising line with a bad month in it looks fine; the
    // drawdown of the same series shows the month.
    if(in.engine==QLatin1String("Drawdown Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Area"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries curve;
            curve.color=s.color; curve.drawLine=true; curve.drawMarkers=false;
            double peak=-std::numeric_limits<double>::infinity();
            double worst=0.0; double worstAt=qQNaN();
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                peak=qMax(peak,s.y[i]);
                // Relative where the peak is positive, absolute otherwise: a
                // per-cent drawdown of a quantity that passes through zero is
                // not a number anyone should be shown.
                const double fall=(peak>0)?(s.y[i]-peak)/peak*100.0:(s.y[i]-peak);
                curve.x.append(s.x[i]); curve.y.append(fall);
                if(fall<worst){ worst=fall; worstAt=s.x[i]; }
            }
            if(curve.x.isEmpty()) continue;
            curve.label=QStringLiteral("%1 (worst %2%3)")
                .arg(s.label).arg(worst,0,'f',1)
                .arg((peak>0)?QStringLiteral("%"):QString());
            out.series.append(curve);
            if(finite(worstAt)){
                PlotSeries mark;
                mark.label=QStringLiteral("maximum drawdown");
                mark.color=in.style.danger;
                mark.drawLine=false; mark.drawMarkers=true;
                mark.markerSize=7.0; mark.markerSizeExplicit=true;
                mark.x={worstAt}; mark.y={worst};
                out.series.append(mark);
            }
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("time");
        out.yAxis=PlotAxis{QStringLiteral("drawdown from the running peak"),false,unsetValue(),unsetValue()};
        return out;
    }

    // The mass haul diagram: cumulative cut minus fill along a chainage, with
    // the balance points where it crosses zero. Between two balance points the
    // earth moves within the job; outside them it is imported or carted away,
    // and that is the difference between a profitable job and a loss.
    if(in.engine==QLatin1String("Mass Haul Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("Area"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries curve,balance;
            curve.label=s.label; curve.color=s.color; curve.drawLine=true;
            balance.label=QStringLiteral("balance points");
            balance.color=in.style.positive;
            balance.drawLine=false; balance.drawMarkers=true;
            balance.markerSize=7.0; balance.markerSizeExplicit=true;
            double running=0.0,previous=qQNaN(),previousX=qQNaN();
            double highest=-std::numeric_limits<double>::infinity();
            double lowest=std::numeric_limits<double>::infinity();
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                running+=s.y[i];
                curve.x.append(s.x[i]); curve.y.append(running);
                highest=qMax(highest,running); lowest=qMin(lowest,running);
                if(finite(previous)&&((previous>0&&running<=0)||(previous<0&&running>=0))){
                    const double span=running-previous;
                    const double t=(std::abs(span)>1e-12)?(-previous/span):0.0;
                    balance.x.append(previousX+t*(s.x[i]-previousX));
                    balance.y.append(0.0);
                }
                previous=running; previousX=s.x[i];
            }
            if(curve.x.isEmpty()) continue;
            curve.label=QStringLiteral("%1 (net %2, most cut %3, most fill %4)")
                .arg(s.label).arg(running,0,'g',4).arg(highest,0,'g',4).arg(lowest,0,'g',4);
            out.series.append(curve);
            if(!balance.x.isEmpty()) out.series.append(balance);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("chainage");
        out.yAxis=PlotAxis{QStringLiteral("cumulative cut minus fill"),false,unsetValue(),unsetValue()};
        return out;
    }

    // Shear and bending moment from a distributed load along a beam: one
    // integral and then a second. They are drawn together because the pair is
    // read together - the moment peaks where the shear crosses zero, and seeing
    // that on one chart is the check that the integration is right.
    if(in.engine==QLatin1String("Shear and Moment")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<3) continue;
            PlotSeries shear,moment;
            shear.label=QStringLiteral("%1 shear").arg(s.label);
            shear.color=s.color; shear.drawLine=true;
            moment.label=QStringLiteral("%1 bending moment").arg(s.label);
            moment.color=in.style.warning; moment.drawLine=true;
            double v=0.0,m=0.0,previousX=qQNaN(),previousLoad=0.0,previousShear=0.0;
            double peakMoment=0.0,peakAt=qQNaN();
            for(int i=0;i<n;++i){
                if(!finite(s.x[i])||!finite(s.y[i])) continue;
                if(finite(previousX)){
                    const double dx=s.x[i]-previousX;
                    v+=0.5*(previousLoad+s.y[i])*dx;      // trapezoidal, both ways
                    m+=0.5*(previousShear+v)*dx;
                }
                shear.x.append(s.x[i]); shear.y.append(v);
                moment.x.append(s.x[i]); moment.y.append(m);
                if(std::abs(m)>std::abs(peakMoment)){ peakMoment=m; peakAt=s.x[i]; }
                previousX=s.x[i]; previousLoad=s.y[i]; previousShear=v;
            }
            if(shear.x.isEmpty()) continue;
            moment.label=QStringLiteral("%1 bending moment (peak %2 at %3)")
                .arg(s.label).arg(peakMoment,0,'g',4)
                .arg(finite(peakAt)?QString::number(peakAt,'g',4):QStringLiteral("-"));
            out.series.append(shear);
            out.series.append(moment);
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("position along the beam");
        out.yAxis=PlotAxis{QStringLiteral("shear and moment"),false,unsetValue(),unsetValue()};
        return out;
    }

    // An eye diagram: the signal cut into symbol-length pieces and laid on top
    // of each other. What it shows is the opening in the middle - how much
    // noise and timing error the link can take before a bit is read wrongly -
    // and that is invisible in the signal drawn end to end.
    //
    // The symbol period comes from the second mapped column's first value, or
    // from the dominant period in the signal when only one column is mapped.
    if(in.engine==QLatin1String("Eye Diagram")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(!in.series.isEmpty()){
            const PlotSeries& s=in.series.at(0);
            const int n=qMin(s.x.size(),s.y.size());
            double period=qQNaN();
            if(in.series.size()>=2)
                for(double v:in.series.at(1).y) if(finite(v)&&v>0){ period=v; break; }
            if(!finite(period)&&n>2){
                // Zero crossings of the centred signal: the median gap between
                // them is one half-symbol, so twice it is the period.
                double mean=0.0; int counted=0;
                for(int i=0;i<n;++i) if(finite(s.y[i])){ mean+=s.y[i]; ++counted; }
                if(counted>0){
                    mean/=double(counted);
                    QVector<double> gaps; double last=qQNaN();
                    for(int i=1;i<n;++i){
                        if(!finite(s.y[i])||!finite(s.y[i-1])) continue;
                        if((s.y[i-1]-mean)*(s.y[i]-mean)<0){
                            if(finite(last)) gaps.append(s.x[i]-last);
                            last=s.x[i];
                        }
                    }
                    if(!gaps.isEmpty()){
                        std::sort(gaps.begin(),gaps.end());
                        period=2.0*gaps[gaps.size()/2];
                    }
                }
            }
            if(finite(period)&&period>0&&n>2){
                const double start=s.x.first();
                PlotSeries trace;
                trace.color=s.color; trace.drawLine=true; trace.drawMarkers=false;
                trace.opacity=0.35;
                trace.lineWidth=0.8; trace.lineWidthExplicit=true;
                int sweeps=0;
                double previousPhase=-1.0;
                for(int i=0;i<n;++i){
                    if(!finite(s.x[i])||!finite(s.y[i])) continue;
                    // Two symbols wide, which is how an eye is drawn: the
                    // opening sits in the middle with a transition either side.
                    const double phase=std::fmod(s.x[i]-start,2.0*period);
                    if(phase<previousPhase){
                        if(trace.x.size()>1){ out.series.append(trace); ++sweeps; }
                        trace.x.clear(); trace.y.clear();
                        trace.label.clear();
                    }
                    previousPhase=phase;
                    trace.x.append(phase); trace.y.append(s.y[i]);
                }
                if(trace.x.size()>1){ out.series.append(trace); ++sweeps; }
                if(!out.series.isEmpty())
                    out.series[0].label=QStringLiteral("%1 (%2 sweeps, symbol %3)")
                                            .arg(s.label).arg(sweeps).arg(period,0,'g',4);
                out.legendVisible=true;
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("time within two symbols"),false,unsetValue(),unsetValue()};
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("amplitude");
        return out;
    }

    // A stereonet: dip direction and dip, on the equal-area projection that
    // structural geology counts orientations on. Drawn as a polar scatter with
    // the radius computed - equal-area means a cluster of readings covers the
    // same area of the net wherever it sits, which is the whole reason to count
    // on this projection rather than on a plain polar plot.
    if(in.engine==QLatin1String("Stereonet")){
        PlotSpec out=derivedAs(in,QStringLiteral("Polar Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& azimuth=in.series.at(0).y;
            const QVector<double>& dip=in.series.at(1).y;
            const int n=qMin(azimuth.size(),dip.size());
            PlotSeries poles;
            poles.label=QStringLiteral("poles to planes");
            poles.color=in.series.at(0).color;
            poles.drawLine=false; poles.drawMarkers=true; poles.markerSize=4.5;
            for(int i=0;i<n;++i){
                if(!finite(azimuth[i])||!finite(dip[i])) continue;
                const double plunge=qBound(0.0,90.0-dip[i],90.0);
                // Schmidt: r = R sqrt(2) sin((90 - plunge)/2), with R = 1.
                const double r=std::sqrt(2.0)*std::sin((90.0-plunge)*kDegToRad/2.0);
                // The pole to a plane lies opposite its dip direction.
                poles.x.append(std::fmod(azimuth[i]+180.0,360.0));
                poles.y.append(r);
            }
            if(!poles.x.isEmpty()) out.series.append(poles);
        }
        out.xAxis=PlotAxis{QStringLiteral("dip direction (deg)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("equal-area radius"),false,0.0,std::sqrt(2.0)};
        return out;
    }

    // A psychrometric chart: dry-bulb temperature against humidity ratio, with
    // the relative-humidity curves that make it readable drawn as part of the
    // figure rather than as a background image. Saturation pressure is the
    // Magnus form, which is within a tenth of a per cent over the range a
    // building is conditioned in.
    if(in.engine==QLatin1String("Psychrometric Chart")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        const auto saturation=[](double t){       // kPa
            return 0.61094*std::exp(17.625*t/(t+243.04));
        };
        const auto ratio=[&saturation](double t,double rh){   // kg water / kg dry air
            const double pressure=101.325;
            const double vapour=rh*saturation(t);
            return 0.62198*vapour/qMax(1e-9,pressure-vapour);
        };
        double tLo=0.0,tHi=50.0;
        if(in.series.size()>=2){
            const QVector<double>& dry=in.series.at(0).y;
            const QVector<double>& humidity=in.series.at(1).y;
            const int n=qMin(dry.size(),humidity.size());
            PlotSeries pts;
            pts.label=QStringLiteral("states");
            pts.color=in.series.at(0).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=5.0;
            double lo=std::numeric_limits<double>::infinity(),hi=-lo;
            for(int i=0;i<n;++i){
                if(!finite(dry[i])||!finite(humidity[i])) continue;
                // A humidity column above 1.5 is read as a percentage.
                const double rh=(humidity[i]>1.5)?humidity[i]/100.0:humidity[i];
                pts.x.append(dry[i]);
                pts.y.append(ratio(dry[i],qBound(0.0,rh,1.0))*1000.0);   // g/kg
                lo=qMin(lo,dry[i]); hi=qMax(hi,dry[i]);
            }
            if(!pts.x.isEmpty()){
                out.series.append(pts);
                tLo=std::floor(lo-2.0); tHi=std::ceil(hi+2.0);
            }
        }
        for(const int rh:{10,20,30,40,50,60,70,80,90,100}){
            PlotSeries line;
            line.label=(rh==100)?QStringLiteral("saturation")
                                :(rh==50?QStringLiteral("relative humidity, 10%% steps"):QString());
            line.color=(rh==100)?in.style.foreground:in.style.gridColor;
            line.drawLine=true; line.drawMarkers=false;
            line.lineWidth=(rh==100)?1.1:0.6; line.lineWidthExplicit=true;
            for(int i=0;i<=60;++i){
                const double t=tLo+(tHi-tLo)*double(i)/60.0;
                line.x.append(t);
                line.y.append(ratio(t,double(rh)/100.0)*1000.0);
            }
            out.series.append(line);
        }
        out.xAxis=PlotAxis{QStringLiteral("dry-bulb temperature (degC)"),false,tLo,tHi};
        out.yAxis=PlotAxis{QStringLiteral("humidity ratio (g/kg dry air)"),false,0.0,unsetValue()};
        return out;
    }

    // =====================================================================
    // The gaps a comparison against LabPlot and sci-draw turned up
    // =====================================================================

    // A dendrogram. The multivariate engine already computes the hierarchical
    // clustering; nothing could draw the tree it produces, so the one output
    // that says WHERE to cut - the height at which the merges suddenly get
    // expensive - had no picture.
    //
    // Two mapped columns: the merge heights, and the size of each merged
    // cluster. Drawn as a linkage tree with the leaves in merge order.
    if(in.engine==QLatin1String("Dendrogram")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        out.legendVisible=false;
        if(!in.series.isEmpty()){
            QVector<double> heights=finiteValues(in.series.at(0));
            if(heights.size()>=2){
                if(heights.size()>200) heights.resize(200);
                // The leaves sit at unit spacing; each merge joins the two
                // nearest unjoined positions and takes their midpoint, which
                // reproduces the shape of a linkage tree from the heights
                // alone.
                QVector<double> positions;
                for(int i=0;i<heights.size()+1;++i) positions.append(double(i));
                QVector<double> live=positions;
                QVector<double> base(live.size(),0.0);
                for(int m=0;m<heights.size()&&live.size()>=2;++m){
                    // Merge the closest pair, which for unit-spaced leaves is
                    // the pair with the smallest gap.
                    int at=0; double closest=std::numeric_limits<double>::infinity();
                    for(int i=0;i+1<live.size();++i){
                        const double gap=live[i+1]-live[i];
                        if(gap<closest){ closest=gap; at=i; }
                    }
                    const double left=live[at],right=live[at+1];
                    const double top=heights[m];
                    const double leftBase=base[at],rightBase=base[at+1];
                    PlotSeries link;
                    link.color=in.series.at(0).color;
                    link.drawLine=true; link.drawMarkers=false;
                    link.lineWidth=1.1; link.lineWidthExplicit=true;
                    link.x={left,left,right,right};
                    link.y={leftBase,top,top,rightBase};
                    out.series.append(link);
                    live[at]=0.5*(left+right);
                    base[at]=top;
                    live.remove(at+1); base.remove(at+1);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("observation"),false,unsetValue(),unsetValue()};
        if(out.yAxis.label.isEmpty()) out.yAxis.label=QStringLiteral("merge distance");
        return out;
    }

    // Precision against recall, with the average precision that summarises it.
    // On an unbalanced problem this is the curve that tells the truth: a ROC
    // can look excellent while almost every positive prediction is wrong,
    // because the false-positive rate is diluted by a huge negative class.
    //
    // Two mapped columns: the score, and the true label as 1 or 0.
    if(in.engine==QLatin1String("Precision-Recall Curve")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const QVector<double>& score=in.series.at(0).y;
            const QVector<double>& truth=in.series.at(1).y;
            const int n=qMin(score.size(),truth.size());
            QVector<QPair<double,bool>> rows;
            int positives=0;
            for(int i=0;i<n;++i){
                if(!finite(score[i])||!finite(truth[i])) continue;
                const bool positive=truth[i]>=0.5;
                rows.append({score[i],positive});
                if(positive) ++positives;
            }
            if(positives>0&&rows.size()>1){
                std::sort(rows.begin(),rows.end(),
                          [](const QPair<double,bool>& a,const QPair<double,bool>& b){
                              return a.first>b.first; });
                PlotSeries curve;
                curve.color=in.series.at(0).color;
                curve.drawLine=true; curve.drawMarkers=false;
                int truePositives=0,called=0;
                double average=0.0,previousRecall=0.0;
                for(const auto& row:rows){
                    ++called;
                    if(row.second) ++truePositives;
                    const double precision=double(truePositives)/double(called);
                    const double recall=double(truePositives)/double(positives);
                    // Average precision: the precision at each point weighted
                    // by the recall it gained, which is the area under this
                    // curve without interpolating between the points.
                    average+=precision*(recall-previousRecall);
                    previousRecall=recall;
                    curve.x.append(recall); curve.y.append(precision);
                }
                curve.label=QStringLiteral("%1 (average precision %2)")
                                .arg(in.series.at(0).label).arg(average,0,'f',3);
                out.series.append(curve);
                // The line a coin-flip classifier sits on: the prevalence.
                PlotSeries chance;
                chance.label=QStringLiteral("chance (%1 positive)")
                                 .arg(double(positives)/double(rows.size()),0,'f',3);
                chance.color=in.style.warning;
                chance.drawLine=true; chance.lineWidth=0.8; chance.lineWidthExplicit=true;
                chance.dashPattern={4.0,3.0};
                const double prevalence=double(positives)/double(rows.size());
                chance.x={0.0,1.0}; chance.y={prevalence,prevalence};
                out.series.append(chance);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("recall"),false,0.0,1.0};
        out.yAxis=PlotAxis{QStringLiteral("precision"),false,0.0,1.02};
        return out;
    }

    // A confusion matrix, as a grid with the counts on it. Two mapped columns -
    // the true class and the predicted one - and the accuracy in the title
    // line, because a matrix without it invites reading the diagonal and
    // guessing.
    if(in.engine==QLatin1String("Confusion Matrix")){
        PlotSpec out=derivedAs(in,QStringLiteral("2D Heatmap"));
        out.legendVisible=false;
        if(in.series.size()>=2){
            const QVector<double>& truth=in.series.at(0).y;
            const QVector<double>& predicted=in.series.at(1).y;
            const int n=qMin(truth.size(),predicted.size());
            QVector<double> classes;
            for(int i=0;i<n;++i){
                for(const double v:{truth[i],predicted[i]}){
                    if(!finite(v)) continue;
                    if(!classes.contains(v)) classes.append(v);
                }
            }
            std::sort(classes.begin(),classes.end());
            if(!classes.isEmpty()&&classes.size()<=20){
                QVector<double> cells(classes.size()*classes.size(),0.0);
                int correct=0,total=0;
                for(int i=0;i<n;++i){
                    if(!finite(truth[i])||!finite(predicted[i])) continue;
                    const int r=classes.indexOf(truth[i]);
                    const int c=classes.indexOf(predicted[i]);
                    if(r<0||c<0) continue;
                    cells[r*classes.size()+c]+=1.0;
                    if(r==c) ++correct;
                    ++total;
                }
                PlotSeries xi,yi,vi;
                for(int r=0;r<classes.size();++r)
                    for(int c=0;c<classes.size();++c){
                        xi.y.append(double(c)); yi.y.append(double(r));
                        vi.y.append(cells[r*classes.size()+c]);
                    }
                xi.label=QStringLiteral("predicted");
                yi.label=QStringLiteral("true");
                vi.label=total>0
                    ? QStringLiteral("count (accuracy %1)").arg(double(correct)/double(total),0,'f',3)
                    : QStringLiteral("count");
                out.series={xi,yi,vi};
                out.xAxis=PlotAxis{QStringLiteral("predicted class"),false,
                                   -0.5,double(classes.size())-0.5};
                out.yAxis=PlotAxis{QStringLiteral("true class"),false,
                                   -0.5,double(classes.size())-0.5};
                return out;
            }
        }
        return out;
    }

    // An MA plot: the log ratio between two conditions against their average
    // intensity. The shape is the point - a ratio that drifts with intensity is
    // a normalisation problem, not biology - and it is invisible in a plot of
    // one condition against the other.
    if(in.engine==QLatin1String("MA Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const QVector<double>& a=in.series.at(0).y;
            const QVector<double>& b=in.series.at(1).y;
            const int n=qMin(a.size(),b.size());
            PlotSeries pts;
            pts.color=in.series.at(0).color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=3.2;
            double sum=0.0; int counted=0;
            for(int i=0;i<n;++i){
                if(!(a[i]>0)||!(b[i]>0)) continue;      // a log needs a positive
                const double la=std::log2(a[i]),lb=std::log2(b[i]);
                pts.x.append(0.5*(la+lb));              // A, the average
                pts.y.append(la-lb);                    // M, the log ratio
                sum+=la-lb; ++counted;
            }
            if(counted>0){
                const double bias=sum/double(counted);
                pts.label=QStringLiteral("%1 vs %2 (median shift %3)")
                              .arg(in.series.at(0).label,in.series.at(1).label)
                              .arg(bias,0,'f',3);
                out.series.append(pts);
                PlotSeries zero;
                zero.label=QStringLiteral("no change");
                zero.color=in.style.warning;
                zero.drawLine=true; zero.lineWidth=0.9; zero.lineWidthExplicit=true;
                zero.dashPattern={4.0,3.0};
                double lo=std::numeric_limits<double>::infinity(),hi=-lo;
                for(double v:pts.x){ lo=qMin(lo,v); hi=qMax(hi,v); }
                zero.x={lo,hi}; zero.y={0.0,0.0};
                out.series.append(zero);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("A, mean log2 intensity"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("M, log2 ratio"),false,unsetValue(),unsetValue()};
        return out;
    }

    // The four attribute control charts. They differ only in what is counted
    // and how the limits are derived, so they are one rewrite with a table
    // rather than four near-identical ones:
    //
    //   p   the fraction defective, limits from the binomial, varying with n
    //   np  the number defective, constant n
    //   c   the count of defects per unit
    //   u   the count of defects per unit of opportunity, varying opportunity
    //
    // Mapped columns: the count, and for p, np and u the sample size beside it.
    if(in.engine==QLatin1String("p-Chart")||in.engine==QLatin1String("np-Chart")
       ||in.engine==QLatin1String("c-Chart")||in.engine==QLatin1String("u-Chart")){
        const bool proportion=(in.engine==QLatin1String("p-Chart"));
        const bool number=(in.engine==QLatin1String("np-Chart"));
        const bool perUnit=(in.engine==QLatin1String("u-Chart"));
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(!in.series.isEmpty()){
            const QVector<double>& counts=in.series.at(0).y;
            const QVector<double> sizes=(in.series.size()>=2)?in.series.at(1).y:QVector<double>();
            const int n=counts.size();
            PlotSeries values,upper,lower,centre;
            values.color=in.series.at(0).color;
            values.drawLine=true; values.drawMarkers=true; values.markerSize=3.5;
            upper.label=QStringLiteral("control limits");
            upper.color=in.style.danger;
            upper.drawLine=true; upper.lineWidth=0.9; upper.lineWidthExplicit=true;
            lower=upper; lower.label.clear();
            centre.label=QStringLiteral("centre");
            centre.color=in.style.foreground;
            centre.drawLine=true; centre.lineWidth=0.7; centre.lineWidthExplicit=true;
            centre.dashPattern={4.0,4.0};

            double totalCount=0.0,totalSize=0.0; int used=0;
            for(int i=0;i<n;++i){
                if(!finite(counts[i])||counts[i]<0) continue;
                const double size=(i<sizes.size()&&finite(sizes[i])&&sizes[i]>0)?sizes[i]:1.0;
                totalCount+=counts[i]; totalSize+=size; ++used;
            }
            if(used>0&&totalSize>0){
                // The centre line: a pooled proportion for p and np, a mean
                // count for c, a pooled rate for u. Pooling rather than
                // averaging the per-sample values is what makes the limits
                // right when the samples are different sizes.
                const double meanSize=totalSize/double(used);
                const double centreValue=
                    proportion ? totalCount/totalSize
                  : number     ? totalCount/double(used)
                  : perUnit    ? totalCount/totalSize
                               : totalCount/double(used);
                int beyond=0;
                for(int i=0;i<n;++i){
                    if(!finite(counts[i])||counts[i]<0) continue;
                    const double size=(i<sizes.size()&&finite(sizes[i])&&sizes[i]>0)?sizes[i]:1.0;
                    const double value=
                        proportion ? counts[i]/size
                      : perUnit    ? counts[i]/size
                                   : counts[i];
                    double spread=0.0;
                    if(proportion)      spread=std::sqrt(qMax(0.0,centreValue*(1.0-centreValue)/size));
                    else if(number)     spread=std::sqrt(qMax(0.0,centreValue*(1.0-centreValue/qMax(1.0,meanSize))));
                    else if(perUnit)    spread=std::sqrt(qMax(0.0,centreValue/size));
                    else                spread=std::sqrt(qMax(0.0,centreValue));
                    const double hi=centreValue+3.0*spread;
                    // A count cannot be negative, and neither can a rate.
                    const double lo=qMax(0.0,centreValue-3.0*spread);
                    if(value>hi||value<lo) ++beyond;
                    values.x.append(double(i+1)); values.y.append(value);
                    upper.x.append(double(i+1)); upper.y.append(hi);
                    lower.x.append(double(i+1)); lower.y.append(lo);
                    centre.x.append(double(i+1)); centre.y.append(centreValue);
                }
                values.label=QStringLiteral("%1 (%2, centre %3, %4 out of control)")
                    .arg(in.series.at(0).label)
                    .arg(in.engine).arg(centreValue,0,'g',4).arg(beyond);
                out.series.append(values);
                out.series.append(upper);
                out.series.append(lower);
                out.series.append(centre);
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("sample"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{proportion?QStringLiteral("fraction defective")
                          :number    ?QStringLiteral("number defective")
                          :perUnit   ?QStringLiteral("defects per unit")
                                     :QStringLiteral("defects per sample"),
                           false,unsetValue(),unsetValue()};
        return out;
    }

    // A rug: one tick per observation along the axis. It costs almost no ink
    // and it puts the actual sample back under a smoothed curve, which is the
    // one thing a density estimate hides - a beautiful KDE over eleven points
    // looks exactly like one over eleven thousand.
    if(in.engine==QLatin1String("Rug Plot")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        double slot=0.0;
        for(const PlotSeries& s:in.series){
            const QVector<double> v=finiteValues(s);
            if(v.isEmpty()) continue;
            double lo=std::numeric_limits<double>::infinity(),hi=-lo;
            for(double value:v){ lo=qMin(lo,value); hi=qMax(hi,value); }
            const double height=(hi>lo)?(hi-lo)*0.03:0.5;
            PlotSeries ticks;
            ticks.label=QStringLiteral("%1 (%2 observations)").arg(s.label).arg(v.size());
            ticks.color=s.color;
            ticks.drawLine=false; ticks.drawMarkers=true;
            ticks.markerSize=2.0; ticks.markerSizeExplicit=true;
            // A tick is two marks a hair apart rather than a drawn segment, so
            // ten thousand of them cost what ten thousand points cost and not
            // what ten thousand polylines cost.
            for(double value:v){
                ticks.x.append(value); ticks.y.append(slot);
                ticks.x.append(value); ticks.y.append(slot+height);
            }
            out.series.append(ticks);
            slot+=height*2.0;
        }
        if(out.xAxis.label.isEmpty()) out.xAxis.label=QStringLiteral("value");
        out.yAxis=PlotAxis{QStringLiteral("series"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------- Power Spectral Density
    // Periodogram of each mapped series, Hann-windowed, on log-log axes because
    // that is where power laws are straight lines and where the useful part of
    // a spectrum lives.
    if(in.engine==QLatin1String("Power Spectral Density")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<16) continue;
            // Sample interval from the mapped x column; 1.0 if it is not usable,
            // in which case the frequency axis is in cycles per sample and the
            // label says so.
            double dt=1.0;
            bool haveTime=false;
            if(s.x.size()>=2&&finite(s.x[0])&&finite(s.x[1])){
                const double d=(s.x[s.x.size()-1]-s.x[0])/double(s.x.size()-1);
                if(finite(d)&&std::abs(d)>1e-15){ dt=std::abs(d); haveTime=true; }
            }
            const int n=nextPowerOfTwo(v.size());
            QVector<double> re(n,0.0),im(n,0.0);
            const double mean=meanOf(v);
            // The catalogue variant picks the taper; Hann when nothing is asked
            // for, which is what this engine always used.
            const SpectralWindow taper=spectralWindowFor(in.variant);
            double windowPower=0.0;
            for(int i=0;i<v.size();++i){
                const double w=spectralWindow(taper,i,v.size());
                re[i]=(v[i]-mean)*w;      // remove the DC term or it swamps the plot
                windowPower+=w*w;
            }
            if(!(windowPower>0.0)) continue;
            fftInPlace(re,im);
            PlotSeries psd;
            psd.label=s.label; psd.color=s.color; psd.dashPattern=s.dashPattern;
            psd.lineWidth=qMax(1.2,s.lineWidth);
            psd.drawLine=true; psd.drawMarkers=false;
            const double scale=2.0*dt/windowPower;
            for(int k=1;k<n/2;++k){
                const double power=(re[k]*re[k]+im[k]*im[k])*scale;
                if(!(power>0.0)) continue;
                psd.x.append(double(k)/(double(n)*dt));
                psd.y.append(power);
            }
            if(psd.x.size()>=2) out.series.append(psd);
            out.xAxis=PlotAxis{haveTime?QStringLiteral("frequency"):QStringLiteral("cycles per sample"),
                               true,unsetValue(),unsetValue()};
        }
        out.yAxis=PlotAxis{QStringLiteral("power spectral density"),true,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------- Spectrogram
    // Short-time Fourier transform of the first mapped series, as a heatmap of
    // time against frequency. Packed into the three columns the field engines
    // read, so the existing heatmap draws it.
    if(in.engine==QLatin1String("Spectrogram")){
        PlotSpec out=in;
        out.engine=QStringLiteral("2D Heatmap");
        out.legendVisible=false;
        out.series.clear();
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<64) continue;
            double dt=1.0;
            if(s.x.size()>=2&&finite(s.x[0])&&finite(s.x[1])){
                const double d=(s.x[s.x.size()-1]-s.x[0])/double(s.x.size()-1);
                if(finite(d)&&std::abs(d)>1e-15) dt=std::abs(d);
            }
            // A window of a sixteenth of the record, half-overlapped: enough
            // frames to see something change, enough samples per frame to
            // resolve what changed.
            const int window=qBound(32,nextPowerOfTwo(v.size()/16),512);
            const int hop=qMax(1,window/2);
            const SpectralWindow taper=spectralWindowFor(in.variant);
            PlotSeries tx,fy,pv;
            for(int start=0;start+window<=v.size();start+=hop){
                QVector<double> re(window,0.0),im(window,0.0);
                double mean=0.0;
                for(int i=0;i<window;++i) mean+=v[start+i];
                mean/=double(window);
                for(int i=0;i<window;++i) re[i]=(v[start+i]-mean)*spectralWindow(taper,i,window);
                fftInPlace(re,im);
                const double centre=(double(start)+window/2.0)*dt;
                for(int k=1;k<window/2;++k){
                    const double power=re[k]*re[k]+im[k]*im[k];
                    tx.y.append(centre);
                    fy.y.append(double(k)/(double(window)*dt));
                    // Decibels: a linear spectrogram shows the loudest frame
                    // and nothing else.
                    pv.y.append(10.0*std::log10(qMax(1e-18,power)));
                }
            }
            tx.label=QStringLiteral("time"); fy.label=QStringLiteral("frequency");
            pv.label=QStringLiteral("dB");
            if(!tx.y.isEmpty()) out.series={tx,fy,pv};
            break;              // one signal, one spectrogram
        }
        out.xAxis=PlotAxis{QStringLiteral("time"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("frequency"),false,unsetValue(),unsetValue()};
        return out;
    }

    // --------------------------------------------------- Cross Correlation
    if(in.engine==QLatin1String("Cross Correlation")){
        PlotSpec out=derivedAs(in,QStringLiteral("Stem"));
        if(in.series.size()>=2){
            QVector<double> a=finiteValues(in.series.at(0));
            QVector<double> b=finiteValues(in.series.at(1));
            const int n=qMin(a.size(),b.size());
            if(n>=8){
                const double ma=meanOf(a), mb=meanOf(b);
                double da=0.0,db=0.0;
                for(int i=0;i<n;++i){ da+=(a[i]-ma)*(a[i]-ma); db+=(b[i]-mb)*(b[i]-mb); }
                const double denom=std::sqrt(da*db);
                if(denom>0.0){
                    PlotSeries xc;
                    xc.label=QStringLiteral("%1 x %2").arg(in.series.at(0).label,in.series.at(1).label);
                    xc.color=in.series.at(0).color;
                    const int maxLag=qMin(n/2,60);
                    for(int lag=-maxLag;lag<=maxLag;++lag){
                        double acc=0.0;
                        for(int i=0;i<n;++i){
                            const int j=i+lag;
                            if(j<0||j>=n) continue;
                            acc+=(a[i]-ma)*(b[j]-mb);
                        }
                        xc.x.append(double(lag));
                        xc.y.append(acc/denom);
                    }
                    out.series.append(xc);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("lag"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("cross correlation"),false,-1.0,1.0};
        return out;
    }

    // ---------------------------------------------------- Parallel Coordinates
    // One vertical axis per mapped column, one polyline per row. Each axis is
    // scaled to its own range, because the whole point is to compare the shape
    // of a row across variables whose units have nothing to do with each other.
    if(in.engine==QLatin1String("Parallel Coordinates")||in.engine==QLatin1String("Andrews Curves")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        out.legendVisible=false;
        const int axes=in.series.size();
        if(axes>=2){
            QVector<Bounds> spans;
            for(const PlotSeries& s:in.series) spans.append(boundsOf(s.y));
            int rows=std::numeric_limits<int>::max();
            for(const PlotSeries& s:in.series) rows=qMin(rows,int(s.y.size()));
            // A hundred polylines is already a smear; beyond that the plot is
            // ink rather than information.
            rows=qMin(rows,120);
            const bool andrews=(in.engine==QLatin1String("Andrews Curves"));
            for(int r=0;r<rows;++r){
                PlotSeries line;
                line.label=QString();
                line.color=in.series.at(r%axes).color;
                line.lineWidth=0.8;
                line.opacity=0.55;
                line.drawLine=true; line.drawMarkers=false;
                if(andrews){
                    // Andrews: each row becomes a Fourier series evaluated over
                    // -pi..pi, so similar rows trace similar curves. The
                    // classic definition, with the first term at 1/sqrt(2).
                    constexpr int kSamples=96;
                    for(int i=0;i<kSamples;++i){
                        const double t=-3.14159265358979323846
                                      +2.0*3.14159265358979323846*double(i)/double(kSamples-1);
                        double value=spans[0].scale(in.series.at(0).y.value(r))/std::sqrt(2.0);
                        for(int a=1;a<axes;++a){
                            const double coeff=spans[a].scale(in.series.at(a).y.value(r));
                            value+=(a%2==1)? coeff*std::sin(((a+1)/2)*t)
                                           : coeff*std::cos((a/2)*t);
                        }
                        line.x.append(t); line.y.append(value);
                    }
                }else{
                    for(int a=0;a<axes;++a){
                        line.x.append(double(a));
                        line.y.append(spans[a].scale(in.series.at(a).y.value(r)));
                    }
                }
                if(line.x.size()>=2) out.series.append(line);
            }
            if(!andrews){
                out.xAxis=PlotAxis{QStringLiteral("variable"),false,-0.2,double(axes)-0.8};
                out.yAxis=PlotAxis{QStringLiteral("scaled value"),false,0.0,1.0};
            }else{
                out.xAxis=PlotAxis{QStringLiteral("t"),false,unsetValue(),unsetValue()};
                out.yAxis=PlotAxis{QStringLiteral("f(t)"),false,unsetValue(),unsetValue()};
            }
        }
        return out;
    }

    // ---------------------------------------------------------- Slope Graph
    // Two states, one line per subject. It exists to show which things went up
    // and which went down, so the two columns are the only x positions there
    // are and the line between them is the whole message.
    if(in.engine==QLatin1String("Slope Graph")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const PlotSeries& before=in.series.at(0);
            const PlotSeries& after=in.series.at(1);
            const int n=qMin(before.y.size(),after.y.size());
            for(int i=0;i<n&&i<60;++i){
                if(!finite(before.y[i])||!finite(after.y[i])) continue;
                PlotSeries slope;
                slope.label=QString();
                // Rising and falling in different colours: the direction is the
                // finding and it should not need tracing by eye.
                slope.color=(after.y[i]>=before.y[i])?in.style.positive:in.style.danger;
                slope.lineWidth=1.1;
                slope.drawLine=true; slope.drawMarkers=true; slope.markerSize=4.0;
                slope.x={0.0,1.0};
                slope.y={before.y[i],after.y[i]};
                out.series.append(slope);
            }
        }
        out.legendVisible=false;
        out.xAxis=PlotAxis{QStringLiteral("state"),false,-0.15,1.15};
        return out;
    }

    // ------------------------------------------------------------ Waterfall
    // Each value as a step from where the last one left off, so a running total
    // is built and broken down in the same picture. The final bar is the total
    // itself, from zero.
    if(in.engine==QLatin1String("Waterfall")){
        PlotSpec out=in;
        // Was "Error Bar", which reads only series[0] as values and series[1]
        // as their uncertainty - so forty one-point step series collapsed into
        // a single drawn point. A floating bar is what a waterfall step is.
        out.engine=QStringLiteral("Floating Bar");
        out.series.clear();
        out.legendVisible=false;
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.isEmpty()) continue;
            double running=0.0;
            for(int i=0;i<v.size()&&i<40;++i){
                PlotSeries step;
                step.label=QString();
                step.color=(v[i]>=0)?in.style.positive:in.style.danger;
                // Carried as {centre, low, high} the way the error bar draw
                // already reads, so a floating bar needs no new geometry.
                const double lo=qMin(running,running+v[i]);
                const double hi=qMax(running,running+v[i]);
                step.x={double(i+1)};
                step.y={lo,hi};
                out.series.append(step);
                running+=v[i];
            }
            PlotSeries total;
            total.label=QStringLiteral("total");
            total.color=in.style.foreground;
            const double lo=qMin(0.0,running), hi=qMax(0.0,running);
            total.x={double(qMin(v.size(),40)+1)};
            total.y={lo,hi};
            out.series.append(total);
            break;
        }
        out.xAxis=PlotAxis{QStringLiteral("step"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------- Confidence Ellipse
    // The scatter with the covariance ellipse that contains 95% of a bivariate
    // normal with the same moments. Drawn from the eigenvectors, which is where
    // the orientation and the two radii come from.
    if(in.engine==QLatin1String("Confidence Ellipse")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        if(in.series.size()>=2){
            const PlotSeries& xs=in.series.at(0);
            const PlotSeries& ys=in.series.at(1);
            const int n=qMin(xs.y.size(),ys.y.size());
            PlotSeries pts;
            pts.label=QStringLiteral("observations");
            pts.color=xs.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=3.6;
            QVector<double> vx,vy;
            for(int i=0;i<n;++i){
                if(!finite(xs.y[i])||!finite(ys.y[i])) continue;
                pts.x.append(xs.y[i]); pts.y.append(ys.y[i]);
                vx.append(xs.y[i]); vy.append(ys.y[i]);
            }
            if(vx.size()>=3){
                out.series.append(pts);
                const Moments2 m=momentsOf(vx,vy);
                const double mx=m.mx, my=m.my;
                const double d=double(m.n-1);
                const double sxx=m.sxx/d, syy=m.syy/d, sxy=m.sxy/d;
                // Eigenvalues of a symmetric 2x2, in closed form.
                const double tr=sxx+syy, det=sxx*syy-sxy*sxy;
                const double disc=std::sqrt(qMax(0.0,tr*tr/4.0-det));
                const double l1=tr/2.0+disc, l2=tr/2.0-disc;
                const double angle=(std::abs(sxy)<1e-15)?0.0:std::atan2(l1-sxx,sxy);
                // chi-square with two degrees of freedom, 95%.
                const double k=std::sqrt(5.991);
                PlotSeries ellipse;
                ellipse.label=QStringLiteral("95% ellipse");
                ellipse.color=in.style.warning;
                ellipse.lineWidth=1.3;
                ellipse.drawLine=true; ellipse.drawMarkers=false;
                for(int i=0;i<=96;++i){
                    const double t=2.0*3.14159265358979323846*double(i)/96.0;
                    const double ex=k*std::sqrt(qMax(0.0,l1))*std::cos(t);
                    const double ey=k*std::sqrt(qMax(0.0,l2))*std::sin(t);
                    ellipse.x.append(mx+ex*std::cos(angle)-ey*std::sin(angle));
                    ellipse.y.append(my+ex*std::sin(angle)+ey*std::cos(angle));
                }
                out.series.append(ellipse);
            }
            out.xAxis.label=xs.label;
            out.yAxis.label=ys.label;
        }
        return out;
    }

    // ------------------------------------------------------------ Ridgeline
    // Stacked densities, each on its own baseline. It reads as a set of
    // distributions changing across groups, which overlaying them on one axis
    // does not.
    if(in.engine==QLatin1String("Ridgeline")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        int row=0;
        // The offset is a fraction of the tallest ridge, so the overlap looks
        // the same whatever the densities happen to be.
        QVector<PlotSeries> curves;
        double tallest=0.0;
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<5) continue;
            std::sort(v.begin(),v.end());
            const KdeCurve kde=kdeCurve(v,128,3.0);
            if(!kde.valid) continue;
            PlotSeries curve;
            curve.label=s.label; curve.color=s.color;
            curve.lineWidth=1.2; curve.drawLine=true; curve.drawMarkers=false;
            curve.x=kde.pos;
            curve.y=kde.density;
            tallest=qMax(tallest,kde.peak);
            curves.append(curve);
        }
        for(PlotSeries& curve:curves){
            const double offset=double(row)*tallest*0.55;
            for(double& y:curve.y) y+=offset;
            out.series.append(curve);
            ++row;
        }
        out.yAxis=PlotAxis{QStringLiteral("density, offset per group"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------------------ Population Pyramid
    // Two groups back to back on a shared category axis, one drawn negative.
    if(in.engine==QLatin1String("Population Pyramid")){
        PlotSpec out=derivedAs(in,QStringLiteral("Horizontal Bar"));
        if(in.series.size()>=2){
            for(int side=0;side<2;++side){
                const PlotSeries& s=in.series.at(side);
                for(int i=0;i<s.y.size()&&i<40;++i){
                    if(!finite(s.y[i])) continue;
                    PlotSeries bar;
                    bar.label=(i==0)?s.label:QString();
                    bar.color=s.color;
                    // The left-hand group is negated so the two grow apart from
                    // a shared spine, which is what makes it a pyramid. Value
                    // on x, category row on y - the order Horizontal Bar reads.
                    bar.x={(side==0)?-std::abs(s.y[i]):std::abs(s.y[i])};
                    bar.y={double(i+1)};
                    out.series.append(bar);
                }
            }
        }
        out.xAxis=PlotAxis{QStringLiteral("count"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("group"),false,unsetValue(),unsetValue()};
        return out;
    }

    // -------------------------------------------------------- Ternary Scatter
    // Three components summing to a whole, projected onto the triangle. Each
    // row is normalised first: compositions that do not sum to one are the
    // normal case, not an error.
    if(in.engine==QLatin1String("Ternary Scatter")||in.engine==QLatin1String("Piper Diagram")){
        PlotSpec out=in;
        // Line Chart, not Scatter: the scatter draw only marks points, and the
        // triangle that makes the projection legible is a line. The line chart
        // honours both flags per series, so one target draws both.
        out.engine=QStringLiteral("Line Chart");
        out.series.clear();
        out.legendVisible=false;
        if(in.series.size()>=3){
            const PlotSeries& a=in.series.at(0);
            const PlotSeries& b=in.series.at(1);
            const PlotSeries& c=in.series.at(2);
            const int n=qMin(a.y.size(),qMin(b.y.size(),c.y.size()));
            PlotSeries pts;
            pts.label=QStringLiteral("composition");
            pts.color=a.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=4.0;
            for(int i=0;i<n;++i){
                if(!finite(a.y[i])||!finite(b.y[i])||!finite(c.y[i])) continue;
                const double total=std::abs(a.y[i])+std::abs(b.y[i])+std::abs(c.y[i]);
                if(!(total>0.0)) continue;
                const double pa=std::abs(a.y[i])/total;
                const double pc=std::abs(c.y[i])/total;
                // Standard barycentric placement on an equilateral triangle.
                pts.x.append(pa+pc/2.0);
                pts.y.append(pc*std::sqrt(3.0)/2.0);
            }
            if(!pts.x.isEmpty()) out.series.append(pts);

            // The triangle itself, so the projection is legible as one.
            PlotSeries frame;
            frame.label=QStringLiteral("%1 / %2 / %3").arg(a.label,b.label,c.label);
            frame.color=in.style.gridColor;
            frame.lineWidth=0.9; frame.drawLine=true; frame.drawMarkers=false;
            frame.x={0.0,1.0,0.5,0.0};
            frame.y={0.0,0.0,std::sqrt(3.0)/2.0,0.0};
            out.series.append(frame);
        }
        out.xAxis=PlotAxis{QString(),false,-0.06,1.06};
        out.yAxis=PlotAxis{QString(),false,-0.06,0.94};
        return out;
    }

    // --------------------------------------------------------------- Patch
    // A closed filled polygon from the mapped coordinates: a region rather than
    // a trace. Used for boundaries, footprints and shaded domains.
    if(in.engine==QLatin1String("Patch")){
        PlotSpec out=derivedAs(in,QStringLiteral("Area"));
        for(const PlotSeries& s:in.series){
            PlotSeries patch=s;
            // Closed explicitly, or the fill runs to the baseline instead of
            // back to where the outline started.
            if(patch.x.size()>=3){
                patch.x.append(patch.x.first());
                patch.y.append(patch.y.first());
            }
            patch.opacity=0.55;
            out.series.append(patch);
        }
        return out;
    }

    // ------------------------------------------------- Comet / Animated Line
    // Both animate in GraphVis 17. A still figure cannot animate, so what is
    // drawn is the state these settle into: the whole trace, with the head
    // marked. Saying that plainly is better than drawing a static plot and
    // calling it an animation.
    if(in.engine==QLatin1String("Comet")||in.engine==QLatin1String("Animated Line")){
        PlotSpec out=derivedAs(in,QStringLiteral("Line Chart"));
        for(const PlotSeries& s:in.series){
            const int n=qMin(s.x.size(),s.y.size());
            if(n<2) continue;
            PlotSeries trail=s;
            trail.drawLine=true; trail.drawMarkers=false;
            trail.opacity=0.5;
            out.series.append(trail);

            PlotSeries head;
            head.label=QStringLiteral("%1 (head)").arg(s.label);
            head.color=s.color;
            head.drawLine=false; head.drawMarkers=true; head.markerSize=7.0;
            head.x={s.x[n-1]}; head.y={s.y[n-1]};
            out.series.append(head);
        }
        return out;
    }

    // --------------------------------------------- Scatter + Marginals / Plot Matrix
    // Both are small multiples, and this backend draws one figure. What it can
    // do honestly is the panel that carries the information: the joint scatter
    // for the first, the first pair for the second, each with the correlation
    // stated so the number that a matrix is scanned for is not lost.
    if(in.engine==QLatin1String("Scatter + Marginals")||in.engine==QLatin1String("Plot Matrix")){
        PlotSpec out=derivedAs(in,QStringLiteral("4D / 5D Scatter"));
        if(in.series.size()>=2){
            const PlotSeries& xs=in.series.at(0);
            const PlotSeries& ys=in.series.at(1);
            const int n=qMin(xs.y.size(),ys.y.size());
            PlotSeries pts;
            pts.color=xs.color;
            pts.drawLine=false; pts.drawMarkers=true; pts.markerSize=3.8;
            QVector<double> vx,vy;
            for(int i=0;i<n;++i){
                if(!finite(xs.y[i])||!finite(ys.y[i])) continue;
                pts.x.append(xs.y[i]); pts.y.append(ys.y[i]);
                vx.append(xs.y[i]); vy.append(ys.y[i]);
            }
            const double r=(vx.size()>=3)?pearson(momentsOf(vx,vy)):0.0;
            pts.label=QStringLiteral("r = %1").arg(r,0,'f',3);
            if(!pts.x.isEmpty()) out.series.append(pts);
            out.xAxis.label=xs.label;
            out.yAxis.label=ys.label;
        }
        return out;
    }

    // -------------------------------------------------- Function and Implicit
    // These plot a formula rather than a dataset. The expression is compiled
    // once and evaluated over a grid; a default is used when nothing has been
    // typed, so the entry draws something recognisable instead of an empty
    // frame the first time it is opened.
    if(in.engine.startsWith(QLatin1String("Function"))
       ||in.engine.startsWith(QLatin1String("Implicit"))){
        PlotSpec out=in;
        const bool parametric=in.engine==QLatin1String("Function 3D Parametric");
        const bool surface=in.engine==QLatin1String("Function Surface")
                         ||in.engine==QLatin1String("Function Mesh")
                         ||in.engine==QLatin1String("Implicit Surface");
        const bool contour=in.engine==QLatin1String("Function Contour")
                         ||in.engine==QLatin1String("Implicit Function");

        QString text=in.expression.trimmed();
        if(text.isEmpty()){
            if(parametric)     text=QStringLiteral("cos(t); sin(t); t/6");
            else if(surface||contour) text=QStringLiteral("sin(x)*cos(y)");
            else               text=QStringLiteral("sin(x)*exp(-x/6)");
        }

        // The domain follows the mapped data when there is any, so a formula
        // can be overlaid on a measurement rather than plotted beside it.
        double lo=-10.0, hi=10.0;
        if(!in.series.isEmpty()){
            const Bounds span=boundsOf(in.series.first().y);
            if(span.hi>span.lo){ lo=span.lo; hi=span.hi; }
        }

        out.series.clear();
        out.legendVisible=false;

        if(parametric){
            const QStringList partsText=text.split(QLatin1Char(';'));
            Expression fx,fy,fz;
            const QStringList vars{QStringLiteral("t")};
            if(partsText.size()>=3&&fx.compile(partsText.at(0),vars)
               &&fy.compile(partsText.at(1),vars)&&fz.compile(partsText.at(2),vars)){
                PlotSeries px,py,pz;
                px.label=QStringLiteral("x(t)"); py.label=QStringLiteral("y(t)");
                pz.label=QStringLiteral("z(t)");
                px.y.reserve(600); py.y.reserve(600); pz.y.reserve(600);
                QVector<double> at(1);
                for(int i=0;i<600;++i){
                    at[0]=lo+(hi-lo)*double(i)/599.0;
                    px.y.append(fx.evaluate(at));
                    py.y.append(fy.evaluate(at));
                    pz.y.append(fz.evaluate(at));
                }
                out.series={px,py,pz};
                out.engine=QStringLiteral("3D Line");
            }else{
                out.title=QStringLiteral("%1 — needs three formulas separated by ; (x; y; z)")
                              .arg(in.engine);
            }
            return out;
        }

        if(surface||contour){
            Expression f;
            const QStringList vars{QStringLiteral("x"),QStringLiteral("y")};
            if(!f.compile(text,vars)){
                out.title=QStringLiteral("%1 — %2").arg(in.engine,f.error());
                return out;
            }
            const bool implicit=in.engine.startsWith(QLatin1String("Implicit"));
            constexpr int kGrid=90;
            PlotSeries gx,gy,gv;
            gx.label=QStringLiteral("x"); gy.label=QStringLiteral("y");
            gv.label=text;
            gx.y.reserve(kGrid*kGrid); gy.y.reserve(kGrid*kGrid); gv.y.reserve(kGrid*kGrid);
            // Hoisted: `f.evaluate({x,y})` brace-constructs - and therefore
            // heap-allocates - a two-element QVector for every one of the 8100
            // grid points.
            QVector<double> at(2);
            for(int j=0;j<kGrid;++j){
                for(int i=0;i<kGrid;++i){
                    const double x=lo+(hi-lo)*double(i)/double(kGrid-1);
                    const double y=lo+(hi-lo)*double(j)/double(kGrid-1);
                    at[0]=x; at[1]=y;
                    const double v=f.evaluate(at);
                    if(!finite(v)) continue;
                    gx.y.append(x); gy.y.append(y); gv.y.append(v);
                }
            }
            out.series={gx,gy,gv};
            // An implicit curve is the zero level set, which is exactly what
            // the contour engine draws; an implicit surface is the same set in
            // three dimensions, so the surface engine gets it.
            out.engine=(in.engine==QLatin1String("Function Mesh"))    ? QStringLiteral("3D Mesh")
                      :(in.engine==QLatin1String("Function Surface")) ? QStringLiteral("3D Topography / Surface")
                      :(in.engine==QLatin1String("Implicit Surface")) ? QStringLiteral("3D Topography / Surface")
                                                                     : QStringLiteral("2D Contour");
            Q_UNUSED(implicit)
            out.xAxis=PlotAxis{QStringLiteral("x"),false,unsetValue(),unsetValue()};
            out.yAxis=PlotAxis{QStringLiteral("y"),false,unsetValue(),unsetValue()};
            return out;
        }

        // Function Plot: y = f(x).
        Expression f;
        if(!f.compile(text,{QStringLiteral("x")})){
            out.title=QStringLiteral("%1 — %2").arg(in.engine,f.error());
            out.engine=QStringLiteral("Line Chart");
            return out;
        }
        PlotSeries curve;
        curve.label=text;
        curve.color=in.series.isEmpty()?QColor(0x66,0x9d,0xdb):in.series.first().color;
        curve.lineWidth=1.5;
        curve.drawLine=true; curve.drawMarkers=false;
        curve.x.reserve(800); curve.y.reserve(800);
        QVector<double> at(1);
        for(int i=0;i<800;++i){
            const double x=lo+(hi-lo)*double(i)/799.0;
            at[0]=x;
            const double y=f.evaluate(at);
            // A pole leaves a gap rather than a vertical stroke across the plot.
            if(!finite(y)) continue;
            curve.x.append(x); curve.y.append(y);
        }
        out.series.append(curve);
        out.engine=QStringLiteral("Line Chart");
        out.xAxis=PlotAxis{QStringLiteral("x"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{text,false,unsetValue(),unsetValue()};
        return out;
    }

    if(in.engine==QLatin1String("Histogram")){
        PlotSpec out=in;
        out.series.clear();
        out.legendVisible=false;
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<2) continue;
            std::sort(v.begin(),v.end());
            const double lo=v.first(), hi=v.last();
            if(!(hi>lo)) continue;
            // Freedman-Diaconis, falling back to Sturges for tiny samples.
            // quantileOf, not v[int(n*0.25)]: the uninterpolated form used here
            // is a different and more biased estimator, so the bin width
            // disagreed with the IQR the box plot draws for the same column.
            const double q1=quantileOf(v,0.25), q3=quantileOf(v,0.75);
            const double iqr=q3-q1;
            int bins;
            if(iqr>0){
                const double width=2.0*iqr/std::cbrt(double(v.size()));
                bins=int(std::ceil((hi-lo)/qMax(width,1e-12)));
            }else{
                bins=int(std::ceil(std::log2(double(v.size()))+1.0));
            }
            bins=qBound(5,bins,120);
            const double width=(hi-lo)/bins;
            QVector<double> counts(bins,0.0);
            for(double x:v){
                int b=int((x-lo)/width);
                counts[qBound(0,b,bins-1)]+=1.0;
            }
            PlotSeries bar;
            bar.label=s.label;
            bar.color=s.color;
            bar.drawLine=false;
            for(int i=0;i<bins;++i){ bar.x.append(lo+width*(i+0.5)); bar.y.append(counts[i]); }
            out.series.append(bar);
        }
        out.xAxis=PlotAxis{in.yAxis.label.isEmpty()?in.series.isEmpty()?QString():in.series.first().label:in.yAxis.label,false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("count"),false,0.0,unsetValue()};
        return out;
    }

    if(in.engine==QLatin1String("Box Plot")){
        // One box per mapped series, placed at x = 1..N. The five summary
        // values are carried in y so the draw step needs no extra structure.
        PlotSpec out=in;
        out.series.clear();
        out.legendVisible=false;
        int slot=1;
        for(const PlotSeries& s:in.series){
            QVector<double> v=finiteValues(s);
            if(v.size()<5){ ++slot; continue; }
            std::sort(v.begin(),v.end());
            const double q1=quantileOf(v,0.25), med=quantileOf(v,0.5), q3=quantileOf(v,0.75);
            const double iqr=q3-q1;
            double lo=q1-1.5*iqr, hi=q3+1.5*iqr;
            // Whiskers reach the furthest observation inside the fences.
            double wlo=v.first(), whi=v.last();
            for(double x:v){ if(x>=lo){ wlo=x; break; } }
            for(int i=v.size()-1;i>=0;--i){ if(v[i]<=hi){ whi=v[i]; break; } }
            PlotSeries box;
            box.label=s.label;
            box.color=s.color;
            box.x={double(slot)};
            box.y={wlo,q1,med,q3,whi};
            out.series.append(box);
            ++slot;
        }
        out.xAxis=PlotAxis{QStringLiteral("series"),false,0.5,double(slot)-0.5};
        out.yAxis=PlotAxis{in.yAxis.label,false,unsetValue(),unsetValue()};
        return out;
    }

    return in;
}

void QtPlotBackend::drawStem(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    const double baseY=f.plotArea.bottom()-((qBound(f.yLo,0.0,f.yHi)-f.yLo)/qMax(1e-300,f.yHi-f.yLo))*f.plotArea.height();
    for(const PlotSeries& s:spec.series){
        const int n=qMin(s.x.size(),s.y.size());
        p->save();
        QPen pen(s.color); pen.setWidthF(qMax(0.4,s.lineWidth)); applySeriesDash(pen,s); p->setPen(pen);
        for(int i=0;i<n;++i){
            const double x=s.x[i],y=s.y[i];
            if(!finite(x)||!finite(y)||(f.xLog&&x<=0)||(f.yLog&&y<=0)) continue;
            const QPointF pt=toDevice(f,x,y);
            p->drawLine(QPointF(pt.x(),baseY),pt);
        }
        p->setPen(Qt::NoPen); p->setBrush(s.color);
        drawSeriesMarkers(p,f,s,1.0);
        p->restore();
    }
}

void QtPlotBackend::drawHorizontalBar(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    // In a horizontal bar chart the axes really are swapped: x carries the
    // VALUE and y carries the CATEGORY. This used to read the value out of y
    // and take the row from the point's position in the array, so the bar
    // lengths were scaled against an axis fitted to the category column and
    // every row landed in one thin band in the middle of the plot. Both
    // engines that reach it emit one series per bar, which made the fault
    // total rather than partial.
    int total=0;
    for(const PlotSeries& s:spec.series) total=qMax(total,qMin(s.x.size(),s.y.size()));
    if(total<=0) return;

    // How many bars share a row, and which of them this one is. One series per
    // bar is the normal shape here - a population pyramid emits eighty - so the
    // series index is NOT the group index: eighty series across forty rows are
    // forty pairs, not eighty groups.
    auto rowKey=[](double v){ return QString::number(v,'g',12); };
    QHash<QString,int> perRow;
    QVector<double> rows;
    for(const PlotSeries& s:spec.series){
        const int n=qMin(s.x.size(),s.y.size());
        for(int i=0;i<n;++i){
            if(!finite(s.y[i])||(f.yLog&&s.y[i]<=0)) continue;
            if(perRow[rowKey(s.y[i])]++==0) rows.push_back(toDevice(f,f.xLo,s.y[i]).y());
        }
    }
    if(rows.isEmpty()) return;
    int groups=1;
    for(int count:std::as_const(perRow)) groups=qMax(groups,count);

    const double slot=slotWidthFrom(rows,f.plotArea.height()/double(qMax(1,rows.size())),
                                    f.plotArea.height());
    const double barH=qMax(1.0,(slot*0.78)/double(groups));
    const double baseX=toDevice(f,qBound(f.xLo,0.0,f.xHi),f.yLo).x();

    QHash<QString,int> ordinal;
    for(const PlotSeries& s:spec.series){
        const int n=qMin(s.x.size(),s.y.size());
        p->save(); p->setPen(Qt::NoPen); p->setBrush(s.color);
        p->setOpacity(qBound(0.0,s.opacity,1.0));
        for(int i=0;i<n;++i){
            const double value=s.x[i], category=s.y[i];
            if(!finite(value)||!finite(category)) continue;
            if((f.xLog&&value<=0)||(f.yLog&&category<=0)) continue;
            const int g=ordinal[rowKey(category)]++;
            const QPointF pt=toDevice(f,value,category);
            const double top=pt.y()-(groups*barH)/2.0+g*barH;
            p->drawRect(QRectF(qMin(baseX,pt.x()),top,std::abs(pt.x()-baseX),barH));
        }
        p->restore();
    }
}

void QtPlotBackend::drawStackedLines(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    // Cumulative bands: each series is drawn on top of the running total.
    if(spec.series.isEmpty()) return;
    int n=spec.series.first().x.size();
    for(const PlotSeries& s:spec.series) n=qMin(n,qMin(s.x.size(),s.y.size()));
    if(n<2) return;
    QVector<double> lower(n,0.0);
    for(const PlotSeries& s:spec.series){
        QPainterPath band;
        for(int i=0;i<n;++i){
            const double y=lower[i]+(finite(s.y[i])?s.y[i]:0.0);
            const QPointF pt=toDevice(f,s.x[i],y);
            if(i==0) band.moveTo(pt); else band.lineTo(pt);
        }
        for(int i=n-1;i>=0;--i) band.lineTo(toDevice(f,s.x[i],lower[i]));
        band.closeSubpath();
        QColor fill=s.color; fill.setAlphaF(0.55);
        QPen pen(s.color,qMax(0.3,s.lineWidth)); applySeriesDash(pen,s);
        p->save(); p->setPen(pen); p->setBrush(fill);
        p->drawPath(band); p->restore();
        for(int i=0;i<n;++i) lower[i]+=finite(s.y[i])?s.y[i]:0.0;
    }
}

void QtPlotBackend::drawErrorBar(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    // First series is the value; a second, if present, is the uncertainty.
    if(spec.series.isEmpty()) return;
    const PlotSeries& v=spec.series.first();
    const bool haveErr=spec.series.size()>1;
    const PlotSeries& e=haveErr?spec.series[1]:v;
    const int n=qMin(v.x.size(),v.y.size());
    p->save();
    QPen pen(v.color); pen.setWidthF(qMax(0.4,v.lineWidth)); applySeriesDash(pen,v); p->setPen(pen);
    QPainterPath line; bool started=false;
    for(int i=0;i<n;++i){
        if(!finite(v.x[i])||!finite(v.y[i])) { started=false; continue; }
        const QPointF pt=toDevice(f,v.x[i],v.y[i]);
        if(!started){ line.moveTo(pt); started=true; } else line.lineTo(pt);
        if(haveErr&&i<e.y.size()&&finite(e.y[i])){
            const double err=std::abs(e.y[i]);
            const QPointF hi=toDevice(f,v.x[i],v.y[i]+err), lo=toDevice(f,v.x[i],v.y[i]-err);
            p->drawLine(hi,lo);
            p->drawLine(QPointF(hi.x()-3,hi.y()),QPointF(hi.x()+3,hi.y()));
            p->drawLine(QPointF(lo.x()-3,lo.y()),QPointF(lo.x()+3,lo.y()));
        }
    }
    p->setBrush(Qt::NoBrush); p->drawPath(line);
    p->restore();
}

// The same bar lying down: one row per series, spanning from a start to an end
// along x. A schedule, an availability timeline and a stratigraphic log are all
// this shape, and none of them can be drawn by drawFloatingBar - a Gantt with
// time on the vertical axis is not a Gantt, it is a puzzle.
//
// One series per bar, as above, but transposed: y = {row}, x = {start, end}.
void QtPlotBackend::drawFloatingRow(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    const double halfHeight=halfSlotAcross(f,spec,true,1.0);
    if(!(halfHeight>0)) return;

    for(const PlotSeries& s:spec.series){
        if(s.y.isEmpty()||s.x.size()<2) continue;
        const double row=s.y.first();
        const double from=s.x[s.x.size()-2],to=s.x[s.x.size()-1];
        if(!finite(row)||!finite(from)||!finite(to)) continue;
        const double cy=toDevice(f,f.xLo,row).y();
        const double xFrom=toDevice(f,from,row).x();
        const double xTo=toDevice(f,to,row).x();
        QColor fill=s.color; fill.setAlphaF(qBound(0.05,s.opacity,1.0)*0.85);
        p->save();
        QPen pen(s.color); pen.setWidthF(qMax(0.4,spec.style.lineWidth*0.8)); p->setPen(pen);
        p->setBrush(fill);
        // A zero-length task is a milestone, and a milestone that is one pixel
        // wide is still a milestone. It must not vanish.
        const double w=qMax(1.5,std::abs(xTo-xFrom));
        p->drawRect(QRectF(qMin(xFrom,xTo),cy-halfHeight,w,halfHeight*2));
        p->restore();
    }
}

// Open, high, low and close, drawn the way a price series is read: the body is
// the open-to-close range and the wick is the whole range, so a long wick over
// a short body says the period moved and came back. Colour carries direction,
// which is the one thing a reader takes from the chart at a glance.
//
// One series per period: x = {slot}, y = {open, high, low, close}.
void QtPlotBackend::drawCandlestick(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    // A candle is a little narrower than a bar, and may be thinner than a
    // pixel and a half when a year of daily prices is on screen.
    const double halfWidth=halfSlotAcross(f,spec,false,0.75,0.34);
    if(!(halfWidth>0)) return;

    for(const PlotSeries& s:spec.series){
        if(s.x.isEmpty()||s.y.size()<4) continue;
        const double slotX=s.x.first();
        const double open=s.y[0],high=s.y[1],low=s.y[2],close=s.y[3];
        if(!finite(slotX)||!finite(open)||!finite(high)||!finite(low)||!finite(close)) continue;
        const double cx=toDevice(f,slotX,f.yLo).x();
        const double yOpen=toDevice(f,slotX,open).y();
        const double yClose=toDevice(f,slotX,close).y();
        const double yHigh=toDevice(f,slotX,high).y();
        const double yLow=toDevice(f,slotX,low).y();
        const bool up=(close>=open);
        const QColor colour=up?spec.style.positive:spec.style.danger;
        p->save();
        QPen pen(colour); pen.setWidthF(qMax(0.6,spec.style.lineWidth)); p->setPen(pen);
        p->drawLine(QPointF(cx,yHigh),QPointF(cx,yLow));
        // A hollow body for a rise and a filled one for a fall, so the chart
        // still reads in monochrome and in the colour-vision modes.
        p->setBrush(up?QBrush(Qt::NoBrush):QBrush(colour));
        const double h=qMax(1.0,std::abs(yClose-yOpen));
        p->drawRect(QRectF(cx-halfWidth,qMin(yOpen,yClose),halfWidth*2,h));
        p->restore();
    }
}

// Half the width of one bar, from the closest neighbouring pair of slots, so
// bars never overlap however unevenly the slots are spaced.
//
// The three floating painters - the vertical bar, the horizontal row and the
// candle - each measured this for themselves, with the same loop, the same
// fallback and the same clamp written out three times. The only differences
// were the axis it is measured along and how much of the slot a mark fills.
double QtPlotBackend::halfSlotAcross(const Frame& f,const PlotSpec& spec,bool alongY,
                                     double minimum,double fraction) const {
    QVector<double> centres;
    for(const PlotSeries& s:spec.series){
        // Not "slots": Qt defines that as a keyword macro, and the error it
        // produces points at the assignment rather than at the name.
        const QVector<double>& seats=alongY?s.y:s.x;
        const QVector<double>& values=alongY?s.x:s.y;
        if(seats.isEmpty()||values.size()<2) continue;
        if(!finite(seats.first())) continue;
        centres.push_back(alongY?toDevice(f,f.xLo,seats.first()).y()
                                :toDevice(f,seats.first(),f.yLo).x());
    }
    if(centres.isEmpty()) return 0.0;
    const double across=alongY?f.plotArea.height():f.plotArea.width();
    const double slot=slotWidthFrom(centres,across/double(qMax(1,centres.size())),across);
    return qBound(minimum,slot*fraction,across*0.45);
}

void QtPlotBackend::drawFloatingBar(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    const double halfWidth=halfSlotAcross(f,spec,false,1.0);
    if(!(halfWidth>0)) return;

    for(const PlotSeries& s:spec.series){
        if(s.x.isEmpty()||s.y.size()<2) continue;
        const double slotX=s.x.first();
        // y = {low, high}; a three-element form carrying a centre first, the
        // shape the waterfall rewrite already produces, is read from the back.
        const double lo=s.y[s.y.size()-2], hi=s.y[s.y.size()-1];
        if(!finite(slotX)||!finite(lo)||!finite(hi)) continue;
        const double cx=toDevice(f,slotX,f.yLo).x();
        const double yLo=toDevice(f,slotX,lo).y();
        const double yHi=toDevice(f,slotX,hi).y();
        QColor fill=s.color; fill.setAlphaF(qBound(0.05,s.opacity,1.0)*0.85);
        p->save();
        QPen pen(s.color); pen.setWidthF(qMax(0.4,spec.style.lineWidth*0.8)); p->setPen(pen);
        p->setBrush(fill);
        // A zero-height step still has to be visible, or a no-change entry in a
        // waterfall silently disappears.
        const double h=qMax(1.0,std::abs(yHi-yLo));
        p->drawRect(QRectF(cx-halfWidth,qMin(yLo,yHi),halfWidth*2,h));
        p->restore();
    }
}

void QtPlotBackend::drawBoxPlot(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    for(const PlotSeries& s:spec.series){
        if(s.x.isEmpty()||s.y.size()<5) continue;
        const double slot=s.x.first();
        const double halfWidth=qMin(28.0,f.plotArea.width()/qMax(2,spec.series.size())*0.28);
        const double cx=toDevice(f,slot,f.yLo).x();
        const double wlo=toDevice(f,slot,s.y[0]).y();
        const double q1 =toDevice(f,slot,s.y[1]).y();
        const double med=toDevice(f,slot,s.y[2]).y();
        const double q3 =toDevice(f,slot,s.y[3]).y();
        const double whi=toDevice(f,slot,s.y[4]).y();
        p->save();
        QPen pen(s.color); pen.setWidthF(qMax(0.6,spec.style.lineWidth)); applySeriesDash(pen,s); p->setPen(pen);
        p->drawLine(QPointF(cx,wlo),QPointF(cx,q1));
        p->drawLine(QPointF(cx,q3),QPointF(cx,whi));
        p->drawLine(QPointF(cx-halfWidth*0.5,wlo),QPointF(cx+halfWidth*0.5,wlo));
        p->drawLine(QPointF(cx-halfWidth*0.5,whi),QPointF(cx+halfWidth*0.5,whi));
        QColor fill=s.color; fill.setAlphaF(0.35);
        p->setBrush(fill);
        p->drawRect(QRectF(cx-halfWidth,qMin(q1,q3),halfWidth*2,std::abs(q3-q1)));
        pen.setWidthF(qMax(1.0,spec.style.lineWidth*1.6)); p->setPen(pen);
        p->drawLine(QPointF(cx-halfWidth,med),QPointF(cx+halfWidth,med));
        p->restore();
    }
}

void QtPlotBackend::drawPie(QPainter* p,const QRectF& target,const PlotSpec& spec,bool donut) const {
    if(spec.series.isEmpty()) return;
    const PlotSeries& s=spec.series.first();
    double total=0;
    for(double v:s.y) if(finite(v)&&v>0) total+=v;
    if(!(total>0)) return;

    const double side=qMin(target.width(),target.height())*0.62;
    const QRectF circle(target.center().x()-side/2,target.center().y()-side/2,side,side);
    static const QColor wedge[]={QColor(0x4f,0x9d,0xf7),QColor(0xf2,0x8e,0x2b),QColor(0x59,0xa1,0x4f),
                                 QColor(0xe1,0x5f,0x99),QColor(0x76,0xb7,0xb2),QColor(0xed,0xc9,0x48),
                                 QColor(0xb0,0x7a,0xa1),QColor(0x9c,0x75,0x5f)};
    p->save();
    double start=90.0*16.0;   // start at 12 o'clock, Qt uses 1/16th degrees
    int idx=0;
    for(int i=0;i<s.y.size();++i){
        const double v=s.y[i];
        if(!finite(v)||v<=0) continue;
        const double span=-(v/total)*360.0*16.0;   // clockwise
        p->setPen(QPen(spec.style.background,1.0));
        p->setBrush(wedge[idx%int(sizeof(wedge)/sizeof(wedge[0]))]);
        p->drawPie(circle,int(start),int(span));
        start+=span; ++idx;
    }
    if(donut){
        const double inner=side*0.55;
        p->setPen(Qt::NoPen);
        p->setBrush(spec.style.background);
        p->drawEllipse(target.center(),inner/2,inner/2);
    }
    drawFloatingTitle(p,target,spec);
    p->restore();
}

void QtPlotBackend::render(QPainter* painter,const QRectF& target,const PlotSpec& inSpec){
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing,true);
    painter->setRenderHint(QPainter::TextAntialiasing,true);
    painter->fillRect(target,inSpec.style.background);

    // Reference, not a copy: the cache owns the prepared spec and it is stable
    // for the whole of this render.
    const PlotSpec& spec=preparedCached(inSpec);

    // Pie and donut have no axes at all, so they skip the frame entirely.
    if(!engineHasAxes(spec.engine)){
        if(spec.engine==QLatin1String("3D Quiver")
           ||spec.engine==QLatin1String("Cone Plot")
           ||spec.engine==QLatin1String("Stream Tube")
           ||spec.engine==QLatin1String("Stream Ribbon")
           ||spec.engine==QLatin1String("Tensor Glyph Field")
           ||spec.engine==QLatin1String("Volume Show")
           ||spec.engine==QLatin1String("Volume Slice")
           ||spec.engine==QLatin1String("Isosurface")
           ||spec.engine==QLatin1String("Isonormals")
           ||spec.engine==QLatin1String("Isocaps")
           ||spec.engine==QLatin1String("Contour Slice")){
            draw3DField(painter,target,spec);
            painter->restore();
            return;
        }
        if(spec.engine==QLatin1String("Network Graph")){
            drawNetwork(painter,target,spec);
            painter->restore();
            return;
        }
        if(spec.engine==QLatin1String("Chord Diagram")){
            drawChord(painter,target,spec);
            painter->restore();
            return;
        }
        if(spec.engine==QLatin1String("Smith Chart")){
            drawSmith(painter,target,spec);
            painter->restore();
            return;
        }
        if(spec.engine==QLatin1String("Mosaic Plot")){
            drawMosaic(painter,target,spec);
            painter->restore();
            return;
        }
        if(spec.engine==QLatin1String("UpSet Plot")){
            drawUpSet(painter,target,spec);
            painter->restore();
            return;
        }
        if(spec.engine==QLatin1String("Treemap")
           ||spec.engine==QLatin1String("Sunburst")
           ||spec.engine==QLatin1String("Venn Diagram")
           ||spec.engine==QLatin1String("Word Cloud")
           ||spec.engine==QLatin1String("Bubble Cloud")
           ||spec.engine==QLatin1String("Sankey Diagram"))
            drawComposition(painter,target,spec);
        else if(spec.engine.startsWith(QLatin1String("3D "))
           ||spec.engine==QLatin1String("Surface + Contours")
           ||spec.engine==QLatin1String("Comet 3D")
           ||spec.engine==QLatin1String("Ribbon"))
            draw3D(painter,target,spec);
        else if(spec.engine==QLatin1String("Polar Line")
                ||spec.engine==QLatin1String("Radar Chart")
                ||spec.engine==QLatin1String("Compass"))
            drawPolar(painter,target,spec,false);
        else if(spec.engine==QLatin1String("Polar Scatter")
                ||spec.engine==QLatin1String("Polar Bubble")
                ||spec.engine==QLatin1String("Polar Histogram")
                ||spec.engine==QLatin1String("Wind Rose"))
            drawPolar(painter,target,spec,true);
        else
            drawPie(painter,target,spec,spec.engine==QLatin1String("Donut"));
        painter->restore();
        return;
    }

    QVector<AxisTick> xTicks,yTicks;
    const Frame f=computeFrame(painter,target,spec,xTicks,yTicks);
    drawChrome(painter,f,spec,xTicks,yTicks);

    painter->save();
    painter->setClipRect(f.plotArea.adjusted(-0.5,-0.5,0.5,0.5));
    if(spec.engine==QLatin1String("Bar"))                    drawBar(painter,f,spec);
    else if(spec.engine==QLatin1String("Histogram"))         drawBar(painter,f,spec);
    else if(spec.engine==QLatin1String("Horizontal Bar"))    drawHorizontalBar(painter,f,spec);
    else if(spec.engine==QLatin1String("Floating Bar"))      drawFloatingBar(painter,f,spec);
    else if(spec.engine==QLatin1String("Floating Row"))      drawFloatingRow(painter,f,spec);
    else if(spec.engine==QLatin1String("Candlestick"))       drawCandlestick(painter,f,spec);
    else if(spec.engine==QLatin1String("Stem"))              drawStem(painter,f,spec);
    else if(spec.engine==QLatin1String("Stacked Lines"))     drawStackedLines(painter,f,spec);
    else if(spec.engine==QLatin1String("Error Bar"))         drawErrorBar(painter,f,spec);
    else if(spec.engine==QLatin1String("Box Plot"))          drawBoxPlot(painter,f,spec);
    else if(spec.engine==QLatin1String("Area"))              drawArea(painter,f,spec);
    else if(spec.engine==QLatin1String("Stairs"))            drawStairs(painter,f,spec);
    else if(spec.engine==QLatin1String("4D / 5D Scatter"))   drawScatter(painter,f,spec);
    else if(spec.engine==QLatin1String("2D Heatmap"))        drawHeatmap(painter,f,spec);
    else if(spec.engine==QLatin1String("2D Histogram"))      drawHeatmap(painter,f,spec);
    else if(spec.engine==QLatin1String("Hexbin Density"))    drawHeatmap(painter,f,spec);
    else if(spec.engine==QLatin1String("Correlation Matrix"))drawHeatmap(painter,f,spec);
    else if(spec.engine==QLatin1String("Covariance Matrix")) drawHeatmap(painter,f,spec);
    else if(spec.engine==QLatin1String("Spy Matrix"))        drawHeatmap(painter,f,spec);
    else if(spec.engine==QLatin1String("Quiver Field")
            ||spec.engine==QLatin1String("Feather")
            ||spec.engine==QLatin1String("Stream Field")
            ||spec.engine==QLatin1String("Stream Particles")
            ||spec.engine==QLatin1String("Phase Portrait")
            ||spec.engine==QLatin1String("Flow Texture (LIC)")
            ||spec.engine==QLatin1String("Divergence Map")
            ||spec.engine==QLatin1String("Vorticity Map"))    drawVectorField(painter,f,spec);
    else if(spec.engine==QLatin1String("2D Contour"))        drawContour(painter,f,spec);
    else if(spec.engine==QLatin1String("Violin Plot"))       drawViolin(painter,f,spec);
    else if(spec.engine==QLatin1String("Raincloud"))         drawViolin(painter,f,spec);
    else if(spec.engine==QLatin1String("Forest Plot"))       drawForest(painter,f,spec);
    else                                                     drawLineChart(painter,f,spec);
    painter->restore();

    drawLegend(painter,f,spec);
    // Last, over the legend as well: a note is about the figure rather than
    // part of it, and one hidden behind the legend is one nobody reads.
    drawAnnotations(painter,f,spec);
    painter->restore();
}

} // namespace graphvis
