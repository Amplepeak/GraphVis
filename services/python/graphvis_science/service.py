from __future__ import annotations
import contextlib, json, sys, traceback
from pathlib import Path


def _load_frame(path: str):
    import pyarrow.ipc as ipc
    import pyarrow as pa
    p=Path(path)
    with pa.memory_map(str(p), "r") as source:
        table=ipc.open_file(source).read_all()
    return table.to_pandas(types_mapper=None)


# The protocol channel, captured before anything else can rebind sys.stdout.
#
# A library printing to stdout writes into the middle of the reply stream. That
# is not hypothetical: PyMuPDF prints "Consider using the pymupdf_layout
# package..." during extraction, which lands between two protocol lines. The
# application skips anything that is not a JSON object, so it survives - but it
# survives by luck, and a library that ever printed something JSON-shaped would
# be read as a reply. main() redirects stray output to stderr for the duration
# of a request; this is the handle the protocol keeps for itself.
_PROTOCOL = sys.stdout


def emit_progress(message: str, percent: float = -1.0) -> None:
    """Say what a long operation is doing WHILE it does it.

    The protocol was one JSON line per request and the application read the
    first line it got as the answer, so an extraction that takes four minutes
    on a scanned paper was four minutes of a completely static window with no
    way to tell it apart from a program that had hung. Reported exactly that
    way: "there's no indication of it loading or done".

    A progress line carries `progress: true`, which is what tells the
    application it is NOT the reply - see AppController's readyRead handler.
    Anything that does not understand the flag sees a line it has no branch
    for, which is the behaviour it already had for stray output.

    percent < 0 means "working, but I cannot say how far", which is honest for
    a step whose length is not known in advance and is better than a bar that
    invents a number.
    """
    try:
        _PROTOCOL.write(json.dumps({"progress": True, "message": str(message),
                                     "percent": float(percent)},
                                    separators=(",", ":")) + "\n")
        _PROTOCOL.flush()
    except Exception:
        # Progress is a courtesy. It must never be the reason an operation
        # fails - a closed pipe here would otherwise take the whole extraction
        # down after the work was already done.
        pass


def _figure_asset(req: dict):
    """A figure plus the calibration needed to read numbers off it.

    De-rendering needs to know where the axes are and what they span. A vision
    model supplies that when the VLM component is installed; without one the
    request carries it, which is the same information typed rather than guessed
    - and it means de-rendering works with no model at all.
    """
    from graphvis_science.literature.intelligence import FigureAsset
    from graphvis_science.literature.vlm import VLMObservation

    observation = VLMObservation(
        plot_type=str(req.get("plot_type") or "line"),
        x_scale=str(req.get("x_scale") or "linear"),
        y_scale=str(req.get("y_scale") or "linear"),
        x_range=tuple(float(v) for v in req["x_range"]) if req.get("x_range") else None,
        y_range=tuple(float(v) for v in req["y_range"]) if req.get("y_range") else None,
        plot_bbox=[float(v) for v in req["bbox"]] if req.get("bbox") else None,
    )
    return FigureAsset(page=int(req.get("page") or 1), path=str(req["image_path"]),
                       caption=str(req.get("caption") or ""), observation=observation)


def _vlm_provider(cfg: dict | None):
    """The vision model to read figures with, or None for the offline path.

    Nothing here is mandatory and nothing is the default. GraphVis reads a paper
    with deterministic parsing - PDF text, tables, embedded figures - and that is
    what runs unless somebody has deliberately chosen a model. `provider` is
    "off" (or absent) for that, "endpoint" for any OpenAI-compatible HTTPS API,
    or "local" for a model on this machine.

    The key arrives in the request and is never written anywhere: the app reads
    it from an environment variable or a file the person nominated, hands it over
    for the one call, and forgets it.
    """
    cfg = cfg or {}
    kind = str(cfg.get("provider") or "off").lower()
    if kind in ("", "off", "none"):
        return None
    if kind in ("endpoint", "openai", "openai-compatible"):
        endpoint = str(cfg.get("endpoint") or "").strip()
        model = str(cfg.get("model") or "").strip()
        if not endpoint or not model:
            raise ValueError("An endpoint and a model name are both needed for a hosted reader")
        from graphvis_science.literature.vlm import OpenAICompatibleVLM
        return OpenAICompatibleVLM(endpoint, model, str(cfg.get("api_key") or "") or None,
                                   timeout=float(cfg.get("timeout") or 90.0))
    if kind == "local":
        model = str(cfg.get("model") or "").strip()
        if not model:
            raise ValueError("A model id is needed for a local reader")
        from graphvis_science.literature.vlm import LocalTransformersVLM
        return LocalTransformersVLM(model, trust_remote_code=bool(cfg.get("trust_remote_code")))
    raise ValueError(f"Unknown reader: {kind}")


