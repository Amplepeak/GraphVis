# =========================================================================
# data_loader.py — .mat / CSV ingestion, alias labels, canonical keys,
# on-disk parse cache, and QThreadPool task plumbing.
# =========================================================================
from __future__ import annotations

import json
import os
import pickle
import re
import tempfile
import warnings
from pathlib import Path

import h5py
import numpy as np
import pandas as pd
from PySide6.QtCore import QObject, QRunnable, QThreadPool, Signal
from scipy import stats

warnings.filterwarnings("ignore", category=RuntimeWarning)

from graphvis.core.paths import (ALIAS_CONFIG_FILE as _ALIAS_PATH, CACHE_DIR as _CACHE_PATH,
                                 LITERATURE_DATASET_DIR as _LIT_PATH, ensure_layout)
ensure_layout()
APP_DIR = os.path.dirname(os.path.abspath(__file__))
ALIAS_CONFIG_FILE = str(_ALIAS_PATH)
CACHE_DIR = str(_CACHE_PATH)
LITERATURE_DATASET_DIR = str(_LIT_PATH)
os.makedirs(CACHE_DIR, exist_ok=True)
os.makedirs(LITERATURE_DATASET_DIR, exist_ok=True)
CACHE_VERSION = "v10_lazy_hdf"


class HDFArrayProxy:
    """Pickle-friendly lazy numeric HDF5/MAT-v7.3 array reference.

    Large 2-D/3-D datasets remain on disk until a graph actually selects them.
    Small 1-D coordinate vectors are still loaded eagerly because they are
    needed for the dataframe/mapping UI.
    """
    def __init__(self, filepath: str, dataset_name: str, shape, dtype, *, squeeze: bool = True, transpose: bool = False):
        self.filepath = str(filepath)
        self.dataset_name = str(dataset_name)
        base_shape = tuple(int(x) for x in shape)
        if squeeze:
            base_shape = tuple(x for x in base_shape if x != 1) or (1,)
        self._shape = base_shape[::-1] if transpose else base_shape
        self._dtype = np.dtype(dtype)
        self.squeeze = bool(squeeze)
        self.transpose = bool(transpose)

    @property
    def shape(self):
        return self._shape

    @property
    def ndim(self):
        return len(self._shape)

    @property
    def size(self):
        return int(np.prod(self._shape, dtype=np.int64))

    @property
    def dtype(self):
        return self._dtype

    @property
    def T(self):
        return HDFArrayProxy(self.filepath, self.dataset_name, self._shape[::-1] if self.transpose else self._shape,
                             self._dtype, squeeze=False, transpose=not self.transpose)

    def _read(self, selection=None):
        with h5py.File(self.filepath, 'r') as fh:
            ds = fh[self.dataset_name]
            arr = np.asarray(ds[...] if selection is None else ds[selection])
        if self.squeeze:
            arr = np.squeeze(arr)
        if self.transpose:
            arr = arr.T
        return arr

    def __array__(self, dtype=None, copy=None):
        arr = self._read()
        if dtype is not None:
            arr = arr.astype(dtype, copy=False)
        elif copy:
            arr = arr.copy()
        return arr

    def __getitem__(self, item):
        # Generic slicing keeps semantics correct. Most GraphVis consumers
        # materialise only after a user selects this array, so the common path
        # still avoids startup I/O.
        return np.asarray(self)[item]

    def astype(self, dtype, copy=True):
        return np.asarray(self).astype(dtype, copy=copy)

    def __repr__(self):
        return f"HDFArrayProxy({Path(self.filepath).name!r}, {self.dataset_name!r}, shape={self.shape}, dtype={self.dtype})"


def _array_shape(value) -> tuple:
    shape = getattr(value, 'shape', None)
    return tuple(shape) if shape is not None else tuple(np.asarray(value).shape)


def _array_ndim(value) -> int:
    ndim = getattr(value, 'ndim', None)
    return int(ndim) if ndim is not None else int(np.asarray(value).ndim)

# Keys that must always be offered in every dropdown / matrix selector /
# gradient field, in this order. When a dataset lacks one, the combo still
# shows it (disabled) so the workflow is identical across runs.
PRIORITY_KEYS = ["vhpr", "sCOD", "VFA", "applied_voltage", "lhs_kdm"]

# canonical key -> substrings that identify it in a raw .mat variable name
_CANONICAL_PATTERNS = {
    "vhpr": ["vhpr"],
    "sCOD": ["scod", "s_cod", "cod"],
    "VFA": ["vfa"],
    "applied_voltage": ["applied_voltage", "e_app", "eapp", "applied_v", "v_app"],
    "lhs_kdm": ["kdm", "k_dm", "kd_m"],
}

DEFAULT_ALIASES = {
    'lhs_flow': 'Flow Rate / Dilution [day⁻¹] (dil)',
    'lhs_area': 'Anodic Surface Area [m²] (a_sur_A)',
    'lhs_scod': 'Influent sCOD [mg/L] (si)',
    'lhs_kdm': 'Decay Rate Constant k_dm [day⁻¹]',
    'applied_voltage': 'Applied Voltage [V]',
    'vhpr': 'Volumetric Hydrogen Production Rate [m³ H₂ m⁻³ day⁻¹] (vhpr)',
    'sCOD': 'Soluble COD [mg/L]',
    'VFA': 'Volatile Fatty Acids [mg/L]',
    'total_VHPR_mean': 'Total System VHPR',
    'df_VHPR_mean': 'Dark Fermentation VHPR',
    'mec_VHPR_mean': 'MEC Stage VHPR',
    'scod_profile': 'sCOD Degradation Profile [mg/L]',
    'vfa_profile': 'VFA Concentration Profile [mg/L]',
}


# ------------------------------------------------------------- aliases
def load_variable_aliases() -> dict:
    if os.path.exists(ALIAS_CONFIG_FILE):
        try:
            with open(ALIAS_CONFIG_FILE, 'r', encoding='utf-8') as fh:
                data = json.load(fh)
            if isinstance(data, dict):
                merged = DEFAULT_ALIASES.copy()
                merged.update(data)
                return merged
        except Exception:
            pass
    save_variable_aliases(DEFAULT_ALIASES)
    return DEFAULT_ALIASES.copy()


def save_variable_aliases(alias_dict: dict) -> bool:
    try:
        with open(ALIAS_CONFIG_FILE, 'w', encoding='utf-8') as fh:
            json.dump(alias_dict, fh, indent=4, ensure_ascii=False)
        return True
    except Exception:
        return False


_ALIAS_CACHE: dict | None = None
_CONTEXT_ALIASES: dict[str, str] = {}


def set_context_aliases(mapping: dict | None):
    """Install transient code-context names without writing them to disk."""
    global _CONTEXT_ALIASES
    _CONTEXT_ALIASES = {str(k): str(v) for k, v in (mapping or {}).items() if v}



