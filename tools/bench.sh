#!/usr/bin/env bash
# Runs the wasm runtime microbenchmarks under node. All arguments pass
# through to Google Benchmark (e.g. --benchmark_filter=BM_BuildWord,
# --benchmark_repetitions=10).
#
# These are wasm numbers, not native ones: the benchmark binary is the
# wasm-release build executed by node's V8, the same engine Chrome runs.
set -euo pipefail
cd "$(dirname "$0")/.."

export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
export EMSDK="${EMSDK:-$HOME/emsdk}"
export PATH="$EMSDK/upstream/emscripten:$PATH"

if [ ! -f build/wasm-release/build.ninja ]; then
  cmake --preset wasm-release >/dev/null
fi
cmake --build --preset wasm-release >/dev/null

node build/wasm-release/words_bench.js "$@"
