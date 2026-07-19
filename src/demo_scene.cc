#include "demo_scene.h"

#include <absl/log/log.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "box.h"
#include "layout.h"
#include "orientation.h"
#include "palette.h"
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

// The original's coordinate convention: the heaviest word has a 1000-unit
// em and everything else scales proportionally with weight (size ∝ count,
// which is what lets the long tail go small enough to fill crevices),
// clamped to a legibility floor. The HBB constants (leaf floor 25, the
// log swells) and spiral constants are calibrated to this scale.
constexpr double kMaxEm = 1000.0;
constexpr double kMinEm = 60.0;
constexpr double kAspect = 1.6;

// The original's world sizing: barely more than the words' total area,
// which is where the packed look comes from — the world is grown to fit
// the words, not the words shrunk to fit a fixed world. Note the pads
// are asymmetric, so the world's true ratio is aspect * 1.2/1.5.
constexpr double kWidthPad = 1.2;
constexpr double kHeightPad = 1.5;
Box worldFor(const std::vector<Word>& wordList, double aspect) {
  double totalArea = 0, maxW = 0, maxH = 0;
  for (const Word& w : wordList) {
    const Box& b = w.localBounds();
    totalArea += b.width() * b.height();
    maxW = std::max(maxW, b.width());
    maxH = std::max(maxH, b.height());
  }
  double width = std::max(maxW, std::sqrt(aspect * totalArea)) * kWidthPad;
  double height = std::max(maxH, std::sqrt(totalArea / aspect)) * kHeightPad;
  return {-width / 2, -height / 2, width / 2, height / 2};
}

// The laid-out ink's width:height ratio, robust to far-flung strays: a
// word's box contributes half its area at each edge, and the compared
// spans hold the central 90% of total area per axis.
double inkAspect(const std::vector<Word>& laid) {
  struct Edge {
    double v, weight;
  };
  std::vector<Edge> xs, ys;
  xs.reserve(2 * laid.size());
  ys.reserve(2 * laid.size());
  double total = 0;
  for (const Word& w : laid) {
    Box b = w.worldBounds();
    double a = b.width() * b.height() / 2;
    xs.push_back({b.minX, a});
    xs.push_back({b.maxX, a});
    ys.push_back({b.minY, a});
    ys.push_back({b.maxY, a});
    total += 2 * a;
  }
  if (total <= 0) return 1.0;
  auto span = [total](std::vector<Edge>& edges) {
    std::sort(edges.begin(), edges.end(),
              [](const Edge& a, const Edge& b) { return a.v < b.v; });
    double acc = 0, lo = edges.front().v, hi = edges.back().v;
    bool loSet = false;
    for (const Edge& e : edges) {
      acc += e.weight;
      if (!loSet && acc >= 0.05 * total) {
        lo = e.v;
        loSet = true;
      }
      if (acc >= 0.95 * total) {
        hi = e.v;
        break;
      }
    }
    return hi - lo;
  };
  double sx = span(xs), sy = span(ys);
  return sy > 0 ? sx / sy : 1.0;
}

// The original renderer fit its viewport to the laid-out content, not the
// layout world — initial placements can legitimately fall outside the
// world (a big word's center-line jitter scales with its own size), and
// cropping them would be wrong. Recenter everything on the content's
// bounding box and size the scene to it, plus a little breathing room.
Scene sceneFitToContent(std::vector<Word>&& laid,
                        const std::vector<Color>& colors,
                        LayoutDebug* debug = nullptr) {
  Box content{0, 0, 0, 0};
  bool first = true;
  for (const Word& w : laid) {
    Box b = w.worldBounds();
    if (first) {
      content = b;
      first = false;
    } else {
      content.minX = std::min(content.minX, b.minX);
      content.minY = std::min(content.minY, b.minY);
      content.maxX = std::max(content.maxX, b.maxX);
      content.maxY = std::max(content.maxY, b.maxY);
    }
  }
  double cx = content.centerX();
  double cy = content.centerY();
  if (debug) {
    for (auto& p : debug->trail) {
      p.x -= cx;
      p.y -= cy;
    }
  }
  Scene scene(content.width() * 1.04, content.height() * 1.06);
  for (size_t i = 0; i < laid.size(); ++i) {
    laid[i].moveBy(-cx, -cy);
    scene.addWord(std::move(laid[i]), colors[i]);
  }
  return scene;
}

}  // namespace

