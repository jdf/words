#pragma once

#include <clipper2/clipper.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "box.h"
#include "hbb.h"
#include "word.h"

namespace words {

// Debug instrumentation: set traceLabel to a word's label and layoutWords
// records every position that word tried (including out-of-bounds skips)
// on its way to rest — the search spiral, ready to draw.
struct LayoutDebug {
  std::string traceLabel;
  std::vector<Clipper2Lib::PointD> trail;
};

// Initial-placement strategy: where a word starts before the spiral
// jiggles it. This is what shapes the overall cloud: spiraling from a
// shared center always yields a disc, while distributing starts along the
// horizontal center line (the original's default, "Random Center Line")
// stretches the cloud into the classic wide lens.
enum class Placement {
  kCenterLine,  // random x across the width, y jittered near the center line
  kCenter,      // everything starts at the exact center
};

// Lookup by slug ("center-line", "center") and human-readable label
// ("Center Line", "Center").
std::optional<Placement> findPlacement(std::string_view name);
std::string_view placementName(Placement placement);

struct LayoutParams {
  // Archimedean search spiral: per step, angle advances dTheta and radius
  // grows dRadius — the original's constants, calibrated to its coordinate
  // convention (the heaviest word has a 1000-unit em).
  double dTheta = 0.04;
  double dRadius = 0.7;
  Placement placement = Placement::kCenterLine;
  HbbParams hbb;
  double quadMinCell = 200.0;
  int quadMaxDepth = 8;
  uint32_t seed = 1;
  // Called after each word is committed (placed count, total). Layout
  // blocks its thread; this is how the host hears about progress mid-run.
  std::function<void(size_t, size_t)> progress;
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
                 const LayoutParams& params = {},
                 LayoutDebug* debug = nullptr);

}  // namespace words
