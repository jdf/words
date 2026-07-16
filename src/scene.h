#pragma once

#include <utility>
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
class Scene {
 public:
  Scene() = default;
  Scene(double width, double height) : width_(width), height_(height) {}

  double width() const { return width_; }
  double height() const { return height_; }

  struct Entry {
    Word word;
    Color color;
  };

  void addWord(Word word, Color color) {
    entries_.push_back({std::move(word), color});
  }
  const std::vector<Entry>& entries() const { return entries_; }
  std::vector<Entry>& entries() { return entries_; }

  // What renderers clear to. The default is the app's original dark gray
  // (#17171c in 8-bit terms); palettes carry their own backgrounds.
  const Color& background() const { return background_; }
  void setBackground(const Color& c) { background_ = c; }

 private:
  double width_ = 1600.0;
  double height_ = 1000.0;
  Color background_{0.09f, 0.09f, 0.11f};
  std::vector<Entry> entries_;
};

}  // namespace words
