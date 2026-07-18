// Unit tests for the vector-PDF writer: document structure, aspect, and
// metadata. Rendering fidelity is covered by the shared outline geometry
// (the same contours the SVG approvals pin).

#include "pdf.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
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

TEST_CASE("pdf is a well-formed vector document") {
  words::Scene scene = tinyScene();
  std::string pdf = words::toPdf(scene, 11 * 72.0);
  CHECK(pdf.compare(0, 8, "%PDF-1.4") == 0);
  CHECK(pdf.find("/Filter /FlateDecode") != std::string::npos);
  // Page preserves the scene aspect: MediaBox [0 0 792 H].
  double expectH = 792.0 * scene.height() / scene.width();
  char h[32];
  std::snprintf(h, sizeof h, "%.2f", expectH);
  CHECK(pdf.find(std::string("/MediaBox [0 0 792.00 ") + h + "]") !=
        std::string::npos);
  CHECK(pdf.rfind("%%EOF\n") == pdf.size() - 6);
  // No producer given: no Info dictionary.
  CHECK(pdf.find("/Info") == std::string::npos);
}

TEST_CASE("the producer tag becomes an Info dictionary") {
  words::Scene scene = tinyScene();
  std::string pdf = words::toPdf(scene, 11 * 72.0, "words build abc123");
  CHECK(pdf.find("<< /Producer (words build abc123) >>") !=
        std::string::npos);
  CHECK(pdf.find("/Info 5 0 R") != std::string::npos);
}

TEST_CASE("parens and backslashes in the producer are escaped") {
  words::Scene scene = tinyScene();
  std::string pdf = words::toPdf(scene, 72.0, "a(b)c\\d");
  CHECK(pdf.find("/Producer (a\\(b\\)c\\\\d)") != std::string::npos);
}

}  // namespace
