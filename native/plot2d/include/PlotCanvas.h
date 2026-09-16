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
#include "ArrowTable.h"
#include "TouchGesture.h"
#include "QtPlotBackend.h"

#include <QQuickPaintedItem>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace graphvis {

class PlotCanvas : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT

// Q_INVOKABLE IN THIS REGION WAS PRIVATE, AND QML WILL NOT CALL A PRIVATE METHOD.
//
// This is a `class`, so everything between Q_OBJECT and the first `public:` -
// nearly three hundred lines of interface declarations - defaulted to private.
// Q_PROPERTY does not care: moc records properties with no access at all and
// they worked throughout. Q_INVOKABLE does: moc records the method's C++
// access, QML's lookup skips anything not public, and the call comes back as
// "Property 'x' of object PlotCanvas is not a function".
//
// Thirteen methods were declared here, and all thirteen are called from QML:
// setAxisLimits, clearAxisLimits, clearAllLimits, setColourLimits,
// clearColourLimits, setColourLevels, setCustomColour, addCustomColour,
// removeCustomColour, clearCustomColours, seedCustomColours,
// suggestedColourCount and expressionFunctions. So the axis-limit controls,
// the colour-scale limits, the banding and the whole custom-colours panel were
// dead at runtime - every button raising a TypeError into the log and doing
// nothing.
//
// Only expressionFunctions was noticed, because it is the one called from a
// binding that evaluates on load; the other twelve are in click handlers, so
// they failed silently unless somebody happened to be reading the log with the
// panel open. Found in graph-check/startup.log, confirmed against the
// metaobject rather than inferred: moc marks exactly these thirteen Private.
//
// Declaring the region public is the fix. It holds only Q_PROPERTY,
// Q_INVOKABLE and QML_ELEMENT - there is nothing here that was private on
// purpose.
public:
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
    // A COLUMN AGAINST ITS OWN AXIS ON THE RIGHT.
    //
    // The secondary-axis machinery - range, ticks, log, label, per-series
    // placement - has been complete for a long time, and eight engines used it
    // internally: a Pareto's cumulative percent, a Bode phase, an X-bar chart's
    // range. What there was no way to do was the ordinary thing, which is to
    // say "temperature on the left, pressure on the right" about two columns of
    // your own data. The whole feature was reachable only by choosing one of
    // those eight specific graphs.
    //
    // Empty means one y axis, which is the default and the common case.
    Q_PROPERTY(QString y2Column READ y2Column WRITE setY2Column NOTIFY sourceChanged)
    // Whether the role is worth offering for the engine now chosen. See
    // QtPlotBackend::honoursSecondaryAxis: measured by handing the engine a
    // series that asks for the right-hand axis and seeing whether it comes back
    // still asking, rather than by consulting a list of engine names.
    Q_PROPERTY(bool supportsSecondaryAxis READ supportsSecondaryAxis NOTIFY stateChanged)
    // How many columns the current engine actually wants, so the interface can
    // say so next to the axis rows rather than leaving it to be discovered.
    Q_PROPERTY(int columnsRequired READ columnsRequired NOTIFY stateChanged)
    // sourceChanged, which is what every column setter emits - so the count
    // updates the moment a mapping reaches the canvas.
    Q_PROPERTY(int columnsMapped READ columnsMapped NOTIFY sourceChanged)
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

    // THE FORMULA, for the seven engines that plot one rather than a dataset.
    //
    // PlotSpec has carried `expression` since those engines were written and
    // nothing could set it: no property, no field, no menu. So Function Plot
    // drew sin(x)*exp(-x/6), Function Surface drew sin(x)*cos(y), and all seven
    // drew their hard-coded demonstration formula whatever the person wanted -
    // an expression compiler, an RPN evaluator and a function table, reachable
    // only as a fixed picture.
    Q_PROPERTY(QString expression READ expression WRITE setExpression NOTIFY sourceChanged)
    // True only on those engines, so the interface offers the field where it
    // means something and nowhere else.
    Q_PROPERTY(bool usesExpression READ usesExpression NOTIFY stateChanged)
    // Whether what is typed COMPILES, and what is wrong with it if not. A
    // formula field that silently draws nothing when you mistype a bracket is
    // worse than no field at all.
    Q_PROPERTY(QString expressionError READ expressionError NOTIFY stateChanged)
    // Every function the expression engine understands, for the help beside the
    // field. Expression::knownFunctions built this list and nobody read it.
    Q_INVOKABLE static QStringList expressionFunctions();
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
    // The vertical, separately. 0 follows gridDensity with one fewer, which is
    // what a single number used to force on both axes - see
    // PlotStyle::gridDensityY for why one number was the wrong shape.
    Q_PROPERTY(int gridDensityY READ gridDensityY WRITE setGridDensityY NOTIFY styleChanged)
    // How a pie or donut says which slice is which: 0 legend, 1 labels on
    // the slices, 2 both, 3 neither. It said nothing at all before this.
    Q_PROPERTY(int pieLabels READ pieLabels WRITE setPieLabels NOTIFY styleChanged)
    // Which way round a polar figure is measured: 0 mathematical (zero at
    // the right, anticlockwise), 1 compass (zero at the top, clockwise). A
    // bearing means the second and a polar scatter means the first, so it is
    // the operator's choice - see PlotStyle::polarConvention.
    Q_PROPERTY(int polarConvention READ polarConvention WRITE setPolarConvention NOTIFY styleChanged)
    // What the legend does with a label too long for its box: 0 elide, 1 wrap.
    // The box is capped so a long label cannot cover the figure; whether the
    // label is then cut or allowed to run onto a second line is a reading
    // preference - see PlotStyle::legendLabels.
    Q_PROPERTY(int legendLabels READ legendLabels WRITE setLegendLabels NOTIFY styleChanged)
    // True only while the engine on screen is drawn in a circle, so the
    // interface can offer the angle convention where it means something.
    Q_PROPERTY(bool polarEngine READ polarEngine NOTIFY stateChanged)
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
    // Lines across the wireframe - see PlotStyle::meshDensity. Separate from
    // fieldResolution because how many lines you can see through is not how
    // finely the surface was sampled.
    Q_PROPERTY(int meshDensity READ meshDensity WRITE setMeshDensity NOTIFY styleChanged)
    // Whether this engine draws a wireframe, so the control can be offered
    // where it does something and greyed where it does not.
    Q_PROPERTY(bool wireframeEngine READ wireframeEngine NOTIFY stateChanged)

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
    // The map actually PAINTED, which is the chosen one unless a colour-vision
    // mode replaced it - see setColourVision for why it has to be able to. The
    // picker goes on showing the chosen map; the figure shows this one, and the
    // two are allowed to differ only because the interface says so, which is
    // what colourVisionNote is for.
    Q_PROPERTY(QString effectiveColourMap READ effectiveColourMap NOTIFY styleChanged)
    // One sentence for the interface: what the colour-vision setting did to the
    // colour map, INCLUDING when the answer is "nothing, it was already safe".
    // Saying nothing in that case is what made the setting look broken.
    Q_PROPERTY(QString colourVisionNote READ colourVisionNote NOTIFY styleChanged)
    // Show the figure as a reader with the chosen deficiency actually sees it.
    //
    // Everything else about this setting is a claim the person setting it
    // cannot check, because they have normal colour vision and the figure looks
    // much as it did. This turns the claim into something to look at. A VIEW
    // only - it never touches an export, because a PDF drawn through a
    // dichromat transform is not a safe figure, it is an unreadable one.
    Q_PROPERTY(bool colourVisionPreview READ colourVisionPreview WRITE setColourVisionPreview NOTIFY styleChanged)
    // True only while a preview is actually being applied, so the interface can
    // mark the figure as a simulation rather than the real thing.
    Q_PROPERTY(bool simulatingColourVision READ simulatingColourVision NOTIFY styleChanged)

    // ------------------------------------------------------------------
    // Axis limits and the colour scale, as a THING THE INTERFACE CAN SHOW.
    //
    // The limits themselves are not new: PlotAxis has carried min and max
    // since the port began and the 2-D frame has always honoured them. What
    // was missing was any way to SET them. A zoom could, by dragging, which is
    // fine for "a bit closer" and useless for "VHPR above ten is not what I am
    // looking at" - and on the 3-D family, which has no drag-zoom at all,
    // there was no way to say it and the painter ignored the fields anyway.
    //
    // One list rather than twelve properties, because what the interface wants
    // to draw is a row per axis and the number of rows depends on the engine:
    // a line chart has two, a 3-D scatter has three, a pie chart has none.
    // Each entry carries everything a row needs and nothing it has to work
    // out for itself:
    //
    //   role      0 x, 1 y, 2 the third column
    //   name      "X", "Y", "Z"
    //   label     the column, as the axis is labelled
    //   used      whether this engine reads this role at all
    //   dataMin   what the column actually spans - the travel of a slider
    //   dataMax
    //   min       the limit in force, or dataMin when it is automatic
    //   max
    //   autoMin   whether that end is fitted rather than chosen
    //   autoMax
    //
    // dataMin and dataMax are what makes a slider possible: a slider needs to
    // know what "all the way left" means, and no control can invent that.
    Q_PROPERTY(QVariantList axisRanges READ axisRanges NOTIFY axisRangesChanged)
    // The same shape for the colour ramp: dataMin, dataMax, min, max, auto*,
    // plus `levels` and whether the engine on screen uses a ramp at all.
    Q_PROPERTY(QVariantMap colourRange READ colourRange NOTIFY axisRangesChanged)

    // role is 0, 1 or 2 as above. An end passed as NaN - Number.NaN from QML -
    // means "fit this end to the data", which is how a field is emptied.
    Q_INVOKABLE void setAxisLimits(int role,double lo,double hi);
    Q_INVOKABLE void clearAxisLimits(int role);
    // Every axis and the colour ramp back to automatic, in one call, without
    // touching the camera or the notes.
    Q_INVOKABLE void clearAllLimits();
    Q_INVOKABLE void setColourLimits(double lo,double hi);
    Q_INVOKABLE void clearColourLimits();
    // 0 is a continuous ramp; 2..256 draws that many discrete bands.
    Q_INVOKABLE void setColourLevels(int levels);
    // Clamp or drop, for values outside the colour range. False clamps them to
    // the nearer end of the ramp, which is the default and what a capped scale
    // usually means; true leaves them undrawn.
    Q_PROPERTY(bool colourOutOfRangeDropped READ colourOutOfRangeDropped
               WRITE setColourOutOfRangeDropped NOTIFY axisRangesChanged)

    // The person's own colours, as "#aarrggbb" strings so QML can bind them
    // straight to a swatch and hand them to a colour dialog.
    //
    // Empty is every figure drawn before this existed. Non-empty overrides the
    // named map on a field and the colour-vision palette on a set of series or
    // pie sectors - see PlotStyle::customColours for why one list serves both.
    Q_PROPERTY(QVariantList customColours READ customColours NOTIFY axisRangesChanged)
    // True while the ramp on screen is a hand-picked one, so the interface can
    // say which of the two is in force without comparing lists.
    Q_PROPERTY(bool usingCustomColours READ usingCustomColours NOTIFY axisRangesChanged)

    Q_INVOKABLE void setCustomColour(int index,const QColor& colour);
    Q_INVOKABLE void addCustomColour(const QColor& colour);
    Q_INVOKABLE void removeCustomColour(int index);
    Q_INVOKABLE void clearCustomColours();
    // Fills the list from what is ON SCREEN RIGHT NOW - the current map
    // sampled at `count` points, or the current palette - so editing starts
    // from the figure the person is looking at rather than from an empty row.
    // Starting from nothing is what makes a colour editor feel like a second
    // job instead of an adjustment.
    Q_INVOKABLE void seedCustomColours(int count);
    // How many swatches the interface should offer by default: the band count
    // when the ramp is banded, the number of series or sectors when it is a
    // set of categories, and a readable handful otherwise.
    Q_INVOKABLE int suggestedColourCount() const;

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
    // True where the figure can be moved WITHIN its frame instead: no axis
    // range to slide, no camera to turn, so a drag and a wheel translate and
    // scale the drawing itself. Exactly the engines viewInteractive excludes
    // and view3D does not claim.
    Q_PROPERTY(bool frameInteractive READ frameInteractive NOTIFY stateChanged)
    // Whether that view has actually been moved, so the interface can offer a
    // way back only when there is something to go back from.
    Q_PROPERTY(bool frameMoved READ frameMoved NOTIFY stateChanged)

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
    // The cursor's DATA coordinates. Exposed because addAnnotation() takes
    // data coordinates and its own comment tells the caller to "use
    // cursorX/cursorY" - and the only caller is QML, which could not see
    // them. The advice was unfollowable for as long as they were plain
    // accessors.
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
    QString y2Column() const { return y2Column_; }
    bool supportsSecondaryAxis() const {
        // A multi-column engine reads its mapped columns as series 0, 1 and 2 -
        // a heatmap's x, y and value - so "this column on the right" is not a
        // question that means anything there, whatever the probe says.
        return !QtPlotBackend::columnPlan(spec_.engine).asSeries
               && qtBackend_.honoursSecondaryAxis(spec_.engine);
    }
    int columnsRequired() const { return QtPlotBackend::columnsRequired(spec_.engine); }
    // How many of the four mapped roles the canvas actually holds.
    //
    // Here rather than in QML because MappingPanel was counting them itself,
    // from its own reading of xColumn, yColumns, zColumn and colorColumn - a
    // second implementation of a question this object can answer, and the two
    // disagreed in front of the user: a correctly drawn heat map with
    // "reads 3 columns and 0 are mapped" printed beside it. The panel now asks
    // the object that draws the figure, so the sentence cannot contradict the
    // picture it is sitting next to.
    int columnsMapped() const {
        return (xColumn_.isEmpty()?0:1)+yColumns_.size()
              +(zColumn_.isEmpty()?0:1)+(colorColumn_.isEmpty()?0:1)
              +((y2Column_.isEmpty()||yColumns_.contains(y2Column_))?0:1);
    }
    void setZColumn(const QString& v);
    void setColorColumn(const QString& v);
    void setY2Column(const QString& v);
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
    QString expression() const { return spec_.expression; }
    void setExpression(const QString& text);
    bool usesExpression() const {
        return spec_.engine.startsWith(QLatin1String("Function"))
            || spec_.engine.startsWith(QLatin1String("Implicit"));
    }
    QString expressionError() const { return expressionError_; }
    bool logY() const { return spec_.yAxis.log10; }
    QString message() const { return message_; }
    QString notice() const { return notice_; }
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
    bool frameInteractive() const;
    bool frameMoved() const { return spec_.frameView.active(); }
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
    // The CHOSEN map, which is what the picker must go on showing even when a
    // colour-vision mode is painting a different one. Reading the painted map
    // back here would make the picker jump to Cividis the moment someone
    // selected a vision mode, and they would have lost their choice.
    QString colourMap() const { return chosenColourMap_; }
    QString effectiveColourMap() const { return spec_.style.colourMap; }
    QString colourVisionNote() const;
    bool colourVisionPreview() const { return colourVisionPreview_; }
    void setColourVisionPreview(bool on);
    bool simulatingColourVision() const;
    // Draws a finished image through the dichromat transform for the chosen
    // mode. Preview path only.
    void paintSimulated(QPainter* painter,const QRectF& target,const QImage& source) const;
    // Resolves chosenColourMap_ + colourVision_ into the painted map and the
    // figure note. Called by every setter that can change either of them.
    void applyColourVisionToMap();
    bool gridVisible() const { return spec_.style.gridVisible; }
    int gridDensity() const { return spec_.style.gridDensity; }
    int gridDensityY() const { return spec_.style.gridDensityY; }
    int pieLabels() const { return spec_.style.pieLabels; }
    int polarConvention() const { return spec_.style.polarConvention; }
    int legendLabels() const { return spec_.style.legendLabels; }
    bool polarEngine() const { return QtPlotBackend::isPolarEngine(spec_.engine); }
    void setGridVisible(bool on);
    void setGridDensity(int ticks);
    void setGridDensityY(int ticks);
    void setPieLabels(int mode);
    void setPolarConvention(int mode);
    void setLegendLabels(int mode);
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
    // `about` is where on the item the gesture happened, so the point under
    // the pointer stays under it. A null point means the centre, which is what
    // a button with no position gets.
    Q_INVOKABLE void zoom3DBy(double factor,const QPointF& about=QPointF());
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
    int meshDensity() const { return spec_.style.meshDensity; }
    bool wireframeEngine() const { return spec_.engine==QStringLiteral("3D Mesh")
                                       ||spec_.engine==QStringLiteral("Function Mesh"); }
    void setMeshDensity(int lines);
    void setFieldResolution(int cells);
    bool usesColourMap() const { return usesColourMap_; }
    QVariantList axisRanges() const;
    QVariantMap colourRange() const;
    bool colourOutOfRangeDropped() const { return spec_.style.colourOutOfRangeDropped; }
    void setColourOutOfRangeDropped(bool drop);
    QVariantList customColours() const;
    bool usingCustomColours() const { return !spec_.style.customColours.isEmpty(); }
    void setBackgroundColor(const QColor& c);
    void setForegroundColor(const QColor& c);
    void setGridColor(const QColor& c);
    void setColourMap(const QString& name);
    // The names setColourMap accepts, in the order they should be offered.
    // Here rather than in QML so the list cannot drift from the one the
    // renderer actually understands.
    // The same names grouped into the eight categories GraphVis 17 used, as
    // [{name, maps: [...]}, ...]. Eighty four names in one list is a list to
    // scroll, not a choice to make.
    // NOT static any more: the list depends on the colour-vision setting, so
    // it has to be able to read it. See the note on the implementation.
    Q_INVOKABLE QVariantList colourMapCategories() const;
    // WHY THIS MAP IS GREYED OUT, measured rather than listed.
    //
    // Empty when the map is usable by the colour-vision mode currently set on
    // this canvas; otherwise one sentence for the tooltip. The measurement
    // lives in ColourVision.h and runs over the map's own table, so a map
    // added to ColourMaps.h is judged the moment it exists - a hand-written
    // list of "bad maps" would go stale against the generated one and nothing
    // would notice.
    //
    // Not static, unlike its neighbours: the answer depends on which reader
    // this canvas is set up for.
    Q_INVOKABLE QString colourMapWarning(const QString& map) const;
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
    // The same raster render with the writer's quality dial exposed, and the
    // option of a transparent ground.
    //
    // exportPng above hands the image to QImage::save, which uses the format's
    // default and offers no way to change it - so a PNG for the web and a PNG
    // for a 600 dpi plate were written identically, and a JPEG could not be
    // told to stop being lossy. `quality` is 0-100 the way a person means it:
    // 100 is best. What that becomes is the format's business - a compression
    // LEVEL for PNG, where the scale runs the other way.
    Q_INVOKABLE bool exportRaster(const QString& filePath,int width,int height,
                                  int quality=90,bool transparent=false);
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
    // Separate from styleChanged so that dragging a limit slider does not make
    // every style-bound control in the interface re-evaluate sixty times a
    // second.
    void axisRangesChanged();
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
    // The MOUSE cursor, which is a different question from updateCursor's
    // coordinate readout despite the name they share. See the definition.
    void refreshCursor();
    void panByPixels(double dx,double dy);
    void zoomAt(const QPointF& pos,double factor);
    // The OTHER kind of view: the drawing moved inside its frame, for the
    // sixty-six engines with no axis range to slide. See PlotFrameView.
    void panFrameByPixels(double dx,double dy);
    void zoomFrameAt(const QPointF& pos,double factor);
    // The x range the figure is about to be DRAWN in, when it has been zoomed,
    // in the unit the axis is labelled in. The preview's point budget is spent
    // inside it rather than across the whole column - see the note above
    // buildPlotSeries in the .cpp for the report this comes from.
