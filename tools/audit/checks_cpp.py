"""C++ checks that need a parser rather than a pattern.

Every check here is registered with a rationale and its known false positives,
so that a reader - person or agent - can evaluate the finding instead of
trusting it. A finding without a stated method is an opinion.

Four of these exist because of bugs this project actually shipped, and they
are the four worth reading first:

  frozen_list_rule       a rule correct for one class, applied to all
  guard_after_use        a guard that runs after the work it prevents
  filename_in_logic      a guard that names a file, and goes stale when split
  two_answers            two independent answers to one question

The rest are the ordinary kind.
"""
from __future__ import annotations

import re
from collections import Counter, defaultdict

from .cppflow import (CONST_METHODS, all_names_in, complexity, exits_early,
                      max_depth, names_in, normalise, statement_calls,
                      terminates)
from .cpplex import matching, text_of
from .cppparse import Block, Stmt, split_top_level
from .model import Finding, describe

PRIMITIVE = {
    "int", "double", "float", "bool", "char", "long", "short", "unsigned",
    "signed", "size_t", "qreal", "qint8", "qint16", "qint32", "qint64",
    "quint8", "quint16", "quint32", "quint64", "int8_t", "int16_t",
    "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "void", "std::size_t", "qsizetype", "const",
}

CHEAP_CALLS = CONST_METHODS | {
    "min", "max", "abs", "qMin", "qMax", "qAbs", "qBound", "size", "count",
    "isEmpty", "empty", "length", "data", "begin", "end", "constBegin",
    "constEnd", "qFuzzyCompare", "isFinite", "isNaN", "std::min", "std::max",
    # Stored dimensions and character classification: inline field reads and
    # table lookups. `for (int x = 0; x < img.width(); ++x)` is idiomatic and
    # costs nothing, and reporting it is how a check loses its reader.
    "width", "height", "depth", "format", "bytesPerLine", "sizeInBytes",
    "isDigit", "isLetter", "isLetterOrNumber", "isSpace", "isUpper",
    "isLower", "isNull", "isPunct", "unicode", "toLatin1", "cell", "row",
    "column", "rowCount", "columnCount", "x", "y", "z", "w",
}

# Calls whose answer CHANGES ON ITS OWN. Hoisting one out of a loop condition
# does not make the loop faster, it makes it infinite: `while (t.elapsed() <
# ms)` is waiting for the clock, and the clock is not in the loop body for any
# mutation test to find.
# Qt's literal constructors: compile-time or a pointer copy, and idiomatic
# inside a condition.
QT_LITERALS = {
    "QStringLiteral", "QLatin1Char", "QLatin1String", "QChar", "QString",
    "QByteArray", "QPoint", "QPointF", "QSize", "QSizeF", "QRect", "QRectF",
    "QColor", "QVariant",
}

SELF_CHANGING = {
    "elapsed", "nsecsElapsed", "hasExpired", "currentMSecsSinceEpoch",
    "currentSecsSinceEpoch", "currentDateTime", "currentTime", "now",
    "random", "rand", "bytesAvailable", "canReadLine", "waitForReadyRead",
    "hasPendingEvents", "processEvents", "hasNext", "peek",
}

GROWTH = {"push_back", "append", "emplace_back", "push_front", "prepend",
          "insert", "emplace"}

MARKERS = re.compile(r"\b(TODO|FIXME|HACK|XXX|BUG|WORKAROUND|TEMPORARY)\b")


def _line(u, n: int) -> str:
    return u.line_text(n)


def _seg(u, a: int, b: int) -> str:
    return text_of(u.toks, a, b)


# --------------------------------------------------------------------------
describe(
    "guard_after_use",
    "A guard runs after the work it exists to prevent",
    "A check placed below the first use of the thing it checks cannot "
    "prevent anything: by the time it runs, the dereference or the index has "
    "already happened. This is the most frequent root cause in this project's "
    "own history, and it is invisible to text search because a correct guard "
    "and a useless one are written identically - only their position differs.",
    "For each early-exit `if` whose condition is a pure NULL test on one "
    "pointer, look for an earlier dereference of that same pointer - `p->x` "
    "or `*p`. The declaration and its own initialiser do not count.",
    "A name reused for two unrelated pointers in one function.\n"
    "Deliberately NOT reported: `container.append(x)` before "
    "`if (container.isEmpty())`. Building a list and then checking whether it "
    "came out empty is the normal idiom, not a guard in the wrong place - "
    "treating it as one produced 110 findings here, none of them real.")


VALIDITY = re.compile(
    r"^\s*(!\s*\w+\s*$|\w+\s*==\s*(nullptr|NULL|0)\s*$|!\s*\w+\s*\.\s*isNull\s*\(\)\s*$)")


def guard_after_use(p, out: list) -> None:
    for u, fn, m in p.bodies():
        if fn.body is None:
            continue
        for st in fn.body.walk():
            if not exits_early(st) or st.cond is None:
                continue
            ca, cb = st.cond
            cond_text = _seg(u, ca, cb)
            if not VALIDITY.match(cond_text.strip()):
                continue
            tested = [n for n in names_in(u.toks, ca, cb)]
            if len(tested) != 1:
                continue
            name = tested[0]
            d = None
            for cand in m.all_decls:
                if cand.name == name and cand.tok < st.a:
                    if d is None or cand.tok > d.tok:
                        d = cand
            if d is None or d.kind not in ("var", "param"):
                continue
            # a use before the guard that actually touches the value
            hits = []
            for i in d.reads:
                if i >= st.a or i == d.tok:
                    continue
                if d.init_a != -1 and d.init_a <= i < d.init_b:
                    continue
                nxt = i + 1
                while nxt < len(u.toks) and not u.toks[nxt].is_code:
                    nxt += 1
                prev = i - 1
                while prev >= 0 and not u.toks[prev].is_code:
                    prev -= 1
                deref = nxt < len(u.toks) and u.toks[nxt].text == "->"
                star = (prev >= 0 and u.toks[prev].text == "*" and
                        (prev == 0 or u.toks[prev - 1].text in
                         ("(", ",", "=", "return", "{", "&&", "||", "!", ";")))
                if deref or star:
                    hits.append(i)
            if not hits:
                continue
            first = hits[0]
            use_line = u.toks[first].line
            if use_line >= st.line:
                continue
            out.append(Finding(
                "guard_after_use", u.rel, st.line, fn.qual,
                f"`{name}` is checked on line {st.line} but already used on line {use_line}",
                why=("The guard cannot prevent the earlier use. If the condition "
                     "can ever be true, the earlier line has already dereferenced, "
                     "indexed or called into it."),
                evidence=f"L{use_line}: {_line(u, use_line)}\nL{st.line}: {_line(u, st.line)}",
                severity="high", confidence="likely", category="correctness",
                suggestion=f"Move the `{name}` check above line {use_line}."))


