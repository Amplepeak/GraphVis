#include "AppController.h"
#include "NativeApi.h"
#include "ColourVision.h"
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDesktopServices>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

static QString cleanLocalPath(const QUrl& url){return url.isLocalFile()?url.toLocalFile():url.toString();}

namespace {
std::function<void(const QString&,double)> gStartupReporter;
void reportStartup(const QString& stage,double fraction){
    if(gStartupReporter) gStartupReporter(stage,fraction);
}
}

void AppController::setStartupReporter(std::function<void(const QString&,double)> reporter){
    gStartupReporter=std::move(reporter);
}

AppController::AppController(QObject* parent):QObject(parent),viewport_(){
    project_=new ProjectWorkspace(this);
    reportStartup(QStringLiteral("Loading the graph catalogue"),0.30);
    loadGraphCatalogue();
#ifndef Q_OS_WIN
    rendererMode_=QStringLiteral("VTK / PBR");
#endif
    QSettings settings(QStringLiteral("GraphVis"),QStringLiteral("GraphVis 18.4"));
    // Startup is intentionally offline/zero-maintenance. Update checks are manual
    // and must never block application launch.
    if(!settings.contains(QStringLiteral("updates/automaticChecks"))) settings.setValue(QStringLiteral("updates/automaticChecks"),false);
    experimentalUi_=settings.value(QStringLiteral("ui/experimental"),true).toBool();
    themeIndex_=settings.value(QStringLiteral("ui/themeIndex"),0).toInt();
    rendererMode_=settings.value(QStringLiteral("ui/rendererMode"),rendererMode_).toString();
    // Maximised by default: a scientific editor wants the whole screen, and it
    // avoids the window opening straddled across a two-monitor desktop.
    displayMode_=qBound(0,settings.value(QStringLiteral("ui/displayMode"),1).toInt(),3);
    plotColourVision_=qBound(0,settings.value(QStringLiteral("plot/colourVision"),0).toInt(),4);
    // Every signal connection below has to happen whether or not the native
    // core loads. When the DLL is missing this constructor used to return here,
    // leaving the science process with no reply path at all - so Literature
    // analysis sat "working" forever instead of reporting the real problem -
    // and leaving the last project unopened for no reason connected to Rust.
    connect(&viewport_,&NativeViewportWindow::nativeStatusChanged,this,[this](const QString&s){setStatus(s);});
    connect(&importWatcher_,&QFutureWatcher<QString>::finished,this,&AppController::handleImportFinished);
    connect(&queryWatcher_,&QFutureWatcher<QString>::finished,this,&AppController::handleQueryFinished);

    reportStartup(QStringLiteral("Opening the native core"),0.48);
    auto& api=NativeApi::instance();
    if(api.load()){
        const QString cache=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+QStringLiteral("/18.4/arrow-cache");
        QDir().mkpath(cache);
        const QByteArray c=QDir::toNativeSeparators(cache).toUtf8();
        runtime_=api.runtimeNew(c.constData());
        viewport_.setRuntime(runtime_);
    }else{
        nativeCoreError_=api.error();
    }

    // The last project reopens itself, but only after the window is up: a scan
    // of an adopted folder can touch thousands of files and must never be the
    // reason start-up feels slow.
    QTimer::singleShot(0,this,[this]{ project_->reopenLast(); });
    reportStartup(QStringLiteral("Preparing the workspace"),0.62);
    connect(&scienceProcess_,&QProcess::readyReadStandardOutput,this,[this]{
        scienceOutput_+=scienceProcess_.readAllStandardOutput();
        // Drain every complete line. The service answers one request at a time,
        // but a reply and a later one can arrive in the same read.
        for(;;){
            const int nl=scienceOutput_.indexOf('\n');
            if(nl<0) break;
            const QByteArray line=scienceOutput_.left(nl); scienceOutput_.remove(0,nl+1);
            if(line.trimmed().isEmpty()) continue;
            const auto doc=QJsonDocument::fromJson(line);
            if(!doc.isObject()) continue;
            // Only a reply to something we actually asked for. handleScienceReply
            // dispatches on pendingScienceOp_, so a stray line - a late reply to
            // a cancelled request, or anything the service prints on its own -
            // would otherwise match no branch and be silently swallowed.
            if(pendingScienceOp_.isEmpty()) continue;
            handleScienceReply(doc.object());
            setBusy(false);
        }
        // Deliberately NOT closing stdin or terminating. The service is a loop
        // over stdin and stays up for the next request - see startScienceOp.
    });
    connect(&scienceProcess_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){
        setStatus(QStringLiteral("Science service failed: %1").arg(scienceProcess_.errorString()));
        if(scanning_){scanning_=false;emit scanChanged();}
        pendingScienceOp_.clear();
        setBusy(false);});
    // If it dies mid-request, say so and let the next request start a fresh one
    // rather than writing into a dead pipe forever.
    connect(&scienceProcess_,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,
            [this](int code,QProcess::ExitStatus){
        if(!pendingScienceOp_.isEmpty()){
            setStatus(QStringLiteral("Science service stopped before answering (exit %1)").arg(code));
            if(scanning_){scanning_=false;emit scanChanged();}
            pendingScienceOp_.clear();
            setBusy(false);
        }
        scienceOutput_.clear();
    });
    refreshState();
    if(runtime_)
        setStatus(QStringLiteral("GraphVis 18.4 ready — fast offline startup; update checks are manual"));
    else
        setStatus(QStringLiteral("Native core unavailable: %1 — data import and queries are disabled")
                      .arg(nativeCoreError_));
}
AppController::~AppController(){
    if(scienceProcess_.state()!=QProcess::NotRunning){
        // The service is a loop over stdin, so closing stdin asks it to finish
        // and exit cleanly - which lets Python flush and release any file it
        // still holds. Only kill it if it ignores that.
        scienceProcess_.closeWriteChannel();
        if(!scienceProcess_.waitForFinished(2000)){
            scienceProcess_.kill();
            scienceProcess_.waitForFinished(1000);
        }
    }
    // Both futures hold the runtime pointer and each worker body is a single
    // blocking call into the Rust core, so there is nothing to interrupt -
    // freeing the runtime while one is still inside it is a use-after-free, and
    // quitting during an import is exactly when that happens. Wait, then free.
    if(importWatcher_.isRunning()) importWatcher_.waitForFinished();
    if(queryWatcher_.isRunning()) queryWatcher_.waitForFinished();
    if(runtime_&&NativeApi::instance().runtimeFree) NativeApi::instance().runtimeFree(runtime_);
    runtime_=nullptr;
}

void AppController::setStatus(const QString&s){if(status_==s)return;status_=s;emit statusChanged();}
void AppController::setBusy(bool value,const QString& label){busy_=value;busyLabel_=value?label:QString();emit busyChanged();}
void AppController::setActiveDatasetId(const QString&id){if(activeDatasetId_==id)return;activeDatasetId_=id;emit activeDatasetChanged();}
void AppController::setRendererMode(const QString& value){
    if(rendererMode_==value)return;
    rendererMode_=value;
    // Persisted because the graphics API is chosen before the GUI exists:
    // main.cpp reads this back at startup to decide whether the scene graph
    // has to be OpenGL for QQuickVTKItem.
    QSettings(QStringLiteral("GraphVis"),QStringLiteral("GraphVis 18.4"))
        .setValue(QStringLiteral("ui/rendererMode"),rendererMode_);
    if(value==QStringLiteral("VTK / PBR")&&!graphicsApiIsOpenGL())
        setStatus(QStringLiteral("VTK / PBR needs the OpenGL scene graph - restart GraphVis to use it"));
    emit rendererModeChanged();
}

