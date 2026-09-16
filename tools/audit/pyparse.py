"""Python analysis, using the standard library's own parser.

Python is the one language here where an exact tree is free: `ast` is the
same parser CPython uses, so there is no reason to approximate.
"""
from __future__ import annotations

import ast
from pathlib import Path


class PyModule:
    __slots__ = ("path", "rel", "src", "tree", "lines", "error")

    def __init__(self, path: Path, rel: str, src: str, tree, lines: int):
        self.path = path
        self.rel = rel
        self.src = src
        self.tree = tree
        self.lines = lines
        self.error = ""

    def line_text(self, line: int) -> str:
        try:
            return self.src.split("\n")[line - 1].strip()
        except IndexError:
            return ""


def parse_python(path: Path, rel: str) -> PyModule:
    src = path.read_text(encoding="utf-8", errors="replace")
    mod = PyModule(path, rel, src, None, src.count("\n") + 1)
    try:
        mod.tree = ast.parse(src, filename=str(path))
    except SyntaxError as exc:
        mod.error = f"line {exc.lineno}: {exc.msg}"
    return mod
