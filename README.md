# GraphVis

**A desktop plotting program for people who publish figures.** It reads your
data and the papers around it, tells you which chart types actually fit, gets
the numbers back out of a published figure, and exports at a journal's real
measurements.

Qt/QML and C++ over a Rust core, with VTK for the 3-D viewport. It runs offline.
No account, no telemetry, nothing leaves the machine unless you configure it to.

![Twelve figures drawn by GraphVis: a 3-D surface, a Sankey diagram, a chord
diagram, a spectrogram, a line-integral-convolution flow texture, a Kaplan-Meier
survival curve, a Piper diagram, a wind rose, a payload-range diagram, a
psychrometric chart, an UpSet plot and a calendar heatmap.](docs/gallery.png)

<sub>Twelve of 440. Every one is rendered by the build in this repository, taken
straight from the gallery the test sweep produces, not drawn for the
advertisement.</sub>

---

## Quick guide

Five minutes, start to finish. Everything below is a button in the application.

**1. Get it running.**
A release archive installs without a toolchain — `Install.exe` on Windows,
`install.sh` on Linux. From source it is `install.bat` / `bash install.sh` once,
then `run.bat` / `bash run.sh`. Details in
[Building from source](#building-from-source).

**2. Bring data in.** *Home ▸ Import scientific dataset.*
CSV and Excel work out of the box. The Science add-on adds MATLAB, HDF5,
NetCDF, Arrow and the rest, converted on import and handed to the same loader a
CSV uses, so what plots is identical whichever door it came in by. A format
whose Python package is missing says exactly what to install rather than
failing quietly.

**3. Ask what to plot.** *Visualize ▸ **Go — scan and draw the best**.*
This is the part worth knowing about. The scanner profiles every variable,
tests the relationships between them, reads the column names for meaning, and
then ranks concrete engines **with the axis mapping to use** — not "try a
scatter plot" but this column on x, that one on y, this one on colour. Results
are cached against a fingerprint of the dataset, so asking again is instant and
changing the data re-scans.

**4. Or choose yourself.** 440 engines in 46 categories, 2,122 catalogued
entries. The catalogue is searchable and every entry carries a preview, so you
are picking from pictures rather than from names.

**5. Bring the literature in.** *Literature ▸ Open literature ▸ **Extract
figures and tables**.*
PDFs are parsed with pdfplumber and PyMuPDF for text and tables. What it finds
becomes context for step 3, so the recommendation knows what the papers around
your data already plot. **Compare with active dataset** puts their numbers
beside yours.

**6. Get the numbers out of a published chart.**
Give it a figure image and calibrate the axes by clicking two known points on
each. It returns the data behind the curve. Linear, logarithmic, reciprocal
(Arrhenius) and symlog axes are all handled. Median error is 0.17% against a
clean synthetic figure — see [What is verified](#what-is-verified-and-what-is-not)
before trusting it on a scanned page.

**7. Export for the journal.** *Save figure.*
Pick one of 65 publication profiles, 61 of them real publishers. The figure is
**re-rendered** at that width, resolution and text size rather than scaled —
shrinking a screen figure to 89 mm takes its 8 pt labels down to about 3 pt, and
that is what comes back from a copy editor. PDF, SVG and PNG.

**8. Paste it into the paper.**
The LaTeX panel writes the float, the caption, the label and the note in the
order your style wants them, in any of 51 citation styles. APA puts the caption
above the figure and the note below. IEEE, Nature and Harvard caption below.
That is why the style changes the block and not only the reference list.

**Optional, off by default.** A local Transformers model or an
OpenAI-compatible endpoint can improve figure classification. Nothing is sent
anywhere unless you set it up, and API keys are read from the environment and
never stored.

---

## Downloaded a public release?

**Not sure what you downloaded?** Open [`START_HERE.txt`](START_HERE.txt) before
running a script. A folder without `Install.exe` is source code and must be
built before it can run.

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

## Building from source

The project root has the same entry points on both supported platforms:

| Task | Windows | Linux |
| --- | --- | --- |
| Set up the native developer toolchain | `install.bat` | `bash install.sh` |
| Build a standalone end-user package | `build.bat` | `bash build.sh` |
| Build if needed and launch locally | `run.bat` | `bash run.sh` |

`build.bat` produces `dist/GraphVis-<version>-Windows.zip`; `build.sh` produces `dist/GraphVis-<version>-Linux.tar.gz`. These are the end-user deliverables and already contain the native executable, assets, and runtime libraries. End users should use the archive's installer/launcher and do not need Python.

See **[`docs/INSTALL_AND_BUILD.md`](docs/INSTALL_AND_BUILD.md)** for the root layout and developer/end-user workflows, and **[`docs/PUBLIC_RELEASE.md`](docs/PUBLIC_RELEASE.md)** for release details.

## Licence

Copyright (C) 2026 amplepeak

GraphVis is free software under the **GNU General Public License, version 3 or
later**. See [`LICENSE`](LICENSE) for the full text.

That choice is deliberate. Anyone may charge for a copy, but whoever does must
hand over the source under the same licence, so the person who paid can pass it
on for free. Nobody can take GraphVis closed, including its author.

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

- All 440 catalogue engines render — 2,122 catalogue entries, every one
  producing output, checked against its own empty-frame baseline. Observed
  rather than inferred: the sweep of 16 September 2026 covered all 440, reported
  `every engine drew data`, and produced a 440-figure gallery byte-identical to
  the stored baseline. The same run checked every engine for sensitivity to a
  constant column and for drawing off the canvas edge, with nothing to report in
  either.
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
