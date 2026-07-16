// Golden-file tests over the scene's pure geometry, serialized as SVG.
// The approved files are viewable images: open tests/goldens/geometry/*.svg
// in a browser to see exactly what the geometry looks like.

#include <ApprovalTests.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <numbers>
#include <vector>

#include "box.h"
#include "demo_scene.h"
#include "scene.h"
#include "svg.h"
#include "word.h"

namespace {

constexpr const char* kFont = WORDS_ASSETS_DIR "/Sexsmith.otf";

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
  scene.update(0.0);
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
  scene.update(0.0);
  verifySvg(words::toSvg(scene));
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
  words::Box world{-words::Scene::kWidth / 2.0, -words::Scene::kHeight / 2.0,
                   words::Scene::kWidth / 2.0, words::Scene::kHeight / 2.0};
  for (size_t i = 0; i < entries.size(); ++i) {
    INFO("word " << i << " out of bounds");
    REQUIRE(world.contains(entries[i].word.worldBounds()));
  }
}
