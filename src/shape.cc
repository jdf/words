#include "shape.h"

#include <cmath>

namespace words {

Shape Shape::equilateralTriangle(double circumradius) {
  constexpr double kCos30 = 0.86602540378;
  return Shape(Clipper2Lib::PathD{
      {0.0, circumradius},
      {-circumradius * kCos30, -circumradius * 0.5},
      {circumradius * kCos30, -circumradius * 0.5},
  });
}

Clipper2Lib::PathsD Shape::worldPath() const {
  double c = std::cos(angleRad_), s = std::sin(angleRad_);
  Clipper2Lib::PathsD world(1);
  world[0].reserve(base_.size());
  for (const auto& v : base_) {
    world[0].push_back({c * v.x - s * v.y + x_, s * v.x + c * v.y + y_});
  }
  return world;
}

}  // namespace words
