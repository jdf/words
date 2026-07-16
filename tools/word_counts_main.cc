// word_counts: counts the words of a UTF-8 text file with the app's exact
// word model (cue.language tokenization, folded keys, majority-casing
// display forms) and writes TSV to stdout — the corpus-building backend for
// tools/make-corpus.py. Stop words are NOT removed; the guessed language is
// reported in the header so consumers can apply the right stop list.
//
// Usage: word_counts <text-file> <stopwords-dir>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "text.h"

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <text-file> <stopwords-dir>\n", argv[0]);
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
  std::vector<words::WordCount> counts = words::countWords(text);

  int tokens = 0;
  for (const words::WordCount& wc : counts) tokens += wc.count;
  std::printf("# language-guess: %s\n",
              language ? language->name().c_str() : "unknown");
  std::printf("# tokens: %d\n", tokens);
  std::printf("# distinct: %zu\n", counts.size());
  for (const words::WordCount& wc : counts) {
    std::printf("%s\t%d\n", wc.display.c_str(), wc.count);
  }
  return 0;
}
