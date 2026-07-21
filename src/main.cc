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
//                        "heat", ...; see src/palette.cc) or a custom one
//                        ("custom:RRGGBB:RRGGBB,..." — background, then
//                        word colors; the page's palette editor emits it)
//   ?variance=<name>     color variance: exact|little|some|lots|wild
//   ?case=<name>         case fold: guess|as-written|lower|upper
//   ?exclude=<w1,w2>     removed words (comma-separated, folded keys)
//   ?recolor=<n>         nonzero: redraw palette assignment from seed n
//   ?loose=<f>           looseness: multiplies the search spiral's
//                        per-typeface base step (1..20, default 1)
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
#include <absl/strings/str_cat.h>
#include <absl/strings/str_split.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
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
#include "text.h"
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
int g_maxWords = 800;    // the word-count slider; benchmarked cap 2000
std::string g_variance;  // variance slug; "" falls back to ?variance=
std::string g_caseFold;  // case-fold slug; "" falls back to ?case=
// Removed words, comma-separated; "" falls back to ?exclude=. (Unlike
// the style dimensions, an empty rebuild value means "none": the UI
// always sends the whole list.)
std::string g_exclude;
bool g_excludeSet = false;
int g_colorSeed = 0;  // 0 = legacy colors; falls back to ?recolor=
// The view camera: pure render state (layout never sees it). Reset on
// every rebuild; the page owns the interaction math.
double g_zoom = 1.0;
double g_camX = 0.0;
double g_camY = 0.0;
// UI override for the orientation strategy; empty = use the URL parameter.
std::string g_orientation;
// UI override for the placement strategy; empty = use the URL parameter.
std::string g_placement;
// UI override for the palette; unset = use the URL parameter. (Unlike
// orientation, the empty string is meaningful: the built-in dark scheme.)
std::optional<std::string> g_palette;
// The Looseness slider: a user multiplier (1..20) on the search
// spiral's per-typeface base step (see spiralBase).
double g_loose = 1.0;

// Per-typeface spiral calibration. Fridge-magnet glyphs are solid tiles
// — mostly ink, nothing nests — so a much coarser search spiral finds
// equivalent positions in far fewer probes (measured at 2000 words:
// footprint +<2%, layout −30% at 2×; ×3 chosen by eye). Other dense
// faces can join this table (or a measured ink-coverage heuristic can
// replace it) as they prove out.
double spiralBase(const std::string& fontPath) {
  return fontPath.ends_with("/fridge.ttf") ? 3.0 : 1.0;
}
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

// The palette parameter resolved to colors plus its status-line label;
// a nullopt palette means the built-in App Colors scheme.
struct PaletteChoice {
  std::optional<words::Palette> palette;
  std::string label = "App Colors";
};
PaletteChoice resolvePalette(const std::string& param) {
  if (const words::NamedPalette* named = words::findPalette(param)) {
    return {named->palette, named->displayName};
  }
  if (std::optional<words::Palette> custom = words::parseCustomPalette(param)) {
    return {std::move(custom), "Custom Palette"};
  }
  return {};
}

// Each rebuild-spec dimension resolved from its UI override, falling
// back to the URL parameter, falling back to the stock default. Shared
// by the full pipeline (buildScene) and the recolor fast path.
std::string currentPaletteParam() {
  return g_palette ? *g_palette : urlParam("palette");
}
double currentVariance() {
  if (auto v = words::findVariance(
          g_variance.empty() ? urlParam("variance") : g_variance)) {
    return *v;
  }
  return words::kDefaultVariance;
}
words::Orientation currentOrientation() {
  std::string name =
      g_orientation.empty() ? urlParam("orientation") : g_orientation;
  if (auto o = words::findOrientation(name)) return *o;
  return words::CloudOptions().orientation;
}
words::Placement currentPlacement() {
  std::string name = g_placement.empty() ? urlParam("placement") : g_placement;
  if (auto p = words::findPlacement(name)) return *p;
  return words::CloudOptions().placement;
}
uint32_t currentColorSeed() {
  return static_cast<uint32_t>(
      g_colorSeed ? g_colorSeed : std::atoi(urlParam("recolor").c_str()));
}

