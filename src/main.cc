// words — engine entry point. The module lives in a Web Worker
// (web/worker.js): the page transfers an OffscreenCanvas and forwards the
// URL query string, and everything heavy — text analysis, shaping, layout,
// rendering — happens off the main thread. Nothing animates, so there is
// no frame loop: the scene draws once at startup and again on resize or
// relayout commands, and the CPU and GPU are otherwise idle.
//
// Host protocol (postMessage, see web/worker.js and web/index.html):
//   in:  setSeed, resize, logScene — dispatched to the wordsXxx exports
//   out: progress {phase, done, total} mid-pipeline, idle when drawn,
//        print/printErr relayed by the worker shell
//
// URL parameters (?text= is read here; ?corpus= and ?font= are fetched by
// the worker before the runtime starts and appear as MEMFS files):
//   ?text=<urlencoded>   cloud this text instead of the default corpus
//   ?corpus=<slug>       cloud tests/corpus/<slug>.tsv's precomputed counts
//                        (the page defaults to moby-dick; ?no-ui hides its
//                        toolbar for the golden harness)
//   ?font=<basename>     use assets/fonts/<basename>.ttf
//   ?palette=<name>      color with an original Wordle palette ("wordly",
//                        "heat", ...; see src/palette.cc)
//   ?variance=<name>     color variance: exact|little|some|lots|wild
//   ?orientation=<name>  horizontal|mostly-horizontal|half-and-half|...
//                        (see src/orientation.h)
//   ?placement=<name>    center-line|center
//   ?seed=<n>            layout randomness (default 1447); live-adjustable
//                        via the ?ui panel

#include <emscripten/em_asm.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5_webgl.h>

#include <GLES3/gl3.h>

#include <absl/log/log.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "box.h"
#include "demo_scene.h"
#include "layout.h"
#include "logging.h"
#include "orientation.h"
#include "palette.h"
#include "pdf.h"
#include "scene.h"
#include "svg.h"
#include "word.h"
#include "word_renderer.h"

