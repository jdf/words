#pragma once

#include <clipper2/clipper.h>

namespace words {

// A closed polygon prop in the scene: fixed base geometry (centered on its
// own origin) with a dynamic rigid transform — rotation plus translation.
// Like Word, the geometry itself never changes after construction; the
// world-space polygon is derived on demand for collision queries.
class Shape {
 public:
  explicit Shape(Clipper2Lib::PathD base) : base_(std::move(base)) {}

  static Shape equilateralTriangle(double circumradius);

  double angle() const { return angleRad_; }
  void setAngle(double angleRad) { angleRad_ = angleRad; }

  double x() const { return x_; }
  double y() const { return y_; }
  void moveTo(double x, double y) {
    x_ = x;
    y_ = y;
  }

  // Unrotated vertices, centered at the origin (what a renderer uploads).
  const Clipper2Lib::PathD& basePath() const { return base_; }

  // The polygon at its current rotation and position, world coordinates.
  Clipper2Lib::PathsD worldPath() const;

 private:
  Clipper2Lib::PathD base_;
  double angleRad_ = 0;
  double x_ = 0, y_ = 0;
};

}  // namespace words
