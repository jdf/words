// Golden-file tests over the scene's pure geometry, serialized as SVG.
// The approved files are viewable images: open tests/goldens/geometry/*.svg
// in a browser to see exactly what the geometry looks like.

#include <ApprovalTests.hpp>
#include <catch2/catch_test_macros.hpp>

#include <absl/strings/str_cat.h>

#include <algorithm>
#include <cstddef>
#include <numbers>
#include <vector>

#include <fstream>
#include <sstream>
#include <string>

#include "box.h"
#include "demo_scene.h"
#include "orientation.h"
#include "text.h"
#include "layout.h"
#include "scene.h"
#include "svg.h"
#include "word.h"

namespace {

constexpr const char* kFont = WORDS_ASSETS_DIR "/fonts/sexsmith.ttf";

auto subdirectory = ApprovalTests::Approvals::useApprovalsSubdirectory(
    "goldens/geometry");

void verifySvg(const std::string& svg) {
  ApprovalTests::Approvals::verify(
      svg, ApprovalTests::Options().fileOptions().withFileExtension(".svg"));
}

}  // namespace

TEST_CASE("hbb of a horizontal word") {
  words::ShapedText shaped = words::shapeText(kFont, "words");
  words::Word word(shaped, 0.3, 0.0);
  word.buildHbb();
  verifySvg(words::hbbDebugSvg(word));
}

TEST_CASE("hbb of a rotated word") {
  words::ShapedText shaped = words::shapeText(kFont, "words");
  words::Word word(shaped, 0.3, 30.0 * std::numbers::pi / 180.0);
  word.buildHbb();
  verifySvg(words::hbbDebugSvg(word));
}

TEST_CASE("cloud layout") {
  words::Scene scene = words::buildCloudScene(kFont);
  verifySvg(words::toSvg(scene));
}

TEST_CASE("text cloud layout") {
  words::Scene scene = words::buildCloudFromText(
      kFont, WORDS_ASSETS_DIR "/stopwords",
      "four score and seven years ago our fathers brought forth on this "
      "continent a new nation conceived in liberty and dedicated to the "
      "proposition that all men are created equal now we are engaged in a "
      "great civil war testing whether that nation or any nation so "
      "conceived and so dedicated can long endure we are met on a great "
      "battlefield of that war nation nation");
  verifySvg(words::toSvg(scene));
}

TEST_CASE("spiral trail of a placed word") {
  // The search spiral "ago" walks through the Moby-Dick cloud before
  // finding a home — the layout-debugging view.
  std::ifstream in(WORDS_ASSETS_DIR "/sample-text.txt");
  std::ostringstream ss;
  ss << in.rdbuf();
  words::LayoutDebug debug;
  debug.traceLabel = "ago";
  words::Scene scene = words::buildCloudFromText(
      kFont, WORDS_ASSETS_DIR "/stopwords", ss.str(),
      {.maxWords = 150, .debug = &debug});
  REQUIRE_FALSE(debug.trail.empty());
  verifySvg(words::toSvg(scene, debug));
}

TEST_CASE("original font collection shapes text") {
  // A few representatives from the original Wordle's font collection:
  // Typodermic display faces and the wide-coverage OFL faces.
  for (const char* font : {"coolvetica", "goudy", "gentium", "chunkfive",
                           "opensansbold"}) {
    INFO(font);
    words::ShapedText shaped = words::shapeText(
        absl::StrCat(WORDS_ASSETS_DIR "/fonts/", font, ".ttf"), "Wordle");
    CHECK_FALSE(shaped.empty());
    CHECK(shaped.upem > 0);
    CHECK(shaped.bounds.width() > 0);
  }
}

TEST_CASE("text cloud in coolvetica") {
  words::Scene scene = words::buildCloudFromText(
      WORDS_ASSETS_DIR "/fonts/coolvetica.ttf",
      WORDS_ASSETS_DIR "/stopwords",
      "four score and seven years ago our fathers brought forth on this "
      "continent a new nation conceived in liberty and dedicated to the "
      "proposition that all men are created equal now we are engaged in a "
      "great civil war testing whether that nation or any nation so "
      "conceived and so dedicated can long endure we are met on a great "
      "battlefield of that war nation nation");
  verifySvg(words::toSvg(scene));
}

