"""QML checks over the object tree.

The headline one is `qml_bool_from_and`, and it is worth explaining because
it is the bug this project actually shipped, twenty-four warnings per run:

    property bool parametric: canvas && canvas.parametric

In JavaScript `a && b` evaluates to A when A is falsy - not to false. So when
`canvas` is undefined, the binding produces `undefined`, Qt refuses to assign
it to a bool, and the property silently keeps its DEFAULT value. The value is
identical whether the code is right or wrong; only the warning distinguishes
them. That is why this has to be found statically, and why it needs the
declared type, which no regular expression has.
"""
from __future__ import annotations

import re

import pathlib

from . import proc
from .cpplex import text_of
from .model import Finding, describe

# A leading `!` coerces to a real boolean, so `!a.b && c` is safe.
BOOLISH = re.compile(r"^(true|false|!.*|.*(===|!==|==|!=|<|>|<=|>=).*)$")

# Properties Qt itself sets, or that exist on every item.
IMPLICIT = {
    "width", "height", "x", "y", "z", "visible", "enabled", "opacity",
    "parent", "anchors", "children", "data", "state", "states", "clip",
    "focus", "scale", "rotation", "implicitWidth", "implicitHeight",
    "activeFocus", "antialiasing", "smooth", "containmentMask", "baselineOffset",
}

# `anchors.fill` sets position AND size, so width/height/x/y all conflict.
# `anchors.centerIn` sets position only - width and height are then required,
# not a conflict. Treating them alike reported 15 items here, 13 of which
# were ordinary centred boxes with a size.
FILL_CONFLICTS = {"width", "height", "x", "y"}
CENTERIN_CONFLICTS = {"x", "y"}
ANCHOR_H = {"anchors.left", "anchors.right", "anchors.horizontalCenter"}
ANCHOR_V = {"anchors.top", "anchors.bottom", "anchors.verticalCenter"}

# Words the lexer returns as identifiers that are really literals.
LITERALS = {"true", "false", "null", "undefined", "NaN", "Infinity"}

REPEATERS = {"Repeater", "ListView", "GridView", "TableView", "PathView",
             "TreeView", "Column", "Row", "Flow", "SwipeView", "StackView"}
DELEGATE_PROPS = {"delegate", "contentItem", "handle", "background",
                  "highlight", "header", "footer", "section.delegate"}


def _txt(doc, a: int, b: int) -> str:
    return text_of(doc.toks, a, b)


def _line(doc, n: int) -> str:
    return doc.line_text(n)


# --------------------------------------------------------------------------
describe(
    "qml_bool_from_and",
    "A bool property bound to `a && b`",
    "In JavaScript `a && b` evaluates to A when A is falsy, so when A is "
    "undefined the binding yields undefined rather than false. Qt refuses to "
    "assign that to a bool and leaves the property at its DEFAULT - which "
    "means the value is the same whether the code is correct or broken, and "
    "only the runtime warning tells them apart. This project shipped fourteen "
    "instances of it.",
    "A property declared `bool` whose binding is a top-level `&&` or `||` "
    "chain with an operand that is an OBJECT reference rather than a boolean: "
    "not a comparison, not a negation, not a literal, and not ending in a "
    "name declared `bool` anywhere in QML or in a Q_PROPERTY. That last test "
    "is the discriminator - `canvas && canvas.parametric` is the bug, "
    "`root.hasData && !root.app.busy` is not, and only the declared type of "
    "the final component tells them apart.",
    "An operand that is genuinely always defined but whose type this cannot "
    "see - a property supplied by a parent component, for instance.\n"
    "Deliberately NOT reported: `a && b ? x : y`, where the && is the "
    "ternary's condition and is coerced to boolean before anything is "
    "assigned.")


def _has_top_level(doc, a: int, b: int, op: str) -> bool:
    depth = 0
    for j in range(a, min(b, len(doc.toks))):
        t = doc.toks[j]
        if t.kind != "punct":
            continue
        if t.text in "([{":
            depth += 1
        elif t.text in ")]}":
            depth -= 1
        elif depth == 0 and t.text == op:
            return True
    return False


def _top_level_split(doc, a: int, b: int, op: str) -> list[tuple[int, int]]:
    out = []
    depth = 0
    start = a
    for j in range(a, b):
        t = doc.toks[j]
        if t.kind == "punct":
            if t.text in "([{":
                depth += 1
            elif t.text in ")]}":
                depth -= 1
            elif t.text == op and depth == 0:
                out.append((start, j))
                start = j + 1
    if start < b:
        out.append((start, b))
    return out