def refresh_alias_cache():
    global _ALIAS_CACHE
    _ALIAS_CACHE = load_variable_aliases()


def get_pretty_label(raw_key) -> str:
    if not raw_key:
        return ""
    global _ALIAS_CACHE
    if _ALIAS_CACHE is None:
        refresh_alias_cache()
    key = str(raw_key)
    if key in _CONTEXT_ALIASES:
        return _CONTEXT_ALIASES[key]
    if key in _ALIAS_CACHE:
        return _ALIAS_CACHE[key]
    stem = key.split('/')[-1]
    return _ALIAS_CACHE.get(stem, stem.replace('lhs_', '').replace('_', ' ').strip().title())


# ------------------------------------------------------------- dataset
class Dataset:
    def __init__(self, name, path, df, matrices=None, meta=None, aux=None, volumes=None, tensors=None, text_data=None, topology=None):
        self.name = name
        self.path = path
        self.df = df if df is not None else pd.DataFrame()
        self.matrices = matrices or {}
        self.volumes = volumes or {}          # name -> 3-D scalar/vector component array
        self.tensors = tensors or {}          # name -> arrays with ndim > 3
        self.aux = aux or {}
        self.text_data = text_data or {}      # source text/categorical payloads for word clouds
        self.topology = topology or {}        # optional vertices/faces for patch rendering
        self.meta = meta or {}
        self.meta.setdefault("assumed", False)
        self.meta.setdefault("assumption_note", "")
        self.meta.setdefault("units", {})
        self.meta.setdefault("variable_context", {})
        self.meta.setdefault("revision", 0)
        self.canonical = self.meta.get("canonical") or detect_canonical_columns(self.df, self.matrices)

    @property
    def units(self) -> dict:
        return self.meta.setdefault("units", {})

    @property
    def variable_context(self) -> dict:
        return self.meta.setdefault("variable_context", {})

    @property
    def assumed(self) -> bool:
        return bool(self.meta.get("assumed", False))

    @assumed.setter
    def assumed(self, value: bool):
        new_value = bool(value)
        if bool(self.meta.get("assumed", False)) != new_value:
            self.meta["assumed"] = new_value
            self.touch()
        else:
            self.meta["assumed"] = new_value

    @property
    def assumption_note(self) -> str:
        return str(self.meta.get("assumption_note", "") or "")

    @property
    def numeric_columns(self):
        return [c for c in self.df.columns if pd.api.types.is_numeric_dtype(self.df[c])]

    @property
    def parameter_columns(self):
        return [c for c in self.numeric_columns if c.startswith('lhs_') or c in ('applied_voltage',)]

    @property
    def response_columns(self):
        keys = ('vhpr', 'yield', 'scod', 'vfa', 'energy', 'cost', 'h2')
        out = [c for c in self.numeric_columns if c not in self.parameter_columns and any(k in c.lower() for k in keys)]
        return out or [c for c in self.numeric_columns if c not in self.parameter_columns]

    def stage_targets(self):
        found = {}
        for col in self.numeric_columns:
            low = col.lower()
            if 'vhpr' not in low:
                continue
            if low.startswith('df') or 'dark' in low or '_df' in low:
                found.setdefault('Dark Fermentation', col)
            elif 'mec' in low:
                found.setdefault('MEC Stage', col)
            elif 'total' in low:
                found.setdefault('Total System', col)
        return found

    @property
    def revision(self) -> int:
        return int(self.meta.get("revision", 0) or 0)

    def touch(self) -> int:
        """Mark in-memory scientific data as changed and invalidate render caches."""
        self.meta["revision"] = self.revision + 1
        return self.revision

    def cache_key(self) -> tuple:
        """Identity used by render caches; includes file and in-memory revision."""
        try:
            mtime = os.path.getmtime(self.path) if self.path and os.path.exists(self.path) else 0.0
        except OSError:
            mtime = 0.0
        shapes = tuple(sorted((k, _array_shape(v)) for k, v in {**self.matrices, **self.volumes, **self.tensors}.items()))
        return (self.name, self.path, len(self.df), tuple(self.df.columns), shapes, mtime, self.revision)

    def describe(self):
        extras = []
        if self.matrices: extras.append(f"{len(self.matrices)} matrices")
        if self.volumes: extras.append(f"{len(self.volumes)} volumes")
        if self.tensors: extras.append(f"{len(self.tensors)} tensors")
        suffix = ("; " + ", ".join(extras)) if extras else ""
        return f"{len(self.df):,} samples x {len(self.numeric_columns)} numeric series{suffix}"


def detect_canonical_columns(df: pd.DataFrame, matrices: dict | None = None) -> dict:
    """Map each PRIORITY key to the dataframe column (or matrix) that carries it.
    Exact column names win; otherwise the first substring match. Missing keys
    map to None so the UI can show them disabled."""
    cols = [c for c in (df.columns if df is not None else [])]
    mats = list((matrices or {}).keys())
    out = {}
    for key, pats in _CANONICAL_PATTERNS.items():
        hit = key if key in cols else None
        if hit is None:
            for c in cols:
                if any(p in c.lower() for p in pats):
                    hit = c
                    break
        if hit is None:
            for m in mats:
                if any(p in m.lower() for p in pats):
                    hit = f"matrix:{m}"
                    break
        out[key] = hit
    return out


