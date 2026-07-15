// words — dependency proof of concept.
// Shapes a string with HarfBuzz, extracts and flattens the glyph outlines
// with FreeType, merges them with Clipper2, and fills the result on the GPU
// with a two-pass stencil trick, on top of the animated WebGL2 triangle.

#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <hb.h>

#include <clipper2/clipper.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

// ---------------------------------------------------------------- shaders

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

constexpr const char* kTextVS = R"(#version 300 es
uniform mat2 u_mat;
uniform vec2 u_offset;
layout(location = 0) in vec2 a_position;
void main() {
  gl_Position = vec4(u_mat * a_position + u_offset, 0.0, 1.0);
}
)";

constexpr const char* kTextFS = R"(#version 300 es
precision mediump float;
uniform vec4 u_color;
out vec4 frag;
void main() {
  frag = u_color;
}
)";

// ---------------------------------------------------------------- GL utils

GLuint compileShader(GLenum type, const char* source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(shader, sizeof log, nullptr, log);
    std::printf("shader compile failed: %s\n", log);
  }
  return shader;
}

GLuint linkProgram(const char* vs, const char* fs) {
  GLuint program = glCreateProgram();
  glAttachShader(program, compileShader(GL_VERTEX_SHADER, vs));
  glAttachShader(program, compileShader(GL_FRAGMENT_SHADER, fs));
  glLinkProgram(program);
  GLint ok = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetProgramInfoLog(program, sizeof log, nullptr, log);
    std::printf("program link failed: %s\n", log);
  }
  return program;
}

// ------------------------------------------------------- outline flattening

// Collects one glyph's outline as flattened contours, y-up, offset by the
// pen position. Everything stays in integer font units (FT_LOAD_NO_SCALE):
// shaping, flattening, and booleans all happen in the font's own coordinate
// space, and one transform maps the result to the screen at draw time.
struct OutlineSink {
  Clipper2Lib::PathsD contours;
  double penX = 0, penY = 0;
  Clipper2Lib::PointD current{0, 0};

  static constexpr int kConicSteps = 8;
  static constexpr int kCubicSteps = 12;

  Clipper2Lib::PointD toUnits(const FT_Vector* v) const {
    return {penX + static_cast<double>(v->x),
            penY + static_cast<double>(v->y)};
  }

  static int moveTo(const FT_Vector* to, void* user) {
    auto* self = static_cast<OutlineSink*>(user);
    self->contours.emplace_back();
    self->current = self->toUnits(to);
    self->contours.back().push_back(self->current);
    return 0;
  }

  static int lineTo(const FT_Vector* to, void* user) {
    auto* self = static_cast<OutlineSink*>(user);
    self->current = self->toUnits(to);
    self->contours.back().push_back(self->current);
    return 0;
  }

  static int conicTo(const FT_Vector* c, const FT_Vector* to, void* user) {
    auto* self = static_cast<OutlineSink*>(user);
    auto p0 = self->current, p1 = self->toUnits(c), p2 = self->toUnits(to);
    for (int i = 1; i <= kConicSteps; ++i) {
      double t = static_cast<double>(i) / kConicSteps, u = 1.0 - t;
      self->contours.back().push_back(
          {u * u * p0.x + 2 * u * t * p1.x + t * t * p2.x,
           u * u * p0.y + 2 * u * t * p1.y + t * t * p2.y});
    }
    self->current = p2;
    return 0;
  }

  static int cubicTo(const FT_Vector* c1, const FT_Vector* c2,
                     const FT_Vector* to, void* user) {
    auto* self = static_cast<OutlineSink*>(user);
    auto p0 = self->current, p1 = self->toUnits(c1), p2 = self->toUnits(c2),
         p3 = self->toUnits(to);
    for (int i = 1; i <= kCubicSteps; ++i) {
      double t = static_cast<double>(i) / kCubicSteps, u = 1.0 - t;
      double a = u * u * u, b = 3 * u * u * t, cc = 3 * u * t * t,
             d = t * t * t;
      self->contours.back().push_back(
          {a * p0.x + b * p1.x + cc * p2.x + d * p3.x,
           a * p0.y + b * p1.y + cc * p2.y + d * p3.y});
    }
    self->current = p3;
    return 0;
  }
};

