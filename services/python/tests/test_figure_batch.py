"""Tests for the .gvfig figure package and folder-scale batch processing.

Both are ports from GraphVis 17 and both touch the filesystem, so everything
here runs against real files in a temporary folder rather than mocks. The
version-1 case matters most: it is the only thing standing between a user and
the figures GraphVis 17 wrote.
"""
from __future__ import annotations

import io
import json
import os
import sys
import tempfile
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import numpy as np
import pandas as pd
import pyarrow as pa
import pyarrow.ipc as ipc

from graphvis_science.data.figure import (FORMAT_NAME, FORMAT_VERSION, FigureError,
                                          load_figure, save_figure)
from graphvis_science.data import batch as batch_mod

passed = 0
failed = 0


def ok(label, condition, detail=""):
    global passed, failed
    if condition:
        passed += 1
        print(f"  ok   {label:<44} {detail}")
    else:
        failed += 1
        print(f"  FAIL {label:<44} {detail}")


def raises(label, fn, exc_type, fragment=""):
    global passed, failed
    try:
        fn()
    except exc_type as exc:
        if fragment and fragment.lower() not in str(exc).lower():
            failed += 1
            print(f"  FAIL {label:<44} wrong message: {exc}")
        else:
            passed += 1
            print(f"  ok   {label:<44} {str(exc)[:46]}")
        return
    except Exception as exc:  # noqa: BLE001
        failed += 1
        print(f"  FAIL {label:<44} raised {type(exc).__name__}: {exc}")
        return
    failed += 1
    print(f"  FAIL {label:<44} did not raise")


def write_arrow(path, frame):
    table = pa.Table.from_pandas(frame, preserve_index=False)
    with pa.OSFile(str(path), "wb") as sink:
        with ipc.new_file(sink, table.schema) as writer:
            writer.write_table(table)
    return str(path)


def read_arrow(path):
    with pa.memory_map(str(path), "r") as source:
        return ipc.open_file(source).read_all().to_pandas()


