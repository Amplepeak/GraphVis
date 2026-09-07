#pragma once
#include <QAtomicInt>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QImage>
#include <QTimer>
#include <QVariantMap>
#include <memory>
// =========================================================================
// PlotCanvas - the QML item that shows a 2-D figure.
//
// It owns a PlotSpec, builds it from the active dataset's Arrow file and the
// catalogue entry staged in the Graph Library, and hands it to whichever
// PlotBackend is selected. Painting and vector export share one code path, so
// what is exported is what was on screen.
// =========================================================================
#include "PlotSpec.h"
#include "QtPlotBackend.h"

#include <QQuickPaintedItem>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace graphvis {

class PlotCanvas : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString arrowPath READ arrowPath WRITE setArrowPath NOTIFY sourceChanged)
    Q_PROPERTY(QString engine READ engine WRITE setEngine NOTIFY sourceChanged)
    Q_PROPERTY(QString variant READ variant WRITE setVariant NOTIFY sourceChanged)
    Q_PROPERTY(QString xColumn READ xColumn WRITE setXColumn NOTIFY sourceChanged)
    Q_PROPERTY(QStringList yColumns READ yColumns WRITE setYColumns NOTIFY sourceChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY sourceChanged)
    Q_PROPERTY(bool logX READ logX WRITE setLogX NOTIFY sourceChanged)
    Q_PROPERTY(bool logY READ logY WRITE setLogY NOTIFY sourceChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(bool engineSupported READ engineSupported NOTIFY stateChanged)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY stateChanged)
    // Figure colours follow the UI theme; without these the canvas
    // stayed dark inside a light theme.
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY styleChanged)
    Q_PROPERTY(QColor foregroundColor READ foregroundColor WRITE setForegroundColor NOTIFY styleChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY styleChanged)
    // 0 Standard, 1 Protanopia, 2 Deuteranopia, 3 Tritanopia, 4 Monochrome.
    // Owned by AppController and persisted, so it survives theme changes and
    // restarts - see ColourVision.h for why it is separate from the UI theme.
    Q_PROPERTY(int colourVision READ colourVision WRITE setColourVision NOTIFY styleChanged)

    // Level of detail.
    //
    // The on-screen draw is always decimated to a fixed budget and always
    // synchronous, which is why the interface cannot hang no matter how large
    // the dataset is - that decimated draw IS the reactive preview. These
    // properties describe the other half: the same picture rendered at full
    // resolution on a worker thread, offered when it is ready rather than
    // swapped in underneath whatever the user is doing.
    Q_PROPERTY(int previewPointCount READ previewPointCount NOTIFY stateChanged)
    Q_PROPERTY(int fullPointCount READ fullPointCount NOTIFY stateChanged)
    Q_PROPERTY(bool previewIsExact READ previewIsExact NOTIFY stateChanged)
    Q_PROPERTY(bool renderInFlight READ renderInFlight NOTIFY renderStateChanged)
    Q_PROPERTY(double renderProgress READ renderProgress NOTIFY renderStateChanged)
    Q_PROPERTY(double renderEstimateSeconds READ renderEstimateSeconds NOTIFY renderStateChanged)
    Q_PROPERTY(QString renderRemainingText READ renderRemainingText NOTIFY renderStateChanged)
    Q_PROPERTY(bool fullRenderWaiting READ fullRenderWaiting NOTIFY renderStateChanged)
    Q_PROPERTY(bool showingFullRender READ showingFullRender NOTIFY renderStateChanged)
    Q_PROPERTY(int pendingEditCount READ pendingEditCount NOTIFY renderStateChanged)
