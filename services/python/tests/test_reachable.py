"""Nothing may be written and left unreachable.

This suite exists because an audit found a lot of it, and because the two worst
bugs in the program's history were both of this shape rather than of the "wrong
answer" shape:

- `literature.extract` returned figures only when a vision model was configured,
  so every feature downstream of the figure list was unreachable without an API
  key, and the de-renderer sat there fully written and never run.
- `fit.modified_gompertz` imported a function that had been deleted. It raised
  ImportError on every call, and nothing ever called it, so nothing noticed.

That is the hazard: code with no caller is code with no test, and code with no
test rots without anybody being told. These tests make "unreachable" a failing
condition rather than a thing somebody notices a year later.

They are deliberately structural - they read the sources rather than running the
application - because the application is C++ and QML and cannot be imported
here. A structural test that catches the whole class is worth more than an
integration test that catches one instance.
"""
from __future__ import annotations

import ast
import re
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[3]
SERVICE = ROOT / "services" / "python" / "graphvis_science" / "service.py"
APP_SRC = ROOT / "app" / "src"
QML = ROOT / "app" / "qml"

# Operations the service offers that the application deliberately does not call,
# each with the reason. An entry here is a decision; an operation missing from
# BOTH this list and the application is the failure this test is for.
SERVICE_ONLY = {
    # The application keeps its own extension list, generated from the same
    # registry, because the file dialog needs filters before the service has
    # been started - and may not have it installed at all.
    "io.formats": "duplicated by AppController::importNameFilters",
    # PlotCanvas carries the estimator names as a static list so the mapping
    # panel can build its controls without a round trip.
    "surface.estimators": "duplicated by PlotCanvas::fieldEstimatorNames",
    # Reachable through analysis.catalogue, which is how every other analysis
    # operation is reached.
    "statistics.describe": "reachable via analysis.catalogue",
}


def _service_ops() -> set[str]:
    source = SERVICE.read_text(encoding="utf-8")
    return set(re.findall(r'op\s*==\s*["\']([a-z_]+\.[a-z_]+)["\']', source))


def _app_text() -> str:
    return "\n".join(p.read_text(encoding="utf-8", errors="ignore")
                     for p in list(APP_SRC.glob("*.cpp")) + list(APP_SRC.glob("*.h")))


@pytest.mark.skipif(not SERVICE.exists(), reason="run from a source tree")
def test_every_service_operation_is_reachable() -> None:
    """An op the application never sends is an op nothing ever runs."""
    app = _app_text()
    orphans = sorted(op for op in _service_ops()
                     if f'"{op}"' not in app and op not in SERVICE_ONLY)
    assert not orphans, (
        "these service operations have no caller in the application, so nothing "
        "exercises them: " + ", ".join(orphans) +
        " — wire them up, delete them, or record why they are service-only in "
        "SERVICE_ONLY above"
    )


@pytest.mark.skipif(not SERVICE.exists(), reason="run from a source tree")
def test_every_service_operation_imports_what_it_uses() -> None:
    """The fit.modified_gompertz failure, made impossible to repeat.

    Each branch imports its implementation lazily, so a function that has been
    renamed or removed is not a failure until somebody calls that branch. Here
    every `from graphvis_science... import X` inside service.py is resolved.
    """
    import importlib

    tree = ast.parse(SERVICE.read_text(encoding="utf-8"))
    missing: list[str] = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.ImportFrom) or not node.module:
            continue
        if not node.module.startswith("graphvis_science"):
            continue
        try:
            module = importlib.import_module(node.module)
        except Exception as exc:                      # pragma: no cover
            missing.append(f"{node.module} will not import: {type(exc).__name__}: {exc}")
            continue
        for alias in node.names:
            if alias.name == "*":
                continue
            if hasattr(module, alias.name):
                continue
            # A name can also be a submodule that has not been imported yet,
            # which `from package import submodule` resolves and getattr does
            # not.
            try:
                importlib.import_module(f"{node.module}.{alias.name}")
            except Exception:
                missing.append(f"{node.module} has no {alias.name!r} (line {node.lineno})")
    assert not missing, "service.py imports names that do not exist: " + "; ".join(missing)


@pytest.mark.skipif(not QML.exists(), reason="run from a source tree")
def test_qml_only_calls_methods_qml_can_call() -> None:
    """A property WRITE accessor is not callable from QML.

    `canvas.setColourOutOfRangeDropped(x)` looks perfectly correct and throws
    "Property ... is not a function" at run time. Nothing catches it: `canvas`
    and `app` are `var` in the QML, so qmllint has no type to check against.
    """
    headers = {
        "canvas": ROOT / "native" / "plot2d" / "include" / "PlotCanvas.h",
        "app": APP_SRC / "AppController.h",
    }
    callable_names: dict[str, set[str]] = {}
    for receiver, header in headers.items():
        if not header.exists():
            pytest.skip(f"{header} not present")
        text = header.read_text(encoding="utf-8")
        names = set(re.findall(r'Q_INVOKABLE[^;{]*?\b(\w+)\s*\(', text))
        for block in re.findall(
                r'(?:public|protected|private)\s+slots\s*:(.*?)'
                r'(?:\n\s*(?:public|protected|private|signals)\b|\Z)', text, re.S):
            names |= set(re.findall(r'\b(\w+)\s*\(', block))
        callable_names[receiver] = names

    # Methods QML itself provides on any Item / QtObject.
    builtin = {"mapToItem", "mapFromItem", "toString", "forceActiveFocus",
               "open", "close", "toLocaleString", "destroy"}

    bad: list[str] = []
    for path in QML.rglob("*.qml"):
        text = path.read_text(encoding="utf-8")
        for match in re.finditer(r'\b(canvas|app)\.(\w+)\s*\(', text):
            receiver, method = match.group(1), match.group(2)
            if method in builtin or method in callable_names[receiver]:
                continue
            line = text[:match.start()].count("\n") + 1
            bad.append(f"{path.relative_to(ROOT)}:{line} {receiver}.{method}()")
    assert not bad, (
        "QML calls C++ members that are neither Q_INVOKABLE nor slots, so they "
        "will throw at run time: " + "; ".join(bad)
    )


@pytest.mark.skipif(not QML.exists(), reason="run from a source tree")
def test_every_qml_component_is_instantiated() -> None:
    """A component nothing creates is a control nobody can reach.

    FullRenderPolicyBox.qml was like this: written, listed in the build, and
    placed in no window, so the setting it offers was reachable only from a menu
    somewhere else.
    """
    files = list(QML.rglob("*.qml"))
    entry = {"Main", "Theme", "VtkViewport"}      # loaded by C++, not by QML
    corpus = {p: p.read_text(encoding="utf-8") for p in files}
    app = _app_text()
    orphans = []
    for path in files:
        name = path.stem
        if name in entry:
            continue
        others = "\n".join(text for p, text in corpus.items() if p != path)
        if re.search(r'(^|[^\w.])' + re.escape(name) + r'\s*\{', others, re.M):
            continue
        if name in app:
            continue
        orphans.append(str(path.relative_to(ROOT)))
    assert not orphans, "QML components nothing instantiates: " + ", ".join(orphans)


@pytest.mark.skipif(not SERVICE.exists(), reason="run from a source tree")
def test_no_public_definition_in_the_package_is_unreferenced() -> None:
    """Dead classes drift, and a drifted class is a trap for whoever finds it.

    The `Peak` dataclass was unreferenced long enough that its fields no longer
    matched what PeakEngine.detect produced - so the first person to use it
    would have got the wrong field names from a type that looked authoritative.
    """
    package = ROOT / "services" / "python" / "graphvis_science"
    sources = {p: p.read_text(encoding="utf-8") for p in package.rglob("*.py")}
    tests = "\n".join(p.read_text(encoding="utf-8")
                      for p in (ROOT / "services" / "python" / "tests").glob("*.py"))
    corpus = "\n".join(sources.values()) + tests

    unreferenced = []
    for path, text in sources.items():
        try:
            tree = ast.parse(text)
        except SyntaxError:                           # pragma: no cover
            continue
        for node in tree.body:
            if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                continue
            if node.name.startswith("_"):
                continue
            uses = len(re.findall(r'\b' + re.escape(node.name) + r'\b', corpus))
            declarations = len(re.findall(
                r'\b(?:def|class)\s+' + re.escape(node.name) + r'\b', corpus))
            if uses - declarations == 0:
                unreferenced.append(f"{path.relative_to(ROOT)}::{node.name}")
    assert not unreferenced, (
        "public definitions nothing references: " + ", ".join(unreferenced) +
        " — use them, or delete them before they drift"
    )


# --------------------------------------------- the interface is not blocked
#
# A different shape of the same disease. Unreachable code is written and never
# runs; a blocking call on the GUI thread runs and stops everything else from
# running, and both are invisible until somebody happens to look.
#
# `startScienceOp` waited up to five seconds on `waitForStarted`, on the GUI
# thread, on the first science operation of a session - and it did so BEFORE
# setBusy, so the window could not even paint the label saying what it was
# waiting for. The timeout is not the cost; the cost is a Python interpreter in
# its own virtual environment being spawned, which on a cold cache or behind a
# virus scanner is seconds. What the person saw was a program that had stopped
# responding.
#
# Waiting in a DESTRUCTOR is a different matter and is deliberately allowed:
# `~AppController` and `~PlotCanvas` wait for their workers because those
# workers hold a pointer into the Rust runtime, and freeing it while one is
# still inside it is a use-after-free. There is nothing to keep responsive at
# that point, and the alternative is a crash on exit.
_BLOCKING = ("waitForStarted", "waitForReadyRead", "waitForBytesWritten")


def _function_bodies(source: str) -> list[tuple[str, str]]:
    """(name, body) for each C++ member function definition, by brace match."""
    out = []
    for match in re.finditer(
            r"^[A-Za-z_][\w:<>,\s\*&]*?\b(\w+)::(~?\w+)\s*\([^;{]*\)\s*"
            r"(?:const\s*)?\{", source, re.M):
        depth, index = 0, match.end() - 1
        while index < len(source):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    break
            index += 1
        out.append((f"{match.group(1)}::{match.group(2)}",
                    source[match.end():index]))
    return out


def test_no_live_path_blocks_the_gui_thread_waiting_for_a_process() -> None:
    offenders: list[str] = []
    for path in sorted(APP_SRC.glob("*.cpp")):
        source = path.read_text(encoding="utf-8", errors="ignore")
        source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
        source = re.sub(r"//[^\n]*", "", source)
        for name, body in _function_bodies(source):
            if "::~" in name:          # destructors: see above
                continue
            for call in _BLOCKING:
                if call + "(" in body:
                    offenders.append(f"{path.name} {name}() calls {call}")
            # waitForFinished outside a destructor is the same fault: it is how
            # the dead-pipe retry used to spend two seconds of the person's
            # time before it could even report the failure.
            if "waitForFinished(" in body:
                offenders.append(f"{path.name} {name}() calls waitForFinished")

    assert not offenders, (
        "blocking waits on the GUI thread, outside a destructor. The window "
        "cannot paint or accept input for the duration, and cannot say why:\n  "
        + "\n  ".join(offenders))


def test_a_queued_science_request_cannot_reach_a_later_process() -> None:
    """The hazard the asynchronous start introduces, closed deliberately.

    Holding a request until `started()` means there is a window in which the
    process it was meant for can fail or be killed. If the queue survived that,
    the next service to come up - for an unrelated operation, or after the
    person pressed Cancel - would receive it. Every path that abandons a
    service must therefore drop the queued request with it.
    """
    source = (APP_SRC / "AppController.cpp").read_text(encoding="utf-8")
    for handler, marker in (
            ("the science service's errorOccurred handler",
             "&scienceProcess_,&QProcess::errorOccurred"),
            ("cancelActiveJob", "void AppController::cancelActiveJob")):
        start = source.index(marker)
        window = source[start:start + 1200]
        assert "pendingScienceRequest_.clear()" in window, (
            f"{handler} abandons the science service without dropping the "
            "request queued for it")


# ------------------------------------ the catalogue and the renderer agree
#
# `QtPlotBackend::supportedEngines()` is a hand-maintained list, and
# `PlotCanvas` reports any engine missing from it as unsupported rather than
# drawing something wrong. That is the right behaviour and it is also a quiet
# one: an engine left off the list does not fail, it just stops being
# available, and the only sign is a catalogue entry that declines to draw.
#
# The reverse is quieter still. A name in the list that no catalogue entry
# uses is an engine nothing can ever select, so it is never swept, never seen,
# and never removed.
#
# The two lists are currently identical - 434 names, no extras on either side.
# This keeps them that way, because the catalogue has been extended four times
# over this port and each extension is an opportunity to update one list and
# not the other.
def test_every_catalogue_engine_is_one_the_renderer_declares() -> None:
    import json

    catalogue = json.loads(
        (ROOT / "config" / "graph_catalogue.json").read_text(encoding="utf-8"))
    entries = {str(e["engine"])
               for c in catalogue["categories"] for e in c.get("entries", [])}

    source = (ROOT / "native" / "plot2d" / "src"
              / "QtPlotBackend.cpp").read_text(encoding="utf-8", errors="ignore")
    start = source.index("static const QStringList kEngines{")
    depth, index = 0, source.index("{", start)
    body_start = index
    while index < len(source):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                break
        index += 1
    body = source[body_start:index]
    declared = [name.replace('\\"', '"') for name in
                re.findall(r'QStringLiteral\("((?:[^"\\]|\\.)*)"\)', body)]

    duplicates = sorted({n for n in declared if declared.count(n) > 1})
    assert not duplicates, (
        "supportedEngines() names the same engine twice: " + ", ".join(duplicates))

    unsupported = sorted(entries - set(declared))
    assert not unsupported, (
        f"{len(unsupported)} catalogue engines are not in supportedEngines(), so "
        "every entry using them reports itself unsupported instead of drawing: "
        + ", ".join(unsupported[:15]))

    unreachable = sorted(set(declared) - entries)
    assert not unreachable, (
        f"{len(unreachable)} engines are declared supported but no catalogue "
        "entry selects them, so nothing can reach them and the sweep never "
        "renders them: " + ", ".join(unreachable[:15]))


def test_every_engine_named_in_a_selftest_exception_list_exists() -> None:
    """An exception list that names a non-existent engine excuses nothing.

    `PlotSelfTest.cpp` carries sets of engine names that are exceptions to a
    property check — `kOrderFree` asserts that specific engines MUST be
    invariant when the rows are shuffled. Membership is tested by name against
    the engine actually being swept, so a name no engine answers to never
    matches, asserts nothing, and is invisible: the run counts the set's size
    and reports it as the number of engines asserted.

    Eight of `kOrderFree`'s seventeen names were like that — "Scatter",
    "Hexbin", "Heatmap", "Swarm Plot", "Contour", "2D Density",
    "Raincloud Plot", "Bubble Chart" — against a catalogue whose engines are
    called "4D / 5D Scatter", "Hexbin Density", "2D Heatmap", "Swarm",
    "2D Contour" and "Raincloud". The engines they were reaching for are
    precisely the summary-shaped ones the check exists to protect, so a
    2-D heatmap that moved when its rows were shuffled would have passed.

    The self-test now fails on this itself. This is the cheaper copy: it runs
    in CI without a build, on every push.
    """
    import json

    catalogue = json.loads(
        (ROOT / "config" / "graph_catalogue.json").read_text(encoding="utf-8"))
    engines = {str(e["engine"])
               for c in catalogue["categories"] for e in c.get("entries", [])}

    source = (ROOT / "native" / "plot2d" / "src"
              / "PlotSelfTest.cpp").read_text(encoding="utf-8", errors="ignore")

    unknown: list[str] = []
    for match in re.finditer(r"static const QSet<QString>\s+(k\w+)\s*\{", source):
        name = match.group(1)
        end = source.index("};", match.end())
        listed = re.findall(r'QStringLiteral\("((?:[^"\\]|\\.)*)"\)',
                            source[match.end():end])
        for engine in listed:
            if engine.replace('\\"', '"') not in engines:
                unknown.append(f"{name}: {engine!r}")

    assert not unknown, (
        "self-test exception lists naming engines that do not exist, so the "
        "exception is never applied and the count the run reports is wrong:\n  "
        + "\n  ".join(unknown))


def test_no_comparison_uses_qlatin1string_on_a_non_ascii_name() -> None:
    """`QLatin1String` on a UTF-8 literal silently never matches.

    `QLatin1String` wraps the bytes of a narrow string literal and reads each
    ONE as a Latin-1 character. The sources here are UTF-8, so a subscript two
    arrives as three bytes and compares as three characters that are not
    U+2082; an accented e arrives as two. Compared against a real QString the
    result is never equal, and nothing warns — not the compiler, not the
    linker, not the sweep.

    Two engine branches were unreachable this way:

      - `Gompertz H₂ Kinetics`, whose branch fits the modified Gompertz
        equation by Gauss-Newton and puts P, Rm and lambda in the legend. It
        never ran. The engine still drew the raw series, so the build check
        passed it every time: "put ink on the page" cannot tell a fitted curve
        from an unfitted one.
      - `L'Abbé Plot`, whose branch builds the treated-against-control scatter
        and the line of no effect.

    Verified by execution, not by reading: compiled against Qt 6, the
    `QLatin1String` comparison returns false for both names and the
    `QStringLiteral` one returns true.

    Comments are stripped first, because the prose describing this fix quotes
    the broken form.
    """
    offenders: list[str] = []
    roots = [ROOT / "native" / "plot2d" / "src",
             ROOT / "native" / "plot2d" / "include",
             ROOT / "app" / "src"]
    for root in roots:
        for path in sorted(root.rglob("*")):
            if path.suffix not in {".cpp", ".h"}:
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
            text = re.sub(r"//[^\n]*", "", text)
            for match in re.finditer(r'QLatin1String\("((?:[^"\\]|\\.)*)"\)', text):
                literal = match.group(1)
                if any(ord(c) > 127 for c in literal):
                    line = text[:match.start()].count("\n") + 1
                    offenders.append(
                        f"{path.relative_to(ROOT)}:{line}: QLatin1String({literal!r})")

    assert not offenders, (
        "QLatin1String on a literal with non-ASCII characters. The comparison "
        "can never succeed, so whatever it guards is dead code:\n  "
        + "\n  ".join(offenders) + "\n  Use QStringLiteral.")


def test_every_catalogue_scale_variant_is_recognised_by_the_renderer() -> None:
    """1,503 of the 2,116 catalogue entries are axis-scale variants.

    An entry carries a `scale` — "Semi-Log Y", "Quantile X", "Mercator",
    "Blackman-Harris" — and the renderer switches on that string. A value the
    C++ does not recognise does not fail: the entry draws, on a linear axis,
    looking like the unscaled engine it was derived from. The person gets a
    plot that is wrong in the one respect they chose it for.

    This is the same silent-mismatch shape as the engine names, and the
    catalogue is generated by a script, so a new variant arrives in bulk.
    Currently all 26 distinct values are referenced.

    Comments are stripped: a scale named only in prose is not handled.
    """
    import json

    catalogue = json.loads(
        (ROOT / "config" / "graph_catalogue.json").read_text(encoding="utf-8"))
    scales = {str(e["scale"]) for c in catalogue["categories"]
              for e in c.get("entries", []) if e.get("scale")}

    corpus = ""
    for name in ("app/src/AppController.cpp",
                 "native/plot2d/src/PlotCanvas.cpp",
                 "native/plot2d/src/QtPlotBackend.cpp"):
        text = (ROOT / name).read_text(encoding="utf-8", errors="ignore")
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        text = re.sub(r"//[^\n]*", "", text)
        corpus += text

    # One value is handled without being named, and the exemption is written
    # down rather than the rule being loosened. `spectralWindowFor` matches
    # every other window by name and RETURNS Hann when none matched, so "Hann"
    # appears nowhere as a literal and is nonetheless the one window that is
    # certain to be applied. Loosening the check to substring matching instead
    # would have passed it for the wrong reason - "Hann" is a substring of
    # "Hanning" and of "Channel".
    handled_by_default = {
        "Hann": "spectralWindowFor returns SpectralWindow::Hann as its fallback",
    }

    unhandled = sorted(s for s in scales
                       if f'"{s}"' not in corpus and s not in handled_by_default)
    assert not unhandled, (
        f"{len(unhandled)} catalogue scale variant(s) the renderer never names, "
        "so those entries draw unscaled and look like the plain engine: "
        + ", ".join(unhandled))

    # An exemption for a value the catalogue no longer has is an exemption
    # nobody will notice has stopped applying.
    stale = sorted(set(handled_by_default) - scales)
    assert not stale, (
        "exempted scale variants that are not in the catalogue any more: "
        + ", ".join(stale))


def test_the_gompertz_fit_reports_whether_it_converged() -> None:
    """A clamped parameter must not be printed as though it were fitted.

    The modified-Gompertz branch clamps `P` and `Rm` from below on every
    Gauss-Newton iteration. On data that is not a saturating curve a step
    drives one of them negative, the clamp pins it, and it stays there — and
    the legend then read `P 0.333 · Rm 1e-12 · λ -0.0514`, in exactly the form
    a real result takes. Those three numbers are usually the point of a batch
    fermentation experiment, and a reader had no way to tell a measurement from
    a floor value.

    The fitter itself is sound. Reimplemented faithfully and run against a true
    modified-Gompertz curve with P=100, Rm=6, λ=5, it recovers exactly those to
    three significant figures with R² = 1.000, and R² = 0.999 with noise added.
    Against pure noise and against a falling line it pins, and now says so.

    This is structural rather than numerical: the fit is inline in a
    `prepareSpecCore` branch and cannot be lifted out by name the way
    `quantileOf` can. What it protects is the honesty, not the arithmetic —
    that the branch still asks whether the fit converged and still reports a
    goodness of fit, rather than going back to printing whatever the clamp left
    behind.
    """
    source = (ROOT / "native" / "plot2d" / "src"
              / "QtPlotBackend.cpp").read_text(encoding="utf-8", errors="ignore")
    # Anchored on the BRANCH GUARD, not the bare name: the name also appears in
    # the supportedEngines list far earlier in the file, and a first attempt at
    # this scanned that instead and failed against a region with no fit in it.
    start = source.index('if(in.engine==QStringLiteral("Gompertz H₂ Kinetics")')
    branch = source[start:start + 8000]

    assert "pinned" in branch, (
        "the Gompertz branch no longer checks whether a parameter ended on its "
        "clamp, so a failed fit is reported as a fitted value again")
    assert "did not converge" in branch, (
        "the Gompertz branch no longer says when the fit failed")
    assert "R²" in branch, (
        "the Gompertz branch no longer reports a goodness of fit, so a fit that "
        "converged to something poor is quotable with nothing to weigh it")


def test_hexbin_density_does_not_share_the_square_celled_painter() -> None:
    """An engine named after a method must perform that method.

    "Hexbin Density" shared the 2-D histogram's `prepareSpecCore` branch AND
    dispatched to `drawHeatmap`, which counts points into square cells and
    draws them with `drawRect`. There was no hexagon anywhere in the renderer.
    The two engines rendered the same picture, which is how the sweep's
    same-picture check found them.

    That is a misnamed method rather than a cosmetic difference. Hexagonal
    binning exists because a hexagonal lattice has a uniform nearest-neighbour
    distance — every neighbour exactly one pitch away — where a square cell has
    neighbours at 1 and at sqrt(2), so a square count is biased by the grid's
    orientation in a way a hexagonal one is not.

    The lattice was verified numerically before this was written, by lifting
    the binning out and running it:

      - 200,000 random points, every one assigned to its nearest centre (the
        two-candidate test is exact, checked against brute force);
      - 100,000 points binned into 656 cells with none lost or double-counted;
      - all six neighbours equidistant, ratio 1.000000 against 1.414 for a
        square grid.

    (The first version of that neighbour check reported 1.732 and looked like a
    broken lattice. The lattice was right; the check's neighbour offsets
    ignored row parity, and an odd row is shifted half a width.)
    """
    source = (ROOT / "native" / "plot2d" / "src"
              / "QtPlotBackend.cpp").read_text(encoding="utf-8", errors="ignore")

    assert "void QtPlotBackend::drawHexbin(" in source, (
        "drawHexbin is gone, so Hexbin Density is back to square cells")

    start = source.index("void QtPlotBackend::drawHeatmap(")
    end = source.index("\n}\n", start)
    heatmap = source[start:end]
    assert "drawHexbin" in heatmap, (
        "drawHeatmap no longer hands Hexbin Density to the hexagonal painter")
    # The square-cell density path must no longer claim the hexbin engine.
    density = re.search(r"const bool density=([^;]+);", heatmap)
    assert density, "drawHeatmap's density flag has moved"
    assert "Hexbin" not in density.group(1), (
        "Hexbin Density is being counted into square cells again: "
        + " ".join(density.group(1).split()))


def test_heatmap_cells_are_placed_through_the_axes() -> None:
    """A grid cell must land where the axes say its data is.

    `drawHeatmap` used to divide the plot area by the cell count and draw from
    its left edge:

        const double cellW = f.plotArea.width()/double(g.nx);
        p->drawRect(QRectF(f.plotArea.left()+cx*cellW, ...));

    That silently assumes the grid spans exactly the range the axes show. It
    does not. `gridFromSeries` derives its own bounds from the data, while the
    frame's range comes from `computeRange`, which reads every series' `.x` and
    `.y` — and for a COLUMN-SHAPED engine the x data lives in `series[0].y`
    while `.x` holds something else. The two ranges differ, and the cells were
    stretched to fill an axis describing different numbers.

    It was invisible because it was self-consistent: the picture filled the
    frame and looked right. It surfaced only when `Hexbin Density` got its own
    painter, which maps through `toDevice`, and the two engines disagreed about
    where the same data was — hexagons in one place, rectangles in another, the
    same column, the same axis label.

    Mapping each cell's own coordinates through `toDevice` also fixes
    logarithmic axes, where linearly-spaced bins were being drawn at uniform
    width.
    """
    source = (ROOT / "native" / "plot2d" / "src"
              / "QtPlotBackend.cpp").read_text(encoding="utf-8", errors="ignore")
    start = source.index("void QtPlotBackend::drawHeatmap(")
    end = source.index("\n}\n", start)
    body = source[start:end]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)

    assert "toDevice(" in body, (
        "drawHeatmap no longer maps its cells through toDevice, so they are "
        "placed without reference to the axes they are drawn against")

    stretched = re.search(r"plotArea\.(width|height)\(\)\s*/\s*double\(g\.n[xy]\)",
                          body)
    assert not stretched, (
        "drawHeatmap is sizing cells by dividing the plot area again: "
        + stretched.group(0) + ". That stretches the grid to fill whatever "
        "range the axes happen to show, which is how every cell ended up at "
        "the wrong coordinate.")


def _backend_source() -> str:
    return (ROOT / "native" / "plot2d" / "src"
            / "QtPlotBackend.cpp").read_text(encoding="utf-8", errors="ignore")


def _strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def test_a_column_shaped_engine_scales_its_axes_from_its_columns() -> None:
    """The axis must describe the column it is labelled with.

    `prepareSpec` labels a field engine's axes from the mapped columns:

        out.xAxis.label = in.series.at(0).label;
        out.yAxis.label = in.series.at(1).label;

    but `computeRange` scaled them from every series' `.x` and `.y`. For these
    twelve engines the painter reads `series[0].y` and `series[1].y` as the two
    positions, and `.x` holds whatever the x MAPPING happened to be — a
    different column entirely.

    So a 2-D histogram of a signal against time drew an x axis running 0 to 12,
    the range of the time column, with "signal" written underneath it, and the
    signal only ever ran from 2.2 to 5.2. Nothing looked broken, because
    `drawHeatmap` stretched its cells across the plot area and agreed with the
    wrong axis. Both halves had to be wrong in the same way for the picture to
    look right.

    This guard asserts the range is taken from the columns. It does not assert
    the list is any particular length — engines get added — only that the
    branch exists, that it reads series 0 and series 1, and that the names it
    covers are the same ones `engineExplain` tells the person are column
    engines. Those two lists drifting apart is how an engine ends up explained
    as one shape and scaled as another.
    """
    source = _backend_source()

    start = source.index("static bool columnShapedAxes(")
    predicate = _strip_comments(source[start:source.index("\n}\n", start)])
    named = set(re.findall(r'QStringLiteral\("([^"]+)"\)', predicate))
    assert named, "columnShapedAxes no longer names any engine"

    start = source.index("QtPlotBackend::Frame QtPlotBackend::computeRange(")
    body = _strip_comments(source[start:source.index("\n}\n", start)])
    assert "columnShapedAxes(spec.engine)" in body, (
        "computeRange no longer asks whether the spec is column-shaped, so a "
        "field engine's axes are back to being scaled from a column nobody "
        "labelled them with")
    for column in ("spec.series.at(0).y", "spec.series.at(1).y"):
        assert column in body, (
            f"computeRange does not read {column}; the column-shaped branch "
            "is not reading the columns")

    # The same names engineExplain uses for its two "needs N mapped columns"
    # messages. Read out of that function rather than repeated here, so this
    # test cannot be the thing that is stale.
    after = source.index("const ColumnPlan plan=columnPlan(e);")
    expected: set[str] = set()
    for which in ("kGridEngines{", "kVectorEngines{"):
        start = source.index(which, after)
        literal = source[start:source.index("};", start)]
        found = set(re.findall(r'QStringLiteral\("([^"]+)"\)',
                               _strip_comments(literal)))
        assert found, f"engineExplain's {which.rstrip('{')} has moved"
        expected |= found
    assert named == expected, (
        "columnShapedAxes and engineExplain disagree about which engines are "
        "column-shaped. Only in one: "
        + ", ".join(sorted(named ^ expected)))


def test_a_field_is_not_padded_on_one_axis_only() -> None:
    """Five per cent of headroom belongs to a series, not to a region.

    `computeRange` pads the y range by 5% so the topmost marker is not drawn on
    the frame. x is never padded. For a field that asymmetry is visible: the
    cells span exactly the bounds `gridFromSeries` measured, so padding y alone
    leaves a strip of background above and below a heatmap and none at either
    side, and the image no longer fills its own plot area.
    """
    source = _backend_source()
    start = source.index("QtPlotBackend::Frame QtPlotBackend::computeRange(")
    body = _strip_comments(source[start:source.index("\n}\n", start)])
    pad = re.search(r"if\(!f\.yLog([^)]*)\)\{", body)
    assert pad, "computeRange's y-padding guard has moved"
    assert "!columns" in pad.group(1), (
        "the 5% y pad is being applied to column-shaped field engines again, "
        "which pads one axis of an image and not the other")


def test_every_field_painter_places_its_grid_through_the_axes() -> None:
    """Each of the three field painters, held to what drawHeatmap now does.

    `drawHeatmap` was fixed first, and fixing one painter of three is how the
    same figure comes out in two different places depending on which engine
    drew it. `drawContour` and the divergence/vorticity branch of
    `drawVectorField` both divided the plot area the same way — and in
    `drawVectorField` the stretch sat in the same function as the arrows and
    streamlines, which already went through `toDevice`, so one painter placed
    the same grid in two places.
    """
    source = _backend_source()
    for name in ("drawHeatmap", "drawContour", "drawVectorField"):
        start = source.index(f"void QtPlotBackend::{name}(")
        body = _strip_comments(source[start:source.index("\n}\n", start)])
        assert "toDevice(" in body, (
            f"{name} no longer maps anything through toDevice, so what it "
            "draws is placed without reference to the axes it is drawn against")
        # A cell SIZE derived from the plot area is the stretch. An arrow
        # length derived from it is not: an arrow is drawn in device pixels
        # by design, and scaling it to the on-screen cell spacing is what
        # keeps a dense field from becoming a solid block of ink.
        stretched = re.search(
            r"const double c\w*\s*=\s*f\.plotArea\.(?:width|height)\(\)"
            r"\s*/\s*double\(", body)
        assert not stretched, (
            f"{name} is sizing cells by dividing the plot area again: "
            + stretched.group(0) + ". That stretches the grid to fill "
            "whatever range the axes happen to show.")


def _dispatch_for(source: str, engine: str) -> str:
    """The painter `render` sends one engine to."""
    hit = re.search(
        r'spec\.engine==QLatin1String\("' + re.escape(engine) + r'"\)\)\s*(\w+)\(',
        source)
    assert hit, f"no render dispatch found for {engine}"
    return hit.group(1)


def test_no_catalogue_entry_is_another_one_under_a_second_name() -> None:
    """Two names, two behaviours — or the catalogue is lying about its size.

    Three entries were aliases. `Fill Between` set `out.engine="Area"` and
    returned; `Raincloud` dispatched to `drawViolin`. In each case the sweep's
    same-picture check found the pair in one group and the two figures were
    byte-identical below the title.

    An alias is not a cosmetic problem. `Fill Between` names the region between
    two curves — what a confidence band, a tolerance envelope or a daily
    min/max range is drawn as — and none of those can be expressed by filling
    each curve to zero, which is what `Area` does. `Raincloud` names a form
    that exists precisely because a violin shows a kernel density estimate and
    discards the observations it was fitted to.

    This is the same defect `Hexbin Density` had when it shared the 2-D
    histogram's square-celled painter, and it is checked the same way.
    """
    source = _backend_source()

    assert _dispatch_for(source, "Fill Between") == "drawFillBetween", (
        "Fill Between is dispatching to someone else's painter again")
    assert _dispatch_for(source, "Raincloud") == "drawRaincloud", (
        "Raincloud is dispatching to someone else's painter again")
    assert _dispatch_for(source, "Violin Plot") == "drawViolin"
    assert _dispatch_for(source, "Area") == "drawArea"

    # And prepareSpec must not put it back by rewriting the engine name, which
    # is how Fill Between became an alias in the first place — the dispatch
    # above would still read correctly and never be reached.
    body = _strip_comments(source)
    rewritten = re.search(
        r'in\.engine==QLatin1String\("Fill Between"\)\)\{[^}]*'
        r'out\.engine=QStringLiteral', body, flags=re.S)
    assert not rewritten, (
        "Fill Between is being rewritten into another engine by prepareSpec, "
        "so its own painter is never reached")


def test_a_violin_is_measured_against_the_data_not_against_its_density() -> None:
    """The axis has to be told the range, because it cannot work it out.

    The violin packing puts the sample POSITIONS in `.x` and the DENSITIES in
    `.y` — `drawViolin` calls `toDevice(f, slot, s.x[i])`, so a point's height
    comes from `.x`. `computeRange` reads `.y` for the y range, so the axis was
    in units of probability density while the violins were drawn in units of
    the data.

    The catalogue fixture hid it: its widest column tops out near 6.9 and its
    narrowest is a p-value in 0..1 whose density peaks near 8.6, so the density
    range happened to *contain* the value range. The only symptom was an axis
    running to 8.6 with nothing above 6.9 — which reads as a badly chosen
    limit, not as the wrong quantity.

    A column of pH readings between 6.9 and 7.4 has a density near 2, and would
    have drawn its violin at y = 7 on an axis ending at 2.
    """
    source = _backend_source()
    start = source.index('in.engine==QLatin1String("Violin Plot")')
    body = _strip_comments(source[start:source.index("\n    }\n", start)])
    for field in ("out.yAxis.min=", "out.yAxis.max="):
        assert field in body, (
            f"the violin rewrite no longer sets {field.rstrip('=')}, so the y "
            "axis is back to being scaled by the kernel density estimate "
            "rather than by the values the violins are drawn at")
    assert "isUnset(out.yAxis.min)" in body, (
        "the violin rewrite is setting the y limit unconditionally, which "
        "overrides a limit the person set themselves")


def test_an_area_draws_the_markers_its_series_asked_for() -> None:
    """`drawArea` dropped `drawMarkers` silently.

    Several engines rewrite themselves into an Area and set it. A payload-range
    envelope is four corner points with straight legs between them, and the
    corners are the numbers the chart is read for: the maximum payload, the
    range at which payload starts trading against fuel, and the ferry range.
    None were drawn, and the figure was indistinguishable from a plain Area —
    which is how it ended up in a same-picture group with one.
    """
    source = _backend_source()
    start = source.index("void QtPlotBackend::drawArea(")
    body = _strip_comments(source[start:source.index("\n}\n", start)])
    assert "s.drawMarkers" in body, (
        "drawArea ignores drawMarkers again, so every engine that rewrites "
        "itself into an Area and asks for markers is silently refused them")


