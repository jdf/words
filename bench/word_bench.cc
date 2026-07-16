// wasm runtime microbenchmarks for the geometry core. Built only in the
// wasm-release preset and run under node via tools/bench.sh; paths are
// relative to the repo root (NODERAWFS reads the real filesystem).

#include <benchmark/benchmark.h>

#include <clipper2/clipper.h>
#include <cstddef>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "box.h"
#include "demo_scene.h"
#include "layout.h"
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
