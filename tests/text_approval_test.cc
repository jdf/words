// Approval test over the word model: a viewable golden of guessed
// language and top words per snippet. The per-function unit tests live
// in src/text_test.cc; this stays with the approval suite so its golden
// resolves under tests/goldens/.

#include <ApprovalTests.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "text.h"

namespace {

const words::StopWordsSet& stopWords() {
  static words::StopWordsSet set(WORDS_ASSETS_DIR "/stopwords");
  return set;
}

TEST_CASE("word count report for a multilingual corpus") {
  const char* snippets[] = {
      "Call me Ishmael. Some years ago—never mind how long precisely—"
      "having little or no money in my purse, and nothing particular to "
      "interest me on shore, I thought I would sail about a little and "
      "see the watery part of the world.",
      "Vor einem großen Walde wohnte ein armer Holzhacker mit seiner "
      "Frau und seinen zwei Kindern; das Bübchen hieß Hänsel und das "
      "Mädchen Gretel. Er hatte wenig zu beißen und zu brechen.",
      "Все счастливые семьи похожи друг на друга, каждая несчастливая "
      "семья несчастлива по-своему. Все смешалось в доме Облонских.",
  };
  std::string report;
  for (const char* text : snippets) {
    const words::StopWords* lang = stopWords().guess(text);
    report += "language: ";
    report += lang ? lang->name() : "(none)";
    report += "\n";
    auto counts = words::countWords(text, lang);
    size_t n = 0;
    for (const auto& wc : counts) {
      if (++n > 5) break;
      report += "  " + wc.display + " x" + std::to_string(wc.count) + "\n";
    }
    report += "\n";
  }
  ApprovalTests::Approvals::verify(report);
}

}  // namespace