def test_a_polar_bubble_sizes_its_bubbles_from_a_column() -> None:
    """Marks all one size is not a bubble chart.

    `Polar Bubble` and `Polar Scatter` both went through `drawPolar` with
    `markersOnly`, which draws every mark at the series' single scalar
    `markerSize`. The two gallery files came out byte-for-byte identical —
    25,541 bytes each — which is what a same-picture check is for.

    The fix is a third mapped column read as the size, which also means
    `columnPlan` has to ask for three: with the default plan the mapping filled
    two and the painter had nothing to size anything with.

    The area/diameter assertion is not a detail. A bubble is read by how much
    ink it is, so sizing the diameter linearly makes a value four times larger
    look sixteen times larger — the commonest way a bubble chart misreports
    its own data.
    """
    source = _backend_source()

    start = source.index("QtPlotBackend::ColumnPlan QtPlotBackend::columnPlan(")
    plan = _strip_comments(source[start:source.index("\n}\n", start)])
    hit = re.search(r'engine==QLatin1String\("Polar Bubble"\)\)\s*return\s*\{(\d+)',
                    plan)
    assert hit, "Polar Bubble has no column plan of its own again"
    assert int(hit.group(1)) >= 3, (
        "Polar Bubble asks for fewer than three columns, so the automatic "
        "mapping never supplies the one the marks are sized by")

    start = source.index("void QtPlotBackend::drawPolar(")
    body = _strip_comments(source[start:source.index("\n}\n", start)])
    assert 'spec.engine==QLatin1String("Polar Bubble")' in body, (
        "drawPolar no longer distinguishes a bubble, so it is Polar Scatter "
        "under a second name again")
    assert "std::sqrt" in body, (
        "the bubble size is not being taken through a square root, so the "
        "marks are sized by diameter and a value four times larger reads as "
        "sixteen times larger")


def test_a_field_axis_is_labelled_with_the_column_it_is_scaled_from() -> None:
    """One list decides both, or the axis contradicts itself.

    `computeRange` now scales a field's axes from series 0 and series 1
    (`columnShapedAxes`). `prepareSpec` has to LABEL them from the same two, or
    the ticks describe one column and the words underneath describe another.

    That mismatch was invisible while both were wrong: the grid engines got the
    labels and nobody got the ranges, so a 2-D histogram showed the time
    column's numbers under the word "signal" and looked merely oddly scaled.
    Fixing the range made it legible — a divergence map came out with ticks
    running over the signal column and "time_h" still written under them.

    So the check is that the two lists are the same list. Anything
    `columnShapedAxes` covers must be labelled from its columns somewhere in
    `prepareSpec`.
    """
    source = _backend_source()
    start = source.index("static bool columnShapedAxes(")
    scaled = set(re.findall(
        r'QStringLiteral\("([^"]+)"\)',
        _strip_comments(source[start:source.index("\n}\n", start)])))
    assert scaled

    # Every branch that takes its axis names from the first two series.
    labelled: set[str] = set()
    for hit in re.finditer(
            r"out\.xAxis\.label=in\.series\.at\(0\)\.label;\s*"
            r"out\.yAxis\.label=in\.series\.at\(1\)\.label;", source):
        # The engine names guarded above it, back to the start of the branch.
        window = source[max(0, hit.start() - 1400):hit.start()]
        labelled |= set(re.findall(r'QStringLiteral\("([^"]+)"\)',
                                   _strip_comments(window)))
        labelled |= set(re.findall(r'QLatin1String\("([^"]+)"\)',
                                   _strip_comments(window)))

    missing = sorted(scaled - labelled)
    assert not missing, (
        "these engines have their axes SCALED from series 0 and 1 but not "
        "LABELLED from them, so the ticks and the words under them describe "
        "different columns: " + ", ".join(missing))


def test_a_stacked_axis_contains_the_stack() -> None:
    """Everything that reasons about a stacked figure reads one definition.

    `drawStackedLines` draws each band at the RUNNING TOTAL. Two other places
    have to know that, for different reasons, and both got it wrong
    independently:

    - `computeRange` measured each series on its own, so five columns each
      reaching about 7 stacked to about 25 on an axis that ended at 7.2 and the
      top three bands were clipped away;
    - `drawLegend`, scoring which corner was free, scored the individual series
      too — so with the axis corrected to 16 every value (2 to 7) sat in the
      lower half, the top-right box scored zero, and the legend stayed sitting
      on a band running along y = 15.

    The second is the instructive one: the same defect, in a function that
    looks unrelated, found only because the picture did not change. Two copies
    of a rule is one copy too many, so both now read `stackedBands()`, and the
    guard is that they do — not merely that each has *a* stacked branch, which
    would pass for two branches that disagreed.

    The accumulation is still compared against the painter's, including the
    rule that a non-finite value contributes zero rather than breaking the
    band. This compares the expressions as written; it does not run them.
    """
    source = _backend_source()

    start = source.index("void QtPlotBackend::drawStackedLines(")
    painter = _strip_comments(source[start:source.index("\n}\n", start)])
    assert re.search(r"finite\(s\.y\[i\]\)\?s\.y\[i\]:0\.0", painter), (
        "drawStackedLines no longer accumulates with the "
        "finite(s.y[i])?s.y[i]:0.0 rule, so stackedBands() is describing a "
        "different picture from the one being drawn")

    start = source.index("static StackedBands stackedBands(")
    helper = _strip_comments(source[start:source.index("\n}\n", start)])
    assert re.search(r"finite\(s\.y\[i\]\)\?s\.y\[i\]:0\.0", helper), (
        "stackedBands() no longer matches the painter's accumulation")
    assert "out.lo=qMin(out.lo,out.top[i]);" in helper, (
        "the extremes are no longer tracked over every partial sum; with a "
        "negative contribution the tallest band is not necessarily the last")
    declaration = _strip_comments(
        source[source.index("struct StackedBands {"):source.index("static StackedBands")])
    assert re.search(r"double\s+lo\s*=\s*0\.0\s*,\s*hi\s*=\s*0\.0\s*;", declaration), (
        "the stack range no longer starts from the baseline, which is drawn "
        "and therefore part of what has to be contained")

    for caller, why in (
            ("QtPlotBackend::Frame QtPlotBackend::computeRange(",
             "the axis is scaled by the tallest single series and the upper "
             "bands are clipped"),
            ("void QtPlotBackend::drawLegend(",
             "the legend picks its corner by where the individual series are, "
             "not where the bands it would cover actually run")):
        start = source.index(caller)
        body = _strip_comments(source[start:source.index("\n}\n", start)])
        assert "stackedBands(spec)" in body, (
            f"{caller.split('::')[-1].rstrip('(')} no longer reads "
            f"stackedBands(), so {why}")


def test_a_borehole_log_is_read_down_the_page() -> None:
    """Depth is vertical and increases downward, or it is not a borehole log.

    It shared the schedule's branch — row, start, end, drawn as a horizontal
    bar — so it came out as a Gantt chart with the word "depth" under the
    horizontal axis, and the sweep found `Availability Timeline`, `Borehole
    Log` and `Gantt Schedule` in one same-picture group.

    `drawHorizontalBar`'s own comment makes this argument in the other
    direction: a Gantt with time on the vertical axis is not a Gantt, it is a
    puzzle. A borehole with depth across the page is that puzzle transposed.

    Inverted by the axis flag, not by negating the depths — negating puts a
    minus sign on every tick and makes the axis label a lie.

    The other two of that group are left alone deliberately: a schedule and an
    availability timeline are the same geometry with different words, and the
    words are how someone finds the figure they want.
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Borehole Log")){')
    body = _strip_comments(source[start:source.index("\n    }\n", start)])

    assert 'derivedAs(in,QStringLiteral("Floating Bar"))' in body, (
        "Borehole Log is being drawn by the horizontal floating-ROW painter "
        "again, which lays depth out across the page")
    assert "out.yAxis.inverted=true" in body, (
        "the depth axis is no longer inverted, so a borehole log reads with "
        "the deepest bed at the top")
    assert "in.yAxis.inverted" in body, (
        "the inversion is being forced, overriding a person who asked for "
        "height above a datum rather than depth below one")
    assert not re.search(r"=\s*-\s*(from|to|depth)", body), (
        "depths are being negated to get the axis direction, which puts a "
        "minus sign on every tick")

    # And the branch it left must not still be testing for it.
    start = source.index('if(in.engine==QLatin1String("Gantt Schedule")')
    gantt = _strip_comments(source[start:source.index("\n    }\n", start)])
    assert "Borehole Log" not in gantt, (
        "the schedule branch still has a dead Borehole Log arm in it")


def _selftest_source() -> str:
    return (ROOT / "native" / "plot2d" / "src"
            / "PlotSelfTest.cpp").read_text(encoding="utf-8", errors="ignore")


def test_a_feather_is_not_gridded() -> None:
    """A feather shows every observation; a quiver shows a lattice of means.

    `Feather` fell through to the quiver branch, so it drew the same gridded,
    strided arrows and the two came out in one same-picture group. The comment
    on that branch said "Quiver, and Feather, which is the same arrows anchored
    on a line" — which described an intention. Both were anchored on the grid.

    The difference is not the mark, it is what is drawn. A current-meter record
    is read for a veer, a lull, a reversal; binning averages those away and
    striding past nine samples in ten does the rest.

    The branch has to come BEFORE `cachedGrid`, or the figure pays for a grid
    it does not use.
    """
    source = _backend_source()
    start = source.index("void QtPlotBackend::drawVectorField(")
    body = _strip_comments(source[start:source.index("\n}\n", start)])

    feather = body.find('spec.engine==QLatin1String("Feather")')
    assert feather != -1, (
        "drawVectorField no longer distinguishes a feather, so it is the "
        "quiver's gridded arrows under a second name again")
    grid = body.find("cachedGrid(")
    assert grid != -1 and feather < grid, (
        "the feather branch is after the grid is built, so a plot that never "
        "looks at the grid is paying to construct one")


def test_every_same_picture_finding_names_engines_that_exist() -> None:
    """A finding about an engine that is not in the catalogue asserts nothing.

    The same-picture report prints a reason beside each cluster that has been
    adjudicated, so a NEW cluster stands out instead of joining eighteen lines
    nobody re-reads. That only works while the findings are true: one naming an
    engine that has been renamed or removed reads as though it still applies.

    The sweep itself catches the other half — a finding whose cluster no longer
    renders alike fails the run — but it cannot catch a typo in a name, because
    a misspelt cluster simply never matches anything and looks unadjudicated.
    So the names are checked against the renderer's own engine list here.
    """
    source = _selftest_source()
    # Either shape of the table: the populated C array, or the QVector the
    # empty form has to use (a zero-size array is a GCC extension and an error
    # under -Wpedantic).
    for opener in ("static const Adjudicated kAdjudicated[]={",
                   "static const QVector<Adjudicated> kAdjudicated{"):
        if opener in source:
            start = source.index(opener)
            break
    else:                                  # neither form: the table has moved
        raise AssertionError(
            "the same-picture findings table has moved or been renamed, so "
            "this check is reading nothing and would pass on any content")
    table = source[start:source.index("};", start)]
    clusters = re.findall(r'\{"([^"]+)",', table)
    # EMPTY IS AN ANSWER, and it is the one the catalogue gives today.
    #
    # This used to assert the table had entries, on the reasoning that an empty
    # match means the table has MOVED and the regex is reading nothing. The
    # reasoning is sound and the assertion was the wrong way to hold it: the
    # last adjudication - Geo Line against Ground Track - was removed when the
    # geographic engines were given coordinates instead of the shared signal
    # column, and the two stopped rendering alike. A table with nothing left to
    # excuse is the catalogue in the state everything here is working towards.
    #
    # So "moved" is checked directly, by requiring the empty table to be
    # written as an empty table. A relocated or renamed one still fails.
    if not clusters:
        # A QVector, not `Adjudicated kAdjudicated[]={}` - a zero-size array is
        # a GCC extension and an error under -Wpedantic, so the empty form of
        # this table has to be expressible without a dialect extension or the
        # table can never be emptied.
        assert "static const QVector<Adjudicated> kAdjudicated{};" in source, (
            "the same-picture findings table has moved or been renamed, so "
            "this check is reading nothing and would pass on any content")

    named: set[str] = set()
    for cluster in clusters:
        named |= {part.strip() for part in cluster.split("=")}

    backend = _backend_source()
    start = backend.index("QStringList QtPlotBackend::supportedEngines()")
    declared = set(re.findall(r'QStringLiteral\("([^"]+)"\)',
                              backend[start:backend.index("\n}\n", start)]))
    if not declared:                       # the list lives in a file-static table
        start = backend.index("kEngines")
        declared = set(re.findall(r'QStringLiteral\("([^"]+)"\)',
                                  backend[start:backend.index("};", start)]))

    missing = sorted(named - declared)
    assert not missing, (
        "the same-picture findings name engines the renderer does not "
        "declare, so those findings can never match a cluster: "
        + ", ".join(missing))

    assert "no longer render alike" in source, (
        "the sweep no longer fails on a finding whose cluster has been fixed, "
        "so a note about Fill Between survives the day it stops being an alias")


def test_the_raincloud_jitter_does_not_draw_lines_of_its_own() -> None:
    """A displacement correlated with the index prints stripes in the rain.

    The first version was `index * 2654435761 % 1024`. The observations are
    sorted, so a point's height rises steadily with its index — and a
    displacement that is near-linear in the index rises with it. Two correlated
    coordinates draw diagonal lines, and the gallery figure had them clearly:
    structure a reader would take for structure in the data, which is the worst
    kind of artefact a plot can have.

    So this measures the property that matters rather than asserting a
    particular hash: run the mixer exactly as the source writes it and check
    the lag-1 autocorrelation of the offsets is near zero. The original scores
    about -0.46. Anything that scores like that draws stripes, whatever it is
    called.
    """
    source = _backend_source()
    start = source.index("void QtPlotBackend::drawRaincloud(")
    body = _strip_comments(source[start:source.index("\n}\n", start)])

    steps = re.findall(r"mixed(\^=mixed>>(\d+)|\*=0x([0-9a-fA-F]+)u)", body)
    assert len(steps) >= 4, (
        "the raincloud jitter no longer looks like a mixing hash; if it has "
        "been replaced, this test has to be replaced with one that measures "
        "the new thing's autocorrelation")
    modulus = re.search(r"mixed%(\d+)u", body)
    assert modulus, "the jitter's range has moved"
    span = int(modulus.group(1))

    mask = 0xFFFFFFFF

    def offset(index: int) -> float:
        mixed = index & mask
        for _, shift, factor in steps:
            if shift:
                mixed ^= mixed >> int(shift)
            else:
                mixed = (mixed * int(factor, 16)) & mask
        return (mixed % span) / span

    values = [offset(i) for i in range(2000)]
    mean = sum(values) / len(values)
    centred = [v - mean for v in values]
    variance = sum(c * c for c in centred)
    lag1 = sum(centred[i] * centred[i - 1] for i in range(1, len(centred)))
    correlation = lag1 / variance

    assert abs(correlation) < 0.05, (
        f"consecutive raincloud offsets are correlated ({correlation:+.3f}); "
        "with the observations sorted by value that draws diagonal stripes "
        "through the rain that are not in the data")


def test_the_legend_goes_where_the_data_is_not() -> None:
    """A legend over the data hides it without leaving a sign that it did.

    The box was always the top right, and in this catalogue that is very often
    where the data is: a stacked band, a cumulative curve and a raincloud's
    last group all finish underneath it. Unlike a clipped axis, this leaves
    nothing for the reader to notice — the figure looks complete.

    Two properties are checked, and the second matters as much as the first:

    - the corner is chosen by counting the figure's own points in each
      candidate, not guessed from the engine name;
    - TIES GO TO THE TOP RIGHT, so a figure with room everywhere is drawn
      exactly where it always was. Without that, every figure in every
      published document moves the first time this is rebuilt, for nothing.
      The strict `<` in the selection is what guarantees it.
    """
    source = _backend_source()
    start = source.index("void QtPlotBackend::drawLegend(")
    body = _strip_comments(source[start:source.index("\n}\n", start)])

    assert "candidates[4]" in body, (
        "drawLegend no longer considers more than one position, so it is back "
        "to being drawn over whatever happens to be in the top right")
    assert "toDevice(" in body, (
        "the corner is being chosen without reference to where the points "
        "actually land")

    first = re.search(r"QRectF\(f\.plotArea\.(right|left)\(\)[^,]*,\s*"
                      r"f\.plotArea\.(top|bottom)\(\)", body)
    assert first and first.group(1) == "right" and first.group(2) == "top", (
        "the first candidate is no longer the top right, so a tie no longer "
        "resolves there and every existing figure moves")

    pick = re.search(r"for\(int k=1;k<4;\+\+k\)\s*if\(occupancy\[k\]\s*(<=?)\s*"
                     r"occupancy\[chosen\]\)", body)
    assert pick, "the corner selection has moved"
    assert pick.group(1) == "<", (
        "the selection uses <=, so a four-way tie picks the LAST candidate "
        "instead of the top right and every figure with room everywhere moves")

    stride = re.search(r"stride=qMax\(1,total/(\d+)\)", body)
    assert stride, (
        "the occupancy count no longer strides, so placing a 90-pixel box "
        "costs a full pass over the data on every repaint")


def test_an_implicit_plot_draws_the_zero_level_and_nothing_else() -> None:
    """f(x, y) = 0 is the definition, not one level among ten.

    `Implicit Function` rewrote to `2D Contour`, and the comment said "an
    implicit curve is the zero level set, which is exactly what the contour
    engine draws". That was wrong about `drawContour`: it draws ten evenly
    spaced levels between the data's minimum and maximum. Zero is one of them
    only by luck, and the other nine are curves of a function nobody asked
    about — for `x^2 + y^2 - 1` the reader gets ten concentric circles where
    the engine's name promises one.

    The variable that would have carried the distinction was computed and then
    discarded with `Q_UNUSED(implicit)`, so the intention was on the record; it
    just never reached the painter. That line must not come back.

    Also checked: a formula with no zero in the domain says so. An empty frame
    with no explanation is the single most confusing thing this program can do,
    and the surrounding code already reports a formula that will not compile.
    """
    source = _backend_source()
    start = source.index('if(in.engine.startsWith(QLatin1String("Function"))')
    rewrite = _strip_comments(source[start:source.index("\n        // Function Plot", start)])

    assert "Q_UNUSED(implicit)" not in rewrite, (
        "the implicit flag is being discarded again, so an implicit plot is "
        "ten arbitrary contours of f rather than the curve where f is zero")
    assert '"@zeroLevelOnly"' in rewrite, (
        "nothing tells the painter this is an implicit plot")
    assert "has no zero between" in rewrite, (
        "a formula with no zero in the domain now draws an empty frame with "
        "no explanation")

    start = source.index("void QtPlotBackend::drawContour(")
    painter = _strip_comments(source[start:source.index("\n}\n", start)])
    assert 'parameter(QStringLiteral("@zeroLevelOnly")' in painter, (
        "drawContour no longer reads the implicit flag")
    assert re.search(r"level<=\(zeroOnly\?1:kLevels\)", painter), (
        "drawContour draws more than one level for an implicit plot")
    assert re.search(r"iso=zeroOnly\?0\.0:", painter), (
        "the implicit curve is no longer pinned to zero, which is the only "
        "value that makes it the thing the engine is named after")
    assert "if(!zeroOnly)" in painter, (
        "a colour bar is drawn beside a single curve, inviting the reader to "
        "look up a value the bar does not show")


def test_a_scatter_with_marginals_has_marginals() -> None:
    """The limitation was real; it was being applied to the wrong figure.

    `Scatter + Marginals` and `Plot Matrix` shared one rewrite and one painter,
    on the reasoning that both are small multiples and this backend draws one
    figure. That holds for a plot matrix — an n x n grid is n² figures and
    there is one frame — but a scatter with its marginals is a *single panel
    with two strips on it*, which is exactly what this backend draws. So the
    two came out identical and one of them had no excuse.

    The strips are the point of the form: the scatter answers how two
    variables move together, the marginals answer what each looks like alone —
    whether an outlier is extreme in x, in y, or only in the pair; whether a
    cloud that looks uniform is two modes in one variable. None of that is
    readable from the joint cloud.

    `Plot Matrix` is left as the honest single panel it always was.
    """
    source = _backend_source()
    assert _dispatch_for(source, "Scatter + Marginals") == "drawScatterMarginals", (
        "Scatter + Marginals is back to a painter that draws no marginals")

    start = source.index("void QtPlotBackend::drawScatterMarginals(")
    body = _strip_comments(source[start:source.index("\n}\n", start)])
    assert "drawScatter(p,f,spec)" in body, "the joint scatter is no longer drawn"
    assert body.count("drawRect(") >= 2, (
        "fewer than two marginal strips are drawn; the form has one per axis")
    assert "f.xLog" in body and "f.yLog" in body, (
        "the marginals are binned in raw space, so on a logarithmic axis the "
        "strip does not line up with the ticks underneath it and every decade "
        "but the last piles into one bin")


def test_every_3d_engine_draws_its_own_mark() -> None:
    """Nine engines were falling into one `else` and drawing a scatter.

    `draw3D` had three branches — surface, line, and everything-else-is-a-
    scatter. So `3D Bar` was a cloud of dots, `3D Stem` had no stalks, `Comet
    3D` had no head, `Surface + Contours` had neither a surface nor a contour,
    and `3D Contour` had no contours. All ten came out byte-identical: the
    largest same-picture group the sweep found.

    Each is named here rather than counted, because the failure this guards
    against is an engine quietly rejoining the `else` — which looks like
    nothing at all, since it still draws a perfectly good scatter.

    The shared machinery must STAY shared. The projection, the painter's
    algorithm sort and the cube are written once and every engine needs all
    three; ten copies of a depth sort is how they drift apart.
    """
    source = _backend_source()
    start = source.index("void QtPlotBackend::draw3D(")
    body = _strip_comments(source[start:source.index("\n}\n", start)])

    for engine in ("3D Bar", "3D Horizontal Bar", "3D Stem", "3D Bubble",
                   "3D Swarm", "Comet 3D", "Ribbon", "Surface + Contours",
                   "3D Contour"):
        assert f'QLatin1String("{engine}")' in body, (
            f"{engine} is no longer distinguished in draw3D, so it falls into "
            "the scatter branch and draws the same picture as nine others")

    # Counted by what they compare, not by the call: the swarm sorts rows by
    # HEIGHT to find the ties it fans apart, which is a different job and a
    # legitimate third sort. Counting `std::sort(` flagged it, which is a test
    # measuring the wrong thing rather than a fault in the code.
    assert body.count("a.depth<b.depth") == 2, (
        "draw3D has a different number of DEPTH sorts than the two it shares "
        "(quads, then marks); a per-engine copy is how two engines come to "
        "disagree about which mark is in front")

    # A bubble sized by diameter misreports its own data — same rule as the
    # polar one.
    bubble = body[body.index("bubbles"):]
    assert "std::sqrt" in bubble, (
        "3D Bubble is sized by diameter, so a value four times larger reads "
        "as sixteen times larger")


def test_a_3d_field_with_nothing_to_draw_draws_nothing() -> None:
    """An empty box with axes on it looks deliberate. A blank canvas does not.

    `draw3DField` drew the cube and the axes first and discovered afterwards
    that it had no vectors. Five engines — `3D Quiver`, `Cone Plot`, `Stream
    Tube`, `Stream Ribbon`, `Tensor Glyph Field` — need six mapped columns and
    the sweep fixture had five, so all five drew an empty box and the sweep
    found them in one same-picture group. They were identical because they were
    all EMPTY, not because they shared an implementation: the code for a cone,
    a tube and a tensor glyph is genuinely different.

    It also defeated the blank check. That compares the render against the same
    spec with its series cleared, and a cleared spec returns at `columns<4`
    without drawing a cube — so the cube counted as data and "every engine drew
    data" passed on five engines that had drawn none.

    Both halves are fixed: the painter returns before the cube, and the fixture
    supplies a real six-column field so the five can be told apart at all.
    """
    source = _backend_source()
    start = source.index("void QtPlotBackend::draw3DField(")
    body = _strip_comments(source[start:source.index("\n}\n", start)])

    guard = body.find("if(!haveVector&&!(engineNeedsScalarOnly(spec.engine))) return;")
    cube = body.find("drawBoundingCube(")
    assert guard != -1, (
        "draw3DField no longer returns when it has no vectors to draw")
    assert cube != -1 and guard < cube, (
        "the guard is after the cube is drawn, so an engine with nothing to "
        "draw still puts a box on the page and still passes the blank check")

    assert body.count("engine.startsWith(QLatin1String(\"Volume\"))") == 0, (
        "the volume family is being identified a second time inside the "
        "painter; the guard above and the painter must agree about who needs "
        "six columns, so both read engineNeedsScalarOnly()")

    fixture = _selftest_source()
    start = fixture.index("QVector<double> fieldX,fieldY,fieldZ,fieldU,fieldV,fieldW;")
    assert "fieldU.append(-y);" in fixture[start:start + 1200], (
        "the sweep's 3-D vector fixture is no longer a rotation, so the five "
        "glyph engines may have no direction to distinguish them")
    for engine in ("3D Quiver", "Cone Plot", "Stream Tube", "Stream Ribbon",
                   "Tensor Glyph Field"):
        assert f'{{QStringLiteral("{engine}"),\n            {{column("x",fieldX)' in fixture, (
            f"{engine} is no longer given the six-column field, so it draws "
            "nothing and cannot be told apart from the other four")


def test_no_engine_differs_from_another_only_in_default_labels() -> None:
    """A catalogue entry earns its place by drawing something different.

    A person can rename an axis themselves, so an engine whose entire
    contribution is two default axis names is not a second engine — it is the
    same engine listed twice, and it inflates a catalogue count that is
    supposed to mean something.

    Four entries were exactly that, and each turned out to have a real method
    behind it that had simply never been written:

    - `Dot Plot` stacks its observations into countable columns (Wilkinson);
      `Strip Plot` is the 1-D scatter. Both were plain scatters.
    - `Beeswarm` never moves a point off its value, pushing it sideways only
      as far as needed to clear its neighbours; `Swarm` bins by value first.
      Beeswarm rewrote straight to Swarm.
    - `Probability Plot` puts cumulative probability on an axis spaced by the
      reference distribution's quantiles, so percentiles are read directly;
      `Q-Q Plot` has quantiles on both axes. Probability Plot relabelled
      itself to Q-Q Plot and returned.
    - `Animated Line` marks and numbers steps along the path, because what an
      animation carries is WHEN; `Comet` fades its trail behind a bright head,
      because what it carries is NOW. Both drew one half-transparent line with
      a marker on the end.
    - `VFA Concentration Profile` draws the total, which is the number a
      digester is reported on and the one thing a stack does not show.
    - `Availability Timeline` computes the uptime percentage per row, which is
      what an availability chart exists to produce.

    This guard is the rule, not the list: no `prepareSpec` branch may consist
    of setting another engine's name and some default labels. It looks for the
    shape that keeps recurring — an engine assignment with nothing but
    `label.isEmpty()` guards after it.
    """
    source = _backend_source()
    body = _strip_comments(source)

    offenders: list[str] = []
    for hit in re.finditer(
            r'in\.engine==QLatin1String\("([^"]+)"\)\)\{\s*'
            r'PlotSpec out=in;\s*'
            r'out\.engine=QStringLiteral\("([^"]+)"\);'
            r'(?P<rest>(?:[^{}]|\{[^{}]*\})*?)return out;',
            body):
        rest = hit.group("rest")
        # Anything other than default-label assignments counts as real work.
        stripped = re.sub(r"if\(out\.[xy]Axis\.label\.isEmpty\(\)\)\s*"
                          r"out\.[xy]Axis\.label=QStringLiteral\(\"[^\"]*\"\);",
                          "", rest)
        stripped = re.sub(r"out\.legendVisible=[^;]*;", "", stripped)
        if not stripped.strip():
            offenders.append(f"{hit.group(1)} -> {hit.group(2)}")

    assert not offenders, (
        "these engines are another engine plus default axis labels, which a "
        "person can set themselves — either give them a method of their own "
        "or drop the catalogue entry: " + "; ".join(offenders))


def test_the_differentiated_engines_still_have_their_methods() -> None:
    """Named, because the rule above cannot see a method that is merely wrong.

    `test_no_engine_differs_from_another_only_in_default_labels` catches the
    shape — an engine assignment with nothing after it. It cannot tell a real
    Wilkinson dot plot from twenty lines that produce a scatter anyway. These
    are the specific mechanisms, each checked for the thing that makes it the
    method it is named after.
    """
    source = _backend_source()

    checks = [
        # The stack itself. Its HEIGHT is checked separately, by
        # test_a_dot_plot_axis_reads_in_dots — this only asks that the
        # observations are binned into columns at all, which is what makes it
        # a dot plot rather than a strip plot.
        ('in.engine==QLatin1String("Dot Plot")', "while(j<v.size()&&(v[j]-v[i])<width) ++j;",
         "a dot plot no longer bins its observations into columns, so it is "
         "a strip plot with a different name"),
        ('in.engine==QLatin1String("Beeswarm")', "pts.y.append(value);",
         "a beeswarm is moving points off their own value, which is the one "
         "thing that separates it from a swarm"),
        ('in.engine==QLatin1String("Probability Plot")', "normalQuantile(p)",
         "a probability plot no longer places its points by the reference "
         "distribution's quantiles"),
        ('in.engine==QLatin1String("VFA Concentration Profile")', "total VFA",
         "the VFA profile no longer draws the total, which is the number the "
         "figure is reported on"),
    ]
    for anchor, needle, why in checks:
        start = source.index(anchor)
        body = _strip_comments(source[start:source.index("\n    }\n", start)])
        assert needle in body, why

    # The probability axis has to be LABELLED in per cent, or the whole point
    # of the form — reading a percentile straight off it — is lost.
    start = source.index("QtPlotBackend::Frame QtPlotBackend::computeFrame(")
    frame = _strip_comments(source[start:source.index("\n}\n", start)])
    assert '"@probabilityAxisY"' in frame, (
        "computeFrame no longer places probability ticks, so the axis carries "
        "normal quantiles labelled as if they were values")
    assert "probability.size()>=3" in frame, (
        "the probability ticks are used even when only one or two fall in "
        "range, which is two labels and no scale")


def _qml(name: str) -> str:
    return (ROOT / "app" / "qml" / name).read_text(encoding="utf-8", errors="ignore")


def test_the_graph_library_shortcuts_resolve_against_the_live_catalogue() -> None:
    """A starred graph must be drawn from the same map as any other row.

    Favourites and Recently used are stored as RECORDS — category, engine,
    variant — and resolved against the catalogue each time they are read. Two
    things follow, and both are the point:

    - a starred entry is drawn from the catalogue's own map, thumbnail and
      description included, so there is no second code path that can look
      different from the rest of the list;
    - an entry whose pack has been switched off, or that a catalogue edit has
      renamed, simply does not resolve. It is left out rather than drawn as a
      blank row — and it is NOT forgotten, because the pack can be switched
      back on and the star was a deliberate act.

    Storing the resolved entry instead would freeze a description and a
    thumbnail path into the settings file, to go stale on the first catalogue
    edit.
    """
    source = (ROOT / "app" / "src" / "AppController.cpp").read_text(
        encoding="utf-8", errors="ignore")

    start = source.index("QVariantList AppController::resolveGraphRecords(")
    body = _strip_comments(source[start:source.index("\n}\n", start)])
    assert "graphEntries_" in body, (
        "the shortcut lists no longer resolve against the catalogue, so they "
        "are drawing from whatever was written to the settings file")
    assert "if(it!=byKey.constEnd()) out.append(*it);" in body, (
        "a record that does not resolve is no longer skipped; it will be "
        "drawn as a row with no engine behind it")

    # Forgetting is the failure that loses the person's work, so the removal
    # has to be the star and nothing else.
    start = source.index("void AppController::toggleFavouriteGraph(")
    toggle = _strip_comments(source[start:source.index("\n}\n", start)])
    assert "favouriteGraphs_.removeAt(i)" in toggle
    assert "favouriteGraphs_.append(record)" in toggle, (
        "favourites are being prepended; a shelf that reorders itself every "
        "time you add to it is one you cannot learn the shape of")

    header = (ROOT / "app" / "src" / "AppController.h").read_text(
        encoding="utf-8", errors="ignore")
    assert "const QString& category=QString());" in header, (
        "noteVisualisation no longer takes a category, so a recent graph "
        "cannot be resolved: an engine name is not unique across categories")


def test_the_recent_graph_list_holds_ten() -> None:
    """Ten, and trimmed on load as well as on write.

    A list written by an older build can be longer than the cap. Trimming only
    when something is added would leave the extra entries on screen until the
    person happened to apply a graph.
    """
    source = (ROOT / "app" / "src" / "AppController.cpp").read_text(
        encoding="utf-8", errors="ignore")
    cap = re.search(r"constexpr int kRecentGraphLimit=(\d+);", source)
    assert cap and int(cap.group(1)) == 10, (
        "the recent-graph cap is not ten")
    assert "while(recentVisualisations_.size()>kRecentGraphLimit)" in source, (
        "the recent list is not trimmed on load, so a longer list written by "
        "an older build survives until the next graph is applied")
    assert "kRecentGraphLimit);" in source, (
        "noteVisualisation is not passing the graph cap, so recents fall back "
        "to the twelve-entry dataset limit")


def test_the_star_does_not_also_stage_the_graph() -> None:
    """Two controls in one row, and the inner one has to stop the outer.

    The row's own TapHandler stages the graph. Without a gesture policy on the
    star's handler, starring something would stage it as well — which is not
    wrong so much as surprising, and surprising is worse in a list of 2,116
    rows where staging replaces what the person was looking at.

    Also checked: the hint row under an empty Favourites group is not a
    catalogue row. It has no entry behind it, so it must not be stageable and
    must not offer a star for a graph that is not there.
    """
    qml = _qml("components/GraphLibrary.qml")
    star = qml[qml.index("id: star"):]
    star = star[:star.index("HoverHandler { id: hover }")]

    # Three things the first version got wrong, and it rendered perfectly while
    # doing nothing at all when clicked.
    #
    # The binding calls isFavouriteGraph(), an ordinary invokable with no
    # notify signal, and QML evaluates a binding over a function call once —
    # there is nothing for the engine to watch. Naming a property that does
    # change is what makes it re-run. Rebuilding the row list was not enough:
    # the view reuses delegates, so a row that stayed put kept its stale glyph.
    assert "root.favouritesRevision" in star, (
        "the star's state is bound to a function call with nothing to notify "
        "it, so the glyph never changes however often the favourites do")
    assert "root.favouritesRevision = root.favouritesRevision + 1" in qml, (
        "nothing bumps the revision, so the binding above has a constant in it")

    # An item with visible:false receives no input. Hit-testing a control whose
    # existence depends on the pointer already being in the right place is a
    # race with itself.
    assert "opacity: star.pick === undefined" in star, (
        "the star is hidden rather than faded, so it cannot be clicked")
    # An ITEM-level visible binding, not any line containing the word:
    # `ToolTip.visible` is legitimate and a bare `"visible:" not in star`
    # flagged it, which is the test being wrong rather than the code.
    hidden = [line for line in star.splitlines()
              if line.strip().startswith("visible:")]
    assert not hidden, (
        "the star is back to being hidden, which takes it out of hit-testing: "
        + "; ".join(h.strip() for h in hidden))

    # The row's own handler stages the graph. The star has to CONSUME the
    # click, not merely also handle it.
    assert "mouse.accepted = true" in star, (
        "the star does not consume its click, so starring a graph also stages "
        "it and replaces what the person was looking at")
    assert "toggleFavouriteGraph" in star

    assert "entry.modelData.hint !== true" in qml, (
        "the catalogue row is drawn for the hint line as well, which has no "
        "entry behind it")
    assert "onGraphShortcutsChanged" in qml, (
        "the library does not rebuild when the shortcut lists change, so the "
        "star appears to do nothing until something else refreshes the list")


def test_the_legend_lists_named_series_only() -> None:
    """An unlabelled series is a piece of a figure, not a thing in the list.

    `drawLegend` drew one row per series whatever the label said, so an empty
    label produced an empty row rather than no row — and two engines already
    relied on the opposite:

    - the ground-track rewrite clears the label on each piece after an
      antimeridian cut and says "one legend entry for the track"; it got one
      entry and several blank ones;
    - the comet's faded trail is six pieces per series, so five columns made a
      legend of thirty-five rows, thirty blank, taller than the plot area it
      was drawn over.

    Also checked: the box is capped to the plot area and the loop stops at the
    bottom of a capped box. A legend that overflows is drawn across the axis
    labels and off the end of the figure.
    """
    source = _backend_source()
    start = source.index("void QtPlotBackend::drawLegend(")
    body = _strip_comments(source[start:source.index("\n}\n", start)])

    assert "if(s.label.isEmpty()) continue;" in body, (
        "the legend lists unlabelled series again, so every helper piece a "
        "rewrite emits gets a blank row")
    assert "if(listed.size()<2) return;" in body, (
        "the two-entry minimum is counting series rather than NAMES, so one "
        "name and nine anonymous pieces still draws a legend")
    assert "f.plotArea.height()-16.0" in body, (
        "the legend box is no longer capped to the plot area")
    # The rows the box can hold are now COUNTED before any are drawn, so the
    # last one can say how many it is hiding rather than the loop simply
    # stopping. The claim is the same: nothing is drawn past the bottom.
    assert "for(double probe=box.top()+4;probe+rowH<=box.bottom()-2;probe+=rowH) ++fits;" in body, (
        "the rows are drawn past the bottom of a capped box")


def test_a_dot_plot_axis_reads_in_dots() -> None:
    """Counting the dots against the axis is the reason to draw one.

    The first version stacked each column from its own slot in steps of 0.06,
    so the y axis said "count" over numbers that were a slot index plus a
    fraction — a column of 212 dots reached y = 13.7 and covered the four
    columns beside it. The height has to BE the count.

    The x axis carries the value being counted, not whatever the x mapping
    held: this engine does not read it, and the inherited label put "time_h"
    under an axis of measurements. The same applied to the probability plot.
    """
    source = _backend_source()
    for anchor, needle, why in (
            ('in.engine==QLatin1String("Dot Plot")', "dots.y.append(double(k)+1.0);",
             "the dot plot's height is not the count, so the axis label is wrong"),
            ('in.engine==QLatin1String("Dot Plot")', "out.xAxis=PlotAxis{",
             "the dot plot inherits an x label describing a column it never reads"),
            ('in.engine==QLatin1String("Probability Plot")', "out.xAxis=PlotAxis{",
             "the probability plot inherits an x label describing a column it "
             "never reads")):
        start = source.index(anchor)
        body = _strip_comments(source[start:source.index("\n    }\n", start)])
        assert needle in body, why


def test_a_beeswarm_stays_inside_its_own_slot() -> None:
    """A swarm in the wrong column is a lie about which group a point is in.

    Placing each point at the nearest free offset is what makes a beeswarm
    asymmetric, and it is the difference from `Swarm`'s symmetric fan. But the
    search has to be bounded: on a column with two hundred points at the same
    height the first version walked out to nearly two whole slots, and group
    one's swarm reached into group two's.

    Both keep every point at its exact value; neither snaps to a row. An
    earlier comment here claimed Swarm moved points off their value — it does
    not, it appends the observation itself. The distinction is the placement
    rule.
    """
    source = _backend_source()
    start = source.index('in.engine==QLatin1String("Beeswarm")')
    body = _strip_comments(source[start:source.index("\n    }\n", start)])

    limit = re.search(r"constexpr double kLimit=([0-9.]+);", body)
    assert limit and float(limit.group(1)) <= 0.5, (
        "the beeswarm's offset is unbounded or wider than half a slot, so a "
        "crowded column spills into the group beside it")
    assert "if(std::abs(offset)>kLimit||step>=kSteps){" in body, (
        "the placement search has no ceiling")
    assert "pts.y.append(value);" in body, (
        "the beeswarm is no longer plotting points at their own value")

    # AND IT HAS TO BE LINEAR. The collision test used to scan every point
    # already placed whose value was within `reach` - and `reach` is a fraction
    # of the SPAN, so the number of points inside it grows with n and the scan
    # was O(n) per candidate offset per point. 24,000 points per column took
    # 38.5 seconds; the same predicate, evaluated directly, takes 144 ms and
    # produces a byte-identical figure.
    #
    # Every offset is a multiple of kStep and consecutive ones are kStep apart,
    # which is more than the kStep*0.9 the old test used - so "clashes" means
    # "at the same offset", and because the values are sorted the only point
    # there that can still be within reach is the last one placed.
    assert "QVector<double> lastAt(kSteps," in body, (
        "the beeswarm no longer remembers one value per offset, so it is back "
        "to scanning everything placed so far - which is quadratic in the row "
        "count and took 38 seconds on a column of 24,000 points")
    assert "if(!used[step]||value-lastAt[step]>reach){" in body, (
        "the collision test is no longer a single lookup")
    assert "placedOffset" not in body, (
        "the old backward scan is back beside the lookup, so the cost is "
        "quadratic again whatever the lookup says")

    start = source.index('if(in.engine==QLatin1String("Swarm")){')
    swarm = _strip_comments(source[start:source.index("\n    }\n", start)])
    assert "pts.y.append(v[i+k]);" in swarm, (
        "Swarm no longer plots the observation itself, which is what the "
        "Beeswarm comment says it does")


def test_the_isosurface_is_built_by_a_method_that_was_verified() -> None:
    """Marching TETRAHEDRA, and the numbers that justify the choice.

    Marching cubes needs a 256-entry triangle table, and one wrong entry
    produces a surface that looks entirely plausible and is wrong in a way
    nobody notices — which is not a thing to write from memory. A tetrahedron
    has four corners, so there are sixteen sign patterns and every one is
    empty, one corner cut off, or two-and-two. That is derivable at the point
    of use.

    It was verified against ``x² + y² + z² − 1 = 0`` before it went near the
    renderer: area converging on 4πr² at second order (−6.11%, −1.44%, −0.37%,
    −0.09% as the lattice doubles) and **zero open edges** at every resolution.

    The open-edge count is the part worth keeping. The first closure check
    hashed vertex coordinates and reported holes that grew with resolution; the
    instrument was wrong, not the mesh. Two tetrahedra cutting the same grid
    edge compute the same point from the same corner values but may walk the
    corners the other way round — lerp(a,b,t) against lerp(b,a,1−t), equal in
    exact arithmetic and not bitwise. Labelling each vertex by the grid edge it
    lies on made identity exact and the holes vanished.

    Two properties are guarded here because a regression in either is silent:
    the quad ordering (four cut points joined in collection order can cross and
    leave a bow-tie), and the cube split (the six tetrahedra must share one
    diagonal, or neighbouring cubes disagree on the face between them and the
    mesh stops being watertight).
    """
    source = _backend_source()

    start = source.index("void marchTetrahedron(")
    body = _strip_comments(source[start:source.index("\n}\n", start)])
    assert "shares(order[k-1],order[k])" in body, (
        "the four-cut quadrilateral is no longer ordered around its rim, so a "
        "crossed quad leaves a bow-tie with a hole in it")
    assert "if(found==3){" in body and "if(found!=4) return;" in body, (
        "the three-cut and four-cut cases are no longer separated")

    start = source.index("QVector<IsoTriangle> isosurfaceOf(")
    walk = _strip_comments(source[start:source.index("\n}\n", start)])
    tets = re.search(r"kTets\[6\]\[4\]=\{([^;]+)\};", walk)
    assert tets, "the cube's tetrahedral split has moved"
    corners = [int(v) for v in re.findall(r"\d+", tets.group(1))]
    assert len(corners) == 24, "the split is no longer six tetrahedra of four"
    # Every tetrahedron must contain both ends of the 0-6 diagonal. That is
    # what makes this particular split tile: neighbouring cubes divided the
    # same way agree on their shared face.
    for t in range(6):
        tet = corners[t * 4:(t + 1) * 4]
        assert 0 in tet and 6 in tet, (
            f"tetrahedron {t} does not share the 0-6 diagonal, so the split no "
            "longer tiles and the surface is a pile of shells rather than a "
            "closed mesh")

    # The verification itself has to stay in the file. A number nobody can find
    # is a number nobody can re-check.
    assert "12.5664" in source and "open edges" in source, (
        "the sphere verification has been removed from the comment, leaving a "
        "256-case-equivalent algorithm with nothing to justify it")


def test_each_volume_engine_reads_the_field_differently() -> None:
    """Six engines, six readings — and a fixture that can tell them apart.

    All six shared one branch and drew the same cloud of faded points. The
    code's own comment called it "an isosurface without the surface", which was
    honest and is exactly the problem: somebody choosing `Isosurface` got a
    point cloud with no way to learn that the backend drew all six alike.

    The fixture mattered as much as the painter. Handed the shared five columns
    these read a binary label as the scalar, and a field that is 0 or 1 has one
    level set — every level gives the same shape or nothing. They were not
    drawing badly; there was nothing there to find.
    """
    source = _backend_source()
    start = source.index("void drawVolumeFamily(")
    body = _strip_comments(source[start:source.index("\n}\n", start)])

    for engine in ("Isocaps", "Isonormals", "Volume Slice", "Contour Slice",
                   "Volume Show"):
        assert f'QLatin1String("{engine}")' in body, (
            f"{engine} is no longer distinguished, so it draws whatever the "
            "fall-through case draws")

    assert "isosurfaceOf(g,iso)" in body, (
        "nothing in the volume family builds a surface any more")
    # By the TYPE being sorted, not by the comparison. `a.depth<b.depth`
    # appears twice in this function - the splat cloud sorts too - so the bare
    # string passed with the facet sort deleted, which is a guard that cannot
    # fail and therefore is not one.
    assert re.search(r"\[\]\(const Facet& a,const Facet& b\)\{ return a\.depth<b\.depth; \}",
                     body), (
        "the isosurface triangles are not depth-sorted, so a face behind "
        "another is drawn over it and the surface reads inside out")

    fixture = _selftest_source()
    assert "QVector<double> volX,volY,volZ,volV;" in fixture, (
        "the scalar-volume fixture is gone, so the volume engines are back to "
        "reconstructing a surface from a binary column")

    # AND ONE FEATURE HAS TO REACH A WALL, or Isocaps has nothing to cap.
    #
    # The caps are where the enclosed region is cut by the walls of the box.
    # With both blobs comfortably inside the domain the engine correctly found
    # no caps and drew the bare isosurface, and the two figures came out 0.07
    # grey levels apart out of 255 - the closest pair left in the gallery once
    # the geographic engines had been separated. The engine was right both
    # times; there was nothing to cut.
    #
    # The ceiling, not a side wall or the floor: both of those produced a real
    # cap that could not be seen, the side wall because it is nearly edge-on in
    # this projection and the floor because the dome standing on it hides its
    # own opening.
    assert "std::hypot(std::hypot(x+0.35,y+0.2),z-0.88)" in fixture, (
        "no feature in the volume fixture reaches a wall of the box, so "
        "Isocaps has nothing to cap and draws the bare isosurface - which is "
        "the same picture as the Isosurface entry beside it")
    blob = fixture[fixture.index("QVector<double> volX"):][:3200]
    assert blob.count("std::exp(") >= 2, (
        "the volume fixture is a single blob; with one feature an isosurface, "
        "its caps and a slice cannot be told apart")


def test_an_implicit_surface_is_a_zero_level_set_in_three_dimensions() -> None:
    """The 3-D half of the same defect as `Implicit Function`, now closable.

    `Implicit Surface` projected z = f(x,y) and came out identical to `Function
    Surface`. The sweep reported the pair every run, and the finding written
    against it said it needed marching cubes over a volume — which was true,
    and was the honest verdict while there was no such routine. There is one
    now, verified against a sphere, so the limitation has stopped being one and
    the finding has been removed with it: a note that no longer describes the
    code is worse than no note.

    It samples f over a lattice and hands four columns to the isosurface
    painter rather than getting a painter of its own. The reconstruction, the
    depth sort and the projection are written once, and this is the same job
    with the field coming from a formula instead of a file.
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Implicit Surface")){')
    body = _strip_comments(source[start:source.index("\n        }\n", start)])

    assert 'QStringLiteral("z")' in body, (
        "the implicit surface no longer compiles its formula in three "
        "variables, so it cannot have a zero level set in three dimensions")
    assert 'derivedAs(in,QStringLiteral("Isosurface"))' in body, (
        "the implicit surface is back to a height-field engine")
    assert '"isoLevel"' in body, (
        "the level is not being pinned, so the surface is drawn at the middle "
        "of the field's range rather than at zero — which is the one value "
        "that makes it the thing the engine is named after")
    assert "has no zero between" in body, (
        "a formula that never changes sign draws an empty cube with no "
        "explanation")

    # And it must be out of the height-field set, or it never reaches the
    # branch above.
    start = source.index("const bool surface=in.engine==QLatin1String(\"Function Surface\")")
    flags = source[start:start + 300]
    assert "Implicit Surface" not in flags, (
        "Implicit Surface is still in the height-field set, so it is drawn as "
        "z = f(x,y) and the new branch is unreachable")

    # The superseded finding must not survive its own subject.
    assert "needs marching cubes over a volume" not in _selftest_source(), (
        "the same-picture findings still carry the limitation that marching "
        "tetrahedra removed")