# --------------------------------------------------------------------------
describe(
    "frozen_list_rule",
    "A hard-coded list used as a rule",
    "A list of names written into a function is a rule frozen at the moment "
    "somebody typed it. It is right for the cases that existed that day and "
    "silently wrong for every case added afterwards, and nothing fails when "
    "it goes stale. This project shipped exactly this bug: a windowing rule "
    "correct for one class of engine was applied to all 434, and the fix was "
    "to ASK the engine rather than consult a list.",
    "A function body containing a literal container of four or more string "
    "literals, where the same function then performs a membership test "
    "(contains / indexOf / find / any comparison chain) against it.",
    "Genuine fixed vocabularies - file extensions, units, an enum's spelling "
    "- are legitimately lists and will be reported here.")


def frozen_list_rule(p, out: list) -> None:
    for u, fn, m in p.bodies():
        toks = u.toks
        j = fn.a
        while j < fn.b:
            t = toks[j]
            if t.kind == "punct" and t.text == "{":
                e = matching(toks, j)
                if e - j < 8:
                    j += 1
                    continue
                # NB: never jump to `e` here. The first brace in a function is
                # its own body, and skipping past it skipped every list inside
                # it - which is every list this check exists to find.
                inner = toks[j + 1:e]
                strs = [x for x in inner if x.kind == "str"]
                others = [x for x in inner if x.is_code and x.kind not in ("str",)
                          and x.text not in (",",)]
                if len(strs) >= 4 and len(others) <= 2:
                    tests = [c for c in statement_calls(toks, fn.a, fn.b)
                             if c in ("contains", "indexOf", "count", "find")]
                    if tests:
                        out.append(Finding(
                            "frozen_list_rule", u.rel, t.line, fn.qual,
                            f"a list of {len(strs)} names is used as a membership rule",
                            why=("A list decides the answer for the names that were "
                                 "known when it was written, and gives the wrong "
                                 "answer for anything added later - silently, because "
                                 "nothing checks that the list is still complete."),
                            evidence=f"L{t.line}: {_line(u, t.line)}",
                            severity="medium", confidence="possible",
                            category="design",
                            suggestion=("Ask the object the question instead of "
                                        "looking the answer up, or derive the list "
                                        "from the same source the objects come from.")))
            j += 1


# --------------------------------------------------------------------------
describe(
    "filename_in_logic",
    "A guard that names a source file",
    "A condition that compares against a source file's name goes stale the "
    "first time that file is split or renamed, and goes stale silently: the "
    "comparison simply stops matching and the guarded behaviour quietly stops "
    "happening. This project has hit it - a guard naming a file that had "
    "since been divided into six.",
    "String literals ending in a source extension that appear as an operand "
    "of a comparison or a contains/startsWith/endsWith call.",
    "Diagnostics, log messages and error text that legitimately name a file.")

# At least one name character before the dot, so that a bare extension
# test - `f.endsWith(".cpp")` - is not mistaken for a file name.
FILE_LITERAL = re.compile(r'"[^"]*[A-Za-z0-9_]\.(cpp|h|hpp|qml|py|rs|json)"$')


def filename_in_logic(p, out: list) -> None:
    for u, fn, m in p.bodies():
        toks = u.toks
        for i in range(fn.a, min(fn.b, len(toks))):
            t = toks[i]
            if t.kind != "str" or not FILE_LITERAL.match(t.text):
                continue
            prev = toks[i - 1].text if i else ""
            nxt = toks[i + 1].text if i + 1 < len(toks) else ""
            comparison = prev in ("==", "!=") or nxt in ("==", "!=")
            called = False
            if prev == "(":
                k = i - 2
                while k >= 0 and not toks[k].is_code:
                    k -= 1
                if k >= 0 and toks[k].text in ("contains", "startsWith",
                                               "endsWith", "compare", "indexOf"):
                    called = True
            if comparison or called:
                out.append(Finding(
                    "filename_in_logic", u.rel, t.line, fn.qual,
                    f"a decision is made by comparing against the file name {t.text}",
                    why=("The comparison stops matching the day the file is renamed "
                         "or split, and nothing reports that it has stopped."),
                    evidence=f"L{t.line}: {_line(u, t.line)}",
                    severity="medium", confidence="likely", category="design",
                    suggestion="Test the property you care about, not the file it lives in."))


# --------------------------------------------------------------------------
describe(
    "two_answers",
    "Two independent answers to one question",
    "When one property is computed in two places, the two drift apart and "
    "the program believes both. This project has hit it repeatedly - most "
    "recently a window title reading one notion of the project name while "
    "the project itself held another.",
    "Within one class, parameterless const methods with the same return type "
    "that read exactly the same set of member fields but have different "
    "bodies.",
    "Deliberate pairs - a value and its formatted form, a getter and a "
    "cached variant - legitimately share inputs.")


def two_answers(p, out: list) -> None:
    for td in p.types:
        if len(td.methods) < 2:
            continue
        field_names = {d.name for d in td.fields}
        if not field_names:
            continue
        groups: dict[tuple, list] = defaultdict(list)
        for fn in td.methods:
            if fn.body is None or fn.params or fn.is_ctor or fn.is_dtor:
                continue
            if "const" not in fn.quals:
                continue
            m = p.model(fn)
            if m is None:
                continue
            u = fn.unit
            reads = {n for n in all_names_in(u.toks, fn.body.a, fn.body.b)
                     if n in field_names}
            if len(reads) < 2:
                continue
            ret = fn.ret.replace("const", "").replace("&", "").strip()
            groups[(ret, tuple(sorted(reads)))].append(fn)
        for (ret, reads), fns in groups.items():
            if len(fns) < 2:
                continue
            bodies = {normalise(f.unit.toks, f.body.a, f.body.b) for f in fns}
            if len(bodies) < 2:
                continue
            names = ", ".join(f.name for f in fns)
            f0 = fns[0]
            out.append(Finding(
                "two_answers", f0.file, f0.line, td.name,
                f"{len(fns)} methods compute a {ret} from the same fields: {names}",
                why=("They read the same state and return the same type by "
                     "different routes. Two answers to one question stay equal "
                     "only by coincidence, and nothing here checks that they do."),
                evidence="reads: " + ", ".join(reads) + "\n" +
                         "\n".join(f"L{f.line}: {f.name}()" for f in fns),
                severity="medium", confidence="possible", category="design",
                suggestion="Have one compute the answer and the other call it.",
                related=[f"{f.file}:{f.line}" for f in fns]))


# --------------------------------------------------------------------------
describe(
    "unused_local",
    "A local that is never read",
    "Dead weight, and sometimes a symptom: a value computed and then not "
    "used is often a line that was meant to be used and is not.",
    "A declared local with no reads, no writes and no address taken, whose "
    "type is primitive - so construction and destruction cannot have effects "
    "- and whose name is not declared more than once in the function.",
    "None known: class-typed locals (RAII guards) and names declared in "
    "several #ifdef arms are deliberately excluded.")