def _bool_property_names(p) -> set:
    """Every property name declared `bool` anywhere in the QML tree."""
    cached = getattr(p, "_bool_props", None)
    if cached is None:
        cached = set()
        for d in p.qml:
            for o in d.objects:
                for pr in o.props:
                    if pr.type == "bool":
                        cached.add(pr.name)
        for td in getattr(p, "types", []):
            for prop in td.properties:
                if prop.get("type", "").strip() == "bool":
                    cached.add(prop.get("name", ""))
        try:
            p._bool_props = cached
        except AttributeError:
            # __slots__ object: no cache, so it is recomputed next time
            pass
    return cached


def _is_object_reference(doc, a: int, b: int, p) -> bool:
    """Is this operand an OBJECT that may be undefined, rather than a bool?

    This is the whole discriminator. `canvas && canvas.parametric` is the bug:
    `canvas` is an object reference, undefined until it is assigned, so the
    expression yields undefined. `root.hasData && !root.app.busy` is not:
    `hasData` is a declared bool, so the expression is a real boolean.

    Reporting every `&&` under a bool property flagged nine bindings here that
    were all fine. The declared type of the LAST component is what separates
    them, and that is only knowable from the object tree.
    """
    toks = [t for t in doc.toks[a:b] if t.kind in ("id", "num", "str", "punct")]
    if not toks:
        return False
    if any(t.text in ("(", ")") for t in toks):
        return False              # a call or a parenthesised expression
    names = [t.text for t in toks if t.kind == "id"]
    if not names:
        return False
    last = names[-1]
    if last in _bool_property_names(p):
        return False
    if re.match(r"^(is|has|can|should|was|are|show|use|enable)[A-Z_]", last):
        return False
    if last in ("visible", "enabled", "checked", "active", "activeFocus",
                "focus", "pressed", "hovered", "valid", "running",
                "completed", "current", "busy", "modal", "open", "opened"):
        return False
    return True


def qml_bool_from_and(p, out: list) -> None:
    for doc in p.qml:
        for obj in doc.objects:
            for prop in obj.props:
                if prop.type != "bool" or prop.binding is None:
                    continue
                a, b = prop.binding.a, prop.binding.b
                if b <= a:
                    continue
                # `!!( ... )` is the fix; do not report it
                first = doc.toks[a]
                if first.text == "!" and a + 1 < b and doc.toks[a + 1].text == "!":
                    continue
                if first.text == "{":
                    continue
                # `a && b ? x : y` - the && is the ternary's CONDITION, not the
                # assigned value, so it is coerced to boolean and cannot leak
                # undefined into the property. Only a && that IS the value
                # matters here.
                if _has_top_level(doc, a, b, "?"):
                    continue
                for op in ("&&", "||"):
                    parts = _top_level_split(doc, a, b, op)
                    if len(parts) < 2:
                        continue
                    risky = []
                    for s, e in parts[:-1] if op == "&&" else parts:
                        seg = _txt(doc, s, e).strip()
                        if not seg or BOOLISH.match(seg):
                            continue
                        if not _is_object_reference(doc, s, e, p):
                            continue
                        risky.append(seg)
                    if risky:
                        out.append(Finding(
                            "qml_bool_from_and", doc.rel, prop.line,
                            f"{obj.type}#{obj.id}" if obj.id else obj.type,
                            f"bool `{prop.name}` is bound to `{op}` over `{risky[0][:40]}`",
                            why=("When that operand is undefined the whole "
                                 "expression is undefined, Qt refuses the "
                                 "assignment, and the property keeps its default "
                                 "value while printing 'Unable to assign "
                                 "[undefined] to bool'."),
                            evidence=f"L{prop.line}: {_line(doc, prop.line)}",
                            severity="high", confidence="likely",
                            category="correctness",
                            suggestion="Wrap the expression in `!!( ... )`."))
                        break


# --------------------------------------------------------------------------
describe(
    "qml_anchors_conflict",
    "An anchor and an explicit size on one item",
    "Qt applies one and warns about the other, and which one wins depends on "
    "evaluation order. The layout looks right until something reorders.",
    "An object that sets `anchors.fill` and also width, height, x or y; or "
    "that sets `anchors.centerIn` and also x or y. centerIn does not size an "
    "item, so a width alongside it is required rather than conflicting.",
    "Setting width alongside anchors.fill is legal when the anchor is "
    "conditional, which this does not model.")


