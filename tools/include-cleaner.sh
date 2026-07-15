#!/usr/bin/env bash
# Include hygiene via clang-tidy's misc-include-cleaner: reports (or, with
# --fix, strips/inserts) unused and missing #includes across src/.
#
#   tools/include-cleaner.sh          # check; nonzero exit on findings
#   tools/include-cleaner.sh --fix    # rewrite the files
#
# Needs build/wasm-debug/compile_commands.json (run ./dev once first).
# clang-tidy can't ask em++ for its implicit sysroot the way clangd's
# --query-driver does, so those paths are passed explicitly.
#
# misc-include-cleaner only analyzes each TU's main file, so headers are
# additionally checked as standalone TUs (second clang-tidy invocation with
# explicit flags instead of the compilation database).
set -euo pipefail
cd "$(dirname "$0")/.."

EMSDK="${EMSDK:-$HOME/emsdk}"
TIDY="${TIDY:-/opt/homebrew/opt/llvm/bin/clang-tidy}"
SYSROOT="$EMSDK/upstream/emscripten/cache/sysroot"
VCPKG_INC="build/wasm-debug/vcpkg_installed/wasm32-emscripten/include"

if [ ! -f build/wasm-debug/compile_commands.json ]; then
  echo "error: build/wasm-debug/compile_commands.json not found; run ./dev first" >&2
  exit 1
fi

FIX=()
if [ "${1:-}" = "--fix" ]; then
  FIX=(--fix --fix-errors)
fi

SYS_ARGS=(
  --extra-arg=--target=wasm32-unknown-emscripten
  --extra-arg=-isystem --extra-arg="$SYSROOT/include/fakesdl"
  --extra-arg=-isystem --extra-arg="$SYSROOT/include/compat"
  --extra-arg=-isystem --extra-arg="$SYSROOT/include/c++/v1"
  --extra-arg=-isystem --extra-arg="$SYSROOT/include"
)

"$TIDY" -p build/wasm-debug --quiet ${FIX[@]+"${FIX[@]}"} \
  "${SYS_ARGS[@]}" \
  src/*.cc

"$TIDY" --quiet ${FIX[@]+"${FIX[@]}"} \
  "${SYS_ARGS[@]}" \
  src/*.h -- \
  -x c++-header -std=c++20 \
  -isystem "$VCPKG_INC" -isystem "$VCPKG_INC/harfbuzz"
