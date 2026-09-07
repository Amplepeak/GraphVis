#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

bash "$ROOT/install.sh"
printf '%s\n' '=== Building the Linux standalone release ==='
bash "$ROOT/tools/Build-LinuxRelease.sh"