def unused_local(p, out: list) -> None:
    for u, fn, m in p.bodies():
        counts = Counter(d.name for d in m.all_decls)
        for d in m.all_decls:
            if d.kind != "var" or d.reads or d.writes or d.escapes:
                continue
            if counts[d.name] > 1:
                continue        # an #ifdef variant, or a reused name
            type_txt = _seg(u, d.type_a, d.type_b) if d.type_a != -1 else ""
            words = set(re.findall(r"[A-Za-z_]\w*", type_txt))
            if not words or not words <= PRIMITIVE:
                continue        # a class type: the constructor may be the point
            out.append(Finding(
                "unused_local", u.rel, d.line, fn.qual,
                f"`{d.name}` is declared and never read",
                why="Nothing reads it, writes it, or takes its address.",
                evidence=f"L{d.line}: {_line(u, d.line)}",
                severity="low", confidence="likely", category="dead-code",
                suggestion="Remove it, or use it."))


# --------------------------------------------------------------------------
describe(
    "unreachable_code",
    "A statement control can never arrive at",
    "Code after an unconditional return, break, continue or throw never "
    "runs. If it looks important, something is wrong above it.",
    "Statements following a statement from which control cannot fall through, "
    "within the same block. Labels clear the state, so switch arms are not "
    "reported, and a preprocessor conditional between the two suppresses the "
    "finding because only one arm compiles.",
    "None known.")


def unreachable_code(p, out: list) -> None:
    for u, fn, m in p.bodies():
        for st in m.unreachable:
            out.append(Finding(
                "unreachable_code", u.rel, st.line, fn.qual,
                "this statement cannot be reached",
                why="Control leaves the block before this line, on every path.",
                evidence=f"L{st.line}: {_line(u, st.line)}",
                severity="medium", confidence="certain", category="dead-code",
                suggestion="Delete it, or fix the control flow above it."))


# --------------------------------------------------------------------------
describe(
    "switch_fallthrough",
    "A switch arm that falls into the next",
    "Almost always a missing break. When deliberate it is conventionally "
    "marked, so an unmarked fallthrough with real work in it is a good bet.",
    "A `case` label whose statements neither terminate nor are empty, "
    "followed by another `case`, with no fallthrough comment or attribute "
    "on the intervening lines.",
    "Deliberate fallthrough marked in a way this does not recognise.")

FALLTHROUGH_NOTE = re.compile(r"fall[\s_-]?(s|ing)?[\s_-]?thr(ough|u)|\[\[fallthrough\]\]",
                              re.I)


def switch_fallthrough(p, out: list) -> None:
    for u, fn, m in p.bodies():
        for st in fn.body.walk():
            if st.kind != "switch" or not isinstance(st.body, Block):
                continue
            stmts = st.body.stmts
            run: list = []
            label_line = 0
            for s in stmts:
                if s.kind == "label":
                    if run and not any(terminates(x) for x in run):
                        seg = u.src.split("\n")[max(0, run[-1].end_line - 1):s.line]
                        if not FALLTHROUGH_NOTE.search("\n".join(seg)):
                            out.append(Finding(
                                "switch_fallthrough", u.rel, run[-1].end_line, fn.qual,
                                f"the arm ending on line {run[-1].end_line} falls into the next case",
                                why=("No break, return, continue or throw, and no "
                                     "comment or [[fallthrough]] saying it is deliberate."),
                                evidence=f"L{label_line}: {_line(u, label_line)}\n"
                                         f"L{run[-1].end_line}: {_line(u, run[-1].end_line)}\n"
                                         f"L{s.line}: {_line(u, s.line)}",
                                severity="high", confidence="likely",
                                category="correctness",
                                suggestion="Add `break;`, or mark the fallthrough."))
                    run = []
                    label_line = s.line
                elif s.kind != "empty":
                    run.append(s)


# --------------------------------------------------------------------------
describe(
    "identical_branches",
    "Both arms of a branch do the same thing",
    "Either the condition is pointless or one arm was meant to differ and "
    "does not - usually a copy-paste that was never finished.",
    "An if/else whose two bodies have identical token sequences.",
    "None known.")


def identical_branches(p, out: list) -> None:
    for u, fn, m in p.bodies():
        for st in fn.body.walk():
            if st.kind != "if" or st.alt is None or st.body is None:
                continue
            a = st.body
            b = st.alt
            ta = normalise(u.toks, a.a, a.b) if isinstance(a, Block) else normalise(u.toks, a.a, a.b)
            tb = normalise(u.toks, b.a, b.b) if isinstance(b, Block) else normalise(u.toks, b.a, b.b)
            if ta and ta == tb:
                out.append(Finding(
                    "identical_branches", u.rel, st.line, fn.qual,
                    "the `if` and `else` bodies are identical",
                    why="Whatever the condition decides, the same thing happens.",
                    evidence=f"L{st.line}: {_line(u, st.line)}\n{ta[:140]}",
                    severity="high", confidence="certain", category="correctness",
                    suggestion="One arm was meant to differ, or the branch is unnecessary."))


# --------------------------------------------------------------------------
describe(
    "duplicate_condition",
    "A condition repeated in one else-if chain",
    "The second arm can never run: the first one with the same condition "
    "always wins.",
    "Textually identical conditions at two positions of a single if/else-if "
    "chain.",
    "Conditions that read the same but depend on state changed by an earlier "
    "arm - possible, but then the chain is hard to read anyway.")


def duplicate_condition(p, out: list) -> None:
    for u, fn, m in p.bodies():
        for st in fn.body.walk():
            if st.kind != "if":
                continue
            chain = []
            cur = st
            while cur is not None and cur.kind == "if" and cur.cond:
                chain.append((normalise(u.toks, *cur.cond), cur.line))
                nxt = cur.alt
                cur = nxt if isinstance(nxt, Stmt) and nxt.kind == "if" else None
            if len(chain) < 2:
                continue
            seen: dict[str, int] = {}
            for text, ln in chain:
                if not text:
                    continue
                if text in seen:
                    out.append(Finding(
                        "duplicate_condition", u.rel, ln, fn.qual,
                        f"this condition already appears on line {seen[text]} of the same chain",
                        why="The earlier arm always wins, so this one is dead.",
                        evidence=f"L{seen[text]}: {_line(u, seen[text])}\nL{ln}: {_line(u, ln)}",
                        severity="high", confidence="certain", category="correctness",
                        suggestion="One of the two was meant to test something else."))
                else:
                    seen[text] = ln


# --------------------------------------------------------------------------
describe(
    "repeated_subcondition",
    "The same test twice in one condition",
    "`a && a` and `a || a` are always a typo for a second, different test.",
    "Identical operands either side of && or || within one condition, "
    "compared as token sequences at the top level of the expression.",
    "None known.")