// ------------------------------------------------------------------ state

struct Contour {
  GLint first = 0;
  GLsizei count = 0;
};

struct State {
  GLuint triangleProgram = 0;
  GLint angleLoc = -1;
  GLint sceneLoc = -1;
  GLuint triangleVao = 0;

  GLuint textProgram = 0;
  GLint matLoc = -1;
  GLint offsetLoc = -1;
  GLint colorLoc = -1;

  GLuint textVao = 0;
  GLuint coverVao = 0;
  GLuint boxVao = 0;
  std::vector<Contour> contours;

  // Text bounds in font units (y-up, baseline at 0).
  double minX = 0, minY = 0, maxX = 0, maxY = 0;

  double startTime = 0.0;
};

State g_state;

constexpr const char* kFontPath = "/fonts/Sexsmith.otf";
constexpr const char* kText = "words";

// The scene is a fixed-aspect logical canvas, uniformly scaled and centered
// into the window (letterboxed when aspects differ), so resizing the window
// never changes the spatial relationships of what's drawn. Placement
// coordinates are fractions of the scene box: x,y in [-1,1], sizes as
// fractions of scene width.
constexpr double kSceneW = 1600.0;
constexpr double kSceneH = 1000.0;

// The triangle, in isotropic scene pixels: exactly equilateral, centered on
// the scene, rotating over time. Kept as data so the CPU-side intersection
// tests see the same geometry the GPU draws.
constexpr double kTriR = 310.0;
constexpr double kTriCos30 = 0.86602540378;
constexpr double kTriBase[3][2] = {
    {0.0, kTriR},
    {-kTriR * kTriCos30, -kTriR * 0.5},
    {kTriR * kTriCos30, -kTriR * 0.5},
};

// One placed word, as a rigid body. Scale and rotation are baked into the
// word-local geometry (the AABB is only valid for one size and angle), and
// the box lives with the curves in that local frame; position is the one
// dynamic degree of freedom, translating curves and box together. That's
// what makes moving a word during layout cheap: the geometry never changes,
// only the offset.
struct WordInstance {
  double k = 1;                    // scene px per font unit (baked)
  double angleRad = 0;             // rotation (baked)
  double sceneX = 0, sceneY = 0;   // translation: word center, scene px
  float r = 1, g = 1, b = 1;
  Clipper2Lib::PathsD localPaths;  // merged outlines, word-local scene px
  double minX = 0, minY = 0, maxX = 0, maxY = 0;  // root AABB, word-local
  bool hit = false;  // triangle currently intersects the translated AABB
};

std::vector<WordInstance> g_words;

