#pragma once
#include <QAtomicInt>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
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
#include "TouchGesture.h"
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
    // Display units. The column label carries the source unit - "Pressure
    // [kPa]" - and setting a target unit rescales what is drawn without
    // touching the data or re-importing anything. Empty means "as imported".
    Q_PROPERTY(QString xUnit READ xUnit WRITE setXUnit NOTIFY sourceChanged)
    Q_PROPERTY(QString yUnit READ yUnit WRITE setYUnit NOTIFY sourceChanged)
    // The unit read out of each axis's column, so QML can offer the
    // conversions that exist rather than a fixed list.
    Q_PROPERTY(QString xSourceUnit READ xSourceUnit NOTIFY stateChanged)
    Q_PROPERTY(QString ySourceUnit READ ySourceUnit NOTIFY stateChanged)
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
    // The colour map for engines that colour a field rather than a series -
    // heatmap, contour, surface, 3-D field, vector field. Empty is Viridis,
    // which is what every one of them was hard-coded to. See
    // PlotStyle::colourMap for the full list and for why there is no rainbow.
    Q_PROPERTY(QString colourMap READ colourMap WRITE setColourMap NOTIFY styleChanged)
    // True only while the engine on screen actually colours a field, so the
    // interface can hide the picker on the engines it would do nothing for.
    Q_PROPERTY(bool usesColourMap READ usesColourMap NOTIFY stateChanged)

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
    // What happens when the full-resolution render lands.
    //
    //   0  Automatic       - it replaces the preview as soon as it is ready.
    //                        The default, because looking at a preview when a
    //                        better picture EXISTS is the wrong picture.
    //   1  Ask every time  - the notice waits for "Show it". What this used to
    //                        do, unconditionally.
    //   2  Ask on long ones- swap silently when the render was quick; ask when
    //                        it took longer than fullRenderAskAfterSeconds, on
    //                        the grounds that by then you have moved on to
    //                        something else and having the view change under
    //                        you is worse than a notice.
    //
    // Even on Automatic the swap is refused while a drag is in progress: a
    // picture that changes under a finger is not a better picture.
    Q_PROPERTY(int fullRenderPolicy READ fullRenderPolicy WRITE setFullRenderPolicy NOTIFY renderStateChanged)
    Q_PROPERTY(double fullRenderAskAfterSeconds READ fullRenderAskAfterSeconds WRITE setFullRenderAskAfterSeconds NOTIFY renderStateChanged)

    // Pan and zoom.
    //
    // The figure had no pointer interaction of any kind - no wheel, no drag,
    // and so nothing for a finger to do either. The axis limits it zooms with
    // were already in PlotSpec and already honoured by the backend; nothing
    // had ever set them.
    //
    // viewInteractive is false for the engines drawn without a rectangular
    // frame - pie, polar, treemap, the 3-D projections - where an axis range
    // means nothing. QML uses it to decide whether to offer the affordance.
    Q_PROPERTY(bool viewInteractive READ viewInteractive NOTIFY stateChanged)
    Q_PROPERTY(bool viewZoomed READ viewZoomed NOTIFY stateChanged)

    // Cursor readout.
    //
    // A figure that can be zoomed into needs a way to say what is under the
    // pointer, and this one had none: the axes told you the range and nothing
    // told you the value. cursorText is ready to display - "t = 4.128 s,
    // p = 101.7 kPa" - built here rather than in QML so the number of
    // significant figures follows the ZOOM. At full extent four figures is
    // noise; zoomed a thousandfold into a transient it is the whole point.
    Q_PROPERTY(QString cursorText READ cursorText NOTIFY cursorChanged)
    Q_PROPERTY(bool cursorOnPlot READ cursorOnPlot NOTIFY cursorChanged)
    Q_PROPERTY(double cursorX READ cursorX NOTIFY cursorChanged)
    Q_PROPERTY(double cursorY READ cursorY NOTIFY cursorChanged)

    // Annotations.
    //
    // While annotating, a click on the figure places a note at the data point
    // under it rather than starting a pan. That is the only mode this canvas
    // has, and it exists because there is no other gesture left: a drag pans, a
    // wheel zooms, a double-click resets, and a plain click has to stay
    // available for the pan to feel immediate.
    Q_PROPERTY(bool annotating READ annotating WRITE setAnnotating NOTIFY annotationsChanged)
    Q_PROPERTY(QVariantList annotations READ annotations NOTIFY annotationsChanged)
    Q_PROPERTY(int annotationCount READ annotationCount NOTIFY annotationsChanged)