namespace {

constexpr const char* kFontPath = "/fonts/sexsmith.ttf";
constexpr const char* kStopWordsDir = "/stopwords";
constexpr const char* kSampleTextPath = "/sample-text.txt";
// Written into MEMFS by the worker's preRun when ?corpus= or ?font= is in
// the URL (see web/worker.js); absent otherwise.
constexpr const char* kCorpusTsvPath = "/corpus.tsv";
constexpr const char* kFontOverridePath = "/font-override.ttf";

struct App {
  words::Scene scene;
  words::WordRenderer wordRenderer;
  std::string fontPath;  // resolved at startup, reused on relayout
  int width = 0;         // drawing-buffer size, device pixels; the page
  int height = 0;        // pushes changes via the resize message
};

App* g_app = nullptr;
uint32_t g_seed = 1447;  // the curated default (see CloudOptions::seed)
// UI override for the orientation strategy; empty = use the URL parameter.
std::string g_orientation;
// UI override for the placement strategy; empty = use the URL parameter.
std::string g_placement;
// UI override for the palette; unset = use the URL parameter. (Unlike
// orientation, the empty string is meaningful: the built-in dark scheme.)
std::optional<std::string> g_palette;
// MEMFS path of user-pasted text (the worker stages it); empty = none —
// use the corpus / ?text= / sample-text sources.
std::string g_textPath;

std::string slurp(const char* path) {
  std::ifstream in(path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// A URL query parameter's value, or "" when absent. Workers see their own
// script URL in location, so the page forwards its query string and the
// worker shell parks it on the global scope.
std::string urlParam(const char* name) {
  char* value = static_cast<char*>(EM_ASM_PTR(
      {
        const search = globalThis.__wordsSearch || "";
        const t = new URLSearchParams(search).get(UTF8ToString($0));
        return t ? stringToNewUTF8(t) : 0;
      },
      name));
  if (!value) return {};
  std::string result(value);
  std::free(value);
  return result;
}

// Progress and completion events for the page (input gating, progress UI).
// postMessage from worker code goes to the page; ordering with the print
// relay is preserved.
void postProgress(const char* phase, size_t done, size_t total) {
  EM_ASM(
      {
        postMessage(
            {type : 'progress', phase : UTF8ToString($0), done : $1, total : $2});
      },
      phase, static_cast<int>(done), static_cast<int>(total));
}

void postIdle() {
  EM_ASM({ postMessage({type : 'idle'}); });
}

// The "text" URL query parameter, or the bundled sample text.
std::string cloudText() {
  std::string text = urlParam("text");
  if (!text.empty()) return text;
  return slurp(kSampleTextPath);
}

// Builds the scene per URL parameters; `description` receives the
// human-readable configuration (orientation · placement · palette · font)
// that the page shows in its status line.
words::Scene buildScene(const std::string& fontPath,
                        std::string* description) {
  words::CloudOptions options;
  words::ColorScheme scheme;
  const char* paletteLabel = "App Colors";
  if (const words::NamedPalette* palette = words::findPalette(
          g_palette ? *g_palette : urlParam("palette"))) {
    scheme.palette = palette->palette;
    if (auto v = words::findVariance(urlParam("variance"))) {
      scheme.variance = *v;
    }
    options.colors = &scheme;
    paletteLabel = palette->displayName;
  }
  std::string orientation =
      g_orientation.empty() ? urlParam("orientation") : g_orientation;
  if (auto o = words::findOrientation(orientation)) {
    options.orientation = *o;
  }
  std::string placement =
      g_placement.empty() ? urlParam("placement") : g_placement;
  if (auto p = words::findPlacement(placement)) {
    options.placement = *p;
  }
  options.seed = g_seed;
  options.progress = postProgress;
  std::string fontLabel = words::fontFamilyName(fontPath);
  if (fontLabel.empty()) fontLabel = fontPath;
  *description =
      std::string(words::orientationName(options.orientation)) + " · " +
      std::string(words::placementName(options.placement)) + " · " +
      paletteLabel + " · " + fontLabel;
  LOG(INFO) << "build: font=" << fontPath
            << " corpus=" << urlParam("corpus") << " config=" << *description
            << " variance=" << urlParam("variance");

  // Source priority: the user's own words, then a corpus TSV, then the
  // ?text= parameter / bundled sample.
  if (!g_textPath.empty()) {
    return words::buildCloudFromText(fontPath, kStopWordsDir,
                                     slurp(g_textPath.c_str()), options);
  }
  std::string tsv = slurp(kCorpusTsvPath);
  if (!tsv.empty()) {
    return words::buildCloudFromCountsTsv(fontPath, kStopWordsDir, tsv,
                                          options);
  }
  return words::buildCloudFromText(fontPath, kStopWordsDir, cloudText(),
                                   options);
}

void render() {
  glViewport(0, 0, g_app->width, g_app->height);

  const words::Color& bg = g_app->scene.background();
  glClearColor(bg.r, bg.g, bg.b, 1.0f);
  glClearStencil(0);
  glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  g_app->wordRenderer.draw(g_app->scene, g_app->width, g_app->height);
}

}  // namespace

// Rebuild the cloud from a new spec (the toolbar, via the worker shell):
// seed, orientation slug ("" keeps the URL's), palette slug ("" is the
// built-in dark scheme), font path ("" keeps the current font — the
// worker stages fetched fonts into MEMFS first), and text path ("" means
// no user text: fall back to corpus / ?text= / sample).
extern "C" EMSCRIPTEN_KEEPALIVE void wordsRebuild(int seed,
                                                  const char* orientation,
                                                  const char* placement,
                                                  const char* palette,
                                                  const char* fontPath,
                                                  const char* textPath) {
  if (!g_app) return;
  g_seed = static_cast<uint32_t>(seed);
  g_orientation = orientation ? orientation : "";
  g_placement = placement ? placement : "";
  g_palette = std::string(palette ? palette : "");
  g_textPath = textPath ? textPath : "";
  if (fontPath && *fontPath) g_app->fontPath = fontPath;
  std::string description;
  g_app->scene = buildScene(g_app->fontPath, &description);
  g_app->wordRenderer.init(g_app->scene);
  render();
  std::printf("%s · seed %u\n", description.c_str(), g_seed);
  postIdle();
}

// The page pushes drawing-buffer size changes (device pixels); no DOM in
// the worker to observe.
extern "C" EMSCRIPTEN_KEEPALIVE void wordsResize(int width, int height) {
  if (!g_app) return;
  if (width == g_app->width && height == g_app->height) return;
  g_app->width = width;
  g_app->height = height;
  EM_ASM(
      {
        Module.canvas.width = $0;
        Module.canvas.height = $1;
      },
      width, height);
  render();
}

// The scene as SVG for export (the source of every save format — the
// page rasterizes it for PNG and PDF). Returned pointer stays valid
// until the next call.
extern "C" EMSCRIPTEN_KEEPALIVE const char* wordsSceneSvg(int background) {
  static std::string svg;
  if (!g_app) return "";
  svg = words::toSvg(g_app->scene, background != 0);
  return svg.c_str();
}

// Scene dimensions, for aspect-correct export UI.
extern "C" EMSCRIPTEN_KEEPALIVE double wordsSceneWidth() {
  return g_app ? g_app->scene.width() : 0;
}
extern "C" EMSCRIPTEN_KEEPALIVE double wordsSceneHeight() {
  return g_app ? g_app->scene.height() : 0;
}

// The scene as a vector PDF (same outlines as the SVG, Flate-compressed
// binary — hence pointer + size, not a C string). The buffer stays valid
// until the next call.
std::string g_pdfBytes;
extern "C" EMSCRIPTEN_KEEPALIVE const char* wordsScenePdf(double pointWidth) {
  if (!g_app) return "";
  g_pdfBytes = words::toPdf(g_app->scene, pointWidth);
  return g_pdfBytes.data();
}
extern "C" EMSCRIPTEN_KEEPALIVE int wordsScenePdfSize() {
  return static_cast<int>(g_pdfBytes.size());
}

// Console-callable engine interrogation (the page's window.words shim
// forwards to the worker): `words._wordsLogScene()` dumps the scene.
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

  // Module.canvas is the OffscreenCanvas the page transferred, already at
  // the right device-pixel size. In a worker there's no DOM to query, so
  // register it under the "#canvas" selector explicitly (specialHTMLTargets
  // is the html5 API's documented escape hatch for exactly this).
  EM_ASM({ specialHTMLTargets["#canvas"] = Module.canvas; });
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
  app.width = EM_ASM_INT(return Module.canvas.width);
  app.height = EM_ASM_INT(return Module.canvas.height);

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

  render();
  postIdle();
  // The runtime stays alive after main returns to service commands.
  return 0;
}
