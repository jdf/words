#pragma once

#include <absl/container/flat_hash_map.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace words {

// Natural-language text analysis for word clouds: tokenizing text into
// words, counting them, guessing the language, and filtering stop words.
// Semantics follow cue.language (the library the original Wordle used);
// Unicode classification comes from utf8proc, so all scripts work.

// Splits text (UTF-8) into words: a word is a run of letters/digits/@/+,
// possibly chained by joiner characters (hyphen, period, colon, slash,
// apostrophes, combining marks, NBSP, ZWNJ/ZWJ, tilde) with letters on both
// sides — so "don't", "e-mail", "U.S.A.", and "3:30" each come out whole.
std::vector<std::string> wordsOf(std::string_view text);

// Unicode-lowercases UTF-8 (locale-independent full mapping), with U+2019
// (right single quote) folded to apostrophe first, as stop lists expect.
std::string foldForMatch(std::string_view word);

// A sort key for alphabetical ordering: decomposed, diacritics stripped,
// casefolded ("Éclair" sorts as "eclair") — the outcome of the original's
// cue.lang Normalizer + toLowerCase, as used by its Alphabetical
// placement strategy.
std::string collationKey(std::string_view word);

// Counts strings, preserving first-seen order for deterministic ties.
class Counter {
 public:
  void note(std::string_view item, int count = 1);

  int count(std::string_view item) const;
  int totalItemCount() const { return total_; }
  size_t distinctCount() const { return items_.size(); }

  // All items, most frequent first; ties broken by first appearance.
  std::vector<std::pair<std::string, int>> byFrequency() const;
  // The min(n, distinct) most frequent items.
  std::vector<std::string> mostFrequent(size_t n) const;

 private:
  std::vector<std::pair<std::string, int>> items_;      // insertion order
  absl::flat_hash_map<std::string, size_t> indexByKey_;  // into items_
  int total_ = 0;
};

// A stop-word list for one language, loaded from a cue.language-format
// word list (UTF-8, one or more words per line, `|` starts a comment).
class StopWords {
 public:
  static StopWords fromFile(const std::string& path);

  const std::string& name() const { return name_; }
  bool loaded() const { return !stopwords_.empty(); }

  // Single-codepoint words are always stop words; otherwise fold (U+2019
  // to apostrophe, lowercase) and look up.
  bool isStopWord(std::string_view word) const;

 private:
  std::string name_;
  std::vector<std::string> stopwords_;  // sorted for binary search
  friend class StopWordsSet;
};

// All known languages' stop lists, for language guessing and filtering.
class StopWordsSet {
 public:
  // Loads every list in `dir` (e.g. "assets/stopwords"); file name = the
  // language name. Missing dir yields an empty set.
  explicit StopWordsSet(const std::string& dir);

  const std::vector<StopWords>& languages() const { return languages_; }
  const StopWords* find(std::string_view name) const;

  // Guesses the text's language: among the 50 most frequent words, the
  // list claiming the most of them as stop words wins. Returns nullptr
  // when nothing matches at all.
  const StopWords* guess(const Counter& wordCounter) const;
  const StopWords* guess(std::string_view text) const;

 private:
  std::vector<StopWords> languages_;
};

// How case variants of a word combine — the original's Case menu.
enum class CaseFold {
  kGuess,      // merge case-insensitively; show the most frequent casing
  kAsWritten,  // distinct spellings stay distinct words
  kLower,      // merge; show lowercase
  kUpper,      // merge; show UPPERCASE
};

// Slug -> value for URL/UI plumbing: "guess", "as-written", "lower",
// "upper"; nullopt for anything else.
std::optional<CaseFold> findCaseFold(std::string_view slug);

// The fold's display transform for one word: Unicode-lowercases or
// -uppercases for kLower/kUpper (full per-code-point mapping, word-final
// sigma handled); identity for kGuess/kAsWritten.
std::string foldDisplay(std::string_view word, CaseFold fold);

// Tokenizes `text` and counts every word, optionally skipping the given
// stop words. Under every fold but kAsWritten the counting key is the
// folded (lowercased) form; kGuess keeps the most frequent casing (ties
// to the first seen) as the display form, like the original's "Guess
// Case for Each Word".
struct WordCount {
  std::string display;
  int count;
};
std::vector<WordCount> countWords(std::string_view text,
                                  const StopWords* reject = nullptr,
                                  CaseFold fold = CaseFold::kGuess);

}  // namespace words
