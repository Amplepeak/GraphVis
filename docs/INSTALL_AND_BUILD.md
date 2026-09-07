# Install, Run, and Build

GraphVis has two supported paths. Keep them separate: source checkouts are for developers, while the archives produced in `dist/` are for end users.

## Developer source checkout

Run every command from this directory, the repository root.

| Goal | Windows | Linux |
| --- | --- | --- |
| Set up the native developer toolchain | Double-click `install.bat` | `bash install.sh` |
| Build a redistributable native package | Double-click `build.bat` | `bash build.sh` |
| Build if necessary and launch a developer copy | Double-click `run.bat` | `bash run.sh` |

`install.bat` and `install.sh` set up only the native stack: CMake, Ninja, the pinned Rust toolchain, and the pinned vcpkg dependency manager. Windows uses `winget` when a developer tool is absent; Linux detects a supported package manager and installs missing native prerequisites. The Qt/VTK/Arrow dependencies are restored during the build. Python is not used by this workflow.

## End-user release

`build.bat` writes the Windows distribution to `dist/GraphVis-<version>-Windows.zip`. Extract it and double-click `Install.exe`; the installed app needs no Python, Rust, Cargo, CMake, or compiler. Use the release folder's `Uninstall.bat` to remove the installed app, all GraphVis settings/cache, and the extracted release folder while preserving the original ZIP.

`build.sh` writes the Linux distribution to `dist/GraphVis-<version>-Linux.tar.gz`. Extract it, run the package's `install.sh`, then use its `run.sh` or the desktop-menu entry. `uninstall.sh` removes the installed runtime, launcher, menu entry, settings/cache/data, and extracted release folder while preserving the original archive. The packaged runtime contains the executable, Qt/QML modules, native libraries, and application assets.

Build Windows artifacts on Windows and Linux artifacts on Linux. This project uses CMake's Qt deployment and native staging rather than PyInstaller, because the desktop executable is C++/Qt with Rust and VTK libraries, not a Python GUI.

## Root layout

```text
GraphVis/
├── install.bat / install.sh       # native developer toolchain setup
├── run.bat / run.sh               # build-if-needed developer launchers
├── build.bat / build.sh           # native release package builders
├── build/                         # generated native staging area
├── dist/                          # generated end-user archives/installers
├── app/                           # Qt/QML desktop application
├── native/                        # Rust, VTK, and native integrations
├── services/python/               # optional add-on, not in the main workflow
├── assets/                        # branding and installable data
├── packaging/ and installer/      # end-user package definitions
├── tools/                         # native build orchestration
└── docs/                          # project and release documentation
```
