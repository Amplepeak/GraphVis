"""Python checks, over the exact tree `ast` gives us.

Nothing here approximates. If a finding is wrong it is because the rule is
wrong, not because the parse was - which makes these the cheapest checks in
the audit to trust and the cheapest to fix.
"""
from __future__ import annotations

import ast
import re
from collections import defaultdict

import pathlib

from . import proc
from .model import Finding, describe

DUNDER_CALLED = {"__init__", "__repr__", "__str__", "__enter__", "__exit__",
                 "__eq__", "__hash__", "__len__", "__iter__", "__next__",
                 "__call__", "__getitem__", "__setitem__", "__contains__",
                 "__post_init__", "__lt__", "__gt__", "__bool__", "__del__"}


def _name_uses(mod) -> set:
    used: set = set()
    for node in ast.walk(mod.tree):
        if isinstance(node, ast.Name):
            used.add(node.id)
        elif isinstance(node, ast.Attribute):
            used.add(node.attr)
        elif isinstance(node, ast.Str) if hasattr(ast, "Str") else False:
            pass
    return used


# --------------------------------------------------------------------------
describe(
    "py_mutable_default",
    "A mutable default argument",
    "A default list or dict is created once, at definition time, and shared "
    "by every call. Appending to it in one call changes it for all the "
    "others - the classic Python trap, and silent.",
    "Function parameters whose default is a list, dict or set literal, or a "
    "call to list(), dict() or set().",
    "None known.")


def py_mutable_default(p, out: list) -> None:
    for mod in p.python:
        if mod.tree is None:
            continue
        for node in ast.walk(mod.tree):
            if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                continue
            for d in list(node.args.defaults) + list(node.args.kw_defaults):
                if d is None:
                    continue
                bad = isinstance(d, (ast.List, ast.Dict, ast.Set)) or (
                    isinstance(d, ast.Call) and isinstance(d.func, ast.Name)
                    and d.func.id in ("list", "dict", "set"))
                if not bad:
                    continue
                out.append(Finding(
                    "py_mutable_default", mod.rel, node.lineno, node.name,
                    f"`{node.name}` has a mutable default argument",
                    why="The default object is created once and shared by every "
                        "call, so a change in one call is visible in the next.",
                    evidence=f"L{node.lineno}: {mod.line_text(node.lineno)}",
                    severity="medium", confidence="certain", category="correctness",
                    suggestion="Default to None and build the value inside."))
                break


# --------------------------------------------------------------------------
describe(
    "py_except_pass",
    "An except that swallows the error",
    "The failure leaves no trace and the caller sees success.",
    "except handlers whose body is only `pass` or `...` AND carry no comment "
    "saying why. A handler that explains itself is doing the right thing and "
    "is not reported.",
    "None known. A handler whose reason is given somewhere other than a "
    "comment in the handler itself.")


def _has_reason(mod, node) -> bool:
    """Does this handler SAY why it is empty?

    The rationale for this check has always been "handlers that are
    deliberately empty should say so", and a handler that does say so was
    still being reported - which punishes exactly the code that did the right
    thing. A comment on the `except` line or inside the handler counts;
    `ast` discards comments, so the source lines are read directly.
    """
    lines = mod.src.split("\n")
    last = getattr(node, "end_lineno", node.lineno) or node.lineno
    for n in range(node.lineno, min(last, len(lines)) + 1):
        text = lines[n - 1]
        hash_at = text.find("#")
        if hash_at == -1:
            continue
        before = text[:hash_at]
        if before.count('"') % 2 or before.count("'") % 2:
            continue                      # a # inside a string
        if text[hash_at:].strip(" #").strip():
            return True
    return False


def py_except_pass(p, out: list) -> None:
    for mod in p.python:
        if mod.tree is None:
            continue
        for node in ast.walk(mod.tree):
            if not isinstance(node, ast.ExceptHandler):
                continue
            body = [s for s in node.body if not isinstance(s, ast.Expr)
                    or not isinstance(getattr(s, "value", None), ast.Constant)]
            if len(body) == 1 and isinstance(body[0], ast.Pass) \
                    and not _has_reason(mod, node):
                out.append(Finding(
                    "py_except_pass", mod.rel, node.lineno, "",
                    "this except discards the error silently",
                    why="Nothing is logged and nothing is re-raised, so the caller "
                        "cannot tell the work failed.",
                    evidence=f"L{node.lineno}: {mod.line_text(node.lineno)}",
                    severity="medium", confidence="certain", category="correctness",
                    suggestion="Log it, or say in a comment why it is safe."))


# --------------------------------------------------------------------------
describe(
    "py_bare_except",
    "A bare `except:`",
    "It catches KeyboardInterrupt and SystemExit too, so it can swallow the "
    "user's attempt to stop the program.",
    "except handlers with no exception type.",
    "None known.")


