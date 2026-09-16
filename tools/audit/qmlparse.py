"""A QML object-tree parser.

Why the object tree matters
---------------------------
Two of the checks this audit used to have were deleted because a regular
expression could not answer them: "does this id resolve?" (1,033 false
positives) and "does this delegate use a role its model does not have?" (72).
Both are answerable, but only against a tree: an id is visible inside the
component that declares it, a delegate's scope includes the model's roles, and
neither fact is visible in a line of text.

The one real QML bug this project shipped - `property bool x: obj && obj.y`
assigning undefined, twenty-four warnings a run - needs the same thing. The
text `a && b` is fine in a binding for `var` and broken in a binding for
`bool`, and only the declaration says which.

Termination of a binding expression is the one genuinely awkward part of QML:
`width: parent.width` ends at the newline, `onClicked: { ... }` ends at the
brace, `states: [ ... ]` ends at the bracket, and `x: a +\n b` continues. The
rule used here is the one the language itself uses - an expression ends at a
newline unless it is obviously incomplete.
"""
from __future__ import annotations

from pathlib import Path

from .cpplex import Tok, lex, matching, text_of

BUILTIN_TYPES = {
    "int", "real", "double", "bool", "string", "url", "color", "date", "var",
    "variant", "point", "rect", "size", "font", "vector2d", "vector3d",
    "vector4d", "quaternion", "matrix4x4", "alias", "list", "enumeration",
}

# Operators that cannot end an expression, so a newline after one continues it.
CONTINUES = {
    "+", "-", "*", "/", "%", "&&", "||", "?", ":", ",", "=", "==", "!=",
    "===", "!==", "<", ">", "<=", ">=", ".", "(", "[", "{", "!", "~", "&",
    "|", "^", "<<", ">>", "+=", "-=", "*=", "/=", "=>", "new", "return",
    "typeof", "in", "of", "instanceof",
}


# Tokens that cannot begin an expression, so a line starting with one is a
# continuation of the line above.
STARTS_CONTINUATION = {
    "?", ":", ".", "?.", ",", ")", "]", "}", "+", "-", "*", "/", "%",
    "&&", "||", "==", "!=", "===", "!==", "<", ">", "<=", ">=", "=",
    "+=", "-=", "*=", "/=", "=>", "&", "|", "^", "<<", ">>",
}


class QmlBinding:
    __slots__ = ("name", "a", "b", "line", "end_line", "is_block", "obj", "target")

    def __init__(self, name: str, a: int, b: int, line: int, end_line: int):
        self.name = name
        self.a, self.b = a, b
        self.line, self.end_line = line, end_line
        self.is_block = False
        self.obj: "QmlObject | None" = None
        self.target = ""              # for `Behavior on x`

    def __repr__(self) -> str:  # pragma: no cover
        return f"Binding({self.name} L{self.line})"


class QmlProp:
    __slots__ = ("name", "type", "line", "readonly", "default", "required",
                 "binding", "obj", "is_alias")

    def __init__(self, name: str, type_: str, line: int):
        self.name = name
        self.type = type_
        self.line = line
        self.readonly = False
        self.default = False
        self.required = False
        self.binding: QmlBinding | None = None
        self.obj: "QmlObject | None" = None
        self.is_alias = type_ == "alias"


class QmlFunc:
    __slots__ = ("name", "params", "line", "end_line", "a", "b", "obj")

    def __init__(self, name: str, params: list, line: int, end_line: int, a: int, b: int):
        self.name = name
        self.params = params
        self.line, self.end_line = line, end_line
        self.a, self.b = a, b
        self.obj: "QmlObject | None" = None


class QmlSignal:
    __slots__ = ("name", "params", "line", "obj")

    def __init__(self, name: str, params: list, line: int):
        self.name = name
        self.params = params
        self.line = line
        self.obj: "QmlObject | None" = None


