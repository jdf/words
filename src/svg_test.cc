// Unit tests for SVG serialization. What full clouds look like as SVG is
// pinned by the geometry approvals; these cover the format contract.

#include "svg.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>

#include "scene.h"
#include "word.h"

namespace {

constexpr const char* kFont = WORDS_ASSETS_DIR "/fonts/sexsmith.ttf";

words::Scene tinyScene() {
  words::Scene scene(400, 250);
  words::Word w(words::shapeText(kFont, "ink"), 0.1, 0.0);
  scene.addWord(std::move(w), {1, 0, 0});
  return scene;
}

TEST_CASE("svg carries the scene: viewBox, background, paths") {
  std::string svg = words::toSvg(tinyScene());
  CHECK(svg.find("viewBox=\"-200 -125 400 250\"") != std::string::npos);
  CHECK(svg.find("<rect") != std::string::npos);
  CHECK(svg.find("<path fill=\"#ff0000\"") != std::string::npos);
  CHECK(svg.find("fill-rule=\"evenodd\"") != std::string::npos);
  // Scene y-up under SVG y-down: the flip transform.
  CHECK(svg.find("scale(1,-1)") != std::string::npos);
}

TEST_CASE("transparent svg omits the background rect only") {
  words::Scene scene = tinyScene();
  std::string opaque = words::toSvg(scene);
  std::string transparent = words::toSvg(scene, false);
  CHECK(opaque.find("<rect") != std::string::npos);
  CHECK(transparent.find("<rect") == std::string::npos);
  CHECK(transparent.find("<path") != std::string::npos);
}

TEST_CASE("the generator tag becomes a desc element") {
  words::Scene scene = tinyScene();
  std::string tagged = words::toSvg(scene, true, "words build abc123");
  CHECK(tagged.find("<desc>words build abc123</desc>") != std::string::npos);
  CHECK(words::toSvg(scene).find("<desc>") == std::string::npos);
}

}  // namespace
