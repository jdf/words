#!/usr/bin/env bash
# Stamp dist with the build identifier: the working copy's commit id
# (jj's @ is itself a commit, so the id is exact even mid-edit) and the
# build date. Writes only on change, so dependents don't rebuild.
#
#   tools/build-info.sh <output.js>
set -euo pipefail
OUT="$1"

if command -v jj >/dev/null 2>&1 && jj root >/dev/null 2>&1; then
  ID=$(jj log --no-graph -r @ -T 'commit_id.short(12)' 2>/dev/null)
else
  ID=$(git rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
fi
DATE=$(date -u +%Y-%m-%d)

TMP="$OUT.tmp"
printf "window.__wordsBuild = { id: '%s', date: '%s' };\n" "$ID" "$DATE" > "$TMP"
if [ -f "$OUT" ] && cmp -s "$TMP" "$OUT"; then
  rm "$TMP"
else
  mv "$TMP" "$OUT"
fi
