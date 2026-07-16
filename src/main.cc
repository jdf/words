// words — application entry point: WebGL2 context, the demo scene, and
// on-demand rendering. Nothing animates, so there is no frame loop at all:
// the scene draws once at startup and again on window resizes, and the CPU
// and GPU are otherwise idle. (?t= in the URL is accepted for the e2e
// screenshot contract, but every frame is a "frozen" frame now.)
//
// URL parameters (?text= is read here; ?corpus= and ?font= are fetched by
// the page before the runtime starts and appear as MEMFS files):
//   ?text=<urlencoded>   cloud this text instead of the bundled sample
//   ?corpus=<slug>       cloud tests/corpus/<slug>.tsv's precomputed counts
//   ?font=<basename>     use assets/fonts/<basename>.ttf
//   ?palette=<name>      color with an original Wordle palette ("wordly",
//                        "heat", ...; see src/palette.cc)
//   ?variance=<name>     color variance: exact|little|some|lots|wild
//   ?orientation=<name>  horizontal|mostly-horizontal|half-and-half|...
//                        (see src/orientation.h)
//   ?placement=<name>    center-line|center
//   ?seed=<n>            layout randomness (default 1); live-adjustable
//                        via wordsSetSeed() from the ?ui panel

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <GLES3/gl3.h>

#include <absl/log/log.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "box.h"
#include "demo_scene.h"
#include "layout.h"
#include "logging.h"
#include "orientation.h"
#include "palette.h"
#include "scene.h"
#include "word.h"
#include "word_renderer.h"

