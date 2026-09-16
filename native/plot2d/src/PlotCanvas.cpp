#include "PlotCanvas.h"
#include <QThread>
#include "SurfaceEstimators.h"
#include "ArrowTable.h"
#include "ColourVision.h"
#include "ColourMaps.h"
#include "ColourMapSafety.h"
#include "ColourMapAdvice.h"
#include "Expression.h"
#include "PublicationProfile.h"
#include "Units.h"

#include "QtPlotBackend.h"

#include <cmath>
#include <limits>

#include <QFileInfo>
#include <QMutex>
#include <memory>
#include <QImage>
#include <QBuffer>
#include <QMouseEvent>
#include <QPainter>
#include <QHoverEvent>
#include <QSet>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QPdfWriter>
#include <QPageSize>
#include <QSvgGenerator>
#include <QImageWriter>
#include <QSet>
#include <QQuickWindow>
#include <QtConcurrent/QtConcurrentRun>
// std::array: the linear-light table in simulateInPlace, which has to be a
// function-local static to be initialised safely from two threads.
#include <array>

namespace graphvis {
namespace {

// GraphVis 17 keeps interactive renders inside a preview budget rather than
// drawing every row; export re-renders from full data. The same idea applies
// here: the screen path decimates, exportPdf/exportPng do not.
constexpr int kInteractivePointBudget = 10000;

// ...but that budget was applied PER SERIES, so the cost of a repaint scaled
// with the number of columns mapped. Six series meant 60,000 points on the GUI
// thread, and the raster engine's cost is superlinear in stroked area:
//
//     3,000 points   37 ms      18,000 points    322 ms
//     6,000 points   81 ms      30,000 points    678 ms
//    10,000 points  152 ms      60,000 points  2,143 ms
//
// So a six-column plot took over two seconds to repaint - during a drag, on
// every frame. Sharing the budget across the series puts it back at 152 ms.
//
// A single-series plot is unchanged: it still gets the whole 10,000. Only the
// multi-series case, which is the slow one, gives anything up, and the floor
// keeps each series detailed enough to read. The full-resolution render is
// untouched - it draws every row, as it always did.
constexpr int kMinimumPointsPerSeries = 1500;

int interactiveBudgetFor(int seriesCount){
    if(seriesCount<=1) return kInteractivePointBudget;
    return qMax(kMinimumPointsPerSeries,kInteractivePointBudget/seriesCount);
}

// Series colours come from ColourVision.h, which measures its palettes through
// a dichromat simulation instead of asserting they are safe. The rotation that
// used to live here carried the comment "colour-blind-safe" and measured 3.6 dE
// under protanopia - two of its eight colours were effectively identical.

// Min/max per bucket, not every Nth point.
//
// Stride sampling aliases: a transient between two sampled indices vanishes
// from the preview entirely, so the preview could show structure the data does
// not have - and hide structure it does - until the full render landed. For a
// scientific tool a preview that quietly drops a spike is worse than a slow
// one. Taking the minimum and maximum of each bucket costs the same single
// pass and preserves the envelope, so preview and full render agree about what
// is in the data even though they disagree about how many points drew it.
//
// x and y are decimated independently but with the same bucket boundaries, so
// a pair stays a pair: index 2b is each bucket's min, 2b+1 its max.
QVector<double> decimate(const QVector<double>& in,int budget){
    if(budget<=0||in.size()<=budget) return in;
    const int buckets=qMax(1,budget/2);
    QVector<double> out; out.reserve(buckets*2);
    const double stride=double(in.size())/double(buckets);
    for(int b=0;b<buckets;++b){
        const int lo=int(b*stride);
        const int hi=qMin(in.size(),qsizetype(qMax(lo+1,int((b+1)*stride))));
        double mn=in[lo], mx=in[lo];
        for(int i=lo;i<hi;++i){
            const double v=in[i];
            if(v!=v) continue;            // a NaN gap must not become the bucket
            if(!(mn==mn)||v<mn) mn=v;
            if(!(mx==mx)||v>mx) mx=v;
        }
        out.append(mn); out.append(mx);
    }
    return out;
}

// Is the x column ordered? That is the precondition the envelope decimation
// above quietly depends on, and nothing was checking it.
//
// min/max per bucket is the right summary for a TIME SERIES: x rises steadily,
// so the two points a bucket emits sit at almost the same x and the pair
// (x-min, y-min) is a real place on the curve to within a pixel.
//
// It is wrong for a set of independent samples. A Latin-hypercube sweep has no
// meaningful row order at all, so bucket b's smallest x and its smallest y come
// from DIFFERENT ROWS, and the point drawn from them is one the data does not
// contain. Every scatter, heat map, contour and 3-D view of such a file was
// therefore drawn from invented pairs - which is exactly what a sparse, wrong
// looking heat map of a parameter sweep is.
bool columnIsOrdered(const QVector<double>& v){
    double last=-std::numeric_limits<double>::infinity();
    int seen=0;
    for(double d:v){
        if(!std::isfinite(d)) continue;
        if(d<last) return false;
        last=d; ++seen;
    }
    return seen>0;
}

// The rows to keep, evenly spaced, when the envelope cannot be used. Plain
// striding: it keeps x and y paired, which is the whole point, and for
// unordered samples there is no envelope to preserve anyway - a stride through
// a scatter is a smaller scatter of the same shape.
QVector<int> strideRows(int rowCount,int budget){
    QVector<int> rows;
    if(budget<=0||rowCount<=budget){
        rows.reserve(rowCount);
        for(int i=0;i<rowCount;++i) rows.append(i);
        return rows;
    }
    rows.reserve(budget);
    const double step=double(rowCount)/double(budget);
    for(int i=0;i<budget;++i){
        const int r=int(i*step);
        if(r<rowCount) rows.append(r);
    }
    return rows;
}

QVector<double> gather(const QVector<double>& in,const QVector<int>& rows){
    QVector<double> out;
    out.reserve(rows.size());
    for(int r:rows) if(r<in.size()) out.append(in[r]);
    return out;
}

// An evenly spaced subset of rows that have ALREADY been chosen. strideRows
// answers the same question for a contiguous 0..n-1; this answers it for the
// scattered set of rows that fall inside a zoomed view.
QVector<int> strideOf(const QVector<int>& base,int budget){
    if(budget<=0||base.size()<=budget) return base;
    QVector<int> rows;
    rows.reserve(budget);
    const double step=double(base.size())/double(budget);
    for(int i=0;i<budget;++i){
        const int r=int(i*step);
        if(r<base.size()) rows.append(base.at(r));
    }
    return rows;
}

// ===========================================================================
// THE POINT BUDGET IS SPENT WHERE THE PERSON IS LOOKING.
//
// Reported as: while zooming, "it goes back to normal after zooming - make
// this left screenshot not happen, people will want to see exactly what they
// are zooming in or out on."
//
// The preview gets 10,000 points, and it was decimating the WHOLE column into
// them however far the figure was zoomed. Zoom a 300,000-row series until a
// seventieth of it is on screen and 10,000 points become about 140 inside the
// frame: the curve arrives as a handful of broken strokes. Then the
// full-resolution render lands a second later with every row and the figure
// turns back into a curve - which is the "it goes back to normal afterwards"
// half of the report, and the reason the fault looked like a rendering glitch
// rather than the budget it actually is.
//
// A budget spent outside the frame buys nothing: those points are clipped away
// before a single pixel is drawn. So the same 10,000 go to the rows inside the
// view, and the preview gets BETTER as the zoom goes deeper instead of worse -
// which is what zooming in is for. At any zoom past about 1.1x the window holds
// fewer rows than the budget, so the preview is then every row there is, the
// full render can add nothing, and previewIsExact says so and skips it.
//
// One row either side of the window is kept deliberately, so the curve enters
// and leaves the frame instead of stopping short of both edges.
//
// Only for engines that read x as an AXIS. A heatmap and its relatives read
// their three mapped columns as series 0, 1 and 2 and grid them, and the grid's
// extent comes from the data it is given - so windowing those would re-bin the
// field on every zoom and change the cell size under the person. Those engines
// are left alone; see the caller.
// ===========================================================================
// Declared in the header so the preview and the full render can ask one
// function for it; the reasoning is the note above.
using ViewWindow=PlotCanvas::ViewWindow;

// Series construction, shared by the on-screen preview and the full-resolution
// render that runs on a worker thread.
//
// One function on purpose: if the two built their series separately they could
// disagree about colour, dash pattern, marker rules or which columns were
// chosen, and the full render would quietly differ from the preview it replaced
// in ways nobody would think to check. A budget of 0 means no decimation.
// The conversion from a column's own unit to the requested display unit, or
// nullptr when there is none. A target the source cannot reach is ignored
// rather than silently scaling by one, so a mistyped unit shows the data
// unchanged instead of a plausible wrong number.
const Units::Conversion* displayConversion(const QString& columnLabel,const QString& target,
                                           Units::Conversion& storage){
    if(target.isEmpty()) return nullptr;
    const QString source=Units::extractUnit(columnLabel);
    if(source.isEmpty()) return nullptr;
    const QVector<Units::Conversion> options=Units::conversionsFor(source);
    for(const Units::Conversion& c:options){
        if(Units::normaliseUnit(c.target).compare(Units::normaliseUnit(target),
                                                  Qt::CaseInsensitive)==0){
            storage=c;
            return &storage;
        }
    }
    return nullptr;
}

void applyConversion(QVector<double>& values,const Units::Conversion* conversion){
    if(!conversion) return;
    for(double& v:values) v=conversion->apply(v);
}

int buildPlotSeries(const ArrowTable& table,const QString& xName,const QStringList& yNames,
                    int colourVision,int budget,PlotSpec& spec,
                    const QString& xUnit=QString(),const QString& yUnit=QString(),
                    const ViewWindow& window=ViewWindow(),int* rowsAvailable=nullptr,
                    const QString& y2Name=QString()){
    spec.series.clear();
    // Cleared every time, because it is written from the mapping below. Left
    // standing, the name of a column that is no longer on the right-hand axis
    // keeps its margin reserved on a figure that no longer has a second axis.
    spec.y2Axis.label.clear();
    if(rowsAvailable) *rowsAvailable=0;
    // CONVERTED FIRST, because the window arrives in the unit the axis is drawn
    // in - the person zoomed a figure labelled in feet, not in metres. It used
    // to be converted after decimation, which is equivalent for the points
    // themselves (a conversion is monotonic, so it commutes with a per-bucket
    // min and max) but leaves nothing in display units to compare the window
    // against.
    QVector<double> rawX=table.column(xName);
    Units::Conversion xStore;
    applyConversion(rawX,displayConversion(xName,xUnit,xStore));

    // Row-preserving unless the x column is ordered. See columnIsOrdered.
    const bool ordered=columnIsOrdered(rawX);

    // The slice of rows inside the view. See ViewWindow.
    //
    // A linear scan rather than a binary search even on the ordered path:
    // columnIsOrdered ignores non-finite values, so an ordered column may still
    // have a NaN in the middle of it, and lower_bound on a range that is not
    // totally ordered is undefined. One pass over the column is a fraction of
    // the decimation that follows.
    int rowLo=0,rowHi=int(rawX.size())-1;
    QVector<int> inWindow;
    if(window.usable()&&rawX.size()>2){
        if(ordered){
            int first=-1,last=-1;
            for(int i=0;i<rawX.size();++i){
                if(!std::isfinite(rawX[i])) continue;
                if(rawX[i]<window.lo){ first=i; continue; }
                if(rawX[i]>window.hi){ last=i; break; }
            }
            rowLo=(first>=0)?first:0;                       // one row before
            rowHi=(last>=0)?last:int(rawX.size())-1;        // and one after
        }else{
            for(int i=0;i<rawX.size();++i)
                if(rawX[i]>=window.lo&&rawX[i]<=window.hi) inWindow.append(i);
            // A window that caught nothing is not a window. Falling back to the
            // whole column shows the person an empty frame's worth of data
            // rather than an empty frame with no explanation.
            if(inWindow.isEmpty()) inWindow=strideRows(int(rawX.size()),0);
        }
    }else if(!ordered){
        inWindow=strideRows(int(rawX.size()),0);
    }

    const int sliceCount=qMax(0,rowHi-rowLo+1);
    const QVector<int> rows=ordered?QVector<int>():strideOf(inWindow,budget);
    QVector<double> xs=ordered?decimate(rawX.mid(rowLo,sliceCount),budget)
                              :gather(rawX,rows);
    const ColourVision vision=colourVisionFromInt(colourVision);
    const QVector<QColor> palette=seriesPalette(vision);
    const QVector<QVector<qreal>> dashes=seriesDashPatterns(vision);
    // The engines that lay out a whole - treemap, icicle, sunburst, Sankey,
    // chord - never get a PlotSeries to take a colour from, so without this
    // they cannot see the colour-vision choice at all. A person's own colours
    // win over the measured palette, exactly as they do for a ramp.
    spec.style.categoryPalette=spec.style.customColours.isEmpty()
                               ?palette:spec.style.customColours;
    int total=0;
    int idx=0;
    for(const QString& yName:yNames){
        if(!table.hasColumn(yName)) continue;
        PlotSeries s;
        s.label=Units::axisLabel(yName);
        s.x=xs;
        // The SAME rows as x when the data is unordered, so a point on screen
        // is a row that exists rather than one column's minimum paired with
        // another column's - and the same slice of them, or the pair would be
        // drawn from two different parts of the file.
        const QVector<double> rawY=table.column(yName);
        s.y=ordered?decimate(rawY.mid(rowLo,qMin(sliceCount,
                                                 qMax(0,int(rawY.size())-rowLo))),budget)
                   :gather(rawY,rows);
        // How many rows this series COULD have drawn - the slice, not the
        // budgeted sample of it. previewIsExact compares the two, so counting
        // the sample here would have reported every preview as exact and
        // skipped the full render for good.
        if(rowsAvailable)
            *rowsAvailable+=ordered
                ?qMin(sliceCount,qMax(0,int(rawY.size())-rowLo))
                :qMin(int(inWindow.size()),int(rawY.size()));
        Units::Conversion yStore;
        if(const Units::Conversion* c=displayConversion(yName,yUnit,yStore)){
            applyConversion(s.y,c);
            s.label=Units::replaceUnit(Units::displayName(yName),c->target);
        }
        s.color=palette.at(idx%palette.size());
        if(!dashes.isEmpty()) s.dashPattern=dashes.at(idx%dashes.size());
        ++idx;
        s.lineWidth=spec.style.lineWidth;
        s.drawLine=spec.engine!=QLatin1String("4D / 5D Scatter");
        // Markers on a decimated preview and on a million-point full render are
        // not the same decision, so this follows the points actually drawn.
        s.drawMarkers=spec.engine==QLatin1String("4D / 5D Scatter")||s.x.size()<=60;
        s.markerSize=spec.engine==QLatin1String("4D / 5D Scatter")?4.5:3.0;
        // THE ONE COLUMN THE PERSON PUT ON THE RIGHT-HAND AXIS.
        //
        // Set here, on the series, rather than anywhere later: everything
        // downstream - the range that is measured, the ticks that are drawn,
        // the log scale, the device transform - already reads this one flag,
        // and it is the same flag the eight engines that build their own
        // secondary series set for themselves. Adding a second way to say it
        // would be two implementations of one question, which is this
        // project's most expensive recurring mistake.
        if(!y2Name.isEmpty()&&yName==y2Name){
            s.secondaryAxis=true;
            // The axis needs a name or it is an unlabelled scale on the right
            // of the figure, which is worse than no second axis at all. The
            // series label is already unit-corrected by the block above.
            spec.y2Axis.label=s.label;
        }
        const int n=qMin(s.x.size(),s.y.size());
        s.x.resize(n); s.y.resize(n);
        total+=n;
        spec.series.append(s);
    }
    return total;
}

} // namespace

PlotCanvas::PlotCanvas(QQuickItem* parent):QQuickPaintedItem(parent){
    setFlag(ItemHasContents,true);
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    spec_.engine=QStringLiteral("Line Chart");

    // Without these the item is transparent to the pointer: no press, no
    // wheel, and no touch either, which is why the figure had never been
    // draggable by any input device.
    setAcceptedMouseButtons(Qt::LeftButton|Qt::MiddleButton);
    setAcceptTouchEvents(true);
    // The cursor readout. Without this the item never sees a pointer that is
    // not pressing a button, which is every pointer that is merely reading the
    // figure.
    setAcceptHoverEvents(true);

    // Edits arrive in bursts - dragging a slider is dozens of changes a second -
    // and starting a multi-second render for each one would be worse than
    // useless. The preview redraws on every one of them; only the expensive
    // render waits for the burst to end.
    fullDebounce_.setSingleShot(true);
    fullDebounce_.setInterval(280);
    connect(&fullDebounce_,&QTimer::timeout,this,&PlotCanvas::startFullRender);
    connect(&fullWatcher_,&QFutureWatcher<QImage>::finished,
            this,&PlotCanvas::handleFullRenderFinished);

    // The end of a gesture, for the gestures that have no end event. See
    // interacting_ in the header.
    interactionIdle_.setSingleShot(true);
    interactionIdle_.setInterval(180);
    connect(&interactionIdle_,&QTimer::timeout,this,[this]{
        interacting_=false;
        update();                 // one last preview, this time antialiased
        scheduleFullRender();
    });

    // The end of a splitter drag, which also has no end event here.
    resizeIdle_.setSingleShot(true);
    resizeIdle_.setInterval(180);
    connect(&resizeIdle_,&QTimer::timeout,this,[this]{
        resizing_=false;
        update();                 // one last preview, this time antialiased
    });

    // A progress bar that only moves when the work finishes is a lie. There is
    // no honest sub-render progress to report from a single QPainter pass, so
    // this reports elapsed against the measured estimate and says so in the
    // label when it overruns.
    // Started with the render and stopped with it. It used to run for the life
    // of the canvas at 8 ticks a second doing nothing, which is enough to stop
    // a laptop ever reaching an idle state.
    progressTick_=new QTimer(this);
    progressTick_->setInterval(120);
    connect(progressTick_,&QTimer::timeout,this,[this]{ emit renderStateChanged(); });
}

PlotCanvas::~PlotCanvas(){
    if(fullCancel_) fullCancel_->storeRelease(1);
    fullWatcher_.waitForFinished();
}


#define GV_SETTER(fn,member,type) \
    void PlotCanvas::fn(const type& v){ if(member==v) return; member=v; dirty_=true; rebuild(); update(); emit sourceChanged(); }

GV_SETTER(setArrowPath,arrowPath_,QString)
GV_SETTER(setXColumn,xColumn_,QString)
GV_SETTER(setYColumns,yColumns_,QStringList)
GV_SETTER(setZColumn,zColumn_,QString)
GV_SETTER(setColorColumn,colorColumn_,QString)
GV_SETTER(setY2Column,y2Column_,QString)
GV_SETTER(setXUnit,xUnit_,QString)
GV_SETTER(setYUnit,yUnit_,QString)

void PlotCanvas::setEngine(const QString& v){
    if(spec_.engine==v) return;
    // What was typed for the engine being left, kept for the rest of the
    // session. Someone comparing two engines on the same decay should not have
    // to retype four masses each time they switch back.
    if(!spec_.parameters.isEmpty()) engineParams_.insert(spec_.engine,spec_.parameters);
    spec_.engine=v;
    adoptEngineParameters();
    engineSupported_=qtBackend_.supports(v);
    dirty_=true; rebuild(); update();
    emit sourceChanged(); emit stateChanged();
}

// ------------------------------------------------------------- parameters
//
// Constants an engine declares - see QtPlotBackend::engineParameters. The
// canvas holds the values and knows nothing about what any of them mean, which
// is what lets a new parameter be one table row and no more.
void PlotCanvas::adoptEngineParameters(){
    const QMap<QString,double> remembered=engineParams_.value(spec_.engine);
    QMap<QString,double> next=QtPlotBackend::engineParameterDefaults(spec_.engine);
    // Remembered values win, but only for keys the engine still declares: a
    // renamed or withdrawn parameter must not linger in the spec where nothing
    // reads it and the fingerprint still hashes it.
    for(auto it=next.begin();it!=next.end();++it){
        const auto had=remembered.constFind(it.key());
        if(had!=remembered.constEnd()) it.value()=*had;
    }
    spec_.parameters=next;
    emit engineParametersChanged();
}

QVariantMap PlotCanvas::engineParameterValues() const {
    QVariantMap out;
    for(auto it=spec_.parameters.constBegin();it!=spec_.parameters.constEnd();++it)
        out.insert(it.key(),it.value());
    return out;
}

QVariantList PlotCanvas::engineParameterList() const {
    QVariantList out;
    const QVector<QtPlotBackend::EngineParameter> declared=
        QtPlotBackend::engineParameters(spec_.engine);
    for(const QtPlotBackend::EngineParameter& p:declared){
        out.append(QVariantMap{
            {QStringLiteral("key"),p.key},
            {QStringLiteral("label"),p.label},
            {QStringLiteral("unit"),p.unit},
            {QStringLiteral("help"),p.help},
            {QStringLiteral("value"),spec_.parameter(p.key,p.defaultValue)},
            {QStringLiteral("defaultValue"),p.defaultValue},
            {QStringLiteral("minimum"),p.minimum},
            {QStringLiteral("maximum"),p.maximum},
            {QStringLiteral("decimals"),p.decimals}});
    }
    return out;
}


void PlotCanvas::setEngineParameter(const QString& key,double value){
    const QVector<QtPlotBackend::EngineParameter> declared=
        QtPlotBackend::engineParameters(spec_.engine);
    for(const QtPlotBackend::EngineParameter& p:declared){
        if(p.key!=key) continue;
        // Clamped here rather than trusted from the interface: a .gvfig and a
        // script reach this too, and an engine's declared range is the range
        // it can draw.
        const double clamped=(value!=value)?p.defaultValue:qBound(p.minimum,value,p.maximum);
        if(qFuzzyCompare(spec_.parameter(key,p.defaultValue),clamped)) return;
        spec_.parameters.insert(key,clamped);
        engineParams_.insert(spec_.engine,spec_.parameters);
        // A repaint is not enough: these change what the engine computes, and
        // the prepared figure is cached on a fingerprint that now differs.
        update();
        scheduleFullRender();
        emit sourceChanged();
        emit stateChanged();
        return;
    }
    // A key the engine does not declare is a caller mistake, not data: ignored
    // rather than stored, so nothing can put a value into the spec that no
    // control will ever show and no engine will ever read.
}

void PlotCanvas::resetEngineParameters(){
    const QMap<QString,double> defaults=
        QtPlotBackend::engineParameterDefaults(spec_.engine);
    if(spec_.parameters==defaults) return;
    spec_.parameters=defaults;
    engineParams_.insert(spec_.engine,spec_.parameters);
    emit engineParametersChanged();
    update();
    scheduleFullRender();
    emit sourceChanged();
    emit stateChanged();
}
void PlotCanvas::setVariant(const QString& v){
    if(spec_.variant==v) return;
    spec_.variant=v; applyVariant(); update(); emit sourceChanged();
}
void PlotCanvas::setTitle(const QString& v){
    if(spec_.title==v) return;
    spec_.title=v; update(); emit sourceChanged();
}
// The figure's own colours. All three are painting decisions - the data and
// the prepared spec are untouched - so each is a repaint plus a re-render, and
// none of them is a rebuild.
//
// The re-render is the part that was missing. update() alone repaints, and
// paint() then returns immediately with the accepted full-resolution image,
// which was drawn in the previous colours. Switching the figure theme on a
// large dataset changed nothing on screen until something else happened to
// invalidate that image.
#define GV_FIGURE_COLOUR(Setter,Member)                          \
void PlotCanvas::Setter(const QColor& c){                        \
    if(spec_.style.Member==c) return;                            \
    spec_.style.Member=c;                                        \
    showingFull_=false;                                          \
    update();                                                    \
    scheduleFullRender();                                        \
    emit styleChanged();                                         \
}
GV_FIGURE_COLOUR(setBackgroundColor,background)
GV_FIGURE_COLOUR(setForegroundColor,foreground)
GV_FIGURE_COLOUR(setGridColor,gridColor)
#undef GV_FIGURE_COLOUR

// The map is part of the spec, so changing it has to invalidate the prepared
// figure the same way a colour change does - update() alone would repaint the
// cached image and show the old colours.
// --------------------------------------------------------------------- notes
//
// Stored in the spec, so they travel with the figure through prepareSpec, the
// full-resolution render, PDF and SVG export and the .gvfig container without
// any of those having to know they exist.
//
// Changing one calls update() rather than rebuild(): a note does not alter the
// data, the range or the prepared spec, so re-deriving all of that to move a
// label would be work for nothing - and on a slow engine it would mean a
// forty-second wait to correct a typo.
QVariantList PlotCanvas::annotations() const {
    QVariantList out;
    for(const PlotAnnotation& a:spec_.annotations){
        out.append(QVariantMap{
            {QStringLiteral("x"),a.x},
            {QStringLiteral("y"),a.y},
            {QStringLiteral("text"),a.text},
            {QStringLiteral("offsetX"),a.offsetX},
            {QStringLiteral("offsetY"),a.offsetY},
            {QStringLiteral("leader"),a.leader}});
    }
    return out;
}

void PlotCanvas::setAnnotating(bool on){
    if(annotating_==on) return;
    annotating_=on;
    noteDrag_=-1;
    noteMoved_=false;
    // A drag half-finished when the mode changed would otherwise pan on the
    // next move event, after the press that started it has been reinterpreted.
    dragging_=false;
    refreshCursor();
    emit annotationsChanged();
}

// WHAT THE POINTER SAYS THE FIGURE WILL DO.
//
// Asked for as "allow a drag function too on the visualisation so it can also
// be dragged to view the right location" - and dragging already worked. A
// press has set dragging_ and every move since has called panByPixels for as
// long as the canvas has accepted mouse buttons at all; a probe that sends a
// press and six moves pans a Line Chart, a Bar and a 2-D Heatmap, and correctly
// refuses on a Pie.
//
// So nothing was missing except any sign that it was there. The pointer stayed
// an arrow over the figure, which is what it is over a label, a background and
// every other part of the window that does nothing when you press it. A
// feature nobody can find is not a feature, and the honest fix is the
// affordance rather than a second implementation of the thing that works.
//
// The open hand is what a map, a PDF reader and every plotting tool use for
// "this will move under you", and it closes while it is moving. Annotating
// keeps its crosshair, because in that mode a press places a note rather than
// grabbing the figure, and the cursor has to say which of the two is about to
// happen.
void PlotCanvas::refreshCursor(){
    if(annotating_){ setCursor(Qt::CrossCursor); return; }
    // view3D is included: a drag turns the cube rather than sliding a range,
    // but it is still "take hold of this and move it", which is what the hand
    // means. frameInteractive is included for the same reason - there the drag
    // moves the drawing inside its frame - and it is the case that most needs
    // the pointer to say so, because until now a treemap or a Sankey did
    // nothing at all when pressed and nobody would think to try again.
    if(!viewInteractive()&&!view3D()&&!frameInteractive()){
        setCursor(Qt::ArrowCursor); return;
    }
    setCursor(dragging_?Qt::ClosedHandCursor:Qt::OpenHandCursor);
}

int PlotCanvas::addAnnotation(double x,double y,const QString& text){
    if(text.trimmed().isEmpty()) return -1;
    if(!std::isfinite(x)||!std::isfinite(y)) return -1;
    PlotAnnotation note;
    note.x=x; note.y=y; note.text=text.trimmed();
    spec_.annotations.append(note);
    // The full-resolution image on screen was rendered without this note, so it
    // is no longer a picture of the current figure.
    showingFull_=false;
    update();
    emit annotationsChanged();
    scheduleFullRender();
    return int(spec_.annotations.size())-1;
}

void PlotCanvas::updateAnnotation(int index,const QString& text){
    if(index<0||index>=spec_.annotations.size()) return;
    const QString trimmed=text.trimmed();
    // Emptying the text deletes it. The alternative is an invisible note that
    // still catches clicks and still exports.
    if(trimmed.isEmpty()){ removeAnnotation(index); return; }
    if(spec_.annotations[index].text==trimmed) return;
    spec_.annotations[index].text=trimmed;
    showingFull_=false;
    update();
    emit annotationsChanged();
    scheduleFullRender();
}

int PlotCanvas::annotationAt(double px,double py) const {
    // Against the rectangles the LAST RENDER recorded, not against a repeat of
    // the placement rules. Those rules clip on the anchor, nudge a label back
    // inside the frame and pin the leading edge of one too wide to fit; a
    // second copy of them here would be free to drift, and the symptom would be
    // notes that cannot be clicked.
    const QVector<QRectF>& boxes=qtBackend_.annotationBoxes();
    const QPointF where(px,py);
    // Backwards, so the note drawn last - the one on top where two overlap - is
    // the one picked.
    for(int i=qMin(boxes.size(),int(spec_.annotations.size()))-1;i>=0;--i)
        if(!boxes[i].isNull()&&boxes[i].contains(where)) return i;
    return -1;
}

void PlotCanvas::moveAnnotation(int index,double offsetX,double offsetY){
    if(index<0||index>=spec_.annotations.size()) return;
    if(!std::isfinite(offsetX)||!std::isfinite(offsetY)) return;
    spec_.annotations[index].offsetX=qBound(-4000.0,offsetX,4000.0);
    spec_.annotations[index].offsetY=qBound(-4000.0,offsetY,4000.0);
    showingFull_=false;
    update();
    emit annotationsChanged();
    scheduleFullRender();
}

void PlotCanvas::removeAnnotation(int index){
    if(index<0||index>=spec_.annotations.size()) return;
    spec_.annotations.remove(index);
    showingFull_=false;
    update();
    emit annotationsChanged();
    scheduleFullRender();
}

void PlotCanvas::clearAnnotations(){
    if(spec_.annotations.isEmpty()) return;
    spec_.annotations.clear();
    showingFull_=false;
    update();
    emit annotationsChanged();
    scheduleFullRender();
}

void PlotCanvas::setGridVisible(bool on){
    if(spec_.style.gridVisible==on) return;
    spec_.style.gridVisible=on;
    // Chrome, not data: the prepared spec and the series are unchanged, so this
    // is a repaint rather than a rebuild. The accepted full-resolution image is
    // a picture of the old grid though, so it stops being the current one.
    showingFull_=false;
    update();
    scheduleFullRender();
    emit styleChanged();
}

void PlotCanvas::setGridDensity(int ticks){
    const int clamped=qBound(0,ticks,25);
    if(spec_.style.gridDensity==clamped) return;
    spec_.style.gridDensity=clamped;
    showingFull_=false;
    update();
    scheduleFullRender();
    emit styleChanged();
}

void PlotCanvas::setGridDensityY(int ticks){
    const int clamped=qBound(0,ticks,25);
    if(spec_.style.gridDensityY==clamped) return;
    spec_.style.gridDensityY=clamped;
    showingFull_=false;
    update();
    scheduleFullRender();
    emit styleChanged();
}

void PlotCanvas::setPieLabels(int mode){
    const int clamped=qBound(0,mode,3);
    if(spec_.style.pieLabels==clamped) return;
    spec_.style.pieLabels=clamped;
    // A painting decision, like the grid: the data and the prepared spec are
    // unchanged, so this is a repaint rather than a rebuild.
    showingFull_=false;
    update();
    scheduleFullRender();
    emit styleChanged();
}

void PlotCanvas::setLegendLabels(int mode){
    const int clamped=qBound(0,mode,1);
    if(spec_.style.legendLabels==clamped) return;
    spec_.style.legendLabels=clamped;
    showingFull_=false;
    update();
    scheduleFullRender();
    emit styleChanged();
}

void PlotCanvas::setPolarConvention(int mode){
    const int clamped=qBound(0,mode,1);
    if(spec_.style.polarConvention==clamped) return;
    spec_.style.polarConvention=clamped;
    showingFull_=false;
    update();
    scheduleFullRender();
    emit styleChanged();
}

void PlotCanvas::setScaleLabelsVisible(bool on){
    if(spec_.style.scaleLabelsVisible==on) return;
    spec_.style.scaleLabelsVisible=on;
    showingFull_=false;
    update();
    scheduleFullRender();
    emit styleChanged();
}

// ======================================================================
// The camera, for the projected engines
//
// A 3-D figure that cannot be turned is a photograph of a 3-D figure. The
// angles were constants inside the painter, so the one view the person got was
// whichever pair of numbers had been typed there - and if the interesting face
// of the surface happened to point away, that was that.
//
// These write onto spec_.view3d, which is the same principle the 2-D pan and
// zoom follow with the axis limits: what is on screen is what is exported, and
// the full-resolution render is a render of the view being looked at rather
// than of the default one.
// ======================================================================

bool PlotCanvas::view3D() const {
    // Every engine drawn into the projected cube. engineHasAxes is false for
    // these AND for pie, polar and treemap, so it cannot be the test on its
    // own: a pie has no camera either.
    const QString& e=spec_.engine;
    if(e.startsWith(QLatin1String("3D "))) return true;
    static const QSet<QString> kProjected{
        QStringLiteral("Surface + Contours"),QStringLiteral("Comet 3D"),
        QStringLiteral("Ribbon"),
        QStringLiteral("Cone Plot"),QStringLiteral("Stream Tube"),
        QStringLiteral("Stream Ribbon"),QStringLiteral("Tensor Glyph Field"),
        QStringLiteral("Volume Show"),QStringLiteral("Volume Slice"),
        QStringLiteral("Isosurface"),QStringLiteral("Isonormals"),
        QStringLiteral("Isocaps"),QStringLiteral("Contour Slice")};
    return kProjected.contains(e)&&!spec_.series.isEmpty();
}

void PlotCanvas::setAzimuth(double degrees){
    // Wrapped rather than clamped: turning past the back of the cube and
    // continuing round is what a person expects from a horizontal drag, and a
    // clamp there feels like the figure has hit something.
    double wrapped=std::fmod(degrees,360.0);
    if(wrapped>180.0) wrapped-=360.0;
    if(wrapped<-180.0) wrapped+=360.0;
    if(qFuzzyCompare(spec_.view3d.azimuth,wrapped)) return;
    spec_.view3d.azimuth=wrapped;
    cameraMoved();
}

void PlotCanvas::setElevation(double degrees){
    // Clamped, and this one has to be: past 90 degrees the cube turns inside
    // out and the painter's algorithm draws the far faces over the near ones,
    // which reads as the surface having been turned inside out - because it
    // has.
    const double clamped=qBound(-89.0,degrees,89.0);
    if(qFuzzyCompare(spec_.view3d.elevation,clamped)) return;
    spec_.view3d.elevation=clamped;
    cameraMoved();
}

void PlotCanvas::rotateByPixels(double dx,double dy){
    if(!view3D()) return;
    // A drag across the width of the item turns the figure most of the way
    // round, which is the gain that makes a cube feel attached to the finger.
    const double across=qMax(160.0,width());
    setAzimuth(spec_.view3d.azimuth+dx*(300.0/across));
    setElevation(spec_.view3d.elevation+dy*(180.0/qMax(160.0,height())));
}

void PlotCanvas::setCameraZoom(double factor){
    if(!(factor>0.0)) return;
    const double next=qBound(0.35,factor,4.0);
    if(qFuzzyCompare(next,spec_.view3d.zoom)) return;
    spec_.view3d.zoom=next;
    cameraMoved();
}

void PlotCanvas::zoom3DBy(double factor,const QPointF& about){
    if(!view3D()||!(factor>0.0)) return;
    // Bounded so the figure cannot be driven out of its own frame. The old
    // ceiling of 40x did exactly that: a few wheel notches and the cube, the
    // axes and every number on them were off-screen, leaving a full-bleed
    // wash of colour that no longer looked like a plot at all - and with
    // nothing recognisable left in view, turning it did not look like turning.
    const double next=qBound(0.35,spec_.view3d.zoom*factor,4.0);
    if(qFuzzyCompare(next,spec_.view3d.zoom)) return;

    // ABOUT THE POINTER, the way the 2-D figures already zoom.
    //
    // This used to change the zoom and nothing else, which scales about the
    // centre of the canvas - so zooming towards a peak in the corner of a
    // surface moved it further out of the frame, and the way to look at it was
    // to zoom in and then discover there was no way to get there.
    //
    // Keeping a screen point fixed under a change of scale is one line of
    // algebra. A model point m is drawn at
    //
    //     S(m) = centre + (pan + M(m)) * scale
    //
    // where M is the projection before scaling. For the point currently under
    // the cursor to stay under it, the pan has to absorb the change:
    //
    //     pan += (P - centre) / scale * (scale/newScale - 1)
    //
    // `centre` and `scale` come from the backend rather than from a second copy
    // of makeProjection here - see QtPlotBackend::cameraFrameFor. A private
    // copy of that arithmetic is how the frame and the picture come to disagree
    // about where the figure is.
    const QRectF target(0,0,width(),height());
    const QtPlotBackend::CameraFrame frame=
        qtBackend_.cameraFrameFor(spec_,target);
    if(frame.valid&&!about.isNull()&&target.contains(about)){
        const double ratio=spec_.view3d.zoom/next;   // scale/newScale
        const QPointF offset=about-frame.origin;
        spec_.view3d.panX+=offset.x()/frame.scale*(ratio-1.0);
        // Screen y grows downward and the projection's does not.
        spec_.view3d.panY-=offset.y()/frame.scale*(ratio-1.0);
    }
    spec_.view3d.zoom=next;
    cameraMoved();
}

void PlotCanvas::resetCamera(){
    spec_.view3d=PlotView3D{};
    cameraMoved();
}

// One place for what a camera change costs.
//
// Each of these used to call scheduleFullRender() directly, which meant a drag
// started - and abandoned - a full-resolution render of two hundred thousand
// points on every mouse-move event, dozens a second, each one bumping the
// generation counter and throwing away the last. The preview redraw is the only
// thing a turning figure needs; the expensive render is what you want when the
// figure has stopped somewhere.
void PlotCanvas::cameraMoved(){
    showingFull_=false;
    interacting_=true;
    interactionIdle_.start();
    update();
    // No scheduleFullRender here at all any more, for either case. A wheel zoom
    // fires this once per notch with no press or release around it, so the old
    // `if(!dragging_)` started - and abandoned - a full render on every notch.
    // The idle timer above asks for exactly one, once the figure has stopped.
    emit styleChanged();
}

// ======================================================================
// Axis limits and the colour scale
//
// The limits go straight onto spec_.xAxis/yAxis/zAxis, which is where the pan
// and zoom already put theirs and where the backend has always read them: one
// notion of "what is being shown", so the screen, the export and the
// full-resolution render cannot disagree about it. Typing a range IS a zoom
// you did not have to drag - and on the 3-D family, where there is no drag to
// do, it is the only way to say it.
//
// They are ALSO kept in limit*_ , because rebuild() clears the axes whenever
// the figure changes and a style change - a colour map, a grid toggle - can
// trigger one. Without the copy, capping VHPR at ten and then changing the
// colour map put the cap back to 160 with nothing on screen to say why. The
// copy is dropped, deliberately, when the COLUMN in that role changes: a
// ceiling of ten belongs to VHPR and means nothing on whatever replaces it.

namespace { PlotAxis* axisFor(PlotSpec& spec,int role){
    switch(role){
    case 0: return &spec.xAxis;
    case 1: return &spec.yAxis;
    case 2: return &spec.zAxis;
    default: return nullptr;
    }
} }

void PlotCanvas::measureRoleSpans(const ArrowTable& table){
    for(int r=0;r<3;++r){
        dataSpanValid_[r]=false; roleUsed_[r]=false;
        dataLo_[r]=0.0; dataHi_[r]=1.0; roleLabel_[r].clear();
    }
    if(spec_.series.isEmpty()) return;

    // Two shapes of engine, and the difference decides which numbers are the
    // x axis. A line chart puts x on every series' x vector and one column per
    // series on y; a 3-D scatter, a heat map and the rest of the multi-column
    // family put each ROLE in its own series, all of them on y. columnPlan is
    // the same question rebuild() asks when it composes them.
    const QtPlotBackend::ColumnPlan plan=QtPlotBackend::columnPlan(spec_.engine);
    const auto span=[](const QVector<double>& v,double& lo,double& hi){
        double a=std::numeric_limits<double>::infinity(),b=-a;
        for(double x:v) if(std::isfinite(x)){ a=qMin(a,x); b=qMax(b,x); }
        if(!std::isfinite(a)||!std::isfinite(b)) return false;
        if(!(b>a)) b=a+1.0;      // a flat column still needs a usable slider
        lo=a; hi=b; return true;
    };

    if(plan.asSeries){
        // resolvedY_ is the ROLE LIST on these engines - roles[0] is the x
        // column - so it is what says which file column each row of the panel
        // is about.
        for(int r=0;r<3&&r<spec_.series.size();++r){
            roleUsed_[r]=true;
            roleLabel_[r]=spec_.series.at(r).label;
            const QString column=(r<resolvedY_.size())?resolvedY_.at(r):QString();
            if(!column.isEmpty()&&table.hasColumn(column))
                dataSpanValid_[r]=span(table.column(column),dataLo_[r],dataHi_[r]);
            else
                dataSpanValid_[r]=span(spec_.series.at(r).y,dataLo_[r],dataHi_[r]);
        }
    }else{
        roleUsed_[0]=roleUsed_[1]=true;
        roleLabel_[0]=spec_.xAxis.label;
        roleLabel_[1]=spec_.yAxis.label;
        if(!resolvedX_.isEmpty()&&table.hasColumn(resolvedX_))
            dataSpanValid_[0]=span(table.column(resolvedX_),dataLo_[0],dataHi_[0]);
        // Several y columns share one axis, so the axis spans all of them.
        double lo=0,hi=0; bool seen=false;
        for(const QString& name:std::as_const(resolvedY_)){
            if(!table.hasColumn(name)) continue;
            double a=0,b=0;
            if(!span(table.column(name),a,b)) continue;
            lo=seen?qMin(lo,a):a; hi=seen?qMax(hi,b):b; seen=true;
        }
        if(seen){ dataLo_[1]=lo; dataHi_[1]=hi; dataSpanValid_[1]=true; }
        // No third role on a series engine: the colour column, where there is
        // one, is a series of its own and not an axis.
    }
}

// THE FILE, READ ONCE - FOR THE WHOLE APPLICATION.
//
// rebuild() opened the Arrow file and read every record batch out of it, and
// rebuild() runs on every property change: a colour map, a grid toggle, a typed
// axis limit. ArrowTable::load's own note measures a 481 MB file at 76 ms, so
// anything that rebuilt often paid that again for a file whose contents had not
// moved. The full-resolution worker read the same file a second time, on
// purpose, because "loading it is most of the cost for a large dataset and
// belongs off the GUI thread too" - which was true when the GUI thread threw
// its copy away after every rebuild, and is not true now that it keeps one.
//
// It stopped being merely wasteful and became load-bearing when the point
// budget started following the view: a zoom re-spends the budget inside the new
// window, which means a rebuild per wheel notch, which would have meant reading
// the whole file per wheel notch.
//
// SHARED, not one per canvas. A canvas-sized cache would have been a memory
// regression rather than a saving - several figure tabs of one dataset would
// each pin their own copy of it - where one entry behind a mutex means N open
// figures of the same file cost ONE copy, which is less than the code used at
// its peak before this existed.
//
// Keyed on the file's IDENTITY - path, size and modification time - not on the
// path alone. An import that writes a new dataset to the same path is ordinary,
// and a cache that trusted the name would go on drawing the previous one.
//
// Handed out as a shared_ptr so that a worker thread reading it cannot have it
// swapped out from under itself when the person opens a different dataset.
namespace {
struct SharedTable {
    QMutex lock;
    QString path;
    qint64 size=-1;
    qint64 stamp=-1;
    std::shared_ptr<const ArrowTable> table;
};
SharedTable& sharedTable(){ static SharedTable cache; return cache; }

std::shared_ptr<const ArrowTable> tableFor(const QString& path){
    if(path.isEmpty()) return {};
    const QFileInfo info(path);
    const qint64 size=info.size();
    const qint64 stamp=info.lastModified().toMSecsSinceEpoch();
    SharedTable& cache=sharedTable();
    QMutexLocker held(&cache.lock);
    if(cache.table&&cache.path==path&&cache.size==size&&cache.stamp==stamp)
        return cache.table;
    // Loaded UNDER the lock deliberately. Two canvases asking for the same
    // file at once would otherwise read it twice and keep the loser's copy;
    // waiting is what each of them would have been doing anyway.
    auto loaded=std::make_shared<ArrowTable>();
    loaded->load(path);
    cache.path=path; cache.size=size; cache.stamp=stamp; cache.table=loaded;
    return cache.table;
}
} // namespace

const ArrowTable& PlotCanvas::loadedTable() const {
    // An empty table to answer with when there is no file. Static so the
    // reference this returns outlives the call, and never written to.
    static const ArrowTable kNoTable;
    table_=tableFor(arrowPath_);
    return table_?*table_:kNoTable;
}

// IS THE X AXIS THE MAPPED X COLUMN?
//
// The window is compared against the mapped x column's values, and the window
// itself comes off the x AXIS. Those are the same quantity for a line, a
// scatter, an area - and they are not the same quantity for most of this
// catalogue. A histogram's x axis is bin values derived from the column being
// counted; a violin's is a slot number; a correlation matrix's is a variable
// index. Windowing one of those by the mapped x column would compare metres
// against counts and drop rows out of the figure on a zoom, silently.
//
// This is the fault this codebase keeps finding in new places: a rule that is
// right for one class of engine applied to every engine. It was written that
// way here too - "not a gridding engine, has axes, therefore window it" - and
// caught before it shipped only because the question "which engines is this
// actually true of" was asked of 434 entries rather than of the one on screen.
//
// Asked of the ENGINE rather than answered from a list, because a list of 434
// names maintained by hand goes stale the first time an engine is added. The
// prepared spec is what gets drawn: if its first series' x is the same array
// the mapped column produced, then the engine draws that column on that axis
// and the window means what it says. preparedFor is a cache lookup - rebuild
// calls it anyway for usesColourMap_ - so this costs three comparisons.
//
// An engine that rewrites only y (a smoothing, a derivative, a cumulative sum)
// passes, and correctly: x really is still the mapped column.
bool PlotCanvas::xAxisIsMappedColumn() const {
    if(spec_.series.isEmpty()) return false;
    const PlotSpec& prepared=qtBackend_.preparedFor(spec_);
    if(prepared.series.isEmpty()) return false;
    const QVector<double>& want=spec_.series.at(0).x;
    const QVector<double>& have=prepared.series.at(0).x;
    if(want.isEmpty()||want.size()!=have.size()) return false;
    const int probes[3]={0,int(want.size()/2),int(want.size()-1)};
    for(int i:probes) if(!(want.at(i)==have.at(i))) return false;
    return true;
}

PlotCanvas::ViewWindow PlotCanvas::windowForDrawing(bool asSeries) const {
    ViewWindow w;
    if(asSeries) return w;
    if(!QtPlotBackend::engineHasAxes(spec_.engine)) return w;
    // Only engines whose x axis IS the mapped column, and only once a rebuild
    // has established that for this engine. The verdict cannot be reached
    // before the series exist, so the first rebuild after an engine changes
    // draws unwindowed and every rebuild after it - the next wheel notch, or
    // the one the idle timer asks for when the gesture stops - is windowed.
    // One frame of a gesture, against a rule that cannot be wrong about an
    // engine nobody thought to add to a list.
    if(!windowUsable_.value(spec_.engine+QLatin1Char('\x1f')+spec_.variant,false))
        return w;
    // BOTH ends, or it is not a window. A figure with only a floor typed into
    // it has an open right-hand side and there is no slice to spend a budget
    // on.
    if(!limitLoSet_[0]||!limitHiSet_[0]) return w;
    w.set=true; w.lo=limitLo_[0]; w.hi=limitHi_[0];
    return w;
}

void PlotCanvas::applyStoredLimits(){
    bool any=false;
    for(int r=0;r<3;++r){
        PlotAxis* axis=axisFor(spec_,r);
        if(!axis) continue;
        // A limit whose role the engine no longer reads is forgotten rather
        // than carried on to whatever occupies that slot next.
        if(!roleUsed_[r]){ limitLoSet_[r]=limitHiSet_[r]=false; continue; }
        if(limitLoSet_[r]){ axis->min=limitLo_[r]; any=true; }
        if(limitHiSet_[r]){ axis->max=limitHi_[r]; any=true; }
    }
    // A typed range is a view, so the figure offers to reset it in the same
    // place a dragged one does.
    if(any) hasView_=true;
}

QVariantList PlotCanvas::axisRanges() const {
    QVariantList out;
    static const char* kNames[3]={"X","Y","Z"};
    for(int r=0;r<3;++r){
        QVariantMap m;
        m.insert(QStringLiteral("role"),r);
        m.insert(QStringLiteral("name"),QString::fromLatin1(kNames[r]));
        m.insert(QStringLiteral("label"),roleLabel_[r]);
        m.insert(QStringLiteral("used"),roleUsed_[r]&&dataSpanValid_[r]);
        m.insert(QStringLiteral("dataMin"),dataLo_[r]);
        m.insert(QStringLiteral("dataMax"),dataHi_[r]);
        m.insert(QStringLiteral("autoMin"),!limitLoSet_[r]);
        m.insert(QStringLiteral("autoMax"),!limitHiSet_[r]);
        // The value a field and a slider should SHOW. An automatic end shows
        // where the axis actually sits, not an empty box: a box that has to be
        // filled before it can be nudged is a box nobody nudges.
        m.insert(QStringLiteral("min"),limitLoSet_[r]?limitLo_[r]:dataLo_[r]);
        m.insert(QStringLiteral("max"),limitHiSet_[r]?limitHi_[r]:dataHi_[r]);
        out.append(m);
    }
    return out;
}

QVariantMap PlotCanvas::colourRange() const {
    // The ramp runs over the third column on the engines that have one, which
    // is every 3-D engine and every gridded field. Where the value being
    // coloured is DERIVED - the magnitude of a vector field, the area of a
    // Voronoi cell - the column span is the wrong yardstick and the fields are
    // shown disabled rather than wrong.
    const bool haveSpan=roleUsed_[2]&&dataSpanValid_[2];
    const double lo=haveSpan?dataLo_[2]:0.0;
    const double hi=haveSpan?dataHi_[2]:1.0;
    const bool autoMin=isUnset(spec_.style.colourMin);
    const bool autoMax=isUnset(spec_.style.colourMax);
    QVariantMap m;
    m.insert(QStringLiteral("used"),usesColourMap_);
    m.insert(QStringLiteral("known"),haveSpan);
    m.insert(QStringLiteral("label"),roleLabel_[2]);
    m.insert(QStringLiteral("dataMin"),lo);
    m.insert(QStringLiteral("dataMax"),hi);
    m.insert(QStringLiteral("autoMin"),autoMin);
    m.insert(QStringLiteral("autoMax"),autoMax);
    m.insert(QStringLiteral("min"),autoMin?lo:spec_.style.colourMin);
    m.insert(QStringLiteral("max"),autoMax?hi:spec_.style.colourMax);
    m.insert(QStringLiteral("levels"),spec_.style.colourLevels);
    m.insert(QStringLiteral("dropOutOfRange"),spec_.style.colourOutOfRangeDropped);
    return m;
}

void PlotCanvas::setAxisLimits(int role,double lo,double hi){
    if(role<0||role>2) return;
    PlotAxis* axis=axisFor(spec_,role);
    if(!axis) return;
    // NaN is how QML says "fit this end". Passing the two ends together rather
    // than one setter each is what lets a range slider move both in one step
    // without the figure being redrawn at an intermediate range where the
    // bottom is momentarily above the top.
    const bool wantLo=std::isfinite(lo);
    const bool wantHi=std::isfinite(hi);
    // A pair that does not increase is refused rather than clamped: silently
    // swapping what somebody typed is worse than leaving it alone, and the
    // fields still show what the axis is.
    if(wantLo&&wantHi&&!(hi>lo)) return;

    limitLoSet_[role]=wantLo; limitHiSet_[role]=wantHi;
    if(wantLo) limitLo_[role]=lo;
    if(wantHi) limitHi_[role]=hi;
    axis->min=wantLo?lo:unsetValue();
    axis->max=wantHi?hi:unsetValue();

    if(wantLo||wantHi) hasView_=true;
    else{
        // Both ends automatic on every axis is the same state resetView leaves
        // the figure in, so it is that state rather than a view of the whole
        // range that happens to look like it.
        bool anyLeft=false;
        for(int r=0;r<3;++r) if(limitLoSet_[r]||limitHiSet_[r]) anyLeft=true;
        if(!anyLeft) hasView_=false;
    }
    showingFull_=false;
    // A TYPED range is a view too, so the point budget follows it exactly as it
    // follows a wheel. Rebuilding is what re-spends the budget inside the new
    // window - see the note above buildPlotSeries - and rebuild() ends by
    // asking for the full-resolution render this used to ask for itself.
    //
    // Only for the x role: the window is an x window, because that is the axis
    // the rows are decimated along.
    if(role==0){ dirty_=true; rebuild(); }
    else scheduleFullRender();
    update();
    emit axisRangesChanged();
    emit stateChanged();
}

void PlotCanvas::clearAxisLimits(int role){
    setAxisLimits(role,std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::quiet_NaN());
}

void PlotCanvas::clearAllLimits(){
    for(int r=0;r<3;++r){
        limitLoSet_[r]=limitHiSet_[r]=false;
        if(PlotAxis* axis=axisFor(spec_,r)){
            axis->min=unsetValue(); axis->max=unsetValue();
        }
    }
    hasView_=false;
    spec_.style.colourMin=unsetValue();
    spec_.style.colourMax=unsetValue();
    showingFull_=false;
    update();
    scheduleFullRender();
    emit axisRangesChanged();
    emit stateChanged();
}

void PlotCanvas::setColourLimits(double lo,double hi){
    const bool wantLo=std::isfinite(lo);
    const bool wantHi=std::isfinite(hi);
    if(wantLo&&wantHi&&!(hi>lo)) return;
    spec_.style.colourMin=wantLo?lo:unsetValue();
    spec_.style.colourMax=wantHi?hi:unsetValue();
    // A painting decision, like the colour map itself: the data and the
    // prepared spec are unchanged, so this repaints rather than rebuilds.
    showingFull_=false;
    update();
    scheduleFullRender();
    emit axisRangesChanged();
}

void PlotCanvas::clearColourLimits(){
    setColourLimits(std::numeric_limits<double>::quiet_NaN(),
                    std::numeric_limits<double>::quiet_NaN());
}

void PlotCanvas::setColourOutOfRangeDropped(bool drop){
    if(spec_.style.colourOutOfRangeDropped==drop) return;
    spec_.style.colourOutOfRangeDropped=drop;
    showingFull_=false;
    update();
    scheduleFullRender();
    emit axisRangesChanged();
}

QVariantList PlotCanvas::customColours() const {
    QVariantList out;
    for(const QColor& c:spec_.style.customColours)
        out.append(c.name(QColor::HexArgb));
    return out;
}

void PlotCanvas::applySeriesColours(){
    // The colours the series would have had, and then the chosen ones over the
    // top. Kept in one place so a pie chart, a line chart and a bar chart agree
    // about which colour is series two.
    const QVector<QColor>& chosen=spec_.style.customColours;
    const QVector<QColor> palette=seriesPalette(colourVisionFromInt(colourVision_));
    if(palette.isEmpty()&&chosen.isEmpty()) return;
    for(int i=0;i<spec_.series.size();++i){
        spec_.series[i].color=chosen.isEmpty()
            ? palette.at(i%palette.size())
            : chosen.at(i%chosen.size());
    }
}

void PlotCanvas::setCustomColour(int index,const QColor& colour){
    if(index<0||index>=spec_.style.customColours.size()) return;
    if(!colour.isValid()) return;
    if(spec_.style.customColours.at(index)==colour) return;
    spec_.style.customColours[index]=colour;
    // Series colours are part of the SPEC, not of the painting, so they are
    // hashed into the fingerprint and have to be rewritten rather than merely
    // repainted. Rewritten in place: a rebuild would re-read the Arrow file to
    // change one swatch.
    applySeriesColours();
    showingFull_=false;
    update();
    scheduleFullRender();
    emit axisRangesChanged();
}

void PlotCanvas::addCustomColour(const QColor& colour){
    if(!colour.isValid()) return;
    if(spec_.style.customColours.size()>=256) return;
    spec_.style.customColours.append(colour);
    applySeriesColours();
    showingFull_=false;
    update();
    scheduleFullRender();
    emit axisRangesChanged();
}

void PlotCanvas::removeCustomColour(int index){
    if(index<0||index>=spec_.style.customColours.size()) return;
    spec_.style.customColours.remove(index);
    applySeriesColours();
    showingFull_=false;
    update();
    scheduleFullRender();
    emit axisRangesChanged();
}

void PlotCanvas::clearCustomColours(){
    if(spec_.style.customColours.isEmpty()) return;
    spec_.style.customColours.clear();
    // Back to the palette, which is what the series carried before anybody
    // chose anything.
    applySeriesColours();
    showingFull_=false;
    update();
    scheduleFullRender();
    emit axisRangesChanged();
}

int PlotCanvas::suggestedColourCount() const {
    if(spec_.style.colourLevels>=2) return spec_.style.colourLevels;
    if(!usesColourMap_){
        // A set of categories: one swatch per series, which for a pie chart is
        // one per sector because the sectors ARE the first series' values.
        const int series=spec_.series.size();
        if(spec_.engine==QLatin1String("Pie")||spec_.engine==QLatin1String("Donut"))
            return qBound(2,int(spec_.series.isEmpty()?0:spec_.series.first().y.size()),12);
        return qBound(2,series,12);
    }
    // A continuous ramp has no natural count. Five stops is enough to shape one
    // and few enough to set by hand in a few seconds.
    return 5;
}

void PlotCanvas::seedCustomColours(int count){
    const int n=qBound(2,count>0?count:suggestedColourCount(),256);
    QVector<QColor> seeded;
    seeded.reserve(n);
    if(usesColourMap_){
        // The map as it is drawn now, sampled evenly. Starting from the ramp
        // on screen means the first thing the person sees after clicking
        // "choose my own" is the figure they already had, unchanged - so the
        // only difference is that it has become editable.
        for(int i=0;i<n;++i){
            const double t=(n==1)?0.5:double(i)/double(n-1);
            seeded.append(colourmaps::sample(colourmaps::tableFor(spec_.style.colourMap),t));
        }
    }else{
        const QVector<QColor> palette=seriesPalette(colourVisionFromInt(colourVision_));
        for(int i=0;i<n;++i)
            seeded.append(palette.isEmpty()?QColor(Qt::gray):palette.at(i%palette.size()));
    }
    spec_.style.customColours=seeded;
    applySeriesColours();
    showingFull_=false;
    update();
    scheduleFullRender();
    emit axisRangesChanged();
}

void PlotCanvas::setColourLevels(int levels){
    // 0 is the continuous ramp; anything else is at least two bands, because
    // "one band" is a figure painted a single colour and nobody means that.
    const int clamped=(levels<=0)?0:qBound(2,levels,256);
    if(spec_.style.colourLevels==clamped) return;
    spec_.style.colourLevels=clamped;
    showingFull_=false;
    update();
    scheduleFullRender();
    emit axisRangesChanged();
}

// The formula the Function and Implicit engines plot.
//
// Validated here rather than only in the renderer, because the renderer's
// answer to a formula it cannot parse is to draw nothing, and "nothing" is what
// an empty dataset looks like too. Compiling as it is typed means the field can
// say "unknown function 'sni'" instead, which is the difference between a typo
// and a program that does not work.
//
// The variables offered depend on the engine, and they are the same ones
// prepareSpecCore binds when it evaluates: t for a parametric curve, x and y
// for a surface or a contour, x alone for a plain function of one variable.
void PlotCanvas::setExpression(const QString& text){
    if(spec_.expression==text) return;
    spec_.expression=text;

    expressionError_.clear();
    const QString trimmed=text.trimmed();
    if(!trimmed.isEmpty()&&usesExpression()){
        QStringList variables;
        if(spec_.engine==QLatin1String("Function 3D Parametric")) variables<<QStringLiteral("t");
        else if(spec_.engine==QLatin1String("Function Surface")
              ||spec_.engine==QLatin1String("Function Mesh")
              ||spec_.engine==QLatin1String("Function Contour")
              ||spec_.engine==QLatin1String("Implicit Surface")
              ||spec_.engine==QLatin1String("Implicit Function"))
            variables<<QStringLiteral("x")<<QStringLiteral("y");
        else variables<<QStringLiteral("x");

        // A parametric curve is three formulas separated by semicolons, so each
        // part is compiled on its own and the message says which one failed.
        const QStringList parts=(spec_.engine==QLatin1String("Function 3D Parametric"))
                                ? trimmed.split(QLatin1Char(';'))
                                : QStringList{trimmed};
        for(int i=0;i<parts.size();++i){
            Expression compiled;
            if(compiled.compile(parts.at(i).trimmed(),variables)) continue;
            expressionError_=(parts.size()>1)
                ? QStringLiteral("part %1: %2").arg(i+1).arg(compiled.error())
                : compiled.error();
            break;
        }
    }

    // A formula IS the data on these engines, so this is a rebuild rather than
    // a repaint - the same as changing a mapped column.
    dirty_=true;
    showingFull_=false;
    update();
    scheduleFullRender();
    emit sourceChanged();
    emit stateChanged();
}

QStringList PlotCanvas::expressionFunctions(){
    return Expression::knownFunctions();
}

// The chosen map, the colour-vision mode, and the figure note that explains any
// difference between them, resolved in one place.
//
// This exists because colour vision used to stop at the SERIES palette. On a
// line chart that is the whole figure and the setting worked; on a surface, a
// heat map, a contour or a vector field there are no series at all - the colour
// map is the data - so on exactly the plots where a colour-blind reader needs
// the setting most, it changed nothing. That is the report this answers: a
// Plasma-coloured surface, and Monochrome selected, and a figure still in full
// colour.
//
// What it does NOT do is silently overrule the person. The chosen map is kept;
// only the painted one changes, only when the measurement in ColourMapSafety.h
// says the chosen map fails for that vision, and the substitute keeps the map's
// role so the figure cannot come to claim something different about the data.
void PlotCanvas::applyColourVisionToMap(){
    const ColourVision vision=colourVisionFromInt(colourVision_);
    const QString chosen=chosenColourMap_.isEmpty()?QStringLiteral("Viridis")
                                                   :chosenColourMap_;
    // nearestSafeFor, not safeSubstituteFor. The fixed table answers with one
    // map per role, so every unreadable sequential map became Cividis whatever
    // the person had chosen - Hot, Terrain and Gist Ncar all landing on the
    // same blue-yellow ramp. The role still cannot change, because the role is
    // what the figure claims about the data; which map inside that role is
    // taste, and the nearest one keeps it.
    // recommendedFor, not nearestSafeFor.
    //
    // nearestSafeFor only acts when the chosen map FAILS the table. Seismic
    // passes for a protanope, so choosing Protanopia left the figure exactly as
    // it was, and that was reported as the setting doing nothing. It was not
    // doing nothing; it was applying the bar for "may this be offered" to the
    // question "what should this person be shown".
    //
    // Somebody who turns on a colour-blind mode is not asking for anything
    // above the minimum, they are asking for something easy to read. So the
    // painted map is held to the RECOMMENDED set for their vision and their
    // map's role, and Seismic becomes Bwr - the same blue-white-red shape, from
    // the tier of maps designed for the job.
    //
    // A map already in that set is left completely alone, so Viridis, Cividis,
    // RdBu and Twilight do not move.
    spec_.style.colourMap=colourmaps::recommendedFor(chosen,vision);
    // A substitution that costs something says so ON THE FIGURE, not only in
    // the panel - the person reading the exported PDF is not the person who
    // chose the setting.
    //
    // Nothing is claimed while custom colours are painting: the map is not on
    // the figure, so a note about which map was substituted would describe
    // something the reader cannot see.
    spec_.figureNote=usingCustomColours()
                         ? QString()
                         : colourmaps::substitutionNote(chosen,vision);
}

QString PlotCanvas::colourVisionNote() const {
    const ColourVision vision=colourVisionFromInt(colourVision_);
    if(vision==ColourVision::Standard) return QString();
    const QString chosen=chosenColourMap_.isEmpty()?QStringLiteral("Viridis")
                                                   :chosenColourMap_;
    const QStringList names=colourVisionNames();
    const QString mode=names.value(colourVision_).section(QLatin1Char(' '),0,0);
    if(!usesColourMap())
        return tr("%1: series colours only - this engine does not colour a field.")
                   .arg(mode);

    // CUSTOM COLOURS BEAT THE MAP, so nothing below is true while they are set.
    //
    // colourMapStyled returns sampleStops(customColours,t) whenever that list
    // is non-empty and never looks at the map at all. So with custom colours in
    // use, choosing a colour-vision mode changed the map, changed the
    // substitute, wrote "drawn in Cividis" into this note - and left the figure
    // exactly as it was, because the map had not been painting it for some
    // time. Reported, correctly, as the setting doing nothing.
    //
    // The note said what the map WOULD be rather than what the figure IS. This
    // says which of the two is on the screen, and how to get back to the other.
    if(usingCustomColours())
        return tr("%1: your own colours are painting this figure, so the colour "
                  "map is not in use and this setting cannot reach it. They are "
                  "not checked for colour-vision safety. Reset to the map to let "
                  "it apply.").arg(mode);

    if(colourmaps::mapIsSafeFor(chosen,vision)){
        // The answer a person gets when they pick a vision mode and the picture
        // does not move. Without it the setting reads as broken, which is the
        // whole reason this string exists.
        return tr("%1: %2 already passes for this vision, so the colour map is "
                  "unchanged.").arg(mode,chosen);
    }
    const QString note=colourmaps::substitutionNote(chosen,vision);
    const QString swap=tr("%1: %2 does not pass, so the figure is drawn in %3.")
                           .arg(mode,chosen,spec_.style.colourMap);
    return note.isEmpty()?swap:(swap+QLatin1Char(' ')+note);
}

void PlotCanvas::setColourMap(const QString& name){
    if(chosenColourMap_==name) return;
    chosenColourMap_=name;
    applyColourVisionToMap();
    // This called rebuild(), which is guarded by `dirty_` and returns
    // immediately unless something set it - and nothing here did. So choosing a
    // colour map changed the spec and then did nothing at all: no repaint, no
    // invalidation of the accepted full-resolution image, which went on being
    // drawn in the old map. The control looked broken because it was.
    //
    // A colour map is a painting decision, like the grid: the data and the
    // prepared spec are unchanged, so this is a repaint rather than a rebuild.
    showingFull_=false;
    update();
    scheduleFullRender();
    emit styleChanged();
}

// The scattered-estimator settings. Every one of these changes what the grid
// CONTAINS rather than how it is painted, so each has to invalidate the cached
// grid - which specFingerprint does, because it hashes them.
#define GV_FIELD_SETTER(Name,Member,Clamp)                       \
void PlotCanvas::set##Name(int v){                               \
    const int clamped=Clamp;                                     \
    if(spec_.style.Member==clamped) return;                      \
    spec_.style.Member=clamped;                                  \
    showingFull_=false;                                          \
    update();                                                    \
    scheduleFullRender();                                        \
    emit styleChanged();                                         \
}
GV_FIELD_SETTER(FieldEstimator,fieldEstimator,
                qBound(-1,v,int(Estimator::Count)-1))