def py_bare_except(p, out: list) -> None:
    for mod in p.python:
        if mod.tree is None:
            continue
        for node in ast.walk(mod.tree):
            if isinstance(node, ast.ExceptHandler) and node.type is None:
                out.append(Finding(
                    "py_bare_except", mod.rel, node.lineno, "",
                    "a bare `except:` catches everything, including Ctrl-C",
                    why="KeyboardInterrupt and SystemExit inherit from "
                        "BaseException, so this catches them too.",
                    evidence=f"L{node.lineno}: {mod.line_text(node.lineno)}",
                    severity="low", confidence="certain", category="correctness",
                    suggestion="`except Exception:` is almost always what is meant."))


# --------------------------------------------------------------------------
describe(
    "py_unused_import",
    "An import nothing uses",
    "Dead weight, and occasionally a rename left half-finished.",
    "Imported names that appear nowhere else in the module, excluding "
    "`__init__.py` re-exports and anything listed in `__all__`.",
    "Imports purely for their side effects. A name used only inside a quoted "
    "type annotation is NOT reported: whole-word occurrences across the file "
    "are counted, not just AST references.")


def py_unused_import(p, out: list) -> None:
    for mod in p.python:
        if mod.tree is None or mod.path.name == "__init__.py":
            continue
        used = _name_uses(mod)
        exported: set = set()
        for node in ast.walk(mod.tree):
            if isinstance(node, ast.Assign):
                for t in node.targets:
                    if isinstance(t, ast.Name) and t.id == "__all__":
                        for el in getattr(node.value, "elts", []):
                            if isinstance(el, ast.Constant):
                                exported.add(el.value)
        src = mod.src
        for node in ast.walk(mod.tree):
            if not isinstance(node, (ast.Import, ast.ImportFrom)):
                continue
            if isinstance(node, ast.ImportFrom) and node.module == "__future__":
                continue
            for alias in node.names:
                if alias.name == "*":
                    continue
                local = alias.asname or alias.name.split(".")[0]
                if local in used or local in exported:
                    continue
                # A name can appear only inside a quoted annotation
                # ("Callable[[int], int]") or a __all__ entry. Counting whole
                # words across the file catches both; testing for the exact
                # quoted string did not.
                if len(re.findall(r"\b" + re.escape(local) + r"\b", src)) > 1:
                    continue
                out.append(Finding(
                    "py_unused_import", mod.rel, node.lineno, "",
                    f"`{local}` is imported and never used",
                    why="The name appears nowhere else in the module.",
                    evidence=f"L{node.lineno}: {mod.line_text(node.lineno)}",
                    severity="low", confidence="likely", category="dead-code",
                    suggestion="Remove the import."))


# --------------------------------------------------------------------------
describe(
    "py_dead_function",
    "A module-level function nothing calls",
    "Code nobody runs is code nobody is testing.",
    "Module-level functions whose name appears nowhere else in any Python "
    "file, are not dunders, do not start with an underscore, are not "
    "decorated, and are not test functions.",
    "Functions called by name through getattr, or exported as an API. Names "
    "that appear in an import statement anywhere are counted as used.")


def py_dead_function(p, out: list) -> None:
    uses: dict[str, int] = defaultdict(int)
    for mod in p.python:
        if mod.tree is None:
            continue
        for node in ast.walk(mod.tree):
            if isinstance(node, ast.Name):
                uses[node.id] += 1
            elif isinstance(node, ast.Attribute):
                uses[node.attr] += 1
            elif isinstance(node, ast.Constant) and isinstance(node.value, str):
                uses[node.value] += 1
                # Registries name their entries "module.path:function". The
                # bare name never appears anywhere else, so without splitting
                # these, every registered adapter reads as dead code.
                for part in re.split(r"[.:/#]", node.value):
                    if part:
                        uses[part] += 1
            elif isinstance(node, (ast.Import, ast.ImportFrom)):
                # `from runtime import literature_dir` IS a use. Counting only
                # Name and Attribute nodes missed it and called an imported,
                # called function dead.
                for alias in node.names:
                    uses[alias.name.split(".")[-1]] += 1
                    if alias.asname:
                        uses[alias.asname] += 1
    for mod in p.python:
        if mod.tree is None:
            continue
        for node in mod.tree.body:
            if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                continue
            n = node.name
            if n.startswith("_") or n in DUNDER_CALLED or n.startswith("test_"):
                continue
            if node.decorator_list:
                continue
            if uses.get(n, 0) > 0:
                continue
            out.append(Finding(
                "py_dead_function", mod.rel, node.lineno, n,
                f"`{n}` is defined and never called",
                why="The name appears in no other Python file, and not as a string.",
                evidence=f"L{node.lineno}: {mod.line_text(node.lineno)}",
                severity="low", confidence="possible", category="dead-code",
                suggestion="Remove it, or wire it up."))


