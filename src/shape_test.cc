// Unit tests for the polygon prop.

#include "shape.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

namespace {

using Catch::Approx;

TEST_CASE("equilateral triangle: three vertices at the circumradius") {
  words::Shape tri = words::Shape::equilateralTriangle(100.0);
  const Clipper2Lib::PathD& base = tri.basePath();
  REQUIRE(base.size() == 3);
  for (const auto& v : base) {
    CHECK(std::hypot(v.x, v.y) == Approx(100.0));
  }
}

TEST_CASE("world path applies rotation then translation") {
  words::Shape s(Clipper2Lib::PathD{{10, 0}, {-10, 0}, {0, 5}});
  s.setAngle(std::numbers::pi / 2.0);  // +90°: (10,0) -> (0,10)
  s.moveTo(100, 200);
  Clipper2Lib::PathsD world = s.worldPath();
  REQUIRE(world.size() == 1);
  REQUIRE(world[0].size() == 3);
  CHECK(world[0][0].x == Approx(100.0).margin(1e-9));
  CHECK(world[0][0].y == Approx(210.0));
}

}  // namespace