// Shapes kText with HarfBuzz, flattens every glyph outline via FreeType, and
// merges the lot with Clipper2. The union's output contours are disjoint
// (holes as separate paths), which is exactly what an even-odd stencil fill
// wants.
//
// Shaping and outline extraction both happen in integer font units — HarfBuzz
// through its own OpenType functions with the scale pinned to units-per-em,
// FreeType with FT_LOAD_NO_SCALE — so there is no fixed-point quantization
// anywhere; the geometry is transformed to screen space exactly once, in the
// vertex shader.
Clipper2Lib::PathsD buildTextGeometry() {
  FT_Library library = nullptr;
  if (FT_Init_FreeType(&library) != 0) {
    std::printf("FT_Init_FreeType failed\n");
    return {};
  }
  FT_Face face = nullptr;
  if (FT_New_Face(library, kFontPath, 0, &face) != 0) {
    std::printf("FT_New_Face failed — font missing from virtual FS?\n");
    return {};
  }

  hb_blob_t* blob = hb_blob_create_from_file_or_fail(kFontPath);
  if (!blob) {
    std::printf("hb_blob_create_from_file_or_fail failed\n");
    return {};
  }
  hb_face_t* hbFace = hb_face_create(blob, 0);
  hb_blob_destroy(blob);
  hb_font_t* hbFont = hb_font_create(hbFace);
  unsigned int upem = hb_face_get_upem(hbFace);
  hb_font_set_scale(hbFont, static_cast<int>(upem), static_cast<int>(upem));

  hb_buffer_t* buffer = hb_buffer_create();
  hb_buffer_add_utf8(buffer, kText, -1, 0, -1);
  hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
  hb_buffer_set_script(buffer, HB_SCRIPT_LATIN);
  hb_buffer_set_language(buffer, hb_language_from_string("en", -1));
  hb_shape(hbFont, buffer, nullptr, 0);

  unsigned int glyphCount = 0;
  hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &glyphCount);
  hb_glyph_position_t* positions =
      hb_buffer_get_glyph_positions(buffer, &glyphCount);

  OutlineSink sink;
  FT_Outline_Funcs funcs = {};
  funcs.move_to = OutlineSink::moveTo;
  funcs.line_to = OutlineSink::lineTo;
  funcs.conic_to = OutlineSink::conicTo;
  funcs.cubic_to = OutlineSink::cubicTo;

  // hb positions are integer font units (scale == upem), as are the outline
  // coordinates under FT_LOAD_NO_SCALE — no /64, nothing to round.
  double penX = 0, penY = 0;
  for (unsigned int i = 0; i < glyphCount; ++i) {
    if (FT_Load_Glyph(face, infos[i].codepoint,
                      FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP) != 0) {
      continue;
    }
    sink.penX = penX + positions[i].x_offset;
    sink.penY = penY + positions[i].y_offset;
    FT_Outline_Decompose(&face->glyph->outline, &funcs, &sink);
    penX += positions[i].x_advance;
    penY += positions[i].y_advance;
  }
  std::printf("shaped %u glyphs (upem %u) → %zu raw contours\n", glyphCount,
              upem, sink.contours.size());

  hb_buffer_destroy(buffer);
  hb_font_destroy(hbFont);
  hb_face_destroy(hbFace);
  FT_Done_Face(face);
  FT_Done_FreeType(library);

  Clipper2Lib::PathsD merged =
      Clipper2Lib::Union(sink.contours, Clipper2Lib::FillRule::NonZero);
  size_t vertexCount = 0;
  for (const auto& path : merged) vertexCount += path.size();
  std::printf("clipper union → %zu contours, %zu vertices\n", merged.size(),
              vertexCount);
  return merged;
}

