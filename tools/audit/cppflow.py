"""Name resolution and control flow over a parsed function.

The parser says where things are. This says what they mean to each other:
which declaration a name refers to, whether a use is a read or a write,
whether a statement can be reached, and whether a guard actually guards
anything.

Two rules govern every judgement made here, both learned the hard way:

  1. When the model cannot see through something - a call that might take a
     reference, a macro, a template - the name is marked ESCAPED and no
     check is allowed to claim it is unused. Silence beats a wrong finding.

  2. A guard is a statement, not a name. "There is an `if` mentioning `ptr`
     somewhere in this function" is what a regex can tell you and is worth
     nothing. What matters is whether the guard's exit dominates the use.
"""
from __future__ import annotations

from .cpplex import Tok, matching
from .cppparse import Block, Decl, Func, Stmt, _prev_code

ASSIGN_OPS = {"=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="}
MEMBER_OPS = {".", "->", "::"}

# Calls that read a container but never modify it.
CONST_METHODS = {
    "size", "count", "length", "empty", "isEmpty", "at", "begin", "end",
    "cbegin", "cend", "front", "back", "first", "last", "contains", "value",
    "constData", "data", "toStdString", "c_str", "indexOf", "startsWith",
    "endsWith", "mid", "left", "right", "trimmed", "simplified", "toLower",
    "toUpper", "split", "join", "isNull", "isValid", "key", "keys", "values",
}

MUTATING_METHODS = {
    "push_back", "push_front", "append", "prepend", "emplace_back",
    "emplace", "insert", "erase", "remove", "removeAt", "removeOne", "clear",
    "resize", "reserve", "assign", "pop_back", "pop_front", "takeFirst",
    "takeLast", "swap", "sort", "fill", "squeeze", "detach", "setValue",
}


class Scope:
    __slots__ = ("block", "decls", "parent", "children")

    def __init__(self, block: Block | None, parent: "Scope | None"):
        self.block = block
        self.decls: dict[str, Decl] = {}
        self.parent = parent
        self.children: list[Scope] = []

    def lookup(self, name: str) -> Decl | None:
        s: Scope | None = self
        while s is not None:
            d = s.decls.get(name)
            if d is not None:
                return d
            s = s.parent
        return None


class FuncModel:
    """Everything resolvable about one function body."""

    __slots__ = ("fn", "toks", "root", "all_decls", "calls", "member_uses",
                 "unreachable", "returns", "by_token")

    def __init__(self, fn: Func, toks: list[Tok]):
        self.fn = fn
        self.toks = toks
        self.root = Scope(fn.body, None)
        self.all_decls: list[Decl] = []
        self.calls: list[tuple[str, int, str]] = []   # (name, token index, receiver)
        self.member_uses: dict[str, list[int]] = {}
        self.unreachable: list[Stmt] = []
        self.returns: list[Stmt] = []
        self.by_token: dict[int, Decl] = {}


def _is_member_access(toks: list[Tok], i: int) -> bool:
    p = i - 1
    while p >= 0 and (toks[p].kind == "comment" or toks[p].pp):
        p -= 1
    return p >= 0 and toks[p].text in MEMBER_OPS


def _next_code_tok(toks: list[Tok], i: int, limit: int) -> int:
    i += 1
    while i < limit and (toks[i].kind == "comment" or toks[i].pp):
        i += 1
    return i


def classify_use(toks: list[Tok], i: int, limit: int) -> str:
    """read | write | readwrite | address, for the identifier token at i."""
    n = _next_code_tok(toks, i, limit)
    nxt = toks[n].text if n < limit else ""
    p = i - 1
    while p >= 0 and (toks[p].kind == "comment" or toks[p].pp):
        p -= 1
    prv = toks[p].text if p >= 0 else ""

    if nxt == "=" :
        return "write"
    if nxt in ASSIGN_OPS:
        return "readwrite"
    if nxt in ("++", "--") or prv in ("++", "--"):
        return "readwrite"
    if prv == "&" :
        # &x - taking an address, unless it is a binary and
        pp = p - 1
        while pp >= 0 and (toks[pp].kind == "comment" or toks[pp].pp):
            pp -= 1
        if pp < 0 or toks[pp].text in ("(", ",", "=", "return", "{", "&&", "||", "!"):
            return "address"
    if nxt == "." or nxt == "->":
        m = _next_code_tok(toks, n, limit)
        meth = toks[m].text if m < limit else ""
        after = _next_code_tok(toks, m, limit)
        if after < limit and toks[after].text == "(":
            if meth in MUTATING_METHODS:
                return "readwrite"
            if meth in CONST_METHODS:
                return "read"
            return "readwrite"        # unknown method: assume it may mutate
        # member field access: x.f = ... is a write to x
        if after < limit and toks[after].text in ASSIGN_OPS:
            return "readwrite"
        return "read"
    if nxt == "[":
        e = matching(toks, n)
        after = _next_code_tok(toks, e, limit)
        if after < limit and toks[after].text in ASSIGN_OPS:
            return "readwrite"
        return "read"
    return "read"


