"""A structural C++ parser: token stream in, a tree of scopes and statements out.

What this is and is not
-----------------------
It is not a compiler front end. It does not resolve overloads, instantiate
templates, or know what a type means. It DOES know, exactly:

  * where every function begins and ends, and which class it belongs to;
  * where every block, loop, branch, switch case and catch clause is;
  * which names are declared in which scope, and where each is read or written;
  * which calls a function makes, and which of its own statements are
    unreachable.

That is the difference between a regular expression and a reader. A regex can
see the text "return" near the text "if". This can see that the guard runs
after the work it was meant to guard - which is the single bug class this
project has hit most often, and which no pattern match can find, because the
pattern is identical whether the guard is in the right place or the wrong one.

Everything here is deliberately total: unknown constructs degrade to an opaque
statement rather than raising. A parser that throws on one odd line stops
auditing the other forty thousand.
"""
from __future__ import annotations

from .cpplex import CONTROL_KEYWORDS, DECL_KEYWORDS, Tok, lex, matching, split_top_level, text_of

ACCESS = {"public", "private", "protected"}
# Words that appear in a declaration but are never the declared name itself.
NOISE = DECL_KEYWORDS | {"template", "operator", "using", "namespace",
                         "export", "requires", "noexcept", "override",
                         "final", "return", "new", "delete", "sizeof"}


class Decl:
    """A declared name, with where it was declared and how it is used."""

    __slots__ = ("name", "kind", "line", "tok", "type_a", "type_b",
                 "reads", "writes", "escapes", "scope_end", "init_a", "init_b")

    def __init__(self, name: str, kind: str, line: int, tok: int,
                 type_a: int = -1, type_b: int = -1):
        self.name = name
        self.kind = kind             # var | param | field | func | type | enumerator
        self.line = line
        self.tok = tok
        self.type_a = type_a
        self.type_b = type_b
        self.reads: list[int] = []   # token indices where the name is read
        self.writes: list[int] = []  # token indices where it is assigned
        self.escapes = False         # passed somewhere we cannot see through
        self.scope_end = -1
        self.init_a = -1
        self.init_b = -1

    def __repr__(self) -> str:  # pragma: no cover
        return f"Decl({self.kind} {self.name} L{self.line})"


def _always_true(t, a: int, b: int) -> bool:
    """`while (true)`, `while (1)`, `for (;;)` - a condition that never ends.

    Only the literal forms. A condition that is always true for a reason a
    reader has to work out is not one this should claim to know.
    """
    code = [x.text for x in t[a:b] if x.is_code]
    return code in ([], ["true"], ["1"])


class Stmt:
    """One statement. `a`..`b` is its full token span."""

    __slots__ = ("kind", "a", "b", "line", "end_line", "cond", "init", "incr",
                 "body", "alt", "parts", "decls", "parent", "cond_always_true")

    def __init__(self, kind: str, a: int, b: int, line: int, end_line: int):
        self.kind = kind
        self.a, self.b = a, b
        self.line, self.end_line = line, end_line
        self.cond: tuple[int, int] | None = None
        self.init: tuple[int, int] | None = None
        self.incr: tuple[int, int] | None = None
        self.body: "Block | Stmt | None" = None
        self.alt: "Block | Stmt | None" = None      # else branch / catch list
        self.parts: list = []                        # catch clauses, cases
        self.decls: list[Decl] = []
        self.parent: "Block | None" = None
        # WHETHER THIS LOOP'S CONDITION IS A LITERAL TRUTH, recorded here
        # because this is the only place the tokens are in hand.
        #
        # `terminates()` in cppflow wanted to answer "while(true) with no
        # break" and could not: it receives a Stmt and no token array, so it
        # built a string from a `_range()` helper that returns an empty list,
        # discarded it, and returned False unconditionally. The intent was
        # written down and never implemented, and nothing noticed, because an
        # under-reporting `terminates` shows up as findings that are absent.
        self.cond_always_true: bool = False

    def __repr__(self) -> str:  # pragma: no cover
        return f"Stmt({self.kind} L{self.line})"


class Block:
    __slots__ = ("a", "b", "line", "end_line", "stmts", "decls", "parent")

    def __init__(self, a: int, b: int, line: int, end_line: int):
        self.a, self.b = a, b
        self.line, self.end_line = line, end_line
        self.stmts: list[Stmt] = []
        self.decls: list[Decl] = []
        self.parent: "Block | None" = None

    def walk(self):
        """Every statement below here, depth first."""
        for s in self.stmts:
            yield s
            for child in (s.body, s.alt):
                if isinstance(child, Block):
                    yield from child.walk()
                elif isinstance(child, Stmt):
                    yield child
                    if isinstance(child.body, Block):
                        yield from child.body.walk()
            for p in s.parts:
                if isinstance(p, Block):
                    yield from p.walk()
                elif isinstance(p, Stmt):
                    yield p
                    if isinstance(p.body, Block):
                        yield from p.body.walk()

    def blocks(self):
        yield self
        for s in self.stmts:
            for child in list(s.parts) + [s.body, s.alt]:
                if isinstance(child, Block):
                    yield from child.blocks()
                elif isinstance(child, Stmt) and isinstance(child.body, Block):
                    yield from child.body.blocks()


