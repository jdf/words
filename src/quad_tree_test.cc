// Unit tests for the spatial index over committed words.

#include "quad_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "box.h"
#include "word.h"

namespace {

constexpr const char* kFont = WORDS_ASSETS_DIR "/fonts/sexsmith.ttf";

TEST_CASE("finds the overlapping word, ignores distant ones") {
  words::ShapedText shaped = words::shapeText(kFont, "index");
  REQUIRE_FALSE(shaped.empty());
  // Storage must not reallocate while indexed.
  std::vector<words::Word> committed;
  committed.reserve(3);
  double step = 0;
  for (int i = 0; i < 3; ++i) {
    committed.emplace_back(shaped, 0.1, 0.0);
    committed.back().buildHbb();
    step = committed.back().collisionBounds().width() * 3;
    committed.back().moveTo(static_cast<double>(i) * step, 0);
  }
  words::QuadTree index(words::Box{-step * 2, -step * 2, step * 5, step * 2});
  for (const words::Word& w : committed) index.add(&w);

  words::Word probe(shaped, 0.1, 0.0);
  probe.buildHbb();
  probe.moveTo(step, 0);  // dead on committed[1]
  CHECK(index.firstIntersecting(probe) == &committed[1]);
  probe.moveTo(step * 10, step * 10);  // far from everything
  CHECK(index.firstIntersecting(probe) == nullptr);
}

TEST_CASE("an empty index intersects nothing") {
  words::QuadTree index(words::Box{-1000, -1000, 1000, 1000});
  words::ShapedText shaped = words::shapeText(kFont, "alone");
  words::Word probe(shaped, 0.1, 0.0);
  probe.buildHbb();
  CHECK(index.firstIntersecting(probe) == nullptr);
}

TEST_CASE("boundary-straddling words are still found") {
  words::ShapedText shaped = words::shapeText(kFont, "straddle");
  std::vector<words::Word> committed;
  committed.reserve(1);
  committed.emplace_back(shaped, 0.1, 0.0);
  committed.back().buildHbb();
  committed.back().moveTo(0, 0);  // dead center: straddles every split line
  words::QuadTree index(words::Box{-4000, -4000, 4000, 4000}, 50.0, 8);
  index.add(&committed[0]);

  words::Word probe(shaped, 0.1, 0.0);
  probe.buildHbb();
  probe.moveTo(0, 0);
  CHECK(index.firstIntersecting(probe) == &committed[0]);
}

}  // namespace
