# Test corpus: word counts of public-domain texts

One TSV per book (`word<TAB>count`, most frequent first), counted with the
app's exact word model (cue.language tokenization, folded keys,
majority-casing display forms) — see `tools/word_counts_main.cc`. Stop words are **not**
removed; each file's `# language-guess:` header names the stop list to
apply. Provenance (title, Project Gutenberg id, URL) is in each file's
header comments; all sources are public domain.

Regenerate or extend with `tools/make-corpus.py` (downloads are cached
under `~/.cache/words/`). Sources are Project Gutenberg, plus Wikisource
where Gutenberg has no holdings (Arabic).

Caveat: the word model has no CJK segmentation (neither did the original
Wordle), so the Japanese and Chinese entries tokenize as punctuation-
delimited clause-runs — they document that limitation rather than provide
meaningful counts. Their `language-guess` headers are likewise unreliable
(Rashomon's English translator's preface wins its vote).
