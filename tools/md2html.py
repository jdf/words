#!/usr/bin/env python3
"""Render a small subset of Markdown to an HTML fragment.

Build-time renderer for content/*.md (the credits dialog). Supports the
subset those documents use: ATX headings, unordered lists, paragraphs,
links, **bold**, *italic*, `code`, and HTML comments (stripped). Escapes
everything else, so the output is safe to insert with innerHTML.

Usage: md2html.py input.md output.html
"""

import html
import re
import sys


def inline(text: str) -> str:
    out = html.escape(text, quote=False)
    out = re.sub(r"`([^`]+)`", r"<code>\1</code>", out)
    out = re.sub(
        r"\[([^\]]+)\]\(([^)\s]+)\)",
        r'<a href="\2" target="_blank" rel="noopener">\1</a>',
        out,
    )
    out = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", out)
    out = re.sub(r"\*([^*]+)\*", r"<em>\1</em>", out)
    return out


def render(md: str) -> str:
    md = re.sub(r"<!--.*?-->", "", md, flags=re.DOTALL)
    parts = []
    in_list = False

    def close_list():
        nonlocal in_list
        if in_list:
            parts.append("</ul>")
            in_list = False

    # Blocks are separated by blank lines; list items may span
    # continuation lines (indented), as may paragraphs.
    for block in re.split(r"\n\s*\n", md):
        block = block.strip("\n")
        if not block.strip():
            continue
        m = re.match(r"(#{1,3}) +(.*)", block)
        if m:
            close_list()
            level = len(m.group(1))
            parts.append(f"<h{level}>{inline(m.group(2).strip())}</h{level}>")
            continue
        if re.match(r"[-*] ", block):
            if not in_list:
                parts.append("<ul>")
                in_list = True
            item_lines = []
            for line in block.split("\n"):
                if re.match(r"[-*] ", line):
                    if item_lines:
                        parts.append(f"<li>{inline(' '.join(item_lines))}</li>")
                    item_lines = [line[2:].strip()]
                else:
                    item_lines.append(line.strip())
            if item_lines:
                parts.append(f"<li>{inline(' '.join(item_lines))}</li>")
            continue
        close_list()
        text = " ".join(line.strip() for line in block.split("\n"))
        parts.append(f"<p>{inline(text)}</p>")
    close_list()
    return "\n".join(parts) + "\n"


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(f"usage: {sys.argv[0]} input.md output.html")
    with open(sys.argv[1], encoding="utf-8") as f:
        fragment = render(f.read())
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write(fragment)


if __name__ == "__main__":
    main()
