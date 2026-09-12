#include <cstdio>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QMutex>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include "AppController.h"
#include "PlotSelfTest.h"
#include "SplashWindow.h"

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {
QMutex gLogMutex;
QString gStartupLogPath;
QtMessageHandler gPreviousHandler = nullptr;

// ------------------------------------------------------------------ crash journal
//
// GraphVis 17 kept a small session journal outside Qt so that a process which
// disappeared - a GPU driver fault in the viewport, an out-of-memory kill on a
// large import - left something behind. v18 had startup.log and
// COLLECT-CRASH-REPORT.bat, which are only useful when there is still a process
// to run them from; a silent disappearance left the next launch with no idea
// anything had happened.
//
// This is that journal and nothing more. No freeze watchdog and no debug
// drawer: a watchdog costs a permanent background thread to report a hang the
// user is already looking at.
//
// The contract is one line: a session writes a journal on start and deletes it
// on a clean exit. A journal still present at the next start is, by
// construction, a session that never got to its own exit.
QString gJournalPath;

QString journalDirectory(){
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
           + QStringLiteral("/18.4/logs");
}

// Turn a journal left over from a previous run into a dated report, and return
// a one-line summary for the log. Empty when the last run exited cleanly.
QString reportUnexpectedExit(){
    const QString dir = journalDirectory();
    const QString stale = dir + QStringLiteral("/session.journal");
    if(!QFile::exists(stale)) return QString();

    QString contents;
    {
        QFile f(stale);
        if(f.open(QIODevice::ReadOnly | QIODevice::Text))
            contents = QString::fromUtf8(f.readAll());
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString reportPath = dir + QStringLiteral("/unexpected-exit-") + stamp + QStringLiteral(".log");
    QFile report(reportPath);
    if(report.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)){
        QTextStream out(&report);
        out << "GraphVis 18.4.0 unexpected exit\n"
            << "Detected: " << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << "\n"
            << "\nThe previous session wrote this journal and never removed it, which means it\n"
               "did not reach its own shutdown. The journal follows.\n\n"
            << contents;
        // The startup log of the run that died is the other half of the story,
        // and the next few lines of this run are about to overwrite it.
        QFile previous(dir + QStringLiteral("/startup.log"));
        if(previous.open(QIODevice::ReadOnly | QIODevice::Text)){
            out << "\n--- startup.log from that session ---\n"
                << QString::fromUtf8(previous.readAll());
        }
    }
    QFile::remove(stale);
    return reportPath;
}

void openSessionJournal(){
    const QString dir = journalDirectory();
    QDir().mkpath(dir);
    gJournalPath = dir + QStringLiteral("/session.journal");
    QFile f(gJournalPath);
    if(!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    QTextStream out(&f);
    out << "Session started: " << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << "\n"
        << "Executable: " << QCoreApplication::applicationFilePath() << "\n"
        << "Qt runtime: " << qVersion() << "\n";
}

// Called only on the way out of main. If the process dies before this, the
// journal stays on disk and the next launch reports it - which is the point.
void closeSessionJournal(){
    if(gJournalPath.isEmpty()) return;
    QFile::remove(gJournalPath);
    gJournalPath.clear();
}

void appendStartupLog(const QString& line)
{
    if (gStartupLogPath.isEmpty())
        return;
    QMutexLocker locker(&gLogMutex);
    QFile f(gStartupLogPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    QTextStream out(&f);
    out << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
        << "  " << line << '\n';
    out.flush();
}

void graphvisMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    QString level;
    switch (type) {
    case QtDebugMsg: level = QStringLiteral("DEBUG"); break;
    case QtInfoMsg: level = QStringLiteral("INFO"); break;
    case QtWarningMsg: level = QStringLiteral("WARNING"); break;
    case QtCriticalMsg: level = QStringLiteral("CRITICAL"); break;
    case QtFatalMsg: level = QStringLiteral("FATAL"); break;
    }
    appendStartupLog(QStringLiteral("[%1] %2%3")
                         .arg(level, message,
                              context.file ? QStringLiteral("  (%1:%2)").arg(QString::fromUtf8(context.file)).arg(context.line)
                                           : QString()));
    if (gPreviousHandler)
        gPreviousHandler(type, context, message);
    else
        // There is normally no prior handler, so without this every
        // qDebug/qWarning stopped reaching the console the moment the handler
        // was installed and existed only in startup.log.
        fprintf(stderr, "[%s] %s\n", qPrintable(level), qPrintable(message));
}

void showFatalStartupMessage(const QString& reason)
{
    const QString text = reason + QStringLiteral("\n\nStartup log:\n") + gStartupLogPath;
#ifdef Q_OS_WIN
    MessageBoxW(nullptr,
                reinterpret_cast<LPCWSTR>(text.utf16()),
                L"GraphVis 18.4.0 could not start",
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
#else
    qCritical().noquote() << text;
#endif
}
}

int main(int argc, char *argv[])
{
    QGuiApplication::setOrganizationName(QStringLiteral("GraphVis"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("graphvis.local"));
    QGuiApplication::setApplicationName(QStringLiteral("GraphVis 18.4"));
    QGuiApplication::setApplicationVersion(QStringLiteral("18.4.0"));

    // Graphics API.
    //
    // QQuickVTKItem requires an OpenGL Qt Quick scene graph, so this used to
    // force OpenGL unconditionally. That was a bad trade: VTK/PBR is a
    // specialist viewport most sessions never open, and on Windows the forced
    // OpenGL scene graph does not survive the native window being recreated -
    // which is exactly what changing a window's frame style does. The window
    // came back painting black.
    //
    // So OpenGL is now requested only when the VTK viewport is the renderer the
    // user last chose. Everything else gets the platform default (Direct3D 11
    // on Windows), which handles window recreation correctly and is faster for
    // the 2-D plotting that is the core of the application.
    const bool wantOpenGl=
        QSettings(QStringLiteral("GraphVis"),QStringLiteral("GraphVis 18.4"))
            .value(QStringLiteral("ui/rendererMode")).toString()==QStringLiteral("VTK / PBR");
    if(wantOpenGl)
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    AppController::setGraphicsApiIsOpenGL(wantOpenGl);

    // Fusion, not Basic.
    //
    // The Basic style hardcodes control backgrounds, so a ComboBox or SpinBox
    // stays white on a dark theme no matter what palette it is given - which is
    // why those controls ignored the theme and their text was unreadable.
    // Fusion is fully palette-driven, so the whole control set follows Theme.
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    QGuiApplication app(argc, argv);

    // --selftest-plot <out.pdf>
    //
    // Renders a known figure through QtPlotBackend into a real QPdfWriter and
    // exits. This is how the vector-export path is verified without driving the
    // UI: the same backend code draws the screen and the PDF, so if this
    // produces a valid vector page the interactive path is drawing correctly
    // too. tools/Full-Diagnose.ps1 runs it on every build.
    {
        const QStringList args = QCoreApplication::arguments();
        const int flag = args.indexOf(QStringLiteral("--selftest-plot"));
        if (flag >= 0) {
            if (flag + 1 >= args.size()) {
                fprintf(stderr, "--selftest-plot needs an output path\n");
                return 2;
            }
            // Both checks, so a build cannot pass while an engine draws nothing.
            const bool exportOk = graphvis::runPlotSelfTest(args.at(flag + 1));
            const bool enginesOk = graphvis::runEngineSweep();
            const bool regressionOk = graphvis::runRegressionChecks();
            // The whole-catalogue property checks: row order, constant columns
            // and ink on the rim, asked of all 434 engines at once. Reporting,
            // so a build log says what every engine answered rather than only
            // which ones failed.
            const bool propertiesOk = graphvis::runPropertyChecks(true);
            return (exportOk && enginesOk && regressionOk && propertiesOk) ? 0 : 1;
        }
    }

    const QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                           + QStringLiteral("/18.4/logs");
    QDir().mkpath(logDir);
    gStartupLogPath = logDir + QStringLiteral("/startup.log");
    {
        QFile f(gStartupLogPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream out(&f);
            out << "GraphVis 18.4.0 startup diagnostics\n"
            << "Scene graph: " << (wantOpenGl ? "OpenGL (for VTK / PBR)" : "platform default") << "\n"
                << "Timestamp: " << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << "\n"
                << "Executable: " << QCoreApplication::applicationFilePath() << "\n"
                << "Application directory: " << QCoreApplication::applicationDirPath() << "\n"
                << "Qt runtime: " << qVersion() << "\n";
        }
    }
    // Before anything else can crash: report a journal the last session left
    // behind, then open this session's own.
    const QString unexpectedExit = reportUnexpectedExit();
    openSessionJournal();

    gPreviousHandler = qInstallMessageHandler(graphvisMessageHandler);
    // Restored before main returns. Left installed, it outlives gLogMutex and
    // gStartupLogPath - both namespace-scope objects with destructors - so any
    // Qt warning emitted from a later static destructor touched a destroyed
    // QMutex and crashed on exit, after the user had closed the window.
    struct HandlerGuard {
        ~HandlerGuard(){ qInstallMessageHandler(gPreviousHandler); }
    } handlerGuard;

    if(!unexpectedExit.isEmpty())
        appendStartupLog(QStringLiteral("The previous session ended unexpectedly. Report written to %1")
                             .arg(unexpectedExit));
    appendStartupLog(QStringLiteral("Starting Qt GUI application"));
    // ":" and not "qrc:". The qrc: form is a URL, which QML and QUrl
    // understand and QIcon does not - it is handed to QFile, which has never
    // heard of a scheme, so this silently produced an EMPTY icon and the
    // window, the taskbar button and the Alt+Tab card have carried the generic
    // Windows placeholder ever since. QIcon has no way to complain: an icon
    // that failed to load and an icon nobody set look identical.
    app.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/GraphVis/assets/graphvis_icon.png")));

    // The splash comes up before AppController, because AppController's
    // constructor is where start-up actually spends its time.
    graphvis::SplashWindow splash;
    splash.show();
    splash.setStage(QStringLiteral("Starting"),0.12);
    AppController::setStartupReporter([&splash](const QString& stage,double fraction){
        splash.setStage(stage,fraction);
    });

    AppController controller;
    AppController::setStartupReporter(nullptr);
    // Say so in the window rather than only in a log nobody opens. This is the
    // whole user-facing half of the crash journal: one sentence naming the file.
    if(!unexpectedExit.isEmpty())
        controller.notify(QStringLiteral("The last session ended unexpectedly. Report: %1")
                              .arg(QFileInfo(unexpectedExit).fileName()));
    appendStartupLog(QStringLiteral("AppController constructed; status: %1").arg(controller.status()));

    splash.setStage(QStringLiteral("Building the interface"),0.74);
    QQmlApplicationEngine engine;
    // GraphVis.VTK is an installed, dynamically-loaded QML module. The main
    // module is compiled into qrc:/qt/qml; appDir/qml is added solely for
    // deployable specialist plugins such as GraphVis.VTK.
    const QString deployedQml = QCoreApplication::applicationDirPath() + QStringLiteral("/qml");
    engine.addImportPath(deployedQml);
    appendStartupLog(QStringLiteral("Added deployed QML import path: %1").arg(deployedQml));

    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError>& warnings) {
                         for (const auto& warning : warnings)
                             appendStartupLog(QStringLiteral("QML: %1").arg(warning.toString()));
                     });

    engine.setInitialProperties({{QStringLiteral("app"), QVariant::fromValue(&controller)}});
    appendStartupLog(QStringLiteral("Loading GraphVis/Main QML module"));
    splash.setStage(QStringLiteral("Loading the workspace"),0.88);
    engine.loadFromModule("GraphVis", "Main");

    if (engine.rootObjects().isEmpty()) {
        splash.close();
        appendStartupLog(QStringLiteral("FATAL: QQmlApplicationEngine created zero root objects"));
        showFatalStartupMessage(QStringLiteral(
            "The GraphVis QML interface failed to load. This is commonly caused by a missing Qt/QML runtime module or an incomplete deployed plugin directory."));
        // Reported through its own dialog and its own log line, so this is not
        // the silent disappearance the journal exists to catch.
        closeSessionJournal();
        return 1;
    }

    splash.setStage(QStringLiteral("Ready"),1.0);
    splash.finish();
    appendStartupLog(QStringLiteral("Main QML window created successfully; entering event loop"));
    const int rc = app.exec();
    appendStartupLog(QStringLiteral("Application event loop exited with code %1").arg(rc));
    // A clean exit, so nothing is left for the next launch to find.
    closeSessionJournal();
    return rc;
}