def build(fn: Func, toks: list[Tok]) -> FuncModel:
    m = FuncModel(fn, toks)
    if fn.body is None:
        return m

    for p in fn.params:
        if p.name:
            d = Decl(p.name, "param", toks[p.a].line, p.a)
            d.scope_end = fn.b
            m.root.decls[p.name] = d
            m.all_decls.append(d)

    def visit(block: Block, scope: Scope) -> None:
        sc = Scope(block, scope)
        scope.children.append(sc)
        for st in block.stmts:
            for d in st.decls:
                # A name declared in a for/if/while header scopes to that
                # STATEMENT, not to the enclosing block. Scoping it to the
                # block made every `for (int i...)` appear to shadow every
                # later one in the same function - 192 findings, none real.
                d.scope_end = st.b if st.kind in (
                    "for", "range-for", "if", "while", "switch") else block.b
                sc.decls.setdefault(d.name, d)
                m.all_decls.append(d)
            for sub in (st.body, st.alt, *st.parts):
                if isinstance(sub, Block):
                    inner = Scope(sub, sc)
                    for d2 in st.decls:
                        inner.decls.setdefault(d2.name, d2)
                    sc.children.append(inner)
                    _visit_into(sub, inner)
                elif isinstance(sub, Stmt):
                    for d2 in sub.decls:
                        d2.scope_end = sub.b
                        sc.decls.setdefault(d2.name, d2)
                        m.all_decls.append(d2)
                    if isinstance(sub.body, Block):
                        inner = Scope(sub.body, sc)
                        sc.children.append(inner)
                        _visit_into(sub.body, inner)

    def _visit_into(block: Block, sc: Scope) -> None:
        for st in block.stmts:
            for d in st.decls:
                # A name declared in a for/if/while header scopes to that
                # STATEMENT, not to the enclosing block. Scoping it to the
                # block made every `for (int i...)` appear to shadow every
                # later one in the same function - 192 findings, none real.
                d.scope_end = st.b if st.kind in (
                    "for", "range-for", "if", "while", "switch") else block.b
                sc.decls.setdefault(d.name, d)
                m.all_decls.append(d)
            for sub in (st.body, st.alt, *st.parts):
                if isinstance(sub, Block):
                    inner = Scope(sub, sc)
                    for d2 in st.decls:
                        inner.decls.setdefault(d2.name, d2)
                    sc.children.append(inner)
                    _visit_into(sub, inner)
                elif isinstance(sub, Stmt):
                    for d2 in sub.decls:
                        d2.scope_end = sub.b
                        sc.decls.setdefault(d2.name, d2)
                        m.all_decls.append(d2)
                    if isinstance(sub.body, Block):
                        inner = Scope(sub.body, sc)
                        sc.children.append(inner)
                        _visit_into(sub.body, inner)

    visit(fn.body, m.root)

    # --- uses -------------------------------------------------------------
    by_name: dict[str, list[Decl]] = {}
    for d in m.all_decls:
        by_name.setdefault(d.name, []).append(d)

    limit = fn.b
    i = fn.a
    while i < limit:
        t = toks[i]
        if not t.is_code:
            i += 1
            continue
        if t.kind == "id":
            nxt = _next_code_tok(toks, i, limit)
            nxt_text = toks[nxt].text if nxt < limit else ""
            if nxt_text == "(" and not _is_member_access(toks, i):
                m.calls.append((t.text, i, ""))
            elif nxt_text == "(" and _is_member_access(toks, i):
                p = _prev_code(toks, i - 1, fn.a)
                recv = ""
                if p - 1 >= fn.a and toks[p - 1].kind == "id":
                    recv = toks[p - 1].text
                m.calls.append((t.text, i, recv))

            if t.text.endswith("_") and _is_member_access(toks, i) is False and \
               by_name.get(t.text) is None:
                m.member_uses.setdefault(t.text, []).append(i)

            cands = by_name.get(t.text)
            if cands and not _is_member_access(toks, i):
                best = None
                for d in cands:
                    if d.tok <= i <= (d.scope_end if d.scope_end > 0 else limit):
                        if best is None or d.tok > best.tok:
                            best = d
                if best is not None:
                    m.by_token[i] = best
                    if i != best.tok:
                        use = classify_use(toks, i, limit)
                        if use == "write":
                            best.writes.append(i)
                        elif use == "readwrite":
                            best.reads.append(i)
                            best.writes.append(i)
                        elif use == "address":
                            best.escapes = True
                            best.reads.append(i)
                        else:
                            best.reads.append(i)
        i += 1

    m.unreachable = unreachable_statements(
        fn.body, fn.unit.pp_cond_lines if fn.unit else None)
    m.returns = [s for s in fn.body.walk() if s.kind == "return"]
    return m


# --------------------------------------------------------------------------
# control flow
# --------------------------------------------------------------------------

