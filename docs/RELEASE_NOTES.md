# GraphVis 18.4 Release Notes

## Public-release packaging refresh

- Windows now ships as an obvious root-level `Install.exe` package with a fallback `Install.bat` and uncluttered `Support/` helpers.
- Linux now has a dedicated `.tar.gz` public-release layout with executable `install.sh` and `run.sh` wrappers.
- Rust is build-time only; public archives carry the compiled native runtime library.
- Optional Python/Julia/R service source is no longer copied into the base desktop runtime.
- Graphviz was removed from the mandatory runtime after source audit found no Graphviz calls in the app/native code. Friendly prerequisite hooks remain ready if a future feature actually needs it.
- Empty `build-support/` and `app/qml/theme/` folders were removed.
- Non-Windows builds default to VTK/PBR because the direct WGPU native-surface constructor is currently Win32-specific.
