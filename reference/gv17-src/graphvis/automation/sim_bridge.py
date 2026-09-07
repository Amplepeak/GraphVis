# =========================================================================
# sim_bridge.py — universal simulation execution + direct data ingestion.
#
# Engine-agnostic wrappers run external computational tools (Python scripts,
# ANSYS MAPDL, COMSOL Multiphysics, AQUASIM, or any CLI mathematical engine)
# in a cancellable background task, then intercept the files each run
# produced — coordinate matrices from parameter sweeps, time series, grid
# arrays, point clouds — and hand them to GraphVis's normal dataset loader so
# they bind instantly to the interactive controls, axis sliders, clipping
# tools and 3-D rendering engine.
#
# The bridge deliberately shells out through subprocess with user-editable
# command templates, so no single vendor ecosystem is hard-wired.
# =========================================================================
from __future__ import annotations

import glob
import os
import subprocess
import sys
import time

import numpy as np
import pandas as pd

from graphvis.data.loader import CancellableTask, Dataset, load_dataset

# {script} → the chosen input/model file, {workdir} → the working directory.
ENGINE_PRESETS: dict[str, dict] = {
    "MATLAB Engine API (in-process)": {
        "command": "(runs via the MATLAB Engine API — no shell command)",
        "hint": "Requires MathWorks' 'matlab.engine' Python package (installed from your MATLAB copy). "
                "Runs the .m script in a shared engine and pulls every numeric workspace variable "
                "(scalars, vectors, N-D sweep matrices) directly into GraphVis — no intermediate files.",
        "outputs": "*.csv;*.mat",
        "api": "matlab",
    },
    "Python script": {
        "command": f'"{sys.executable}" "{{script}}"',
        "hint": "Any Python model/ODE solver. Write results to CSV/NPY/MAT files in the working directory.",
        "outputs": "*.csv;*.npy;*.npz;*.mat;*.json",
    },
    "ANSYS MAPDL (batch)": {
        "command": 'mapdl -b -i "{script}" -o "{workdir}/mapdl_run.out"',
        "hint": "Requires ANSYS MAPDL on PATH. Export tabular results with /OUTPUT or *VWRITE to CSV.",
        "outputs": "*.csv;*.txt;*.out",
    },
    "COMSOL Multiphysics (batch)": {
        "command": 'comsolbatch -inputfile "{script}"',
        "hint": "Requires COMSOL on PATH. Add an Export node writing CSV/TXT data files.",
        "outputs": "*.csv;*.txt",
    },
    "AQUASIM (command line)": {
        "command": 'aquasimc "{script}"',
        "hint": "Requires the AQUASIM CLI. Configure result list files as plain-text output.",
        "outputs": "*.txt;*.csv;*.lis",
    },
    "Generic command / other engine": {
        "command": '"{script}"',
        "hint": "Any executable or shell command. Edit the template freely; {script} and {workdir} are substituted.",
        "outputs": "*.csv;*.txt;*.json;*.npy;*.mat",
    },
}


def _load_numpy_artifact(path: str) -> Dataset:
    """Turn .npy/.npz arrays into a GraphVis Dataset (series or matrices)."""
    name = os.path.splitext(os.path.basename(path))[0]
    frames: dict[str, np.ndarray] = {}
    if path.lower().endswith(".npz"):
        with np.load(path, allow_pickle=False) as bundle:
            frames = {k: np.asarray(bundle[k]) for k in bundle.files}
    else:
        frames = {name: np.asarray(np.load(path, allow_pickle=False))}
    series: dict[str, np.ndarray] = {}
    matrices: dict[str, np.ndarray] = {}
    for key, arr in frames.items():
        arr = np.asarray(arr, dtype=float)
        if arr.ndim == 1:
            series[key] = arr
        elif arr.ndim == 2 and 1 in arr.shape:
            series[key] = arr.ravel()
        elif arr.ndim == 2:
            # Either a pre-gridded response matrix or an N×k point cloud.
            if arr.shape[1] <= 6 and arr.shape[0] > arr.shape[1]:
                for i in range(arr.shape[1]):
                    series[f"{key}_c{i + 1}"] = arr[:, i]
            else:
                matrices[key] = arr
        else:
            matrices[key] = arr
    length = max((v.size for v in series.values()), default=0)
    df = pd.DataFrame({k: pd.Series(v) for k, v in series.items() if v.size == length}) if series else pd.DataFrame()
    return Dataset(name, path, df, matrices=matrices)


def ingest_output_file(path: str) -> Dataset:
    """Parse one engine-produced file into a Dataset via the normal loaders."""
    low = path.lower()
    if low.endswith((".npy", ".npz")):
        return _load_numpy_artifact(path)
    if low.endswith((".out", ".lis")):
        # Engine logs with embedded tables: try the delimited-text loader.
        return load_dataset(os.path.splitext(path)[0] + ".txt") if os.path.exists(
            os.path.splitext(path)[0] + ".txt") else load_dataset(path)
    return load_dataset(path)