GV_FIELD_SETTER(FieldExtrapolation,fieldExtrapolation,
                qBound(0,v,int(Extrapolation::Count)-1))
GV_FIELD_SETTER(FieldValuePolicy,fieldValuePolicy,
                qBound(0,v,int(ValuePolicy::Count)-1))
GV_FIELD_SETTER(FieldResponseSpace,fieldResponseSpace,
                qBound(0,v,int(ResponseSpace::Count)-1))
GV_FIELD_SETTER(FieldNeighbours,fieldNeighbours,qBound(4,v,512))
GV_FIELD_SETTER(FieldFootprint,fieldFootprint,
                qBound(0,v,int(FailureFootprint::Count)-1))
GV_FIELD_SETTER(FieldBridging,fieldBridging,qBound(0,v,int(Bridging::Count)-1))
GV_FIELD_SETTER(FieldBridgeMaxCells,fieldBridgeMaxCells,qBound(1,v,2500))
GV_FIELD_SETTER(FieldInvalidDisplay,fieldInvalidDisplay,
                qBound(0,v,int(InvalidDisplay::Count)-1))
GV_FIELD_SETTER(FieldKrigingVariogram,fieldKrigingVariogram,qBound(0,v,2))
#undef GV_FIELD_SETTER

void PlotCanvas::setFieldIdwPower(double v){
    const double clamped=qBound(0.5,v,8.0);
    if(qFuzzyCompare(spec_.style.fieldIdwPower,clamped)) return;
    spec_.style.fieldIdwPower=clamped;
    showingFull_=false; update(); scheduleFullRender(); emit styleChanged();
}

