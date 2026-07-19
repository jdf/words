#!/usr/bin/env python3
"""Builds tests/corpus/: word-count tables for public-domain texts.

Two tiers share the pipeline (download, cached in ~/.cache/words/; strip
site boilerplate; count words with the app's exact word model via the
word_counts host tool; write tests/corpus/<slug>.tsv with provenance
headers). Stop words are not removed; the guessed language is recorded so
consumers can apply the right stop list.

- BOOKS below: the multilingual test fixtures. Written in full and kept
  byte-stable — golden tests depend on them.
- CORPUS-CANDIDATES.md: the bundled library, one table row per work.
  Library TSVs are truncated to the top MAX_ROWS rows (the engine never
  lays out more than 2000 words); plays get a speaker-stripped <slug>.tsv
  plus an untouched <slug>-full.tsv.

Sources: Project Gutenberg (plain-text cache editions, fetched through a
PG mirror per their robot policy) and, where Gutenberg has no holdings
(Arabic), Wikisource via the TextExtracts API.

Usage: tools/make-corpus.py   (builds build/host-test/word_counts if needed)
"""

import html.parser
import json
import pathlib
import re
import subprocess
import sys
import tempfile
import time
import unicodedata
import urllib.error
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
    # Gutenberg's Russian shelf has no literary prose (mostly LibriVox
    # audio); Dostoevsky comes from Russian Wikisource instead.
    {"wikisource": ("ru", "Белые ночи (Достоевский)/1988 (СО)"),
     "slug": "belye-nochi", "lang": "russian",
     "title": "Белые ночи (Достоевский / White Nights)"},
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

# Bulk fetches go through a mirror per PG's robot policy; www is the
# fallback when the mirror lacks a file.
GUTENBERG_HOSTS = ["https://gutenberg.pglaf.org", "https://www.gutenberg.org"]
FETCH_DELAY_S = 1.0

CANDIDATES_MD = REPO / "CORPUS-CANDIDATES.md"
MAX_ROWS = 4000  # library truncation; engine tops out at 2000 words
EXTRA_VOLUMES = {2833: [2834]}  # The Portrait of a Lady
SLUG_OVERRIDES = {
    100: "shakespeare-complete",
    2147: "poe-works",
    10: "king-james-bible",
    7986: "chekhov-plays",
    128: "arabian-nights",
    6133: "arsene-lupin",
    6593: "tom-jones",
}


def fetch_gutenberg(gid: int) -> pathlib.Path:
    cache = CACHE / "gutenberg"
    cache.mkdir(parents=True, exist_ok=True)
    path = cache / f"pg{gid}.txt"
    if path.exists():
        return path
    for host in GUTENBERG_HOSTS:
        url = f"{host}/cache/epub/{gid}/pg{gid}.txt"
        print(f"  fetching {url}")
        try:
            req = urllib.request.Request(
                url, headers={"User-Agent":
                              "words-corpus-builder/1.0 "
                              "(https://github.com/jdf/words)"})
            with urllib.request.urlopen(req, timeout=120) as r:
                path.write_bytes(r.read())
            time.sleep(FETCH_DELAY_S)
            return path
        except urllib.error.URLError as e:
            print(f"  ({e}; trying next host)")
    raise RuntimeError(f"could not fetch pg{gid} from any host")


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


class _HtmlText(html.parser.HTMLParser):
    """Visible text of rendered wiki HTML, skipping non-content markup
    (styles, scripts, and Wikisource's ws-noexport/noprint apparatus)."""

    SKIP_TAGS = {"style", "script"}
    SKIP_CLASSES = ("ws-noexport", "noprint")

    def __init__(self):
        super().__init__()
        self.parts: list[str] = []
        self.skip_depth = 0

    def _skippable(self, tag, attrs) -> bool:
        classes = dict(attrs).get("class", "")
        return tag in self.SKIP_TAGS or any(
            c in classes for c in self.SKIP_CLASSES)

    def handle_starttag(self, tag, attrs):
        if self.skip_depth or self._skippable(tag, attrs):
            self.skip_depth += 1

    def handle_endtag(self, tag):
        self.skip_depth = max(0, self.skip_depth - 1) if self.skip_depth else 0

    def handle_data(self, data):
        if not self.skip_depth:
            self.parts.append(data)


