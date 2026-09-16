"""Does each check actually catch the thing it claims to catch?

A check that reports nothing is indistinguishable from a check that is
broken. This project has already shipped one guard that could never fire -
`find_top_level` looking for `(` when its own loop made that impossible - and
nothing noticed, because "no findings" and "no bugs" read the same in a
report.

So every check gets two fixtures: a POSITIVE it must find, and a NEGATIVE it
must leave alone. The negative half is the important half. A check that
flags everything passes the positive test perfectly.

    python tools/passive_audit.py --selftest
"""
from __future__ import annotations

import tempfile
from pathlib import Path

# A function big enough to reach oversized_function's thresholds at all.
#
# Generated rather than typed, because the check only looks at functions over
# 220 lines or complexity 60, and a fixture written by hand at that size would
# be unreadable - and an unreadable fixture is one nobody can tell is testing
# the right thing.
def _many_branches(n: int, terminating: bool) -> str:
    """`n` top-level `if`s, each either returning or falling through.

    Terminating gives a DISPATCH CHAIN - the shape the check must leave alone,
    because long is the correct shape for a dispatch table. Falling through
    gives a function with the same length and the same complexity that is
    genuinely tangled, and must still be reported. The two differ in one
    keyword, which is what makes the pair worth having: anything that passes
    both is reading the property rather than the size.
    """
    body = []
    for i in range(n):
        if terminating:
            body.append(f'    if(name=="e{i}"){{ out.v={i}; return out; }}')
        else:
            body.append(f'    if(name=="e{i}"){{ out.v+={i}; }}')
    return ("struct R{int v;};\n"
            "R pick(const char* name){\n"
            "    R out{0};\n" + "\n".join(body) + "\n    return out;\n}\n")


