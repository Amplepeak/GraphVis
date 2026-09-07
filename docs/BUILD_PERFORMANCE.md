# Build and installation performance

GraphVis end users should receive a precompiled installer. Rust/CMake/vcpkg provisioning is a developer/CI concern only.

## 18.4 build-performance changes

1. Rust is pinned to 1.98.0; normal builds never run `rustup update stable`.
2. Release builds require a committed `native/Cargo.lock` and use `--locked` throughout.
3. vcpkg is pinned by `builtin-baseline` and a fixed checkout commit.
4. vcpkg binary packages are stored in `.cache/vcpkg-binaries`; GitHub Actions caches that directory with `actions/cache` rather than the removed `x-gha` provider.
5. Cargo targets persist in `.cache/cargo-target`, outside disposable CMake trees.
6. `sccache` wraps Rust and C/C++ compilers when available; it is downloaded as a checksum-verified prebuilt binary during Windows bootstrap.
7. CMake 3.29+ selects mold/LLD only when available and otherwise falls back to the platform linker.
8. `windows-fast` uses the Rust `fast` Cargo profile instead of compiling the core with full `--release` settings.
9. `cargo-chef` pre-warms third-party Rust dependencies in the dev container image.
10. `scripts/Build-WithCacheFallback.ps1` retries a failed cached build with caches disabled/purged, preserving the same lockfiles/toolchain/features.

## Daily loop

```powershell
run.bat
```

This does not invoke Winget, rustup updates, Git fetches, vcpkg install, or cargo fetch after the one-time developer bootstrap.

## Full release

```powershell
build.bat
```

Release builds intentionally restore/check the pinned dependency graph before compiling.
