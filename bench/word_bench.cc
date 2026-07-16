// wasm runtime microbenchmarks for the geometry core. Built only in the
// wasm-release preset and run under node via tools/bench.sh; paths are
// relative to the repo root (NODERAWFS reads the real filesystem).

#include <benchmark/benchmark.h>

#include <clipper2/clipper.h>

#include <numbers>

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
