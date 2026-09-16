"""An exact C++ lexer, in pure Python, with no dependencies.

Why hand-written rather than libclang
-------------------------------------
libclang needs the real include tree, the real compile flags and a matching
binary wheel on the machine that runs the audit. The audit has to run on a
Windows box, during a build, with nothing installed, against headers half of
which will not resolve without Qt's own include paths. A parser that needs a
working build is a parser that stops working exactly when you most want to
know what is wrong.

So this is a lexer that is exact about the things a static reader can be exact
about - where a token starts and ends, what is a comment, what is inside a
string - and honest about the rest. Everything downstream is built on the
promise made here: if a name appears as an identifier token, it appears in the
code, not in prose and not inside a string literal. Nearly every wrong finding
this audit has ever produced traces back to that promise being made by a
regular expression that could not keep it.

The hard parts, all of which have bitten a naive scanner in this codebase:

  * raw strings:  R"json({"a":1})json"  - contains quotes, braces and comment
    markers, and ends only at the matching delimiter.
  * digit separators:  1'000'000  - an apostrophe that is not a character
    literal.
  * line splices:  a backslash at end of line, inside or outside a token.
  * comment markers inside strings:  "http://example.com"  is not a comment.
  * string markers inside comments:  // don't  is not an unterminated string.
"""
from __future__ import annotations

KEYWORDS = {
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
    "bool", "break", "case", "catch", "char", "char8_t", "char16_t",
    "char32_t", "class", "co_await", "co_return", "co_yield", "compl",
    "concept", "const", "consteval", "constexpr", "constinit", "const_cast",
    "continue", "decltype", "default", "delete", "do", "double",
    "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false",
    "float", "for", "friend", "goto", "if", "inline", "int", "long",
    "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr",
    "operator", "or", "or_eq", "private", "protected", "public", "register",
    "reinterpret_cast", "requires", "return", "short", "signed", "sizeof",
    "static", "static_assert", "static_cast", "struct", "switch", "template",
    "this", "thread_local", "throw", "true", "try", "typedef", "typeid",
    "typename", "union", "unsigned", "using", "virtual", "void", "volatile",
    "wchar_t", "while", "xor", "xor_eq",
}

# Type-ish keywords that can legitimately open a declaration.
DECL_KEYWORDS = {
    "auto", "bool", "char", "char8_t", "char16_t", "char32_t", "class",
    "const", "constexpr", "consteval", "constinit", "double", "enum",
    "explicit", "extern", "float", "friend", "inline", "int", "long",
    "mutable", "register", "short", "signed", "static", "struct",
    "thread_local", "typedef", "typename", "union", "unsigned", "virtual",
    "void", "volatile", "wchar_t",
}

CONTROL_KEYWORDS = {
    "if", "else", "for", "while", "do", "switch", "case", "default", "try",
    "catch", "return", "break", "continue", "goto", "throw",
}

# Longest-first, so that >>= is not read as >> then =.
PUNCTUATORS = [
    "<<=", ">>=", "->*", "...", "<=>",
    "::", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", ".*",
    "{", "}", "[", "]", "(", ")", ";", ":", "?", ".", "+", "-", "*", "/",
    "%", "^", "&", "|", "~", "!", "=", "<", ">", ",", "#", "\\", "@", "$",
]


class Tok:
    """One token.

    `kind` is one of: id, kw, num, str, chr, punct, comment, pp.
    `line` is 1-based and is the line the token STARTS on.
    `pp` marks every token belonging to a preprocessor directive, including
    the directive body, so that a parser can skip directives wholesale without
    having to re-detect them.
    """

    __slots__ = ("kind", "text", "line", "col", "pp", "i")

    def __init__(self, kind: str, text: str, line: int, col: int, pp: bool = False):
        self.kind = kind
        self.text = text
        self.line = line
        self.col = col
        self.pp = pp
        self.i = -1          # index into the token list, filled in by lex()

    def __repr__(self) -> str:  # pragma: no cover - debugging aid
        return f"Tok({self.kind},{self.text!r},L{self.line})"

    @property
    def is_code(self) -> bool:
        return self.kind not in ("comment",) and not self.pp


