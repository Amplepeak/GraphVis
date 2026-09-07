#!/usr/bin/env bash
set -euo pipefail

__graphvis_env_pause_on_error() {
  rc=$?
  if [[ $rc -ne 0 && -t 0 && "${BASH_SOURCE[0]}" == "$0" ]]; then
    printf "\nGraphVis environment setup failed with exit code %s.\n" "$rc" >&2
    read -r -p "Press Enter to close..." _ || true
  fi
}
trap __graphvis_env_pause_on_error EXIT
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export CARGO_TARGET_DIR="${CARGO_TARGET_DIR:-$ROOT/.cache/cargo-target}"
export SCCACHE_DIR="${SCCACHE_DIR:-$ROOT/.cache/sccache}"
mkdir -p "$CARGO_TARGET_DIR" "$SCCACHE_DIR" "$ROOT/.cache/vcpkg-binaries"

if command -v sccache >/dev/null 2>&1; then
  export RUSTC_WRAPPER=sccache
else
  unset RUSTC_WRAPPER || true
fi

# Fast linkers are opportunistic only. If unavailable, preserve the default linker.
case "$(uname -s)" in
  Linux*)
    if command -v mold >/dev/null 2>&1; then
      export RUSTFLAGS="${RUSTFLAGS:-} -C link-arg=-fuse-ld=mold"
      export CMAKE_EXE_LINKER_FLAGS="${CMAKE_EXE_LINKER_FLAGS:-} -fuse-ld=mold"
      export CMAKE_SHARED_LINKER_FLAGS="${CMAKE_SHARED_LINKER_FLAGS:-} -fuse-ld=mold"
      GV_LINKER=mold
    elif command -v ld.lld >/dev/null 2>&1; then
      export RUSTFLAGS="${RUSTFLAGS:-} -C link-arg=-fuse-ld=lld"
      export CMAKE_EXE_LINKER_FLAGS="${CMAKE_EXE_LINKER_FLAGS:-} -fuse-ld=lld"
      export CMAKE_SHARED_LINKER_FLAGS="${CMAKE_SHARED_LINKER_FLAGS:-} -fuse-ld=lld"
      GV_LINKER=lld
    else GV_LINKER=system; fi
    ;;
  Darwin*)
    if command -v ld64.lld >/dev/null 2>&1; then
      export RUSTFLAGS="${RUSTFLAGS:-} -C linker=clang -C link-arg=-fuse-ld=lld"
      GV_LINKER=lld
    else GV_LINKER=system; fi
    ;;
  *) GV_LINKER=system ;;
esac

if [[ "${1:-}" == "--print" ]]; then
  printf 'CARGO_TARGET_DIR=%s\nSCCACHE_DIR=%s\nRUSTC_WRAPPER=%s\nLINKER=%s\n' \
    "$CARGO_TARGET_DIR" "$SCCACHE_DIR" "${RUSTC_WRAPPER:-<default>}" "$GV_LINKER"
fi
