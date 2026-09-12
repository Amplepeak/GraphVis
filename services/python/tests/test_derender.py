"""Reading numbers back off a figure, checked against a figure we drew.

Two failures this suite exists to prevent, both of which had already happened.

`literature.extract` only returned a figure list when a vision model was
configured. Every downstream feature - choosing a figure, calibrating it,
tracing a curve - starts by selecting from that list, so the whole de-rendering
half of the program was unreachable without an API key, while the code it would
have called sat there fully written and passing no test.
`test_figures_are_found_without_a_model` makes that a failing test.

And a de-render that returns a plausible number of rows says nothing about
whether those rows are the right numbers. So the figure here is drawn from a
formula and the traced values are compared against that formula. A trace that
comes back with the wrong scale, a flipped y axis or an off-by-the-margin
calibration all produce confident, wrong, non-empty output - which is the worst
failure this feature can have, because the result looks like data.
"""
from __future__ import annotations

import json
import math
import struct
import subprocess
import sys
import zlib
from pathlib import Path

import pandas as pd
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from graphvis_science import service  # noqa: E402

# The plot area inside the drawn figure, and what its axes are declared to span.
BOX = (60, 40, 480, 330)
X_RANGE = (0.0, 120.0)
Y_RANGE = (0.0, 1.0)
CURVE_RGB = (214, 39, 40)


def _truth(pixel_x: float) -> float:
    """The y value the red curve was drawn at, in axis units."""
    centre = 330 - 280 * (0.5 + 0.45 * math.sin((pixel_x - BOX[0]) / 60.0))
    return (BOX[3] - centre) / (BOX[3] - BOX[1]) * (Y_RANGE[1] - Y_RANGE[0]) + Y_RANGE[0]


def _write_figure(path: Path) -> None:
    """A 'published figure': an axes box, a red curve and a blue distractor.

    Written by hand rather than with matplotlib so the test has no plotting
    dependency and so the curve's position is exactly the formula above rather
    than whatever a renderer did with it.
    """
    width, height = 520, 380
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            colour = (250, 250, 250)
            if BOX[0] <= x <= BOX[2] and BOX[1] <= y <= BOX[3]:
                colour = (255, 255, 255)
                if x in (BOX[0], BOX[2]) or y in (BOX[1], BOX[3]):
                    colour = (40, 40, 40)
                red_y = 330 - int(280 * (0.5 + 0.45 * math.sin((x - BOX[0]) / 60.0)))
                if abs(y - red_y) <= 2:
                    colour = CURVE_RGB
                # A second series in another colour. A trace that picks this up
                # too is a tolerance that is too wide, and the errors below say
                # so loudly.
                blue_y = 330 - int(280 * (0.5 + 0.30 * math.cos((x - BOX[0]) / 45.0)))
                if abs(y - blue_y) <= 2:
                    colour = (31, 119, 180)
            row.extend(colour)
        rows.append(row)

    raw = b"".join(b"\x00" + bytes(r) for r in rows)

    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 6))
        + chunk(b"IEND", b"")
    )


@pytest.fixture()
def figure(tmp_path: Path) -> Path:
    path = tmp_path / "figure.png"
    _write_figure(path)
    return path


def _derender(figure: Path, **overrides) -> dict:
    request = {
        "op": "literature.derender",
        "image_path": str(figure),
        "rgb": list(CURVE_RGB),
        "bbox": list(BOX),
        "x_range": list(X_RANGE),
        "y_range": list(Y_RANGE),
        "x_scale": "linear",
        "y_scale": "linear",
        "plot_type": "line",
        "tolerance": 45.0,
    }
    request.update(overrides)
    return service.dispatch(request)


def test_traced_values_match_the_curve_that_was_drawn(figure: Path) -> None:
    reply = _derender(figure)
    assert reply["ok"], reply
    # One sample per pixel column of the plot area, give or take the ends.
    assert reply["rows"] > 300, reply["rows"]

    frame = pd.read_csv(reply["csv_path"])
    errors = sorted(abs(row.y - _truth(row.pixel_x)) for row in frame.itertuples())
    n = len(errors)
    span = Y_RANGE[1] - Y_RANGE[0]
    # 2% of the axis range. The residual that remains is the drawn line's own
    # thickness: a three-pixel line in a 290-pixel box is 1% before anybody has
    # made a mistake, so a tighter bound here would be measuring the test.
    assert errors[int(n * 0.95)] < 0.02 * span, (
        f"95th percentile error {errors[int(n * 0.95)]:.4f} of a {span} range"
    )
    assert errors[-1] < 0.05 * span, f"worst error {errors[-1]:.4f}"


