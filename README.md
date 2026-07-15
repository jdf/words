# wrdl

Wordle word clouds, reimplemented in C++ / WebAssembly / WebGL2.

## Dev loop

```sh
./dev            # configure (first time) + build + start dev server
```

then open / reload <http://localhost:8787/>. That's the whole loop: edit,
`./dev`, reload. In VS Code, the default build task (⇧⌘B) runs the same
thing, and the "Run wrdl (Chrome)" launch config builds and opens Chrome
with the debugger attached.

`./dev release` builds and serves an optimized build instead.

## Toolchain

- **CMake ≥ 3.25 + Ninja**, driven by `CMakePresets.json`
  (`wasm-debug`, `wasm-release`).
- **vcpkg** in manifest mode (`vcpkg.json`), triplet `wasm32-emscripten`,
  chainloading the Emscripten toolchain. Add dependencies to `vcpkg.json`;
  the next configure installs them.
- **Emscripten** via emsdk, expected at `~/emsdk` (override with `$EMSDK`).
  vcpkg is expected at `~/vcpkg` (override with `$VCPKG_ROOT`).

## Layout

- `src/` — C++ sources.
- `web/` — static harness (HTML/CSS/JS); copied into the build's `dist/`
  by the build, so it goes through the same `./dev` + reload loop.
- `tools/serve.py` — dev server (no-store caching, COOP/COEP for future
  wasm-threads support).
- `build/<preset>/dist/` — everything the browser loads.

## Debugging

Debug builds emit DWARF and a source map. With Chrome's
[C/C++ DevTools Support](https://chromewebstore.google.com/detail/pdcpmagijalfljmkmjngeonclgbbannb)
extension you can set breakpoints in C++ from DevTools.