public:
    explicit PlotCanvas(QQuickItem* parent=nullptr);
    ~PlotCanvas() override;

    QString arrowPath() const { return arrowPath_; }
    QString engine() const { return spec_.engine; }
    QString variant() const { return spec_.variant; }
    QString xColumn() const { return xColumn_; }
    QStringList yColumns() const { return yColumns_; }
    QString xUnit() const { return xUnit_; }
    QString yUnit() const { return yUnit_; }
    QString xSourceUnit() const;
    QString ySourceUnit() const;
    // Every unit this column's own unit can be converted to, target names only.
    // Empty for a column with no unit in its label, which is the common case.
    Q_INVOKABLE QStringList unitOptions(const QString& column) const;

    // The canvas state a .gvfig stores, and the reverse. Everything the user
    // chose - engine, variant, title, columns, log axes, display units, colour
    // vision and the three theme colours. Deliberately a plain map: the figure
    // package stores it verbatim, so adding a field here needs no change on
    // either side of the pipe.
    Q_INVOKABLE QVariantMap figureState() const;
    Q_INVOKABLE void applyFigureState(const QVariantMap& state);
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
    int fullRenderPolicy() const{return fullRenderPolicy_;}
    void setFullRenderPolicy(int policy);
    double fullRenderAskAfterSeconds() const{return fullRenderAskAfterSeconds_;}
    void setFullRenderAskAfterSeconds(double seconds);
    int pendingEditCount() const{return pendingEdits_;}
    // Swap the finished full-resolution render in. Deliberately a decision the
    // user makes: a render landing mid-drag must not replace the view they are
    // working against.
    Q_INVOKABLE void acceptFullRender();
    Q_INVOKABLE void cancelFullRender();

    bool viewInteractive() const;
    bool viewZoomed() const { return hasView_; }
    QString cursorText() const { return cursorText_; }
    bool cursorOnPlot() const { return cursorOnPlot_; }
    double cursorX() const { return cursorX_; }
    double cursorY() const { return cursorY_; }
    bool annotating() const { return annotating_; }
    void setAnnotating(bool on);
    QVariantList annotations() const;
    int annotationCount() const { return int(spec_.annotations.size()); }
    // x and y are DATA coordinates - use cursorX/cursorY, or the point handed
    // to annotationRequested. Returns the new note's index.
    Q_INVOKABLE int addAnnotation(double x,double y,const QString& text);
    Q_INVOKABLE void updateAnnotation(int index,const QString& text);
    Q_INVOKABLE void moveAnnotation(int index,double offsetX,double offsetY);
    Q_INVOKABLE void removeAnnotation(int index);
    Q_INVOKABLE void clearAnnotations();
    // Back to fitting the data. Also what a double-click and a double-tap do.
    Q_INVOKABLE void resetView();
    // For a QML button or a keyboard shortcut: >1 zooms in, about the centre.
    Q_INVOKABLE void zoomBy(double factor);
    QColor gridColor() const { return spec_.style.gridColor; }
    QString colourMap() const { return spec_.style.colourMap; }
    bool usesColourMap() const { return usesColourMap_; }
    void setBackgroundColor(const QColor& c);
    void setForegroundColor(const QColor& c);
    void setGridColor(const QColor& c);
    void setColourMap(const QString& name);
    // The names setColourMap accepts, in the order they should be offered.
    // Here rather than in QML so the list cannot drift from the one the
    // renderer actually understands.
    Q_INVOKABLE static QStringList colourMapNames();
    // The same names grouped into the eight categories GraphVis 17 used, as
    // [{name, maps: [...]}, ...]. Eighty four names in one list is a list to
    // scroll, not a choice to make.
    Q_INVOKABLE static QVariantList colourMapCategories();
    // A base64 PNG strip of one map, for showing beside its name: nobody knows
    // what "Gist Ncar" looks like from the words.
    Q_INVOKABLE static QString colourMapPreview(const QString& name,int width=96,int height=14);

    void setArrowPath(const QString& v);
    void setEngine(const QString& v);
    void setVariant(const QString& v);
    void setXColumn(const QString& v);
    void setYColumns(const QStringList& v);
    void setXUnit(const QString& v);
    void setYUnit(const QString& v);
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