// main.cpp records what it actually chose, so the UI can say so honestly
// instead of guessing.
static bool gGraphicsApiIsOpenGL=false;
void AppController::setGraphicsApiIsOpenGL(bool value){gGraphicsApiIsOpenGL=value;}
bool AppController::graphicsApiIsOpenGL(){return gGraphicsApiIsOpenGL;}
void AppController::setExperimentalUi(bool value){if(experimentalUi_==value)return;experimentalUi_=value;QSettings(QStringLiteral("GraphVis"),QStringLiteral("GraphVis 18.4")).setValue(QStringLiteral("ui/experimental"),value);emit experimentalUiChanged();}
void AppController::setWorkspaceMode(const QString& value){if(workspaceMode_==value)return;workspaceMode_=value;emit workspaceModeChanged();}

void AppController::refreshState(){
    if(!runtime_)return;const QString text=NativeApi::instance().takeString(NativeApi::instance().stateJson(runtime_));
    const auto doc=QJsonDocument::fromJson(text.toUtf8());if(!doc.isObject())return;const auto o=doc.object();workspaceName_=o.value("workspace_name").toString("Untitled");datasets_.clear();
    for(const auto& v:o.value("datasets").toArray())datasets_.push_back(v.toObject().toVariantMap());
    if(activeDatasetId_.isEmpty()&&!datasets_.isEmpty())activeDatasetId_=datasets_.last().toMap().value("id").toString();
    emit stateChanged();emit activeDatasetChanged();
}

// The native core reads Arrow IPC, CSV/TSV and Parquet. Everything else goes
// through the Python importer, which converts it to Arrow IPC and hands the
// result straight back here - the routing the core's own error message asks for.
static bool nativeReadsExtension(const QString& suffix){
    static const QStringList native{QStringLiteral("arrow"),QStringLiteral("ipc"),
                                    QStringLiteral("csv"),QStringLiteral("tsv"),
                                    QStringLiteral("parquet")};
    return native.contains(suffix.toLower());
}

namespace {
// "*.a *.b *.c" from a set of bare extensions, sorted so the dialog reads the
// same way every time.
QString wildcardsFor(const QSet<QString>& extensions){
    QStringList list;
    list.reserve(extensions.size());
    for(const QString& e:extensions) list<<QStringLiteral("*.")+e;
    list.sort();
    return list.join(QLatin1Char(' '));
}
} // namespace

QStringList AppController::importNameFilters() const{
    // The "all" line is GENERATED from importableExtensions(), which is itself
    // kept in step with importer.py's READERS registry. It used to be a third
    // copy of the same 149 extensions written out by hand, so adding a format
    // meant three edits and the three lists had already drifted apart.
    return {
        QStringLiteral("All supported data (%1)").arg(wildcardsFor(importableExtensions())),
        QStringLiteral("MATLAB (*.mat)"),
        QStringLiteral("Tables and text (*.asc *.csv *.dat *.fwf *.htm *.html *.json *.jsonl *.log *.ndjson *.prn *.psv *.toml "
                       "*.tsv *.txt *.xml *.yaml *.yml)"),
        QStringLiteral("Spreadsheets (*.numbers *.ods *.xls *.xlsb *.xlsm *.xlsx)"),
        QStringLiteral("Columnar (*.arrow *.avro *.feather *.ipc *.orc *.parquet *.pq)"),
        QStringLiteral("HDF5 and NetCDF (*.h5 *.hdf *.hdf5 *.he5 *.cdf *.nc *.nc4 *.netcdf)"),
        QStringLiteral("Statistics packages (*.dta *.por *.rda *.rdata *.rds *.sas7bdat *.sav *.xpt)"),
        QStringLiteral("Arrays and databases (*.mpk *.msgpack *.npy *.npz *.pickle *.pkl *.db *.ddb *.duckdb *.sqlite *.sqlite3)"),
        QStringLiteral("Business and accounting (*.accdb *.dbf *.mdb)"),
        QStringLiteral("Finance and banking (*.mt940 *.ofx *.qfx *.qif *.sta)"),
        QStringLiteral("Reports and documents (*.docx *.pdf)"),
        QStringLiteral("Marine and oceanography (*.000 *.aqd *.btl *.cnv *.ctd *.gps *.grb *.grib *.grib2 *.nmea *.odv *.pd0 *.prf *.rsk "
                       "*.vec *.wpr *.xyz)"),
        QStringLiteral("Diving and dive computers (*.dl7 *.sml *.ssrf *.uddf *.zxl *.zxu)"),
        QStringLiteral("GPS tracks and geospatial (*.gpx *.kml *.kmz *.geojson *.gpkg *.shp)"),
        QStringLiteral("Instruments and audio (*.tdm *.tdms *.aif *.aiff *.flac *.ogg *.wav *.las)"),
        QStringLiteral("Mass spec, flow cytometry, spectroscopy (*.mgf *.mzml *.mzxml *.fcs *.dx *.jcamp *.jdx)"),
        QStringLiteral("Astronomy, physics, seismic (*.fit *.fits *.fts *.root *.miniseed *.mseed *.sac *.segy *.sgy)"),
        QStringLiteral("Physiology and imaging (*.bdf *.cnt *.edf *.fif *.set *.abf *.nwb *.dcm *.dicom *.nii *.tif *.tiff)"),
        QStringLiteral("Genomics and sequences (*.bed *.fa *.fasta *.fastq *.fna *.fq *.gff *.gff3 *.gtf *.sam *.vcf)"),
        QStringLiteral("Molecular structures (*.cif *.mol *.pdb *.sdf)"),
        QStringLiteral("All files (*)")
    };
}

