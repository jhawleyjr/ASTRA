#!/usr/bin/env bash
# Configure/build ASan and run unit-labeled CTest tests; excludes physics and Python.
set -euo pipefail

if [[ $# -ne 0 ]]; then
  echo "Usage: ${0##*/} (no arguments; see docs/process/validation.md)" >&2
  exit 2
fi
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd -- "$repo_root"
if ! command -v just >/dev/null 2>&1; then
  echo "ASTRA requires just; see scripts/setup-ubuntu.sh." >&2
  exit 127
fi
exec just test-unit