protected:
    // Mouse, wheel and touch. A finger is not a second-class pointer here: one
    // finger pans, two pinch to zoom and drag together, and a double-tap
    // resets - none of which Qt's mouse synthesis could have given, because
    // there was no wheel handler and no middle button to synthesise onto.
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void touchEvent(QTouchEvent* e) override;
    // Hover, for the cursor readout. Separate from mouseMoveEvent because that
    // one fires only while a button is held - the readout is wanted exactly
    // when nothing is being dragged.
    void hoverMoveEvent(QHoverEvent* e) override;
    void hoverLeaveEvent(QHoverEvent* e) override;

public:
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
    void cursorChanged();
    void annotationsChanged();
    // A click landed on the figure while annotating, at these DATA coordinates.
    // QML asks for the text and calls addAnnotation; the canvas deliberately
    // does not put up a dialog of its own, because a renderer that opens
    // windows is a renderer that cannot be used headless.
    void annotationRequested(double x,double y);

private:
    // Seed the view from what is currently drawn, so the first drag or pinch
    // continues from the figure the user is looking at rather than from a
    // range invented here.
    void ensureView();
    // The axis ranges currently on screen, WITHOUT creating a view. ensureView
    // writes the range onto the axes and sets hasView_, which is what makes
    // viewZoomed true and the "Reset view" button appear - so a readout that
    // called it would make merely moving the pointer across the figure look
    // like a zoom the user had performed.
    bool currentView(double& xLo,double& xHi,bool& xLog,
                     double& yLo,double& yHi,bool& yLog) const;
    void updateCursor(const QPointF& pos);
    void panByPixels(double dx,double dy);
    void zoomAt(const QPointF& pos,double factor);
    void commitView();       // write the view onto the axes and redraw
    QRectF interactionArea() const;
    bool hasView_=false;
    QString cursorText_;
    bool cursorOnPlot_=false;
    double cursorX_=0.0, cursorY_=0.0;
    bool annotating_=false;
    QPointF lastPointer_;
    bool dragging_=false;
    // Fingers. The same reader the 3-D viewport uses - see TouchGesture.h for
    // why that is one class rather than two copies of the same arithmetic.
    TouchGesture touch_;

    void rebuild();          // reload data and regenerate the spec's series
    void applyVariant();     // catalogue scale variants: Semi-Log X, etc.
    void renderTo(QPainter* painter,const QRectF& target);

    QtPlotBackend qtBackend_;
    PlotSpec spec_;
    QString arrowPath_;
    QString xColumn_;
    QStringList yColumns_;
    QString xUnit_;
    QString yUnit_;
    QStringList available_;
    QString message_;
    bool engineSupported_=true;
    int pointCount_=0;
    int fullPointCount_=0;
    int colourVision_=0;
    // Recomputed in rebuild(), where the prepared spec already exists, so the
    // property is a read rather than a preparation each time QML binds to it.
    bool usesColourMap_=false;
    int fullRenderPolicy_=0;               // Automatic
    double fullRenderAskAfterSeconds_=5.0;
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
    // Milliseconds per point, learned from completed renders and used for the
    // "about N seconds" estimate. PER ENGINE, because the engines differ by
    // three orders of magnitude and one running average across all of them is
    // wrong for every one:
    //
    //     Line Chart      0.05 ms/point
    //     Derivative      2.4  ms/point      (measured, 16,000 points, 900x650)
    //
    // Not because Derivative computes anything - its preparation is 0.4 ms -
    // but because differentiating a measurement amplifies its noise, and the
    // curve that results crosses most of the plot height at every sample.
    // Qt's antialiased rasteriser charges for every one of those crossings.
    // A shared average meant the first Derivative render was announced as
    // taking a second and took forty.
    //
    // Keyed on the CATALOGUE engine, so a Spectrogram and a hand-built heatmap
    // are not averaged together just because both end up drawn as one.
    QHash<QString,double> msPerPoint_;
    static constexpr double kDefaultMsPerPoint=0.0025;
};

} // namespace graphvis