def repeated_subcondition(p, out: list) -> None:
    for u, fn, m in p.bodies():
        for st in fn.body.walk():
            if not st.cond:
                continue
            a, b = st.cond
            for op in ("&&", "||"):
                parts = split_top_level(u.toks, a, b, op, angles=False)
                if len(parts) < 2:
                    continue
                texts = [normalise(u.toks, s, e).strip() for s, e in parts]
                dup = [x for x, c in Counter(t for t in texts if t).items() if c > 1]
                if dup:
                    out.append(Finding(
                        "repeated_subcondition", u.rel, st.line, fn.qual,
                        f"`{dup[0][:60]}` appears twice in one condition",
                        why="The second test can never change the result.",
                        evidence=f"L{st.line}: {_line(u, st.line)}",
                        severity="high", confidence="certain", category="correctness",
                        suggestion="One of the two was meant to test something else."))
                    break


# --------------------------------------------------------------------------
describe(
    "self_comparison",
    "An expression compared with itself",
    "`a == a` and `a - a` are constants. Either a typo for a nearby name or "
    "left over from an edit.",
    "Identical token sequences either side of <, >, <= or >= within one "
    "statement. Equality is deliberately excluded: `x == x` and `x != x` are "
    "the standard idiom for testing whether a float is NaN, and reporting "
    "them produced 14 findings here, all of them that idiom.",
    "None known, now that equality is excluded.")


def self_comparison(p, out: list) -> None:
    for u, fn, m in p.bodies():
        toks = u.toks
        for st in fn.body.walk():
            for i in range(st.a, min(st.b, len(toks))):
                if toks[i].kind != "punct" or toks[i].text not in ("<", ">", "<=", ">="):
                    continue
                left_end = i
                k = i - 1
                depth = 0
                while k >= st.a:
                    tx = toks[k]
                    if tx.kind == "punct":
                        if tx.text in ")]}":
                            depth += 1
                        elif tx.text in "([{":
                            if depth == 0:
                                break
                            depth -= 1
                        elif depth == 0 and tx.text in ("&&", "||", ",", ";", "=",
                                                        "?", ":", "+", "-", "*", "/"):
                            break
                    k -= 1
                left = normalise(toks, k + 1, left_end).strip()
                j = i + 1
                depth = 0
                while j < min(st.b, len(toks)):
                    tx = toks[j]
                    if tx.kind == "punct":
                        if tx.text in "([{":
                            depth += 1
                        elif tx.text in ")]}":
                            if depth == 0:
                                break
                            depth -= 1
                        elif depth == 0 and tx.text in ("&&", "||", ",", ";",
                                                        "?", ":", "+", "-", "*", "/"):
                            break
                    j += 1
                right = normalise(toks, i + 1, j).strip()
                if left and left == right and not left.replace(".", "").isdigit():
                    out.append(Finding(
                        "self_comparison", u.rel, toks[i].line, fn.qual,
                        f"`{left[:40]}` is compared with itself",
                        why="The result is a constant, whatever the value is.",
                        evidence=f"L{toks[i].line}: {_line(u, toks[i].line)}",
                        severity="high", confidence="likely", category="correctness",
                        suggestion="One side was probably meant to be a different name."))
                    break


# --------------------------------------------------------------------------
describe(
    "assert_with_work",
    "An assertion that does the work",
    "Q_ASSERT and assert compile out of release builds. An assertion whose "
    "expression has an effect behaves one way in a debug build and another "
    "in the build the user runs - the hardest class of bug to reproduce.",
    "A call to Q_ASSERT / Q_ASSERT_X / assert whose argument contains an "
    "assignment, an increment, or a call that is not a known pure accessor.",
    "Calls that are pure but not in this module's list of pure names.")


def assert_with_work(p, out: list) -> None:
    for u, fn, m in p.bodies():
        toks = u.toks
        for name, i, _recv in m.calls:
            if name not in ("Q_ASSERT", "Q_ASSERT_X", "assert", "Q_CHECK_PTR"):
                continue
            k = i + 1
            while k < fn.b and not toks[k].is_code:
                k += 1
            if k >= fn.b or toks[k].text != "(":
                continue
            e = matching(toks, k)
            bad = []
            for j in range(k + 1, e):
                t = toks[j]
                if t.kind == "punct" and (t.text == "=" or t.text in ("++", "--")):
                    nxt = toks[j + 1].text if j + 1 < e else ""
                    if t.text == "=" and nxt == "=":
                        continue
                    bad.append(t.text)
                elif t.kind == "id":
                    nx = j + 1
                    while nx < e and not toks[nx].is_code:
                        nx += 1
                    if nx < e and toks[nx].text == "(" and t.text not in CHEAP_CALLS:
                        bad.append(t.text + "()")
            if bad:
                out.append(Finding(
                    "assert_with_work", u.rel, toks[i].line, fn.qual,
                    f"{name} contains work that disappears in release: {bad[0]}",
                    why=("The macro compiles to nothing in a release build, so "
                         "whatever it does here happens only in debug builds."),
                    evidence=f"L{toks[i].line}: {_line(u, toks[i].line)}",
                    severity="high", confidence="likely", category="correctness",
                    suggestion="Do the work above the assertion and assert on the result."))


# --------------------------------------------------------------------------
describe(
    "connect_without_context",
    "A connect to a lambda with no context object",
    "A three-argument connect with a lambda has no receiver, so the "
    "connection outlives the objects the lambda captured. It fires after "
    "they are destroyed.",
    "Calls named `connect` whose last argument begins with a lambda "
    "introducer and which have exactly three arguments.",
    "A lambda that captures nothing with a lifetime shorter than the sender.")


def connect_without_context(p, out: list) -> None:
    for u, fn, m in p.bodies():
        toks = u.toks
        for name, i, _recv in m.calls:
            if name != "connect":
                continue
            k = i + 1
            while k < fn.b and not toks[k].is_code:
                k += 1
            if k >= fn.b or toks[k].text != "(":
                continue
            e = matching(toks, k)
            args = split_top_level(toks, k + 1, e, ",", angles=True)
            if len(args) != 3:
                continue
            s, en = args[-1]
            while s < en and not toks[s].is_code:
                s += 1
            if s < en and toks[s].text == "[":
                caps = normalise(toks, s, matching(toks, s) + 1)
                if caps.strip() in ("[ ]", "[]"):
                    continue
                out.append(Finding(
                    "connect_without_context", u.rel, toks[i].line, fn.qual,
                    "connect to a capturing lambda with no context object",
                    why=("With no receiver the connection is not broken when the "
                         "captured objects die, and the lambda runs against "
                         "destroyed state."),
                    evidence=f"L{toks[i].line}: {_line(u, toks[i].line)}",
                    severity="high", confidence="likely", category="correctness",
                    suggestion="Pass `this` (or the captured object) as the third argument."))


