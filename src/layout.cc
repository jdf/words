#include "layout.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <random>
#include <string_view>
#include <vector>

#include "box.h"
#include "quad_tree.h"
#include "word.h"

namespace words {

std::optional<Placement> findPlacement(std::string_view name) {
  if (name == "center-line") return Placement::kCenterLine;
  if (name == "center") return Placement::kCenter;
  return std::nullopt;
}

std::string_view placementName(Placement placement) {
  switch (placement) {
    case Placement::kCenterLine: return "Center Line";
    case Placement::kCenter: return "Center";
  }
  return "?";
}

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
  size_t committed = 0;
  for (size_t idx : order) {
    Word& w = wordList[idx];
    bool traced = debug && w.label() == debug->traceLabel;
    const Box& b = w.localBounds();

    // A saturated spiral (one that has outgrown the world without finding
    // room) commits its word wherever it next happens to be free — which
    // is a full world-dimension away from its seed, a far-flung stray.
    // The original behaved exactly this way, but its 150-word cap made
    // saturation rare; at higher densities we retry from fresh seeds
    // instead, and only the last attempt is allowed to run to infinity.
    constexpr int kMaxSeeds = 5;
    bool placed = false;
    for (int attempt = 0; !placed; ++attempt) {
      bool lastAttempt = attempt + 1 >= kMaxSeeds;
      double startX = startXs[idx];
      double startY = bounds.centerY();
      if (attempt > 0) {
        startX = bounds.minX + unit(rng) * bounds.width();
      }
      if (params.placement == Placement::kCenterLine || attempt > 0) {
        // place(): y jittered near the horizontal center line, jitter
        // scale proportional to the word's own smaller dimension.
        double sign = unit(rng) < 0.5 ? -1.0 : 1.0;
        startY += derriere(rng, 1.4) * 1.25 *
                  std::min(b.width(), b.height()) * sign;
      }
      w.moveTo(startX, startY);
      if (traced) debug->trail.push_back({w.x(), w.y()});

      double theta = angle(rng);
      double radius = 1.0;
      bool saturated = false;  // spiral has outgrown the world
      const Word* lastHit = nullptr;

      while (true) {
        const Word* hit = (lastHit && w.intersectsWord(*lastHit))
                              ? lastHit
                              : index.firstIntersecting(w);
        if (!hit) {
          placed = true;
          break;
        }
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

        // The moment the spiral outgrows the world, its position is a
        // world-dimension away from the seed — anything it finds out
        // there is a stray. Reseed instead (except on the last attempt,
        // which keeps the original run-to-infinity guarantee).
        if (saturated && !lastAttempt) break;
      }
    }

    index.add(&w);
    if (params.progress) params.progress(++committed, order.size());
  }
}

}  // namespace words
