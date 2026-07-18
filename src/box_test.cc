// Unit tests for the axis-aligned bounding box.

#include "box.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

namespace {

TEST_CASE("box measures and centers") {
  words::Box b{-2, -1, 4, 5};
  CHECK(b.width() == 6.0);
  CHECK(b.height() == 6.0);
  CHECK(b.centerX() == 1.0);
  CHECK(b.centerY() == 2.0);
}

TEST_CASE("box translation moves both corners") {
  words::Box b{0, 0, 2, 3};
  words::Box t = b.translated(10, -5);
  CHECK(t.minX == 10.0);
  CHECK(t.minY == -5.0);
  CHECK(t.maxX == 12.0);
  CHECK(t.maxY == -2.0);
  // The original is untouched.
  CHECK(b.minX == 0.0);
}

TEST_CASE("overlap is strict: shared edges do not overlap") {
  words::Box a{0, 0, 10, 10};
  CHECK(a.overlaps({5, 5, 15, 15}));
  CHECK(a.overlaps({-5, -5, 1, 1}));
  CHECK_FALSE(a.overlaps({10, 0, 20, 10}));  // touching right edge
  CHECK_FALSE(a.overlaps({0, 10, 10, 20}));  // touching top edge
  CHECK_FALSE(a.overlaps({20, 20, 30, 30}));
}

TEST_CASE("containment is inclusive of edges") {
  words::Box a{0, 0, 10, 10};
  CHECK(a.contains({0, 0, 10, 10}));  // itself
  CHECK(a.contains({2, 2, 8, 8}));
  CHECK_FALSE(a.contains({-1, 2, 8, 8}));
  CHECK_FALSE(a.contains({2, 2, 8, 11}));
}

TEST_CASE("asPath is the four corners, counterclockwise") {
  words::Box b{1, 2, 3, 4};
  Clipper2Lib::PathD p = b.asPath();
  REQUIRE(p.size() == 4);
  CHECK(p[0].x == 1.0);
  CHECK(p[0].y == 2.0);
  CHECK(p[2].x == 3.0);
  CHECK(p[2].y == 4.0);
  // Shoelace signed area is positive for CCW.
  double area = 0;
  for (size_t i = 0; i < p.size(); ++i) {
    const auto& a = p[i];
    const auto& c = p[(i + 1) % p.size()];
    area += a.x * c.y - c.x * a.y;
  }
  CHECK(area > 0);
}

}  // namespace