# --------------------------------------------------------------------------
describe(
    "py_syntax_error",
    "A Python file that does not parse",
    "It cannot run. If it is imported anywhere, that import fails.",
    "Any file `ast.parse` rejects.",
    "None - it is the compiler's own answer.")


def py_syntax_error(p, out: list) -> None:
    for mod in p.python:
        if mod.error:
            out.append(Finding(
                "py_syntax_error", mod.rel, 1, "",
                f"does not parse: {mod.error}",
                why="Python itself rejects this file.",
                evidence=mod.error, severity="high", confidence="certain",
                category="build", suggestion="Fix the syntax error."))




# --------------------------------------------------------------------------
describe(
    "py_flakes",
    "A dead assignment, a dead import or an f-string with nothing in it",
    "The audit reads Python with its own parser, which knows the shapes it was "
    "taught and no others. pyflakes resolves names properly, and what it finds "
    "here is the residue a refactor leaves: a variable computed and never read, "
    "an import kept after its last use, an f-string whose placeholder was "
    "removed. None of those is a defect on its own - and all of them are where "
    "a real one hides. One of them was hiding a real one: an unused "
    "`from runtime import literature_dir` was the only thing making that "
    "function look alive, and it had had no caller for as long as it existed.",
    "Runs pyflakes over services/python and tools, and reports two of its "
    "messages: an unused local, and an f-string with no placeholder. An unused "
    "import is left to `py_unused_import`, which already answers that.",
    "Two categories are dropped and both for a reason measured here. "
    "`redefinition of unused` fires on the service's operation dispatch, where "
    "each `if op==...` branch imports what that branch needs - mutually "
    "exclusive by construction and correct. And a line carrying `noqa` is "
    "skipped, because pyflakes does not honour it and this project uses it to "
    "record a deliberate import - `from uncertainties import umath` registers "
    "the maths functions and binds nothing.")


# NOT "imported but unused": `py_unused_import` above already reports that,
# and two checks answering one question means every dead import is two
# findings - which is the same "one question, one answer" this project keeps
# recording, arriving as report noise instead of as a wrong number.
_FLAKE_REPORTED = (
    "assigned to but never used",
    "f-string is missing placeholders",
)


def py_flakes(p, out: list) -> None:
    import shutil
    import sys as _sys
    runner = shutil.which("pyflakes")
    cmd = [runner] if runner else [_sys.executable, "-m", "pyflakes"]
    targets = [str(p.root / "services" / "python"), str(p.root / "tools")]
    res = proc.run(cmd + targets, cwd=p.root, timeout=180)
    if res is None:
        p.checks_not_run.append(("py_flakes", "pyflakes did not finish in time"))
        return
    # pyflakes exits 1 when it has findings and 0 when it has none; anything
    # else - not installed, a usage error - is the tool not running rather than
    # the code being clean, and silence there is what makes a dead check look
    # like a passing one.
    if res.returncode not in (0, 1):
        first = (res.stderr or res.stdout).strip().splitlines()
        p.checks_not_run.append(
            ("py_flakes", first[0][:160] if first else
             f"pyflakes exited {res.returncode}"))
        return
    for raw in (res.stdout or "").splitlines():
        m = re.match(r"^(.*?):(\d+):\d+: (.*)$", raw.strip())
        if not m:
            continue
        path, line, message = m.group(1), int(m.group(2)), m.group(3)
        if not any(k in message for k in _FLAKE_REPORTED):
            continue
        try:
            rel = str(pathlib.Path(path).resolve().relative_to(p.root)).replace("\\", "/")
        except ValueError:
            continue
        # The source line, for the noqa test and for the evidence. A file
        # pyflakes could read and this cannot is a real oddity, so it is
        # reported rather than swallowed - the audit's own py_except_pass rule,
        # applied to the check that enforces it.
        try:
            source = pathlib.Path(path).read_text(
                encoding="utf-8", errors="ignore").split("\n")[line - 1]
        except (OSError, IndexError) as exc:
            p.checks_not_run.append(
                ("py_flakes", f"{rel}:{line} could not be read back: {exc}"))
            continue
        # pyflakes does not honour noqa; this project uses it to record a
        # deliberate import. See describe() above.
        if "noqa" in source:
            continue
        out.append(Finding(
            "py_flakes", rel, line, "", message,
            why="Not a defect by itself. It is what a refactor leaves behind, "
                "and it is where a real one hides: the only reason "
                "`literature_dir` looked alive was an import nothing used.",
            evidence=f"L{line}: {source.strip()[:160]}",
            severity="low", confidence="certain", category="dead-code",
            suggestion="Remove it, or use it."))


ALL = [
    py_flakes,
    py_syntax_error,
    py_mutable_default,
    py_except_pass,
    py_bare_except,
    py_unused_import,
    py_dead_function,
]
