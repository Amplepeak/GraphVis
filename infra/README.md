# GraphVis pre-warmed build environments

## Rust audit/dev image

`Dockerfile.rust-dev` uses cargo-chef so dependency compilation is isolated in a cacheable layer.
Build it once or pull the CI-published GHCR image, then bind-mount the source tree.

```bash
docker build -f infra/Dockerfile.rust-dev -t graphvis-rust-dev:18.4 .
docker run --rm -it -v "$PWD:/workspace" graphvis-rust-dev:18.4
```

The Docker image is for Rust core/audit work. The Windows Qt/VTK desktop release remains built on a
Windows runner because its deployed ABI is MSVC + native Qt/VTK.

## Cache integrity model

Caches are accelerators only. They are never the source of dependency truth:

* Rust version: `rust-toolchain.toml`
* Rust graph: committed `native/Cargo.lock`
* C/C++ graph: `vcpkg.json` + pinned `builtin-baseline`
* release flags: CMake presets and Cargo profiles

`scripts/Build-WithCacheFallback.ps1` retries once with compiler/package caches purged. If the clean
retry fails, the build fails; it never changes dependencies or features to make the build pass.
