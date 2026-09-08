"""List the engines drawn against a pair of ordinary axes.

tools/add_scale_variants.py refuses to run without this list, and rightly so: a
quantile axis on a pie chart is not a thing, and guessing which engines have
axes is exactly the sort of hand-maintained second opinion that drifts from the
code and then puts uncdrawable entries in the catalogue.

The list is read out of QtPlotBackend.cpp itself - from the supportedEngines()
table and the engineHasAxes() predicate - rather than kept as a file of its own.
Both are parsed strictly: this script fails if either has changed shape, because
a probe that silently falls back to a partial answer is worse than no probe.
engineHasAxes is a chain of

    return engine!=QLatin1String("Pie")
        && ...
        && !engine.startsWith(QLatin1String("3D "))

so the exclusions are exactly the string literals in it, plus the one prefix.

    python3 tools/probe_axis_engines.py [native/plot2d/src/QtPlotBackend.cpp] \
        > /tmp/axis-engines.txt
"""
import pathlib
import re
import sys

src_path = pathlib.Path(sys.argv[1] if len(sys.argv) > 1
                        else "native/plot2d/src/QtPlotBackend.cpp")
src = src_path.read_text(encoding="utf-8")


def body(signature):
    """The text of a function, from its opening brace to the matching one."""
    start = src.find(signature)
    if start < 0:
        raise SystemExit("probe: %r is not in %s - has it been renamed?"
                         % (signature, src_path))
    brace = src.index("{", start)
    depth = 0
    for i in range(brace, len(src)):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[brace:i + 1]
    raise SystemExit("probe: unbalanced braces after %r" % signature)


LITERAL = re.compile(r'Q(?:StringLiteral|Latin1String)\("((?:[^"\\]|\\.)*)"\)')


def unescape(s):
    return s.replace('\\"', '"').replace("\\\\", "\\")


engines = [unescape(m) for m in LITERAL.findall(body("QStringList QtPlotBackend::supportedEngines"))]
if len(engines) < 100:
    raise SystemExit("probe: supportedEngines yielded only %d names - "
                     "the table is not where it was" % len(engines))

axes_body = body("bool QtPlotBackend::engineHasAxes")
excluded = {unescape(m) for m in LITERAL.findall(axes_body)}
prefixes = tuple(unescape(m) for m in
                 re.findall(r'startsWith\(QLatin1String\("((?:[^"\\]|\\.)*)"\)\)', axes_body))
if not excluded:
    raise SystemExit("probe: engineHasAxes excludes nothing - it has changed shape")
# The prefix literals also appear in `excluded`; they are exclusions either way,
# so nothing needs removing from that set.

kept = [e for e in engines
        if e not in excluded and not any(e.startswith(p) for p in prefixes)]
seen = set()
for e in kept:
    if e in seen:
        continue
    seen.add(e)
    print(e)
