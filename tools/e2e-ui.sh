#!/usr/bin/env bash
# End-to-end UI behavior tests: drive the real page (UI mode, release
# wasm build) in the pinned headless Chrome and assert the page↔engine
# message routing — color-only changes take the recolor fast path,
# commits of previewed changes dedupe to nothing, and the App-Colors
# crossing still relayouts. Complements the pixel goldens
# (tools/e2e-golden.sh), which run ?no-ui and never see the toolbar.
# The cases live in tools/e2e-ui.mjs.
#
#   tools/e2e-ui.sh
set -euo pipefail
cd "$(dirname "$0")/.."

CHROME="${CHROME:-$(tools/get-chrome.sh)}"
PORT=8789

# Build the release dist directly (a no-op when e2e-golden.sh just ran).
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
export EMSDK="${EMSDK:-$HOME/emsdk}"
export PATH="$EMSDK/upstream/emscripten:$PATH"
if [ ! -f build/wasm-release/build.ninja ]; then
  cmake --preset wasm-release >/dev/null
fi
cmake --build --preset wasm-release >/dev/null

if [ ! -d node_modules/puppeteer-core ]; then
  npm install >/dev/null
fi

# The harness owns this port; kill only a leftover LISTENER (see
# e2e-golden.sh for why not a bare -ti).
lsof -ti :"$PORT" -sTCP:LISTEN 2>/dev/null | xargs kill 2>/dev/null || true

python3 tools/serve.py "$PWD/build/wasm-release/dist" "$PORT" &
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null' EXIT
for _ in $(seq 1 50); do
  curl -sf -o /dev/null "http://localhost:$PORT/" && break
  sleep 0.1
done

node tools/e2e-ui.mjs "$CHROME" "http://localhost:$PORT/"
