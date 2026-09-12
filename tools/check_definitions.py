#!/usr/bin/env python3
"""Every member function declared in a header must be defined somewhere.

Written after a removal script deleted two definitions as collateral -
`PlotCanvas::setXTransform` and `PlotCanvas::applyVariant` - by matching a
one-line function and swallowing the comment block and whole function that
followed it. The build caught that, but only by luck: the linker reports an
undefined symbol only when something still REFERENCES it. A definition deleted
out from under a declaration that nobody calls yet links perfectly and is simply
gone, and the first person to call it gets a link error with no idea when it
disappeared.

So this does not ask "does it link". It asks the stricter question: is every
declaration backed by a definition. It needs no compiler and takes under a
second.

Two things it has to model, because the first draft of this script did not and
reported 35 findings of which 35 were false:

  - Macro-generated definitions. `GV_SETTER(setZColumn,zColumn_,QString)` and
    `GV_FIELD_SETTER(FieldEstimator,...)` define PlotCanvas::setZColumn and
    PlotCanvas::setFieldEstimator, and a search for "PlotCanvas::setZColumn("
    finds neither. Seventeen of the false findings were this. So the macro
    bodies are read, the name template inside each (`fn`, or `set##Name`) is
    found, and every invocation is expanded.

  - Inline bodies in the header. A `return QUrl::fromLocalFile(path);` inside an
    inline method looks exactly like a declaration of `fromLocalFile` to a
    line-based regex. So brace depth is tracked and only lines at class scope
    are considered.

A scanner that does not model how the code under it actually produces its
symbols will condemn most of a codebase, confidently.

What is deliberately NOT a finding:
  - signals              moc generates the definitions
  - inline bodies        defined in the header itself
  - = default/= delete   the compiler generates them
  - pure virtual         no definition by design
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

TARGETS = {
    "native/plot2d/include/PlotCanvas.h": "PlotCanvas",
    "app/src/AppController.h": "AppController",
    "app/src/ProjectWorkspace.h": "ProjectWorkspace",
}

SOURCE_DIRS = ["native/plot2d/src", "app/src", "native/plot2d/include"]

DECL = re.compile(
    r"""^[ \t]*
        (?:(?:virtual|static|explicit|inline|constexpr|Q_INVOKABLE)\s+)*
        (?P<ret>[A-Za-z_][\w:<>,\s\*&]*?)
        \b(?P<name>\w+)\s*
        \((?P<args>[^;{]*)\)\s*
        (?:const|noexcept|override|final|\s)*
        ;\s*$""",
    re.X,
)

# #define NAME(params) body. Continuations are joined before this runs - a
# greedy `.*` swallows its own trailing backslash, so a multi-line macro body
# has to be folded onto one line first or the body reads as empty and every
# macro-generated definition goes unseen.
MACRO = re.compile(r"^[ \t]*#\s*define\s+(\w+)\(([^)]*)\)(.*)$", re.M)


def join_continuations(text: str) -> str:
    return re.sub(r"\\[ \t]*\r?\n", " ", text)


def strip_block_comments(text: str) -> str:
    return re.sub(r"/\*.*?\*/", "", text, flags=re.S)


def split_top_level(text: str) -> list[str]:
    """Split on commas that are not inside brackets - macro arguments nest."""
    parts, depth, current = [], 0, []
    for ch in text:
        if ch in "([{<":
            depth += 1
        elif ch in ")]}>":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append("".join(current).strip())
            current = []
        else:
            current.append(ch)
    parts.append("".join(current).strip())
    return parts


def macro_generated(text: str, cls: str) -> set[str]:
    """Names that macro invocations in `text` define on `cls`."""
    produced: set[str] = set()
    text = join_continuations(text)
    for macro_name, params, body in MACRO.findall(text):
        parameters = [p.strip() for p in params.split(",") if p.strip()]
        # The name the macro body builds, e.g. `fn` or `set##Name`.
        templates = re.findall(rf"\b{cls}\s*::\s*([A-Za-z_#\w]+)\s*\(", body)
        if not templates:
            continue
        # Every invocation of this macro, arguments included.
        for call in re.finditer(rf"^[ \t]*{macro_name}\(", text, re.M):
            start = call.end()
            depth, index = 1, start
            while index < len(text) and depth:
                if text[index] == "(":
                    depth += 1
                elif text[index] == ")":
                    depth -= 1
                index += 1
            arguments = split_top_level(text[start:index - 1])
            binding = dict(zip(parameters, arguments))
            for template in templates:
                name = template
                for parameter, argument in binding.items():
                    name = re.sub(rf"\b{re.escape(parameter)}\b", argument, name)
                name = name.replace("##", "")
                if re.fullmatch(r"\w+", name):
                    produced.add(name)
    return produced


def definitions(cls: str) -> set[str]:
    names: set[str] = set()
    for folder in SOURCE_DIRS:
        for path in (ROOT / folder).rglob("*.cpp"):
            raw = path.read_text(encoding="utf-8", errors="ignore")
            body = strip_block_comments(raw)
            body = re.sub(r"//[^\n]*", "", body)
            names |= set(re.findall(rf"\b{cls}\s*::\s*(\w+)\s*\(", body))
            names |= macro_generated(raw, cls)
    return names


def declarations(header: Path) -> list[tuple[str, int]]:
    """Member functions this header declares at class scope and does not define."""
    raw = strip_block_comments(header.read_text(encoding="utf-8"))
    found: list[tuple[str, int]] = []
    access = "normal"
    depth = 0
    class_depth: int | None = None

    for number, line in enumerate(raw.splitlines(), 1):
        bare = re.sub(r"//.*$", "", line)
        opening = depth

        if re.match(rf"^\s*class\s+\w+", bare) and class_depth is None:
            class_depth = depth

        section = re.match(r"^\s*(public|protected|private)?\s*(slots|signals)?\s*:", bare)
        if section and (section.group(1) or section.group(2)):
            access = "signals" if section.group(2) == "signals" else "normal"

        depth += bare.count("{") - bare.count("}")

        # Only class-scope lines are declarations. Inside an inline body a
        # `return QUrl::fromLocalFile(p);` is indistinguishable from one.
        if class_depth is None or opening != class_depth + 1:
            continue
        if access == "signals":
            continue
        if re.search(r"=\s*(0|default|delete)\s*;", bare):
            continue
        if re.search(r"\bQ_(PROPERTY|OBJECT|ENUM|CLASSINFO|SIGNALS|SLOTS|DISABLE_COPY)\b", bare):
            continue
        if re.match(r"^\s*(class|struct|enum|using|typedef|friend|template)\b", bare):
            continue

        match = DECL.match(bare)
        if not match:
            continue
        ret, name = match.group("ret").strip(), match.group("name")
        if not ret or ret.endswith(("::", "=", "return")) or name == ret:
            continue
        found.append((name, number))
    return found


def main() -> int:
    problems: list[str] = []
    for relative, cls in TARGETS.items():
        header = ROOT / relative
        if not header.exists():
            print(f"skipped (not present): {relative}")
            continue
        defined = definitions(cls)
        declared = declarations(header)
        missing = [(n, ln) for n, ln in declared if n not in defined]
        print(f"{relative}: {len(declared)} declarations, "
              f"{len(defined)} definitions, {len(missing)} undefined")
        for name, line in missing:
            problems.append(f"  {relative}:{line}  {cls}::{name} declared, never defined")

    if problems:
        print("\nDeclared with no definition anywhere:")
        print("\n".join(problems))
        print("\nEach of these is a link error waiting for its first caller.")
        return 1
    print("\nEvery declaration has a definition.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