class Param:
    __slots__ = ("name", "type", "a", "b", "is_ref", "is_const", "is_ptr", "has_default")

    def __init__(self, name: str, type_: str, a: int, b: int):
        self.name = name
        self.type = type_
        self.a, self.b = a, b
        self.is_ref = "&" in type_
        self.is_const = type_.startswith("const") or " const" in type_
        self.is_ptr = "*" in type_
        self.has_default = False


class Func:
    __slots__ = ("name", "cls", "line", "end_line", "a", "b", "body", "params",
                 "ret", "quals", "file", "unit", "is_ctor", "is_dtor",
                 "is_template", "access", "lambdas", "decl_only", "sig_a", "sig_b")

    def __init__(self, name: str, cls: str, line: int, end_line: int, a: int, b: int):
        self.name = name
        self.cls = cls
        self.line, self.end_line = line, end_line
        self.a, self.b = a, b
        self.body: Block | None = None
        self.params: list[Param] = []
        self.ret = ""
        self.quals: set[str] = set()
        self.file = ""
        self.unit: "Unit | None" = None
        self.is_ctor = False
        self.is_dtor = False
        self.is_template = False
        self.access = "public"
        self.lambdas: list[Block] = []
        self.decl_only = False
        self.sig_a = a
        self.sig_b = a

    @property
    def qual(self) -> str:
        return f"{self.cls}::{self.name}" if self.cls else self.name

    @property
    def lines(self) -> int:
        return self.end_line - self.line + 1

    def __repr__(self) -> str:  # pragma: no cover
        return f"Func({self.qual} L{self.line}-{self.end_line})"


class TypeDef:
    __slots__ = ("kind", "name", "line", "end_line", "a", "b", "bases",
                 "methods", "fields", "file", "is_qobject", "signals",
                 "properties", "invokables", "slots_")

    def __init__(self, kind: str, name: str, line: int, end_line: int, a: int, b: int):
        self.kind = kind
        self.name = name
        self.line, self.end_line = line, end_line
        self.a, self.b = a, b
        self.bases: list[str] = []
        self.methods: list[Func] = []
        self.fields: list[Decl] = []
        self.file = ""
        self.is_qobject = False
        self.signals: list[Func] = []
        self.properties: list[dict] = []
        self.invokables: list[Func] = []
        self.slots_: list[Func] = []


class Unit:
    """One parsed translation unit."""

    __slots__ = ("path", "rel", "toks", "src", "funcs", "types", "globals",
                 "includes", "macros", "pp_if_zero", "lines", "namespaces",
                 "pp_cond_lines")

    def __init__(self, path, rel_: str, src: str, toks: list[Tok]):
        self.path = path
        self.rel = rel_
        self.src = src
        self.toks = toks
        self.funcs: list[Func] = []
        self.types: list[TypeDef] = []
        self.globals: list[Decl] = []
        self.includes: list[tuple[str, int]] = []
        self.macros: dict[str, int] = {}
        self.pp_if_zero: list[tuple[int, int]] = []
        self.pp_cond_lines: set[int] = set()
        self.lines = src.count("\n") + 1
        self.namespaces: list[str] = []

    def line_text(self, line: int) -> str:
        try:
            return self.src.split("\n")[line - 1].strip()
        except IndexError:
            return ""


# --------------------------------------------------------------------------
# helpers over the token stream
# --------------------------------------------------------------------------

def _skip_noise(toks: list[Tok], j: int, b: int) -> int:
    while j < b and (toks[j].kind == "comment" or toks[j].pp):
        j += 1
    return j


def _next_code(toks: list[Tok], j: int, b: int) -> int:
    return _skip_noise(toks, j, b)


def _prev_code(toks: list[Tok], j: int, a: int) -> int:
    while j >= a and (toks[j].kind == "comment" or toks[j].pp):
        j -= 1
    return j


def _skip_template_args(toks: list[Tok], j: int, b: int) -> int:
    """Skip a balanced <...> starting at j (which must be '<')."""
    depth = 0
    while j < b:
        t = toks[j]
        if t.kind == "punct":
            if t.text == "<":
                depth += 1
            elif t.text == ">":
                depth -= 1
                if depth == 0:
                    return j + 1
            elif t.text == ">>":
                depth -= 2
                if depth <= 0:
                    return j + 1
            elif t.text in "([{":
                j = matching(toks, j)
            elif t.text == ";":
                return j            # not a template after all
        j += 1
    return j


def top_level_groups(toks: list[Tok], a: int, b: int, open_ch: str = "(") -> list[tuple[int, int]]:
    """Spans of balanced groups at depth zero within a..b."""
    out = []
    depth = 0
    j = a
    while j < b:
        t = toks[j]
        if t.kind == "punct":
            if t.text in "([{":
                if depth == 0 and t.text == open_ch:
                    e = matching(toks, j)
                    out.append((j, e))
                    j = e
                    depth = 0
                    j += 1
                    continue
                depth += 1
            elif t.text in ")]}":
                depth -= 1
        j += 1
    return out


