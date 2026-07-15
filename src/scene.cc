#include "scene.h"

#include <cmath>

namespace words {

const Clipper2Lib::PathD& Scene::triangleBase() {
  static const Clipper2Lib::PathD base = [] {
    constexpr double kCos30 = 0.86602540378;
    return Clipper2Lib::PathD{
        {0.0, kTriangleR},
        {-kTriangleR * kCos30, -kTriangleR * 0.5},
        {kTriangleR * kCos30, -kTriangleR * 0.5},
    };
  }();
  return base;
}

Clipper2Lib::PathsD Scene::trianglePath() const {
  double c = std::cos(triangleAngle_), s = std::sin(triangleAngle_);
  Clipper2Lib::PathsD tri(1);
  for (const auto& v : triangleBase()) {
    tri[0].push_back({c * v.x - s * v.y, s * v.x + c * v.y});
  }
  return tri;
}

void Scene::update(double t) {
  triangleAngle_ = t * 0.6;
  Clipper2Lib::PathsD tri = trianglePath();
  for (Entry& e : entries_) {
    e.hit = e.word.boxIntersects(tri);
  }
}

}  // namespace words
