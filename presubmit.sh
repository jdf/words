#!/usr/bin/env bash
# Gate for `jj push-main` (see the alias: runs this before moving `main`).
# Set SKIP_PRESUBMIT=1 to bypass.
set -euo pipefail
cd "$(dirname "$0")"

# Markdown formatting: prettier with proseWrap always (.prettierrc),
# the same formatter VS Code uses on save.
echo "== markdown format"
if [ ! -d node_modules/prettier ]; then
  npm install >/dev/null
fi
if ! npx prettier --check "**/*.md" >/dev/null; then
  echo "markdown needs formatting: npx prettier --write '**/*.md'" >&2
  exit 1
fi

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

# Golden tests, gated on anything that can affect geometry or rendering.
CHANGED_TESTABLE=$(jj diff --from 'trunk()' --to @ --summary 2>/dev/null |
  awk '$1 != "D" {print $NF}' | grep -cE '^(src|tests|web|assets)/' || true)
if [ "${CHANGED_TESTABLE:-0}" -gt 0 ]; then
  # Tier 1: geometry approvals (fast once the host preset exists; the
  # first run configures it, which builds the native dependencies).
  echo "== geometry approval tests"
  if [ ! -f build/host-test/build.ninja ]; then
    cmake --preset host-test >/dev/null
  fi
  cmake --build --preset host-test >/dev/null
  ctest --preset host-test --output-on-failure 2>&1 | tail -2

  # Tier 2: e2e raster golden via pinned Chrome for Testing + SwiftShader.
  echo "== e2e image golden"
  tools/e2e-golden.sh
else
  echo "== golden tests: no src/tests/web/assets changes, skipping"
fi

echo "== presubmit OK"
