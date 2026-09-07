#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_APP="$ROOT/app"
INSTALL_ROOT="${XDG_DATA_HOME:-$HOME/.local/share}/graphvis/18.4"
BIN_DIR="${XDG_BIN_HOME:-$HOME/.local/bin}"
DESKTOP_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
GRAPHVIZ_URL="https://graphviz.org/download/"

say() { printf '%s\n' "$*"; }
fail() { say ""; say "ERROR: $*" >&2; exit 1; }

open_url() {
  if command -v xdg-open >/dev/null 2>&1; then xdg-open "$1" >/dev/null 2>&1 || true; fi
  say "$1"
}

ensure_graphviz() {
  [[ -f "$SOURCE_APP/requirements/graphviz.required" ]] || return 0
  if command -v dot >/dev/null 2>&1; then
    say "Graphviz found: $(command -v dot)"
    return 0
  fi

  say ""
  say "This GraphVis build includes a feature that needs Graphviz."
  say "Graphviz is not installed on this computer."
  printf 'Install Graphviz automatically now? [Y/n] '
  read -r answer || answer="y"
  case "${answer:-y}" in
    n|N|no|NO) open_url "$GRAPHVIZ_URL"; fail "Install Graphviz, then run install.sh again." ;;
  esac

  if command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update && sudo apt-get install -y graphviz
  elif command -v dnf >/dev/null 2>&1; then
    sudo dnf install -y graphviz
  elif command -v zypper >/dev/null 2>&1; then
    sudo zypper --non-interactive install graphviz
  elif command -v pacman >/dev/null 2>&1; then
    sudo pacman -S --noconfirm graphviz
  else
    open_url "$GRAPHVIZ_URL"
    fail "No supported package manager was detected. Install Graphviz from the URL above, then run install.sh again."
  fi

  command -v dot >/dev/null 2>&1 || { open_url "$GRAPHVIZ_URL"; fail "Graphviz installation did not provide the 'dot' command."; }
}

verify_payload() {
  [[ -x "$SOURCE_APP/graphvis" ]] || fail "The precompiled app/graphvis executable is missing. Re-extract the official Linux release."
  [[ -f "$SOURCE_APP/libgraphvis_ffi.so" ]] || fail "The compiled Rust runtime library libgraphvis_ffi.so is missing."
  if [[ -f "$SOURCE_APP/internal/runtime-manifest.sha256" ]] && command -v sha256sum >/dev/null 2>&1; then
    (cd "$SOURCE_APP" && sha256sum -c internal/runtime-manifest.sha256 --quiet) || fail "Runtime integrity verification failed. Re-download the official release."
  fi
}

say "GraphVis 18.4 Linux installer"
say "No Rust, Cargo, CMake, or Python installation is required."
verify_payload
ensure_graphviz

rm -rf "$INSTALL_ROOT"
mkdir -p "$INSTALL_ROOT" "$BIN_DIR" "$DESKTOP_DIR"
cp -a "$SOURCE_APP/." "$INSTALL_ROOT/"
chmod +x "$INSTALL_ROOT/graphvis"

cat > "$BIN_DIR/graphvis" <<LAUNCHER
#!/usr/bin/env bash
# GraphVis managed launcher - removed by GraphVis uninstall.sh
export LD_LIBRARY_PATH="$INSTALL_ROOT/lib:$INSTALL_ROOT:\${LD_LIBRARY_PATH:-}"
exec "$INSTALL_ROOT/graphvis" "\$@"
LAUNCHER
chmod +x "$BIN_DIR/graphvis"

ICON="$INSTALL_ROOT/share/graphvis/assets/branding/graphvis_icon.png"
cat > "$DESKTOP_DIR/graphvis-18.4.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=GraphVis 18.4
Comment=Native graph visualization
Exec=$BIN_DIR/graphvis
Icon=$ICON
Terminal=false
Categories=Science;Graphics;DataVisualization;
StartupNotify=true
DESKTOP
chmod +x "$DESKTOP_DIR/graphvis-18.4.desktop"

say ""
say "GraphVis installed successfully."
say "Launch it from your application menu or run: $BIN_DIR/graphvis"
