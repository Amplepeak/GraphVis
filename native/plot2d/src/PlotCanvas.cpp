#include "PlotCanvas.h"
#include "ArrowTable.h"
#include "ColourVision.h"
#include "PublicationProfile.h"
#include "Units.h"

#include "QtPlotBackend.h"

#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPdfWriter>
#include <QPageSize>
#include <QSvgGenerator>
#include <QQuickWindow>
#include <QtConcurrent/QtConcurrentRun>

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
                    const QString& xUnit=QString(),const QString& yUnit=QString()){
    spec.series.clear();
    QVector<double> xs=decimate(table.column(xName),budget);
    Units::Conversion xStore;
    applyConversion(xs,displayConversion(xName,xUnit,xStore));
    const ColourVision vision=colourVisionFromInt(colourVision);
    const QVector<QColor> palette=seriesPalette(vision);
    const QVector<QVector<qreal>> dashes=seriesDashPatterns(vision);
    int total=0;
    int idx=0;
    for(const QString& yName:yNames){
        if(!table.hasColumn(yName)) continue;
        PlotSeries s;
        s.label=Units::axisLabel(yName);
        s.x=xs;
        s.y=decimate(table.column(yName),budget);
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

    // Edits arrive in bursts - dragging a slider is dozens of changes a second -
    // and starting a multi-second render for each one would be worse than
    // useless. The preview redraws on every one of them; only the expensive
    // render waits for the burst to end.
    fullDebounce_.setSingleShot(true);
    fullDebounce_.setInterval(280);
    connect(&fullDebounce_,&QTimer::timeout,this,&PlotCanvas::startFullRender);
    connect(&fullWatcher_,&QFutureWatcher<QImage>::finished,
            this,&PlotCanvas::handleFullRenderFinished);

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
GV_SETTER(setXUnit,xUnit_,QString)
GV_SETTER(setYUnit,yUnit_,QString)

void PlotCanvas::setEngine(const QString& v){
    if(spec_.engine==v) return;
    spec_.engine=v;
    engineSupported_=qtBackend_.supports(v);
    dirty_=true; rebuild(); update();
    emit sourceChanged(); emit stateChanged();
}
void PlotCanvas::setVariant(const QString& v){
    if(spec_.variant==v) return;
    spec_.variant=v; applyVariant(); update(); emit sourceChanged();
}
void PlotCanvas::setTitle(const QString& v){
    if(spec_.title==v) return; spec_.title=v; update(); emit sourceChanged();
}
void PlotCanvas::setBackgroundColor(const QColor& c){ if(spec_.style.background==c) return; spec_.style.background=c; update(); emit styleChanged(); }
void PlotCanvas::setForegroundColor(const QColor& c){ if(spec_.style.foreground==c) return; spec_.style.foreground=c; update(); emit styleChanged(); }
void PlotCanvas::setGridColor(const QColor& c){ if(spec_.style.gridColor==c) return; spec_.style.gridColor=c; update(); emit styleChanged(); }

void PlotCanvas::setColourVision(int mode){
    const int clamped=qBound(0,mode,4);
    if(colourVision_==clamped) return;
    colourVision_=clamped;
    dirty_=true;
    emit styleChanged();
    update();
}

void PlotCanvas::setLogX(bool v){ if(spec_.xAxis.log10==v) return; spec_.xAxis.log10=v; update(); emit sourceChanged(); }
void PlotCanvas::setLogY(bool v){ if(spec_.yAxis.log10==v) return; spec_.yAxis.log10=v; update(); emit sourceChanged(); }

// Catalogue entries carry their axis-scale variant as a separate field, so
// "Line Chart" + "Semi-Log X" is one entry rather than a distinct engine.
void PlotCanvas::applyVariant(){
    const QString v=spec_.variant;
    if(v.compare(QLatin1String("Logarithmic Scale"),Qt::CaseInsensitive)==0){ spec_.xAxis.log10=true; spec_.yAxis.log10=true; }
    else if(v.compare(QLatin1String("Semi-Log X"),Qt::CaseInsensitive)==0){ spec_.xAxis.log10=true; spec_.yAxis.log10=false; }
    else if(v.compare(QLatin1String("Semi-Log Y"),Qt::CaseInsensitive)==0){ spec_.xAxis.log10=false; spec_.yAxis.log10=true; }
    emit stateChanged();
}

void PlotCanvas::rebuild(){
    if(!dirty_) return;
    dirty_=false;
    spec_.series.clear();
    pointCount_=0;
    available_.clear();
    engineSupported_=qtBackend_.supports(spec_.engine);

    if(arrowPath_.isEmpty()){ message_=QStringLiteral("Import or select a dataset"); emit stateChanged(); return; }

    ArrowTable table;
    if(!table.load(arrowPath_)){ message_=table.error(); emit stateChanged(); return; }
    available_=table.columnNames();

    if(!engineSupported_){
        message_=QStringLiteral("“%1” is in the catalogue but its engine is not ported yet").arg(spec_.engine);
        emit stateChanged(); return;
    }

    // Fall back to the first numeric columns so a freshly staged graph draws
    // something meaningful before the user has chosen a mapping.
    QString xName=xColumn_;
    QStringList yNames=yColumns_;
    if(xName.isEmpty()&&!available_.isEmpty()) xName=available_.first();
    if(yNames.isEmpty()){
        for(const QString& c:std::as_const(available_)){ if(c!=xName){ yNames.append(c); break; } }
    }
    if(yNames.isEmpty()){ message_=QStringLiteral("Need at least two numeric columns to plot"); emit stateChanged(); return; }

    pointCount_=buildPlotSeries(table,xName,yNames,colourVision_,
                                interactiveBudgetFor(int(yNames.size())),spec_,
                                xUnit_,yUnit_);

    // How many points there would have been without the budget. The difference
    // between this and pointCount_ is the whole reason the full-resolution
    // render exists; when they match, the preview already IS the finished
    // picture and no worker is started at all.
    fullPointCount_=0;
    for(const QString& yName:std::as_const(yNames))
        if(table.hasColumn(yName))
            fullPointCount_+=qMin(table.column(xName).size(),table.column(yName).size());
    resolvedX_=xName;
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
    message_=spec_.series.isEmpty()?QStringLiteral("Selected columns have no numeric data")
                                   :QStringLiteral("%1 · %2 points").arg(spec_.engine).arg(pointCount_);
    applyVariant();
    emit stateChanged();
    scheduleFullRender();
}

void PlotCanvas::renderTo(QPainter* painter,const QRectF& target){
    // Every backend selection still exports through Qt: a rasterising backend
    // cannot emit vectors. See docs/PORT-PLAN.md, rule 2.
    qtBackend_.render(painter,target,spec_);
}

void PlotCanvas::geometryChange(const QRectF& newGeometry,const QRectF& oldGeometry){
    QQuickPaintedItem::geometryChange(newGeometry,oldGeometry);
    // Only the size matters; being moved does not change what was rendered.
    if(newGeometry.size()==oldGeometry.size()) return;
    if(newGeometry.width()<=0||newGeometry.height()<=0) return;
    scheduleFullRender();
}

void PlotCanvas::paint(QPainter* painter){
    rebuild();
    const QRectF target(0,0,width(),height());

    // The accepted full-resolution render, if there is one and it still matches
    // the current settings. Anything else falls through to the live preview.
    if(showingFull_&&!fullImage_.isNull()&&readyGeneration_==generation_){
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
    ArrowTable table;
    if(arrowPath_.isEmpty()||!table.load(arrowPath_)) return full;   // preview is all there is
    PlotSpec candidate=spec_;
    if(buildPlotSeries(table,resolvedX_,resolvedY_,colourVision_,0,candidate,xUnit_,yUnit_)<=0)
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
        {QStringLiteral("xColumn"),xColumn_},
        {QStringLiteral("yColumns"),QVariant(yColumns_)},
        {QStringLiteral("logX"),spec_.xAxis.log10},
        {QStringLiteral("logY"),spec_.yAxis.log10},
        {QStringLiteral("xUnit"),xUnit_},
        {QStringLiteral("yUnit"),yUnit_},
        {QStringLiteral("colourVision"),colourVision_},
        {QStringLiteral("background"),spec_.style.background.name(QColor::HexArgb)},
        {QStringLiteral("foreground"),spec_.style.foreground.name(QColor::HexArgb)},
        {QStringLiteral("grid"),spec_.style.gridColor.name(QColor::HexArgb)},
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
    xColumn_=text("xColumn",xColumn_);
    if(state.contains(QStringLiteral("yColumns")))
        yColumns_=state.value(QStringLiteral("yColumns")).toStringList();
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

    dirty_=true;
    rebuild();
    update();
    emit sourceChanged();
    emit styleChanged();
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
    qtBackend_.render(&painter,QRectF(0,0,image.width(),image.height()),full);
    painter.end();
    return image.save(filePath);
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

void PlotCanvas::scheduleFullRender(){
    ++generation_;
    if(renderInFlight_) ++pendingEdits_;
    invalidateFullRender();

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

    // Everything the worker needs is copied by value. It re-reads the Arrow
    // file itself rather than sharing the table, because loading it is most of
    // the cost for a large dataset and belongs off the GUI thread too.
    const QString path=arrowPath_;
    const QString xName=resolvedX_;
    const QStringList yNames=resolvedY_;
    const int vision=colourVision_;
    PlotSpec templateSpec=spec_;
    templateSpec.series.clear();
    const qreal dpr=window()?window()->effectiveDevicePixelRatio():1.0;
    // Braces, not parentheses: QSize logical(int(width()),int(height())) is a
    // function declaration, not a variable - C++'s most vexing parse, and the
    // compiler only complains about it much further down in the lambda.
    const QSize logical{int(width()),int(height())};

    fullWatcher_.setFuture(QtConcurrent::run(
        [path,xName,yNames,vision,templateSpec,dpr,logical,cancel]()->QImage{
            if(cancel->loadAcquire()) return QImage();
            ArrowTable table;
            if(!table.load(path)) return QImage();
            if(cancel->loadAcquire()) return QImage();

            PlotSpec full=templateSpec;
            if(buildPlotSeries(table,xName,yNames,vision,0,full)<=0) return QImage();
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
        msPerPoint_=(msPerPoint_<=0.0)?observed:(msPerPoint_*0.6+observed*0.4);
    }
    fullImage_=image;
    readyGeneration_=renderingGeneration_;
    fullRenderWaiting_=true;
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
    return (double(fullPointCount_)*msPerPoint_)/1000.0;
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
    {
        ArrowTable table;
        if(!arrowPath_.isEmpty()&&table.load(arrowPath_))
            buildPlotSeries(table,resolvedX_,resolvedY_,colourVision_,0,publication);
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
