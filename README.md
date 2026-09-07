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