// Uploads merged contours as one VBO (fan-filled per contour in the stencil
// pass) plus a cover quad over the text bounds.
void uploadTextGeometry(const Clipper2Lib::PathsD& paths) {
  std::vector<float> data;
  g_state.contours.clear();
  bool first = true;
  for (const auto& path : paths) {
    Contour c;
    c.first = static_cast<GLint>(data.size() / 2);
    c.count = static_cast<GLsizei>(path.size());
    g_state.contours.push_back(c);
    for (const auto& p : path) {
      data.push_back(static_cast<float>(p.x));
      data.push_back(static_cast<float>(p.y));
      if (first) {
        g_state.minX = g_state.maxX = p.x;
        g_state.minY = g_state.maxY = p.y;
        first = false;
      } else {
        g_state.minX = std::min(g_state.minX, p.x);
        g_state.maxX = std::max(g_state.maxX, p.x);
        g_state.minY = std::min(g_state.minY, p.y);
        g_state.maxY = std::max(g_state.maxY, p.y);
      }
    }
  }
  if (data.empty()) return;

  glGenVertexArrays(1, &g_state.textVao);
  glBindVertexArray(g_state.textVao);
  GLuint vbo = 0;
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(),
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

  const float quad[] = {
      static_cast<float>(g_state.minX), static_cast<float>(g_state.minY),
      static_cast<float>(g_state.maxX), static_cast<float>(g_state.minY),
      static_cast<float>(g_state.minX), static_cast<float>(g_state.maxY),
      static_cast<float>(g_state.maxX), static_cast<float>(g_state.maxY),
  };
  glGenVertexArrays(1, &g_state.coverVao);
  glBindVertexArray(g_state.coverVao);
  GLuint coverVbo = 0;
  glGenBuffers(1, &coverVbo);
  glBindBuffer(GL_ARRAY_BUFFER, coverVbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
}

// ------------------------------------------------------------------ frame

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

void drawTriangle(int width, int height, double angle) {
  glUseProgram(g_state.triangleProgram);
  glUniform1f(g_state.angleLoc, static_cast<float>(angle));
  // Vertices are scene pixels; rotation happens in that isotropic space,
  // so the NDC mapping here is per-axis but shape-preserving on screen.
  double s = std::min(width / kSceneW, height / kSceneH);
  glUniform2f(g_state.sceneLoc, static_cast<float>(s * 2.0 / width),
              static_cast<float>(s * 2.0 / height));
  glBindVertexArray(g_state.triangleVao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}

// One rendering of the text: width as a fraction of scene width, center
// position in scene fractions [-1,1], rotation about the text's own center, and a fill color.
struct Placement {
  float widthFrac;
  float x, y;
  float angleDeg;
  float r, g, b;
};

constexpr Placement kPlacements[] = {
    {0.52f, -0.08f, 0.52f, 0.0f, 0.93f, 0.93f, 0.90f},   // big headline
    {0.30f, 0.42f, -0.35f, 90.0f, 0.36f, 0.72f, 0.86f},  // vertical, cyan
    {0.34f, -0.42f, -0.28f, 24.0f, 0.96f, 0.65f, 0.14f}, // tilted, orange
    {0.18f, -0.10f, -0.68f, -12.0f, 0.55f, 0.83f, 0.30f},// small, green
    {0.12f, 0.30f, 0.02f, -90.0f, 0.80f, 0.45f, 0.78f},  // tiny, plum
    {0.10f, -0.72f, 0.10f, 45.0f, 0.85f, 0.33f, 0.31f},  // tiny, brick
};

// Resolves each Placement into a rigid word: scale and rotation are applied
// to the font-unit outlines to produce word-local geometry (centered on the
// word's own origin), and the root AABB is computed from those points — an
// axis-aligned box only fits rotated text if it's computed after the
// rotation. Position stays separate as the dynamic translation.
void buildWordInstances(const Clipper2Lib::PathsD& fontPaths) {
  double textW = g_state.maxX - g_state.minX;
  double cx = (g_state.minX + g_state.maxX) / 2.0;
  double cy = (g_state.minY + g_state.maxY) / 2.0;

  g_words.clear();
  for (const Placement& p : kPlacements) {
    WordInstance w;
    w.k = p.widthFrac * kSceneW / textW;
    w.angleRad = p.angleDeg * M_PI / 180.0;
    w.sceneX = p.x * kSceneW / 2.0;
    w.sceneY = p.y * kSceneH / 2.0;
    w.r = p.r;
    w.g = p.g;
    w.b = p.b;

    double c = std::cos(w.angleRad), s = std::sin(w.angleRad);
    bool first = true;
    for (const auto& path : fontPaths) {
      Clipper2Lib::PathD out;
      out.reserve(path.size());
      for (const auto& q : path) {
        double x = q.x - cx, y = q.y - cy;
        double lx = w.k * (c * x - s * y);
        double ly = w.k * (s * x + c * y);
        out.push_back({lx, ly});
        if (first) {
          w.minX = w.maxX = lx;
          w.minY = w.maxY = ly;
          first = false;
        } else {
          w.minX = std::min(w.minX, lx);
          w.maxX = std::max(w.maxX, lx);
          w.minY = std::min(w.minY, ly);
          w.maxY = std::max(w.maxY, ly);
        }
      }
      w.localPaths.push_back(std::move(out));
    }
    g_words.push_back(std::move(w));
  }
}

// Uploads every word's local AABB as a 4-vertex loop in one VBO. The box is
// drawn with the word's translation applied, so it rides along if the word
// moves.
void uploadBoxGeometry() {
  std::vector<float> data;
  for (const WordInstance& w : g_words) {
    const double corners[4][2] = {{w.minX, w.minY},
                                  {w.maxX, w.minY},
                                  {w.maxX, w.maxY},
                                  {w.minX, w.maxY}};
    for (const auto& corner : corners) {
      data.push_back(static_cast<float>(corner[0]));
      data.push_back(static_cast<float>(corner[1]));
    }
  }
  glGenVertexArrays(1, &g_state.boxVao);
  glBindVertexArray(g_state.boxVao);
  GLuint vbo = 0;
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(),
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
}

// Draws one word with the two-pass stencil fill. Pass 2 zeroes the
// stencil bits it consumes, so consecutive (even overlapping) placements
// never see each other's coverage.
void drawWord(const WordInstance& w, int width, int height) {
  double cx = (g_state.minX + g_state.maxX) / 2.0;
  double cy = (g_state.minY + g_state.maxY) / 2.0;

  // Font units → device pixels → NDC, rotation applied in isotropic space.
  double s = std::min(width / kSceneW, height / kSceneH);
  double pxPerUnit = w.k * s;
  double sxNdc = pxPerUnit * 2.0 / width;
  double syNdc = pxPerUnit * 2.0 / height;
  double c = std::cos(w.angleRad), sn = std::sin(w.angleRad);
  // Column-major mat2: NDC-scale ∘ rotate, applied to (pos - center).
  const float mat[4] = {
      static_cast<float>(sxNdc * c), static_cast<float>(syNdc * sn),
      static_cast<float>(-sxNdc * sn), static_cast<float>(syNdc * c)};
  const float px = static_cast<float>(w.sceneX * s * 2.0 / width);
  const float py = static_cast<float>(w.sceneY * s * 2.0 / height);
  const float ox = px - static_cast<float>(mat[0] * cx + mat[2] * cy);
  const float oy = py - static_cast<float>(mat[1] * cx + mat[3] * cy);

  glUniformMatrix2fv(g_state.matLoc, 1, GL_FALSE, mat);
  glUniform2f(g_state.offsetLoc, ox, oy);

  // Pass 1: even-odd coverage into the stencil buffer, no color writes.
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  glStencilFunc(GL_ALWAYS, 0, 0xFF);
  glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
  glBindVertexArray(g_state.textVao);
  for (const Contour& c2 : g_state.contours) {
    glDrawArrays(GL_TRIANGLE_FAN, c2.first, c2.count);
  }

  // Pass 2: paint a cover quad wherever the stencil ended up odd, zeroing
  // the stencil as we go so the next placement starts clean.
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glStencilFunc(GL_EQUAL, 1, 0x1);
  glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);
  glUniform4f(g_state.colorLoc, w.r, w.g, w.b, 1.0f);
  glBindVertexArray(g_state.coverVao);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void drawText(int width, int height) {
  if (g_state.contours.empty()) return;
  glUseProgram(g_state.textProgram);
  glEnable(GL_STENCIL_TEST);
  for (const WordInstance& w : g_words) {
    drawWord(w, width, height);
  }
  glDisable(GL_STENCIL_TEST);
}

// Draws the translated AABB of every word the triangle currently hits.
void drawBoxes(int width, int height) {
  glUseProgram(g_state.textProgram);
  double s = std::min(width / kSceneW, height / kSceneH);
  // Scene px → NDC, no rotation: boxes are axis-aligned by construction.
  const float mat[4] = {static_cast<float>(s * 2.0 / width), 0.0f, 0.0f,
                        static_cast<float>(s * 2.0 / height)};
  glUniformMatrix2fv(g_state.matLoc, 1, GL_FALSE, mat);
  glBindVertexArray(g_state.boxVao);
  for (size_t i = 0; i < g_words.size(); ++i) {
    const WordInstance& w = g_words[i];
    if (!w.hit) continue;
    glUniform2f(g_state.offsetLoc,
                static_cast<float>(w.sceneX * s * 2.0 / width),
                static_cast<float>(w.sceneY * s * 2.0 / height));
    glUniform4f(g_state.colorLoc, w.r, w.g, w.b, 1.0f);
    glDrawArrays(GL_LINE_LOOP, static_cast<GLint>(i * 4), 4);
  }
}

// The scene-space collision query: triangle polygon vs each word's
// translated AABB, decided by Clipper2 on the actual geometry.
void updateIntersections(double triAngle) {
  double c = std::cos(triAngle), s = std::sin(triAngle);
  Clipper2Lib::PathsD tri(1);
  for (const auto& v : kTriBase) {
    tri[0].push_back({c * v[0] - s * v[1], s * v[0] + c * v[1]});
  }
  for (WordInstance& w : g_words) {
    Clipper2Lib::PathsD box(1);
    box[0] = {{w.minX + w.sceneX, w.minY + w.sceneY},
              {w.maxX + w.sceneX, w.minY + w.sceneY},
              {w.maxX + w.sceneX, w.maxY + w.sceneY},
              {w.minX + w.sceneX, w.maxY + w.sceneY}};
    Clipper2Lib::PathsD overlap =
        Clipper2Lib::Intersect(tri, box, Clipper2Lib::FillRule::NonZero);
    w.hit = !overlap.empty();
  }
}

void frame() {
  int width = 0, height = 0;
  syncCanvasSize(&width, &height);
  glViewport(0, 0, width, height);

  glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
  glClearStencil(0);
  glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  double elapsed = emscripten_get_now() / 1000.0 - g_state.startTime;
  double triAngle = elapsed * 0.6;
  updateIntersections(triAngle);

  drawTriangle(width, height, triAngle);
  drawText(width, height);
  drawBoxes(width, height);
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

  g_state.triangleProgram = linkProgram(kTriangleVS, kTriangleFS);
  g_state.angleLoc = glGetUniformLocation(g_state.triangleProgram, "u_angle");
  g_state.sceneLoc = glGetUniformLocation(g_state.triangleProgram, "u_scene");

  g_state.textProgram = linkProgram(kTextVS, kTextFS);
  g_state.matLoc = glGetUniformLocation(g_state.textProgram, "u_mat");
  g_state.offsetLoc = glGetUniformLocation(g_state.textProgram, "u_offset");
  g_state.colorLoc = glGetUniformLocation(g_state.textProgram, "u_color");

  g_state.startTime = emscripten_get_now() / 1000.0;

  // Triangle: interleaved position (xy) + color (rgb), one vertex per line.
  // Positions come from kTriBase (isotropic scene pixels) so the GPU draws
  // exactly the polygon the CPU intersection tests use.
  const float vertices[] = {
      static_cast<float>(kTriBase[0][0]), static_cast<float>(kTriBase[0][1]),
      0.96f, 0.65f, 0.14f,  // top: orange
      static_cast<float>(kTriBase[1][0]), static_cast<float>(kTriBase[1][1]),
      0.24f, 0.66f, 0.85f,  // left: cyan
      static_cast<float>(kTriBase[2][0]), static_cast<float>(kTriBase[2][1]),
      0.55f, 0.83f, 0.30f,  // right: green
  };
  GLuint vbo = 0;
  glGenVertexArrays(1, &g_state.triangleVao);
  glBindVertexArray(g_state.triangleVao);
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        reinterpret_cast<void*>(2 * sizeof(float)));

  Clipper2Lib::PathsD merged = buildTextGeometry();
  uploadTextGeometry(merged);
  buildWordInstances(merged);
  uploadBoxGeometry();
  std::printf("words up: GL_VERSION = %s\n", glGetString(GL_VERSION));

  emscripten_set_main_loop(frame, 0, /*simulate_infinite_loop=*/false);
  return 0;
}
