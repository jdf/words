#!/usr/bin/env python3
"""Merge compile databases for clangd.

The editor needs one compile_commands.json, but the project has two
builds: wasm-debug (the engine) and host-test (colocated unit tests,
tests/, tools/). This writes build/clangd/compile_commands.json with the
wasm entry for any file both builds compile, and the host entry for
files only the host build knows. Run by ./dev after configuring; cheap
enough to run always.
"""

import json
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parent.parent
out_dir = root / "build" / "clangd"

merged: dict[str, dict] = {}
for build in ("host-test", "wasm-debug"):  # wasm last: it wins overlaps
    db = root / "build" / build / "compile_commands.json"
    if not db.exists():
        continue
    for entry in json.loads(db.read_text()):
        merged[entry["file"]] = entry

if not merged:
    sys.exit("no compile databases found; configure wasm-debug or host-test")

out_dir.mkdir(parents=True, exist_ok=True)
out = out_dir / "compile_commands.json"
out.write_text(json.dumps(sorted(merged.values(), key=lambda e: e["file"]),
                          indent=1))
print(f"{out}: {len(merged)} entries")
