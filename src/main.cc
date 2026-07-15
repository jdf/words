// words — application entry point: WebGL2 context, the demo scene (six
// renderings of one shaped word plus a rotating triangle exercising the
// collision machinery), and the main loop.

#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <algorithm>
#include <cstdio>
#include <numbers>
#include <utility>

#include "gl_util.h"
#include "scene.h"
#include "word.h"
#include "word_renderer.h"

namespace {

constexpr const char* kFontPath = "/fonts/Sexsmith.otf";
constexpr const char* kText = "words";

constexpr const char* kTriangleVS = R"(#version 300 es
uniform float u_angle;
uniform vec2 u_scene;
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec3 a_color;
out vec3 v_color;
void main() {
  float c = cos(u_angle), s = sin(u_angle);
  vec2 p = mat2(c, s, -s, c) * a_position;
  gl_Position = vec4(p * u_scene, 0.0, 1.0);
  v_color = a_color;
}
)";

constexpr const char* kTriangleFS = R"(#version 300 es
precision mediump float;
in vec3 v_color;
out vec4 frag;
void main() {
  frag = vec4(v_color, 1.0);
}
)";

struct App {
  words::Scene scene;
  words::WordRenderer renderer;
  GLuint triangleProgram = 0;
  GLint angleLoc = -1;
  GLint sceneLoc = -1;
  GLuint triangleVao = 0;
  double startTime = 0;
};

App g_app;

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

void buildScene() {
  words::ShapedText shaped = words::shapeText(kFontPath, kText);
  if (shaped.empty()) return;
  for (const Placement& p : kPlacements) {
    double scale = p.widthFrac * words::Scene::kWidth / shaped.bounds.width();
    words::Word word(shaped, scale, p.angleDeg * std::numbers::pi / 180.0);
    word.moveTo(p.x * words::Scene::kWidth / 2.0,
                p.y * words::Scene::kHeight / 2.0);
    g_app.scene.addWord(std::move(word), p.color);
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

void drawTriangle(int width, int height) {
  glUseProgram(g_app.triangleProgram);
  glUniform1f(g_app.angleLoc,
              static_cast<float>(g_app.scene.triangleAngle()));
  // Vertices are scene pixels; rotation happens in that isotropic space,
  // so the NDC mapping here is per-axis but shape-preserving on screen.
  double s = std::min(width / words::Scene::kWidth,
                      height / words::Scene::kHeight);
  glUniform2f(g_app.sceneLoc, static_cast<float>(s * 2.0 / width),
              static_cast<float>(s * 2.0 / height));
  glBindVertexArray(g_app.triangleVao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}

void frame() {
  int width = 0, height = 0;
  syncCanvasSize(&width, &height);
  glViewport(0, 0, width, height);

  double elapsed = emscripten_get_now() / 1000.0 - g_app.startTime;
  g_app.scene.update(elapsed);

  glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
  glClearStencil(0);
  glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  drawTriangle(width, height);
  g_app.renderer.draw(g_app.scene, width, height);
}

void initTriangle() {
  g_app.triangleProgram = words::linkProgram(kTriangleVS, kTriangleFS);
  g_app.angleLoc = glGetUniformLocation(g_app.triangleProgram, "u_angle");
  g_app.sceneLoc = glGetUniformLocation(g_app.triangleProgram, "u_scene");

  // Interleaved position (xy, scene px from Scene::triangleBase — the same
  // polygon the collision tests use) + color (rgb).
  const auto& base = words::Scene::triangleBase();
  const float colors[3][3] = {
      {0.96f, 0.65f, 0.14f},  // top: orange
      {0.24f, 0.66f, 0.85f},  // left: cyan
      {0.55f, 0.83f, 0.30f},  // right: green
  };
  float vertices[15];
  for (int i = 0; i < 3; ++i) {
    vertices[i * 5 + 0] = static_cast<float>(base[i].x);
    vertices[i * 5 + 1] = static_cast<float>(base[i].y);
    vertices[i * 5 + 2] = colors[i][0];
    vertices[i * 5 + 3] = colors[i][1];
    vertices[i * 5 + 4] = colors[i][2];
  }
  GLuint vbo = 0;
  glGenVertexArrays(1, &g_app.triangleVao);
  glBindVertexArray(g_app.triangleVao);
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        reinterpret_cast<void*>(2 * sizeof(float)));
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

  initTriangle();
  buildScene();
  g_app.renderer.init(g_app.scene);
  g_app.startTime = emscripten_get_now() / 1000.0;

  std::printf("words up: GL_VERSION = %s\n", glGetString(GL_VERSION));

  emscripten_set_main_loop(frame, 0, /*simulate_infinite_loop=*/false);
  return 0;
}