def _tiny_png(path: str) -> str:
    """A 16x16 white PNG, written without PIL.

    The connection test sends a picture because that is what these endpoints
    take; it is deliberately the smallest legal one, so a test costs a token or
    two rather than a page of a paper.
    """
    import zlib, struct
    w = h = 16
    raw = b"".join(b"\x00" + bytes([255, 255, 255] * w) for _ in range(h))
    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw))
           + chunk(b"IEND", b""))
    Path(path).write_bytes(png)
    return path


def dispatch(req: dict) -> dict:
    op=req.get("op", "health")
    if op=="health":
        return {"ok":True,"service":"graphvis-python-science","version":"18.4.0","transport":"Arrow IPC + JSON control"}
    if op=="statistics.describe":
        df=_load_frame(req["arrow_path"])
        cols=req.get("columns") or list(df.select_dtypes("number").columns)
        return {"ok":True,"result":df[cols].describe().to_dict()}
    if op=="literature.extract":
        # ONE path, model or no model.
        #
        # This used to branch: with a reader configured it ran the pipeline and
        # returned the figures it found; without one it ran the bare extractor
        # and returned `figures: []`. Since every downstream feature - picking a
        # figure, calibrating it, tracing a curve back into numbers - starts by
        # choosing a figure from that list, the entire de-rendering half of the
        # program was unreachable unless you had an API key. That is exactly
        # backwards: pulling the figure IMAGES out of a PDF is PyMuPDF and needs
        # no model at all. The model reads the axes for you; it is not what
        # finds the pictures.
        #
        # So the pipeline runs either way and the provider - which may be None -
        # only decides whether each figure also gets an observation attached.
        from graphvis_science.literature.intelligence import LiteratureIntelligencePipeline
        provider=_vlm_provider(req.get("vlm"))
        out_dir=str(req.get("out_dir") or (Path(req["path"]).parent/"graphvis_literature"))
        # The pipeline is tolerant of PyMuPDF being absent - it records a
        # warning and returns no figures rather than failing the extraction, so
        # there is no second, expensive text pass to fall back to here.
        analysed=LiteratureIntelligencePipeline(provider).analyze(
            req["path"],out_dir,progress=emit_progress)
        result=analysed.extraction
        reader=getattr(provider,"name","reader") if provider is not None else ""
        figures=[]
        for fig in analysed.figures:
            obs=fig.observation
            figures.append({
                "page":fig.page,"path":fig.path,"caption":fig.caption,
                "plot_type":getattr(obs,"plot_type","") if obs else "",
                "confidence":getattr(obs,"confidence",0.0) if obs else 0.0,
                "x_label":getattr(obs,"x_label","") if obs else "",
                "y_label":getattr(obs,"y_label","") if obs else "",
                "x_scale":getattr(obs,"x_scale","") if obs else "",
                "y_scale":getattr(obs,"y_scale","") if obs else "",
                "legend":list(getattr(obs,"legend",[]) or []) if obs else [],
                # Said out loud rather than swallowed: a figure the reader
                # failed on looks exactly like a figure it had nothing to say
                # about, and the difference is usually a bad key.
                "error":str(fig.diagnostics.get("vlm_error","")) if fig.diagnostics else ""})
        datasets=[]
        for d in getattr(result,"datasets",[]):
            datasets.append({"name":getattr(d,"name","dataset"),"rows":len(d.df),"columns":list(map(str,d.df.columns))})
        return {"ok":True,"title":getattr(result,"title",""),"datasets":datasets,"text_chars":len(getattr(result,"text","")),"saved_paths":list(getattr(result,"saved_paths",[]) or []),"parameters":dict(getattr(result,"parameters",{}) or {}),"semantic_context":dict(getattr(result,"semantic_context",{}) or {}),"warnings":list(getattr(result,"warnings",[]) or []),"figures":figures,"reader":reader}

    if op=="literature.vlm_test":
        # Does the chosen reader answer at all? Asked with a 16x16 blank rather
        # than a paper, so nothing of the person's leaves the machine to find
        # out whether an endpoint and a key are right.
        import tempfile
        provider=_vlm_provider(req.get("vlm"))
        if provider is None:
            return {"ok":True,"reader":"","detail":"No reader chosen - papers are read offline."}
        with tempfile.TemporaryDirectory() as tmp:
            path=_tiny_png(str(Path(tmp)/"probe.png"))
            try:
                obs=provider.analyze_figure(path,"This is a blank test image. Reply with JSON.")
            except Exception as exc:
                return {"ok":False,"reader":getattr(provider,"name","reader"),
                        "error":f"{type(exc).__name__}: {exc}"}
        return {"ok":True,"reader":getattr(provider,"name","reader"),
                "detail":f"Answered: plot_type '{obs.plot_type}', confidence {obs.confidence:.2f}"}
    if op=="literature.extractions":
        # Everything extracted from a paper before now. Each extraction already
        # writes a sidecar next to its dataset; nothing ever read them back, so
        # a session that ended took its extractions with it.
        from graphvis_science.literature.extractor import load_extraction_sidecars
        return {"ok": True, "extractions": load_extraction_sidecars()}
    if op=="literature.derender":
        # Trace one coloured series out of a figure and write it as a CSV
        # beside the image. This is the reason the de-renderer exists - getting
        # the numbers back out of a published plot - and it had no caller.
        from graphvis_science.literature.derender import ChartDerenderer
        from graphvis_science.literature.intelligence import LiteratureIntelligencePipeline
        figure = _figure_asset(req)
        colour = tuple(int(c) for c in (req.get("rgb") or (0, 0, 0)))[:3]
        if len(colour) != 3:
            return {"ok": False, "error": "rgb needs three channel values, 0-255"}
        pipeline = LiteratureIntelligencePipeline()
        try:
            frame = pipeline.derender_coloured_series(
                figure, colour, tolerance=float(req.get("tolerance") or 45.0),
                backend=str(req.get("backend") or "fastplotlib"))
        except (ValueError, OSError) as exc:
            return {"ok": False, "error": str(exc)}
        return {"ok": True, "rows": int(len(frame)),
                "columns": [str(c) for c in frame.columns],
                "csv_path": figure.reconstructed_data,
                "script_path": figure.reconstructed_code}
    if op=="literature.heatmap":
        # A heatmap or image panel as a numeric matrix, so a colour-mapped
        # figure becomes a dataset rather than a picture.
        from graphvis_science.literature.derender import ChartDerenderer
        box = req.get("bbox")
        if not box or len(box) != 4:
            return {"ok": False, "error": "bbox needs four pixel values: left, top, right, bottom"}
        derenderer = ChartDerenderer(str(req["image_path"]))
        matrix = derenderer.extract_heatmap_matrix(
            tuple(int(v) for v in box), width=int(req.get("width") or 160),
            height=int(req.get("height") or 120),
            grayscale=bool(req.get("grayscale", True)))
        return {"ok": True, "shape": [int(s) for s in matrix.shape],
                "values": matrix.tolist()}
    if op=="io.import":
        # Convert any supported scientific format into Arrow IPC. The native
        # core reads Arrow/CSV/Parquet itself; everything else arrives here.
        from graphvis_science.data.importer import import_to_arrow, ImportError_, ALL_EXT
        try:
            return import_to_arrow(req["path"], req.get("out_dir") or "")
        except ImportError_ as exc:
            return {"ok": False, "error": str(exc), "supported": list(ALL_EXT)}
    # ------------------------------------------------------------------ analysis
    # One dispatch for every analysis operation, declared in
    # graphvis_science/operations.py.
    #
    # This was seventy lines of hand-written if-blocks, and it advertised six
    # operations that did not exist - `psd`, `spectrogram`, `autocorrelation`,
    # `cluster`, `classify`, `regress` - because a getattr that misses looks
    # exactly like a feature not yet written. It also guessed at result field
    # names and returned empty replies when it guessed wrong. The registry is
    # checked by a test that resolves every target and runs every operation.
    if op == "analysis.catalogue":
        from graphvis_science.operations import catalogue
        return {"ok": True, "operations": catalogue()}

    if op.startswith("analysis."):
        from graphvis_science.operations import OperationError, run
        what = op.split(".", 1)[1]
        try:
            df = _load_frame(req["arrow_path"])
        except (KeyError, OSError) as exc:
            return {"ok": False, "error": f"could not read the dataset: {exc}"}
        try:
            payload = run(what, df, req)
        except OperationError as exc:
            return {"ok": False, "error": str(exc)}
        except Exception as exc:                      # noqa: BLE001
            # The engines raise ValueError for data they cannot work with -
            # too few points, a singular matrix - and that is an answer, not a
            # crash. It reaches the user as a sentence either way.
            return {"ok": False, "error": f"{what} failed: {exc}"}
        payload["ok"] = True
        return payload

    if op=="surface.estimate":
        # Scattered x/y/z -> a regular grid, using any of the sixteen estimators
        # ported from GraphVis 17 (Delaunay, natural neighbour, IDW, Shepard,
        # RBF, kriging, MLS, LOESS and the structured matrix methods).
        #
        # The grid comes back as an ordinary three-column Arrow file - x, y and
        # the estimated value, flattened - which is exactly the shape the native
        # field engines already read. The desktop needs no new geometry code for
        # this; it loads the result like any other dataset.
        import numpy as np
        from graphvis_science.analysis.surfaces import (interpolate_surface, ALL_ESTIMATORS,
                                                        ESTIMATOR_CATEGORIES)
        df=_load_frame(req["arrow_path"])
        for key in ("x","y","z"):
            if req.get(key) not in df.columns:
                return {"ok":False,"error":f"column '{req.get(key)}' is not in this dataset"}
        estimator=req.get("estimator") or "Auto (data-aware)"
        if estimator not in ALL_ESTIMATORS:
            return {"ok":False,"error":f"unknown estimator '{estimator}'","estimators":list(ALL_ESTIMATORS)}
        try:
            grid=interpolate_surface(
                df[req["x"]].to_numpy(dtype=float),
                df[req["y"]].to_numpy(dtype=float),
                df[req["z"]].to_numpy(dtype=float),
                resolution=int(req.get("resolution") or 160),
                estimator=estimator,
                smoothing=float(req.get("smoothing") or 0.0),
                x_log=bool(req.get("x_log")), y_log=bool(req.get("y_log")),
                neighbors=int(req.get("neighbors") or 32),
                idw_power=float(req.get("idw_power") or 2.0),
                loess_fraction=float(req.get("loess_fraction") or 0.25),
                kriging_variogram=req.get("kriging_variogram") or "Exponential",
            )
        except Exception as exc:
            return {"ok":False,"error":f"{estimator} failed: {exc}"}

        # A failed cell inside the data's own footprint is different from a cell
        # outside the convex hull: the first is a gap in a simulation sweep that
        # can reasonably be filled, the second is extrapolation. impute_surface_
        # invalid fills only the first, and records which cells it touched -
        # which is why it is offered here and why it is off unless asked for.
        imputation=str(req.get("imputation") or "")
        imputed=0
        if imputation:
            from graphvis_science.analysis.surfaces import IMPUTATION_MODES, impute_surface_invalid
            if imputation not in IMPUTATION_MODES:
                return {"ok":False,"error":f"unknown imputation '{imputation}'",
                        "imputation_modes":list(IMPUTATION_MODES)}
            before=np.ma.getmaskarray(grid.Z).sum()
            grid=impute_surface_invalid(grid, imputation)
            imputed=int(before-np.ma.getmaskarray(grid.Z).sum())

        # A masked cell is a cell with no defensible estimate - outside the
        # convex hull, say. It travels as NaN, which the engines already read
        # as a gap rather than as a zero.
        z=np.ma.filled(grid.Z.astype(float), np.nan)
        import pyarrow as pa, pyarrow.ipc as ipc, os as _os
        out_dir=req.get("out_dir") or _os.path.dirname(req["arrow_path"])
        _os.makedirs(out_dir, exist_ok=True)
        stem=_os.path.splitext(_os.path.basename(req["arrow_path"]))[0]
        out_path=_os.path.join(out_dir, f"{stem}-surface.arrow")
        table=pa.table({req["x"]: pa.array(np.asarray(grid.X).ravel()),
                        req["y"]: pa.array(np.asarray(grid.Y).ravel()),
                        req["z"]: pa.array(z.ravel())})
        with pa.OSFile(out_path,"wb") as sink:
            with ipc.new_file(sink, table.schema) as writer:
                writer.write_table(table)
        finite=np.isfinite(z)
        return {"ok":True,"path":out_path,"method":grid.method,
                "structured":bool(grid.structured),
                "nx":int(np.asarray(grid.X).shape[1]),"ny":int(np.asarray(grid.X).shape[0]),
                "cells":int(z.size),"estimated":int(finite.sum()),
                "imputation":imputation,"imputed":imputed,
                "notes":list(grid.notes or [])}

    if op=="surface.estimators":
        from graphvis_science.analysis.surfaces import ESTIMATOR_CATEGORIES, IMPUTATION_MODES
        return {"ok":True,
                "categories":[{"name":n,"estimators":list(v)} for n,v in ESTIMATOR_CATEGORIES],
                "imputation_modes":list(IMPUTATION_MODES)}

    # ------------------------------------------------------------------ figures
    # The .gvfig / .gvis package, ported from GraphVis 17. A figure that can be
    # reopened as a figure rather than only exported as a picture: the ZIP holds
    # the canvas state and the Arrow payloads it was drawn from.
    if op=="figure.save":
        from graphvis_science.data.figure import save_figure
        try:
            return save_figure(req["path"], req.get("spec") or {},
                               req.get("datasets") or [], req.get("metadata") or {})
        except OSError as exc:
            return {"ok":False,"error":f"could not write the figure: {exc}"}

    if op=="figure.load":
        from graphvis_science.data.figure import load_figure, FigureError
        try:
            return load_figure(req["path"], req.get("out_dir") or "")
        except FigureError as exc:
            return {"ok":False,"error":str(exc)}
        except OSError as exc:
            return {"ok":False,"error":f"could not read the figure: {exc}"}

    # ------------------------------------------------------------------- solver
    # The MATLAB Engine API path from GraphVis 17's sim_bridge. Every other
    # engine is a shell command and runs natively through QProcess; this one
    # cannot, because it runs the script in a live MATLAB session and reads the
    # workspace back with no files in between - which is the whole point of it
    # for a parameter sweep.
    if op=="solver.matlab":
        import os
        import numpy as np
        import pandas as pd
        script=req.get("script") or ""
        if not os.path.exists(script):
            return {"ok":False,"error":f"script not found: {script}"}
        try:
            import matlab.engine  # type: ignore
        except Exception:
            return {"ok":False,"error":
                    "The MATLAB Engine API for Python is not installed. Install it from your "
                    "MATLAB copy: cd matlabroot/extern/engines/python && python setup.py install. "
                    "The batch preset runs a .m file without it."}
        workdir=req.get("workdir") or os.path.dirname(script) or os.getcwd()
        engine=matlab.engine.start_matlab()
        try:
            engine.cd(workdir, nargout=0)
            engine.eval("run('%s')"%script.replace(os.sep,"/"), nargout=0)
            names=[str(v) for v in (engine.eval("who", nargout=1) or [])]
            series={}
            skipped=[]
            for var in names:
                try:
                    value=np.squeeze(np.asarray(engine.workspace[str(var)], dtype=float))
                except Exception:
                    # A struct, a cell array or a string. Named, not silently lost.
                    skipped.append(str(var)); continue
                if value.ndim==0:
                    series[str(var)]=np.asarray([float(value)])
                elif value.ndim==1:
                    series[str(var)]=value
                else:
                    # A sweep matrix becomes one column per column, which is what
                    # the mapping combos and the 3-D engines can actually bind to.
                    if value.ndim==2 and value.shape[1]<=64:
                        for i in range(value.shape[1]):
                            series[f"{var}_c{i+1}"]=value[:,i]
                    else:
                        skipped.append(f"{var} ({value.ndim}-D)")
        finally:
            try: engine.quit()
            except Exception: pass

        if not series:
            return {"ok":True,"arrow_path":"","columns":0,
                    "variables":names,"skipped":skipped}
        # Only the variables that share the longest length can be one table.
        # The rest are reported rather than padded into a misleading frame.
        length=max(v.size for v in series.values())
        columns={k:v for k,v in series.items() if v.size==length}
        skipped+= [f"{k} (length {v.size}, not {length})"
                   for k,v in series.items() if v.size!=length]

        import pyarrow as pa, pyarrow.ipc as ipc
        out_dir=req.get("out_dir") or workdir
        os.makedirs(out_dir, exist_ok=True)
        stem=os.path.splitext(os.path.basename(script))[0] or "matlab"
        out_path=os.path.join(out_dir, f"matlab_{stem}.arrow")
        table=pa.Table.from_pandas(pd.DataFrame(columns), preserve_index=False)
        with pa.OSFile(out_path,"wb") as sink:
            with ipc.new_file(sink, table.schema) as writer:
                writer.write_table(table)
        return {"ok":True,"arrow_path":out_path,"rows":int(length),
                "columns":len(columns),"variables":names,"skipped":skipped}

    # -------------------------------------------------------------------- batch
    # Folder-scale processing. scan lists what the importer can actually read in
    # a folder; run processes them and optionally writes a report.
    if op=="batch.scan":
        from graphvis_science.data import batch as batch_mod
        try:
            paths=batch_mod.scan(req["folder"], bool(req.get("recursive")))
        except (NotADirectoryError, OSError) as exc:
            return {"ok":False,"error":str(exc)}
        return {"ok":True,"paths":paths,"count":len(paths),
                "operations":list(batch_mod.OPERATIONS)}

    if op=="batch.run":
        from graphvis_science.data import batch as batch_mod
        paths=req.get("paths") or []
        if not paths:
            try:
                paths=batch_mod.scan(req["folder"], bool(req.get("recursive")))
            except (KeyError, NotADirectoryError, OSError) as exc:
                return {"ok":False,"error":f"nothing to process: {exc}"}
        if not paths:
            return {"ok":False,"error":"no readable datasets were found in that folder"}
        try:
            result=batch_mod.run(paths, req.get("operation") or "summary",
                                 req.get("out_dir") or "")
        except ValueError as exc:
            return {"ok":False,"error":str(exc)}
        report=""
        report_error=""
        if req.get("report_path"):
            try:
                report=batch_mod.write_report(result, req["report_path"],
                                              req.get("report_format") or "html",
                                              req.get("title") or "GraphVis Batch Report")
            except (RuntimeError, ValueError, OSError) as exc:
                # A missing report writer must not discard a run that worked.
                report_error=str(exc)
        return {"ok":True,
                "successes":result.successes,
                "failures":result.failures,
                "report":report,
                "report_error":report_error,
                "items":[{"path":i.path,"ok":i.ok,"summary":i.summary,"payload":i.payload}
                         for i in result.items]}

    if op=="io.formats":
        from graphvis_science.data.importer import ALL_EXT, format_groups
        return {"ok": True, "extensions": list(ALL_EXT), "groups": format_groups()}
    if op=="dataset.scan":
        # Smart Suite / Scan Dataset. intelligent_scan.scan_dataset expects a
        # GraphVis 17 Dataset object; the native app has an Arrow file, so adapt
        # the few attributes the scanner actually reads.
        from graphvis_science.analysis.intelligent_scan import scan_dataset

        class _ArrowDataset:
            def __init__(self, path: str, name: str, df):
                self.path = path
                self.name = name
                self.df = df
                self.numeric_columns = [str(c) for c in df.select_dtypes("number").columns]
                self.matrices = {}
                self.volumes = {}

        class _LiteratureContext:
            # intelligent_scan reads literature entries with getattr(.title,
            # .semantic_context), so the JSON the app sends is wrapped rather
            # than passed as bare dicts.
            def __init__(self, entry: dict):
                self.title = str(entry.get("title") or "Literature")
                self.semantic_context = dict(entry.get("semantic_context") or {})

        df = _load_frame(req["arrow_path"])
        ds = _ArrowDataset(req["arrow_path"], req.get("name") or "dataset", df)
        lit = [_LiteratureContext(e) for e in (req.get("literature") or []) if isinstance(e, dict)]
        budget = float(req.get("budget_seconds") or 30.0)

        # A scan's budget goes up to three days, and a scan that is interrupted
        # after two of them should not start again from nothing. The scanner has
        # always taken a checkpoint callback and a resume state; nothing passed
        # them, so every interrupted scan was lost. With a cache directory in the
        # request - the application sends its own scan-cache folder - the work is
        # written as it goes and picked up on the next attempt.
        resume, checkpoint, cache_dir = None, None, str(req.get("cache_dir") or "")
        if cache_dir:
            from graphvis_science.analysis.intelligent_scan import (
                clear_scan_checkpoint, load_scan_checkpoint, save_scan_checkpoint)
            try:
                resume = load_scan_checkpoint(ds, cache_dir, lit or None)
            except Exception:                          # noqa: BLE001
                resume = None                          # a bad checkpoint is not a failed scan

            def checkpoint(state):                     # noqa: F811 - deliberate rebind
                try:
                    save_scan_checkpoint(state, cache_dir, ds, lit or None)
                except Exception:                      # noqa: BLE001
                    pass

        result = scan_dataset(ds, lit or None, time_budget_seconds=budget,
                              resume_state=resume, checkpoint=checkpoint)
        if cache_dir:
            clear_scan_checkpoint(ds, cache_dir, lit or None)
        return {"ok": True, "scan": result, "literature_used": len(lit),
                "resumed": bool(resume)}
    if op=="cache.clear":
        # The service is long-lived, and the surface estimators keep a geometry
        # cache - triangulations and neighbour graphs - that can hold tens of
        # megabytes per dataset. Clearing the application's scan cache clears
        # this one too, which is the only way it was ever going to be released
        # short of restarting the service.
        from graphvis_science.analysis.surfaces import clear_geometry_cache
        clear_geometry_cache()
        return {"ok": True, "cleared": "geometry"}
    # "fit.modified_gompertz" used to be here and has been removed. It was both
    # unreachable AND broken: it imported `fit_modified_gompertz`, which does
    # not exist in analysis/fitting.py and has not for some time. Calling it
    # raised ImportError, and nothing ever called it, so nothing noticed - which
    # is the whole hazard of an operation with no caller.
    #
    # Nothing is lost. "modified gompertz" is a registered model, so the fit is
    # reachable the way every other model is:
    #
    #     {"op": "analysis.run", "kind": "fit_model", "model": "modified gompertz", ...}
    #
    # One route to a thing, and it is the route the Analysis panel already uses.
    raise KeyError(f"Unknown operation: {op}")


def main() -> int:
    for line in sys.stdin:
        try:
            req=json.loads(line)
            # Whatever a library decides to print goes to stderr, where it is a
            # diagnostic. stdout carries the protocol and nothing else.
            with contextlib.redirect_stdout(sys.stderr):
                res=dispatch(req)
        except Exception as exc:
            res={"ok":False,"error":f"{type(exc).__name__}: {exc}","traceback":traceback.format_exc(limit=6)}
        _PROTOCOL.write(json.dumps(res,default=str,separators=(",",":"))+"\n");_PROTOCOL.flush()
    return 0

if __name__=="__main__": raise SystemExit(main())