void PlotCanvas::setFieldSmoothing(double v){
    const double clamped=qBound(0.0,v,10.0);
    if(qFuzzyCompare(spec_.style.fieldSmoothing,clamped)) return;
    spec_.style.fieldSmoothing=clamped;
    showingFull_=false; update(); scheduleFullRender(); emit styleChanged();
}

QStringList PlotCanvas::fieldEstimatorNames(){ return estimatorNames(); }

QVariantList PlotCanvas::fieldEstimatorList(){
    // Name, family and whether this build computes it. The interface offers
    // only the implemented ones; the rest are in the list so a figure saved by
    // GraphVis 17 still round-trips through the same indices.
    const QStringList names=estimatorNames();
    const QStringList groups=estimatorGroupNames();
    QVariantList out;
    for(int i=0;i<names.size();++i){
        QVariantMap m;
        m.insert(QStringLiteral("index"),i);
        m.insert(QStringLiteral("name"),names.at(i));
        m.insert(QStringLiteral("group"),groups.value(i));
        m.insert(QStringLiteral("available"),estimatorImplemented(Estimator(i)));
        out.append(m);
    }
    return out;
}

QStringList PlotCanvas::fieldExtrapolationNames(){ return extrapolationNames(); }
QStringList PlotCanvas::fieldFootprintNames(){ return failureFootprintNames(); }
QStringList PlotCanvas::fieldBridgingNames(){ return bridgingNames(); }
QStringList PlotCanvas::fieldInvalidDisplayNames(){ return invalidDisplayNames(); }
QStringList PlotCanvas::fieldKrigingVariogramNames(){ return krigingVariogramNames(); }

