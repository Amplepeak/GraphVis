#include <cstdio>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QMutex>
#include <QThread>
#include <QAtomicInt>
#include <QTimer>
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
        // The startup log of the run that died is the other half of the story.
        // It is read from startup.previous.log, which main() renames it to
        // before writing this session's - see the note there. Reading
        // startup.log itself, which is what this did, returns the log of the
        // session writing this report.
        QFile previous(dir + QStringLiteral("/startup.previous.log"));
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

// EVERY WARNING THE INTERFACE PRODUCES WHILE IT STARTS, counted.
//
// Not a second log: the handler below already writes them all. This is the
// count, so --selftest-ui can fail the build on them instead of leaving them in
// a file for somebody to notice. See the flag for what that is worth.
QAtomicInt gStartupWarnings{0};

void graphvisMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (type == QtWarningMsg || type == QtCriticalMsg)
        gStartupWarnings.fetchAndAddOrdered(1);
    QString level;
    switch (type) {
    case QtDebugMsg: level = QStringLiteral("DEBUG"); break;
    case QtInfoMsg: level = QStringLiteral("INFO"); break;
    case QtWarningMsg: level = QStringLiteral("WARNING"); break;
    case QtCriticalMsg: level = QStringLiteral("CRITICAL"); break;
    case QtFatalMsg: level = QStringLiteral("FATAL"); break;
    }
    // WHICH THREAD, for the warnings that are only ever about a thread.
    //
    // "Timers cannot be started from another thread" and its relatives are Qt
    // telling you that an object was touched from somewhere it does not live.
    // The message names neither the object nor the thread, so a warning in
    // startup.log says only that it happened - which is where this one has sat
    // unexplained across several builds.
    //
    // Naming the thread is most of the answer. The GUI thread, the Qt Quick
    // scene-graph render thread and a QtConcurrent pool thread are three very
    // different bugs, and the first of them is not a bug at all. Cheap enough
    // to do unconditionally on the handful of messages that match: no symbols,
    // no backtrace library, no permanent cost.
    QString where;
    if(message.contains(QLatin1String("another thread"))
       ||message.contains(QLatin1String("different thread"))
       ||message.contains(QLatin1String("Cannot create children"))){
        const QThread* current = QThread::currentThread();
        const QThread* gui = QCoreApplication::instance()
                                 ? QCoreApplication::instance()->thread() : nullptr;
        const QString name = current ? current->objectName() : QString();
        where = QStringLiteral("  [thread %1%2%3]")
                    .arg(QString::number(reinterpret_cast<quintptr>(current),16),
                         name.isEmpty() ? QString() : QStringLiteral(" \"%1\"").arg(name),
                         current && current == gui ? QStringLiteral(" = GUI")
                                                   : QStringLiteral(" != GUI"));
    }
    appendStartupLog(QStringLiteral("[%1] %2%3%4")
                         .arg(level, message,
                              context.file ? QStringLiteral("  (%1:%2)").arg(QString::fromUtf8(context.file)).arg(context.line)
                                           : QString(),
                              where));
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
            // Unbuffered, because this output has to survive the process
            // dying. CHECK-GRAPHS.bat redirects stdout to a file, which makes
            // it block-buffered, and a self-test that crashes therefore loses
            // everything printed since the last 4 KB flush - including the
            // line naming whatever it was working on when it died.
            //
            // That is not hypothetical. A run ended in 0xC00000FD, a stack
            // overflow, and left an output file cut off mid-word inside an
            // earlier line: the one thing the tool exists to report, which
            // engine it was on, was the one thing lost. A diagnostic that
            // cannot describe its own crash is not a diagnostic.
            //
            // The cost is a write syscall per printf, on a run that already
            // renders hundreds of figures.
            setvbuf(stdout, nullptr, _IONBF, 0);
            setvbuf(stderr, nullptr, _IONBF, 0);
            // --selftest-gallery <dir>, optional and alongside the above.
            //
            // The sweep already renders every engine and then discards the
            // picture. This keeps them, one PNG per engine, which is the only
            // practical way to look at 434 figures: whether an engine put data
            // on the page is answered mechanically on every build, but whether
            // the legend covers the data, or an axis is labelled in the wrong
            // units, or the thing is readable at 89 mm, is not - and nobody
            // has yet looked.
            //
            // Off unless asked for, so the build check is unchanged and does
            // not start writing several hundred files.
            QString galleryDir;
            const int galleryFlag = args.indexOf(QStringLiteral("--selftest-gallery"));
            if (galleryFlag >= 0) {
                if (galleryFlag + 1 >= args.size()) {
                    fprintf(stderr, "--selftest-gallery needs an output directory\n");
                    return 2;
                }
                galleryDir = args.at(galleryFlag + 1);
            }
            // Both checks, so a build cannot pass while an engine draws nothing.
            const bool exportOk = graphvis::runPlotSelfTest(args.at(flag + 1));
            const bool enginesOk = graphvis::runEngineSweep(galleryDir);
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
    // THE LAST SESSION'S LOG, KEPT BEFORE THIS ONE OVERWRITES IT.
    //
    // reportUnexpectedExit() reads startup.log to attach "the startup log of
    // the run that died", and its own comment said "the next few lines of this
    // run are about to overwrite it". They were not about to - they already
    // had. The truncating write below used to happen FIRST, a dozen lines
    // before the report was generated, so every unexpected-exit report ever
    // written carried the log of the session doing the reporting, timestamped
    // milliseconds AFTER the crash it was supposed to explain. Both reports on
    // this machine show exactly that, and it is why two crashes went
    // undiagnosed.
    //
    // Renamed rather than copied-on-crash: the previous session's log is worth
    // having whether or not it died, and a rename cannot half-succeed.
    const QString previousLogPath = logDir + QStringLiteral("/startup.previous.log");
    QFile::remove(previousLogPath);
    QFile::rename(gStartupLogPath, previousLogPath);
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

    // --selftest-ui
    //
    // THE ONE CHECK NOTHING ELSE HERE PERFORMS: it opens the interface.
    //
    // Every other check in this project drives the backend directly. 320 guards
    // and a 434-engine sweep run without a window ever existing, and a QML
    // binding is only evaluated when one does. Everything that reached the user
    // in one week was invisible to all of them and visible within seconds of
    // launching the program:
    //
    //   Theme.fontSizeLarge          a property that does not exist
    //   root.Window.width            a property not on that type
    //   FigureTabBar.qml             a file the build had never been told about
    //   colourMapWarning             declared, never defined
    //   resetStaging                 staged a mapping and never applied it
    //
    // The first four are a name resolving to nothing. QML resolves names at
    // runtime, so none of them is a syntax error and qmllint passes them all.
    //
    // So: load the interface, let it settle, and fail if it complained. It runs
    // under -platform offscreen from BUILD-AND-CHECK, needs no display, and
    // takes about a second.
    //
    // It is deliberately NOT a test of behaviour. It cannot click anything and
    // it would not have caught the mapping bug above, which needs a dataset and
    // a panel. It catches the broken-name class, which is four of the five and
    // the cheapest to catch.
    if (QCoreApplication::arguments().contains(QStringLiteral("--selftest-ui"))) {
        // Two turns of the event loop, then a moment: bindings evaluate when
        // the component completes, but a Loader, an Instantiator and anything
        // deferred by Qt.callLater land a turn or two later - and the figure
        // restore in VisualizeWorkspace is exactly that.
        QCoreApplication::processEvents();
        QTimer::singleShot(1200, &app, [&app]{
            QCoreApplication::processEvents();
            const int warnings = gStartupWarnings.loadAcquire();
            if (warnings > 0) {
                fprintf(stderr,
                        "--selftest-ui: the interface produced %d warning(s) "
                        "while starting.\nThey are in:\n  %s\n"
                        "A QML name that resolves to nothing is not a syntax "
                        "error and qmllint does not see it.\n",
                        warnings, qPrintable(gStartupLogPath));
                app.exit(3);
                return;
            }
            fprintf(stderr, "--selftest-ui: interface loaded, no warnings.\n");
            app.exit(0);
        });
    }

    const int rc = app.exec();
    appendStartupLog(QStringLiteral("Application event loop exited with code %1").arg(rc));
    // A clean exit, so nothing is left for the next launch to find.
    closeSessionJournal();
    return rc;
}