def qml_anchors_conflict(p, out: list) -> None:
    for doc in p.qml:
        for obj in doc.objects:
            names = {b.name: b.line for b in obj.bindings}
            if "anchors.fill" in names:
                fill, conflicts = names["anchors.fill"], FILL_CONFLICTS
                kind = "anchors.fill"
            elif "anchors.centerIn" in names:
                fill, conflicts = names["anchors.centerIn"], CENTERIN_CONFLICTS
                kind = "anchors.centerIn"
            else:
                continue
            clash = sorted(conflicts & set(names))
            if not clash:
                continue
            ln = min(names[c] for c in clash)
            out.append(Finding(
                "qml_anchors_conflict", doc.rel, ln,
                f"{obj.type}#{obj.id}" if obj.id else obj.type,
                f"{kind} and an explicit {', '.join(clash)} on the same item",
                why="Qt honours one and warns about the other; which one wins "
                    "depends on the order the bindings are evaluated.",
                evidence=f"L{fill}: {_line(doc, fill)}\nL{ln}: {_line(doc, ln)}",
                severity="medium", confidence="likely", category="correctness",
                suggestion="Drop the anchor, or drop the explicit geometry."))


# --------------------------------------------------------------------------
describe(
    "qml_completed_in_delegate",
    "Component.onCompleted inside a repeated delegate",
    "A delegate is created and destroyed as the view scrolls, so anything "
    "onCompleted does - registering, reparenting, appending to a model - "
    "happens again on every recycle and never gets undone. This project has "
    "already had one reparenting bug of exactly this shape.",
    "A non-empty `Component.onCompleted` handler on an object inside a "
    "delegate, contentItem, highlight or other per-item property of a view "
    "or repeater.",
    "Delegates in a view that is never scrolled or rebuilt.")


def qml_completed_in_delegate(p, out: list) -> None:
    for doc in p.qml:
        for obj in doc.objects:
            for h in obj.handlers:
                if h.name not in ("Component.onCompleted", "onCompleted"):
                    continue
                body = _txt(doc, h.a, h.b).strip()
                if body in ("{}", "{ }", ""):
                    continue
                inside = None
                for anc in [obj] + list(obj.ancestors()):
                    par = anc.parent
                    if par is None:
                        continue
                    if par.type in REPEATERS or anc.type == "Component":
                        inside = par
                        break
                    for bnd in par.bindings:
                        if bnd.name in DELEGATE_PROPS and \
                                bnd.a <= anc.a < bnd.b:
                            inside = par
                            break
                    if inside:
                        break
                if inside is None:
                    continue
                out.append(Finding(
                    "qml_completed_in_delegate", doc.rel, h.line,
                    f"{obj.type}#{obj.id}" if obj.id else obj.type,
                    f"Component.onCompleted runs on every recycle of this "
                    f"{inside.type} delegate",
                    why=("Delegates are created and destroyed as the view "
                         "scrolls. Whatever this does happens again each time "
                         "and is never undone."),
                    evidence=f"L{h.line}: {_line(doc, h.line)}",
                    severity="medium", confidence="likely", category="correctness",
                    suggestion="Move it to the view, or make it idempotent."))


# --------------------------------------------------------------------------
describe(
    "qml_unused_property",
    "A declared property nothing reads",
    "A property declared and never read is either dead weight or a rename "
    "that was only half done - the old name still declared, the new one used.",
    "A property declared on an object whose name appears nowhere else in any "
    "QML file and in no C++ source, and which is not an override of a known "
    "Qt property.",
    "Properties read only from C++ through a name built at runtime, and "
    "properties that exist to be set by a parent component. A read-only "
    "property is exempt when the object declares three or more of that type: "
    "that is an enumeration or a scale spelled out, and half an enumeration "
    "is worse than an unused one.")


def _vocabulary_types(obj) -> set:
    """Types this object declares a whole VOCABULARY of.

    `readonly property int sidebarNone: 0 / sidebarLeft: 1 / sidebarRight: 2`
    is one enumeration spelled out, and a spacing scale is one scale. Using
    only part of it today does not make the rest dead - deleting the unused
    members would leave a half-enumeration, which is worse than leaving it and
    actively misleading to the next reader.

    Three or more read-only siblings of one type is the test.
    """
    counts: dict = {}
    for pr in obj.props:
        if pr.readonly:
            counts[pr.type] = counts.get(pr.type, 0) + 1
    return {t for t, n in counts.items() if n >= 3}


