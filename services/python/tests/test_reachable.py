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
