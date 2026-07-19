// Unit tests for the text->cloud pipeline entry points. Full-scale cloud
// geometry is pinned by the approvals and e2e goldens; these cover the
// pipeline's contracts on tiny inputs.

#include "demo_scene.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <vector>

#include "scene.h"
#include "svg.h"
#include "text.h"

namespace {

constexpr const char* kFont = WORDS_ASSETS_DIR "/fonts/sexsmith.ttf";
constexpr const char* kStops = WORDS_ASSETS_DIR "/stopwords";

constexpr const char* kTsv =
    "# language-guess: english\n"
    "whale\t20\n"
    "ship\t10\n"
    "sea\t5\n"
    "captain\t3\n"
    "harpoon\t2\n";

TEST_CASE("a counts cloud holds at most maxWords words") {
  words::CloudOptions options;
  options.maxWords = 3;
  words::Scene scene =
      words::buildCloudFromCountsTsv(kFont, kStops, kTsv, options);
  CHECK(scene.entries().size() == 3);
  // The most frequent word is the biggest.
  CHECK(scene.entries()[0].word.scale() >= scene.entries()[1].word.scale());
}

TEST_CASE("the pipeline is deterministic for a fixed seed") {
  words::CloudOptions options;
  options.seed = 99;
  std::string a =
      words::toSvg(words::buildCloudFromCountsTsv(kFont, kStops, kTsv, options));
  std::string b =
      words::toSvg(words::buildCloudFromCountsTsv(kFont, kStops, kTsv, options));
  CHECK(a == b);
  options.seed = 100;
  std::string c =
      words::toSvg(words::buildCloudFromCountsTsv(kFont, kStops, kTsv, options));
  CHECK(a != c);
}

// Library-style corpus: one row per exact spelling, count-descending,
// flagged so the engine can apply any case fold at load.
constexpr const char* kAsWrittenTsv =
    "# language-guess: english\n"
    "# case: as-written\n"
    "Whale\t6\n"
    "whale\t5\n"
    "ship\t4\n"
    "WHALE\t2\n"
    "Ship\t1\n";

TEST_CASE("as-written corpus rows honor every case fold") {
  words::CloudOptions options;
  auto labels = [&](words::CaseFold fold) {
    options.caseFold = fold;
    std::vector<std::string> out;
    words::Scene scene =
        words::buildCloudFromCountsTsv(kFont, kStops, kAsWrittenTsv, options);
    for (const words::Scene::Entry& e : scene.entries()) {
      out.push_back(e.word.label());
    }
    return out;
  };
  // Merging folds: whale 6+5+2=13 beats ship 4+1=5, and the majority
  // casing (the first row of each folded key) is Whale / ship.
  CHECK(labels(words::CaseFold::kGuess) ==
        std::vector<std::string>{"Whale", "ship"});
  CHECK(labels(words::CaseFold::kLower) ==
        std::vector<std::string>{"whale", "ship"});
  CHECK(labels(words::CaseFold::kUpper) ==
        std::vector<std::string>{"WHALE", "SHIP"});
  // As written: every spelling stands alone, in stored (count) order.
  CHECK(labels(words::CaseFold::kAsWritten) ==
        std::vector<std::string>{"Whale", "whale", "ship", "WHALE", "Ship"});
}

TEST_CASE("merged corpus rows still fold lower and upper") {
  // Fixture-style TSV (no "# case:" header): kLower / kUpper transform
  // the stored display forms; kAsWritten shows the stored casing.
  words::CloudOptions options;
  options.caseFold = words::CaseFold::kUpper;
  words::Scene scene =
      words::buildCloudFromCountsTsv(kFont, kStops, kTsv, options);
  CHECK(scene.entries()[0].word.label() == "WHALE");
}

TEST_CASE("free text goes through counting: stop words drop out") {
  words::Scene scene = words::buildCloudFromText(
      kFont, kStops, "the whale and the whale and the ship", {});
  bool sawThe = false, sawWhale = false;
  for (const words::Scene::Entry& e : scene.entries()) {
    if (e.word.label() == "the") sawThe = true;
    if (e.word.label() == "whale") sawWhale = true;
  }
  CHECK(sawWhale);
  CHECK_FALSE(sawThe);
}

TEST_CASE("empty input yields an empty scene, not a crash") {
  words::Scene scene = words::buildCloudFromText(kFont, kStops, "", {});
  CHECK(scene.entries().empty());
}

TEST_CASE("progress reports both phases in order") {
  bool sawShaping = false, sawLayout = false, ordered = true;
  words::CloudOptions options;
  options.progress = [&](const char* phase, size_t, size_t) {
    if (std::string(phase) == "shaping") {
      sawShaping = true;
      if (sawLayout) ordered = false;  // shaping must come first
    } else {
      sawLayout = true;
    }
  };
  words::buildCloudFromCountsTsv(kFont, kStops, kTsv, options);
  CHECK(sawShaping);
  CHECK(sawLayout);
  CHECK(ordered);
}

}  // namespace
