#pragma once
#include <functional>
#include <QObject>
#include <QHash>
#include <QRect>
#include <QSet>
#include <QStringList>
#include <QFutureWatcher>
#include <QDateTime>
#include <QProcess>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include "NativeViewportWindow.h"
#include "ProjectWorkspace.h"

class AppController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QWindow* viewportWindow READ viewportWindow CONSTANT)
    Q_PROPERTY(QVariantList datasets READ datasets NOTIFY stateChanged)
    Q_PROPERTY(QString activeDatasetId READ activeDatasetId WRITE setActiveDatasetId NOTIFY activeDatasetChanged)
    Q_PROPERTY(QString activeArrowPath READ nativeArrowPath NOTIFY activeDatasetChanged)
    // The SQL table name the native core registered this dataset under.
    // Without it the SQL box defaults to a table that does not exist.
    Q_PROPERTY(QString activeTableName READ activeTableName NOTIFY activeDatasetChanged)
    Q_PROPERTY(QStringList activeColumns READ activeColumns NOTIFY activeDatasetChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString workspaceName READ workspaceName NOTIFY stateChanged)
    Q_PROPERTY(QString rendererMode READ rendererMode WRITE setRendererMode NOTIFY rendererModeChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString busyLabel READ busyLabel NOTIFY busyChanged)
    Q_PROPERTY(QString sqlResult READ sqlResult NOTIFY sqlResultChanged)
    Q_PROPERTY(bool experimentalUi READ experimentalUi WRITE setExperimentalUi NOTIFY experimentalUiChanged)
    Q_PROPERTY(QString workspaceMode READ workspaceMode WRITE setWorkspaceMode NOTIFY workspaceModeChanged)
    Q_PROPERTY(QUrl literatureUrl READ literatureUrl NOTIFY literatureChanged)
    Q_PROPERTY(QVariantMap literatureAnalysis READ literatureAnalysis NOTIFY literatureChanged)
    Q_PROPERTY(bool scienceServiceAvailable READ scienceServiceAvailable NOTIFY scienceServiceAvailabilityChanged)
    // Every add-dataset action leaves a row here - queued, converting,
    // importing, loaded or failed, with the reason. A status line that flashes
    // past is not evidence that anything happened.
    Q_PROPERTY(QVariantList importLog READ importLog NOTIFY importLogChanged)
    // The open project, if any. A folder on disk that is rescanned every start,
    // which is what makes a dataset added in March still be listed in September.
    Q_PROPERTY(ProjectWorkspace* project READ project CONSTANT)
    // Colour vision for plots, held apart from the UI theme on purpose: someone
    // who needs a deuteranopia-safe figure needs it whatever the interface looks
    // like, and needs it to stay set. Persisted; see native/plot2d ColourVision.h.
    Q_PROPERTY(int plotColourVision READ plotColourVision WRITE setPlotColourVision NOTIFY plotColourVisionChanged)
    Q_PROPERTY(QStringList plotColourVisionNames READ plotColourVisionNames CONSTANT)
    Q_PROPERTY(QString plotColourVisionSummary READ plotColourVisionSummary NOTIFY plotColourVisionChanged)
    // The field colour map and what happens when a full-resolution render
    // lands. Both belong to the person, not to a figure: someone who wants the
    // full render to appear by itself wants that on the next dataset too, and
    // someone working in Cividis is doing so for a reason that outlives one
    // plot. Persisted here and applied to the canvas by QML.
    Q_PROPERTY(QString plotColourMap READ plotColourMap WRITE setPlotColourMap NOTIFY plotDisplayChanged)
    Q_PROPERTY(int fullRenderPolicy READ fullRenderPolicy WRITE setFullRenderPolicy NOTIFY plotDisplayChanged)
    Q_PROPERTY(double fullRenderAskAfterSeconds READ fullRenderAskAfterSeconds WRITE setFullRenderAskAfterSeconds NOTIFY plotDisplayChanged)
    Q_PROPERTY(QStringList fullRenderPolicyNames READ fullRenderPolicyNames CONSTANT)
    // The FIGURE's background, which is not the interface's.
    //
    //   0 Follow the interface theme (what it always did)
    //   1 Dark      2 Light      3 White
    //
    // A figure going into a paper is white whatever the person likes to work
    // in, and someone working at night wants a dark interface around a white
    // plot without switching the whole application. Tying the two together made
    // one of those impossible.
    Q_PROPERTY(int figureTheme READ figureTheme WRITE setFigureTheme NOTIFY plotDisplayChanged)
    Q_PROPERTY(QStringList figureThemeNames READ figureThemeNames CONSTANT)
    // The grid, on its own terms. 0 density means the long-standing default.
    Q_PROPERTY(bool plotGridVisible READ plotGridVisible WRITE setPlotGridVisible NOTIFY plotDisplayChanged)
    Q_PROPERTY(int plotGridDensity READ plotGridDensity WRITE setPlotGridDensity NOTIFY plotDisplayChanged)
    // Numbers on the axes. On by default: an axis with a name and no scale can
    // be looked at and not read.
    Q_PROPERTY(bool plotScaleLabels READ plotScaleLabels WRITE setPlotScaleLabels NOTIFY plotDisplayChanged)
    // How the unsampled cells of a gridded field are estimated. Scattered
    // measurements almost never fall one to a grid cell, and drawing only the
    // cells that were hit gives coloured confetti rather than a map. See
    // PlotStyle::fieldInterpolation.
    Q_PROPERTY(int plotFieldInterpolation READ plotFieldInterpolation WRITE setPlotFieldInterpolation NOTIFY plotDisplayChanged)
    // The shape of the window. Six of them, because six different ways of
    // working came out of the design comparison and there was no reason to
    // throw five away: the engine does not care what the window looks like.
    //
    //   0 Notebook       several figures as a document        (the default)
    //   1 Inspector      one scrolling column on the right, no tabs
    //   2 Command bar    almost no chrome; everything through a search box
    //   3 Workflow rail  data, then graph, then figure, left to right
    //   4 Ribbon         a contextual band across the top
    //   5 Studio         full-bleed canvas with the controls floating over it
    Q_PROPERTY(int uiLayout READ uiLayout WRITE setUiLayout NOTIFY plotDisplayChanged)
    Q_PROPERTY(QStringList uiLayoutNames READ uiLayoutNames CONSTANT)
    Q_PROPERTY(QStringList uiLayoutDescriptions READ uiLayoutDescriptions CONSTANT)
    // True when the current layout draws several figures at once. Read by the
    // workspace; kept as its own name because that is the question it asks.
    Q_PROPERTY(bool notebookLayout READ notebookLayout NOTIFY plotDisplayChanged)
    Q_PROPERTY(QStringList plotFieldInterpolationNames READ plotFieldInterpolationNames CONSTANT)

    // What was open last time.
    //
    // Quitting forgot the dataset entirely, so every session began by finding
    // the same file in the same folder again - and a renderer that took the
    // application down with it did the same, which is the moment you least want
    // to be hunting for a path. Paths and names only; nothing reopens without
    // being asked for.
    Q_PROPERTY(QVariantList recentDatasets READ recentDatasets NOTIFY recentsChanged)
    Q_PROPERTY(QVariantList recentVisualisations READ recentVisualisations NOTIFY recentsChanged)
    Q_PROPERTY(bool dataFormatsInstallerAvailable READ dataFormatsInstallerAvailable CONSTANT)
    Q_PROPERTY(QVariantMap smartRenderPlan READ smartRenderPlan NOTIFY smartRenderChanged)
    // GraphVis 17 graph catalogue: 318 entries in 30 categories, loaded from
    // config/graph_catalogue.json (embedded as a QML module resource).
    // Smart Suite / Scan Dataset (GraphVis 17 intelligent_scan), run through
    // the optional Python science service.
    Q_PROPERTY(QVariantList scanRecommendations READ scanRecommendations NOTIFY scanChanged)
    Q_PROPERTY(QVariantMap scanSummary READ scanSummary NOTIFY scanChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanChanged)
    // Last batch run: successes, failures, the report path and any items.
    Q_PROPERTY(QVariantMap batchResult READ batchResult NOTIFY batchChanged)
    Q_PROPERTY(QVariantList batchItems READ batchItems NOTIFY batchChanged)
    // The solver bridge: an external computational engine running as a
    // cancellable job, with the tail of its own output while it runs.
    Q_PROPERTY(bool solverRunning READ solverRunning NOTIFY solverChanged)
    Q_PROPERTY(QString solverOutput READ solverOutput NOTIFY solverChanged)
    Q_PROPERTY(QVariantMap solverResult READ solverResult NOTIFY solverChanged)
    // The optional science components, and whether each is installed. Read
    // from tools/optional-components.json by the same script the installer
    // uses, so the list cannot drift between the two.
    Q_PROPERTY(QVariantList optionalComponents READ optionalComponents NOTIFY componentsChanged)
    Q_PROPERTY(bool componentsBusy READ componentsBusy NOTIFY componentsChanged)
    Q_PROPERTY(QString componentsOutput READ componentsOutput NOTIFY componentsChanged)
    // True once an opened paper has been analysed, so its variables and
    // plot intent can steer the scan (Phase 4).
    Q_PROPERTY(bool literatureContextAvailable READ literatureContextAvailable NOTIFY literatureChanged)
    // The reply from the most recent analysis.* or surface.* operation, exactly
    // as the service sent it. The panels read what they need out of it rather
    // than each result type needing its own property.
    Q_PROPERTY(QVariantMap analysisResult READ analysisResult NOTIFY analysisChanged)
    Q_PROPERTY(QString analysisKind READ analysisKind NOTIFY analysisChanged)
    Q_PROPERTY(bool analysisOk READ analysisOk NOTIFY analysisChanged)
    // Every analysis operation the service offers, asked for rather than
    // listed here. The panel used to carry its own list of eight, which is how
    // it came to offer six that did not exist - the service is the only thing
    // that knows what it can actually do.
    Q_PROPERTY(QVariantList analysisCatalogue READ analysisCatalogue NOTIFY analysisCatalogueChanged)
    // Persisted UI theme index. Kept in C++ so QML needs no QtCore module,
    // whose absence from the deployed layout stopped the app from starting.
    Q_PROPERTY(int themeIndex READ themeIndex WRITE setThemeIndex NOTIFY themeIndexChanged)
    // How the window opens: 0 windowed, 1 maximised, 2 fullscreen,
    // 3 borderless. Persisted alongside the theme.
    Q_PROPERTY(int displayMode READ displayMode WRITE setDisplayMode NOTIFY displayModeChanged)
    Q_PROPERTY(QStringList displayModeNames READ displayModeNames CONSTANT)
    // NOT constant any more: switching a catalogue pack off rebuilds all three
    // of these, and a CONSTANT property never tells QML it changed - the
    // library would go on listing categories it no longer holds.
    Q_PROPERTY(QVariantList graphCategories READ graphCategories NOTIFY graphCatalogueChanged)
    Q_PROPERTY(int graphEntryCount READ graphEntryCount NOTIFY graphCatalogueChanged)
    // How many entries the "Include advanced" switch actually reveals. The
    // library used to print `graphEntryCount - 82`, a constant that any
    // catalogue edit makes wrong and a smaller catalogue makes negative.
    Q_PROPERTY(int advancedEntryCount READ advancedEntryCount NOTIFY graphCatalogueChanged)

    // The catalogue packs, and how many entries each holds.
    //
    // A pack is a filter over a catalogue that ships whole - every engine is
    // compiled in either way - so this is about what the library SHOWS, not
    // about what is installed. All on by default; Core cannot be switched off.
    Q_PROPERTY(QVariantList graphPacks READ graphPacks NOTIFY graphCatalogueChanged)
    Q_PROPERTY(int hiddenEntryCount READ hiddenEntryCount NOTIFY graphCatalogueChanged)
