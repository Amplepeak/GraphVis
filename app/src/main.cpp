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
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <functional>
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

// WAS THIS MESSAGE CAUSED BY THE PROGRAM, OR BY HOW THE CHECK LAUNCHED IT?
//
// ONE QUESTION, ONE ANSWER, and it used to have two. --selftest-ui counted the
// font-directory warning and exited 3; tools/build_report.py matched the same
// message against a list of its own and reported "problems: none". Both were
// defensible and they disagreed on every run, which is the shape this project
// pays for most often - and the one the reader resolves by believing whichever
// of the two they happened to read.
//
// The answer belongs here because this is the side that KNOWS. The check runs
// the binary under `-platform offscreen`; Qt's offscreen plugin uses the
// generic font database and looks for a deployed lib/fonts, where the Windows
// plugin uses the system fonts and says nothing. Measured 2026-09-16: the
// offscreen run at 17:45:57 logged it and a normal launch of the same binary at
// 18:08:58 did not. A normal launch therefore never reaches this branch, and if
// the message ever appears WITHOUT the offscreen platform it is counted, which
// is exactly right - that would be the program's fault.
//
// The log line carries the reason with it, so the report reads a STATEMENT
// rather than matching a message by name in a table of its own. A table of
// names in a second file is how a message that stopped being harmless goes on
// being excused.
bool gOffscreenPlatform = false;

QString harnessCause(QtMsgType type, const QString& message)
{
    if (type != QtWarningMsg && type != QtCriticalMsg)
        return QString();
    if (gOffscreenPlatform
        && message.contains(QLatin1String("QFontDatabase: Cannot find font directory")))
        return QStringLiteral("caused by -platform offscreen, which is how the check "
                              "runs this binary; a normal launch does not emit it");
    return QString();
}

void graphvisMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const QString harness = harnessCause(type, message);
    if ((type == QtWarningMsg || type == QtCriticalMsg) && harness.isEmpty())
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
    // ONE MESSAGE, ONE LINE. Qt writes multi-line messages - the font warning
    // carries a two-sentence note after a newline - and the report reads this
    // file a line at a time. Left as it came, the first physical line held the
    // text that identifies the fault and the SECOND held the [HARNESS] marker,
    // so the report saw an unmarked warning followed by a line it did not
    // recognise, and counted the very message the marker exists to excuse. The
    // marker was written and correct; it was on the wrong line.
    //
    // Flattened here rather than worked around there, because a message split
    // across lines is a message any line-based reader can only half see. The
    // wrap is kept visible as "  |  " so nothing is silently run together.
    // ASCII, not a middle dot: QLatin1String compares byte by byte, so a
    // non-ASCII literal in one can never match - there is a standing check for
    // exactly that, and it caught this one.
    QString flat = message;
    flat.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    flat.replace(QLatin1Char('\n'), QLatin1String("  |  "));
    appendStartupLog(QStringLiteral("[%1]%2 %3%4%5")
                         .arg(level,
                              // IMMEDIATELY AFTER THE LEVEL, before the message
                              // rather than after it. The message's length and
                              // shape are Qt's business; this marker's position
                              // must not depend on them.
                              harness.isEmpty() ? QString()
                                                : QStringLiteral(" [HARNESS: %1]").arg(harness),
                              flat,
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

// ---------------------------------------------------------------------------
// The interface, ACTUALLY WALKED.
//
// WHY THIS EXISTS, in one sentence: on 16 September a build shipped that died
// with an access violation on every single launch, and every check this project
// runs said it was fine.
//
// The fault was `QSet<QString>(f().begin(), f().end())` in AppController -
// two calls, two temporaries, an iterator pair straddling both - reached from a
// QML binding on a file dialog in the Data panel. Why nothing saw it:
//
//   the 440-engine sweep and the gallery never construct an AppController;
//   the QML interface tests use GENERATED STAND-INS for AppController and
//     PlotCanvas, so the stand-in's exportNameFilters() returned an empty list
//     and the real one was never called;
//   qmllint cannot see into C++ at all;
//   --selftest-ui below opened the window, which crashed - and the build report
//     counted warning lines in a log the dead process never wrote to, so zero
//     complaints scored as an improvement.
//
// The common shape: a defect in real C++ reached from a QML binding is
// invisible to every check that does not build the real objects AND then touch
// the panel that reads them. --selftest-ui builds the real objects. It has
// never touched a panel - its own note above says so: "It is deliberately NOT
// a test of behaviour."
//
// So this walks. It puts the shell into each workspace and each layout in turn,
// adds and removes a figure, and ATTRIBUTES every warning to the step that
// caused it. "Opening Data produced 2 warnings" is a bug report; "the interface
// produced 2 warnings" is a puzzle.
//
// REPORTING, NOT YET ASSERTING. This pass names what it found and does not fail
// the build on it, for the same reason the frame-overrun check spent a week
// reporting before it was allowed to fail: a check whose first act is to break
// the build is a check that gets switched off before anyone reads it. Once a
// few clean runs have gone by, the count below becomes a failure.
//
// It also must never be the thing that takes the program down. Every step is
// skipped with a named reason when its precondition is absent, and nothing here
// throws.
namespace {

// Pump the event loop for a while. Bindings evaluate on completion, but a
// Loader, an Instantiator and anything deferred by Qt.callLater land a turn or
// two later - which is most of what changing a workspace sets off.
void settleFor(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

// The first object whose QML type name starts with `prefix`. QML types get
// metaobject names like "VisualizeWorkspace_QMLTYPE_42", so the prefix is the
// component's own file name and nothing else can match it.
QObject* findQmlType(QObject* from, const char* prefix)
{
    if (!from)
        return nullptr;
    if (QLatin1StringView(from->metaObject()->className()).startsWith(QLatin1StringView(prefix)))
        return from;
    const QObjectList kids = from->children();
    for (QObject* kid : kids)
        if (QObject* found = findQmlType(kid, prefix))
            return found;
    return nullptr;
}

struct StepResult {
    QString name;
    int warnings = 0;
    QString skipped;        // empty when the step ran
    QString failed;         // empty when the step was satisfied
};

// Run one step and charge it with whatever the interface complained about
// while it was running.
StepResult runStep(const QString& name, const std::function<QString()>& body, int settleMs)
{
    StepResult r;
    r.name = name;
    const int before = gStartupWarnings.loadAcquire();
    r.failed = body();
    settleFor(settleMs);
    r.warnings = gStartupWarnings.loadAcquire() - before;
    return r;
}

int walkTheInterface(AppController& controller, QQmlApplicationEngine& engine)
{
    QList<StepResult> results;

    QObject* shell = engine.rootObjects().isEmpty() ? nullptr
                                                    : engine.rootObjects().constFirst();

    // ---- every workspace ------------------------------------------------
    //
    // This is the step that would have caught the crash: the Data workspace
    // builds DataWorkspace, whose Save dialog binds `nameFilters` to the
    // controller method that walked off the end of a freed buffer.
    const QStringList modes{QStringLiteral("Home"), QStringLiteral("Literature"),
                            QStringLiteral("Visualize"), QStringLiteral("Data"),
                            QStringLiteral("Analysis"), QStringLiteral("Publish")};
    const QString startingMode = controller.workspaceMode();
    for (const QString& mode : modes) {
        results.append(runStep(QStringLiteral("workspace: %1").arg(mode),
                               [&controller, &mode]() -> QString {
                                   controller.setWorkspaceMode(mode);
                                   return QString();
                               }, 220));
    }
    controller.setWorkspaceMode(startingMode);
    settleFor(120);

    // ---- every window shape ---------------------------------------------
    //
    // Each layout builds a different shell: different rails, different panels,
    // a different secondary panel. tst_workspace_loads already does this
    // against stand-ins; this does it against the real controller and the real
    // canvas, which is the difference that matters.
    const QStringList layouts = controller.uiLayoutNames();
    const int startingLayout = controller.uiLayout();
    for (int i = 0; i < layouts.size(); ++i) {
        results.append(runStep(QStringLiteral("layout %1: %2").arg(i).arg(layouts.at(i)),
                               [&controller, i]() -> QString {
                                   controller.setUiLayout(i);
                                   return QString();
                               }, 180));
    }
    controller.setUiLayout(startingLayout);
    settleFor(200);

    // ---- figures, which is where pressing things matters ----------------
    //
    // The notebook delegate that reported every figure as figure zero was
    // invisible to everything until something selected a figure that was not
    // the first. This adds one, selects it, and closes it, checking the count
    // each time - so a close button that removes the wrong figure shows up as
    // a number rather than as a picture nobody looked at.
    results.append(runStep(QStringLiteral("figures: add, select, close"),
        [shell]() -> QString {
            QObject* ws = findQmlType(shell, "VisualizeWorkspace");
            if (!ws)
                return QStringLiteral("SKIP: no VisualizeWorkspace in the tree");
            const QVariant countBefore = ws->property("figureCount");
            if (!countBefore.isValid())
                return QStringLiteral("SKIP: VisualizeWorkspace has no figureCount");
            const int before = countBefore.toInt();
            if (!QMetaObject::invokeMethod(ws, "addFigure"))
                return QStringLiteral("SKIP: addFigure could not be invoked");
            settleFor(200);
            const int added = ws->property("figureCount").toInt();
            if (added != before + 1)
                return QStringLiteral("adding a figure left %1 figures, expected %2")
                       .arg(added).arg(before + 1);
            if (!QMetaObject::invokeMethod(ws, "selectFigure", Q_ARG(QVariant, QVariant(before))))
                return QStringLiteral("SKIP: selectFigure could not be invoked");
            settleFor(150);
            const int chosen = ws->property("figureIndex").toInt();
            if (chosen != before)
                return QStringLiteral("selecting figure %1 left figure %2 current")
                       .arg(before).arg(chosen);
            if (!QMetaObject::invokeMethod(ws, "closeFigure", Q_ARG(QVariant, QVariant(before))))
                return QStringLiteral("SKIP: closeFigure could not be invoked");
            settleFor(200);
            const int after = ws->property("figureCount").toInt();
            if (after != before)
                return QStringLiteral("closing the figure just added left %1, expected %2")
                       .arg(after).arg(before);
            return QString();
        }, 150));

    // ---- what it found ---------------------------------------------------
    int noisy = 0;
    int unsatisfied = 0;
    for (const StepResult& r : results) {
        const bool isSkip = r.failed.startsWith(QLatin1String("SKIP:"));
        if (r.warnings > 0 || !r.failed.isEmpty()) {
            QString line = QStringLiteral("--selftest-ui: %1").arg(r.name);
            if (r.warnings > 0)
                line += QStringLiteral("  [%1 warning(s)]").arg(r.warnings);
            if (!r.failed.isEmpty())
                line += QStringLiteral("  %1").arg(r.failed);
            appendStartupLog(line);
            fprintf(stderr, "%s\n", qPrintable(line));
        }
        if (r.warnings > 0)
            ++noisy;
        if (!r.failed.isEmpty() && !isSkip)
            ++unsatisfied;
    }
    const QString summary =
        QStringLiteral("--selftest-ui: walked %1 step(s); %2 produced warnings, "
                       "%3 did not do what they claim")
        .arg(results.size()).arg(noisy).arg(unsatisfied);
    appendStartupLog(summary);
    fprintf(stderr, "%s\n", qPrintable(summary));
    return unsatisfied;
}

} // namespace


int main(int argc, char *argv[])
{
    // READ FROM argv, NOT FROM QGuiApplication::platformName().
    //
    // The platform name is only available once the application object exists,
    // and messages are logged before that. Read here, before anything can be
    // logged, so harnessCause gives the same answer on the first message as on
    // the last. Both spellings, because the check writes `-platform offscreen`
    // and Qt also accepts `-platform=offscreen`.
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("-platform") && i + 1 < argc)
            gOffscreenPlatform = QString::fromLocal8Bit(argv[i + 1])
                                     .startsWith(QLatin1String("offscreen"));
        else if (arg.startsWith(QLatin1String("-platform=")))
            gOffscreenPlatform = arg.mid(10).startsWith(QLatin1String("offscreen"));
    }
    if (qEnvironmentVariable("QT_QPA_PLATFORM").startsWith(QLatin1String("offscreen")))
        gOffscreenPlatform = true;

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

    // --first-run: START AS SOMEBODY WHO HAS NEVER RUN THIS.
    //
    // Every check this project has ever run does so as the developer, whose
    // %LOCALAPPDATA% holds settings, an arrow cache, a scan cache and a
    // previous session with a dataset in it - all of which the interface
    // restores on the way up. A new install has none of that, and it is a
    // different path through the same code: no cached dataset, no figure to
    // restore, every default taken rather than read back. Nobody had ever run
    // it, and it is the one path a release cannot get a second go at.
    //
    // WHY NOT JUST POINT %LOCALAPPDATA% SOMEWHERE ELSE - which is what the
    // build script tried first, and why it silently did nothing. On Windows
    // QStandardPaths does not read that variable: it asks the shell, through
    // SHGetKnownFolderPath(FOLDERID_LocalAppData). So the app went on writing
    // to the real profile, the redirected folder stayed empty, and the check
    // reported nothing because there was nothing to find. Measured: two full
    // runs produced no startup-firstrun.log at all.
    //
    // Qt has the switch this actually needs. setTestModeEnabled(true) moves
    // every writable standard path into a `qttest` subfolder - so this writes
    // to a place of its own, never touches the real profile, and cannot
    // corrupt a session by being interrupted. Emptying it first is what makes
    // the run a FIRST one rather than a second.
    if (QCoreApplication::arguments().contains(QStringLiteral("--first-run"))) {
        QStandardPaths::setTestModeEnabled(true);
        const QString fresh =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (!fresh.isEmpty())
            QDir(fresh).removeRecursively();
        QStandardPaths::setTestModeEnabled(true);
        fprintf(stderr, "--first-run: starting with an empty profile at\n  %s\n",
                qPrintable(QStandardPaths::writableLocation(
                    QStandardPaths::AppLocalDataLocation)));
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
    // IT USED TO SAY, HERE, that this was "deliberately NOT a test of
    // behaviour - it cannot click anything", and that it "would not have caught
    // the mapping bug above, which needs a dataset and a panel".
    //
    // That was true and it was the hole a crash walked through: a build that
    // died on every launch passed this check, because the fault was in real C++
    // reached from a binding on a panel this never opened. So it presses things
    // now - see walkTheInterface above, which puts the shell into each
    // workspace and each layout and adds, selects and closes a figure.
    //
    // The two halves are still separate on purpose. The count below is about
    // STARTING and fails the build, unchanged. The walk reports and does not
    // fail yet.
    if (QCoreApplication::arguments().contains(QStringLiteral("--selftest-ui"))) {
        // Two turns of the event loop, then a moment: bindings evaluate when
        // the component completes, but a Loader, an Instantiator and anything
        // deferred by Qt.callLater land a turn or two later - and the figure
        // restore in VisualizeWorkspace is exactly that.
        QCoreApplication::processEvents();
        QTimer::singleShot(1200, &app, [&app, &controller, &engine]{
            QCoreApplication::processEvents();
            // The count from STARTING, taken before the walk adds to it, so the
            // existing verdict keeps meaning exactly what it meant before.
            const int warnings = gStartupWarnings.loadAcquire();
            // Then press things. Reporting only - see walkTheInterface.
            walkTheInterface(controller, engine);
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