# ------------------------------------------------------------- loading
class BioprocessDataLoader:
    @staticmethod
    def load_mat_v73(filepath, session_name=None, use_cache=True) -> Dataset:
        if not session_name:
            session_name = os.path.basename(filepath)
        cache_path = os.path.join(CACHE_DIR, f"{session_name}.cache_{CACHE_VERSION}.pkl")

        if use_cache and os.path.exists(cache_path):
            try:
                if os.path.getmtime(cache_path) >= os.path.getmtime(filepath):
                    with open(cache_path, 'rb') as fh:
                        payload = pickle.load(fh)
                    return Dataset(session_name, filepath, payload['df'], payload.get('matrices'),
                                   payload.get('meta'), payload.get('aux'), payload.get('volumes'),
                                   payload.get('tensors'), payload.get('text_data'), payload.get('topology'))
            except Exception:
                pass

        raw = BioprocessDataLoader._read_raw(filepath)

        series, matrices, volumes, tensors, aux = {}, {}, {}, {}, {}
        for name, arr in raw.items():
            short = name.split('/')[-1]
            if arr.ndim == 1 and arr.size >= 2:
                series.setdefault(short, arr)
            elif arr.ndim == 2:
                matrices.setdefault(short, arr)
            elif arr.ndim == 3:
                volumes.setdefault(short, arr)
            elif arr.ndim > 3:
                tensors.setdefault(short, arr)

        # Force-include vhpr as a first-class series when it exists under any name.
        if 'vhpr' not in series:
            fallback = next((k for k in series if 'vhpr' in k.lower()), None)
            if fallback:
                series['vhpr'] = series[fallback]

        if series:
            n = int(stats.mode([len(v) for v in series.values()], keepdims=False).mode)
            kept = {}
            for k, v in series.items():
                if len(v) >= n:
                    kept[k] = v[:n]
                else:
                    aux[k] = v
            series = kept
        else:
            n = 0

        fixed = {k: (m.T if n and m.shape[1] == n and m.shape[0] != n else m) for k, m in matrices.items()}
        # Orient (samples × time): if a time-like vector matches axis 0 but not axis 1, transpose.
        time_lens = {v.size for k, v in aux.items() if any(t in k.lower() for t in ('time', 'tspan', 't_', 'days', 'hours'))}
        for k, m in fixed.items():
            if m.shape[0] != n and m.shape[1] != n and m.shape[0] in time_lens and m.shape[1] not in time_lens:
                fixed[k] = m.T
        df = pd.DataFrame(series).loc[:, lambda d: d.nunique(dropna=True) > 1] if series else pd.DataFrame()
        meta = {'source': filepath, 'n_raw_keys': len(raw), 'aux_keys': sorted(aux),
                'canonical': detect_canonical_columns(df, fixed)}

        try:
            with open(cache_path, 'wb') as fh:
                pickle.dump({'df': df, 'matrices': fixed, 'volumes': volumes, 'tensors': tensors,
                             'aux': aux, 'meta': meta}, fh)
        except Exception:
            pass
        return Dataset(session_name, filepath, df, fixed, meta, aux, volumes, tensors)

    @staticmethod
    def _read_raw(filepath) -> dict:
        """Read every numeric array from a .mat file. v7.3 (HDF5) via h5py;
        v5/v6/v7 via scipy.io.loadmat."""
        raw = {}
        if h5py.is_hdf5(filepath):
            with h5py.File(filepath, 'r') as fh:
                def visit(name, node):
                    if not isinstance(node, h5py.Dataset):
                        return
                    try:
                        if node.dtype.kind not in ('f', 'i', 'u', 'b'):
                            return
                        effective = tuple(int(v) for v in node.shape if int(v) != 1) or (1,)
                        if len(effective) == 1:
                            # 1-D vectors feed the dataframe/axis selectors and
                            # are normally small, so load them eagerly.
                            arr = np.squeeze(np.asarray(node))
                            raw[name] = arr.astype(np.float64, copy=False)
                        else:
                            raw[name] = HDFArrayProxy(filepath, name, node.shape, node.dtype, squeeze=True)
                    except Exception:
                        return
                fh.visititems(visit)
            return raw
        from scipy.io import loadmat
        data = loadmat(filepath, squeeze_me=True, struct_as_record=False)

        def walk(prefix, obj):
            if isinstance(obj, np.ndarray) and obj.dtype.kind in ('f', 'i', 'u', 'b'):
                arr = np.squeeze(obj)
                if arr.ndim >= 1:
                    raw[prefix] = arr.astype(np.float64, copy=False)
            elif hasattr(obj, '_fieldnames'):
                for fn in obj._fieldnames:
                    walk(f"{prefix}/{fn}", getattr(obj, fn))
            elif isinstance(obj, np.ndarray) and obj.dtype == object:
                for i, item in enumerate(obj.ravel()):
                    walk(f"{prefix}/{i}", item)
        for k, v in data.items():
            if k.startswith('__'):
                continue
            walk(k, v)
        return raw

    @staticmethod
    def apply_noise_filter(df, cols, method="Z-Score", threshold=2.5):
        cols = [c for c in cols if c in df.columns and pd.api.types.is_numeric_dtype(df[c])]
        if not cols:
            return df
        keep = pd.Series(True, index=df.index)
        for col in cols:
            v = df[col].astype(float)
            finite = np.isfinite(v)
            if method == "Z-Score":
                mu, sd = v[finite].mean(), v[finite].std(ddof=0)
                if sd > 0:
                    keep &= (~finite) | (np.abs(v - mu) / sd <= threshold)
            elif method == "IQR":
                q1, q3 = v.quantile(0.25), v.quantile(0.75)
                iqr = q3 - q1
                if iqr > 0:
                    keep &= (~finite) | ((v >= q1 - 1.5 * iqr) & (v <= q3 + 1.5 * iqr))
        return df[keep]


def _numericise_frame(df: pd.DataFrame) -> pd.DataFrame:
    """Coerce number-like columns while retaining genuinely categorical labels."""
    out = df.copy()
    for c in out.columns:
        if pd.api.types.is_numeric_dtype(out[c]):
            continue
        converted = pd.to_numeric(out[c], errors="coerce")
        # Replace only when at least half the non-empty values are numeric.
        denom = max(int(out[c].notna().sum()), 1)
        if int(converted.notna().sum()) / denom >= 0.5:
            out[c] = converted
    return out.dropna(axis=1, how="all")


def _extract_units_from_columns(columns) -> dict:
    from graphvis.data.variable_context import extract_unit
    return {str(c): extract_unit(str(c)) for c in columns if extract_unit(str(c))}


def _dataset_from_arrays(filepath: str, arrays: dict[str, np.ndarray], kind: str, text_data=None, meta_extra=None) -> Dataset:
    """Normalise heterogeneous scientific arrays into the common data model.

    1-D arrays of the modal length become dataframe series; 2-D arrays stay as
    matrices, 3-D arrays become volumes and higher-dimensional arrays are kept
    as tensors so parameter sweeps and ODE state cubes are not flattened/lost.
    """
    series, matrices, volumes, tensors, aux = {}, {}, {}, {}, {}
    for raw_name, raw in (arrays or {}).items():
        try:
            ndim = _array_ndim(raw)
            dtype = getattr(raw, 'dtype', None) or np.asarray(raw).dtype
        except Exception:
            continue
        if np.dtype(dtype).kind not in ('f', 'i', 'u', 'b'):
            continue
        name = str(raw_name).split('/')[-1] or str(raw_name)
        if ndim == 1:
            arr = np.squeeze(np.asarray(raw)).astype(np.float64, copy=False)
            if arr.size >= 2:
                series.setdefault(name, arr)
        elif ndim == 2:
            matrices.setdefault(name, raw)
        elif ndim == 3:
            volumes.setdefault(name, raw)
        elif ndim > 3:
            tensors.setdefault(name, raw)
    if series:
        lengths = [len(v) for v in series.values()]
        n = int(stats.mode(lengths, keepdims=False).mode)
        kept = {}
        for k, v in series.items():
            if len(v) >= n:
                kept[k] = v[:n]
            else:
                aux[k] = v
        series = kept
        df = pd.DataFrame(series)
        df = df.loc[:, lambda d: d.nunique(dropna=True) > 1]
    else:
        df = pd.DataFrame()
    meta = {'source': filepath, 'kind': kind, 'n_raw_keys': len(arrays or {}),
            'units': _extract_units_from_columns(df.columns)}
    if meta_extra:
        meta.update(meta_extra)
    return Dataset(os.path.basename(filepath), filepath, df, matrices, meta, aux,
                   volumes=volumes, tensors=tensors, text_data=text_data or {})