# (check id, source that must be flagged, source that must NOT be flagged)
CPP_CASES: list[tuple[str, str, str]] = [
    ("guard_after_use", """
struct N { int v; N* next; };
int bad(N* p){
    int a = p->v;
    if(!p) return 0;
    return a;
}
""", """
struct N { int v; N* next; };
int good(N* p){
    if(!p) return 0;
    return p->v;
}
"""),

    ("unreachable_code", """
int bad(int x){
    return x;
    x = 2;
}
""", """
int good(int x){
    switch(x){
    case 1: return 1;
    case 2: return 2;
    }
    return 0;
}
"""),

    ("switch_fallthrough", """
int bad(int x){
    int r=0;
    switch(x){
    case 1:
        r = 1;
    case 2:
        r = 2;
        break;
    }
    return r;
}
""", """
int good(int x){
    int r=0;
    switch(x){
    case 1:
        r = 1;
        break;
    case 2:
        // falls through
    case 3:
        r = 2;
        break;
    }
    return r;
}
"""),

    ("identical_branches", """
int bad(bool c){
    if(c){ return 1; } else { return 1; }
}
""", """
int good(bool c){
    if(c){ return 1; } else { return 2; }
}
"""),

    ("duplicate_condition", """
int bad(int x){
    if(x > 1) return 1;
    else if(x > 2) return 2;
    else if(x > 1) return 3;
    return 0;
}
""", """
int good(int x){
    if(x > 1) return 1;
    else if(x > 2) return 2;
    return 0;
}
"""),

    ("repeated_subcondition", """
bool bad(bool a,bool b){
    if(a && a) return true;
    return b;
}
""", """
bool good(bool a,bool b){
    if(a && b) return true;
    return b;
}
"""),

    ("self_comparison", """
bool bad(int a){
    if(a < a) return true;
    return false;
}
""", """
bool good(double a){
    if(a != a) return true;      // the NaN idiom, not a self-comparison bug
    if(a == a) return false;
    return false;
}
"""),

    ("assert_with_work", """
void bad(int* p){
    Q_ASSERT(doTheWork(p));
}
""", """
void good(int* p){
    Q_ASSERT(p != nullptr);
}
"""),

    ("connect_without_context", """
void bad(){
    connect(sender, &A::sig, [this]{ useIt(); });
}
""", """
void good(){
    connect(sender, &A::sig, this, [this]{ useIt(); });
}
"""),

    ("swallowed_exception", """
void bad(){
    try { risky(); } catch(const std::exception&) { }
}
""", """
void good(){
    try { risky(); } catch(const std::exception& e) { report(e); }
}
"""),

    ("shadowed_declaration", """
int bad(){
    int v = 1;
    { int v = 2; return v; }
}
""", """
int good(){
    for(int i=0;i<3;++i){ }
    for(int i=0;i<3;++i){ }
    return 0;
}
"""),

    ("unused_local", """
int bad(){
    int unusedThing = 4;
    return 7;
}
""", """
int good(){
    QMutexLocker guard(&m);      // RAII: the constructor is the point
    return 7;
}
"""),

    ("disabled_block", """
#if 0
void bad(){ return; }
#endif
""", """
#ifdef Q_OS_WIN
void good(){ return; }
#endif
"""),

    ("leftover_marker", """
// TODO: come back to this
void bad(){}
""", """
// This is finished.
void good(){}
"""),

    ("mutable_static", """
void bad(){ static int counter = 0; ++counter; }
""", """
void good(){ static const int kLimit = 4; use(kLimit); }
"""),

    # A written threading argument is an answer, and the check must accept it.
    # The positive half is the SAME declaration with a comment that says
    # nothing about threads - so this pair tests the exemption itself rather
    # than testing that a static is a static.
    ("mutable_static", """
// The number of times this has run. Handy when debugging.
static int gCount = 0;
void bump(){ ++gCount; }
""", """
// Written once at start-up, before the QML engine exists, so the only thread
// in existence at the write is the one doing it.
static int gCount = 0;
void bump(){ ++gCount; }
"""),

    # ...and the comment has to be THIS declaration's. An annotated static
    # must not excuse the unannotated one three lines below it, which is how a
    # whole file goes quiet after one exemption.
    ("mutable_static", """
// Written before any second thread exists, so the write cannot race.
static int gSafe = 0;

static int gOther = 0;

void f(){ gSafe = 1; gOther = 2; }
""", """
// Written before any second thread exists, so the write cannot race.
static int gSafe = 0;
void f(){ gSafe = 1; }
"""),

    # A Meyers singleton is one line, so its threading note goes above the
    # FUNCTION - there is nowhere else. Looking only above the `static` token
    # found nothing for the entire population of singletons.
    ("mutable_static", """
// The one Api. Returns the shared instance.
Api& Api::instance(){ static Api api; return api; }
""", """
// Constructed on first use; load() is called from the GUI thread before any
// worker thread exists, so every later read is of a value already written.
Api& Api::instance(){ static Api api; return api; }
"""),

    ("filename_in_logic", """
bool bad(const QString& f){
    return f == "PlotCanvas.cpp";
}
""", """
bool good(const QString& f){
    return f.endsWith(".cpp");
}
"""),

    ("copy_per_iteration", """
void bad(const QVector<QString>& v){
    for(auto s : v){ use(s.size()); }
}
""", """
void good(const QVector<QString>& v){
    for(const auto& s : v){ use(s.size()); }
}
"""),

    ("frozen_list_rule", """
bool bad(const QString& e){
    const QStringList kMapped{"scatter","line","area","step","bar"};
    return kMapped.contains(e);
}
""", """
bool good(const QString& e){
    return engineFor(e).usesMappedAxis();
}
"""),
]

# Added after the lists above so the generator is defined by the time it runs.
# Same length and same complexity in both halves; only the `return` differs.
CPP_CASES += [
    ("oversized_function",
     _many_branches(70, terminating=False),
     _many_branches(70, terminating=True)),

    # A dispatch chain has to be the BULK of the function, not a preamble to
    # one. Ten guard clauses in front of a genuinely tangled body must still be
    # reported, or the exemption becomes a way to hide anything by opening with
    # a few early returns.
    ("oversized_function",
     _many_branches(12, terminating=True).replace(
         "    return out;\n}",
         "    for(int i=0;i<9;++i){ if(i&1){ for(int j=0;j<9;++j){ "
         "if(j&1){ for(int k=0;k<9;++k){ if(k&1){ out.v+=i*j*k; } } } } } }\n"
         + "\n".join(f'    if(name[{i}]) out.v+={i};' for i in range(60))
         + "\n    return out;\n}"),
     _many_branches(70, terminating=True)),

    # The other shape: one expression, no nesting. engineHasAxes is 118 lines
    # of `engine != "X" && ...` - complexity 67 and nothing to hold in your
    # head. The positive half is the same length with real nesting in it.
    ("oversized_function",
     "bool bad(const char* e, int n){\n    int t=0;\n"
     + "\n".join(f'    if(n>{i}){{ for(int j=0;j<{i};++j){{ if(j&1) t+=j; }} }}'
                 for i in range(70))
     + "\n    return t>0;\n}\n",
     "bool good(const char* e){\n    return "
     + "\n        && ".join(f'e != "engine{i}"' for i in range(70))
     + ";\n}\n"),
]