// Kept in step with importer.py's READERS registry. Checking here means an
// unreadable file is refused immediately with the reason, rather than being
// queued behind a converter that was never going to help.
const QSet<QString>& AppController::importableExtensions(){
    static const QSet<QString> all{
        QStringLiteral("000"),QStringLiteral("abf"),QStringLiteral("accdb"),
        QStringLiteral("aif"),QStringLiteral("aiff"),QStringLiteral("aqd"),
        QStringLiteral("arrow"),QStringLiteral("asc"),QStringLiteral("avro"),
        QStringLiteral("bdf"),QStringLiteral("bed"),QStringLiteral("btl"),QStringLiteral("cdf"),
        QStringLiteral("cif"),QStringLiteral("cnt"),QStringLiteral("cnv"),QStringLiteral("csv"),
        QStringLiteral("ctd"),QStringLiteral("dat"),QStringLiteral("db"),QStringLiteral("dbf"),
        QStringLiteral("dcm"),QStringLiteral("ddb"),QStringLiteral("dicom"),
        QStringLiteral("dl7"),QStringLiteral("docx"),QStringLiteral("dta"),
        QStringLiteral("duckdb"),QStringLiteral("dx"),QStringLiteral("edf"),
        QStringLiteral("fa"),QStringLiteral("fasta"),QStringLiteral("fastq"),
        QStringLiteral("fcs"),QStringLiteral("feather"),QStringLiteral("fif"),
        QStringLiteral("fit"),QStringLiteral("fits"),QStringLiteral("flac"),
        QStringLiteral("fna"),QStringLiteral("fq"),QStringLiteral("fts"),QStringLiteral("fwf"),
        QStringLiteral("geojson"),QStringLiteral("gff"),QStringLiteral("gff3"),
        QStringLiteral("gpkg"),QStringLiteral("gps"),QStringLiteral("gpx"),
        QStringLiteral("grb"),QStringLiteral("grib"),QStringLiteral("grib2"),
        QStringLiteral("gtf"),QStringLiteral("h5"),QStringLiteral("hdf"),QStringLiteral("hdf5"),
        QStringLiteral("he5"),QStringLiteral("htm"),QStringLiteral("html"),
        QStringLiteral("ipc"),QStringLiteral("jcamp"),QStringLiteral("jdx"),
        QStringLiteral("json"),QStringLiteral("jsonl"),QStringLiteral("kml"),
        QStringLiteral("kmz"),QStringLiteral("las"),QStringLiteral("log"),QStringLiteral("mat"),
        QStringLiteral("mdb"),QStringLiteral("mgf"),QStringLiteral("miniseed"),
        QStringLiteral("mol"),QStringLiteral("mpk"),QStringLiteral("mseed"),
        QStringLiteral("msgpack"),QStringLiteral("mt940"),QStringLiteral("mzml"),
        QStringLiteral("mzxml"),QStringLiteral("nc"),QStringLiteral("nc4"),
        QStringLiteral("ndjson"),QStringLiteral("netcdf"),QStringLiteral("nii"),
        QStringLiteral("nmea"),QStringLiteral("npy"),QStringLiteral("npz"),
        QStringLiteral("numbers"),QStringLiteral("nwb"),QStringLiteral("ods"),
        QStringLiteral("odv"),QStringLiteral("ofx"),QStringLiteral("ogg"),QStringLiteral("orc"),
        QStringLiteral("parquet"),QStringLiteral("pd0"),QStringLiteral("pdb"),
        QStringLiteral("pdf"),QStringLiteral("pickle"),QStringLiteral("pkl"),
        QStringLiteral("por"),QStringLiteral("pq"),QStringLiteral("prf"),QStringLiteral("prn"),
        QStringLiteral("psv"),QStringLiteral("qfx"),QStringLiteral("qif"),QStringLiteral("rda"),
        QStringLiteral("rdata"),QStringLiteral("rds"),QStringLiteral("root"),
        QStringLiteral("rsk"),QStringLiteral("sac"),QStringLiteral("sam"),
        QStringLiteral("sas7bdat"),QStringLiteral("sav"),QStringLiteral("sdf"),
        QStringLiteral("segy"),QStringLiteral("set"),QStringLiteral("sgy"),
        QStringLiteral("shp"),QStringLiteral("sml"),QStringLiteral("sqlite"),
        QStringLiteral("sqlite3"),QStringLiteral("ssrf"),QStringLiteral("sta"),
        QStringLiteral("tdm"),QStringLiteral("tdms"),QStringLiteral("tif"),
        QStringLiteral("tiff"),QStringLiteral("toml"),QStringLiteral("tsv"),
        QStringLiteral("txt"),QStringLiteral("uddf"),QStringLiteral("vcf"),
        QStringLiteral("vec"),QStringLiteral("wav"),QStringLiteral("wpr"),QStringLiteral("xls"),
        QStringLiteral("xlsb"),QStringLiteral("xlsm"),QStringLiteral("xlsx"),
        QStringLiteral("xml"),QStringLiteral("xpt"),QStringLiteral("xyz"),
        QStringLiteral("yaml"),QStringLiteral("yml"),QStringLiteral("zxl"),
        QStringLiteral("zxu")
    };
    return all;
}

void AppController::noteImport(const QString& path,const QString& state,const QString& detail){
    // A converted Arrow temporary reports against the file the user chose.
    const QString subject=convertedFrom_.value(path,path);
    for(int i=0;i<importLog_.size();++i){
        QVariantMap row=importLog_.at(i).toMap();
        if(row.value(QStringLiteral("path")).toString()!=subject) continue;
        row.insert(QStringLiteral("state"),state);
        row.insert(QStringLiteral("detail"),detail);
        row.insert(QStringLiteral("time"),QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")));
        importLog_.replace(i,row);
        emit importLogChanged();
        return;
    }
    importLog_.prepend(QVariantMap{
        {QStringLiteral("path"),subject},
        {QStringLiteral("name"),QFileInfo(subject).fileName()},
        {QStringLiteral("state"),state},
        {QStringLiteral("detail"),detail},
        {QStringLiteral("time"),QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))},
    });
    while(importLog_.size()>50) importLog_.removeLast();
    emit importLogChanged();
}

void AppController::clearImportLog(){importLog_.clear();emit importLogChanged();}

// The add-on installer sits beside the application in a deployed layout and at
// the repository root in a development stage; look in both.
QString AppController::dataFormatsInstaller() const{
    const QString appDir=QCoreApplication::applicationDirPath();
    const QStringList candidates{
        appDir+QStringLiteral("/INSTALL-DATA-FORMATS.bat"),
        appDir+QStringLiteral("/../INSTALL-DATA-FORMATS.bat"),
        appDir+QStringLiteral("/../../INSTALL-DATA-FORMATS.bat"),
        appDir+QStringLiteral("/../../../INSTALL-DATA-FORMATS.bat"),
    };
    for(const QString& c:candidates){
        const QFileInfo info(c);
        if(info.exists()) return info.absoluteFilePath();
    }
    return {};
}

bool AppController::installDataFormats(){
    const QString installer=dataFormatsInstaller();
    if(installer.isEmpty()){
        setStatus(QStringLiteral("Could not find INSTALL-DATA-FORMATS.bat beside the application"));
        return false;
    }
    if(!QDesktopServices::openUrl(QUrl::fromLocalFile(installer))){
        setStatus(QStringLiteral("Could not launch ") + installer);
        return false;
    }
    setStatus(QStringLiteral("Started %1 - press Check again when it finishes")
                  .arg(QFileInfo(installer).fileName()));
    return true;
}

bool AppController::refreshScienceServiceAvailability(){
    const bool available=scienceServiceAvailable();
    emit scienceServiceAvailabilityChanged();
    setStatus(available
              ? QStringLiteral("GraphVis Science add-on found - all %1 dataset formats are available")
                    .arg(importableExtensions().size())
              : QStringLiteral("GraphVis Science add-on is still not installed"));
    return available;
}

// Adding a dataset is a queue, not a single shot.
//
// The old version refused outright whenever a job was already running, so
// dropping three files imported the first and silently dropped the other two,
// and every rejection was a status line that scrolled away. Now each file is
// admitted or refused with a logged reason, and accepted files wait their turn.
const QSet<QString>& AppController::literatureExtensions(){
    static const QSet<QString> all{
        QStringLiteral("pdf"),QStringLiteral("docx"),QStringLiteral("doc"),
        QStringLiteral("odt"),QStringLiteral("rtf"),QStringLiteral("txt"),
        QStringLiteral("md"),QStringLiteral("tex"),QStringLiteral("epub"),
        QStringLiteral("html"),QStringLiteral("htm"),QStringLiteral("xml"),
        QStringLiteral("ris"),QStringLiteral("bib"),QStringLiteral("nbib"),
    };
    return all;
}

