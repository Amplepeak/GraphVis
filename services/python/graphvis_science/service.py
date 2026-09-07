from __future__ import annotations
import json, sys, traceback
from pathlib import Path


def _load_frame(path: str):
    import pyarrow.ipc as ipc
    import pyarrow as pa
    p=Path(path)
    with pa.memory_map(str(p), "r") as source:
        table=ipc.open_file(source).read_all()
    return table.to_pandas(types_mapper=None)


def dispatch(req: dict) -> dict:
    op=req.get("op", "health")
    if op=="health":
        return {"ok":True,"service":"graphvis-python-science","version":"18.4.0","transport":"Arrow IPC + JSON control"}
    if op=="statistics.describe":
        df=_load_frame(req["arrow_path"])
        cols=req.get("columns") or list(df.select_dtypes("number").columns)
        return {"ok":True,"result":df[cols].describe().to_dict()}
    if op=="literature.extract":
        from graphvis_science.literature.extractor import extract_literature
        result=extract_literature(req["path"])
        datasets=[]
        for d in getattr(result,"datasets",[]):
            datasets.append({"name":getattr(d,"name","dataset"),"rows":len(d.df),"columns":list(map(str,d.df.columns))})
        return {"ok":True,"title":getattr(result,"title",""),"datasets":datasets,"text_chars":len(getattr(result,"text","")),"saved_paths":list(getattr(result,"saved_paths",[]) or []),"parameters":dict(getattr(result,"parameters",{}) or {}),"semantic_context":dict(getattr(result,"semantic_context",{}) or {}),"warnings":list(getattr(result,"warnings",[]) or [])}
    if op=="io.import":
        # Convert any supported scientific format into Arrow IPC. The native
        # core reads Arrow/CSV/Parquet itself; everything else arrives here.
        from graphvis_science.data.importer import import_to_arrow, ImportError_, ALL_EXT
        try:
            return import_to_arrow(req["path"], req.get("out_dir") or "")
        except ImportError_ as exc:
            return {"ok": False, "error": str(exc), "supported": list(ALL_EXT)}
    # ------------------------------------------------------------------ analysis
    # These modules were written, tested and imported cleanly, and had no route
    # from the application: the service exposed no operation for any of them and
    # AnalysisPanel.qml carried three permanently disabled buttons whose own
    # tooltips said the wiring was "intentionally disabled until an operation
    # contract is selected". This is that contract.
    #
    # One shape for all of them: name the Arrow file and the columns, get JSON
    # back. Anything returning a curve returns it as x/y arrays the plot
    # catalogue can draw directly.
    if op.startswith("analysis."):
        import numpy as np
        import pandas as pd
        what=op.split(".",1)[1]
        df=_load_frame(req["arrow_path"])

        def column(key, required=True):
            name=req.get(key)
            if name is None and not required: return None
            if name not in df.columns:
                raise KeyError(f"column '{name}' is not in this dataset")
            return df[name].to_numpy(dtype=float)

        def numbers(a):
            """JSON has no NaN. Non-finite becomes null, which every consumer
            here already reads as a gap rather than as a zero.

            Vectorised deliberately. Written as
            ``[None if not np.isfinite(v) else float(v) for v in arr]`` this
            dispatched a NumPy ufunc per element, and on a 200k-row PCA that
            one line was 94% of the whole operation - 2.1 s of a 2.3 s call,
            against 0.1 s for the PCA itself. isfinite runs once over the
            array, tolist() converts in C, and the null patching touches only
            the elements that need it - usually none."""
            if a is None: return []
            arr=np.asarray(a, dtype=float).ravel()
            out=arr.tolist()
            finite=np.isfinite(arr)
            if not finite.all():
                for i in np.flatnonzero(~finite):
                    out[int(i)]=None
            return out

        def frame_out(f):
            """A DataFrame as columns plus column-wise rows. Several of these
            engines return their real answer as a table, not as scalars."""
            if f is None or not isinstance(f, pd.DataFrame) or f.empty:
                return {"columns":[], "rows":{}}
            out={}
            for c in f.columns:
                col=f[c]
                if pd.api.types.is_numeric_dtype(col):
                    out[str(c)]=numbers(col.to_numpy())
                else:
                    # Same reason as numbers(): one vectorised isna instead of
                    # a pandas call per element.
                    values=col.astype(str).tolist()
                    missing=col.isna().to_numpy()
                    if missing.any():
                        for i in np.flatnonzero(missing):
                            values[int(i)]=None
                    out[str(c)]=values
            return {"columns":[str(c) for c in f.columns], "rows":out}

        def scalars(d):
            out={}
            for k,v in (d or {}).items():
                try: out[str(k)]=None if v is None or not np.isfinite(float(v)) else float(v)
                except (TypeError, ValueError): out[str(k)]=str(v)
            return out

        try:
            if what=="limits":
                from graphvis_science.analysis.limits import analyse_limits
                r=analyse_limits(column("x"), column("y"), req.get("options"))
                feats=[]
                for f in (r.features or []):
                    # A feature is a horizontal/vertical rule, a band, a curve or
                    # a point - each carries a different subset of the fields.
                    feats.append({"kind":f.kind,"label":f.label,"group":getattr(f,"group",""),
                                  "value":(None if f.value is None else float(f.value)),
                                  "x":numbers(f.x),"y":numbers(f.y),
                                  "lo":numbers(f.lo),"hi":numbers(f.hi)})
                return {"ok":True,"features":feats,
                        "summary":list(r.summary or []),"notes":list(r.notes or []),
                        "text":r.text()}

            if what=="forecast":
                from graphvis_science.analysis.predictive import forecast_series
                b=forecast_series(column("x"), column("y"),
                                  extension=float(req.get("extension") or 0.20),
                                  confidence=float(req.get("confidence") or 0.95))
                return {"ok":True,"model":b.model,"r2":float(b.r2),
                        "x":numbers(b.x),"y":numbers(b.y),
                        "lower":numbers(b.lower),"upper":numbers(b.upper),
                        "original_x_max":float(b.original_x_max),
                        "extrapolated":numbers(b.extrapolated)}

            if what in ("fft","psd","spectrogram","autocorrelation"):
                from graphvis_science.analysis.signal import SignalEngine
                fn=getattr(SignalEngine, what, None)
                if fn is None:
                    return {"ok":False,"error":f"signal operation '{what}' is not available",
                            "operations":[m for m in dir(SignalEngine) if not m.startswith("_")]}
                r=fn(column("y"), column("x", required=False))
                return {"ok":True,"name":r.name,"x":numbers(r.x),"y":numbers(r.y),
                        "table":frame_out(r.table),"details":scalars(r.details)}

            if what=="weibull":
                from graphvis_science.analysis.reliability import ReliabilityEngine
                r=ReliabilityEngine.weibull(column("y"), confidence=float(req.get("confidence") or 0.95))
                return {"ok":True,"name":r.name,"table":frame_out(r.table),
                        "details":scalars(r.details)}

            if what=="regression":
                from graphvis_science.analysis.regression import RegressionEngine
                preds=req.get("predictors") or []
                if not preds: return {"ok":False,"error":"regression needs at least one predictor column"}
                missing=[c for c in [req.get("y"),*preds] if c not in df.columns]
                if missing: return {"ok":False,"error":f"columns not in this dataset: {', '.join(missing)}"}
                r=RegressionEngine.linear(df, req["y"], preds, robust=bool(req.get("robust")))
                # coefficients is a DataFrame, not a mapping - treating it as one
                # is what raised "The truth value of a DataFrame is ambiguous".
                return {"ok":True,"name":r.name,"coefficients":frame_out(r.coefficients),
                        "metrics":scalars(r.metrics),
                        "predictions":numbers(r.predictions),"residuals":numbers(r.residuals)}

            if what in ("pca","cluster","classify","regress"):
                from graphvis_science.analysis.ml import MultivariateEngine
                fn=getattr(MultivariateEngine, what, None)
                if fn is None:
                    return {"ok":False,"error":f"multivariate operation '{what}' is not available",
                            "operations":[m for m in dir(MultivariateEngine) if not m.startswith("_")]}
                r=(fn(df, req.get("columns"), n_components=req.get("components"),
                      scale=bool(req.get("scale",True))) if what=="pca" else fn(df, req.get("columns")))
                return {"ok":True,"name":r.name,"table":frame_out(r.table),
                        "scores":frame_out(r.scores),"loadings":frame_out(r.loadings),
                        "metrics":scalars(r.metrics),"details":scalars(r.details)}

            if what=="doe":
                from graphvis_science.analysis.doe import DOEEngine
                bounds={k:(float(v[0]),float(v[1])) for k,v in (req.get("bounds") or {}).items()}
                if not bounds: return {"ok":False,"error":"a design needs at least one named bound"}
                kind=(req.get("design") or "latin_hypercube")
                fn=getattr(DOEEngine, kind, None)
                if fn is None:
                    return {"ok":False,"error":f"unknown design '{kind}'",
                            "designs":[m for m in dir(DOEEngine) if not m.startswith("_")]}
                r=fn(bounds, int(req.get("runs") or 16))
                return {"ok":True,"kind":r.kind,"design":frame_out(r.design),
                        "diagnostics":scalars(r.diagnostics)}

            if what=="advisor":
                from graphvis_science.analysis.electro import intelligent_visualization_advisor
                return {"ok":True,"recommendations":[{"engine":c,"why":w}
                                                     for c,w in intelligent_visualization_advisor(df)]}

            if what=="domain":
                from graphvis_science.analysis.electro import detect_domain_data
                return {"ok":True,"domains":detect_domain_data(df)}

            return {"ok":False,"error":f"unknown analysis operation '{what}'",
                    "operations":["limits","forecast","fft","psd","weibull","regression",
                                  "pca","doe","advisor","domain"]}
        except KeyError as exc:
            return {"ok":False,"error":str(exc).strip("'")}
        except Exception as exc:
            return {"ok":False,"error":f"{what} failed: {exc}"}

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
                "notes":list(grid.notes or [])}

    if op=="surface.estimators":
        from graphvis_science.analysis.surfaces import ESTIMATOR_CATEGORIES
        return {"ok":True,"categories":[{"name":n,"estimators":list(v)} for n,v in ESTIMATOR_CATEGORIES]}

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
        result = scan_dataset(ds, lit or None,
                              time_budget_seconds=float(req.get("budget_seconds") or 30.0))
        return {"ok": True, "scan": result, "literature_used": len(lit)}
    if op=="fit.modified_gompertz":
        from graphvis_science.analysis.fitting import fit_modified_gompertz
        df=_load_frame(req["arrow_path"]); out=fit_modified_gompertz(df[req["x"]].to_numpy(float),df[req["y"]].to_numpy(float))
        return {"ok":True,"result":out if isinstance(out,dict) else getattr(out,"__dict__",str(out))}
    raise KeyError(f"Unknown operation: {op}")


def main() -> int:
    for line in sys.stdin:
        try:
            req=json.loads(line); res=dispatch(req)
        except Exception as exc:
            res={"ok":False,"error":f"{type(exc).__name__}: {exc}","traceback":traceback.format_exc(limit=6)}
        sys.stdout.write(json.dumps(res,default=str,separators=(",",":"))+"\n");sys.stdout.flush()
    return 0

if __name__=="__main__": raise SystemExit(main())
