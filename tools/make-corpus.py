#!/usr/bin/env python3
"""Builds tests/corpus/: word-count tables for public-domain texts.

For each configured book: download (cached in ~/.cache/words/gutenberg/ or
~/.cache/words/wikisource/), strip site boilerplate, count words with the
app's exact word model (the word_counts host tool), and write
tests/corpus/<slug>.tsv with provenance headers. Stop words are not
removed; the guessed language is recorded so consumers can apply the right
stop list.

Sources: Project Gutenberg (plain-text cache editions) and, where
Gutenberg has no holdings (Arabic), Wikisource via the TextExtracts API.

Usage: tools/make-corpus.py   (builds build/host-test/word_counts if needed)
"""

import json
import pathlib
import re
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request

REPO = pathlib.Path(__file__).resolve().parent.parent
CACHE = pathlib.Path.home() / ".cache" / "words"
CORPUS = REPO / "tests" / "corpus"
TOOL = REPO / "build" / "host-test" / "word_counts"

# expected language per the source's catalog, cross-checked against our
# stop-list guesser. "unknown" is itself an expectation: we have no stop
# lists (and no tokenization support) for Chinese and Japanese — their
# entries document how unsegmented CJK text comes through the word model.
BOOKS = [
    {"gutenberg": 2701, "slug": "moby-dick", "lang": "english"},
    {"gutenberg": 17989, "slug": "monte-cristo", "lang": "french"},
    {"gutenberg": 22367, "slug": "die-verwandlung", "lang": "german"},
    {"gutenberg": 2000, "slug": "don-quijote", "lang": "spanish"},
    {"gutenberg": 1012, "slug": "divina-commedia", "lang": "italian"},
    {"gutenberg": 3333, "slug": "os-lusiadas", "lang": "portuguese"},
    {"gutenberg": 7000, "slug": "kalevala", "lang": "finnish"},
    {"gutenberg": 30774, "slug": "moskoviya", "lang": "russian"},
    {"gutenberg": 45252, "slug": "hatzofe", "lang": "hebrew"},
    # Japanese: unsegmented CJK makes every Japanese "word" a unique
    # clause-run, so the handful of English words in the translator's
    # preface wins the language vote. A known, documented limitation.
    {"gutenberg": 1982, "slug": "rashomon", "lang": "japanese",
     "expect_guess": "english"},
    {"gutenberg": 23839, "slug": "lunyu", "lang": "chinese",
     "expect_guess": "unknown"},
    {"wikisource": ("ar", "كليلة ودمنة"), "slug": "kalila-wa-dimna",
     "lang": "arabic", "title": "كليلة ودمنة (Kalila wa-Dimna)"},
]

START_RE = re.compile(r"\*\*\* ?START OF (THE|THIS) PROJECT GUTENBERG EBOOK")
END_RE = re.compile(r"\*\*\* ?END OF (THE|THIS) PROJECT GUTENBERG EBOOK")


def fetch_gutenberg(gid: int) -> pathlib.Path:
    cache = CACHE / "gutenberg"
    cache.mkdir(parents=True, exist_ok=True)
    path = cache / f"pg{gid}.txt"
    if not path.exists():
        url = f"https://www.gutenberg.org/cache/epub/{gid}/pg{gid}.txt"
        print(f"  fetching {url}")
        with urllib.request.urlopen(url) as r:
            path.write_bytes(r.read())
    return path


def wikisource_api(lang: str, **params) -> dict:
    query = urllib.parse.urlencode(
        {"action": "query", "format": "json", **params})
    # Wikimedia rejects requests without a descriptive User-Agent.
    req = urllib.request.Request(
        f"https://{lang}.wikisource.org/w/api.php?{query}",
        headers={"User-Agent":
                 "words-corpus-builder/1.0 (https://github.com/jdf/words)"})
    with urllib.request.urlopen(req) as r:
        return json.load(r)


def fetch_wikisource(lang: str, page: str) -> pathlib.Path:
    """Concatenated plain text of `page` and all its chapter subpages."""
    cache = CACHE / "wikisource"
    cache.mkdir(parents=True, exist_ok=True)
    path = cache / f"{lang}-{page.replace('/', '_').replace(' ', '_')}.txt"
    if path.exists():
        return path
    listing = wikisource_api(lang, list="allpages", apnamespace=0,
                             aplimit=500, apprefix=f"{page}/")
    titles = [page] + [p["title"] for p in listing["query"]["allpages"]]
    print(f"  fetching {len(titles)} pages from {lang}.wikisource.org")
    parts = []
    for title in titles:
        extract = wikisource_api(lang, prop="extracts", explaintext=1,
                                 titles=title)
        for p in extract["query"]["pages"].values():
            parts.append(p.get("extract", ""))
    path.write_text("\n\n".join(parts))
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
    for book in BOOKS:
        slug, expected_lang = book["slug"], book["lang"]
        if "gutenberg" in book:
            gid = book["gutenberg"]
            print(f"{slug} (pg{gid})")
            raw = fetch_gutenberg(gid).read_text(encoding="utf-8-sig")
            header, body = strip_boilerplate(raw)
            title = header_field(header, "Title")
            source = (f"Project Gutenberg #{gid} "
                      f"(https://www.gutenberg.org/ebooks/{gid})")
        else:
            lang_code, page = book["wikisource"]
            print(f"{slug} ({lang_code}.wikisource)")
            body = fetch_wikisource(lang_code, page).read_text()
            title = book["title"]
            source = (f"Wikisource "
                      f"(https://{lang_code}.wikisource.org/wiki/"
                      f"{urllib.parse.quote(page.replace(' ', '_'))})")
        counts = count_words(body)
        guessed = re.search(r"# language-guess: (\S+)", counts).group(1)
        out = CORPUS / f"{slug}.tsv"
        out.write_text(
            f"# {title}\n"
            f"# source: {source}, public domain\n"
            f"# generator: tools/make-corpus.py (word_counts host tool)\n"
            + counts)
        note = ""
        expected_guess = book.get("expect_guess", expected_lang)
        if guessed != expected_guess:
            note = f"  *** GUESS != EXPECTED ({expected_guess})"
            failures += 1
        print(f"  {title}: guess={guessed}{note}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