def qml_unused_property(p, out: list) -> None:
    for doc in p.qml:
        for obj in doc.objects:
            vocab = _vocabulary_types(obj)
            for prop in obj.props:
                if prop.name in IMPLICIT or prop.is_alias or prop.required:
                    continue
                if prop.default:
                    continue
                if prop.readonly and prop.type in vocab:
                    continue
                uses = p.qml_tokens.get(prop.name, 0)
                if uses > 1:
                    continue
                if p.cpp_tokens.get(prop.name, 0):
                    continue
                handler = "on" + prop.name[:1].upper() + prop.name[1:] + "Changed"
                if p.qml_tokens.get(handler, 0) or p.cpp_tokens.get(handler, 0):
                    continue
                out.append(Finding(
                    "qml_unused_property", doc.rel, prop.line,
                    f"{obj.type}#{obj.id}" if obj.id else obj.type,
                    f"`{prop.name}` is declared and never read",
                    why=("The name appears once in the whole QML tree - this "
                         "declaration - and nowhere in C++."),
                    evidence=f"L{prop.line}: {_line(doc, prop.line)}",
                    severity="low", confidence="possible", category="dead-code",
                    suggestion="Remove it, or finish the rename it is left over from."))


# --------------------------------------------------------------------------
describe(
    "qml_unresolved_id",
    "A reference to an id that does not exist",
    "An unresolvable id is a runtime ReferenceError that Qt reports once per "
    "evaluation, and a binding that silently never produces a value.",
    "A lowercase identifier used at the head of a member expression inside a "
    "binding, which is not: an id declared in this file, a property or "
    "function in scope, a JavaScript local, a known global, a QML built-in, "
    "or a C++ context property. Anything the model cannot see is treated as "
    "resolvable, so this errs heavily towards silence.",
    "Ids injected by a parent component, and roles supplied by a model - "
    "both of which are why the earlier regex version produced 1,033 false "
    "positives and had to be deleted.")

QML_GLOBALS = {
    "console", "Qt", "Math", "JSON", "Date", "Number", "String", "Object",
    "Array", "Boolean", "parseInt", "parseFloat", "isNaN", "isFinite",
    "undefined", "null", "true", "false", "this", "parent", "index",
    "model", "modelData", "section", "styleData", "text", "arguments",
    "print", "gc", "RegExp", "Error", "Promise", "Map", "Set", "Symbol",
    "encodeURIComponent", "decodeURIComponent", "NaN", "Infinity",
}


def _scope_names(obj) -> set:
    """Every name visible from inside `obj`: its own members and its ancestors'."""
    names = set(QML_GLOBALS)
    chain = [obj] + list(obj.ancestors())
    for o in chain:
        if o.id:
            names.add(o.id)
        for pr in o.props:
            names.add(pr.name)
        for f in o.functions:
            names.add(f.name)
        for s in o.signals:
            names.add(s.name)
    return names


def qml_unresolved_id(p, out: list) -> None:
    for doc in p.qml:
        file_ids = set(doc.ids)
        # Anything imported, any component in the same directory, and every
        # id or property anywhere in the tree is treated as possibly in scope.
        known_types = {d.name for d in p.qml}
        reported: set = set()
        for obj in doc.objects:
            visible = _scope_names(obj) | file_ids | known_types
            for bnd in list(obj.bindings) + list(obj.handlers):
                if bnd.name == "id":
                    continue
                for j in range(bnd.a, min(bnd.b, len(doc.toks))):
                    t = doc.toks[j]
                    if t.kind != "id" or not t.text[:1].islower():
                        continue
                    prev = doc.toks[j - 1].text if j else ""
                    if prev in (".", "function", "var", "let", "const", "property"):
                        continue
                    nxt = doc.toks[j + 1].text if j + 1 < len(doc.toks) else ""
                    if nxt != ".":
                        continue        # only head-of-member-expression
                    if t.text in visible:
                        continue
                    if p.cpp_tokens.get(t.text, 0) or p.qml_tokens.get(t.text, 0) > 1:
                        continue
                    key = (doc.rel, t.text)
                    if key in reported:
                        continue
                    reported.add(key)
                    out.append(Finding(
                        "qml_unresolved_id", doc.rel, t.line,
                        f"{obj.type}#{obj.id}" if obj.id else obj.type,
                        f"`{t.text}` is used here but declared nowhere in scope",
                        why=("Not an id in this file, not a property or function "
                             "of this object or any ancestor, not a known global, "
                             "and not a name C++ exposes."),
                        evidence=f"L{t.line}: {_line(doc, t.line)}",
                        severity="medium", confidence="possible",
                        category="correctness",
                        suggestion="Check the spelling, or the id it should refer to."))