void PlotCanvas::setFieldLoessFraction(double v){
    const double clamped=qBound(0.02,v,1.0);
    if(qFuzzyCompare(spec_.style.fieldLoessFraction,clamped)) return;
    spec_.style.fieldLoessFraction=clamped;
    showingFull_=false; update(); scheduleFullRender(); emit styleChanged();
}
QStringList PlotCanvas::fieldValuePolicyNames(){ return valuePolicyNames(); }
QStringList PlotCanvas::fieldResponseSpaceNames(){ return responseSpaceNames(); }

void PlotCanvas::setFieldInterpolation(int mode){
    const int clamped=qBound(0,mode,3);
    if(spec_.style.fieldInterpolation==clamped) return;
    spec_.style.fieldInterpolation=clamped;
    // Unlike the colour map this DOES change what is drawn rather than how, and
    // the gridded field is cached on the spec fingerprint - which hashes this,
    // so the cache rebuilds itself.
    showingFull_=false;
    update();
    scheduleFullRender();
    emit styleChanged();
}

void PlotCanvas::setFieldResolution(int cells){
    const int clamped=cells<=0?0:qBound(12,cells,360);
    if(spec_.style.fieldResolution==clamped) return;
    spec_.style.fieldResolution=clamped;
    showingFull_=false;
    update();
    scheduleFullRender();
    emit styleChanged();
}

void PlotCanvas::setMeshDensity(int lines){
    // 0 means "follow the grid", which is what the wireframe did before this
    // existed. Anything else is clamped to a range a person can actually read:
    // below four the mesh stops describing the surface, above about 120 it is
    // solid again and the control has stopped doing anything.
    const int clamped=lines<=0?0:qBound(4,lines,120);
    if(spec_.style.meshDensity==clamped) return;
    spec_.style.meshDensity=clamped;
    showingFull_=false;
    update();
    scheduleFullRender();
    emit styleChanged();
}


// The same eighty four, grouped the way GraphVis 17 grouped them. A flat list
// of eighty four names is not a choice anyone can make; "Diverging" and
// "Perceptually Uniform" are how the choice is actually reasoned about.
// The map chooser's list, FOR THIS READER.
//
// It used to be the eight authored categories, the same eight for everybody.
// Choosing a colour-vision mode then left every map still on offer, so the
// setting looked as though it had done nothing - which is what was reported.
// Greying the unusable ones was the first answer and it was only half of one:
// a greyed row is still a row, still takes space, and still reads as something
// you might be able to have.
//
// So in a colour-vision mode the unusable maps are not listed at all. Two
// deliberate exceptions:
//
//   - The map the person has CHOSEN is always listed, even when it fails for
//     this reader. Dropping it would leave the control showing a selection
//     that is not in its own list, and the figure is being drawn in a
//     substitute anyway - which the tooltip and the note beside the chooser
//     both say. Hiding their choice would hide that explanation too.
//
//   - A "Best for" group goes first, from bestForReader. Hiding the bad maps
//     answers "which can I not have"; it does not answer "which should I
//     pick", and of the maps that pass, some clear the bar by a point and
//     some by fifteen.
QVariantList PlotCanvas::colourMapCategories() const {
    const ColourVision vision=colourVisionFromInt(colourVision_);
    const QString chosen=chosenColourMap_.isEmpty()?QStringLiteral("Viridis")
                                                   :chosenColourMap_;
    QVariantList out;

    if(vision!=ColourVision::Standard){
        const QStringList best=colourmaps::bestForReader(vision);
        if(!best.isEmpty()){
            // Named for the reader rather than "Recommended", so it is clear
            // the list is a measurement about them and not a house style.
            const QString reader=colourVisionNames().value(colourVision_)
                                     .section(QLatin1Char(' '),0,0);
            out.append(QVariantMap{
                {QStringLiteral("name"),tr("Best for %1").arg(reader)},
                {QStringLiteral("maps"),QVariant(best)}});
        }
    }

    // A GROUP PER DEFICIENCY, ALWAYS, whatever mode the person is in.
    //
    // The "Best for" group above appears only when a colour-vision mode is
    // switched on, which serves a reader choosing for themselves and nobody
    // else. Most figures are drawn by someone with ordinary colour vision for
    // an audience that is not: a paper is read by everyone who reads it, and
    // about one man in twelve has some form of deficiency. That person cannot
    // switch a mode on in this program to help them, because they are not the
    // one using it.
    //
    // So the four shortlists are here all the time, folded, ordered by the
    // measurement. They are the same lists `bestForReader` returns - the same
    // function, not a second copy of the ranking - so the group a reader sees
    // when their own mode is on and the group an author picks from cannot say
    // different things.
    const QStringList visionNames=colourVisionNames();
    for(int mode=1;mode<visionNames.size();++mode){
        const ColourVision kind=colourVisionFromInt(mode);
        if(kind==ColourVision::Standard) continue;
        // Already offered at the top under "Best for", and offering the same
        // list twice is two chances to notice it has changed in one place.
        if(kind==vision) continue;
        const QStringList best=colourmaps::bestForReader(kind);
        if(best.isEmpty()) continue;
        const QString reader=visionNames.value(mode)
                                 .section(QLatin1Char(' '),0,0);
        out.append(QVariantMap{
            {QStringLiteral("name"),tr("Colour-blind \u00b7 %1").arg(reader)},
            {QStringLiteral("maps"),QVariant(best)}});
    }

    for(const auto& cat:colourmaps::categories()){
        QStringList keep;
        for(const QString& map:cat.second)
            if(vision==ColourVision::Standard
               ||colourmaps::mapIsSafeFor(map,vision)
               ||map==chosen)
                keep.append(map);
        // A category with nothing left in it is a heading with no contents.
        if(keep.isEmpty()) continue;
        out.append(QVariantMap{
            {QStringLiteral("name"),cat.first},
            {QStringLiteral("maps"),QVariant(keep)}});
    }
    return out;
}