def test_applying_a_graph_never_fails_silently() -> None:
    """"Apply doesn't always work" was Apply saying nothing when it couldn't.

    `root.canvas` is null in the notebook layout whenever the sheet has not
    finished loading or no cell is current, and every handler that used it
    began `if (!c) return`. So applying a graph, taking a scan recommendation
    and changing the column mapping all did nothing and reported nothing in
    that state: the button depressed, the list highlighted the choice, and the
    figure did not change.

    From outside, that is indistinguishable from a broken button. Returning
    early is still right — there is genuinely nowhere to draw — but the silence
    was not, so every one of those paths now goes through `targetCanvas()`,
    which explains what it could not do and why.
    """
    qml = _qml("workspaces/VisualizeWorkspace.qml")

    assert "function targetCanvas(what)" in qml, (
        "the canvas is being resolved without a place to report that there "
        "isn't one")

    # No HANDLER may read root.canvas straight into an early return again.
    # targetCanvas itself is exempt: it is the one place that is supposed to
    # read it and decide what to say about a null, and the first version of
    # this assertion flagged the helper it exists to require.
    helper = qml.index("function targetCanvas(what)")
    outside = qml[:helper] + qml[qml.index("\n    }", helper):]
    for bad in ("var c = root.canvas\n", "var t = root.canvas\n",
                "var target = root.canvas\n"):
        assert bad not in outside, (
            "a handler reads root.canvas directly and will return silently "
            "when it is null: " + bad.strip())

    # And targetCanvas has to actually say something.
    body = qml[qml.index("function targetCanvas(what)"):]
    body = body[:body.index("\n    }")]
    assert "notify" in body, (
        "targetCanvas returns null without telling anyone, which is the "
        "silence this whole guard is about")
    assert "notebookLayout" in body, (
        "the explanation does not distinguish 'no cell is selected' from 'the "
        "figure is not ready', which are different problems with different "
        "fixes for the person reading it")


def test_the_export_button_offers_the_options_the_backend_has() -> None:
    """Three export functions, eight parameters, and QML called one with none.

    The button said "Export PDF" and wrote a 6 × 4 inch, 600 dpi PDF. An SVG, a
    300 dpi plate, a slide-sized PNG and a figure at a journal's exact column
    width were all already possible in `PlotCanvas` and none was reachable.

    Also guarded: the format list is ASKED of the canvas rather than written
    out here. The raster formats come from plugins that may not have been
    deployed, and a dialog that offers TIFF and then writes nothing is worse
    than one that never offered it.
    """
    qml = _qml("components/ExportDialog.qml")

    for call in ("exportPdf(", "exportSvg(", "exportRaster("):
        assert call in qml, f"the export dialog cannot reach {call}"
    assert "root.canvas.exportFormats()" in qml, (
        "the format list is hard-coded rather than asked of the canvas, so it "
        "can offer a format this build cannot write")
    assert "root.dpi" in qml and "qualitySlider" in qml, (
        "resolution or quality is no longer offered")

    workspace = _qml("workspaces/VisualizeWorkspace.qml")
    assert 'text: "Export"' in workspace, (
        "the button names one format again")
    assert "exportDialog.open()" in workspace
    assert "FullRenderPolicyBox" not in workspace, (
        "the 250px policy combo is back in the toolbar, where it pushed the "
        "export button off the end of the row")

    cmake = (ROOT / "app" / "CMakeLists.txt").read_text(encoding="utf-8")
    assert "qml/components/ExportDialog.qml" in cmake, (
        "the dialog is not in the QML module, so it does not exist at runtime "
        "however correct the file is")


def test_the_zoom_buttons_zoom_where_the_person_is_looking() -> None:
    """Zooming about the centre slides the feature out from under them.

    The wheel has always zoomed about the pointer, through `zoomAt`. The
    buttons could not, because a button has no pointer position — so they used
    the middle of the frame, and every click moved the thing being examined
    further away.

    The remembered position has to survive the pointer LEAVING the figure,
    because pressing the button is how it leaves: it moves off the plot and
    onto the button before the click arrives. Clearing it on hover-leave would
    mean the button never saw anything but the centre.
    """
    source = (ROOT / "native" / "plot2d" / "src" / "PlotCanvas.cpp").read_text(
        encoding="utf-8", errors="ignore")

    start = source.index("void PlotCanvas::zoomBy(")
    body = _strip_comments(source[start:source.index("\n}\n", start)])
    assert "haveLastPointer_" in body and "lastPointerOnPlot_" in body, (
        "zoomBy is back to zooming about the middle of the frame")
    assert "area.center()" in body, (
        "there is no fallback for a pointer that has never been on the figure")

    start = source.index("void PlotCanvas::hoverLeaveEvent(")
    leave = _strip_comments(source[start:source.index("\n}\n", start)])
    assert "haveLastPointer_" not in leave, (
        "the remembered position is cleared when the pointer leaves, so the "
        "zoom buttons — which are reached BY leaving — never see it")


def test_every_style_setter_invalidates_the_accepted_render() -> None:
    """A control that changes nothing on screen looks broken, because it is.

    `paint()` returns early with the accepted full-resolution image whenever
    one matches the current generation. So a setter that changes the spec and
    calls `update()` — and nothing else — repaints a photograph of the previous
    settings. On any dataset large enough to have a second render, which is
    every dataset these settings matter on, the change never appears.

    `setColourMap` was found and fixed this way, and its comment says so: "no
    invalidation of the accepted full-resolution image, which went on being
    drawn in the old map. The control looked broken because it was." Four more
    setters had the identical shape — the colour-vision palette and the three
    figure colours — and the colour-vision one is the setting somebody reaches
    for precisely because they cannot read the current colours.

    Rotating the figure is what made it seem to work: `cameraMoved()` drops
    `showingFull_`, so the live preview returns with the new palette. The
    setting reached the renderer the whole time; the picture was stale.
    """
    source = (ROOT / "native" / "plot2d" / "src" / "PlotCanvas.cpp").read_text(
        encoding="utf-8", errors="ignore")

    for setter in ("setColourVision", "setColourMap", "setGridVisible",
                   "setGridDensity"):
        start = source.index(f"void PlotCanvas::{setter}(")
        body = _strip_comments(source[start:source.index("\n}\n", start)])
        assert "scheduleFullRender();" in body, (
            f"{setter} changes the spec without re-running the full render, "
            "so the figure keeps showing the previous settings")

    # The three figure colours share one macro; the point is that it contains
    # the re-render, not which of them is written out longhand.
    start = source.index("#define GV_FIGURE_COLOUR")
    macro = source[start:source.index("#undef GV_FIGURE_COLOUR", start)]
    for setter in ("setBackgroundColor", "setForegroundColor", "setGridColor"):
        assert setter in macro, (
            f"{setter} has left the shared definition; check it still "
            "invalidates the accepted render")
    assert "scheduleFullRender();" in macro and "showingFull_=false;" in macro, (
        "the figure colours repaint without invalidating the accepted render")


def test_a_style_change_does_not_discard_the_zoom() -> None:
    """`rebuild()` clears the view, and a style change can trigger one.

    The rule it implements is right for a DATA change — a zoom belongs to the
    figure it was made on, and keeping it across a change of dataset or engine
    opens the next figure clipped to a range that means nothing in it. But
    `rebuild()` is also what re-derives the series colours, so choosing a
    colour-vision palette ran it and silently reset a zoom the person had
    dragged.

    The comment about `limit*_` further down the same file already noticed that
    "a style change - a colour map, a grid toggle - can trigger one"; this is
    the other half of the same observation.
    """
    source = (ROOT / "native" / "plot2d" / "src" / "PlotCanvas.cpp").read_text(
        encoding="utf-8", errors="ignore")
    start = source.index("void PlotCanvas::rebuild(")
    body = _strip_comments(source[start:start + 2000])

    assert "if(hasView_&&!keepViewOnRebuild_){" in body, (
        "rebuild() discards the view unconditionally again, so changing the "
        "colour-vision palette resets a dragged zoom")
    assert "keepViewOnRebuild_=false;" in body, (
        "the flag is not cleared inside rebuild(), so it can leak into the "
        "next DATA change and keep a zoom that belongs to a different figure")

    start = source.index("void PlotCanvas::setColourVision(")
    setter = _strip_comments(source[start:source.index("\n}\n", start)])
    assert "keepViewOnRebuild_=true;" in setter, (
        "the colour-vision setter does not protect the view, and it is the one "
        "style setter that genuinely needs the rebuild")


def test_the_colour_panel_says_which_setting_each_line_is_about() -> None:
    """Two true sentences about different things, neither saying which.

    The panel states the INTERFACE theme's colour-vision status and, directly
    below it, the GRAPH's series palette. The file's own opening comment says
    conflating them "is exactly the mistake to avoid" — and the layout invited
    it anyway: "Standard theme — no colour vision adjustment" sat immediately
    above a box reading "Deuteranopia (green-blind)", and that was reported as
    the setting not working.

    Nothing was wrong with either sentence. The first was about the panels and
    the second about the figure, and neither said so.
    """
    qml = _qml("components/ColourVisionBar.qml")
    assert "Interface theme —" in qml, (
        "the theme line does not say it is about the interface, so it reads as "
        "a statement about the graph and contradicts the control below it")
    assert "set separately" in qml, (
        "nothing tells the reader the graph colours are a different setting")

    # And the series combo has to say what it does NOT affect. A 3-D surface
    # is coloured by the field map, so changing the series palette leaves it
    # alone — which is correct, and looks exactly like the control failing.
    assert "field" in qml and "3-D surface" in qml, (
        "the series-colour control does not say that a heat map, contour or "
        "surface is coloured by the field map instead, which is the case where "
        "changing it correctly does nothing visible")


def test_a_phase_portrait_counts_its_fixed_points_once() -> None:
    """Eight markers for three fixed points is a wrong answer, not clutter.

    A zero sitting on or near a grid line satisfies "both components change
    sign" in every cell that touches it, so one critical point is found two,
    three or four times. Drawn straight from the loop, the sweep's Duffing
    field produced EIGHT markers with eight labels piled on each other — and a
    reader counting critical points off the figure would have got eight, which
    is the one thing a phase portrait must not get wrong.

    Checked by arithmetic before it was written, on the painter's own grid:

        u = y,  v = x − x³ − 0.2y
        exact:  saddle at (0,0);  stable spirals at (±1, 0)
        found:  8 detections, correctly classified, 3 distinct points

    The merge radius is scaled to the CELL because that is what produces the
    duplicates. A fraction of the plot area was the first guess: on a 13-cell
    grid the copies sat 40 px apart and the radius came out 17, so all eight
    survived. 1.6 cells is 64 px there, against 140 px between the field's
    genuinely distinct points.
    """
    source = _backend_source()
    start = source.index("if(portrait){")
    body = _strip_comments(source[start:source.index("\n    }else{", start)])

    assert "QVector<Critical> criticals;" in body, (
        "fixed points are drawn straight from the detection loop again, so "
        "one point is marked once per cell that touches it")
    assert "if(duplicate) continue;" in body, (
        "the de-duplication is gone")

    radius = re.search(r"nearby=qMax\(6\.0,([0-9.]+)\*qMax\(gridPitchX,gridPitchY\)\)", body)
    assert radius, (
        "the merge radius is no longer scaled to the cell, which is the only "
        "quantity that tracks how far apart the duplicates actually are")
    assert 1.0 <= float(radius.group(1)) <= 3.0, (
        "the merge radius is outside one to three cells: below that the "
        "duplicates survive, above it two genuinely distinct points merge")

    # And the classification itself, which is the content of the figure.
    for kind in ("saddle", "centre", "stable spiral", "stable node"):
        assert f'"{kind}"' in body, f"the {kind} branch has gone"
    assert "trace*trace<4.0*det" in body, (
        "the spiral/node split is no longer the trace-determinant test")

    fixture = _selftest_source()
    assert "flowV.append(x-x*x*x-0.2*y);" in fixture, (
        "the 2-D field fixture is no longer the Duffing system, so the "
        "classification may not be exercised: the shared columns give u a "
        "p-value and v a 0/1 label, neither of which ever changes sign, and a "
        "field with no zeros makes Phase Portrait identical to Stream Field")


def test_a_colour_vision_mode_reaches_the_field_colour_map() -> None:
    """A surface has no series, so a series-only setting cannot reach it.

    This is the third round of the same report and the first one about the
    right thing. The first two were repaint bugs — `setColourVision` changed
    the spec and `paint()` went on returning the accepted full-resolution
    image, so the figure stayed in the old palette. Both were fixed, and the
    report came back anyway: a 3-D surface, Monochrome selected, still in full
    colour.

    Nothing was broken by then. A surface, a heat map, a contour and a vector
    field have no `PlotSeries` at all — the colour MAP is the data — so a
    setting that only assigned series colours had no path to the picture on
    exactly the engines where a colour-blind reader needs it most.

    So the mode now resolves the colour map too, and three things have to hold
    for that to be honest rather than merely different:

    - the chosen map is KEPT. Only the painted one changes, so turning the mode
      off restores the choice instead of leaving the substitute behind.
    - the substitute keeps the map's ROLE. Swapping a diverging map for a
      sequential one would throw away the centre the diverging map exists to
      show, which changes what the figure claims about the data.
    - saving a figure writes the chosen map, not the painted one.
    """
    source = (ROOT / "native" / "plot2d" / "src" / "PlotCanvas.cpp").read_text(
        encoding="utf-8", errors="ignore")
    body = _strip_comments(source)

    assert "void PlotCanvas::applyColourVisionToMap()" in body, (
        "the colour-vision mode no longer resolves the colour map, so it "
        "cannot reach a surface, a heat map, a contour or a vector field")

    # Called from every setter that can change either input.
    for setter in ("setColourVision", "setColourMap", "applyFigureState"):
        start = body.index(f"PlotCanvas::{setter}(")
        end = body.index("\n}", start)
        assert "applyColourVisionToMap()" in body[start:end], (
            f"{setter} no longer resolves the painted colour map, so the two "
            "inputs can disagree with what is on screen")

    assert "chosenColourMap_" in body, (
        "the chosen map is no longer kept apart from the painted one, so a "
        "substitution silently becomes the person's selection")
    assert 'QStringLiteral("colourMap"),chosenColourMap_' in body, (
        "saving a figure writes the PAINTED colour map, which bakes a "
        "colour-vision substitution into the file permanently")

    header = (ROOT / "native" / "plot2d" / "include" / "ColourMapSafety.h").read_text(
        encoding="utf-8", errors="ignore")
    assert "GENERATED by tools/measure_colourmap_cvd.py" in header, (
        "the safety table is no longer generated, so it is a list of maps "
        "somebody classified by eye — which is the mistake ColourVision.h "
        "exists to say was made once already")

    # Role is preserved by construction: every branch of the substitution
    # switches on roleOf and answers within that role.
    for role in ("Sequential", "Diverging", "Cyclic"):
        assert f"case MapRole::{role}:" in header, (
            f"the {role} branch of the substitution has gone, so a map of that "
            "kind is replaced by one that reads differently")


def test_monochrome_is_a_request_for_no_colour_not_a_legibility_test() -> None:
    """Plasma survives a greyscale print. That is not a reason to keep it.

    Measured through a Rec.709 luma reduction, Plasma's worst far-apart pair is
    16.9 dE76 — comfortably over the margin, so "is this map safe in
    monochrome" answers yes, and the first version of the table set the
    monochrome bit from that measurement. A person who had chosen Monochrome
    then kept a full-colour surface, which is the exact complaint the whole
    change exists to answer.

    Legibility and greyness are different questions. The monochrome bit is set
    only for the maps that are achromatic by construction, so the bit for
    Plasma must be CLEAR even though Plasma passes the measurement.
    """
    header = (ROOT / "native" / "plot2d" / "include" / "ColourMapSafety.h").read_text(
        encoding="utf-8", errors="ignore")

    entries = dict(
        (name, int(bits, 16))
        for name, bits in re.findall(
            r'\{"([^"]+)",MapRole::\w+,(0x[0-9a-f]{2})\}', header))
    assert entries, "the safety table has no entries at all"

    mono = 1 << 3
    for colourful in ("Plasma", "Viridis", "Cividis", "Inferno", "Magma"):
        assert not entries[colourful] & mono, (
            f"{colourful} is marked right for monochrome. It reads perfectly "
            "well in greyscale and it is not grey, so choosing Monochrome "
            "would leave the figure in full colour — the reported bug")
    for grey in ("Gray", "Greys", "Binary", "Gist Gray", "Gist Yarg"):
        assert entries[grey] & mono, (
            f"{grey} is achromatic and is not marked right for monochrome, so "
            "choosing Monochrome would needlessly replace it")

    # And an unnamed map must be judged as Viridis, not waved through.
    assert 'name.isEmpty()?QStringLiteral("Viridis")' in header, (
        "an empty colour map is no longer resolved to Viridis before the "
        "lookup, so the DEFAULT map is judged as an unknown one and passes "
        "every vision by omission — the default figure stays in colour after "
        "a request for monochrome")


def test_a_figure_note_is_reserved_before_it_is_drawn() -> None:
    """A note the layout did not make room for lands on the axis label.

    The figure note carries what the picture CANNOT show — a colour map
    substituted for a colour-vision mode that can no longer mark a diverging
    centre, for instance. It is exported with the figure on purpose: the person
    reading the PDF is not the person who chose the setting, and a limitation
    recorded only in the catalogue entry never reaches them.

    Layout and the painter must agree about its size, so both go through one
    function.
    """
    source = _backend_source()
    body = _strip_comments(source)

    assert "figureNoteRect(" in body, "the figure note is gone"
    assert body.count("figureNoteRect(") >= 3, (
        "the note is no longer measured through the shared helper by both the "
        "layout and the painter, so one of them will disagree with the other "
        "about how much room it takes")

    frame = body[body.index("QtPlotBackend::Frame QtPlotBackend::computeFrame("):]
    frame = frame[:frame.index("\n}")]
    assert "figureNoteRect(" in frame, (
        "computeFrame no longer reserves room for the note, so it is drawn "
        "over the x axis label")
    # The plot area's height is what actually has to shrink. Looking for the
    # name `bottomMargin` anywhere in the function was the first version of
    # this, and it did not work: the name appears both where it is declared and
    # where it is used, so renaming the declaration left the use behind and the
    # guard passed on code that does not compile. The height expression itself
    # is the thing to read.
    height = re.search(r"qMax\(10\.0,target\.height\(\)-kMarginTop-(\w+)\)", frame)
    assert height, (
        "the plot area's height is no longer target.height() less the two "
        "margins, so this guard can no longer see what it subtracts")
    assert height.group(1) != "kMarginBottom", (
        "the plot area is back to a fixed bottom margin, so a figure note "
        "either overlaps the x axis label or falls off the bottom of an export")
    assert re.search(rf"{height.group(1)}=kMarginBottom\+\(", frame), (
        f"{height.group(1)} is not kMarginBottom plus the note's own height, "
        "so the room reserved is not the room the note takes")

    spec = (ROOT / "native" / "plot2d" / "include" / "PlotSpec.h").read_text(
        encoding="utf-8", errors="ignore")
    assert "QString figureNote;" in spec, "PlotSpec has no figure note field"


def test_a_custom_figure_background_keeps_its_text_readable() -> None:
    """A background picker that leaves unreadable axes has not finished.

    The ink is derived from the background rather than chosen, because someone
    picking a ground is not undertaking to find a matching ink for it.

    The first attempt split on luminance at 0.18 and returned one of two fixed
    inks. Checked against the swatches it offers, the mid grey #6e6e6e came out
    at 4.03:1 — under the 4.5 ordinary text wants — and moving the threshold
    did not help, because at that ground BOTH tuned inks are poor and the best
    any fixed pair can do there is 4.03. The threshold was not the problem;
    having only two inks was. So the better ink is chosen by contrast, and pure
    black or white steps in when neither tuned ink clears the bar.

    That last part is what makes a FREE picker safe: the ground is whatever
    somebody chose, and black and white are the extremes, so the fallback is
    the best contrast that exists for it.
    """
    source = (ROOT / "app" / "src" / "AppController.cpp").read_text(
        encoding="utf-8", errors="ignore")
    body = _strip_comments(source)

    assert "static double contrastRatio(" in body, (
        "the ink is no longer chosen by contrast ratio, so a mid-grey "
        "background gets text nobody can read")
    assert "relativeLuminance" in body, (
        "contrast is measured from something other than relative luminance, "
        "which puts black text on navy: the eye is not equally sensitive to "
        "the three primaries and a lightness figure does not say so")

    foreground = body[body.index("QColor AppController::figureForeground()"):]
    foreground = foreground[:foreground.index("\n}")]
    assert "4.5" in foreground, (
        "the contrast floor has gone, so the tuned ink is used even where it "
        "fails")
    assert "Qt::white" in foreground and "Qt::black" in foreground, (
        "the pure black/white fallback has gone, so a background the picker "
        "can produce but the swatches do not offer can still be unreadable")

    # THE INKS AND THE THRESHOLD ARE READ OUT OF THE SOURCE, not copied here.
    # Copied, they made this guard unfalsifiable in the way that matters: a
    # reversion that weakened the light ink to #9aa4ac left the test passing,
    # because the test was sweeping its own private copy of a pair the
    # application no longer used. A mirror of a constant is not a check on it.
    # The weights too, for the same reason. These are not a tuning knob - they
    # are the WCAG coefficients, and the eye's unequal sensitivity to the three
    # primaries is the whole reason contrast is measured from luminance rather
    # than from lightness. Copied into this file they were unfalsifiable: a
    # reversion to 0.3333 each left the test green.
    weights = re.findall(r"0\.(?:2126|7152|0722)", body)
    assert {"0.2126", "0.7152", "0.0722"} <= set(weights), (
        "relative luminance is no longer weighted by the WCAG coefficients, so "
        "contrast is being measured against a colour space the eye does not "
        "have - which is what puts black text on navy")

    tuned = re.findall(r"QColor (?:light|dark)Ink\(0x([0-9a-f]{2}),0x([0-9a-f]{2}),0x([0-9a-f]{2})\)",
                       foreground)
    assert len(tuned) == 2, (
        "the two tuned inks can no longer be read out of figureForeground, so "
        "the sweep below would be checking a pair the application does not use")
    LIGHT_INK, DARK_INK = ("#%s%s%s" % t for t in tuned)
    floor = float(re.search(r"qMax\(onLight,onDark\)>=([0-9.]+)", foreground).group(1))

    def channel(raw: float) -> float:
        v = raw / 255.0
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4

    def luminance(hex_colour: str) -> float:
        r, g, b = (int(hex_colour[i:i + 2], 16) for i in (1, 3, 5))
        return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b)

    def ratio(a: str, b: str) -> float:
        la, lb = luminance(a), luminance(b)
        return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)

    def ink(bg: str) -> str:
        on_light, on_dark = ratio(bg, LIGHT_INK), ratio(bg, DARK_INK)
        if max(on_light, on_dark) >= floor:
            return LIGHT_INK if on_light > on_dark else DARK_INK
        return "#ffffff" if ratio(bg, "#ffffff") > ratio(bg, "#000000") else "#000000"

    # THE OFFERED SWATCHES USED TO BE THE THING CHECKED. There is no longer a
    # list of offered grounds: the grid of twenty-four was removed from the
    # panel because it held two rows of sidebar height for colours the picker
    # and the preset drop-down already reach. So the property that matters is
    # no longer "the colours we chose are safe" but the stronger one the
    # derivation was written for - **any** background someone picks gets ink
    # that clears 4.5:1.
    #
    # Swept over the RGB cube rather than sampled at a list, because a list is
    # what let the weaker claim look sufficient for so long.
    worst, worst_at = 99.0, None
    fallback_worst, fallback_at = 99.0, None
    for r in range(0, 256, 5):
        for g in range(0, 256, 5):
            for b in range(0, 256, 5):
                bg = "#%02x%02x%02x" % (r, g, b)
                worst_here = ratio(bg, ink(bg))
                if worst_here < worst:
                    worst, worst_at = worst_here, bg
                # The fallback branch on its own: the grounds where NEITHER
                # tuned ink clears the threshold and pure black or white has to
                # carry it.
                if max(ratio(bg, LIGHT_INK), ratio(bg, DARK_INK)) < floor:
                    f = max(ratio(bg, "#ffffff"), ratio(bg, "#000000"))
                    if f < fallback_worst:
                        fallback_worst, fallback_at = f, bg

    assert worst >= 4.5, (
        f"a background of {worst_at} gets ink at only {worst:.3f}:1, under the "
        "4.5 ordinary text needs - and the background is whatever the picker "
        "was pointed at, so there is no list of safe colours to fall back on")
    assert 4.49 <= worst < 4.51, (
        f"the worst case over the whole cube is now {worst:.3f}:1 at "
        f"{worst_at}, not the 4.500 the source states. Either the inks or the "
        "threshold moved; the comment quotes a measured number and has to be "
        "re-measured with it")
    # WHAT THE TUNED INKS ACTUALLY BUY, which is not the floor. The floor is
    # guaranteed by the black/white fallback whatever the tuned pair is, so
    # asserting it cannot detect a weakened ink - two reversions proved exactly
    # that, passing while the light ink was dulled to #9aa4ac. What the pair is
    # for is keeping the harsh fallback rare: pure black on a mid tint is legal
    # and ugly. As shipped the pair covers 83% of the cube on its own; dulling
    # the light ink drops that to 63%, the dark ink to 40%.
    covered = 0
    total = 0
    for r in range(0, 256, 5):
        for g in range(0, 256, 5):
            for b in range(0, 256, 5):
                bg = "#%02x%02x%02x" % (r, g, b)
                total += 1
                if max(ratio(bg, LIGHT_INK), ratio(bg, DARK_INK)) >= floor:
                    covered += 1
    assert covered / total >= 0.80, (
        f"the two tuned inks now carry only {100.0 * covered / total:.0f}% of "
        "the colour cube between them, against 83% as shipped - so the rest of "
        "it falls to pure black or white, which clears the bar and looks it. "
        "One of the inks has been dulled toward the middle")

    assert 4.57 <= fallback_worst < 4.60, (
        f"the fallback branch bottoms out at {fallback_worst:.3f}:1 at "
        f"{fallback_at}, not the 4.583 the source states. These are two "
        "different numbers and an earlier version of that comment quoted the "
        "second as though it were the first - which is why both are asserted")

    controller_h = (ROOT / "app" / "src" / "AppController.h").read_text(
        encoding="utf-8", errors="ignore")
    picker = (ROOT / "app" / "qml" / "components"
              / "FigureBackgroundPicker.qml").read_text(
        encoding="utf-8", errors="ignore")
    assert "figureBackgroundSwatches" not in controller_h, (
        "the swatch list is back on the controller with nothing in the QML "
        "reading it, which is an unreachable property")
    assert "figureBackgroundSwatches" not in picker, (
        "the swatch grid is back in the background panel - it was removed to "
        "give the sidebar back two rows of height")
    assert 'text: "Pick\u2026"' in picker or "Pick" in picker, (
        "the full colour picker has gone from the background panel, which was "
        "the route the swatch grid was removed in favour of")