QStringList AppController::literatureNameFilters() const{
    // Same again: the "all" line comes from literatureExtensions(), which had
    // no callers at all precisely because this restated it.
    return {
        QStringLiteral("All literature (%1)").arg(wildcardsFor(literatureExtensions())),
        QStringLiteral("Papers and theses (*.pdf *.docx *.odt *.epub)"),
        QStringLiteral("Text and markup (*.txt *.md *.tex *.html *.htm *.xml)"),
        QStringLiteral("Reference exports (*.ris *.bib *.nbib)"),
        QStringLiteral("All files (*)")
    };
}

bool AppController::importDataset(const QUrl& url){
    const QString path=cleanLocalPath(url);
    if(path.isEmpty()){setStatus(QStringLiteral("That is not a local file"));return false;}

    const QFileInfo info(path);
    if(!info.exists()){
        noteImport(path,QStringLiteral("failed"),QStringLiteral("File not found"));
        return false;
    }
    if(!runtime_){
        noteImport(path,QStringLiteral("failed"),
                   QStringLiteral("The native core is not loaded, so nothing can be imported"));
        return false;
    }

    const QString suffix=info.suffix().toLower();
    const bool native=nativeReadsExtension(suffix);
    if(!native&&!importableExtensions().contains(suffix)){
        noteImport(path,QStringLiteral("failed"),
                   suffix.isEmpty()
                       ? QStringLiteral("This file has no extension, so GraphVis cannot tell what reads it")
                       : QStringLiteral("No reader for .%1 files. GraphVis reads %2 formats; see the "
                                        "Add dataset dialog for the full list.")
                             .arg(suffix).arg(importableExtensions().size()));
        return false;
    }
    if(!native&&!scienceServiceAvailable()){
        noteImport(path,QStringLiteral("failed"),
                   QStringLiteral(".%1 files are converted to Arrow by the GraphVis Science add-on, "
                                  "which is not installed yet.").arg(suffix));
        emit scienceServiceAvailabilityChanged();
        return false;
    }

    if(!importQueue_.contains(path)) importQueue_.append(path);
    noteImport(path,QStringLiteral("queued"),QString());
    pumpImportQueue();
    return true;
}

bool AppController::importDatasetPath(const QString& path){
    return importDataset(QUrl::fromLocalFile(path));
}

void AppController::pumpImportQueue(){
    if(busy_||importQueue_.isEmpty()||!runtime_) return;
    const QString path=importQueue_.takeFirst();
    const QString suffix=QFileInfo(path).suffix().toLower();

    if(!nativeReadsExtension(suffix)){
        const QString outDir=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                             +QStringLiteral("/18.4/imported");
        QDir().mkpath(outDir);
        pendingConvertSource_=path;
        noteImport(path,QStringLiteral("converting"),
                   QStringLiteral("Reading with the GraphVis Science add-on"));
        if(!startScienceOp(QJsonObject{{"op","io.import"},{"path",path},{"out_dir",outDir}},
                           QStringLiteral("io.import"),
                           QStringLiteral("Converting %1").arg(QFileInfo(path).fileName()))){
            pendingConvertSource_.clear();
            noteImport(path,QStringLiteral("failed"),status_);
            QTimer::singleShot(0,this,&AppController::pumpImportQueue);
        }
        return;
    }

    noteImport(path,QStringLiteral("importing"),QString());
    cancelRequested_=false;
    setBusy(true,QStringLiteral("Importing %1").arg(QFileInfo(convertedFrom_.value(path,path)).fileName()));
    const QByteArray p=QDir::toNativeSeparators(path).toUtf8(); void* runtime=runtime_;
    importWatcher_.setProperty("sourcePath",path);
    importWatcher_.setFuture(QtConcurrent::run([runtime,p]{return NativeApi::instance().takeString(NativeApi::instance().importDataset(runtime,p.constData()));}));
}
void AppController::handleImportFinished(){
    const QString response=importWatcher_.result();
    const QString path=importWatcher_.property("sourcePath").toString();
    const QString shown=QFileInfo(convertedFrom_.value(path,path)).fileName();
    if(cancelRequested_){
        if(runtime_) NativeApi::instance().undo(runtime_);
        cancelRequested_=false; setBusy(false); refreshState();
        noteImport(path,QStringLiteral("failed"),QStringLiteral("Cancelled"));
        setStatus(QStringLiteral("Cancelled import rolled back to the last confirmed application state"));
        importQueue_.clear();
        return;
    }
    setBusy(false);
    const auto doc=QJsonDocument::fromJson(response.toUtf8());
    if(!doc.isObject()||doc.object().contains("error")){
        const QString reason=doc.isObject()?doc.object().value(QStringLiteral("error")).toString():response;
        noteImport(path,QStringLiteral("failed"),reason.isEmpty()?response:reason);
        setStatus(QStringLiteral("Could not import %1").arg(shown));
        convertedFrom_.remove(path);
        pumpImportQueue();
        return;
    }
    activeDatasetId_=doc.object().value("id").toString();
    refreshState();
    const QVariantMap loaded=activeDataset();
    noteImport(path,QStringLiteral("loaded"),
               QStringLiteral("%1 rows, %2 numeric columns")
                   .arg(loaded.value(QStringLiteral("rows")).toLongLong())
                   .arg(loaded.value(QStringLiteral("numeric_columns")).toList().size()));
    convertedFrom_.remove(path);
    setStatus(QStringLiteral("Imported %1 into the Arrow-native workspace").arg(shown));
    workspaceMode_=QStringLiteral("Visualize"); emit workspaceModeChanged();
    pumpImportQueue();
}

QVariantMap AppController::activeDataset() const{for(const auto&v:datasets_){const auto m=v.toMap();if(m.value("id").toString()==activeDatasetId_)return m;}return{};}
QStringList AppController::activeColumns() const{QStringList out;for(const auto&v:activeDataset().value("numeric_columns").toList())out<<v.toString();return out;}
QString AppController::nativeArrowPath() const{return activeDataset().value("arrow_ipc_path").toString();}

// Mirrors sanitize_name() in native/crates/graphvis-ffi/src/lib.rs, which is
// what the DataFusion table is actually registered as. Keep the two in step.
QString AppController::activeTableName() const{
    const QString name=activeDataset().value(QStringLiteral("name")).toString();
    QString out;
    for(const QChar c:name) out.append((c.unicode()<128&&c.isLetterOrNumber())?c:QLatin1Char('_'));
    if(out.isEmpty()) out=QStringLiteral("dataset");
    if(out.at(0).isDigit()) out.prepend(QLatin1Char('d'));
    return out;
}