# --------------------------------------------------------------------------
# Checks that had no case until now. Each pair is the smallest program that
# makes the claim in the check's own `describe(...)` true, and the same program
# with the claim made false - so what is being tested is the judgement rather
# than the presence of a keyword.
CPP_CASES += [
    # A function nothing calls. The negative is a CYCLE rather than a straight
    # chain: any chain ends in a function nobody calls, which is the same
    # finding one link along, and a negative fixture that trips the check is
    # worse than none.
    ("dead_function", """
int neverCalledByAnything(int x){ return x + 1; }
""", """
int ping(int x);
int pong(int x){ return ping(x - 1); }
int ping(int x){ return x > 0 ? pong(x) : 0; }
"""),

    # Two const methods computing the same type from the same fields by
    # different routes. The negative has one CALL the other, which is the fix
    # the check suggests - and it also stops reading the fields, which is why
    # it drops out of the grouping rather than merely tying.
    ("two_answers", """
struct Totals {
    int a; int b;
    int total() const { return a + b; }
    int sum() const { return b + a; }
};
""", """
struct Totals {
    int a; int b;
    int total() const { return a + b; }
    int sum() const { return total(); }
};
"""),

    # A call in a loop condition whose answer cannot change. The negative is
    # the same loop with the value hoisted, which is the whole suggestion.
    ("expensive_call_in_loop_condition", """
struct Counter { int measure() const; };
int bad(const Counter& c){
    int t = 0;
    for(int i = 0; i < c.measure(); ++i) t += i;
    return t;
}
""", """
struct Counter { int measure() const; };
int good(const Counter& c){
    const int n = c.measure();
    int t = 0;
    for(int i = 0; i < n; ++i) t += i;
    return t;
}
"""),
    # The crash of 16 September, reduced to its shape. The negative is the fix
    # that shipped: one call into a local, then begin() and end() from that.
    ("iterator_pair_from_temporaries", """
struct S { };
QStringList extensions(){ return QStringList(); }
QSet<QString> names(){
    return QSet<QString>(extensions().begin(), extensions().end());
}
""", """
struct S { };
QStringList extensions(){ return QStringList(); }
QSet<QString> names(){
    const QStringList all = extensions();
    return QSet<QString>(all.begin(), all.end());
}
"""),
]


QML_CASES: list[tuple[str, str, str]] = [
    ("qml_bool_from_and", """
import QtQuick
Item {
    property var canvas
    property bool parametric: canvas && canvas.parametric
}
""", """
import QtQuick
Item {
    property var canvas
    property bool parametric: !!(canvas && canvas.parametric)
}
"""),

    ("qml_anchors_conflict", """
import QtQuick
Item {
    Rectangle { anchors.fill: parent; width: 40 }
}
""", """
import QtQuick
Item {
    Rectangle { anchors.fill: parent }
}
"""),

    ("qml_completed_in_delegate", """
import QtQuick
ListView {
    delegate: Item { Component.onCompleted: register(this) }
}
""", """
import QtQuick
ListView {
    delegate: Item { Component.onCompleted: { } }
}
"""),

    ("qml_unused_property", """
import QtQuick
Item {
    property int neverReadAnywhere: 4
}
""", """
import QtQuick
Item {
    property int used: 4
    Text { text: used }
}
"""),
]


QML_CASES += [
    # A name that is not an id, not a property, and not anything C++ exposes.
    # The negative declares the id, which is the only difference.
    ("qml_unresolved_id", """
import QtQuick
Item {
    id: root
    width: sidebar.width
}
""", """
import QtQuick
Item {
    id: root
    Item { id: sidebar; width: 10 }
    width: sidebar.width
}
"""),

    # An imperative write over a binding. The negative writes a binding BACK
    # with Qt.binding, which is the documented way to do it - and reporting
    # that would say the opposite of what the code does.
    ("qml_binding_overwritten", """
import QtQuick
Item {
    id: root
    property int source: 0
    width: root.source * 2
    function nudge() { width = 10 }
}
""", """
import QtQuick
Item {
    id: root
    property int source: 0
    width: root.source * 2
    function nudge() { width = Qt.binding(function(){ return root.source * 3 }) }
}
"""),

    # A signal with no handler anywhere. The negative handles it in the same
    # file, which is the cheapest thing that makes the claim false.
    ("qml_signal_never_handled", """
import QtQuick
Item {
    signal nothingListensToThis()
}
""", """
import QtQuick
Item {
    signal somethingListensToThis()
    onSomethingListensToThis: console.log("heard")
}
"""),
]


