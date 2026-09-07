#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
VCPKG_ROOT="$ROOT/.tooling/vcpkg"
# See tools/Bootstrap-WindowsBuild.ps1 for why this is read from vcpkg.json
# rather than written out again here.
VCPKG_COMMIT="$(sed -n 's/.*"builtin-baseline"[[:space:]]*:[[:space:]]*"\([0-9a-f]\{40\}\)".*/\1/p' "$ROOT/vcpkg.json" | head -1)"
# Not fail() - that is defined further down and this line runs before it.
[[ -n "$VCPKG_COMMIT" ]] || { printf 'ERROR: vcpkg.json has no builtin-baseline; cannot pin vcpkg.\n' >&2; exit 1; }
RUST_VERSION='1.98.0'

say() { printf '%s\n' "$*"; }
fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
need_sudo() { [[ ${EUID:-$(id -u)} -eq 0 ]] || command -v sudo >/dev/null 2>&1 || fail 'Administrator access (sudo) is needed to install native build prerequisites.'; }
run_root() { if [[ ${EUID:-$(id -u)} -eq 0 ]]; then "$@"; else sudo "$@"; fi; }

install_native_prerequisites() {
  local missing=0
  for tool in git cmake ninja c++ make curl unzip zip; do command -v "$tool" >/dev/null 2>&1 || missing=1; done
  [[ $missing -eq 0 ]] && return

  need_sudo
  say 'Installing missing native build prerequisites...'
  if command -v apt-get >/dev/null 2>&1; then
    run_root apt-get update
    run_root apt-get install -y build-essential cmake ninja-build git curl zip unzip pkg-config
  elif command -v dnf >/dev/null 2>&1; then
    run_root dnf install -y gcc-c++ make cmake ninja-build git curl zip unzip pkgconf-pkg-config
  elif command -v zypper >/dev/null 2>&1; then
    run_root zypper --non-interactive install gcc-c++ make cmake ninja git curl zip unzip pkg-config
  elif command -v pacman >/dev/null 2>&1; then
    run_root pacman -Sy --needed --noconfirm base-devel cmake ninja git curl zip unzip pkgconf
  else
    fail 'No supported package manager was found. Install a C++ compiler, CMake, Ninja, Git, curl, zip, unzip, and pkg-config, then run install.sh again.'
  fi
}

install_native_prerequisites

if ! command -v cargo >/dev/null 2>&1; then
  say 'Installing the pinned Rust toolchain...'
  curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain "$RUST_VERSION"
fi
[[ -f "$HOME/.cargo/env" ]] && source "$HOME/.cargo/env"
command -v rustup >/dev/null 2>&1 || fail 'Rustup installation did not complete successfully.'
rustup toolchain install "$RUST_VERSION" --profile minimal

if [[ ! -d "$VCPKG_ROOT/.git" ]]; then
  say 'Downloading the pinned native dependency manager...'
  mkdir -p "$ROOT/.tooling"
  git clone --filter=blob:none https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
fi
git -C "$VCPKG_ROOT" fetch --depth 1 origin "$VCPKG_COMMIT"
git -C "$VCPKG_ROOT" checkout --detach "$VCPKG_COMMIT"
if [[ ! -x "$VCPKG_ROOT/vcpkg" ]]; then
  "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
fi

say '[OK] Native developer environment is ready.'
say 'Run ./build.sh for a distributable package or ./run.sh to build and launch GraphVis.'
