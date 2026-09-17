"""The C++ statistical primitives, compiled and checked against NumPy.

Every box plot, violin, histogram and percentile band in the renderer stands on
three small functions in QtPlotBackend.cpp: `quantileOf`, `meanOf` and
`stdevOf`. They decide the median and quartiles people quote off a box plot, the
IQR that sets Freedman-Diaconis bin widths, and the spread in Silverman's
bandwidth for every kernel density estimate. They are 20 lines with no Qt in
them beyond the container type.

Reading them is not verification. This extracts them from the source as they
actually are, compiles them behind a minimal container shim, runs them, and
compares against NumPy - an implementation written by other people for other
reasons, which is what makes the agreement worth something.

Extraction is by function name and brace matching rather than by line number, so
editing the file above them does not silently stop the test from checking
anything. If a function cannot be found, that is a failure, not a skip: a check
that quietly tests nothing is worse than no check.

The compiler is the one thing that may be absent, and only then does this skip.

Not covered here, and worth being straight about: the binning and bandwidth
RULES that sit on top of these. Freedman-Diaconis (width = 2 IQR / n^(1/3),
falling back to Sturges = ceil(log2 n + 1)) and Silverman
(h = 0.9 min(sd, IQR/1.349) n^(-1/5)) were checked by reading them against the
standard forms and both are right, but they are written inline inside painting
functions that need a live QPainter, so they cannot be lifted out this way.
Testing those needs the built binary.
"""
from __future__ import annotations

import re
import shutil
import subprocess
import textwrap
from pathlib import Path

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[3]
# Both halves of the backend that hold a primitive this file compiles.
#
# `quantileOf`, `meanOf` and `stdevOf` were anonymous-namespace functions
# inside QtPlotBackend.cpp until that file was split across seven translation
# units, and they now live in the shared header all seven see. `logTicks` is a
# member of QtPlotBackend and stayed put. Naming one file found three of the
# four and failed on whichever was in the other.
BACKEND = ROOT / "native" / "plot2d" / "src" / "QtPlotBackend.cpp"
BACKEND_SHARED = ROOT / "native" / "plot2d" / "src" / "QtPlotBackendShared.h"


def _backend_text() -> str:
    return "\n".join(p.read_text(encoding="utf-8", errors="ignore")
                      for p in (BACKEND_SHARED, BACKEND) if p.exists())

WANTED = ("quantileOf", "meanOf", "stdevOf")

SHIM = textwrap.dedent("""
    #include <vector>
    #include <cmath>
    #include <algorithm>
    #include <cstdio>

    // The only Qt the extracted functions touch is the container. Everything
    // the shim adds is a name QVector has and std::vector spells differently;
    // no arithmetic is reimplemented here, so nothing in the maths under test
    // comes from this file.
    template<class T> struct QVector : std::vector<T> {
        using std::vector<T>::vector;
        bool isEmpty() const { return this->empty(); }
        int  size()    const { return int(std::vector<T>::size()); }
        const T& first() const { return this->front(); }
        const T& last()  const { return this->back(); }
    };
""")

MAIN = textwrap.dedent("""
    int main(){
        QVector<double> a; for(int i=0;i<=10;++i) a.push_back(double(i));
        QVector<double> b{1,2,3,4};
        QVector<double> c{2,4,4,4,5,5,7,9};
        QVector<double> d{-3.5, 0.25, 0.25, 7.0, 11.75, 12.0, 100.0};
        printf("%.15g %.15g %.15g %.15g %.15g\\n",
               quantileOf(a,0.0),quantileOf(a,0.25),quantileOf(a,0.5),
               quantileOf(a,0.75),quantileOf(a,1.0));
        printf("%.15g %.15g %.15g\\n",
               quantileOf(b,0.25),quantileOf(b,0.5),quantileOf(b,0.75));
        printf("%.15g %.15g %.15g\\n",
               quantileOf(d,0.1),quantileOf(d,0.5),quantileOf(d,0.9));
        printf("%.15g %.15g\\n", meanOf(c), stdevOf(c));
        printf("%.15g %.15g\\n", meanOf(d), stdevOf(d));
        return 0;
    }
""")