// The status line's description, kept in parts so the recolor fast path
// can swap the palette label without re-running the pipeline.
struct SceneDescription {
  std::string title;  // "Book Title · " when a corpus TSV names one
  std::string orientation, placement, palette, font;
  std::string str() const {
    return absl::StrCat(title, orientation, " · ", placement, " · ", palette,
                        " · ", font);
  }
};
SceneDescription g_desc;

// Builds the scene per the resolved spec, leaving the human-readable
// configuration in g_desc for the page's status line.
words::Scene buildScene(const std::string& fontPath) {
  words::CloudOptions options;
  words::ColorScheme scheme;
  PaletteChoice choice = resolvePalette(currentPaletteParam());
  if (choice.palette) {
    scheme.palette = *std::move(choice.palette);
    scheme.variance = currentVariance();
    options.colors = &scheme;
  }
  if (auto f = words::findCaseFold(
          g_caseFold.empty() ? urlParam("case") : g_caseFold)) {
    options.caseFold = *f;
  }
  for (std::string_view word : absl::StrSplit(
           g_excludeSet ? g_exclude : urlParam("exclude"), ',',
           absl::SkipEmpty())) {
    options.exclude.emplace_back(word);
  }
  options.orientation = currentOrientation();
  options.placement = currentPlacement();
  options.seed = g_seed;
  options.colorSeed = currentColorSeed();
  options.maxWords = static_cast<size_t>(g_maxWords);
  options.spiralStep =
      spiralBase(fontPath) * std::clamp(g_loose, 1.0, 20.0);
  // The world takes the canvas's shape: portrait screens get portrait
  // clouds. (The e2e viewport is 1200x750 — exactly the 1.6 default.)
  if (g_app && g_app->width > 0 && g_app->height > 0) {
    options.aspect =
        static_cast<double>(g_app->width) / static_cast<double>(g_app->height);
  }
  options.progress = postProgress;
  std::string fontLabel = words::fontFamilyName(fontPath);
  if (fontLabel.empty()) fontLabel = fontPath;
  g_desc = {"", std::string(words::orientationName(options.orientation)),
            std::string(words::placementName(options.placement)),
            std::move(choice.label), std::move(fontLabel)};
  LOG(INFO) << "build: font=" << fontPath
            << " corpus=" << urlParam("corpus") << " config=" << g_desc.str()
            << " variance=" << urlParam("variance");

  // Source priority: the user's own words, then a corpus TSV, then the
  // ?text= parameter / bundled sample.
  if (!g_textPath.empty()) {
    return words::buildCloudFromText(fontPath, kStopWordsDir,
                                     slurp(g_textPath.c_str()), options);
  }
  std::string tsv = slurp(kCorpusTsvPath);
  if (!tsv.empty()) {
    // The TSV's first header line is the book's title ("# Moby Dick; Or,
    // The Whale") — lead the status line with it.
    if (tsv.starts_with("# ")) {
      const size_t eol = tsv.find('\n');
      g_desc.title = absl::StrCat(
          tsv.substr(2, eol == std::string::npos ? std::string::npos
                                                 : eol - 2),
          " · ");
    }
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

  g_app->wordRenderer.draw(g_app->scene, g_app->width, g_app->height, g_zoom,
                           g_camX, g_camY);
}

}  // namespace

// Stage timings, console-only (the status line is pinned by the e2e
// goldens): scene = the whole pipeline (shape memo + layout), renderer
// = per-word VAO/VBO churn through the Emscripten GL proxy, draw =
// CPU-side command submission for the stencil-and-cover passes (GPU
// execution is asynchronous and not visible here). Contour vertices
// drive both GL stages — heavy outlines (Fridge) multiply them.
void logStageTimings(const char* what, const words::Scene& scene,
                     std::chrono::steady_clock::time_point t0,
                     std::chrono::steady_clock::time_point t1,
                     std::chrono::steady_clock::time_point t2,
                     std::chrono::steady_clock::time_point t3) {
  size_t contourVerts = 0;
  for (const words::Scene::Entry& e : scene.entries()) {
    for (const auto& path : e.word.localPaths()) contourVerts += path.size();
  }
  const auto ms = [](auto a, auto b) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(b - a)
        .count();
  };
  const std::string line = absl::StrCat(
      what, " timings: scene ", ms(t0, t1), "ms, renderer.init ", ms(t1, t2),
      "ms, draw-submit ", ms(t2, t3), "ms; ", scene.entries().size(),
      " words, ", contourVerts, " contour vertices");
  LOG(INFO) << line;
  // Also hand the line to the page (the worker's own console is awkward
  // to scrape); app.js logs it and parks it on window.__wordsTiming.
  EM_ASM({ postMessage({type : 'timing', text : UTF8ToString($0)}); },
         line.c_str());
}

