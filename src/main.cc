// words — application entry point: WebGL2 context, the demo scene, and the
// main loop. Passing ?t=<seconds> in the URL renders exactly one frame of
// the scene frozen at that time — the deterministic mode the e2e golden
// test captures.

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <GLES3/gl3.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "demo_scene.h"
#include "scene.h"
#include "shape.h"
#include "shape_renderer.h"
#include "word_renderer.h"

namespace {

constexpr const char* kFontPath = "/fonts/Sexsmith.otf";
constexpr const char* kStopWordsDir = "/stopwords";
constexpr const char* kSampleTextPath = "/sample-text.txt";

struct App {
  words::Scene scene{words::Shape::equilateralTriangle(1.0)};
  words::WordRenderer wordRenderer;
  words::ShapeRenderer shapeRenderer;
  double startTime = 0;
  double frozenTime = -1;  // >= 0: render one frame at this time, no loop
};

App* g_app = nullptr;

// Value of the "t" URL query parameter, or -1 if absent/invalid.
double frozenTimeFromUrl() {
  return EM_ASM_DOUBLE({
    const t = parseFloat(new URLSearchParams(location.search).get('t'));
    return isNaN(t) ? -1 : t;
  });
}

// The "text" URL query parameter, or the bundled sample text.
std::string cloudText() {
  char* fromUrl = static_cast<char*>(EM_ASM_PTR({
    const t = new URLSearchParams(location.search).get('text');
    return t ? stringToNewUTF8(t) : 0;
  }));
  if (fromUrl) {
    std::string text(fromUrl);
    std::free(fromUrl);
    if (!text.empty()) return text;
  }
  std::ifstream in(kSampleTextPath);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
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

  double t = g_app->frozenTime >= 0
                 ? g_app->frozenTime
                 : emscripten_get_now() / 1000.0 - g_app->startTime;
  g_app->scene.update(t);

  glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
  glClearStencil(0);
  glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  g_app->shapeRenderer.draw(g_app->scene.shape(), width, height);
  g_app->wordRenderer.draw(g_app->scene, width, height);
}

}  // namespace

int main() {
  double frozenTime = frozenTimeFromUrl();

  EmscriptenWebGLContextAttributes attrs;
  emscripten_webgl_init_context_attributes(&attrs);
  attrs.majorVersion = 2;
  attrs.minorVersion = 0;
  attrs.antialias = true;
  attrs.stencil = true;
  // Frozen frames must survive later composites (there is no loop to
  // repaint them); live rendering keeps the cheaper default.
  attrs.preserveDrawingBuffer = frozenTime >= 0;

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

  app.scene = words::buildCloudFromText(kFontPath, kStopWordsDir, cloudText());
  app.wordRenderer.init(app.scene);
  app.shapeRenderer.init(app.scene.shape(),
                         std::vector<words::Color>{
                             {0.96f, 0.65f, 0.14f},  // top: orange
                             {0.24f, 0.66f, 0.85f},  // left: cyan
                             {0.55f, 0.83f, 0.30f},  // right: green
                         });
  app.startTime = emscripten_get_now() / 1000.0;
  app.frozenTime = frozenTime;

  std::printf("words up: GL_VERSION = %s\n", glGetString(GL_VERSION));

  if (app.frozenTime >= 0) {
    std::printf("frozen frame at t=%.3f\n", app.frozenTime);
    // Render exactly twice — the first frame's canvas resize clears the
    // drawing buffer — and run no loop: preserveDrawingBuffer keeps the
    // result visible, and headless screenshots don't wait out an
    // animation that renders the same pixels forever.
    frame();
    frame();
  } else {
    emscripten_set_main_loop(frame, 0, /*simulate_infinite_loop=*/false);
  }
  return 0;
}