Scene buildCloudScene(const std::string& fontPath) {
  std::mt19937 rng(20080623);  // deterministic demo; Wordle's birthday-ish
  std::uniform_real_distribution<double> unit(0.0, 1.0);

  double maxWeight = kVocabulary[0].weight;
  std::vector<Word> laid;
  std::vector<Color> colors;
  laid.reserve(std::size(kVocabulary));
  for (const Entry& e : kVocabulary) {
    ShapedText shaped = shapeText(fontPath, e.text);
    if (shaped.empty()) continue;
    // Scale by em size, not by bounding box: a word's importance sets its
    // type size, and ascenders/descenders mustn't shrink it.
    double em = std::max(kMinEm, kMaxEm * e.weight / maxWeight);
    double scale = em / shaped.upem;
    double angle =
        orientationAngle(Orientation::kMostlyHorizontal, e.text, rng);
    laid.emplace_back(shaped, scale, angle);
    colors.push_back(kPalette[static_cast<size_t>(unit(rng) *
                                                  std::size(kPalette))]);
  }

  Box world = worldFor(laid, kAspect);
  layoutWords(laid, world, LayoutParams{});
  return sceneFitToContent(std::move(laid), colors);
}

namespace {

// The shared tail of every text pipeline: frequency-ranked counts become
// sized, oriented, colored, laid-out words.
Scene cloudFromCounts(const std::string& fontPath,
                      std::vector<WordCount>&& counts,
                      const CloudOptions& options) {
  if (counts.empty()) return Scene();
  if (counts.size() > options.maxWords) counts.resize(options.maxWords);

  std::mt19937 rng(20080623);
  std::uniform_real_distribution<double> unit(0.0, 1.0);

  double maxCount = counts[0].count;
  std::vector<Word> laid;
  std::vector<Color> colors;
  laid.reserve(counts.size());
  for (size_t i = 0; i < counts.size(); ++i) {
    const WordCount& wc = counts[i];
    if (options.progress && i % 32 == 0) {
      options.progress("shaping", i, counts.size());
    }
    ShapedText shaped = shapeText(fontPath, wc.display);
    if (shaped.empty()) continue;
    // Type size proportional to frequency, like the original.
    double em = std::max(kMinEm, kMaxEm * wc.count / maxCount);
    double scale = em / shaped.upem;
    double angle = orientationAngle(options.orientation, wc.display, rng);
    laid.emplace_back(shaped, scale, angle);
    if (options.colors) {
      colors.push_back(varied(options.colors->palette.pick(rng),
                              options.colors->variance, rng));
    } else {
      colors.push_back(
          kPalette[static_cast<size_t>(unit(rng) * std::size(kPalette))]);
    }
  }
  if (laid.empty()) return Scene();

  auto layoutStart = std::chrono::steady_clock::now();
  // Shape-driven layouts ignore the canvas aspect. The central disc
  // needs an equal-sided world (a non-square world flattens its short
  // sides): kHeightPad/kWidthPad cancels worldFor's asymmetric pads.
  // The square layout wants square *content*, which is different:
  // center-line seeding spreads to the world's full width immediately
  // while height only grows under density pressure, so the world must
  // be narrower than square — by a factor that depends on density,
  // font, and orientation mix. Rather than model all that, measure it:
  // a sizing pass lays out in a near-square world, the ink's actual
  // width:height ratio corrects the world, and the real pass lands
  // square. Both passes are deterministic (same seed).
  double aspect = options.aspect;
  if (options.placement == Placement::kCenter) {
    aspect = kHeightPad / kWidthPad;
  } else if (options.placement == Placement::kSquare) {
    aspect = kHeightPad / kWidthPad / 1.4;
  } else if (options.placement == Placement::kVerticalCenterLine) {
    // The tall orientation of the canvas ratio: a column on desktop,
    // and on a portrait phone simply the canvas shape itself.
    aspect = std::min(options.aspect, 1.0 / options.aspect);
  }
  LayoutParams params;
  params.placement = options.placement;
  params.seed = options.seed;
  if (options.placement == Placement::kSquare) {
    LayoutParams sizing = params;  // no progress: it's a throwaway pass
    layoutWords(laid, worldFor(laid, aspect), sizing);
    // Word boxes overreport width vs rendered ink by ~5% (AABBs carry
    // whitespace, most of all for rotated words); 1.05 calibrates the
    // box measure to the pixel measure.
    aspect /= std::clamp(inkAspect(laid) / 1.05, 0.5, 2.0);
  }
  Box world = worldFor(laid, aspect);
  if (options.progress) {
    params.progress = [&options](size_t done, size_t total) {
      options.progress("layout", done, total);
    };
  }
  layoutWords(laid, world, params, options.debug);
  auto layoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - layoutStart)
                      .count();
  size_t wordCount = laid.size();
  Scene scene = sceneFitToContent(std::move(laid), colors, options.debug);
  if (options.colors) scene.setBackground(options.colors->palette.background);
  LOG(INFO) << "cloud: " << wordCount << " words laid out in " << layoutMs
            << "ms; world " << static_cast<int>(world.width()) << "x"
            << static_cast<int>(world.height()) << ", scene "
            << static_cast<int>(scene.width()) << "x"
            << static_cast<int>(scene.height());
  return scene;
}

}  // namespace

