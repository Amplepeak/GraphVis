"""Writing a dataset back out.

THE IMPORTER HAD 149 READERS AND NO WRITERS, and the manual said so in as many
words: *"It does not edit your data files: a dataset on disk is read and never
written back."* What that meant in use is that you could clean a column, convert
its units, derive a ratio or run an analysis, and there was no way to keep any
of it except as a picture. For a program whose job is turning tables into
figures, that is the first wall a working scientist walks into.

Writers are registered by extension in WRITERS below, exactly as the readers
are, so adding a format is one line and a function rather than a branch in a
dispatcher. The pairing is deliberate: every format here can also be read, so
anything this writes can be opened again - by GraphVis, and by whatever the
person uses next.

WHAT THIS DOES NOT DO. It never writes over the file a dataset was imported
from. The importer's contract - read, never write back - is worth keeping for
the SOURCE, because a dataset on disk is usually somebody's instrument output
and this program is not the thing that should edit it. Writing goes to a path
the caller names, and `overwrite` has to be asked for.
"""
from __future__ import annotations

import os
from pathlib import Path

import pandas as pd


class ExportError(Exception):
    """A write that cannot be done, said in a sentence a person can act on."""


def _need(module: str, package: str):
    """The same lazy-import courtesy the readers extend.

    A missing optional package becomes "this format needs X" rather than an
    ImportError from the middle of a write.
    """
    try:
        return __import__(module)
    except ImportError as exc:                            # pragma: no cover
        raise ExportError(
            f"Writing this format needs the {package} package. "
            f"Install it, or choose CSV, which needs nothing."
        ) from exc


# --------------------------------------------------------------------- writers
def _w_csv(frame: pd.DataFrame, path: str) -> None:
    frame.to_csv(path, index=False)


def _w_tsv(frame: pd.DataFrame, path: str) -> None:
    frame.to_csv(path, sep="\t", index=False)


def _w_json(frame: pd.DataFrame, path: str) -> None:
    # `records` rather than the default column-of-arrays: it is the shape a
    # reader of the file expects, and it round-trips through `_r_json`.
    frame.to_json(path, orient="records", indent=1)


def _w_jsonl(frame: pd.DataFrame, path: str) -> None:
    frame.to_json(path, orient="records", lines=True)


def _w_excel(frame: pd.DataFrame, path: str) -> None:
    _need("openpyxl", "openpyxl")
    frame.to_excel(path, index=False)


def _w_parquet(frame: pd.DataFrame, path: str) -> None:
    _need("pyarrow", "pyarrow")
    frame.to_parquet(path, index=False)


def _w_feather(frame: pd.DataFrame, path: str) -> None:
    _need("pyarrow", "pyarrow")
    frame.reset_index(drop=True).to_feather(path)


def _w_arrow(frame: pd.DataFrame, path: str) -> None:
    pa = _need("pyarrow", "pyarrow")
    from pyarrow import ipc
    table = pa.Table.from_pandas(frame, preserve_index=False)
    with pa.OSFile(path, "wb") as sink:
        with ipc.new_file(sink, table.schema) as writer:
            writer.write_table(table)


def _w_html(frame: pd.DataFrame, path: str) -> None:
    Path(path).write_text(frame.to_html(index=False), encoding="utf-8")


def _w_markdown(frame: pd.DataFrame, path: str) -> None:
    _need("tabulate", "tabulate")
    Path(path).write_text(frame.to_markdown(index=False), encoding="utf-8")


WRITERS: dict[str, tuple] = {}


def _register(exts, fn, group: str) -> None:
    for e in exts:
        WRITERS[e] = (fn, group)


_register((".csv",), _w_csv, "Tables")
_register((".tsv",), _w_tsv, "Tables")
_register((".txt", ".dat"), _w_tsv, "Tables")
_register((".json",), _w_json, "Tables")
_register((".jsonl", ".ndjson"), _w_jsonl, "Tables")
_register((".xlsx",), _w_excel, "Spreadsheets")
_register((".parquet", ".pq"), _w_parquet, "Columnar")
_register((".feather",), _w_feather, "Columnar")
_register((".arrow", ".ipc"), _w_arrow, "Columnar")
_register((".html", ".htm"), _w_html, "Documents")
_register((".md",), _w_markdown, "Documents")

ALL_EXT = tuple(sorted(WRITERS))


def format_groups() -> dict[str, list[str]]:
    """The writable formats, grouped, for a file dialog's filter list."""
    groups: dict[str, list[str]] = {}
    for ext, (_fn, group) in WRITERS.items():
        groups.setdefault(group, []).append(ext)
    return {g: sorted(v) for g, v in sorted(groups.items())}


def _suffix(path: str) -> str:
    return os.path.splitext(str(path))[1].lower()


def export_frame(frame: pd.DataFrame, path: str, *,
                 columns=None, overwrite: bool = False) -> dict:
    """Write one frame to `path`, choosing the writer by extension."""
    if frame is None or len(frame.columns) == 0:
        raise ExportError("There is nothing to write: the dataset has no columns.")
    target = Path(path)
    if not str(target):
        raise ExportError("A file to write to has not been chosen.")
    ext = _suffix(path)
    if ext not in WRITERS:
        raise ExportError(
            f"GraphVis cannot write '{ext or 'a file with no extension'}'. "
            f"It can write: {', '.join(ALL_EXT)}.")
    # OVERWRITING IS ASKED FOR, NOT ASSUMED. The whole reason this module
    # exists is that data on disk was untouchable; the first thing it must not
    # do is quietly replace something.
    if target.exists() and not overwrite:
        raise ExportError(
            f"{target.name} already exists. Choose another name, or say "
            f"overwrite.")

    out = frame
    if columns:
        wanted = [str(c) for c in columns]
        missing = [c for c in wanted if c not in frame.columns]
        if missing:
            raise ExportError(
                "These columns are not in the dataset: " + ", ".join(missing))
        out = frame[wanted]

    parent = target.parent
    if str(parent):
        parent.mkdir(parents=True, exist_ok=True)
    fn, group = WRITERS[ext]
    try:
        fn(out, str(target))
    except ExportError:
        raise
    except OSError as exc:
        raise ExportError(f"Could not write {target.name}: {exc}") from exc

    return {
        "ok": True,
        "path": str(target.resolve()),
        "format": ext,
        "kind": group,
        "rows": int(len(out)),
        "columns": list(out.columns),
    }


def export_arrow(arrow_path: str, path: str, *, columns=None,
                 overwrite: bool = False) -> dict:
    """Write the dataset GraphVis is holding, named by its Arrow cache file.

    The application knows a dataset by the `.arrow` file the importer wrote, so
    that is what it can name. Reading it back rather than keeping a frame in
    memory also means the thing written is the thing the program is actually
    using, not a copy that could have drifted from it.
    """
    pa = _need("pyarrow", "pyarrow")
    from pyarrow import ipc
    try:
        with pa.memory_map(str(arrow_path), "rb") as source:
            frame = ipc.open_file(source).read_all().to_pandas()
    except (OSError, pa.ArrowInvalid) as exc:
        raise ExportError(
            f"Could not read the dataset back from {Path(arrow_path).name}: "
            f"{exc}") from exc
    return export_frame(frame, path, columns=columns, overwrite=overwrite)
