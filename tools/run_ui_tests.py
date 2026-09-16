#!/usr/bin/env python3
"""Run the QML interface tests.

Why this exists
---------------
Every other check in this project drives the backend: 439 engines swept, the
regression checks measured, 339 Python tests, and `--selftest-ui`, which loads
the window and lets it settle. Not one of them presses anything. A QML binding
is only evaluated when something instantiates the component, so a binding that
assigns `undefined` to a `bool`, a delegate that reads a property it was never
given, or a panel that stages a mapping and never applies it is invisible to all
of them - and all three have shipped.

These tests instantiate the real components with stand-in data and assert what
happens. They need no C++ from this project: the panels take their canvas and
controller as `var`, so a QtObject with the right properties is indistinguishable
from the real one, and that is what keeps the tests runnable anywhere Qt is.

The module is staged from the file list in app/CMakeLists.txt rather than from a
hand-written list, so the tests cannot drift from what the application ships.

    python tools/run_ui_tests.py           # all of tests/qml
    python tools/run_ui_tests.py -v        # every test name
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "tests" / "qml"


def find_runner() -> str | None:
    """qmltestrunner, wherever this Qt put it."""
    found = shutil.which("qmltestrunner")
    if found:
        return found
    roots = [ROOT / "build" / "vcpkg_installed", ROOT / "build", Path("/usr/lib/qt6")]
    env = os.environ.get("QTDIR") or os.environ.get("Qt6_DIR")
    if env:
        roots.insert(0, Path(env))
    for r in roots:
        if not r.is_dir():
            continue
        for name in ("qmltestrunner.exe", "qmltestrunner"):
            for hit in r.rglob(name):
                return str(hit)
    return None


def stage_module(into: Path) -> int:
    """A source-tree copy of the GraphVis module, with a qmldir."""
    cmake = (ROOT / "app" / "CMakeLists.txt").read_text(encoding="utf-8")
    block = cmake.split("set(GRAPHVIS_QML_FILES", 1)[1].split(")", 1)[0]
    files = [l.strip() for l in block.splitlines() if l.strip().endswith(".qml")]
    if not files:
        raise SystemExit("app/CMakeLists.txt: GRAPHVIS_QML_FILES is empty or has moved")
    out = into / "GraphVis"
    out.mkdir(parents=True, exist_ok=True)
    lines = ["module GraphVis"]
    for f in files:
        src = ROOT / "app" / f
        if not src.exists():
            raise SystemExit(f"{f} is in GRAPHVIS_QML_FILES and not on disk")
        rel = f[len("qml/"):]
        dst = out / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(src, dst)
        # Theme is the module's singleton; everything else is a plain type.
        prefix = "singleton " if dst.stem == "Theme" else ""
        lines.append(f"{prefix}{dst.stem} 1.0 {rel}")
    # The C++ types, as generated stand-ins, listed in the same qmldir so a
    # component that contains one resolves instead of failing to load.
    for name in stage_native_stand_ins(out):
        lines.append(f"{name} 1.0 {name}.qml")
    for name in stage_newer_qt_shims(out):
        lines.append(f"{name} 1.0 {name}.qml")
    (out / "qmldir").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return len(files)


# A QML stand-in for each C++ type the interface uses, GENERATED FROM ITS HEADER.
#
# WHY THIS EXISTS. Four QML files could not be instantiated by these tests at
# all: FigureCanvas and FigureCell contain a `PlotCanvas`, which is a C++ type
# and is not in the staged module, and VisualizeWorkspace and NotebookCanvas
# contain those. Two of the four are the application's main workspaces. Every
# test that tried got "Type NotebookCanvas unavailable" and the file it was
# about went untested, quietly, for as long as these tests have existed.
#
# What lived in that hole: the notebook's figure delegate re-declared a
# `required property int index` that FigureCell already had, which shadowed the
# base's copy and left it uninitialised - and Qt does not warn and carry on for
# an uninitialised required property, it refuses to build the object. The
# notebook drew no figures whatever. The linter reported the shadowing as a
# style note; nothing else in the project loads QML at all.
#
# WHY IT IS GENERATED. A hand-written stand-in is a second declaration of the
# canvas's interface, free to drift from the real one - and a test double that
# has a property the real type lost passes a test the application fails, which
# is worse than no test. The properties, invokables and signals here are read
# out of the header's own Q_PROPERTY, Q_INVOKABLE and signals declarations, so
# there is one source for what a PlotCanvas offers and this follows it.
#
# WHAT IT IS NOT. It paints nothing and computes nothing: it holds a value when
# QML writes one and returns a default when QML reads one. These tests are about
# bindings, delegates and wiring - whether a panel stages a mapping, whether a
# required property is filled, whether a list goes where it is sent. Anything
# about what gets DRAWN is answered by the 440-engine sweep and the property
# checks, which run the real backend and no QML.
QML_DEFAULT = {
    "int": "0", "bool": "false", "double": "0.0", "qreal": "0.0", "real": "0.0",
    "float": "0.0", "QString": '""', "QStringList": "[]", "QVariantList": "[]",
    "QVariantMap": "({})", "QVariant": "undefined", "QColor": '"#000000"',
    "QPointF": "Qt.point(0,0)", "QRectF": "Qt.rect(0,0,0,0)", "QSizeF": "Qt.size(0,0)",
    # A TYPE MISSING FROM THIS TABLE IS NOT A SMALL GAP. It becomes
    # `undefined`, which Qt refuses to assign - "Unable to assign
    # [undefined] to QUrl" from the export dialog's currentFolder, once
    # per instantiation, in a suite whose whole method is failOnWarning.
    "QUrl": '""',
}
QML_TYPE = {
    "int": "int", "bool": "bool", "double": "real", "qreal": "real", "real": "real",
    "float": "real", "QString": "string", "QStringList": "var", "QVariantList": "var",
    "QVariantMap": "var", "QVariant": "var", "QColor": "color", "QPointF": "point",
    "QRectF": "rect", "QSizeF": "size", "QUrl": "url",
}


def _qml_property(ctype: str, name: str, generated: set[str],
                  maps: dict | None = None) -> str:
    # A pointer to a type that has a stand-in of its own becomes an instance of
    # it, so `app.project.hasProject` reads a value instead of throwing.
    if ctype in generated:
        return f"    property var {name}: {ctype} {{ }}"
    # A map whose keys are declared somewhere readable gets them, so
    # `spec.rail` is a boolean rather than undefined.
    if maps and name in maps:
        return f"    property var {name}: {maps[name]}"
    kind = QML_TYPE.get(ctype, "var")
    value = QML_DEFAULT.get(ctype, "undefined")
    return f"    property {kind} {name}: {value}"


def stage_native_stand_ins(out: Path) -> list[str]:
    """One .qml per C++ type these tests need, written from its header."""
    # THE CONTROLLER TOO, for the same reason and with more force.
    #
    # Every panel takes `app` as a `var` and reads properties off it, and each
    # test used to hand over a QtObject carrying the four or five it happened to
    # need. That is a stand-in per test, each an incomplete and separately
    # drifting sketch of a type with 119 properties - and the failure mode is
    # not a clear error, it is "Unable to assign [undefined] to int" seventeen
    # times per instantiation, which the eye learns to scroll past. A test
    # drowning in warnings is a test that cannot use failOnWarning, and
    # failOnWarning is how this suite catches its own class of defect.
    headers = {
        "PlotCanvas": ROOT / "native" / "plot2d" / "include" / "PlotCanvas.h",
        "AppController": ROOT / "app" / "src" / "AppController.h",
        # A POINTER PROPERTY IS AN OBJECT, and `undefined` is not one.
        #
        # `Q_PROPERTY(ProjectWorkspace* project ...)` left `app.project` as
        # undefined in the stand-in, and ProjectPanel reads
        # `root.project.hasProject` thirty-odd times - each one a TypeError
        # rather than a missing value. The pointed-to type gets its own
        # stand-in for the same reason the canvas does, generated from its own
        # header so it cannot drift either, and the property below is given one.
        "ProjectWorkspace": ROOT / "app" / "src" / "ProjectWorkspace.h",
    }
    written = []
    for name, header in headers.items():
        if not header.exists():
            raise SystemExit(
                f"{header} has moved, so the {name} stand-in cannot be generated "
                "from it - and a hand-written one would be free to drift")
        text = header.read_text(encoding="utf-8", errors="ignore")
        text = re.sub(r"//[^\n]*", "", text)

        # ONLY FOR A PROPERTY THIS HEADER ACTUALLY DECLARES. The builder was
        # read for AppController unconditionally, so staging a tree whose
        # controller has no `uiLayoutSpec` - the audit's own fixture for the
        # linter check - died on a FileNotFoundError for a source it did not
        # need. In this tree the property is there and the source is still
        # required, so the guard below is unchanged for the case it guards.
        maps = {prop: _map_defaults(ROOT, cpp, builder, struct_header)
                for prop, (cpp, builder, struct_header)
                in MAP_PROPERTY_BUILDERS.items()
                if re.search(rf"\b{prop}\b", text)} if name == "AppController" else {}
        seen, lines = set(), []
        for ctype, prop in re.findall(
                r"Q_PROPERTY\(\s*([A-Za-z_:<>\s\*]+?)\s+([A-Za-z_]\w*)\s+READ", text):
            ctype = ctype.strip().split()[-1].strip("*&")
            if prop in seen:
                continue
            seen.add(prop)
            lines.append(_qml_property(ctype, prop, set(headers) - {name}, maps))
        if not lines:
            raise SystemExit(f"no Q_PROPERTY found in {header.name}; the stand-in "
                             "would be an empty object that satisfies anything")

        calls = []
        for ret, fn in re.findall(
                r"Q_INVOKABLE\s+(?:static\s+)?([A-Za-z_:<>\s\*]+?)\s+([A-Za-z_]\w*)\s*\(",
                text):
            ret = ret.strip().split()[-1].strip("*&")
            if fn in seen:
                continue
            seen.add(fn)
            value = QML_DEFAULT.get(ret, "undefined")
            body = "" if ret == "void" else f" return {value}"
            calls.append(f"    function {fn}() {{{body} }}")

        # THE CHANGE SIGNALS ARE ALREADY THERE, twice over.
        #
        # QML generates `<prop>Changed` for every property declared above, and
        # Item brings its own for `state`, `enabled`, `visible` and the rest. A
        # signal declaration that collides with either is not a warning, it is
        # "Duplicate signal name: invalid override of property change signal or
        # superclass signal" and the whole stand-in fails to load - taking the
        # four components it exists for down with it, which is the hole this was
        # written to close.
        #
        # The C++ type's NOTIFY signals are exactly this kind, so dropping them
        # loses nothing: a binding onto a stand-in property is notified by the
        # property's own signal. `skipped` is returned rather than swallowed, so
        # a signal that is NOT a change signal and does get dropped is visible
        # in the run's output instead of going missing quietly.
        base_props = {
            # QQuickItem, and the two QQuickPaintedItem adds. Written out
            # because there is no way to ask QML for them from here.
            "state", "enabled", "visible", "parent", "focus", "activeFocus",
            "opacity", "rotation", "scale", "smooth", "clip", "children",
            "width", "height", "x", "y", "z", "implicitWidth", "implicitHeight",
            "antialiasing", "baselineOffset", "containmentMask", "palette",
            "transformOrigin", "childrenRect", "data", "resources", "states",
            "transitions", "transform", "anchors", "renderTarget", "fillColor",
        }
        sigs, skipped = [], []
        block = re.search(r"\bsignals:(.*?)(?:\n\s*(?:public|private|protected)\b|\n\};)",
                          text, re.S)
        if block:
            for sig in re.findall(r"\bvoid\s+([A-Za-z_]\w*)\s*\(", block.group(1)):
                if sig in seen:
                    continue
                seen.add(sig)
                stem = sig[:-len("Changed")] if sig.endswith("Changed") else None
                if stem and (stem in seen or stem in base_props):
                    continue
                if sig in {b + "Changed" for b in base_props}:
                    skipped.append(sig)
                    continue
                sigs.append(f"    signal {sig}()")
        if skipped:
            print(f"  {name} stand-in: dropped {', '.join(sorted(skipped))} "
                  "(collides with a property change signal)")

        body = "\n".join(["import QtQuick", "import GraphVis", "", "Item {"]
                         + lines + calls + sigs + ["}", ""])
        (out / f"{name}.qml").write_text(body, encoding="utf-8")
        written.append(name)
    return written



# A QtQuick type this Qt is too old for, shimmed - and only if it is missing.
#
# VisualizeWorkspace embeds the native renderer with `WindowContainer`, which
# QtQuick added in 6.8. On an older Qt the whole workspace fails to load with
# "WindowContainer is not a type" - so the application's main workspace was
# untestable for a second, entirely different reason from the C++ one, and the
# error looks exactly like a defect in the file.
#
# Shimmed rather than skipped, because what these tests ask of that workspace is
# whether its bindings and its wiring hold, and a window container embeds a
# surface this harness has nothing to put in anyway. Staged ONLY when the
# running Qt does not provide it: where it does, the real type is used and
# nothing here shadows it. Either way the run says which.
def _qtquick_has(type_name: str) -> bool:
    """Whether the Qt these tests will run against declares this QtQuick type."""
    for base in ("/usr/lib/x86_64-linux-gnu/qt6/qml", "/usr/lib/qt6/qml"):
        types = Path(base) / "QtQuick" / "plugins.qmltypes"
        if types.exists():
            return f'"{type_name}"' in types.read_text(encoding="utf-8", errors="ignore")
    # No qmltypes to read: assume the type is there rather than shadow a real
    # one. A missing type then fails loudly, which is the safe direction.
    return True


NEWER_QT_TYPES = {
    # name: the QML body to stand in with, and what it is standing in for.
    "WindowContainer": (
        "import QtQuick\n\n"
        "// Stands in for QtQuick's WindowContainer (Qt 6.8+) on an older Qt.\n"
        "// Holds the window it is given and draws nothing.\n"
        "Item {\n"
        "    property var window: null\n"
        "}\n"
    ),
}


def stage_newer_qt_shims(out: Path) -> list[str]:
    written = []
    for name, body in NEWER_QT_TYPES.items():
        if _qtquick_has(name):
            continue
        (out / f"{name}.qml").write_text(body, encoding="utf-8")
        print(f"  {name}: this Qt does not have it, so a stand-in is staged "
              "(it is a QtQuick type, not one of ours)")
        written.append(name)
    return written



# A MAP-VALUED PROPERTY, with its keys, read out of the code that builds it.
#
# This was written off once. The controller's `uiLayoutSpec` is a QVariantMap
# and a header says a property is a map without saying which keys it has, so
# `spec.rail` was undefined against the stand-in and a boolean against the real
# controller - three "Unable to assign [undefined] to bool" per workspace, and a
# test that could never assert on them. The note in tst_workspace_loads.qml said
# the exclusion would go "if a way is found to declare those keys in one place
# that both C++ and this can read".
#
# There is one, and it was already there: `uiLayoutAsMap` in UiLayouts.cpp is a
# straight run of `m.insert(QStringLiteral("key"), <value>)`, and every value is
# a member of the `UiLayout` struct in UiLayouts.h with a declared type. The
# keys come from the inserts and the types from the struct, so this follows the
# same rule as everything else here: one source, read rather than copied.
#
# Only the keys and their TYPES. The values are each type's default, not the
# defaults of any particular layout - a stand-in that shipped the Notebook
# layout's numbers would be a fixture pretending to be a type. A test that wants
# a specific shape sets the keys it cares about, which is now possible because
# the keys exist.
MAP_PROPERTY_BUILDERS = {
    # property on the controller: (the .cpp that builds it, the builder's name,
    #                              the .h with the struct the values come from)
    "uiLayoutSpec": ("app/src/UiLayouts.cpp", "uiLayoutAsMap", "app/src/UiLayouts.h"),
}


def _struct_member_types(header_text: str) -> dict:
    """Every `<type> <name>` in the header, as a flat name -> type map.

    Flat on purpose: the builder names its values `l.<member>`, and a member
    name that appeared in two structs with two types would be a reason to stop
    rather than to guess - which the caller does.
    """
    out, seen_twice = {}, set()
    for ctype, name in re.findall(
            r"^\s{4}([A-Za-z_][\w:]*)\s+([A-Za-z_]\w*)\s*(?:=|;)", header_text, re.M):
        if name in out and out[name] != ctype:
            seen_twice.add(name)
        out[name] = ctype
    for name in seen_twice:
        out.pop(name, None)
    return out


def _map_defaults(root: Path, cpp: str, builder: str, header: str) -> str:
    source = (root / cpp).read_text(encoding="utf-8", errors="ignore")
    body = re.search(builder + r"\([^)]*\)\s*\{(.*?)\n\}", source, re.S)
    if not body:
        raise SystemExit(
            f"{builder} was not found in {cpp}, so the map stand-in cannot be "
            "generated from it - and a hand-written key list would be free to "
            "drift from the map QML actually receives")
    members = _struct_member_types((root / header).read_text(encoding="utf-8",
                                                             errors="ignore"))
    pairs = []
    for key, value in re.findall(
            r'insert\(QStringLiteral\("(\w+)"\)\s*,\s*(.+?)\);', body.group(1)):
        value = value.strip()
        if value.startswith("int("):
            pairs.append(f'"{key}": 0')                 # an enum, as QML sees it
            continue
        member = re.fullmatch(r"\w+\.(\w+)", value)
        ctype = members.get(member.group(1)) if member else None
        if ctype in ("int", "double", "qreal", "float"):
            pairs.append(f'"{key}": 0')
        elif ctype == "bool":
            pairs.append(f'"{key}": false')
        elif ctype == "QString" or ctype is None:
            # None covers a value that is a call rather than a member, such as
            # the group's name. Every one of those in this builder is a string;
            # if that stops being true the key is still present, which is the
            # property that matters, and the type shows up as a mismatch.
            pairs.append(f'"{key}": ""')
        else:
            pairs.append(f'"{key}": ""')
    if not pairs:
        raise SystemExit(f"{builder} in {cpp} inserted no keys this could read")
    return "({ " + ", ".join(pairs) + " })"


def main() -> int:
    runner = find_runner()
    if not runner:
        print("[SKIP] qmltestrunner was not found, so the interface tests did not run.")
        print("       It ships with Qt's qtdeclarative tools. Everything else is")
        print("       unaffected; this is the only check that needs it.")
        return 0                     # not a build failure
    if not TESTS.is_dir():
        print(f"[SKIP] no tests at {TESTS.relative_to(ROOT)}")
        return 0

    with tempfile.TemporaryDirectory(prefix="graphvis-qml-") as tmp:
        staged = Path(tmp)
        count = stage_module(staged)
        env = dict(os.environ)
        env.setdefault("QT_QPA_PLATFORM", "offscreen")
        cmd = [runner, "-import", str(staged), "-input", str(TESTS)]
        if "-v" in sys.argv:
            cmd.append("-v2")
        print(f"interface tests: {count} QML files staged, running {TESTS.name}/")
        done = subprocess.run(cmd, env=env, capture_output=True, text=True)

    out = done.stdout + done.stderr
    fails = [l.strip() for l in out.splitlines() if l.startswith("FAIL!")]
    totals = [l for l in out.splitlines() if l.startswith("Totals:")]
    for line in fails[:30]:
        print("  " + line)
    # The warnings a test failed on are printed after it, and are the useful half.
    if fails:
        for line in out.splitlines():
            if "Unable to assign" in line or "TypeError" in line or "is not a" in line:
                print("  " + line.strip())
    print("  " + (totals[-1] if totals else "no totals reported"))
    if done.returncode != 0 and not fails:
        print(out[-2000:])
    return 1 if done.returncode != 0 else 0


if __name__ == "__main__":
    raise SystemExit(main())
