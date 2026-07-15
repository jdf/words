#!/usr/bin/env bash
# Gate for `jj push-main` (see the alias: runs this before moving `main`).
# Set SKIP_PRESUBMIT=1 to bypass.
set -euo pipefail
cd "$(dirname "$0")"

echo "== include hygiene (clang-tidy misc-include-cleaner)"
tools/include-cleaner.sh

echo "== presubmit OK"
