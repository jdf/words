#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "layout.h"
#include "orientation.h"
#include "palette.h"
#include "scene.h"

namespace words {

// How to color a cloud: a palette (word colors + background) and the
// original's per-word color variance. When none is given, the clouds use
// the app's built-in dark-scheme colors.
struct ColorScheme {
  Palette palette;
  double variance = kDefaultVariance;
};

// Knobs for the text->cloud pipeline. The defaults are the app's stock
// look: up to 800 words, one word in four vertical, center-line seeding,
// dark-scheme colors.
struct CloudOptions {
  size_t maxWords = 800;
  Orientation orientation = Orientation::kMostlyHorizontal;
  Placement placement = Placement::kCenterLine;
  uint32_t seed = 1;  // layout randomness (seeding positions, spirals)
  const ColorScheme* colors = nullptr;  // null = built-in dark scheme
  LayoutDebug* debug = nullptr;
};

// Builds the demo world: a word cloud of project-flavored vocabulary,
// weighted, colored from a small palette, mixed horizontal/vertical, laid
// out by the spiral+quadtree engine — plus the rotating-triangle prop that
// exercises the collision machinery. Deterministic for a fixed font.
Scene buildCloudScene(const std::string& fontPath);

// The real pipeline: analyzes unstructured text (tokenize, guess the
// language from the stop lists in `stopWordsDir`, drop its stop words,
// count), sizes the most frequent words linearly by count, and lays them
// out. Deterministic for fixed inputs.
Scene buildCloudFromText(const std::string& fontPath,
                         const std::string& stopWordsDir,
                         std::string_view text,
                         const CloudOptions& options = {});

// The same pipeline entered with precomputed counts: `tsv` is a
// tests/corpus file (word<TAB>count lines, most frequent first, stop
// words retained, `# language-guess:` header naming the stop list to
// apply). Deterministic for fixed inputs.
Scene buildCloudFromCountsTsv(const std::string& fontPath,
                              const std::string& stopWordsDir,
                              std::string_view tsv,
                              const CloudOptions& options = {});

}  // namespace words