def test_the_colour_vision_strip_is_optional_and_off_by_default() -> None:
    """It held permanent sidebar space for a setting most people never open.

    The sidebar is 400 px and the graph library in it holds 2,116 entries. A
    panel that is always there costs the library rows in every session. So the
    setting lives in the View menu, which costs nothing when it is not wanted,
    and the strip is there for anyone who does change it often — off until
    asked for, and remembered once asked.

    The figure's background, grid and field colour map did NOT move: those are
    settings nearly every figure gets adjusted.
    """
    header = (ROOT / "app" / "src" / "AppController.h").read_text(
        encoding="utf-8", errors="ignore")
    assert "bool colourVisionToolbar_=false;" in header, (
        "the colour-vision strip is on by default again, which is the sidebar "
        "panel back in a different place")

    sidebar = _qml("components/ControlSidebar.qml")
    assert "ColourVisionBar" not in sidebar, (
        "the colour-vision panel is back in the sidebar")
    assert "FigureAppearanceBar" in sidebar, (
        "the figure's background, grid and field colour map have left the "
        "sidebar too — those were meant to stay")

    menu = _qml("components/MainMenuBar.qml")
    assert "plotColourVisionNames" in menu, (
        "the colour-vision modes are not in the menu bar, so with the strip "
        "off by default there is no way to reach the setting at all")
    assert "colourVisionToolbarVisible" in menu, (
        "the menu no longer offers the strip, so once hidden it cannot be "
        "brought back")

    workspace = _qml("workspaces/VisualizeWorkspace.qml")
    assert "visible: root.app.colourVisionToolbarVisible" in workspace, (
        "the strip is no longer bound to its setting, so it is either always "
        "there or never")

    appearance = _qml("components/FigureAppearanceBar.qml")
    for kept in ("ColourMapSelector", "FigureStyleBar"):
        assert kept in appearance, (
            f"{kept} is no longer in the sidebar panel; the field colour map, "
            "the background and the grid were all meant to stay there")


def test_the_colour_map_box_is_the_width_of_its_longest_name() -> None:
    """A fixed 190 is padding on a toolbar where width is the scarce thing.

    The plot toolbar holds the cursor read-out, the zoom buttons, two unit
    selectors, this control and the export button; the comment beside it in
    VisualizeWorkspace records that an over-wide control there once pushed the
    export button off the end of the row entirely.

    The width is MEASURED rather than typed in. A number written here would be
    right for one font and wrong on the next theme, and the theme is a thing
    the person changes.
    """
    source = _qml("components/ColourMapSelector.qml")

    assert "FontMetrics" in source and "advanceWidth" in source, (
        "the colour-map box no longer measures its widest name, so its width "
        "is a number somebody typed for one font")
    assert not re.search(r"implicitWidth:\s*\d+\s*$", source, re.M), (
        "the colour-map box is back to a fixed implicit width")
    assert "root.widestName" in source, (
        "the implicit width no longer comes from the widest name")

    # The indicator must not be measured: it is anchored to this control, so
    # reading its width from the control's own width is a binding loop.
    width_line = source[source.index("implicitWidth:"):]
    width_line = width_line[:width_line.index("\n")]
    assert "indicator" not in width_line, (
        "the implicit width reads the indicator's width, which is a binding "
        "loop: the indicator is laid out from the width being computed")

    appearance = _qml("components/FigureAppearanceBar.qml")
    selector = appearance[appearance.index("ColourMapSelector {"):]
    selector = selector[:selector.index("\n        }")]
    assert "Layout.fillWidth: true" not in selector, (
        "the sidebar stretches the colour-map box across the panel again, "
        "which undoes the measured width")


def test_a_specialised_palette_says_which_vision_it_fails() -> None:
    """The caveat was a guess. Measured, it is true of two palettes of three.

    ColourVision.h recorded one number per palette — its score for the
    deficiency it was built for — and the outstanding-work note read that
    silence as a conclusion: "a palette specialised for one deficiency is unsafe
    for another". Simulated through all three, the Protanopia and Deuteranopia
    palettes each collapse a pair for a tritanope (6.7 and 7.9 dE76, against a
    margin of about 10), and the Tritanopia palette is fine for all three.

    It matters because someone selecting a specialised palette is usually being
    considerate about a figure other people will read, and a figure in a paper
    is read by people with all three. Saying nothing turns that considerate
    choice into a figure a different reader cannot use.
    """
    header = (ROOT / "native" / "plot2d" / "include" / "ColourVision.h").read_text(
        encoding="utf-8", errors="ignore")

    assert "crossVisionRisk" in header, (
        "the cross-deficiency risk is no longer recorded, so a palette that "
        "fails a vision it was not built for says nothing about it")
    # The measured table, not a single number per palette.
    for row in ("Standard", "Protanopia", "Deuteranopia", "Tritanopia"):
        assert re.search(rf"//\s+{row}\s+[\d.]+\s+[\d.]+\s+[\d.]+\s+[\d.]+", header), (
            f"the {row} palette no longer carries all four measurements, so "
            "the cross-deficiency claim is back to being an assertion")

    risk = header[header.index("inline QString crossVisionRisk("):]
    risk = risk[:risk.index("\n}")]
    for failing in ("Protanopia", "Deuteranopia"):
        assert f"case ColourVision::{failing}:" in risk, (
            f"the {failing} palette is no longer flagged, but it measures under "
            "the margin for a tritanope")
    assert "Tritanopia" not in risk, (
        "the Tritanopia palette is flagged as risky. It measures 16.2 for a "
        "protanope and 17.9 for a deuteranope, both clear of the margin, so "
        "the warning would be false")

    source = (ROOT / "app" / "src" / "AppController.cpp").read_text(
        encoding="utf-8", errors="ignore")
    summary = source[source.index("QString AppController::plotColourVisionSummary()"):]
    summary = summary[:summary.index("\n}")]
    assert "crossVisionRisk" in summary, (
        "the summary no longer warns when the chosen palette fails another "
        "deficiency")


def test_the_default_implicit_surface_has_a_box_that_contains_it() -> None:
    """Half a default — a formula with no domain to show it in — is not one.

    The sampling box for a formula engine is the range of the FIRST MAPPED
    COLUMN, applied to all three of x, y and z. For a formula someone typed
    over their own measurement that is deliberate and useful. For the default
    it is a region of space chosen by an unrelated column: the sweep fixture's
    first column runs 2.11 to 5.24, so the default unit sphere was sampled in
    the cube [2.11, 5.24]³, which it never reaches.

    Every part of the machinery then behaved correctly and the result was still
    wrong. The engine reported that the formula has no zero in range; the
    earlier fix stopped it drawing a surface underneath that sentence; and the
    sweep failed the run for an engine that draws nothing. Three correct
    behaviours, one useless default.

    So the default formula brings its own box. Only this engine needs it: a
    zero LEVEL SET depends on where the box is, while sin(x)*cos(y) crosses
    zero in almost any box, so the other function engines are left alone —
    which is why this asserts the box is conditional on the default rather than
    unconditional.
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Implicit Surface")){')
    body = _strip_comments(source[start:source.index("if(surface||contour){", start)])

    assert "const bool defaulted=formula.isEmpty();" in body, (
        "the engine no longer distinguishes a typed formula from the default, "
        "so it cannot give the default a box of its own")
    assert re.search(r"if\(defaulted\)\{\s*boxLo=-2\.0;\s*boxHi=2\.0;\s*\}", body), (
        "the default formula no longer brings its own box, so the unit sphere "
        "is sampled wherever the first mapped column happens to live and the "
        "default draws nothing")

    # And the sampling must USE it — a box computed and then ignored is the
    # same bug with an extra variable.
    assert body.count("boxLo+(boxHi-boxLo)") == 3, (
        "the sampling loop no longer uses the box on all three axes")
    assert "lo+(hi-lo)*double(i)/double(kSide-1)" not in body, (
        "the sampling loop is back on the data-derived range")
    # The message has to quote the range actually searched, or it reports a
    # range the engine did not look in.
    assert ".arg(boxLo,0,'g',3).arg(boxHi,0,'g',3)" in body, (
        "the 'no zero between' message quotes a different range from the one "
        "that was searched")


def test_an_empty_figure_uses_the_preparers_own_reason() -> None:
    """A wrong explanation is worse than a vague one — it is actionable.

    When prepareSpec empties a spec it usually knows exactly why and writes it
    into the title. The generic fallback answers every such case with "its own
    guards rejected the data - most often too few rows", which for an implicit
    surface whose formula has no zero is simply untrue, and sends the reader
    off to examine rows that are fine.
    """
    source = _backend_source()
    start = source.index("QString QtPlotBackend::explainEmpty(")
    body = _strip_comments(source[start:source.index("\n}\n", start)])

    reason = body.index("prepared.title!=chosen.title")
    generic = body.index("produced no points from the mapped columns")
    assert reason < generic, (
        "the generic 'guards rejected the data' sentence is reached before the "
        "preparer's own explanation, so the specific reason is never used")
    assert "return prepared.title;" in body, (
        "the preparer's explanation is no longer returned")


def _invokables_not_public(header_text: str) -> list:
    """Every Q_INVOKABLE that C++ access rules leave non-public.

    A `class` starts private, so anything declared between the opening brace and
    the first `public:` is private whatever it looks like.
    """
    text = _strip_comments(header_text)
    access = None            # None until the class body opens
    offenders = []
    for line in text.splitlines():
        stripped = line.strip()
        if re.match(r"^class\s+\w+", stripped):
            access = "private"          # a class defaults to private
            continue
        if access is None:
            continue
        label = re.match(r"^(public|protected|private)(\s+slots)?\s*:", stripped)
        if label:
            access = label.group(1)
            continue
        if "Q_INVOKABLE" in stripped and access != "public":
            name = re.search(r"(\w+)\s*\(", stripped)
            offenders.append((name.group(1) if name else stripped, access))
    return offenders


def test_every_invokable_is_reachable_from_qml() -> None:
    """QML will not call a private method, and says so in a way nobody reads.

    `PlotCanvas` is a `class`, so the three hundred lines of interface
    declarations between `Q_OBJECT` and the first `public:` defaulted to
    private. Q_PROPERTY does not care — moc records properties with no access
    at all, and every property worked throughout. Q_INVOKABLE does: moc records
    the C++ access, QML's lookup skips anything not public, and the call comes
    back as "Property 'x' of object PlotCanvas is not a function".

    Thirteen methods were declared there and all thirteen are called from QML —
    setAxisLimits, clearAxisLimits, clearAllLimits, setColourLimits,
    clearColourLimits, setColourLevels, setCustomColour, addCustomColour,
    removeCustomColour, clearCustomColours, seedCustomColours,
    suggestedColourCount, expressionFunctions. The axis-limit controls, the
    colour-scale limits, the banding and the whole custom-colours panel were
    dead at runtime.

    Only one was ever noticed, because it is the only one called from a binding
    that evaluates on load. The other twelve are in click handlers, so they
    failed silently: the button did nothing and a TypeError went to a log
    nobody was reading. That is the failure mode this guard exists for — the
    compiler cannot see it, and neither can anyone using the program.
    """
    for name in ("native/plot2d/include/PlotCanvas.h",
                 "app/src/AppController.h"):
        header = (ROOT / name).read_text(encoding="utf-8", errors="ignore")
        offenders = _invokables_not_public(header)
        assert not offenders, (
            f"{name} declares Q_INVOKABLE methods that are not public, so QML "
            f"cannot call them and the controls that do will fail silently: "
            f"{offenders}")


def test_the_invokable_access_guard_can_actually_fail() -> None:
    """The guard above reads C++ access rules, so check it reads them right.

    A guard that cannot fail is worse than none: it reports the thing it
    watches as healthy for ever. This feeds it the shape of the bug it is meant
    to catch and the shape of the fix, and requires it to tell them apart.
    """
    broken = """
class Thing : public QObject {
    Q_OBJECT
    Q_PROPERTY(int a READ a)
    Q_INVOKABLE void dead();
public:
    Q_INVOKABLE void live();
};
"""
    fixed = """
class Thing : public QObject {
    Q_OBJECT
public:
    Q_PROPERTY(int a READ a)
    Q_INVOKABLE void live();
    Q_INVOKABLE void alsoLive();
};
"""
    hidden_behind_private = """
class Thing : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE void live();
private:
    Q_INVOKABLE void dead();
};
"""
    assert [n for n, _ in _invokables_not_public(broken)] == ["dead"]
    assert _invokables_not_public(fixed) == []
    assert [n for n, _ in _invokables_not_public(hidden_behind_private)] == ["dead"]


def test_colour_vision_gives_the_series_a_second_channel() -> None:
    """Eight colours distinct in CIELAB can still be eight lines you cannot tell apart.

    Dash patterns used to be Monochrome-only, on the reasoning that a dichromat
    still receives hue and so needs no help beyond a tuned palette. Rendered and
    simulated, that reasoning does not survive a real figure: six series of the
    Standard palette put through the protanope transform leave series 2 and
    series 5 as two near-identical mustards, and the Protanopia palette — the
    one tuned for that reader — still hands out three blues. A 2-pixel stroke
    carries far less colour signal than the patch the measurement was made on.

    So every mode except Standard now varies the dash as well as the hue. A dash
    survives each deficiency, a greyscale print and a photocopy.

    Standard keeps no dashes on purpose: it is the default and it is safe for
    all three dichromacies, so dashing there would be the program deciding what
    everyone's figures look like.
    """
    header = (ROOT / "native" / "plot2d" / "include" / "ColourVision.h").read_text(
        encoding="utf-8", errors="ignore")
    body = _strip_comments(header)

    dashes = body[body.index("inline QVector<QVector<qreal>> seriesDashPatterns("):]
    dashes = dashes[:dashes.index("\n}")]
    assert "mode==ColourVision::Standard" in dashes, (
        "dash patterns are gated on something other than Standard — if that is "
        "Monochrome again, colour is once more the only thing telling two "
        "lines apart for a dichromat")
    assert "mode!=ColourVision::Monochrome" not in dashes, (
        "dash patterns are Monochrome-only again")
    # Enough distinct patterns to matter, and the first one solid so an ordinary
    # single-series figure is not gratuitously dashed.
    patterns = re.findall(r"\{[\d., ]*\}", dashes)
    assert len(patterns) >= 6, (
        f"only {len(patterns)} dash patterns, which runs out before the palette "
        "does")
    assert patterns[0].strip() == "{}", (
        "the first series is no longer solid, so every one-series figure in a "
        "colour-vision mode comes out dashed for no reason")


def test_the_colour_vision_preview_never_reaches_an_export() -> None:
    """A PDF drawn through a dichromat transform is not a safe figure.

    The preview answers the complaint that the setting appears to do nothing:
    the person choosing it has normal colour vision, so they cannot see what it
    bought them. Simulating the finished pixels shows them.

    It must stay a VIEW. An export drawn through the transform would be a figure
    nobody can read properly — the opposite of the thing being asked for — so
    the simulation may appear in paint() and nowhere near the export paths or
    the full-resolution render that feeds them.
    """
    source = (ROOT / "native" / "plot2d" / "src" / "PlotCanvas.cpp").read_text(
        encoding="utf-8", errors="ignore")
    body = _strip_comments(source)

    assert "simulateInPlace(" in body, "the colour-vision simulation is gone"
    assert "bool PlotCanvas::simulatingColourVision()" in body, (
        "nothing reports whether a simulation is being shown, so the interface "
        "cannot mark the figure as one")

    # Applied on both paint paths: the live preview AND the accepted full
    # render. Missing the second is the exact shape of bug this setting has
    # already been bitten by twice — it would appear to do nothing on any
    # dataset big enough to have a full render.
    paint = body[body.index("void PlotCanvas::paint("):]
    paint = paint[:paint.index("\n}")]
    assert "paintSimulated" in paint, (
        "the accepted full-resolution image is not passed through the "
        "simulation, so turning the preview on does nothing on exactly the "
        "datasets that have one")

    for path in ("exportPdf", "exportRaster", "exportSvg", "startFullRender"):
        if f"PlotCanvas::{path}(" not in body:
            continue
        fn = body[body.index(f"PlotCanvas::{path}("):]
        fn = fn[:fn.index("\n}")]
        assert "simulateInPlace" not in fn and "paintSimulated" not in fn, (
            f"{path} draws through the colour-vision simulation. An exported "
            "figure must be the real one — a PDF put through a dichromat "
            "transform is unreadable, not accessible")


def test_each_axis_can_be_given_its_own_grid_count() -> None:
    """One number for both axes is a fine default ratio and a poor rule.

    `gridDensity` drove x and derived y as "one fewer". A wide time series wants
    many ticks across and few up; a tall profile wants the reverse; neither
    could be asked for. `gridDensityY` is separate, and 0 keeps the old
    behaviour exactly — including the 7-and-6 default — so no figure made
    before this existed changes.
    """
    spec = (ROOT / "native" / "plot2d" / "include" / "PlotSpec.h").read_text(
        encoding="utf-8", errors="ignore")
    assert "int gridDensityY = 0;" in spec, "the vertical grid count is gone"

    backend = _strip_comments(_backend_source())
    want_y = re.search(r"const int wantY=(.*?);", backend, re.S)
    assert want_y, "the vertical tick target is no longer computed"
    # The CONDITION, not a mention. Looking for the name anywhere in the
    # expression was the first version and it did not work: qBound(2,
    # spec.style.gridDensityY, 25) keeps the name in the string even when the
    # branch that selects it has been replaced by `false`, so the guard passed
    # on code that ignores the setting entirely. The same weakness as the
    # bottomMargin guard, found the same way - by reverting the thing.
    assert "spec.style.gridDensityY>0" in want_y.group(1), (
        "the vertical tick count no longer TESTS its own setting, so whatever "
        "is typed into the 'up' box is ignored")
    # The fallback has to be the old rule, or every existing figure re-ticks.
    assert "gridDensity-1" in want_y.group(1), (
        "0 no longer falls back to one fewer than across, so a figure that "
        "never set this changes how it is ticked")

    style = (ROOT / "app" / "qml" / "components" / "FigureStyleBar.qml").read_text(
        encoding="utf-8", errors="ignore")
    assert "plotGridDensityY" in style, (
        "the panel offers no way to set the vertical count")
    assert "Layout.fillWidth: true" not in style.split("SpinBox")[1].split("}")[0], (
        "the grid count box fills the row again — it is a two-digit number and "
        "it was reported as taking the space of the panel's main control")


def test_the_sweep_fixture_colours_its_series() -> None:
    """The gallery is the artefact the visual pass is done on.

    The shared fixture set no colours, so all five series took PlotSeries'
    default blue and every multi-series figure in the gallery was five
    identical blue shapes under a five-entry legend. That is not what GraphVis
    draws — buildPlotSeries assigns seriesPalette() per series — so the pass was
    being done on pictures no user would ever see.

    It also blinded two of the sweep's own checks: same-picture clustering and
    the "renders exactly as a Line Chart" test both compare rendered images, and
    with every series one colour they were comparing figures with a whole
    channel removed. Adding the palette can only separate engines that were
    being compared with less information; it cannot merge any.
    """
    source = (ROOT / "native" / "plot2d" / "src" / "PlotSelfTest.cpp").read_text(
        encoding="utf-8", errors="ignore")
    body = _strip_comments(source)

    assert "seriesPalette(ColourVision::Standard)" in body, (
        "the sweep fixture no longer takes the real palette, so every "
        "multi-series figure in the gallery is one colour")

    inputs = body[body.index("SweepInputs sweepInputs()"):]
    inputs = inputs[:inputs.index("\n}")]
    assert re.search(r"series\[i\]->color=palette", inputs), (
        "the palette is fetched and not assigned, which is the same picture "
        "with an extra variable")


def test_a_mosaic_names_its_rows_as_well_as_its_columns() -> None:
    """Counts without saying what they are counts of.

    A mosaic plot is a two-way contingency table. The columns were labelled
    along the bottom and the rows were not labelled at all, so the figure showed
    a grid of numbers with no way to tell which category each band was. The left
    margin was already reserved — eight per cent of the width — and nothing was
    drawn in it.

    Against the FIRST column, because that is the only place a row is a single
    band at a known height: every later column gives the same row a different
    height, so there is no shared baseline to label against.
    """
    source = _backend_source()
    start = source.index("void QtPlotBackend::drawMosaic(")
    body = _strip_comments(source[start:source.index("\n}", start)])

    assert body.count("drawText(") >= 3, (
        "the mosaic draws fewer labels than before — the row names are gone")
    assert "QString::number(rows[r]" in body, (
        "the mosaic no longer names its rows, so the bands cannot be told apart")
    assert "c==0" in body, (
        "the row label is no longer tied to the first column, so it is drawn "
        "once per column at a different height each time")


def test_the_x_axis_gets_the_headroom_the_y_axis_has() -> None:
    """A reason about fields was applied to every engine.

    The y axis has had five per cent of headroom for a long time, "so the
    topmost marker is not drawn on the frame", and it excludes fields — a field
    is an image of a region and a region has no headroom, so `!columns` sits
    right there in the condition.

    The x axis had no padding at all, and its comment gave the same
    field-shaped reason. So the exclusion that belonged to fields was applied
    to everything: Global Sensitivity, whose value axis IS x, drew its longest
    bars into the right-hand frame line; Horizontal Bar did the same; the
    rightmost point of any line or scatter sat on the frame. The figure read as
    clipped.

    The zero baseline has to survive it. Horizontal Bar clamps xLo to zero just
    above, and padding both ends would lift the bars off the axis they are
    measured from — so an axis pinned to zero is padded at the far end only.
    """
    body = _strip_comments(_backend_source())
    frame = body[body.index("QtPlotBackend::Frame QtPlotBackend::computeRange("):]
    frame = frame[:frame.index("\n}")]

    assert "if(!f.xLog&&!columns){" in frame, (
        "the x axis is unpadded again, so the extreme value of every engine "
        "whose data runs along x is drawn on the frame")
    # Same overflow-safe form as the y pad: (hi-lo)*0.05 goes infinite on a
    # column holding both 1e308 and -1e308.
    assert "xHi*0.05-xLo*0.05" in frame, (
        "the x pad is computed as (hi - lo) * 0.05, which overflows to infinity "
        "on a wide column and turns every coordinate into NaN — the y pad "
        "carries a comment about exactly this")
    # The STATEMENTS, not the names. This guard has now been written wrongly
    # three times in this file and caught each time by reverting the thing it
    # watches: a name appears at its declaration AND at its use, so renaming
    # the declaration leaves the name in the source and the guard passes on
    # code that does not compile. Match what the line does.
    assert "if(!pinnedLow&&!xLoStated) xLo-=pad;" in frame, (
        "the low end of x is padded unconditionally, so a horizontal bar chart "
        "draws its bars floating off the zero axis they are measured from")
    assert "if(!pinnedHigh&&!xHiStated) xHi+=pad;" in frame, (
        "the high end of x is padded unconditionally, which moves an axis that "
        "was deliberately pinned to zero")

    # And it must still be excluded for a field, which is what the whole
    # `!columns` condition exists for.
    xpad = frame[frame.index("if(!f.xLog&&!columns){"):]
    xpad = xpad[:xpad.index("\n    }")]
    assert "columns" not in xpad.replace("!columns", ""), (
        "the field exclusion has moved inside the x pad body, where it no "
        "longer excludes anything")


def test_an_axis_of_categories_keeps_its_own_ticks() -> None:
    """There is no class 1.5.

    The tick chooser picks round numbers, which is right for a measurement and
    wrong for a category. A confusion matrix runs from -0.5 to n-0.5 with its
    cells at the integers, and got ticks at 0.5, 1.5 and 2.5 — three labels
    naming classes that do not exist and none naming the four that do.

    The ticks carry a value AND a label because the two differ: cells are
    indexed 0..n-1, and the classes they stand for are whatever was in the data.
    """
    spec = (ROOT / "native" / "plot2d" / "include" / "PlotSpec.h").read_text(
        encoding="utf-8", errors="ignore")
    assert "QVector<double> tickValues" in spec and "QStringList tickLabels" in spec, (
        "an axis can no longer carry its own ticks, so every categorical axis "
        "is labelled with round numbers between its categories")
    # The default initialisers are load-bearing: thirty-odd sites brace-init
    # PlotAxis positionally and stop short of these two.
    assert "tickValues = {}" in spec and "tickLabels = {}" in spec, (
        "the new axis fields have no default initialiser, so every positional "
        "PlotAxis{...} in the backend raises -Wmissing-field-initializers and "
        "the zero-warning build is gone")

    body = _strip_comments(_backend_source())
    frame = body[body.index("QtPlotBackend::Frame QtPlotBackend::computeFrame("):]
    frame = frame[:frame.index("\n}")]
    assert "spec.xAxis.tickValues.isEmpty()" in frame, (
        "computeFrame ignores an axis's own ticks again")
    assert "spec.yAxis.tickValues.isEmpty()" in frame, (
        "only one axis honours its own ticks")

    matrix = body[body.index('in.engine==QLatin1String("Confusion Matrix")'):]
    matrix = matrix[:matrix.index("return out;\n    }")]
    assert "out.xAxis.tickValues.append" in matrix, (
        "the confusion matrix no longer labels its classes")


def test_a_stated_grid_resolution_is_not_clamped_up() -> None:
    """A four-class matrix is four cells wide, not twelve.

    Grid resolution is inferred from the sample count and floored at twelve —
    sensible for a scattered field, where fewer cells leave no structure to
    see. It was applied to stated resolutions too, so a confusion matrix over
    four classes was drawn as a smooth twelve-by-twelve field: the diagonal
    smeared across it, and cells showing counts that appear in no row of the
    table.

    The floor now applies only to the guess.
    """
    body = _strip_comments(_backend_source())
    assert "statedResolution" in body, (
        "the grid floor no longer distinguishes a stated resolution from an "
        "inferred one, so an engine that knows its own size cannot say so")
    assert "qBound(statedResolution?2:12,wanted,ceiling)" in body, (
        "the twelve-cell floor is back on every path, which redraws an n-by-n "
        "table as a twelve-by-twelve field")

    matrix = body[body.index('in.engine==QLatin1String("Confusion Matrix")'):]
    matrix = matrix[:matrix.index("return out;\n    }")]
    assert "out.style.fieldResolution=classes.size();" in matrix, (
        "the confusion matrix no longer states its own size, so the resolution "
        "is guessed from the sample count and its cells stop matching its "
        "classes")


def test_a_pie_takes_its_colours_from_the_category_palette() -> None:
    """A pie's slices are the points of one series, not separate series.

    The wedge colours fell back to collecting `ps.color` from each series, on
    the reasoning that the canvas puts a palette entry on each one. That is true
    of a line chart and false of a pie: a pie built from a category column and a
    value column has one or two series, so the loop collected one colour and
    every slice was drawn in it.

    It passed the sweep by accident. The shared fixture hands over five columns,
    so five colours came out and the slices alternated — an ordinary pie, from
    one category column and one value column, came out flat.

    `categoryPalette` is the right source and already existed for exactly this:
    buildPlotSeries sets it for "the engines that lay out a whole — treemap,
    icicle, sunburst, Sankey, chord — which never get a PlotSeries to take a
    colour from". A pie is one of those and was left off the list.
    """
    source = _backend_source()
    start = source.index("void QtPlotBackend::drawPie(")
    body = _strip_comments(source[start:source.index("\n}", start)])

    assert "spec.style.categoryPalette" in body, (
        "a pie no longer reads the category palette, so it ignores the "
        "colour-vision setting and the person's own colours")
    palette = body.index("spec.style.categoryPalette")
    per_series = body.index("for(const PlotSeries& ps:spec.series)")
    assert palette < per_series, (
        "the per-series fallback is consulted before the category palette, "
        "which is the ordering that made every pie one colour")

    # Distinctness, not count: two series both carrying PlotSeries' default
    # blue satisfied "at least two colours" and the pie was flat anyway.
    assert "QSet<QRgb> distinct;" in body, (
        "the fallback counts colours instead of distinguishing them, so a pie "
        "whose series all carry the default colour is drawn flat")
    assert "distinct.size()<2" in body, (
        "the built-in wedge list is no longer used when the series colours do "
        "not actually differ")


def test_a_pie_can_name_its_slices_and_the_person_chooses_how() -> None:
    """A pie has no axis to read names off, and it named them nowhere.

    Wedges with no names and no key is a picture of some proportions rather
    than a figure anybody can read. Both remedies are legitimate and which one
    reads better depends on the figure — labels on the slices when there are few
    with short names, a legend when there are many — so it is a choice, not a
    default nobody can change.

    A slice too thin to hold its label must be left to the legend rather than
    have text laid across its neighbours. Measured against the wedge's CHORD at
    the radius the label sits at: the text is horizontal and centred, so what
    has to fit is the straight distance across the wedge. A first version
    compared arc length to text height, which a 3% slice passes comfortably —
    and its label still lay across both neighbours, because a 15 px wedge cannot
    hold 34 px of text however long its arc is.
    """
    spec = (ROOT / "native" / "plot2d" / "include" / "PlotSpec.h").read_text(
        encoding="utf-8", errors="ignore")
    assert "int pieLabels = 0;" in spec, (
        "a pie has no way to name its slices again")

    source = _backend_source()
    start = source.index("void QtPlotBackend::drawPie(")
    body = _strip_comments(source[start:source.index("\n}", start)])

    assert "spec.style.pieLabels" in body, "the pie ignores the naming choice"
    for want in ("wantSliceLabels", "wantLegend"):
        assert want in body, f"the pie no longer offers {want}"
    assert "chord=2.0*radius*std::sin" in body, (
        "the on-slice label no longer checks the wedge's chord, so a thin "
        "slice's label is drawn across its neighbours")
    assert "box.width()<=chord" in body, (
        "the chord is computed and not used to decide whether the label fits")

    # The ink has to be chosen against the WEDGE, not the figure: a dark label
    # on a dark slice is the same as no label.
    assert "0.2126*fill.redF()" in body, (
        "the slice label's colour is no longer chosen against the slice it is "
        "drawn on")

    header = (ROOT / "app" / "src" / "AppController.h").read_text(
        encoding="utf-8", errors="ignore")
    assert "plotPieLabelNames" in header, (
        "the choice is not offered to the person, which was the whole point")

    panel = _qml("components/FigureAppearanceBar.qml")
    assert "plotPieLabels" in panel, "the panel offers no way to choose"
    assert 'root.canvas.engine === "Pie"' in panel, (
        "the control is shown on every engine, most of which have no slices — "
        "a control that does nothing is one more thing to try before believing "
        "it does nothing")


def test_a_scatter_draws_the_reference_lines_appended_to_it() -> None:
    """Thirteen engines were missing the thing that makes them that engine.

    `drawScatter` drew markers and nothing else, on the reasoning that for a
    scatter the markers ARE the plot. True of the scatter's own data; false of
    the reference lines that thirteen rewrites append to it — Bland-Altman's
    bias and limits of agreement, the Q-Q reference, Volcano and Manhattan's
    significance thresholds, both arms of a Funnel plot, MA's zero line,
    Influence's Cook's-distance contours, and five more.

    Each appends a two-point series with drawLine set. The painter ignored it,
    so the line vanished and its two endpoints were drawn as stray markers —
    a Bland-Altman legend naming bias and ±1.96 SD above a figure containing
    four dots and no lines.
    """
    body = _strip_comments(_backend_source())
    start = body.index("void QtPlotBackend::drawScatter(")
    scatter = body[start:body.index("\n}", start)]

    assert "if(s.drawLine){" in scatter, (
        "the scatter ignores a series that asks for a line again, so every "
        "reference line appended to it disappears")
    assert "if(!s.drawLine||s.drawMarkers){" in scatter, (
        "the marker guard no longer distinguishes a reference line from the "
        "data, so a two-point rule is drawn as two stray markers")

    # And the engine must not depend on its CALLER having cleared the flag.
    # The sweep's fixture leaves PlotSeries' default of true, which drew a
    # bare scatter as a line chart, identical to Line Chart.
    # Sliced from the RAW source: the marker that ends this region is a
    # comment, and _strip_comments has already removed it from `body`.
    raw = _backend_source()
    core = raw[raw.index("PlotSpec QtPlotBackend::prepareSpecCore("):]
    core = core[:core.index("----------------- ECDF")]
    assert 'in.engine==QLatin1String("4D / 5D Scatter")' in core, (
        "a scatter chosen directly no longer forces markers, so whether it "
        "draws lines depends on whoever built the series")
    assert "s.drawLine=false; s.drawMarkers=true;" in core, (
        "the forced marker style is gone")


def test_no_label_carries_a_doubled_percent_sign() -> None:
    """QString::arg is not printf.

    A comment in this file already records the finding — "A single %, not %%:
    QString::arg is not printf and does not collapse a doubled one, so '%%'
    reaches the user verbatim" — and four other labels still had it: a funnel
    plot's "95%% funnel", a stress-strain "0.2%% offset", a psychrometric
    "10%% steps" and a regression "95%% band".
    """
    source = _backend_source()
    # The comment documenting the rule is allowed to spell it out.
    offenders = [line.strip() for line in source.splitlines()
                 if "%%" in line and not line.strip().startswith("//")]
    assert not offenders, (
        "labels still carry a doubled percent sign, which reaches the figure "
        f"verbatim: {offenders}")


def test_every_polar_engine_has_a_title_and_says_what_the_angle_is() -> None:
    """A compass with no compass on it.

    `drawPolar` drew four labelled rings and twelve UNLABELLED spokes, so every
    polar engine showed a direction nobody could read: a wind rose whose
    bearings are its entire content, a stereonet with no orientation, a compass
    with nothing to read a heading against. The radius carried numbers and the
    angle carried none.

    It also never called `drawFloatingTitle` — 28 calls elsewhere in the file
    and none here — so none of these figures had a title either.

    The painter has THREE exits: the wedge/spoke branch and the bubble branch
    both return early. A title added at only the last one leaves the roses and
    the compass untitled, which is the same bug in a smaller place.
    """
    source = _backend_source()
    start = source.index("void QtPlotBackend::drawPolar(")
    body = source[start:source.index("\n// ---", start)]

    assert body.count("drawFloatingTitle(p,target,spec);") == 3, (
        "drawPolar does not draw its title on all three of its exits, so the "
        "engines leaving by an early return have no title")

    stripped = _strip_comments(body)
    assert "spoke*30.0" in stripped, (
        "the twelve spokes are unlabelled again, so no polar figure says what "
        "its angle is")
    assert "radius+14.0" in stripped, (
        "the angular labels are no longer placed outside the rim, where the "
        "46 px the radius gives back to the frame is reserved for them")

    # The labels must describe the convention actually drawn. Both the label
    # and the marks go through toScreenDegrees, so they agree whichever
    # convention the figure is set to; placing a label by the raw angle would
    # describe a figure this is not drawing.
    assert "std::cos(angle)*(radius+14.0)" in stripped, (
        "the angular label is no longer placed by the same angle convention "
        "the wedges are drawn with, so the labels and the data disagree")


def test_the_polar_angle_convention_is_applied_in_exactly_one_place() -> None:
    """Which way round a compass goes, decided once.

    `drawPolar` measured every angle the way mathematics does - zero at three
    o'clock, increasing anticlockwise - and drew a WIND ROSE with it. A bearing
    of 0 is north; the rose put it due east and turned the wrong way. So did
    the compass, the stereonet and the radar. The convention is not a property
    of the painter, it is a property of the FIGURE: a polar scatter of phase
    against amplitude is mathematical and a wind rose is not, and both are
    drawn by this one function. Hence `PlotStyle::polarConvention`, set per
    figure by the operator.

    What makes that safe is that the arithmetic exists once. There are five
    places an angle becomes a screen position in here - the spoke labels, the
    marks and paths, the wedges, the compass arrow, the bubbles - and a second
    copy of `90.0-x` in any one of them is how a figure ends up with its
    wedges in one convention and its labels in the other, which is worse than
    having no setting at all because it looks answered.

    So: one definition, and every angle site goes through it.
    """
    source = _backend_source()
    start = source.index("void QtPlotBackend::drawPolar(")
    body = _strip_comments(source[start:source.index("\n// ---", start)])

    assert "const auto toScreenDegrees=" in body, (
        "drawPolar no longer converts a data angle into a screen angle in one "
        "named place, so the convention is decided wherever someone last "
        "needed it")
    assert "spec.style.polarConvention==1" in body, (
        "the polar convention is no longer read from the figure, so a wind "
        "rose and a polar scatter are measured the same way again")
    assert body.count("90.0-") == 1, (
        "the bearing arithmetic appears more than once in drawPolar, so a "
        "second copy can disagree with toScreenDegrees and put the wedges in "
        "one convention and the labels in the other")

    # Every angle that reaches the screen, through the one function. Five
    # call sites; the definition carries no parenthesis so it is not counted.
    assert body.count("toScreenDegrees(") == 5, (
        "an angle in drawPolar is no longer converted - one of the label, "
        "mark, wedge, arrow or bubble sites is drawing a raw data angle")



def test_series_left_at_the_default_colour_are_given_distinct_ones() -> None:
    """A legend naming three series above a picture drawn in one colour.

    `PlotSeries::color` has a default - one blue - so a caller that hands over
    three series without setting any of them gets three IDENTICAL series. A
    grouped bar chart came out as eight pairs of the same blue bar under a
    legend naming "measured" and "predicted"; the legend said two things and
    the picture said one. The pie had the same fault from the other direction
    and was fixed in `drawPie` alone, which left every other painter with it.

    The application never showed it, because `PlotCanvas::buildPlotSeries`
    assigns from the palette first. That is exactly the shape of the scatter's
    marker flag: **the backend must not depend on its caller having filled
    something in**, because the next caller will not.

    Filled in once, before the rewrites - a rewrite copies its source series'
    colour onto what it derives, so doing it afterwards leaves every derived
    figure carrying the default. And only where the colour was left at the
    default: a rewrite's grey reference line or a person's own colour is a
    choice, and this overrides nothing.
    """
    source = _backend_source()
    assert "void assignDefaultSeriesColours(PlotSpec& spec)" in source, (
        "nothing fills in a series colour the caller left at the default, so a "
        "legend can name series the picture draws in one colour")

    body_start = source.index("void assignDefaultSeriesColours(PlotSpec& spec)")
    body = _strip_comments(source[body_start:source.index("\n} // namespace", body_start)])
    assert "PlotSeries().color" in body, (
        "the default colour is no longer read from PlotSeries itself, so a "
        "change to that default silently stops this working")
    assert "if(spec.series.size()<2) return;" in body, (
        "a single series is being recoloured - there is nothing for it to be "
        "confused with, and its colour is the one the caller asked for")

    # Both entry points, and before the rewrite chain in each.
    for entry, call in (
            ("QtPlotBackend::preparedCached", "prepCache_.prepared=prepareSpecCore(coloured);"),
            ("QtPlotBackend::prepareSpec", "PlotSpec out=prepareSpecCore(coloured);")):
        start = source.index(entry)
        window = source[start:start + 3000]
        assert "assignDefaultSeriesColours(coloured);" in window and call in window, (
            f"{entry} no longer assigns colours before preparing the spec, so "
            "whichever path the figure takes decides whether it has any")


def test_a_horizontal_bar_chosen_from_the_library_is_transposed() -> None:
    """The chart that drew the row numbers as the bar lengths.

    `drawHorizontalBar` reads the VALUE from x and the CATEGORY ROW from y -
    the axes really are swapped in this chart, deliberately. Three rewrites
    hand it that shape: Global Sensitivity, Population Pyramid and Silhouette
    Plot, each emitting one single-point series per bar.

    Nothing gave that shape to a Horizontal Bar chosen from the library. It
    arrived as an ordinary series - category in x, measurement in y - so the
    painter read each category NUMBER as the length of its bar and each
    measurement as the row it belonged to. The chart drew, which is why it
    survived a sweep that asks only whether an engine put ink on the page: it
    was a picture of the data transposed, not a picture of nothing.

    The rewrites must be left alone. What separates an already-transposed spec
    from one that still needs it is that theirs carry a single point per
    series; transposing twice puts the chart back as it was.
    """
    source = _backend_source()
    start = source.index("PlotSpec QtPlotBackend::prepareSpecCore")
    head = _strip_comments(source[start:start + 6000])

    assert 'in.engine==QLatin1String("Horizontal Bar")' in head, (
        "a Horizontal Bar chosen directly is no longer transposed, so it reads "
        "its category numbers as bar lengths")
    assert "if(qMin(s.x.size(),s.y.size())>1){ ordinary=true; break; }" in head, (
        "the transpose no longer distinguishes the ordinary shape from the "
        "one-point-per-bar shape the rewrites emit, so it will run on theirs "
        "too and put those charts back the way they were")
    assert "bar.x={s.y[i]};" in head and "bar.y={s.x[i]};" in head, (
        "the transpose no longer swaps value and category, which is the whole "
        "of what it does")
    assert "out.xAxis=in.yAxis;" in head and "out.yAxis=in.xAxis;" in head, (
        "the axis labels no longer follow their data across, so the value axis "
        "is labelled with the category column's name")


def test_a_single_bar_is_a_bar_and_not_a_filled_rectangle() -> None:
    """One category, and the chart became a solid block.

    `slotWidthFrom` measures the gap between neighbouring bar positions. With
    only ONE position there is no gap to measure, so it took the fallback - and
    every caller's fallback is the plot's whole extent divided by the number of
    positions, which for one position is the whole extent. A Global Sensitivity
    of a single input was drawn as a rectangle from the left edge to its value
    and from the top of the frame to the bottom: a shape that says nothing
    about the input it is drawn for.
    """
    source = _backend_source()
    start = source.index("double QtPlotBackend::slotWidthFrom(")
    body = _strip_comments(source[start:source.index("\n}", start)])
    assert "qMin(fallback,qMax(1.0,limit)/3.0)" in body, (
        "the single-slot fallback is no longer capped, so a chart with one "
        "category fills its whole plot area with one bar")
    assert "finite(minGap)?minGap" in body, (
        "the cap is being applied to a MEASURED gap as well as to the guess, "
        "which would narrow every ordinary bar chart")


def test_error_bar_asks_for_two_columns_because_it_reads_two() -> None:
    """A legend of five series above a figure containing two.

    `drawErrorBar` reads `series[0]` as the measurement and `series[1]` as the
    uncertainty and ignores everything after that. With no column plan the
    engine fell to the default, which lets any number of y columns be mapped -
    so five mapped columns drew two and the legend named all five.
    """
    source = _backend_source()
    start = source.index("static const QSet<QString> kPairs{")
    pairs = source[start:source.index("if(kPairs.contains(engine))", start)]
    assert 'QStringLiteral("Error Bar")' in pairs, (
        "Error Bar has no two-column plan again, so the mapping panel will "
        "accept columns the painter never reads")


def test_global_sensitivity_names_its_inputs() -> None:
    """A ranking of five inputs, labelled 1 to 5.

    The rewrite turns off the legend - one key per bar would repeat the axis -
    so the input names had nowhere left to appear and the y axis numbered its
    rows. Which input matters most is the entire content of the chart, and it
    was the one thing the figure did not say.
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Global Sensitivity")){')
    body = _strip_comments(source[start:start + 2600])
    assert "out.yAxis.tickValues.append" in body and "out.yAxis.tickLabels.append" in body, (
        "the sensitivity chart no longer puts its input names on the axis, and "
        "its legend is off, so the bars are unlabelled")
    assert "out.legendVisible=false;" in body, (
        "the legend is back on, which repeats on the right what the axis now "
        "says on the left")