def load_hdf_dataset(filepath: str) -> Dataset:
    arrays = {}
    with h5py.File(filepath, 'r') as fh:
        def visit(name, node):
            if not isinstance(node, h5py.Dataset):
                return
            try:
                if node.dtype.kind in ('f', 'i', 'u', 'b'):
                    arrays[name] = HDFArrayProxy(filepath, name, node.shape, node.dtype, squeeze=True)
            except Exception:
                pass
        fh.visititems(visit)
    return _dataset_from_arrays(filepath, arrays, 'hdf')


def load_netcdf_dataset(filepath: str) -> Dataset:
    try:
        import xarray as xr
    except ImportError as exc:
        raise ImportError("NetCDF support requires xarray plus h5netcdf or scipy.") from exc

    # Prefer backends with reliable binary wheels on Windows. The optional
    # netCDF4 C-extension is not required for GraphVis and is deliberately
    # avoided here so a missing compiler can never block normal NetCDF use.
    ds = None
    errors = []
    for engine in ("h5netcdf", "scipy", None):
        try:
            ds = xr.open_dataset(filepath, engine=engine) if engine else xr.open_dataset(filepath)
            break
        except Exception as exc:
            errors.append(f"{engine or 'auto'}: {exc}")
    if ds is None:
        raise ValueError("Could not open NetCDF file. Tried h5netcdf/scipy/auto backends: " + " | ".join(errors))

    arrays = {}
    units = {}
    try:
        for name, da in {**ds.coords, **ds.data_vars}.items():
            try:
                arrays[name] = np.asarray(da.values)
                if da.attrs.get('units'):
                    units[name] = str(da.attrs['units'])
            except Exception:
                continue
    finally:
        ds.close()
    out = _dataset_from_arrays(filepath, arrays, 'netcdf')
    out.meta.setdefault('units', {}).update(units)
    return out


def load_tdms_dataset(filepath: str) -> Dataset:
    """Load the modern National Instruments TDMS container via npTDMS."""
    try:
        from nptdms import TdmsFile
    except ImportError as exc:
        raise ImportError("National Instruments TDMS support requires 'nptdms'.") from exc
    td = TdmsFile.read(filepath)
    arrays, units = {}, {}
    for group in td.groups():
        for ch in group.channels():
            key = f"{group.name}/{ch.name}"
            try:
                arrays[key] = np.asarray(ch[:])
                unit = ch.properties.get('unit_string') or ch.properties.get('unit')
                if unit:
                    units[key] = str(unit)
            except Exception:
                continue
    out = _dataset_from_arrays(filepath, arrays, 'tdms')
    out.meta.setdefault('units', {}).update(units)
    return out


def load_tdm_dataset(filepath: str) -> Dataset:
    """Load legacy National Instruments TDM + TDX pairs.

    Legacy ``.tdm`` is an XML metadata document whose bulk arrays normally
    live in a sibling ``.tdx`` file.  GraphVis uses the optional TDMtermite
    decoder for that pair and converts its channel-group output into the
    regular :class:`Dataset` table model.  Keeping this separate from npTDMS
    avoids treating two unrelated NI formats as if they were interchangeable.
    """
    path = Path(filepath)
    candidates = [path.with_suffix('.tdx'), path.with_suffix('.TDX')]
    tdx = next((p for p in candidates if p.exists()), None)
    if tdx is None:
        raise FileNotFoundError(
            f"Legacy NI TDM requires the companion TDX file next to {path.name}."
        )
    try:
        try:
            import TDMtermite as termite  # package/module spelling on PyPI
        except ImportError:
            import tdmtermite as termite  # compatibility with alternate builds
    except ImportError as exc:
        raise ImportError(
            "Legacy NI .tdm/.tdx support requires the optional 'TDMtermite' package. "
            "Run update.bat to install/repair the optional TDMtermite connector"
        ) from exc

    with tempfile.TemporaryDirectory(prefix='graphvis_tdm_') as tmp:
        try:
            decoder = termite.tdmtermite(os.fsencode(str(path)), os.fsencode(str(tdx)))
            out_dir = os.path.join(tmp, '')
            decoder.write_all(os.fsencode(out_dir))
        except Exception as exc:
            raise ValueError(f"TDMtermite could not decode {path.name}: {exc}") from exc

        outputs = sorted(Path(tmp).glob('*.csv'))
        if not outputs:
            outputs = sorted(p for p in Path(tmp).iterdir() if p.is_file())
        frames, text_data = [], {}
        for idx, out_path in enumerate(outputs):
            frame = None
            for kwargs in (
                {'sep': None, 'engine': 'python', 'comment': '#'},
                {'sep': r'\s+', 'engine': 'python', 'comment': '#'},
                {'sep': ',', 'engine': 'python', 'comment': '#'},
            ):
                try:
                    trial = pd.read_csv(out_path, **kwargs)
                    if trial.shape[1] >= 1 and len(trial):
                        frame = trial
                        break
                except Exception:
                    continue
            if frame is None or frame.empty:
                continue
            group = out_path.stem
            cols = []
            for c in frame.columns:
                name = str(c).strip() or f"column_{len(cols)}"
                # Prefix every group after the first (and any duplicate) so
                # identically named NI channels are never silently overwritten.
                if idx or any(name in f.columns for f in frames):
                    name = f"{group}/{name}"
                cols.append(name)
            frame.columns = cols
            for c in frame.columns:
                if not pd.api.types.is_numeric_dtype(frame[c]):
                    vals = [str(v) for v in frame[c].dropna().tolist()]
                    if vals:
                        text_data[c] = vals
            frames.append(_numericise_frame(frame).reset_index(drop=True))

        if not frames:
            raise ValueError(f"No channel data could be recovered from {path.name}.")
        df = pd.concat(frames, axis=1)
        meta = {
            'source': str(path), 'kind': 'tdm', 'tdx_source': str(tdx),
            'decoder': 'TDMtermite', 'units': _extract_units_from_columns(df.columns),
        }
        return Dataset(path.name, str(path), df, meta=meta, text_data=text_data)