bool AppController::applyMapping(const QString&x,const QString&y,const QString&z,const QString&color,double pointSize,double opacity,bool invertOpacity,int voxelBins,bool smartRender,int smartProfile){
    if(busy_){setStatus("Wait for the active data job to finish");return false;} if(activeDatasetId_.isEmpty()){setStatus("Import/select a dataset first");return false;}
    const unsigned profile=smartRender?unsigned(qBound(1,smartProfile,3)):0u;
    smartRenderPlan_.clear();
    // The Qt 2-D backend takes its mapping straight from the PlotCanvas
    // properties in QML; it has no native viewport to push buffers into.
    if(rendererMode_==QStringLiteral("Qt 2-D")){
        setStatus(QStringLiteral("Mapping applied to the Qt 2-D canvas"));
        emit smartRenderChanged();
        return true;
    }
    if(rendererMode_==QStringLiteral("Native WGPU")){
        const bool ok=viewport_.mapDataset(activeDatasetId_,x,y,z,color,pointSize,opacity,invertOpacity,unsigned(qMax(0,voxelBins)),profile);
        if(ok){smartRenderPlan_=viewport_.lastMappingResult().value(QStringLiteral("smart_plan")).toMap();emit smartRenderChanged();
            if(smartRender&&!smartRenderPlan_.isEmpty())setStatus(QStringLiteral("Smart Render optimized %1 visible points · %2 contrast · %3 interpolation").arg(viewport_.lastMappingResult().value("points").toInt()).arg(smartRenderPlan_.value("color_transfer").toString()).arg(smartRenderPlan_.value("interpolation").toString()));
            else setStatus("Mapping applied directly to persistent WGPU buffers");}
        return ok;
    }
    if(smartRender&&runtime_&&NativeApi::instance().smartPlan){
        const QByteArray id=activeDatasetId_.toUtf8(), c=color.toUtf8();
        const QString result=NativeApi::instance().takeString(NativeApi::instance().smartPlan(runtime_,id.constData(),c.constData(),profile));
        const auto doc=QJsonDocument::fromJson(result.toUtf8());if(doc.isObject()&&!doc.object().contains("error"))smartRenderPlan_=doc.object().toVariantMap();
        emit smartRenderChanged();
    }else emit smartRenderChanged();
    setStatus(smartRender?QStringLiteral("Smart Render plan applied to %1 specialist viewport").arg(rendererMode_):QStringLiteral("Manual mapping applied to %1 specialist viewport").arg(rendererMode_));return true;
}
void AppController::setPointStyle(double pointSize,double opacity,bool invertOpacity){viewport_.setPointStyle(pointSize,opacity,invertOpacity);}

QString AppController::runSql(const QString&sql){
    if(!runtime_||busy_)return QStringLiteral("A native job is already running");
    const QString output=QStandardPaths::writableLocation(QStandardPaths::TempLocation)+QStringLiteral("/graphvis18-query.arrow");
    const QByteArray q=sql.toUtf8(),o=QDir::toNativeSeparators(output).toUtf8(); void* runtime=runtime_;
    cancelRequested_=false; setBusy(true,QStringLiteral("Streaming DataFusion query"));sqlResult_=QStringLiteral("Running…");emit sqlResultChanged();
    queryWatcher_.setFuture(QtConcurrent::run([runtime,q,o]{return NativeApi::instance().takeString(NativeApi::instance().queryToIpc(runtime,q.constData(),o.constData()));}));
    return sqlResult_;
}
void AppController::handleQueryFinished(){
    if(cancelRequested_){cancelRequested_=false;sqlResult_=QStringLiteral("Cancelled");setBusy(false);setStatus("Cancelled query result discarded");emit sqlResultChanged();return;}
    sqlResult_=queryWatcher_.result();setBusy(false);setStatus(QStringLiteral("DataFusion query complete"));emit sqlResultChanged();
}
void AppController::cancelActiveJob(){
    if(scienceProcess_.state()!=QProcess::NotRunning){scienceProcess_.kill();cancelRequested_=false;setBusy(false);setStatus("Literature analysis cancelled");return;}
    if(importWatcher_.isRunning()||queryWatcher_.isRunning()){
        cancelRequested_=true;
        importWatcher_.cancel(); queryWatcher_.cancel();
        setBusy(true,QStringLiteral("Cancelling at native job boundary…"));
        setStatus("Cancellation requested; GraphVis will discard the query result or roll back an imported dataset when the native call returns");
    }
}
void AppController::undo(){if(!busy_&&runtime_&&NativeApi::instance().undo(runtime_)){refreshState();setStatus("Undo");}}
void AppController::redo(){if(!busy_&&runtime_&&NativeApi::instance().redo(runtime_)){refreshState();setStatus("Redo");}}
QString AppController::pluginServicePath(const QString&language)const{return QCoreApplication::applicationDirPath()+QStringLiteral("/services/")+language;}

QString AppController::scienceServiceExecutable() const{
#ifdef Q_OS_WIN
    return qEnvironmentVariable("LOCALAPPDATA",QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))+QStringLiteral("/GraphVis/18.4/python-science/Scripts/graphvis-python-science.exe");
#else
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+QStringLiteral("/18.4/python-science/bin/graphvis-python-science");
#endif
}
bool AppController::scienceServiceAvailable() const{return QFileInfo::exists(scienceServiceExecutable());}
QString AppController::scienceServiceInstallHint() const{return scienceServiceAvailable()?scienceServiceExecutable():QStringLiteral("Advanced literature extraction uses the optional GraphVis Science add-on. The standard app works without Python.");}
void AppController::openLiterature(const QUrl& url){literatureUrl_=url;literatureAnalysis_.clear();workspaceMode_=QStringLiteral("Literature");emit workspaceModeChanged();emit literatureChanged();setStatus(QStringLiteral("Opened literature: %1").arg(QFileInfo(cleanLocalPath(url)).fileName()));}
void AppController::openLiteraturePath(const QString& path){
    openLiterature(QUrl::fromLocalFile(path));
}
void AppController::clearLiterature(){literatureUrl_=QUrl();literatureAnalysis_.clear();emit literatureChanged();}
void AppController::analyzeLiterature(){
    if(literatureUrl_.isEmpty()){setStatus("Open a PDF first");return;} if(busy_){setStatus("A native/science job is already running");return;}
    const QString exe=scienceServiceExecutable(); if(!QFileInfo::exists(exe)){setStatus(scienceServiceInstallHint());emit scienceServiceAvailabilityChanged();return;}
    literatureAnalysis_.clear();
    startScienceOp(QJsonObject{{"op","literature.extract"},{"path",cleanLocalPath(literatureUrl_)}},
                   QStringLiteral("literature.extract"),
                   QStringLiteral("Extracting literature intelligence"));
}


bool AppController::importFirstLiteratureDataset(){
    const QVariantList paths=literatureAnalysis_.value(QStringLiteral("saved_paths")).toList();
    if(paths.isEmpty()){setStatus(QStringLiteral("No extracted dataset is available yet"));return false;}
    const QString path=paths.first().toString();
    if(path.isEmpty()||!QFileInfo::exists(path)){setStatus(QStringLiteral("Extracted dataset path is unavailable"));return false;}
    return importDataset(QUrl::fromLocalFile(path));
}


bool AppController::exportProjectState(const QUrl& url){
    if(!runtime_)return false;
    const QString path=cleanLocalPath(url); if(path.isEmpty())return false;
    const QString json=NativeApi::instance().takeString(NativeApi::instance().stateJson(runtime_));
    QFile file(path); if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate)){setStatus(QStringLiteral("Could not export project state: %1").arg(file.errorString()));return false;}
    file.write(json.toUtf8()); file.write("\n"); file.close();
    setStatus(QStringLiteral("Exported native project recipe/provenance: %1").arg(QFileInfo(path).fileName())); return true;
}


