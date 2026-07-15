#pragma once

#include <vector>

#include "word.h"

namespace words {

struct Color {
  float r = 1, g = 1, b = 1;
};

// The world: a fixed-aspect logical canvas holding placed words. The scene
// is defined in its own coordinate space ("scene pixels", origin at the
// center, y-up); renderers map it uniformly into whatever viewport they
// have, so window shape never changes spatial relationships.
//
// Also holds the demo prop: an equilateral triangle rotating about the
// scene center, used to exercise the collision machinery.
class Scene {
 public:
  static constexpr double kWidth = 1600.0;
  static constexpr double kHeight = 1000.0;

  struct Entry {
    Word word;
    Color color;
    bool hit = false;  // triangle currently intersects the word's root box
  };

  void addWord(Word word, Color color) {
    entries_.push_back({std::move(word), color, false});
  }
  const std::vector<Entry>& entries() const { return entries_; }
  std::vector<Entry>& entries() { return entries_; }

  // Demo triangle, an equilateral polygon centered on the scene.
  static constexpr double kTriangleR = 310.0;
  double triangleAngle() const { return triangleAngle_; }
  // Unrotated vertices (scene px), the polygon the GPU also draws.
  static const Clipper2Lib::PathD& triangleBase();
  // The triangle at its current rotation, in world coordinates.
  Clipper2Lib::PathsD trianglePath() const;

  // Advances the world to time `t` (seconds): rotates the triangle and
  // recomputes each word's hit flag with Clipper2 in scene space.
  void update(double t);

 private:
  std::vector<Entry> entries_;
  double triangleAngle_ = 0;
};

}  // namespace words