def load_table_dataset(filepath: str) -> Dataset:
    """Load common tabular formats into the Dataset model."""
    lower = filepath.lower()
    text_data = {}
    if lower.endswith('.json'):
        try:
            df = pd.read_json(filepath)
        except ValueError:
            df = pd.read_json(filepath, lines=True)
        kind = 'json'
    elif lower.endswith(('.xlsx', '.xls', '.xlsm')):
        df = pd.read_excel(filepath)
        kind = 'excel'
    elif lower.endswith('.tsv'):
        df = pd.read_csv(filepath, sep='\t')
        kind = 'tsv'
    elif lower.endswith(('.txt', '.asc', '.dat')):
        # First attempt delimiter sniffing; fall back to a single text column.
        try:
            df = pd.read_csv(filepath, sep=None, engine='python', comment='#')
        except Exception:
            lines = Path(filepath).read_text(encoding='utf-8', errors='ignore').splitlines()
            df = pd.DataFrame({'text': lines})
        kind = 'text'
    elif lower.endswith(('.html', '.htm')):
        tables = pd.read_html(filepath)
        if not tables:
            raise ValueError('No HTML tables were found.')
        df = tables[0]
        kind = 'html'
    elif lower.endswith('.xml'):
        try:
            df = pd.read_xml(filepath)
        except Exception as exc:
            raise ValueError(f"Could not interpret XML as a tabular document: {exc}") from exc
        kind = 'xml'
    else:
        df = pd.read_csv(filepath)
        kind = 'csv'
    for c in df.columns:
        if not pd.api.types.is_numeric_dtype(df[c]):
            vals = [str(v) for v in df[c].dropna().tolist()]
            if vals:
                text_data[str(c)] = vals
    df = _numericise_frame(df)
    meta = {'source': filepath, 'kind': kind, 'units': _extract_units_from_columns(df.columns)}
    return Dataset(os.path.basename(filepath), filepath, df, meta=meta, text_data=text_data)



def load_audio_dataset(filepath: str) -> Dataset:
    """Load WAV/FLAC-like audio into time/channel columns. WAV uses SciPy;
    other audio containers use the optional soundfile package.
    """
    lower = filepath.lower()
    if lower.endswith('.wav'):
        from scipy.io import wavfile
        rate, data = wavfile.read(filepath)
    else:
        try:
            import soundfile as sf
        except ImportError as exc:
            raise ImportError("Non-WAV audio support requires soundfile.") from exc
        data, rate = sf.read(filepath, always_2d=False)
    arr = np.asarray(data)
    if arr.ndim == 1:
        frame = pd.DataFrame({'time_s': np.arange(arr.size, dtype=float) / float(rate), 'amplitude': arr.astype(float)})
    elif arr.ndim == 2:
        frame = pd.DataFrame({'time_s': np.arange(arr.shape[0], dtype=float) / float(rate)})
        for i in range(arr.shape[1]):
            frame[f'channel_{i+1}'] = arr[:, i].astype(float)
    else:
        raise ValueError(f"Unsupported audio shape: {arr.shape}")
    peak = float(np.nanmax(np.abs(frame.select_dtypes(include=[np.number]).drop(columns=['time_s'], errors='ignore').to_numpy()))) if len(frame) else 0.0
    if peak > 1.0 and np.issubdtype(arr.dtype, np.integer):
        scale = float(max(abs(np.iinfo(arr.dtype).min), np.iinfo(arr.dtype).max))
        for c in frame.columns:
            if c != 'time_s': frame[c] = frame[c] / scale
    return Dataset(os.path.basename(filepath), filepath, frame, meta={'source': filepath, 'kind': 'audio', 'sample_rate_hz': int(rate), 'units': {'time_s': 's'}})


def load_mass_spec_dataset(filepath: str) -> Dataset:
    """Load common open mass-spectrometry exchange formats (MGF/mzML/mzXML)."""
    lower = filepath.lower()
    rows: list[dict] = []
    if lower.endswith('.mgf'):
        try:
            from pyteomics import mgf
        except ImportError as exc:
            raise ImportError("MGF import requires pyteomics.") from exc
        reader = mgf.MGF(filepath)
    elif lower.endswith('.mzml'):
        try:
            from pyteomics import mzml
        except ImportError as exc:
            raise ImportError("mzML import requires pyteomics.") from exc
        reader = mzml.MzML(filepath)
    elif lower.endswith('.mzxml'):
        try:
            from pyteomics import mzxml
        except ImportError as exc:
            raise ImportError("mzXML import requires pyteomics.") from exc
        reader = mzxml.MzXML(filepath)
    else:
        raise ValueError('Unsupported mass-spectrometry format.')
    with reader:
        for scan_i, spectrum in enumerate(reader):
            mz = np.asarray(spectrum.get('m/z array', []), float)
            intensity = np.asarray(spectrum.get('intensity array', []), float)
            n = min(mz.size, intensity.size)
            scan_id = spectrum.get('id') or spectrum.get('params', {}).get('title') or scan_i
            rt = np.nan
            try:
                scan = (spectrum.get('scanList', {}).get('scan') or [{}])[0]
                rt = float(scan.get('scan start time', np.nan))
            except Exception:
                pass
            rows.extend({'scan': scan_i, 'scan_id': str(scan_id), 'retention_time': rt, 'mz': float(mz[j]), 'intensity': float(intensity[j])} for j in range(n))
            if scan_i >= 9999 and sum(len(v) for v in [rows]) > 5_000_000:
                break
    frame = pd.DataFrame(rows)
    return Dataset(os.path.basename(filepath), filepath, frame, meta={'source': filepath, 'kind': 'mass_spectrometry', 'units': {'mz': 'm/z', 'intensity': 'a.u.'}}, text_data={'scan_id': frame.get('scan_id', pd.Series(dtype=str)).astype(str).tolist()})


def load_flow_cytometry_dataset(filepath: str) -> Dataset:
    """Load FCS flow-cytometry event tables through the optional fcsparser package."""
    try:
        import fcsparser
    except ImportError as exc:
        raise ImportError("FCS flow-cytometry support requires fcsparser.") from exc
    meta, frame = fcsparser.parse(filepath, reformat_meta=True)
    frame = _numericise_frame(frame)
    return Dataset(os.path.basename(filepath), filepath, frame, meta={'source': filepath, 'kind': 'flow_cytometry', 'instrument_meta': {str(k): str(v) for k,v in dict(meta).items() if np.isscalar(v)}})