class MatlabEngineTask(CancellableTask):
    """Run a .m script through the MATLAB Engine API and pull the workspace.

    Multi-dimensional matrices, vectors and scalars land directly in a
    GraphVis Dataset — zero file round-trip — so parameter-sweep outputs bind
    immediately to the mapping combos, clipping sliders and 3-D renderer.
    """

    def __init__(self, script: str, workdir: str = ""):
        super().__init__(f"matlab-engine:{os.path.basename(script)}")
        self.script = str(script)
        self.workdir = str(workdir or os.path.dirname(script) or os.getcwd())

    def execute(self) -> dict:
        started = time.time()
        self.stage("Starting MATLAB engine…", 5)
        try:
            import matlab.engine  # type: ignore
        except Exception as exc:
            raise RuntimeError(
                "The MATLAB Engine API for Python is not installed. Install it from your MATLAB copy "
                "(cd matlabroot/extern/engines/python && python setup.py install).") from exc
        eng = matlab.engine.start_matlab()
        try:
            if self.cancelled:
                raise Exception("Cancelled")
            eng.cd(self.workdir, nargout=0)
            self.stage(f"Running {os.path.basename(self.script)}…", 30)
            name = os.path.splitext(os.path.basename(self.script))[0]
            eng.eval(f"run('{self.script.replace(os.sep, '/')}')", nargout=0)
            if self.cancelled:
                raise Exception("Cancelled")
            self.stage("Pulling workspace variables…", 75)
            var_names = [str(v) for v in (eng.eval("who", nargout=1) or [])]
            series: dict[str, np.ndarray] = {}
            matrices: dict[str, np.ndarray] = {}
            skipped: list[str] = []
            for var in var_names:
                try:
                    value = np.asarray(eng.workspace[str(var)], dtype=float)
                except Exception:
                    skipped.append(str(var))
                    continue
                value = np.squeeze(value)
                if value.ndim == 0:
                    series[str(var)] = np.asarray([float(value)])
                elif value.ndim == 1:
                    series[str(var)] = value
                else:
                    matrices[str(var)] = value
            length = max((v.size for v in series.values()), default=0)
            df = pd.DataFrame({k: pd.Series(v) for k, v in series.items()
                               if v.size == length and length > 1}) if series else pd.DataFrame()
            aux = {k: v for k, v in series.items() if v.size != length or length <= 1}
            ds = Dataset(f"matlab_{name}", self.script, df, matrices=matrices, aux=aux)
            self.stage("MATLAB bridge complete", 100)
            return {"returncode": 0, "stdout_tail": f"Workspace variables: {', '.join(var_names) or 'none'}",
                    "datasets": [ds] if (not df.empty or matrices or aux) else [],
                    "files": [], "errors": [f"non-numeric variable skipped: {v}" for v in skipped],
                    "seconds": round(time.time() - started, 2)}
        finally:
            try:
                eng.quit()
            except Exception:
                pass


class SimulationRunTask(CancellableTask):
    """Run an external engine command and collect its fresh output files."""

    def __init__(self, command: str, workdir: str, script: str = "",
                 output_patterns: str = "*.csv", timeout_seconds: float = 3600.0):
        super().__init__(f"sim-bridge:{os.path.basename(script) or 'command'}")
        self.command = str(command)
        self.workdir = str(workdir or os.getcwd())
        self.script = str(script or "")
        self.output_patterns = [p.strip() for p in str(output_patterns or "*.csv").split(";") if p.strip()]
        self.timeout_seconds = max(5.0, float(timeout_seconds))

    def _resolved_command(self) -> str:
        return self.command.replace("{script}", self.script).replace("{workdir}", self.workdir)

    def execute(self) -> dict:
        started = time.time()
        cmd = self._resolved_command()
        self.stage(f"Launching engine: {cmd[:120]}…", 5)
        proc = subprocess.Popen(cmd, cwd=self.workdir, shell=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, errors="replace")
        tail: list[str] = []
        while True:
            if self.cancelled:
                proc.kill()
                raise Exception("Cancelled")
            if time.time() - started > self.timeout_seconds:
                proc.kill()
                raise RuntimeError(f"Engine run exceeded the {self.timeout_seconds:.0f}s time limit.")
            line = proc.stdout.readline() if proc.stdout else ""
            if line:
                tail.append(line.rstrip())
                tail = tail[-40:]
                self.stage(f"Engine: {line.strip()[:90]}", 40)
            elif proc.poll() is not None:
                break
            else:
                time.sleep(0.05)
        code = int(proc.returncode or 0)
        self.stage("Collecting simulation outputs…", 80)
        fresh: list[str] = []
        for pattern in self.output_patterns:
            for path in glob.glob(os.path.join(self.workdir, pattern)):
                try:
                    if os.path.getmtime(path) >= started - 1.0:
                        fresh.append(path)
                except OSError:
                    continue
        datasets: list[Dataset] = []
        errors: list[str] = []
        for path in sorted(set(fresh)):
            if self.cancelled:
                raise Exception("Cancelled")
            try:
                ds = ingest_output_file(path)
                if (ds.df is not None and not ds.df.empty) or ds.matrices or getattr(ds, "volumes", None):
                    datasets.append(ds)
            except Exception as exc:
                errors.append(f"{os.path.basename(path)}: {exc}")
        self.stage("Simulation bridge complete", 100)
        return {"returncode": code, "stdout_tail": "\n".join(tail), "datasets": datasets,
                "files": sorted(set(fresh)), "errors": errors,
                "seconds": round(time.time() - started, 2)}