def test_a_summary_against_one_position_is_measured_whole() -> None:
    """The box plot that drew only the bottom of every distribution.

    `computeRange` paired x with y and stopped at the shorter of the two, which
    is right for a curve and wrong for the shape several engines use: ONE
    position and a list of numbers measured there. A box plot carries
    x={slot} and y={whisker,q1,median,q3,whisker}, so the minimum was one and
    the axis was fitted to the LOWER WHISKER ALONE - every box in the catalogue
    had its median, upper quartile and top whisker drawn above the frame and
    clipped off. The figure looked deliberate.

    A Gantt row is the same shape transposed, y={row} and x={start,end}, so
    only each bar's start counted and the bars ran off the right-hand edge.

    And a forest plot is drawn SIDEWAYS on top of that: `drawForest` calls
    toDevice(f, s.y[k], row), so the estimate goes along x while the series
    carries it in y. Measuring x from .x scaled the effect axis to the NUMBER
    OF STUDIES - forty studies of effects between 1 and 4 on an axis running
    to 40.
    """
    source = _backend_source()
    start = source.index("QtPlotBackend::Frame QtPlotBackend::computeRange")
    body = _strip_comments(source[start:source.index("\n    // A stacked band", start)])

    assert "const bool summary=(nx==1&&ny>1)||(ny==1&&nx>1);" in body, (
        "computeRange no longer recognises a summary against one position, so "
        "a box plot's axis is fitted to its lower whisker alone")
    assert "const int n=summary?qMax(nx,ny):qMin(nx,ny);" in body, (
        "the summary is no longer walked to its full length, which is the "
        "whole of what recognising it was for")
    assert 'transposed=spec.engine==QLatin1String("Forest Plot")' in body, (
        "the forest plot's sideways packing is no longer known to the frame, "
        "so its effect axis is scaled to the number of studies")
    assert "double x=transposed?s.y[qMin(i,ny-1)]:s.x[qMin(i,nx-1)]" in body, (
        "the forest plot's x is no longer read from .y, so the estimate and "
        "the row have swapped back")


def test_a_stated_axis_limit_is_the_limit() -> None:
    """Type 100, get 105.

    The headroom that keeps the topmost marker off the frame was added after
    the explicit limits were applied, so it moved them: an axis maximum of 100
    was drawn as 105 and a minimum of 0 as -5. Headroom is for a bound that was
    MEASURED from the data. A bound that was asked for is already the answer.
    """
    source = _backend_source()
    start = source.index("QtPlotBackend::Frame QtPlotBackend::computeRange")
    body = _strip_comments(source[start:source.index("\n    if(spec.xAxis.inverted)", start)])

    for name in ("xLoStated", "xHiStated", "yLoStated", "yHiStated"):
        assert f"const bool {name}=" in body, (
            f"computeRange no longer records whether {name[:1]} {name[1:3]} was "
            "stated, so the padding below cannot tell a measured bound from a "
            "requested one")
    # And the limits must still be APPLIED. Recording that a bound was stated
    # and then not using it would leave every one of these assertions true on
    # code that ignores the axis limits entirely.
    assert "if(xLoStated) xLo=" in body and "if(xHiStated) xHi=" in body, (
        "a stated x limit is no longer applied at all, so the axis is fitted "
        "to the data whatever the figure asks for")
    assert "if(yLoStated) yLo=" in body and "if(yHiStated) yHi=" in body, (
        "a stated y limit is no longer applied at all")
    assert "if(!yLoStated) yLo-=pad;" in body and "if(!yHiStated) yHi+=pad;" in body, (
        "the y headroom is applied to stated limits again, so a figure ignores "
        "the numbers it was given")
    assert "if(!pinnedLow&&!xLoStated) xLo-=pad;" in body, (
        "the x headroom is applied to a stated minimum again")
    assert "if(!pinnedHigh&&!xHiStated) xHi+=pad;" in body, (
        "the x headroom is applied to a stated maximum again")


def test_a_legend_does_not_repeat_itself_or_cover_the_figure() -> None:
    """Ten rows for six things, in a box over half the plot.

    Three separate faults in one panel.

    A flight profile emits one series per leg per column, each named for the
    leg, so five columns of similar shape produced "top of climb 5" twice and
    "top of climb 1" twice - rows a reader has to compare against each other
    before trusting any of them.

    A lift curve names its series with the parameters fitted through them:
    "signal (a 0.1851 /deg, 10.604 /rad, CLmax 5.238)". The box was as wide as
    the longest label and nothing else, so it covered the right-hand half of
    the figure. The parameter belongs in the label; the label belongs inside a
    box that leaves the graph visible.

    And a box that ran out of vertical room simply stopped drawing, leaving a
    legend that looks complete and is not - twelve series, seven names, and
    nothing to say the other five exist. The pie's legend already ends with
    "+N more".
    """
    source = _backend_source()
    start = source.index("void QtPlotBackend::drawLegend(")
    body = _strip_comments(source[start:source.index("\nvoid QtPlotBackend::drawLineChart", start)])

    assert "if(seen.contains(s.label)) continue;" in body, (
        "the legend lists a name more than once again")
    assert "f.plotArea.width()/3.0" in body, (
        "the legend's width is no longer capped against the plot, so a long "
        "label draws a box over the figure")
    assert "fm.elidedText(keyFor(s),Qt::ElideRight,widest+4)" in body, (
        "labels are no longer elided to the capped width, so they are drawn "
        "outside the box that was sized for them")
    assert 'QStringLiteral("+%1 more")' in body, (
        "a legend that does not fit no longer says how many rows it is hiding")


def test_a_histogram_of_several_columns_is_overlaid_not_divided() -> None:
    """Five columns, and every bar one pixel wide.

    Two faults with the same root: a rule correct for grouped bars applied to
    histograms.

    `drawBar` divides the slot between series so grouped bars stand side by
    side. Five histograms bin independently and are compared by being laid over
    one another, so dividing each bin five ways left hairlines.

    And the width itself was measured from every series' positions POOLED into
    one list. Five independent binnings interleave, so the smallest gap in that
    list is the distance between two unrelated bins. One finely binned column
    decided how every other column was drawn.

    A grouped bar chart is unchanged by either: its series share their
    categories, so each one's own spacing IS the shared spacing.
    """
    source = _backend_source()
    start = source.index("void QtPlotBackend::drawBar(")
    body = _strip_comments(source[start:source.index("\n// ---", start)])

    assert 'const bool overlaid=spec.engine==QLatin1String("Histogram");' in body, (
        "a multi-column histogram divides the bin between its columns again, "
        "so every bar is a hairline")
    assert "const int groups=overlaid?1:qMax(1,spec.series.size());" in body, (
        "the overlay no longer stops the slot being divided")
    assert "auto slotFor=[&](const PlotSeries& s){" in body, (
        "the bar width is measured from the pooled positions again, so the "
        "most finely binned column decides the width of every other")
    assert "const double slot=slotFor(s);" in body, (
        "the per-series width is computed and not used")

    hist = source.index('if(in.engine==QLatin1String("Histogram")){')
    rewrite = _strip_comments(source[hist:hist + 2600])
    assert "out.legendVisible=in.series.size()>1;" in rewrite, (
        "a histogram of several columns has no legend again, so nothing says "
        "which colour is which column")
    assert "bar.opacity=(in.series.size()>1)?0.55:1.0;" in rewrite, (
        "overlaid histograms are opaque again, so only the last column drawn "
        "is visible")


def test_a_derivative_at_the_edge_of_a_grid_is_one_sided() -> None:
    """The bright stripe that was an artefact of the stencil.

    Both places that differentiate a grid wrote the central difference as
    (f[i+1] - f[i-1]) / 2h and let the sampler CLAMP the index. On the first
    and last row that takes two samples one cell apart and still divides by two
    cells of distance, so every boundary cell got half the slope it should
    have. A divergence map of a nearly constant field came out uniform with a
    bright band along the top and bottom edges, and the colour scale was then
    set by the artefact.

    The slope, aspect and hillshade maps avoided that by skipping the outer
    ring entirely - and then set their axes to the FULL grid, so the map was
    drawn as a coloured rectangle inset inside its own frame with empty
    background all the way round.

    Dividing by the distance actually sampled is the one-sided derivative at
    the edge and the central difference everywhere else.
    """
    source = _backend_source()

    div = source.index('const bool vorticity=engine==QLatin1String("Vorticity Map");')
    dbody = _strip_comments(source[div:div + 2600])
    assert "auto slopeX=[&](auto&& sample,int cx,int cy){" in dbody, (
        "the divergence and vorticity maps differentiate with a clamped "
        "central difference again, so their edges are half-slope artefacts")
    assert "(double(b-a)*qMax(1e-12,cellX))" in dbody, (
        "the divergence difference no longer divides by the distance actually "
        "sampled, which is the whole of the fix")

    terr = source.index('if(in.engine==QLatin1String("Slope Map")')
    tbody = _strip_comments(source[terr:terr + 3400])
    assert "for(int j=0;j<filled.ny;++j){" in tbody and "for(int i=0;i<filled.nx;++i){" in tbody, (
        "the terrain maps skip their outer ring again, so the field is drawn "
        "inset inside a frame labelled for the whole grid")
    assert "const int iA=qMax(0,i-1),iB=qMin(filled.nx-1,i+1);" in tbody, (
        "the terrain gradient no longer falls back to a one-sided difference "
        "at the boundary")


def test_a_p_value_of_zero_does_not_set_the_axis() -> None:
    """One rounding artefact, and the threshold line vanished.

    Both -log10(p) engines clamped p to 1e-300, which draws a zero at y = 300
    on an axis whose real content lies between 0 and about 8. The genome-wide
    threshold a Manhattan plot exists to be read against sits at 7.3; it was a
    pixel above the baseline, and every genuine peak with it.

    A zero is a rounding artefact of whatever produced it. Drawn a decade below
    the smallest p-value the data actually contains it is still the most
    extreme point on the figure - which is honest - without deciding the scale
    for everything else.
    """
    source = _backend_source()
    assert "double negLogFloor(const QVector<double>& values)" in source, (
        "nothing works out where a zero p-value should be drawn, so it is "
        "clamped to a fixed 1e-300 again and takes the axis with it")
    body = _strip_comments(source[source.index("double negLogFloor("):
                                  source.index("double negLogFloor(") + 700])
    assert "smallest*0.1" in body, (
        "the floor is no longer relative to the smallest p-value present, so "
        "it is a fixed number again")

    for engine in ('Volcano Plot', 'Manhattan Plot'):
        start = source.index(f'if(in.engine==QLatin1String("{engine}")')
        window = _strip_comments(source[start:start + 2600])
        assert "negLogFloor(" in window and "qBound(floorP," in window, (
            f"{engine} clamps its p-values to a fixed floor again")


def test_engines_that_name_groups_put_the_names_on_the_axis() -> None:
    """Five boxes, numbered 1 to 5.

    A box plot turns its legend off - one key per box would repeat the axis -
    so with the axis numbering its slots the boxes had no names anywhere. A
    correlation matrix went further and joined every name into the x axis
    LABEL: one line of text reading "signal, paired, p_value, label, cost",
    which is the information present and unusable. A radar chart labelled its
    spokes 0, 30, 60 - the position of each variable expressed in degrees.

    All three now use the tick labels the confusion matrix already had, and
    drawPolar falls back to degrees only for the engines whose angle really is
    a direction.
    """
    source = _backend_source()

    assert "void labelMatrixAxes(PlotSpec& out,const QStringList& names)" in source, (
        "the square matrices no longer label their axes with the variable "
        "names, so the rows and columns are numbered")
    helper = _strip_comments(source[source.index("void labelMatrixAxes("):
                                    source.index("void labelMatrixAxes(") + 900])
    assert "out.style.fieldResolution=k;" in helper, (
        "a k-by-k matrix no longer states its resolution, so the heatmap "
        "resamples it onto a grid of its own and the cells stop corresponding "
        "to pairs of variables")

    box = source.index('if(in.engine==QLatin1String("Box Plot")){')
    bbody = _strip_comments(source[box:box + 2600])
    assert "out.xAxis.tickValues.append(box.x.first());" in bbody, (
        "the box plot numbers its boxes again, and its legend is off, so "
        "nothing names them")

    polar = source.index("void QtPlotBackend::drawPolar(")
    pbody = _strip_comments(source[polar:source.index("\n// ---", polar)])
    assert "const int named=qMin(spec.xAxis.tickValues.size(),spec.xAxis.tickLabels.size());" in pbody, (
        "drawPolar always labels its spokes in degrees again, so a radar "
        "chart's variables are drawn as angles")


def test_a_strip_plot_is_a_strip_and_not_a_smaller_scatter() -> None:
    """Two catalogue entries, one marker size apart.

    Strip Plot kept the incoming x - the time column - cleared drawLine and set
    markerSize to 4.2. That is 4D / 5D Scatter with one constant changed, and a
    person choosing it from a library of four hundred is asking for the figure
    that name means: every observation of a group at that group's position.

    The jitter is what separates it from Beeswarm, which moves points until
    none overlap and so distorts where they are. It has to be deterministic, or
    the same data draws a different picture each time.
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Strip Plot")){')
    body = _strip_comments(source[start:start + 2200])

    assert "pts.x.append(double(slot)+(frac-0.5)*0.32);" in body, (
        "a strip plot puts its points back at the incoming x, which makes it a "
        "scatter with a different marker size")
    assert "std::fmod(double(i)*0.6180339887498949,1.0)" in body, (
        "the jitter is no longer the deterministic low-discrepancy sequence, "
        "so the same data draws a different figure each time")
    assert 'out.xAxis=PlotAxis{QStringLiteral("group")' in body, (
        "the strip plot's x axis is no longer the group, so it is labelled "
        "with whatever column the x mapping held")


def test_a_fit_whose_parameters_are_impossible_is_not_drawn() -> None:
    """Vmax 4.6, and a spike of a hundred million.

    v = Vmax*S/(Km + S). With Km negative the denominator passes through zero
    inside the sampled range; the guard that stopped it dividing by zero -
    qMax(1e-12, ...) - turned the pole into about 1e8. One column whose
    double-reciprocal line sloped the wrong way put that spike on a figure
    whose data reaches 7, and every real curve became a flat line along the
    bottom of an axis scaled to the artefact.

    A fit whose parameters are not physical is a fit that failed. The points
    are still drawn and the label says why, which is what the Gompertz rewrite
    does with a fit that will not converge.
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Michaelis-Menten")){')
    body = _strip_comments(source[start:start + 3000])

    assert "if(line.ok&&line.intercept>0&&!(line.slope*vmaxTest>0)){" in body, (
        "a Michaelis-Menten fit with a non-positive Km is drawn again, so its "
        "pole sets the axis for every other series on the figure")
    assert "no Michaelis-Menten fit: Km" in body, (
        "the figure no longer says why a series has no fitted curve, which "
        "leaves a reader to guess whether one was attempted")

def test_the_polar_convention_travels_with_the_figure() -> None:
    """A wind rose and a polar scatter in the same notebook.

    The convention was made a setting because neither answer is right for every
    figure - and that is only true if two figures can disagree. Saved on the
    APPLICATION it would be one answer for the whole notebook, so the wind rose
    would be wrong whenever the polar scatter was right, which is the position
    the setting exists to get out of.

    So it is written into the figure's own state, with the pie's slice naming
    beside it for the same reason. A file written before either existed carries
    neither, and the canvas keeps the default rather than resetting to one the
    person never chose.
    """
    canvas = (ROOT / "native/plot2d/src/PlotCanvas.cpp").read_text(encoding="utf-8")

    start = canvas.index("QVariantMap PlotCanvas::figureState() const {")
    saved = canvas[start:canvas.index("\n}", start)]
    assert '{QStringLiteral("polarConvention"),spec_.style.polarConvention}' in saved, (
        "a figure no longer saves which way round its angles are measured, so "
        "reopening a wind rose draws it in whatever convention the last figure "
        "left behind")
    assert '{QStringLiteral("pieLabels"),spec_.style.pieLabels}' in saved, (
        "a figure no longer saves how its pie names its slices")

    start = canvas.index("void PlotCanvas::applyFigureState(")
    restored = canvas[start:start + 6000]
    assert 'setPolarConvention(number("polarConvention"' in restored, (
        "the saved polar convention is written and never read back")
    assert 'setPieLabels(number("pieLabels"' in restored, (
        "the saved pie labelling is written and never read back")


def test_a_fitted_curve_that_runs_away_is_refused() -> None:
    """Two engines, one shape of fault.

    A Michaelis-Menten hyperbola with a negative Km has its pole inside the
    sampled range; a rating curve Q = a*h^b with b negative goes to infinity at
    small stage. In both the arithmetic is correct and the physics is not, and
    in both the runaway value then SET THE AXIS - a spike of 1e8 on a figure
    whose data reaches 7, a curve reaching 340 where every discharge is under
    8 - so every other series on the plot became a flat line along the bottom.

    A fit whose parameters are impossible is a fit that failed. The observations
    are still drawn and the label says why there is no curve, which is what the
    Gompertz rewrite already does with a fit that will not converge.
    """
    source = _backend_source()

    mm = source.index('if(in.engine==QLatin1String("Michaelis-Menten")){')
    mbody = _strip_comments(source[mm:mm + 3000])
    assert "if(line.ok&&line.intercept>0&&!(line.slope*vmaxTest>0)){" in mbody, (
        "a Michaelis-Menten fit with a non-positive Km is drawn again")

    rc = source.index('if(in.engine==QLatin1String("Rating Curve")){')
    rbody = _strip_comments(source[rc:rc + 3000])
    assert "if(line.ok&&!(line.slope>0)){" in rbody, (
        "a rating curve is fitted to a column whose discharge falls with "
        "stage, so its power law runs away at small stage and takes the axis")
    assert "no rating fitted" in rbody, (
        "the figure no longer says why a series has no rating curve")

def test_a_figure_with_its_own_coordinates_gets_no_rectangular_frame() -> None:
    """A triangle inside a box ruled 0.0 to 1.0.

    `engineHasAxes` decides this for an engine drawn by its own painter - a
    pie, a polar plot, a Piper diagram. It cannot decide it for an engine that
    REWRITES onto one that does have axes: a ternary scatter becomes a Line
    Chart so that drawLineChart can draw its triangle and its points, and the
    decision was then taken on the name "Line Chart". Every other member of the
    triangular family was already excluded by name and this one was missed,
    because its name is not the name the frame code sees.

    So the spec carries the answer and it travels through however many
    derivations follow. The title is not chrome in that sense - it names the
    figure whatever coordinates the figure is drawn in - so it is still drawn.
    """
    spec = (ROOT / "native/plot2d/include/PlotSpec.h").read_text(encoding="utf-8")
    assert "bool framed = true;" in spec, (
        "a spec can no longer say it brings its own coordinate system, so the "
        "frame decision is taken on the rewritten engine's name")

    source = _backend_source()
    start = source.index("void QtPlotBackend::drawChrome(")
    body = _strip_comments(source[start:start + 3000])
    assert "if(!spec.framed){" in body, (
        "drawChrome draws its box, ticks and grid whatever the spec says, so a "
        "ternary diagram is boxed again")

    tern = source.index('if(in.engine==QLatin1String("Ternary Scatter")){')
    tbody = _strip_comments(source[tern:tern + 5200])
    assert "out.framed=false;" in tbody, (
        "the ternary scatter no longer says it has its own axes")


def test_engines_whose_shape_is_the_reading_keep_their_aspect() -> None:
    """Mohr's circle, drawn as an ellipse.

    Both axes carry stress in the same unit and the radius IS the maximum
    shear, so it is read by looking at how round the circle is. Fitting each
    axis to its own range drew an ellipse, with nothing in the picture to say
    the distortion belonged to the frame rather than to the material. An
    impedance locus, a shaft orbit, a hodograph and a Poincare plot all have
    the same property: one quantity, two components, and a shape that means
    something.

    The shorter range is WIDENED rather than the longer one cropped - cropping
    would hide data to preserve a shape - and the ticks are re-chosen over the
    widened range, or the new part of the axis would carry no labels.
    """
    spec = (ROOT / "native/plot2d/include/PlotSpec.h").read_text(encoding="utf-8")
    assert "bool equalAspect = false;" in spec, (
        "a spec can no longer ask for equal units on both axes")

    source = _backend_source()
    start = source.index("Frame QtPlotBackend::computeFrame(")
    body = _strip_comments(source[start:source.index("\n    lastPlotArea_=f.plotArea;", start)])
    assert "if(spec.equalAspect&&!f.xLog&&!f.yLog" in body, (
        "computeFrame no longer equalises the two axes, so a Mohr's circle is "
        "drawn as an ellipse again")
    assert "const double unit=qMax(perPixelX,perPixelY);" in body, (
        "the equalising takes the smaller of the two scales, which CROPS the "
        "longer axis and hides data to preserve a shape")
    assert "xTicks=linearTicks(nxFrom,nxTo,wantX);" in body, (
        "the ticks are not re-chosen over the widened range, so the part of "
        "the axis the widening added carries no labels")

    assert source.count("out.equalAspect=true;") == 9, (
        "the set of engines asking for equal axes has changed - it was Mohr's "
        "Circle, EIS: Nyquist, Cole-Cole, Shaft Orbit, Hodograph, Poincare, "
        "the constellation diagram, the Nyquist stability plot and the "
        "Youden plot, each of which either carries one quantity on both axes "
        "or draws a circle the reading depends on")


def test_a_colour_mapped_field_has_a_bar_and_not_a_legend() -> None:
    """A heatmap with a three-row key naming its own columns.

    A field's series ARE the x, y and value columns, so a legend over one lists
    the column headings against swatches in colours that appear nowhere in the
    picture. Most rewrites onto a field turn the legend off; the lasagna plot
    did not, and carried "time / subject / value" across the corner of its own
    heatmap.

    Stated once in the painter rather than at each rewrite, because "did the
    author remember" is not a property a figure should depend on.
    """
    source = _backend_source()
    start = source.index("void QtPlotBackend::drawLegend(")
    body = _strip_comments(source[start:start + 2000])
    assert 'if(usesColourMap(spec.engine)&&!spec.engine.startsWith(QLatin1String("3D ")))' in body, (
        "a colour-mapped field draws a legend again, listing the columns it "
        "was built from beside its own colour bar")


def test_a_caterpillar_plot_is_packed_the_way_the_forest_painter_reads() -> None:
    """Twenty-four studies, three marks, all on row zero.

    The rewrite sorted the studies and returned three full-length COLUMNS with
    the engine set to Forest Plot. But a rewrite's output does not come back
    through prepareSpecCore, so drawForest received those columns raw - and it
    reads x as a single row position and y as {estimate, low, high}. It drew
    three bars on row zero out of the first three studies and discarded the
    rest: a handful of marks in the corner of an empty plot, with the sorting
    the engine exists for invisible.

    Recursing hands the sorted columns to the Forest branch, which packs one
    single-point series per row. Same packing, one copy of it.
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Caterpillar Plot")){')
    body = _strip_comments(source[start:start + 2200])
    assert "return prepareSpecCore(out);" in body, (
        "the caterpillar plot hands drawForest three raw columns again, so it "
        "draws three bars out of the first three studies and drops the rest")


def test_isocaps_fill_the_side_the_surface_encloses() -> None:
    """A solid cube with the isosurface invisible inside it.

    An isosurface at a level encloses the region where the field EXCEEDS it.
    The caps filled the cells BELOW it instead, which for a volume with its
    features in the middle is very nearly the whole wall.

    What made it survive is that the surface is unaffected either way: the
    boundary between the two regions is the same set of triangles, so the only
    thing that looked wrong was the caps - and a cap that covers everything
    looks like a cap that is working.
    """
    source = _backend_source()
    start = source.index("    if(caps){")
    body = _strip_comments(source[start:start + 2000])
    assert "if(!(v00>=iso&&v10>=iso&&v11>=iso&&v01>=iso)) continue;" in body, (
        "the isocaps fill the cells outside the enclosed region again, so the "
        "figure is a solid box")


def test_a_bubble_label_fits_inside_its_bubble() -> None:
    """A word lying across its own circle and its neighbours.

    A word cloud sizes its box to its text; a bubble cloud sizes its circle to
    its VALUE and then chose the text size from the same value independently.
    A small share with a long name came out clipped to "igna" with the ends
    outside the bubble, which reads as two different labels.

    The widest text a circle can hold is its inscribed square, d/sqrt(2).
    """
    source = _backend_source()
    start = source.index("            if(bubble){")
    body = _strip_comments(source[start:start + 2000])
    assert "const double usable=box.width()*0.70;" in body, (
        "the bubble's label is no longer measured against what the circle can "
        "hold")
    assert "ifm.elidedText(part.label,Qt::ElideRight,usable)" in body, (
        "a name that will not fit at the smallest readable size is drawn "
        "across the bubble instead of being elided")


def test_a_grid_sample_sits_at_the_centre_of_its_cell() -> None:
    """A four-class matrix drawn a quarter of a cell to the right.

    The grid's bounds were the data's own extremes, divided into equal bins
    starting at the minimum - which puts the first sample on the LEFT EDGE of
    the first cell and the last on the right edge of the last. On a
    hundred-and-sixty-cell heatmap that is invisible. On a confusion matrix of
    four classes it is a quarter of a cell, and the matrix engines set their
    axes to -0.5 .. n-0.5, which are cell EDGES - so the cells and the ticks
    described different places.

    The binning has to move with the bounds, or the picture is drawn from one
    grid and filled from another.
    """
    source = _backend_source()
    start = source.index("QtPlotBackend::ValueGrid QtPlotBackend::gridFromSeries(")
    body = _strip_comments(source[start:start + 5200])
    assert "const double halfX=(xHi-xLo)/(2.0*double(qMax(1,side-1)));" in body, (
        "the grid's bounds are the data extremes again, so every cell is drawn "
        "half a cell off the value it stands for")
    assert "g.xLo=xLo-halfX; g.xHi=xHi+halfX;" in body, (
        "the half-cell margin is computed and not applied")
    assert "int((xs[i]-g.xLo)/(g.xHi-g.xLo)*side)" in body, (
        "samples are binned against the data extremes while the cells are "
        "drawn against the grid's edges, so the two disagree")

def test_an_alluvial_stage_cannot_exceed_the_node_count() -> None:
    """Six nodes over thirty-seven stages.

    Stages come from a longest-path relaxation, capped at n passes so a cycle
    cannot loop forever. But a cap on the PASSES is not a cap on the ANSWER:
    around a cycle every pass pushes each node one stage further along, so six
    nodes with a cycle in them settled at thirty-seven stages. The diagram was
    laid out in thirty-seven columns, the nodes crowded into the last two, and
    nineteen twentieths of the canvas was empty. The report underneath said the
    graph has a cycle - true, and not the thing that made it unreadable.

    A longest path through n nodes visits at most n of them.

    And when the cycle means EVERY edge runs backwards, nothing is drawn at all
    - which was a row of bars against a blank canvas with the reason in small
    type along the bottom edge. That reads as a broken renderer. The same
    sentence in the middle reads as an answer.
    """
    source = _backend_source()
    start = source.index("void QtPlotBackend::drawAlluvial(")
    body = _strip_comments(source[start:start + 9000])

    assert "stage[e.to[k]]<qMin(n-1,stage[e.from[k]]+1)" in body, (
        "the alluvial stage is unbounded again, so a cycle spreads six nodes "
        "over dozens of empty columns")
    assert "backward==e.from.size()" in body, (
        "an alluvial that can draw no flow at all leaves a blank canvas with "
        "the reason in the footer")


def test_a_glyph_grid_is_not_an_axis() -> None:
    """An axis reading 0, 2, 4 under a row of glyphs.

    A star glyph plot and a waffle chart lay their marks out on a grid of
    POSITIONS, and the numbers on that grid are the positions themselves.
    Both turned off the gridlines and the scale labels, which left the box and
    its ticks - the two pieces of chrome that actually carry the numbers.
    """
    source = _backend_source()
    # Three rewrites say they bring their own coordinates: the ternary scatter,
    # the star glyph grid and the waffle chart. Counted rather than searched
    # for by engine name, because each says it at the end of its own branch and
    # the name is far enough above to be out of any sensible slice.
    assert source.count("        out.framed=false;\n        return out;") == 3, (
        "the number of rewrites declaring their own coordinates has changed - "
        "it was the ternary scatter, the star glyph plot and the waffle chart, "
        "and the last two lay their marks out on a grid of POSITIONS whose "
        "numbers are the positions themselves")


def test_a_bar_built_as_an_outline_is_filled() -> None:
    """A Pareto chart of hollow rectangles.

    Two engines build their bars as closed polylines on a Line Chart rather
    than through drawBar, because drawBar reads each series as a GROUP and
    stands them side by side - hand it a cumulative line and it comes back as a
    second row of bars. The blocks that construction produced were outlines,
    and the length of a bar is read from the ink in it. An abatement cost curve
    is worse still: its reading is the AREA of each block.
    """
    spec = (ROOT / "native/plot2d/include/PlotSpec.h").read_text(encoding="utf-8")
    assert "bool fillClosed = false;" in spec, (
        "a series can no longer say its closed outline encloses a solid")

    source = _backend_source()
    start = source.index("void QtPlotBackend::drawLineChart(")
    body = _strip_comments(source[start:start + 2200])
    assert "if(s.fillClosed&&path.elementCount()>2){" in body, (
        "drawLineChart strokes a closed block and never fills it")
    assert source.count("block.fillClosed=true;") == 2, (
        "the Pareto chart and the abatement cost curve no longer ask for their "
        "blocks to be filled")


def test_a_log_axis_of_many_decades_does_not_label_every_one() -> None:
    """Thirty-six numbers stacked into the height of the plot.

    A jitter bathtub runs from about 1e-2 down to 1e-36 - bit error rates
    really do span that - and every decade got its own label. Unreadable, and
    worse than unreadable: it looks like the renderer has failed rather than
    like an axis covering many decades.

    The TICK stays on every decade, because the gridlines are the sense of
    scale on a log axis; only the label is thinned.
    """
    source = _backend_source()
    start = source.index("QVector<AxisTick> QtPlotBackend::logTicks(")
    body = _strip_comments(source[start:start + 2600])

    assert "const int every=(span>12)?int(std::ceil(double(span)/12.0)):1;" in body, (
        "a log axis labels every decade again, so one spanning thirty-six of "
        "them is a column of numbers with no gaps")
    assert "ticks.append({major,QString(),true});" in body, (
        "an unlabelled decade is dropped rather than kept as a tick, so the "
        "gridlines that give a log axis its sense of scale go with the labels")

def test_two_quantities_of_different_scale_get_two_ordinates() -> None:
    """"voltage / power", "sCOD / removal %", "|Z| / phase".

    Five engines derived a second quantity from the same columns and drew it
    against the first one's axis. P = IV, so a cell working at a volt and tens
    of amps has a power in the tens and a voltage under one: the voltage curve
    - the thing a polarisation plot is FOR - became a flat line along the
    bottom, and the axis label named both quantities and measured neither.

    A scree plot worked around it by MULTIPLYING the cumulative share by the
    largest eigenvalue so that it fitted, and putting the apology in the axis
    title: "dashed: cumulative share, scaled". The number a scree plot is read
    for could not be read off the figure at all. A Pareto chart did the same
    with its running share.

    So the frame carries a second ordinate, measured only from the series that
    ask for it, drawn up the right-hand side with no gridlines of its own - a
    second set of horizontal lines through one plot area cannot be told from
    the first. The legend says which series belongs to it, because nothing else
    on the page does.
    """
    spec = (ROOT / "native/plot2d/include/PlotSpec.h").read_text(encoding="utf-8")
    assert "bool secondaryAxis = false;" in spec, (
        "a series can no longer ask for the right-hand ordinate")
    assert "PlotAxis y2Axis;" in spec, (
        "the spec no longer carries the right-hand ordinate's own label and "
        "log flag")

    header = (ROOT / "native/plot2d/include/QtPlotBackend.h").read_text(encoding="utf-8")
    assert "double y2Lo=0, y2Hi=1;" in header and "bool hasY2=false;" in header, (
        "the frame no longer carries a second ordinate's range")

    source = _backend_source()
    start = source.index("QtPlotBackend::Frame QtPlotBackend::computeRange")
    body = _strip_comments(source[start:source.index("\n    // A stacked band", start)])
    assert "double& loY=s.secondaryAxis?y2Lo:yLo;" in body, (
        "a series on the right-hand ordinate is measured into the left-hand "
        "range again, so the two quantities are back to fighting over one axis")

    # The painters have to follow, or the line and its markers part company.
    line = source.index("void QtPlotBackend::drawLineChart(")
    assert "toDeviceOn(f,x,y,s.secondaryAxis)" in source[line:line + 2400], (
        "drawLineChart maps every series against the left-hand ordinate again")
    marks = source.index("void QtPlotBackend::drawSeriesMarkers(")
    assert "toDeviceOn(f,x,y,s.secondaryAxis)" in source[marks:marks + 1600], (
        "the markers are mapped against the left-hand ordinate while the line "
        "is mapped against the right, so a series is drawn in two places")

    legend = source.index("void QtPlotBackend::drawLegend(")
    assert 'QStringLiteral("  (right)")' in source[legend:legend + 6000], (
        "the legend no longer says which series is read off the right-hand "
        "axis, so two ordinates are worse than the one crowded one they "
        "replaced")

    # And the engines that needed it. A census, so a new one is noticed and
    # has to be justified here rather than appearing quietly.
    #
    # Six, since the X-bar and R chart joined them. A bore turned to 48 mm has
    # a subgroup range near 0.9, and on one axis scaled 0 to 50 the means were
    # a flat line across the top and the ranges a flat line along the bottom:
    # neither chart could be read. The conventional answer is two stacked
    # panels, and the right-hand ordinate is what this renderer has to say it
    # with — including for the range LIMITS, which belong on the axis of the
    # thing they limit.
    assert source.count("secondaryAxis=true;") == 6, (
        "the set of engines using the right-hand ordinate has changed - it was "
        "the polarisation power, the sCOD removal percentage, the scree plot's "
        "cumulative share, the Pareto chart's running share and its 80% line, "
        "and the X-bar and R chart's range series")
    assert "ranges.secondaryAxis=true;" in source, (
        "the R chart is back on the mean chart's ordinate, where a range of "
        "0.9 is a flat line along the bottom of a chart of 48 mm bores")
    assert "level(QStringLiteral(\"range limits\"),d4*averageRange,in.style.positive,true,true);" in source, (
        "the range control limits are drawn against the means' scale again, so "
        "a limit of 1.9 is a line just above zero on a chart of 48 mm bores")
    assert "curve.secondaryAxis=(k>=2);" in source, (
        "a Bode plot's phase shares the magnitude's axis again - the one case "
        "where which channel goes right is decided by position rather than "
        "stated, because the first mapped channel is the magnitude and the "
        "second is the phase")
    # Comment-stripped: each of these survives in the note explaining why it
    # is gone, which is where it belongs.
    stripped = _strip_comments(source)
    for gone in ('QStringLiteral("voltage / power")',
                 'QStringLiteral("sCOD / removal %")',
                 'QStringLiteral("|Z| / phase")',
                 "dashed: cumulative share, scaled"):
        assert gone not in stripped, (
            f"{gone} is back: an axis label naming two quantities is an axis "
            "measuring neither")

def test_process_capability_bins_its_measurements() -> None:
    """A chart named for a distribution that drew a time series.

    It set the engine to "Histogram" and returned - but a rewrite's output does
    not come back through prepareSpecCore, so the branch that does the binning
    never ran. Two hundred and forty measurements came out as two hundred and
    forty bars at their own values: a solid block of ink in the shape of the
    data over time, with the two specification limits somewhere inside it.

    The limits reached from 0 to 1, which on an axis of counts running to fifty
    is a tick two pixels tall. And they were drawn as BARS, because drawBar
    drew everything as bars - so a rule marking the lower limit came out the
    same shape as the data it exists to be read against.
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Process Capability")){')
    body = _strip_comments(source[start:start + 4200])

    assert "out=prepareSpecCore(counted);" in body, (
        "the measurements are handed to drawBar unbinned again, so a "
        "capability chart draws the data over time")
    assert "counted.series={values};" in body, (
        "the specification limits are binned along with the measurements - "
        "they are not a column of observations")
    assert "line.y={0.0,tallest};" in body, (
        "the limits are drawn from 0 to 1 again, which against a count axis is "
        "a tick a few pixels tall")