def load_excel_workbook(filepath: str) -> dict[str, Dataset]:
    """Return every Excel worksheet as a distinct Dataset without flattening the workbook."""
    sheets = pd.read_excel(filepath, sheet_name=None)
    out: dict[str, Dataset] = {}
    for sheet_name, frame in sheets.items():
        text_data = {str(c): [str(v) for v in frame[c].dropna().tolist()] for c in frame.columns if not pd.api.types.is_numeric_dtype(frame[c])}
        numeric = _numericise_frame(frame.copy())
        name = f"{os.path.basename(filepath)} :: {sheet_name}"
        out[str(sheet_name)] = Dataset(name, filepath, numeric, meta={'source': filepath, 'kind': 'excel_sheet', 'workbook': os.path.basename(filepath), 'sheet': str(sheet_name), 'units': _extract_units_from_columns(numeric.columns)}, text_data=text_data)
    return out

def load_dataset(filepath: str) -> Dataset:
    lower = filepath.lower()
    if lower.endswith(('.csv', '.tsv', '.txt', '.asc', '.dat', '.json', '.xlsx', '.xls', '.xlsm', '.html', '.htm', '.xml')):
        return load_table_dataset(filepath)
    if lower.endswith(('.h5', '.hdf', '.hdf5')):
        # MATLAB v7.3 files are HDF5 but .mat should stay on the MATLAB parser.
        return load_hdf_dataset(filepath)
    if lower.endswith(('.nc', '.netcdf')):
        return load_netcdf_dataset(filepath)
    if lower.endswith('.tdms'):
        return load_tdms_dataset(filepath)
    if lower.endswith('.tdm'):
        return load_tdm_dataset(filepath)
    if lower.endswith(('.wav', '.flac', '.ogg')):
        return load_audio_dataset(filepath)
    if lower.endswith(('.mgf', '.mzml', '.mzxml')):
        return load_mass_spec_dataset(filepath)
    if lower.endswith('.fcs'):
        return load_flow_cytometry_dataset(filepath)
    if lower.endswith('.mat'):
        return BioprocessDataLoader.load_mat_v73(filepath)
    raise ValueError(f"Unsupported dataset format: {os.path.splitext(filepath)[1] or filepath}")


def load_csv_dataset(filepath: str) -> Dataset:
    return load_table_dataset(filepath)


CHART_TYPES = [
    # Core / MATLAB-style graph library
    "Line Chart", "3D Line", "Stairs", "Error Bar", "Area", "Stacked Lines",
    "Function Plot", "Function 3D Parametric", "Implicit Function", "Implicit Surface", "Function Contour", "Function Surface", "Function Mesh",
    "4D / 5D Scatter", "3D Scatter", "3D Bubble", "Swarm", "3D Swarm", "Plot Matrix",
    "Histogram", "2D Histogram", "Scatter + Marginals", "Box Plot", "Violin Plot", "Raincloud",
    "Bar", "Horizontal Bar", "3D Bar", "3D Horizontal Bar", "Stem", "3D Stem", "Pie", "Donut", "Word Cloud", "Bubble Cloud",
    "Parallel Coordinates", "Spy Matrix", "Pareto Front",
    "2D Heatmap", "2D Contour", "3D Contour", "3D Topography / Surface", "3D Mesh",
    "Surface + Contours", "Waterfall", "Ribbon", "Hexbin Density",
    "Quiver Field", "Feather", "Stream Field", "3D Quiver", "Stream Ribbon", "Stream Tube", "Cone Plot",
    "Polar Line", "Polar Histogram", "Polar Scatter", "Polar Bubble", "Compass",
    "Geo Line", "Geo Scatter", "Geo Density", "Geo Bubble",
    "Isosurface", "Isocaps", "Isonormals", "Volume Show", "Patch", "Volume Slice", "Contour Slice",
    "Animated Line", "Comet", "Comet 3D", "Stream Particles",
    # Domain-specific analysis views
    "Global Sensitivity", "1D Marginal Responses",
    "sCOD Degradation Profile", "VFA Concentration Profile",
    "Polarisation & Power Curve", "EIS: Nyquist", "EIS: Bode", "Gompertz H₂ Kinetics",
]

# Synchronize with the searchable 100+ graph catalogue. The loader owns
# capability checks, while graph_library owns names/categories/tooltips.
try:
    from graphvis.rendering.graph_library import GRAPH_LIBRARY
    for _entry in GRAPH_LIBRARY:
        _engine = _entry.get("engine")
        if _engine and _engine not in CHART_TYPES:
            CHART_TYPES.append(_engine)
except Exception:
    pass


_THREE_NUMERIC = {
    "3D Line", "3D Scatter", "3D Bubble", "3D Swarm", "3D Bar", "3D Horizontal Bar", "3D Stem",
    "2D Heatmap", "2D Contour", "3D Contour", "3D Topography / Surface", "3D Mesh",
    "Surface + Contours", "Waterfall", "Ribbon", "EIS: Bode", "Quiver Field", "Stream Field",
    "Ternary Scatter",
    "Vorticity Map", "Divergence Map", "Phase Portrait", "Flow Texture (LIC)", "Tensor Glyph Field",
}
_SURFACE_MATRIX_CHARTS = {"2D Heatmap", "2D Contour", "3D Contour", "3D Topography / Surface", "3D Mesh",
                          "Surface + Contours", "Waterfall", "Ribbon", "Quiver Field", "Stream Field"}
_ONE_NUMERIC = {"Histogram", "Box Plot", "Violin Plot", "Raincloud", "Pie", "Donut", "Polar Histogram", "Word Cloud",
                "KDE Density", "ECDF", "Cumulative Histogram", "Q-Q Plot", "Probability Plot", "Ridgeline", "Strip Plot",
                "Beeswarm", "Spectrogram", "Power Spectral Density", "Autocorrelation", "Lag Plot", "Dot Plot", "Event Plot"}
_EXPRESSION_CHARTS = {"Function Plot", "Function 3D Parametric", "Implicit Function", "Implicit Surface",
                      "Function Contour", "Function Surface", "Function Mesh"}
_VOLUME_CHARTS = {"Isosurface", "Isocaps", "Isonormals", "Volume Show", "Volume Slice", "Contour Slice"}
_VECTOR3_CHARTS = {"3D Quiver", "Stream Ribbon", "Stream Tube", "Cone Plot"}
_ANIMATION_CHARTS = {"Animated Line", "Comet", "Comet 3D", "Stream Particles"}


def _lat_lon_columns(dataset: Dataset):
    cols = {str(c).lower(): c for c in dataset.df.columns}
    lat = next((v for k, v in cols.items() if k in ('lat', 'latitude') or 'latitude' in k), None)
    lon = next((v for k, v in cols.items() if k in ('lon', 'lng', 'longitude') or 'longitude' in k), None)
    return lat, lon


