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
  static constexpr double kWidth = 1600.0;
  static constexpr double kHeight = 1000.0;

  struct Entry {
    Word word;
    Color color;
  };

  void addWord(Word word, Color color) {
    entries_.push_back({std::move(word), color});
  }
  const std::vector<Entry>& entries() const { return entries_; }
  std::vector<Entry>& entries() { return entries_; }

 private:
  std::vector<Entry> entries_;
};

}  // namespace words
