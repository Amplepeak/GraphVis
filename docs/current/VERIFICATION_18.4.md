# GraphVis 18.4 Public Release Verification

## Before building

1. Confirm `native/Cargo.lock` exists and is committed.
2. Run the packaging/architecture acceptance tests.
3. Confirm the source contains no empty `build-support/` or `app/qml/theme/` directories.

## Windows

1. Run `build.bat` on the prepared Windows build machine.
2. Confirm `dist\GraphVis-18.4.0-Windows.zip` exists.
3. Open the ZIP and confirm the root immediately shows `Install.exe`, `Install.bat`, and `README.md`.
4. Confirm `Support\Repair.bat` and `Support\Uninstall.bat` exist.
5. Install on a clean Windows user account with no Rust/Python toolchain and launch GraphVis from the created shortcut.

## Linux

1. Run `./tools/Build-LinuxRelease.sh` on the prepared Linux build machine.
2. Confirm `dist/GraphVis-18.4.0-Linux.tar.gz` exists.
3. Confirm `install.sh`, `run.sh`, and `app/graphvis` are executable inside the tar archive.
4. Install on a clean supported Linux system with no Rust/Python toolchain.
5. Confirm GraphVis starts in VTK/PBR mode and `ldd` reports no missing packaged runtime dependencies.