# --------------------------------------------------------------------------
describe(
    "qml_binding_overwritten",
    "A property with both a binding and an imperative assignment",
    "Assigning to a property in JavaScript destroys its binding permanently. "
    "The property stops tracking whatever it was following, and nothing says "
    "so - it simply stops updating.",
    "A property that has a declarative binding ON THE SAME OBJECT and is also "
    "the target of a plain `=` assignment inside that object's own handlers "
    "or functions. Assignments of `Qt.binding(...)` are excluded: that is the "
    "documented way to put a binding BACK, so reporting it says the opposite "
    "of what the code does.",
    "Assignments guarded so they only run once, and properties whose binding "
    "is deliberately temporary. A property initialised to a literal constant "
    "is not treated as bound, because there is nothing for the assignment to "
    "destroy.")


def _tracks_something(doc, a: int, b: int) -> bool:
    """Does this binding FOLLOW anything, or is it just an initial value?

    `Qt.rect(0, 0, 0, 0)`, `Window.Hidden` and `false` are constants that
    happen to be written as expressions - assigning over them destroys
    nothing, because there was never anything to track. `app.exportDirectory`
    and `root.fmt(root.curLo)` do follow something.

    The test: a free identifier (not after a dot) that begins with a lowercase
    letter. Types and enums are capitalised by QML convention - `Qt`,
    `Window`, `Theme` - and object references are not.
    """
    for j in range(a, min(b, len(doc.toks))):
        t = doc.toks[j]
        if t.kind != "id" or t.text in LITERALS:
            continue
        if j > 0 and doc.toks[j - 1].text == ".":
            continue
        if t.text[:1].islower():
            return True
    return False


def qml_binding_overwritten(p, out: list) -> None:
    for doc in p.qml:
        for obj in doc.objects:
            # PER OBJECT. A flat table of property names across the whole file
            # matched `visible` on the root against `visible = false` inside a
            # child, and `text:` on a Label against `text =` in a TextField -
            # eight findings, none of them about the same property.
            bound: dict[str, int] = {}
            for bnd in obj.bindings:
                if bnd.is_block or "." in bnd.name:
                    continue
                # `property int ticks: 0` and `visible: false` are initial
                # VALUES, not bindings to anything - assigning over them
                # destroys nothing. The QML lexer hands back true/false/null
                # as identifiers, so testing for "contains an identifier" was
                # not enough and counted every boolean default as a binding.
                if not _tracks_something(doc, bnd.a, bnd.b):
                    continue
                bound.setdefault(bnd.name, bnd.line)
            if not bound:
                continue
            spans = [(f.a, f.b) for f in obj.functions] + \
                    [(h.a, h.b) for h in obj.handlers]
            for a, b in spans:
                for j in range(a, min(b, len(doc.toks) - 1)):
                    t = doc.toks[j]
                    if t.kind != "id" or t.text not in bound:
                        continue
                    nxt = doc.toks[j + 1]
                    if nxt.text != "=" or (j + 2 < len(doc.toks) and
                                           doc.toks[j + 2].text == "="):
                        continue
                    prev = doc.toks[j - 1].text if j else ""
                    if prev in ("var", "let", "const", "property"):
                        continue
                    # `slider.value = ...` from inside the slider is still this
                    # object; `other.value = ...` is not.
                    if prev == ".":
                        owner = doc.toks[j - 2].text if j >= 2 else ""
                        if owner not in ("", obj.id):
                            continue
                    if t.line == bound[t.text]:
                        continue
                    # `value = Qt.binding(function(){...})` REINSTATES the
                    # binding. It is the documented way to put one back after
                    # an imperative write, so reporting it says the opposite
                    # of what is happening.
                    k = j + 2
                    while k < b and not doc.toks[k].kind == "id":
                        k += 1
                    tail = " ".join(x.text for x in doc.toks[j + 2:min(j + 6, b)])
                    if "Qt . binding" in tail or "Qt.binding" in tail:
                        continue
                    out.append(Finding(
                        "qml_binding_overwritten", doc.rel, t.line,
                        f"{obj.type}#{obj.id}" if obj.id else obj.type,
                        f"`{t.text}` is assigned here, destroying the binding "
                        f"declared on line {bound[t.text]}",
                        why=("An imperative assignment in QML removes the "
                             "binding for good. From this point the property "
                             "no longer tracks what it was following."),
                        evidence=f"L{bound[t.text]}: {_line(doc, bound[t.text])}\n"
                                 f"L{t.line}: {_line(doc, t.line)}",
                        severity="medium", confidence="possible",
                        category="correctness",
                        suggestion="Make the binding cover both cases, or put it "
                                   "back with Qt.binding()."))
                    break


