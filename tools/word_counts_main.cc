// word_counts: counts the words of a UTF-8 text file with the app's exact
// word model (cue.language tokenization, folded keys, majority-casing
// display forms) and writes TSV to stdout — the corpus-building backend for
// tools/make-corpus.py. Stop words are NOT removed; the guessed language is
// reported in the header so consumers can apply the right stop list.
//
// Usage: word_counts <text-file> <stopwords-dir> [--case=<fold>]
//
// --case=as-written emits one row per exact spelling (and a "# case:
// as-written" header) so the engine can apply any case fold at load;
// the default remains the merged, majority-casing table the fixtures
// are pinned to.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "text.h"

int main(int argc, char** argv) {
  words::CaseFold fold = words::CaseFold::kGuess;
  if (argc == 4) {
    constexpr const char kPrefix[] = "--case=";
    std::optional<words::CaseFold> parsed;
    if (std::strncmp(argv[3], kPrefix, sizeof(kPrefix) - 1) == 0) {
      parsed = words::findCaseFold(argv[3] + sizeof(kPrefix) - 1);
    }
    if (!parsed) {
      std::fprintf(stderr, "bad case fold: %s\n", argv[3]);
      return 2;
    }
    fold = *parsed;
  } else if (argc != 3) {
    std::fprintf(stderr,
                 "usage: %s <text-file> <stopwords-dir> [--case=<fold>]\n",
                 argv[0]);
    return 2;
  }

  std::ifstream in(argv[1]);
  if (!in) {
    std::fprintf(stderr, "cannot read %s\n", argv[1]);
    return 1;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string text = ss.str();

  words::StopWordsSet stopSets(argv[2]);
  const words::StopWords* language = stopSets.guess(text);
  std::vector<words::WordCount> counts =
      words::countWords(text, nullptr, fold);

  int tokens = 0;
  for (const words::WordCount& wc : counts) tokens += wc.count;
  std::printf("# language-guess: %s\n",
              language ? language->name().c_str() : "unknown");
  if (fold == words::CaseFold::kAsWritten) {
    std::printf("# case: as-written\n");
  }
  std::printf("# tokens: %d\n", tokens);
  std::printf("# distinct: %zu\n", counts.size());
  for (const words::WordCount& wc : counts) {
    std::printf("%s\t%d\n", wc.display.c_str(), wc.count);
  }
  return 0;
}