public:
    explicit PlotCanvas(QQuickItem* parent=nullptr);
    ~PlotCanvas() override;

    QString arrowPath() const { return arrowPath_; }
    QString engine() const { return spec_.engine; }
    QString variant() const { return spec_.variant; }
    QString xColumn() const { return xColumn_; }
    QStringList yColumns() const { return yColumns_; }
    QString title() const { return spec_.title; }
    bool logX() const { return spec_.xAxis.log10; }
    bool logY() const { return spec_.yAxis.log10; }
    QString message() const { return message_; }
    bool engineSupported() const { return engineSupported_; }
    int pointCount() const { return pointCount_; }
    QColor backgroundColor() const { return spec_.style.background; }
    QColor foregroundColor() const { return spec_.style.foreground; }
    int colourVision() const{return colourVision_;}
    void setColourVision(int mode);

    int previewPointCount() const{return pointCount_;}
    int fullPointCount() const{return fullPointCount_;}
    bool previewIsExact() const{return fullPointCount_<=pointCount_;}
    // spec_ carries the decimated preview series, because that is what the
    // screen draws. Every export path has to re-read the Arrow file at full
    // resolution first; exportPdf and exportPng used to skip that and write out
    // the 10,000-point preview while their own comments said otherwise.
    PlotSpec specWithFullData() const;
    bool renderInFlight() const{return renderInFlight_;}
    double renderProgress() const;
    double renderEstimateSeconds() const;
    QString renderRemainingText() const;
    bool fullRenderWaiting() const{return fullRenderWaiting_;}
    bool showingFullRender() const{return showingFull_;}
    int pendingEditCount() const{return pendingEdits_;}
    // Swap the finished full-resolution render in. Deliberately a decision the
    // user makes: a render landing mid-drag must not replace the view they are
    // working against.
    Q_INVOKABLE void acceptFullRender();
    Q_INVOKABLE void cancelFullRender();
    QColor gridColor() const { return spec_.style.gridColor; }
    void setBackgroundColor(const QColor& c);
    void setForegroundColor(const QColor& c);
    void setGridColor(const QColor& c);

    void setArrowPath(const QString& v);
    void setEngine(const QString& v);
    void setVariant(const QString& v);
    void setXColumn(const QString& v);
    void setYColumns(const QStringList& v);
    void setTitle(const QString& v);
    void setLogX(bool v);
    void setLogY(bool v);

    // Numeric columns found in the active dataset, for the mapping UI.
    // True vector PDF with embedded fonts, drawn by the same backend code.
    Q_INVOKABLE bool exportPdf(const QString& filePath,double widthIn=6.0,double heightIn=4.0,int dpi=600);
    Q_INVOKABLE bool exportPng(const QString& filePath,int width=1600,int height=1000);
    Q_INVOKABLE bool exportSvg(const QString& filePath,double widthIn=6.0,double heightIn=4.0);

    // Publication profiles. Exporting through a profile is the point of them:
    // the figure is re-rendered at the journal's column width, text size and
    // resolution rather than scaled from whatever the window happened to be.
    Q_INVOKABLE QStringList publicationProfiles() const;
    Q_INVOKABLE QVariantMap publicationProfile(const QString& name) const;
    Q_INVOKABLE bool exportWithProfile(const QString& filePath,const QString& profileName,
                                       const QString& format);
    // Everything needed to redraw this figure elsewhere: the mapping, the
    // engine, the axes and the profile. GraphVis 17 called this export_code.
    Q_INVOKABLE QString reproducibleScript(const QString& profileName) const;

    void paint(QPainter* painter) override;
    // A resize invalidates the full-resolution image: paint() draws it scaled
    // into the new size, so without this the "full resolution" view became a
    // stretched bitmap of the old geometry and stayed that way until something
    // unrelated changed. The existing 280 ms debounce absorbs a resize drag.
    void geometryChange(const QRectF& newGeometry,const QRectF& oldGeometry) override;

signals:
    void sourceChanged();
    void stateChanged();
    void styleChanged();
    void renderStateChanged();

private:
    void rebuild();          // reload data and regenerate the spec's series
    void applyVariant();     // catalogue scale variants: Semi-Log X, etc.
    void renderTo(QPainter* painter,const QRectF& target);

    QtPlotBackend qtBackend_;
    PlotSpec spec_;
    QString arrowPath_;
    QString xColumn_;
    QStringList yColumns_;
    QStringList available_;
    QString message_;
    bool engineSupported_=true;
    int pointCount_=0;
    int fullPointCount_=0;
    int colourVision_=0;
    bool dirty_=true;

    // ---- full-resolution render, off the GUI thread
    void scheduleFullRender();
    void startFullRender();
    void handleFullRenderFinished();
    void invalidateFullRender();

    QString resolvedX_;
    QStringList resolvedY_;
    QImage fullImage_;
    QFutureWatcher<QImage> fullWatcher_;
    QTimer fullDebounce_;
    // Drives the progress readout, and only runs while a render is in flight.
    QTimer* progressTick_=nullptr;
    QElapsedTimer fullTimer_;
    // Shared with the worker so an abandoned render stops promptly instead of
    // finishing work nobody will look at.
    std::shared_ptr<QAtomicInt> fullCancel_;
    int generation_=0;          // bumped by every edit
    int renderingGeneration_=-1;
    int readyGeneration_=-1;
    int pendingEdits_=0;
    bool renderInFlight_=false;
    bool fullRenderWaiting_=false;
    bool showingFull_=false;
    // Milliseconds per point, measured. Starts as a deliberate over-estimate:
    // showing a progress bar that was not needed is a smaller sin than hiding
    // one that was.
    double msPerPoint_=0.0025;
};

} // namespace graphvis