def _run_cpp(src: str, check_name: str) -> int:
    from . import checks_cpp
    from .project import Project
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        (root / "native").mkdir()
        f = root / "native" / "case.cpp"
        f.write_text(src, encoding="utf-8")
        p = Project(root)
        p.load_cpp()
        p.load_flow()
        out: list = []
        fn = getattr(checks_cpp, check_name, None)
        if fn is None:
            return -1
        fn(p, out)
        return len([x for x in out if x.check == check_name])


def _run_qml(src: str, check_name: str) -> int:
    from . import checks_qml
    from .project import Project
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        (root / "app" / "qml").mkdir(parents=True)
        (root / "app" / "qml" / "Case.qml").write_text(src, encoding="utf-8")
        p = Project(root)
        p.load_qml()
        out: list = []
        fn = getattr(checks_qml, check_name, None)
        if fn is None:
            return -1
        fn(p, out)
        return len([x for x in out if x.check == check_name])


def _run_project(files: dict[str, str], check_name: str) -> int:
    """A cross-file check needs a small TREE, not one source file.

    Its fixtures are a mapping of relative path to content, written into a
    temporary root. That is the only way to test a check whose whole subject is
    two files disagreeing - and those are the checks this project gets the most
    out of, so they are exactly the ones that must not go untested.
    """
    # BOTH MODULES. This looked in `checks_project` alone, so a cross-file
    # check that lives in `checks_qml` - `qml_cpp_member_missing` compares QML
    # against a C++ header - could not be given a fixture at all and was named
    # in the report as uncovered with no way to fix it.
    from . import checks_project, checks_qml
    from .project import Project
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        for rel, text in files.items():
            path = root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        p = Project(root)
        # BOTH LANGUAGES. A cross-file check is cross-file in whichever
        # direction it needs: `theme_name_missing` compares QML against QML,
        # `engine_without_catalogue_entry` compares C++ against JSON, and
        # `notify_never_emitted` reads types out of a header. Loading only QML
        # meant three of those could not be given a fixture at all - they saw
        # an empty tree and reported nothing, which is indistinguishable from
        # passing, which is the whole thing this file exists to prevent.
        p.load_cpp()
        p.load_qml()
        out: list = []
        fn = (getattr(checks_project, check_name, None)
              or getattr(checks_qml, check_name, None))
        if fn is None:
            return -1
        fn(p, out)
        return len([x for x in out if x.check == check_name])


# A catalogue whose header is right, and the same catalogue with one number
# moved. Written as one function so the two fixtures cannot drift apart - the
# negative case has to be the positive case with the defect removed, or the
# test proves nothing about the defect.
def _catalogue(entry_count: int, pack_count: int) -> str:
    import json as _json
    return _json.dumps({
        "category_count": 1,
        "entry_count": entry_count,
        "engine_count": 2,
        "categories": [{"name": "C", "pack": "base", "entries": [
            {"engine": "A", "pack": "base"}, {"engine": "B", "pack": "base"}]}],
        "packs": [{"id": "base", "name": "Core", "description": "",
                   "entryCount": pack_count}],
    })


# A controller header with one property, and the QML that reads it. The pair
# exists as a function so the two fixtures cannot drift: the negative case must
# be the positive case with the defect removed, or it proves nothing.
def _boundary(member: str) -> dict:
    return {
        "app/src/AppController.h":
            "#pragma once\nclass AppController : public QObject {\n"
            "    Q_OBJECT\npublic:\n"
            "    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)\n"
            "    Q_INVOKABLE void openExport();\nsignals:\n"
            "    void busyChanged();\n};\n",
        "native/plot2d/include/PlotCanvas.h":
            "#pragma once\nclass PlotCanvas : public QQuickPaintedItem {\n"
            "    Q_OBJECT\npublic:\n"
            "    Q_PROPERTY(QString engine READ engine NOTIFY sourceChanged)\n"
            "signals:\n    void sourceChanged();\n};\n",
        "app/src/ProjectWorkspace.h":
            "#pragma once\nclass ProjectWorkspace : public QObject {\n"
            "    Q_OBJECT\npublic:\n"
            "    Q_PROPERTY(bool hasProject READ hasProject NOTIFY changed)\n"
            "signals:\n    void changed();\n};\n",
        "app/qml/Case.qml":
            "import QtQuick\nItem {\n    required property var app\n"
            f"    visible: app.{member}\n}}\n",
    }


