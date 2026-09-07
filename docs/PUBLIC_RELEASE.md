# Public Release Guide — GraphVis 18.4

This is the short release-engineering guide for producing end-user archives that do not expose developer internals.

## Final Windows archive

```text
GraphVis-18.4.0-Windows.zip
└─ GraphVis-18.4.0-Windows/
   ├─ Install.exe          ← primary: double-click this
   ├─ Install.bat          ← friendly fallback wrapper
   ├─ Uninstall.bat        ← remove GraphVis completely (ZIP is retained)
   ├─ README.md            ← short end-user instructions
   └─ Support/
      ├─ Repair.bat
      └─ Uninstall.bat
```

`Install.exe` embeds the precompiled staged application. The user does not see the `.nsi`, PowerShell maintenance files, Rust source, CMake files, tests, or CI configuration.

## Final Linux archive

```text
GraphVis-18.4.0-Linux.tar.gz
└─ GraphVis-18.4.0-Linux/
   ├─ install.sh           ← install/check dependencies
   ├─ run.sh               ← launch GraphVis
   ├─ uninstall.sh         ← remove GraphVis and all GraphVis data
   ├─ README.md            ← short end-user instructions
   └─ app/                 ← precompiled self-contained runtime directory
      ├─ graphvis
      ├─ libgraphvis_ffi.so
      ├─ qml/
      ├─ plugins/          ← when emitted by Qt deployment
      ├─ lib/              ← deployed non-system runtime libraries
      ├─ share/
      └─ internal/
         └─ runtime-manifest.sha256
```

The Linux build uses Qt's CMake deployment step to collect runtime/QML dependencies, then verifies `ldd` does not report missing shared libraries before packaging. A `.tar.gz` is used so executable bits on `install.sh`, `run.sh`, and `graphvis` are preserved.

## Rust: build-time only

Do **not** ask end users to install Rust. The developer/CI build compiles the Rust workspace in release mode and stages the resulting native library beside the Qt executable:

- Windows: `graphvis_ffi.dll`
- Linux: `libgraphvis_ffi.so`

GraphVis loads that compiled library at runtime. A literal one-file static executable is not the right target for this application because Qt/QML and VTK use plugins/shared libraries. The correct zero-friction target is a **self-contained precompiled runtime bundle** with no Rust/Cargo requirement.

## Python audit

The standard desktop path imports CSV/TSV, Parquet, and Arrow through the native Rust core and opens PDFs through Qt. Python is only used by the optional science/literature service under `services/python/`.

For the first public release, keep Python source in the **source/developer** package, but do not copy it into the base Windows/Linux runtime. This avoids forcing Python, NumPy, SciPy, PyArrow, Torch/VLM dependencies, or a virtual environment onto every end user.

## Graphviz audit and prerequisite logic

The current app/native source does not invoke `dot`, `Graphviz`, `GRAPHVIZ_DOT`, or a Graphviz API. Therefore Graphviz is **not a current runtime prerequisite** and should not increase installer size.

If a future feature truly requires Graphviz, enable the ready prerequisite gate instead of silently failing:

### Windows / NSIS

`installer/windows/Prerequisites.nsh` contains `CheckGraphviz`. Compile with `/DGRAPHVIS_REQUIRE_GRAPHVIZ` to activate it. The function checks `dot.exe`; if missing it explains the problem in plain English and offers to open the official download page.

### Linux / Bash

`packaging/linux/install.sh` enables its Graphviz check when the packaged runtime contains `app/requirements/graphviz.required`. The script first checks for `dot`, then offers automatic installation through `apt`, `dnf`, `zypper`, or `pacman`. If that is not possible, it opens or prints the official Graphviz download page.

## NSIS: 3-step developer build

1. **Install NSIS once.** Download/install NSIS, or on a managed Windows developer machine use `winget install --id NSIS.NSIS -e`.
2. **Build/stage GraphVis.** Run `build.bat`. It builds the native release first, verifies the staged runtime, then invokes `makensis` on `installer\windows\GraphVis.nsi`.
3. **Publish the generated ZIP.** Use `dist\GraphVis-18.4.0-Windows.zip`. Do not publish the raw `.nsi` file as the user-facing installer.

Manual NSIS-only compile, after `build\stage` already exists:

```text
makensis.exe /DGraphVisStage="C:\path\to\GraphVis\build\stage" /DGraphVisOutputDir="C:\path\to\GraphVis\dist\GraphVis-18.4.0-Windows" installer\windows\GraphVis.nsi
```

## Source cleanup decisions

Removed empty folders:

- `build-support/`
- `app/qml/theme/`

Kept developer-only folders because they have real build/test value, but they are excluded from public archives:

- `.cargo/`, `.devcontainer/`, `.github/`, `dev/`, `infra/`, `scripts/`, `tests/`, `tools/`

Kept optional language services in source (`services/python`, `services/julia`, `services/r`) but removed them from the standard CMake install payload.

## Current Linux parity note

The Rust direct WGPU surface constructor is Win32-specific. On non-Windows systems GraphVis now defaults to the VTK/PBR viewport so the Linux package does not present a broken WGPU default. A future Linux-native WGPU surface implementation can restore WGPU as a first-choice renderer there.

## Cargo lockfile for source checkouts

If `native/Cargo.lock` is absent, the Windows bootstrap and Linux release builder generate it automatically so a fresh source checkout can build. Before publishing a production release, review and commit the generated lockfile; the subsequent `cargo fetch --locked` and native builds then use that pinned dependency graph.