def test_a_bar_chart_can_carry_a_reference_rule() -> None:
    """The same fix the scatter needed, one painter along.

    Thirteen rewrites append reference lines to a scatter and `drawScatter` was
    ignoring the flag; `drawBar` was doing the same to the specification limits
    a capability chart marks on its histogram.

    And it needs the same protection: PlotSeries' default for that flag is
    TRUE, so a caller that maps its columns and hands them over would get a
    line chart under the name "Bar". A Bar chosen from the library is bars
    whatever arrives.
    """
    source = _backend_source()
    start = source.index("void QtPlotBackend::drawBar(")
    body = _strip_comments(source[start:source.index("\n// ---", start)])
    assert "if(s.drawLine&&n>=2){" in body, (
        "drawBar draws a reference rule as bars again")

    head = _strip_comments(source[source.index("PlotSpec QtPlotBackend::prepareSpecCore"):
                                  source.index("PlotSpec QtPlotBackend::prepareSpecCore") + 6000])
    assert 'if(in.engine==QLatin1String("Bar")){' in head, (
        "a Bar chosen from the library keeps whatever drawLine its caller left "
        "set, so it can come out as a line chart")


def test_the_learning_and_validation_curves_answer_different_questions() -> None:
    """Two catalogue entries, one figure, and an axis label between them.

    Both drew the training and validation scores as two lines and differed only
    in what the x axis was called. They are not the same question: a learning
    curve asks whether more data would help and is read at the point the
    validation score stops climbing; a validation curve asks which value of the
    hyperparameter is best and is read at its peak.

    The gap between the two curves at that point is the other half of either
    reading - a model scoring 0.99 on its training set and 0.72 on held-out
    data is overfitting, and the number that says so is the difference.
    """
    source = _backend_source()
    start = source.index('const bool learning=(in.engine==QLatin1String("Learning Curve"));')
    body = _strip_comments(source[start:start + 4000])

    assert "if(valid.y[i]-valid.y[i-1]<rise*0.02){ mark=i; break; }" in body, (
        "a learning curve no longer finds its plateau, so it is a validation "
        "curve with a different axis label again")
    assert "plateau at %1; gap %2" in body, (
        "the learning curve no longer states where more data stops paying")
    assert "best at %1, score %2; gap %3" in body, (
        "the validation curve no longer states its best hyperparameter")

def test_a_plot_matrix_is_a_matrix() -> None:
    """A catalogue entry that drew a scatter of the first pair.

    It shared a branch with Scatter + Marginals and came out as one panel, on
    the reasoning that "an n x n grid of panels is n^2 figures and there is one
    frame". That was true of the FRAME machinery and not of the canvas -
    drawScatterMarginals was already subdividing the plot area to put a
    distribution along each edge, and a matrix is the same idea on a grid.

    The layout is what makes it scannable: scatters below the diagonal, each
    variable's distribution ON it, and the correlation for each pair above.
    A reader sweeps the upper triangle for a number that stands out and looks
    at the panel opposite. Each panel is drawn against its own two ranges,
    because panels sharing one range would be a matrix of one variable's units.
    """
    header = (ROOT / "native/plot2d/include/QtPlotBackend.h").read_text(encoding="utf-8")
    assert "void drawPlotMatrix(QPainter* p, const QRectF& target" in header, (
        "the plot matrix has no painter of its own again, so it is a scatter "
        "of the first pair under a different title")

    source = _backend_source()
    start = source.index("void QtPlotBackend::drawPlotMatrix(")
    body = _strip_comments(source[start:source.index("\nvoid QtPlotBackend::drawAlluvial", start)])

    assert "if(row==col){" in body, (
        "the diagonal no longer shows each variable's own distribution")
    assert "if(row<col){" in body and 'QString::number(r,\'f\',2)' in body, (
        "the upper triangle no longer carries the correlation, which is the "
        "number a matrix is scanned for")
    assert "span[col].lo" in body and "span[row].lo" in body, (
        "the panels are drawn against a shared range again, which makes the "
        "matrix a picture of one variable's units")

    # Six variables is 36 panels; past that the panels shrink faster than the
    # figure grows.
    assert 'if(engine==QLatin1String("Plot Matrix")) return {2,6,true};' in source, (
        "the plot matrix is back to two mapped columns, which is a matrix of "
        "one pair")
    assert 'engine!=QLatin1String("Plot Matrix")' in source, (
        "a rectangular frame is drawn round the matrix - an axis for none of "
        "its panels")

def test_a_rug_plots_rows_are_rows() -> None:
    """An axis labelled "series" over the numbers 0.09 and 0.27.

    The rows were stacked at whatever three per cent of each column's own range
    came to, accumulated - so the vertical position was an arithmetic artefact
    of the data's spread and the axis was labelled for something else entirely.

    A rug's vertical position carries no quantity at all. It is a row, and the
    only useful thing about a row is whose it is.
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Rug Plot")){')
    body = _strip_comments(source[start:start + 3000])

    assert "const double slot=double(slotIndex);" in body, (
        "the rug's rows are stacked by data range again, so their positions "
        "are an artefact of how spread each column happens to be")
    assert "out.yAxis.tickLabels.append(s.label);" in body, (
        "the rows are numbered rather than named, and a row's position means "
        "nothing else")
    assert "out.yAxis.tickValues=values;" in body, (
        "the axis is reassigned after the tick labels are appended, which "
        "drops them")

def test_a_polar_figure_offers_its_convention_and_nothing_else_does() -> None:
    """The setting, where the person can reach it.

    A per-figure convention that only the C++ knows about is a constant with
    extra steps. It has to travel: canvas property, controller setting, and a
    control the operator can see - but ONLY on the figures it means something
    for. An `Angles` box on a bar chart is a control that does nothing, which
    costs more than it saves: it is one more thing to try before believing it.

    `polarEngine` is what gates it, and it has to come from the same list the
    painter dispatches on - a second list is how Stereonet ends up dispatching
    to the polar painter while the sidebar says it is not a polar figure.
    """
    canvas = (ROOT / "native/plot2d/include/PlotCanvas.h").read_text(encoding="utf-8")
    assert "polarConvention" in canvas and "polarEngine" in canvas, (
        "the canvas no longer carries the polar convention or says whether "
        "this engine is a polar one, so QML cannot offer the setting")

    backend = _backend_source()
    assert "bool QtPlotBackend::isPolarEngine(" in backend, (
        "the list of polar engines is no longer shared, so the painter's "
        "dispatch and the sidebar's question can answer differently")
    assert "else if(isPolarEngine(spec.engine))" in backend, (
        "the painter's dispatch no longer asks the shared list, so it is "
        "keeping its own idea of which engines are drawn in a circle")
    assert "QtPlotBackend::isPolarEngine(spec_.engine)" in canvas, (
        "the canvas answers polarEngine from a list of its own, so the "
        "sidebar can say a figure is not polar while the painter draws it "
        "in a circle - which is exactly how Stereonet was lost before")

    bar = (ROOT / "app/qml/components/FigureAppearanceBar.qml").read_text(encoding="utf-8")
    assert "root.canvas.polarEngine" in bar, (
        "the Angles control is no longer gated on the engine being a polar "
        "one, so it shows on every figure and does nothing on almost all")
    assert "plotPolarConvention" in bar, (
        "the Angles control is no longer wired to the persisted setting")

def test_a_lollipop_is_not_a_stem_plot_under_another_name() -> None:
    """Two catalogue entries, 0.03 grey levels apart out of 255.

    A perceptual pass over the whole 434-figure gallery - 64x40 greyscale,
    title cropped, mean absolute difference - put Stem and Lollipop closer
    together than any pair that was not literally the same painter on the same
    data. They were: the Lollipop rewrite asked for a 5pt marker against the
    stem plot's 4pt default and said nothing about the stalk, so the whole of
    the distinction between the two entries was half a pixel of radius.

    A lollipop reads as a HEAD ON A STALK - the value lives in the disc and the
    stalk only ties it to the baseline. A stem plot is a line whose end happens
    to be marked. That difference has to be big enough to see from across the
    figure, which is why the numbers below are multiples and not increments.
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Lollipop")){')
    body = _strip_comments(source[start:start + 1800])

    assert "s.markerSize=qMax(s.markerSize,11.0);" in body, (
        "a lollipop's head is no longer several times the stem plot's marker, "
        "so the two catalogue entries are the same picture again")
    assert "s.lineWidth=qMax(s.lineWidth,3.0);" in body, (
        "a lollipop's stalk is back at the stem plot's hairline, so the only "
        "thing telling the two figures apart is the size of the dot")
    assert "s.markerSizeExplicit=true;" in body and "s.lineWidthExplicit=true;" in body, (
        "the lollipop's head and stalk are no longer marked as deliberate, so "
        "a publication profile will treat them as untouched defaults and "
        "shrink them back to the stem plot's")


def test_zero_pins_the_x_axis_only_where_zero_is_the_baseline() -> None:
    """A pin written as a test on the number, not on the engine.

    The x pad skips the low end when `xLo == 0`, so that a horizontal bar is
    not lifted off the axis its lengths are measured from. That is right for a
    horizontal bar and wrong for everything else whose first point happens to
    sit at zero - a stem plot's sample index, a scatter's first category, a
    time axis measured from the start of the run. Zero is where their data
    begins, not what it is measured from.

    It showed as soon as the lollipop's head got big enough to see: the mark at
    x = 0 was drawn straddling the frame line and came out a semicircle. Same
    shape of fault as the frame pairing and the bar slot division - a rule
    correct for one class, applied to all of them by a test that never names
    the class.
    """
    source = _backend_source()
    start = source.index("    // AND THE SAME FOR X, which had none.")
    body = _strip_comments(source[start:start + 2600])

    assert "const bool pinnedLow=(zeroOnX&&xLo==0.0);" in body, (
        "the x pin is back to asking only whether the bound is zero, so every "
        "figure whose data starts at zero has its leftmost mark drawn half "
        "outside the frame")
    assert "const bool pinnedHigh=(zeroOnX&&xHi==0.0);" in body, (
        "the high end of the x pin no longer asks whether zero is this "
        "engine's baseline")

    clamp = _strip_comments(source)
    assert 'const bool zeroOnX = spec.engine==QLatin1String("Horizontal Bar");' in clamp, (
        "the engines that measure along x from zero are no longer named in "
        "one place, so the clamp and the pin can disagree about which they are")
    assert "if(zeroOnX&&!f.xLog){ xLo=qMin(xLo,0.0); xHi=qMax(xHi,0.0); }" in clamp, (
        "the zero clamp on x no longer reads the shared answer")


def test_the_geographic_engines_are_demonstrated_on_coordinates() -> None:
    """Eight map engines, and a decaying sine where the longitude should be.

    The sweep hands every engine without a fixture the shared columns, and the
    geographic ones had no fixture — so `signal`, which is 2 + 3(1 - e^-t/2)
    plus a small sine, was being projected as a longitude. Every map in the
    catalogue was the same three-degree squiggle, and Geo Line, Ground Track
    and Great Circle Route came out as literally the same picture: 0.000 and
    0.002 grey levels apart out of 255, the closest pair in the gallery.

    A catalogue figure exists to show what an engine does. Demonstrating one on
    data that cannot reach any of its behaviour is the same failure as the
    engine not having the behaviour — which is why this was not left as an
    adjudication saying "these longitudes span three degrees so nothing crosses
    the antimeridian".
    """
    source = _selftest_source()

    assert 'QStringLiteral("Great Circle Route"),{column("longitude",routeLon)' in source, (
        "the great circle route is no longer demonstrated on a route, so it "
        "is a Geo Line again")
    assert 'QStringLiteral("Ground Track"),{column("longitude",trackLon)' in source, (
        "the ground track is no longer demonstrated on a ground track")
    assert 'QStringLiteral("Geo Bubble"),{column("longitude",siteLon)' in source, (
        "the bubble map has lost its third column, so there is nothing for it "
        "to size its bubbles by")

    body = _strip_comments(source[source.index("QVector<double> trackLon,trackLat;"):
                                  source.index("const QHash<QString,QVector<PlotSeries>> shaped{")])
    assert "while(lon>180.0) lon-=360.0;" in body and "while(lon<-180.0) lon+=360.0;" in body, (
        "the ground track's longitude is no longer wrapped into [-180, 180], "
        "so it never crosses the antimeridian and the cut that distinguishes "
        "the engine from a Geo Line has nothing to do")

    route = source[source.index("QVector<double> routeLon"):
                   source.index("QVector<double> pathLon")]
    assert "139.69" in route and "51.51" in route, (
        "the route no longer spans a hemisphere, so the great circle and the "
        "straight lon/lat line are the same curve to within a pixel")


def test_a_great_circle_says_what_it_saved() -> None:
    """A curve on its own is not an argument.

    The entry exists because the obvious line — straight in longitude and
    latitude, which is what plotting the two columns gives — is longer. Drawn
    alone the great circle is just a curve, and the reader cannot see what it
    is shorter than. So the straight one goes beside it, dashed, and both
    legends carry their length on the ground.
    """
    source = _backend_source()
    start = source.index("        if(in.engine==QLatin1String(\"Great Circle Route\")&&n>=2){")
    body = _strip_comments(source[start:start + 2600])

    assert "direct.dashPattern={5.0,4.0};" in body, (
        "the straight lon/lat line is no longer dashed, so the figure shows "
        "two solid routes and does not say which one is the engine's answer")
    assert "flat+=greatCircleMetres(pLon,pLat,qLon,qLat);" in body, (
        "the straight line's length is measured in degrees again rather than "
        "on the ground, so the number beside it is not comparable with the arc")
    assert 'QStringLiteral("great circle, %1 km")' in body, (
        "the great circle no longer states its length")
    assert 'QStringLiteral("straight in lon/lat, %1 km")' in body, (
        "the line being beaten no longer states its length, so the saving is "
        "a shape the reader has to estimate")


def test_a_bubble_map_sizes_its_bubbles_by_the_column_it_was_given() -> None:
    """A third column, read by nothing.

    Geo Bubble set one marker size for the whole map, so it was a Geo Scatter
    drawn slightly heavier — 0.09 grey levels apart in the perceptual pass —
    and the magnitude column it is handed was never read.

    Marker size is a property of a series, not of a point, so the honest way to
    vary it is graduated symbols: classes across the range, one series each,
    and a legend that says what each size is worth. Area, not radius: a circle
    twice the radius carries four times the ink, and a reader compares ink.
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Geo Bubble")&&in.series.size()>=3){')
    body = _strip_comments(source[start:start + 3000])

    assert "bucket.markerSize=2.0*std::sqrt(4.0+double(k)*26.0);" in body, (
        "the bubble sizes are back to growing with the value rather than with "
        "its square root, so a doubling is drawn as a quadrupling")
    assert "const bool last=(k==classes-1);" in body, (
        "the top class no longer takes its own upper bound, so the largest "
        "point on the map - the one the reader looks for - is the one missing")
    assert 'bucket.label=QStringLiteral("%1 %2-%3").arg(unit)' in body, (
        "the size classes no longer say what they are worth, so the map has "
        "five sizes and no way to read any of them")


def test_a_legend_key_is_a_picture_of_what_the_series_draws() -> None:
    """Every row drew an 18-pixel stroke, whatever the series was.

    On a scatter that is a line the figure does not contain. On the graduated
    symbol map it is worse than absent: the sizes ARE the reading, and the key
    showed five identical strokes against five different size classes. On a bar
    chart it is a hairline standing for a solid block of colour.

    So the key draws what the series draws — a line where there is a line, a
    dot at the series' own marker size where there are markers, and a filled
    block where the series is a solid. The row and the swatch column grow with
    the largest marker they have to show, because a 14pt bubble in an 18-pixel
    slot is clipped to a sliver and reads as the same size as the 10pt one.
    """
    source = _backend_source()
    start = source.index("void QtPlotBackend::drawLegend(")
    body = _strip_comments(source[start:source.index("\nvoid QtPlotBackend::drawLineChart", start)])

    assert "const double swatchW=qBound(18.0,keyMark+6.0,34.0);" in body, (
        "the swatch column no longer widens for the markers it has to show, "
        "so the larger size classes are clipped to the same width and the "
        "graduated key stops being readable")
    assert "const double rowH=qMax(fm.height()+3,qMin(keyMark,26.0)+4.0);" in body, (
        "the legend rows no longer make room for their markers, so a large "
        "bubble is drawn taller than the row and clipped by the one above")
    assert "if(s->drawMarkers){" in body and "p->drawEllipse(QPointF((x0+x1)/2.0,cy),r,r);" in body, (
        "a series drawn as points no longer gets a point in the key, so every "
        "scatter's legend shows a line the figure does not contain")
    assert "if(!s->drawLine&&!s->drawMarkers){" in body, (
        "a series that is a solid - a bar, a band, a wedge - no longer gets a "
        "filled swatch, so its key is a stroke standing for a block of colour")
    assert "p->setOpacity(qBound(0.15,s->opacity,1.0));" in body, (
        "the key no longer carries the series' own opacity, so a translucent "
        "overlay is keyed by an opaque swatch in a colour that is nowhere in "
        "the picture")


def test_the_engines_that_read_a_point_are_given_points() -> None:
    """Four aviation and wind engines, fitted against a row index.

    `column` numbers its rows, which is right for an engine that reads a list
    of values and wrong for one that reads a point out of a single series' own
    x and y — a drag polar reads (CD, CL), an envelope reads (speed, load
    factor), a wind rose reads (bearing, magnitude). Handed a numbered column
    the drag polar's parabola fit returned a negative zero-lift drag, failed
    its own sanity test, and fell through to drawing the raw points: the
    figure was a 4D/5D Scatter of the shared columns, 0.8 grey levels from the
    actual 4D/5D Scatter entry beside it in the gallery.

    The engine was right to refuse. It was being asked to fit a signal against
    its own subscript. With real coefficients the fit recovers CD0 0.0208 and
    k 0.0479 from data generated at 0.021 and 0.047, which is the check that
    the engine and the fixture now agree about what the columns mean.
    """
    source = _selftest_source()
    assert "const auto paired=[](const QString& label,const QVector<double>& xs," in source, (
        "the fixture helper that builds a series from two measured columns is "
        "gone, so every point-reading engine is back to fitting against a row "
        "index")

    for engine, fixture in (("V-n Flight Envelope", "vnV,vnN"),
                            ("Altitude-Mach Envelope", "amM,amAlt"),
                            ("Drag Polar", "polarCD,polarCL"),
                            ("Wind Rose", "windDir,windSpeed"),
                            ("Polar Histogram", "windDir,windSpeed")):
        assert f'QStringLiteral("{engine}"),{{paired(' in source and fixture in source, (
            f"{engine} is no longer demonstrated on the quantities it reads, "
            "so its figure is the shared signal columns under a new title")

    wind = source[source.index("QVector<double> windDir,windSpeed;"):
                  source.index("const QHash<QString,QVector<PlotSeries>> shaped{")]
    assert "const bool gale=(v>0.78);" in wind, (
        "the wind fixture no longer has a second, stronger population, so the "
        "direction the wind blows most often and the direction it blows "
        "hardest are the same - and a wind rose weighted by magnitude draws "
        "the same picture as a polar histogram that counts, which is the one "
        "thing telling the two catalogue entries apart")


def test_a_fixture_of_its_own_keeps_its_own_axis_names() -> None:
    """A drag coefficient on an axis labelled "time_h".

    The sweep set both axis labels to the shared time base for every engine,
    including the ones given a fixture of their own. Each engine sets its own
    names only `if(label.isEmpty())` — which is correct, because in the
    application the label is the name of the column the person mapped, and an
    engine must not overwrite a person's own heading. So the sweep was
    supplying a name for a column it had not supplied, and the catalogue
    figure said the abscissa was time when it was a drag coefficient.
    """
    source = _selftest_source()
    start = source.index("        PlotSpec spec;\n        spec.engine=engine;")
    body = _strip_comments(source[start:start + 2200])

    assert "const bool ownData=shaped.contains(engine);" in body, (
        "the sweep no longer distinguishes an engine running on its own "
        "fixture from one running on the shared columns")
    assert "if(!ownData){" in body, (
        "the shared time base is being stamped on engines that were given "
        "their own data, so their axes are labelled with a column the sweep "
        "never handed them")
    assert "spec.series=ownData?shaped.value(engine)" in body, (
        "the fixture lookup and the axis-label decision no longer ask the "
        "same question, so they can disagree about which engines have data "
        "of their own")


def test_a_comet_fades() -> None:
    """A 3-D Line with a dot on the end.

    The trail was stroked at one uniform opacity, so the figure showed where
    the path went and said nothing about which end of it is now — and the head,
    whose whole job is to mark the present, had nothing to be meaningful
    against. The perceptual pass put Comet 3D and 3D Line 0.13 grey levels
    apart out of 255.

    The first ramp written here was squared, which looked right in the
    arithmetic and threw the oldest two thirds of the trail away: the figure
    came back as a dot in an empty cube, a comet that has lost the path it is
    travelling along. Linear, from a floor that stays visible, and carried
    partly by the width so the fade survives being printed at 89 mm.
    """
    source = _backend_source()
    start = source.index("        if(comet||ribbon){")
    body = _strip_comments(source[start:start + 6000])

    assert "p->setOpacity(0.18+0.77*age);" in body, (
        "the comet's trail is back to one uniform opacity, so Comet 3D is a "
        "3D Line with a dot on the end")
    assert "pen.setWidthF(qMax(0.6,spec.style.lineWidth*(0.55+1.05*age)));" in body, (
        "the trail no longer thins towards its tail, so the fade is opacity "
        "alone on a line that was already a hairline")
    assert "constexpr int kSegments=8;" in body, (
        "the trail is no longer drawn in segments, so there is nothing for "
        "the ramp to be applied to")


def test_the_engines_whose_content_needs_coarse_data_get_coarse_data() -> None:
    """Two step charts at 240 samples, and a swarm with no ties to spread.

    Stairs and Step Mid differ by half a sample — Step Mid changes level midway
    between samples rather than at one, which is the honest reading when a
    value is a measurement at a point rather than a level that held. On the
    shared 240-point base over twelve units that difference is a quarter of a
    pixel, and both figures were smooth curves with no visible tread at all.

    3D Swarm fans rows that share a height apart so a pile of equal readings
    becomes a shape. The shared columns are a smooth curve where almost no two
    rows share a height, so almost nothing was nudged and the figure was a
    3D Scatter.

    Neither engine was at fault. A demonstration at a sample rate that hides
    the engine's content is the same failure as the engine not having it.
    """
    source = _selftest_source()

    assert 'QStringLiteral("Stairs"),{paired("tariff band",stepT,stepLevel)' in source, (
        "the step charts are back on the 240-point shared base, where a tread "
        "is half a pixel wide and both figures are smooth curves")
    assert 'QStringLiteral("Step Mid"),{paired("tariff band",stepT,stepLevel)' in source, (
        "Step Mid no longer shares the coarse fixture, so the half-sample "
        "shift that is the whole difference between it and Stairs is invisible")
    assert 'QStringLiteral("3D Swarm"),{column("level",swarmX)' in source, (
        "the swarm has no ties to spread again, so it draws a 3D Scatter")

    step = source[source.index("QVector<double> stepT,stepLevel,stepSecond;"):
                  source.index("QVector<double> swarmX,swarmY,swarmZ;")]
    assert "for(int i=0;i<12;++i){" in step, (
        "the step fixture is no longer coarse, so the treads close up again")

    swarm = source[source.index("QVector<double> swarmX,swarmY,swarmZ;"):
                   source.index("const QHash<QString,ShapedAxes> shapedAxes{")]
    assert "for(int level=0;level<5;++level){" in swarm and "for(int k=0;k<40;++k){" in swarm, (
        "the swarm fixture no longer piles many readings onto few levels, so "
        "there is nothing for the engine to fan apart")


def test_a_fixture_that_does_not_name_its_own_axes_says_what_they_are() -> None:
    """Axes with no names at all.

    Suppressing the shared "time_h"/"value" for an engine with its own fixture
    is right for the engines that name their own axes — a drag polar knows its
    abscissa is a drag coefficient — and left the plain ones with no axis names
    whatever, which is a figure that does not say what it is a figure of.

    An engine is only entitled to overwrite a caller's label when it has
    computed a new quantity. The rest take the name of the column they were
    handed, so the fixture is what has to supply it.
    """
    source = _selftest_source()
    assert "QHash<QString,ShapedAxes> shapedAxes;" in source, (
        "the sweep inputs no longer carry axis names for the fixtures, so an "
        "engine that does not name its own axes draws a figure with none")
    assert '{QStringLiteral("Stairs"),{QStringLiteral("hour"),' in source, (
        "the step chart's own fixture no longer says what its axes are")

    start = source.index("        const bool ownData=shaped.contains(engine);")
    body = _strip_comments(source[start:start + 700])
    assert "}else if(shapedAxes.contains(engine)){" in body, (
        "the sweep no longer applies a fixture's own axis names, so every "
        "plain engine given data of its own draws unlabelled axes")


def test_the_figures_drawn_in_a_circle_are_given_bearings() -> None:
    """Five polar entries, all pointing the same way.

    Polar Line, Polar Scatter, Polar Bubble, Compass and Stereonet read the
    shared `signal` column as an angle in DEGREES. It runs from about 2 to 5,
    so every point in every one of those figures lay in a three-degree sliver
    just above due east: the catalogue showed five circles with a smear against
    one edge, Polar Line and Polar Scatter came out 0.31 grey levels apart, and
    the Compass drew a dozen arrows all pointing at the same target.

    A cardioid for the line, because a radiation pattern is what someone
    choosing "Polar Line" is picturing — the whole circle used, a maximum, a
    null, and a front-to-back ratio that can be read off the figure.
    """
    source = _selftest_source()

    for engine, needle in (
            ("Polar Line", 'paired("cardioid",lobeAngle,lobeCardioid)'),
            ("Polar Scatter", 'paired("contacts",bearing,range)'),
            ("Polar Bubble", 'column("strength",strength)'),
            ("Compass", 'paired("surface current",currentDir,currentSpeed)'),
            ("Stereonet", 'column("dip direction",dipDir)')):
        assert f'QStringLiteral("{engine}"),{{' in source and needle in source, (
            f"{engine} is back on the shared signal column as its bearing, so "
            "every point in it lands in a three-degree sliver east of centre")

    compass = source[source.index("QVector<double> currentDir,currentSpeed;"):
                     source.index("QVector<double> dipDir,dipAngle;")]
    assert "for(int i=0;i<12;++i){" in compass, (
        "the compass fixture is no longer a dozen vectors, and any more than "
        "that draws a disc rather than a rose that can be read one arrow at "
        "a time")

    net = source[source.index("QVector<double> dipDir,dipAngle;"):
                 source.index("const QHash<QString,ShapedAxes> shapedAxes{")]
    assert "dipAngle.append(12.0+68.0*u);" in net, (
        "the stereonet's dips no longer span shallow to steep, so its poles "
        "land in one ring instead of across the net")


def test_a_streamline_says_which_way_it_goes_and_does_not_wrap() -> None:
    """A set of curves that describes a flow and its exact reverse equally well.

    Nothing on the page chose between them. It mattered most on the phase
    portrait, where a stable spiral and an unstable one draw identical
    trajectories and differ only in which way they are travelled — so the
    classification in the labels was asserting something the picture did not
    show.

    The heads were added first and could not be seen, which turned out to be
    the larger fault: the integration ran 160 steps of six tenths of a cell,
    about fourteen units of arc, and the demonstration field is three and a
    half units across. Every streamline wrapped three times round the same
    closed orbit and the middle of the figure went solid. A length measured in
    the domain rather than in steps fixes both.
    """
    source = _backend_source()
    start = source.index("    if(streaming){")
    body = _strip_comments(source[start:start + 7000])

    assert "const double maxArc=0.45*std::hypot(gu.xHi-gu.xLo,gu.yHi-gu.yLo);" in body, (
        "the streamline length is measured in steps again rather than in the "
        "domain, so on a small domain every line wraps and the field goes "
        "solid in the middle")
    assert "if(!texture&&travelled>=maxArc) break;" in body, (
        "nothing stops a streamline at its arc budget, so the budget is a "
        "number that is computed and not used")
    assert "if(!texture&&!haveHead&&started&&travelled>=maxArc*0.5){" in body, (
        "the direction head is placed by step count again, which on a line "
        "that stops early is past its end - so the lines that stop early, "
        "which is most of them, get no head at all")
    assert "p->drawPolygon(head);" in body, (
        "the streamlines have no direction on them, so the figure describes "
        "the flow and its exact reverse equally well")


def test_the_curl_maps_are_shown_a_field_that_has_some() -> None:
    """A divergence map of a constant.

    The Duffing system is the right field for a phase portrait — three fixed
    points, one of each kind the painter classifies — and the wrong one for
    these two maps: its divergence is du/dx + dv/dy = 0 + (-0.2), the same
    number everywhere. The Divergence Map came out one flat colour across the
    whole plot area, which is a correct picture of a constant and a catalogue
    entry that says nothing about the engine.

    A radial source at the origin, whose divergence changes sign at r = 1, and
    an off-centre vortex whose vorticity sits where it is. One field, both maps
    with structure, and no change to either engine's arithmetic.
    """
    source = _selftest_source()
    assert 'QStringLiteral("Divergence Map"),\n            {column("x",curlX)' in source, (
        "the divergence map is back on the Duffing field, whose divergence is "
        "the same number everywhere - so the figure is one flat colour")
    assert 'QStringLiteral("Vorticity Map"),\n            {column("x",curlX)' in source, (
        "the vorticity map no longer has a field with a vortex in it")

    field = source[source.index("QVector<double> curlX,curlY,curlU,curlV;"):
                   source.index("// A SCALAR VOLUME, for the family")]
    assert "curlU.append(x*spread-vy*swirl*1.6);" in field, (
        "the field has lost its radial part, so there is nothing for the "
        "divergence map to show")
    assert "curlV.append(y*spread+vx*swirl*1.6);" in field, (
        "the field has lost its vortex, so there is nothing for the vorticity "
        "map to show")


def test_youngs_modulus_is_fitted_over_the_elastic_region() -> None:
    """A number labelled E that was not Young's modulus, to four figures.

    The elastic region was taken as "everything below a tenth of the strain
    range", which is not the elastic region of anything ductile: a tensile test
    that necks at 20% strain yields at about 0.4%, so the fit swept in every
    point up to 2% strain — the whole elastic region and most of the
    work-hardening plateau — and returned 17 GPa for a material with a real
    modulus of 70. The figure printed it as "E 1.686e+04".

    Below forty per cent of the ultimate stress is the usual laboratory rule
    and it is a rule about the right axis. A ductile metal is still elastic
    there whatever its ductility; a brittle one has almost no plastic range to
    sweep in by mistake.
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Stress-Strain Curve")){')
    body = _strip_comments(source[start:start + 5000])

    assert "const double knee=(peak>0)?peak*0.4:0.0;" in body, (
        "the elastic region is measured on the strain axis again, so the "
        "reported modulus includes the plastic range and is several times too "
        "small")
    assert "while(elastic<curve.y.size()&&curve.y[elastic]<=knee) ++elastic;" in body \
        or "while(elastic<curve.x.size()&&curve.y[elastic]<=knee) ++elastic;" in body, (
        "the stress rule is computed and not applied")
    assert "if(elastic<3){" in body, (
        "there is no fallback for a curve too coarse for the stress rule, so "
        "a three-point tensile test fits a line through fewer than two points")


