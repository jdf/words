#!/usr/bin/env bash
# Prints the path to the pinned Chrome for Testing binary used by the e2e
# golden tests, downloading it first if needed. The version is pinned in
# tools/chrome-version.txt so goldens never drift with system Chrome
# updates; bump the pin and re-bless deliberately.
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION=$(tr -d '[:space:]' < tools/chrome-version.txt)
CACHE="${WORDS_CHROME_DIR:-$HOME/.cache/words/chrome}"

existing=""
if [ -d "$CACHE" ]; then
  existing=$(find "$CACHE" -type f -name 'Google Chrome for Testing' -path "*$VERSION*" 2>/dev/null | head -1 || true)
fi
if [ -z "$existing" ]; then
  npx -y @puppeteer/browsers install "chrome@$VERSION" --path "$CACHE" >&2
  existing=$(find "$CACHE" -type f -name 'Google Chrome for Testing' -path "*$VERSION*" 2>/dev/null | head -1)
  # macOS quarantines the download; headless launches then hang forever on
  # a Gatekeeper prompt nobody can see. Strip it.
  if [ -n "$existing" ] && command -v xattr >/dev/null; then
    xattr -dr com.apple.quarantine "${existing%%.app/*}.app" 2>/dev/null || true
  fi
fi
if [ -z "$existing" ]; then
  echo "error: could not install Chrome for Testing $VERSION" >&2
  exit 1
fi
echo "$existing"
