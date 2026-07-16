#include "layout.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <random>
#include <vector>

#include "box.h"
#include "quad_tree.h"
#include "word.h"

namespace words {

namespace {

// The original's "derriere" distribution: values hugging ±0.5, thinning
// toward 0 and ±1.5 — jitter that keeps words near the line but not on it.
double derriere(std::mt19937& rng, double centerDensity) {
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  double sign = unit(rng) < 0.5 ? -1.0 : 1.0;
  return 0.5 + std::pow(unit(rng), centerDensity) * sign;
}

}  // namespace

void layoutWords(std::vector<Word>& wordList, const Box& bounds,
                 const LayoutParams& params, LayoutDebug* debug) {
  std::mt19937 rng(params.seed);
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  std::uniform_real_distribution<double> angle(0.0, 2.0 * std::numbers::pi);

  for (Word& w : wordList) {
    w.buildHbb(params.hbb);
  }

  // prepare(): each word draws its starting x up front (uniform across the
  // world width), and placement runs biggest-area first — early words claim
  // the middle band, later ones fill in around them.
  std::vector<double> startXs(wordList.size(), bounds.centerX());
  if (params.placement == Placement::kCenterLine) {
    for (double& x : startXs) {
      x = bounds.minX + unit(rng) * bounds.width();
    }
  }
  std::vector<size_t> order(wordList.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    const Box& ba = wordList[a].localBounds();
    const Box& bb = wordList[b].localBounds();
    return ba.width() * ba.height() > bb.width() * bb.height();
  });

  QuadTree index(bounds, params.quadMinCell, params.quadMaxDepth);
  for (size_t idx : order) {
    Word& w = wordList[idx];
    bool traced = debug && w.label() == debug->traceLabel;
    double startX = startXs[idx];
    double startY = bounds.centerY();
    if (params.placement == Placement::kCenterLine) {
      // place(): y jittered near the horizontal center line, jitter scale
      // proportional to the word's own smaller dimension.
      const Box& b = w.localBounds();
      double sign = unit(rng) < 0.5 ? -1.0 : 1.0;
      startY += derriere(rng, 1.4) * 1.25 * std::min(b.width(), b.height()) *
                sign;
    }
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