def test_the_offset_construction_line_stops_where_it_is_used() -> None:
    """A construction line that set the axis of the figure it explains.

    Carried across the whole strain range, a 70 GPa 0.2% offset line reaches
    14,000 MPa on a specimen that breaks at 410 — so the construction line set
    the y axis and the curve was a flat trace along the bottom of the frame.
    The line exists to be intersected; a little past the intersection it has
    nothing left to say.
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Stress-Strain Curve")){')
    body = _strip_comments(source[start:start + 5000])

    assert "qMin(strainHi,offset+1.15*peak/line.slope)" in body, (
        "the 0.2% offset line runs the full strain range again, so on a stiff "
        "specimen it is thirty times taller than the curve and owns the axis")
    assert "offsetLine.x={offset,stopAt};" in body, (
        "the offset line's end is computed and not used")


def test_the_engines_that_fit_a_named_law_are_shown_that_law() -> None:
    """Constants quoted to four significant figures, from a decaying sine.

    Each of these engines fits a named physical law and puts the recovered
    constants in the legend, which is the most quotable thing on a figure. On
    the shared columns those constants were fabricated: "Ea 0.0 kJ/mol",
    "EC50 1.386, Hill 1.26" on data with no dose in it, "Vmax 4.598, Km 0.104"
    on a sine, "max crosswind 0.0" on a wind field three thousandths of a knot
    across, a weight and balance envelope reporting 240 loadings out of limits.

    The fixtures below are drawn FROM the law each engine fits, so the number
    in the legend can be checked against the number the data was made with.
    That check is the point of them, and it caught three faults the figures
    alone did not show: the modulus fitted over the plastic range, the S-N
    Basquin exponent fitted through the endurance limit, and the rating curve
    fitted without the datum offset it had been given.
    """
    source = _selftest_source()

    for engine, needle in (
            ("Payload-Range Diagram", 'paired("payload",prRange,prPayload)'),
            ("Lift Curve", 'paired("clean",liftAlpha,liftCl)'),
            ("Flight Profile", 'paired("cruise profile",profMinute,profAltitude)'),
            ("Runway Crosswind", 'column("wind speed (kt)",rwSpeed)'),
            ("Weight and Balance Envelope", 'paired("utility envelope",wbArm,wbWeight)'),
            ("Weibull Probability Plot", 'column("bearing life (h)",weibullLife)'),
            ("Reliability Growth", 'column("cumulative test hours",growthTime)'),
            ("MTBF Trend", 'column("fleet",mtbfTime)'),
            ("S-N Fatigue Curve", 'paired("6061-T6",snCycles,snStress)'),
            ("Bathtub Curve", 'column("service life (h)",bathtubLife)'),
            ("Stress-Strain Curve", 'paired("6061-T6",ssStrain,ssStress)'),
            ("Arrhenius Plot", 'paired("hydrolysis",arrTemp,arrRate)'),
            ("Titration Curve", "titrantVol,titrantPh"),
            ("Calibration Curve", 'paired("standards",calConc,calSignal)'),
            ("Michaelis-Menten", 'paired("alkaline phosphatase",mmSubstrate,mmRate)'),
            ("Dose-Response Curve", 'paired("compound A",doseConc,doseEffect)'),
            ("Rating Curve", 'paired("gauging station",gaugeStage,gaugeFlow)'),
            ("Wind Power Curve", 'paired("2.3 MW turbine",windSpeedMs,windPower)')):
        assert f'QStringLiteral("{engine}"),' in source and needle in source, (
            f"{engine} is back on the shared signal columns, so the constants "
            "in its legend are fitted to a decaying sine and quoted as though "
            "they were measurements")

    # The constants themselves, because a fixture that no longer contains the
    # law is a fixture the engine cannot be checked against.
    assert "4.1e9*std::exp(-72500.0/(8.314462618*T))" in source, (
        "the Arrhenius fixture no longer has a known activation energy, so "
        "nothing checks the Ea the engine reports")
    assert "8.4*s/(0.42+s)" in source, (
        "the kinetics fixture no longer saturates to a known Vmax and Km")
    assert "92.0/(1.0+std::pow(35.0/d,1.4))" in source, (
        "the dose-response fixture no longer has a known EC50 and Hill slope")
    assert "12.4*std::pow(h,2.1)" in source, (
        "the rating fixture no longer follows the power law the engine fits - "
        "an offset the engine has no term for makes the figure a "
        "demonstration of a model mismatch")
    assert "70000.0*e" in source, (
        "the tensile fixture no longer has a known Young's modulus")

    ss = source[source.index("QVector<double> ssStrain,ssStress;"):
                source.index("QVector<double> arrTemp,arrRate;")]
    assert "for(int i=0;i<=24;++i){" in ss, (
        "the tensile fixture no longer samples the elastic region densely, so "
        "the modulus is fitted from two or three points and the figure "
        "demonstrates a sampling artefact")

    ph = source[source.index("QVector<double> titrantVol,titrantPh;"):
                source.index("QVector<double> calConc,calSignal;")]
    assert "constexpr double pKa=4.76" in ph, (
        "the titration is back to a strong acid, whose half-equivalence pH "
        "the engine still reports as a pKa - a number with no referent")


def test_the_engines_that_read_a_record_are_given_records() -> None:
    """Twelve engines reading a shape the shared columns do not have.

    A date and a count; a row and the two ends of a bar; four price columns; an
    origin and a destination. Handed five signal columns each of them drew
    something — a Gantt schedule of 240 overlapping bars in a fan, an OHLC
    chart of 240 candles a pixel wide, a borehole log of 240 beds, an
    availability timeline reporting uptime computed from a sine.

    Three of these fixtures were wrong on the first try and the figures said
    so, which is the argument for checking a fixture against the engine's own
    arithmetic rather than against whether the picture looks busy:

    * the traffic counts supplied SPEED where the engine reads FLOW, so it
      fitted a parabola to a straight line, reported a free-flow speed of 8.4
      against 104, and drew the flow curve peaking at 215 on an axis of speeds
      that never exceed 104;
    * the psychrometric states supplied the humidity RATIO where the engine
      reads relative HUMIDITY — the unit on an axis is not always the unit of
      the column that produced it — so every state was read as a few per cent
      RH and sat along the floor of the chart;
    * the borehole had one track, which is a correct log and a three-pixel-wide
      figure, when the reason a log is drawn rather than tabulated is that beds
      are correlated across holes.
    """
    source = _selftest_source()

    for engine, needle in (
            ("Calendar Heatmap", 'paired("commits",calDay,calCount)'),
            ("Rainflow Matrix", 'column("load (MPa)",loadHistory)'),
            ("Gantt Schedule", 'column("task",taskRow)'),
            ("Availability Timeline", 'column("machine",mcRow)'),
            ("Borehole Log", 'column("BH-1",bedTrack)'),
            ("OHLC Candlestick", 'column("open",ohlcOpen)'),
            ("Origin-Destination Flow", 'column("origin lon",odOLon)'),
            ("Cumulative Flow", 'column("backlog",cfBacklog)'),
            ("Inventory Sawtooth", 'paired("stock on hand",stockDay,stockLevel)'),
            ("Fundamental Diagram", 'paired("M6 northbound",trafficDensity,trafficFlow)'),
            ("Eye Diagram", 'paired("received",eyeTime,eyeVolts)'),
            ("Psychrometric Chart", 'column("relative humidity (%)",psyRelHumidity)')):
        assert f'QStringLiteral("{engine}"),' in source and needle in source, (
            f"{engine} is back on the shared signal columns, which do not have "
            "the shape it reads")

    assert "trafficFlow.append(qMax(0.0,k*v)" in source, (
        "the fundamental diagram is handed a speed column again, so the engine "
        "fits a parabola to a straight line and draws a flow curve on a speed "
        "axis")
    assert "psyRelHumidity.append(34.0+42.0" in source, (
        "the psychrometric chart is handed a humidity ratio again, which the "
        "engine reads as a relative humidity of a few per cent")

    beds = source[source.index("QVector<double> bedTrack,bedFrom,bedTo;"):
                  source.index("QVector<double> ohlcOpen,ohlcHigh,ohlcLow,ohlcClose;")]
    assert "bedTrack.append(1.0);" in beds, (
        "the borehole log is back to a single hole, so there is nothing to "
        "correlate the beds across and the figure is three pixels wide")

    ohlc = source[source.index("QVector<double> ohlcOpen,ohlcHigh,ohlcLow,ohlcClose;"):
                  source.index("QVector<double> odOLon,odOLat,odDLon,odDLat,odCount;")]
    assert "ohlcHigh.append(qMax(open,close)+wick);" in ohlc, (
        "the candles' highs no longer bracket both ends, so the fixture is "
        "drawing price bars that cannot occur")
    assert "ohlcLow.append(qMin(open,close)-wick);" in ohlc, (
        "the candles' lows no longer bracket both ends")

    load = source[source.index("QVector<double> loadHistory;"):
                  source.index("QVector<double> taskRow,taskFrom,taskTo;")]
    assert "42.0*std::sin(x*0.11+0.7)" in load, (
        "the load history has lost its nested cycles, so rainflow counting - "
        "which exists to separate cycles of different range from each other - "
        "fills a single cell of the matrix")


def test_a_beam_stands_on_its_supports() -> None:
    """A moment diagram that was never zero at a support.

    Integrating the load from the left end with nothing at that end is a
    CANTILEVER — built in at x = 0, free at the far end. It is a good answer to
    a different question, and it was drawn under the heading "shear and moment"
    for a beam nobody said was a cantilever: a uniform load came out with the
    moment growing to its largest value at the free end and neither diagram
    returning to zero.

    Every engineer's first check on a moment diagram is that it is zero at a
    simple support, and this one failed it everywhere. A single span on two
    supports is statically determinate, so the reactions follow from the load
    alone and no boundary condition has to be asked for: 12 kN/m over 8 m now
    gives shear ±48 kN crossing zero at mid-span and a peak moment of
    96 kN·m — wL/2 and wL²/8 exactly.
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Shear and Moment")){')
    body = _strip_comments(source[start:start + 4200])

    assert "const double reactionB=-(loadMoment-totalLoad*spanLo)/span;" in body, (
        "the far support's reaction is no longer taken from moments about the "
        "near one, so the beam is a cantilever again and its moment diagram "
        "is not zero at either support")
    assert "reactionA=-totalLoad-reactionB;" in body, (
        "the near support's reaction is no longer taken from the sum of "
        "forces, so the shear diagram does not close")
    assert "double v=reactionA,m=0.0," in body, (
        "the shear no longer starts at the near support's reaction, so the "
        "reactions are computed and thrown away")
    assert "loadMoment+=0.5*(pw*px+s.y[i]*s.x[i])*dx;" in body, (
        "the first moment of the load is no longer accumulated, so there is "
        "nothing to take moments about the support with")
    assert 'QStringLiteral("shear and moment (simply supported)")' in source, (
        "the figure no longer says which support condition it assumed, which "
        "is the one thing a reader cannot recover from the curves")


def test_the_process_charts_are_shown_a_process() -> None:
    """A control chart of a rising sine, reporting every point out of control.

    A CUSUM of one reported "210 points beyond h"; a capability study of one
    drew a bimodal histogram and computed a Cpk from it. The charts were right
    — a sine IS out of control — and saying so 240 times demonstrates nothing.

    The deviate behind these fixtures was wrong on the first attempt in a way
    worth keeping: it summed twelve terms of a golden-ratio sequence rather
    than a generator's output, on the reasoning that a low-discrepancy sequence
    is reproducible where a PRNG might not be. It is reproducible and it is not
    noise — consecutive draws differ by twelve steps of the same irrational
    rotation, so the sum is very nearly periodic — and the capability histogram
    came out BIMODAL, two clean humps with a gap between them, which is what a
    reader would have taken as the finding of the figure.
    """
    source = _selftest_source()

    assert "quint32 spcState=0x2545F491u;" in source, (
        "the control-chart fixtures are back on a low-discrepancy sequence, "
        "which is reproducible and is not noise - the capability histogram "
        "comes out bimodal and the charts sawtooth")
    assert "spcState^=spcState<<13; spcState^=spcState>>17; spcState^=spcState<<5;" in source, (
        "the deviate no longer advances a generator, so every draw is the same")

    for engine, needle in (
            ("Control Chart", 'column("bore diameter (mm)",spcValue)'),
            ("CUSUM Chart", 'column("bore diameter (mm)",spcValue)'),
            ("EWMA Chart", 'column("bore diameter (mm)",spcValue)'),
            ("X-bar and R Chart", 'column("part 1",xbarA)'),
            ("Process Capability", 'column("LSL",capLower)'),
            ("p-Chart", 'column("defective",defects)'),
            ("Funnel Plot", 'column("standard error",studyError)'),
            ("CTD Profile", 'column("depth (m)",ctdDepth)'),
            ("T-S Diagram", 'column("salinity (PSU)",tsSalinity)'),
            ("Drawdown Curve", 'paired("fund",equityDay,equityValue)'),
            ("Mass Haul Diagram", 'paired("alignment",chainage,cutFill)'),
            ("Shear and Moment", 'paired("uniform load",beamStation,beamLoad)')):
        assert f'QStringLiteral("{engine}"),' in source and needle in source, (
            f"{engine} is back on the shared signal columns")

    assert "spcValue.append(48.0+(i>=150?0.9:0.0)" in source, (
        "the process has no step in it, so a control chart, a CUSUM and an "
        "EWMA all draw a process in control and none of them demonstrates "
        "what it is for - detecting the shift")
    assert "capLower.append(47.0); capUpper.append(49.0);" in source, (
        "the capability study has no specification, so there is no Cp or Cpk "
        "to compute and the figure is a histogram")
    assert "capValue.append(48.25+0.28*normalAt(i,5.0));" in source, (
        "the capability process is no longer off-centre, so Cp and Cpk are "
        "the same number and the reason both exist is invisible")
    assert "beamLoad.append(-12.0);" in source, (
        "the beam no longer carries a uniform load, so its shear and moment "
        "cannot be checked against wL/2 and wL^2/8")


def test_a_note_an_engine_produced_reaches_the_page() -> None:
    """Four engines producing annotations that reached nothing.

    `applyLimits` refreshes the painting decisions on a cached prepared spec
    from the caller's — colour scale, custom colours, the 3-D view, and
    annotations — because none of them is in the fingerprint. That was written
    when only a caller could make an annotation. Four rewrites now do: the
    ternary diagram's corner names, the VFA profile's peak, the availability
    timeline's uptime percentages and the animated line's step numbers. Every
    one of them was overwritten by the caller's list, which is normally empty.

    The symptom is the worst kind: the code that makes the note is there, it
    runs, it looks correct in review, and nothing appears on the figure. It was
    found by adding a fifth — the ternary corner names — and watching three
    annotations turn into none.
    """
    source = _backend_source()
    spec = (ROOT / "native/plot2d/include/PlotSpec.h").read_text(encoding="utf-8")

    assert "bool derived = false;" in spec, (
        "PlotAnnotation can no longer say who made it, so the refresh cannot "
        "tell an engine's note from a caller's and discards both")

    start = source.index("PlotSpec& applyLimits(")
    body = _strip_comments(source[start:start + 3000])
    assert "if(note.derived) fromEngine.append(note);" in body, (
        "the refresh no longer keeps the notes a rewrite produced, so four "
        "engines are computing annotations that reach nothing")
    assert "out.annotations+=fromEngine;" in body, (
        "the engine's notes are collected and not put back")
    assert "out.annotations=in.annotations;" in body, (
        "the caller's notes are no longer refreshed from the caller, so a note "
        "added after the prepared spec was cached never appears")

    # And every site that makes one says so, or it is dropped again.
    assert source.count("note.derived=true;") == 4, (
        "the set of engines producing their own annotations has changed - it "
        "was the ternary corner names, the VFA peak, the availability "
        "timeline's uptime percentages and the animated line's step numbers. "
        "A new one that does not set `derived` is discarded silently")


def test_a_ternary_diagram_names_its_corners() -> None:
    """A scatter of dots in a triangle.

    The composition names were put in the triangle's legend label — on a figure
    whose legend is switched off two lines above — so they appeared nowhere.
    Without them a reader can see that a point is near one vertex and has no
    way to learn which of the three components that vertex is.

    The labels go outside the triangle, which is also where the room had to be
    made for them: the axis limits were -0.06 to 1.06, tight enough that a
    label placed outside fell past drawAnnotations' clip rectangle and was not
    drawn at all.
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Ternary Scatter")){')
    body = _strip_comments(source[start:start + 5200])

    assert "corner(0.0,0.0,b.label,-10.0,14.0);" in body, (
        "the lower-left corner is no longer named")
    assert "corner(1.0,0.0,a.label,-10.0,14.0);" in body, (
        "the lower-right corner is no longer named")
    assert "corner(0.5,std::sqrt(3.0)/2.0,c.label,-12.0,-6.0);" in body, (
        "the apex is no longer named")
    assert "out.xAxis=PlotAxis{QString(),false,-0.14,1.14};" in body, (
        "the axis limits are back to hugging the triangle, so every corner "
        "label falls outside the clip rectangle and none of them is drawn")
    assert "out.yAxis=PlotAxis{QString(),false,-0.16,1.00};" in body, (
        "there is no longer room below the base or above the apex for their "
        "labels")


def test_the_multivariate_engines_are_shown_few_enough_things_to_compare() -> None:
    """Six engines whose content is a small number of things compared.

    At 240 rows each of them drew a smear: a dendrogram of two hundred leaves,
    a slope graph of sixty crossing lines, a population pyramid of forty rows
    of a sine, a ternary scatter squeezed into a thumbprint in the middle of
    its own triangle. The caps inside the engines kept them from being worse
    and could not make them readable.

    The waterfall is the clearest case. Every step was positive, so the chart
    was a staircase that only climbed — which is a cumulative sum, and the one
    thing a waterfall is chosen over a cumulative sum to show is where the
    ground was given back.
    """
    source = _selftest_source()

    for engine, needle in (
            ("Dendrogram", 'column("merge height",mergeHeight)'),
            ("Slope Graph", 'column("2019",beforeRate)'),
            ("Waterfall", 'column("bridge",bridgeStep)'),
            ("Population Pyramid", 'column("men",menByBand)'),
            ("Ternary Scatter", 'column("sand",sandPct)'),
            ("Parallel Coordinates", 'column("sepal length",irisSepalL)'),
            ("Andrews Curves", 'column("petal width",irisPetalW)')):
        assert f'QStringLiteral("{engine}"),' in source and needle in source, (
            f"{engine} is back on 240 rows of the shared columns, where its "
            "content is a smear")

    bridge = source[source.index("QVector<double> bridgeStep;"):
                    source.index("QVector<double> menByBand,womenByBand;")]
    assert "-64.0" in bridge and "-112.0" in bridge, (
        "the waterfall's steps are all gains again, so the chart is a "
        "staircase that only climbs - which is a cumulative sum")

    tri = source[source.index("QVector<double> sandPct,siltPct,clayPct;"):
                 source.index("QVector<double> irisSepalL,irisSepalW,irisPetalL,irisPetalW;")]
    assert "*(1.0-u)" in tri, (
        "the ternary compositions no longer fill the simplex, so the points "
        "cluster near the centroid and the triangle is mostly empty")


def test_a_catalogue_classifier_is_good_and_not_perfect() -> None:
    """An ROC curve reporting an area of exactly 1.000.

    A perfect classifier is the one result that means the data is wrong, and
    the catalogue was showing it twice — the ROC curve and the
    precision-recall curve both ran on the shared columns, where the `label`
    column is a deterministic function of the row index and every score
    separates it completely. A reader looking for what the engine does saw a
    figure that cannot occur.

    Two overlapping score populations with a 22% positive rate now give
    AUC 0.914 and average precision 0.800, which is what a real model looks
    like and what makes the two figures worth comparing with each other.
    """
    source = _selftest_source()
    assert 'QStringLiteral("ROC Curve"),\n            {column("score",modelScore)' in source, (
        "the ROC curve is back on the shared columns, where it reports a "
        "perfect classifier")
    assert 'QStringLiteral("Precision-Recall Curve"),\n            {column("score",modelScore)' in source, (
        "the precision-recall curve is back on the shared columns")

    model = source[source.index("QVector<double> modelScore,modelTruth;"):
                   source.index("QVector<double> sampleA,sampleB;")]
    assert "(positive?1.45:-0.35)" in model, (
        "the two score populations no longer overlap, so the classifier is "
        "perfect again and both curves report an area of 1.000")
    assert "uniform()<0.22" in model, (
        "the positive rate is gone, so the precision-recall curve's chance "
        "line has no prevalence to draw")


def test_the_terrain_engines_are_given_terrain() -> None:
    """Five engines reading a digital elevation model, handed a decaying sine.

    The slope map was a picture of the signal's own curvature, the aspect map
    of the direction that curvature pointed, and the hypsometric curve of the
    sine's own distribution. None of them was wrong; all of them were answering
    a question about a signal column.
    """
    source = _selftest_source()

    for engine in ("Slope Map", "Aspect Map", "Hillshade"):
        assert f'QStringLiteral("{engine}"),{{column("easting (m)",demX)' in source, (
            f"{engine} is back on the shared columns, so it is a picture of "
            "the signal's own curvature")
    assert 'QStringLiteral("Terrain Profile"),\n            {column("longitude",pathLonDeg)' in source, (
        "the terrain profile no longer has a path with heights along it")
    assert 'QStringLiteral("Hypsometric Curve"),{column("elevation (m)",demZ)}},' in source, (
        "the hypsometric curve is no longer computed from the elevation model")
    assert 'QStringLiteral("Duration Curve"),{column("discharge (m3/s)",dailyFlow)}},' in source, (
        "the flow duration curve has no flows, so the slope between the 10th "
        "and 90th percentile - which is the whole reading - is the slope of a "
        "sine")

    dem = source[source.index("QVector<double> demX,demY,demZ;"):
                 source.index("QVector<double> pathLonDeg,pathLatDeg,pathElevation;")]
    assert "340.0*std::exp(-std::pow((y-1200.0)/430.0,2.0))" in dem, (
        "the elevation model has lost its ridge, so the slope and aspect maps "
        "have no relief to measure")
    assert "-180.0*std::exp(-std::pow((x-760.0)/300.0,2.0))" in dem, (
        "the elevation model has lost its valley")


def test_a_concentration_curve_is_given_the_column_it_ranks_by() -> None:
    """An engine that correctly drew nothing.

    A concentration curve needs two columns: the outcome, and the variable the
    population is RANKED by — that second column is the entire difference
    between it and a Lorenz curve of the outcome alone. Given one, the engine
    drew nothing at all and the sweep reported it as an engine that draws
    nothing, which was the sweep doing its job on a fixture that was half a
    fixture.

    Public health spending falls with income, so the curve sits above the
    diagonal: the outcome favours the poor, which is the reading the figure
    exists for.
    """
    source = _selftest_source()
    assert 'column("health spending",healthSpend),\n             column("household income",householdIncome)' in source, (
        "the concentration curve has lost its ranking column, so it draws "
        "nothing and the sweep fails")
    assert "healthSpend.append(1400.0*std::pow(income/28000.0,-0.35)" in source, (
        "the outcome no longer varies with the ranking variable, so the curve "
        "lies on the diagonal and says nothing")

    income = source[source.index("QVector<double> householdIncome,healthSpend;"):
                    source.index("QVector<double> speciesCount;")]
    assert "std::exp(10.1+0.62*(z-6.0))" in income, (
        "the incomes are no longer log-normal, so the Lorenz curve's Gini is "
        "the 0.08 a smooth column gives rather than a real one")


def test_the_ranked_and_ordered_engines_have_something_to_rank() -> None:
    """A rank-abundance curve of a smooth column is a straight line at the top.

    These engines sort their input and read the SHAPE of the sorted sequence: a
    log-series for the species counts, an elbow in the eigenvalues, a knee in
    the within-cluster sum of squares. A smooth column sorts into a smooth
    curve with no feature anywhere, so each of them drew a correct picture of
    nothing in particular.
    """
    source = _selftest_source()

    assert 'QStringLiteral("Rank-Abundance Curve"),{column("individuals",speciesCount)}},' in source, (
        "the rank-abundance curve has no species counts")
    assert "speciesCount.append(std::ceil(2100.0*std::pow(0.84,double(i))));" in source, (
        "the abundances no longer follow a log-series, so the long tail of "
        "rare species the curve exists to show is not there")
    assert 'QStringLiteral("Scree Plot"),{column("eigenvalue",eigenvalue)}},' in source, (
        "the scree plot has no eigenvalues")
    assert "const double ev[12]={4.62,2.81,1.74,1.12,0.44," in source, (
        "the eigenvalues no longer have an elbow, so there is no component "
        "count for the figure to suggest")
    assert 'QStringLiteral("Elbow Plot"),{paired("within-cluster SSE",clusterK,clusterSse)}},' in source, (
        "the elbow plot has no within-cluster sum of squares")
    assert "clusterSse.append(210.0*std::pow(double(k),-1.45)+18.0);" in source, (
        "the sum of squares no longer bends, so the knee the engine marks is "
        "placed on a curve that has none")


def test_the_genomics_engines_have_a_signal_to_find() -> None:
    """A Manhattan plot with nothing above the line, and an MA plot with no fan.

    A Manhattan plot is read for the peaks that cross genome-wide
    significance; on the shared columns the whole figure was below the line.
    An MA plot is read for the few genes off the zero axis and for the way the
    scatter widens at low intensity; on a smooth column it was a smooth curve.
    """
    source = _selftest_source()

    assert 'QStringLiteral("Manhattan Plot"),{column("chromosome 6",gwasP)}},' in source, (
        "the Manhattan plot has no p-values")
    gwas = source[source.index("QVector<double> gwasP;"):
                  source.index("QVector<double> householdIncome,healthSpend;")]
    assert "p=std::pow(u,1.0+70.0*lift);" in gwas, (
        "the association peaks are gone, so nothing crosses the genome-wide "
        "line and the threshold the engine draws has nothing to separate")

    assert 'column("control",sampleA),column("treated",sampleB)' in source, (
        "the MA plot has no pair of samples to compare")
    ma = source[source.index("QVector<double> sampleA,sampleB;"):
                source.index("QVector<double> gwasP;")]
    assert "const double noise=0.9+1.6/std::sqrt(intensity);" in ma, (
        "the noise no longer grows at low intensity, so the fan shape the MA "
        "plot is read for is not in the data")
    assert "if(u>0.985)" in ma and "else if(u<0.015)" in ma, (
        "no gene is differentially expressed, so every point sits on the zero "
        "line and the figure has no finding in it")


def test_the_event_and_time_frequency_engines_are_shown_events_and_change() -> None:
    """An event plot of 240 evenly spaced samples is five parallel rules.

    It draws one tick per event, which is right; handed a dense uniform column
    the ticks close up into a solid line and nothing on the figure reads as an
    event. Three Poisson spike trains at different rates do.

    A spectrogram of a stationary signal is a heatmap of horizontal stripes — a
    correct picture, and one that shows nothing the power spectral density
    above it does not already show more clearly. The whole reason to spend a
    second axis on time is that the content CHANGES, so the fixture is a linear
    chirp from 20 Hz to 220 with a fixed-frequency burst laid across it.
    """
    source = _selftest_source()

    assert 'QStringLiteral("Event Plot"),{paired("unit 1",spikeA,spikeA)' in source, (
        "the event plot is back on the shared columns, where 240 evenly "
        "spaced ticks close up into a solid line")
    spikes = source[source.index("QVector<double> spikeA,spikeB,spikeC;"):
                    source.index("QVector<double> chirpTime,chirpValue;")]
    assert "at+=-std::log(qMax(1e-9,uniform()))/rate[k];" in spikes, (
        "the spike trains are no longer Poisson, so the events are evenly "
        "spaced and the figure is a row of rules again")
    assert "const double rate[3]={7.5,2.8,14.0};" in spikes, (
        "the three trains no longer differ in rate, so there is nothing to "
        "compare between the rows")

    assert 'QStringLiteral("Spectrogram"),{paired("chirp",chirpTime,chirpValue)}},' in source, (
        "the spectrogram is back on a stationary signal, where it is a "
        "heatmap of horizontal stripes")
    chirp = source[source.index("QVector<double> chirpTime,chirpValue;"):
                   source.index("const QHash<QString,ShapedAxes> shapedAxes{")]
    assert "std::sin(2.0*M_PI*(20.0*t+100.0*t*t/4.0))" in chirp, (
        "the signal no longer sweeps, so the spectrogram's second axis carries "
        "nothing the power spectral density does not already show")
    assert "(t>1.6&&t<2.3)?0.55*std::sin(2.0*M_PI*640.0*t):0.0" in chirp, (
        "the fixed-frequency burst is gone, so the figure has a sweep and "
        "nothing to contrast it against")


def test_the_four_attribute_charts_can_be_told_apart() -> None:
    """Four catalogue entries whose difference a constant lot size erases.

    A p-chart and a u-chart have STEPPED control limits — they widen on a small
    lot and tighten on a large one — while an np-chart and a c-chart, which
    assume a constant lot, have straight ones. That is the main reason there
    are four entries rather than two. On a constant lot size the difference
    does not exist to be drawn, and the four figures came out proportional to
    each other: 1.06 grey levels between the p-chart and the u-chart, 2.0
    between the p-chart and the np-chart.

    The second half is that a defective is not a defect. A p-chart counts the
    ITEMS that failed; a c- or u-chart counts the FAULTS found, and one item
    can carry several. Given the same column both pairs drew the same shape
    with a different axis label.
    """
    source = _selftest_source()

    lots = source[source.index("QVector<double> defects,lotSize,faults;"):
                  source.index("QVector<double> studyEffect,studyError;")]
    assert "const double lot=std::floor(90.0+340.0*v);" in lots, (
        "the lot size is constant again, so the p-chart's and u-chart's "
        "stepped control limits are straight and all four attribute charts "
        "are proportional to each other")
    assert "faults.append(std::floor((i==11?0.19:0.072)*lot" in lots, (
        "the fault count is gone, so the c- and u-charts are drawn from the "
        "count of defective ITEMS - which is the p-chart's column, not theirs")

    assert '{QStringLiteral("c-Chart"),{column("faults",faults)}},' in source, (
        "the c-chart counts defectives again rather than faults")
    assert '{QStringLiteral("u-Chart"),{column("faults",faults),column("units",lotSize)}},' in source, (
        "the u-chart counts defectives again rather than faults per unit")


def test_a_spy_matrix_is_shown_some_zeros() -> None:
    """The one engine whose entire content is where the zeros are.

    The shared demonstration columns are dense — every value non-zero — so
    every pair of variables was jointly non-zero and the spy matrix was a solid
    block of one colour. Correct for dense data, and a catalogue entry showing
    nothing at all.

    Eight variables each observed on a band of rows give a banded pattern:
    variables more than two apart never appear together, which is the kind of
    structure a spy plot is chosen to reveal.
    """
    source = _selftest_source()
    assert 'QStringLiteral("Spy Matrix"),\n            {column("v1",sparseCols[0])' in source, (
        "the spy matrix is back on the dense shared columns, where every pair "
        "is jointly non-zero and the figure is a solid block")
    block = source[source.index("QVector<QVector<double>> sparseCols(8);"):
                   source.index("const QHash<QString,ShapedAxes> shapedAxes{")]
    assert "const bool present=(std::abs(band-j)<=1);" in block, (
        "the sparsity pattern has no structure in it, so the spy matrix shows "
        "either everything or nothing")
    assert "sparseCols[j].append(present?" in block, (
        "the pattern is computed and not written into the columns")


def test_a_confusion_matrix_has_a_diagonal_to_be_off() -> None:
    """A confusion matrix of a classifier that has learned nothing.

    It shared the mosaic plot's two category columns, where the true class is
    i%4 and the predicted is (i/4)%3 — two independent counters. The matrix
    came out with no diagonal at all and an accuracy of 0.250, which is chance
    on four classes.

    What the figure is read for is the OFF-diagonal: which pairs of classes get
    mistaken for each other. That needs a diagonal to be off. 84% correct with
    classes 1 and 2 confused for each other far more often than any other pair
    gives the figure something to say.
    """
    source = _selftest_source()
    assert 'QStringLiteral("Confusion Matrix"),{column("true",trueClass),' in source, (
        "the confusion matrix is back on the mosaic plot's independent "
        "category columns, where it reports chance accuracy and has no "
        "diagonal")
    block = source[source.index("QVector<double> trueClass,predictedClass;"):
                   source.index("const QHash<QString,ShapedAxes> shapedAxes{")]
    assert "if(u<0.26) guess=(truth==1)?2:1;" in block, (
        "the two classes that get confused for each other no longer do, so "
        "the off-diagonal - which is the whole reading - is flat noise")
    assert "int guess=truth;" in block, (
        "the prediction no longer starts from the truth, so the classifier is "
        "guessing and the matrix has no diagonal")


def test_the_formula_engines_are_demonstrated_on_their_own_domain() -> None:
    """Seven engines that plot a formula, drawn over a measurement's range.

    Their domain follows the mapped data when there is any, so that a formula
    can be overlaid on a measurement. That is right, and it meant the catalogue
    drew every one of them over the shared signal column's range of 2.1 to 5.2.
    `sin(x)*cos(y)` on that window has its zero set at x = pi and y = 3pi/2 and
    nowhere else, so the Implicit Function entry was a cross — two straight
    lines, mathematically correct and unrecognisable as what the engine does.

    An empty series list is the honest demonstration. These engines plot a
    formula, not a dataset, and with nothing mapped they use their own default
    domain of -10 to 10 — which is the state a person sees the first time they
    open one.
    """
    source = _selftest_source()
    for engine in ("Function Plot", "Function Contour", "Function Surface",
                   "Function Mesh", "Function 3D Parametric",
                   "Implicit Function", "Implicit Surface"):
        assert f'{{QStringLiteral("{engine}"),{{}}}},' in source, (
            f"{engine} is given columns again, so its domain shrinks to the "
            "range of a measurement and the formula's shape is lost")

    backend = _backend_source()
    start = backend.index('if(in.engine.startsWith(QLatin1String("Function"))')
    body = _strip_comments(backend[start:start + 1600])
    assert "double lo=-10.0, hi=10.0;" in body, (
        "the default domain is gone, so an engine with nothing mapped has no "
        "range to plot its formula over")
    assert "if(span.hi>span.lo){ lo=span.lo; hi=span.hi; }" in body, (
        "a formula can no longer be overlaid on a measurement, which is why "
        "the domain follows the data in the first place")


def test_an_isoconversional_plot_has_something_to_find() -> None:
    """A perfectly horizontal trace across the plot.

    The demonstration used a single constant activation energy of 150 kJ/mol,
    and the engine dutifully recovered 150 at every conversion. That is the
    correct answer for a one-step reaction and it is the one result an
    isoconversional plot is never run to find: the whole reason to compute Ea
    at each conversion separately is to see whether it CHANGES, because a
    change means the mechanism changes partway through and a single Arrhenius
    fit to the whole run is wrong.

    Two overlapping steps — about 135 kJ/mol early, rising through a transition
    near 45% conversion to about 185 late — give the figure the shape it is
    read for, and the mean line then shows what a single fit would have
    reported instead.
    """
    source = _selftest_source()
    block = source[source.index("const double rates[]={2.0,5.0,10.0,20.0,40.0};"):]
    block = block[:1400]

    assert "return 135000.0+50000.0/(1.0+std::exp(-(share-0.45)/0.07));" in block, (
        "the activation energy is constant again, so the isoconversional plot "
        "is a horizontal line and demonstrates the one answer it is never run "
        "to find")
    assert "const double factor=1.052*energyAt(share)/8.3145;" in block, (
        "the varying energy is computed and not used, so the temperatures are "
        "still generated from a single constant")


def test_no_engine_is_given_two_fixtures() -> None:
    """A duplicate key in a QHash literal is silently dropped.

    The fixture table is a `QHash<QString, QVector<PlotSeries>>` built from a
    brace-initialiser list. Listing an engine twice is not an error, not a
    warning, and not visible in the figure: the last entry wins and the other
    one simply never runs. A Duane Plot fixture added near the top of the table
    was shadowed by one four hundred lines below it, and the only way it showed
    was that the figure's series carried the OTHER entry's label.

    The same applies to the axis-name table beside it. Both are checked here
    because the compiler cannot: a duplicate key is well-formed C++ that means
    something other than what it looks like.
    """
    source = _selftest_source()

    def keys(opener: str, closer: str) -> list[str]:
        start = source.index(opener)
        end = source.index(closer, start)
        # The KEY only: an opening brace has to follow the comma. Without that
        # the value's own QStringLiterals match too - the axis-name table's
        # values are pairs of them - and every axis name is read as an engine.
        return re.findall(r'\{QStringLiteral\("((?:[^"\\]|\\.)+)"\),\s*\{',
                          source[start:end])

    fixtures = keys("const QHash<QString,QVector<PlotSeries>> shaped{",
                    "\n    };")
    assert len(fixtures) > 200, (
        "the fixture table has moved or its shape has changed, so this check "
        "is reading nothing")
    duplicates = sorted({name for name in fixtures if fixtures.count(name) > 1})
    assert not duplicates, (
        "these engines are listed twice in the fixture table, so one of the "
        "two entries silently never runs: " + ", ".join(duplicates))

    axes = keys("const QHash<QString,ShapedAxes> shapedAxes{", "\n    };")
    axisDuplicates = sorted({name for name in axes if axes.count(name) > 1})
    assert not axisDuplicates, (
        "these engines are listed twice in the axis-name table: "
        + ", ".join(axisDuplicates))


def test_the_last_pair_reading_engines_are_given_pairs() -> None:
    """A volcano plot with nothing significant on it.

    Six more engines that read a specific pair and were given the shared
    signals. The volcano is the clearest: its p column was a smooth
    exponential decay, so no gene was significant, the figure said so, and it
    drew one flat row of grey dots.

    The volcano fixture was also wrong on the first try, in a way worth
    recording: the null fold change and the null p-value were both derived from
    the same normal deviate, which made them perfectly correlated and drew the
    null cloud as a narrow V pinched at the origin — a picture of the
    generator, not of an experiment. Under the null the p-values are uniform,
    the fold changes are small and symmetric, and they have nothing to do with
    each other. That independence is what gives the volcano its shoulders.
    """
    source = _selftest_source()

    for engine, needle in (
            ("Volcano Plot", 'column("log2 fold change",foldChange)'),
            ("Bland-Altman", 'column("reference",referenceMethod)'),
            ("L'Abbé Plot", 'column("control arm",controlArm)'),
            ("Prediction Error Plot", 'column("observed",observedY)'),
            ("Learning Curve", 'column("training",trainScore)'),
            ("Validation Curve", 'column("training",paramTrain)')):
        assert needle in source, (
            f"{engine} is back on the shared signal columns, which are not "
            "the pair it reads")

    volcano = source[source.index("QVector<double> foldChange,foldP;"):
                     source.index("QVector<double> referenceMethod,newMethod;")]
    assert "double p=uniform();                       // null: uniform" in volcano, (
        "the null p-values are derived from the same deviate as the fold "
        "change again, so the two are perfectly correlated and the null cloud "
        "is a narrow V rather than the body of the volcano")

    labbe = source[source.index("QVector<double> controlArm,treatedArm;"):
                   source.index("QVector<double> observedY,predictedY;")]
    assert "const double benefit=0.30*(1.0-control);" in labbe, (
        "the treatment effect no longer varies with the control rate, so the "
        "points sit parallel to the diagonal and the heterogeneity a L'Abbe "
        "plot exists to make visible is not in the data")

    curves = source[source.index("QVector<double> trainScore,validScore,paramTrain,paramValid;"):
                    source.index("const QHash<QString,ShapedAxes> shapedAxes{")]
    assert "validScore.append(0.93-0.40*std::exp(-n/4.5));" in curves, (
        "the learning curve no longer plateaus, so the point at which more "
        "data stops paying - which is the whole reading - does not exist")
    assert "paramValid.append(0.90-0.014*(k-11.0)*(k-11.0)*0.09" in curves, (
        "the validation curve no longer peaks, so the best value of the "
        "hyperparameter - which is the whole reading - does not exist")


def test_a_thread_warning_says_which_thread() -> None:
    """A warning that has sat in startup.log unexplained across several builds.

    "QObject::startTimer: Timers cannot be started from another thread" and its
    relatives are Qt telling you that an object was touched from somewhere it
    does not live. The message names neither the object nor the thread, so the
    log entry says only that it happened.

    Naming the thread is most of the answer: the GUI thread, the Qt Quick
    scene-graph render thread and a QtConcurrent pool thread are three very
    different bugs, and the first of them is not a bug at all. Qt names its own
    threads, so the object name alone usually identifies the culprit — and it
    costs no symbol lookup, no backtrace library, and nothing at all on the
    messages that do not match.
    """
    source = (ROOT / "app/src/main.cpp").read_text(encoding="utf-8")
    start = source.index("void graphvisMessageHandler(")
    body = _strip_comments(source[start:source.index("\n}\n", start)])

    assert 'message.contains(QLatin1String("another thread"))' in body, (
        "the message handler no longer recognises a cross-thread warning, so "
        "the log says only that one happened")
    assert "const QThread* current = QThread::currentThread();" in body, (
        "the handler no longer records which thread the warning came from")
    assert "current && current == gui ? QStringLiteral(\" = GUI\")" in body, (
        "the handler no longer says whether the thread was the GUI one, which "
        "is the difference between a bug and a false alarm")
    assert "name.isEmpty() ? QString() : QStringLiteral(\" \\\"%1\\\"\").arg(name)" in body, (
        "the thread's name is no longer logged - Qt names its own threads, and "
        "the name is usually the whole answer")
    assert "where));" in body, (
        "the thread note is built and not appended to the log line")


def test_a_figure_does_not_depend_on_the_hash_seed() -> None:
    """One figure in 434 whose checksum changed between two runs of the binary.

    Qt randomises its QHash seed per process, so an engine that iterates a
    QHash to decide what to draw — or in what order — produces a different
    picture every run, and nothing inside a single run can see it. The hexbin
    painter iterated its cell counts straight out of a QHash; neighbouring
    hexagons share an edge and, with antialiasing, the shared pixels belong to
    whichever was drawn last.

    A figure that is not reproducible cannot be compared with itself, which is
    most of what the sweep does — and a person re-exporting a figure for a
    paper should get the file they had before. The sweep now renders every
    engine a second time under a deterministic seed and fails on any that
    differ, which is a check no amount of re-running a single build could make.
    """
    selftest = _selftest_source()
    assert "QHashSeed::setDeterministicGlobalSeed();" in selftest, (
        "the sweep no longer re-renders under a second hash seed, so an engine "
        "whose picture depends on hash order is invisible to it")
    assert "QHashSeed::resetRandomGlobalSeed();" in selftest, (
        "the sweep leaves the hash seed deterministic for everything after it, "
        "which hides exactly the fault this check exists to find")
    assert "unstable.append(engine);" in selftest, (
        "the second render is made and not compared")
    assert 'draw a DIFFERENT picture when the "' in selftest, (
        "the sweep no longer reports engines whose output is not reproducible")

    source = _backend_source()
    hexbin = _strip_comments(source[source.index("void QtPlotBackend::drawHexbin("):
                                    source.index("void QtPlotBackend::drawHeatmap(")])
    assert "std::sort(cells.begin(),cells.end());" in hexbin, (
        "the hexbin cells are painted straight out of a QHash again, so the "
        "figure differs between runs")

    cloud = source.index("std::sort(ordered.begin(),ordered.end(),")
    assert "return a.first<b.first; });" in source[cloud:cloud + 400], (
        "the word cloud's sort is not a total order, so words with equal "
        "counts change place between runs")

    sun = source.index("QVector<int> filled;")
    assert "std::sort(filled.begin(),filled.end());" in source[sun:sun + 400], (
        "the sunflower plot emits its cells straight out of a QHash again")


def test_the_bourdet_derivative_slides_its_window() -> None:
    """A well test of 24,000 readings took 36.9 seconds to draw.

    Two quadratic passes in one engine, both the same mistake. The derivative
    is taken over a fixed span in log time and the data is sorted, so the
    window's ends only ever move forward — but each point copied its window
    into two vectors and called `fitLine` on it. Then the radial-flow plateau
    search did it again: a fixed-length window over the derivative, copied out
    and fitted fresh at every start position, eighteen thousand vectors of six
    thousand doubles.

    A least-squares slope needs five running numbers — the count and the sums
    of x, y, x² and xy. Adding a point at the leading edge and dropping one at
    the trailing edge keeps all five current. **36,872 ms to 355 ms, and the
    rendered figure is byte-identical.**
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Pressure Derivative Plot")){')
    body = _strip_comments(source[start:start + 7000])

    assert "const auto addPoint=[&](int k){" in body and "const auto dropPoint=[&](int k){" in body, (
        "the Bourdet derivative no longer slides its window, so every point "
        "copies its neighbourhood and fits it fresh - quadratic in the number "
        "of readings")
    assert "sx+=lx; sy+=ly; sxx+=lx*lx; sxy+=lx*ly; ++m;" in body, (
        "the running sums the sliding window depends on are gone")
    assert "const auto take=[&](int i,double sign){" in body, (
        "the plateau search no longer slides its window either, so the second "
        "quadratic pass is back")
    assert "if(start>0){ take(start-1,-1.0); take(start+window-1,1.0); }" in body, (
        "the plateau window is rebuilt rather than advanced")
    assert "QVector<double> wx,wy;" not in body, (
        "the plateau search is copying its window out again, which is the "
        "allocation the rewrite removed")


