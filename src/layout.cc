#include "layout.h"

#include <cmath>
#include <numbers>
#include <random>
#include <vector>

#include "box.h"
#include "quad_tree.h"
#include "word.h"

namespace words {

void layoutWords(std::vector<Word>& wordList, const Box& bounds,
                 const LayoutParams& params, LayoutDebug* debug) {
  std::mt19937 rng(params.seed);
  std::uniform_real_distribution<double> angle(0.0, 2.0 * std::numbers::pi);

  for (Word& w : wordList) {
    w.buildHbb(params.hbb);
  }

  QuadTree index(bounds, params.quadMinCell, params.quadMaxDepth);
  for (Word& w : wordList) {
    bool traced = debug && w.label() == debug->traceLabel;
    const double startX = bounds.centerX();
    const double startY = bounds.centerY();
    w.moveTo(startX, startY);
    if (traced) debug->trail.push_back({w.x(), w.y()});

    double theta = angle(rng);
    double radius = 1.0;
    bool saturated = false;  // spiral has outgrown the world
    const Word* lastHit = nullptr;

    while (true) {
      const Word* hit =
          (lastHit && w.intersectsWord(*lastHit)) ? lastHit
                                                  : index.firstIntersecting(w);
      if (!hit) break;
      lastHit = hit;

      // Advance along the spiral; skip positions that stick out of the
      // world until the spiral is bigger than the world itself.
      do {
        w.moveTo(startX + radius * std::cos(theta),
                 startY + radius * std::sin(theta));
        if (traced) debug->trail.push_back({w.x(), w.y()});
        theta += params.dTheta;
        radius += params.dRadius;
        if (radius > bounds.width() || radius > bounds.height()) {
          saturated = true;
        }
      } while (!saturated && !bounds.contains(w.worldBounds()));
    }

    index.add(&w);
  }
}

}  // namespace words
