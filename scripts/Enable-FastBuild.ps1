$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$env:CARGO_TARGET_DIR = Join-Path $Root '.cache\cargo-target'
$env:SCCACHE_DIR = Join-Path $Root '.cache\sccache'
$env:VCPKG_BINARY_SOURCES = "clear;files,$(Join-Path $Root '.cache\vcpkg-binaries'),readwrite"
New-Item -ItemType Directory -Force $env:CARGO_TARGET_DIR,$env:SCCACHE_DIR,(Join-Path $Root '.cache\vcpkg-binaries') | Out-Null

if (Get-Command sccache -ErrorAction SilentlyContinue) {
  $env:RUSTC_WRAPPER='sccache'
  $env:CMAKE_C_COMPILER_LAUNCHER='sccache'
  $env:CMAKE_CXX_COMPILER_LAUNCHER='sccache'
} else {
  Remove-Item Env:RUSTC_WRAPPER -ErrorAction SilentlyContinue
}

# rust-lld ships with the Rust toolchain. Use it only after a probe; otherwise keep MSVC link.exe.
$env:GRAPHVIS_FAST_RUST_LINKER='default'
$lld = Get-ChildItem "$env:USERPROFILE\.rustup\toolchains" -Filter 'rust-lld.exe' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if ($lld) {
  $probe = & $lld.FullName '--version' 2>$null
  if ($LASTEXITCODE -eq 0) {
    $env:CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_LINKER=$lld.FullName
    $env:GRAPHVIS_FAST_RUST_LINKER='rust-lld'
  }
}
Write-Host "GraphVis fast environment enabled. Rust linker: $env:GRAPHVIS_FAST_RUST_LINKER"
Write-Host "Cargo target: $env:CARGO_TARGET_DIR"
Write-Host "vcpkg binary cache: $env:VCPKG_BINARY_SOURCES"