PROJECT_CASES: list[tuple[str, dict, dict]] = [
    # The defect: QML reads a member the controller does not declare. This is
    # the shape `openExport` shipped in, one step removed - there the member
    # existed on the wrong object rather than not at all, and the read was
    # equally silent.
    ("qml_cpp_member_missing", _boundary("notAProperty"), _boundary("busy")),
    # An invokable is a member too, and a signal handler name is not one this
    # check should invent: both are checked against what the header declares.
    ("qml_cpp_member_missing", _boundary("openExprt"), _boundary("openExport")),

    ("catalogue_counts_disagree",
     {"config/graph_catalogue.json": _catalogue(3, 2)},
     {"config/graph_catalogue.json": _catalogue(2, 2)}),

    ("catalogue_counts_disagree",
     {"config/graph_catalogue.json": _catalogue(2, 7)},
     {"config/graph_catalogue.json": _catalogue(2, 2)}),

    ("help_topic_missing",
     {"config/help_topics.json":
      '{"sections":[{"id":"s"}],"topics":[{"id":"real","section":"s"}]}',
      "app/qml/Case.qml": 'import QtQuick\nItem { function f() { openHelp("gone", "") } }\n'},
     {"config/help_topics.json":
      '{"sections":[{"id":"s"}],"topics":[{"id":"real","section":"s"}]}',
      "app/qml/Case.qml": 'import QtQuick\nItem { function f() { openHelp("real", "") } }\n'}),

    # The empty id is how a call site says "open at the contents". Reporting it
    # would make the check fire on correct code, which is the failure the
    # negative fixtures exist to catch.
    ("help_topic_missing",
     {"config/help_topics.json":
      '{"sections":[{"id":"s"}],"topics":[{"id":"real","section":"s"}]}',
      "app/qml/Case.qml": 'import QtQuick\nItem { property string startAt: "nope" }\n'},
     {"config/help_topics.json":
      '{"sections":[{"id":"s"}],"topics":[{"id":"real","section":"s"}]}',
      "app/qml/Case.qml": 'import QtQuick\nItem { property string startAt: "" }\n'}),

    # One list written in two languages. The positive case is C++ gaining a
    # mode that QML has not heard of, which is the way it actually goes wrong -
    # and which reads as "this mode filters nothing" rather than as an error.
    ("vision_mode_list_mismatch",
     {"app/src/ColourVision.h":
      'inline QStringList colourVisionNames(){\n'
      '    return { QStringLiteral("Standard"), QStringLiteral("Protanopia"),\n'
      '             QStringLiteral("Monochrome") };\n}\n',
      "app/qml/Theme.qml":
      'import QtQuick\nQtObject {\n'
      '    readonly property var visionKinds: ["", "protanopia"]\n}\n'},
     {"app/src/ColourVision.h":
      'inline QStringList colourVisionNames(){\n'
      '    return { QStringLiteral("Standard"), QStringLiteral("Protanopia"),\n'
      '             QStringLiteral("Monochrome") };\n}\n',
      "app/qml/Theme.qml":
      'import QtQuick\nQtObject {\n'
      '    readonly property var visionKinds: ["", "protanopia", "achromatopsia"]\n}\n'}),
]


# A catalogue whose entries carry a verified flag, and the same catalogue with
# one entry's flag turned off. One function, so the two cannot drift.
def _verified_catalogue(verified) -> dict:
    import json as _json
    entry = {"engine": "A", "name": "A", "pack": "base"}
    if verified is not None:
        entry["verified"] = verified
    return {"config/graph_catalogue.json": _json.dumps({
        "category_count": 1, "entry_count": 1, "engine_count": 1,
        "categories": [{"name": "C", "pack": "base", "entries": [entry]}],
        "packs": [{"id": "base", "name": "Core", "description": "",
                   "entryCount": 1}]})}


