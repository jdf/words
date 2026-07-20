# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with
code in this repository.

## What this is

Wordle word clouds reimplemented in C++20 → WebAssembly → WebGL2, by the
original Wordle author. The original Java source lives at
`/Users/jdf/wordle-master` for consultation — **reproduce outcomes, not
idioms**. Live at https://mrfeinberg.com/words/ (via `./deploy`: the release
dist is staged into the ~/mrfeinberg.com site repo at `words/`, committed there,
then rsynced — the site repo is the versioned deploy artifact) and GitHub Pages
(CI on push to `main`).

## Commands

```sh
./dev                 # configure (first time) + build wasm-debug + serve http://localhost:8787/
./dev release         # same, optimized build
./dev stop            # stop the dev server

# Host unit + approval tests (Catch2 v3 + ApprovalTests, no GL needed)
cmake --preset host-test && cmake --build --preset host-test
ctest --preset host-test                   # all 71+ tests
ctest --preset host-test -R <regex>        # single test by name
build/host-test/words_tests "<test name>"  # or run the binary directly

tools/coverage.sh [--html]     # clang source-based line coverage; regenerates COVERAGE.md
tools/e2e-golden.sh [--bless]  # byte-exact PNG goldens, pinned Chrome-for-Testing + SwiftShader
tools/bench.sh [--benchmark_filter=...]  # google-benchmark under node (wasm-release)
node --prof build/wasm-release/words_bench.js --benchmark_filter=... \
  && node --prof-process isolate*.log   # symbolized wasm own-time profile
node tools/profile-rebuild.mjs "$(tools/get-chrome.sh)" http://localhost:8787/
                               # in-browser per-stage rebuild timings
tools/include-cleaner.sh --fix # fix include-hygiene failures (misc-include-cleaner)
./presubmit.sh                 # markdown format + include hygiene + both golden tiers
```

- Version control is **jj** (colocated git). `jj push-main` runs
  `./presubmit.sh` before moving `main` (`SKIP_PRESUBMIT=1` bypasses). Commit
  finished work; the user pushes and deploys themselves.
- Approval-test mismatches write a `.received` file next to the `.approved`
  golden (`tests/goldens/geometry/`); inspect it (they're viewable SVGs) and
  rename over the `.approved` file to bless.
- Markdown is formatted by prettier with `proseWrap: always` (`.prettierrc`);
  presubmit enforces `npx prettier --check '**/*.md'`.

## Toolchain

Toolchain details (presets, emsdk/vcpkg paths, local vcpkg workarounds): README
§§ Toolchain, Dependencies.

clangd reads `build/clangd/compile_commands.json`, a **merged** database
(`tools/merge-compile-db.py`, refreshed by `./dev`): wasm entries win overlaps,
host entries cover the tests.

## Architecture

Two build targets share one core:

- **`words_core`** (static lib) — the GL-free world model: text analysis,
  shaping, geometry, layout, scene, SVG/PDF export. Builds natively (tests) and
  for wasm (app). This is where almost all logic lives.
- **wasm app** (`src/main.cc`, `gl_util.cc`, `word_renderer.cc`, Emscripten
  only) — WebGL2 rendering. Runs inside a **Web Worker** (`web/worker.js`) that
  owns a transferred OffscreenCanvas; there is no rAF loop — draws happen on
  demand.

The page (`web/index.html` + `web/app.js`, deliberately vanilla JS — no
TypeScript) is pure UI. `web/` is copied into `build/<preset>/dist/` by the
build, so HTML/CSS/JS edits go through the same `./dev` + reload loop.

Pipeline: text → tokenize / language-guess / stop-word strip / case-majority
count (`src/text.cc`, utf8proc) → size ∝ count → placement seeding + spiral
search with hierarchical-bounding-box + quadtree collision (`src/hbb.cc`,
`src/quad_tree.cc`, `src/layout.cc`) → stencil-filled WebGL2. All glyph geometry
is shaped in integer font units (HarfBuzz scale pinned to upem,
`FT_LOAD_NO_SCALE`) and transformed to screen space exactly once, at draw time.

The engine is a deterministic function of its spec
(`wordsRebuild(seed, orientation, placement, palette, fontPath, textPath, maxWords, variance)`);
the worker stages fonts and user text into MEMFS on demand. Camera
(zoom/pan/pinch) is pure render state — invisible to layout, exports, and
goldens. URL parameters are read at boot (`font=goudy` = locked, `font=~goudy` =
random-mode; `?no-ui` = fixed legacy defaults, used by goldens) but never
written back — the address bar stays as the user arrived; feedback reports carry
a `specUrl()` repro link instead. Every UI mutation flows through `apply()` as
an undo/redo action — everything must be undoable.

## Testing conventions

Every `src/foo.h` has a colocated `src/foo_test.cc`, globbed into `words_tests`
(exceptions: `gl_util`/`word_renderer` are wasm-GL-only, `main.cc` is the
shell). The text approval lives in `tests/text_approval_test.cc` so its golden
path resolves. E2e matrix: 26 byte-exact PNGs over moby-dick (orientations ×
placements × palettes × fonts + a 2000-word saturated center disc).

## Gotchas

- **Never pass `--disable-gpu`** to headless Chrome — OffscreenCanvas composites
  through the GPU process and you get blank screenshots. Poll the engine's idle
  flag; `waitForFunction`/rAF starves in headless.
- Chrome-for-Testing screenshot hangs (even on about:blank): either an invisible
  macOS Keychain prompt (user must Allow) or stale OS trust on the cached binary
  (`rm -rf ~/.cache/words/chrome`, re-download the same pin).
- ssh/rsync to the deploy host goes **only** through the `mrfeinberg-claude`
  alias (dedicated key; `./deploy` uses it, dry-run with `-n`). Never use the
  user's own ssh identity from Claude's shell — auth fails and repeated failures
  trip Dreamhost's IP block.
- Comment on GitHub issues as the bot via `tools/bot-comment.py <N>` (body on
  stdin) — never `gh issue comment`, which posts as the user.
- `buildCloudFromCountsTsv` takes TSV _content_, not a path (a path silently
  lays out 0 words).
- The phone-portrait media block must stay **last** in the stylesheet — source
  order matters.
- iOS: claim canvas `gesturestart/change/end` or Safari's page pinch-zoom
  hijacks the camera; in iOS Chrome, any `await` between click and `window.open`
  expires user activation and the popup is blocked.
