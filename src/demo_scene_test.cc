// Unit tests for the text->cloud pipeline entry points. Full-scale cloud
// geometry is pinned by the approvals and e2e goldens; these cover the
// pipeline's contracts on tiny inputs.

#include "demo_scene.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>

#include "scene.h"
#include "svg.h"

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
