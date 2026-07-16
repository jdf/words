#!/usr/bin/env bash
# End-to-end golden image tests: renders the real wasm build in headless
# Chrome on SwiftShader-backed ANGLE (the same GL stack Chrome uses for
# WebGL, minus the GPU) and byte-compares screenshots against the approved
# goldens — one per corpus book, in a script-appropriate font, plus the
# default sample-text cloud.
#
#   tools/e2e-golden.sh          # verify all against tests/goldens/e2e/
#   tools/e2e-golden.sh --bless  # approve the current renderings
#
# Determinism notes: SwiftShader is CPU rasterization — bit-exact for a
# fixed Chrome version on a fixed architecture. The browser is a pinned
# Chrome for Testing build (tools/chrome-version.txt), so system Chrome
# updates can't shift pixels; bump the pin and re-bless deliberately.
set -euo pipefail
cd "$(dirname "$0")/.."

CHROME="${CHROME:-$(tools/get-chrome.sh)}"
PORT=8788
SIZE=1200,750
GOLDEN_DIR=tests/goldens/e2e

CASES=()

# The strategy matrix: one golden per orientation strategy, placement
# strategy, and palette, all over the same corpus (moby-dick) with the
# same seed. Each dimension varies alone while the other two hold fixed
# values, and every image's status line names its configuration. Color
# verification happens here, through the real GL pipeline (SwANGLE), per
# the project's testing rule.
ORIENTATIONS="horizontal mostly-horizontal long-horizontal-likely
              half-and-half mostly-vertical vertical any-which-way"
for o in $ORIENTATIONS; do
  CASES+=("orientation-$o|?corpus=moby-dick&orientation=$o&placement=center-line&palette=blue-meets-orange&variance=little")
done
PLACEMENTS="center-line center"
for p in $PLACEMENTS; do
  CASES+=("placement-$p|?corpus=moby-dick&placement=$p&orientation=mostly-horizontal&palette=chilled-summer&variance=little")
done
PALETTES="bw wb wordly asparagus bluesugar heat ghostly chilled-summer
          blue-meets-orange yramirp"
for p in $PALETTES; do
  CASES+=("palette-$p|?corpus=moby-dick&palette=$p&placement=center-line&orientation=mostly-horizontal&variance=little")
done

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

FAILED=0
for case in "${CASES[@]}"; do
  NAME="${case%%|*}"
  QUERY="${case#*|}"
  GOLDEN="$GOLDEN_DIR/$NAME.png"
  RECEIVED="build/e2e-received-$NAME.png"

  "$CHROME" --headless --disable-gpu --use-angle=swiftshader \
    --window-size="$SIZE" --hide-scrollbars --virtual-time-budget=8000 \
    --screenshot="$RECEIVED" "http://localhost:$PORT/$QUERY" 2>/dev/null

  if [ "${1:-}" = "--bless" ]; then
    mkdir -p "$GOLDEN_DIR"
    cp "$RECEIVED" "$GOLDEN"
    echo "blessed: $GOLDEN ($(wc -c < "$GOLDEN" | tr -d ' ') bytes)"
  elif [ ! -f "$GOLDEN" ]; then
    echo "no golden at $GOLDEN — run with --bless after verifying $RECEIVED" >&2
    FAILED=1
  elif cmp -s "$GOLDEN" "$RECEIVED"; then
    echo "e2e golden OK: $NAME"
  else
    echo "e2e golden MISMATCH: compare $RECEIVED against $GOLDEN" >&2
    FAILED=1
  fi
done

if [ "${1:-}" = "--bless" ]; then
  echo "blessed with chrome $("$CHROME" --version | tr -d '\n')"
  exit 0
fi
exit $FAILED
