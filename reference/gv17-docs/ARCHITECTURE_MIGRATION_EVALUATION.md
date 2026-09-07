# GraphVis Hybrid Architecture Evaluation & Migration Plan

*GraphVis 16.3 — Principal-engineer review of the framework/runtime options for
zero-latency UI responsiveness on weak hardware, seamless interop, and massive
data throughput.*

## 1. Candidate stacks evaluated

| Stack | UI latency | Compute throughput | Interop with existing code | Migration risk | Verdict |
|---|---|---|---|---|---|
| **A. Full Rust rewrite** (egui/iced + wgpu) | Excellent | Excellent | None — 300+ Python modules, matplotlib's 130-engine renderer, SciPy estimators and the literature/NLP stack must all be reimplemented | Extreme (multi-year; loses matplotlib's publication output) | **Rejected** |
| **B. C++/Qt rewrite** (QCustomPlot/VTK) | Excellent | Very good | Poor — same total-rewrite problem, plus VTK cannot reproduce matplotlib's journal-grade text/vector output | Very high | **Rejected** |
| **C. Electron/Web** (WebGL + WASM) | Poor on weak hardware (Chromium baseline ~300 MB RAM) | Good via WASM | Moderate | High; violates the low-spec requirement outright | **Rejected** |
| **D. Status quo** (pure Python/PySide6/matplotlib) | Good after the 16.x pipeline work, but single hot loops (kriging, Sibson, k-NN sweeps) remain GIL-bound | Adequate to ~10⁵ points | — | None | Insufficient alone at 10⁶+ points |
| **E. Hybrid: Python/Qt shell + optional Rust compute crate (PyO3) + GPU path via existing vispy/pyvista** | Good→Excellent | Excellent where it matters | **Seamless** — NumPy buffers cross the FFI boundary zero-copy; everything else untouched | Low, incremental, per-kernel | **Selected** |

## 2. Why hybrid (E), concretely

- **The UI layer is not the bottleneck.** Profiling (see §4) shows stalls come
  from numerical kernels and matplotlib scene construction — both already
  moved off the GUI thread by the 16.x two-pool worker architecture, the 10-s
  fast-preview fallback, cooperative cancellation, 3-D drag LOD and Low-Power
  Mode. Replacing Qt would spend years re-earning capabilities we already have.
- **matplotlib is a scientific asset, not a liability.** Journal-compliant
  Type-42/SVG-text export, 130 validated chart engines and the entire styling
  system depend on it. Any stack that discards it must reimplement publication
  output — the single hardest requirement to get right.
- **Rust earns its place per-kernel, not wholesale.** PyO3 + maturin gives
  zero-copy `ndarray` views over NumPy buffers; a `graphvis_native` wheel can
  accelerate IDW/k-NN/kriging/Sibson loops 10–50× with no API change. The seam
  is committed as `graphvis/core/native_accel.py`: every call site receives the
  compiled kernel when the wheel is installed and the identical NumPy/SciPy
  reference implementation otherwise, so weak machines without the wheel lose
  nothing and gain nothing but compatibility.
- **GPU:** wgpu-through-Rust is unnecessary — the optional vispy/pyvista GPU
  preview (`rendering/gpu.py`) already provides hardware-accelerated 3-D for
  machines that have it, and Low-Power Mode governs machines that don't.

## 3. Migration roadmap (incremental, always shippable)

1. **Phase 0 (done, 16.x):** worker-pool isolation, cooperative cancellation,
   fast-preview fallback, LOD 3-D drags, Low-Power Mode, point decimation.
2. **Phase 1 (done, 16.3):** `native_accel` FFI seam + kernel contract
   (`idw`, `knn_distances`, `pareto_envelope`), stable float64 ABI, parity
   tolerance 1e-9, verified by smoke test when the wheel is present.
3. **Phase 2:** publish `graphvis_native` (Rust: `ndarray`, `rayon`, `kiddo`
   KD-tree) as an optional dependency in `requirements/gpu.txt`-style tier.
4. **Phase 3:** extend the contract to kriging solves and Sibson stolen-area
   weights if profiling still shows them dominant after Phase 2.
5. **Exit criterion for any further migration:** a kernel only moves to Rust
   after profiling shows it >20 % of interactive render wall-clock.

## 4. Codebase audit findings (fixed in 16.x unless noted)

**Race conditions / thread-safety**
- Matplotlib confined to one worker (`HEAVY_POOL` size 1); numerical
  precompute on a separate pool; GUI thread draws only. ✔ by design.
- Render supersession by monotonic sequence + cooperative cancel tokens in
  every estimator loop (fixed 16.0–16.1).
- `_RENDER_DURATION_HISTORY` / `_LOW_SPEC_HARDWARE` globals are written only
  from the GUI thread (verified call sites). ✔
- Geometry-plan cache guarded by `_GEOMETRY_CACHE_LOCK`. ✔

**Memory / resource leaks**
- Old Qt canvases explicitly deparented + `deleteLater` on install. ✔
- Stale inspector artist leaked across figure swaps → fixed 16.1.
- SQLite connector left `.db` handles open on Windows → fixed 16.0.
- Figure caches are bounded (8 in-memory / 40 on disk, LRU-pruned). ✔

**Performance bottlenecks addressed**
- Uncoalesced per-mouse-move full redraw in 3-D (`Axes3D._on_move`) → frame
  coalescing + heavy-artist LOD hiding (16.1).
- Per-keystroke spinbox commits triggering render storms and corrupted
  min/max ranges → keyboard-tracking off + typing-commit debounce (16.2/16.3).
- Whole-figure pickling latency → disk figure cache made opt-in (15.x). ✔

**Hygiene removals (16.3)**
- Dead `CollapsibleBox` widget class + stale import (superseded by
  `DetachablePanel`) — deleted.
- Outdated patch-note artifacts `PACKAGE_MANIFEST_PREVIOUS.md`,
  `TEST_REPORT_PREVIOUS.md`, `README_LEGACY.md`, `tests/README_PREVIOUS.md`
  — deleted (current CHANGELOG.md is the single history of record).
- `archive/` holds user-managed release archives and is intentionally kept.

## 5. Non-goals

- No Electron/web runtime (violates the weak-hardware constraint).
- No VTK/native scene graph for the primary canvas while publication output
  remains matplotlib-based.
- No mandatory compiled dependency: GraphVis must always run pure-Python.
