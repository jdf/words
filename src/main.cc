// words — application entry point: WebGL2 context, the demo scene (six
// renderings of one shaped word plus a rotating triangle exercising the
// collision machinery), and the main loop.

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <GLES3/gl3.h>

#include <cstdio>
#include <numbers>
#include <utility>
#include <vector>

#include "scene.h"
#include "shape.h"
#include "shape_renderer.h"
#include "word.h"
#include "word_renderer.h"

namespace {

constexpr const char* kFontPath = "/fonts/Sexsmith.otf";
constexpr const char* kText = "words";
constexpr double kTriangleRadius = 310.0;

struct App {
  words::Scene scene{words::Shape::equilateralTriangle(kTriangleRadius)};
  words::WordRenderer wordRenderer;
  words::ShapeRenderer shapeRenderer;
  double startTime = 0;
};

App* g_app = nullptr;

// One rendering of the text: width as a fraction of scene width, center
// position in scene fractions [-1,1], rotation, and a fill color.
struct Placement {
  float widthFrac;
  float x, y;
  float angleDeg;
  words::Color color;
};

constexpr Placement kPlacements[] = {
    {0.52f, -0.08f, 0.52f, 0.0f, {0.93f, 0.93f, 0.90f}},   // big headline
    {0.30f, 0.42f, -0.35f, 90.0f, {0.36f, 0.72f, 0.86f}},  // vertical, cyan
    {0.34f, -0.42f, -0.28f, 24.0f, {0.96f, 0.65f, 0.14f}}, // tilted, orange
    {0.18f, -0.10f, -0.68f, -12.0f, {0.55f, 0.83f, 0.30f}},// small, green
    {0.12f, 0.30f, 0.02f, -90.0f, {0.80f, 0.45f, 0.78f}},  // tiny, plum
    {0.10f, -0.72f, 0.10f, 45.0f, {0.85f, 0.33f, 0.31f}},  // tiny, brick
};

void buildScene(words::Scene& scene) {
  words::ShapedText shaped = words::shapeText(kFontPath, kText);
  if (shaped.empty()) return;
  for (const Placement& p : kPlacements) {
    double scale = p.widthFrac * words::Scene::kWidth / shaped.bounds.width();
    words::Word word(shaped, scale, p.angleDeg * std::numbers::pi / 180.0);
    word.moveTo(p.x * words::Scene::kWidth / 2.0,
                p.y * words::Scene::kHeight / 2.0);
    scene.addWord(std::move(word), p.color);
  }
}

// Keeps the drawing buffer matched to the canvas CSS size × devicePixelRatio.
void syncCanvasSize(int* width, int* height) {
  double cssWidth = 0, cssHeight = 0;
  emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);
  double dpr = emscripten_get_device_pixel_ratio();
  int w = static_cast<int>(cssWidth * dpr);
  int h = static_cast<int>(cssHeight * dpr);
  int currentW = 0, currentH = 0;
  emscripten_get_canvas_element_size("#canvas", &currentW, &currentH);
  if (w != currentW || h != currentH) {
    emscripten_set_canvas_element_size("#canvas", w, h);
  }
  *width = w;
  *height = h;
}

void frame() {
  int width = 0, height = 0;
  syncCanvasSize(&width, &height);
  glViewport(0, 0, width, height);

  double elapsed = emscripten_get_now() / 1000.0 - g_app->startTime;
  g_app->scene.update(elapsed);

  glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
  glClearStencil(0);
  glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  g_app->shapeRenderer.draw(g_app->scene.shape(), width, height);
  g_app->wordRenderer.draw(g_app->scene, width, height);
}

}  // namespace

int main() {
  EmscriptenWebGLContextAttributes attrs;
  emscripten_webgl_init_context_attributes(&attrs);
  attrs.majorVersion = 2;
  attrs.minorVersion = 0;
  attrs.antialias = true;
  attrs.stencil = true;

  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx =
      emscripten_webgl_create_context("#canvas", &attrs);
  if (ctx <= 0) {
    std::printf("failed to create WebGL2 context (%d)\n",
                static_cast<int>(ctx));
    return 1;
  }
  emscripten_webgl_make_context_current(ctx);

  static App app;
  g_app = &app;

  buildScene(app.scene);
  app.wordRenderer.init(app.scene);
  app.shapeRenderer.init(app.scene.shape(),
                         std::vector<words::Color>{
                             {0.96f, 0.65f, 0.14f},  // top: orange
                             {0.24f, 0.66f, 0.85f},  // left: cyan
                             {0.55f, 0.83f, 0.30f},  // right: green
                         });
  app.startTime = emscripten_get_now() / 1000.0;

  std::printf("words up: GL_VERSION = %s\n", glGetString(GL_VERSION));

  emscripten_set_main_loop(frame, 0, /*simulate_infinite_loop=*/false);
  return 0;
}
