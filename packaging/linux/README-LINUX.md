# GraphVis 18.4 for Linux

## Install

Double-click **`install.sh`** (or run `./install.sh`).

The archive already contains the compiled GraphVis runtime. You do **not** need Rust, Cargo, CMake, or Python.

## Run

Use the GraphVis application/menu entry created by the installer, or double-click **`run.sh`**.

If an optional future feature needs an external dependency, the installer will explain what is missing and offer the appropriate install path instead of failing silently.

## Remove GraphVis completely

Double-click **`uninstall.sh`** (or run `./uninstall.sh`) from this extracted release folder. It removes GraphVis itself, its launcher/menu entry, settings, cache, and user data, then removes this extracted folder. It does **not** touch the original downloaded `.tar.gz` archive.
