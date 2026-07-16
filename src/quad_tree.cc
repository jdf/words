#include "quad_tree.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "box.h"
#include "word.h"

namespace words {

QuadTree::QuadTree(const Box& bounds, double minCellWidth, int maxDepth)
    : minCellWidth_(minCellWidth), maxDepth_(maxDepth) {
  root_.cx = bounds.centerX();
  root_.cy = bounds.centerY();
  root_.half = std::max(bounds.width(), bounds.height()) / 2.0;
  root_.depth = 1;
}

int QuadTree::quadrantOf(const Node& n, const Box& b) {
  if (b.minX <= n.cx && b.maxX > n.cx) return -1;
  if (b.minY <= n.cy && b.maxY > n.cy) return -1;
  bool east = b.minX > n.cx;
  bool south = b.minY > n.cy;
  return (south ? 2 : 0) + (east ? 1 : 0);
}

QuadTree::Node* QuadTree::descend(Node* n, const Box& b,
                                  bool createChildren) {
  while (true) {
    if (n->depth >= maxDepth_ || 2 * n->half <= minCellWidth_) return n;
    int q = quadrantOf(*n, b);
    if (q < 0) return n;
    if (!n->kids[q]) {
      if (!createChildren) return n;
      auto kid = std::make_unique<Node>();
      double h = n->half / 2.0;
      kid->cx = n->cx + ((q & 1) ? h : -h);
      kid->cy = n->cy + ((q & 2) ? h : -h);
      kid->half = h;
      kid->depth = n->depth + 1;
      n->kids[q] = std::move(kid);
    }
    n = n->kids[q].get();
  }
}

void QuadTree::add(const Word* word) {
  descend(&root_, word->collisionBounds(), /*createChildren=*/true)
      ->objects.push_back(word);
}

const Word* QuadTree::firstIntersecting(const Word& candidate) const {
  // Home: the deepest existing node wholly containing the candidate.
  // (Unallocated deeper nodes hold no objects, so stopping early only
  // widens the subtree scan, never misses anything.)
  std::vector<const Node*> path;
  const Node* n = &root_;
  path.push_back(n);
  Box b = candidate.collisionBounds();
  while (n->depth < maxDepth_ && 2 * n->half > minCellWidth_) {
    int q = quadrantOf(*n, b);
    if (q < 0 || !n->kids[q]) break;
    n = n->kids[q].get();
    path.push_back(n);
  }

  // Everything below home...
  if (const Word* hit = firstInSubtree(*n, candidate)) return hit;
  // ...plus straddlers on the ancestor path (home itself was covered by
  // the subtree scan).
  for (size_t i = 0; i + 1 < path.size(); ++i) {
    for (const Word* w : path[i]->objects) {
      if (candidate.intersectsWord(*w)) return w;
    }
  }
  return nullptr;
}

const Word* QuadTree::firstInSubtree(const Node& n,
                                     const Word& candidate) const {
  for (const Word* w : n.objects) {
    if (candidate.intersectsWord(*w)) return w;
  }
  for (const auto& kid : n.kids) {
    if (!kid) continue;
    if (const Word* hit = firstInSubtree(*kid, candidate)) return hit;
  }
  return nullptr;
}

}  // namespace words