def find_top_level(toks: list[Tok], a: int, b: int, what: str,
                   angles: bool = False) -> int:
    """First `what` at bracket depth zero, or -1.

    The opening-bracket test comes BEFORE the depth increment, so that this
    can find `(`, `[` and `{` as well as `;` and `,`. An earlier version put
    the increment first, which meant it could never find an opening bracket
    at all - and every caller that asked it to silently got -1 and carried on
    as though the construct were absent. A guard that always answers "no"
    is not a guard; it is the same mistake this audit exists to find.
    """
    depth = 0
    j = a
    while j < b:
        t = toks[j]
        if t.kind == "punct":
            if depth == 0 and t.text == what:
                return j
            if angles and t.text == "<" and depth == 0 and j > a and \
                    toks[j - 1].kind in ("id", "kw"):
                nk = _skip_template_args(toks, j, b)
                if nk > j:
                    j = nk
                    continue
            if t.text in "([{":
                depth += 1
            elif t.text in ")]}":
                depth -= 1
                if depth < 0:
                    return -1
        j += 1
    return -1


# --------------------------------------------------------------------------
# statement-level parsing
# --------------------------------------------------------------------------

DECL_TAIL = {"=", ";", ",", "(", "{", "[", ")"}


def _has_top_level_logic(toks: list[Tok], a: int, b: int) -> bool:
    """A top-level && or || means this is an expression, whatever it looks like.

    `window<3||window>n` reads as a declaration to any parser that treats
    `<`..`>` as template brackets: type `window<3||window>`, name `n`. C++
    has the same ambiguity and resolves it with a symbol table this audit
    does not have. The `||` is the giveaway and costs one scan to find.
    """
    depth = 0
    for j in range(a, b):
        t = toks[j]
        if t.kind != "punct":
            continue
        if t.text in "([{":
            depth += 1
        elif t.text in ")]}":
            depth -= 1
        elif depth == 0 and t.text in ("||", "&&", "==", "!=", "?"):
            return True
    return False


def declared_names(toks: list[Tok], a: int, b: int,
                   require_init: bool = False) -> list[Decl]:
    """Names declared by the declaration statement spanning a..b (no ';').

    Returns [] when the statement is an expression rather than a declaration.
    The test is structural, not a word list: a declaration is a type-ish run
    followed by a declarator, and an expression is not.

    `require_init` is for the condition of an if/while/switch, where C++ only
    permits a declaration WITH an initialiser - `if (int n = f())`. Demanding
    the `=` there is exact, and removes the entire class of conditions that
    merely resemble declarations.
    """
    j = _skip_noise(toks, a, b)
    if j >= b:
        return []
    if _has_top_level_logic(toks, j, b):
        return []
    if require_init and find_top_level(toks, j, b, "=", angles=True) == -1:
        return []
    first = toks[j]
    if first.kind == "kw" and first.text in CONTROL_KEYWORDS:
        return []
    if first.kind == "kw" and first.text in ("using", "typedef", "friend",
                                             "static_assert", "namespace"):
        return []
    if first.text in ("delete", "new", "throw"):
        return []

    # A local type definition declares fields, not locals. `struct Edge {
    # double from[3]; int varies; };` must not contribute a local called
    # `varies` - it contributed two, before this test existed.
    if first.kind == "kw" and first.text in ("struct", "class", "union", "enum"):
        brace = find_top_level(toks, a, b, "{", angles=True)
        if brace != -1:
            close = matching(toks, brace)
            tail = [x for x in toks[close + 1:b] if x.is_code]
            if not tail:
                return []
            # `struct P { ... } pace{a,b,c};` declares `pace`, not a,b,c.
            out2: list[Decl] = []
            for s2, e2 in split_top_level(toks, close + 1, b, ","):
                k2 = _skip_noise(toks, s2, e2)
                while k2 < e2 and toks[k2].text in ("*", "&", "&&"):
                    k2 += 1
                if k2 < e2 and toks[k2].kind == "id":
                    out2.append(Decl(toks[k2].text, "var", toks[k2].line, k2))
            return out2

    typeish = first.kind == "kw" and first.text in DECL_KEYWORDS
    if not typeish:
        # id (:: id)* (<...>)? followed by * & or an identifier
        if first.kind != "id":
            return []
        k = j + 1
        saw_qual = False
        while k < b:
            t = toks[k]
            if t.text == "::":
                saw_qual = True
                k += 1
                if k < b and toks[k].kind in ("id", "kw"):
                    k += 1
                continue
            if t.text == "<":
                nk = _skip_template_args(toks, k, b)
                if nk == k:
                    break
                k = nk
                continue
            break
        # after the type-ish run we need * & or a plain identifier.
        # `&&` is deliberately NOT a declarator token here: an rvalue-reference
        # local is rare, `a && b(c)` is not, and treating the logical operator
        # as a declarator invented a variable in every such condition.
        type_tokens = k - j
        stars = 0
        while k < b and toks[k].text in ("*", "&", "const", "volatile"):
            if toks[k].text in ("*", "&"):
                stars += 1
            k += 1
        if k < b and toks[k].kind == "id":
            nxt = toks[k + 1].text if k + 1 < b else ";"
            if nxt == "(":
                # `Type name(args)` is a declaration; `obj.f(x)` and `a && f(x)`
                # are not. Require a real multi-token type run and no pointer
                # punctuation, which is what distinguishes them.
                typeish = stars == 0 and (type_tokens > 1 or saw_qual)
            elif nxt in DECL_TAIL:
                typeish = True
        if not typeish:
            return []

    # Walk the declarator list.
    out: list[Decl] = []
    k = j
    # skip leading specifiers
    while k < b and toks[k].kind == "kw" and toks[k].text in (
            "static", "const", "constexpr", "consteval", "constinit", "inline",
            "extern", "mutable", "thread_local", "volatile", "register",
            "typename", "struct", "class", "enum", "union", "virtual",
            "explicit", "friend"):
        k += 1
    type_a = k
    for s, e in split_top_level(toks, j, b, ","):
        # the declared name is the last identifier before = ( { [ or the end
        stop = e
        for stopper in ("=", "{", "[", "("):
            p = find_top_level(toks, s, e, stopper, angles=True)
            if p != -1:
                stop = min(stop, p)
        name_tok = -1
        m = stop - 1
        while m >= s:
            t = toks[m]
            if t.kind == "id":
                name_tok = m
                break
            if t.kind == "kw" and t.text not in ("const", "volatile", "auto"):
                break
            if t.text in (")", "]"):
                break
            m -= 1
        if name_tok == -1:
            continue
        if toks[name_tok].text in NOISE:
            continue
        d = Decl(toks[name_tok].text, "var", toks[name_tok].line, name_tok,
                 type_a, min(stop, name_tok))
        if stop < e:
            d.init_a, d.init_b = stop, e
        out.append(d)
    return out


