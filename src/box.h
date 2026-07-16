#pragma once

#include <clipper2/clipper.h>

namespace words {

// Axis-aligned bounding box.
struct Box {
  double minX = 0, minY = 0, maxX = 0, maxY = 0;

  double width() const { return maxX - minX; }
  double height() const { return maxY - minY; }
  double centerX() const { return (minX + maxX) / 2.0; }
  double centerY() const { return (minY + maxY) / 2.0; }

  Box translated(double dx, double dy) const {
    return {minX + dx, minY + dy, maxX + dx, maxY + dy};
  }

  bool overlaps(const Box& o) const {
    return o.maxX > minX && o.maxY > minY && o.minX < maxX && o.minY < maxY;
  }

  bool contains(const Box& o) const {
    return o.minX >= minX && o.maxX <= maxX && o.minY >= minY && o.maxY <= maxY;
  }

  // The box as a closed CCW polygon, for boolean ops against other geometry.
  Clipper2Lib::PathD asPath() const {
    return {{minX, minY}, {maxX, minY}, {maxX, maxY}, {minX, maxY}};
  }
};

}  // namespace words
