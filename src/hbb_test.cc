// Unit tests for hierarchical bounding boxes over polygon ink.

#include "hbb.h"

#include <catch2/catch_test_macros.hpp>

#include "box.h"

namespace {

// A filled square as "ink".
words::Hbb squareHbb(double size, const words::HbbParams& params = {}) {
  Clipper2Lib::PathD square;
  square.emplace_back(0.0, 0.0);
  square.emplace_back(size, 0.0);
  square.emplace_back(size, size);
  square.emplace_back(0.0, size);
  Clipper2Lib::PathsD paths{square};
  return words::Hbb(paths, words::Box{0, 0, size, size}, params);
}

TEST_CASE("an hbb over ink is nonempty; over nothing, empty") {
  CHECK_FALSE(squareHbb(100).empty());
  words::Hbb none;
  CHECK(none.empty());
}

TEST_CASE("the root box swells beyond the ink") {
  words::Hbb h = squareHbb(100);
  const words::Box& root = h.rootBox();
  CHECK(root.minX < 0);
  CHECK(root.minY < 0);
  CHECK(root.maxX > 100);
  CHECK(root.maxY > 100);
  CHECK(h.swellH() > 0);
  CHECK(h.swellV() > 0);
}

TEST_CASE("visit walks root-first and marks ink leaves") {
  words::Hbb h = squareHbb(400);
  int boxes = 0, leaves = 0, rootDepth = -1;
  h.visit([&](const words::Box&, int depth, bool leaf) {
    if (boxes == 0) rootDepth = depth;
    ++boxes;
    if (leaf) ++leaves;
  });
  CHECK(rootDepth == 0);
  CHECK(boxes >= 1);
  CHECK(leaves >= 1);
}

TEST_CASE("intersection respects translation") {
  words::Hbb a = squareHbb(100);
  words::Hbb b = squareHbb(100);
  CHECK(a.intersects(b, 0, 0, 0, 0));  // dead overlap
  // Separated far beyond the swell in x.
  double far = a.rootBox().width() * 3;
  CHECK_FALSE(a.intersects(b, 0, 0, far, 0));
  // Diagonal separation just outside the swollen boxes.
  CHECK_FALSE(a.intersects(b, 0, 0, far, far));
}

TEST_CASE("a solid square's tree is shallow: no internal structure") {
  // A solid block collapses subdivisions that find no boundary; the tree
  // stays small compared to the leaf-resolution worst case.
  words::Hbb h = squareHbb(800);
  int boxes = 0;
  h.visit([&](const words::Box&, int, bool) { ++boxes; });
  CHECK(boxes < 64);
}

}  // namespace