def chart_capabilities(dataset: Dataset | None) -> dict:
    caps = {}
    if dataset is None:
        return {c: ((c in _EXPRESSION_CHARTS), "" if c in _EXPRESSION_CHARTS else "No dataset loaded.") for c in CHART_TYPES}
    ncols = len(dataset.numeric_columns)
    nrows = len(dataset.df)
    lat, lon = _lat_lon_columns(dataset)
    has_volume = bool(getattr(dataset, 'volumes', {}))
    volumes = list(getattr(dataset, 'volumes', {}).values())
    vector3 = len(volumes) >= 3 and any(np.asarray(v).ndim == 3 for v in volumes)
    has_text = bool(getattr(dataset, 'text_data', {})) or any(not pd.api.types.is_numeric_dtype(dataset.df[c]) for c in dataset.df.columns)
    for chart in CHART_TYPES:
        if chart in _EXPRESSION_CHARTS:
            caps[chart] = (True, "")
        elif chart in _VOLUME_CHARTS:
            caps[chart] = (has_volume, "" if has_volume else "Needs a 3-D scalar array/volume.")
        elif chart in _VECTOR3_CHARTS:
            caps[chart] = (vector3, "" if vector3 else "Needs three compatible 3-D U/V/W component arrays.")
        elif chart == "Patch":
            ok = bool(getattr(dataset, 'topology', {})) or ncols >= 3
            caps[chart] = (ok, "" if ok else "Needs vertices/faces topology or >=3 numeric coordinate columns.")
        elif chart == "Word Cloud":
            caps[chart] = (has_text, "" if has_text else "Needs text/categorical data.")
        elif chart == "Bubble Cloud":
            ok = has_text and ncols >= 1
            caps[chart] = (ok, "" if ok else "Needs labels/text plus a numeric weight series.")
        elif chart in _ONE_NUMERIC:
            caps[chart] = (ncols >= 1 or (chart == "Word Cloud" and has_text), "" if ncols >= 1 else "Needs >=1 numeric series.")
        elif chart in _THREE_NUMERIC:
            has_surface_matrix = chart in _SURFACE_MATRIX_CHARTS and any(_array_ndim(m) == 2 and min(_array_shape(m)) >= 2 for m in dataset.matrices.values())
            if has_surface_matrix:
                caps[chart] = (True, "")
            elif ncols < 3:
                caps[chart] = (False, "Needs >=3 numeric series or a 2-D pre-gridded matrix." if chart in _SURFACE_MATRIX_CHARTS else "Needs >=3 numeric series.")
            elif nrows < 6 and chart not in ("EIS: Bode", "3D Bar", "3D Horizontal Bar", "3D Stem"):
                caps[chart] = (False, "Needs >=6 samples.")
            else:
                caps[chart] = (True, "")
        elif chart in ("Geo Line", "Geo Scatter", "Geo Density", "Geo Bubble"):
            ok = lat is not None and lon is not None
            caps[chart] = (ok, "" if ok else "Needs latitude and longitude columns.")
        elif chart in ("sCOD Degradation Profile", "VFA Concentration Profile"):
            key = 'scod' if 'sCOD' in chart else 'vfa'
            has = any(key in k.lower() for k in list(dataset.matrices) + dataset.numeric_columns)
            caps[chart] = (has, "" if has else f"No {key.upper()} profile in this dataset.")
        elif chart == "Spy Matrix":
            ok = bool(dataset.matrices) or ncols >= 2
            caps[chart] = (ok, "" if ok else "Needs a matrix or >=2 numeric columns.")
        elif chart in ("Parallel Coordinates", "Plot Matrix", "Stacked Lines", "Correlation Matrix", "Covariance Matrix", "Radar Chart", "Andrews Curves"):
            ok = ncols >= 3
            caps[chart] = (ok, "" if ok else "Needs >=3 numeric series.")
        elif chart in _ANIMATION_CHARTS:
            need = 3 if chart == "Comet 3D" else 2
            ok = ncols >= need
            caps[chart] = (ok, "" if ok else f"Needs >={need} numeric series.")
        elif chart in ("Feather",):
            ok = ncols >= 2
            caps[chart] = (ok, "" if ok else "Needs vector U/V series.")
        else:
            caps[chart] = (ncols >= 2, "" if ncols >= 2 else "Needs >=2 numeric series.")
    return caps




def suggest_axis_mapping(dataset: Dataset | None) -> dict[str, str | None]:
    """Heuristic X/Y/Z recommendation based on names, units and data shape."""
    if dataset is None or dataset.df is None or dataset.df.empty:
        return {"x": None, "y": None, "z": None}
    cols = list(dataset.numeric_columns)
    if not cols:
        return {"x": None, "y": None, "z": None}
    units = getattr(dataset, "units", {}) or {}

    def x_score(c):
        low = str(c).lower(); unit = str(units.get(c, "")).lower()
        score = 0.0
        name_weights = (("time",12),("freq",11),("voltage",10),("potential",10),("current",9),
                        ("temperature",8),("temp",7),("pressure",6),("concentration",5),("flow",5),
                        ("dose",5),("distance",4),("wavelength",8),("lhs_",7))
        # Overlapping aliases such as "temperature"/"temp" represent one clue,
        # not two independent votes. Use the strongest semantic match.
        score += max((weight for token, weight in name_weights if token in low), default=0)
        if low in ("x","t","time"): score += 9
        if unit in ("s","ms","min","h","hz","khz","v","mv","k","°c","degc","nm","um","mm","m"):
            score += 3
        try:
            v = dataset.df[c].to_numpy(float); v=v[np.isfinite(v)]
            if len(v)>2:
                dv=np.diff(v)
                monotonic=max(np.mean(dv>=0),np.mean(dv<=0))
                score += 4*float(monotonic)
                score += min(2.0, dataset.df[c].nunique(dropna=True)/max(len(dataset.df),1)*2)
        except Exception: pass
        return score

    def response_score(c):
        low=str(c).lower(); score=0.0
        for token,weight in (("response",10),("yield",10),("rate",9),("vhpr",12),("power",8),("energy",7),
                             ("signal",8),("intensity",8),("absorb",7),("current",6),("voltage",5),("cod",7),
                             ("vfa",7),("pressure",4),("temperature",3)):
            if token in low: score += weight
        if c in getattr(dataset,"response_columns",[]): score += 6
        try:
            v=dataset.df[c].to_numpy(float); v=v[np.isfinite(v)]
            if len(v)>2 and np.ptp(v)>0: score += min(3.0,float(np.log10(1+dataset.df[c].nunique())))
        except Exception: pass
        return score

    x=max(cols,key=x_score)
    remaining=[c for c in cols if c!=x]
    y=max(remaining,key=response_score) if remaining else None
    rem2=[c for c in remaining if c!=y]
    z=max(rem2,key=response_score) if rem2 else None
    return {"x":x,"y":y,"z":z}