// The tail every full rebuild shares (wordsRebuild and the recolor
// fallback): fresh view, full pipeline, renderer re-init, draw, status.
static void rebuildScene() {
  g_zoom = 1.0;
  g_camX = 0.0;
  g_camY = 0.0;
  const auto t0 = std::chrono::steady_clock::now();
  g_app->scene = buildScene(g_app->fontPath);
  const auto t1 = std::chrono::steady_clock::now();
  g_app->wordRenderer.init(g_app->scene);
  const auto t2 = std::chrono::steady_clock::now();
  render();
  const auto t3 = std::chrono::steady_clock::now();
  logStageTimings("rebuild", g_app->scene, t0, t1, t2, t3);
  std::printf("%s · seed %u\n", g_desc.str().c_str(), g_seed);
  postIdle();
}

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
                                                  const char* textPath,
                                                  int maxWords,
                                                  const char* variance,
                                                  const char* caseFold,
                                                  const char* exclude,
                                                  int colorSeed,
                                                  double looseness) {
  if (!g_app) return;
  g_seed = static_cast<uint32_t>(seed);
  if (maxWords > 0) g_maxWords = maxWords;
  g_loose = looseness > 0 ? looseness : 1.0;
  g_variance = variance ? variance : "";
  g_caseFold = caseFold ? caseFold : "";
  g_exclude = exclude ? exclude : "";
  g_excludeSet = true;
  g_colorSeed = colorSeed;
  g_orientation = orientation ? orientation : "";
  g_placement = placement ? placement : "";
  g_palette = std::string(palette ? palette : "");
  g_textPath = textPath ? textPath : "";
  if (fontPath && *fontPath) g_app->fontPath = fontPath;
  rebuildScene();
}

// Color-only spec changes (palette, variance, recolor seed): reassign
// colors on the already-laid-out scene and redraw — no shaping, no
// layout, no renderer re-init (colors are per-word draw uniforms), and
// the camera survives. Exactness: recolorScene replays the pipeline's
// shared RNG stream, so the result is identical to a full rebuild —
// except when the change crosses between App Colors and a palette,
// which alters the stream's per-word draw count (and thus a real
// rebuild's layout); that case falls back to the full pipeline.
extern "C" EMSCRIPTEN_KEEPALIVE void wordsRecolor(const char* palette,
                                                  const char* variance,
                                                  int colorSeed) {
  if (!g_app) return;
  const bool hadColors =
      resolvePalette(currentPaletteParam()).palette.has_value();
  g_palette = std::string(palette ? palette : "");
  g_variance = variance ? variance : "";
  g_colorSeed = colorSeed;
  PaletteChoice choice = resolvePalette(currentPaletteParam());
  if (choice.palette.has_value() != hadColors) {
    rebuildScene();
    return;
  }
  words::CloudOptions options;
  words::ColorScheme scheme;
  if (choice.palette) {
    scheme.palette = *std::move(choice.palette);
    scheme.variance = currentVariance();
    options.colors = &scheme;
  }
  options.orientation = currentOrientation();
  options.colorSeed = currentColorSeed();
  const auto t0 = std::chrono::steady_clock::now();
  words::recolorScene(g_app->scene, options);
  const auto t1 = std::chrono::steady_clock::now();
  render();
  const auto t2 = std::chrono::steady_clock::now();
  const auto ms = [](auto a, auto b) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(b - a)
        .count();
  };
  const std::string line = absl::StrCat(
      "recolor timings: recolor ", ms(t0, t1), "ms, draw-submit ",
      ms(t1, t2), "ms; ", g_app->scene.entries().size(), " words");
  LOG(INFO) << line;
  EM_ASM({ postMessage({type : 'timing', text : UTF8ToString($0)}); },
         line.c_str());
  g_desc.palette = std::move(choice.label);
  std::printf("%s · seed %u\n", g_desc.str().c_str(), g_seed);
  postIdle();
}

