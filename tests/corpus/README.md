# Test corpus: word counts of public-domain texts

One TSV per book (`word<TAB>count`, most frequent first), counted with the app's
exact word model (cue.language tokenization) — see `tools/word_counts_main.cc`.
Every file is counted **as-written** (`# case: as-written` header, one row per
exact spelling), so the app can apply any case fold at load: the engine
case-merges for Guess/lowercase/UPPERCASE and uses rows verbatim for As Written.
(Case-merged files without the header still load; the engine then treats their
stored majority casing as As Written.) Stop words are **not** removed; each
file's `# language-guess:` header names the stop list to apply. Provenance
(title, Project Gutenberg id, URL) is in each file's header comments; all
sources are public domain.

Regenerate or extend with `tools/make-corpus.py` (downloads are cached under
`~/.cache/words/`). Sources are Project Gutenberg, plus Wikisource where
Gutenberg has no holdings (Arabic).

Two tiers live here side by side:

- **Fixtures** (the multilingual dozen in the script's `BOOKS` list): full
  distinct-word tables; moby-dick anchors the e2e goldens.
- **Library** (driven by `CORPUS-CANDIDATES.md` at the repo root): the works
  bundled for the site’s "Use a Book" picker. Truncated to the top 4000 rows
  (`# truncated:` header) since the engine lays out at most 2000 words; each
  carries a `# category:` header. Plays come in two flavors: `<slug>.tsv` with
  speaker headers / stage directions stripped, `<slug>-full.tsv` verbatim.

Caveat: the word model has no CJK segmentation (neither did the original
Wordle), so the Japanese and Chinese entries tokenize as punctuation- delimited
clause-runs — they document that limitation rather than provide meaningful
counts. Their `language-guess` headers are likewise unreliable (Rashomon's
English translator's preface wins its vote).