# --------------------------------------------------------------------------
describe(
    "expensive_call_in_loop_condition",
    "A call re-evaluated on every iteration of a loop",
    "A loop condition runs once per iteration. A cheap accessor there is "
    "free; anything else is the loop body's cost multiplied by its length.",
    "A call in a `for` or `while` condition whose name is not a known cheap "
    "accessor or literal constructor, where NOTHING it depends on changes per "
    "iteration: not its receiver, not its arguments, not the loop variable, "
    "and not anything the increment or body writes.",
    "Calls that are cheap but not in this module's list. Deliberately "
    "excluded: calls whose answer changes on its own - a timer's elapsed(), a "
    "stream's bytesAvailable() - because hoisting those does not speed the "
    "loop up, it stops it terminating.")



def _mutated_in(toks, name: str, a: int, b: int) -> bool:
    """Does the span change `name`, or call a non-const method on it?"""
    from .cppflow import ASSIGN_OPS
    for j in range(max(0, a), min(b, len(toks))):
        t = toks[j]
        if t.kind != "id" or t.text != name:
            continue
        nx = j + 1
        while nx < b and not toks[nx].is_code:
            nx += 1
        # PREFIX increment too. Looking only at the token after the name saw
        # `j++` and missed `++j`, so every loop whose body advances its own
        # index with a prefix operator looked as though nothing changed.
        pv = j - 1
        while pv >= a and not toks[pv].is_code:
            pv -= 1
        if pv >= a and toks[pv].text in ("++", "--"):
            return True
        if nx >= b:
            continue
        if toks[nx].text in ASSIGN_OPS or toks[nx].text in ("++", "--"):
            return True
        if toks[nx].text in (".", "->"):
            mm = nx + 1
            while mm < b and not toks[mm].is_code:
                mm += 1
            if mm < b and toks[mm].text not in CONST_METHODS:
                return True
    return False


def _args_mutated(toks, name: str, ca: int, cb: int, ba: int, bb: int) -> bool:
    for j in range(ca, cb):
        if toks[j].kind != "id" or toks[j].text != name:
            continue
        k = j + 1
        while k < cb and not toks[k].is_code:
            k += 1
        if k >= cb or toks[k].text != "(":
            continue
        e = matching(toks, k)
        for arg in (toks[x] for x in range(k + 1, min(e, len(toks)))):
            if arg.kind == "id" and _mutated_in(toks, arg.text, ba, bb):
                return True
    return False


def expensive_call_in_loop_condition(p, out: list) -> None:
    for u, fn, m in p.bodies():
        toks = u.toks
        for st in fn.body.walk():
            if st.kind not in ("for", "while") or not st.cond:
                continue
            a, b = st.cond
            body_span = (st.body.a, st.body.b) if st.body is not None else None

            # Everything that is DIFFERENT on each pass: the loop variable,
            # anything the increment changes, anything the body changes. A
            # call taking one of these has to be re-evaluated - that is the
            # loop working, not repeated work. Without the loop variable in
            # this set, every `finite(v)` and `cross(p)` in a condition was
            # reported, because the increment is not the body.
            changing = {d.name for d in st.decls}
            if st.incr:
                for j in range(*st.incr):
                    if toks[j].kind == "id":
                        changing.add(toks[j].text)
            if body_span:
                for j in range(*body_span):
                    t = toks[j]
                    if t.kind == "id" and _mutated_in(toks, t.text, *body_span):
                        changing.add(t.text)

            for c in statement_calls(toks, a, b):
                if c in CHEAP_CALLS or c in SELF_CHANGING or \
                        c.startswith("q") or c in QT_LITERALS or len(c) <= 3:
                    continue
                recv = ""
                args: set = set()
                for j in range(a, b):
                    if toks[j].kind == "id" and toks[j].text == c and j >= 2:
                        if toks[j - 1].text in (".", "->") and toks[j - 2].kind == "id":
                            recv = toks[j - 2].text
                        k = j + 1
                        while k < b and not toks[k].is_code:
                            k += 1
                        if k < b and toks[k].text == "(":
                            e = matching(toks, k)
                            args = {toks[x].text for x in range(k + 1, min(e, len(toks)))
                                    if toks[x].kind == "id"}
                        break
                if recv and recv in changing:
                    continue
                if args & changing:
                    continue
                # A call on something the body changes MUST be re-evaluated:
                # `while (!in.atEnd())` over a stream the body reads from is
                # the loop working correctly, not repeated work.
                if recv and body_span and _mutated_in(toks, recv, *body_span):
                    continue
                if body_span and _args_mutated(toks, c, a, b, *body_span):
                    continue
                out.append(Finding(
                    "expensive_call_in_loop_condition", u.rel, st.line, fn.qual,
                    f"`{c}()` is called on every iteration of this loop",
                    why=("The condition is evaluated once per pass, and nothing "
                         "it depends on changes inside the loop, so the answer "
                         "is the same every time."),
                    evidence=f"L{st.line}: {_line(u, st.line)}",
                    severity="medium", confidence="possible", category="performance",
                    suggestion="Hoist the value into a local above the loop."))
                break


# --------------------------------------------------------------------------
# growth_without_reserve was written here and deleted after measurement.
# It reported 1,946 findings: every `append` inside every loop in the tree.
# The rationale claimed it only fired when the trip count was known before
# the loop - and the code never checked that, so it fired on `field.append(c)`
# building a string character by character. A check whose rationale describes
# a judgement its code does not make is worse than no check, because the
# rationale is what persuades the reader to believe it. Qt's containers
# amortise growth anyway, so the honest value here was near zero.
#   The rule this keeps proving: flag a problem, never a pattern.

# --------------------------------------------------------------------------
describe(
    "copy_per_iteration",
    "A range-for that copies each element",
    "`for (auto x : c)` copies every element. For anything larger than a "
    "pointer that is a per-iteration cost with no benefit, and the fix is one "
    "character.",
    "A range-for declaring a non-reference loop variable that the body never "
    "assigns to, where the body uses the variable as an object (member "
    "access or passing it on) rather than as a number.",
    "Element types that really are cheap to copy but are not spelled as "
    "primitives.")


def copy_per_iteration(p, out: list) -> None:
    for u, fn, m in p.bodies():
        toks = u.toks
        for st in fn.body.walk():
            if st.kind != "range-for" or not st.init or not isinstance(st.body, Block):
                continue
            a, b = st.init
            decl_text = _seg(u, a, b)
            if "&" in decl_text or "*" in decl_text:
                continue        # a reference already, or a pointer: no copy
            if not st.decls:
                continue
            d = st.decls[0]
            words = set(re.findall(r"[A-Za-z_]\w*", decl_text)) - {d.name}
            if words & PRIMITIVE:
                continue
            uses_as_object = False
            for j in range(st.body.a, min(st.body.b, len(toks))):
                if toks[j].kind == "id" and toks[j].text == d.name:
                    nx = j + 1
                    while nx < st.body.b and not toks[nx].is_code:
                        nx += 1
                    if nx < st.body.b and toks[nx].text in (".", "->"):
                        uses_as_object = True
                    if nx < st.body.b and toks[nx].text == "=":
                        uses_as_object = False
                        break
            if not uses_as_object:
                continue
            out.append(Finding(
                "copy_per_iteration", u.rel, st.line, fn.qual,
                f"`{d.name}` is copied once per iteration",
                why="The body only reads it, so a const reference does the same work "
                    "without the copy.",
                evidence=f"L{st.line}: {_line(u, st.line)}",
                severity="low", confidence="likely", category="performance",
                suggestion=f"`const auto& {d.name}`"))


