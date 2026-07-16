#!/usr/bin/env bash
# End-to-end golden image test: renders the real wasm build in headless
# Chrome on SwiftShader-backed ANGLE (the same GL stack Chrome uses for
# WebGL, minus the GPU) at a frozen scene time, and byte-compares the
# screenshot against the approved golden.
#
#   tools/e2e-golden.sh          # verify against tests/goldens/
#   tools/e2e-golden.sh --bless  # approve the current rendering
#
# Determinism notes: SwiftShader is CPU rasterization — bit-exact for a
# fixed Chrome version on a fixed architecture. The browser is a pinned
# Chrome for Testing build (tools/chrome-version.txt), so system Chrome
# updates can't shift pixels; bump the pin and re-bless deliberately.
set -euo pipefail
cd "$(dirname "$0")/.."

CHROME="${CHROME:-$(tools/get-chrome.sh)}"
PORT=8788
T=2.0
SIZE=1200,750
GOLDEN=tests/goldens/e2e/t2.png
RECEIVED=build/e2e-received.png

# Build the release dist directly (not via ./dev, which would also point
# the dev server at the release build).
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
export EMSDK="${EMSDK:-$HOME/emsdk}"
export PATH="$EMSDK/upstream/emscripten:$PATH"
if [ ! -f build/wasm-release/build.ninja ]; then
  cmake --preset wasm-release >/dev/null
fi
cmake --build --preset wasm-release >/dev/null

python3 tools/serve.py "$PWD/build/wasm-release/dist" "$PORT" &
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null' EXIT
sleep 0.3

"$CHROME" --headless=new --disable-gpu --use-angle=swiftshader \
  --window-size="$SIZE" --hide-scrollbars --virtual-time-budget=8000 \
  --screenshot="$RECEIVED" "http://localhost:$PORT/?t=$T" 2>/dev/null

if [ "${1:-}" = "--bless" ]; then
  mkdir -p "$(dirname "$GOLDEN")"
  cp "$RECEIVED" "$GOLDEN"
  echo "blessed: $GOLDEN ($(wc -c < "$GOLDEN" | tr -d ' ') bytes, chrome $("$CHROME" --version | tr -d '\n'))"
  exit 0
fi

if [ ! -f "$GOLDEN" ]; then
  echo "no golden at $GOLDEN — run with --bless after verifying $RECEIVED" >&2
  exit 1
fi

if cmp -s "$GOLDEN" "$RECEIVED"; then
  echo "e2e golden OK"
else
  echo "e2e golden MISMATCH: compare $RECEIVED against $GOLDEN" >&2
  exit 1
fi
