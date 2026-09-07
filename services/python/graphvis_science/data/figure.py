"""Native editable GraphVis figure package (.gvfig / .gvis).

A port of GraphVis 17's ``core/native_figure.py``. The container is unchanged -
a ZIP holding a JSON manifest plus data payloads - so it stays portable,
transparent, versioned, and restores a figure without executing any code.

What changed is the payload. v17 stored each dataset as CSV because its
datasets were pandas frames. v18's datasets are already Arrow IPC on disk, so a
v18 file stores the Arrow file verbatim: no conversion on save, no re-parsing
on load, and the bytes in the container are the bytes the renderer reads.

v17 files still open. ``load_figure`` accepts version 1 and converts its CSV
frames to Arrow on the way through, which is the only reason this module knows
what a CSV is.
"""
from __future__ import annotations

import io
import json
import os
import time
import zipfile
from pathlib import Path
from typing import Any

FORMAT_NAME = "GraphVis Native Figure"
FORMAT_VERSION = 2
SUFFIXES = (".gvfig", ".gvis")


class FigureError(ValueError):
    """A .gvfig that cannot be read, with a sentence a user can act on."""


def _arrow_from_csv(csv_bytes: bytes, out_path: str) -> int:
    """v17 compatibility only. Returns the row count."""
    import pandas as pd
    import pyarrow as pa
    import pyarrow.ipc as ipc

    frame = pd.read_csv(io.BytesIO(csv_bytes))
    frame.columns = [str(c) for c in frame.columns]
    table = pa.Table.from_pandas(frame, preserve_index=False)
    with pa.OSFile(out_path, "wb") as sink:
        with ipc.new_file(sink, table.schema) as writer:
            writer.write_table(table)
    return int(len(frame))


def save_figure(path: str, spec: dict, datasets: list[dict],
                metadata: dict[str, Any] | None = None) -> dict:
    """Write a .gvfig.

    ``spec`` is the canvas state as the native side reports it - engine,
    variant, title, columns, log flags, display units, colours. It is stored
    verbatim and handed back verbatim, so adding a field to the canvas does not
    need a change here.

    ``datasets`` is a list of ``{"name": str, "arrow_path": str}``. A dataset
    whose Arrow file has gone missing is recorded in the manifest and skipped
    rather than failing the save, because losing the figure over one absent
    payload is worse than saving what is still there.
    """
    target = Path(path)
    if target.suffix.lower() not in SUFFIXES:
        target = target.with_suffix(".gvfig")
    target.parent.mkdir(parents=True, exist_ok=True)

    manifest: dict[str, Any] = {
        "format": FORMAT_NAME,
        "version": FORMAT_VERSION,
        "created": time.time(),
        "spec": dict(spec or {}),
        "metadata": dict(metadata or {}),
        "datasets": [],
    }
    missing: list[str] = []

    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for i, entry in enumerate(datasets or []):
            name = str(entry.get("name") or f"dataset{i}")
            source = str(entry.get("arrow_path") or "")
            if not source or not os.path.exists(source):
                missing.append(name)
                continue
            inner = f"datasets/{i:03d}/table.arrow"
            zf.write(source, inner)
            manifest["datasets"].append({
                "name": name,
                "source_path": source,
                "arrow": inner,
                "bytes": int(os.path.getsize(source)),
            })
        zf.writestr("manifest.json",
                    json.dumps(manifest, indent=2, ensure_ascii=False, default=str))

    return {
        "ok": True,
        "path": str(target),
        "datasets": len(manifest["datasets"]),
        "missing": missing,
        "bytes": int(target.stat().st_size),
    }


def load_figure(path: str, out_dir: str) -> dict:
    """Read a .gvfig, extracting its payloads into ``out_dir``.

    Returns the spec exactly as it was stored plus the extracted Arrow files.
    Version 1 files - GraphVis 17's - carry CSV frames and are converted here.
    """
    src = Path(path)
    if not src.exists():
        raise FigureError(f"File not found: {path}")
    if not zipfile.is_zipfile(src):
        raise FigureError("This is not a GraphVis figure file - it is not a ZIP container.")

    os.makedirs(out_dir, exist_ok=True)

    with zipfile.ZipFile(src, "r") as zf:
        try:
            raw = zf.read("manifest.json")
        except KeyError:
            raise FigureError("This ZIP has no manifest.json, so it is not a GraphVis figure.")
        try:
            manifest = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise FigureError(f"The figure's manifest is corrupt: {exc}")

        if manifest.get("format") != FORMAT_NAME:
            raise FigureError("Not a GraphVis native figure file.")
        version = int(manifest.get("version", 0) or 0)
        if version > FORMAT_VERSION:
            raise FigureError(
                f"This figure was written by a newer GraphVis (format version {version}). "
                f"This build reads up to version {FORMAT_VERSION}."
            )

        datasets = []
        warnings: list[str] = []
        for i, rec in enumerate(manifest.get("datasets") or []):
            name = str(rec.get("name") or f"dataset{i}")
            stem = "".join(c if (c.isalnum() or c in "-_") else "_" for c in name) or f"dataset{i}"
            out_path = os.path.join(out_dir, f"{stem}.arrow")
            try:
                if version >= 2 and rec.get("arrow"):
                    with open(out_path, "wb") as fh:
                        fh.write(zf.read(rec["arrow"]))
                    rows = -1
                elif rec.get("frame"):
                    # GraphVis 17: a CSV frame.
                    rows = _arrow_from_csv(zf.read(rec["frame"]), out_path)
                else:
                    warnings.append(f"'{name}' has no data payload and was skipped")
                    continue
            except KeyError:
                warnings.append(f"'{name}' names a payload that is not in the container")
                continue
            datasets.append({
                "name": name,
                "arrow_path": out_path,
                "rows": rows,
                "source_path": str(rec.get("source_path") or rec.get("path") or ""),
            })

    return {
        "ok": True,
        "path": str(src),
        "version": version,
        "spec": dict(manifest.get("spec") or {}),
        "metadata": dict(manifest.get("metadata") or {}),
        "datasets": datasets,
        "warnings": warnings,
    }
