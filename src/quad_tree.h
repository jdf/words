#pragma once

#include <array>
#include <memory>
#include <vector>

#include "box.h"
#include "word.h"

namespace words {

// Spatial index of committed (already-placed) words. Each word is stored at
// the smallest node whose region wholly contains its root bounds; words
// straddling a split line live at internal nodes. A candidate query checks
// its home node's entire subtree plus the object lists on the ancestor
// path — complete, because anything overlapping the candidate either fits
// inside the home region (and so lives in its subtree) or straddles a
// boundary of some ancestor (and so lives on the ancestor path).
//
// Holds pointers into the caller's word storage; that storage must outlive
// the index and must not reallocate while indexed.
class QuadTree {
 public:
  explicit QuadTree(const Box& bounds, double minCellWidth = 200.0,
                    int maxDepth = 8);

  // Indexes a placed word at its current position.
  void add(const Word* word);

  // The first committed word whose HBB intersects the candidate's, or
  // nullptr. Both HBBs must be built.
  const Word* firstIntersecting(const Word& candidate) const;

 private:
  struct Node {
    double cx = 0, cy = 0, half = 0;
    int depth = 1;
    std::vector<const Word*> objects;
    std::array<std::unique_ptr<Node>, 4> kids;  // created on demand
  };

  // Index of the child quadrant wholly containing `b`, or -1 if it
  // straddles a split line of `n`.
  static int quadrantOf(const Node& n, const Box& b);
  Node* descend(Node* n, const Box& b, bool createChildren);
  const Word* firstInSubtree(const Node& n, const Word& candidate) const;

  double minCellWidth_;
  int maxDepth_;
  Node root_;
};

}  // namespace words
