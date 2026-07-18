// Unit tests for the scene container.

#include "scene.h"

#include <catch2/catch_test_macros.hpp>
#include <utility>

#include "word.h"

namespace {

constexpr const char* kFont = WORDS_ASSETS_DIR "/fonts/sexsmith.ttf";

TEST_CASE("scene defaults: 1600x1000, the app's dark background") {
  words::Scene s;
  CHECK(s.width() == 1600.0);
  CHECK(s.height() == 1000.0);
  CHECK(s.entries().empty());
  // #17171c in float terms.
  CHECK(s.background().r == 0.09f);
  CHECK(s.background().g == 0.09f);
  CHECK(s.background().b == 0.11f);
}

TEST_CASE("scene carries explicit dimensions and background") {
  words::Scene s(320, 200);
  CHECK(s.width() == 320.0);
  CHECK(s.height() == 200.0);
  s.setBackground({1, 0, 0});
  CHECK(s.background().r == 1.0f);
}

TEST_CASE("added words keep their color and position") {
  words::ShapedText shaped = words::shapeText(kFont, "hi");
  REQUIRE_FALSE(shaped.empty());
  words::Scene s;
  words::Word w(shaped, 0.1, 0.0);
  w.moveTo(12, -34);
  s.addWord(std::move(w), {0.25f, 0.5f, 0.75f});
  REQUIRE(s.entries().size() == 1);
  CHECK(s.entries()[0].word.x() == 12.0);
  CHECK(s.entries()[0].word.y() == -34.0);
  CHECK(s.entries()[0].color.g == 0.5f);
}

}  // namespace
