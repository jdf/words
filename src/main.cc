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
#include "word_renderer.h"

namespace {

constexpr const char* kFontPath = "/fonts/Sexsmith.otf";
constexpr const char* kStopWordsDir = "/stopwords";
constexpr const char* kSampleTextPath = "/sample-text.txt";

struct App {
  words::Scene scene;
  words::WordRenderer wordRenderer;
  bool frozen = false;  // render once, run no loop (for e2e screenshots)
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

  glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
  glClearStencil(0);
  glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  g_app->wordRenderer.draw(g_app->scene, width, height);
}

}  // namespace

int main() {
  bool frozen = frozenTimeFromUrl() >= 0;

  EmscriptenWebGLContextAttributes attrs;
  emscripten_webgl_init_context_attributes(&attrs);
  attrs.majorVersion = 2;
  attrs.minorVersion = 0;
  attrs.antialias = true;
  attrs.stencil = true;
  // Frozen frames must survive later composites (there is no loop to
  // repaint them); live rendering keeps the cheaper default.
  attrs.preserveDrawingBuffer = frozen;

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
  app.frozen = frozen;

  std::printf("words up: GL_VERSION = %s\n", glGetString(GL_VERSION));

  if (app.frozen) {
    std::printf("frozen frame\n");
    // Render exactly twice — the first frame's canvas resize clears the
    // drawing buffer — and run no loop: preserveDrawingBuffer keeps the
    // result visible, and headless screenshots don't wait for a loop.
    frame();
    frame();
  } else {
    // Nothing animates; the loop just tracks canvas resizes.
    emscripten_set_main_loop(frame, 0, /*simulate_infinite_loop=*/false);
  }
  return 0;
}