# --------------------------------------------------------------------------
describe(
    "swallowed_exception",
    "A catch that does nothing",
    "An empty catch turns a failure into a wrong answer with no trace. The "
    "program carries on as if the work succeeded.",
    "A catch clause whose body contains no statements, or only a comment.",
    "Catches that are deliberately empty because the failure is genuinely "
    "expected - these should say so in a comment, which this does not "
    "currently read.")


def swallowed_exception(p, out: list) -> None:
    for u, fn, m in p.bodies():
        for st in fn.body.walk():
            if st.kind != "try":
                continue
            for c in st.parts:
                body = c.body
                if isinstance(body, Block) and not body.stmts:
                    out.append(Finding(
                        "swallowed_exception", u.rel, c.line, fn.qual,
                        "this catch discards the exception silently",
                        why="The failure leaves no trace and the caller sees success.",
                        evidence=f"L{c.line}: {_line(u, c.line)}",
                        severity="medium", confidence="certain", category="correctness",
                        suggestion="Log it, or say in a comment why it is safe to ignore."))


# --------------------------------------------------------------------------
describe(
    "oversized_function",
    "A function too large to hold in your head",
    "Length alone is weak evidence; length together with branching and "
    "nesting is the shape of a function that is hard to change without "
    "breaking something.",
    "Functions over 220 lines, or with cyclomatic complexity over 60, or "
    "nested more than 6 deep. Reported with all three numbers so the reader "
    "can judge which is the problem.",
    "Dispatch tables and long switch statements are long but simple; their "
    "complexity number is high and their nesting is not. A function with "
    "complexity 3 or less and no nesting is a literal data table and is "
    "deliberately not reported however long it is. Nor is a DISPATCH CHAIN - "
    "a long run of independent branches that each answer and return - or a "
    "single expression built from a long chain of comparisons: see "
    "`_is_dispatch_chain` for why those two are the right shape rather than a "
    "problem.")


def _is_dispatch_chain(fn) -> bool:
    """Is this function a flat run of independent branches that each return?

    LONG IS THE CORRECT SHAPE FOR A DISPATCH TABLE, and reporting one as
    oversized is the check flagging a pattern rather than a problem - which
    this audit's first rule forbids, because a check that names six functions
    nobody will ever act on is a check people learn to scroll past.

    The six `prepareEngineGroupN` functions are the case in hand: about 3,300
    lines and complexity 700 each, and every one of them is

        if(in.engine==QLatin1String("X")){ ...; return out; }
        if(in.engine==QLatin1String("Y")){ ...; return out; }

    sixty times over. Splitting one buys nothing - the same number of branches
    in more files - and they were ALREADY split once, from a 1.78 MB
    translation unit into six. Nothing about the next split would make any
    branch easier to read, because no branch is hard to read; there are simply
    a lot of them.

    Two shapes count, because the codebase has both:

      * a run of top-level `if`s whose bodies terminate. Measured on the
        statements at the function's own level, so a genuinely tangled function
        that happens to open with three guard clauses is not excused - the
        branches have to be most of what the function IS.
      * a function that is essentially one `return` of a long boolean chain.
        `engineHasAxes` is 118 lines and complexity 67 and is a list of sixty-six
        names joined by `&&`. Its nesting is zero, which is the giveaway: there
        is nothing to hold in your head, only something to scroll.

    A function that is neither is still reported, however long.
    """
    stmts = [s for s in fn.body.stmts] if fn.body is not None else []
    if not stmts:
        return False

    # Shape two: one expression, no nesting. Two statements allowed, because a
    # table often opens with a local alias or a short-circuit guard.
    if len(stmts) <= 2 and max_depth(fn.body) == 0:
        return True

    # Shape one: independent branches that each answer.
    branches = [s for s in stmts if s.kind == "if" and terminates(s.body)]
    if len(branches) < 10:
        return False
    # ...and they have to be the bulk of the function, not a preamble to it.
    return len(branches) >= 0.6 * len(stmts)


def oversized_function(p, out: list) -> None:
    for u, fn, m in p.bodies():
        n = fn.lines
        cx = complexity(fn)
        depth = max_depth(fn.body)
        if n < 220 and cx < 60 and depth < 7:
            continue
        if cx <= 3 and depth <= 1:
            continue        # a data table: long, but nothing to hold in your head
        if _is_dispatch_chain(fn):
            continue
        worst = "length" if n >= 220 else ("branching" if cx >= 60 else "nesting")
        out.append(Finding(
            "oversized_function", u.rel, fn.line, fn.qual,
            f"{n} lines, complexity {cx}, nesting {depth}",
            why=(f"Mostly {worst}. Complexity counts the independent paths through "
                 "the function; nesting is how deep the deepest block sits."),
            evidence=f"L{fn.line}: {_line(u, fn.line)}",
            severity="low" if n < 400 else "medium", confidence="certain",
            category="maintainability",
            suggestion="Split out the part that can be named."))


# --------------------------------------------------------------------------
describe(
    "mutable_static",
    "Mutable state shared across every call",
    "A non-const static is shared by every caller and every thread. In a "
    "render path that is a data race waiting for a second thread.",
    "Function-local and file-scope statics whose declaration has no const "
    "and no constexpr.",
    "Deliberate caches and singletons where the lock is not visible from the "
    "declaration. A static whose function or whose own type mentions a mutex, "
    "lock or atomic is NOT reported: the prompt was to check the locking, and "
    "if it is visibly there the prompt is noise. Nor is one whose immediately "
    "preceding comment is about threading - a declaration that has written "
    "down which thread touches it and when has answered the question this "
    "check asks, and reporting it anyway is how a low-severity check becomes "
    "wallpaper. `thread_local` is excluded too - it is per-thread, which is "
    "the opposite of shared.")