# --------------------------------------------------------------------------
describe(
    "qml_signal_never_handled",
    "A signal nothing connects to",
    "A signal declared and never handled is either dead, or the handler was "
    "renamed and the signal was not.",
    "A declared signal whose `onName` handler appears in no QML file and "
    "whose name appears in no C++ source.",
    "Signals handled by a name built at runtime, or connected from C++ with "
    "a string.")


def qml_signal_never_handled(p, out: list) -> None:
    for doc in p.qml:
        for obj in doc.objects:
            for sig in obj.signals:
                handler = "on" + sig.name[:1].upper() + sig.name[1:]
                if p.qml_tokens.get(handler, 0) or p.cpp_tokens.get(handler, 0):
                    continue
                if p.cpp_tokens.get(sig.name, 0):
                    continue
                if p.qml_tokens.get(sig.name, 0) > 1:
                    continue
                out.append(Finding(
                    "qml_signal_never_handled", doc.rel, sig.line,
                    f"{obj.type}#{obj.id}" if obj.id else obj.type,
                    f"signal `{sig.name}` has no handler anywhere",
                    why=f"Neither `{handler}` nor `{sig.name}` appears anywhere "
                        "else in QML or C++.",
                    evidence=f"L{sig.line}: {_line(doc, sig.line)}",
                    severity="low", confidence="possible", category="dead-code",
                    suggestion="Remove it, or connect it."))




# --------------------------------------------------------------------------
describe(
    "qml_type_error",
    "QML that names a property or method its type does not have",
    "A QML file that reads a member the type does not have is not a compile "
    "error and not a runtime error either - the read yields undefined, the "
    "binding quietly does nothing, and the button it was behind stops working "
    "with no message anywhere. Two of those shipped: a notebook delegate that "
    "re-declared a property its base type already had, so every figure in the "
    "notebook reported itself as figure zero and the close button removed the "
    "wrong one; and an `openExport` declared four levels deep instead of on the "
    "workspace root, which left File > Export doing nothing at all.",
    "Runs Qt's own qmllint against the application's QML, staged as a module "
    "through the SAME function the interface tests use - so the C++ types have "
    "their generated stand-ins and the linter can resolve them. Two message "
    "shapes are reported and the rest are dropped: a property that already "
    "exists in the base type, and a member not found on a NAMED type.",
    "A member read off a Loader's `item` is reported by qmllint against type "
    "QObject, because that is what `item` is to a static reader, and there are "
    "sixteen of those here that are all correct code. They are dropped by name: "
    "a finding against QObject is not reported. That exclusion is why the "
    "seventeenth - the real one, against VisualizeWorkspace - was worth "
    "reporting rather than lost in the noise.")


# `Property "x" already exists in base type "T", use a different name.`
_QML_OVERRIDE = re.compile(
    r'Property "(?P<name>[^"]+)" already exists in base type "(?P<type>[^"]+)"')
# `Member "x" not found on type "T"`
_QML_NO_MEMBER = re.compile(
    r'Member "(?P<name>[^"]+)" not found on type "(?P<type>[^"]+)"')


def _qmllint_binary(root):
    """Qt's qmllint, wherever this Qt put it."""
    import shutil
    for name in ("qmllint", "pyside6-qmllint"):
        found = shutil.which(name)
        if found:
            return found
    for base in (root / "build" / "vcpkg_installed", root / "build",
                 pathlib.Path("/usr/lib/qt6")):
        if not base.is_dir():
            continue
        for name in ("qmllint.exe", "qmllint"):
            for hit in base.rglob(name):
                return str(hit)
    return None