namespace {

constexpr const char* kFontPath = "/fonts/sexsmith.ttf";
constexpr const char* kStopWordsDir = "/stopwords";
constexpr const char* kSampleTextPath = "/sample-text.txt";
// Written into MEMFS by the page's preRun when ?corpus= or ?font= is in
// the URL (see web/index.html); absent otherwise.
constexpr const char* kCorpusTsvPath = "/corpus.tsv";
constexpr const char* kFontOverridePath = "/font-override.ttf";

struct App {
  words::Scene scene;
  words::WordRenderer wordRenderer;
  std::string fontPath;  // resolved at startup, reused on relayout
};

App* g_app = nullptr;
uint32_t g_seed = 1;

std::string slurp(const char* path) {
  std::ifstream in(path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// A URL query parameter's value, or "" when absent.
std::string urlParam(const char* name) {
  char* value = static_cast<char*>(EM_ASM_PTR(
      {
        const t = new URLSearchParams(location.search).get(UTF8ToString($0));
        return t ? stringToNewUTF8(t) : 0;
      },
      name));
  if (!value) return {};
  std::string result(value);
  std::free(value);
  return result;
}

// The "text" URL query parameter, or the bundled sample text.
std::string cloudText() {
  std::string text = urlParam("text");
  if (!text.empty()) return text;
  return slurp(kSampleTextPath);
}

// Builds the scene per URL parameters; `description` receives the
// human-readable configuration (orientation · placement · palette) that
// the page shows in its status line.
words::Scene buildScene(const std::string& fontPath,
                        std::string* description) {
  words::CloudOptions options;
  words::ColorScheme scheme;
  const char* paletteLabel = "App Colors";
  if (const words::NamedPalette* palette =
          words::findPalette(urlParam("palette"))) {
    scheme.palette = palette->palette;
    if (auto v = words::findVariance(urlParam("variance"))) {
      scheme.variance = *v;
    }
    options.colors = &scheme;
    paletteLabel = palette->displayName;
  }
  if (auto o = words::findOrientation(urlParam("orientation"))) {
    options.orientation = *o;
  }
  if (auto p = words::findPlacement(urlParam("placement"))) {
    options.placement = *p;
  }
  options.seed = g_seed;
  std::string fontLabel = words::fontFamilyName(fontPath);
  if (fontLabel.empty()) fontLabel = fontPath;
  *description =
      std::string(words::orientationName(options.orientation)) + " · " +
      std::string(words::placementName(options.placement)) + " · " +
      paletteLabel + " · " + fontLabel;
  LOG(INFO) << "build: font=" << fontPath
            << " corpus=" << urlParam("corpus") << " config=" << *description
            << " variance=" << urlParam("variance");

  std::string tsv = slurp(kCorpusTsvPath);
  if (!tsv.empty()) {
    return words::buildCloudFromCountsTsv(fontPath, kStopWordsDir, tsv,
                                          options);
  }
  return words::buildCloudFromText(fontPath, kStopWordsDir, cloudText(),
                                   options);
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

void render() {
  int width = 0, height = 0;
  syncCanvasSize(&width, &height);
  glViewport(0, 0, width, height);

  const words::Color& bg = g_app->scene.background();
  glClearColor(bg.r, bg.g, bg.b, 1.0f);
  glClearStencil(0);
  glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  g_app->wordRenderer.draw(g_app->scene, width, height);
}

EM_BOOL onResize(int /*eventType*/, const EmscriptenUiEvent* /*event*/,
                 void* /*userData*/) {
  render();
  return EM_TRUE;
}

}  // namespace

// Relayout with a new seed — the first live JS->C++ command (wired to the
// ?ui panel's slider). Same text, font, and strategies; new randomness.
extern "C" EMSCRIPTEN_KEEPALIVE void wordsSetSeed(int seed) {
  if (!g_app) return;
  g_seed = static_cast<uint32_t>(seed);
  std::string description;
  g_app->scene = buildScene(g_app->fontPath, &description);
  g_app->wordRenderer.init(g_app->scene);
  render();
  std::printf("%s · seed %u\n", description.c_str(), g_seed);
}

// Console-callable engine interrogation (the page exposes the module as
// window.words): `words._wordsLogScene()` dumps the scene to the console.
extern "C" EMSCRIPTEN_KEEPALIVE void wordsLogScene() {
  if (!g_app) {
    LOG(WARNING) << "no scene yet";
    return;
  }
  const words::Scene& scene = g_app->scene;
  const words::Color& bg = scene.background();
  LOG(INFO) << "scene " << static_cast<int>(scene.width()) << "x"
            << static_cast<int>(scene.height()) << ", "
            << scene.entries().size() << " words, background rgb(" << bg.r
            << ", " << bg.g << ", " << bg.b << ")";
  size_t n = 0;
  for (const words::Scene::Entry& e : scene.entries()) {
    if (++n > 10) {
      LOG(INFO) << "  ... and " << scene.entries().size() - 10 << " more";
      break;
    }
    const words::Box& b = e.word.localBounds();
    LOG(INFO) << "  \"" << e.word.label() << "\" at ("
              << static_cast<int>(e.word.x()) << ", "
              << static_cast<int>(e.word.y()) << "), "
              << static_cast<int>(b.width()) << "x"
              << static_cast<int>(b.height());
  }
}

int main() {
  words::initLogging();

  EmscriptenWebGLContextAttributes attrs;
  emscripten_webgl_init_context_attributes(&attrs);
  attrs.majorVersion = 2;
  attrs.minorVersion = 0;
  attrs.antialias = true;
  attrs.stencil = true;
  // With no frame loop, the drawn frame must survive later composites.
  attrs.preserveDrawingBuffer = true;

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

  std::string seedParam = urlParam("seed");
  if (!seedParam.empty()) {
    g_seed = static_cast<uint32_t>(std::strtoul(seedParam.c_str(), nullptr, 10));
  }

  std::ifstream fontOverride(kFontOverridePath);
  app.fontPath = fontOverride ? kFontOverridePath : kFontPath;
  std::string description;
  app.scene = buildScene(app.fontPath, &description);
  app.wordRenderer.init(app.scene);

  std::printf("words up: GL_VERSION = %s\n", glGetString(GL_VERSION));
  // Last print line = the page's status overlay: the human-readable
  // configuration, so every golden names what it shows.
  std::printf("%s\n", description.c_str());

  // Draw twice at startup (the first pass resizes the canvas, which clears
  // the buffer mid-task in some engines), then only on window resizes. The
  // emscripten runtime stays alive after main returns to service the
  // callback.
  render();
  render();
  emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr,
                                 EM_FALSE, onResize);
  return 0;
}