def mutable_static(p, out: list) -> None:
    for u in p.units:
        toks = u.toks
        for i, t in enumerate(toks):
            if t.kind != "kw" or t.text != "static" or not t.is_code:
                continue
            j = i + 1
            words = []
            while j < len(toks) and toks[j].text not in (";", "{", "("):
                if toks[j].is_code:
                    words.append(toks[j].text)
                j += 1
            if not words or "const" in words or "constexpr" in words:
                continue
            # `static thread_local int depth` is one per THREAD, which is the
            # opposite of shared - reporting it said the reverse of the truth.
            if "thread_local" in words:
                continue
            if j < len(toks) and toks[j].text == "(":
                continue        # a static function

            # The declared NAME, not the last token before the semicolon:
            # `static bool gOpenGL=false;` is named gOpenGL, and taking the
            # last token reported a variable called `false`.
            eq = words.index("=") if "=" in words else len(words)
            head = [w for w in words[:eq] if w not in ("*", "&", "[", "]")]
            name = head[-1] if head else "?"

            fn = next((f for f in u.funcs if f.line <= t.line <= f.end_line), None)
            # Guarded state is the normal, correct way to have a process-wide
            # cache. The rationale always said this was a prompt to check the
            # locking; if the locking is visibly there, there is nothing to
            # prompt about.
            if fn is not None and _is_guarded(u, fn, j):
                continue
            # ...or the declaration already ANSWERS the question.
            #
            # The suggestion has always been "make it const, or say what guards
            # it", and a declaration that does say so was still being reported
            # - which punishes exactly the code that did the right thing, and
            # is how a low-severity check turns into wallpaper. Two statics in
            # this project carry a written argument for why they are safe, and
            # re-deriving that argument every pass is work the comment exists
            # to prevent. Same precedent as py_except_pass.
            #
            # It must be a comment about THREADING, not any comment at all: the
            # point is that the question was addressed, and "// the current
            # dataset" addresses nothing.
            if _explains_threading(u, i, fn):
                continue
            out.append(Finding(
                "mutable_static", u.rel, t.line, fn.qual if fn else "",
                f"`{name}` is mutable state shared by every call",
                why="Every caller and every thread sees the same object, no "
                    "mutex is visible near it, and nothing near the "
                    "declaration says why that is safe.",
                evidence=f"L{t.line}: {_line(u, t.line)}",
                severity="low", confidence="likely", category="concurrency",
                suggestion="Make it const, guard it, or write down which "
                           "thread touches it and when."))


LOCKS = ("QMutex", "QMutexLocker", "QReadWriteLock", "QReadLocker",
         "QWriteLocker", "std::mutex", "mutex", "lock_guard", "unique_lock",
         "scoped_lock", "atomic", "QAtomic")

# Words that mean the comment is about WHO TOUCHES THIS AND WHEN, which is the
# question mutable_static asks. A comment that says none of these has not
# answered it, whatever else it says.
THREAD_WORDS = ("thread", "mutex", "lock", "atomic", "race", "concurren",
                "reentran", "re-entran", "serialis", "seriali z", "main loop",
                "event loop", "before ", "single-threaded", "gui thread",
                "render thread")


def _comment_block_above(u, at: int) -> str:
    """The comment block immediately above token `at`, lowercased.

    Immediately above: comment tokens whose lines run up to it with no code
    token in between. A comment three declarations earlier is about something
    else, and treating it as cover would silence a whole file after one
    annotated declaration.
    """
    line = u.toks[at].line
    text = []
    k = at - 1
    while k >= 0:
        t = u.toks[k]
        if t.kind == "comment":
            # Contiguity is by LINE, not by token index: a blank line between
            # the comment and the declaration still reads as one block, but a
            # gap of several lines does not.
            if line - t.line > len(t.text.split("\n")) + 1:
                break
            text.append(t.text.lower())
            line = t.line
            k -= 1
            continue
        if t.is_code:
            break
        k -= 1
    return " ".join(text)


def _explains_threading(u, static_at: int, fn=None) -> bool:
    """Has the threading question been answered next to this declaration?

    Two places count, because there are two places people write it.
    A file-scope static is annotated above itself. A FUNCTION-LOCAL static -
    a Meyers singleton, most often - is annotated above the function, because
    `NativeApi& NativeApi::instance(){ static NativeApi api; return api; }` is
    one line and there is nowhere else for the comment to go. Looking only
    above the `static` token found nothing for those, which is the whole
    population of singletons.
    """
    if any(w in _comment_block_above(u, static_at) for w in THREAD_WORDS):
        return True
    if fn is None:
        return False
    # The function's own comment block, anchored at its first code token.
    anchor = next((k for k, t in enumerate(u.toks)
                   if t.is_code and t.line >= fn.line), None)
    if anchor is None or u.toks[anchor].line != fn.line:
        return False
    return any(w in _comment_block_above(u, anchor) for w in THREAD_WORDS)


def _raw(u, a: int, b: int) -> str:
    """Token text over a range, untruncated.

    `_seg` goes through text_of, which caps at 160 characters for evidence
    lines. Searching a function body with it silently looked at the first
    line and a half.
    """
    return " ".join(t.text for t in u.toks[max(0, a):min(b, len(u.toks))]
                    if t.is_code)


def _type_named(u, name: str):
    for td in u.types:
        if td.name == name:
            return td
    return None


def _is_guarded(u, fn, decl_end: int) -> bool:
    """Is there a lock in the function that owns this static, or in its type?"""
    if any(w in _raw(u, fn.a, fn.b) for w in LOCKS):
        return True
    for word in (x.text for x in u.toks[fn.a:decl_end] if x.is_code):
        td = _type_named(u, word)
        if td is not None and any(w in _raw(u, td.a, td.b) for w in LOCKS):
            return True
    return False


# --------------------------------------------------------------------------
describe(
    "disabled_block",
    "Code inside #if 0",
    "Code that cannot compile is code nobody is maintaining. It reads as if "
    "it works.",
    "Regions between `#if 0` and the matching `#endif`.",
    "None known.")


def disabled_block(p, out: list) -> None:
    for u in p.units:
        for start, end in u.pp_if_zero:
            out.append(Finding(
                "disabled_block", u.rel, start, "",
                f"{end - start} lines disabled with #if 0",
                why="It does not compile, so nothing keeps it correct.",
                evidence=f"L{start}: {_line(u, start)}",
                severity="low", confidence="certain", category="dead-code",
                suggestion="Delete it; git remembers."))


# --------------------------------------------------------------------------
describe(
    "leftover_marker",
    "A TODO, FIXME or HACK",
    "A note somebody left for themselves and did not come back to.",
    "Comment tokens containing TODO, FIXME, HACK, XXX, BUG, WORKAROUND or "
    "TEMPORARY as whole words.",
    "None - it is a literal search, reported for completeness.")


def leftover_marker(p, out: list) -> None:
    for u in p.units:
        for t in u.toks:
            if t.kind != "comment":
                continue
            mt = MARKERS.search(t.text)
            if not mt:
                continue
            fn = next((f for f in u.funcs if f.line <= t.line <= f.end_line), None)
            # The comment already contains the word, so prefixing it printed
            # "TODO: TODO: ...".
            body = t.text.strip("/* ").strip()
            out.append(Finding(
                "leftover_marker", u.rel, t.line, fn.qual if fn else "",
                body[:90] if body.upper().startswith(mt.group(1))
                else f"{mt.group(1)}: {body[:80]}",
                why="Left in the source as a note to come back to.",
                evidence=f"L{t.line}: {_line(u, t.line)}",
                severity="info", confidence="certain", category="housekeeping"))