// The view camera: zoom magnifies around scene point (cx, cy). The page
// computes the interaction (anchored wheel zoom, drag pan) and pushes
// absolute state; rendering is a single cached-buffer draw, cheap enough
// for wheel-rate updates.
// The word under a canvas position (device pixels, y-down), for the
// page's context menu: inverts the renderer's projection (fit scale x
// zoom, camera recenter — see WordRenderer::draw), then picks the
// smallest word AABB containing the point, so a small word tucked into
// a big word's box gap stays pickable. Returns "" for a miss; the
// pointer stays valid until the next call.
extern "C" EMSCRIPTEN_KEEPALIVE const char* wordsHitTest(double px,
                                                         double py) {
  static std::string label;
  label.clear();
  if (!g_app || g_app->width <= 0 || g_app->height <= 0) return label.c_str();
  const words::Scene& scene = g_app->scene;
  if (scene.width() <= 0 || scene.height() <= 0) return label.c_str();
  const double w = g_app->width;
  const double h = g_app->height;
  const double s =
      std::min(w / scene.width(), h / scene.height()) * g_zoom;
  const double sceneX = (2.0 * px / w - 1.0) * w / (2.0 * s) + g_camX;
  const double sceneY = (1.0 - 2.0 * py / h) * h / (2.0 * s) + g_camY;
  double bestArea = 0;
  for (const words::Scene::Entry& e : scene.entries()) {
    const words::Box b = e.word.worldBounds();
    if (sceneX < b.minX || sceneX > b.maxX || sceneY < b.minY ||
        sceneY > b.maxY) {
      continue;
    }
    const double area = b.width() * b.height();
    if (label.empty() || area < bestArea) {
      label = e.word.label();
      bestArea = area;
    }
  }
  return label.c_str();
}

extern "C" EMSCRIPTEN_KEEPALIVE void wordsSetCamera(double zoom, double cx,
                                                    double cy) {
  if (!g_app) return;
  g_zoom = zoom;
  g_camX = cx;
  g_camY = cy;
  render();
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

// The build identifier, set once by the worker at boot; embedded in
// export metadata so shipped files say which build made them.
std::string g_buildId;
extern "C" EMSCRIPTEN_KEEPALIVE void wordsSetBuildId(const char* id) {
  g_buildId = id;
}
static std::string buildTag() {
  return g_buildId.empty() ? std::string()
                           : absl::StrCat("words build ", g_buildId);
}

// The scene as SVG for export (the source of every save format — the
// page rasterizes it for PNG and PDF). Returned pointer stays valid
// until the next call.
extern "C" EMSCRIPTEN_KEEPALIVE const char* wordsSceneSvg(int background) {
  static std::string svg;
  if (!g_app) return "";
  svg = words::toSvg(g_app->scene, background != 0, buildTag());
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
  g_pdfBytes = words::toPdf(g_app->scene, pointWidth, buildTag());
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
  std::string maxParam = urlParam("max");
  if (!maxParam.empty()) {
    int m = std::atoi(maxParam.c_str());
    if (m > 0) g_maxWords = m;
  }
  const double loose = std::atof(urlParam("loose").c_str());
  if (loose > 0) g_loose = loose;

  // Boot-font resolution: ?font= staged at its canonical /fonts/ path
  // (so the shape memo survives into the first rebuild), then the
  // legacy override path, then the preloaded default.
  const std::string bootFont = urlParam("font");
  const std::string canonicalFont = absl::StrCat("/fonts/", bootFont, ".ttf");
  if (!bootFont.empty() && std::ifstream(canonicalFont).good()) {
    app.fontPath = canonicalFont;
  } else {
    std::ifstream fontOverride(kFontOverridePath);
    app.fontPath = fontOverride ? kFontOverridePath : kFontPath;
  }
  const auto bootT0 = std::chrono::steady_clock::now();
  app.scene = buildScene(app.fontPath);
  const auto bootT1 = std::chrono::steady_clock::now();
  app.wordRenderer.init(app.scene);
  const auto bootT2 = std::chrono::steady_clock::now();

  std::printf("words up: GL_VERSION = %s\n", glGetString(GL_VERSION));
  // Last print line = the page's status overlay: the human-readable
  // configuration, so every golden names what it shows.
  std::printf("%s\n", g_desc.str().c_str());

  render();
  const auto bootT3 = std::chrono::steady_clock::now();
  logStageTimings("boot", app.scene, bootT0, bootT1, bootT2, bootT3);
  postIdle();
  // The runtime stays alive after main returns to service commands.
  return 0;
}
