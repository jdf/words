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

# name|query — gentium covers accented Latin, Greek, and Cyrillic;
# sbl-hebrew and scheherazade cover the RTL scripts. (No corpus case for
# rashomon/lunyu: the font collection has no CJK ideographs, same as the
# original Wordle.)
CASES=(
  "t2|?t=2"
  "moby-dick|?corpus=moby-dick"
  "monte-cristo|?corpus=monte-cristo&font=gentium"
  "die-verwandlung|?corpus=die-verwandlung&font=gentium"
  "don-quijote|?corpus=don-quijote&font=gentium"
  "divina-commedia|?corpus=divina-commedia&font=gentium"
  "os-lusiadas|?corpus=os-lusiadas&font=gentium"
  "kalevala|?corpus=kalevala&font=gentium"
  "belye-nochi|?corpus=belye-nochi&font=gentium"
  "hatzofe|?corpus=hatzofe&font=sbl-hebrew"
  "kalila-wa-dimna|?corpus=kalila-wa-dimna&font=scheherazade"
  # Color verification happens here, through the real GL pipeline: one
  # chromatic palette (hue variance, white background) and one achromatic
  # (brightness variance — the hue<0.01 branch), per the original's
  # PaletteManager/ColorVariance semantics.
  "wordly|?corpus=moby-dick&palette=wordly&variance=some"
  "heat-wild|?corpus=moby-dick&palette=heat&variance=wild"
  # Arbitrary rotations through the real renderer (OrientationStrategy's
  # ANY_WHICH_WAY): angled words, angled HBB collisions, angled stencil
  # fills.
  "any-which-way|?corpus=moby-dick&orientation=any-which-way"
)

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