def wikisource_page_text(lang: str, title: str) -> str:
    """Plain text of one page: TextExtracts when it works, otherwise the
    rendered parse (Proofread-extension transclusions are invisible to
    TextExtracts) with markup stripped."""
    extract = wikisource_api(lang, prop="extracts", explaintext=1,
                             titles=title)
    for p in extract["query"]["pages"].values():
        if p.get("extract"):
            return p["extract"]
    parsed = wikisource_api(lang, action="parse", prop="text", page=title)
    stripper = _HtmlText()
    stripper.feed(parsed["parse"]["text"]["*"])
    return "".join(stripper.parts)


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
    parts = [wikisource_page_text(lang, title) for title in titles]
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
    return "\n".join(lines[:start]), clean_body("\n".join(lines[start + 1 : end]))


def clean_body(body: str) -> str:
    """Drops transcriber apparatus that survives inside the book body:
    [Illustration: ...] captions and Distributed Proofreaders credit
    paragraphs. Typographic non-breaking spaces (Wikisource sets them
    after Russian particles) become plain spaces — the word model treats
    NBSP as a joiner, which would weld phrases like "что вы" into single
    tokens. The words of the book itself are untouched."""
    body = body.replace(" ", " ")
    body = re.sub(r"\[Illustration[^\]]*\]", "", body)
    paragraphs = re.split(r"\n\s*\n", body)
    # Anchored tightly: "produced by" appears in Melville's own prose.
    credit = re.compile(r"^\s*Produced by|Distributed Proofread|"
                        r"Transcriber['’]s Note", re.IGNORECASE)
    keep = [p for p in paragraphs if not credit.search(p)]
    return "\n\n".join(keep)


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


def parse_candidates() -> list[dict]:
    """The library: one entry per `| id | work | author | notes |` table
    row of CORPUS-CANDIDATES.md, categorized by the enclosing heading."""
    entries, category = [], ""
    for line in CANDIDATES_MD.read_text().splitlines():
        if line.startswith("## "):
            category = re.sub(r"\s*\(.*", "", line[3:].strip())
        m = re.match(r"\|\s*(\d+)\s*\|([^|]+)\|([^|]+)\|([^|]*)\|", line)
        if m and "Handling notes" not in category:
            entries.append({
                "gutenberg": int(m.group(1)),
                "work": m.group(2).strip(),
                "author": m.group(3).strip(),
                "notes": m.group(4).strip(),
                "category": category,
                "play": category == "Plays",
            })
    return entries


def slugify(work: str) -> str:
    s = unicodedata.normalize("NFD", work)
    s = "".join(c for c in s if not unicodedata.combining(c))
    s = re.sub(r"['’]", "", s)
    s = re.split(r"[:;,]", s)[0].lower()
    s = re.sub(r"^(the|a|an) ", "", s.strip())
    return re.sub(r"[^a-z0-9]+", "-", s).strip("-")


# Speaker headers in PG plays are ALL-CAPS lines ending in a period
# ("HAMLET.", "LADY MACBETH.", also "ACT I." / "SCENE II."); stage
# directions are bracketed (possibly multiline) or Enter/Exit lines.
SPEAKER_RE = re.compile(r"^[ \t]*[A-Z][A-Z'’ .\-]*\.\s*$", re.MULTILINE)
DIRECTION_RE = re.compile(r"\[[^\]]*\]", re.DOTALL)
STAGE_RE = re.compile(
    r"^[ \t]*(ACT\b|SCENE\b|PROLOGUE\b|EPILOGUE\b|DRAMATIS PERSON|"
    r"Enter |Exit |Exeunt|Re-enter )[^\n]*$", re.MULTILINE)


def strip_play_apparatus(body: str) -> str:
    body = DIRECTION_RE.sub(" ", body)
    body = SPEAKER_RE.sub("", body)
    return STAGE_RE.sub("", body)


