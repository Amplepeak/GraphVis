#!/usr/bin/env bash
set -euo pipefail

__graphvis_pause_on_error() {
  rc=$?
  if [[ $rc -ne 0 && -t 0 ]]; then
    printf "\nGraphVis audit failed with exit code %s.\n" "$rc" >&2
    read -r -p "Press Enter to close..." _ || true
  fi
}
trap __graphvis_pause_on_error EXIT
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NATIVE="$ROOT/native"
APPLY=0
[[ "${1:-}" == "--apply" ]] && APPLY=1

log(){ printf '\n== %s ==\n' "$*"; }
need(){ command -v "$1" >/dev/null 2>&1 || { echo "missing required tool: $1" >&2; exit 2; }; }
need cargo
need rustc
cd "$NATIVE"

if [[ ! -f Cargo.lock ]]; then
  echo "ERROR: native/Cargo.lock is missing. Generate it once with the pinned Rust toolchain, review it, commit it, then rerun." >&2
  exit 3
fi

log "Deterministic dependency resolution"
cargo metadata --locked --format-version 1 >/dev/null
cargo fetch --locked

log "Formatting"
cargo fmt --all -- --check

log "Clippy: warnings are errors"
cargo clippy --workspace --all-targets --all-features --locked -- -D warnings

log "Unit/integration tests"
cargo test --workspace --all-features --locked

log "Unused dependencies: cargo-machete"
if command -v cargo-machete >/dev/null 2>&1 || cargo machete --version >/dev/null 2>&1; then
  set +e
  cargo machete --with-metadata
  machete_rc=$?
  set -e
  if [[ $machete_rc -eq 1 && $APPLY -eq 1 ]]; then
    echo "Unused dependencies were detected. Automatic mutation is deliberately guarded."
    echo "Review the report and edit Cargo.toml, then rerun this script; this prevents macro/build-script false-positive deletion."
    exit 10
  elif [[ $machete_rc -gt 1 ]]; then exit $machete_rc; fi
else
  echo "cargo-machete not installed; install with: cargo install --locked cargo-machete"
fi

log "Compile-backed unused dependency check (nightly, optional)"
if rustup toolchain list 2>/dev/null | grep -q '^nightly'; then
  if cargo +nightly udeps --version >/dev/null 2>&1; then
    cargo +nightly udeps --workspace --all-targets
  else
    echo "cargo-udeps unavailable; install with: cargo +nightly install --locked cargo-udeps"
  fi
else
  echo "nightly not installed; skipping cargo-udeps. Stable build integrity is unaffected."
fi

log "Dead/legacy source markers"
if command -v rg >/dev/null 2>&1; then
  rg -n --hidden --glob '!target/**' --glob '!.cache/**' \
    '(TODO\(remove\)|DEPRECATED|deprecated shim|compatibility shim|legacy shim|migration-only|obsolete)' "$ROOT" || true
else
  grep -RInE 'TODO\(remove\)|DEPRECATED|deprecated shim|compatibility shim|legacy shim|migration-only|obsolete' "$ROOT" --exclude-dir=.cache --exclude-dir=target || true
fi

log "Duplicate Rust functions (candidate scan, not destructive)"
python3 - "$NATIVE" <<'PY'
import re, sys
from pathlib import Path
root=Path(sys.argv[1]); seen={}
for p in root.rglob('*.rs'):
    txt=p.read_text(errors='ignore')
    for m in re.finditer(r'(?m)^\s*(?:pub(?:\([^)]*\))?\s+)?fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(',txt):
        seen.setdefault(m.group(1),[]).append(str(p.relative_to(root)))
for name,paths in sorted(seen.items()):
    if len(set(paths))>1:
        print(f'{name}: {", ".join(sorted(set(paths)))}')
PY

log "Repository artifact hygiene"
find "$ROOT" -type d \( -name target -o -name __pycache__ -o -name .pytest_cache -o -name build \) -prune -print
find "$ROOT" -type f \( -name '*.pyc' -o -name '*.pyo' -o -name '*.tmp' -o -name '*.bak' \) -print
if [[ $APPLY -eq 1 ]]; then
  find "$ROOT" -type d \( -name __pycache__ -o -name .pytest_cache \) -prune -exec rm -rf {} + || true
  find "$ROOT" -type f \( -name '*.pyc' -o -name '*.pyo' -o -name '*.tmp' -o -name '*.bak' \) -delete || true
fi

log "Release build integrity"
cargo build --workspace --release --locked
printf '\nAUDIT PASS: deterministic Rust workspace is formatted, lint-clean, tested, and release-buildable.\n'