def lex(src: str) -> list[Tok]:
    """Tokenise a translation unit. Comments are kept, as comment tokens."""
    out: list[Tok] = []
    n = len(src)
    i = 0
    line = 1
    bol = 0                    # index of the beginning of the current line
    at_line_start = True       # only counting whitespace so far on this line
    pp_depth_line = -1         # the logical line a directive occupies

    def col_of(idx: int) -> int:
        return idx - bol + 1

    while i < n:
        c = src[i]

        # --- line splice: backslash immediately before a newline ------------
        if c == "\\" and i + 1 < n and (src[i + 1] == "\n" or src[i + 1:i + 3] == "\r\n"):
            i += 2 if src[i + 1] == "\n" else 3
            line += 1
            bol = i
            # A spliced directive is still the same logical directive. Without
            # this, every continuation line of a multi-line #define lexed as
            # ordinary code, and the parser dutifully reported the macro body
            # as a function with unused locals in it.
            if pp_depth_line != -1:
                pp_depth_line = line
            continue

        # --- newline --------------------------------------------------------
        if c == "\n":
            i += 1
            line += 1
            bol = i
            at_line_start = True
            if pp_depth_line != -1:
                pp_depth_line = -1
            continue

        if c in " \t\r\f\v":
            i += 1
            continue

        in_pp = pp_depth_line == line

        # --- comments --------------------------------------------------------
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            start, sl = i, line
            i += 2
            while i < n:
                if src[i] == "\\" and i + 1 < n and src[i + 1] == "\n":
                    i += 2
                    line += 1
                    bol = i
                    continue
                if src[i] == "\n":
                    break
                i += 1
            out.append(Tok("comment", src[start:i], sl, col_of(start), in_pp))
            at_line_start = False
            continue

        if c == "/" and i + 1 < n and src[i + 1] == "*":
            start, sl = i, line
            i += 2
            while i + 1 < n and not (src[i] == "*" and src[i + 1] == "/"):
                if src[i] == "\n":
                    line += 1
                    bol = i + 1
                i += 1
            i = min(i + 2, n)
            out.append(Tok("comment", src[start:i], sl, col_of(start), in_pp))
            at_line_start = False
            continue

        # --- preprocessor directive -----------------------------------------
        if c == "#" and at_line_start:
            pp_depth_line = line
            out.append(Tok("pp", "#", line, col_of(i), True))
            i += 1
            at_line_start = False
            continue

        at_line_start = False

        # --- raw string literal ----------------------------------------------
        # Optional encoding prefix, then R, then "delim( ... )delim"
        if c in "RuUL" or (c in "u8" and src[i:i + 2] == "u8"):
            j = i
            if src.startswith("u8", j):
                j += 2
            elif src[j] in "uUL":
                j += 1
            if j < n and src[j] == "R" and j + 1 < n and src[j + 1] == '"':
                start, sl = i, line
                j += 2
                d0 = j
                while j < n and src[j] not in "( \t\n\\":
                    j += 1
                delim = src[d0:j]
                closer = ")" + delim + '"'
                if j < n and src[j] == "(":
                    j += 1
                    end = src.find(closer, j)
                    if end == -1:
                        end = n
                        j = n
                    else:
                        j = end + len(closer)
                    body = src[start:j]
                    line += body.count("\n")
                    if "\n" in body:
                        bol = start + body.rfind("\n") + 1
                    out.append(Tok("str", body, sl, col_of(start), in_pp))
                    i = j
                    continue

        # --- string / character literal ---------------------------------------
        if c in "\"'" or (c in "uUL" and i + 1 < n and src[i + 1] in "\"'") or \
           (src.startswith("u8", i) and i + 2 < n and src[i + 2] in "\"'"):
            start, sl = i, line
            if src.startswith("u8", i):
                i += 2
            elif src[i] in "uUL" and src[i + 1] in "\"'":
                i += 1
            quote = src[i]
            i += 1
            while i < n:
                ch = src[i]
                if ch == "\\":
                    if i + 1 < n and src[i + 1] == "\n":
                        i += 2
                        line += 1
                        bol = i
                        continue
                    i += 2
                    continue
                if ch == quote:
                    i += 1
                    break
                if ch == "\n":       # unterminated - stop at the line end
                    break
                i += 1
            out.append(Tok("str" if quote == '"' else "chr",
                           src[start:i], sl, col_of(start), in_pp))
            continue

        # --- number -------------------------------------------------------------
        if c.isdigit() or (c == "." and i + 1 < n and src[i + 1].isdigit()):
            start = i
            i += 1
            while i < n:
                ch = src[i]
                if ch.isalnum() or ch == "." or ch == "_":
                    i += 1
                elif ch == "'" and i + 1 < n and src[i + 1].isalnum():
                    i += 2                                   # digit separator
                elif ch in "+-" and src[i - 1] in "eEpP" and \
                        not src[start:i].lower().startswith("0x") or \
                        (ch in "+-" and src[i - 1] in "pP"):
                    i += 1
                else:
                    break
            out.append(Tok("num", src[start:i], line, col_of(start), in_pp))
            continue

        # --- identifier / keyword -------------------------------------------------
        if c.isalpha() or c == "_" or ord(c) > 127:
            start = i
            i += 1
            while i < n and (src[i].isalnum() or src[i] == "_" or ord(src[i]) > 127):
                i += 1
            word = src[start:i]
            out.append(Tok("kw" if word in KEYWORDS else "id",
                           word, line, col_of(start), in_pp))
            continue

        # --- punctuator ---------------------------------------------------------
        for p in PUNCTUATORS:
            if src.startswith(p, i):
                out.append(Tok("punct", p, line, col_of(i), in_pp))
                i += len(p)
                break
        else:
            i += 1                                   # unknown byte: skip it

    for idx, t in enumerate(out):
        t.i = idx
    return out


