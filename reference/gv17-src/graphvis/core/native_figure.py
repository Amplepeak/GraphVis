"""Native editable GraphVis figure package (.gvfig / .gvis).

The format is a ZIP container with a JSON manifest plus CSV/NPY payloads. It is
portable, transparent, versioned, and restores PlotSpec, annotations, raw data,
matrices/volumes/tensors, units and view state without executing code.
"""
from __future__ import annotations

import io
import json
import time
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
import pandas as pd

from graphvis.data.loader import Dataset
from graphvis.rendering.render_core import PlotSpec

FORMAT_NAME = "GraphVis Native Figure"
FORMAT_VERSION = 1


@dataclass(slots=True)
class NativeFigureBundle:
    spec: PlotSpec
    datasets: dict[str, Dataset]
    metadata: dict[str, Any]


def _write_array(zf: zipfile.ZipFile, path: str, arr: np.ndarray) -> None:
    bio = io.BytesIO()
    np.save(bio, np.asarray(arr), allow_pickle=False)
    zf.writestr(path, bio.getvalue())


def _read_array(zf: zipfile.ZipFile, path: str) -> np.ndarray:
    return np.load(io.BytesIO(zf.read(path)), allow_pickle=False)


def save_native_figure(path: str | Path, spec: PlotSpec, metadata: dict[str, Any] | None = None) -> str:
    target = Path(path)
    if target.suffix.lower() not in (".gvfig", ".gvis"):
        target = target.with_suffix(".gvfig")
    target.parent.mkdir(parents=True, exist_ok=True)
    manifest: dict[str, Any] = {
        "format": FORMAT_NAME,
        "version": FORMAT_VERSION,
        "created": time.time(),
        "spec": spec.to_dict(),
        "metadata": metadata or {},
        "datasets": [],
    }
    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for i, (name, ds) in enumerate(spec.datasets.items()):
            base = f"datasets/{i:03d}"
            csv_path = f"{base}/frame.csv"
            zf.writestr(csv_path, ds.df.to_csv(index=False))
            rec: dict[str, Any] = {
                "name": name,
                "path": ds.path,
                "frame": csv_path,
                "meta": ds.meta,
                "units": getattr(ds, "units", {}),
                "text_columns": getattr(ds, "text_columns", []),
                "assumed": bool(getattr(ds, "assumed", False)),
                "assumption_note": str(getattr(ds, "assumption_note", "")),
                "text_data": getattr(ds, "text_data", {}) or {},
                "matrices": {}, "volumes": {}, "tensors": {}, "aux": {}, "topology": {},
            }
            for store_name in ("matrices", "volumes", "tensors", "aux"):
                for j, (key, arr) in enumerate((getattr(ds, store_name, {}) or {}).items()):
                    ap = f"{base}/{store_name}/{j:03d}.npy"
                    _write_array(zf, ap, np.asarray(arr))
                    rec[store_name][key] = ap
            for j, (key, arr) in enumerate((getattr(ds, "topology", {}) or {}).items()):
                try:
                    ap = f"{base}/topology/{j:03d}.npy"
                    _write_array(zf, ap, np.asarray(arr))
                    rec["topology"][key] = ap
                except Exception:
                    pass
            manifest["datasets"].append(rec)
        zf.writestr("manifest.json", json.dumps(manifest, indent=2, ensure_ascii=False, default=str))
    return str(target)


def load_native_figure(path: str | Path) -> NativeFigureBundle:
    src = Path(path)
    with zipfile.ZipFile(src, "r") as zf:
        manifest = json.loads(zf.read("manifest.json").decode("utf-8"))
        if manifest.get("format") != FORMAT_NAME:
            raise ValueError("Not a GraphVis native figure file.")
        if int(manifest.get("version", 0)) > FORMAT_VERSION:
            raise ValueError(f"This .gvfig uses newer format version {manifest.get('version')}.")
        datasets: dict[str, Dataset] = {}
        for rec in manifest.get("datasets", []):
            frame = pd.read_csv(io.BytesIO(zf.read(rec["frame"])))
            stores: dict[str, dict[str, np.ndarray]] = {}
            for store_name in ("matrices", "volumes", "tensors", "aux", "topology"):
                stores[store_name] = {key: _read_array(zf, p) for key, p in rec.get(store_name, {}).items()}
            meta = dict(rec.get("meta") or {})
            meta.setdefault("units", rec.get("units") or {})
            ds = Dataset(rec["name"], rec.get("path") or str(src), frame,
                         matrices=stores["matrices"], meta=meta, aux=stores["aux"],
                         volumes=stores["volumes"], tensors=stores["tensors"],
                         text_data=rec.get("text_data") or {}, topology=stores["topology"])
            ds.assumed = bool(rec.get("assumed", False))
            ds.meta["assumption_note"] = str(rec.get("assumption_note", ""))
            datasets[rec["name"]] = ds
        spec = PlotSpec.from_dict(manifest.get("spec") or {}, registry=datasets)
        return NativeFigureBundle(spec=spec, datasets=datasets, metadata=manifest.get("metadata") or {})
