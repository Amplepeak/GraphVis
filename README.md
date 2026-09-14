# GraphVis 18.4

**Fast native graph visualization. Install it, launch it, visualize.**

**Not sure what you downloaded?** Open [`START_HERE.txt`](START_HERE.txt) before running a script. A folder without `Install.exe` is source code and must be built before it can run.

GraphVis is a Qt/QML desktop application backed by a precompiled Rust/WGPU/DataFusion/Arrow core and native VTK rendering. Public releases are built so end users do **not** need Rust, Cargo, CMake, Python, or a developer toolchain.

## Downloaded a public release?

### Windows
Open the ZIP and double-click **`Install.exe`**. That is the main entry point.

`Install.bat` is included only as a friendly fallback if Windows file associations or security software make the EXE less obvious. Use the root-level `Uninstall.bat` for a complete removal; the optional repair helper remains under `Support/`.

### Linux
Extract the `.tar.gz`, then double-click or run **`install.sh`**. After installation, use **`run.sh`** or the installed GraphVis desktop/menu entry.

To remove it completely, run **`uninstall.sh`** from that extracted release folder. It keeps the original `.tar.gz` download.

The Linux archive preserves executable permissions and carries the precompiled GraphVis runtime. No Rust toolchain is required on the end-user machine.

## What is intentionally *not* in the base public runtime?

- **Rust/Cargo:** build-time only. The release contains the compiled native library (`graphvis_ffi.dll` on Windows, `libgraphvis_ffi.so` on Linux).
- **Python:** optional science/literature add-on source lives in `services/python/` for developers, but the standard desktop app does not require Python.
- **Julia/R plugin source:** optional extension work, not required to launch GraphVis.
- **Graphviz:** not currently called by the desktop/native runtime, so it is not bundled or installed. Ready-to-enable prerequisite handling is retained for a future feature that truly requires it.
- **Developer/CI folders:** `.github/`, `dev/`, `infra/`, `tests/`, `tools/`, and build scripts never go into the end-user archive.

## Source checkout quick start

The project root has the same entry points on both supported platforms:

| Task | Windows | Linux |
| --- | --- | --- |
| Set up the native developer toolchain | `install.bat` | `bash install.sh` |
| Build a standalone end-user package | `build.bat` | `bash build.sh` |
| Build if needed and launch locally | `run.bat` | `bash run.sh` |

`build.bat` produces `dist/GraphVis-<version>-Windows.zip`; `build.sh` produces `dist/GraphVis-<version>-Linux.tar.gz`. These are the end-user deliverables and already contain the native executable, assets, and runtime libraries. End users should use the archive's installer/launcher and do not need Python.

See **[`docs/INSTALL_AND_BUILD.md`](docs/INSTALL_AND_BUILD.md)** for the root layout and developer/end-user workflows, and **[`docs/PUBLIC_RELEASE.md`](docs/PUBLIC_RELEASE.md)** for release details.

## Licence

GraphVis is free software under the **GNU General Public License, version 3 or
later**. See [`LICENSE`](LICENSE) for the full text.

The bundled third-party libraries keep their own licences, reproduced in
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md). Regenerate that file after
any dependency change:

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools\Generate-Notices.ps1
```

**Qt** is used under the LGPL v3. It is linked dynamically and shipped as
separate `Qt6*.dll` files so that a user may substitute their own build of the
same Qt version; Qt's source is available from <https://download.qt.io/>.

## What is verified, and what is not

GraphVis is a research tool. It is more useful to say plainly where it has been
checked than to imply it has been checked everywhere.

**Verified**

- All 434 catalogue engines render — 2,116 catalogue entries, every one
  producing output, checked against its own empty-frame baseline. Observed
  rather than inferred: the sweep of 12 September 2026 covered all 434 and
  reported `every engine drew data`. The same run checked 434 engines for
  sensitivity to a constant column and 434 for drawing off the canvas edge,
  with nothing to report in either.
- Analysis results against closed-form answers: descriptive statistics, t-tests,
  ANOVA, Cohen's d, p-value adjustment, power, regression, curve and surface
  fitting, uncertainty propagation, unit conversion, FFT, Weibull, PCA, peak
  detection and the modified-Gompertz, Nyquist and polarisation fits. Expected
  values come from the mathematics, never from the program's own output
  (`services/python/tests/test_numbers.py`).
- The C++ quantile, mean and standard deviation that every box plot, violin,
  histogram and percentile band stands on are compiled out of the renderer source
  and checked against NumPy (`services/python/tests/test_cxx_numerics.py`).
- The 16 surface estimators against analytic ground truth — linear precision for
  interpolants, smoothing behaviour for approximants, gradient continuity for C1
  elements.
- No unreachable code: every service operation, QML component and public C++ API
  has a caller, and every declaration has a definition.

**Not verified**

- **The visual pass.** That an engine draws, and draws the right numbers, does
  not mean it is well labelled or readable at publication size. This is the
  largest outstanding piece of work.
- **Figure de-rendering accuracy** is measured at 0.17% median error against a
  clean synthetic figure. A scanned page with compression artefacts, a grid under
  the curve and overlapping series will be harder; the tolerance control exists
  for that.
- **Colour-vision palettes** are measured and correct, but a palette specialised
  for one deficiency is not safe for another. Leave the standard palette selected
  unless you have a specific reason.

## Citing

If GraphVis contributed to published work, please cite the version you used —
`VERSION` records it, and `CHANGELOG.md` records what changed between versions.
Where a figure depends on a fitted parameter or a reported statistic, cite the
method rather than the program: the fits and tests are standard, and the tests
above name the closed forms they were checked against.
