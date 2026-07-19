#!/usr/bin/env bash
# Line-coverage report for the C++ core: clang source-based coverage over
# the host test suite (unit tests colocated in src/*_test.cc plus the
# approval suite), reported per production file with llvm-cov.
#
#   tools/coverage.sh          # build, run tests, print the report
#   tools/coverage.sh --html   # ...and write build/host-coverage/html/
#
# Test files and third-party code are excluded from the report; the wasm-
# only files (gl_util, word_renderer, main) never enter the host build.
set -euo pipefail
cd "$(dirname "$0")/.."

export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
BUILD=build/host-coverage

if [ ! -f "$BUILD/build.ninja" ]; then
  cmake --preset host-coverage >/dev/null
fi
cmake --build --preset host-coverage >/dev/null

# One profile per test process (catch_discover_tests runs each case
# separately); merge afterwards.
rm -rf "$BUILD/profraw"
mkdir -p "$BUILD/profraw"
LLVM_PROFILE_FILE="$PWD/$BUILD/profraw/%p.profraw" \
  ctest --preset host-coverage --output-on-failure >/dev/null

xcrun llvm-profdata merge -sparse "$BUILD"/profraw/*.profraw \
  -o "$BUILD/coverage.profdata"

IGNORE='(_test\.cc|tests/|vcpkg_installed|/usr|Xcode)'
xcrun llvm-cov report "$BUILD/words_tests" \
  -instr-profile="$BUILD/coverage.profdata" \
  -ignore-filename-regex="$IGNORE"

# Regenerate the committed COVERAGE.md from the same data. No timestamp,
# so the file only changes when the numbers do; prettier keeps it in the
# repo's markdown format (presubmit checks all .md).
xcrun llvm-cov export "$BUILD/words_tests" \
  -instr-profile="$BUILD/coverage.profdata" \
  -ignore-filename-regex="$IGNORE" \
  -summary-only | python3 tools/coverage-md.py > COVERAGE.md
if [ ! -d node_modules/prettier ]; then
  npm install >/dev/null
fi
npx prettier --write COVERAGE.md >/dev/null

if [ "${1:-}" = "--html" ]; then
  xcrun llvm-cov show "$BUILD/words_tests" \
    -instr-profile="$BUILD/coverage.profdata" \
    -ignore-filename-regex="$IGNORE" \
    -format=html -output-dir="$BUILD/html"
  echo
  echo "detailed report: $BUILD/html/index.html"
fi