// A strip of the map, as an image the interface can show beside its name.
// A colour map cannot be chosen from a word: nobody knows what "Gist Ncar"
// looks like, and eighty four words is a list to scroll rather than a choice
// to make.
QString PlotCanvas::colourMapPreview(const QString& name,int width,int height){
    const int w=qBound(8,width,512), h=qBound(4,height,128);
    QImage strip(w,h,QImage::Format_RGB32);
    const auto table=colourmaps::tableFor(name);
    for(int x=0;x<w;++x){
        const QRgb rgb=colourmaps::sample(table,double(x)/double(w-1)).rgb();
        for(int y=0;y<h;++y) strip.setPixel(x,y,rgb);
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    strip.save(&buffer,"PNG");
    return QStringLiteral("data:image/png;base64,")+QString::fromLatin1(png.toBase64());
}

// Empty when the map is usable by the CURRENT reader, and a sentence saying
// what goes wrong and what will be drawn instead when it is not.
//
// Goes through colourmaps::colourMapWarning, which is built on the SAME
// mapIsSafeFor that applyColourVisionToMap uses to choose what to paint. That
// is the whole point of it: this drives the greying-out of entries in the map
// chooser, and if it were measured independently the chooser and the figure
// would disagree - which they did, on 48 of 252 map-and-reader pairs, until
// both were pointed at the one table.
//
// Deliberately a function of the CURRENT setting rather than of a mode passed
// in: the caller is the map chooser, and the question it asks is "can the
// person looking at this screen read this map", which has one answer at a time.
QString PlotCanvas::colourMapWarning(const QString& map) const {
    return colourmaps::colourMapWarning(map,colourVisionFromInt(colourVision_));
}

void PlotCanvas::setColourVision(int mode){
    const int clamped=qBound(0,mode,4);
    if(colourVision_==clamped) return;
    colourVision_=clamped;
    // The SAME bug setColourMap above was fixed for, in four more setters.
    //
    // This set dirty_ and repainted, and paint() returns early with the
    // accepted full-resolution image whenever one matches the generation - so
    // on any dataset big enough to have a second render, which is every dataset
    // this setting matters on, the figure went on being drawn in the OLD
    // palette. Choosing a colour-vision mode appeared to do nothing at all.
    //
    // Rotating the figure is what made it seem to work: cameraMoved() drops
    // showingFull_, so the live preview comes back with the new palette, and
    // the render that lands when the rotation stops has it too. The setting was
    // reaching the renderer the whole time; the picture on screen was a
    // photograph of the previous one.
    //
    // And the fix above was still not enough, which is the report that followed
    // it: a 3-D surface with Monochrome selected stayed in full colour. The
    // repaint was reaching the renderer correctly by then - there was simply
    // nothing for it to change. A surface has no series, so a setting that only
    // touched the series palette had no way to reach the picture at all. The
    // colour map is the data on that engine, so the setting has to reach the
    // colour map; applyColourVisionToMap is where that happens.
    applyColourVisionToMap();

    // Unlike a colour map, this one DOES need the rebuild: the series colours
    // are assigned there. Hence keepViewOnRebuild_ - see rebuild().
    keepViewOnRebuild_=true;
    dirty_=true;
    showingFull_=false;
    update();
    scheduleFullRender();
    emit styleChanged();
}

// The transform belongs to the DATA, so unlike the grid or the colour map this
// has to go all the way back through the rewrite - a quantile axis changes what
// every point's coordinate is.
void PlotCanvas::setXTransform(int mode){
    const int clamped=qBound(0,mode,4);
    if(spec_.xAxis.transform==clamped&&!linkTransforms_) return;
    spec_.xAxis.transform=clamped;
    spec_.xAxis.log10=(clamped==AxisLog10);
    if(linkTransforms_&&spec_.yAxis.transform!=clamped){
        spec_.yAxis.transform=clamped;
        spec_.yAxis.log10=(clamped==AxisLog10);
    }
    // A view set on the old values means nothing on the new ones, and keeping
    // it would open the figure clipped to a range that no longer exists in it.
    hasView_=false;
    spec_.xAxis.min=unsetValue(); spec_.xAxis.max=unsetValue();
    spec_.yAxis.min=unsetValue(); spec_.yAxis.max=unsetValue();
    showingFull_=false;
    update();
    scheduleFullRender();
    emit sourceChanged();
    emit stateChanged();
}

void PlotCanvas::setYTransform(int mode){
    const int clamped=qBound(0,mode,4);
    if(spec_.yAxis.transform==clamped&&!linkTransforms_) return;
    spec_.yAxis.transform=clamped;
    spec_.yAxis.log10=(clamped==AxisLog10);
    if(linkTransforms_&&spec_.xAxis.transform!=clamped){
        spec_.xAxis.transform=clamped;
        spec_.xAxis.log10=(clamped==AxisLog10);
    }
    hasView_=false;
    spec_.xAxis.min=unsetValue(); spec_.xAxis.max=unsetValue();
    spec_.yAxis.min=unsetValue(); spec_.yAxis.max=unsetValue();
    showingFull_=false;
    update();
    scheduleFullRender();
    emit sourceChanged();
    emit stateChanged();
}

// The third mapped column, which is a height on one engine and a measured
// quantity running under a colour map on another.
//
// The link tick is deliberately NOT honoured here. "Both axes the same" is
// about x and y, which are the pair a reader compares; a heat map's colour
// scale and its two position axes are not that pair, and standardising the
// value column because someone logged the x axis would be a surprise.
void PlotCanvas::setZTransform(int mode){
    const int clamped=qBound(0,mode,4);
    if(spec_.zAxis.transform==clamped) return;
    spec_.zAxis.transform=clamped;
    spec_.zAxis.log10=(clamped==AxisLog10);
    hasView_=false;
    showingFull_=false;
    update();
    scheduleFullRender();
    emit sourceChanged();
    emit stateChanged();
}

QString PlotCanvas::thirdAxisRole() const {
    const QtPlotBackend::ColumnPlan plan=QtPlotBackend::columnPlan(spec_.engine);
    // maximum 0 means "every mapped column", which is at least three for the
    // engines that reach here at all.
    // A FIXED third column only. maximum 0 means "as many as are mapped", and
    // those columns are peers - a radar's spokes, parallel coordinates' axes -
    // so there is no third axis there to scale on its own.
    if(!plan.asSeries||plan.maximum<3) return QString();
    // Height, or the quantity the colour runs over. usesColourMap answers for
    // the PREPARED engine, so a Spectrogram - a heat map by the time anything
    // is drawn - gets the right word.
    if(spec_.engine.startsWith(QLatin1String("3D "))
       ||spec_.engine==QLatin1String("Surface + Contours")
       ||spec_.engine==QLatin1String("Ribbon")
       ||spec_.engine==QLatin1String("Comet 3D"))
        return QStringLiteral("Z scale");
    if(QtPlotBackend::usesColourMap(spec_.engine)) return QStringLiteral("Colour scale");
    return QStringLiteral("3rd column");
}

void PlotCanvas::setLinkAxisTransforms(bool on){
    if(linkTransforms_==on) return;
    linkTransforms_=on;
    // Turning the link on adopts what x is doing, rather than resetting both to
    // linear: the person who ticks it has just set x to what they want.
    if(on&&spec_.yAxis.transform!=spec_.xAxis.transform) setXTransform(spec_.xAxis.transform);
    emit sourceChanged();
}

// Catalogue entries carry their axis-scale variant as a separate field, so
// "Line Chart" + "Semi-Log X" is one entry rather than a distinct engine.
// A catalogue entry's scale variant, applied to the two axes.
//
// This knew three names and wrote spec_.xAxis.log10 straight, which had two
// faults. It could not express anything but a log axis, so the whole of the
// catalogue's scale dimension was log or nothing; and it never wrote the linear
// case, so choosing a plain entry after a Semi-Log X one left the x axis
// logarithmic with nothing on screen saying why.
//
// Both axes are now always set, from one table, in terms of AxisTransform. The
// names here are the vocabulary the catalogue's `scale` field uses and the two
// cannot drift, because this is the only thing that reads it.
void PlotCanvas::applyVariant(){
    struct Variant { const char* name; int x; int y; };
    static const Variant kVariants[]={
        {"Linear Scale",            AxisLinear,   AxisLinear},
        {"Logarithmic Scale",       AxisLog10,    AxisLog10},
        {"Semi-Log X",              AxisLog10,    AxisLinear},
        {"Semi-Log Y",              AxisLinear,   AxisLog10},
        // log10(1 + x), for the columns a plain log axis cannot take: anything
        // that legitimately reaches zero.
        {"Log(1+x) Scale",          AxisLog1p,    AxisLog1p},
        {"Semi-Log(1+x) X",         AxisLog1p,    AxisLinear},
        {"Semi-Log(1+x) Y",         AxisLinear,   AxisLog1p},
        // Standard deviations from the mean, which is how two columns in
        // different units are compared on one pair of axes.
        {"Standardised (Z-Score)",  AxisZScore,   AxisZScore},
        {"Z-Score X",               AxisZScore,   AxisLinear},
        {"Z-Score Y",               AxisLinear,   AxisZScore},
        // Position in the sorted sample, which is how a column that is mostly
        // one value stops being one pixel.
        {"Quantile Scale",          AxisQuantile, AxisQuantile},
        {"Quantile X",              AxisQuantile, AxisLinear},
        {"Quantile Y",              AxisLinear,   AxisQuantile},
    };

    const QString v=spec_.variant;
    int x=AxisLinear, y=AxisLinear;
    bool matched=v.isEmpty();
    for(const Variant& k:kVariants){
        if(v.compare(QLatin1String(k.name),Qt::CaseInsensitive)!=0) continue;
        x=k.x; y=k.y; matched=true;
        break;
    }
    // An unrecognised variant leaves the axes alone rather than silently
    // flattening them to linear: a catalogue entry naming a scale this build
    // does not know is a catalogue that is ahead of the code, and quietly
    // drawing the wrong scale is worse than drawing the last one.
    if(!matched) return;

    spec_.xAxis.transform=x; spec_.xAxis.log10=(x==AxisLog10);
    spec_.yAxis.transform=y; spec_.yAxis.log10=(y==AxisLog10);
    emit stateChanged();
}

namespace {

// How many distinct finite values a column has, counted no further than `upTo`.
//
// Stopping early matters: this runs on every rebuild over columns of a couple
// of hundred thousand rows, and the questions asked of it are "is this
// constant" and "does this have more than a handful of levels". Neither needs
// the exact answer for a column with a hundred thousand of them.
int distinctValueCount(const QVector<double>& v,int upTo){
    QSet<double> seen;
    seen.reserve(qMin(upTo,64)*2);
    for(double d:v){
        if(d!=d) continue;                      // a gap is not a value
        seen.insert(d);
        if(seen.size()>=upTo) return upTo;
    }
    return seen.size();
}

// How well spread a column is: what fraction of its full range the middle 98%
// of its values actually occupy. 1.0 is a uniform sweep; near 0 means one or
// two outliers hold the range open while everything else shares a pixel.
//
// Sampled, not sorted whole. This is asked of every column of a 200,000-row
// file and only to rank them, so a few thousand evenly spaced values give the
// same ordering for a fraction of the work.
double columnSpread(const QVector<double>& v){
    constexpr int kSamples=4096;
    QVector<double> sample;
    sample.reserve(qMin(v.size(),qsizetype(kSamples)));
    const int stride=qMax(1,int(v.size()/kSamples));
    for(int i=0;i<v.size();i+=stride){
        const double d=v[i];
        if(std::isfinite(d)) sample.append(d);
    }
    if(sample.size()<16) return 0.0;
    std::sort(sample.begin(),sample.end());
    const double lo=sample.first(), hi=sample.last();
    const double span=hi-lo;
    if(!(span>0.0)||!std::isfinite(span)) return 0.0;
    const double p01=sample.at(int(sample.size()*0.01));
    const double p99=sample.at(qMin(sample.size()-1,qsizetype(sample.size()*0.99)));
    return qBound(0.0,(p99-p01)/span,1.0);
}

// The column to put on the x axis when the user has not chosen one.
//
// Two things make a column readable as an axis, and the old rule - "the first
// one" - tested neither:
//
//   How many DISTINCT values it has. A column with five levels across two
//   hundred thousand rows is a flag, and drawing against it stacks every point
//   onto five verticals. That is what happened: the first column of a
//   Latin-hypercube sweep was named "x", was a solver flag with five values and
//   one stray outlier at 200,000, and every engine drew every point into one
//   corner of an axis stretched across the whole range.
//
//   How well SPREAD it is. Cardinality alone is not enough - the fix that only
//   counted distinct values went straight from a five-level flag to a column
//   that is 97% zeros, which is degenerate in the same way for the same reason.
//
// The name is a tie-break only. "x", "time" and the rest are usually meant as
// an axis and are usually also well spread, so the tests agree except in
// exactly the case that broke.
QString chooseAxisColumn(const ArrowTable& table,const QStringList& columns){
    if(columns.isEmpty()) return QString();
    constexpr int kCeiling=512;
    static const QStringList kAxisNames{
        QStringLiteral("x"),QStringLiteral("time"),QStringLiteral("t"),
        QStringLiteral("index"),QStringLiteral("i"),QStringLiteral("step"),
        QStringLiteral("sample"),QStringLiteral("depth"),QStringLiteral("date"),
        QStringLiteral("timestamp"),QStringLiteral("wavelength"),QStringLiteral("frequency")};

    QString best;
    double bestScore=-1.0;
    bool bestNamed=false;
    for(const QString& name:columns){
        const QVector<double> v=table.column(name);
        const int distinct=distinctValueCount(v,kCeiling);
        if(distinct<2) continue;                       // constant: never an axis
        const double spread=columnSpread(v);
        // Cardinality carries the decision, spread vetoes the degenerate ones.
        // Both are normalised so neither can dominate on units alone.
        const double score=(double(distinct)/double(kCeiling))*qMax(0.05,spread);
        const bool named=kAxisNames.contains(name.trimmed().toLower());
        const bool better=(score>bestScore*1.0001)
                        ||(qFuzzyCompare(score+1.0,bestScore+1.0)&&named&&!bestNamed);
        if(better){ best=name; bestScore=score; bestNamed=named; }
    }
    // Everything was constant. The old rule, so the caller still gets a column
    // and the empty-plot message can explain the rest.
    return best.isEmpty()?columns.first():best;
}

} // namespace

void PlotCanvas::rebuild(){
    if(!dirty_) return;
    dirty_=false;
    // A zoom belongs to the figure it was made on. Keeping it across a change
    // of dataset, engine or mapped columns would open the next figure already
    // clipped to a range that means nothing in it - and with no visible reason
    // why, since the range came from a plot that is no longer on screen.
    // A STYLE change must not throw the zoom away.
    //
    // The rule below is right for a data change - a zoom belongs to the figure
    // it was made on - but rebuild() is also what re-derives the series
    // colours, so choosing a colour-vision palette ran it and silently reset a
    // zoom the person had dragged. The comment further down about limit*_
    // already noticed that "a style change - a colour map, a grid toggle - can
    // trigger one"; this is the other half of that observation.
    if(hasView_&&!keepViewOnRebuild_){
        hasView_=false;
        spec_.xAxis.min=unsetValue(); spec_.xAxis.max=unsetValue();
        spec_.yAxis.min=unsetValue(); spec_.yAxis.max=unsetValue();
    }
    keepViewOnRebuild_=false;
    spec_.series.clear();
    // Annotations are deliberately NOT cleared here, unlike the zoom above.
    // A zoom is a view of a figure and means nothing on the next one; a note is
    // something the person typed, and silently deleting typed text because the
    // engine changed is a worse failure than a note left pointing at a value
    // the new figure does not have. One that falls outside the range is clipped
    // away by drawAnnotations and returns if the range covers it again, so a
    // stale note is invisible rather than wrong. Clear Notes empties them.
    pointCount_=0;
    notice_.clear();           // recomputed at the end; a stale explanation of
                               // the LAST figure is worse than none
    usesColourMap_=false;      // recomputed at the end; cleared so the early
                               // returns below cannot leave the last figure's
                               // answer behind
    available_.clear();
    engineSupported_=qtBackend_.supports(spec_.engine);

    if(arrowPath_.isEmpty()){ message_=QStringLiteral("Import or select a dataset"); emit stateChanged(); return; }

    const ArrowTable& table=loadedTable();
    if(!table.isValid()){ message_=table.error(); emit stateChanged(); return; }
    available_=table.columnNames();

    if(!engineSupported_){
        message_=QStringLiteral("“%1” is in the catalogue but its engine is not ported yet").arg(spec_.engine);
        emit stateChanged(); return;
    }

    // Fall back to a sensible pair of columns so a freshly staged graph draws
    // something meaningful before the user has chosen a mapping.
    //
    // "The first column" was the old rule and it is a bad one. A 200,000-row
    // Latin-hypercube sweep arrived with its first column literally named "x" -
    // which looked like an obvious axis and was in fact a solver flag with FIVE
    // distinct values, 194,762 rows sharing one of them and a single stray row
    // holding 200,000. Every engine then drew every point into the bottom-left
    // corner of an axis stretched across 200,000, and the user reasonably
    // reported that no visualisation worked at all.
    //
    // So the x axis is chosen by what makes a readable axis - how many DISTINCT
    // values a column has - rather than by where it happens to sit in the file.
    // The name is worth something too, but only as a tie-break: a column called
    // "x" or "time" is usually meant as an axis, and is usually also
    // high-cardinality, so the two agree except in exactly the case that broke.
    QString xName=xColumn_;
    QStringList yNames=yColumns_;
    if(xName.isEmpty()&&!available_.isEmpty()) xName=chooseAxisColumn(table,available_);
    if(yNames.isEmpty()){
        // The first column that is not the x axis AND is not constant. A column
        // with one value draws a flat line whatever is on the other axis.
        for(const QString& c:std::as_const(available_)){
            if(c==xName) continue;
            if(distinctValueCount(table.column(c),3)<2) continue;
            yNames.append(c); break;
        }
        // Nothing varied: fall back to the old rule rather than refusing to
        // draw, so the picture and the message below agree about what happened.
        if(yNames.isEmpty())
            for(const QString& c:std::as_const(available_)){ if(c!=xName){ yNames.append(c); break; } }
    }
    if(yNames.isEmpty()){ message_=QStringLiteral("Need at least two numeric columns to plot"); emit stateChanged(); return; }

    // A multi-column engine reads its inputs as SERIES: series 0, 1 and 2 are a
    // heatmap's x, y and value, a network's from, to and weight, a 3-D
    // scatter's x, y and z. So for those the mapped roles all have to become
    // series, in the order the person set them, rather than only the Y one.
    //
    // This is what was broken. The workspace set yColumns to a single-element
    // list and dropped Z and Colour, so every engine needing a third column got
    // one series, returned before drawing anything, and left a frame with an
    // empty middle - while a Line Chart, which wants exactly one, worked fine.
    // "The line graph draws and nothing else does" was a precise description of
    // the bug.
    //
    // Ordinary series engines are untouched: they keep x on the x axis and one
    // series per Y column, which is what makes several Y columns draw several
    // lines.
    const QtPlotBackend::ColumnPlan plan=QtPlotBackend::columnPlan(spec_.engine);
    if(plan.asSeries){
        QStringList roles;
        roles.append(xName);
        for(const QString& c:std::as_const(yNames)) if(!c.isEmpty()) roles.append(c);
        if(!zColumn_.isEmpty()) roles.append(zColumn_);
        if(!colorColumn_.isEmpty()) roles.append(colorColumn_);
        roles.removeAll(QString());
        roles.removeDuplicates();

        // Top up from the file when the engine reads more columns than are
        // mapped. Without this, choosing a Cone Plot on a fresh dataset shows a
        // correct, empty, unexplained frame until the person has guessed that
        // it wants six columns and found the two extra slots - and a person who
        // has just clicked an entry in the graph library is entitled to see the
        // graph. The columns are the same ones chooseAxisColumn ranks, best
        // first, so the automatic choice is the useful one rather than whatever
        // happens to be leftmost in the file. Anything mapped by hand is kept
        // and stays in its slot; this only fills the empty ones.
        for(const QString& c:std::as_const(available_)){
            if(roles.size()>=plan.minimum) break;
            if(roles.contains(c)) continue;
            if(distinctValueCount(table.column(c),3)<2) continue;
            roles.append(c);
        }

        // Only as many as the engine reads. A fourth column handed to a
        // three-column engine is a series it will ignore, drawn in the legend
        // and counted in the point total - which reads as a bug of its own.
        // maximum == 0 is the exception and it means the opposite: a
        // correlation matrix, parallel coordinates and a radar chart get WIDER
        // with every column, so trimming those to a fixed count would throw
        // away the figure the person asked for.
        if(plan.maximum>0)
            while(roles.size()>plan.maximum) roles.removeLast();

        // Whatever the person mapped, up to what the engine reads - not only
        // the exact count. A Mosaic Plot works from two columns and uses a
        // third for counts if it has one; requiring all three before composing
        // anything would leave it with a single series and nothing drawn, which
        // is the bug this whole block exists to fix. Each engine's own guard
        // decides whether what it got is enough, and explainEmpty says so when
        // it is not.
        if(roles.size()>=2) yNames=roles;
    }

    // THE RIGHT-HAND AXIS COLUMN, if one was chosen and this engine draws the
    // mapped columns as series at all. It joins the Y list rather than
    // replacing anything: "pressure on the right" means pressure is also
    // plotted, and dropping it from yNames would leave a labelled second axis
    // with nothing drawn against it.
    //
    // A column already mapped to Y is not appended twice - it just moves to the
    // other axis, which is the sensible reading of choosing it in both places.
    QString y2Name;
    if(!plan.asSeries&&!y2Column_.isEmpty()&&table.hasColumn(y2Column_)
       &&y2Column_!=xName&&supportsSecondaryAxis()){
        y2Name=y2Column_;
        if(!yNames.contains(y2Name)) yNames.append(y2Name);
    }

    // The window the budget is spent in. Read from the STORED limits rather
    // than from the axes, because the block at the top of this function has
    // just cleared the axes and applyStoredLimits, at the bottom, has not put
    // them back yet - these are the same numbers it is about to install.
    //
    // Only for engines that read x as an axis. See ViewWindow: an engine whose
    // mapped columns become series 0, 1 and 2 grids them, and the grid takes
    // its extent from the data it is handed, so windowing one would re-bin the
    // field and change its cell size every time the person zoomed.
    const ViewWindow window=windowForDrawing(plan.asSeries);
    resolvedAsSeries_=plan.asSeries;

    pointCount_=buildPlotSeries(table,xName,yNames,colourVision_,
                                interactiveBudgetFor(int(yNames.size())),spec_,
                                xUnit_,yUnit_,window,&fullPointCount_,y2Name);

    // fullPointCount_ is how many points there would have been without the
    // budget, IN THE WINDOW. The difference between it and pointCount_ is the
    // whole reason the full-resolution render exists; when they match, the
    // preview already IS the finished picture and no worker is started at all -
    // which is now what happens as soon as a zoom leaves fewer rows on screen
    // than the budget, so a zoomed figure is final the moment it is drawn.
    resolvedX_=xName;
    resolvedY2_=y2Name;
    resolvedY_=yNames;

    // "Pressure [kPa]" reads as "Pressure (kPa)", and a converted axis says the
    // unit it is actually drawn in rather than the one the file was written in.
    spec_.xAxis.label=xUnit_.isEmpty()
        ? Units::axisLabel(xName)
        : Units::replaceUnit(Units::displayName(xName),xUnit_);
    spec_.yAxis.label=yNames.size()==1
        ? (yUnit_.isEmpty() ? Units::axisLabel(yNames.first())
                            : Units::replaceUnit(Units::displayName(yNames.first()),yUnit_))
        : QStringLiteral("value");
    if(spec_.title.isEmpty()) spec_.title=spec_.engine;
    // What the status line says, and it has to be able to say "nothing was
    // drawn, and here is why". "2D Heatmap - 20000 points" over an empty plot
    // area is true and useless: the columns ARE mapped and the points ARE
    // there, and the engine wanted a third column it never got.
    // The status stays short and always says the same kind of thing, so the
    // pill it goes in never changes size. Anything that needs a sentence goes
    // to the notice below the figure instead.
    if(spec_.series.isEmpty()){
        message_=QStringLiteral("%1 · no data").arg(spec_.engine);
        notice_=QStringLiteral("The selected columns have no numeric data in them.");
    }else{
        message_=QStringLiteral("%1 · %2 points").arg(spec_.engine).arg(pointCount_);
        notice_=QtPlotBackend::explainEmpty(spec_,qtBackend_.preparedFor(spec_));
    }
    applyVariant();
    // Which control the interface should offer, decided from the engine that
    // will actually be drawn rather than the one that was chosen. prepareSpec
    // is cached on the spec's fingerprint, so this is a lookup, not a second
    // preparation.
    usesColourMap_=QtPlotBackend::usesColourMap(qtBackend_.preparedFor(spec_).engine);
    // Whether the point budget may follow the view on this engine. Recorded
    // here, from the figure that was just built, and read by the NEXT
    // windowForDrawing - see the note there.
    windowUsable_.insert(spec_.engine+QLatin1Char('\x1f')+spec_.variant,
                         xAxisIsMappedColumn());
    // What each role spans, and then the person's own limits back on top.
    //
    // In this order because applyStoredLimits drops a limit whose role the new
    // figure does not read, and it can only know that once the spans have been
    // measured. Both AFTER the series are composed and before anything is
    // drawn, so the first paint of a rebuilt figure is already at the range
    // that was asked for rather than snapping to it a frame later.
    measureRoleSpans(table);
    applyStoredLimits();
    // Whether this figure can be grabbed at all depends on the engine and on
    // there being series to move, and both are only settled here - so the
    // pointer is told after the figure is built rather than when the engine
    // name changed. Switching from a Pie to a Line Chart has to take the hand
    // back off and put it back on.
    refreshCursor();
    emit axisRangesChanged();
    emit stateChanged();
    scheduleFullRender();
}

namespace {
// Forces full quality for the length of an export, whatever the screen is
// doing, and puts the flag back afterwards.
//
// This exists because the canvas shares ONE backend between the live preview
// and every export - deliberately, so that what is exported cannot disagree
// with what is on screen about anything but resolution. Draft mode is set on
// that shared backend by paint(), and it stays set for 180 ms after the last
// wheel notch or drag. An export starting inside that window would have been
// written with antialiasing off and its dense series thinned: a PDF for a
// paper, drawn at preview quality, with nothing on screen to say so.
//
// It was reachable rather than theoretical - a keyboard shortcut or a scripted
// export lands well inside 180 ms - and the commit that introduced draft mode
// claimed outright that nothing leaving the application is ever drawn in it.
// This is what makes that true instead of likely.
class FullQuality {
public:
    explicit FullQuality(QtPlotBackend& backend)
        : backend_(backend), was_(backend.draft()) { backend_.setDraft(false); }
    ~FullQuality(){ backend_.setDraft(was_); }
private:
    QtPlotBackend& backend_;
    bool was_;
};
} // namespace

// SHOWING THE FIGURE AS THE READER IT IS FOR ACTUALLY SEES IT.
//
// Everything else about colour vision is a claim: the palette is measured, the
// colour map is substituted, the panel explains itself - and the person setting
// it has normal colour vision, sees a figure that looks much as it did, and
// reasonably concludes the setting does nothing. That was the report, three
// times, and the first two answers were real bugs while the third was not.
//
// Simulation converts the claim into something you can look at. It is the same
// Machado, Oliveira & Fernandes (2009) transform the colour maps were measured
// with, applied to the finished pixels, so what appears is the figure as a
// protanope, deuteranope or tritanope receives it - two mustard lines that
// looked distinct a moment ago, sitting on top of each other.
//
// A VIEW, never an export. The figure on disk must be the real one: an exported
// PDF drawn through a dichromat transform is not a colour-blind-safe figure, it
// is a figure nobody can read properly. So this lives in paint() and nowhere
// near exportPdf, exportRaster or the full-resolution render that feeds them.
namespace {
// Severity 1.0, in linear sRGB. Rows of the 3x3, flattened.
// The matrices live in ColourVision.h now, beside the palettes they were
// measured with and beside colourMapWarning, which simulates single colours
// through the same transform. Two copies would be two ideas of what a protanope
// sees - and the map-safety verdicts would drift from the preview that is
// supposed to demonstrate them.

// Rec.709 luma, which is what a monochrome reader is left with.
void simulateInPlace(QImage& img,int mode){
    const double* m=nullptr;
    switch(mode){
    case 1: m=detail::visionMatrix(ColourVision::Protanopia); break;
    case 2: m=detail::visionMatrix(ColourVision::Deuteranopia); break;
    case 3: m=detail::visionMatrix(ColourVision::Tritanopia); break;
    case 4: break;                 // monochrome, handled below
    default: return;
    }
    // Built once rather than per pixel: the transform is in LINEAR light, so
    // every channel needs a gamma round trip and 8-bit inputs only have 256
    // possible values.
    //
    // A DATA RACE, AND A REACHABLE ONE. This was
    //
    //     static double toLinear[256];
    //     static bool ready=false;
    //     if(!ready){ ...fill the table...; ready=true; }
    //
    // and two threads do get here. `paintSimulated` is called from
    // PlotCanvas::paint, which Qt Quick runs on the SCENE-GRAPH RENDER THREAD,
    // and from PlotCanvas::renderTo, which the export path runs on the GUI
    // thread. Both entering for the first time at once is an unsynchronised
    // write to the same array by two threads, which is undefined behaviour
    // whatever the values happen to be.
    //
    // It is not saved by both writing the same numbers. The store to `ready`
    // carries no release ordering, so the other thread is allowed to observe
    // ready == true while the table is still the zero-initialised array it
    // started as - and a frame drawn from that table is black.
    //
    // A function-local static initialised by a lambda is the fix the language
    // already provides: C++11 guarantees it is initialised exactly once, and
    // that every thread reaching it either performs that initialisation or
    // waits for it and then sees the finished object.
    static const std::array<double,256> toLinear=[]{
        std::array<double,256> table{};
        for(int i=0;i<256;++i){
            const double c=i/255.0;
            table[i]=c<=0.04045?c/12.92:std::pow((c+0.055)/1.055,2.4);
        }
        return table;
    }();
    const auto toSrgb=[](double c){
        c=qBound(0.0,c,1.0);
        const double v=c<=0.0031308?12.92*c:1.055*std::pow(c,1.0/2.4)-0.055;
        return int(qBound(0.0,v*255.0+0.5,255.0));
    };
    if(img.format()!=QImage::Format_ARGB32&&img.format()!=QImage::Format_RGB32)
        img=img.convertToFormat(QImage::Format_ARGB32);
    for(int y=0;y<img.height();++y){
        QRgb* row=reinterpret_cast<QRgb*>(img.scanLine(y));
        for(int x=0;x<img.width();++x){
            const QRgb p=row[x];
            const double r=toLinear[qRed(p)],g=toLinear[qGreen(p)],b=toLinear[qBlue(p)];
            if(!m){
                const int y709=toSrgb(0.2126*r+0.7152*g+0.0722*b);
                row[x]=qRgba(y709,y709,y709,qAlpha(p));
                continue;
            }
            row[x]=qRgba(toSrgb(m[0]*r+m[1]*g+m[2]*b),
                         toSrgb(m[3]*r+m[4]*g+m[5]*b),
                         toSrgb(m[6]*r+m[7]*g+m[8]*b),qAlpha(p));
        }
    }
}
} // namespace

void PlotCanvas::setColourVisionPreview(bool on){
    if(colourVisionPreview_==on) return;
    colourVisionPreview_=on;
    // A repaint, not a rebuild: the figure is unchanged and only the pixels
    // shown are passed through the transform.
    update();
    emit styleChanged();
}

bool PlotCanvas::simulatingColourVision() const {
    return colourVisionPreview_&&colourVision_>0;
}

void PlotCanvas::paintSimulated(QPainter* painter,const QRectF& target,
                                const QImage& source) const {
    QImage seen=source;
    simulateInPlace(seen,colourVision_);
    painter->drawImage(target,seen,QRectF(QPointF(0,0),QSizeF(seen.size())));
}

void PlotCanvas::renderTo(QPainter* painter,const QRectF& target){
    // Every backend selection still exports through Qt: a rasterising backend
    // cannot emit vectors. See docs/PORT-PLAN.md, rule 2.
    if(simulatingColourVision()){
        // Drawn into an image first, because the transform needs finished
        // pixels. Only on the preview path - see the note above about exports.
        const qreal dpr=window()?window()->effectiveDevicePixelRatio():1.0;
        QImage buffer(QSize(qMax(1,int(target.width()*dpr)),
                            qMax(1,int(target.height()*dpr))),
                      QImage::Format_ARGB32);
        buffer.setDevicePixelRatio(dpr);
        buffer.fill(spec_.style.background);
        QPainter into(&buffer);
        qtBackend_.render(&into,QRectF(0,0,target.width(),target.height()),spec_);
        into.end();
        paintSimulated(painter,target,buffer);
        return;
    }
    qtBackend_.render(painter,target,spec_);
}

// ======================================================================
// Pan and zoom
//
// The view is written straight onto spec_.xAxis/yAxis min and max. Those
// fields existed, the backend already honoured them ahead of the fitted range,
// and nothing in the application had ever set them - so a zoom needs no second
// notion of "what is being shown" that the exports and the full-resolution
// render could then disagree with. What is on screen is what is exported,
// which is the rule this canvas is built on.
//
// The view is kept in the space the axis is DRAWN in - log10 when the axis is
// logarithmic - because that is the space a zoom should be uniform in. It is
// converted back on the way onto the axis, which stores real data values.
// ======================================================================

bool PlotCanvas::viewInteractive() const {
    // The same question render() asks. A pie, a radar, a treemap and the 3-D
    // projections have no axis range to change, and offering to drag them
    // would be an affordance that does nothing.
    return QtPlotBackend::engineHasAxes(spec_.engine)&&!spec_.series.isEmpty();
}

QRectF PlotCanvas::interactionArea() const {
    // Where the figure was actually drawn. Falling back to the whole item is
    // only wrong by the margins, and only until the first paint.
    const QRectF drawn=qtBackend_.lastPlotArea();
    if(drawn.width()>1.0&&drawn.height()>1.0) return drawn;
    return QRectF(0,0,qMax(1.0,width()),qMax(1.0,height()));
}

void PlotCanvas::ensureView(){
    if(hasView_) return;
    const QtPlotBackend::DataRange r=qtBackend_.rangeFor(spec_);
    const auto usable=[](double lo,double hi){ return lo==lo&&hi==hi&&hi>lo; };
    if(!usable(r.xLo,r.xHi)||!usable(r.yLo,r.yHi)) return;
    // Straight onto the axes, in real data values.
    spec_.xAxis.min=r.xLog?std::pow(10.0,r.xLo):r.xLo;
    spec_.xAxis.max=r.xLog?std::pow(10.0,r.xHi):r.xHi;
    spec_.yAxis.min=r.yLog?std::pow(10.0,r.yLo):r.yLo;
    spec_.yAxis.max=r.yLog?std::pow(10.0,r.yHi):r.yHi;
    hasView_=true;
}

namespace {
// The view in drawn space, and back again. Two conversions in one place so a
// log axis cannot be handled one way going in and another coming out.
struct AxisView { double lo=0,hi=1; bool log=false; };
AxisView viewOf(const PlotAxis& axis){
    AxisView v; v.log=axis.log10;
    const double lo=isUnset(axis.min)?0.0:axis.min;
    const double hi=isUnset(axis.max)?1.0:axis.max;
    v.lo=v.log?std::log10(qMax(1e-300,lo)):lo;
    v.hi=v.log?std::log10(qMax(1e-300,hi)):hi;
    return v;
}
void writeBack(PlotAxis& axis,const AxisView& v){
    axis.min=v.log?std::pow(10.0,v.lo):v.lo;
    axis.max=v.log?std::pow(10.0,v.hi):v.hi;
}
} // namespace

// THE FIGURE LAGGING BEHIND THE WHEEL.
//
// Reported as "when zooming it shows the pre-zoom picture and goes back to
// normal after zooming". It was not showing the wrong range: it was showing an
// EARLIER one. The two screenshots were x 50..350 and x 80..240, and those are
// the same zoom about the same anchor - solve for it and both give x ~ 114 -
// so the first was a frame from partway through the gesture, still on screen
// when the gesture had already moved on.
//
// Measured on 300,000 points: one preview repaint at full quality is 110 ms,
// falling to 21 ms as the zoom narrows - about 17 frames a second across ten
// notches. A wheel delivers notches far faster than that, so the displayed
// frame runs several notches behind and only catches up when the wheel stops,
// which is exactly the "goes back to normal afterwards" half of the report.
//
// Draft mode exists for precisely this, and the 2-D view never entered it.
// paint() sets draft from `dragging_||interacting_||resizing_`, and
// `interacting_` is set in ONE place - cameraMoved(), which is the 3-D camera.
// So DRAGGING a 2-D figure was draft, because the press sets dragging_, while a
// wheel zoom, a +/- button and a touch pan - none of which has a press or a
// release around it - all painted at full quality on every step.
//
// cameraMoved's own comment describes this fault being found and fixed for the
// 3-D camera: "a wheel zoom fires this once per notch with no press or release
// around it, so the old `if(!dragging_)` started - and abandoned - a full
// render on every notch." The same sentence was true of the 2-D view one
// screen further down, and nobody carried it across. This is the recurring root
// cause in its mirror image: not a rule right for one class applied to all, but
// a fix right for every class applied to one.
//
// So a gesture now says that it is one. Draft is on for its duration, the
// full-resolution render is asked for ONCE when it stops rather than started
// and abandoned on every notch, and the idle timer that already ends a 3-D
// gesture ends this one too.
void PlotCanvas::commitView(View how){
    // A DRAGGED range is a typed range that was not typed: the two are the same
    // state, so a pan or a pinch updates the stored copy and the fields in the
    // panel move with the figure. Without this, dragging and then changing a
    // colour map would put the range back to whatever was last typed.
    for(int r=0;r<2;++r){
        const PlotAxis* axis=(r==0)?&spec_.xAxis:&spec_.yAxis;
        limitLoSet_[r]=!isUnset(axis->min);
        limitHiSet_[r]=!isUnset(axis->max);
        if(limitLoSet_[r]) limitLo_[r]=axis->min;
        if(limitHiSet_[r]) limitHi_[r]=axis->max;
    }
    emit axisRangesChanged();

    // Said BEFORE the rebuild, because scheduleFullRender reads it: no
    // full-resolution render is started while a gesture is in progress, and
    // rebuild() ends by asking for one.
    if(how==View::Gesture){
        interacting_=true;
        interactionIdle_.start();
    }

    // The series themselves, rebuilt for the new window - EITHER WAY.
    //
    // This is what spends the point budget where the person is now looking,
    // see the note above buildPlotSeries, and it has to happen on the settled
    // path too: Reset view widens the range back to the whole column, and
    // without a rebuild the figure would go on being drawn from the few
    // thousand rows the zoom had narrowed it to - a reset that did not reset.
    // My own check caught that one.
    //
    // Affordable once per wheel notch only because loadedTable() no longer
    // re-reads the Arrow file to do it.
    dirty_=true;
    rebuild();
    update();
    emit stateChanged();
}

void PlotCanvas::panByPixels(double dx,double dy){
    if(!viewInteractive()) return;
    ensureView();
    if(!hasView_) return;
    const QRectF area=interactionArea();
    AxisView vx=viewOf(spec_.xAxis),vy=viewOf(spec_.yAxis);
    const double spanX=vx.hi-vx.lo,spanY=vy.hi-vy.lo;
    if(!(spanX>0)||!(spanY>0)) return;
    const double stepX=dx*spanX/area.width();
    const double stepY=dy*spanY/area.height();
    vx.lo-=stepX; vx.hi-=stepX;
    // Dragging the figure down should move the data down with the finger, and
    // y grows upwards on screen.
    vy.lo+=stepY; vy.hi+=stepY;
    writeBack(spec_.xAxis,vx); writeBack(spec_.yAxis,vy);
    // A gesture. A mouse pan already had draft through dragging_, but a TOUCH
    // pan arrives here with no press or release around it and had the same
    // fault the wheel did.
    commitView(View::Gesture);
}

// Can this figure be moved inside its frame?
//
// The three modes are exclusive by construction and this is the third: an axis
// range to slide (viewInteractive), a camera to turn (view3D), or the drawing
// itself to move. Written as "neither of the other two" rather than as a list
// of engine names, for the reason the windowed-budget work settled - a
// hand-maintained list of which of 440 engines a rule applies to is stale on
// the next commit. An engine added tomorrow with no axes and no camera gets
// this without anyone remembering to add it.
//
// The empty-series test is the one viewInteractive already makes: there is no
// sense offering to move a figure with nothing drawn in it.
bool PlotCanvas::frameInteractive() const {
    return !viewInteractive()&&!view3D()&&!spec_.series.isEmpty();
}

void PlotCanvas::panFrameByPixels(double dx,double dy){
    if(!frameInteractive()) return;
    const QRectF area=interactionArea();
    if(!(area.width()>0.0)||!(area.height()>0.0)) return;
    // The figure follows the pointer exactly - a drag of forty pixels moves it
    // forty pixels - at every magnification. That is what dividing the pixel
    // delta by the frame and NOT by the zoom gives: the pan is applied outside
    // the scale in applyFrameView, so it is already in frame units.
    spec_.frameView.panX+=dx/area.width();
    spec_.frameView.panY+=dy/area.height();
    emit stateChanged();
    commitView(View::Gesture);
}

void PlotCanvas::zoomFrameAt(const QPointF& pos,double factor){
    if(!frameInteractive()) return;
    if(!(factor>0.0)||!std::isfinite(factor)) return;
    const QRectF area=interactionArea();
    if(!(area.width()>0.0)||!(area.height()>0.0)) return;

    PlotFrameView v=spec_.frameView;
    // A floor and a ceiling, both reachable. Below 1 the figure shrinks inside
    // its frame, which is worth having - it is how you see a network graph
    // whose layout has run past the edges - and 0.2 is far enough out. Past 40
    // the clip rectangle holds a few glyphs and there is nothing more to see.
    const double next=qBound(0.2,v.zoom*factor,40.0);
    if(next==v.zoom) return;

    // ABOUT THE POINTER. applyFrameView scales about the frame's centre and is
    // kept a pure transform, so that the export and the preview cannot
    // disagree about where the figure sits; keeping a point still is therefore
    // expressed here, as the pan that achieves it.
    //
    // px,py is the pointer relative to the centre as a fraction of the frame.
    // The figure point under it is (p - pan)/zoom, and holding that still
    // across the change of zoom gives the new pan directly.
    const double px=(pos.x()-area.center().x())/area.width();
    const double py=(pos.y()-area.center().y())/area.height();
    const double figureX=(px-v.panX)/v.zoom;
    const double figureY=(py-v.panY)/v.zoom;
    v.panX=px-figureX*next;
    v.panY=py-figureY*next;
    v.zoom=next;

    if(!std::isfinite(v.panX)||!std::isfinite(v.panY)) return;
    spec_.frameView=v;
    emit stateChanged();
    commitView(View::Gesture);
}

void PlotCanvas::zoomAt(const QPointF& pos,double factor){
    if(!viewInteractive()) return;
    ensureView();
    if(!hasView_) return;
    if(!(factor>0)||!(factor==factor)) return;
    const QRectF area=interactionArea();
    AxisView vx=viewOf(spec_.xAxis),vy=viewOf(spec_.yAxis);
    const double spanX=vx.hi-vx.lo,spanY=vy.hi-vy.lo;
    if(!(spanX>0)||!(spanY>0)) return;

    // Where the pointer sits inside the drawn area, 0..1. Clamped rather than
    // rejected, so a wheel event that lands on the axis labels still zooms
    // about the nearest edge instead of doing nothing.
    const double tx=qBound(0.0,(pos.x()-area.left())/area.width(),1.0);
    const double ty=qBound(0.0,(area.bottom()-pos.y())/area.height(),1.0);
    const double anchorX=vx.lo+tx*spanX;
    const double anchorY=vy.lo+ty*spanY;

    // A span that reaches zero can never be zoomed back out, and one that
    // reaches infinity stops being drawable. Both are refused rather than
    // clamped silently to a range the user did not ask for.
    const double newSpanX=spanX/factor,newSpanY=spanY/factor;
    if(!(newSpanX>1e-12)||!(newSpanY>1e-12)) return;
    if(!std::isfinite(newSpanX)||!std::isfinite(newSpanY)) return;
    if(newSpanX>1e12*qMax(1.0,std::abs(anchorX))) return;
    if(newSpanY>1e12*qMax(1.0,std::abs(anchorY))) return;

    vx.lo=anchorX-tx*newSpanX; vx.hi=vx.lo+newSpanX;
    vy.lo=anchorY-ty*newSpanY; vy.hi=vy.lo+newSpanY;
    writeBack(spec_.xAxis,vx); writeBack(spec_.yAxis,vy);
    // Every 2-D zoom arrives here - the wheel, the +/- buttons through zoomBy,
    // and a two-finger pinch - and not one of them has a press or a release
    // around it. See the note on commitView.
    commitView(View::Gesture);
}

// The range on screen, read rather than written. When the user has zoomed, the
// axes carry it; when they have not, it comes from the same rangeFor() the
// renderer used, so the readout agrees with the picture in both cases.
bool PlotCanvas::currentView(double& xLo,double& xHi,bool& xLog,
                             double& yLo,double& yHi,bool& yLog) const {
    const auto usable=[](double lo,double hi){ return lo==lo&&hi==hi&&hi>lo; };
    if(hasView_){
        const AxisView vx=viewOf(spec_.xAxis), vy=viewOf(spec_.yAxis);
        xLo=vx.lo; xHi=vx.hi; xLog=vx.log;
        yLo=vy.lo; yHi=vy.hi; yLog=vy.log;
        return usable(xLo,xHi)&&usable(yLo,yHi);
    }
    const QtPlotBackend::DataRange r=qtBackend_.rangeFor(spec_);
    xLo=r.xLo; xHi=r.xHi; xLog=r.xLog;
    yLo=r.yLo; yHi=r.yHi; yLog=r.yLog;
    return usable(xLo,xHi)&&usable(yLo,yHi);
}

void PlotCanvas::updateCursor(const QPointF& pos){
    const bool wasOn=cursorOnPlot_;
    const QString wasText=cursorText_;
    cursorOnPlot_=false;

    // Only for the engines drawn in a rectangular frame. On a pie, a treemap
    // or a 3-D projection a pixel does not correspond to an (x, y) at all, and
    // a readout there would be a confident fabrication.
    double xLo,xHi,yLo,yHi; bool xLog,yLog;
    const QRectF area=interactionArea();
    if(viewInteractive()&&spec_.series.size()>0
       &&area.width()>1.0&&area.height()>1.0
       &&area.contains(pos)
       &&currentView(xLo,xHi,xLog,yLo,yHi,yLog)){
        const double tx=(pos.x()-area.left())/area.width();
        const double ty=(area.bottom()-pos.y())/area.height();
        const double vx=xLo+tx*(xHi-xLo);
        const double vy=yLo+ty*(yHi-yLo);
        cursorX_=xLog?std::pow(10.0,vx):vx;
        cursorY_=yLog?std::pow(10.0,vy):vy;
        if(std::isfinite(cursorX_)&&std::isfinite(cursorY_)){
            cursorOnPlot_=true;
            // Remembered for the zoom buttons. See lastPointerOnPlot_.
            lastPointerOnPlot_=pos;
            haveLastPointer_=true;
            // Significant figures from the SPAN on screen, not from the value:
            // at full extent four figures is noise, and zoomed a thousandfold
            // into a transient four figures is the entire reason for zooming.
            // One more digit than the span needs, so the last one moves.
            const auto digitsFor=[](double lo,double hi,bool log,double value){
                const double span=log?(std::pow(10.0,hi)-std::pow(10.0,lo)):(hi-lo);
                if(!(span>0.0)||!std::isfinite(span)) return 6;
                const double magnitude=qMax(std::abs(value),std::abs(span));
                const double decades=std::log10(magnitude/span);
                return int(qBound(1.0,std::ceil(decades)+3.0,15.0));
            };
            const auto axisName=[](const PlotAxis& a,const char* fallback){
                return a.label.isEmpty()?QString::fromLatin1(fallback):a.label;
            };
            cursorText_=QStringLiteral("%1 = %2   %3 = %4")
                .arg(axisName(spec_.xAxis,"x"),
                     QString::number(cursorX_,'g',digitsFor(xLo,xHi,xLog,cursorX_)),
                     axisName(spec_.yAxis,"y"),
                     QString::number(cursorY_,'g',digitsFor(yLo,yHi,yLog,cursorY_)));
        }
    }
    if(!cursorOnPlot_) cursorText_.clear();
    if(cursorOnPlot_!=wasOn||cursorText_!=wasText) emit cursorChanged();
}

void PlotCanvas::hoverMoveEvent(QHoverEvent* e){
    updateCursor(e->position());
    // Not accepted: hover is informational, and swallowing it would stop
    // anything layered over the figure from seeing the pointer.
    QQuickPaintedItem::hoverMoveEvent(e);
}

void PlotCanvas::hoverLeaveEvent(QHoverEvent* e){
    if(cursorOnPlot_){
        cursorOnPlot_=false;
        cursorText_.clear();
        emit cursorChanged();
    }
    QQuickPaintedItem::hoverLeaveEvent(e);
}

void PlotCanvas::zoomBy(double factor){
    // About the last point the pointer was over, not the middle of the frame.
    //
    // Zooming about the centre means the feature somebody is looking at slides
    // away from under them as they zoom in on it, and they have to pan it back
    // - every step, on every click. The wheel has always zoomed about the
    // pointer, through zoomAt; the buttons could not, because a button has no
    // pointer position of its own. So the canvas keeps the last one that was
    // over the figure, which is where the person was looking immediately
    // before they reached for the button.
    //
    // Falls back to the centre when the pointer has never been on the figure -
    // a keyboard, or a fresh window - which is the old behaviour and the only
    // sensible answer when nothing is known.
    const QRectF area=interactionArea();
    const QPointF about=(haveLastPointer_&&area.contains(lastPointerOnPlot_))
                            ? lastPointerOnPlot_ : area.center();
    // Routed the same way the wheel is. The + and - buttons are the keyboard
    // and trackpad-less path to the same gesture, and a figure that zooms with
    // the wheel and not with the button beside it is the kind of gap nobody
    // reports because they assume they have misunderstood the button.
    if(frameInteractive()) zoomFrameAt(about,factor);
    else zoomAt(about,factor);
}

void PlotCanvas::resetView(){
    // The in-frame view first, and OUTSIDE the hasView_ guard.
    //
    // hasView_ is about an axis range, and an axis-less figure never sets it -
    // so the early return below would have made Reset view do nothing on
    // exactly the engines the in-frame view exists for. A reset that silently
    // does nothing on a figure you have plainly moved is worse than no reset.
    if(spec_.frameView.active()){
        spec_.frameView=PlotFrameView{};
        emit stateChanged();
        commitView();
    }
    if(!hasView_) return;
    hasView_=false;
    // Back to knowing nothing about where the person was looking, so the next
    // zoom from a reset figure is about the middle rather than about a corner
    // they happened to pass over three views ago.
    haveLastPointer_=false;
    spec_.xAxis.min=unsetValue(); spec_.xAxis.max=unsetValue();
    spec_.yAxis.min=unsetValue(); spec_.yAxis.max=unsetValue();
    // A TYPED limit is a view too, so Reset view clears it. The alternative -
    // resetting the drag but leaving the numbers - gives a button that
    // sometimes appears to do nothing, on exactly the figures where the person
    // set a range deliberately and has forgotten they did.
    spec_.zAxis.min=unsetValue(); spec_.zAxis.max=unsetValue();
    for(int r=0;r<3;++r) limitLoSet_[r]=limitHiSet_[r]=false;
    emit axisRangesChanged();
    commitView();
}

void PlotCanvas::mousePressEvent(QMouseEvent* e){
    // A 3-D figure is dragged to turn it rather than to slide an axis range,
    // so it accepts the press even though viewInteractive is false for it -
    // and a figure with neither axes nor a camera is dragged to move the
    // drawing inside its frame, which is the third case.
    if(!viewInteractive()&&!view3D()&&!frameInteractive()){ e->ignore(); return; }
    // While annotating, a click places a note instead of starting a pan. The
    // coordinates come from updateCursor, which is the same conversion the
    // readout uses - so the note lands exactly where the readout said it would,
    // and there is only one place for that arithmetic to be wrong.
    if(annotating_){
        // A press on an EXISTING note picks it up instead of placing a new one
        // beside it. Until this existed, a note could be added and the whole
        // lot cleared, and nothing in between: PlotCanvas has had
        // removeAnnotation and moveAnnotation all along, and no path in the
        // interface reached either of them.
        const int hit=annotationAt(e->position().x(),e->position().y());
        if(hit>=0){
            noteDrag_=hit;
            noteMoved_=false;
            notePress_=e->position();
            noteStart_=QPointF(spec_.annotations[hit].offsetX,
                               spec_.annotations[hit].offsetY);
            e->accept();
            return;
        }
        updateCursor(e->position());
        if(cursorOnPlot_) emit annotationRequested(cursorX_,cursorY_);
        e->accept();
        return;
    }
    dragging_=true;
    refreshCursor();
    lastPointer_=e->position();
    e->accept();
}

void PlotCanvas::mouseMoveEvent(QMouseEvent* e){
    if(noteDrag_>=0){
        const QPointF delta=e->position()-notePress_;
        // A few pixels of slack before a press becomes a drag, or every click
        // meant to open a note nudges it first - which on a careful hand is
        // invisible and on a trackpad is not.
        if(!noteMoved_&&std::hypot(delta.x(),delta.y())<4.0){ e->accept(); return; }
        noteMoved_=true;
        // Offsets are in typographic points, the same units drawAnnotations
        // scales by dpi/72 - so the note follows the pointer at any figure
        // resolution rather than drifting from it.
        const double toPoints=72.0/double(qMax(1,spec_.style.dpi));
        moveAnnotation(noteDrag_,noteStart_.x()+delta.x()*toPoints,
                                 noteStart_.y()+delta.y()*toPoints);
        e->accept();
        return;
    }
    if(!dragging_){ e->ignore(); return; }
    const QPointF delta=e->position()-lastPointer_;
    lastPointer_=e->position();
    if(view3D()) rotateByPixels(delta.x(),delta.y());
    else if(frameInteractive()) panFrameByPixels(delta.x(),delta.y());
    else panByPixels(delta.x(),delta.y());
    e->accept();
}

void PlotCanvas::mouseReleaseEvent(QMouseEvent* e){
    if(noteDrag_>=0){
        const int picked=noteDrag_;
        const bool moved=noteMoved_;
        noteDrag_=-1;
        noteMoved_=false;
        // A press that did not travel is a click, and a click on a note means
        // "open this one" - which is where deleting and re-wording it live.
        if(!moved) emit annotationPicked(picked);
        e->accept();
        return;
    }
    const bool was=dragging_;
    dragging_=false;
    refreshCursor();
    // The full-resolution render was held back for the whole drag - see
    // cameraMoved - so this is where it is asked for. The idle timer would
    // reach the same place a fifth of a second later; a release is a definite
    // end to the gesture and there is no reason to wait for it.
    if(was){ interactionIdle_.stop(); interacting_=false; update(); scheduleFullRender(); }
    e->accept();
}

void PlotCanvas::mouseDoubleClickEvent(QMouseEvent* e){
    if(view3D()) resetCamera();
    else resetView();
    e->accept();
}

void PlotCanvas::wheelEvent(QWheelEvent* e){
    if(!viewInteractive()&&!view3D()&&!frameInteractive()){ e->ignore(); return; }
    // The same gain as the 3-D viewport, so one notch feels the same in both.
    const double factor=std::pow(1.0015,double(e->angleDelta().y()));
    if(view3D()) zoom3DBy(factor,e->position());
    else if(frameInteractive()) zoomFrameAt(e->position(),factor);
    else zoomAt(e->position(),factor);
    e->accept();
}

void PlotCanvas::touchEvent(QTouchEvent* e){
    if(!viewInteractive()&&!view3D()&&!frameInteractive()){ e->ignore(); return; }
    if(e->type()==QEvent::TouchEnd||e->type()==QEvent::TouchCancel){
        touch_.reset(); e->accept(); return;
    }
    const TouchGesture::Step step=touch_.update(e->points(),e->type()==QEvent::TouchBegin);
    if(!step.usable){ e->accept(); return; }

    // The in-frame view takes the same gestures as the 2-D one below - one
    // finger drags, two pinch, a pinch that also moves does both - because to
    // the hand it is the same action on the same figure. Wiring only the mouse
    // would have given a treemap that moves with a trackpad drag and not with
    // a finger, on the same machine.
    if(frameInteractive()){
        if(step.fingers>=2&&step.scale!=1.0) zoomFrameAt(step.centre,step.scale);
        if(step.movement.x()!=0.0||step.movement.y()!=0.0)
            panFrameByPixels(step.movement.x(),step.movement.y());
        e->accept();
        return;
    }

    if(view3D()){
        // One finger turns it, two pinch to zoom. Deliberately NOT panning on
        // two fingers as the 2-D case does: the cube is fitted to the frame, so
        // there is nowhere to pan it to, and a figure that slides out of its
        // own frame under a pinch is a bug rather than a feature.
        if(step.fingers>=2&&step.scale!=1.0) zoom3DBy(step.scale,step.centre);
        else if(step.movement.x()!=0.0||step.movement.y()!=0.0)
            rotateByPixels(step.movement.x(),step.movement.y());
        e->accept();
        return;
    }

    if(step.fingers>=2&&step.scale!=1.0) zoomAt(step.centre,step.scale);
    // Both one finger and two pan, so a pinch that also moves does both -
    // which is what a pinch on a map does.
    if(step.movement.x()!=0.0||step.movement.y()!=0.0)
        panByPixels(step.movement.x(),step.movement.y());
    e->accept();
}

void PlotCanvas::geometryChange(const QRectF& newGeometry,const QRectF& oldGeometry){
    QQuickPaintedItem::geometryChange(newGeometry,oldGeometry);
    // Only the size matters; being moved does not change what was rendered.
    if(newGeometry.size()==oldGeometry.size()) return;
    if(newGeometry.width()<=0||newGeometry.height()<=0) return;
    // DRAFT WHILE THE SIZE IS STILL MOVING.
    //
    // `dragging_` and `interacting_` cover gestures on the canvas - a pan, a
    // pinch - and a splitter drag is neither, so a resize rendered every
    // intermediate size at full quality. On a figure that takes tens of
    // milliseconds to draw, that is the drag's whole budget spent redrawing
    // sizes nobody will ever look at.
    //
    // Same idle-timer shape as interactionIdle_, and for the same reason: a
    // splitter drag has no end event this item can see.
    resizing_=true;
    resizeIdle_.start();
    // Keeping the rendered image: see the note in scheduleFullRender. A resize
    // asks for a new render at the new size, it does not make the figure on
    // screen wrong.
    scheduleFullRender(Retain::RenderedImage);
}

void PlotCanvas::paint(QPainter* painter){
    rebuild();
    const QRectF target(0,0,width(),height());
    // Draft while the figure is being moved. This is the single most valuable
    // line in the interactive path: see QtPlotBackend::setDraft.
    qtBackend_.setDraft(dragging_||interacting_||resizing_);

    // The accepted full-resolution render, if there is one and it still matches
    // the current settings. Anything else falls through to the live preview.
    if(showingFull_&&!fullImage_.isNull()&&readyGeneration_==generation_){
        // The accepted render goes through the simulation too, or turning the
        // preview on would appear to do nothing on exactly the datasets big
        // enough to have a full render - which is the same shape of bug this
        // whole setting has been bitten by twice already.
        if(simulatingColourVision()){ paintSimulated(painter,target,fullImage_); return; }
        painter->drawImage(target,fullImage_,QRectF(QPointF(0,0),QSizeF(fullImage_.size())));
        return;
    }

    if(spec_.series.isEmpty()){
        painter->fillRect(target,spec_.style.background);
        painter->setPen(spec_.style.foreground);
        QFont f(spec_.style.fontFamily); f.setPointSizeF(11); painter->setFont(f);
        painter->drawText(target,Qt::AlignCenter,message_);
        return;
    }
    renderTo(painter,target);
}

PlotSpec PlotCanvas::specWithFullData() const {
    PlotSpec full=spec_;
    if(arrowPath_.isEmpty()) return full;                  // preview is all there is
    const ArrowTable& table=loadedTable();
    if(!table.isValid()) return full;
    PlotSpec candidate=spec_;
    if(buildPlotSeries(table,resolvedX_,resolvedY_,colourVision_,0,candidate,xUnit_,yUnit_,
                       ViewWindow(),nullptr,resolvedY2_)<=0)
        return full;
    return candidate;
}

QString PlotCanvas::xSourceUnit() const {
    return Units::extractUnit(resolvedX_.isEmpty()?xColumn_:resolvedX_);
}

QString PlotCanvas::ySourceUnit() const {
    const QStringList& names=resolvedY_.isEmpty()?yColumns_:resolvedY_;
    return names.isEmpty()?QString():Units::extractUnit(names.first());
}

QStringList PlotCanvas::unitOptions(const QString& column) const {
    QStringList out;
    const QVector<Units::Conversion> options=Units::conversionsFor(Units::extractUnit(column));
    out.reserve(options.size());
    for(const Units::Conversion& c:options) out.append(c.target);
    return out;
}

QVariantMap PlotCanvas::figureState() const {
    return QVariantMap{
        {QStringLiteral("engine"),spec_.engine},
        {QStringLiteral("variant"),spec_.variant},
        {QStringLiteral("title"),spec_.title},
        // The formula, for the engines that plot one. A figure whose whole
        // content is an expression has not been saved without it.
        {QStringLiteral("expression"),spec_.expression},
        {QStringLiteral("xColumn"),xColumn_},
        {QStringLiteral("yColumns"),QVariant(yColumns_)},
        // All four mapped roles, not the two that used to be written. See
        // applyFigureState.
        {QStringLiteral("zColumn"),zColumn_},
        {QStringLiteral("colorColumn"),colorColumn_},
        {QStringLiteral("y2Column"),y2Column_},
        {QStringLiteral("logX"),spec_.xAxis.log10},
        {QStringLiteral("logY"),spec_.yAxis.log10},
        {QStringLiteral("xUnit"),xUnit_},
        {QStringLiteral("yUnit"),yUnit_},
        {QStringLiteral("colourVision"),colourVision_},
        {QStringLiteral("background"),spec_.style.background.name(QColor::HexArgb)},
        {QStringLiteral("foreground"),spec_.style.foreground.name(QColor::HexArgb)},
        {QStringLiteral("grid"),spec_.style.gridColor.name(QColor::HexArgb)},
        // The CHOSEN map, not the painted one. Saving the painted map would
        // bake a colour-vision substitution into the figure permanently: reopen
        // it with the vision mode off and the substitute would have become the
        // selection, with no way left to tell it from a deliberate choice.
        {QStringLiteral("colourMap"),chosenColourMap_},
        {QStringLiteral("annotations"),annotations()},
        // The engine's constants. Written as a map of key to number, so a
        // figure saved with four masses reopens with them and a figure from an
        // engine that declares none carries an empty map rather than nothing.
        {QStringLiteral("engineParameters"),engineParameterValues()},
        // The camera. A 3-D figure IS its angle - the whole reason to turn one
        // is that the shape reads from where you put it - and a figure that
        // reopened at the default angle had thrown away the only thing the
        // person did to it. Written for every figure, ignored on reopening by
        // the engines that have no camera.
        {QStringLiteral("azimuth"),spec_.view3d.azimuth},
        {QStringLiteral("elevation"),spec_.view3d.elevation},
        {QStringLiteral("cameraZoom"),spec_.view3d.zoom},
        // The in-frame view, for the same reason and with the same history. A
        // treemap somebody magnified onto one branch before saving is a figure
        // ABOUT that branch; reopening it whole would throw away the only
        // thing they did to it, exactly as reopening a 3-D figure at the
        // default angle used to. Written for every figure and ignored on
        // reopening by the engines that have axes instead.
        {QStringLiteral("framePanX"),spec_.frameView.panX},
        {QStringLiteral("framePanY"),spec_.frameView.panY},
        {QStringLiteral("frameZoom"),spec_.frameView.zoom},
        // The zoom, when there is one. A figure saved while zoomed in reopens
        // showing what was on screen when it was saved; one saved fitted to
        // its data carries no limits at all and stays that way.
        {QStringLiteral("xMin"),isUnset(spec_.xAxis.min)?QVariant():QVariant(spec_.xAxis.min)},
        {QStringLiteral("xMax"),isUnset(spec_.xAxis.max)?QVariant():QVariant(spec_.xAxis.max)},
        {QStringLiteral("yMin"),isUnset(spec_.yAxis.min)?QVariant():QVariant(spec_.yAxis.min)},
        {QStringLiteral("yMax"),isUnset(spec_.yAxis.max)?QVariant():QVariant(spec_.yAxis.max)},
        // The third axis, which only the projected engines read - and the
        // colour scale, which is a reading decision as much as the map itself
        // is. A figure whose whole point is that VHPR is capped at ten has not
        // been saved if it reopens at a hundred and sixty.
        {QStringLiteral("zMin"),isUnset(spec_.zAxis.min)?QVariant():QVariant(spec_.zAxis.min)},
        {QStringLiteral("zMax"),isUnset(spec_.zAxis.max)?QVariant():QVariant(spec_.zAxis.max)},
        {QStringLiteral("colourMin"),isUnset(spec_.style.colourMin)
                                     ?QVariant():QVariant(spec_.style.colourMin)},
        {QStringLiteral("colourMax"),isUnset(spec_.style.colourMax)
                                     ?QVariant():QVariant(spec_.style.colourMax)},
        {QStringLiteral("colourLevels"),spec_.style.colourLevels},
        {QStringLiteral("colourDrop"),spec_.style.colourOutOfRangeDropped},
        // The chosen colours, as hex. A figure whose whole point is the palette
        // somebody built for it has not been saved if it reopens in viridis.
        {QStringLiteral("customColours"),customColours()},
        // WHICH WAY ROUND THE ANGLES GO, and how a pie names its slices.
        //
        // These two travel with the FIGURE and not with the application,
        // because a notebook can hold a wind rose and a polar scatter side by
        // side and they are read in opposite directions - the reason the
        // convention was made a setting at all. An application-wide value would
        // have made the second figure wrong whenever the first was right.
        {QStringLiteral("polarConvention"),spec_.style.polarConvention},
        // And what a legend does with a label too long for its box, which
        // belongs to the figure for the same reason: one figure's series are
        // named "control" and "treated", another's carry a fitted constant and
        // its units, and the answer that suits one spoils the other.
        //
        // Deliberately NOT in specFingerprint. That hashes the INPUT to
        // prepareSpec and nothing in prepareSpec reads this - the legend is
        // measured and drawn in drawLegend, at paint time. Hashing a draw-time
        // field there would throw away prepared figures for a change that
        // cannot alter them. What does have to happen is a repaint, which is
        // what setLegendLabels asks for.
        {QStringLiteral("legendLabels"),spec_.style.legendLabels},
        // How many lines the wireframe has. A figure-scoped setting for the
        // same reason as the two above: a notebook can hold a coarse mesh for
        // reading a shape and a fine one for reading a value.
        {QStringLiteral("meshDensity"),spec_.style.meshDensity},
        {QStringLiteral("pieLabels"),spec_.style.pieLabels},
    };
}

void PlotCanvas::applyFigureState(const QVariantMap& state){
    // Every field is optional. A figure written by an older build simply does
    // not carry the newer ones, and the canvas keeps what it already had rather
    // than resetting to a default the user never chose.
    const auto text=[&state](const char* key,const QString& fallback){
        const QVariant v=state.value(QString::fromLatin1(key));
        return v.isValid()?v.toString():fallback;
    };
    const auto flag=[&state](const char* key,bool fallback){
        const QVariant v=state.value(QString::fromLatin1(key));
        return v.isValid()?v.toBool():fallback;
    };

    spec_.engine=text("engine",spec_.engine);
    spec_.variant=text("variant",spec_.variant);
    spec_.title=text("title",spec_.title);
    spec_.expression=text("expression",spec_.expression);
    xColumn_=text("xColumn",xColumn_);
    if(state.contains(QStringLiteral("yColumns")))
        yColumns_=state.value(QStringLiteral("yColumns")).toStringList();
    // THE OTHER MAPPED ROLES, which were not saved at all.
    //
    // Only x and y were written out, so reopening a saved heat map, a 3-D
    // scatter or anything else reading a third column got the figure back with
    // its value column gone - and then filled the empty slot automatically, so
    // it drew a plausible picture of a column the person had not chosen. A
    // silently different figure is worse than an empty one.
    zColumn_=text("zColumn",zColumn_);
    colorColumn_=text("colorColumn",colorColumn_);
    y2Column_=text("y2Column",y2Column_);
    spec_.xAxis.log10=flag("logX",spec_.xAxis.log10);
    spec_.yAxis.log10=flag("logY",spec_.yAxis.log10);
    xUnit_=text("xUnit",xUnit_);
    yUnit_=text("yUnit",yUnit_);
    if(state.contains(QStringLiteral("colourVision")))
        colourVision_=state.value(QStringLiteral("colourVision")).toInt();

    // A colour is restored only if it parses. A malformed hex string in a
    // hand-edited manifest must not turn the plot black.
    const auto colour=[&state](const char* key,const QColor& fallback){
        const QColor c(state.value(QString::fromLatin1(key)).toString());
        return c.isValid()?c:fallback;
    };
    spec_.style.background=colour("background",spec_.style.background);
    spec_.style.foreground=colour("foreground",spec_.style.foreground);
    spec_.style.gridColor=colour("grid",spec_.style.gridColor);
    // Absent in a figure saved before the map was a choice, and absent is
    // exactly right: those figures were all drawn with Viridis, which is what
    // an empty string means.
    chosenColourMap_=state.value(QStringLiteral("colourMap")).toString();
    // Resolved rather than assigned, because the restored figure has to obey
    // the colour-vision mode in force NOW, not the one in force when it was
    // saved - the setting belongs to the reader, not to the file.
    applyColourVisionToMap();

    // See figureState: these belong to the figure rather than to the
    // application. A file written before they existed carries neither, and the
    // canvas keeps what it has - which is the default each of them had.
    const auto number=[&state](const char* key,int fallback){
        const QVariant v=state.value(QString::fromLatin1(key));
        bool ok=false;
        const int n=v.toInt(&ok);
        return (v.isValid()&&ok)?n:fallback;
    };
    setPolarConvention(number("polarConvention",spec_.style.polarConvention));
    setPieLabels(number("pieLabels",spec_.style.pieLabels));
    setLegendLabels(number("legendLabels",spec_.style.legendLabels));
    setMeshDensity(number("meshDensity",spec_.style.meshDensity));

    // The engine's constants, from the file where the file has them and from
    // the engine's own declaration where it does not - a figure saved before
    // this existed must open with the defaults rather than with nothing, or a
    // Dalitz written by the older build would draw no boundary at all.
    adoptEngineParameters();
    const QVariantMap savedParams=state.value(QStringLiteral("engineParameters")).toMap();
    if(!savedParams.isEmpty()){
        const QVector<QtPlotBackend::EngineParameter> declared=
            QtPlotBackend::engineParameters(spec_.engine);
        for(const QtPlotBackend::EngineParameter& param:declared){
            const QVariant v=savedParams.value(param.key);
            bool ok=false;
            const double parsed=v.toDouble(&ok);
            // Clamped and checked, because a .gvfig is a file on disk that
            // someone may have edited by hand.
            if(!ok||parsed!=parsed) continue;
            spec_.parameters.insert(param.key,qBound(param.minimum,parsed,param.maximum));
        }
        engineParams_.insert(spec_.engine,spec_.parameters);
        // After the values, not before: adoptEngineParameters above emitted the
        // declaration at its defaults, and the interface must end up showing
        // what the figure was saved with.
        emit engineParametersChanged();
    }

    // Notes. Absent in a figure saved before they existed, which reads as none.
    // Rebuilt rather than merged: applyFigureState restores a figure, it does
    // not add to the one already open.
    spec_.annotations.clear();
    const QVariantList savedNotes=state.value(QStringLiteral("annotations")).toList();
    for(const QVariant& v:savedNotes){
        const QVariantMap m=v.toMap();
        PlotAnnotation note;
        note.x=m.value(QStringLiteral("x")).toDouble();
        note.y=m.value(QStringLiteral("y")).toDouble();
        note.text=m.value(QStringLiteral("text")).toString();
        note.offsetX=m.value(QStringLiteral("offsetX"),12.0).toDouble();
        note.offsetY=m.value(QStringLiteral("offsetY"),-18.0).toDouble();
        note.leader=m.value(QStringLiteral("leader"),true).toBool();
        // A note with no text or no position is not a note. Dropped rather than
        // restored, so a hand-edited container cannot put an invisible object
        // into the figure.
        if(note.text.trimmed().isEmpty()) continue;
        if(!std::isfinite(note.x)||!std::isfinite(note.y)) continue;
        spec_.annotations.append(note);
    }

    // The camera, before the rebuild below - it is part of the figure, not of
    // the view, and rebuild does not touch it. Clamped through the setters so
    // an edited file cannot put the cube inside out.
    if(state.contains(QStringLiteral("azimuth")))
        setAzimuth(state.value(QStringLiteral("azimuth")).toDouble());
    if(state.contains(QStringLiteral("elevation")))
        setElevation(state.value(QStringLiteral("elevation")).toDouble());
    if(state.contains(QStringLiteral("cameraZoom")))
        setCameraZoom(state.value(QStringLiteral("cameraZoom")).toDouble());

    // The in-frame view. BOUNDED AND CHECKED on the way in, like the camera
    // above it: this arrives from a file, and a hand-edited or truncated one
    // must not be able to set a zoom of zero - which no gesture could ever
    // undo, because every gesture multiplies it.
    {
        const auto finite=[](double v){ return v==v&&v>-1e12&&v<1e12; };
        PlotFrameView v;
        if(state.contains(QStringLiteral("framePanX")))
            v.panX=state.value(QStringLiteral("framePanX")).toDouble();
        if(state.contains(QStringLiteral("framePanY")))
            v.panY=state.value(QStringLiteral("framePanY")).toDouble();
        if(state.contains(QStringLiteral("frameZoom")))
            v.zoom=state.value(QStringLiteral("frameZoom")).toDouble();
        if(!finite(v.panX)||!finite(v.panY)) { v.panX=0.0; v.panY=0.0; }
        v.zoom=finite(v.zoom)?qBound(0.2,v.zoom,40.0):1.0;
        spec_.frameView=v;
    }

    dirty_=true;
    rebuild();
    // After rebuild, deliberately: rebuild clears any view, and these limits
    // are the view this figure was saved with.
    const auto limit=[&state](const char* key,double fallback){
        const QVariant v=state.value(QString::fromLatin1(key));
        if(!v.isValid()||v.isNull()) return fallback;
        bool ok=false;
        const double number=v.toDouble(&ok);
        return (ok&&number==number)?number:fallback;
    };
    spec_.xAxis.min=limit("xMin",unsetValue());
    spec_.xAxis.max=limit("xMax",unsetValue());
    spec_.yAxis.min=limit("yMin",unsetValue());
    spec_.yAxis.max=limit("yMax",unsetValue());
    spec_.zAxis.min=limit("zMin",unsetValue());
    spec_.zAxis.max=limit("zMax",unsetValue());
    spec_.style.colourMin=limit("colourMin",unsetValue());
    spec_.style.colourMax=limit("colourMax",unsetValue());
    if(state.contains(QStringLiteral("colourLevels")))
        spec_.style.colourLevels=qBound(0,state.value(QStringLiteral("colourLevels")).toInt(),256);
    if(state.contains(QStringLiteral("colourDrop")))
        spec_.style.colourOutOfRangeDropped=state.value(QStringLiteral("colourDrop")).toBool();
    if(state.contains(QStringLiteral("customColours"))){
        spec_.style.customColours.clear();
        const QVariantList list=state.value(QStringLiteral("customColours")).toList();
        for(const QVariant& v:list){
            const QColor c(v.toString());
            // A malformed entry is skipped rather than turning the figure
            // black, the same rule the background and foreground follow.
            if(c.isValid()) spec_.style.customColours.append(c);
        }
        applySeriesColours();
    }
    // The stored copy too, or the first rebuild after reopening - which a
    // style change is enough to cause - would put the saved range back to the
    // full extent of the data.
    for(int r=0;r<3;++r){
        const PlotAxis* axis=(r==0)?&spec_.xAxis:(r==1)?&spec_.yAxis:&spec_.zAxis;
        limitLoSet_[r]=!isUnset(axis->min);
        limitHiSet_[r]=!isUnset(axis->max);
        if(limitLoSet_[r]) limitLo_[r]=axis->min;
        if(limitHiSet_[r]) limitHi_[r]=axis->max;
    }
    hasView_=!isUnset(spec_.xAxis.min)||!isUnset(spec_.yAxis.min)
           ||!isUnset(spec_.zAxis.min)||!isUnset(spec_.zAxis.max);
    emit axisRangesChanged();
    update();
    emit sourceChanged();
    emit styleChanged();
    emit stateChanged();
}

bool PlotCanvas::exportPdf(const QString& filePath,double widthIn,double heightIn,int dpi){
    if(filePath.isEmpty()) return false;
    rebuild();
    if(spec_.series.isEmpty()) return false;

    QPdfWriter writer(filePath);
    writer.setResolution(dpi);
    writer.setPageSize(QPageSize(QSizeF(widthIn,heightIn),QPageSize::Inch));
    writer.setPageMargins(QMarginsF(0,0,0,0));

    // Publication output is drawn on white with dark ink regardless of the
    // interactive theme, matching GraphVis 17's controlled export background,
    // and from every row rather than the screen's point budget.
    PlotSpec publication=specWithFullData();
    publication.style.background=Qt::white;
    publication.style.foreground=QColor(0x11,0x11,0x11);
    publication.style.gridColor=QColor(0xd8,0xd8,0xd8);

    QPainter painter(&writer);
    if(!painter.isActive()) return false;
    const QRectF target(0,0,widthIn*dpi,heightIn*dpi);
    painter.setWindow(target.toRect());
    const FullQuality fullQuality(qtBackend_);
    qtBackend_.render(&painter,target,publication);
    painter.end();
    return QFileInfo::exists(filePath);
}

bool PlotCanvas::exportPng(const QString& filePath,int width,int height){
    if(filePath.isEmpty()) return false;
    rebuild();
    if(spec_.series.isEmpty()) return false;
    const PlotSpec full=specWithFullData();
    QImage image(qMax(16,width),qMax(16,height),QImage::Format_ARGB32_Premultiplied);
    image.fill(full.style.background);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing,true);
    painter.setRenderHint(QPainter::TextAntialiasing,true);
    const FullQuality fullQuality(qtBackend_);
    qtBackend_.render(&painter,QRectF(0,0,image.width(),image.height()),full);
    painter.end();
    return image.save(filePath);
}

