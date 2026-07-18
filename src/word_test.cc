// Unit tests for Word: shaped geometry with baked scale/rotation and a
// single dynamic degree of freedom (position).

#include "word.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <numbers>

#include "box.h"

namespace {

constexpr const char* kFont = WORDS_ASSETS_DIR "/fonts/sexsmith.ttf";

TEST_CASE("a shaped word has ink, centered on its own origin") {
  words::ShapedText shaped = words::shapeText(kFont, "whale");
  REQUIRE_FALSE(shaped.empty());
  words::Word w(shaped, 0.1, 0.0);
  CHECK_FALSE(w.localPaths().empty());
  const words::Box& b = w.localBounds();
  CHECK(b.width() > 0);
  CHECK(b.height() > 0);
  CHECK(b.width() > b.height());  // "whale" is wider than tall
  // Centered: the box straddles the origin.
  CHECK(b.minX < 0);
  CHECK(b.maxX > 0);
}

TEST_CASE("rotation by 90 degrees transposes the bounds") {
  words::ShapedText shaped = words::shapeText(kFont, "whale");
  words::Word flat(shaped, 0.1, 0.0);
  words::Word up(shaped, 0.1, std::numbers::pi / 2.0);
  CHECK(up.localBounds().height() > up.localBounds().width());
  CHECK(up.localBounds().height() ==
        Catch::Approx(flat.localBounds().width()).epsilon(0.01));
}

TEST_CASE("moving translates world bounds, never local geometry") {
  words::ShapedText shaped = words::shapeText(kFont, "hi");
  words::Word w(shaped, 0.1, 0.0);
  const words::Box local = w.localBounds();
  w.moveTo(100, 50);
  CHECK(w.worldBounds().minX == local.minX + 100);
  CHECK(w.worldBounds().maxY == local.maxY + 50);
  w.moveBy(-100, -50);
  CHECK(w.x() == 0.0);
  CHECK(w.worldBounds().minX == local.minX);
  CHECK(w.localBounds().minX == local.minX);
}

TEST_CASE("hbb collision: overlapping words collide, distant ones do not") {
  words::ShapedText shaped = words::shapeText(kFont, "collide");
  words::Word a(shaped, 0.1, 0.0);
  words::Word b(shaped, 0.1, 0.0);
  a.buildHbb();
  b.buildHbb();
  a.moveTo(0, 0);
  b.moveTo(0, 0);  // dead overlap
  CHECK(a.intersectsWord(b));
  b.moveTo(a.collisionBounds().width() * 4, 0);  // far beyond any swell
  CHECK_FALSE(a.intersectsWord(b));
}

TEST_CASE("collision bounds swell beyond the ink bounds") {
  words::ShapedText shaped = words::shapeText(kFont, "roomy");
  words::Word w(shaped, 0.1, 0.0);
  w.buildHbb();
  CHECK(w.collisionBounds().width() > w.worldBounds().width());
  CHECK(w.collisionBounds().height() > w.worldBounds().height());
}

}  // namespace
