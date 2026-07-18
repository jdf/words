// Unit tests for the layout engine: slug lookups and the collision-free
// invariant. What laid-out clouds look like is pinned by the geometry
// approvals and e2e goldens.

#include "layout.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "box.h"
#include "word.h"

namespace {

constexpr const char* kFont = WORDS_ASSETS_DIR "/fonts/sexsmith.ttf";

TEST_CASE("placement lookup by slug") {
  CHECK(words::findPlacement("center-line") == words::Placement::kCenterLine);
  CHECK(words::findPlacement("center") == words::Placement::kCenter);
  CHECK(words::findPlacement("square") == words::Placement::kSquare);
  CHECK(words::findPlacement("vertical-center-line") ==
        words::Placement::kVerticalCenterLine);
  CHECK(words::findPlacement("alphabetical") ==
        words::Placement::kAlphabetical);
  CHECK_FALSE(words::findPlacement("nope").has_value());
  CHECK_FALSE(words::findPlacement("").has_value());
}

TEST_CASE("placement names") {
  CHECK(words::placementName(words::Placement::kCenterLine) == "Center Line");
  CHECK(words::placementName(words::Placement::kCenter) == "Center");
  CHECK(words::placementName(words::Placement::kSquare) == "Square");
  CHECK(words::placementName(words::Placement::kVerticalCenterLine) ==
        "Vertical Center Line");
  CHECK(words::placementName(words::Placement::kAlphabetical) ==
        "Alphabetical");
}

// The engine's one hard promise: no two committed words' padded ink
// overlaps, under every placement strategy.
TEST_CASE("laid-out words never collide") {
  const std::vector<std::string> labels = {"one", "two",   "three", "four",
                                           "five", "sixty", "seven", "eight"};
  for (words::Placement placement :
       {words::Placement::kCenterLine, words::Placement::kCenter,
        words::Placement::kSquare, words::Placement::kVerticalCenterLine,
        words::Placement::kAlphabetical}) {
    std::vector<words::Word> wordsList;
    wordsList.reserve(labels.size());
    for (const std::string& label : labels) {
      wordsList.emplace_back(words::shapeText(kFont, label), 0.15, 0.0);
    }
    words::LayoutParams params;
    params.placement = placement;
    words::layoutWords(wordsList, words::Box{-800, -500, 800, 500}, params);
    for (size_t i = 0; i < wordsList.size(); ++i) {
      for (size_t j = i + 1; j < wordsList.size(); ++j) {
        INFO("placement " << words::placementName(placement) << ": words "
                          << i << " and " << j);
        CHECK_FALSE(wordsList[i].intersectsWord(wordsList[j]));
      }
    }
  }
}

TEST_CASE("layout is deterministic for a fixed seed") {
  auto lay = [](unsigned seed) {
    std::vector<words::Word> ws;
    for (const char* label : {"alpha", "beta", "gamma"}) {
      ws.emplace_back(words::shapeText(kFont, label), 0.15, 0.0);
    }
    words::LayoutParams params;
    params.seed = seed;
    words::layoutWords(ws, words::Box{-600, -400, 600, 400}, params);
    std::vector<std::pair<double, double>> positions;
    for (const words::Word& w : ws) positions.emplace_back(w.x(), w.y());
    return positions;
  };
  CHECK(lay(7) == lay(7));
  CHECK(lay(7) != lay(8));
}

}  // namespace
