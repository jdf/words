#include "demo_scene.h"

#include <string_view>

#include <cstdint>
#include <cstddef>
#include <numbers>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "box.h"
#include "layout.h"
#include "scene.h"
#include "text.h"
#include "word.h"

namespace words {

namespace {

// Weighted vocabulary, most important first (layout order).
struct Entry {
  const char* text;
  double weight;
};

constexpr Entry kVocabulary[] = {
    {"words", 100}, {"cloud", 62},   {"glyph", 55},   {"type", 50},
    {"kern", 46},   {"outline", 42}, {"spiral", 40},  {"font", 38},
    {"vector", 34}, {"curve", 32},   {"serif", 30},   {"ink", 28},
    {"shape", 27},  {"box", 26},     {"tree", 25},    {"bezier", 24},
    {"raster", 22}, {"stencil", 21}, {"union", 20},   {"scene", 19},
    {"weight", 18}, {"layout", 17},  {"swell", 16},   {"split", 15},
    {"prune", 14},  {"leaf", 14},    {"node", 13},    {"place", 13},
    {"rotate", 12}, {"scale", 12},   {"golden", 11},  {"wasm", 11},
    {"webgl", 10},  {"clip", 10},    {"quad", 9},     {"hull", 9},
    {"pack", 8},    {"turn", 8},     {"fit", 8},      {"jot", 8},
};

constexpr Color kPalette[] = {
    {0.93f, 0.93f, 0.90f},  // off-white
    {0.36f, 0.72f, 0.86f},  // cyan
    {0.96f, 0.65f, 0.14f},  // orange
    {0.55f, 0.83f, 0.30f},  // green
    {0.80f, 0.45f, 0.78f},  // plum
    {0.85f, 0.33f, 0.31f},  // brick
};

// Em height of the heaviest word, scene px; other words scale linearly
// with weight, floored so the least words stay legible.
constexpr double kMaxEmPx = 230.0;
constexpr double kMinEmPx = 24.0;
constexpr double kVerticalFraction = 0.25;

}  // namespace

Scene buildCloudScene(const std::string& fontPath) {
  Scene scene;

  std::mt19937 rng(20080623);  // deterministic demo; Wordle's birthday-ish
  std::uniform_real_distribution<double> unit(0.0, 1.0);

  double maxWeight = kVocabulary[0].weight;
  std::vector<Word> laid;
  std::vector<Color> colors;
  laid.reserve(std::size(kVocabulary));
  for (const Entry& e : kVocabulary) {
    ShapedText shaped = shapeText(fontPath, e.text);
    if (shaped.empty()) continue;
    double emPx =
        kMinEmPx + (kMaxEmPx - kMinEmPx) * (e.weight / maxWeight);
    // Scale by em size, not by bounding box: a word's importance sets its
    // type size, and ascenders/descenders mustn't shrink it.
    double scale = emPx / shaped.upem;
    double angle =
        unit(rng) < kVerticalFraction ? std::numbers::pi / 2.0 : 0.0;
    laid.emplace_back(shaped, scale, angle);
    colors.push_back(kPalette[static_cast<size_t>(unit(rng) *
                                                  std::size(kPalette))]);
  }

  Box world{-Scene::kWidth / 2.0, -Scene::kHeight / 2.0, Scene::kWidth / 2.0,
            Scene::kHeight / 2.0};
  layoutWords(laid, world, LayoutParams{});

  for (size_t i = 0; i < laid.size(); ++i) {
    scene.addWord(std::move(laid[i]), colors[i]);
  }
  return scene;
}

Scene buildCloudFromText(const std::string& fontPath,
                         const std::string& stopWordsDir,
                         std::string_view text, size_t maxWords) {
  Scene scene;

  StopWordsSet stopSets(stopWordsDir);
  const StopWords* language = stopSets.guess(text);
  std::vector<WordCount> counts = countWords(text, language);
  if (counts.empty()) return scene;
  if (counts.size() > maxWords) counts.resize(maxWords);

  std::mt19937 rng(20080623);
  std::uniform_real_distribution<double> unit(0.0, 1.0);

  double maxCount = counts[0].count;
  std::vector<Word> laid;
  std::vector<Color> colors;
  laid.reserve(counts.size());
  for (const WordCount& wc : counts) {
    ShapedText shaped = shapeText(fontPath, wc.display);
    if (shaped.empty()) continue;
    // Type size linear in frequency, like the original.
    double emPx = kMinEmPx + (kMaxEmPx - kMinEmPx) * (wc.count / maxCount);
    double scale = emPx / shaped.upem;
    double angle =
        unit(rng) < kVerticalFraction ? std::numbers::pi / 2.0 : 0.0;
    laid.emplace_back(shaped, scale, angle);
    colors.push_back(
        kPalette[static_cast<size_t>(unit(rng) * std::size(kPalette))]);
  }

  Box world{-Scene::kWidth / 2.0, -Scene::kHeight / 2.0, Scene::kWidth / 2.0,
            Scene::kHeight / 2.0};
  layoutWords(laid, world, LayoutParams{});

  for (size_t i = 0; i < laid.size(); ++i) {
    scene.addWord(std::move(laid[i]), colors[i]);
  }
  return scene;
}

}  // namespace words