# An engine the backend dispatches on, and a catalogue that may or may not
# offer it. The dispatch has to be written in the shape the check requires -
# `in.engine == QLatin1String("...")` - because matching any string literal is
# exactly the false-positive the check's own note records.
def _engine_tree(catalogued: bool) -> dict:
    import json as _json
    engines = [{"engine": "Ghost Engine", "name": "Ghost Engine", "pack": "base",
                "verified": True}] if catalogued else \
              [{"engine": "Other", "name": "Other", "pack": "base",
                "verified": True}]
    return {
        "config/graph_catalogue.json": _json.dumps({
            "category_count": 1, "entry_count": 1, "engine_count": 1,
            "categories": [{"name": "C", "pack": "base", "entries": engines}],
            "packs": [{"id": "base", "name": "Core", "description": "",
                       "entryCount": 1}]}),
        "native/plot2d/src/QtPlotBackendEngines9.cpp":
            'struct PlotSpec { const char* engine; };\n'
            'bool prepareEngineGroup9(const PlotSpec& in){\n'
            '    if(in.engine == QLatin1String("Ghost Engine")){ return true; }\n'
            '    return false;\n}\n',
    }


# A Theme with one property, and a file that reads either it or a name nobody
# declared.
def _theme_tree(member: str) -> dict:
    return {
        "app/qml/Theme.qml":
            "import QtQuick\nQtObject {\n    property color text: \"#000\"\n}\n",
        "app/qml/Case.qml":
            "import QtQuick\nItem {\n    property color c: Theme." + member + "\n}\n",
    }


# A build list and a directory that agree, or do not.
def _build_tree(extra: str) -> dict:
    listed = "    qml/Case.qml\n" + (f"    {extra}\n" if extra else "")
    return {
        "app/CMakeLists.txt": "set(GRAPHVIS_QML_FILES\n" + listed + ")\n",
        "app/qml/Case.qml": "import QtQuick\nItem { }\n",
    }


# A delegate that declares a required property - which turns Qt's implicit
# injection OFF - and then uses an injected name anyway.
def _delegate_tree(declares_index: bool) -> dict:
    extra = "            required property int index\n" if declares_index else ""
    return {"app/qml/Case.qml":
            "import QtQuick\n"
            "Item {\n"
            "    Repeater {\n"
            "        model: 3\n"
            "        delegate: Item {\n"
            "            required property string name\n"
            + extra +
            "            width: index\n"
            "        }\n"
            "    }\n"
            "}\n"}


# An invokable on the wrong side of an access specifier.
def _invokable_tree(access: str) -> dict:
    return {"app/src/Case.h":
            "#pragma once\nclass Case : public QObject {\n"
            "    Q_OBJECT\n"
            f"{access}:\n"
            "    Q_INVOKABLE void doThing();\n};\n"}


PROJECT_CASES += [
    ("catalogue_unverified", _verified_catalogue(False), _verified_catalogue(True)),
    # An entry with NO flag at all is the other half of the same check, and it
    # is the half that reads as "nothing is wrong" in the picker.
    ("catalogue_unverified", _verified_catalogue(None), _verified_catalogue(True)),
    ("engine_without_catalogue_entry", _engine_tree(False), _engine_tree(True)),
    ("theme_name_missing", _theme_tree("notAThing"), _theme_tree("text")),
    ("qml_build_list_mismatch", _build_tree("qml/Gone.qml"), _build_tree("")),
    ("qml_delegate_missing_required_model",
     _delegate_tree(False), _delegate_tree(True)),
    ("invokable_in_private_section",
     _invokable_tree("private"), _invokable_tree("public")),
]


# A property whose NOTIFY signal is declared, and the three things that can
# make it fire. The positive is the header alone: nothing emits it, so every
# QML binding on `busy` is evaluated once and then frozen.
#
# THE SECOND NEGATIVE IS THE ONE THAT MATTERS. A signal-to-signal `connect` is
# how this code base drives derived properties - `windowTitleChanged` is never
# written with `emit` at all - and the repaired check reported it as frozen the
# first time it ran. A check that fires on the project's own correct idiom gets
# turned off within a week.
def _notify_tree(body: str) -> dict:
    files = {
        "app/src/Case.h":
            "#pragma once\nclass Case : public QObject {\n"
            "    Q_OBJECT\npublic:\n"
            "    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)\n"
            "    bool busy() const;\nsignals:\n"
            "    void busyChanged();\n    void stateChanged();\n};\n"}
    if body:
        files["app/src/Case.cpp"] = '#include "Case.h"\n' + body
    return files


