#pragma once

#include <clipper2/clipper.h>

#include <cstdint>
#include <functional>
#include <vector>

#include "box.h"

namespace words {

// Knobs for hierarchical-bounding-box construction. The swell parameters
// are the word cloud's spacing aesthetic: every box is inflated by a
// logarithm of the word's size, so large words command more breathing room
// than small ones, but sublinearly. The leaf parameters set collision
// fidelity: leaves bottom out at an absolute display-space size, with a
// relative cap so enormous words don't subdivide forever.
struct HbbParams {
  double leafFloor = 25.0;        // min leaf dimension, scene px
  double leafFracOfWidth = 0.01;  // ...but never finer than this × width
  double hSwellFactor = 1.8;      // horizontal pad = hSwellFactor·ln(width)
  double vSwellBase = 1.2;        // vertical pad = vSwellBase + ln(height)
};

// A hierarchical bounding box over a word's ink: an adaptive binary
// subdivision (longest axis halves) in which a child exists only where its
// half actually intersects the glyph outlines. Empty space is simply absent
// from the tree, and regions that subdivided without finding internal
// structure collapse back into single leaves — so depth concentrates at
// glyph boundaries. A node with no children is an ink leaf.
//
// Boxes are stored in the owner's local frame; queries take the world
// translations of both owners, so moving a word never touches its tree.
class Hbb {
 public:
  Hbb() = default;

  // Builds over `paths` (disjoint filled contours, holes as separate
  // paths — i.e. shapeText output after transform) with `bounds` their
  // bounding box, all in the owner's local frame.
  Hbb(const Clipper2Lib::PathsD& paths, const Box& bounds,
      const HbbParams& params);

  bool empty() const { return nodes_.empty(); }

  // True if any ink leaf of this tree (translated by ax,ay) overlaps any
  // ink leaf of `other` (translated by bx,by). Alternating descent:
  // whichever side still has structure refines.
  bool intersects(const Hbb& other, double ax, double ay, double bx,
                  double by) const;

  // Visits every box, root first. `leaf` marks ink leaves. Boxes are the
  // swollen (collision-truth) ones; deflate by swellH()/swellV() to
  // recover the raw construction boxes.
  void visit(
      const std::function<void(const Box&, int depth, bool leaf)>& fn) const;

  double swellH() const { return swellH_; }
  double swellV() const { return swellV_; }

 private:
  struct Node {
    Box box;
    int32_t a = -1, b = -1;  // children; both -1 = ink leaf
  };

  int build(const class InkTester& ink, const Box& box, double minSize);
  bool nodeIntersects(int32_t i, const Hbb& other, int32_t j, double ax,
                      double ay, double bx, double by) const;
  void visitNode(int32_t i, int depth,
                 const std::function<void(const Box&, int, bool)>& fn) const;

  std::vector<Node> nodes_;  // nodes_[0] is the root when non-empty
  double swellH_ = 0, swellV_ = 0;
};

}  // namespace words