def qml_type_error(p, out: list) -> None:
    runner = _qmllint_binary(p.root)
    if runner is None:
        p.checks_not_run.append(
            ("qml_type_error",
             "qmllint was not found; it ships with Qt's qtdeclarative tools"))
        return
    # STAGED BY THE INTERFACE TESTS' OWN FUNCTION, not by a second copy of it.
    #
    # The linter can only report a member missing from a NAMED type if it can
    # resolve that type, and half of this application's QML contains a
    # PlotCanvas, which is C++. Linting the source tree directly leaves those
    # unresolved and the interesting findings never appear at all. run_ui_tests
    # already builds a module with generated stand-ins for exactly that reason,
    # so this asks it rather than growing a second arrangement free to drift.
    import contextlib
    import importlib.util
    import io
    import json
    import pathlib as _pathlib
    import tempfile
    spec = importlib.util.spec_from_file_location(
        "graphvis_ui_tests", p.root / "tools" / "run_ui_tests.py")
    if spec is None or spec.loader is None:
        p.checks_not_run.append(
            ("qml_type_error", "tools/run_ui_tests.py could not be loaded"))
        return
    mod = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(mod)
        with tempfile.TemporaryDirectory(prefix="graphvis-lint-") as tmp:
            staged = _pathlib.Path(tmp)
            # Its staging narrates what it did; the audit has its own report.
            with contextlib.redirect_stdout(io.StringIO()):
                mod.stage_module(staged)
            report = staged / "lint.json"
            files = [str(q) for q in (staged / "GraphVis").rglob("*.qml")]
            res = proc.run([runner, "-I", str(staged), "--json", str(report)]
                           + files, cwd=p.root, timeout=300)
            if res is None or not report.exists():
                p.checks_not_run.append(
                    ("qml_type_error", "qmllint did not produce a report"))
                return
            data = json.loads(report.read_text(encoding="utf-8"))
    except SystemExit as exc:
        p.checks_not_run.append(("qml_type_error", f"staging refused: {exc}"))
        return
    except Exception as exc:                              # pragma: no cover
        p.checks_not_run.append(
            ("qml_type_error", f"{type(exc).__name__}: {exc}"))
        return

    for entry in data.get("files", []):
        name = _pathlib.Path(entry.get("filename", "")).name
        rel = _qml_source_path(p, name)
        if rel is None:
            continue
        for w in entry.get("warnings") or []:
            message = w.get("message", "")
            line = int(w.get("line") or 1)
            hit = _QML_OVERRIDE.search(message)
            if hit:
                out.append(Finding(
                    "qml_type_error", rel, line, "",
                    f"`{hit.group('name')}` is declared again although "
                    f"{hit.group('type')} already has it",
                    why="A re-declared property SHADOWS the base type's copy. "
                        "The base's is then never initialised, and if it was "
                        "required Qt logs that once and carries on with the "
                        "default - which is how every cell in the notebook "
                        "came to report itself as figure zero.",
                    evidence=message[:300], severity="medium",
                    confidence="certain", category="correctness",
                    suggestion="Remove the re-declaration, or rename it."))
                continue
            hit = _QML_NO_MEMBER.search(message)
            # A member read off a Loader's `item` is reported against QObject
            # and is correct code. See the note in describe() above.
            if hit and hit.group("type") != "QObject":
                out.append(Finding(
                    "qml_type_error", rel, line, "",
                    f"`{hit.group('name')}` is not a member of "
                    f"{hit.group('type')}",
                    why="The read yields undefined rather than failing, so a "
                        "binding on it quietly does nothing and a call through "
                        "it raises only when something presses the control. "
                        "`openExport` was in this state and File > Export did "
                        "nothing at all.",
                    evidence=message[:300], severity="medium",
                    confidence="certain", category="correctness",
                    suggestion="Declare it on the type, or call it on the "
                               "object that has it."))


def _qml_source_path(p, basename: str):
    """The staged file's path back in the source tree, or None if it is ours.

    The staged module also holds the generated stand-ins, which have no source
    file and nothing to report against.
    """
    for doc in p.qml:
        if _pathlib_name(doc.rel) == basename:
            return doc.rel
    return None


def _pathlib_name(rel: str) -> str:
    return rel.replace("\\", "/").rsplit("/", 1)[-1]




# --------------------------------------------------------------------------
describe(
    "qml_cpp_member_missing",
    "QML reads a member off a C++ object that the C++ class does not have",
    "The controller, the canvas and the project workspace all arrive in QML as "
    "`property var`, so nothing static can see what they are: qmllint resolves "
    "them to nothing and checks nothing, and the application has 322 reads "
    "across that boundary. A rename on either side is silent - the read yields "
    "undefined, the binding quietly does nothing, and the control behind it "
    "stops working with no message. `openExport` shipped in that state and "
    "File > Export did nothing at all.",
    "Collects every `app.x`, `canvas.x` and `project.x` in the QML and compares "
    "them against the Q_PROPERTY, Q_INVOKABLE and signal names declared in that "
    "object's own header. QQuickItem's own members are excluded for the canvas, "
    "which is one.",
    "A local JavaScript variable named `app`, `canvas` or `project` would be "
    "read as the object. There are none. A member reached through a chain - "
    "`app.project.name` - is checked at each named prefix and not beyond.")


