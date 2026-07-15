#include "demo_scene.h"

#include <numbers>
#include <string>
#include <utility>

#include "scene.h"
#include "shape.h"
#include "word.h"

namespace words {

namespace {

constexpr const char* kText = "words";
constexpr double kTriangleRadius = 310.0;

// One rendering of the text: width as a fraction of scene width, center
// position in scene fractions [-1,1], rotation, and a fill color.
struct Placement {
  float widthFrac;
  float x, y;
  float angleDeg;
  Color color;
};

constexpr Placement kPlacements[] = {
    {0.52f, -0.08f, 0.52f, 0.0f, {0.93f, 0.93f, 0.90f}},   // big headline
    {0.30f, 0.42f, -0.35f, 90.0f, {0.36f, 0.72f, 0.86f}},  // vertical, cyan
    {0.34f, -0.42f, -0.28f, 24.0f, {0.96f, 0.65f, 0.14f}}, // tilted, orange
    {0.18f, -0.10f, -0.68f, -12.0f, {0.55f, 0.83f, 0.30f}},// small, green
    {0.12f, 0.30f, 0.02f, -90.0f, {0.80f, 0.45f, 0.78f}},  // tiny, plum
    {0.10f, -0.72f, 0.10f, 45.0f, {0.85f, 0.33f, 0.31f}},  // tiny, brick
};

}  // namespace

Scene buildDemoScene(const std::string& fontPath) {
  Scene scene(Shape::equilateralTriangle(kTriangleRadius));
  ShapedText shaped = shapeText(fontPath, kText);
  if (shaped.empty()) return scene;
  for (const Placement& p : kPlacements) {
    double scale = p.widthFrac * Scene::kWidth / shaped.bounds.width();
    Word word(shaped, scale, p.angleDeg * std::numbers::pi / 180.0);
    word.moveTo(p.x * Scene::kWidth / 2.0, p.y * Scene::kHeight / 2.0);
    scene.addWord(std::move(word), p.color);
  }
  return scene;
}

}  // namespace words
