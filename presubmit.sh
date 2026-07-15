#!/usr/bin/env bash
# Gate for `jj push-main` (see the alias: runs this before moving `main`).
# Set SKIP_PRESUBMIT=1 to bypass.
set -euo pipefail
cd "$(dirname "$0")"

# Only check src/ files that changed relative to the remote main. (A header
# change can in principle alter include-cleaner findings in files that
# include it; run `tools/include-cleaner.sh` with no arguments for the full
# sweep.)
CHANGED=$(jj diff --from 'trunk()' --to @ --summary 2>/dev/null |
  awk '$1 != "D" {print $NF}' | grep -E '^src/.*\.(cc|h)$' || true)
EXISTING=()
for f in $CHANGED; do
  [ -f "$f" ] && EXISTING+=("$f")
done

if [ "${#EXISTING[@]}" -eq 0 ]; then
  echo "== include hygiene: no changed src/ files, skipping"
else
  echo "== include hygiene (${#EXISTING[@]} changed file(s))"
  tools/include-cleaner.sh "${EXISTING[@]}"
fi

echo "== presubmit OK"