# Per-book cleanups, keyed by ebook id.
SPECIAL_CLEAN = {
    10: lambda body: re.sub(r"\b\d+:\d+\b", " ", body),  # KJV verse refs
}


def truncate_counts(counts: str) -> str:
    """Top MAX_ROWS data rows of a word_counts table (comment headers
    kept) — far beyond the engine's 2000-word ceiling, at a fraction of
    the payload."""
    lines = counts.splitlines()
    headers = [l for l in lines if l.startswith("#")]
    data = [l for l in lines if not l.startswith("#")]
    if len(data) <= MAX_ROWS:
        return counts
    headers.append(f"# truncated: top {MAX_ROWS} of {len(data)} rows")
    return "\n".join(headers + data[:MAX_ROWS]) + "\n"


def build_library_book(entry: dict) -> list[str]:
    """Fetches, cleans, counts, writes; returns the slugs written."""
    gid = entry["gutenberg"]
    slug = SLUG_OVERRIDES.get(gid, slugify(entry["work"]))
    raw = fetch_gutenberg(gid).read_text(encoding="utf-8-sig")
    header, body = strip_boilerplate(raw)
    for extra in EXTRA_VOLUMES.get(gid, []):
        extra_raw = fetch_gutenberg(extra).read_text(encoding="utf-8-sig")
        body += "\n\n" + strip_boilerplate(extra_raw)[1]
    if gid in SPECIAL_CLEAN:
        body = SPECIAL_CLEAN[gid](body)
    title = header_field(header, "Title").replace("\n", " — ")
    source = (f"Project Gutenberg #{gid} "
              f"(https://www.gutenberg.org/ebooks/{gid})")
    variants = ([(slug, strip_play_apparatus(body)), (f"{slug}-full", body)]
                if entry["play"] else [(slug, body)])
    written = []
    for vslug, vbody in variants:
        counts = truncate_counts(count_words(vbody))
        guessed = re.search(r"# language-guess: (\S+)", counts).group(1)
        (CORPUS / f"{vslug}.tsv").write_text(
            f"# {title}\n"
            f"# source: {source}, public domain\n"
            f"# generator: tools/make-corpus.py (word_counts host tool)\n"
            f"# category: {entry['category']}\n"
            + counts)
        note = "" if guessed == "english" else f"  (guess: {guessed})"
        print(f"  {vslug}: {title}{note}")
        written.append(vslug)
    return written


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
            body = clean_body(fetch_wikisource(lang_code, page).read_text())
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

    # The bundled library. Slugs must not collide with each other or
    # with the fixtures above.
    seen = {b["slug"] for b in BOOKS}
    entries = parse_candidates()
    print(f"library: {len(entries)} works from {CANDIDATES_MD.name}")
    # The picker manifest: slug, display title/author (from the
    # candidates doc, not PG's long-form titles), category. moby-dick is
    # a fixture but belongs in the picker too.
    index = [("moby-dick", "Moby Dick", "Melville", "American")]
    for i, entry in enumerate(entries):
        print(f"[{i + 1}/{len(entries)}] {entry['work']} "
              f"(pg{entry['gutenberg']})")
        try:
            slugs = build_library_book(entry)
        except Exception as e:  # keep going; report at the end
            print(f"  *** FAILED: {e}")
            failures += 1
            continue
        index.append(
            (slugs[0], entry["work"], entry["author"], entry["category"]))
        for slug in slugs:
            if slug in seen:
                print(f"  *** SLUG COLLISION: {slug}")
                failures += 1
            seen.add(slug)
    index.sort(key=lambda r: r[1].lower())
    (CORPUS / "index.tsv").write_text(
        "# corpus picker manifest: slug\ttitle\tauthor\tcategory\n"
        "# generator: tools/make-corpus.py from CORPUS-CANDIDATES.md\n"
        + "".join("\t".join(row) + "\n" for row in index))
    print(f"index.tsv: {len(index)} works")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
