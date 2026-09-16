"""Checks that span two languages or two files.

The most valuable findings in this project's history have all been of one
shape: two places that had to agree, and did not. A QML file on disk but not
in the build list. An engine in the catalogue with nothing to render it. A
property read from QML that C++ never declared. None of these is visible from
inside either file.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

from . import proc
from .model import Finding, describe


def _read_json(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return None


# --------------------------------------------------------------------------
describe(
    "qml_delegate_missing_required_model",
    "A delegate that declares a required property and still uses `model`",
    "Qt 6 stops injecting the implicit `model`, `index` and `modelData` into "
    "a delegate as soon as that delegate declares any `required property`. "
    "Code that used them keeps compiling and fails at runtime with "
    "'ReferenceError: model is not defined'. This project shipped that bug.",
    "A delegate object declaring at least one `required property`, which "
    "references `model`, `index` or `modelData` without declaring those as "
    "required properties itself.",
    "None known. An earlier version of this check looked sixty lines past a "
    "Repeater for the two texts and matched 72 unrelated sites; it was "
    "deleted. This one asks the object tree which properties the delegate "
    "declares, which is the question that was always being asked.")

INJECTED = ("model", "index", "modelData")
DELEGATE_PROPS = {"delegate", "contentItem", "highlight", "header", "footer"}
REPEATERS = {"Repeater", "Instantiator", "ListView", "GridView", "TableView",
             "PathView", "TreeView", "SwipeView", "StackView", "Column", "Row"}


def _is_delegate(obj) -> bool:
    par = obj.parent
    if par is None:
        return False
    if par.type == "Component" and par.parent is not None:
        par = par.parent
    for bnd in par.bindings:
        if bnd.name in DELEGATE_PROPS and bnd.a <= obj.a < bnd.b:
            return True
    return par.type in REPEATERS and obj.type == "Component"


def qml_delegate_missing_required_model(p, out: list) -> None:
    for doc in p.qml:
        for obj in doc.objects:
            required = [pr for pr in obj.props if pr.required]
            if not required or not _is_delegate(obj):
                continue
            declared = {pr.name for pr in obj.props}
            used: dict[str, int] = {}
            for j in range(obj.a, min(obj.b, len(doc.toks))):
                t = doc.toks[j]
                if t.kind != "id" or t.text not in INJECTED:
                    continue
                if t.text in declared:
                    continue
                prev = doc.toks[j - 1].text if j else ""
                if prev in (".", "property", "required"):
                    continue
                nxt = doc.toks[j + 1].text if j + 1 < len(doc.toks) else ""
                if t.text == "model" and nxt not in (".", ")", ",", "]"):
                    continue
                used.setdefault(t.text, t.line)
            if not used:
                continue
            names = ", ".join(sorted(used))
            ln = min(used.values())
            out.append(Finding(
                "qml_delegate_missing_required_model", doc.rel, ln,
                f"{obj.type}#{obj.id}" if obj.id else obj.type,
                f"this delegate declares `required property` and still uses {names}",
                why=("Declaring any required property turns off Qt's implicit "
                     f"injection, so {names} is undefined here at runtime even "
                     "though it compiles."),
                evidence=f"L{required[0].line}: {doc.line_text(required[0].line)}\n"
                         f"L{ln}: {doc.line_text(ln)}",
                severity="high", confidence="likely", category="correctness",
                suggestion=f"Declare `required property` for {names} as well."))


# --------------------------------------------------------------------------
describe(
    "theme_name_missing",
    "A Theme property that does not exist",
    "QML resolves names at runtime, so a size bound to a Theme property "
    "nobody declared is not an error: the text simply comes out at the "
    "default and nobody notices. `Theme.fontSizeLarge` shipped that way.",
    "Every `Theme.<name>` reference in any QML file, checked against the "
    "properties, functions and enum values actually declared in Theme.qml.",
    "Properties added to Theme from C++ at runtime.")


def theme_name_missing(p, out: list) -> None:
    theme = next((d for d in p.qml if d.name == "Theme"), None)
    if theme is None or theme.root is None:
        return
    declared: set = set()
    for obj in theme.root.walk():
        declared |= {pr.name for pr in obj.props}
        declared |= {f.name for f in obj.functions}
        declared |= {s.name for s in obj.signals}
        if obj.id:
            declared.add(obj.id)
    for doc in p.qml:
        seen: set = set()
        for j, t in enumerate(doc.toks):
            if t.kind != "id" or t.text != "Theme":
                continue
            if j + 2 >= len(doc.toks) or doc.toks[j + 1].text != ".":
                continue
            name = doc.toks[j + 2]
            if name.kind != "id" or name.text in declared or name.text in seen:
                continue
            if name.text[:1].isupper():
                continue                       # an enum or nested type
            seen.add(name.text)
            out.append(Finding(
                "theme_name_missing", doc.rel, name.line, "",
                f"`Theme.{name.text}` is not declared in Theme.qml",
                why=("QML resolves this at runtime and finds nothing, so the "
                     "binding silently produces undefined and the default is used."),
                evidence=f"L{name.line}: {doc.line_text(name.line)}",
                severity="high", confidence="likely", category="correctness",
                suggestion=f"Declare `{name.text}` in Theme.qml, or fix the spelling."))


# --------------------------------------------------------------------------
describe(
    "qml_build_list_mismatch",
    "A QML file the build list and the disk disagree about",
    "A file on disk but not in GRAPHVIS_QML_FILES cannot be imported at "
    "runtime; a file in the list but not on disk fails the build. Both are "
    "silent until the moment they are not.",
    "The set difference, both ways, between GRAPHVIS_QML_FILES in "
    "app/CMakeLists.txt and the .qml files under app/qml - except that a "
    "file named ANYWHERE in CMakeLists is treated as accounted for, since a "
    "lazily-loaded view is legitimately added as a raw resource instead.",
    "A file mentioned in CMakeLists only in a comment.")


def qml_build_list_mismatch(p, out: list) -> None:
    cm = p.root / "app" / "CMakeLists.txt"
    qml_dir = p.root / "app" / "qml"
    if not cm.exists() or not qml_dir.is_dir():
        return
    text = cm.read_text(encoding="utf-8", errors="replace")
    if "set(GRAPHVIS_QML_FILES" not in text:
        return
    block = text.split("set(GRAPHVIS_QML_FILES", 1)[1].split(")", 1)[0]
    listed = {l.strip() for l in block.splitlines() if l.strip().endswith(".qml")}
    on_disk = {f"qml/{q.relative_to(qml_dir).as_posix()}"
               for q in qml_dir.rglob("*.qml")}
    for missing in sorted(listed - on_disk):
        out.append(Finding(
            "qml_build_list_mismatch", "app/CMakeLists.txt", 1, "",
            f"{missing} is listed in the build and not on disk",
            why="The build references a file that is not there.",
            evidence=missing, severity="high", confidence="certain",
            category="build",
            suggestion="Remove it from the list, or restore the file."))
    for stray in sorted(on_disk - listed):
        # A file can be added to the build in another way - as a raw resource
        # with QT_RESOURCE_ALIAS, for something a Loader fetches by URL. The
        # question is whether the build accounts for the file at all, not
        # whether it is in this one list. Asking about the list reported
        # app/qml/lazy/VtkViewport.qml, which CMakeLists mentions three times
        # and explains in a comment.
        if stray in text or Path(stray).name in text:
            continue
        out.append(Finding(
            "qml_build_list_mismatch", f"app/{stray}", 1, "",
            "on disk and named nowhere in app/CMakeLists.txt, so it cannot "
            "be imported",
            why="Qt resources are built from CMakeLists; a file it never "
                "mentions does not exist at runtime.",
            evidence=stray, severity="high", confidence="likely",
            category="build",
            suggestion="Add it to GRAPHVIS_QML_FILES, or delete it."))


# --------------------------------------------------------------------------
describe(
    "catalogue_unverified",
    "A graph the catalogue offers that nothing measures",
    "An engine offered in the picker with no measured check behind it can "
    "render a blank figure and nobody finds out until a user picks it.",
    "Entries in config/graph_catalogue.json whose `verified` flag is false, "
    "and entries carrying no flag at all.",
    "None known.")


def catalogue_unverified(p, out: list) -> None:
    path = p.root / "config" / "graph_catalogue.json"
    cat = _read_json(path)
    if not cat:
        return
    entries = [e for c in cat.get("categories", []) for e in c.get("entries", [])]
    unverified = sorted({e.get("engine", "?") for e in entries
                         if e.get("verified") is False})
    for name in unverified[:40]:
        out.append(Finding(
            "catalogue_unverified", "config/graph_catalogue.json", 1, name,
            f"`{name}` is offered in the picker with nothing measuring it",
            why="No measured check exists for this engine, so a blank or wrong "
                "figure would not be caught.",
            evidence=name, severity="medium", confidence="certain",
            category="verification",
            suggestion="Add a measured check, or mark the entry unverified in the UI."))
    missing = [e.get("name", "?") for e in entries if "verified" not in e]
    if missing:
        out.append(Finding(
            "catalogue_unverified", "config/graph_catalogue.json", 1, "",
            f"{len(missing)} catalogue entries carry no verified flag at all",
            why="The flag is what the picker uses to mark a graph unverified; "
                "an entry without one is neither verified nor marked.",
            evidence=", ".join(missing[:8]), severity="medium",
            confidence="certain", category="verification",
            suggestion="Run tools/mark_verified_engines.py."))


# --------------------------------------------------------------------------
describe(
    "engine_without_catalogue_entry",
    "An engine the backend can draw and the catalogue does not offer",
    "Work that shipped and cannot be reached. The code is there, tested or "
    "not, and no user can select it.",
    "String literals compared against a variable named `engine` inside the "
    "prepareEngineGroup dispatchers, checked against the engine names in "
    "config/graph_catalogue.json. The `engine ==` shape is required, not just "
    "the literal: without it the check matched axis labels and reported "
    "twenty engines that were never engines.",
    "Engines deliberately reachable only as a variant of another.")


def _alias_of_known(p, rel: str, tok: int, known: set) -> bool:
    """Is another spelling in the same condition already in the catalogue?

    Walks OUTWARD through the nesting. The literal sits inside
    `QStringLiteral( ... )`, so stopping at the first enclosing bracket looked
    only at the string's own wrapper and never reached the `if ( ... || ... )`
    that holds the other spelling.
    """
    from .cpplex import matching
    u = next((x for x in p.units if x.rel == rel), None)
    if u is None:
        return False
    toks = u.toks
    start = tok
    for _level in range(4):
        depth = 0
        opener = -1
        j = start - 1
        while j > 0:
            t = toks[j]
            if t.kind == "punct":
                if t.text == ")":
                    depth += 1
                elif t.text == "(":
                    if depth == 0:
                        opener = j
                        break
                    depth -= 1
                elif t.text in (";", "{", "}"):
                    return False
            j -= 1
        if opener == -1:
            return False
        close = matching(toks, opener)
        for k in range(opener + 1, min(close, len(toks))):
            if toks[k].kind == "str" and toks[k].text.strip('"') in known:
                return True
        start = opener
    return False


def engine_without_catalogue_entry(p, out: list) -> None:
    cat = _read_json(p.root / "config" / "graph_catalogue.json")
    if not cat:
        return
    known = {e.get("engine") for c in cat.get("categories", [])
             for e in c.get("entries", [])}
    handled: dict[str, tuple] = {}
    for u in p.units:
        if "QtPlotBackendEngines" not in u.rel:
            continue
        for i, t in enumerate(u.toks):
            if t.kind != "str" or not t.is_code:
                continue
            # The dispatch is `in.engine == QLatin1String("Rank-Abundance Curve")`.
            # Requiring that shape matters: matching any lowercase string
            # operand picked up axis labels - "abundance", "activity", "age" -
            # and reported twenty engines that do not exist.
            k = i - 1
            while k >= 0 and (not u.toks[k].is_code or
                              u.toks[k].text in ("(", "QLatin1String",
                                                 "QStringLiteral")):
                k -= 1
            if k < 0 or u.toks[k].text != "==":
                continue
            m = k - 1
            while m >= 0 and not u.toks[m].is_code:
                m -= 1
            if m < 0 or u.toks[m].text != "engine":
                continue
            name = t.text.strip('"')
            if not name or not name[:1].isupper():
                continue
            handled.setdefault(name, (u.rel, t.line, i))
    orphans = sorted(set(handled) - known)
    for name in orphans[:20]:
        rel, line, tok = handled[name]
        # One engine is often reached by more than one spelling:
        #     if (engine == "Gompertz H\u2082 Kinetics"
        #         || engine == "Gompertz H2 Kinetics")
        # The ASCII fallback has no catalogue entry of its own and does not
        # need one - the engine is reachable. Reporting it said an engine was
        # unreachable when the line above it reaches it.
        if _alias_of_known(p, rel, tok, known):
            continue
        out.append(Finding(
            "engine_without_catalogue_entry", rel, line, name,
            f"`{name}` is handled in the backend and is in no catalogue entry",
            why="Nothing in the picker maps to it, so no user can select it.",
            evidence=f"{rel}:{line}", severity="low",
            confidence="possible", category="dead-code",
            suggestion="Add a catalogue entry, or remove the handler."))


# --------------------------------------------------------------------------
describe(
    "notify_never_emitted",
    "A Q_PROPERTY whose NOTIFY signal is never emitted",
    "QML binds to a property through its NOTIFY signal. If nothing ever "
    "emits it, every binding on that property is evaluated once and then "
    "frozen - the UI simply stops updating, with no error anywhere.",
    "The NOTIFY signal named by each Q_PROPERTY, looked for as an EMISSION - "
    "`emit sig()`, `sig()`, `this->sig()` - anywhere in the C++ tree, rather "
    "than as a mention. See _emits below for why the distinction is the whole "
    "check.",
    "Signals emitted through a macro or from generated code.")


def _connect_targets(p) -> set:
    """Every signal or slot named as the TARGET of a `connect(...)` call.

    A SIGNAL-TO-SIGNAL CONNECT IS AN EMISSION, and it is the idiom this code
    base uses for derived properties: `windowTitleChanged` is never written
    with `emit`, it is connected to three other signals, so it fires whenever
    any of them does. Reading only for `emit` reported it as frozen, which was
    wrong and was caught the first time the repaired check ran over the real
    tree.

    The TARGET position specifically, argument four. A signal named as a
    connect SOURCE is something other code emits, and counting that position
    too would excuse exactly the signal this check exists to find.
    """
    cached = getattr(p, "_connect_target_cache", None)
    if cached is not None:
        return cached
    from .cpplex import matching
    names: set = set()
    for u in p.units:
        toks = u.toks
        for i, tok in enumerate(toks):
            if tok.kind != "id" or not tok.is_code or tok.text != "connect":
                continue
            k = i + 1
            while k < len(toks) and not toks[k].is_code:
                k += 1
            if k >= len(toks) or toks[k].text != "(":
                continue
            end = matching(toks, k)
            if end <= k:
                continue
            args: list = [[]]
            depth = 0
            for j in range(k + 1, end):
                tk = toks[j]
                if not tk.is_code:
                    continue
                if tk.text in "([{":
                    depth += 1
                elif tk.text in ")]}":
                    depth -= 1
                if tk.text == "," and depth == 0:
                    args.append([])
                    continue
                args[-1].append(tk)
            if len(args) < 4:
                continue
            for j, tk in enumerate(args[3]):
                if tk.kind == "id" and j and args[3][j - 1].text == "::":
                    names.add(tk.text)
    try:
        p._connect_target_cache = names
    except Exception:                                   # pragma: no cover
        pass
    return names


def _emits(p, sig: str) -> bool:
    """Is this signal ever EMITTED, as opposed to merely named?

    THIS CHECK COULD NOT FIRE. It used to ask whether the name appeared more
    than once in the tree - and a never-emitted signal always appears exactly
    twice, once in the `Q_PROPERTY(... NOTIFY sig)` that names it and once in
    the `void sig();` that declares it. So every never-emitted NOTIFY signal
    was skipped by the guard meant to find it, and "no findings" read exactly
    like "nothing wrong". That is the same shape as `find_top_level` looking
    for a `(` its own loop had already consumed, which is the defect this
    project's self-test file opens by describing.

    Counting is the wrong question anyway: one signal is often the NOTIFY of
    several properties, so the baseline is not a constant to subtract. What
    makes a signal emitted is a CALL, so that is what is looked for - or a
    connect that carries another signal's call through to it; see
    _connect_targets.
    """
    if sig in _connect_targets(p):
        return True
    for u in p.units:
        toks = u.toks
        for i, tok in enumerate(toks):
            if tok.kind != "id" or not tok.is_code or tok.text != sig:
                continue
            prev = ""
            j = i - 1
            while j >= 0 and not toks[j].is_code:
                j -= 1
            if j >= 0:
                prev = toks[j].text
            nxt = ""
            k = i + 1
            while k < len(toks) and not toks[k].is_code:
                k += 1
            if k < len(toks):
                nxt = toks[k].text
            if prev == "emit":
                return True
            # A call, but not the declaration of one: `void sig();` names it
            # and emits nothing, and neither does `NOTIFY sig)`.
            if nxt == "(" and prev not in ("void", "&", "*"):
                return True
    return False


def notify_never_emitted(p, out: list) -> None:
    for td in p.types:
        for prop in td.properties:
            sig = prop.get("notify")
            if not sig:
                continue
            if _emits(p, sig):
                continue
            out.append(Finding(
                "notify_never_emitted", td.file, prop.get("line", td.line),
                f"{td.name}::{prop.get('name', '?')}",
                f"NOTIFY signal `{sig}` is declared and never emitted",
                why=("QML re-evaluates a binding when this signal fires. Nothing "
                     "fires it, so every binding on this property is evaluated "
                     "once and never again."),
                evidence=f"Q_PROPERTY({prop.get('type', '')} {prop.get('name', '')} "
                         f"NOTIFY {sig})",
                severity="high", confidence="likely", category="correctness",
                suggestion=f"Emit {sig} where the value changes."))


# --------------------------------------------------------------------------
describe(
    "invokable_in_private_section",
    "A Q_INVOKABLE QML can see and C++ hides",
    "Q_INVOKABLE in a private section is callable from QML and not from C++. "
    "It usually means the access specifier moved and the method did not.",
    "Methods marked Q_INVOKABLE whose enclosing access section is private or "
    "protected.",
    "Deliberate: a method meant only for QML.")


def invokable_in_private_section(p, out: list) -> None:
    for td in p.types:
        for fn in td.methods:
            if "invokable" not in fn.quals:
                continue
            if fn.access in ("private", "protected"):
                out.append(Finding(
                    "invokable_in_private_section", td.file, fn.line, fn.qual,
                    f"`{fn.name}` is Q_INVOKABLE inside a {fn.access} section",
                    why="QML can call it; C++ cannot. That is rarely deliberate.",
                    evidence=f"L{fn.line}", severity="low", confidence="likely",
                    category="design",
                    suggestion="Move it to the public section, or drop Q_INVOKABLE."))


# --------------------------------------------------------------------------
describe(
    "declaration_without_definition",
    "A declared function with no definition anywhere",
    "It links only because nothing calls it. The first caller is a link error.",
    "Delegated to tools/check_definitions.py, so that there is one definition "
    "of this rule rather than two that can disagree.",
    "Whatever that tool's own limits are.")


def declaration_without_definition(p, out: list) -> None:
    script = p.root / "tools" / "check_definitions.py"
    if not script.exists():
        return
    res = proc.run([sys.executable, str(script)], cwd=p.root, timeout=180)
    if res is None:
        return
    for line in (res.stdout + res.stderr).splitlines():
        if re.search(r"\bundefined\b", line) and not line.strip().endswith("0 undefined"):
            out.append(Finding(
                "declaration_without_definition", "tools/check_definitions.py", 1,
                "", line.strip()[:140],
                why="Reported by the project's own definition guard.",
                evidence=line.strip()[:200], severity="high", confidence="certain",
                category="build"))


# --------------------------------------------------------------------------
describe(
    "catalogue_counts_disagree",
    "A catalogue summary number that the catalogue's own entries contradict",
    "The graph library and the Graph packs menu show these numbers. When they "
    "disagree with the entries they claim to summarise, the program states a "
    "figure that is simply false - and nothing on screen looks wrong, so it "
    "survives until somebody adds up the packs by hand. It had already "
    "happened: packs[].entryCount summed to 2116 against an entry_count of "
    "2122, because one script recomputed the headline and another recomputed "
    "the packs and nothing compared them.",
    "Recomputes entry_count, engine_count, category_count and every pack's "
    "entryCount from config/graph_catalogue.json's own entries, and reports "
    "any that differs from the value stored beside them.",
    "None. The entries are the catalogue; a summary of them has exactly one "
    "correct value.")


def catalogue_counts_disagree(p, out: list) -> None:
    rel = "config/graph_catalogue.json"
    doc = _read_json(p.root / "config" / "graph_catalogue.json")
    if not isinstance(doc, dict):
        return
    cats = doc.get("categories")
    if not isinstance(cats, list) or not cats:
        return
    entries = [e for c in cats for e in c.get("entries", [])]

    counted = {
        "category_count": len(cats),
        "entry_count": len(entries),
        "engine_count": len({e.get("engine") for e in entries}),
    }
    for key, right in counted.items():
        stored = doc.get(key)
        if stored is not None and stored != right:
            out.append(Finding(
                "catalogue_counts_disagree", rel, 1, key,
                f"`{key}` says {stored}; the entries say {right}",
                why="The entries are the catalogue. A header that disagrees "
                    "with them is shown to the user as fact.",
                evidence=f"{key}: stored {stored}, counted {right}",
                severity="medium", confidence="certain", category="correctness",
                suggestion="Run tools/assign_catalogue_packs.py, which "
                           "recomputes every one of these from the entries."))

    per_pack: dict[str, int] = {}
    for e in entries:
        per_pack[e.get("pack")] = per_pack.get(e.get("pack"), 0) + 1
    for pack in doc.get("packs", []):
        if not isinstance(pack, dict) or "entryCount" not in pack:
            continue
        right = per_pack.get(pack.get("id"), 0)
        if pack["entryCount"] != right:
            out.append(Finding(
                "catalogue_counts_disagree", rel, 1, str(pack.get("id")),
                f"pack `{pack.get('id')}` claims {pack['entryCount']} entries; "
                f"{right} entries name it",
                why="This number is printed on the Graph packs menu.",
                evidence=f"{pack.get('id')}: stored {pack['entryCount']}, "
                         f"counted {right}",
                severity="medium", confidence="certain", category="correctness",
                suggestion="Run tools/assign_catalogue_packs.py."))


# --------------------------------------------------------------------------
describe(
    "help_topic_missing",
    "A menu that opens the help at a topic which does not exist",
    "Opening the help at a renamed topic does not fail - it shows the FIRST "
    "topic instead, so the menu item appears to work and quietly answers a "
    "different question. This is the silent name mismatch that keeps costing "
    "this project time, in the one place where the reader has no way to tell "
    "they were sent somewhere else.",
    "Every `startAt = \"id\"` and `openHelp(\"id\", ...)` literal in app/qml, "
    "checked against the ids in config/help_topics.json. Section arguments "
    "are checked against that file's section ids the same way.",
    "A topic id built at run time rather than written as a literal; none "
    "exists, and one would not be checkable from here.")


def help_topic_missing(p, out: list) -> None:
    doc = _read_json(p.root / "config" / "help_topics.json")
    if not isinstance(doc, dict):
        return
    topics = {t.get("id") for t in doc.get("topics", []) if isinstance(t, dict)}
    sections = {s.get("id") for s in doc.get("sections", []) if isinstance(s, dict)}
    if not topics:
        return

    # ("", "") is how a call site says "open at the contents", so the empty
    # string is a legitimate argument rather than a missing id.
    for doc_qml in p.qml:
        for m in re.finditer(r'\bstartAt\s*[:=]\s*"([^"]*)"', doc_qml.src):
            if m.group(1) and m.group(1) not in topics:
                out.append(_help_finding(doc_qml, m, "topic", m.group(1)))
        for m in re.finditer(r'\bstartSection\s*[:=]\s*"([^"]*)"', doc_qml.src):
            if m.group(1) and m.group(1) not in sections:
                out.append(_help_finding(doc_qml, m, "section", m.group(1)))
        for m in re.finditer(r'\bopenHelp\s*\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\)',
                             doc_qml.src):
            if m.group(1) and m.group(1) not in topics:
                out.append(_help_finding(doc_qml, m, "topic", m.group(1)))
            if m.group(2) and m.group(2) not in sections:
                out.append(_help_finding(doc_qml, m, "section", m.group(2)))


def _help_finding(doc_qml, m, what: str, name: str) -> Finding:
    line = doc_qml.src.count("\n", 0, m.start()) + 1
    return Finding(
        "help_topic_missing", doc_qml.rel, line, name,
        f"opens the help at {what} `{name}`, which is not in help_topics.json",
        why="The browser falls back to the first topic, so this menu item "
            "silently answers a different question.",
        evidence=m.group(0),
        severity="medium", confidence="certain", category="correctness",
        suggestion="Use an id that exists, or add the topic to "
                   "config/help_written.json and run tools/make_help.py.")


# --------------------------------------------------------------------------
describe(
    "help_out_of_date",
    "The shipped manual does not match its sources",
    "config/help_topics.json is generated from config/help_written.json and "
    "the program's own tables. If it has not been regenerated, the manual in "
    "the executable describes a version of the program that no longer exists "
    "- and it is the one document nobody can check against the thing it "
    "describes.",
    "Runs tools/make_help.py --check, which rebuilds the document in memory "
    "and compares it byte for byte.",
    "None. The comparison is against the generator's own output.")


def help_out_of_date(p, out: list) -> None:
    script = p.root / "tools" / "make_help.py"
    if not script.exists():
        return
    res = proc.run([sys.executable, str(script), "--check"], cwd=p.root,
                   timeout=120)
    if res is None or res.returncode == 0:
        return
    detail = (res.stderr or res.stdout).strip().splitlines()
    out.append(Finding(
        "help_out_of_date", "config/help_topics.json", 1, "",
        detail[0][:140] if detail else "the generated manual is out of date",
        why="The shipped manual is generated; this copy is not what the "
            "generator would produce from the current sources.",
        evidence="\n".join(detail[:6])[:400],
        severity="medium", confidence="certain", category="correctness",
        suggestion="Run tools/make_help.py."))


# --------------------------------------------------------------------------
describe(
    "citation_styles_stale",
    "The picker's style list is not the formatter's style list",
    "The interface offers citation styles from a generated file and the "
    "science service formats from the Python table it is generated from. When "
    "they diverge, a style can be offered and then not applied - which is "
    "precisely the fault this area was rebuilt for: a `style` argument the "
    "formatter accepted and ignored, so a person who chose IEEE got APA with "
    "nothing on screen to say their choice had done nothing.",
    "Runs tools/make_citation_styles.py --check, which regenerates the list "
    "from graphvis_science/citation_styles.py and compares it byte for byte "
    "with config/citation_styles.json.",
    "None. It is the same script that writes the file.")


def citation_styles_stale(p, out: list) -> None:
    script = p.root / "tools" / "make_citation_styles.py"
    if not script.exists():
        return
    res = proc.run([sys.executable, str(script), "--check"], cwd=p.root,
                   timeout=120)
    if res is None or res.returncode == 0:
        return
    detail = (res.stderr or res.stdout).strip().splitlines()
    out.append(Finding(
        "citation_styles_stale", "config/citation_styles.json", 1, "",
        detail[0][:140] if detail else "the generated style list is out of date",
        why="The picker reads this file and the formatter reads the table it "
            "is generated from. A style offered by one and unknown to the "
            "other is an interface that accepts a choice it cannot honour.",
        evidence="\n".join(detail[:6])[:400],
        severity="medium", confidence="certain", category="correctness",
        suggestion="Run tools/make_citation_styles.py."))


# --------------------------------------------------------------------------
describe(
    "designed_colourmaps_stale",
    "The designed colour-vision palettes are not what the designer produces",
    "ColourMapsCvd.h holds palettes built for a named colour-vision "
    "deficiency, and the separation each one achieves is written into the "
    "file as a comment and into its registry row as a number. A file edited "
    "by hand, or left behind when the designer changed, states a measurement "
    "nobody took - and that number is what the picker orders the colourblind "
    "category by.",
    "Runs tools/design_cvd_colourmaps.py --check, which rebuilds every "
    "palette and compares the header byte for byte.",
    "None. It is the same script that writes the file.")


def designed_colourmaps_stale(p, out: list) -> None:
    script = p.root / "tools" / "design_cvd_colourmaps.py"
    if not script.exists():
        return
    res = proc.run([sys.executable, str(script), "--check"], cwd=p.root,
                   timeout=600)
    if res is None:
        p.checks_not_run.append(
            ("designed_colourmaps_stale",
             "the designer did not finish within its time limit"))
        return
    if res.returncode == 0:
        return
    # EXIT 2 MEANS THE DESIGNER COULD NOT RUN, which is not a statement about
    # the file. It needs numpy, the machine running the audit need not have it,
    # and this check used to report the resulting ImportError traceback as
    # "the designed colour-vision palettes are not what the designer produces" -
    # a verdict on a header, from a missing package, complete with a Python
    # stack trace where the evidence should be.
    #
    # The sibling check below had already learnt this and says so in a comment
    # six lines long. This one was written next to it and did not apply it,
    # which is this project's most-recorded root cause: a rule correct for one
    # case, not carried to the case beside it.
    #
    # Told apart by the EXIT CODE rather than by reading the output for words
    # like "stale". The script knows which of the three things happened and now
    # says so in the one place that cannot be ambiguous.
    if res.returncode != 1:
        detail = (res.stderr or res.stdout).strip().splitlines()
        p.checks_not_run.append(
            ("designed_colourmaps_stale",
             detail[0][:160] if detail else
             f"the designer exited {res.returncode} without checking anything"))
        return
    detail = (res.stderr or res.stdout).strip().splitlines()
    out.append(Finding(
        "designed_colourmaps_stale",
        "native/plot2d/include/ColourMapsCvd.h", 1, "",
        detail[0][:140] if detail else "the designed palettes are out of date",
        why="Each palette carries the separation it was measured at, and the "
            "picker orders by that number. A stale file means the order is "
            "from a measurement of something else.",
        evidence="\n".join(detail[:6])[:400],
        severity="medium", confidence="certain", category="correctness",
        suggestion="Run tools/design_cvd_colourmaps.py."))


# --------------------------------------------------------------------------
describe(
    "theme_cvd_tags_stale",
    "A theme whose colour-vision tag no longer matches its colours",
    "The tags decide which themes the picker offers once a colour-vision mode "
    "is set. A theme whose colours were edited without re-measuring keeps its "
    "old verdict, so it is either offered to a reader it no longer serves or "
    "hidden from one it does - and both are invisible from the file.",
    "Runs tools/measure_theme_cvd.py --check, which re-simulates every theme's "
    "positive/warning/danger colours through each deficiency and compares the "
    "verdict against the `safe` field on that theme's own line.",
    "None. It is the same tool that writes the tags.")


def theme_cvd_tags_stale(p, out: list) -> None:
    script = p.root / "tools" / "measure_theme_cvd.py"
    if not script.exists():
        return
    res = proc.run([sys.executable, str(script), "--check"], cwd=p.root,
                   timeout=180)
    if res is None or res.returncode == 0:
        return
    text = (res.stderr + res.stdout).strip()
    # numpy is not guaranteed on the machine running the audit, and "the tool
    # could not run" is not "the tags are wrong". Reporting the first as the
    # second is how a check gets ignored.
    #
    # RETURNING QUIETLY WAS ONLY HALF RIGHT. It keeps the false finding out of
    # the report and puts nothing in its place, so a check that has not run for
    # months reads exactly like a check that passes, with no way to tell from
    # the report which it is. It is recorded now, where the reader is already
    # being told what the pass does not cover.
    #
    # By EXIT CODE, not by looking for the word "stale" in the output. The word
    # test could not tell "no numpy" from "no verdict" and handed both to the
    # report as a raw traceback, under a heading that promises a sentence.
    if res.returncode != 1:
        p.checks_not_run.append(
            ("theme_cvd_tags_stale",
             text.splitlines()[0][:160] if text else
             f"the theme measurer exited {res.returncode} without a verdict"))
        return
    out.append(Finding(
        "theme_cvd_tags_stale", "app/qml/Theme.qml", 1, "",
        text.splitlines()[0][:140],
        why="The measured verdict and the tag on the theme line disagree.",
        evidence=text[:400], severity="medium", confidence="certain",
        category="correctness",
        suggestion="Run tools/measure_theme_cvd.py --write."))


# --------------------------------------------------------------------------
describe(
    "vision_mode_list_mismatch",
    "Theme.qml names a different number of colour-vision modes from C++",
    "Theme.visionKinds is indexed by the program's colour-vision mode number. "
    "A mode added to colourVisionNames() and not here reads as no mode at all, "
    "so the theme filter silently stops applying for it - and a mode removed "
    "leaves every later index pointing at the wrong deficiency, which filters "
    "against the wrong measurement.",
    "Counts the QStringLiteral entries in colourVisionNames() in ColourVision.h "
    "and the entries in Theme.qml's visionKinds, and compares.",
    "None. They are one list written twice, which is why this exists.")


def vision_mode_list_mismatch(p, out: list) -> None:
    header = next(p.root.rglob("ColourVision.h"), None)
    theme = p.root / "app" / "qml" / "Theme.qml"
    if header is None or not theme.exists():
        return
    src = header.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"colourVisionNames\s*\(\s*\)\s*\{(.*?)\}", src, re.S)
    if not m:
        return
    cpp = len(re.findall(r"QStringLiteral\s*\(", m.group(1)))

    qml_src = theme.read_text(encoding="utf-8", errors="replace")
    q = re.search(r"property var visionKinds:\s*\[(.*?)\]", qml_src, re.S)
    if not q:
        return
    qml = len(re.findall(r'"', q.group(1))) // 2
    if cpp and qml and cpp != qml:
        out.append(Finding(
            "vision_mode_list_mismatch", "app/qml/Theme.qml", 1, "visionKinds",
            f"visionKinds has {qml} entries; colourVisionNames() has {cpp}",
            why="visionKinds is indexed by the mode number the C++ side "
                "hands over, so a different length means some index maps to "
                "the wrong deficiency or to none.",
            evidence=f"C++ {cpp}, QML {qml}",
            severity="medium", confidence="certain", category="correctness",
            suggestion="Add the missing kind to Theme.qml's visionKinds and "
                       "run tools/measure_theme_cvd.py --write."))


ALL = [
    qml_delegate_missing_required_model,
    theme_name_missing,
    qml_build_list_mismatch,
    catalogue_unverified,
    engine_without_catalogue_entry,
    notify_never_emitted,
    invokable_in_private_section,
    declaration_without_definition,
    catalogue_counts_disagree,
    help_topic_missing,
    help_out_of_date,
    citation_styles_stale,
    designed_colourmaps_stale,
    theme_cvd_tags_stale,
    vision_mode_list_mismatch,
]
