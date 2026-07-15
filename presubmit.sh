#!/usr/bin/env bash
# Gate for `jj push-main` (see the alias: runs this before moving `main`).
# Set SKIP_PRESUBMIT=1 to bypass.
set -euo pipefail
cd "$(dirname "$0")"

# Only check src/ files that changed relative to the remote main. Since
# every file explicitly includes every name it touches (enforced here), a
# header's own include changes can't affect findings in unchanged dependents
# — checking changed files is sound. The one exception is *moving* a
# declaration between headers, which re-attributes the symbol's provider in
# unchanged includers; after such a refactor, run `tools/include-cleaner.sh`
# with no arguments for a full sweep.
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