TEST_CASE("any-which-way cloud properties") {
  // Arbitrary rotations through the whole pipeline: every pairwise HBB
  // collision here runs at angles the 0/90 clouds never exercise.
  words::Scene scene = words::buildCloudFromText(
      kFont, WORDS_ASSETS_DIR "/stopwords",
      "four score and seven years ago our fathers brought forth on this "
      "continent a new nation conceived in liberty and dedicated to the "
      "proposition that all men are created equal now we are engaged in a "
      "great civil war testing whether that nation or any nation so "
      "conceived and so dedicated can long endure we are met on a great "
      "battlefield of that war nation nation",
      {.orientation = words::Orientation::kAnyWhichWay});
  const auto& entries = scene.entries();
  REQUIRE(entries.size() >= 25);
  for (size_t i = 0; i < entries.size(); ++i) {
    for (size_t j = i + 1; j < entries.size(); ++j) {
      INFO("words " << i << " and " << j << " overlap");
      REQUIRE_FALSE(entries[i].word.intersectsWord(entries[j].word));
    }
  }
}

TEST_CASE("alphabetical cloud properties") {
  // Alphabetical placement: seeds run A->Z across the width, and the
  // spiral only jiggles locally — so the finished cloud must still read
  // left-to-right. Check the ordering statistically (early-alphabet words
  // sit left of late-alphabet ones on average) plus the usual no-overlap
  // invariant.
  words::Scene scene = words::buildCloudFromText(
      kFont, WORDS_ASSETS_DIR "/stopwords",
      "four score and seven years ago our fathers brought forth on this "
      "continent a new nation conceived in liberty and dedicated to the "
      "proposition that all men are created equal now we are engaged in a "
      "great civil war testing whether that nation or any nation so "
      "conceived and so dedicated can long endure we are met on a great "
      "battlefield of that war nation nation",
      {.placement = words::Placement::kAlphabetical});
  const auto& entries = scene.entries();
  REQUIRE(entries.size() >= 25);

  std::vector<const words::Scene::Entry*> sorted;
  for (const auto& e : entries) sorted.push_back(&e);
  std::sort(sorted.begin(), sorted.end(),
            [](const words::Scene::Entry* a, const words::Scene::Entry* b) {
              return words::collationKey(a->word.label()) <
                     words::collationKey(b->word.label());
            });
  double firstHalf = 0, secondHalf = 0;
  size_t half = sorted.size() / 2;
  for (size_t i = 0; i < half; ++i) firstHalf += sorted[i]->word.x();
  for (size_t i = half; i < sorted.size(); ++i) {
    secondHalf += sorted[i]->word.x();
  }
  CHECK(firstHalf / half <
        secondHalf / static_cast<double>(sorted.size() - half));

  for (size_t i = 0; i < entries.size(); ++i) {
    for (size_t j = i + 1; j < entries.size(); ++j) {
      INFO("words " << i << " and " << j << " overlap");
      REQUIRE_FALSE(entries[i].word.intersectsWord(entries[j].word));
    }
  }
}

TEST_CASE("cloud layout properties") {
  words::Scene scene = words::buildCloudScene(kFont);
  const auto& entries = scene.entries();
  REQUIRE(entries.size() >= 40);

  // No two committed words' padded ink may overlap.
  for (size_t i = 0; i < entries.size(); ++i) {
    for (size_t j = i + 1; j < entries.size(); ++j) {
      INFO("words " << i << " and " << j << " overlap");
      REQUIRE_FALSE(entries[i].word.intersectsWord(entries[j].word));
    }
  }

  // Every word fits inside the scene.
  words::Box world{-scene.width() / 2.0, -scene.height() / 2.0,
                   scene.width() / 2.0, scene.height() / 2.0};
  for (size_t i = 0; i < entries.size(); ++i) {
    INFO("word " << i << " out of bounds");
    REQUIRE(world.contains(entries[i].word.worldBounds()));
  }
}