# ------------------------------------------------------------- literature
def parse_and_save_literature_data(filepath: str, extract_dataset: bool = True) -> list[str]:
    """Backwards-compatible wrapper around literature.extract_literature."""
    from graphvis.literature.extractor import extract_literature
    return extract_literature(filepath, extract_dataset=extract_dataset).saved_paths


# ------------------------------------------------------------- tasks
class TaskSignals(QObject):
    finished = Signal(object)
    failed = Signal(str)
    progress = Signal(str, int)
    cancelled = Signal()


class CancellableTask(QRunnable):
    def __init__(self, label="task"):
        super().__init__()
        self.label = label
        self.signals = TaskSignals()
        self._cancelled = False
        self.setAutoDelete(True)

    def cancel(self):
        self._cancelled = True

    @property
    def cancelled(self):
        return self._cancelled

    def stage(self, text, pct=-1):
        if self._cancelled:
            raise Exception("Cancelled")
        self.signals.progress.emit(text, pct)

    def execute(self):
        raise NotImplementedError

    def run(self):
        try:
            if self._cancelled:
                self.signals.cancelled.emit()
                return
            result = self.execute()
            if self._cancelled:
                self.signals.cancelled.emit()
            else:
                self.signals.finished.emit(result)
        except Exception as exc:
            if str(exc) == "Cancelled":
                self.signals.cancelled.emit()
            else:
                import traceback
                tb = traceback.format_exc()
                try:
                    from graphvis.core.session_diagnostics import record_activity, record_exception
                    record_activity("Background task failed", f"task={self.label}; error={type(exc).__name__}: {exc}")
                    record_exception(f"Background task failed: {self.label}", exc=exc, traceback_text=tb, fatal=False)
                except Exception:
                    pass
                self.signals.failed.emit(f"{type(exc).__name__}: {exc}\n{tb}")


class LoadTask(CancellableTask):
    def __init__(self, filepath, project=None):
        super().__init__(f"load:{os.path.basename(filepath)}")
        self.filepath = os.path.abspath(filepath)
        self.project = project

    def execute(self):
        path = self.filepath
        if self.project is not None:
            def copy_progress(msg, pct):
                self.stage(msg, min(18, 2 + int(max(0, min(100, pct)) * 0.16)))
            if hasattr(self.project, "ingest_file_progress"):
                path = self.project.ingest_file_progress(path, "datasets", progress=copy_progress, cancelled=lambda: self.cancelled)
            else:
                self.stage(f"Copying {os.path.basename(path)} into project…", 3)
                path = self.project.ingest_file(path, "datasets")
        self.stage(f"Reading {os.path.basename(path)}", 20)
        result = load_dataset(path)
        self.stage(f"Finalising {os.path.basename(path)}", 96)
        return result


class LiteratureTask(CancellableTask):
    def __init__(self, filepath, extract_dataset=True, ocr=True, project=None, time_budget_seconds=1800.0):
        super().__init__(f"literature:{os.path.basename(filepath)}")
        self.filepath, self.extract_dataset, self.ocr = os.path.abspath(filepath), extract_dataset, ocr
        self.project = project
        self.time_budget_seconds = max(10.0, min(float(time_budget_seconds or 1800.0), 259200.0))

    def execute(self):
        from graphvis.literature.extractor import extract_literature
        path = self.filepath
        if self.project is not None:
            def copy_progress(msg, pct):
                self.stage(msg, min(12, 1 + int(max(0, min(100, pct)) * 0.11)))
            if hasattr(self.project, "ingest_file_progress"):
                path = self.project.ingest_file_progress(path, "literature", progress=copy_progress, cancelled=lambda: self.cancelled)
            else:
                self.stage(f"Copying {os.path.basename(path)} into project…", 2)
                path = self.project.ingest_file(path, "literature")
        out_dir = str(self.project.literature_datasets_dir) if self.project is not None else None
        return extract_literature(path, extract_dataset=self.extract_dataset,
                                  ocr=self.ocr, progress=self.stage, output_dir=out_dir,
                                  time_budget_seconds=self.time_budget_seconds,
                                  cancelled=lambda: self.cancelled)


class LiteratureBatchTask(CancellableTask):
    """Extract several literature/supplement files as one linked ingestion batch."""
    def __init__(self, filepaths, extract_dataset=True, ocr=True, project=None, time_budget_seconds=1800.0):
        paths = [os.path.abspath(p) for p in filepaths]
        super().__init__(f"literature-batch:{len(paths)} files")
        self.filepaths, self.extract_dataset, self.ocr = paths, extract_dataset, ocr
        self.project = project
        self.time_budget_seconds = max(10.0, min(float(time_budget_seconds or 1800.0), 259200.0))

    def execute(self):
        from graphvis.literature.extractor import extract_literature, save_extraction
        paths = list(self.filepaths)
        if self.project is not None:
            copied = []
            total_copy = max(len(paths), 1)
            for i, src in enumerate(paths):
                def copy_progress(msg, pct, i=i):
                    base = int(1 + 9 * i / total_copy)
                    span = max(1, int(9 / total_copy))
                    self.stage(f"[{i + 1}/{total_copy}] {msg}", min(11, base + int(span * max(0, min(100, pct)) / 100)))
                if hasattr(self.project, "ingest_file_progress"):
                    copied.append(self.project.ingest_file_progress(src, "literature", progress=copy_progress, cancelled=lambda: self.cancelled))
                else:
                    self.stage(f"Copying linked source {i + 1}/{total_copy}: {os.path.basename(src)}", int(2 + 8 * i / total_copy))
                    copied.append(self.project.ingest_file(src, "literature"))
            paths = copied
        results = []
        primary = next((p for p in paths if p.lower().endswith('.pdf')), paths[0] if paths else None)
        total = max(len(paths), 1)
        for i, path in enumerate(paths):
            base0 = int(12 + 84 * i / total)
            span = max(int(84 / total), 1)
            def progress(msg, pct=-1, base=base0, span=span):
                local = 0 if pct is None or pct < 0 else min(max(int(pct), 0), 100)
                self.stage(f"[{i + 1}/{total}] {msg}", min(98, base + int(span * local / 100)))
            ext = extract_literature(path, extract_dataset=False, ocr=self.ocr, progress=progress,
                                     time_budget_seconds=self.time_budget_seconds,
                                     cancelled=lambda: self.cancelled)
            ext.primary_document = primary
            ext.linked_files = list(paths)
            ext.batch_id = os.path.basename(primary or path)
            if self.extract_dataset and ext.datasets:
                out_dir = str(self.project.literature_datasets_dir) if self.project is not None else None
                save_extraction(ext, output_dir=out_dir)
            results.append(ext)
        self.stage("Batch ingestion complete", 100)
        return results


def submit(task: CancellableTask, pool=None):
    (pool or QThreadPool.globalInstance()).start(task)
    return task
