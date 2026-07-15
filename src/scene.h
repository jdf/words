#pragma once

#include <utility>
#include <vector>

#include "shape.h"
#include "word.h"

namespace words {

struct Color {
  float r = 1, g = 1, b = 1;
};

// The world: a fixed-aspect logical canvas holding placed words and a shape
// prop. The scene is defined in its own coordinate space ("scene pixels",
// origin at the center, y-up); renderers map it uniformly into whatever
// viewport they have, so window shape never changes spatial relationships.
class Scene {
 public:
  static constexpr double kWidth = 1600.0;
  static constexpr double kHeight = 1000.0;

  struct Entry {
    Word word;
    Color color;
    bool hit = false;  // shape currently intersects the word's root box
  };

  explicit Scene(Shape shape) : shape_(std::move(shape)) {}

  void addWord(Word word, Color color) {
    entries_.push_back({std::move(word), color, false});
  }
  const std::vector<Entry>& entries() const { return entries_; }
  std::vector<Entry>& entries() { return entries_; }

  const Shape& shape() const { return shape_; }
  Shape& shape() { return shape_; }

  // Advances the world to time `t` (seconds): spins the shape and
  // recomputes each word's hit flag with Clipper2 in scene space.
  void update(double t);

 private:
  std::vector<Entry> entries_;
  Shape shape_;
};

}  // namespace words