// =========================================================================
// Graph catalogue (ported from GraphVis 17)
//
// config/graph_catalogue.json is generated from GraphVis 17's
// src/graphvis/rendering/graph_library.py and embedded as a QML module
// resource. It carries all 318 catalogue entries across 30 categories, each
// with its rendering engine key, axis-scale variant, advanced flag,
// description and the filename of its pre-rendered thumbnail.
//
// The thumbnails themselves are installed to share/graphvis/assets and are
// loaded from disk rather than embedded, so the executable stays small.
// =========================================================================
void AppController::loadGraphCatalogue(){
    QFile f(QStringLiteral(":/qt/qml/GraphVis/catalogue/graph_catalogue.json"));
    if(!f.open(QIODevice::ReadOnly)){
        qWarning("GraphVis: graph_catalogue.json is missing from the application resources");
        return;
    }
    QJsonParseError err{};
    const QJsonDocument doc=QJsonDocument::fromJson(f.readAll(),&err);
    if(err.error!=QJsonParseError::NoError||!doc.isObject()){
        qWarning("GraphVis: graph_catalogue.json is not valid JSON: %s",qPrintable(err.errorString()));
        return;
    }
    const QJsonArray cats=doc.object().value(QStringLiteral("categories")).toArray();
    for(const QJsonValue& cv:cats){
        const QJsonObject co=cv.toObject();
        const QString category=co.value(QStringLiteral("name")).toString();
        QVariantList entries;
        for(const QJsonValue& ev:co.value(QStringLiteral("entries")).toArray()){
            QVariantMap e=ev.toObject().toVariantMap();
            e.insert(QStringLiteral("category"),category);
            entries.append(e);
            graphEntries_.append(e);
        }
        QVariantMap cat;
        cat.insert(QStringLiteral("name"),category);
        cat.insert(QStringLiteral("count"),entries.size());
        cat.insert(QStringLiteral("entries"),entries);
        graphCategories_.append(cat);
    }

    // Thumbnails live beside the executable in a deployed layout, and in the
    // source tree when running straight out of the build directory.
    const QString appDir=QCoreApplication::applicationDirPath();
    const QStringList roots{appDir+QStringLiteral("/share/graphvis/assets"),
                            appDir+QStringLiteral("/../../assets"),
                            appDir+QStringLiteral("/assets")};
    for(const QString& root:roots){
        if(QDir(root+QStringLiteral("/graph_previews")).exists()){
            graphPreviewDir_=QDir(root+QStringLiteral("/graph_previews")).absolutePath();
            const QString compact=root+QStringLiteral("/graph_previews_compact");
            graphPreviewCompactDir_=QDir(compact).exists()?QDir(compact).absolutePath():graphPreviewDir_;
            break;
        }
    }
}

// Mirrors graph_library.fuzzy_score: exact substring on the name wins, then
// description/category, then a best-token similarity ratio.
double AppController::fuzzyScore(const QString& query,const QVariantMap& entry){
    const QString q=query.trimmed().toLower();
    if(q.isEmpty()) return 1.0;
    const QString name=entry.value(QStringLiteral("name")).toString().toLower();
    const QString engine=entry.value(QStringLiteral("engine")).toString().toLower();
    const QString desc=entry.value(QStringLiteral("description")).toString().toLower();
    const QString cat=entry.value(QStringLiteral("category")).toString().toLower();
    if(name.contains(q)||engine.contains(q)) return 1.0;
    if(desc.contains(q)||cat.contains(q)) return 0.9;

    // Token overlap: proportion of query characters covered by the best token.
    double best=0.0;
    // One compiled regex for the life of the program. This used to construct
    // three of them per catalogue entry, and searchGraphs() calls this for all
    // 318 entries - so every keystroke in the Graph Library compiled and threw
    // away nearly a thousand regular expressions.
    static const QRegularExpression kWordBreak(QStringLiteral("[^a-z0-9]+"));
    QStringList tokens=engine.split(kWordBreak,Qt::SkipEmptyParts);
    tokens+=name.split(kWordBreak,Qt::SkipEmptyParts);
    tokens+=desc.split(kWordBreak,Qt::SkipEmptyParts).mid(0,8);
    for(const QString& t:std::as_const(tokens)){
        if(t.isEmpty()) continue;
        if(t.startsWith(q)){best=qMax(best,0.85);continue;}
        int hit=0;
        for(const QChar c:q) if(t.contains(c)) ++hit;
        best=qMax(best,double(hit)/double(q.size()) * 0.6);
    }
    return best;
}

QVariantList AppController::searchGraphs(const QString& query,bool includeAdvanced,int limit) const{
    QList<QPair<double,QVariantMap>> scored;
    for(const QVariant& v:graphEntries_){
        const QVariantMap e=v.toMap();
        if(e.value(QStringLiteral("advanced")).toBool()&&!includeAdvanced) continue;
        const double score=fuzzyScore(query,e);
        if(query.trimmed().isEmpty()||score>=0.38) scored.append({score,e});
    }
    std::sort(scored.begin(),scored.end(),[](const QPair<double,QVariantMap>& a,const QPair<double,QVariantMap>& b){
        if(!qFuzzyCompare(a.first,b.first)) return a.first>b.first;
        const QString ca=a.second.value(QStringLiteral("category")).toString();
        const QString cb=b.second.value(QStringLiteral("category")).toString();
        if(ca!=cb) return ca<cb;
        return a.second.value(QStringLiteral("engine")).toString()<b.second.value(QStringLiteral("engine")).toString();
    });
    QVariantList out;
    for(const auto& p:std::as_const(scored)){
        if(limit>0&&out.size()>=limit) break;
        out.append(p.second);
    }
    return out;
}

QString AppController::graphThumbnail(const QString& fileName,bool compact) const{
    if(fileName.isEmpty()) return {};
    const QString dir=compact&&!graphPreviewCompactDir_.isEmpty()?graphPreviewCompactDir_:graphPreviewDir_;
    if(dir.isEmpty()) return {};
    const QString path=dir+QLatin1Char('/')+fileName;
    if(!QFileInfo::exists(path)) return {};
    return QUrl::fromLocalFile(path).toString();
}

QString AppController::exportPath(const QString& baseName,const QString& extension) const{
    const QString dir=QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                      +QStringLiteral("/GraphVis/exports");
    QDir().mkpath(dir);
    QString stem=baseName.isEmpty()?QStringLiteral("figure"):baseName;
    stem.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")),QStringLiteral("_"));
    return QDir(dir).absoluteFilePath(stem+QLatin1Char('.')+extension);
}


// =========================================================================
// Optional Python science service
//
// One PERSISTENT process, one JSON line per request, one JSON line per reply.
//
// This used to launch a fresh interpreter for every single request and kill it
// again as soon as the reply arrived. Importing pandas and pyarrow costs about
// 400 ms before the process has even opened the file, and a 12.7 MB CSV takes
// about 150 ms to actually convert - so two thirds of every import, every
// dataset scan and every literature extraction was interpreter start-up, paid
// again each time. service.py has always been a loop over stdin; nothing on the
// Python side had to change, the C++ side was simply hanging up after one turn.
//
// The reply handler must know which operation it is answering: before Smart
// Suite there was only one, and every reply was assumed to be a literature
// extraction.
// =========================================================================
bool AppController::ensureScienceService(){
    if(scienceProcess_.state()!=QProcess::NotRunning) return true;
    const QString exe=scienceServiceExecutable();
    if(!QFileInfo::exists(exe)){
        setStatus(scienceServiceInstallHint());
        emit scienceServiceAvailabilityChanged();
        return false;
    }
    scienceOutput_.clear();
    scienceProcess_.setProgram(exe);
    scienceProcess_.setArguments({});
    scienceProcess_.start();
    if(!scienceProcess_.waitForStarted(5000)){
        setStatus(QStringLiteral("Could not start science service: %1").arg(scienceProcess_.errorString()));
        return false;
    }
    return true;
}

