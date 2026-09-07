#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

if [[ ! -x .tooling/vcpkg/vcpkg ]]; then
  bash "$ROOT/install.sh"
fi

APP="$ROOT/build/stage-linux/graphvis"
if [[ ! -x "$APP" ]]; then
  printf '%s\n' 'No native build was found. Creating a standalone build first...'
  bash "$ROOT/build.sh"
fi

if [[ ! -x "$APP" ]]; then
  printf '%s\n' 'ERROR: GraphVis was built but build/stage-linux/graphvis is missing.' >&2
  exit 1
fi

export LD_LIBRARY_PATH="$ROOT/build/stage-linux/lib:$ROOT/build/stage-linux${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$APP" "$@"
