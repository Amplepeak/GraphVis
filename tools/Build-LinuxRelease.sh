#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
VCPKG_ROOT="${VCPKG_ROOT:-$ROOT/.tooling/vcpkg}"
BUILD="$ROOT/build/linux-release"
STAGE="$ROOT/build/stage-linux"
DIST="$ROOT/dist"
NAME="GraphVis-$VERSION-Linux"
OUT="$DIST/$NAME"
ARCHIVE="$DIST/$NAME.tar.gz"

need() { command -v "$1" >/dev/null 2>&1 || { printf 'Missing developer tool: %s\n' "$1" >&2; exit 2; }; }
for tool in cmake ninja cargo tar sha256sum ldd file; do need "$tool"; done
if [[ ! -f "$ROOT/native/Cargo.lock" ]]; then
  echo 'native/Cargo.lock is missing; generating it for this source checkout.' >&2
  cargo generate-lockfile --manifest-path "$ROOT/native/Cargo.toml"
  echo 'Commit native/Cargo.lock before publishing a production release.' >&2
fi
[[ -f "$ROOT/native/Cargo.lock" ]] || { echo 'native/Cargo.lock could not be generated.' >&2; exit 2; }
[[ -n "$VCPKG_ROOT" && -x "$VCPKG_ROOT/vcpkg" ]] || { echo 'Set VCPKG_ROOT to the pinned vcpkg checkout.' >&2; exit 2; }
export VCPKG_ROOT

"$VCPKG_ROOT/vcpkg" install --triplet x64-linux --x-manifest-root="$ROOT"
cargo fetch --locked --manifest-path "$ROOT/native/Cargo.toml"

cd "$ROOT"
cmake --preset linux-release
cmake --build --preset linux-release --parallel
rm -rf "$STAGE"
cmake --install "$BUILD" --prefix "$STAGE"

[[ -x "$STAGE/graphvis" ]] || { echo 'Staged graphvis executable is missing.' >&2; exit 3; }
[[ -f "$STAGE/libgraphvis_ffi.so" ]] || { echo 'Staged libgraphvis_ffi.so is missing.' >&2; exit 3; }
[[ -f "$STAGE/qml/GraphVis/VTK/qmldir" ]] || { echo 'Staged VTK QML module is missing.' >&2; exit 3; }

# Fail packaging if any staged ELF object has an unresolved shared dependency.
while IFS= read -r -d '' f; do
  if file "$f" | grep -q 'ELF'; then
    missing="$(ldd "$f" 2>/dev/null | grep 'not found' || true)"
    if [[ -n "$missing" ]]; then
      printf 'Unresolved runtime dependency in %s:\n%s\n' "$f" "$missing" >&2
      exit 4
    fi
  fi
done < <(find "$STAGE" -type f -print0)

rm -rf "$OUT" "$ARCHIVE"
mkdir -p "$OUT/app/internal"
cp -a "$STAGE/." "$OUT/app/"
cp "$ROOT/packaging/linux/install.sh" "$OUT/install.sh"
cp "$ROOT/packaging/linux/run.sh" "$OUT/run.sh"
cp "$ROOT/packaging/linux/uninstall.sh" "$OUT/uninstall.sh"
cp "$ROOT/packaging/linux/README-LINUX.md" "$OUT/README.md"
chmod +x "$OUT/install.sh" "$OUT/run.sh" "$OUT/uninstall.sh" "$OUT/app/graphvis"

(
  cd "$OUT/app"
  find . -type f ! -path './internal/runtime-manifest.sha256' -print0 \
    | sort -z \
    | xargs -0 sha256sum > internal/runtime-manifest.sha256
)

tar -C "$DIST" -czf "$ARCHIVE" "$NAME"
printf 'Linux public release ready: %s\n' "$ARCHIVE"