# --------------------------------------------------------------------------
describe(
    "shadowed_declaration",
    "An inner name hiding an outer one",
    "Two different variables with one name in overlapping scopes. The reader "
    "has to track which is which, and an edit in the wrong place compiles.",
    "A declaration in a nested scope with the same name as one in an "
    "enclosing scope of the same function.",
    "Deliberate shadowing in short lambdas, which is idiomatic.")


def shadowed_declaration(p, out: list) -> None:
    for u, fn, m in p.bodies():
        seen: dict[str, list] = defaultdict(list)
        for d in m.all_decls:
            seen[d.name].append(d)
        for name, ds in seen.items():
            if len(ds) < 2:
                continue
            ds = sorted(ds, key=lambda d: d.tok)
            for outer, inner in zip(ds, ds[1:]):
                if u.pp_cond_lines and any(outer.line <= L <= inner.line
                                           for L in u.pp_cond_lines):
                    continue      # two #ifdef arms of one name, not shadowing
                if outer.scope_end > inner.tok and outer.kind in ("var", "param"):
                    out.append(Finding(
                        "shadowed_declaration", u.rel, inner.line, fn.qual,
                        f"`{name}` here hides the one declared on line {outer.line}",
                        why="Both are live at this point and only one is visible.",
                        evidence=f"L{outer.line}: {_line(u, outer.line)}\n"
                                 f"L{inner.line}: {_line(u, inner.line)}",
                        severity="low", confidence="likely",
                        category="maintainability",
                        suggestion="Rename the inner one."))
                    break


# --------------------------------------------------------------------------
describe(
    "dead_function",
    "A function nothing calls",
    "Code nobody runs is code nobody is testing, and it still has to be read "
    "and maintained.",
    "A function whose name appears exactly once across all C++ sources (its "
    "own definition) and never in any QML file, and which is not a Qt slot, "
    "signal, invokable, override, constructor, destructor, operator or main.",
    "Anything called through a macro, a template this does not instantiate, "
    "or the Rust FFI boundary. Virtual overrides are excluded by looking at "
    "every declaration of the name, because `override` is written on the "
    "header declaration and not on the definition.")

QT_CALLED = {"eventFilter", "paintEvent", "resizeEvent", "mousePressEvent",
             "mouseMoveEvent", "mouseReleaseEvent", "wheelEvent", "keyPressEvent",
             "keyReleaseEvent", "hoverMoveEvent", "hoverEnterEvent",
             "hoverLeaveEvent", "geometryChange", "updatePaintNode",
             "componentComplete", "classBegin", "timerEvent", "main",
             "qt_static_metacall", "createWindow"}


KEPT = re.compile(r"for the test|for tests|deliberately|survives|kept "
                  r"because|on purpose", re.I)


def _python_mentions(p, name: str) -> bool:
    words = getattr(p, "_py_words", None)
    if words is None:
        words = set()
        for mod in p.python:
            words.update(re.findall(r"[A-Za-z_]\w*", mod.src))
        try:
            p._py_words = words
        except AttributeError:
            # __slots__ object: no cache, so it is recomputed next time
            pass
    return name in words


def _kept_on_purpose(u, fn) -> bool:
    """A comment in the six lines above saying why this is still here."""
    for t in u.toks:
        if t.kind != "comment":
            continue
        if fn.line - 7 <= t.line < fn.line and KEPT.search(t.text):
            return True
    return False


def _property_accessors(p) -> set:
    """Every function named by a Q_PROPERTY as a READ, WRITE or NOTIFY.

    Qt calls these through the metaobject, so the name never appears at a
    call site. Without this, every property getter reads as dead code.
    """
    cached = getattr(p, "_prop_accessors", None)
    if cached is None:
        cached = set()
        for td in p.types:
            for prop in td.properties:
                for key in ("read", "write", "notify", "member"):
                    if prop.get(key):
                        cached.add(prop[key])
        try:
            p._prop_accessors = cached
        except AttributeError:
            # __slots__ object: no cache, so it is recomputed next time
            pass
    return cached


def dead_function(p, out: list) -> None:
    for u in p.units:
        for fn in u.funcs:
            if fn.body is None or fn.is_ctor or fn.is_dtor:
                continue
            if fn.name.startswith("operator") or fn.name.startswith("~"):
                continue
            if fn.name in QT_CALLED or fn.access in ("signals",) or \
                    "slots" in fn.access or "invokable" in fn.quals or \
                    "override" in fn.quals or "virtual" in fn.quals:
                continue
            # `override` is written on the DECLARATION in the header, not on
            # the out-of-line definition in the .cpp. Reading only this one
            # reported every virtual override as dead. Ask every declaration
            # of the name, which is the question actually being asked.
            if any(o.name == fn.name and
                   ({"override", "virtual", "invokable"} & o.quals or
                    o.access == "signals" or "slots" in o.access)
                   for o in p.funcs):
                continue
            if fn.is_template or fn.name.startswith("gv_"):
                continue
            if fn.name in _property_accessors(p):
                continue
            uses = p.cpp_tokens.get(fn.name, 0)
            decl_uses = sum(1 for other in p.funcs if other.name == fn.name)
            if uses > decl_uses:
                continue
            if p.qml_tokens.get(fn.name, 0):
                continue
            # A name the Python side mentions - the test suite included - is
            # referenced. `worstCollapse` is asserted about by name in
            # test_reachable.py and is deliberately kept as a measurement.
            if _python_mentions(p, fn.name):
                continue
            # A hook that exists FOR the tests says so above itself.
            # `fieldFor` is commented "The gridded field, for the tests", and
            # calling that dead code is calling the test harness dead.
            if _kept_on_purpose(u, fn):
                continue
            out.append(Finding(
                "dead_function", u.rel, fn.line, fn.qual,
                f"`{fn.name}` is defined and never called",
                why=(f"The name appears {uses} time(s) in the whole C++ tree and "
                     "not at all in QML, which accounts for its declaration and "
                     "definition only."),
                evidence=f"L{fn.line}: {_line(u, fn.line)}",
                severity="low", confidence="possible", category="dead-code",
                suggestion="Remove it, or wire it up."))


ALL = [
    guard_after_use,
    frozen_list_rule,
    filename_in_logic,
    two_answers,
    unused_local,
    unreachable_code,
    switch_fallthrough,
    identical_branches,
    duplicate_condition,
    repeated_subcondition,
    self_comparison,
    assert_with_work,
    connect_without_context,
    expensive_call_in_loop_condition,
    copy_per_iteration,
    swallowed_exception,
    oversized_function,
    mutable_static,
    disabled_block,
    leftover_marker,
    shadowed_declaration,
    dead_function,
]
