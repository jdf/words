#!/usr/bin/env python3
"""Generate web/favicon.svg from the Sexsmith 'w' glyph.

A rounded rectangle, black with a 1px light-green stroke (the sidebar's
discovery green, #7ed99a), the lowercase Sexsmith 'w' centered in the
same green — the glyph outline extracted straight from the shipped TTF,
so the icon is the app's own lettering, not a lookalike.

fontTools is fetched into a private venv under build/ on first run; the
system Python is never touched. The PNG fallback is rasterized from the
SVG by tools/e2e-shot machinery's pinned Chrome:

    python3 tools/make-favicon.py
    "$(tools/get-chrome.sh)" --headless --screenshot=web/favicon.png \
        --window-size=32,32 --default-background-color=00000000 \
        file://$PWD/web/favicon.svg
"""

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VENV = os.path.join(ROOT, "build", "favicon-venv")

try:
    from fontTools.pens.boundsPen import BoundsPen
    from fontTools.pens.svgPathPen import SVGPathPen
    from fontTools.ttLib import TTFont
except ModuleNotFoundError:
    if os.environ.get("FAVICON_VENV"):  # the re-exec below failed too
        raise
    if not os.path.isdir(VENV):
        subprocess.run([sys.executable, "-m", "venv", VENV], check=True)
        subprocess.run(
            [os.path.join(VENV, "bin", "pip"), "install", "-q", "fonttools"],
            check=True,
        )
    os.environ["FAVICON_VENV"] = "1"
    os.execv(os.path.join(VENV, "bin", "python3"),
             [os.path.join(VENV, "bin", "python3")] + sys.argv)

GREEN = "#7ed99a"
SIZE = 32

font = TTFont(os.path.join(ROOT, "assets", "fonts", "sexsmith.ttf"))
glyph_set = font.getGlyphSet()
glyph = glyph_set[font.getBestCmap()[ord("w")]]

bounds = BoundsPen(glyph_set)
glyph.draw(bounds)
xmin, ymin, xmax, ymax = bounds.bounds
path = SVGPathPen(glyph_set)
glyph.draw(path)

# Center the glyph's bounding box in the icon, fit inside a comfortable
# margin, and flip font coordinates (y-up) into SVG's y-down.
scale = min(22.0 / (xmax - xmin), 18.0 / (ymax - ymin))
tx = SIZE / 2 - scale * (xmin + xmax) / 2
ty = SIZE / 2 + scale * (ymin + ymax) / 2

svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="{SIZE}" height="{SIZE}" viewBox="0 0 {SIZE} {SIZE}">
  <rect x="0.5" y="0.5" width="{SIZE - 1}" height="{SIZE - 1}" rx="6"
        fill="#000" stroke="{GREEN}" stroke-width="1"/>
  <path transform="translate({tx:.3f} {ty:.3f}) scale({scale:.5f} -{scale:.5f})"
        fill="{GREEN}" d="{path.getCommands()}"/>
</svg>
"""

out = os.path.join(ROOT, "web", "favicon.svg")
with open(out, "w") as f:
    f.write(svg)
print(f"wrote {out} (glyph bbox {xmax - xmin:.0f}x{ymax - ymin:.0f} "
      f"font units, scale {scale:.4f})")