def test_x_values_span_the_declared_range(figure: Path) -> None:
    """A calibration that ignores the box maps into the wrong x entirely."""
    frame = pd.read_csv(_derender(figure)["csv_path"])
    assert frame.x.min() == pytest.approx(X_RANGE[0], abs=1.0)
    assert frame.x.max() == pytest.approx(X_RANGE[1], abs=1.0)


def test_a_colour_that_is_not_there_returns_nothing_rather_than_noise(figure: Path) -> None:
    """Silence is the honest answer, and the interface says so."""
    reply = _derender(figure, rgb=[0, 200, 0], tolerance=10.0)
    assert reply["ok"], reply
    assert reply["rows"] == 0


def test_a_reconstruction_script_is_written_beside_the_data(figure: Path) -> None:
    """The point is a figure somebody else can redraw, not just a CSV."""
    reply = _derender(figure)
    script = Path(reply["script_path"])
    assert script.exists()
    assert script.read_text(encoding="utf-8").strip()


def test_figures_are_found_without_a_model(tmp_path: Path, monkeypatch) -> None:
    """literature.extract must return figures on the offline path.

    Everything else here is unreachable without it: there is nothing to
    calibrate until there is something to select.
    """
    pytest.importorskip("pymupdf", reason="figure images come out of the PDF with PyMuPDF")
    import pymupdf

    # A one-page PDF with one raster figure in it.
    pdf = tmp_path / "paper.pdf"
    doc = pymupdf.open()
    page = doc.new_page()
    image = tmp_path / "panel.png"
    _write_figure(image)
    page.insert_image(pymupdf.Rect(40, 40, 440, 340), filename=str(image))
    page.insert_text((40, 380), "Figure 1 A drawn curve")
    doc.save(str(pdf))
    doc.close()

    reply = service.dispatch({"op": "literature.extract", "path": str(pdf),
                              "out_dir": str(tmp_path / "out"),
                              # The offline default: no reader configured.
                              "vlm": {"provider": 0}})
    assert reply["ok"], reply
    assert reply["reader"] == "", "no model was configured, so none should be claimed"
    assert reply["figures"], "the offline path must still find the figures"
    assert Path(reply["figures"][0]["path"]).exists()


def _one_page_paper(tmp_path: Path) -> Path:
    """A PDF with a figure and a text layer, for the protocol tests."""
    pymupdf = pytest.importorskip("pymupdf")
    image = tmp_path / "panel.png"
    _write_figure(image)
    pdf = tmp_path / "paper.pdf"
    doc = pymupdf.open()
    page = doc.new_page()
    page.insert_image(pymupdf.Rect(40, 40, 440, 340), filename=str(image))
    page.insert_text((40, 380), "Figure 1 A drawn curve")
    page.insert_text((40, 400), "Table 1 shows the yield at 10 20 30 and 40 minutes.")
    doc.save(str(pdf))
    doc.close()
    return pdf


def _run_service(request: dict, tmp_path: Path) -> list[str]:
    """Talk to the service the way the application does: a line in, lines out."""
    root = Path(__file__).resolve().parents[1]
    proc = subprocess.run(
        [sys.executable, "-m", "graphvis_science.service"],
        input=json.dumps(request) + "\n",
        capture_output=True, text=True, cwd=str(root), timeout=600,
    )
    return [line for line in proc.stdout.splitlines() if line.strip()]


def test_stdout_carries_the_protocol_and_nothing_else(tmp_path: Path) -> None:
    """A library printing to stdout writes into the middle of the reply stream.

    PyMuPDF really does print a suggestion about pymupdf_layout during
    extraction. The application survived it only because it skips lines that do
    not parse as JSON objects - a library that ever printed something
    JSON-shaped would have been read as a reply.
    """
    pdf = _one_page_paper(tmp_path)
    lines = _run_service({"op": "literature.extract", "path": str(pdf),
                          "out_dir": str(tmp_path / "out"),
                          "vlm": {"provider": 0}}, tmp_path)
    for line in lines:
        parsed = json.loads(line)   # raises if anything else got in
        assert isinstance(parsed, dict), line