def extract(source: str, name: str) -> str:
    """One free function, from its signature to its matching closing brace."""
    match = re.search(rf"^[A-Za-z_][\w:<>,\s\*&]*?\b{name}\s*\([^;{{]*\)\s*\{{",
                      source, re.M)
    assert match, (f"{name} is in neither {BACKEND_SHARED.name} nor "
                   f"{BACKEND.name} - has it been renamed or moved again?")
    depth, index = 0, match.start()
    while index < len(source):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():index + 1]
        index += 1
    raise AssertionError(f"{name} has no closing brace")


@pytest.mark.skipif(not BACKEND.exists(), reason="run from a source tree")
@pytest.mark.skipif(shutil.which("g++") is None, reason="no C++ compiler")
def test_cxx_quantile_mean_and_stdev_agree_with_numpy(tmp_path: Path) -> None:
    source = _backend_text()
    program = SHIM + "\n".join(extract(source, name) for name in WANTED) + MAIN

    cpp = tmp_path / "primitives.cpp"
    cpp.write_text(program, encoding="utf-8")
    binary = tmp_path / "primitives"
    build = subprocess.run(["g++", "-O2", "-o", str(binary), str(cpp)],
                           capture_output=True, text=True)
    assert build.returncode == 0, f"the extracted C++ did not compile:\n{build.stderr}"

    out = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
    lines = [[float(v) for v in line.split()] for line in out.stdout.strip().splitlines()]

    a = np.arange(0.0, 11.0)
    b = np.array([1.0, 2, 3, 4])
    c = np.array([2.0, 4, 4, 4, 5, 5, 7, 9])
    d = np.array([-3.5, 0.25, 0.25, 7.0, 11.75, 12.0, 100.0])

    # quantileOf interpolates at q*(n-1), which is NumPy's default "linear"
    # method (type 7 in the Hyndman-Fan numbering).
    assert lines[0] == pytest.approx(list(np.quantile(a, [0, .25, .5, .75, 1])), abs=1e-12)
    assert lines[1] == pytest.approx(list(np.quantile(b, [.25, .5, .75])), abs=1e-12)
    assert lines[2] == pytest.approx(list(np.quantile(d, [.1, .5, .9])), abs=1e-12)
    # stdevOf divides by n-1: the sample standard deviation.
    assert lines[3] == pytest.approx([c.mean(), c.std(ddof=1)], abs=1e-12)
    assert lines[4] == pytest.approx([d.mean(), d.std(ddof=1)], abs=1e-12)


@pytest.mark.skipif(not BACKEND.exists(), reason="run from a source tree")
def test_the_histogram_and_the_box_plot_use_the_same_quantile() -> None:
    """One quantile estimator, or the two disagree about the same column.

    Freedman-Diaconis takes the IQR to size the bins and the box plot draws the
    IQR as a box. An earlier version indexed `v[int(n*0.25)]` for the bin width
    while the box used the interpolated `quantileOf`, so a histogram and a box
    plot of one column were built on two different numbers. The comment
    recording that fix is still in the source; this makes it a test.
    """
    source = _backend_text()
    # Comments first. The comment recording the original fix quotes the bad
    # form as the thing it replaced - "quantileOf, not v[int(n*0.25)]" - so a
    # scan of the raw text finds the documentation and calls it the defect.
    code = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    code = re.sub(r"//[^\n]*", "", code)
    crude = re.findall(r"\[\s*int\s*\(\s*\w+(?:\.size\(\))?\s*\*\s*0\.(?:25|75)\s*\)\s*\]",
                       code)
    assert not crude, (
        "an uninterpolated quantile has come back: " + ", ".join(crude) +
        " - use quantileOf so every panel reports the same quartiles"
    )


