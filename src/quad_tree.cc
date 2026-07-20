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
  // (An off-center grid — split lines moved off the dominant seeding
  // point — was benched and bought nothing: center placement's cost is
  // spiral path length through the packed disc, not root-straddler
  // scans. Layouts are provably index-structure-independent — the
  // spiral only asks hit-or-no-hit — so such experiments are safe to
  // retry if the workload changes.)
  root_.cx = bounds.centerX();
  root_.cy = bounds.centerY();
  root_.half = std::max(bounds.width(), bounds.height()) / 2.0;
  root_.depth = 1;
}

// Whether the root's region wholly contains `b`. Below the root this
// holds by construction (a box enters a child only via quadrantOf, and
// children tile the parent), but a saturated spiral's last attempt may
// commit a word beyond the world — such a box stays at the root, where
// every query scans it, keeping the child-region pruning sound.
bool QuadTree::rootContains(const Box& b) const {
  return b.minX >= root_.cx - root_.half && b.maxX <= root_.cx + root_.half &&
         b.minY >= root_.cy - root_.half && b.maxY <= root_.cy + root_.half;
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
  const Box b = word->collisionBounds();
  Node* home = rootContains(b) ? descend(&root_, b, /*createChildren=*/true)
                               : &root_;
  home->objects.push_back(word);
}

const Word* QuadTree::firstIntersecting(const Word& candidate) const {
  // Home: the deepest existing node wholly containing the candidate.
  // (Unallocated deeper nodes hold no objects, so stopping early only
  // widens the subtree scan, never misses anything.)
  const Node* home = &root_;
  Box b = candidate.collisionBounds();
  if (rootContains(b)) {
    while (home->depth < maxDepth_ && 2 * home->half > minCellWidth_) {
      int q = quadrantOf(*home, b);
      if (q < 0 || !home->kids[q]) break;
      home = home->kids[q].get();
    }
  }

  // Everything below home...
  if (const Word* hit = firstInSubtree(*home, candidate, b)) return hit;
  // ...plus straddlers on the ancestor path (home itself was covered by
  // the subtree scan). Re-descending costs a few quadrantOf calls and
  // saves a per-query heap allocation for the path.
  for (const Node* n = &root_; n != home;
       n = n->kids[quadrantOf(*n, b)].get()) {
    for (const Word* w : n->objects) {
      if (candidate.intersectsWord(*w)) return w;
    }
  }
  return nullptr;
}

const Word* QuadTree::firstInSubtree(const Node& n, const Word& candidate,
                                     const Box& b) const {
  for (const Word* w : n.objects) {
    if (candidate.intersectsWord(*w)) return w;
  }
  for (const auto& kid : n.kids) {
    if (!kid) continue;
    // Every word in the kid's subtree has its collision box wholly
    // inside the kid's region, so a region strictly disjoint from the
    // candidate's box can hold no hit. (Strict: edge-touching boxes go
    // through intersectsWord, which owns the boundary semantics.)
    if (b.minX > kid->cx + kid->half || b.maxX < kid->cx - kid->half ||
        b.minY > kid->cy + kid->half || b.maxY < kid->cy - kid->half) {
      continue;
    }
    if (const Word* hit = firstInSubtree(*kid, candidate, b)) return hit;
  }
  return nullptr;
}

}  // namespace words
