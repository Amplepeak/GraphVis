# =========================================================================
# native_accel.py — optional compiled-acceleration seam (hybrid architecture).
#
# GraphVis's evaluated migration path (docs/ARCHITECTURE_MIGRATION_EVALUATION.md)
# keeps the Python/Qt shell and moves proven numerical hot spots into an
# optional Rust extension crate (PyO3/maturin, distributed as the
# ``graphvis_native`` wheel).  This module is the single boundary: callers ask
# for an accelerated kernel and always receive a working callable — the
# compiled one when the wheel is installed, the NumPy/SciPy reference
# implementation otherwise.  No behavioural difference is permitted between
# the two, so the extension can be adopted per-machine with zero risk.
# =========================================================================
from __future__ import annotations

from typing import Callable

_NATIVE = None
_NATIVE_CHECKED = False


def _native_module():
    """Import the optional compiled extension exactly once."""
    global _NATIVE, _NATIVE_CHECKED
    if not _NATIVE_CHECKED:
        _NATIVE_CHECKED = True
        try:
            import graphvis_native   # type: ignore  # optional Rust/PyO3 wheel
            _NATIVE = graphvis_native
        except Exception:
            _NATIVE = None
    return _NATIVE


def native_available() -> bool:
    return _native_module() is not None


def native_version() -> str | None:
    mod = _native_module()
    return getattr(mod, "__version__", None) if mod is not None else None


def get_kernel(name: str, fallback: Callable) -> Callable:
    """Return the compiled kernel ``name`` if present, else ``fallback``.

    Kernel contract (stable ABI for the Rust crate):
      - ``idw(points, values, queries, power, neighbors) -> ndarray``
      - ``knn_distances(points, queries, k) -> ndarray``
      - ``pareto_envelope(x, y, maximise) -> ndarray``
    All arrays are contiguous float64; results must match the reference
    implementation to within 1e-9 (verified by the acceleration smoke test
    whenever the wheel is installed).
    """
    mod = _native_module()
    kernel = getattr(mod, name, None) if mod is not None else None
    return kernel if callable(kernel) else fallback