SPEC = {
    "engine": "Line Chart",
    "variant": "Semi-Log Y",
    "title": "Pressure decay",
    "xColumn": "time [s]",
    "yColumns": ["Pressure [kPa]"],
    "logX": False,
    "logY": True,
    "xUnit": "",
    "yUnit": "Pa",
    "colourVision": 0,
}

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    frame = pd.DataFrame({
        "time [s]": np.arange(50, dtype=float),
        "Pressure [kPa]": np.linspace(101.3, 3.7, 50),
    })
    source_arrow = write_arrow(tmp / "run.arrow", frame)

    print("\n=== a figure round-trips ===")
    target = tmp / "decay.gvfig"
    saved = save_figure(str(target), SPEC, [{"name": "run", "arrow_path": source_arrow}],
                        {"author": "test"})
    ok("save reports success", saved["ok"] is True)
    ok("file exists on disk", target.exists(), f"{saved['bytes']} bytes")
    ok("one dataset stored", saved["datasets"] == 1)
    ok("nothing reported missing", saved["missing"] == [])
    ok("the container is a real ZIP", zipfile.is_zipfile(target))

    out = tmp / "extracted"
    loaded = load_figure(str(target), str(out))
    ok("load reports success", loaded["ok"] is True)
    ok("version is the current one", loaded["version"] == FORMAT_VERSION,
       f"v{loaded['version']}")
    ok("spec comes back unchanged", loaded["spec"] == SPEC)
    ok("metadata comes back", loaded["metadata"].get("author") == "test")
    ok("one dataset extracted", len(loaded["datasets"]) == 1)

    restored = read_arrow(loaded["datasets"][0]["arrow_path"])
    ok("row count survives", len(restored) == len(frame), f"{len(restored)} rows")
    ok("column names survive", list(restored.columns) == list(frame.columns))
    ok("values survive exactly",
       bool(np.allclose(restored["Pressure [kPa]"], frame["Pressure [kPa]"])))

    print("\n=== the extension is enforced, not assumed ===")
    odd = save_figure(str(tmp / "no-suffix"), SPEC, [])
    ok("a missing suffix becomes .gvfig", odd["path"].endswith(".gvfig"),
       Path(odd["path"]).name)
    gvis = save_figure(str(tmp / "keeps.gvis"), SPEC, [])
    ok(".gvis is kept as given", gvis["path"].endswith(".gvis"))

    print("\n=== a missing payload does not lose the figure ===")
    partial = save_figure(str(tmp / "partial.gvfig"), SPEC, [
        {"name": "present", "arrow_path": source_arrow},
        {"name": "vanished", "arrow_path": str(tmp / "not-here.arrow")},
    ])
    ok("the readable dataset is still stored", partial["datasets"] == 1)
    ok("the missing one is named, not swallowed", partial["missing"] == ["vanished"],
       str(partial["missing"]))

    print("\n=== GraphVis 17 files still open ===")
    # A version-1 container exactly as v17 wrote it: CSV frames, "frame" key.
    v17 = tmp / "legacy.gvfig"
    legacy_manifest = {
        "format": FORMAT_NAME,
        "version": 1,
        "created": 0,
        "spec": {"engine": "Scatter Plot"},
        "metadata": {},
        "datasets": [{"name": "legacy", "path": "C:/old/run.csv",
                      "frame": "datasets/000/frame.csv", "meta": {}}],
    }
    with zipfile.ZipFile(v17, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("datasets/000/frame.csv", frame.to_csv(index=False))
        zf.writestr("manifest.json", json.dumps(legacy_manifest))

    old = load_figure(str(v17), str(tmp / "legacy-out"))
    ok("a v17 figure loads", old["ok"] is True, f"v{old['version']}")
    ok("its spec is returned", old["spec"].get("engine") == "Scatter Plot")
    ok("its CSV frame became Arrow", len(old["datasets"]) == 1)
    legacy_frame = read_arrow(old["datasets"][0]["arrow_path"])
    ok("v17 rows survive the conversion", len(legacy_frame) == len(frame),
       f"{len(legacy_frame)} rows")
    ok("v17 values survive the conversion",
       bool(np.allclose(legacy_frame["Pressure [kPa]"], frame["Pressure [kPa]"])))

    print("\n=== bad input is refused with a sentence, not a traceback ===")
    raises("a file that is not there", lambda: load_figure(str(tmp / "nope.gvfig"), str(tmp)),
           FigureError, "not found")

    plain = tmp / "plain.txt"
    plain.write_text("this is not a zip", encoding="utf-8")
    raises("a file that is not a ZIP", lambda: load_figure(str(plain), str(tmp)),
           FigureError, "ZIP")

    nomanifest = tmp / "nomanifest.gvfig"
    with zipfile.ZipFile(nomanifest, "w") as zf:
        zf.writestr("something.txt", "hello")
    raises("a ZIP with no manifest", lambda: load_figure(str(nomanifest), str(tmp)),
           FigureError, "manifest")

    wrongformat = tmp / "wrong.gvfig"
    with zipfile.ZipFile(wrongformat, "w") as zf:
        zf.writestr("manifest.json", json.dumps({"format": "Something Else", "version": 1}))
    raises("a manifest for another format",
           lambda: load_figure(str(wrongformat), str(tmp)), FigureError, "not a graphvis")

    future = tmp / "future.gvfig"
    with zipfile.ZipFile(future, "w") as zf:
        zf.writestr("manifest.json",
                    json.dumps({"format": FORMAT_NAME, "version": FORMAT_VERSION + 5}))
    raises("a newer format version", lambda: load_figure(str(future), str(tmp)),
           FigureError, "newer")

    corrupt = tmp / "corrupt.gvfig"
    with zipfile.ZipFile(corrupt, "w") as zf:
        zf.writestr("manifest.json", "{not json at all")
    raises("a corrupt manifest", lambda: load_figure(str(corrupt), str(tmp)),
           FigureError, "corrupt")

# --------------------------------------------------------------------- batch
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    folder = tmp / "runs"
    folder.mkdir()
    nested = folder / "second-pass"
    nested.mkdir()

    for i in range(3):
        pd.DataFrame({"t": np.arange(10.0), "v": np.arange(10.0) * (i + 1)}).to_csv(
            folder / f"run{i}.csv", index=False)
    pd.DataFrame({"t": np.arange(4.0), "v": np.ones(4)}).to_csv(
        nested / "deep.csv", index=False)
    (folder / "notes.md").write_text("not a dataset", encoding="utf-8")
    (folder / "broken.csv").write_text("this,is\nnot,numeric\n", encoding="utf-8")

    print("\n=== the scan asks the importer what it can read ===")
    exts = batch_mod.readable_extensions()
    ok("the extension set is large, not the v17 26", len(exts) > 100, f"{len(exts)} extensions")
    ok("every entry is dotted", all(e.startswith(".") for e in exts))

    flat = batch_mod.scan(str(folder))
    ok("non-recursive skips the subfolder",
       all("second-pass" not in p for p in flat), f"{len(flat)} files")
    ok("the markdown note is not picked up",
       all(not p.endswith(".md") for p in flat))
    deep = batch_mod.scan(str(folder), recursive=True)
    ok("recursive finds the subfolder", len(deep) == len(flat) + 1,
       f"{len(deep)} vs {len(flat)}")
    raises("a folder that is not one",
           lambda: batch_mod.scan(str(folder / "missing")), NotADirectoryError, "not a folder")

    print("\n=== a run reports per file and does not abort ===")
    result = batch_mod.run(flat, "summary", str(tmp / "stage"))
    ok("every file is accounted for", len(result.items) == len(flat),
       f"{len(result.items)} items")
    ok("the good files succeeded", result.successes >= 3, f"{result.successes} ok")
    ok("the bad file failed on its own", result.failures >= 1, f"{result.failures} failed")
    ok("successes plus failures is the total",
       result.successes + result.failures == len(result.items))
    good = next(i for i in result.items if i.ok)
    ok("a summary payload names its columns", "v" in good.payload["columns"],
       str(good.payload["columns"]))
    bad = next(i for i in result.items if not i.ok)
    ok("a failure carries the reason", ":" in bad.summary, bad.summary[:44])

    print("\n=== the other two operations ===")
    # Explicitly a file that reads, not flat[0] - sorted() puts broken.csv first,
    # which is exactly the trap this suite exists to catch.
    one_good = [str(folder / "run0.csv")]
    converted = batch_mod.run(one_good, "convert", str(tmp / "converted"))
    arrow_out = converted.items[0].payload["arrow_path"]
    ok("convert writes an Arrow file", os.path.exists(arrow_out), Path(arrow_out).name)
    described = batch_mod.run(one_good, "describe", str(tmp / "stage2"))
    ok("describe returns per-column statistics",
       "v" in (described.items[0].payload or {}),
       str(list((described.items[0].payload or {}).keys())))
    raises("an unknown operation", lambda: batch_mod.run(one_good, "teleport"),
           ValueError, "unknown batch operation")

    print("\n=== the HTML report ===")
    report = batch_mod.write_html_report(result, str(tmp / "report.html"), "Run 4")
    text = Path(report).read_text(encoding="utf-8")
    ok("the report is written", os.path.exists(report))
    ok("the title is in it", "Run 4" in text)
    ok("the counts are in it", f"{result.successes} succeeded" in text)
    ok("every file appears", all(Path(i.path).name in text for i in result.items))
    ok("the failure is marked ERROR", "ERROR" in text)
    ok("it is valid enough to be a document", text.startswith("<!doctype html>"))
    ok("the run records its output", report in result.output_files)
    raises("an unknown report format",
           lambda: batch_mod.write_report(result, str(tmp / "x.zzz"), "zzz"),
           ValueError, "unknown report format")

print(f"\n{passed} passed, {failed} failed")
sys.exit(1 if failed else 0)
