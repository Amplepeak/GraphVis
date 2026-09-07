# GraphVis 18.4 Packaging / DevEx Specification

## Public end-user surface

Windows publishes `dist/GraphVis-18.4.0-Windows.zip`. Its root contains `Install.exe`, `Install.bat`, and `README.md`; repair/removal helpers are under `Support/`.

Linux publishes `dist/GraphVis-18.4.0-Linux.tar.gz`. Its root contains executable `install.sh` and `run.sh`, a short README, and the precompiled self-contained runtime under `app/`.

End users never need Rust, Cargo, CMake, vcpkg, or NSIS.

## Runtime dependencies

The Rust native core is compiled during developer/CI builds and distributed as `graphvis_ffi.dll` or `libgraphvis_ffi.so`.

The current desktop/native runtime does not call Graphviz, so Graphviz is not shipped as a mandatory dependency. `installer/windows/Prerequisites.nsh` and the Linux installer contain ready-to-enable friendly Graphviz checks for a future feature that actually requires it.

Python/Julia/R services are optional extension source and are not copied into the standard public runtime.

## State tracking

Windows writes `%LOCALAPPDATA%\GraphVis\18.4\.setup_complete` only after the executable, Rust native library, VTK QML module/plugin, and SHA-256 runtime manifest validate.

## Developer surface

Windows developers use `install.bat` once and `run.bat` for normal work. Public Windows packaging uses `build.bat`.

Linux public packaging uses `tools/Build-LinuxRelease.sh` from a prepared build machine with the pinned Rust/vcpkg dependency graph.

See `docs/PUBLIC_RELEASE.md` for the end-user folder trees and NSIS compile instructions.