bool AppController::startScienceOp(const QJsonObject& request,const QString& op,const QString& busyLabel){
    if(busy_){setStatus(QStringLiteral("A native/science job is already running"));return false;}
    if(!ensureScienceService()) return false;

    // A reply left over from a cancelled request would be read as the answer to
    // this one, so the buffer starts clean.
    scienceOutput_.clear();
    pendingScienceOp_=op;
    setBusy(true,busyLabel);
    const qint64 written=scienceProcess_.write(QJsonDocument(request).toJson(QJsonDocument::Compact)+"\n");
    if(written<0){
        // The pipe died between the state check and the write. Start a fresh
        // service and try exactly once more.
        scienceProcess_.kill();
        scienceProcess_.waitForFinished(2000);
        if(!ensureScienceService()||scienceProcess_.write(QJsonDocument(request).toJson(QJsonDocument::Compact)+"\n")<0){
            setStatus(QStringLiteral("Could not reach the science service"));
            pendingScienceOp_.clear(); setBusy(false); return false;
        }
    }
    return true;
}

void AppController::handleScienceReply(const QJsonObject& obj){
    const QString op=pendingScienceOp_;
    pendingScienceOp_.clear();
    const bool ok=obj.value(QStringLiteral("ok")).toBool();
    const QString error=obj.value(QStringLiteral("error")).toString();

    if(op==QStringLiteral("dataset.scan")){
        scanning_=false;
        if(ok){
            const QJsonObject scan=obj.value(QStringLiteral("scan")).toObject();
            scanRecommendations_=scan.value(QStringLiteral("recommendations")).toArray().toVariantList();
            scanSummary_=QVariantMap{
                {QStringLiteral("dataset_name"),scan.value(QStringLiteral("dataset_name")).toString()},
                {QStringLiteral("analysis_seconds"),scan.value(QStringLiteral("analysis_seconds")).toDouble()},
                {QStringLiteral("time_budget_seconds"),scan.value(QStringLiteral("time_budget_seconds")).toDouble()},
                {QStringLiteral("budget_exhausted"),scan.value(QStringLiteral("budget_exhausted")).toBool()},
                {QStringLiteral("examined_pairs"),scan.value(QStringLiteral("examined_pairs")).toInt()},
                {QStringLiteral("count"),scanRecommendations_.size()},
            };
            scanSummary_.insert(QStringLiteral("literature_used"),
                                obj.value(QStringLiteral("literature_used")).toInt());
            setStatus(scanRecommendations_.isEmpty()
                      ? QStringLiteral("Scan finished but found no usable mappings")
                      : QStringLiteral("Scan complete: %1 recommended graphs in %2 s")
                            .arg(scanRecommendations_.size())
                            .arg(scan.value(QStringLiteral("analysis_seconds")).toDouble(),0,'f',1));
        }else{
            scanRecommendations_.clear();
            scanSummary_=QVariantMap{{QStringLiteral("error"),error}};
            setStatus(QStringLiteral("Scan failed: %1").arg(error));
        }
        emit scanChanged();
        return;
    }

    if(op==QStringLiteral("io.import")){
        const QString source=pendingConvertSource_;
        pendingConvertSource_.clear();
        if(ok){
            const QString arrow=obj.value(QStringLiteral("arrow_path")).toString();
            // Remember which file this Arrow temporary came from, so the log row
            // and every message keep naming the file the user actually chose.
            convertedFrom_.insert(arrow,source);
            noteImport(source,QStringLiteral("converting"),
                       QStringLiteral("Read as %1 (%2 rows) - loading")
                           .arg(obj.value(QStringLiteral("kind")).toString())
                           .arg(obj.value(QStringLiteral("rows")).toInt()));
            setStatus(QStringLiteral("Converted %1 - importing").arg(QFileInfo(source).fileName()));
            // busy_ is still set by the caller of this handler, so the native
            // import is queued rather than run re-entrantly.
            QTimer::singleShot(0,this,[this,arrow]{
                importQueue_.prepend(arrow);
                pumpImportQueue();
            });
        }else{
            noteImport(source,QStringLiteral("failed"),
                       error.isEmpty()?QStringLiteral("The reader returned no data"):error);
            setStatus(QStringLiteral("Could not read %1").arg(QFileInfo(source).fileName()));
            QTimer::singleShot(0,this,&AppController::pumpImportQueue);
        }
        return;
    }

    if(op.startsWith(QLatin1String("analysis."))||op==QLatin1String("surface.estimate")){
        analysisResult_=obj.toVariantMap();
        emit analysisChanged();
        if(!ok){
            setStatus(QStringLiteral("%1 failed: %2").arg(analysisKind_,error));
            return;
        }
        // A surface comes back as a three-column Arrow grid. Queue it like any
        // other dataset and the existing field engines draw it - no new
        // geometry path for an estimated surface.
        if(op==QLatin1String("surface.estimate")){
            const QString grid=obj.value(QStringLiteral("path")).toString();
            const QString method=obj.value(QStringLiteral("method")).toString();
            if(!grid.isEmpty()&&QFileInfo::exists(grid)){
                QTimer::singleShot(0,this,[this,grid]{
                    importQueue_.prepend(grid);
                    pumpImportQueue();
                });
                setStatus(QStringLiteral("%1 surface ready (%2 of %3 cells estimated)")
                              .arg(method)
                              .arg(obj.value(QStringLiteral("estimated")).toInt())
                              .arg(obj.value(QStringLiteral("cells")).toInt()));
            }else{
                setStatus(QStringLiteral("%1 produced no grid file").arg(method));
            }
            return;
        }
        setStatus(QStringLiteral("%1 complete").arg(analysisKind_));
        return;
    }

    // literature.extract, and anything added later that fills the same slot.
    literatureAnalysis_=obj.toVariantMap();
    emit literatureChanged();
    setStatus(ok?QStringLiteral("Literature intelligence complete")
                :QStringLiteral("Literature service error: %1").arg(error));
}

bool AppController::literatureContextAvailable() const{
    return !literatureAnalysis_.value(QStringLiteral("semantic_context")).toMap().isEmpty();
}

// =========================================================================
// Analysis and surface estimation
//
// These reach modules that were written, tested and completely unreachable:
// the service exposed no operation for them and the Analysis tab carried three
// permanently disabled buttons. limits, forecast, fft, weibull, regression,
// pca, doe, advisor and domain all answer through one request shape.
// =========================================================================
bool AppController::runAnalysis(const QString& kind,const QVariantMap& options){
    if(activeDatasetId_.isEmpty()){
        setStatus(QStringLiteral("Import or select a dataset first"));
        return false;
    }
    const QString arrow=nativeArrowPath();
    if(arrow.isEmpty()||!QFileInfo::exists(arrow)){
        setStatus(QStringLiteral("The active dataset has no Arrow file yet"));
        return false;
    }
    const QString op=QStringLiteral("analysis.")+kind;
    QJsonObject request{{"op",op},{"arrow_path",arrow}};
    // Whatever the caller supplied travels as-is: an operation asks for the
    // columns and settings it needs and ignores the rest.
    for(auto it=options.constBegin();it!=options.constEnd();++it)
        request.insert(it.key(),QJsonValue::fromVariant(it.value()));

    analysisKind_=kind;
    return startScienceOp(request,op,QStringLiteral("Running %1").arg(kind));
}

