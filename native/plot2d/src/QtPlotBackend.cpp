#include "QtPlotBackend.h"
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
        // Spectral. One radix-2 FFT, Hann-windowed, serves all three.
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

QtPlotBackend::Frame QtPlotBackend::computeFrame(QPainter* p,const QRectF& target,const PlotSpec& spec,
                                                 QVector<AxisTick>& xTicks,QVector<AxisTick>& yTicks) const {
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

    if(!f.yLog){ const double pad=(yHi-yLo)*0.05; yLo-=pad; yHi+=pad; }

    f.xLo=xLo; f.xHi=xHi; f.yLo=yLo; f.yHi=yHi;

    xTicks=f.xLog?logTicks(xLo,xHi):linearTicks(xLo,xHi,7);
    yTicks=f.yLog?logTicks(yLo,yHi):linearTicks(yLo,yHi,6);

    // Widen the left margin so the longest y label always fits.
    const QFontMetricsF fm(font(spec,spec.style.tickSize),p->device());
    double widest=0;
    for(const AxisTick& t:yTicks) if(!t.minor) widest=qMax(widest,fm.horizontalAdvance(t.label));
    const double left=qMax(kMarginLeft,widest+kTickLen+14.0+fm.height());

    f.plotArea=QRectF(target.left()+left,target.top()+kMarginTop,
                      qMax(10.0,target.width()-left-kMarginRight),
                      qMax(10.0,target.height()-kMarginTop-kMarginBottom));
    return f;
}