def test_progress_is_reported_and_never_goes_backwards(tmp_path: Path) -> None:
    """A bar that reaches 100%, says Done, and then drops to 96% reads as broken.

    The extractor counts its own work 0..100 and two phases follow it, so its
    scale has to be squeezed rather than passed through.
    """
    pdf = _one_page_paper(tmp_path)
    lines = [json.loads(line) for line in
             _run_service({"op": "literature.extract", "path": str(pdf),
                           "out_dir": str(tmp_path / "out"),
                           "vlm": {"provider": 0}}, tmp_path)]
    progress = [line for line in lines if line.get("progress")]
    replies = [line for line in lines if not line.get("progress")]

    assert len(progress) >= 3, "too few steps reported to be worth showing"
    assert len(replies) == 1, "exactly one reply per request"
    # The reply must be LAST, or the application takes a progress line as the
    # answer and drops the real one.
    assert not lines[-1].get("progress")

    seen = [p["percent"] for p in progress if p["percent"] >= 0]
    assert seen == sorted(seen), f"progress went backwards: {seen}"
    assert max(seen) <= 100
    assert all(p["message"] for p in progress), "a step with no name says nothing"


# --------------------------------------------------------------------- corpus
#
# LiteratureCorpusRunner has sat in graphvis_science/validation with no caller
# since it was written. It is the right shape for exactly this job - run a set
# of cases through one processor, record what succeeded, compare against ground
# truth where there is any - so it runs the de-render corpus rather than being
# deleted for being unused.
#
# Three cases, because the failures worth catching are not "did it crash": a
# tolerance too tight finds nothing, a tolerance too loose picks up the
# neighbouring series, and only the middle one is right. A runner that reports
# 3 successes and 0 failures on those three is reporting nothing, so the
# assertions below are about the NUMBERS each case produced.


def test_the_corpus_runner_drives_the_de_renderer(tmp_path: Path) -> None:
    from graphvis_science.validation.corpus import CorpusCase, LiteratureCorpusRunner

    image = tmp_path / "figure.png"
    _write_figure(image)

    traced: dict[str, int] = {}

    def process(case: CorpusCase) -> str | None:
        tolerance = {"tight": 2.0, "right": 45.0, "too loose": 400.0}[case.name]
        reply = _derender(Path(case.source), tolerance=tolerance)
        assert reply["ok"], reply
        traced[case.name] = reply["rows"]
        return reply.get("csv_path")

    cases = [CorpusCase(name=n, source=str(image), kind="line")
             for n in ("tight", "right", "too loose")]
    report = LiteratureCorpusRunner(process).run(cases)

    assert report.cases == 3
    assert report.failures == 0, report.rows
    assert report.failure_rate == 0.0
    # Every case is recorded with how long it took, which is what makes the
    # report worth keeping when it is run over a real corpus.
    assert all("seconds" in row for row in report.rows)

    # This figure is drawn with hard edges, so the curve is exactly one RGB
    # value and even a tolerance of 2 matches every column of it. That is a
    # fact about the fixture, not about the de-renderer: a scanned page is
    # antialiased and compressed, and there a tight tolerance finds nothing.
    # Said here because the obvious assertion - tight finds nothing - is wrong
    # for this figure and would be a test asserting its own mistake.
    assert traced["tight"] > 300, traced
    assert traced["right"] > 300, traced

    # What the corpus is actually for: a tolerance wide enough to swallow the
    # second series stops tracing the first one. More rows is not the tell -
    # a WRONG curve is - so this compares against the formula.
    loose = pd.read_csv(_derender(image, tolerance=400.0)["csv_path"])
    errors = sorted(abs(row.y - _truth(row.pixel_x)) for row in loose.itertuples())
    assert errors[len(errors) // 2] > 0.02, (
        "a tolerance wide enough to catch both series should no longer trace the "
        "red one accurately - if it does, this case is testing nothing"
    )

    report.save(tmp_path / "report.json")
    assert (tmp_path / "report.json").exists()