bool AppController::estimateSurface(const QString& x,const QString& y,const QString& z,
                                    const QString& estimator,int resolution){
    if(activeDatasetId_.isEmpty()){
        setStatus(QStringLiteral("Import or select a dataset first"));
        return false;
    }
    const QString arrow=nativeArrowPath();
    if(arrow.isEmpty()||!QFileInfo::exists(arrow)){
        setStatus(QStringLiteral("The active dataset has no Arrow file yet"));
        return false;
    }
    if(x.isEmpty()||y.isEmpty()||z.isEmpty()){
        setStatus(QStringLiteral("A surface needs three mapped columns"));
        return false;
    }
    const QString cache=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        +QStringLiteral("/18.4/arrow-cache");
    QDir().mkpath(cache);
    QJsonObject request{{"op","surface.estimate"},{"arrow_path",arrow},
                        {"x",x},{"y",y},{"z",z},
                        {"estimator",estimator.isEmpty()?QStringLiteral("Auto (data-aware)"):estimator},
                        {"resolution",qBound(16,resolution,600)},
                        {"out_dir",cache}};
    analysisKind_=QStringLiteral("surface");
    return startScienceOp(request,QStringLiteral("surface.estimate"),
                          QStringLiteral("Estimating surface (%1)")
                              .arg(estimator.isEmpty()?QStringLiteral("auto"):estimator));
}

void AppController::scanDataset(double budgetSeconds,bool useLiterature){
    if(activeDatasetId_.isEmpty()){setStatus(QStringLiteral("Import or select a dataset first"));return;}
    const QString arrow=nativeArrowPath();
    if(arrow.isEmpty()||!QFileInfo::exists(arrow)){setStatus(QStringLiteral("The active dataset has no Arrow file yet"));return;}

    QJsonObject request{{"op","dataset.scan"},
                        {"arrow_path",arrow},
                        {"name",activeDataset().value(QStringLiteral("name")).toString()},
                        {"budget_seconds",budgetSeconds}};

    // Phase 4: an analysed paper contributes its variables and inferred plot
    // type, so the scanner ranks graphs the paper actually implies rather than
    // whatever the dataset alone suggests.
    if(useLiterature&&literatureContextAvailable()){
        QJsonObject entry{
            {"title",literatureAnalysis_.value(QStringLiteral("title")).toString()},
            {"semantic_context",QJsonObject::fromVariantMap(
                literatureAnalysis_.value(QStringLiteral("semantic_context")).toMap())}};
        request.insert(QStringLiteral("literature"),QJsonArray{entry});
    }
    if(startScienceOp(request,QStringLiteral("dataset.scan"),
                      QStringLiteral("Scanning dataset (%1 s budget)").arg(int(budgetSeconds)))){
        scanning_=true;
        scanRecommendations_.clear();
        scanSummary_.clear();
        emit scanChanged();
    }
}

void AppController::clearScan(){
    scanRecommendations_.clear(); scanSummary_.clear(); scanning_=false; emit scanChanged();
}

void AppController::setThemeIndex(int value){
    if(themeIndex_==value) return;
    themeIndex_=value;
    QSettings(QStringLiteral("GraphVis"),QStringLiteral("GraphVis 18.4"))
        .setValue(QStringLiteral("ui/themeIndex"),themeIndex_);
    emit themeIndexChanged();
}

QStringList AppController::displayModeNames() const{
    return {QStringLiteral("Windowed"),
            QStringLiteral("Maximised"),
            QStringLiteral("Fullscreen"),
            QStringLiteral("Borderless window")};
}

void AppController::setDisplayMode(int value){
    const int mode=qBound(0,value,3);
    if(displayMode_==mode) return;
    displayMode_=mode;
    QSettings(QStringLiteral("GraphVis"),QStringLiteral("GraphVis 18.4"))
        .setValue(QStringLiteral("ui/displayMode"),displayMode_);
    emit displayModeChanged();
}

// Choose the screen the user is actually looking at: the one under the mouse
// pointer, falling back to the primary screen. QGuiApplication has no notion of
// a "current" screen, so without this Qt centres on the virtual desktop and a
// dual-monitor machine gets a window split down the bezel.
static QScreen* graphvisTargetScreen(){
    if(QScreen* s=QGuiApplication::screenAt(QCursor::pos())) return s;
    return QGuiApplication::primaryScreen();
}

QRect AppController::targetScreenGeometry() const{
    QScreen* screen=graphvisTargetScreen();
    return screen?screen->geometry():QRect(0,0,1280,800);
}

QRect AppController::targetWorkAreaGeometry() const{
    QScreen* screen=graphvisTargetScreen();
    return screen?screen->availableGeometry():QRect(0,0,1280,800);
}

QRect AppController::preferredWindowGeometry(int width,int height) const{
    QScreen* screen=graphvisTargetScreen();
    const QRect work=screen?screen->availableGeometry():QRect(0,0,1280,800);

    // A geometry saved from a previous windowed session wins, but only if the
    // screen it was saved on still exists and still contains it - otherwise a
    // laptop undocked from its second monitor opens off-screen.
    const QSettings settings(QStringLiteral("GraphVis"),QStringLiteral("GraphVis 18.4"));
    const QRect saved=settings.value(QStringLiteral("ui/windowGeometry")).toRect();
    if(saved.isValid()&&saved.width()>=640&&saved.height()>=480){
        for(QScreen* candidate:QGuiApplication::screens()){
            const QRect area=candidate->availableGeometry();
            if(area.contains(saved.center())){
                QRect fitted=saved;
                fitted.setWidth(qMin(fitted.width(),area.width()));
                fitted.setHeight(qMin(fitted.height(),area.height()));
                if(!area.contains(fitted)) fitted.moveTo(area.topLeft());
                return fitted;
            }
        }
    }

    // Never open larger than the work area, and leave a small margin so the
    // window does not look like a failed maximise.
    const int w=qMin(width,work.width()-40);
    const int h=qMin(height,work.height()-40);
    return QRect(work.x()+(work.width()-w)/2,work.y()+(work.height()-h)/2,w,h);
}

void AppController::saveWindowGeometry(int x,int y,int width,int height){
    if(width<640||height<480) return;
    QSettings(QStringLiteral("GraphVis"),QStringLiteral("GraphVis 18.4"))
        .setValue(QStringLiteral("ui/windowGeometry"),QRect(x,y,width,height));
}

QStringList AppController::plotColourVisionNames() const{
    return graphvis::colourVisionNames();
}

QString AppController::plotColourVisionSummary() const{
    const QStringList names=graphvis::colourVisionNames();
    const QString chosen=names.value(qBound(0,plotColourVision_,names.size()-1));
    return plotColourVision_==0
        ? QStringLiteral("Graph colours: %1 - distinguishable for protanopia, deuteranopia and tritanopia").arg(chosen)
        : QStringLiteral("Graph colours: %1 - series palette fixed for this vision type").arg(chosen);
}

void AppController::setPlotColourVision(int value){
    const int mode=qBound(0,value,4);
    if(plotColourVision_==mode) return;
    plotColourVision_=mode;
    QSettings(QStringLiteral("GraphVis"),QStringLiteral("GraphVis 18.4"))
        .setValue(QStringLiteral("plot/colourVision"),plotColourVision_);
    emit plotColourVisionChanged();
    setStatus(plotColourVisionSummary());
}