QPointF QtPlotBackend::toDevice(const Frame& f,double x,double y) const {
    if(f.xLog) x=(x>0)?std::log10(x):f.xLo;
    if(f.yLog) y=(y>0)?std::log10(y):f.yLo;
    const double tx=(x-f.xLo)/qMax(1e-300,f.xHi-f.xLo);
    const double ty=(y-f.yLo)/qMax(1e-300,f.yHi-f.yLo);
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
// Viridis, sampled at sixteen stops and interpolated. Perceptually uniform and
// monotone in lightness, so the map still reads as ordered in greyscale and for
// every colour vision deficiency - which a rainbow map does not.
QColor viridis(double t){
    static const double stops[16][3]={
        {0.267,0.005,0.329},{0.283,0.100,0.422},{0.278,0.184,0.487},{0.254,0.265,0.530},
        {0.222,0.339,0.549},{0.192,0.406,0.556},{0.165,0.470,0.558},{0.141,0.533,0.555},
        {0.121,0.596,0.544},{0.135,0.659,0.518},{0.208,0.718,0.473},{0.328,0.773,0.407},
        {0.478,0.821,0.318},{0.647,0.858,0.210},{0.825,0.885,0.107},{0.993,0.906,0.144}};
    t=qBound(0.0,t,1.0);
    const double pos=t*15.0;
    const int i=qBound(0,int(pos),14);
    const double frac=pos-i;
    const double r=stops[i][0]*(1-frac)+stops[i+1][0]*frac;
    const double g=stops[i][1]*(1-frac)+stops[i+1][1]*frac;
    const double b=stops[i][2]*(1-frac)+stops[i+1][2]*frac;
    return QColor::fromRgbF(r,g,b);
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

void QtPlotBackend::drawHeatmap(QPainter* p,const Frame& f,const PlotSpec& spec) const {
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
            p->setBrush(viridis(t));
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
        QPen pen(viridis(frac));
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
                    QPen mesh(viridis(t)); mesh.setWidthF(qMax(0.4,spec.style.lineWidth*0.7));
                    p->setPen(mesh);
                }else{
                    p->setBrush(viridis(t));
                    QPen edge(viridis(t).darker(115)); edge.setWidthF(0.3);
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
            p->setBrush(viridis(t));
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
                p->setBrush(viridis((value-out.vLo)/(out.vHi-out.vLo)));
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
                QPen pen(viridis(qBound(0.0,carried/magMax,1.0)));
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
                QPen pen(viridis(mag/magMax));
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

// Hann. Without a window the spectrum of anything that does not fit a whole
// number of periods into the record is dominated by leakage from the edges,
// which looks like broadband noise that is not there.
double hann(int i,int n){
    if(n<2) return 1.0;
    return 0.5*(1.0-std::cos(2.0*3.14159265358979323846*double(i)/double(n-1)));
}

} // namespace

// ---------------------------------------------------------------------------
// Composition and hierarchy.
//
// Treemap, sunburst, Venn, word cloud and Sankey all lay out a whole rather
// than plot a coordinate, so none of them uses the axis frame. They share this
// one function because they share the same input shape - a list of labelled
// magnitudes - and differ only in how the area is divided.
void QtPlotBackend::drawComposition(QPainter* p,const QRectF& target,const PlotSpec& spec) const {
    // Each mapped series contributes its total as one part of the whole.
    struct Part { QString label; double value; QColor colour; };
    QVector<Part> parts;
    for(const PlotSeries& s:spec.series){
        double total=0.0;
        for(double v:s.y) if(finite(v)) total+=std::abs(v);
        if(total>0.0) parts.append({s.label,total,s.color});
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
            const double scale=0.7+0.6*(parts[i].value/parts[0].value);
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
            const double weight=part.value/parts.first().value;
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

    const double unit=proj.scale*0.10;
    for(const Glyph& g:glyphs){
        const int i=g.index;
        const double t=qBound(0.0,magnitude[i]/magMax,1.0);
        const QColor colour=viridis(t);

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
        const unsigned char lg=a->log10?1:0; fnvBytes(h,&lg,1);
        fnvDouble(h,a->min); fnvDouble(h,a->max);
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
        return prepCache_.prepared;

    prepCache_.prepared=prepareSpec(in);
    prepCache_.engine=in.engine;
    prepCache_.variant=in.variant;
    prepCache_.expression=in.expression;
    prepCache_.seriesCount=seriesCount;
    prepCache_.pointCount=pointCount;
    prepCache_.hash=hash;
    prepCache_.valid=true;
    return prepCache_.prepared;
}

PlotSpec QtPlotBackend::prepareSpec(const PlotSpec& in) const {
    // ----------------------------------------------------------------- ECDF
    // The empirical distribution: every observation contributes one step of
    // 1/n. Drawn as a staircase because that is what it is - joining the points
    // with straight lines would claim values between observations that were
    // never measured.
    if(in.engine==QLatin1String("ECDF")){
        PlotSpec out=in;
        out.engine=QStringLiteral("Stairs");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("4D / 5D Scatter");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Line Chart");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("4D / 5D Scatter");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Line Chart");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Line Chart");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("4D / 5D Scatter");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Line Chart");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Line Chart");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Horizontal Bar");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Line Chart");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Line Chart");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Line Chart");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Line Chart");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Line Chart");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Line Chart");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("4D / 5D Scatter");
        out.series.clear();
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
            PlotSpec out=in;
            out.engine=drawAs;
            out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("4D / 5D Scatter");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("4D / 5D Scatter");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Polar Histogram");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Radar Chart");
        out.series.clear();
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
    // Longitude east, latitude north, drawn as an equirectangular projection.
    // There is no basemap and this does not pretend to be a map: the axes are
    // labelled as coordinates so nobody reads a country outline that is not
    // there. Geo Density bins to a heatmap, the rest are scatter and line.
    if(in.engine.startsWith(QLatin1String("Geo "))){
        PlotSpec out=in;
        if(in.engine==QLatin1String("Geo Density")){
            out.engine=QStringLiteral("2D Histogram");
            PlotSpec relabelled=out;
            return prepareSpec(relabelled);
        }
        out.engine=(in.engine==QLatin1String("Geo Line"))
                       ? QStringLiteral("Line Chart") : QStringLiteral("4D / 5D Scatter");
        out.series.clear();
        if(in.series.size()>=2){
            const PlotSeries& lon=in.series.at(0);
            const PlotSeries& lat=in.series.at(1);
            const int n=qMin(lon.y.size(),lat.y.size());
            PlotSeries pts;
            pts.label=QStringLiteral("position");
            pts.color=lon.color;
            pts.drawLine=(in.engine==QLatin1String("Geo Line"));
            pts.drawMarkers=!pts.drawLine;
            pts.markerSize=(in.engine==QLatin1String("Geo Bubble"))?5.5:3.6;
            pts.lineWidth=qMax(1.2,lon.lineWidth);
            for(int i=0;i<n;++i){
                if(!finite(lon.y[i])||!finite(lat.y[i])) continue;
                pts.x.append(lon.y[i]); pts.y.append(lat.y[i]);
            }
            if(!pts.x.isEmpty()) out.series.append(pts);
        }
        out.xAxis=PlotAxis{QStringLiteral("longitude (deg E)"),false,unsetValue(),unsetValue()};
        out.yAxis=PlotAxis{QStringLiteral("latitude (deg N)"),false,unsetValue(),unsetValue()};
        return out;
    }

    // ------------------------------------------- Power Spectral Density
    // Periodogram of each mapped series, Hann-windowed, on log-log axes because
    // that is where power laws are straight lines and where the useful part of
    // a spectrum lives.
    if(in.engine==QLatin1String("Power Spectral Density")){
        PlotSpec out=in;
        out.engine=QStringLiteral("Line Chart");
        out.series.clear();
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
            double windowPower=0.0;
            for(int i=0;i<v.size();++i){
                const double w=hann(i,v.size());
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
            PlotSeries tx,fy,pv;
            for(int start=0;start+window<=v.size();start+=hop){
                QVector<double> re(window,0.0),im(window,0.0);
                double mean=0.0;
                for(int i=0;i<window;++i) mean+=v[start+i];
                mean/=double(window);
                for(int i=0;i<window;++i) re[i]=(v[start+i]-mean)*hann(i,window);
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Stem");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Line Chart");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Line Chart");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Line Chart");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Line Chart");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Horizontal Bar");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Area");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("Line Chart");
        out.series.clear();
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
        PlotSpec out=in;
        out.engine=QStringLiteral("4D / 5D Scatter");
        out.series.clear();
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

void QtPlotBackend::drawFloatingBar(QPainter* p,const Frame& f,const PlotSpec& spec) const {
    // Bar width from the closest neighbouring pair, so bars never overlap
    // however the slots are spaced.
    QVector<double> centres;
    for(const PlotSeries& s:spec.series){
        if(s.x.isEmpty()||s.y.size()<2) continue;
        if(!finite(s.x.first())) continue;
        centres.push_back(toDevice(f,s.x.first(),f.yLo).x());
    }
    if(centres.isEmpty()) return;
    const double slot=slotWidthFrom(centres,f.plotArea.width()/double(qMax(1,centres.size())),
                                    f.plotArea.width());
    const double halfWidth=qBound(1.0,slot*0.36,f.plotArea.width()*0.45);

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
    painter->restore();
}

} // namespace graphvis
