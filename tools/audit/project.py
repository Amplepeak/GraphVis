"""Loading the tree once, so every check reads the same model.

Two separate lessons are baked into the file selection here.

The first: `native/target/` is Rust's build directory. It contains a vendored
copy of zstd.h among other things, and the previous audit was reading it -
auditing a dependency's header, reporting nothing useful, and paying for it on
every pass. A tree walk that does not know what it is walking is not cheap
just because it is simple.

The second: parse once. The audit's whole cost is parsing, so the parse
belongs in one place that every check shares, rather than each check
re-reading the files it cares about. That is the same fix that took the
previous version from 9.1 s to 2.2 s, applied before rather than after.
"""
from __future__ import annotations

import time
from collections import Counter, defaultdict
from pathlib import Path

from .cppparse import Func, TypeDef, Unit, parse_file

SKIP_DIRS = {
    "target", "build", "out", ".git", "__pycache__", "node_modules",
    "third_party", "vendor", "stage", "_deps", "CMakeFiles", ".venv",
    "dist", "build-reports", "graph-check",
}

CPP_SUFFIX = {".cpp", ".cc", ".cxx", ".h", ".hpp", ".hxx"}


def _keep(p: Path, root: Path) -> bool:
    try:
        parts = p.relative_to(root).parts
    except ValueError:
        return False
    return not any(part in SKIP_DIRS for part in parts[:-1])


class Project:
    """Every source file in the tree, parsed once."""

    def __init__(self, root: Path):
        self.root = root
        self.units: list[Unit] = []
        self.funcs: list[Func] = []
        self.types: list[TypeDef] = []
        self.by_type: dict[str, TypeDef] = {}
        self.models: dict[int, object] = {}
        self.qml: list = []
        self.python: list = []
        self.cpp_tokens: Counter = Counter()
        self.qml_tokens: Counter = Counter()
        self.call_sites: dict[str, int] = defaultdict(int)
        self.timings: dict[str, float] = {}
        self.parse_errors: list[tuple[str, str]] = []
        # A CHECK THAT COULD NOT RUN IS NOT A CHECK THAT PASSED.
        #
        # Some checks answer their question by running a tool - the colourmap
        # designer, the theme measurer - and those tools need packages the
        # machine running the audit may not have. There are two wrong things to
        # do about that and both have been done here:
        #
        #   * report it as a finding about the FILE. The report then said "the
        #     designed colour-vision palettes are not what the designer
        #     produces" and printed a Python traceback as its evidence, on a
        #     machine where the only thing wrong was that numpy is not
        #     installed. That is a check crying wolf, which is how a check gets
        #     ignored and then deleted.
        #   * return quietly, which is what the sibling check did. Silence makes
        #     a check that never runs look exactly like a check that passes, and
        #     nothing anywhere says otherwise.
        #
        # So a check that cannot run says so, here, and the report prints it
        # under its own heading beside "what this pass did not look at" - where
        # a reader is already being told what the pass does not cover.
        self.checks_not_run: list[tuple[str, str]] = []

    # ---- discovery ------------------------------------------------------
    def cpp_files(self) -> list[Path]:
        out: list[Path] = []
        for d in (self.root / "native", self.root / "app" / "src",
                  self.root / "app" / "include"):
            if not d.is_dir():
                continue
            for p in d.rglob("*"):
                if p.suffix in CPP_SUFFIX and p.is_file() and _keep(p, self.root):
                    out.append(p)
        for d in (self.root / "native",):
            for p in d.rglob("include/*.h"):
                if p.is_file() and _keep(p, self.root) and p not in out:
                    out.append(p)
        return sorted(set(out))

    def qml_files(self) -> list[Path]:
        d = self.root / "app" / "qml"
        if not d.is_dir():
            return []
        return sorted(p for p in d.rglob("*.qml") if _keep(p, self.root))

    def python_files(self) -> list[Path]:
        out: list[Path] = []
        for d in (self.root / "services" / "python", self.root / "tools"):
            if d.is_dir():
                out += [p for p in d.rglob("*.py") if _keep(p, self.root)]
        return sorted(out)

    def rel(self, p: Path) -> str:
        try:
            return str(p.relative_to(self.root)).replace("\\", "/")
        except ValueError:
            return str(p).replace("\\", "/")

    # ---- loading ---------------------------------------------------------
    def load_cpp(self) -> None:
        t0 = time.time()
        for p in self.cpp_files():
            try:
                u = parse_file(p, self.rel(p))
            except Exception as exc:                  # never let one file stop the pass
                self.parse_errors.append((self.rel(p), f"{type(exc).__name__}: {exc}"))
                continue
            self.units.append(u)
            self.funcs.extend(u.funcs)
            self.types.extend(u.types)
            for td in u.types:
                self.by_type.setdefault(td.name, td)
            for t in u.toks:
                if t.kind == "id" and t.is_code:
                    self.cpp_tokens[t.text] += 1
        self.timings["parse_cpp"] = time.time() - t0

    def load_flow(self) -> None:
        from . import cppflow
        t0 = time.time()
        for u in self.units:
            for f in u.funcs:
                if f.body is not None:
                    try:
                        self.models[id(f)] = cppflow.build(f, u.toks)
                    except Exception as exc:
                        self.parse_errors.append(
                            (f"{u.rel}:{f.line}", f"flow {type(exc).__name__}: {exc}"))
        self.timings["resolve_cpp"] = time.time() - t0

    def model(self, f: Func):
        return self.models.get(id(f))

    def load_qml(self) -> None:
        from .qmlparse import parse_qml
        t0 = time.time()
        for p in self.qml_files():
            try:
                doc = parse_qml(p, self.rel(p))
            except Exception as exc:
                self.parse_errors.append((self.rel(p), f"qml {type(exc).__name__}: {exc}"))
                continue
            self.qml.append(doc)
            for t in doc.toks:
                if t.kind == "id":
                    self.qml_tokens[t.text] += 1
        self.timings["parse_qml"] = time.time() - t0

    def load_python(self) -> None:
        from .pyparse import parse_python
        t0 = time.time()
        for p in self.python_files():
            try:
                mod = parse_python(p, self.rel(p))
            except Exception as exc:
                self.parse_errors.append((self.rel(p), f"py {type(exc).__name__}: {exc}"))
                continue
            self.python.append(mod)
        self.timings["parse_python"] = time.time() - t0

    def load(self) -> "Project":
        self.load_cpp()
        self.load_flow()
        self.load_qml()
        self.load_python()
        return self

    # ---- convenience -----------------------------------------------------
    def bodies(self):
        for u in self.units:
            for f in u.funcs:
                if f.body is not None:
                    m = self.models.get(id(f))
                    if m is not None:
                        yield u, f, m

    def stats(self) -> dict:
        return {
            "cpp_files": len(self.units),
            "cpp_lines": sum(u.lines for u in self.units),
            "functions": len(self.funcs),
            "functions_with_bodies": sum(1 for u in self.units for f in u.funcs
                                         if f.body is not None),
            "types": len(self.types),
            "qml_files": len(self.qml),
            "qml_lines": sum(d.lines for d in self.qml),
            "python_files": len(self.python),
            "python_lines": sum(m.lines for m in self.python),
            "tokens": sum(len(u.toks) for u in self.units),
        }