bool PlotCanvas::exportRaster(const QString& filePath,int width,int height,
                              int quality,bool transparent){
    if(filePath.isEmpty()) return false;
    rebuild();
    if(spec_.series.isEmpty()) return false;
    const PlotSpec full=specWithFullData();
    QImage image(qMax(16,width),qMax(16,height),QImage::Format_ARGB32_Premultiplied);
    // A transparent ground is for dropping a figure onto a slide that is not
    // white. Only the BACKGROUND goes: the ink, the grid and the axis furniture
    // are all still drawn, so the figure is the same figure with nothing behind
    // it. Formats that cannot carry an alpha channel flatten it themselves, and
    // the dialog does not offer the option for those.
    image.fill(transparent?QColor(Qt::transparent):full.style.background);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing,true);
    painter.setRenderHint(QPainter::TextAntialiasing,true);
    const FullQuality fullQuality(qtBackend_);
    qtBackend_.render(&painter,QRectF(0,0,image.width(),image.height()),full);
    painter.end();

    QImageWriter writer(filePath);
    const QByteArray format=writer.format().toLower();
    if(format=="png"){
        // PNG's dial is a COMPRESSION LEVEL, 0 (none, largest) to 9, and Qt
        // takes it as 0-100 on that inverted scale. Handing it a quality
        // straight through would write the biggest files when the person asked
        // for the best picture - and PNG is lossless either way, so "best"
        // only ever meant "smallest that still decodes quickly".
        writer.setCompression(qBound(0,(100-qBound(0,quality,100))/11,9));
    }else{
        writer.setQuality(qBound(0,quality,100));
    }
    return writer.write(image);
}