class QmlObject:
    __slots__ = ("type", "id", "line", "end_line", "a", "b", "props",
                 "bindings", "handlers", "functions", "signals", "children",
                 "parent", "doc", "on_target", "is_component")

    def __init__(self, type_: str, line: int, end_line: int, a: int, b: int):
        self.type = type_
        self.id = ""
        self.line, self.end_line = line, end_line
        self.a, self.b = a, b
        self.props: list[QmlProp] = []
        self.bindings: list[QmlBinding] = []
        self.handlers: list[QmlBinding] = []
        self.functions: list[QmlFunc] = []
        self.signals: list[QmlSignal] = []
        self.children: list[QmlObject] = []
        self.parent: "QmlObject | None" = None
        self.doc: "QmlDoc | None" = None
        self.on_target = ""
        self.is_component = False

    def walk(self):
        yield self
        for c in self.children:
            yield from c.walk()

    def ancestors(self):
        p = self.parent
        while p is not None:
            yield p
            p = p.parent

    def __repr__(self) -> str:  # pragma: no cover
        return f"QmlObject({self.type}{'#' + self.id if self.id else ''} L{self.line})"


class QmlDoc:
    __slots__ = ("path", "rel", "src", "toks", "imports", "root", "lines",
                 "objects", "ids", "name", "pragmas")

    def __init__(self, path: Path, rel: str, src: str, toks: list[Tok]):
        self.path = path
        self.rel = rel
        self.src = src
        self.toks = toks
        self.imports: list[tuple[str, int]] = []
        self.pragmas: list[str] = []
        self.root: QmlObject | None = None
        self.lines = src.count("\n") + 1
        self.objects: list[QmlObject] = []
        self.ids: dict[str, QmlObject] = {}
        self.name = path.stem

    def line_text(self, line: int) -> str:
        try:
            return self.src.split("\n")[line - 1].strip()
        except IndexError:
            return ""


def _code(toks: list[Tok], j: int, b: int) -> int:
    while j < b and toks[j].kind == "comment":
        j += 1
    return j


def _expr_end(toks: list[Tok], j: int, b: int) -> int:
    """Where a binding expression starting at j ends.

    QML terminates an expression at a newline unless the line is obviously
    unfinished. Getting this wrong in either direction is costly: too eager
    and a multi-line expression is truncated, too lax and the next property in
    the object is swallowed into this one's expression.
    """
    j = _code(toks, j, b)
    if j >= b:
        return j
    if toks[j].text == "{":
        return matching(toks, j) + 1
    if toks[j].text == "[":
        return matching(toks, j) + 1

    depth = 0
    last = j
    k = j
    while k < b:
        t = toks[k]
        if t.kind == "comment":
            k += 1
            continue
        if t.kind == "punct":
            if t.text in "([{":
                depth += 1
            elif t.text in ")]}":
                if depth == 0:
                    return last + 1
                depth -= 1
            elif t.text == ";" and depth == 0:
                return k + 1
        if depth == 0 and k > j and t.line > toks[last].line:
            prev = toks[last]
            # An expression continues if the previous line could not end one,
            # OR if this line cannot START one. The second half matters: a
            # ternary written as
            #     property bool x: a && b
            #         ? c : d
            # is complete at the end of the first line and still continues,
            # because `?` cannot begin an expression. Without this the binding
            # was truncated at `b`, and every check reading the binding saw
            # half of it.
            if prev.text not in CONTINUES and prev.kind not in ("comment",) \
                    and t.text not in STARTS_CONTINUATION:
                return last + 1
        last = k
        k += 1
    return min(last + 1, b)