def text_of(toks: list[Tok], a: int, b: int) -> str:
    """A readable one-line rendering of a token range, for evidence."""
    parts: list[str] = []
    for t in toks[a:b]:
        if t.kind == "comment":
            continue
        s = t.text
        if parts and (s.isalnum() or s[0] == "_" or s[0] in "\"'") and \
           (parts[-1][-1].isalnum() or parts[-1][-1] in "_\"'"):
            parts.append(" ")
        parts.append(s)
    out = "".join(parts)
    return out if len(out) <= 160 else out[:157] + "..."


def matching(toks: list[Tok], i: int) -> int:
    """Index of the bracket closing the one at `i`, or len(toks) if unclosed."""
    opens = {"(": ")", "[": "]", "{": "}"}
    if toks[i].text not in opens:
        return i
    want = opens[toks[i].text]
    depth = 0
    n = len(toks)
    j = i
    while j < n:
        t = toks[j]
        if t.kind == "punct":
            if t.text in opens:
                depth += 1
            elif t.text in (")", "]", "}"):
                depth -= 1
                if depth == 0:
                    return j if t.text == want else j
        j += 1
    return n


def skip_angles(toks: list[Tok], j: int, b: int) -> int:
    """If `<` at j opens a template argument list, the index just past its `>`.

    Otherwise -1. The distinction matters because a comma inside QMap<K,V> is
    not a declarator separator, and a parser that thinks it is will declare a
    variable called QString. That is not a hypothetical: it is what the first
    version of this file did, twice.
    """
    if toks[j].text != "<":
        return -1
    depth = 0
    k = j
    while k < b:
        t = toks[k]
        if t.kind == "punct":
            if t.text == "<":
                depth += 1
            elif t.text == ">":
                depth -= 1
                if depth == 0:
                    return k + 1
            elif t.text == ">>":
                depth -= 2
                if depth <= 0:
                    return k + 1
            elif t.text in "([{":
                k = matching(toks, k)
            elif t.text in (";", ")", "}", "{"):
                return -1
        k += 1
    return -1


def split_top_level(toks: list[Tok], a: int, b: int, sep: str = ",",
                    angles: bool = True) -> list[tuple[int, int]]:
    """Split a token range on `sep` at bracket depth zero.

    With `angles`, a template argument list counts as a bracket, so that
    QMap<QString,double> is one type rather than two declarators.
    """
    spans: list[tuple[int, int]] = []
    depth = 0
    start = a
    j = a
    while j < b:
        t = toks[j]
        if t.kind == "punct":
            if angles and t.text == "<" and depth == 0 and j > a and \
                    toks[j - 1].kind in ("id", "kw"):
                nk = skip_angles(toks, j, b)
                if nk != -1:
                    j = nk
                    continue
            if t.text in "([{":
                depth += 1
            elif t.text in ")]}":
                depth -= 1
            elif t.text == sep and depth == 0:
                spans.append((start, j))
                start = j + 1
        j += 1
    if start < b:
        spans.append((start, b))
    return spans
