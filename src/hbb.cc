#include "hbb.h"

#include <clipper2/clipper.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "box.h"

namespace words {

namespace {

Box boundsOfPath(const Clipper2Lib::PathD& path) {
  Box b{path[0].x, path[0].y, path[0].x, path[0].y};
  for (const auto& p : path) {
    b.minX = std::min(b.minX, p.x);
    b.maxX = std::max(b.maxX, p.x);
    b.minY = std::min(b.minY, p.y);
    b.maxY = std::max(b.maxY, p.y);
  }
  return b;
}

// Liang–Barsky: does the segment p→q pass through rect r?
bool segmentIntersectsRect(double px, double py, double qx, double qy,
                           const Box& r) {
  double t0 = 0.0, t1 = 1.0;
  double dx = qx - px, dy = qy - py;
  const double p[4] = {-dx, dx, -dy, dy};
  const double q[4] = {px - r.minX, r.maxX - px, py - r.minY, r.maxY - py};
  for (int i = 0; i < 4; ++i) {
    if (p[i] == 0.0) {
      if (q[i] < 0.0) return false;  // parallel and outside
      continue;
    }
    double t = q[i] / p[i];
    if (p[i] < 0.0) {
      if (t > t1) return false;
      t0 = std::max(t0, t);
    } else {
      if (t < t0) return false;
      t1 = std::min(t1, t);
    }
  }
  return t0 <= t1;
}

}  // namespace

// Answers "does this axis-aligned rect touch ink?" against a fixed set of
// disjoint filled contours. A rect touches ink iff some contour edge passes
// through it, or — with no edge inside — the rect lies wholly in the filled
// region, decided by an even-odd test of its center (which handles holes:
// inside outer + inside hole = even = no ink).
class InkTester {
 public:
  explicit InkTester(const Clipper2Lib::PathsD& paths) : paths_(paths) {
    contourBounds_.reserve(paths.size());
    for (const auto& path : paths) {
      contourBounds_.push_back(boundsOfPath(path));
    }
  }

  bool intersects(const Box& rect) const {
    for (size_t c = 0; c < paths_.size(); ++c) {
      if (!contourBounds_[c].overlaps(rect)) continue;
      const auto& path = paths_[c];
      size_t n = path.size();
      for (size_t i = 0; i < n; ++i) {
        const auto& p = path[i];
        const auto& q = path[(i + 1) % n];
        if (segmentIntersectsRect(p.x, p.y, q.x, q.y, rect)) {
          return true;
        }
      }
    }
    return insideInk(rect.centerX(), rect.centerY());
  }

 private:
  bool insideInk(double x, double y) const {
    int crossings = 0;
    for (size_t c = 0; c < paths_.size(); ++c) {
      const Box& b = contourBounds_[c];
      if (y < b.minY || y > b.maxY || x > b.maxX) continue;
      const auto& path = paths_[c];
      size_t n = path.size();
      for (size_t i = 0; i < n; ++i) {
        const auto& p = path[i];
        const auto& q = path[(i + 1) % n];
        if ((p.y > y) != (q.y > y)) {
          double xAtY = p.x + (q.x - p.x) * (y - p.y) / (q.y - p.y);
          if (xAtY > x) ++crossings;
        }
      }
    }
    return (crossings & 1) != 0;
  }

  const Clipper2Lib::PathsD& paths_;
  std::vector<Box> contourBounds_;
};

Hbb::Hbb(const Clipper2Lib::PathsD& paths, const Box& bounds,
         const HbbParams& params) {
  if (paths.empty()) return;
  InkTester ink(paths);
  double minSize =
      std::max(params.leafFloor, bounds.width() * params.leafFracOfWidth);
  build(ink, bounds, minSize);

  // Swell: the spacing aesthetic. Every box inflates by a logarithm of the
  // word's overall size, so neighbors keep sublinearly size-proportional
  // distance. Also serves as collision slop.
  double h = std::max(0.0, params.hSwellFactor * std::log(bounds.width()));
  double v = std::max(0.0, params.vSwellBase + std::log(bounds.height()));
  swellH_ = h;
  swellV_ = v;
  for (Node& n : nodes_) {
    n.box.minX -= h;
    n.box.maxX += h;
    n.box.minY -= v;
    n.box.maxY += v;
  }
}

int Hbb::build(const InkTester& ink, const Box& box, double minSize) {
  int32_t index = static_cast<int32_t>(nodes_.size());
  nodes_.push_back({box, -1, -1});

  Box first, second;
  if (box.width() >= box.height()) {
    if (box.width() <= minSize) return index;
    double mid = box.centerX();
    first = {box.minX, box.minY, mid, box.maxY};
    second = {mid, box.minY, box.maxX, box.maxY};
  } else {
    if (box.height() <= minSize) return index;
    double mid = box.centerY();
    first = {box.minX, box.minY, box.maxX, mid};
    second = {box.minX, mid, box.maxX, box.maxY};
  }

  // Children exist only where there is ink; recursion may add nodes, so
  // don't hold a reference across the calls.
  int32_t a = ink.intersects(first) ? build(ink, first, minSize) : -1;
  int32_t b = ink.intersects(second) ? build(ink, second, minSize) : -1;

  // Prune filled regions: if both halves subdivided without finding any
  // internal structure, the parent alone is just as good a leaf.
  auto isLeaf = [this](int32_t i) {
    return nodes_[i].a == -1 && nodes_[i].b == -1;
  };
  if (a != -1 && b != -1 && isLeaf(a) && isLeaf(b)) {
    nodes_.resize(index + 1);  // children are the last two nodes
    a = b = -1;
  }
  nodes_[index].a = a;
  nodes_[index].b = b;
  return index;
}

bool Hbb::intersects(const Hbb& other, double ax, double ay, double bx,
                     double by) const {
  if (nodes_.empty() || other.nodes_.empty()) return false;
  return nodeIntersects(0, other, 0, ax, ay, bx, by);
}

bool Hbb::nodeIntersects(int32_t i, const Hbb& other, int32_t j, double ax,
                         double ay, double bx, double by) const {
  const Node& mine = nodes_[i];
  const Node& theirs = other.nodes_[j];
  if (!mine.box.translated(ax, ay).overlaps(theirs.box.translated(bx, by))) {
    return false;
  }

  if (mine.a == -1 && mine.b == -1) {
    // I'm an ink leaf; let the other side refine if it can.
    if (theirs.a == -1 && theirs.b == -1) return true;
    return other.nodeIntersects(j, *this, i, bx, by, ax, ay);
  }

  return (mine.a != -1 && nodeIntersects(mine.a, other, j, ax, ay, bx, by)) ||
         (mine.b != -1 && nodeIntersects(mine.b, other, j, ax, ay, bx, by));
}

void Hbb::visit(
    const std::function<void(const Box&, int depth, bool leaf)>& fn) const {
  if (!nodes_.empty()) visitNode(0, 0, fn);
}

void Hbb::visitNode(
    int32_t i, int depth,
    const std::function<void(const Box&, int, bool)>& fn) const {
  const Node& n = nodes_[i];
  fn(n.box, depth, n.a == -1 && n.b == -1);
  if (n.a != -1) visitNode(n.a, depth + 1, fn);
  if (n.b != -1) visitNode(n.b, depth + 1, fn);
}

}  // namespace words
