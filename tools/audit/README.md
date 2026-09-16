# The passive audit

A standing, parsing audit of the GraphVis tree. Static and read-only: it
compiles nothing, renders nothing, writes nothing outside `build-reports/`,
and needs no build — so it can run while a build runs.

```
python tools/passive_audit.py              # write build-reports/AUDIT.md + .json
python tools/passive_audit.py --print      # ...and print it
python tools/passive_audit.py --selftest   # prove every check works
python tools/passive_audit.py --list       # every check and what it looks for
python tools/passive_audit.py --explain guard_after_use
python tools/passive_audit.py --accept     # record current findings as known
python tools/passive_audit.py --watch      # a pass on every change
```

## If you are an agent

Read **`build-reports/AUDIT.json`**, not the Markdown. It carries:

- `findings[]` — each with a stable `id`, `file`, `line`, `symbol`, `what`,
  `why`, `evidence`, `suggestion`, `severity` and `confidence`.
- `checks{}` — for every check: why it matters, **the method it used**, and
  **the false positives it is known to produce**.
- `coverage` and `not_examined` — what was read, and what was not looked at
  at all.
- `self_test` — whether each check still catches its own fixture.

Two things to hold onto:

**`severity` and `confidence` are different questions.** Severity is how much
it would matter if the finding is real. Confidence is how likely it is to be
real. A `low`/`certain` finding is a fact about dead code; a
`high`/`possible` one is worth twenty minutes of reading and may be nothing.

**Every finding is a suspicion with its method attached, not a verdict.**
Before acting on one, read `checks[<check>].method` and
`checks[<check>].known_false_positives` and decide whether this instance is
one of them. That is why the method travels with the finding: an agent given
a verdict has to trust it, and an agent given a method can check it.

`id` is stable across edits that only move the code — it is a hash of the
check, file, symbol and message shape, with line numbers and digits removed —
so it can be used to track or suppress a finding across runs.

## How it is built

```
cpplex.py     exact C++ lexer: raw strings, digit separators, line splices,
              preprocessor directives. No dependencies.
cppparse.py   structure: every function, class, block, statement, scope,
              parameter and Q_PROPERTY. Total - unknown constructs degrade to
              an opaque statement rather than raising.
cppflow.py    resolution: which declaration a name refers to, whether each
              use reads or writes it, what is reachable, how complex.
qmlparse.py   QML object tree: objects, ids, declared properties with their
              TYPES, bindings, handlers, functions, signals.
pyparse.py    Python, via the standard library's own `ast`.

project.py    loads the tree once; every check shares the one parse.
model.py      what a Finding is, and the check registry.
checks_*.py   the checks themselves.
report.py     AUDIT.md for a person, AUDIT.json for a program.
selftest.py   a fixture each check must catch, and one it must leave alone.
```

There is deliberately no libclang. libclang needs the real include tree, the
real compile flags and a matching binary wheel on the machine that runs the
audit — which means it stops working exactly when you most want to know what
is wrong.

## The three rules for adding a check

**1. Flag a problem, never a pattern.** A check that reports a *shape* of code
rather than a defect gets disabled within a week, and takes the credibility of
the other forty with it. Several checks have been written and deleted here for
exactly this; the counts they produced are recorded where they stood, so the
next person does not rewrite them:

| deleted check | reported | why it was wrong |
|---|---|---|
| `growth_without_reserve` | 1,946 | flagged every `append` in every loop; the rationale claimed a trip-count test the code never made |
| `taking_the_first` | 149 | a judgement about intent, not a defect |
| `loops_without_reserve` | 401 | same as above, earlier |
| `delegates_without_model` | 72 | needed QML scope, matched a 60-line window — **now reinstated properly** as `qml_delegate_missing_required_model` |
| `undeclared_qml_ids` | 1,033 | needed QML scope — **now reinstated properly** as `qml_unresolved_id` |

**2. State the method.** Call `describe(id, title, rationale, method,
false_positives)` next to the check. If you cannot write down the method, the
check is not ready. If the method describes a judgement, the code must
actually make that judgement — `growth_without_reserve` was deleted precisely
because its rationale promised a test its code did not perform, and the
rationale is what persuades the reader to believe the finding.

**3. Test it both ways.** Add a positive fixture and a negative one to
`selftest.py`. The negative is the important half: a check that flags
everything passes the positive test perfectly. The self-test has already
caught three checks that could never fire and one that fired on clean code.

## What it cannot do

It runs nothing. It cannot tell you whether a figure renders, whether a button
does what it says, or how long anything takes. A clean report is not a working
program — it is the absence of the mistakes this can see.

Specifically not examined: template instantiation, overload resolution and
macro bodies; both arms of an `#ifdef` are parsed and flow checks stay silent
across them; Rust sources and the FFI boundary; generated output, vendored
headers and `native/target/`.
