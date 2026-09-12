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
BACKEND = ROOT / "native" / "plot2d" / "src" / "QtPlotBackend.cpp"

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
    assert match, f"{name} is not in {BACKEND.name} - has it been renamed?"
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
    source = BACKEND.read_text(encoding="utf-8", errors="ignore")
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
    source = BACKEND.read_text(encoding="utf-8", errors="ignore")
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