class _StmtParser:
    def __init__(self, toks: list[Tok], func: Func | None = None):
        self.t = toks
        self.func = func

    def block(self, a: int, b: int, parent: Block | None = None) -> Block:
        """a points at '{', b at the index just past its '}'."""
        blk = Block(a, b, self.t[a].line, self.t[b - 1].line if b - 1 < len(self.t) else self.t[a].line)
        blk.parent = parent
        j = a + 1
        end = b - 1
        while True:
            j = _skip_noise(self.t, j, end)
            if j >= end:
                break
            s = self.statement(j, end, blk)
            if s is None:
                break
            s.parent = blk
            blk.stmts.append(s)
            blk.decls.extend(s.decls)
            j = s.b
        return blk

    def _sub(self, j: int, end: int, parent: Block | None):
        """A loop/branch body: either a block or a single statement."""
        j = _skip_noise(self.t, j, end)
        if j >= end:
            return None, j
        if self.t[j].text == "{":
            e = matching(self.t, j) + 1
            return self.block(j, e, parent), e
        s = self.statement(j, end, parent)
        return s, (s.b if s else j + 1)

    def statement(self, j: int, end: int, parent: Block | None) -> Stmt | None:
        t = self.t
        j = _skip_noise(t, j, end)
        if j >= end:
            return None
        tok = t[j]
        ln = tok.line

        if tok.text == ";":
            return Stmt("empty", j, j + 1, ln, ln)

        if tok.text == "{":
            e = matching(t, j) + 1
            st = Stmt("block", j, e, ln, t[min(e - 1, len(t) - 1)].line)
            st.body = self.block(j, e, parent)
            return st

        if tok.kind == "kw":
            w = tok.text
            if w in ("if",):
                k = j + 1
                k = _skip_noise(t, k, end)
                if k < end and t[k].kind == "kw" and t[k].text == "constexpr":
                    k += 1
                    k = _skip_noise(t, k, end)
                if k < end and t[k].text == "(":
                    ce = matching(t, k)
                    st = Stmt("if", j, ce + 1, ln, ln)
                    st.cond = (k + 1, ce)
                    st.decls = declared_names(t, k + 1, ce, require_init=True)
                    body, nxt = self._sub(ce + 1, end, parent)
                    st.body = body
                    k = _skip_noise(t, nxt, end)
                    if k < end and t[k].kind == "kw" and t[k].text == "else":
                        alt, nxt2 = self._sub(k + 1, end, parent)
                        st.alt = alt
                        nxt = nxt2
                    st.b = nxt
                    st.end_line = t[min(nxt - 1, len(t) - 1)].line
                    return st

            if w in ("while", "switch"):
                k = _skip_noise(t, j + 1, end)
                if k < end and t[k].text == "(":
                    ce = matching(t, k)
                    st = Stmt(w, j, ce + 1, ln, ln)
                    st.cond = (k + 1, ce)
                    st.cond_always_true = _always_true(t, k + 1, ce)
                    st.decls = declared_names(t, k + 1, ce, require_init=True)
                    body, nxt = self._sub(ce + 1, end, parent)
                    st.body = body
                    st.b = nxt
                    st.end_line = t[min(nxt - 1, len(t) - 1)].line
                    return st

            if w == "for":
                k = _skip_noise(t, j + 1, end)
                if k < end and t[k].text == "(":
                    ce = matching(t, k)
                    st = Stmt("for", j, ce + 1, ln, ln)
                    colon = find_top_level(t, k + 1, ce, ":")
                    semis = []
                    depth = 0
                    m = k + 1
                    while m < ce:
                        if t[m].kind == "punct":
                            if t[m].text in "([{":
                                depth += 1
                            elif t[m].text in ")]}":
                                depth -= 1
                            elif t[m].text == ";" and depth == 0:
                                semis.append(m)
                        m += 1
                    if colon != -1 and not semis:
                        st.kind = "range-for"
                        st.init = (k + 1, colon)
                        st.cond = (colon + 1, ce)
                        st.decls = declared_names(t, k + 1, colon)
                    elif len(semis) >= 2:
                        st.init = (k + 1, semis[0])
                        st.cond = (semis[0] + 1, semis[1])
                        st.incr = (semis[1] + 1, ce)
                        st.decls = declared_names(t, k + 1, semis[0])
                    body, nxt = self._sub(ce + 1, end, parent)
                    st.body = body
                    st.b = nxt
                    st.end_line = t[min(nxt - 1, len(t) - 1)].line
                    return st

            if w == "do":
                body, nxt = self._sub(j + 1, end, parent)
                st = Stmt("do", j, nxt, ln, ln)
                st.body = body
                k = _skip_noise(t, nxt, end)
                if k < end and t[k].text == "while":
                    k = _skip_noise(t, k + 1, end)
                    if k < end and t[k].text == "(":
                        ce = matching(t, k)
                        st.cond = (k + 1, ce)
                        st.cond_always_true = _always_true(t, k + 1, ce)
                        k = ce + 1
                    while k < end and t[k].text != ";":
                        k += 1
                    k += 1
                st.b = k
                st.end_line = t[min(k - 1, len(t) - 1)].line
                return st

            if w == "try":
                k = _skip_noise(t, j + 1, end)
                st = Stmt("try", j, j + 1, ln, ln)
                if k < end and t[k].text == "{":
                    e = matching(t, k) + 1
                    st.body = self.block(k, e, parent)
                    k = e
                while True:
                    k = _skip_noise(t, k, end)
                    if k < end and t[k].kind == "kw" and t[k].text == "catch":
                        m = _skip_noise(t, k + 1, end)
                        cd = None
                        if m < end and t[m].text == "(":
                            ce = matching(t, m)
                            cd = (m + 1, ce)
                            m = ce + 1
                        m = _skip_noise(t, m, end)
                        cst = Stmt("catch", k, m, t[k].line, t[k].line)
                        cst.cond = cd
                        if m < end and t[m].text == "{":
                            e = matching(t, m) + 1
                            cst.body = self.block(m, e, parent)
                            m = e
                        cst.b = m
                        cst.end_line = t[min(m - 1, len(t) - 1)].line
                        st.parts.append(cst)
                        k = m
                        continue
                    break
                st.b = k
                st.end_line = t[min(k - 1, len(t) - 1)].line
                return st

            if w in ("case", "default"):
                k = j + 1
                depth = 0
                while k < end:
                    if t[k].kind == "punct":
                        if t[k].text in "([{":
                            depth += 1
                        elif t[k].text in ")]}":
                            depth -= 1
                        elif t[k].text == ":" and depth == 0:
                            break
                    k += 1
                st = Stmt("label", j, k + 1, ln, t[min(k, len(t) - 1)].line)
                return st

            if w in ("return", "break", "continue", "goto", "throw"):
                k = find_top_level(t, j, end, ";")
                if k == -1:
                    k = end - 1
                st = Stmt(w, j, k + 1, ln, t[min(k, len(t) - 1)].line)
                if w in ("return", "throw") and k > j + 1:
                    st.cond = (j + 1, k)
                return st

        # a label: `identifier :` not followed by ':'
        if tok.kind == "id" and j + 1 < end and t[j + 1].text == ":":
            return Stmt("label", j, j + 2, ln, ln)

        # plain statement up to the next top-level ';'
        k = find_top_level(t, j, end, ";")
        if k == -1:
            # a trailing construct without a semicolon - take the rest
            k = end - 1
        st = Stmt("expr", j, k + 1, ln, t[min(k, len(t) - 1)].line)
        st.decls = declared_names(t, j, k)
        if st.decls:
            st.kind = "decl"
        return st