class _QmlParser:
    def __init__(self, doc: QmlDoc):
        self.doc = doc
        self.t = doc.toks

    def parse(self) -> QmlDoc:
        t = self.t
        n = len(t)
        j = 0
        # imports and pragmas
        while j < n:
            j = _code(t, j, n)
            if j >= n:
                break
            if t[j].kind == "id" and t[j].text == "import":
                line = t[j].line
                k = j + 1
                parts = []
                while k < n and t[k].line == line:
                    parts.append(t[k].text)
                    k += 1
                self.doc.imports.append((" ".join(parts), line))
                j = k
                continue
            if t[j].kind == "id" and t[j].text == "pragma":
                line = t[j].line
                k = j + 1
                while k < n and t[k].line == line:
                    k += 1
                self.doc.pragmas.append(text_of(t, j, k))
                j = k
                continue
            break
        # root object
        j = _code(t, j, n)
        if j < n:
            obj, _ = self.object_at(j, n, None)
            self.doc.root = obj
        return self.doc

    def _type_name(self, j: int, b: int) -> tuple[str, int]:
        """A possibly qualified type name starting at j."""
        parts = [self.t[j].text]
        k = j + 1
        while k + 1 < b and self.t[k].text == "." and self.t[k + 1].kind == "id":
            parts.append(self.t[k + 1].text)
            k += 2
        return ".".join(parts), k

    def object_at(self, j: int, b: int, parent: QmlObject | None):
        t = self.t
        name, k = self._type_name(j, b)
        k = _code(t, k, b)
        on_target = ""
        if k < b and t[k].kind == "id" and t[k].text == "on":
            k += 1
            k = _code(t, k, b)
            if k < b:
                on_target, k = self._type_name(k, b)
                k = _code(t, k, b)
        if k >= b or t[k].text != "{":
            return None, j + 1
        close = matching(t, k)
        obj = QmlObject(name, t[j].line, t[close].line, j, close + 1)
        obj.on_target = on_target
        obj.parent = parent
        obj.doc = self.doc
        self.doc.objects.append(obj)
        if parent is not None:
            parent.children.append(obj)
        self.members(k + 1, close, obj)
        return obj, close + 1

    def property_at(self, j: int, b: int, obj: QmlObject, mods: set) -> int:
        """`property <type> <name> [: expr]` starting at the `property` token.

        Returns the index just past the declaration, or j if this was not one.
        The declared TYPE is the whole point: it is what makes `bool x: a && b`
        a bug and `var x: a && b` ordinary code.
        """
        t = self.t
        if j >= b or t[j].text != "property":
            return j
        k = _code(t, j + 1, b)
        if k >= b:
            return j
        type_name, k = self._type_name(k, b)
        # `property list<Item> things`
        k = _code(t, k, b)
        if k < b and t[k].text == "<":
            depth = 0
            m = k
            while m < b:
                if t[m].text == "<":
                    depth += 1
                elif t[m].text == ">":
                    depth -= 1
                    if depth == 0:
                        m += 1
                        break
                m += 1
            type_name = type_name + text_of(t, k, m)
            k = _code(t, m, b)
        if k >= b or t[k].kind != "id":
            return j
        name = t[k].text
        prop = QmlProp(name, type_name, t[j].line)
        prop.readonly = "readonly" in mods
        prop.default = "default" in mods
        prop.required = "required" in mods
        prop.obj = obj
        obj.props.append(prop)
        k = _code(t, k + 1, b)
        if k < b and t[k].text == ":":
            ve = _expr_end(t, k + 1, b)
            bind = QmlBinding(name, k + 1, ve, t[j].line,
                              t[max(k, min(ve, b) - 1)].line)
            bind.obj = obj
            vs = _code(t, k + 1, b)
            bind.is_block = vs < b and t[vs].text == "{"
            prop.binding = bind
            obj.bindings.append(bind)
            # `property Component c: Item { ... }`
            if vs < b and t[vs].kind == "id" and t[vs].text[:1].isupper():
                _nm, k2 = self._type_name(vs, b)
                k3 = _code(t, k2, b)
                if k3 < b and t[k3].text == "{":
                    self.object_at(vs, b, obj)
            return max(ve, k + 1)
        return k

    def members(self, a: int, b: int, obj: QmlObject) -> None:
        t = self.t
        j = a
        while j < b:
            j = _code(t, j, b)
            if j >= b:
                break
            tok = t[j]

            if tok.text == ";":
                j += 1
                continue

            if tok.kind in ("id", "kw"):
                w = tok.text

                # ---- modifiers before `property` -------------------------
                if w in ("readonly", "default", "required") and \
                        j + 1 < b and t[j + 1].text in ("property", "readonly",
                                                        "default", "required"):
                    k = j
                    mods = set()
                    while k < b and t[k].text in ("readonly", "default", "required"):
                        mods.add(t[k].text)
                        k += 1
                    j2 = self.property_at(k, b, obj, mods)
                    if j2 > j:
                        j = j2
                        continue

                if w == "property":
                    j2 = self.property_at(j, b, obj, set())
                    if j2 > j:
                        j = j2
                        continue

                if w == "signal":
                    k = j + 1
                    k = _code(t, k, b)
                    if k < b and t[k].kind == "id":
                        nm = t[k].text
                        params = []
                        k2 = _code(t, k + 1, b)
                        if k2 < b and t[k2].text == "(":
                            e = matching(t, k2)
                            params = [x.text for x in t[k2 + 1:e] if x.kind == "id"]
                            k2 = e + 1
                        s = QmlSignal(nm, params, tok.line)
                        s.obj = obj
                        obj.signals.append(s)
                        j = k2
                        continue

                if w in ("function", "component"):
                    if w == "component":
                        k = _code(t, j + 1, b)
                        if k < b and t[k].kind == "id" and \
                                _code(t, k + 1, b) < b and t[_code(t, k + 1, b)].text == ":":
                            k2 = _code(t, _code(t, k + 1, b) + 1, b)
                            child, nxt = self.object_at(k2, b, obj)
                            if child is not None:
                                child.is_component = True
                                j = nxt
                                continue
                    k = _code(t, j + 1, b)
                    if k < b and t[k].kind == "id":
                        nm = t[k].text
                        k2 = _code(t, k + 1, b)
                        params = []
                        if k2 < b and t[k2].text == "(":
                            e = matching(t, k2)
                            params = [x.text for x in t[k2 + 1:e] if x.kind == "id"]
                            k2 = _code(t, e + 1, b)
                        if k2 < b and t[k2].text == ":":       # typed return
                            k2 = _code(t, k2 + 1, b)
                            while k2 < b and t[k2].text != "{":
                                k2 += 1
                        if k2 < b and t[k2].text == "{":
                            e = matching(t, k2)
                            f = QmlFunc(nm, params, tok.line, t[e].line, j, e + 1)
                            f.obj = obj
                            obj.functions.append(f)
                            j = e + 1
                            continue

                # ---- `name: value` or `a.b.c: value` ----------------------
                qname, k = self._type_name(j, b)
                k = _code(t, k, b)
                if k < b and t[k].text == ":":
                    ve = _expr_end(t, k + 1, b)
                    bind = QmlBinding(qname, k + 1, ve, tok.line,
                                      t[max(k, min(ve, b) - 1)].line)
                    bind.obj = obj
                    bind.is_block = _code(t, k + 1, b) < b and t[_code(t, k + 1, b)].text == "{"
                    if qname == "id":
                        idtok = _code(t, k + 1, b)
                        if idtok < b and t[idtok].kind == "id":
                            obj.id = t[idtok].text
                            self.doc.ids.setdefault(obj.id, obj)
                    elif qname.startswith("on") and len(qname) > 2 and qname[2].isupper():
                        obj.handlers.append(bind)
                    elif "." in qname and qname.split(".")[-1].startswith("on") and \
                            len(qname.split(".")[-1]) > 2 and qname.split(".")[-1][2].isupper():
                        obj.handlers.append(bind)
                    else:
                        obj.bindings.append(bind)
                    # a binding whose value is an object: `delegate: Item {}`
                    vs = _code(t, k + 1, b)
                    if vs < b and t[vs].kind == "id" and t[vs].text[:1].isupper():
                        after, _k2 = self._type_name(vs, b)
                        k3 = _code(t, _k2, b)
                        if k3 < b and t[k3].text == "{":
                            child, _ = self.object_at(vs, b, obj)
                    elif vs < b and t[vs].text == "[":
                        e = matching(t, vs)
                        m = vs + 1
                        while m < e:
                            m = _code(t, m, e)
                            if m < e and t[m].kind == "id" and t[m].text[:1].isupper():
                                child, nxt = self.object_at(m, e, obj)
                                if child is not None:
                                    m = nxt
                                    continue
                            m += 1
                    j = max(ve, k + 1)
                    continue

                # ---- a child object -------------------------------------
                if k < b and (t[k].text == "{" or
                              (t[k].kind == "id" and t[k].text == "on")):
                    child, nxt = self.object_at(j, b, obj)
                    if child is not None:
                        j = nxt
                        continue
            j += 1


def parse_qml(path: Path, rel: str) -> QmlDoc:
    src = path.read_text(encoding="utf-8", errors="replace")
    toks = [t for t in lex(src)]
    for t in toks:
        if t.kind == "chr":
            t.kind = "str"
        if t.kind == "kw":
            t.kind = "id"
    doc = QmlDoc(path, rel, src, toks)
    return _QmlParser(doc).parse()
