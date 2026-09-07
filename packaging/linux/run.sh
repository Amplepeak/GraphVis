#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
INSTALLED="${XDG_DATA_HOME:-$HOME/.local/share}/graphvis/18.4/graphvis"
if [[ -x "$INSTALLED" ]]; then
  INSTALL_ROOT="$(dirname "$INSTALLED")"
  export LD_LIBRARY_PATH="$INSTALL_ROOT/lib:$INSTALL_ROOT:${LD_LIBRARY_PATH:-}"
  exec "$INSTALLED" "$@"
fi
if [[ -x "$ROOT/app/graphvis" ]]; then
  export LD_LIBRARY_PATH="$ROOT/app/lib:$ROOT/app:${LD_LIBRARY_PATH:-}"
  exec "$ROOT/app/graphvis" "$@"
fi
printf 'GraphVis is not installed and app/graphvis is missing. Run install.sh first.\n' >&2
exit 2