# --------------------------------------------------------------------------
# declaration-level parsing
# --------------------------------------------------------------------------

class Parser:
    def __init__(self, path, rel_: str, src: str):
        self.toks = lex(src)
        self.unit = Unit(path, rel_, src, self.toks)
        self.ns: list[str] = []

    def parse(self) -> Unit:
        self._preprocessor()
        self._decls(0, len(self.toks), cls=None, access="public")
        return self.unit

    # ---- preprocessor ----------------------------------------------------
    def _preprocessor(self) -> None:
        t = self.toks
        n = len(t)
        j = 0
        if_stack: list[tuple[str, int, bool]] = []
        while j < n:
            if t[j].kind == "pp" and t[j].text == "#":
                k = j + 1
                if k < n and t[k].pp:
                    word = t[k].text
                    line = t[k].line
                    if word == "include":
                        m = k + 1
                        parts = []
                        while m < n and t[m].pp and t[m].line == line:
                            parts.append(t[m].text)
                            m += 1
                        self.unit.includes.append(("".join(parts), line))
                    elif word == "define":
                        m = k + 1
                        if m < n and t[m].pp:
                            self.unit.macros[t[m].text] = t[m].line
                    if word in ("if", "ifdef", "ifndef", "else", "elif", "endif"):
                        self.unit.pp_cond_lines.add(line)
                    if word in ("if", "ifdef", "ifndef"):
                        rest = []
                        m = k + 1
                        while m < n and t[m].pp and t[m].line == line:
                            rest.append(t[m].text)
                            m += 1
                        dead = word == "if" and "".join(rest).strip() in ("0", "false")
                        if_stack.append((word, line, dead))
                    elif word in ("endif",):
                        if if_stack:
                            w, ln, dead = if_stack.pop()
                            if dead:
                                self.unit.pp_if_zero.append((ln, line))
            j += 1

    # ---- declarations ----------------------------------------------------
    def _decls(self, a: int, b: int, cls: TypeDef | None, access: str) -> None:
        t = self.toks
        j = a
        start = a
        while j < b:
            if t[j].kind == "comment" or t[j].pp:
                if j == start:
                    start = j + 1
                j += 1
                continue
            tok = t[j]

            if tok.kind == "kw" and tok.text in ACCESS and \
               j + 1 < b and t[j + 1].text == ":":
                access = tok.text
                j += 2
                start = j
                continue
            # Qt's Q_SIGNALS / Q_SLOTS sections
            if tok.kind == "id" and tok.text in ("signals", "Q_SIGNALS") and \
               j + 1 < b and t[j + 1].text == ":":
                access = "signals"
                j += 2
                start = j
                continue
            if tok.kind == "id" and tok.text in ("slots", "Q_SLOTS") and \
               j + 1 < b and t[j + 1].text == ":":
                access = "slots" if access not in ACCESS else access + " slots"
                j += 2
                start = j
                continue

            if tok.kind == "punct":
                if tok.text in "([":
                    j = matching(t, j) + 1
                    continue
                if tok.text == "<" and j > start:
                    prev = t[_prev_code(t, j - 1, start)]
                    if prev.kind in ("id", "kw"):
                        nj = _skip_template_args(t, j, b)
                        if nj > j:
                            j = nj
                            continue
                if tok.text == ";":
                    self._finish_decl(start, j, cls, access, has_body=False)
                    j += 1
                    start = j
                    continue
                if tok.text == "{":
                    e = matching(t, j)
                    self._finish_decl(start, j, cls, access,
                                      has_body=True, brace=j, brace_end=e)
                    j = e + 1
                    # a type definition is followed by declarators then ';'
                    k = _skip_noise(t, j, b)
                    if k < b and t[k].text == ";":
                        j = k + 1
                    start = j
                    continue
                if tok.text == "}":
                    j += 1
                    start = j
                    continue
            j += 1
        return

    def _finish_decl(self, a: int, b: int, cls: TypeDef | None, access: str,
                     has_body: bool, brace: int = -1, brace_end: int = -1) -> None:
        t = self.toks
        a = _skip_noise(t, a, b if b > a else a + 1)
        if a >= b:
            return
        head = [x for x in t[a:b] if x.is_code]
        if not head:
            return

        words = [x.text for x in head]

        # ---- namespace -----------------------------------------------------
        if "namespace" in words[:2] and has_body:
            name = ""
            for x in head:
                if x.kind == "id":
                    name = x.text
                    break
            self.unit.namespaces.append(name)
            self.ns.append(name)
            self._decls(brace + 1, brace_end, cls, access)
            self.ns.pop()
            return

        if words[0] == "extern" and has_body:
            self._decls(brace + 1, brace_end, cls, access)
            return

        # ---- class / struct / union / enum ----------------------------------
        kw_i = -1
        for idx, x in enumerate(head):
            if x.kind == "kw" and x.text in ("class", "struct", "union", "enum"):
                kw_i = idx
                break
            if x.kind == "kw" and x.text in ("template", "export"):
                continue
            if x.kind == "id":
                continue
            if x.text in ("<", ">", ",", "::", "typename"):
                continue
            break
        paren = find_top_level(t, a, b, "(")
        if kw_i != -1 and has_body and paren == -1:
            name = ""
            k = kw_i + 1
            while k < len(head):
                x = head[k]
                if x.kind == "id":
                    nxt = head[k + 1].text if k + 1 < len(head) else ""
                    if nxt in ("", ":", "{", "final") or head[k + 1].kind == "id":
                        if nxt == "final" or nxt in ("", ":", "{"):
                            name = x.text
                            break
                    name = x.text
                k += 1
            if not name:
                for x in reversed(head):
                    if x.kind == "id":
                        name = x.text
                        break
            td = TypeDef(head[kw_i].text, name or "<anonymous>",
                         t[a].line, t[brace_end].line, a, brace_end + 1)
            td.file = self.unit.rel
            colon = find_top_level(t, a, brace, ":")
            if colon != -1:
                for s, e in split_top_level(t, colon + 1, brace, ","):
                    ids = [x.text for x in t[s:e] if x.kind == "id"]
                    if ids:
                        td.bases.append(ids[-1])
            self.unit.types.append(td)
            if td.kind == "enum":
                for s, e in split_top_level(t, brace + 1, brace_end, ","):
                    k2 = _skip_noise(t, s, e)
                    if k2 < e and t[k2].kind == "id":
                        d = Decl(t[k2].text, "enumerator", t[k2].line, k2)
                        td.fields.append(d)
                return
            inner_access = "private" if td.kind == "class" else "public"
            before = len(self.unit.funcs)
            before_g = len(self.unit.globals)
            self._decls(brace + 1, brace_end, td, inner_access)
            for f in self.unit.funcs[before:]:
                if f.cls in ("", td.name):
                    f.cls = td.name
                    td.methods.append(f)
                    if f.access == "signals":
                        td.signals.append(f)
                    elif "slots" in f.access:
                        td.slots_.append(f)
            for d in self.unit.globals[before_g:]:
                d.kind = "field"
                td.fields.append(d)
            td.is_qobject = any(x.text in ("Q_OBJECT", "Q_GADGET") for x in t[brace + 1:brace_end])
            self._collect_qt_macros(td, brace + 1, brace_end)
            return

        # ---- using / typedef / static_assert / friend ------------------------
        if words[0] in ("using", "typedef", "static_assert", "friend"):
            return

        # ---- function --------------------------------------------------------
        fn = self._as_function(a, b, cls, access, has_body, brace, brace_end)
        if fn is not None:
            return

        # ---- plain variable declaration ---------------------------------------
        if not has_body or brace == -1:
            for d in declared_names(t, a, b):
                d.kind = "field" if cls else "var"
                self.unit.globals.append(d)
        else:
            # `Type name{init};` or `Type name = {...};`
            for d in declared_names(t, a, brace):
                d.kind = "field" if cls else "var"
                self.unit.globals.append(d)

    def _collect_qt_macros(self, td: TypeDef, a: int, b: int) -> None:
        t = self.toks
        j = a
        while j < b:
            x = t[j]
            if x.kind == "id" and x.text == "Q_PROPERTY" and j + 1 < b and t[j + 1].text == "(":
                e = matching(t, j + 1)
                body = [y for y in t[j + 2:e] if y.is_code]
                prop: dict = {"line": x.line}
                words = [y.text for y in body]
                if len(words) >= 2:
                    prop["type"] = " ".join(words[:-1]) if len(words) < 3 else words[0]
                    # name is the token before the first keyword like READ
                    for k, w in enumerate(words):
                        if w in ("READ", "WRITE", "NOTIFY", "MEMBER", "CONSTANT", "FINAL"):
                            prop["name"] = words[k - 1] if k else ""
                            prop["type"] = " ".join(words[:k - 1])
                            break
                    else:
                        prop["name"] = words[-1]
                    for k, w in enumerate(words):
                        if w in ("READ", "WRITE", "NOTIFY", "MEMBER") and k + 1 < len(words):
                            prop[w.lower()] = words[k + 1]
                if prop.get("name"):
                    td.properties.append(prop)
                j = e + 1
                continue
            j += 1

    def _as_function(self, a: int, b: int, cls: TypeDef | None, access: str,
                     has_body: bool, brace: int, brace_end: int) -> Func | None:
        t = self.toks
        limit = brace if (has_body and brace != -1) else b
        # the first top-level ( whose preceding token names something
        groups = top_level_groups(t, a, limit, "(")
        if not groups:
            return None
        pa = pb = -1
        for (g0, g1) in groups:
            p = _prev_code(t, g0 - 1, a)
            if p < a:
                continue
            pt = t[p]
            if pt.kind == "id" or pt.text in (">", "operator") or \
               (pt.kind == "kw" and pt.text in ("operator",)):
                pa, pb = g0, g1
                break
        if pa == -1:
            return None

        name_i = _prev_code(t, pa - 1, a)
        name = t[name_i].text
        is_dtor = False
        is_op = False
        if name == ">":
            # operator< <...> ( - rare; bail
            return None
        k = _prev_code(t, name_i - 1, a)
        if k >= a and t[k].text == "~":
            is_dtor = True
            name = "~" + name
            name_i = k
        # operator names: `operator` `==` (
        if k >= a and t[k].kind == "kw" and t[k].text == "operator":
            is_op = True
            name = "operator" + name
            name_i = k
        elif t[name_i].kind == "kw" and t[name_i].text == "operator":
            is_op = True
            name = "operator()"

        owner = cls.name if cls else ""
        ret_end = name_i
        q = _prev_code(t, name_i - 1, a)
        if q >= a and t[q].text == "::":
            qq = _prev_code(t, q - 1, a)
            if qq >= a and t[qq].kind == "id":
                owner = t[qq].text
                ret_end = qq

        if not has_body:
            # a declaration, or a variable initialised by a call
            toks_after = [x.text for x in t[pb + 1:b] if x.is_code]
            if not is_op and not is_dtor and owner == (cls.name if cls else "") and \
               cls is None and not toks_after:
                # `Foo bar(1,2);` at namespace scope is a variable
                head_words = [x.text for x in t[a:name_i] if x.is_code]
                if len(head_words) == 1 and t[name_i].kind == "id" and \
                   all(y.kind in ("num", "str", "id", "punct") for y in t[pa + 1:pb]):
                    return None

        end_line = t[brace_end].line if (has_body and brace_end != -1) else t[max(a, b - 1)].line
        fn = Func(name, owner, t[a].line, end_line, a,
                  (brace_end + 1) if has_body and brace_end != -1 else b)
        fn.sig_a, fn.sig_b = a, limit
        fn.file = self.unit.rel
        fn.unit = self.unit
        fn.access = access
        fn.is_dtor = is_dtor
        fn.decl_only = not has_body
        fn.is_ctor = (not is_dtor) and owner != "" and name == owner
        fn.is_template = any(x.text == "template" for x in t[a:min(a + 4, b)] if x.is_code)
        ret_words = [x.text for x in t[a:ret_end] if x.is_code]
        fn.ret = " ".join(w for w in ret_words if w not in ("static", "virtual",
                                                            "inline", "explicit",
                                                            "constexpr", "friend"))
        for w in ("static", "virtual", "explicit", "inline", "constexpr", "friend"):
            if w in ret_words:
                fn.quals.add(w)
        tail = [x.text for x in t[pb + 1:limit] if x.is_code]
        for w in ("const", "override", "final", "noexcept", "= 0", "delete", "default"):
            if w in tail:
                fn.quals.add(w)
        if "=" in tail and "0" in tail:
            fn.quals.add("pure")
        if "=" in tail and "delete" in tail:
            fn.quals.add("deleted")
        if any(x.text == "Q_INVOKABLE" for x in t[a:name_i] if x.is_code):
            fn.quals.add("invokable")

        for s, e in split_top_level(t, pa + 1, pb, ","):
            s2 = _skip_noise(t, s, e)
            if s2 >= e:
                continue
            words = [x for x in t[s2:e] if x.is_code]
            if not words:
                continue
            if len(words) == 1 and words[0].text in ("void",):
                continue
            eq = find_top_level(t, s2, e, "=")
            stop = eq if eq != -1 else e
            pname = ""
            m = stop - 1
            while m >= s2:
                if t[m].kind == "id":
                    nx = t[m + 1].text if m + 1 < stop else ""
                    if nx in ("", ")", "[", ","):
                        pname = t[m].text
                    break
                if t[m].text in (")", "]"):
                    break
                m -= 1
            ptype = text_of(t, s2, (m if pname else stop))
            p = Param(pname, ptype.strip(), s2, e)
            p.has_default = eq != -1
            fn.params.append(p)

        if has_body and brace != -1:
            sp = _StmtParser(t, fn)
            fn.body = sp.block(brace, brace_end + 1)
            self._find_lambdas(fn)
        self.unit.funcs.append(fn)
        if fn.quals & {"invokable"} and cls is not None:
            cls.invokables.append(fn)
        return fn

    def _find_lambdas(self, fn: Func) -> None:
        t = self.toks
        j = fn.a
        while j < fn.b:
            if t[j].text == "[" and t[j].kind == "punct":
                e = matching(t, j)
                k = _skip_noise(t, e + 1, fn.b)
                if k < fn.b and t[k].text == "(":
                    k = matching(t, k) + 1
                    k = _skip_noise(t, k, fn.b)
                    while k < fn.b and t[k].text in ("mutable", "noexcept", "constexpr",
                                                     "->", "const"):
                        k += 1
                        if k < fn.b and t[k - 1].text == "->":
                            while k < fn.b and t[k].text not in ("{", ";"):
                                k += 1
                        k = _skip_noise(t, k, fn.b)
                    if k < fn.b and t[k].text == "{":
                        be = matching(t, k)
                        sp = _StmtParser(t, fn)
                        fn.lambdas.append(sp.block(k, be + 1))
                        j = be + 1
                        continue
                elif k < fn.b and t[k].text == "{":
                    be = matching(t, k)
                    sp = _StmtParser(t, fn)
                    fn.lambdas.append(sp.block(k, be + 1))
                    j = be + 1
                    continue
            j += 1


def parse_file(path, rel_: str) -> Unit:
    src = path.read_text(encoding="utf-8", errors="replace")
    return Parser(path, rel_, src).parse()