Scene buildCloudFromText(const std::string& fontPath,
                         const std::string& stopWordsDir,
                         std::string_view text, const CloudOptions& options) {
  StopWordsSet stopSets(stopWordsDir);
  const StopWords* language = stopSets.guess(text);
  std::vector<WordCount> counts =
      countWords(text, language, options.caseFold);
  LOG(INFO) << "text: " << text.size() << " bytes, language "
            << (language ? language->name() : "(none)") << ", "
            << counts.size() << " distinct words after stop-word removal";
  return cloudFromCounts(fontPath, std::move(counts), options);
}

Scene buildCloudFromCountsTsv(const std::string& fontPath,
                              const std::string& stopWordsDir,
                              std::string_view tsv,
                              const CloudOptions& options) {
  StopWordsSet stopSets(stopWordsDir);
  const StopWords* language = nullptr;
  // Corpus files carry "# case: as-written" and one row per exact
  // spelling, so every fold can be applied here. A legacy case-merged
  // file (no header) still loads: kLower / kUpper transform its stored
  // display forms, and kAsWritten shows the stored majority casing —
  // the per-spelling counts are unrecoverable.
  bool asWrittenRows = false;
  std::vector<WordCount> counts;
  size_t pos = 0;
  while (pos < tsv.size()) {
    size_t eol = tsv.find('\n', pos);
    std::string_view line = tsv.substr(
        pos, eol == std::string_view::npos ? std::string_view::npos
                                           : eol - pos);
    pos = eol == std::string_view::npos ? tsv.size() : eol + 1;
    if (line.empty()) continue;
    if (line.front() == '#') {
      constexpr std::string_view kGuess = "# language-guess: ";
      if (line.starts_with(kGuess)) {
        language = stopSets.find(line.substr(kGuess.size()));
      }
      if (line == "# case: as-written") asWrittenRows = true;
      continue;
    }
    size_t tab = line.find('\t');
    if (tab == std::string_view::npos) continue;
    std::string_view word = line.substr(0, tab);
    int count = 0;
    std::from_chars(line.data() + tab + 1, line.data() + line.size(),
                    count);
    if (count <= 0) continue;
    // The corpus keeps stop words; the cloud drops them, like the live
    // pipeline does after its language guess.
    if (language && language->isStopWord(word)) continue;
    counts.push_back({std::string(word), count});
  }
  if (asWrittenRows && options.caseFold != CaseFold::kAsWritten) {
    // Case-merge at load, reproducing countWords' choices: rows arrive
    // count-descending with first-appearance ties, so the first row per
    // folded key is the majority casing (countWords keeps the earliest
    // of the max-count forms).
    std::vector<WordCount> merged;
    std::unordered_map<std::string, size_t> byKey;  // fold key -> merged idx
    merged.reserve(counts.size());
    for (WordCount& wc : counts) {
      auto [it, fresh] =
          byKey.try_emplace(foldForMatch(wc.display), merged.size());
      if (fresh) {
        merged.push_back(std::move(wc));
      } else {
        merged[it->second].count += wc.count;
      }
    }
    // Summing can reorder; the layout sizes from counts[0] and truncates
    // by position, so restore count-descending order (stable: ties keep
    // majority-row order).
    std::stable_sort(merged.begin(), merged.end(),
                     [](const WordCount& a, const WordCount& b) {
                       return a.count > b.count;
                     });
    counts = std::move(merged);
  }
  if (options.caseFold == CaseFold::kLower ||
      options.caseFold == CaseFold::kUpper) {
    for (WordCount& wc : counts) {
      wc.display = foldDisplay(wc.display, options.caseFold);
    }
  }
  return cloudFromCounts(fontPath, std::move(counts), options);
}

}  // namespace words