PROJECT_CASES += [
    ("notify_never_emitted", _notify_tree(""),
     _notify_tree("void Case::poke(){ emit busyChanged(); }\n")),
    ("notify_never_emitted", _notify_tree(""),
     _notify_tree("Case::Case(){ connect(this, &Case::stateChanged,\n"
                  "                      this, &Case::busyChanged); }\n")),
]


# The definition guard, delegated to. The check runs tools/check_definitions.py
# as a subprocess and reads its verdict, so the fixture has to be a tree that
# script can walk - including the script itself, copied from this project so
# the fixture cannot test a different version of the rule than the one that
# ships.
def _definition_tree(defined: bool) -> dict:
    script = (Path(__file__).resolve().parent.parent
              / "check_definitions.py").read_text(encoding="utf-8")
    files = {
        "tools/check_definitions.py": script,
        "native/plot2d/include/PlotCanvas.h":
            "#pragma once\nclass PlotCanvas {\npublic:\n"
            "    void neverDefinedAnywhere();\n};\n",
    }
    if defined:
        files["native/plot2d/src/PlotCanvas.cpp"] = (
            '#include "PlotCanvas.h"\n'
            "void PlotCanvas::neverDefinedAnywhere(){ }\n")
    return files


PROJECT_CASES += [
    ("declaration_without_definition",
     _definition_tree(False), _definition_tree(True)),
]


# THE GENERATED-FILE CHECKS, which are all the same shape: run the generator
# with --check and believe its EXIT CODE.
#
# What is being tested here is the check's wiring, not the generator's
# arithmetic - whether a non-zero exit becomes a finding, a zero exit becomes
# silence, and (for the designer) an exit that means "I could not run" becomes
# neither. That last one is the whole reason these fixtures exist: the
# designer's check used to print a Python traceback from a missing numpy as
# its evidence for "the designed palettes are not what the designer produces",
# which is a verdict on a header issued by an absent package. A stub generator
# is the only way to make a machine produce all three answers on demand.
def _script_tree(name: str, code: int, target: str) -> dict:
    return {
        f"tools/{name}":
            "import sys\n"
            "sys.stderr.write('the generated file is out of date\\n')\n"
            f"sys.exit({code})\n",
        target: "{}\n",
    }


PROJECT_CASES += [
    ("citation_styles_stale",
     _script_tree("make_citation_styles.py", 1, "config/citation_styles.json"),
     _script_tree("make_citation_styles.py", 0, "config/citation_styles.json")),
    ("help_out_of_date",
     _script_tree("make_help.py", 1, "config/help_topics.json"),
     _script_tree("make_help.py", 0, "config/help_topics.json")),
    ("theme_cvd_tags_stale",
     _script_tree("measure_theme_cvd.py", 1, "app/qml/Theme.qml"),
     _script_tree("measure_theme_cvd.py", 0, "app/qml/Theme.qml")),
    ("designed_colourmaps_stale",
     _script_tree("design_cvd_colourmaps.py", 1,
                  "native/plot2d/include/ColourMapsCvd.h"),
     _script_tree("design_cvd_colourmaps.py", 0,
                  "native/plot2d/include/ColourMapsCvd.h")),
    # AN EXIT THAT MEANS "I COULD NOT RUN" IS NOT A FINDING. Exit 2 is the
    # designer saying numpy is absent; reporting it as a stale header is the
    # defect this branch was written to fix, and this is the fixture that
    # keeps it fixed.
    ("designed_colourmaps_stale",
     _script_tree("design_cvd_colourmaps.py", 1,
                  "native/plot2d/include/ColourMapsCvd.h"),
     _script_tree("design_cvd_colourmaps.py", 2,
                  "native/plot2d/include/ColourMapsCvd.h")),
]


