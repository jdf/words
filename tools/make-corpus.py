#!/usr/bin/env python3
"""Builds tests/corpus/: word-count tables for public-domain texts.

For each configured Project Gutenberg book: download (cached in
~/.cache/words/gutenberg/), strip the Gutenberg header/footer boilerplate,
count words with the app's exact word model (the word_counts host tool),
and write tests/corpus/<slug>.tsv with provenance headers. Stop words are
not removed; the guessed language is recorded so consumers can apply the
right stop list.

Usage: tools/make-corpus.py   (builds build/host-test/word_counts if needed)
"""

import pathlib
import re
import subprocess
import sys
import tempfile
import urllib.request

REPO = pathlib.Path(__file__).resolve().parent.parent
CACHE = pathlib.Path.home() / ".cache" / "words" / "gutenberg"
CORPUS = REPO / "tests" / "corpus"
TOOL = REPO / "build" / "host-test" / "word_counts"

# (gutenberg id, slug, expected language) — language per the Gutenberg
# catalog; the script cross-checks it against the book's own metadata and
# against our stop-list guesser.
BOOKS = [
    (2701, "moby-dick", "english"),
    (17989, "monte-cristo", "french"),
    (22367, "die-verwandlung", "german"),
    (2000, "don-quijote", "spanish"),
    (1012, "divina-commedia", "italian"),
    (3333, "os-lusiadas", "portuguese"),
    (7000, "kalevala", "finnish"),
]

START_RE = re.compile(r"\*\*\* ?START OF (THE|THIS) PROJECT GUTENBERG EBOOK")
END_RE = re.compile(r"\*\*\* ?END OF (THE|THIS) PROJECT GUTENBERG EBOOK")


def fetch(gid: int) -> pathlib.Path:
    CACHE.mkdir(parents=True, exist_ok=True)
    path = CACHE / f"pg{gid}.txt"
    if not path.exists():
        url = f"https://www.gutenberg.org/cache/epub/{gid}/pg{gid}.txt"
        print(f"  fetching {url}")
        with urllib.request.urlopen(url) as r:
            path.write_bytes(r.read())
    return path


def header_field(header: str, name: str) -> str:
    m = re.search(rf"^{name}: (.+)$", header, re.MULTILINE)
    return m.group(1).strip() if m else "unknown"


def strip_boilerplate(raw: str) -> tuple[str, str]:
    """Returns (gutenberg metadata header, book body)."""
    lines = raw.splitlines()
    start = end = None
    for i, line in enumerate(lines):
        if start is None and START_RE.search(line):
            start = i
        elif END_RE.search(line):
            end = i
            break
    if start is None or end is None:
        raise ValueError("Gutenberg START/END markers not found")
    return "\n".join(lines[:start]), "\n".join(lines[start + 1 : end])


def count_words(body: str) -> str:
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write(body)
        tmp = f.name
    try:
        return subprocess.run(
            [TOOL, tmp, REPO / "assets" / "stopwords"],
            capture_output=True, text=True, check=True).stdout
    finally:
        pathlib.Path(tmp).unlink()


def main() -> int:
    if not TOOL.exists():
        subprocess.run(["cmake", "--build", "--preset", "host-test",
                        "--target", "word_counts"], cwd=REPO, check=True)
    CORPUS.mkdir(parents=True, exist_ok=True)
    failures = 0
    for gid, slug, expected_lang in BOOKS:
        print(f"{slug} (pg{gid})")
        raw = fetch(gid).read_text(encoding="utf-8-sig")
        header, body = strip_boilerplate(raw)
        title = header_field(header, "Title")
        catalog_lang = header_field(header, "Language").lower()
        counts = count_words(body)
        guessed = re.search(r"# language-guess: (\S+)", counts).group(1)
        out = CORPUS / f"{slug}.tsv"
        out.write_text(
            f"# {title}\n"
            f"# source: Project Gutenberg #{gid} "
            f"(https://www.gutenberg.org/ebooks/{gid}), public domain\n"
            f"# generator: tools/make-corpus.py (word_counts host tool)\n"
            + counts)
        note = ""
        if guessed != expected_lang:
            note = f"  *** GUESS != EXPECTED ({expected_lang})"
            failures += 1
        print(f"  {title}: catalog={catalog_lang} guess={guessed}{note}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
