# Test corpus: word counts of public-domain texts

One TSV per book (`word<TAB>count`, most frequent first), counted with the
app's exact word model (cue.language tokenization, folded keys, first-seen
display casing) — see `tools/word_counts_main.cc`. Stop words are **not**
removed; each file's `# language-guess:` header names the stop list to
apply. Provenance (title, Project Gutenberg id, URL) is in each file's
header comments; all sources are public domain.

Regenerate or extend with `tools/make-corpus.py` (downloads are cached in
`~/.cache/words/gutenberg/`).