QStringList PlotCanvas::exportFormats() const {
    // Vector first, because that is what a figure should be saved as if it is
    // going anywhere near a paper.
    QStringList out{QStringLiteral("pdf"),QStringLiteral("svg")};
    // Then whatever Qt can actually write here. Asked rather than assumed: the
    // raster formats come from plugins that may or may not have been deployed,
    // and a Save dialog that offers TIFF and then writes nothing is worse than
    // one that never offered it.
    static const QStringList kWanted{QStringLiteral("png"),QStringLiteral("jpg"),
                                     QStringLiteral("jpeg"),QStringLiteral("tif"),
                                     QStringLiteral("tiff"),QStringLiteral("bmp"),
                                     QStringLiteral("webp")};
    QSet<QString> have;
    for(const QByteArray& f:QImageWriter::supportedImageFormats())
        have.insert(QString::fromLatin1(f).toLower());
    for(const QString& w:kWanted) if(have.contains(w)) out.append(w);
    // And the two that are not pictures: the script that redraws the figure and
    // the numbers it was drawn from.
    out.append(QStringLiteral("py"));
    out.append(QStringLiteral("csv"));
    return out;
}

bool PlotCanvas::exportData(const QString& filePath) const {
    if(filePath.isEmpty()||spec_.series.isEmpty()) return false;
    QFile file(filePath);
    if(!file.open(QIODevice::WriteOnly|QIODevice::Text)) return false;
    QTextStream out(&file);
    // One block per series rather than one wide table: the series of a figure
    // do not in general share an x column - a box plot's five summary values
    // and a fitted curve's two hundred are both series here - and forcing them
    // into aligned columns would either pad with blanks or silently drop rows.
    for(const PlotSeries& s:spec_.series){
        out<<"# series: "<<(s.label.isEmpty()?QStringLiteral("(unnamed)"):s.label)<<"\n";
        out<<(spec_.xAxis.label.isEmpty()?QStringLiteral("x"):spec_.xAxis.label)<<","
           <<(spec_.yAxis.label.isEmpty()?QStringLiteral("y"):spec_.yAxis.label)<<"\n";
        const int n=qMin(s.x.size(),s.y.size());
        for(int i=0;i<n;++i)
            out<<QString::number(s.x[i],'g',12)<<","<<QString::number(s.y[i],'g',12)<<"\n";
        out<<"\n";
    }
    return true;
}

bool PlotCanvas::exportFigure(const QString& filePath,int width,int height){
    if(filePath.isEmpty()) return false;
    const QString ext=QFileInfo(filePath).suffix().toLower();

    if(ext==QLatin1String("pdf")) return exportPdf(filePath);
    // EPS through the PDF writer would produce a PDF with the wrong extension,
    // which opens in nothing that expects EPS. Qt cannot write PostScript - the
    // print engine that could was removed in Qt 5 - so this says so rather than
    // writing a file that looks right and is not.
    if(ext==QLatin1String("eps")) return false;
    if(ext==QLatin1String("svg")) return exportSvg(filePath);
    if(ext==QLatin1String("csv")) return exportData(filePath);
    if(ext==QLatin1String("py")){
        QFile file(filePath);
        if(!file.open(QIODevice::WriteOnly|QIODevice::Text)) return false;
        QTextStream(&file)<<reproducibleScript(QString());
        return true;
    }
    // Everything else goes to the raster path, where Qt picks the encoder from
    // the extension. Refused up front when this build cannot write it, so the
    // caller can say which formats are available instead of reporting a failure
    // after the person has chosen a filename.
    if(!exportFormats().contains(ext)) return false;
    return exportPng(filePath,width,height);
}