public:
    explicit AppController(QObject* parent=nullptr);
    ~AppController() override;
    QWindow* viewportWindow(){return &viewport_;}
    QVariantList datasets() const{return datasets_;}
    QString activeDatasetId() const{return activeDatasetId_;}
    QStringList activeColumns() const;
    QString status() const{return status_;}
    QString workspaceName() const{return workspaceName_;}
    QString rendererMode() const{return rendererMode_;}
    bool busy() const{return busy_;}
    QString busyLabel() const{return busyLabel_;}
    QString sqlResult() const{return sqlResult_;}
    bool experimentalUi() const{return experimentalUi_;}
    QString workspaceMode() const{return workspaceMode_;}
    QUrl literatureUrl() const{return literatureUrl_;}
    QVariantMap literatureAnalysis() const{return literatureAnalysis_;}
    QVariantMap analysisResult() const{return analysisResult_;}
    QString analysisKind() const{return analysisKind_;}
    bool analysisOk() const{return analysisResult_.value(QStringLiteral("ok")).toBool();}
    QVariantList analysisCatalogue() const{return analysisCatalogue_;}

    // One entry point for every analysis operation the service exposes:
    // limits, forecast, fft, weibull, regression, pca, doe, advisor, domain.
    // `options` is merged into the request, so a caller passes only what that
    // operation needs (predictors for a regression, bounds for a design).
    Q_INVOKABLE bool runAnalysis(const QString& kind,const QVariantMap& options=QVariantMap());
    // Ask the service what it can do. Answered once per session and cached,
    // because it is the same answer every time.
    Q_INVOKABLE void refreshAnalysisCatalogue();
    // Scattered x/y/z to a regular grid through one of the sixteen estimators.
    // The result is an ordinary three-column Arrow file, so it imports and
    // draws like any other dataset.
    Q_INVOKABLE bool estimateSurface(const QString& x,const QString& y,const QString& z,
                                     const QString& estimator,int resolution=160,
                                     const QString& imputation=QString());
    bool scienceServiceAvailable() const;
    QVariantMap smartRenderPlan() const{return smartRenderPlan_;}
    QVariantList scanRecommendations() const{return scanRecommendations_;}
    QVariantMap scanSummary() const{return scanSummary_;}
    bool scanning() const{return scanning_;}
    QVariantMap batchResult() const{return batchResult_;}
    QVariantList batchItems() const{return batchItems_;}
    bool solverRunning() const{return solverProcess_.state()!=QProcess::NotRunning;}
    QString solverOutput() const{return solverTail_.join(QLatin1Char('\n'));}
    QVariantMap solverResult() const{return solverResult_;}
    QVariantList optionalComponents() const{return components_;}
    bool componentsBusy() const{return componentProcess_.state()!=QProcess::NotRunning;}
    QString componentsOutput() const{return componentTail_.join(QLatin1Char('\n'));}
    int themeIndex() const{return themeIndex_;}
    int displayMode() const{return displayMode_;}
    void setDisplayMode(int value);
    QStringList displayModeNames() const;
    // Window placement. Qt centres a sized window on the whole virtual desktop,
    // which on a two-monitor machine straddles the gap between the screens.
    // These pick one real screen and stay inside its work area instead.
    Q_INVOKABLE QRect preferredWindowGeometry(int width,int height) const;
    Q_INVOKABLE QRect targetScreenGeometry() const;
    Q_INVOKABLE QRect targetWorkAreaGeometry() const;
    Q_INVOKABLE void saveWindowGeometry(int x,int y,int width,int height);
    bool literatureContextAvailable() const;
    void setThemeIndex(int value);
    QVariantList graphCategories() const{return graphCategories_;}
    QVariantList graphPacks() const{return graphPacks_;}
    int hiddenEntryCount() const{return hiddenEntryCount_;}
    Q_INVOKABLE bool packEnabled(const QString& id) const;
    Q_INVOKABLE void setPackEnabled(const QString& id,bool on);
    int graphEntryCount() const{return graphEntries_.size();}
    int advancedEntryCount() const{
        int n=0;
        for(const QVariant& v:graphEntries_)
            if(v.toMap().value(QStringLiteral("advanced")).toBool()) ++n;
        return n;
    }

    void setActiveDatasetId(const QString& id);
    void setRendererMode(const QString& value);
    void setExperimentalUi(bool value);
    void setWorkspaceMode(const QString& value);

    Q_INVOKABLE bool importDataset(const QUrl& url);
    // Same reason as ProjectWorkspace::openProjectPath: a native path with a
    // space or a backslash does not survive being concatenated into a URL.
    Q_INVOKABLE bool importDatasetPath(const QString& path);
    // Qt file-dialog nameFilters covering every format the importer
    // handles. Mirrors services/python/graphvis_science/data/importer.py.
    Q_INVOKABLE QStringList importNameFilters() const;
    Q_INVOKABLE bool applyMapping(const QString& x,const QString& y,const QString& z,const QString& color,double pointSize,double opacity,bool invertOpacity,int voxelBins,bool smartRender=true,int smartProfile=1);
    Q_INVOKABLE void setPointStyle(double pointSize,double opacity,bool invertOpacity);
    Q_INVOKABLE QString runSql(const QString& sql);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void resetCamera(){viewport_.resetCamera();}
    Q_INVOKABLE QVariantMap activeDataset() const;
    Q_INVOKABLE QString nativeArrowPath() const;
    QString activeTableName() const;
    Q_INVOKABLE void refreshState();
    Q_INVOKABLE QString pluginServicePath(const QString& language) const;
    Q_INVOKABLE void openLiterature(const QUrl& url);
    Q_INVOKABLE void openLiteraturePath(const QString& path);
    Q_INVOKABLE void clearLiterature();
    Q_INVOKABLE void analyzeLiterature();
    Q_INVOKABLE QString scienceServiceInstallHint() const;
    // What "literature" can mean. A paper is usually a PDF, but a thesis
    // chapter, a lab protocol or a set of notes is just as likely to be a Word
    // document, a text file or an ebook, and refusing those made the feature
    // narrower than the work it is meant to support.
    Q_INVOKABLE QStringList literatureNameFilters() const;
    static const QSet<QString>& literatureExtensions();
    QVariantList importLog() const{return importLog_;}
    ProjectWorkspace* project() const{return project_;}
    int plotColourVision() const{return plotColourVision_;}
    QString plotColourMap() const{return plotColourMap_;}
    void setPlotColourMap(const QString& name);
    int fullRenderPolicy() const{return fullRenderPolicy_;}
    void setFullRenderPolicy(int policy);
    double fullRenderAskAfterSeconds() const{return fullRenderAskAfterSeconds_;}
    void setFullRenderAskAfterSeconds(double seconds);
    QStringList fullRenderPolicyNames() const{
        return {QStringLiteral("Show it as soon as it is ready"),
                QStringLiteral("Ask every time"),
                QStringLiteral("Ask only when the render was slow")};
    }
    int figureTheme() const{return figureTheme_;}
    void setFigureTheme(int mode);
    QStringList figureThemeNames() const{
        return {QStringLiteral("Follow the interface theme"),
                QStringLiteral("Dark"),QStringLiteral("Light"),QStringLiteral("White")};
    }
    bool plotGridVisible() const{return plotGridVisible_;}
    void setPlotGridVisible(bool on);
    int plotGridDensity() const{return plotGridDensity_;}
    void setPlotGridDensity(int ticks);
    bool plotScaleLabels() const{return plotScaleLabels_;}
    void setPlotScaleLabels(bool on);
    int plotFieldInterpolation() const{return plotFieldInterpolation_;}
    void setPlotFieldInterpolation(int mode);
    int uiLayout() const{return uiLayout_;}
    void setUiLayout(int layout);
    bool notebookLayout() const{return uiLayout_==0;}
    QStringList uiLayoutNames() const{
        return {QStringLiteral("Notebook"),QStringLiteral("Inspector"),
                QStringLiteral("Command bar"),QStringLiteral("Workflow rail"),
                QStringLiteral("Ribbon"),QStringLiteral("Studio")};
    }
    QStringList uiLayoutDescriptions() const{
        return {
            QStringLiteral("Several figures stacked as a document, so two graphs can be "
                           "compared side by side rather than one at a time."),
            QStringLiteral("One scrolling column of settings on the right, no tabs. "
                           "Nothing is ever hidden behind another tab."),
            QStringLiteral("Almost no permanent chrome. The figure gets the whole window "
                           "and the controls appear only while they are being used."),
            QStringLiteral("Data, then graph, then figure, left to right — the order the "
                           "work actually happens in."),
            QStringLiteral("A band across the top that changes with the task, as Excel and "
                           "Origin do."),
            QStringLiteral("Full-bleed canvas with the controls floating over it, movable "
                           "and closable.")};
    }
    QStringList plotFieldInterpolationNames() const{
        return {QStringLiteral("None - only the cells that were measured"),
                QStringLiteral("Nearest - each gap takes its closest measurement"),
                QStringLiteral("Linear - a smooth surface through the measurements"),
                QStringLiteral("Cubic - smoother still")};
    }
    QVariantList recentDatasets() const{return recentDatasets_;}
    QVariantList recentVisualisations() const{return recentVisualisations_;}
    // Called by the workspace whenever a catalogue entry is applied, so the
    // list is what was actually DRAWN rather than what was clicked in the
    // library and then abandoned.
    Q_INVOKABLE void noteVisualisation(const QString& engine,const QString& variant);
    // Re-import a remembered file. Named separately from importDataset because
    // it must cope with the file having been moved or deleted since, and say so
    // rather than failing silently.
    Q_INVOKABLE void openRecentDataset(const QString& path);
    Q_INVOKABLE void clearRecents();
    void setPlotColourVision(int value);
    QStringList plotColourVisionNames() const;
    QString plotColourVisionSummary() const;
    Q_INVOKABLE void clearImportLog();
    // The optional science add-on ships as a script beside the application.
    // Running it is what turns .mat and the other 140-odd formats on.
    bool dataFormatsInstallerAvailable() const{return !dataFormatsInstaller().isEmpty();}
    Q_INVOKABLE bool installDataFormats();
    Q_INVOKABLE bool refreshScienceServiceAvailability();
    // Recorded by main.cpp. VTK needs an OpenGL scene graph; everything else
    // runs better on the platform default, and forcing OpenGL is what left a
    // recreated window painting black on this hardware.
    static void setGraphicsApiIsOpenGL(bool value);
    static bool graphicsApiIsOpenGL();
    // Startup progress. The construction of this object IS most of start-up -
    // the graph catalogue, the native core, the science probe - so the splash
    // has to hear about it from in here rather than guessing from outside.
    static void setStartupReporter(std::function<void(const QString&,double)> reporter);
    // Everything importer.py can read, so an unsupported file is refused with a
    // clear reason instead of being handed to a converter that cannot help.
    static const QSet<QString>& importableExtensions();
    Q_INVOKABLE void cancelActiveJob();
    Q_INVOKABLE bool importFirstLiteratureDataset();
    Q_INVOKABLE bool exportProjectState(const QUrl& url);
    Q_INVOKABLE void notify(const QString& message){ setStatus(message); }

    // Fuzzy catalogue search, ported from GraphVis 17
    // src/graphvis/rendering/graph_library.py (fuzzy_score / search_entries).
    Q_INVOKABLE QVariantList searchGraphs(const QString& query,bool includeAdvanced=true,int limit=200) const;
    // Absolute file URL of an entry's pre-rendered thumbnail, or "" when absent.
    Q_INVOKABLE QString graphThumbnail(const QString& fileName,bool compact=true) const;
    // Absolute path for a figure export, creating the directory. Keeps QML
    // from having to know anything about the filesystem layout.
    Q_INVOKABLE QString exportPath(const QString& baseName,const QString& extension) const;
    // The same suggestion as a file URL, for a Save dialog's currentFile, and
    // the reverse conversion for what one hands back. QML gets URLs from a
    // FileDialog and the canvas takes paths; doing this in QML with string
    // surgery on "file:///" is the kind of thing that works until a path has a
    // space or a drive letter in it.
    Q_INVOKABLE QUrl suggestedExportUrl(const QString& baseName,const QString& extension) const{
        return QUrl::fromLocalFile(exportPath(baseName,extension));
    }
    Q_INVOKABLE QString localPathOf(const QUrl& url) const{
        return url.isLocalFile()?url.toLocalFile():url.toString();
    }

    // Deep scan of the active dataset. budgetSeconds is a soft thinking
    // budget: larger budgets widen column breadth and pair sampling.
    //
    // A scan of a 200k-row dataset costs seconds of real thinking, and the
    // answer only changes when the data does. force skips the cache; the
    // rescan button in ProjectPanel passes true.
    Q_INVOKABLE void scanDataset(double budgetSeconds,bool useLiterature=true,bool force=false);
    Q_INVOKABLE void clearScan();

    // The .gvfig / .gvis figure package, ported from GraphVis 17. A figure you
    // can reopen and revise rather than only export as a picture: the container
    // holds the canvas state and the Arrow payloads it was drawn from.
    //
    // canvasState comes from PlotCanvas::figureState(), because the canvas
    // lives in QML and this object never sees it.
    Q_INVOKABLE bool saveFigure(const QUrl& url,const QVariantMap& canvasState);
    Q_INVOKABLE bool openFigure(const QUrl& url);

    // Folder-scale batch processing. scanBatchFolder lists what the importer
    // can actually read there; runBatch processes them and, given a report
    // path, writes an HTML, Word or PDF report of what worked.
    Q_INVOKABLE bool scanBatchFolder(const QUrl& folder,bool recursive=false);
    Q_INVOKABLE bool runBatch(const QUrl& folder,const QString& operation,
                              bool recursive=false,const QString& reportFormat=QString());

    // The solver bridge, ported from GraphVis 17's automation/sim_bridge.
    // Engine-agnostic on purpose: the command is a user-editable template with
    // {script} and {workdir} substituted, so no vendor is hard-wired.
    //
    // Whatever files the run produced are queued through the ordinary import
    // path, which means all 149 formats, not a special case for each engine.
    Q_INVOKABLE QVariantList solverPresets() const;
    Q_INVOKABLE bool runSolver(const QString& command,const QUrl& workdir,const QUrl& script,
                               const QString& outputPatterns=QString(),int timeoutSeconds=3600);
    Q_INVOKABLE void cancelSolver();
    // The MATLAB Engine API path has no shell command: it runs the .m file in a
    // shared engine and pulls the workspace with no intermediate files. That
    // needs Python, so it goes through the science service.
    Q_INVOKABLE bool runMatlabScript(const QUrl& script,const QUrl& workdir=QUrl());

    // Optional components. The heavy or rarely-wanted parts - domain formats,
    // chart reading - are chosen at install time and can be added or dropped
    // at any point afterwards without reinstalling anything else.
    Q_INVOKABLE void refreshComponents();
    Q_INVOKABLE bool installComponents(const QStringList& keys);
    Q_INVOKABLE bool removeComponents(const QStringList& keys);
    // Drops every cached scan for the open project (or the shared cache when
    // no project is open). Returns the number of files removed.
    Q_INVOKABLE int clearScanCache();

