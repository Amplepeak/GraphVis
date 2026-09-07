#pragma once
#include <functional>
#include <QObject>
#include <QHash>
#include <QRect>
#include <QSet>
#include <QStringList>
#include <QFutureWatcher>
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
    Q_PROPERTY(bool dataFormatsInstallerAvailable READ dataFormatsInstallerAvailable CONSTANT)
    Q_PROPERTY(QVariantMap smartRenderPlan READ smartRenderPlan NOTIFY smartRenderChanged)
    // GraphVis 17 graph catalogue: 318 entries in 30 categories, loaded from
    // config/graph_catalogue.json (embedded as a QML module resource).
    // Smart Suite / Scan Dataset (GraphVis 17 intelligent_scan), run through
    // the optional Python science service.
    Q_PROPERTY(QVariantList scanRecommendations READ scanRecommendations NOTIFY scanChanged)
    Q_PROPERTY(QVariantMap scanSummary READ scanSummary NOTIFY scanChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanChanged)
    // True once an opened paper has been analysed, so its variables and
    // plot intent can steer the scan (Phase 4).
    Q_PROPERTY(bool literatureContextAvailable READ literatureContextAvailable NOTIFY literatureChanged)
    // The reply from the most recent analysis.* or surface.* operation, exactly
    // as the service sent it. The panels read what they need out of it rather
    // than each result type needing its own property.
    Q_PROPERTY(QVariantMap analysisResult READ analysisResult NOTIFY analysisChanged)
    Q_PROPERTY(QString analysisKind READ analysisKind NOTIFY analysisChanged)
    Q_PROPERTY(bool analysisOk READ analysisOk NOTIFY analysisChanged)
    // Persisted UI theme index. Kept in C++ so QML needs no QtCore module,
    // whose absence from the deployed layout stopped the app from starting.
    Q_PROPERTY(int themeIndex READ themeIndex WRITE setThemeIndex NOTIFY themeIndexChanged)
    // How the window opens: 0 windowed, 1 maximised, 2 fullscreen,
    // 3 borderless. Persisted alongside the theme.
    Q_PROPERTY(int displayMode READ displayMode WRITE setDisplayMode NOTIFY displayModeChanged)
    Q_PROPERTY(QStringList displayModeNames READ displayModeNames CONSTANT)
    Q_PROPERTY(QVariantList graphCategories READ graphCategories CONSTANT)
    Q_PROPERTY(int graphEntryCount READ graphEntryCount CONSTANT)
    // How many entries the "Include advanced" switch actually reveals. The
    // library used to print `graphEntryCount - 82`, a constant that any
    // catalogue edit makes wrong and a smaller catalogue makes negative.
    Q_PROPERTY(int advancedEntryCount READ advancedEntryCount CONSTANT)
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

    // One entry point for every analysis operation the service exposes:
    // limits, forecast, fft, weibull, regression, pca, doe, advisor, domain.
    // `options` is merged into the request, so a caller passes only what that
    // operation needs (predictors for a regression, bounds for a design).
    Q_INVOKABLE bool runAnalysis(const QString& kind,const QVariantMap& options=QVariantMap());
    // Scattered x/y/z to a regular grid through one of the sixteen estimators.
    // The result is an ordinary three-column Arrow file, so it imports and
    // draws like any other dataset.
    Q_INVOKABLE bool estimateSurface(const QString& x,const QString& y,const QString& z,
                                     const QString& estimator,int resolution=160);
    bool scienceServiceAvailable() const;
    QVariantMap smartRenderPlan() const{return smartRenderPlan_;}
    QVariantList scanRecommendations() const{return scanRecommendations_;}
    QVariantMap scanSummary() const{return scanSummary_;}
    bool scanning() const{return scanning_;}
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

    // Deep scan of the active dataset. budgetSeconds is a soft thinking
    // budget: larger budgets widen column breadth and pair sampling.
    //
    // A scan of a 200k-row dataset costs seconds of real thinking, and the
    // answer only changes when the data does. force skips the cache; the
    // rescan button in ProjectPanel passes true.
    Q_INVOKABLE void scanDataset(double budgetSeconds,bool useLiterature=true,bool force=false);
    Q_INVOKABLE void clearScan();
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
    void scienceServiceAvailabilityChanged();
    void importLogChanged();
    void plotColourVisionChanged();
    void smartRenderChanged();
    void scanChanged();
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
    QString scanCacheDir() const;
    QString scanCacheKey(const QJsonObject& request) const;
    void applyScanReply(const QJsonObject& obj,bool cached);
    // Where the in-flight scan's reply will be written on success.
    QString pendingScanCachePath_;
    int themeIndex_=0;
    int displayMode_=1;
    void loadGraphCatalogue();
    static double fuzzyScore(const QString& query,const QVariantMap& entry);
    QVariantList graphCategories_;
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
