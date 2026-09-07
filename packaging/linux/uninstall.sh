#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
CACHE_HOME="${XDG_CACHE_HOME:-$HOME/.cache}"
STATE_HOME="${XDG_STATE_HOME:-$HOME/.local/state}"
INSTALL_ROOT="$DATA_HOME/graphvis/18.4"
BIN_DIR="${XDG_BIN_HOME:-$HOME/.local/bin}"
DESKTOP_FILE="$DATA_HOME/applications/graphvis-18.4.desktop"
LAUNCHER="$BIN_DIR/graphvis"

fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
remove_dir() { [[ -d "$1" ]] && rm -rf -- "$1"; }

# This protects against ever treating an arbitrary directory as a release root.
[[ "$ROOT" != "/" && -f "$ROOT/README.md" && -f "$ROOT/uninstall.sh" && -e "$ROOT/app/graphvis" ]] \
  || fail 'Run uninstall.sh from the extracted GraphVis release folder.'

printf '%s\n' 'Removing GraphVis and all GraphVis-owned user data...'
if [[ -f "$LAUNCHER" ]] && grep -Fq "$INSTALL_ROOT/graphvis" "$LAUNCHER"; then
  rm -f -- "$LAUNCHER"
fi
rm -f -- "$DESKTOP_FILE"
remove_dir "$DATA_HOME/graphvis"
remove_dir "$DATA_HOME/GraphVis"
remove_dir "$CONFIG_HOME/GraphVis"
remove_dir "$CACHE_HOME/GraphVis"
remove_dir "$STATE_HOME/GraphVis"
rm -f -- "${TMPDIR:-/tmp}/graphvis18-query.arrow"

printf '%s\n' 'GraphVis has been removed. The original downloaded archive is not touched.'
printf '%s\n' 'Removing this extracted release folder...'
GRAPHVIS_RELEASE_ROOT="$ROOT" nohup bash -c 'sleep 1; rm -rf -- "$GRAPHVIS_RELEASE_ROOT"' >/dev/null 2>&1 &