signals:
    void stateChanged();
    void activeDatasetChanged();
    void statusChanged();
    void rendererModeChanged();
    void busyChanged();
    void sqlResultChanged();
    void experimentalUiChanged();
    void workspaceModeChanged();
    void literatureChanged();
    void analysisChanged();
    void analysisCatalogueChanged();
    void scienceServiceAvailabilityChanged();
    void importLogChanged();
    void plotColourVisionChanged();
    void plotDisplayChanged();
    void recentsChanged();
    void graphCatalogueChanged();
    void smartRenderChanged();
    void scanChanged();
    // A loaded figure's canvas state, for QML to hand to PlotCanvas. The
    // datasets it carried are imported first, so by the time this fires the
    // columns the state names are there to be selected.
    void figureLoaded(const QVariantMap& canvasState);
    void batchChanged();
    void solverChanged();
    void componentsChanged();
    void themeIndexChanged();
    void displayModeChanged();
private:
    void setStatus(const QString& s);
    void setBusy(bool value,const QString& label={});
    void handleImportFinished();
    void handleQueryFinished();
    QString scienceServiceExecutable() const;
    QString dataFormatsInstaller() const;
    void noteImport(const QString& path,const QString& state,const QString& detail);
    void pumpImportQueue();
    QVariantList importLog_;
    ProjectWorkspace* project_=nullptr;
    int plotColourVision_=0;
    QString plotColourMap_;                 // empty means Viridis
    int fullRenderPolicy_=0;                // 0 automatic, 1 always ask, 2 ask when slow
    double fullRenderAskAfterSeconds_=5.0;
    int figureTheme_=0;                     // follow the interface theme
    bool plotGridVisible_=true;
    int plotGridDensity_=0;                 // 0 = the default 7 x 6
    bool plotScaleLabels_=true;
    int plotFieldInterpolation_=2;          // Linear
    int uiLayout_=0;                        // Notebook
    // Newest first, capped. Each entry is {path, name}; visualisations are
    // {engine, variant, label}.
    QVariantList recentDatasets_;
    QVariantList recentVisualisations_;
    void rememberDataset(const QString& path,const QString& name);
    void loadRecents();
    void saveRecents();
    QStringList importQueue_;
    // Converted Arrow file -> the file the user actually chose, so the log row
    // stays on their file rather than sprouting a second row for a temporary.
    QHash<QString,QString> convertedFrom_;
    void handleScienceReply(const QJsonObject& obj);
    bool startScienceOp(const QJsonObject& request,const QString& op,const QString& busyLabel);
    // Starts the science service if it is not already up. It is deliberately
    // long-lived: launching a fresh interpreter per request cost ~400 ms of
    // pandas/pyarrow import before any work began. See AppController.cpp.
    bool ensureScienceService();
    QString pendingScienceOp_;
    // Set while a non-native file is being converted to Arrow by the
    // science service, so the reply knows to import the result.
    QString pendingConvertSource_;
    QVariantList scanRecommendations_;
    QVariantMap scanSummary_;
    bool scanning_=false;
    // Scan cache. The key fingerprints the Arrow file (path, size, mtime), the
    // budget, and the literature context, so any change to the inputs misses.
    // The guard three operations open with: there has to be an active dataset
    // and it has to have been written to Arrow. Returns its path, or an empty
    // string having already told the user which of the two is missing.
    QString requireActiveArrow();
    QString scanCacheDir() const;
    QString scanCacheKey(const QJsonObject& request) const;
    void applyScanReply(const QJsonObject& obj,bool cached);
    // Where the in-flight scan's reply will be written on success.
    QString pendingScanCachePath_;

    // Batch state. A figure needs none: the reply carries everything and is
    // applied on arrival.
    QVariantMap batchResult_;
    QVariantList batchItems_;

    // Solver bridge.
    QProcess solverProcess_;
    QStringList solverTail_;
    QString solverPatterns_;
    QString solverWorkdir_;
    QDateTime solverStarted_;
    QVariantMap solverResult_;
    bool solverCancelled_=false;
    void collectSolverOutputs(int exitCode);

    // Optional components.
    QProcess componentProcess_;
    QStringList componentTail_;
    QVariantList components_;
    // "list" while reading the catalogue, "change" while installing or removing.
    QString componentMode_;
    QByteArray componentOutput_;
    bool startComponentScript(const QStringList& arguments,const QString& mode);
    QString componentScriptPath() const;
    int themeIndex_=0;
    int displayMode_=1;
    void loadGraphCatalogue();
    static double fuzzyScore(const QString& query,const QVariantMap& entry);
    QVariantList graphCategories_;
    QVariantList graphPacks_;
    int hiddenEntryCount_=0;
    QVariantList graphEntries_;
    QString graphPreviewDir_;
    QString graphPreviewCompactDir_;
    void* runtime_{};
    NativeViewportWindow viewport_;
    QVariantList datasets_;
    QString activeDatasetId_;
    QString workspaceName_=QStringLiteral("Untitled");
    QString status_;
    QString rendererMode_=QStringLiteral("Native WGPU");
    bool busy_=false;
    QString busyLabel_;
    QString sqlResult_;
    bool experimentalUi_=true;
    QString workspaceMode_=QStringLiteral("Home");
    QUrl literatureUrl_;
    QVariantMap literatureAnalysis_;
    QVariantMap analysisResult_;
    QString analysisKind_;
    QVariantList analysisCatalogue_;
    QFutureWatcher<QString> importWatcher_;
    QFutureWatcher<QString> queryWatcher_;
    QProcess scienceProcess_;
    QByteArray scienceOutput_;
    bool cancelRequested_=false;
    // Why the native core did not load, kept so the status line can say it
    // rather than the constructor giving up halfway through.
    QString nativeCoreError_;
    QVariantMap smartRenderPlan_;
};