@pytest.mark.skipif(not BACKEND.exists(), reason="run from a source tree")
def test_no_painter_invents_its_own_categorical_colours() -> None:
    """A `draw*` painter must take its colours from the figure's palette.

    PlotCanvas chooses a categorical palette measured through a dichromat
    projection and hands it to every series it builds. The engines that lay out
    a WHOLE rather than a set of series - chord, alluvial, circos, icicle, hive,
    mosaic, and the geology ternaries - never see a series, and each had grown
    its own `QColor::fromHsvF` hue wheel. The effect was that switching to the
    Deuteranopia palette changed the line charts and left those engines drawing
    exactly the rainbow the setting exists to avoid, with nothing on screen
    saying the setting had not applied.

    They now go through `categoryColour`, which reads `style.categoryPalette`
    and falls back to the original arithmetic when no palette is set. This keeps
    it that way: hue arithmetic inside a painter is the shape of the bug.

    `prepareSpecCore` is deliberately not covered. It builds PlotSeries rather
    than painting, and those colours are a separate question from this one.
    """
    source = _backend_text()
    code = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    code = re.sub(r"//[^\n]*", "", code)

    # The whole file, not only the painters. `prepareSpecCore` builds the
    # PlotSeries that several engines are drawn from, and eighteen of its
    # branches picked their own hues too - so a figure could be coloured past
    # the palette before any painter ran. Both halves now go through
    # categoryColour, which makes the invariant simple enough to state: the
    # single fallback inside that helper is the only place in the renderer that
    # may call QColor::fromHsvF.
    outside_helper = re.sub(
        r"QColor categoryColour\(.*?\n\}", "", code, flags=re.S)
    stray = outside_helper.count("QColor::fromHsvF") + outside_helper.count("QColor::fromHslF")
    assert stray == 0, (
        f"{stray} call(s) to QColor::fromHsvF outside categoryColour(). Hue "
        "arithmetic in a painter or a prepare branch cannot see the "
        "colour-vision setting.")

    offenders: list[str] = []
    for match in re.finditer(r"^\w[\w:<>,\s\*&]*\bQtPlotBackend::(draw\w+)\s*\([^;{]*\)"
                             r"\s*(?:const\s*)?\{", code, re.M):
        name = match.group(1)
        depth, index, body_start = 0, match.end() - 1, match.end() - 1
        while index < len(code):
            if code[index] == "{":
                depth += 1
            elif code[index] == "}":
                depth -= 1
                if depth == 0:
                    break
            index += 1
        body = code[body_start:index]
        hits = body.count("QColor::fromHsvF") + body.count("QColor::fromHslF")
        if hits:
            offenders.append(f"{name} ({hits})")

    assert not offenders, (
        "painters building their own hues instead of using categoryColour(), so "
        "the colour-vision setting does not reach them: " + ", ".join(offenders)
    )


LOG_SHIM = textwrap.dedent("""
    #include <QString>
    #include <QVector>
    #include <cmath>
    #include <limits>
    #include <cstdio>

    // Only the names logTicks reaches for. No tick arithmetic is written here:
    // formatTick decides decimals from the step it is handed, which is the
    // behaviour the fix depends on, so it is reproduced exactly as the real one
    // reads rather than simplified.
    struct AxisTick { double value=0.0; QString label; bool minor=false; };
    static bool gvFinite(double v){ return std::isfinite(v); }
    #define finite gvFinite
    static QString superscript(int e){ return QStringLiteral("^")+QString::number(e); }
    static QString formatTick(double v,double step){
        const int decimals = step<1.0 ? qMax(0,int(std::ceil(-std::log10(step)))) : 0;
        return QString::number(v,'f',decimals);
    }
    static QVector<AxisTick> linearTicks(double,double,int){ return {}; }
""")

LOG_MAIN = textwrap.dedent("""
    int main(){
        const double cases[][2]={
            {2.0,9.0},{20.0,90.0},{1.5,9.0},{0.02,0.09},{3.0,7.0},
            {1.0,1000.0},{0.5,50.0},{1e-3,1e3},
        };
        for(const auto& c:cases){
            const QVector<AxisTick> t=logTicks(std::log10(c[0]),std::log10(c[1]));
            int labelled=0;
            for(const AxisTick& a:t) if(!a.label.isEmpty()) ++labelled;
            printf("%g %g %d %d\\n",c[0],c[1],int(t.size()),labelled);
        }
        return 0;
    }
""")


