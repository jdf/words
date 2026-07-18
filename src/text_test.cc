// Text analysis: tokenizer semantics (from cue.language), counting,
// stop words, and language guessing.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "text.h"

namespace {

constexpr const char* kStopWordsDir = WORDS_ASSETS_DIR "/stopwords";

const words::StopWordsSet& stopWords() {
  static words::StopWordsSet set(kStopWordsDir);
  return set;
}

std::vector<std::string> tok(const char* text) {
  return words::wordsOf(text);
}

}  // namespace

TEST_CASE("tokenizer keeps joined words whole") {
  CHECK(tok("don't stop believing") ==
        std::vector<std::string>{"don't", "stop", "believing"});
  CHECK(tok("the U.S.A. is big.") ==
        std::vector<std::string>{"the", "U.S.A", "is", "big"});
  CHECK(tok("e-mail me at 3:30") ==
        std::vector<std::string>{"e-mail", "me", "at", "3:30"});
  CHECK(tok("rock--and--roll") ==
        std::vector<std::string>{"rock--and--roll"});
}

TEST_CASE("tokenizer drops trailing and leading punctuation") {
  CHECK(tok("wait... what?!") == std::vector<std::string>{"wait", "what"});
  CHECK(tok("(parenthetical)") == std::vector<std::string>{"parenthetical"});
  CHECK(tok("'quoted'") == std::vector<std::string>{"quoted"});
}

TEST_CASE("tokenizer handles non-Latin scripts") {
  CHECK(tok("Ελληνικά και λέξεις") ==
        std::vector<std::string>{"Ελληνικά", "και", "λέξεις"});
  CHECK(tok("русские слова") ==
        std::vector<std::string>{"русские", "слова"});
  CHECK(tok("naïve façade") == std::vector<std::string>{"naïve", "façade"});
}

TEST_CASE("folding lowercases across scripts and normalizes quotes") {
  CHECK(words::foldForMatch("DON\xE2\x80\x99T") == "don't");
  CHECK(words::foldForMatch("Ößen") == "ößen");
  CHECK(words::foldForMatch("ΛΈΞΕΙΣ") == "λέξεις");
}

TEST_CASE("counter orders by frequency with stable ties") {
  words::Counter c;
  for (const char* w : {"b", "a", "b", "c", "a", "b"}) c.note(w);
  auto by = c.byFrequency();
  REQUIRE(by.size() == 3);
  CHECK(by[0].first == "b");
  CHECK(by[0].second == 3);
  CHECK(by[1].first == "a");
  CHECK(by[2].first == "c");
  CHECK(c.totalItemCount() == 6);
}

TEST_CASE("stop words: single characters always match") {
  const words::StopWords* english = stopWords().find("english");
  REQUIRE(english != nullptr);
  CHECK(english->isStopWord("a"));
  CHECK(english->isStopWord("ß"));  // one code point, several bytes
  CHECK(english->isStopWord("The"));
  CHECK(english->isStopWord("don\xE2\x80\x99t"));  // don’t via fold
  CHECK_FALSE(english->isStopWord("wordle"));
}

TEST_CASE("language guessing") {
  REQUIRE(stopWords().languages().size() == 30);

  auto guess = [&](const char* text) {
    const words::StopWords* g = stopWords().guess(text);
    return g ? g->name() : std::string("(none)");
  };

  CHECK(guess("it was the best of times, it was the worst of times, it "
              "was the age of wisdom, it was the age of foolishness") ==
        "english");
  CHECK(guess("Longtemps, je me suis couché de bonne heure. Parfois, à "
              "peine ma bougie éteinte, mes yeux se fermaient si vite") ==
        "french");
  CHECK(guess("En un lugar de la Mancha, de cuyo nombre no quiero "
              "acordarme, no ha mucho tiempo que vivía un hidalgo") ==
        "spanish");
  CHECK(guess("Alle glücklichen Familien gleichen einander, jede "
              "unglückliche Familie ist auf ihre eigene Weise unglücklich") ==
        "german");
  CHECK(guess("qwzx bnmp vrtk") == "(none)");
}

TEST_CASE("collation keys fold case and diacritics") {
  // The alphabetical placement sorts by these: the outcome of the
  // original's Normalizer + toLowerCase.
  CHECK(words::collationKey("Éclair") == "eclair");
  CHECK(words::collationKey("WHALE") == "whale");
  CHECK(words::collationKey("naïve") == "naive");
  CHECK(words::collationKey("Настенька") == "настенька");
  // So "Éclair" sorts among the e's, not after z.
  CHECK(words::collationKey("Éclair") < words::collationKey("whale"));
}

TEST_CASE("counting with stop word removal") {
  const words::StopWords* english = stopWords().find("english");
  auto counts = words::countWords(
      "The quick brown fox jumps over the lazy dog. The dog sleeps; the "
      "Fox runs. FOX!",
      english);
  REQUIRE(counts.size() >= 3);
  CHECK(counts[0].display == "fox");  // fox/Fox/FOX tie; first-seen wins
  CHECK(counts[0].count == 3);
  CHECK(counts[1].display == "dog");
  CHECK(counts[1].count == 2);
  // "The"/"the"/"over" are gone.
  for (const auto& wc : counts) {
    CHECK(wc.display != "the");
    CHECK(wc.display != "The");
    CHECK(wc.display != "over");
  }
}

TEST_CASE("display form uses the majority casing") {
  // "Guess Case for Each Word": the most frequent casing is the display
  // form, so a capitalized first occurrence (a title, a sentence start)
  // can't stick to a word that's ordinarily lowercase — and vice versa.
  auto counts = words::countWords(
      "Whale ahoy! The whale, the whale! A whale for every sailor, and "
      "every sailor a whale. NASA said so; Nasa did not, nasa neither: "
      "NASA. Ahab spoke.");
  auto display = [&](const char* folded) -> std::string {
    for (const auto& wc : counts) {
      if (words::foldForMatch(wc.display) == folded) return wc.display;
    }
    return "(missing)";
  };
  CHECK(display("whale") == "whale");  // 4 lower beat 1 capitalized
  CHECK(display("nasa") == "NASA");    // 2 upper beat Nasa/nasa
  CHECK(display("ahab") == "Ahab");    // lone casing kept as-is
}
