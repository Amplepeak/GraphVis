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
    // The other two mapped roles. A multi-column engine reads its inputs as
    // series - a heatmap's x, y and value; a 3-D scatter's x, y and z - so it
    // needs THREE or FOUR columns, and the interface has always offered four
    // (X, Y, Z, Colour). Only X and Y ever reached this canvas: the workspace
    // set yColumns to a single-element list and dropped the rest on the floor.
    // So a Line Chart drew and every engine wanting a third column drew a frame
    // and nothing in it, which is exactly what a user saw and reported as "all
    // of the visualisations don't show".
    Q_PROPERTY(QString zColumn READ zColumn WRITE setZColumn NOTIFY sourceChanged)
    Q_PROPERTY(QString colorColumn READ colorColumn WRITE setColorColumn NOTIFY sourceChanged)
    // How many columns the current engine actually wants, so the interface can
    // say so next to the axis rows rather than leaving it to be discovered.
    Q_PROPERTY(int columnsRequired READ columnsRequired NOTIFY stateChanged)
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
    // How each axis's values are transformed. 0 Linear, 1 Log10, 2 Log(1+x),
    // 3 z-score, 4 quantile - see AxisTransform in PlotSpec.h.
    //
    // Independent per axis, which is the point: a heavily skewed x against a
    // z-scored y is a perfectly ordinary thing to want and there was no way to
    // ask for it. linkAxisTransforms is for when it is not - setting either
    // then sets both.
    Q_PROPERTY(int xTransform READ xTransform WRITE setXTransform NOTIFY sourceChanged)
    Q_PROPERTY(int yTransform READ yTransform WRITE setYTransform NOTIFY sourceChanged)
    Q_PROPERTY(int zTransform READ zTransform WRITE setZTransform NOTIFY sourceChanged)
    // What the third mapped column IS on the engine currently chosen, so the
    // control can be labelled "Z scale" on a 3-D figure and "Colour scale" on a
    // heat map. Empty when the engine takes only two columns, which is how the
    // interface knows to leave the control out rather than offer a scale for an
    // axis that is not there.
    Q_PROPERTY(QString thirdAxisRole READ thirdAxisRole NOTIFY sourceChanged)
    Q_PROPERTY(bool linkAxisTransforms READ linkAxisTransforms WRITE setLinkAxisTransforms NOTIFY sourceChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    // Why the figure looks the way it does, when there is something to say -
    // a missing mapped column, an axis with five levels, one outlier holding
    // the range open. Empty when the picture is fine.
    //
    // Deliberately SEPARATE from message. They were one string, and the moment
    // the canvas learned to explain itself a two-line sentence went into a
    // status pill that sizes to its content and does not wrap - so it stretched
    // across the window, covered the Export button and the colour map picker,
    // and stayed there because the condition was permanent. A short status and
    // a long explanation are different things and belong in different places.
    Q_PROPERTY(QString notice READ notice NOTIFY stateChanged)
    Q_PROPERTY(bool engineSupported READ engineSupported NOTIFY stateChanged)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY stateChanged)
    // Figure colours follow the UI theme; without these the canvas
    // stayed dark inside a light theme.
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY styleChanged)
    Q_PROPERTY(QColor foregroundColor READ foregroundColor WRITE setForegroundColor NOTIFY styleChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY styleChanged)
    // The grid, on its own terms rather than the theme's. Some figures are
    // read off the grid and want it fine; a figure going into a paper usually
    // wants it gone. Neither is a property of the interface's colour scheme,
    // which is what it used to be tied to.
    Q_PROPERTY(bool gridVisible READ gridVisible WRITE setGridVisible NOTIFY styleChanged)
    Q_PROPERTY(int gridDensity READ gridDensity WRITE setGridDensity NOTIFY styleChanged)
    // Numbers on the axes. On, because an axis with a name and no scale can be
    // looked at but not read - which is what every 3-D figure was.
    Q_PROPERTY(bool scaleLabelsVisible READ scaleLabelsVisible WRITE setScaleLabelsVisible NOTIFY styleChanged)

    // The camera, for the engines drawn as a projection. Separate from the 2-D
    // pan and zoom because turning a cube and sliding an axis range are not the
    // same operation and must not share a gesture.
    Q_PROPERTY(bool view3D READ view3D NOTIFY stateChanged)
    Q_PROPERTY(double azimuth READ azimuth WRITE setAzimuth NOTIFY styleChanged)
    Q_PROPERTY(double elevation READ elevation WRITE setElevation NOTIFY styleChanged)
    // How close the camera sits. zoom3DBy multiplies it, which a wheel wants
    // and a slider cannot use: a control that can only say "a bit more" has no
    // position to show. Same bounds as zoom3DBy applies.
    Q_PROPERTY(double cameraZoom READ cameraZoom WRITE setCameraZoom NOTIFY styleChanged)
    Q_PROPERTY(int fieldInterpolation READ fieldInterpolation WRITE setFieldInterpolation NOTIFY styleChanged)
    // The scattered estimator and its policies. -1 on fieldEstimator keeps the
    // grid-filling path, which is what every existing figure uses.
    Q_PROPERTY(int fieldEstimator READ fieldEstimator WRITE setFieldEstimator NOTIFY styleChanged)
    Q_PROPERTY(int fieldExtrapolation READ fieldExtrapolation WRITE setFieldExtrapolation NOTIFY styleChanged)
    Q_PROPERTY(int fieldValuePolicy READ fieldValuePolicy WRITE setFieldValuePolicy NOTIFY styleChanged)
    Q_PROPERTY(int fieldResponseSpace READ fieldResponseSpace WRITE setFieldResponseSpace NOTIFY styleChanged)
    Q_PROPERTY(int fieldNeighbours READ fieldNeighbours WRITE setFieldNeighbours NOTIFY styleChanged)
    Q_PROPERTY(double fieldIdwPower READ fieldIdwPower WRITE setFieldIdwPower NOTIFY styleChanged)
    Q_PROPERTY(double fieldSmoothing READ fieldSmoothing WRITE setFieldSmoothing NOTIFY styleChanged)
    Q_PROPERTY(int fieldFootprint READ fieldFootprint WRITE setFieldFootprint NOTIFY styleChanged)
    Q_PROPERTY(int fieldBridging READ fieldBridging WRITE setFieldBridging NOTIFY styleChanged)
    Q_PROPERTY(int fieldBridgeMaxCells READ fieldBridgeMaxCells WRITE setFieldBridgeMaxCells NOTIFY styleChanged)
    Q_PROPERTY(int fieldInvalidDisplay READ fieldInvalidDisplay WRITE setFieldInvalidDisplay NOTIFY styleChanged)
    Q_PROPERTY(int fieldKrigingVariogram READ fieldKrigingVariogram WRITE setFieldKrigingVariogram NOTIFY styleChanged)
    Q_PROPERTY(double fieldLoessFraction READ fieldLoessFraction WRITE setFieldLoessFraction NOTIFY styleChanged)
    Q_PROPERTY(int fieldResolution READ fieldResolution WRITE setFieldResolution NOTIFY styleChanged)

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
    QString zColumn() const { return zColumn_; }
    QString colorColumn() const { return colorColumn_; }
    int columnsRequired() const { return QtPlotBackend::columnsRequired(spec_.engine); }
    void setZColumn(const QString& v);
    void setColorColumn(const QString& v);
    QString xUnit() const { return xUnit_; }
    QString yUnit() const { return yUnit_; }
    QString xSourceUnit() const;
    QString ySourceUnit() const;
    // Every unit this column's own unit can be converted to, target names only.
    // Empty for a column with no unit in its label, which is the common case.
    Q_INVOKABLE QStringList unitOptions(const QString& column) const;

    // The constants the current engine declares, each with its current value,
    // so the interface can build one control per row without knowing what any
    // of them mean. Empty for all but a few engines.
    //
    // A list of maps rather than a typed model deliberately: this is the whole
    // interface between "an engine needs a number" and "there is a box to type
    // it in", and adding a parameter must not need a change on the QML side.
    // Notified when the DECLARATION changes - a new engine, a reopened figure,
    // a reset - and deliberately not when a value is typed. The model backs a
    // Repeater, so emitting on every edit would destroy and rebuild the box
    // being typed in the instant it was committed, and tabbing from one mass to
    // the next would land nowhere.
    Q_PROPERTY(QVariantList engineParameters READ engineParameterList NOTIFY engineParametersChanged)
    QVariantList engineParameterList() const;
    // The values alone, for the figure package.
    QVariantMap engineParameterValues() const;
    Q_INVOKABLE void setEngineParameter(const QString& key,double value);
    Q_INVOKABLE double engineParameter(const QString& key) const;
    // Back to what the engine declares. Someone who has typed four masses into
    // the wrong figure needs one click, not four corrections.
    Q_INVOKABLE void resetEngineParameters();

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
    QString notice() const { return notice_; }
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
    // Which note is under this point in the canvas's own coordinates, or -1.
    // Matched against the rectangles the last render recorded rather than
    // against a second copy of the placement rules.
    Q_INVOKABLE int annotationAt(double px,double py) const;
    Q_INVOKABLE void removeAnnotation(int index);
    Q_INVOKABLE void clearAnnotations();
    // Back to fitting the data. Also what a double-click and a double-tap do.
    Q_INVOKABLE void resetView();
    // For a QML button or a keyboard shortcut: >1 zooms in, about the centre.
    Q_INVOKABLE void zoomBy(double factor);
    QColor gridColor() const { return spec_.style.gridColor; }
    QString colourMap() const { return spec_.style.colourMap; }
    bool gridVisible() const { return spec_.style.gridVisible; }
    int gridDensity() const { return spec_.style.gridDensity; }
    void setGridVisible(bool on);
    void setGridDensity(int ticks);
    bool scaleLabelsVisible() const { return spec_.style.scaleLabelsVisible; }
    void setScaleLabelsVisible(bool on);
    // True for the engines drawn as a projection into a cube: the 3-D family,
    // the surfaces, and the vector and volume fields. Those rotate rather than
    // pan, and QML asks so it can say so in the tooltip.
    bool view3D() const;
    double azimuth() const { return spec_.view3d.azimuth; }
    double elevation() const { return spec_.view3d.elevation; }
    double cameraZoom() const { return spec_.view3d.zoom; }
    void setCameraZoom(double factor);
    void setAzimuth(double degrees);
    void setElevation(double degrees);
    // Drag: horizontal turns, vertical tilts. Pixels, so the caller does not
    // have to know the gain.
    Q_INVOKABLE void rotateByPixels(double dx,double dy);
    // Repaint now, and schedule the expensive render only once the drag is over.
    void cameraMoved();
    Q_INVOKABLE void zoom3DBy(double factor);
    Q_INVOKABLE void resetCamera();
    // How the unsampled cells of a gridded field are estimated, and how fine
    // that grid is. See PlotStyle::fieldInterpolation.
    int fieldInterpolation() const { return spec_.style.fieldInterpolation; }
    int fieldEstimator() const { return spec_.style.fieldEstimator; }
    int fieldExtrapolation() const { return spec_.style.fieldExtrapolation; }
    int fieldValuePolicy() const { return spec_.style.fieldValuePolicy; }
    int fieldResponseSpace() const { return spec_.style.fieldResponseSpace; }
    int fieldNeighbours() const { return spec_.style.fieldNeighbours; }
    double fieldIdwPower() const { return spec_.style.fieldIdwPower; }
    double fieldSmoothing() const { return spec_.style.fieldSmoothing; }
    int fieldFootprint() const { return spec_.style.fieldFootprint; }
    int fieldBridging() const { return spec_.style.fieldBridging; }
    int fieldBridgeMaxCells() const { return spec_.style.fieldBridgeMaxCells; }
    int fieldInvalidDisplay() const { return spec_.style.fieldInvalidDisplay; }
    void setFieldFootprint(int v);
    void setFieldBridging(int v);
    void setFieldBridgeMaxCells(int v);
    void setFieldInvalidDisplay(int v);
    int fieldKrigingVariogram() const { return spec_.style.fieldKrigingVariogram; }
    double fieldLoessFraction() const { return spec_.style.fieldLoessFraction; }
    void setFieldKrigingVariogram(int v);
    void setFieldLoessFraction(double v);
    void setFieldEstimator(int v);
    void setFieldExtrapolation(int v);
    void setFieldValuePolicy(int v);
    void setFieldResponseSpace(int v);
    void setFieldNeighbours(int v);
    void setFieldIdwPower(double v);
    void setFieldSmoothing(double v);
    // The names, and which of them this build computes, so a picker cannot
    // drift from the module.
    Q_INVOKABLE static QStringList fieldEstimatorNames();
    Q_INVOKABLE static QVariantList fieldEstimatorList();
    Q_INVOKABLE static QStringList fieldExtrapolationNames();
    Q_INVOKABLE static QStringList fieldFootprintNames();
    Q_INVOKABLE static QStringList fieldBridgingNames();
    Q_INVOKABLE static QStringList fieldInvalidDisplayNames();
    Q_INVOKABLE static QStringList fieldKrigingVariogramNames();
    Q_INVOKABLE static QStringList fieldValuePolicyNames();
    Q_INVOKABLE static QStringList fieldResponseSpaceNames();
    void setFieldInterpolation(int mode);
    int fieldResolution() const { return spec_.style.fieldResolution; }
    void setFieldResolution(int cells);
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
    int xTransform() const { return spec_.xAxis.transform; }
    int yTransform() const { return spec_.yAxis.transform; }
    int zTransform() const { return spec_.zAxis.transform; }
    void setXTransform(int mode);
    void setYTransform(int mode);
    void setZTransform(int mode);
    QString thirdAxisRole() const;
    bool linkAxisTransforms() const { return linkTransforms_; }
    void setLinkAxisTransforms(bool on);
    // The names, in order, so a picker does not have to keep its own copy that
    // can drift from the enum.
    Q_INVOKABLE static QStringList axisTransformNames(){
        return {QStringLiteral("Linear"),
                QStringLiteral("Log10"),
                QStringLiteral("Log(1 + x)"),
                QStringLiteral("Z-score"),
                QStringLiteral("Quantile")};
    }

    // Numeric columns found in the active dataset, for the mapping UI.
    // True vector PDF with embedded fonts, drawn by the same backend code.
    Q_INVOKABLE bool exportPdf(const QString& filePath,double widthIn=6.0,double heightIn=4.0,int dpi=600);
    Q_INVOKABLE bool exportPng(const QString& filePath,int width=1600,int height=1000);
    Q_INVOKABLE bool exportSvg(const QString& filePath,double widthIn=6.0,double heightIn=4.0);

    // One entry point that picks the right one from the file's extension.
    //
    // Three export functions existed and QML called exportPdf from one button,
    // so PDF was the only format anyone could actually reach - and PNG, which
    // was already written and working, may as well not have been. This is what
    // a Save Figure dialog needs: hand it whatever the person typed and let the
    // format follow the name they gave it.
    //
    // .pdf, .svg and .eps are vector and stay editable in Illustrator,
    // Inkscape or CorelDRAW. .png, .jpg, .jpeg, .tif, .tiff, .bmp and .webp are
    // raster, drawn at whatever size is asked for rather than scaled up from
    // the window. .py writes the matplotlib script that redraws the figure,
    // .csv the numbers behind it - the two that let someone else check the
    // figure rather than only look at it.
    Q_INVOKABLE bool exportFigure(const QString& filePath,int width=1600,int height=1000);
    // The formats this build can actually write, extensions only, in the order
    // a Save dialog should offer them. Raster support depends on which of Qt's
    // image plugins were deployed, so this asks rather than assumes: offering
    // TIFF and then failing silently is worse than not offering it.
    Q_INVOKABLE QStringList exportFormats() const;
    // The numbers the figure is drawn from, as CSV. One row per point, one
    // block per series.
    Q_INVOKABLE bool exportData(const QString& filePath) const;

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
    void engineParametersChanged();
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
    // A click landed on an existing note while annotating. QML opens its editor,
    // where it can be re-worded or deleted. A DRAG on a note moves it instead
    // and emits nothing, because moving is finished when the button comes up.
    void annotationPicked(int index);

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
    bool linkTransforms_=false;
    // Parameter values per engine, so switching to another engine and back
    // returns the masses that were typed rather than the declared defaults.
    // Session-lived; a figure saved to a .gvfig carries its own copy.
    QHash<QString,QMap<QString,double>> engineParams_;
    // Put the declared defaults, or what was last typed for this engine, onto
    // the spec. Called whenever the engine changes.
    void adoptEngineParameters();

    void rebuild();          // reload data and regenerate the spec's series
    void applyVariant();     // catalogue scale variants: Semi-Log X, etc.
    void renderTo(QPainter* painter,const QRectF& target);

    QtPlotBackend qtBackend_;
    // A note being dragged, and where the press started. -1 when none is.
    int noteDrag_=-1;
    bool noteMoved_=false;
    QPointF notePress_;
    QPointF noteStart_;
    PlotSpec spec_;
    QString arrowPath_;
    QString xColumn_;
    QStringList yColumns_;
    QString zColumn_;
    QString colorColumn_;
    QString xUnit_;
    QString yUnit_;
    QStringList available_;
    QString message_;
    QString notice_;
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
    // Interaction is not the same thing as a held mouse button. A wheel zoom
    // has no press and no release, and a touch flick is over before the figure
    // has finished moving - so `dragging_` alone left the expensive path in use
    // for exactly the gestures that need the cheap one. This is set by every
    // camera and view change and cleared a fifth of a second after the last
    // one, which is also when the full-resolution render is asked for; a
    // second wheel notch inside that window restarts it rather than starting
    // another render nobody will see.
    bool interacting_=false;
    QTimer interactionIdle_;
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