def test_a_category_cap_is_checked_before_the_table_is_built() -> None:
    """A guard that runs after the work it exists to prevent is not a guard.

    The mosaic plot gives up when either axis has more than forty levels — and
    the check sat after the loop that builds the contingency table, so a
    continuous column mapped by mistake built a 24,000 by 24,000 table and then
    threw it away. Every new column appends a cell to every existing row and
    every new row allocates a vector as long as the column list, so both the
    work and the memory are quadratic in the row count: **18.9 seconds**, the
    slowest thing in the catalogue.

    The confusion matrix had the same fault in a different shape: its cap of
    twenty classes was applied after collecting the class list, and the
    collection asked `QVector::contains` once per value — a linear scan, so 576
    million comparisons to reach a verdict of "too many".

    Counting first is linear, stops as soon as it has seen one too many, and
    reaches the same verdict. Mosaic 18,906 ms to 1.8 ms; confusion matrix
    719 ms to 1.9 ms; every figure in the gallery byte-identical.
    """
    source = _backend_source()

    mosaic = _strip_comments(source[source.index("void QtPlotBackend::drawMosaic("):
                                    source.index("void QtPlotBackend::drawMosaic(") + 4000])
    assert "constexpr int kMaxLevels=40;" in mosaic, (
        "the mosaic plot's level cap is no longer a named constant, so the "
        "early check and the late one can disagree")
    assert "if(tooMany(columnOf)||tooMany(rowOf)) return;" in mosaic, (
        "the mosaic plot builds its contingency table before checking how many "
        "levels there are, so a continuous column costs a 24,000-square table")
    assert "if(seen.size()>kMaxLevels) return true;" in mosaic, (
        "the level count no longer stops as soon as it has seen one too many")

    start = source.index('if(in.engine==QLatin1String("Confusion Matrix")){')
    confusion = _strip_comments(source[start:start + 3000])
    assert "constexpr int kMaxClasses=20;" in confusion, (
        "the confusion matrix's class cap is no longer a named constant")
    assert "for(int i=0;i<n&&seen.size()<=kMaxClasses;++i){" in confusion, (
        "the class list is collected in full before the cap is applied")
    assert "if(seen.contains(v)) continue;" in confusion, (
        "membership is tested against the vector again, which is a linear scan "
        "per value")


def test_a_split_search_does_not_refit_from_scratch() -> None:
    """A titration logged at 24,000 points took 12.8 seconds.

    The endpoint is found by searching for the split that two straight lines
    fit best — and the search copied both halves out with `mid`, fitted each,
    and then walked every point again to total the residuals. Four vector
    allocations and three O(n) passes per split, for n splits.

    A least-squares fit and its residual sum both come from six running
    numbers: the count and the sums of x, y, x², xy and y². Moving the split
    one place right moves one point from the right side to the left, so both
    sides stay current in constant time and the right side is the totals minus
    the left. **12,804 ms to 94 ms, byte-identical.**
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Conductometric Titration")){')
    body = _strip_comments(source[start:start + 5000])

    assert "struct Sums {" in body and "void add(double px,double py,double sign){" in body, (
        "the split search no longer carries running sums, so every candidate "
        "split refits both halves from scratch")
    assert "rss=s.yy-fit.intercept*s.y-fit.slope*s.xy;" in body, (
        "the residual sum is walked point by point again rather than taken "
        "from the sums the fit already needed")
    assert "if(split>3) left.add(xs[split-1],ys[split-1],1.0);" in body, (
        "the left-hand sums are rebuilt rather than advanced")
    assert "QVector<double> lx=xs.mid(0,split)" not in body, (
        "the halves are being copied out again, which is the allocation the "
        "rewrite removed")


def test_a_grouping_key_that_is_continuous_is_refused() -> None:
    """24,000 isotherms of one point each.

    A master curve groups its readings by temperature and shifts each isotherm
    against everything placed so far, with a coarse-then-fine search. Every
    distinct temperature becomes its own isotherm — so a temperature *logged
    alongside* the sweep, rather than a list of the temperatures the sweep was
    run at, made 24,000 groups and an assembly quadratic in their number: 19.9
    seconds for a figure that cannot mean anything, because an isotherm of one
    point has no overlap to be shifted against.

    Forty is already far more than any real time-temperature superposition, and
    the count stops as soon as it is exceeded. **19,856 ms to 2.0 ms.**
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Master Curve (TTS)")){')
    body = _strip_comments(source[start:start + 3000])

    assert "constexpr int kMaxIsotherms=40;" in body, (
        "the master curve no longer caps the number of isotherms, so a "
        "continuous temperature column makes one per row")
    assert "if(levels.size()>kMaxIsotherms) return out;" in body, (
        "the isotherm count is taken and not acted on")
    assert body.index("constexpr int kMaxIsotherms=40;") < body.index("QMap<double,QVector<QPair<double,double>>> isotherms;"), (
        "the cap is checked after the isotherms have been built, which is the "
        "fault it exists to prevent")


def test_the_growth_window_search_slides_and_breaks_ties_deliberately() -> None:
    """The one rewrite that moved a pixel, and why it was allowed to.

    A growth curve quotes its rate from the STRAIGHTEST window, because a
    culture spends its beginning in lag and its end in stationary phase and a
    rate fitted across all three describes none of them. The search copied the
    window out with `mid` at every start position and walked it twice more -
    once for the mean, once for the residuals - which is quadratic in the
    reading count.

    The same six running numbers as everywhere else give the fit, the residual
    sum and the total sum of squares at once. But unlike the other five
    rewrites this one did NOT come back byte-identical, and the reason is
    worth keeping: a clean exponential phase is straight along its whole
    length, so every window inside it scores the same R-squared and the same
    slope. Fifteen start positions tied, and the winner was decided by the last
    bits of the accumulation - which the change of summation order flipped. The
    rate in the legend never moved; the drawn segment slid along the curve.

    An accidental tie-break is not a result. Requiring a real improvement
    before displacing the incumbent makes the answer the EARLIEST window of the
    phase, which is both stable under any summation order and the answer worth
    quoting: a growth rate is measured from where exponential growth begins.
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Growth Rate (OD)")){')
    body = _strip_comments(source[start:start + 6400])

    assert "const auto take=[&](int i,double sign){" in body, (
        "the growth window is rebuilt at every start position rather than "
        "advanced, which is quadratic in the reading count")
    assert "if(start>0){ take(start-1,-1.0); take(start+window-1,1.0); }" in body, (
        "the running sums are no longer advanced by one place per step")
    assert "const double ssRes=syy-f.intercept*sy-f.slope*sxy;" in body, (
        "the residual sum is walked point by point again rather than taken "
        "from the sums the fit already needed")
    assert "const double mustBeat=bestFit+1e-12*std::abs(bestFit)+1e-15;" in body, (
        "the tie between equally straight windows is decided by floating-point "
        "noise again, so the drawn segment moves along the curve whenever the "
        "sums are accumulated in a different order")
    assert "if(quality>mustBeat){ bestFit=quality; best=f; bestAt=start; }" in body, (
        "the incumbent window is displaced without having to beat it, which is "
        "the accidental tie-break the explicit one replaced")
    assert "xs.mid(start,window)" not in body, (
        "the window is being copied out again, which is the allocation the "
        "rewrite removed")


def test_a_composite_curve_accumulates_capacity_instead_of_rescanning() -> None:
    """A pinch analysis of 24,000 streams took 4.1 seconds.

    A composite curve is enthalpy accumulated upward through the temperature
    intervals the stream endpoints cut the range into, and the capacity of each
    interval was found by asking every stream whether it covered that interval
    — one pass per interval, and there are two intervals per stream.

    Every endpoint is itself a level, so a stream covers a *contiguous run* of
    intervals and nothing outside it. Adding its capacity where the run begins
    and removing it after the run ends leaves a prefix sum that is the capacity
    of each interval in turn.

    The interpolation onto the other curve was the second half of the cost: a
    linear scan for the bracketing breakpoint, asked once per breakpoint inside
    an eighty-step bisection. **4,145 ms to 173 ms, byte-identical.**
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Composite Curves (Pinch)")){')
    body = _strip_comments(source[start:start + 8000])

    assert "QVector<double> delta(levels.size()+1,0.0);" in body, (
        "the composite curve no longer carries a running capacity, so every "
        "interval asks every stream again")
    assert "delta[loAt+1]+=s.cp;" in body and "delta[hiAt+1]-=s.cp;" in body, (
        "a stream's capacity is no longer added where its run of intervals "
        "begins and removed after it ends")
    assert "cp+=delta[i];" in body, (
        "the interval capacities are filed and never accumulated")
    assert "for(const Stream& s:streams)\n                        if(s.lo<=levels[i-1]" not in body, (
        "the inner scan over every stream is back, which is the quadratic pass "
        "the prefix sum removed")
    assert "std::lower_bound(c.cbegin(),c.cend(),h," in body, (
        "the composite curve is searched linearly for the bracketing "
        "breakpoint again, inside a bisection that runs eighty times")


def test_the_seasonal_period_search_transforms_instead_of_looping() -> None:
    """A series of 24,000 readings spent two seconds deciding its own period.

    With no period column mapped, the period is the lag of the strongest
    autocorrelation — and every lag up to a third of the record was scored by
    its own pass over the record. The centred moving average that follows had
    the same shape, re-adding every reading of the window at every position,
    and the window is one period wide, so it grew with the record too.

    The autocovariance at *every* lag is one inverse transform of the power
    spectrum, and the overlap count — which is what keeps missing readings
    honestly accounted for rather than assumed absent — is the same
    correlation of the validity mask with itself. The moving average simply
    moves.

    **Subseries 1,822 ms to 165 ms, decomposition 2,079 ms to 620 ms, both
    byte-identical.** A periodic series correlates exactly as well at twice its
    period as at its period, so the tie is broken deliberately toward the
    shorter lag rather than by the last bits of the transform.
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Seasonal Decomposition")')
    body = _strip_comments(source[start:start + 9000])

    assert "const int size=nextPowerOfTwo(qMax(4,n*2));" in body, (
        "the period search no longer zero-pads for a transform, so it is back "
        "to one pass over the record per candidate lag")
    assert body.count("fftInPlace(dr,di); fftInPlace(kr,ki);") == 2, (
        "the autocovariance needs BOTH transforms - forward to the power "
        "spectrum and back again - and one of them has gone, so what is read "
        "off as a correlation is a spectrum")
    assert "const double used=std::round(kr[lag]/double(size));" in body, (
        "the overlap count is no longer the correlation of the validity mask, "
        "so missing readings are being assumed absent")
    assert "if(c>best+1e-12*std::abs(best)){ best=c; period=lag; }" in body, (
        "the tie between a period and its multiples is decided by transform "
        "noise again, which reports a season of twenty-four months for a year")
    assert "const auto shift=[&](int j,int sign){" in body, (
        "the centred moving average is rebuilt at every position rather than "
        "moved, which costs the record length times the period")
    assert "if(i>half){ shift(i-half-1,-1); shift(i+half,1); }" in body, (
        "the moving window's running totals are no longer advanced one place "
        "at a time")


def test_an_envelope_is_indexed_before_loadings_are_tested_against_it() -> None:
    """Four fleets of 24,000 loadings against a 24,000-vertex envelope: 12.1 s.

    Whether a loading is within limits is a point-in-polygon test, and it
    walked the whole envelope for every loading — two and a half billion
    straddle tests. The arithmetic was right; it was done far too many times.

    An edge can only matter to a loading whose weight lies between the edge's
    ends, so the edges are filed by the bands of weight they cross and a
    loading consults only its own band. An edge is filed in every band it
    spans, which is one entry per band crossing, so the index is a single pass.

    **12,051 ms to 506 ms, byte-identical — and identical is the right word,
    not "close": crossings are counted by parity, so visiting the same
    straddling edges in a different order gives the same verdict, and each one
    gets the same arithmetic as before.**
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Weight and Balance Envelope")){')
    body = _strip_comments(source[start:start + 6000])

    assert "QVector<QVector<int>> byBand(bands);" in body, (
        "the envelope is no longer indexed by weight, so every loading walks "
        "every edge")
    assert "for(int s=a;s<=b;++s) byBand[s].append(i);" in body, (
        "an edge is no longer filed in every band it spans, so a loading can "
        "miss a crossing and be called within limits when it is not")
    assert "for(const int i:std::as_const(byBand[bandOf(py)])){" in body, (
        "the point-in-polygon test consults the whole envelope again rather "
        "than the band the loading falls in")
    assert body.index("QVector<QVector<int>> byBand(bands);") < body.index("const auto inside=[&](double px,double py){"), (
        "the index is built after the test that uses it")


def test_lowess_is_evaluated_at_a_bounded_number_of_anchors() -> None:
    """A smoother evaluated at more points than the figure has pixels.

    LOWESS fits over a fixed *fraction* of the sample — a quarter here — so
    unlike the Savitzky-Golay window beside it, which is capped at fifty either
    side, its window grows with the record and the whole smoother is quadratic:
    3.3 seconds at 24,000 readings, with nothing to show for it. A local
    regression is smooth by construction, so between two fits a thousandth of
    the record apart there is nothing for the curve to do but go straight.

    The fit is now evaluated at a bounded number of anchors and the readings
    between them are carried on the straight line joining their neighbours,
    which is Cleveland's own `delta` expressed as a count rather than as a
    distance. **3,259 ms to 736 ms, byte-identical** — and below the cap every
    reading is still an anchor, so for anything of ordinary size the old
    behaviour is not merely preserved but literally unchanged.
    """
    source = _backend_source()
    start = source.index('if(in.engine==QLatin1String("Savitzky-Golay Smoothing")')
    body = _strip_comments(source[start:start + 9000])

    assert "constexpr int kMaxFits=2000;" in body, (
        "LOWESS no longer bounds the number of local fits, so its cost is the "
        "record length times a quarter of the record again")
    assert "const int stride=qMax(1,(m+kMaxFits-1)/kMaxFits);" in body, (
        "the anchors are no longer spread across the whole record, so the "
        "smoother either fits everything or stops short of the end")
    assert "if(anchors.isEmpty()||anchors.last()!=m-1) anchors.append(m-1);" in body, (
        "the last reading is no longer an anchor, so the smoothed curve is "
        "extrapolated past its last fit rather than ending on one")
    assert "smooth.y.append(fitted[k]+t*(fitted[k+1]-fitted[k]));" in body, (
        "the readings between anchors are no longer carried on the line "
        "joining them")
    assert "int half=qBound(2,m/40,50);" in body, (
        "the Savitzky-Golay window is no longer capped, so the smoother that "
        "was the linear one of the pair has become quadratic")


def test_a_deliberate_quadratic_still_has_a_limit() -> None:
    """A Voronoi diagram of 24,000 rows ground for a quarter of an hour.

    The clipping is quadratic *deliberately* — it is argued in the source that
    half-plane clipping is the construction that cannot go wrong, where the
    dual of the Delaunay triangulation fails on unbounded cells and cocircular
    sites and fails by drawing a plausible picture rather than an obvious one.
    That argument is sound and the choice stands. **A deliberate quadratic
    still needs a limit**, or the defensible construction becomes an engine
    that simply never returns, which is the one failure worse than a wrong
    picture.

    Two thousand cells is already more than a reader can tell apart. The count
    stops one past the limit so refusing costs nothing, and `explainEmpty`
    names the same number — a blank figure with no explanation is what the
    mosaic and confusion-matrix caps were criticised for before they had one.

    The duplicate-site check was a second, undefended quadratic hiding in the
    same function: it asked the list of sites collected so far whether it
    already held this position, which is a scan per row — seven billion
    comparisons on a survey of 120,000 rows before any tessellation began, and
    the same fault the confusion matrix had.
    """
    source = _backend_source()
    start = source.index("void QtPlotBackend::drawVoronoi(")
    body = _strip_comments(source[start:start + 4000])

    assert "constexpr int kMaxSites=2000;" in body, (
        "the Voronoi tessellation has no limit again, so a scattered survey of "
        "any size grinds instead of refusing")
    assert "if(sx.size()>kMaxSites){ tooMany=true; break; }" in body, (
        "the site count is taken and not acted on, which is a guard that runs "
        "after the work it exists to prevent")
    assert "if(tooMany) return;" in body, (
        "the tessellation proceeds despite having too many sites")
    assert "QSet<QPair<double,double>> taken;" in body, (
        "the duplicate-site check is back to scanning the sites collected so "
        "far, which is a scan per row")
    assert "for(int k=0;k<sx.size()&&!seen;++k)" not in body, (
        "the linear scan for a coincident site has returned")
    assert "const QPair<double,double> key((px==0.0)?0.0:px,(py==0.0)?0.0:py);" in body, (
        "zero is no longer normalised before hashing, so a negative zero and a "
        "positive one are filed apart and a duplicate the old scan caught now "
        "gets its own cell")

    explain = _strip_comments(
        source[source.index("QString QtPlotBackend::explainEmpty("):][:14000])
    assert 'e==QLatin1String("Voronoi Diagram")&&n>=2' in explain, (
        "a refused Voronoi is a blank figure with no explanation, which is "
        "exactly what the category caps were criticised for")
    assert "if(here.size()>2000){ over=true; break; }" in explain, (
        "the explanation counts every distinct position rather than stopping "
        "at the limit, so explaining the refusal costs more than the refusal")


def test_three_engines_share_one_sliding_window_search() -> None:
    """The same quadratic, written out three times.

    The Tauc plot's steepest rise, the Kubelka-Munk absorption edge and the
    creep curve's flattest secondary stage are one search with two sign
    conventions — and each had its own copy, each copying the window out with
    `mid` at every start position and fitting it fresh. Three copies means a
    fault has to be found three times, which is how the first two were fixed in
    this catalogue and the third was not.

    The shared search slides: five running numbers, one point added at the
    leading edge and one dropped at the trailing edge.

    **The Tauc figure moved, for the same reason the growth window did.** The
    fixture's absorption edge is exactly linear above 2.40 eV, so 32 of the 44
    candidate windows tie at precisely the steepest slope and every one of them
    reports a gap of 2.400000 eV. Which of the 32 won was decided by the last
    bits of the accumulation; it is now decided deliberately, and the earliest
    wins — so the tangent is drawn from the absorption onset, which is where a
    Tauc gap is read, rather than from wherever along the straight part the
    arithmetic happened to land.
    """
    source = _backend_source()
    start = source.index("WindowFit slidingBestFit(")
    body = _strip_comments(source[start:start + 2600])

    assert "const auto take=[&](int i,double sign){" in body, (
        "the shared window search no longer slides, so it is back to fitting "
        "each window from scratch")
    assert "if(start>0){ take(start-1,-1.0); take(start+window-1,1.0); }" in body, (
        "the running sums are rebuilt rather than advanced one place")
    assert "if(!(f.slope>bestSlope+1e-12*std::abs(bestSlope))) continue;" in body, (
        "the steepest-window tie is decided by floating-point noise again, so "
        "the drawn tangent moves along a straight edge between builds")
    assert "if(out.at>=0&&!(f.slope<bestSlope-1e-12*std::abs(bestSlope))) continue;" in body, (
        "the flattest-window tie is decided by noise, or the first fittable "
        "window can no longer win and the search starts from a number rather "
        "than from the data")

    whole = _strip_comments(source)
    assert "xs.mid(start,window)" not in whole and "tx.mid(start,window)" not in whole, (
        "an engine is copying its window out again rather than calling the "
        "shared sliding search")
    assert whole.count("slidingBestFit(") == 4, (
        "the shared search has 1 definition and should have exactly 3 callers "
        "- the Tauc plot, the creep curve and the Kubelka-Munk edge; one of "
        "them has gone back to its own copy")
    assert "const WindowFit picked=slidingBestFit(xs,ys,window,true);" in whole, (
        "the Tauc plot no longer asks for the steepest window")
    assert "const WindowFit picked=slidingBestFit(xs,ys,window,false);" in whole, (
        "the creep curve no longer asks for the flattest window - and asking "
        "for the steepest would report the tertiary run-up to rupture as the "
        "minimum creep rate")
    assert "const WindowFit picked=slidingBestFit(tx,ty,window,true);" in whole, (
        "the Kubelka-Munk edge no longer asks for the steepest window")


def _estimators_source() -> str:
    return (ROOT / "native" / "plot2d" / "src"
            / "SurfaceEstimators.cpp").read_text(encoding="utf-8", errors="ignore")


def _estimators_header() -> str:
    return (ROOT / "native" / "plot2d" / "include"
            / "SurfaceEstimators.h").read_text(encoding="utf-8", errors="ignore")


def test_the_triangulation_has_the_limit_its_comment_claimed() -> None:
    """A comment that says a thing is fast is not a measurement.

    `delaunay` carried the note "this is O(n^1.5) in practice and the caller
    caps n". Both halves were false and both were cheap to check. Every
    inserted point tests the circumcircle of *every* triangle standing — no
    adjacency, no point location — so the work is one full scan per point, and
    doubling the sample quadruples the time exactly as that predicts: 4.8 ms at
    500 samples, 92 ms at 2,000, 1,555 ms at 8,000.

    And of the two callers, neither capped anything. The ternary contour handed
    it every row, and ran past three quarters of a minute on 24,000
    compositions without finishing.

    The construction is unchanged — a slow triangulation that is right beats a
    fast one that is subtly wrong, and the alternative is an adjacency rewrite
    whose output order would move every contour label. What changed is that the
    limit the comment claimed now exists, in the header, where a third caller
    cannot miss it.
    """
    header = _estimators_header()
    source = _strip_comments(_estimators_source())

    assert "constexpr int kDelaunayLimit = 3000;" in header, (
        "the triangulation limit is no longer declared where callers can ask "
        "before calling, so a refusal becomes a blank figure")
    assert "if(n<3||n>kDelaunayLimit) return out;" in source, (
        "delaunay triangulates any number of samples again, so a large "
        "scattered set does not come back")
    raw = _estimators_source()
    assert "THIS IS QUADRATIC" in raw, (
        "the triangulation no longer says what its cost actually is - and the "
        "comment it replaced asserted a speed nobody had measured, which is "
        "how two callers came to rely on a cap that did not exist")
    assert "8694 ms" in raw and "92.0 ms" in raw, (
        "the measurements behind the quadratic claim have gone, leaving "
        "another assertion about speed with nothing behind it")


def test_auto_does_not_choose_the_quadratic_method_for_large_samples() -> None:
    """The Auto ladder named the right idea and picked the wrong method.

    Its own comment says a large sample "gets the local method that stays
    O(n)" — and it chose `DelaunayLinear`, which is local to *evaluate* and
    quadratic to *build*. So for exactly the samples the rule existed to
    protect, it selected the one method whose cost grows fastest: **8.7 seconds
    at 16,000 samples**, against **28 ms** for the modified Shepard weighting,
    which is flat from 400 samples to 16,000 because its cost is the number of
    grid nodes rather than the sample count.

    A triangulating method asked for explicitly on too large a sample now falls
    back the same way a structured method asked for on scattered data already
    did — rather than returning an empty field, which a figure reads as
    "nothing was measured here".
    """
    source = _strip_comments(_estimators_source())

    assert "else if(pts.size()<=kDelaunayLimit) chosen=Estimator::DelaunayLinear;" in source, (
        "Auto picks a triangulation without asking whether the sample can be "
        "triangulated")
    assert "else chosen=Estimator::ModifiedShepard;" in source, (
        "Auto has no local method to fall to for a large sample, so it is back "
        "to choosing the quadratic one for the biggest inputs")
    assert ("if((chosen==Estimator::DelaunayLinear||chosen==Estimator::CloughTocher)\n"
            "       &&pts.size()>kDelaunayLimit)\n"
            "        chosen=Estimator::ModifiedShepard;") in source, (
        "a triangulating estimator asked for explicitly on too large a sample "
        "no longer falls back, so it returns an empty field that reads as "
        "'nothing was measured here'")


def test_a_ternary_contour_refuses_before_it_triangulates() -> None:
    """24,000 compositions ran until the probe killed it.

    The engine handed every row to a quadratic triangulation. It now asks the
    limit *before* calling — which is the whole reason the limit is in the
    header — and says on the figure why nothing is contoured, naming the same
    number. A contour map of 24,000 samples over a triangle 500 pixels on a
    side would not have been readable in any case.

    **Over 45 seconds to 5 ms**, and every figure in the gallery unchanged.
    """
    source = _backend_source()
    start = source.index("void QtPlotBackend::drawTernaryContour(")
    body = _strip_comments(source[start:start + 8000])

    assert "if(points.size()>kDelaunayLimit){" in body, (
        "the ternary contour triangulates any number of compositions again")
    assert body.index("if(points.size()>kDelaunayLimit){") < body.index("delaunay(points);"), (
        "the limit is checked after the triangulation it exists to prevent, "
        "which is a guard that does not guard")
    assert "contouring between more than " in body, (
        "a refused ternary contour is an empty triangle with no explanation")


def test_a_sounding_labels_its_isobars_where_they_are() -> None:
    """Twelve pressure labels, none of them against its own line.

    Four sounding charts share one drawing function, and three of them have
    isobars that run level to the left edge of the frame — so a tick at
    `box.left()` is on the line it labels. **A tephigram is a rotation**: its
    isobars slope, and the left edge of the frame is a place most of them never
    reach. Its twelve labels were stacked down the left margin in a column no
    isobar passed through, stopping two thirds of the way down a frame they are
    meant to span.

    The anchor is stated per chart rather than derived, and that is the point of
    this guard. Deriving it is what went wrong at the first attempt:
    `at(coolest,hPa)` looks like the left end of the isobar and is, on an
    emagram and a Stuve — but a **Skew-T applies its skew inside `at`**, so
    there the cool end climbs to the right with height, and the labels walked
    off diagonally across the plot. Asking each chart what shape its isobars are
    cannot make that mistake.

    Separately: two rows are drawn below the frame — the tick numbers, then the
    note saying what the axis is — and only one row and fourteen pixels were
    reserved. The note ran past the bottom of the canvas on **all four** charts
    and had its descenders cut off.
    """
    source = _backend_source()
    start = source.index("void QtPlotBackend::drawSounding(")
    body = _strip_comments(source[start:start + 22000])

    assert ("const QPointF end=(chart==SoundingChart::Tephigram)\n"
            "                          ? at(coolest,hPa)\n"
            "                          : QPointF(box.left(),at(coolest,hPa).y());") in body, (
        "the isobar label anchor is back to one rule for four charts - either "
        "pinned to the frame, which is wrong for the rotated chart, or taken "
        "from at(coolest,hPa), which is wrong for the skewed one")
    assert "const double bottom=target.bottom()-fm.height()*2.4-10.0;" in body, (
        "the frame no longer leaves room for both rows drawn under it, so the "
        "axis note is cut off by the bottom of the canvas")
    assert "p->drawText(QRectF(box.left(),target.bottom()-fm.height()-2.0," in body, (
        "the axis note is hung off the frame again rather than pinned to the "
        "canvas, so the space it needs is not the space reserved for it")
    assert "if(!previousChip.isNull()&&chip.intersects(previousChip)) continue;" in body, (
        "crowded isobars paint their labels over each other again - 1000, 925 "
        "and 850 land within a few pixels on a rotated chart")
    assert "if(chip.top()<box.top())       chip.moveTop(box.top());" in body, (
        "a label that sits inside the frame is no longer kept inside it, so "
        "the topmost isobar's number is drawn over the frame line and the "
        "readout strip above")
