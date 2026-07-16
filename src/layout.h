#pragma once

#include <cstdint>
#include <vector>

#include "box.h"
#include "hbb.h"
#include "word.h"

namespace words {

struct LayoutParams {
  // Archimedean search spiral: per step, angle advances dTheta and radius
  // grows dRadius (both scene px / radians).
  double dTheta = 0.04;
  double dRadius = 0.7;
  HbbParams hbb;
  double quadMinCell = 200.0;
  int quadMaxDepth = 8;
  uint32_t seed = 1;
};

// Places `wordList` into `bounds`, in the order given (classically most
// important first). Each word starts at the center and walks an Archimedean
// spiral (random starting angle) until it collides with nothing already
// committed; committed words live in a quadtree, and the previous collider
// is re-tested first (on a spiral, the thing that blocked you last step
// almost always still does). Positions outside `bounds` are skipped until
// the spiral outgrows the world, which guarantees termination.
//
// Builds every word's HBB (the expensive phase). On return, every word has
// its final position. Deterministic for a given seed.
void layoutWords(std::vector<Word>& wordList, const Box& bounds,
                 const LayoutParams& params = {});

}  // namespace words