public:
    struct ViewWindow {
        bool set=false;
        double lo=0.0,hi=0.0;
        bool usable() const { return set&&hi>lo&&lo==lo&&hi==hi; }
    };
private:
    // asSeries engines are excluded: they grid their mapped columns and a grid
    // takes its extent from the data it is handed.
    ViewWindow windowForDrawing(bool asSeries) const;
    // And so is every engine whose x axis is not the mapped x column - a
    // histogram's bin values, a violin's slot number. See the definition.
    bool xAxisIsMappedColumn() const;
    QHash<QString,bool> windowUsable_;

    // The dataset, read once for the whole application and kept until the FILE
    // changes. See the note on the definition: a zoom now rebuilds the series,
    // and rebuilding used to re-read the whole Arrow file.
    //
    // The canvas keeps a reference to whichever table it last drew from, so a
    // worker still reading one cannot have it freed when the person opens a
    // different dataset.
    const ArrowTable& loadedTable() const;
    mutable std::shared_ptr<const ArrowTable> table_;
    // What the last rebuild decided about the engine, so the full-resolution
    // render asks the same question rather than a second copy of it.
    bool resolvedAsSeries_=false;

    // Write the view onto the axes and redraw.
    //
    // Gesture means "more of this is coming": draft quality on, the expensive
    // render held back until the idle timer says the gesture has stopped.
    // Settled means a single deliberate change - Reset view - which should have
    // its full-resolution render started at once. See the note on commitView
    // for the report this distinction comes from.
    enum class View { Settled, Gesture };
    void commitView(View how=View::Settled);
    QRectF interactionArea() const;
    bool hasView_=false;
    QString cursorText_;
    bool cursorOnPlot_=false;
    // Where the pointer last was ON the figure, kept after it leaves.
    //
    // The zoom buttons had nothing to zoom about, so they zoomed about the
    // middle - and the middle is almost never what somebody is looking at. The
    // pointer's own position is, and the reason it has to survive the pointer
    // LEAVING is that pressing the button is how it leaves: it moves off the
    // figure and onto the button before the click arrives. Clearing it on
    // hoverLeave would mean the button never saw anything but the centre.
    QPointF lastPointerOnPlot_;
    bool haveLastPointer_=false;
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
    QString y2Column_;
    QString xUnit_;
    QString yUnit_;
    QStringList available_;
    QString message_;
    QString notice_;
    bool engineSupported_=true;
    int pointCount_=0;
    int fullPointCount_=0;
    int colourVision_=0;
    // The map the PERSON chose, kept apart from spec_.style.colourMap, which
    // holds the map actually painted. They differ whenever a colour-vision mode
    // has substituted a safe map, and keeping both is what lets the choice
    // survive: turn the vision mode off again and the original map comes back,
    // rather than the substitute having quietly become the selection.
    QString chosenColourMap_;
    // Off by default: a simulation shown without being asked for would be a
    // program lying about what the figure looks like.
    bool colourVisionPreview_=false;
    // Set by a style change that needs the rebuild for its own reasons - the
    // series palette - to stop that rebuild discarding the person's zoom.
    // Cleared by rebuild() itself, so it can never leak into a data change.
    bool keepViewOnRebuild_=false;
    // Recomputed in rebuild(), where the prepared spec already exists, so the
    // property is a read rather than a preparation each time QML binds to it.
    bool usesColourMap_=false;
    // Empty when the formula compiles, or when there is none to compile.
    QString expressionError_;
    // What each mapped role SPANS, measured in rebuild() from the series that
    // were actually composed. The sliders' travel, and the value a field falls
    // back to when its end is automatic.
    double dataLo_[3]={0,0,0};
    double dataHi_[3]={1,1,1};
    bool dataSpanValid_[3]={false,false,false};
    bool roleUsed_[3]={false,false,false};
    QString roleLabel_[3];
    // Written by setAxisLimits, read by rebuild() so that a limit survives the
    // rebuild a style change triggers - and is dropped when the figure itself
    // changes, because a ceiling of ten on VHPR means nothing on another
    // column. See applyStoredLimits.
    double limitLo_[3]={0,0,0};
    double limitHi_[3]={0,0,0};
    bool limitLoSet_[3]={false,false,false};
    bool limitHiSet_[3]={false,false,false};
    // Re-colours the series in place from the chosen colours or the palette.
    // A full rebuild would re-read the Arrow file to change eight colours,
    // which on a 200,000-row table is a visible pause for a swatch click.
    void applySeriesColours();
    void applyStoredLimits();
    // Measured from the FULL column rather than from spec_.series, which the
    // preview budget has already thinned: a stride through 200,000 rows can
    // miss the largest value in the file, and a slider whose right-hand end is
    // not the top of the column is a slider that cannot select the top of the
    // column.
    void measureRoleSpans(const ArrowTable& table);
    int fullRenderPolicy_=0;               // Automatic
    double fullRenderAskAfterSeconds_=5.0;
    bool dirty_=true;

    // ---- full-resolution render, off the GUI thread
    // Whether a pending render should keep the picture already on screen.
    //
    // Every caller but one is an EDIT - a column, an engine, a colour map -
    // after which the rendered image is a picture of the previous settings and
    // has to go. A resize is the exception: the image is still a correct
    // picture of this figure, drawn at the wrong size, and discarding it is
    // what made the figure vanish for the length of a splitter drag.
    enum class Retain { Nothing, RenderedImage };
    void scheduleFullRender(Retain retain=Retain::Nothing);
    void startFullRender();
    void handleFullRenderFinished();
    void invalidateFullRender();

    QString resolvedX_;
    QStringList resolvedY_;
    // Which of resolvedY_ went on the right-hand axis, so that the
    // full-resolution render composes the same figure as the preview. A
    // preview with two axes and a final render with one is precisely the
    // shape of defect this file has been bitten by before.
    QString resolvedY2_;
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
    // A splitter drag, which is a resize and not a gesture on the canvas, so
    // neither dragging_ nor interacting_ covers it. Without this the figure was
    // redrawn at full quality at every intermediate size.
    QTimer resizeIdle_;
    bool resizing_=false;
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