# The linter check. It is the only one that stages a whole module and runs
# Qt's own qmllint, so its fixture is a whole small project: the staging
# function copied from this tree, the build list it reads, the three headers
# the stand-in generator insists on, and two components.
#
# The defect is the one that shipped. A delegate re-declared a property its
# base type already had, which SHADOWS the base's copy - and the base's was
# required, so Qt logged one line and carried on with zero for every row.
# Every figure in the notebook then reported itself as figure zero.
def _lint_tree(redeclares: bool) -> dict:
    tools = Path(__file__).resolve().parent.parent
    extra = "    property int index: 0\n" if redeclares else ""
    return {
        "tools/run_ui_tests.py":
            (tools / "run_ui_tests.py").read_text(encoding="utf-8"),
        "app/CMakeLists.txt":
            "set(GRAPHVIS_QML_FILES\n    qml/Base.qml\n    qml/Case.qml\n)\n",
        "app/qml/Base.qml":
            "import QtQuick\nItem {\n    property int index: 0\n}\n",
        "app/qml/Case.qml":
            "import QtQuick\nimport GraphVis\nBase {\n" + extra + "}\n",
        "native/plot2d/include/PlotCanvas.h":
            "#pragma once\nclass PlotCanvas : public QQuickPaintedItem {\n"
            "    Q_OBJECT\npublic:\n"
            "    Q_PROPERTY(QString engine READ engine NOTIFY sourceChanged)\n"
            "signals:\n    void sourceChanged();\n};\n",
        "app/src/AppController.h":
            "#pragma once\nclass AppController : public QObject {\n"
            "    Q_OBJECT\npublic:\n"
            "    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)\n"
            "signals:\n    void busyChanged();\n};\n",
        "app/src/ProjectWorkspace.h":
            "#pragma once\nclass ProjectWorkspace : public QObject {\n"
            "    Q_OBJECT\npublic:\n"
            "    Q_PROPERTY(bool hasProject READ hasProject NOTIFY changed)\n"
            "signals:\n    void changed();\n};\n",
    }


PROJECT_CASES += [
    ("qml_type_error", _lint_tree(True), _lint_tree(False)),
]


def run(verbose: bool = True) -> tuple[int, int, list[str]]:
    """Returns (passed, total, failures)."""
    failures: list[str] = []
    passed = 0
    total = 0
    for name, positive, negative in CPP_CASES:
        total += 1
        hit = _run_cpp(positive, name)
        miss = _run_cpp(negative, name)
        if hit == -1:
            failures.append(f"{name}: no such check")
        elif hit < 1:
            failures.append(f"{name}: missed the case it exists to catch")
        elif miss != 0:
            failures.append(f"{name}: fired {miss}x on code that is fine")
        else:
            passed += 1
    for name, positive, negative in QML_CASES:
        total += 1
        hit = _run_qml(positive, name)
        miss = _run_qml(negative, name)
        if hit == -1:
            failures.append(f"{name}: no such check")
        elif hit < 1:
            failures.append(f"{name}: missed the case it exists to catch")
        elif miss != 0:
            failures.append(f"{name}: fired {miss}x on code that is fine")
        else:
            passed += 1
    for name, positive, negative in PROJECT_CASES:
        total += 1
        hit = _run_project(positive, name)
        miss = _run_project(negative, name)
        if hit == -1:
            failures.append(f"{name}: no such check")
        elif hit < 1:
            failures.append(f"{name}: missed the case it exists to catch")
        elif miss != 0:
            failures.append(f"{name}: fired {miss}x on a tree that is fine")
        else:
            passed += 1
    # EVERY CHECK HAS A CASE, or is named as not having one.
    #
    # "33/33 pass" says how many cases ran. It says NOTHING about how many
    # checks exist, so a check added without a fixture is not counted, not
    # named, and not distinguishable from one that is covered - which is the
    # same "silence reads as a pass" this file's own opening paragraph is
    # about, one level up. A check with no case is exactly the check that can
    # quietly stop firing.
    uncovered = uncovered_checks()
    if verbose:
        line = f"check self-test: {passed}/{total} pass"
        if uncovered:
            line += (f"; {len(uncovered)} with no case: "
                     + ", ".join(uncovered))
        print(line)
        for f in failures:
            print("  FAIL", f)
    return passed, total, failures


def uncovered_checks() -> list:
    """Checks the audit runs that no fixture ever exercises."""
    return sorted(_registered_checks() - {n for n, _, _ in CPP_CASES}
                  - {n for n, _, _ in QML_CASES}
                  - {n for n, _, _ in PROJECT_CASES})


def _registered_checks() -> set:
    """Every check the audit actually runs, by name.

    Read off the ALL lists rather than a second hand-kept roster, for the
    reason this project has recorded about every other list like it: a roster
    that can disagree with the thing it describes eventually does.
    """
    from . import checks_cpp, checks_project, checks_qml
    names: set = set()
    for mod in (checks_cpp, checks_qml, checks_project):
        for fn in getattr(mod, "ALL", []):
            names.add(getattr(fn, "__name__", ""))
    return {n for n in names if n}
