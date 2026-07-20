// wasm runtime microbenchmarks for the geometry core. Built only in the
// wasm-release preset and run under node via tools/bench.sh; paths are
// relative to the repo root (NODERAWFS reads the real filesystem).

#include <benchmark/benchmark.h>

#include <clipper2/clipper.h>
#include <cstddef>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_map>
#include <fstream>
#include <numbers>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "box.h"
#include "demo_scene.h"
#include "layout.h"
#include "quad_tree.h"
#include "scene.h"
#include "shape.h"
#include "text.h"
#include "word.h"

namespace {

constexpr const char* kFont = "assets/fonts/sexsmith.ttf";
constexpr const char* kText = "words";

// Font load + HarfBuzz shaping + outline flattening + Clipper2 union:
// the one-time cost of turning a string into geometry.
void BM_ShapeText(benchmark::State& state) {
  for (auto _ : state) {
    words::ShapedText shaped = words::shapeText(kFont, kText);
    benchmark::DoNotOptimize(shaped);
  }
}
BENCHMARK(BM_ShapeText)->Unit(benchmark::kMicrosecond);

// Word construction: baking scale+rotation into word-local geometry and
// computing the root AABB. This is the seed of HBB-construction cost.
void BM_BuildWord(benchmark::State& state) {
  words::ShapedText shaped = words::shapeText(kFont, kText);
  double angleRad = state.range(0) * std::numbers::pi / 180.0;
  for (auto _ : state) {
    words::Word word(shaped, 0.5, angleRad);
    benchmark::DoNotOptimize(word);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_BuildWord)->Arg(0)->Arg(45)->Unit(benchmark::kMicrosecond);

// HBB construction — the expensive phase of layout, and the number the
// whole hierarchical design exists to amortize. Range arg = em size in
// scene px (leaf resolution is absolute, so bigger words build deeper).
void BM_BuildHbb(benchmark::State& state) {
  words::ShapedText shaped = words::shapeText(kFont, kText);
  double emPx = static_cast<double>(state.range(0));
  words::Word word(shaped, emPx / shaped.upem,
                   30.0 * std::numbers::pi / 180.0);
  for (auto _ : state) {
    word.buildHbb();
    benchmark::DoNotOptimize(word);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_BuildHbb)->Arg(100)->Arg(400)->Arg(1000)->Unit(
    benchmark::kMicrosecond);

// HBB-vs-HBB queries, near-miss (adjacent) and clear-miss (far) cases.
void BM_HbbIntersects(benchmark::State& state) {
  words::ShapedText shaped = words::shapeText(kFont, kText);
  words::Word a(shaped, 0.23, 0.0);
  words::Word b(shaped, 0.1, std::numbers::pi / 2.0);
  a.buildHbb();
  b.buildHbb();
  a.moveTo(0, 0);
  b.moveTo(state.range(0), 0);
  for (auto _ : state) {
    bool hit = a.intersectsWord(b);
    benchmark::DoNotOptimize(hit);
  }
}
BENCHMARK(BM_HbbIntersects)->Arg(150)->Arg(2000);

// Full cloud layout (40 words, shaping excluded): HBB builds + spiral
// search + quadtree queries, end to end.
void BM_Layout(benchmark::State& state) {
  std::vector<words::ShapedText> shaped;
  std::vector<double> scales;
  const char* vocab[] = {"words", "cloud",  "glyph", "type",  "kern",
                         "outline", "spiral", "font",  "vector", "curve"};
  for (int rep = 0; rep < 4; ++rep) {
    for (const char* text : vocab) {
      shaped.push_back(words::shapeText(kFont, text));
      // Em sizes spanning the real convention (heaviest word em = 1000).
      scales.push_back((100.0 + 900.0 / (1 + shaped.size() % 7)) /
                       shaped.back().upem);
    }
  }
  for (auto _ : state) {
    state.PauseTiming();
    std::vector<words::Word> wordList;
    double totalArea = 0;
    for (size_t i = 0; i < shaped.size(); ++i) {
      wordList.emplace_back(shaped[i], scales[i],
                            i % 4 == 3 ? std::numbers::pi / 2.0 : 0.0);
      const words::Box& b = wordList.back().localBounds();
      totalArea += b.width() * b.height();
    }
    // World sized from content, like the real pipeline.
    double w = std::sqrt(1.6 * totalArea) * 1.2;
    double h = std::sqrt(totalArea / 1.6) * 1.5;
    words::Box world{-w / 2, -h / 2, w / 2, h / 2};
    state.ResumeTiming();
    words::layoutWords(wordList, world);
    benchmark::DoNotOptimize(wordList);
  }
  state.SetItemsProcessed(state.iterations() * shaped.size());
}
BENCHMARK(BM_Layout)->Unit(benchmark::kMillisecond);

// Layout of a real text's cloud at increasing density (shaping excluded):
// the Moby-Dick sample, sized and oriented exactly as the app does it.
// Range arg = word cap. This is where superlinear growth shows up — later
// words spiral through ever-denser occupancy before finding a home.
void BM_LayoutText(benchmark::State& state) {
  std::ifstream in("assets/sample-text.txt");
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string text = ss.str();
  words::StopWordsSet stopSets("assets/stopwords");
  std::vector<words::WordCount> counts =
      words::countWords(text, stopSets.guess(text));
  size_t cap = std::min(counts.size(), static_cast<size_t>(state.range(0)));
  counts.resize(cap);

  std::mt19937 rng(20080623);
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  std::vector<words::ShapedText> shaped;
  std::vector<double> scales;
  std::vector<double> angles;
  double maxCount = counts[0].count;
  for (const words::WordCount& wc : counts) {
    shaped.push_back(words::shapeText(kFont, wc.display));
    double em = std::max(60.0, 1000.0 * wc.count / maxCount);
    scales.push_back(em / shaped.back().upem);
    angles.push_back(unit(rng) < 0.25 ? std::numbers::pi / 2.0 : 0.0);
  }

  for (auto _ : state) {
    state.PauseTiming();
    std::vector<words::Word> wordList;
    double totalArea = 0;
    for (size_t i = 0; i < shaped.size(); ++i) {
      wordList.emplace_back(shaped[i], scales[i], angles[i]);
      const words::Box& b = wordList.back().localBounds();
      totalArea += b.width() * b.height();
    }
    double w = std::sqrt(1.6 * totalArea) * 1.2;
    double h = std::sqrt(totalArea / 1.6) * 1.5;
    words::Box world{-w / 2, -h / 2, w / 2, h / 2};
    state.ResumeTiming();
    words::layoutWords(wordList, world);
    benchmark::DoNotOptimize(wordList);
  }
  state.SetItemsProcessed(state.iterations() * shaped.size());
}
BENCHMARK(BM_LayoutText)->Arg(150)->Arg(400)->Arg(800)->Unit(
    benchmark::kMillisecond);

// The whole app startup cost after the GL context: stop-list loading,
// language guessing, counting, shaping every word, HBBs, and layout.
void BM_CloudFromText(benchmark::State& state) {
  std::ifstream in("assets/sample-text.txt");
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string text = ss.str();
  for (auto _ : state) {
    words::Scene scene = words::buildCloudFromText(
        kFont, "assets/stopwords", text,
        {.maxWords = static_cast<size_t>(state.range(0))});
    benchmark::DoNotOptimize(scene);
  }
}
BENCHMARK(BM_CloudFromText)->Arg(800)->Unit(benchmark::kMillisecond);

// Same pipeline minus counting, fed by the full Moby-Dick vocabulary
// (18k+ distinct words) — the scaling data behind the word-count
// slider's ceiling: where does a rebuild stop feeling interactive?
void BM_CloudFromCounts(benchmark::State& state) {
  std::ifstream in("tests/corpus/moby-dick.tsv");
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string tsv = ss.str();
  for (auto _ : state) {
    words::Scene scene = words::buildCloudFromCountsTsv(
        kFont, "assets/stopwords", tsv,
        {.maxWords = static_cast<size_t>(state.range(0))});
    benchmark::DoNotOptimize(scene);
  }
}
BENCHMARK(BM_CloudFromCounts)
    ->Arg(400)
    ->Arg(800)
    ->Arg(1600)
    ->Arg(2400)
    ->Arg(3200)
    ->Arg(4000)
    ->Unit(benchmark::kMillisecond);

// ---------------------------------------------------------------------------
// Differential phase benchmarks: the same Moby-Dick vocabulary carved
// into the pipeline's three phases (shaping, HBB construction, spiral +
// index search), each parameterized by font and density so the slow
// axis identifies itself. Fridge (AlphaFridgeMagnetsAllCap) is the
// heaviest outline set in the collection; Sexsmith the baseline.

struct PreparedVocab {
  std::vector<words::ShapedText> shaped;
  std::vector<double> scales;
  std::vector<double> angles;
};

// The full Moby-Dick vocabulary (the bundled sample text runs dry at
// ~740 distinct words — not enough for the 2000-word regime). The TSV
// stores as-written rows; merge them the way the engine does (folded
// key, first row wins the display) and drop English stop words.
const std::vector<words::WordCount>& mobyCounts() {
  static const std::vector<words::WordCount> counts = [] {
    std::ifstream in("tests/corpus/moby-dick.tsv");
    words::StopWordsSet stopSets("assets/stopwords");
    const words::StopWords* english = stopSets.find("english");
    std::vector<words::WordCount> merged;
    std::unordered_map<std::string, size_t> byKey;
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty() || line[0] == '#') continue;
      size_t tab = line.find('\t');
      if (tab == std::string::npos) continue;
      std::string word = line.substr(0, tab);
      int count = std::atoi(line.c_str() + tab + 1);
      if (count <= 0) continue;
      if (english && english->isStopWord(word)) continue;
      auto [it, fresh] = byKey.try_emplace(words::foldForMatch(word),
                                           merged.size());
      if (fresh) {
        merged.push_back({word, count});
      } else {
        merged[it->second].count += count;
      }
    }
    std::stable_sort(merged.begin(), merged.end(),
                     [](const words::WordCount& a,
                        const words::WordCount& b) {
                       return a.count > b.count;
                     });
    return merged;
  }();
  return counts;
}

PreparedVocab prepareVocab(const char* font, size_t cap) {
  const std::vector<words::WordCount>& all = mobyCounts();
  std::vector<words::WordCount> counts(
      all.begin(),
      all.begin() + static_cast<long>(std::min(cap, all.size())));
  PreparedVocab v;
  std::mt19937 rng(20080623);
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  double maxCount = counts[0].count;
  for (const words::WordCount& wc : counts) {
    v.shaped.push_back(words::shapeText(font, wc.display));
    double em = std::max(60.0, 1000.0 * wc.count / maxCount);
    v.scales.push_back(em / v.shaped.back().upem);
    v.angles.push_back(unit(rng) < 0.25 ? std::numbers::pi / 2.0 : 0.0);
  }
  return v;
}

std::vector<words::Word> buildWords(const PreparedVocab& v) {
  std::vector<words::Word> wordList;
  wordList.reserve(v.shaped.size());
  for (size_t i = 0; i < v.shaped.size(); ++i) {
    wordList.emplace_back(v.shaped[i], v.scales[i], v.angles[i]);
  }
  return wordList;
}

words::Box worldOf(const std::vector<words::Word>& wordList, double aspect,
                   double pad) {
  double totalArea = 0;
  for (const words::Word& w : wordList) {
    const words::Box& b = w.localBounds();
    totalArea += b.width() * b.height();
  }
  double w = std::sqrt(aspect * totalArea) * 1.2 * pad;
  double h = std::sqrt(totalArea / aspect) * 1.5 * pad;
  return {-w / 2, -h / 2, w / 2, h / 2};
}

constexpr const char* kFridge = "assets/fonts/fridge.ttf";
const char* fontArg(int64_t a) { return a == 0 ? kFont : kFridge; }
const char* fontName(int64_t a) { return a == 0 ? "sexsmith" : "fridge"; }

// Phase (a): shaping — FreeType outlines + HarfBuzz + Clipper2 union,
// per distinct word. Args: font, word cap.
void BM_PhaseShape(benchmark::State& state) {
  const std::vector<words::WordCount>& all = mobyCounts();
  size_t cap = std::min(all.size(), static_cast<size_t>(state.range(1)));
  for (auto _ : state) {
    for (size_t i = 0; i < cap; ++i) {
      words::ShapedText shaped =
          words::shapeText(fontArg(state.range(0)), all[i].display);
      benchmark::DoNotOptimize(shaped);
    }
  }
  state.SetLabel(fontName(state.range(0)));
  state.SetItemsProcessed(state.iterations() * cap);
}
BENCHMARK(BM_PhaseShape)
    ->Args({0, 800})
    ->Args({1, 800})
    ->Args({0, 2000})
    ->Args({1, 2000})
    ->Unit(benchmark::kMillisecond);

// Phase (b): HBB construction over the whole vocabulary (word
// construction excluded via pause).
void BM_PhaseHbb(benchmark::State& state) {
  PreparedVocab v =
      prepareVocab(fontArg(state.range(0)),
                   static_cast<size_t>(state.range(1)));
  for (auto _ : state) {
    state.PauseTiming();
    std::vector<words::Word> wordList = buildWords(v);
    state.ResumeTiming();
    for (words::Word& w : wordList) {
      w.buildHbb({});
    }
    benchmark::DoNotOptimize(wordList);
  }
  state.SetLabel(fontName(state.range(0)));
  state.SetItemsProcessed(state.iterations() * v.shaped.size());
}
BENCHMARK(BM_PhaseHbb)
    ->Args({0, 800})
    ->Args({1, 800})
    ->Args({0, 2000})
    ->Args({1, 2000})
    ->Unit(benchmark::kMillisecond);

// Phases (b)+(c) together: layoutWords (HBB build + seeding + spiral +
// quadtree queries). Subtract BM_PhaseHbb for the pure search cost.
// Args: font, word cap.
void BM_PhaseLayout(benchmark::State& state) {
  PreparedVocab v =
      prepareVocab(fontArg(state.range(0)),
                   static_cast<size_t>(state.range(1)));
  for (auto _ : state) {
    state.PauseTiming();
    std::vector<words::Word> wordList = buildWords(v);
    words::Box world = worldOf(wordList, 1.6, 1.0);
    state.ResumeTiming();
    words::layoutWords(wordList, world);
    benchmark::DoNotOptimize(wordList);
  }
  state.SetLabel(fontName(state.range(0)));
  state.SetItemsProcessed(state.iterations() * v.shaped.size());
}
BENCHMARK(BM_PhaseLayout)
    ->Args({0, 800})
    ->Args({1, 800})
    ->Args({0, 2000})
    ->Args({1, 2000})
    ->Unit(benchmark::kMillisecond);

// Placement differential: the center disc packs every seed at the world
// center (straddling every quadtree split line), center-line spreads
// seeds across the width. Args: placement (0 = center-line, 1 =
// center), word cap. Sexsmith only — the font axis is covered above.
void BM_PhasePlacement(benchmark::State& state) {
  PreparedVocab v = prepareVocab(kFont, static_cast<size_t>(state.range(1)));
  bool center = state.range(0) == 1;
  for (auto _ : state) {
    state.PauseTiming();
    std::vector<words::Word> wordList = buildWords(v);
    // Mirror the app: center gets the square world with the 4/pi pad.
    words::Box world = center
                           ? worldOf(wordList, 1.5 / 1.2,
                                     4.0 / std::numbers::pi)
                           : worldOf(wordList, 1.6, 1.0);
    words::LayoutParams params;
    params.placement = center ? words::Placement::kCenter
                              : words::Placement::kCenterLine;
    state.ResumeTiming();
    words::layoutWords(wordList, world, params);
    benchmark::DoNotOptimize(wordList);
  }
  state.SetLabel(state.range(0) == 1 ? "center" : "center-line");
  state.SetItemsProcessed(state.iterations() * v.shaped.size());
}
BENCHMARK(BM_PhasePlacement)
    ->Args({0, 800})
    ->Args({1, 800})
    ->Args({0, 2000})
    ->Args({1, 2000})
    ->Unit(benchmark::kMillisecond);

// The quadtree in isolation: a committed 2000-word center cloud, probed
// along a fresh archimedean spiral from the center — the exact query
// stream the placement loop generates. ns/probe is the number an index
// swap would have to beat.
void BM_QuadTreeProbe(benchmark::State& state) {
  static PreparedVocab v = prepareVocab(kFont, 2000);
  static std::vector<words::Word> committed = [] {
    std::vector<words::Word> wordList = buildWords(v);
    words::Box world =
        worldOf(wordList, 1.5 / 1.2, 4.0 / std::numbers::pi);
    words::LayoutParams params;
    params.placement = words::Placement::kCenter;
    words::layoutWords(wordList, world, params);
    return wordList;
  }();
  static words::Box world =
      worldOf(committed, 1.5 / 1.2, 4.0 / std::numbers::pi);
  words::QuadTree index(world);
  for (const words::Word& w : committed) index.add(&w);
  words::Word probe(v.shaped[120], v.scales[120], 0.0);
  probe.buildHbb({});
  size_t probes = 0;
  for (auto _ : state) {
    double theta = 0.0, radius = 1.0;
    for (int i = 0; i < 1000; ++i) {
      probe.moveTo(radius * std::cos(theta), radius * std::sin(theta));
      const words::Word* hit = index.firstIntersecting(probe);
      benchmark::DoNotOptimize(hit);
      theta += 0.04;
      radius += 0.7;
    }
    probes += 1000;
  }
  state.SetItemsProcessed(static_cast<int64_t>(probes));
}
BENCHMARK(BM_QuadTreeProbe)->Unit(benchmark::kMillisecond);

// The per-frame collision query: triangle polygon vs a word's root box.
void BM_BoxIntersects(benchmark::State& state) {
  words::ShapedText shaped = words::shapeText(kFont, kText);
  words::Word word(shaped, 0.5, 0.4);
  word.moveTo(100, 50);
  words::Shape triangle = words::Shape::equilateralTriangle(310.0);
  triangle.setAngle(1.2);
  Clipper2Lib::PathsD triPath = triangle.worldPath();
  for (auto _ : state) {
    bool hit = word.boxIntersects(triPath);
    benchmark::DoNotOptimize(hit);
  }
}
BENCHMARK(BM_BoxIntersects);

}  // namespace

BENCHMARK_MAIN();