@pytest.mark.skipif(not BACKEND.exists(), reason="run from a source tree")
@pytest.mark.skipif(shutil.which("g++") is None, reason="no C++ compiler")
def test_a_log_axis_always_carries_at_least_one_label(tmp_path: Path) -> None:
    """A logarithmic axis must put numbers on itself, at every range.

    `logTicks` puts major ticks on whole decades and unlabelled minors at
    2..9 inside each. Data spanning LESS than one decade without crossing a
    power of ten — 2 to 9, 20 to 90, 0.02 to 0.09 — therefore produced eight
    minor ticks and **not one label**. The figure drew, the axis line drew,
    the axis title drew, and nothing said what any position along it meant.

    This was found by looking at the engine gallery: `EIS: Bode` sets its own
    logarithmic frequency axis, and a sweep inside one decade came out with a
    bare axis captioned "frequency". It reaches much further than one engine —
    the catalogue carries 245 "Logarithmic Scale" entries, 243 "Semi-Log X"
    and 246 "Semi-Log Y", and real data sitting inside a single decade is
    entirely ordinary.

    The ranges below deliberately mix sub-decade, multi-decade and
    decade-crossing cases, so a fix that labelled the minors by breaking the
    decade labels would fail here too.
    """
    source = _backend_text()
    # The class qualifier goes: the function is compiled free-standing, and it
    # calls linearTicks and formatTick unqualified inside its own body anyway.
    body = extract(source, "logTicks").replace("QtPlotBackend::", "")
    program = LOG_SHIM + body + LOG_MAIN

    cpp = tmp_path / "logticks.cpp"
    cpp.write_text(program, encoding="utf-8")
    binary = tmp_path / "logticks"
    qt = "/usr/include/x86_64-linux-gnu/qt6"
    # ASKED BEFORE COMPILING, because "is Qt installed" is a question about the
    # machine and not about the compiler's wording. Without this the test fell
    # through to the stderr check below, which looked for the string "QtCore" -
    # and the error a bare runner actually produces is
    #     fatal error: QString: No such file or directory
    # naming the header the program included, not the directory it lives in. A
    # guard that matches a NAME rather than the CONDITION, which is this
    # project's most expensive recurring mistake, and it failed the science
    # workflow - a job whose own comment says it runs deliberately without Qt.
    if not (Path(qt) / "QtCore" / "QString").exists():
        pytest.skip(f"no Qt 6 headers at {qt} to compile against")
    build = subprocess.run(
        ["g++", "-O1", "-std=c++20", "-fPIC", "-o", str(binary), str(cpp),
         "-I", qt, "-I", f"{qt}/QtCore", "-lQt6Core"],
        capture_output=True, text=True)
    if build.returncode != 0:
        # Only a MISSING QT is a skip. Anything else is a real failure, and
        # reporting it as a skip is how a check quietly stops checking - the
        # first version of this test did exactly that, reporting "no Qt 6
        # headers" for a compile error that had nothing to do with headers.
        # Any Qt header, not one directory's name: see the note above.
        if re.search(r"fatal error: Q[A-Za-z0-9_]*: No such file", build.stderr):
            pytest.skip("a Qt 6 header is missing, so this cannot be compiled here")
        raise AssertionError(
            "the extracted logTicks did not compile:\n" + build.stderr[:1500])

    out = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
    bare: list[str] = []
    for line in out.stdout.strip().splitlines():
        lo, hi, total, labelled = line.split()
        assert int(total) > 0, f"no ticks at all for {lo}..{hi}"
        if int(labelled) == 0:
            bare.append(f"{lo}..{hi}")

    assert not bare, (
        "logarithmic axis ranges that produce ticks but no labels, leaving the "
        "reader an axis with no numbers on it: " + ", ".join(bare))