def terminates(s: Stmt | Block | None) -> bool:
    """True when control cannot fall out of the bottom of this statement."""
    if s is None:
        return False
    if isinstance(s, Block):
        return any(terminates(x) for x in s.stmts)
    if s.kind in ("return", "break", "continue", "goto", "throw"):
        return True
    if s.kind == "if":
        return bool(s.alt) and terminates(s.body) and terminates(s.alt)
    if s.kind == "block":
        return terminates(s.body)
    if s.kind == "try":
        return terminates(s.body) and all(terminates(p.body) for p in s.parts)
    if s.kind in ("while", "do"):
        # A LOOP THAT NEVER ENDS DOES NOT FALL OUT OF ITS BOTTOM.
        #
        # This said "while(true) without a break" and then returned False
        # whatever it found: it built a string out of a `_range()` helper that
        # returned an empty list, discarded the string, and fell through. The
        # intent was written down and never implemented, and nothing noticed -
        # an under-reporting `terminates` shows up as findings that are absent,
        # which is the shape this project has recorded as "a check that cannot
        # fire is not a check".
        #
        # The condition is answered by the parser, where the tokens are, and
        # only for the literal forms - `while (true)`, `while (1)`, `for (;;)`.
        # A condition that is always true for a reason a reader has to work out
        # is not one this claims to know.
        return bool(getattr(s, "cond_always_true", False)) and not _breaks_out(s.body)
    return False


def _breaks_out(node, depth: int = 0) -> bool:
    """Whether a `break` inside this body belongs to the loop that owns it.

    A `break` in a nested loop or switch belongs to THAT one, so the walk stops
    at the boundary rather than counting it.
    """
    if node is None:
        return False
    if isinstance(node, Block):
        return any(_breaks_out(x, depth) for x in node.stmts)
    if node.kind == "break":
        return depth == 0
    if node.kind in ("while", "do", "for", "switch", "range-for"):
        return False
    return (_breaks_out(node.body, depth)
            or _breaks_out(node.alt, depth)
            or any(_breaks_out(p.body, depth) for p in (node.parts or [])))


def unreachable_statements(block: Block,
                           pp_cond_lines: set[int] | None = None) -> list[Stmt]:
    """Statements control can never arrive at.

    A label CLEARS the dead flag rather than being skipped over: `case` is a
    jump target, so `case A: return 1; case B: return 2;` has nothing dead in
    it. Skipping labels instead of clearing on them reported every arm of
    every switch in the codebase - 214 of them - as unreachable code.

    `pp_cond_lines` are the lines carrying #if/#else/#endif. Two returns in
    the two halves of an #ifdef are not one return followed by dead code, and
    this model cannot see which half is compiled, so it says nothing.
    """
    out: list[Stmt] = []
    for b in block.blocks():
        dead = False
        prev_end = 0
        for st in b.stmts:
            if st.kind == "label":
                dead = False
                prev_end = st.end_line
                continue
            if st.kind == "empty":
                continue
            if dead:
                if not (pp_cond_lines and
                        any(prev_end <= L <= st.line for L in pp_cond_lines)):
                    out.append(st)
                dead = False        # report the first one only, per block
            if terminates(st):
                dead = True
            prev_end = st.end_line
    return out


def exits_early(s: Stmt) -> bool:
    """An `if` whose body leaves the function: the shape of a guard."""
    return s.kind == "if" and s.body is not None and terminates(s.body) and not s.alt


def names_in(toks: list[Tok], a: int, b: int) -> set[str]:
    out = set()
    for i in range(max(0, a), min(b, len(toks))):
        t = toks[i]
        if t.kind == "id" and t.is_code and not _is_member_access(toks, i):
            out.add(t.text)
    return out


def all_names_in(toks: list[Tok], a: int, b: int) -> set[str]:
    return {toks[i].text for i in range(max(0, a), min(b, len(toks)))
            if toks[i].kind == "id" and toks[i].is_code}


def statement_calls(toks: list[Tok], a: int, b: int) -> list[str]:
    out = []
    for i in range(max(0, a), min(b, len(toks))):
        t = toks[i]
        if t.kind == "id" and t.is_code:
            n = _next_code_tok(toks, i, b)
            if n < b and toks[n].text == "(":
                out.append(t.text)
    return out


def normalise(toks: list[Tok], a: int, b: int) -> str:
    """A canonical rendering of a token range, for equality comparison."""
    return " ".join(t.text for t in toks[max(0, a):min(b, len(toks))] if t.is_code)


def complexity(fn: Func) -> int:
    """Cyclomatic complexity: one plus every branch point."""
    if fn.body is None:
        return 1
    c = 1
    for st in fn.body.walk():
        if st.kind in ("if", "for", "range-for", "while", "do", "catch"):
            c += 1
        elif st.kind == "label":
            c += 1
    toks = fn.unit.toks if fn.unit else []
    for i in range(fn.a, min(fn.b, len(toks))):
        if toks[i].is_code and toks[i].text in ("&&", "||", "?"):
            c += 1
    return c


def max_depth(block: Block, d: int = 0) -> int:
    best = d
    for st in block.stmts:
        for sub in (st.body, st.alt, *st.parts):
            if isinstance(sub, Block):
                best = max(best, max_depth(sub, d + 1))
            elif isinstance(sub, Stmt) and isinstance(sub.body, Block):
                best = max(best, max_depth(sub.body, d + 1))
    return best
