# words

Wordle word clouds, reimplemented in C++ / WebAssembly / WebGL2.

[![words demo — Moby-Dick as a word cloud in the yramirP palette](media/demo.png)](https://jdf.github.io/words/?palette=yramirp&variance=little)

**[Live demo](https://jdf.github.io/words/?palette=yramirp&variance=little)** —
this exact cloud, interactive: 🔄 re-layouts, with undo/redo. A release build
deployed to GitHub Pages by [CI](.github/workflows/deploy.yml) on every push to
`main`. (GitHub READMEs can't run WebAssembly, so the image above links to the
real thing.)

## Dev loop

```sh
./dev            # configure (first time) + build + start dev server
```

then open / reload <http://localhost:8787/>. That's the whole loop: edit,
`./dev`, reload. In VS Code, the default build task (⇧⌘B) runs the same thing,
and the "Run words (Chrome)" launch config builds and opens Chrome with the
debugger attached.

`./dev release` builds and serves an optimized build instead.

## Toolchain

- **CMake ≥ 3.25 + Ninja**, driven by `CMakePresets.json` (`wasm-debug`,
  `wasm-release`).
- **vcpkg** in manifest mode (`vcpkg.json`), triplet `wasm32-emscripten`,
  chainloading the Emscripten toolchain. Add dependencies to `vcpkg.json`; the
  next configure installs them.
- **Emscripten** via emsdk, expected at `~/emsdk` (override with `$EMSDK`).
  vcpkg is expected at `~/vcpkg` (override with `$VCPKG_ROOT`).

## Dependencies

FreeType (font loading, glyph outlines), HarfBuzz (shaping/kerning), and
Clipper2 (polygon booleans), all built for `wasm32-emscripten` by vcpkg. Two
local workarounds, both of which should be revisited on toolchain updates:

- `triplets/` overlays the community triplet so the chainloaded toolchain is
  `triplets/emscripten-chainload.cmake` (Emscripten's toolchain plus extra
  compile flags — the stock triplet's direct chainload silently drops
  `VCPKG_C(XX)_FLAGS`). Currently it defines
  `HB_NO_PRAGMA_GCC_DIAGNOSTIC_ERROR`, because emsdk's clang 23 fires
  `-Wunused-template` inside HarfBuzz's headers, which HarfBuzz's own pragmas
  promote to errors.
- `ports/harfbuzz` overlays the stock port to pass `-Dutilities=disabled`; the
  hb-gpu-* utilities fail to link on wasm (mixed `-pthread` objects) and we only
  need the library.

The default font (Typodermic's Sexsmith, CC0 public domain) is preloaded from
`assets/fonts/` into the Emscripten virtual FS; the rest of the original
Wordle's font collection (see `assets/fonts/README.md`) is served statically and
fetched on demand via `?font=<basename>`. All text geometry is shaped and
extracted in integer font units (HarfBuzz scale pinned to upem,
`FT_LOAD_NO_SCALE`) and transformed to screen space exactly once, at draw time.

## Layout

- `src/` — C++ sources.
- `web/` — static harness (HTML/CSS/JS); copied into the build's `dist/` by the
  build, so it goes through the same `./dev` + reload loop.
- `tools/serve.py` — dev server (no-store caching, COOP/COEP for future
  wasm-threads support).
- `build/<preset>/dist/` — everything the browser loads.

## Testing

Two tiers of golden-image tests:

- **Geometry approvals**
  (`cmake --preset host-test && ctest --preset host-test`): the GL-free world
  model builds natively and serializes the demo scene to SVG; ApprovalTests
  compares against `tests/goldens/geometry/*.approved.svg`. The approved files
  are viewable images — open them in a browser. On a mismatch, inspect the
  `.received` file and rename it over the `.approved` one to bless. Runs in the
  presubmit when `src/` or `tests/` changed.
- **SwANGLE e2e** (`tools/e2e-golden.sh`): renders the real wasm release build
  in headless Chrome on SwiftShader-backed ANGLE — CPU rasterization,
  byte-deterministic for a fixed browser version — at a frozen scene time
  (`?t=<seconds>` URL parameter), and byte-compares the screenshot against
  `tests/goldens/e2e/`. The browser is a pinned
  [Chrome for Testing](https://googlechromelabs.github.io/chrome-for-testing/)
  build (version in `tools/chrome-version.txt`, auto-downloaded to
  `~/.cache/words/chrome` by `tools/get-chrome.sh`), so system browser updates
  can't shift pixels. `--bless` approves the current rendering; bump the pin and
  re-bless deliberately.

## Benchmarks

```sh
tools/bench.sh                                   # all benchmarks
tools/bench.sh --benchmark_filter=BM_BuildWord   # google-benchmark args pass through
```

Google Benchmark compiled to wasm and run under node — the same V8 that runs the
app in Chrome, so these are wasm runtime numbers, not native ones. Built only in
the `wasm-release` preset (vcpkg feature `bench`); sources in `bench/`.

## Hygiene

`./presubmit.sh` (run automatically by `jj push-main`; skip with
`SKIP_PRESUBMIT=1`) enforces include hygiene via clang-tidy's
misc-include-cleaner: unused `#include`s fail the push. Fix a report
automatically with:

```sh
tools/include-cleaner.sh --fix
```

In-editor, `.clangd` enables the same analysis (strict unused/missing include
diagnostics) with quick-fixes.

## Debugging

Debug builds emit DWARF and a source map. With Chrome's
[C/C++ DevTools Support](https://chromewebstore.google.com/detail/pdcpmagijalfljmkmjngeonclgbbannb)
extension you can set breakpoints in C++ from DevTools.