// ------------------------------------------------- full-resolution rendering
void PlotCanvas::invalidateFullRender(){
    // Any edit makes a finished render stale, so the view drops back to the
    // preview at once rather than showing a picture of the previous settings.
    if(fullCancel_) fullCancel_->storeRelease(1);
    fullImage_=QImage();
    readyGeneration_=-1;
    fullRenderWaiting_=false;
    showingFull_=false;
}

void PlotCanvas::scheduleFullRender(Retain retain){
    // THE TIMER MUST BE TOUCHED FROM THE GUI THREAD, AND THIS IS REACHED FROM
    // THE RENDER THREAD.
    //
    // This is the "QObject::startTimer: Timers cannot be started from another
    // thread" warning that had been in startup.log, unexplained, since before
    // the audit began. The chain is short and entirely ordinary:
    //
    //     PlotCanvas::paint()          <- scene-graph RENDER thread
    //       -> rebuild()               <- only does anything when dirty_
    //         -> scheduleFullRender()
    //           -> fullDebounce_.start()
    //
    // QTimer::start() on a running timer calls stop() first, which is why the
    // warnings arrive in killTimer/startTimer pairs. And it only happens when a
    // rebuild is still pending at paint time rather than having already been
    // done on the GUI thread, which is why it appeared a handful of times in a
    // long session and never in a --selftest-plot run: the selftest never opens
    // a window, so nothing ever paints.
    //
    // Marshalled rather than restructured. Moving the rebuild off paint() is a
    // much larger change to the thing most likely to break, and the debounce
    // does not care about one turn of the event loop - it is a timer whose
    // whole purpose is to wait.
    if(QThread::currentThread()!=thread()){
        QMetaObject::invokeMethod(this,[this,retain]{ scheduleFullRender(retain); },
                                  Qt::QueuedConnection);
        return;
    }
    ++generation_;
    if(renderInFlight_) ++pendingEdits_;

    // NOTHING EXPENSIVE WHILE A GESTURE IS IN PROGRESS.
    //
    // A wheel notch, a +/- button and a touch pan each rebuild the figure now,
    // and rebuild() ends here - so without this a zoom would start, and
    // abandon, a full-resolution render of the whole dataset on every step of
    // the gesture. That is the fault cameraMoved() records having fixed for the
    // 3-D camera; the 2-D view had it too, and this is the one place both now
    // pass through.
    //
    // invalidate rather than schedule: the accepted image is a picture of a
    // range the person has left and has to go, but a cancelled render returns a
    // null image that handleFullRenderFinished discards on its own, so nothing
    // needs a generation stamp to make that safe. The idle timer asks for
    // exactly one render when the gesture stops - and at a deep zoom
    // previewIsExact is true by then, so it asks for none at all.
    if(interacting_){
        invalidateFullRender();
        fullDebounce_.stop();
        renderInFlight_=false;
        emit renderStateChanged();
        update();
        return;
    }
    // A RESIZE IS NOT AN EDIT, and throwing the figure away for one is why it
    // vanished while a splitter was being dragged.
    //
    // invalidateFullRender drops the rendered image because "any edit makes a
    // finished render stale" - true of a changed column, a changed engine, a
    // changed colour map. It is NOT true of a resize: the picture is still a
    // correct picture of exactly this figure, drawn at the wrong size. Dropping
    // it left nothing on screen but whatever the preview could redraw within
    // the frame, on every frame of the drag, and on a dataset large enough to
    // need a full render that is a figure that blinks out for the whole
    // gesture and reappears when the mouse stops.
    //
    // Kept and drawn stretched instead. paint() already scales it to the item,
    // so a drag now shows the real figure very slightly soft until the new
    // render lands - which is what every other application does and what the
    // person expected.
    if(retain==Retain::RenderedImage&&!fullImage_.isNull()&&showingFull_){
        if(fullCancel_) fullCancel_->storeRelease(1);
        fullRenderWaiting_=false;
        // The CONTENT is still this generation's content. Only the size is out
        // of date, and paint() asks about content.
        readyGeneration_=generation_;
    }else{
        invalidateFullRender();
    }

    // Nothing to render at full resolution if the preview already shows every
    // point, and nothing to render at all without data.
    if(spec_.series.isEmpty()||previewIsExact()||arrowPath_.isEmpty()){
        renderInFlight_=false;
        fullDebounce_.stop();
        emit renderStateChanged();
        update();
        return;
    }
    fullDebounce_.start();
    emit renderStateChanged();
    update();
}

void PlotCanvas::startFullRender(){
    if(width()<=0||height()<=0) return;

    if(fullCancel_) fullCancel_->storeRelease(1);
    auto cancel=std::make_shared<QAtomicInt>(0);
    fullCancel_=cancel;

    renderingGeneration_=generation_;
    pendingEdits_=0;
    renderInFlight_=true;
    fullTimer_.restart();
    progressTick_->start();
    emit renderStateChanged();

    // Everything the worker needs is copied by value.
    //
    // The TABLE is shared rather than re-read. That comment used to say the
    // opposite - "it re-reads the Arrow file itself rather than sharing the
    // table, because loading it is most of the cost for a large dataset and
    // belongs off the GUI thread too" - and it was right while the GUI thread
    // threw its copy away after every rebuild. It is not right now: this thread
    // has just read the file to build the preview, so the worker's own read is
    // a second pass over the same bytes for a second copy of the same numbers,
    // and on a large dataset that is the single most expensive thing the render
    // does. A shared_ptr, so it stays alive even if the person opens a
    // different dataset while this render is still going.
    const std::shared_ptr<const ArrowTable> shared=table_;
    const QString path=arrowPath_;
    const QString xName=resolvedX_;
    const QStringList yNames=resolvedY_;
    const int vision=colourVision_;
    // THE UNITS TOO.
    //
    // rebuild() passes xUnit_ and yUnit_ to buildPlotSeries and this did not,
    // so a figure whose axis had been converted - metres to feet, kPa to bar -
    // had its preview drawn in the chosen unit and its ACCEPTED
    // full-resolution image drawn in the file's own. The picture changed under
    // the person a few hundred milliseconds after they stopped touching it,
    // with the axis label still naming the unit they picked. Silent, and only
    // on datasets large enough to have a full render at all, which is why it
    // had never been seen.
    //
    // Same rule as everywhere else in this file: what is on screen, what is
    // exported and what is rendered at full resolution have to be the same
    // figure, so anything rebuild() feeds the builder has to be fed to it here.
    const QString xUnit=xUnit_;
    const QString yUnit=yUnit_;
    // And the same window the preview used, so the two are pictures of the same
    // rows. Points outside it are clipped away before anything is drawn, so
    // reading and gridding them was work spent on pixels that do not exist -
    // and at a deep zoom it is nearly all of the file.
    const ViewWindow drawWindow=windowForDrawing(resolvedAsSeries_);
    PlotSpec templateSpec=spec_;
    templateSpec.series.clear();
    const qreal dpr=window()?window()->effectiveDevicePixelRatio():1.0;
    // Braces, not parentheses: QSize logical(int(width()),int(height())) is a
    // function declaration, not a variable - C++'s most vexing parse, and the
    // compiler only complains about it much further down in the lambda.
    const QSize logical{int(width()),int(height())};

    fullWatcher_.setFuture(QtConcurrent::run(
        [shared,path,xName,yNames,vision,templateSpec,dpr,logical,cancel,
         xUnit,yUnit,drawWindow]()->QImage{
            if(cancel->loadAcquire()) return QImage();
            // The preview's table when there is one - there almost always is,
            // because a full render is only ever scheduled after a rebuild.
            // Falling back to a read of its own keeps this correct if that ever
            // stops being true.
            ArrowTable own;
            const ArrowTable* table=shared.get();
            if(!table||!table->isValid()){
                if(!own.load(path)) return QImage();
                table=&own;
            }
            if(cancel->loadAcquire()) return QImage();

            PlotSpec full=templateSpec;
            if(buildPlotSeries(*table,xName,yNames,vision,0,full,xUnit,yUnit,drawWindow)<=0)
                return QImage();
            if(cancel->loadAcquire()) return QImage();

            QImage image(logical*dpr,QImage::Format_ARGB32_Premultiplied);
            if(image.isNull()) return QImage();
            image.setDevicePixelRatio(dpr);
            image.fill(full.style.background);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing,true);
            painter.setRenderHint(QPainter::TextAntialiasing,true);
            // The same backend that draws the screen and the PDF, so the full
            // render cannot disagree with the preview about anything except how
            // many points went into it.
            QtPlotBackend backend;
            backend.render(&painter,QRectF(QPointF(0,0),QSizeF(logical)),full);
            painter.end();
            if(cancel->loadAcquire()) return QImage();
            return image;
        }));
}

void PlotCanvas::handleFullRenderFinished(){
    renderInFlight_=false;
    progressTick_->stop();
    const qint64 elapsed=fullTimer_.elapsed();
    QImage image=fullWatcher_.future().resultCount()>0?fullWatcher_.result():QImage();

    // A render for settings that have since changed is discarded, not shown.
    if(image.isNull()||renderingGeneration_!=generation_){
        emit renderStateChanged();
        return;
    }
    if(fullPointCount_>0&&elapsed>0){
        const double observed=double(elapsed)/double(fullPointCount_);
        const double known=msPerPoint_.value(spec_.engine,0.0);
        msPerPoint_[spec_.engine]=(known<=0.0)?observed:(known*0.6+observed*0.4);
    }
    fullImage_=image;
    readyGeneration_=renderingGeneration_;

    // Show it, or offer it. See PlotCanvas::fullRenderPolicy.
    //
    // Never while a drag is in progress, whatever the policy: the render was
    // started for the view the drag has already left, and swapping a picture
    // out from under a finger is worse than showing a preview for another
    // moment. The notice is what that case falls back to.
    const bool quick=(double(elapsed)/1000.0)<=fullRenderAskAfterSeconds_;
    const bool swapNow=!dragging_
                     &&(fullRenderPolicy_==0
                      ||(fullRenderPolicy_==2&&quick));
    if(swapNow){
        showingFull_=true;
        fullRenderWaiting_=false;
        emit renderStateChanged();
        update();
        return;
    }
    fullRenderWaiting_=true;
    emit renderStateChanged();
}

void PlotCanvas::setFullRenderPolicy(int policy){
    const int clamped=qBound(0,policy,2);
    if(fullRenderPolicy_==clamped) return;
    fullRenderPolicy_=clamped;
    // A render already waiting was held under the OLD policy. Switching to
    // Automatic should show it now rather than leave a notice the setting says
    // should not exist.
    if(fullRenderPolicy_==0&&fullRenderWaiting_) acceptFullRender();
    else emit renderStateChanged();
}

void PlotCanvas::setFullRenderAskAfterSeconds(double seconds){
    const double clamped=qBound(0.0,seconds,600.0);
    if(qFuzzyCompare(fullRenderAskAfterSeconds_,clamped)) return;
    fullRenderAskAfterSeconds_=clamped;
    emit renderStateChanged();
}

void PlotCanvas::acceptFullRender(){
    if(readyGeneration_!=generation_||fullImage_.isNull()){
        // Asked for after further edits, so build the render they actually want
        // rather than showing a stale picture.
        fullRenderWaiting_=false;
        emit renderStateChanged();
        fullDebounce_.start(0);
        return;
    }
    showingFull_=true;
    fullRenderWaiting_=false;
    emit renderStateChanged();
    update();
}

void PlotCanvas::cancelFullRender(){
    progressTick_->stop();
    if(fullCancel_) fullCancel_->storeRelease(1);
    fullDebounce_.stop();
    renderInFlight_=false;
    fullRenderWaiting_=false;
    emit renderStateChanged();
}

double PlotCanvas::renderEstimateSeconds() const{
    if(fullPointCount_<=0) return 0.0;
    // No measurement for this engine yet: the shared default, which is roughly
    // a line chart. It is wrong for the slow engines, and it is only wrong once
    // - the first completed render replaces it with what that engine actually
    // costs on this machine.
    const double rate=msPerPoint_.value(spec_.engine,kDefaultMsPerPoint);
    return (double(fullPointCount_)*rate)/1000.0;
}

double PlotCanvas::renderProgress() const{
    if(!renderInFlight_) return fullRenderWaiting_?1.0:0.0;
    const double estimate=renderEstimateSeconds()*1000.0;
    if(estimate<=0.0) return 0.0;
    // Capped below 1 while work is still running: a bar sitting at 100% that
    // keeps going is worse than one admitting it is only an estimate.
    return qMin(0.97,double(fullTimer_.elapsed())/estimate);
}

QString PlotCanvas::renderRemainingText() const{
    if(fullRenderWaiting_) return QStringLiteral("ready");
    if(!renderInFlight_) return QString();
    const double remaining=renderEstimateSeconds()-double(fullTimer_.elapsed())/1000.0;
    if(remaining<=0.5) return QStringLiteral("finishing");
    if(remaining<60.0) return QStringLiteral("about %1s left").arg(int(remaining+0.5));
    return QStringLiteral("about %1 min left").arg(int(remaining/60.0+0.5));
}

// ------------------------------------------------------- publication export
QStringList PlotCanvas::publicationProfiles() const{
    QStringList names;
    for(const PublicationProfile& p:builtinProfiles()) names<<p.name;
    return names;
}

QVariantMap PlotCanvas::publicationProfile(const QString& name) const{
    const PublicationProfile p=profileByName(name);
    return QVariantMap{
        {QStringLiteral("name"),p.name},
        {QStringLiteral("dpi"),p.dpi},
        {QStringLiteral("widthIn"),p.figureWidthIn},
        {QStringLiteral("heightIn"),p.figureHeightIn},
        {QStringLiteral("widthMm"),p.figureWidthIn*25.4},
        {QStringLiteral("fontFamily"),p.fontFamily},
        {QStringLiteral("baseFontSize"),p.baseFontSize},
        {QStringLiteral("lineWidth"),p.lineWidth},
        {QStringLiteral("colorSpace"),p.colorSpace},
        {QStringLiteral("notes"),p.notes},
    };
}

bool PlotCanvas::exportSvg(const QString& filePath,double widthIn,double heightIn){
    if(filePath.isEmpty()) return false;
    rebuild();
    if(spec_.series.isEmpty()) return false;

    QSvgGenerator generator;
    generator.setFileName(filePath);
    // SVG is measured in points at 90 dpi by Qt's generator; the viewBox is
    // what makes it scale cleanly, so both are set.
    const double dpi=96.0;
    generator.setSize(QSize(int(widthIn*dpi),int(heightIn*dpi)));
    generator.setViewBox(QRect(0,0,int(widthIn*dpi),int(heightIn*dpi)));
    generator.setTitle(spec_.title.isEmpty()?QStringLiteral("GraphVis figure"):spec_.title);
    generator.setDescription(QStringLiteral("Drawn by GraphVis 18"));

    // Full data, like exportPdf and exportPng. This had the same fault they
    // did - exporting the screen's 10,000-point preview - and being the one
    // export with no caller made it a trap for whoever wired it up next.
    PlotSpec publication=specWithFullData();
    publication.style.background=Qt::white;
    publication.style.foreground=QColor(0x11,0x11,0x11);
    publication.style.gridColor=QColor(0xd8,0xd8,0xd8);

    QPainter painter(&generator);
    if(!painter.isActive()) return false;
    const FullQuality fullQuality(qtBackend_);
    qtBackend_.render(&painter,QRectF(0,0,widthIn*dpi,heightIn*dpi),publication);
    painter.end();
    return QFileInfo::exists(filePath);
}

bool PlotCanvas::exportWithProfile(const QString& filePath,const QString& profileName,
                                   const QString& format){
    if(filePath.isEmpty()) return false;
    rebuild();
    if(spec_.series.isEmpty()) return false;

    const PublicationProfile profile=profileByName(profileName).normalised();

    // Re-rendered at the profile's geometry rather than scaled from the window.
    // Scaling a screen figure to a column width shrinks the text with it, which
    // is exactly how a 6 pt caption becomes a 3 pt one.
    PlotSpec publication=profile.applyTo(spec_);

    // The full data, not the screen's decimated preview. An export is where the
    // point budget stops applying.
    //
    // With the chosen units, for the same reason the full-resolution render
    // needs them: a converted axis was drawn from the file's own values here
    // while the label named the unit the person picked, so the published
    // figure disagreed with the screen it was published from. specWithFullData
    // passes them; this and the worker were the two places that did not.
    if(!arrowPath_.isEmpty()){
        const ArrowTable& table=loadedTable();
        if(table.isValid())
            buildPlotSeries(table,resolvedX_,resolvedY_,colourVision_,0,publication,
                            xUnit_,yUnit_);
    }

    const QString kind=format.toLower();
    if(kind==QLatin1String("pdf")){
        QPdfWriter writer(filePath);
        writer.setResolution(profile.dpi);
        writer.setPageSize(QPageSize(QSizeF(profile.figureWidthIn,profile.figureHeightIn),
                                     QPageSize::Inch));
        writer.setPageMargins(QMarginsF(0,0,0,0));
        writer.setTitle(publication.title);
        QPainter painter(&writer);
        if(!painter.isActive()) return false;
        const QRectF target(0,0,profile.figureWidthIn*profile.dpi,
                            profile.figureHeightIn*profile.dpi);
        painter.setWindow(target.toRect());
        const FullQuality fullQuality(qtBackend_);
        qtBackend_.render(&painter,target,publication);
        painter.end();
        return QFileInfo(filePath).size()>0;
    }
    if(kind==QLatin1String("svg")){
        QSvgGenerator generator;
        generator.setFileName(filePath);
        const int w=int(profile.figureWidthIn*96.0), h=int(profile.figureHeightIn*96.0);
        generator.setSize(QSize(w,h));
        generator.setViewBox(QRect(0,0,w,h));
        generator.setTitle(publication.title);
        QPainter painter(&generator);
        if(!painter.isActive()) return false;
        const FullQuality fullQuality(qtBackend_);
        qtBackend_.render(&painter,QRectF(0,0,w,h),publication);
        painter.end();
        return QFileInfo::exists(filePath);
    }

    // PNG, at the profile's resolution. A 1000 dpi Elsevier line-art figure at
    // 90 mm is 3543 px wide; that is the point of the setting.
    const int w=qMax(64,int(profile.figureWidthIn*profile.dpi));
    const int h=qMax(64,int(profile.figureHeightIn*profile.dpi));
    // Guard against an accidental gigapixel: 1000 dpi on an A4-width figure is
    // fine, 1000 dpi on a 20-inch one is 200 megapixels and will not allocate.
    if(qint64(w)*qint64(h)>qint64(120000000)) return false;
    QImage image(w,h,QImage::Format_ARGB32_Premultiplied);
    if(image.isNull()) return false;
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing,true);
        painter.setRenderHint(QPainter::TextAntialiasing,true);
        // Drawn in inches-worth of logical space and scaled up, so a 600 dpi
        // export has the same proportions as the 96 dpi preview rather than
        // hairline text on a huge canvas.
        const double logicalW=profile.figureWidthIn*96.0;
        const double logicalH=profile.figureHeightIn*96.0;
        painter.scale(double(w)/logicalW,double(h)/logicalH);
        const FullQuality fullQuality(qtBackend_);
        qtBackend_.render(&painter,QRectF(0,0,logicalW,logicalH),publication);
    }
    image.setDotsPerMeterX(int(profile.dpi/0.0254));
    image.setDotsPerMeterY(int(profile.dpi/0.0254));
    return image.save(filePath);
}

QString PlotCanvas::reproducibleScript(const QString& profileName) const{
    const PublicationProfile p=profileByName(profileName).normalised();
    // Python with matplotlib, because that is what the reader of a methods
    // section will have. It is a record of what was plotted, not a promise that
    // matplotlib will draw it identically - GraphVis and matplotlib disagree
    // about tick placement, and saying so here is better than being asked why.
    QStringList out;
    out<<QStringLiteral("# Regenerates this GraphVis figure from the same source data.")
       <<QStringLiteral("#")
       <<QStringLiteral("# GraphVis draws it with its own backend, so tick placement and")
       <<QStringLiteral("# minor spacing will differ from the exported PDF. The data, the")
       <<QStringLiteral("# mapping and the publication geometry are exact.")
       <<QString()
       <<QStringLiteral("import pandas as pd")
       <<QStringLiteral("import matplotlib.pyplot as plt")
       <<QString()
       <<QStringLiteral("data = pd.read_feather(r\"%1\")").arg(arrowPath_)
       <<QString()
       <<QStringLiteral("plt.rcParams.update({")
       <<QStringLiteral("    \"font.family\": \"%1\",").arg(p.fontFamily)
       <<QStringLiteral("    \"font.size\": %1,").arg(p.baseFontSize)
       <<QStringLiteral("    \"axes.linewidth\": %1,").arg(p.lineWidth,0,'g',3)
       <<QStringLiteral("    \"savefig.dpi\": %1,").arg(p.dpi)
       <<QStringLiteral("    \"pdf.fonttype\": 42,   # embed fonts as TrueType, not paths")
       <<QStringLiteral("})")
       <<QString()
       <<QStringLiteral("fig, ax = plt.subplots(figsize=(%1, %2))")
             .arg(p.figureWidthIn,0,'f',3).arg(p.figureHeightIn,0,'f',3);
    for(const QString& y:resolvedY_){
        out<<QStringLiteral("ax.plot(data[\"%1\"], data[\"%2\"], linewidth=%3, label=\"%2\")")
                 .arg(resolvedX_,y).arg(p.lineWidth,0,'g',3);
    }
    out<<QStringLiteral("ax.set_xlabel(\"%1\")").arg(spec_.xAxis.label)
       <<QStringLiteral("ax.set_ylabel(\"%1\")").arg(spec_.yAxis.label);
    if(spec_.xAxis.log10) out<<QStringLiteral("ax.set_xscale(\"log\")");
    if(spec_.yAxis.log10) out<<QStringLiteral("ax.set_yscale(\"log\")");
    if(!spec_.title.isEmpty()) out<<QStringLiteral("ax.set_title(\"%1\")").arg(spec_.title);
    out<<QStringLiteral("ax.grid(%1)").arg(p.gridVisible?QStringLiteral("True"):QStringLiteral("False"))
       <<QStringLiteral("ax.legend(fontsize=%1)").arg(p.legendSize,0,'g',3)
       <<QStringLiteral("fig.tight_layout(pad=%1)").arg(p.marginPadding*12.0,0,'f',2)
       <<QStringLiteral("fig.savefig(\"figure.pdf\")")
       <<QString()
       <<QStringLiteral("# Engine: %1").arg(spec_.engine)
       <<QStringLiteral("# Profile: %1 (%2 mm wide, %3 dpi, %4)")
             .arg(p.name).arg(p.figureWidthIn*25.4,0,'f',1).arg(p.dpi).arg(p.colorSpace);
    if(!p.notes.isEmpty()) out<<QStringLiteral("# %1").arg(p.notes);
    return out.join(QLatin1Char('\n'))+QLatin1Char('\n');
}

} // namespace graphvis
