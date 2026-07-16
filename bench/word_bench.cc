// wasm runtime microbenchmarks for the geometry core. Built only in the
// wasm-release preset and run under node via tools/bench.sh; paths are
// relative to the repo root (NODERAWFS reads the real filesystem).

#include <benchmark/benchmark.h>

#include <clipper2/clipper.h>
#include <cstddef>

#include <numbers>
#include <vector>

#include "box.h"
#include "layout.h"
#include "shape.h"
#include "word.h"

namespace {

constexpr const char* kFont = "assets/Sexsmith.otf";
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
BENCHMARK(BM_BuildHbb)->Arg(60)->Arg(230)->Arg(500)->Unit(
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
      scales.push_back((24.0 + 206.0 / (1 + shaped.size() % 7)) /
                       shaped.back().upem);
    }
  }
  words::Box world{-800, -500, 800, 500};
  for (auto _ : state) {
    state.PauseTiming();
    std::vector<words::Word> wordList;
    for (size_t i = 0; i < shaped.size(); ++i) {
      wordList.emplace_back(shaped[i], scales[i],
                            i % 4 == 3 ? std::numbers::pi / 2.0 : 0.0);
    }
    state.ResumeTiming();
    words::layoutWords(wordList, world);
    benchmark::DoNotOptimize(wordList);
  }
  state.SetItemsProcessed(state.iterations() * shaped.size());
}
BENCHMARK(BM_Layout)->Unit(benchmark::kMillisecond);

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