# The three objects QML receives as `var`, and the header that declares each.
# A table rather than a guess, and the check fails loudly if a header moves:
# quietly checking nothing is the state this exists to prevent.
_QML_CPP_BOUNDARY = {
    "app": ("app", "src", "AppController.h"),
    "canvas": ("native", "plot2d", "include", "PlotCanvas.h"),
    "project": ("app", "src", "ProjectWorkspace.h"),
}

# QQuickItem's own surface, which a PlotCanvas has by inheritance and no header
# of ours declares.
_QQUICK_ITEM = frozenset({
    "width", "height", "x", "y", "z", "visible", "enabled", "opacity", "parent",
    "children", "anchors", "rotation", "scale", "clip", "focus", "state",
    "states", "transform", "implicitWidth", "implicitHeight", "childrenRect",
    "data", "activeFocus", "antialiasing", "smooth", "baselineOffset",
    "objectName", "destroy", "forceActiveFocus", "mapToItem", "mapFromItem",
    "grabToImage", "update", "left", "right", "top", "bottom",
    "horizontalCenter", "verticalCenter",
})


def _cpp_members(header_text: str) -> set:
    text = re.sub(r"//[^\n]*", "", header_text)
    names = set(re.findall(
        r"Q_PROPERTY\(\s*[A-Za-z_][\w:<>*&\s]*?\s+([A-Za-z_]\w*)\s+READ", text))
    names |= set(re.findall(
        r"Q_INVOKABLE\s+(?:static\s+)?[A-Za-z_][\w:<>*&\s]*?\s+([A-Za-z_]\w*)\s*\(",
        text))
    block = re.search(
        r"\bsignals:(.*?)(?:\n\s*(?:public|private|protected)\b|\n\};)",
        text, re.S)
    if block:
        names |= set(re.findall(r"\bvoid\s+([A-Za-z_]\w*)\s*\(", block.group(1)))
    return names


def qml_cpp_member_missing(p, out: list) -> None:
    offered: dict = {}
    for prefix, parts in _QML_CPP_BOUNDARY.items():
        header = p.root.joinpath(*parts)
        if not header.exists():
            p.checks_not_run.append(
                ("qml_cpp_member_missing",
                 f"{'/'.join(parts)} is not where this expects it, so the "
                 f"`{prefix}.` boundary was not checked"))
            continue
        names = _cpp_members(header.read_text(encoding="utf-8", errors="ignore"))
        if not names:
            p.checks_not_run.append(
                ("qml_cpp_member_missing",
                 f"no Q_PROPERTY found in {parts[-1]}; checking against an "
                 "empty set would pass everything"))
            continue
        offered[prefix] = names | (_QQUICK_ITEM if prefix == "canvas" else set())
    if not offered:
        return

    for doc in p.qml:
        # The file's own source. Comments stripped first: a member named in a
        # comment is not a read, and this check's whole value is that its
        # findings are certain.
        text = re.sub(r"//[^\n]*", "", doc.src)
        if not text:
            continue
        for prefix, names in offered.items():
            for m in re.finditer(rf"\b{prefix}\.([A-Za-z_]\w*)", text):
                member = m.group(1)
                if member in names:
                    continue
                line = text.count("\n", 0, m.start()) + 1
                out.append(Finding(
                    "qml_cpp_member_missing", doc.rel, line, "",
                    f"`{prefix}.{member}` is not declared on "
                    f"{_QML_CPP_BOUNDARY[prefix][-1][:-2]}",
                    why="The object arrives in QML as `var`, so this read is "
                        "invisible to qmllint and yields undefined at runtime "
                        "rather than failing. The binding does nothing and the "
                        "control behind it stops working, silently.",
                    evidence=f"L{line}: {_line(doc, line)}",
                    severity="high", confidence="certain", category="correctness",
                    suggestion=f"Declare it on {_QML_CPP_BOUNDARY[prefix][-1]}, "
                               "or read it off the object that has it."))


ALL = [
    qml_cpp_member_missing,
    qml_type_error,
    qml_bool_from_and,
    qml_anchors_conflict,
    qml_completed_in_delegate,
    qml_unused_property,
    qml_unresolved_id,
    qml_binding_overwritten,
    qml_signal_never_handled,
]
