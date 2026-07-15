// wrdl — infrastructure smoke test.
// Brings up a WebGL2 context, compiles a shader pair, and animates a
// triangle. Exists to prove the emscripten/CMake/vcpkg/WebGL pipeline
// end to end; will be replaced by the real renderer.

#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <cmath>
#include <cstdio>

namespace {

constexpr const char* kVertexShader = R"(#version 300 es
uniform float u_angle;
uniform vec2 u_aspect;
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec3 a_color;
out vec3 v_color;
void main() {
  float c = cos(u_angle), s = sin(u_angle);
  vec2 p = mat2(c, s, -s, c) * a_position;
  gl_Position = vec4(p * u_aspect, 0.0, 1.0);
  v_color = a_color;
}
)";

constexpr const char* kFragmentShader = R"(#version 300 es
precision mediump float;
in vec3 v_color;
out vec4 frag;
void main() {
  frag = vec4(v_color, 1.0);
}
)";

struct State {
  GLuint program = 0;
  GLint angleLoc = -1;
  GLint aspectLoc = -1;
  double startTime = 0.0;
};

State g_state;

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
  glClear(GL_COLOR_BUFFER_BIT);

  double elapsed = emscripten_get_now() / 1000.0 - g_state.startTime;
  glUseProgram(g_state.program);
  glUniform1f(g_state.angleLoc, static_cast<float>(elapsed * 0.6));
  float aspect = height > 0 ? static_cast<float>(width) / height : 1.0f;
  glUniform2f(g_state.aspectLoc, aspect > 1.0f ? 1.0f / aspect : 1.0f,
              aspect > 1.0f ? 1.0f : aspect);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}

}  // namespace

int main() {
  EmscriptenWebGLContextAttributes attrs;
  emscripten_webgl_init_context_attributes(&attrs);
  attrs.majorVersion = 2;
  attrs.minorVersion = 0;
  attrs.antialias = true;

  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx =
      emscripten_webgl_create_context("#canvas", &attrs);
  if (ctx <= 0) {
    std::printf("failed to create WebGL2 context (%d)\n",
                static_cast<int>(ctx));
    return 1;
  }
  emscripten_webgl_make_context_current(ctx);

  std::printf("wrdl up: GL_VERSION = %s\n", glGetString(GL_VERSION));

  g_state.program = linkProgram(kVertexShader, kFragmentShader);
  g_state.angleLoc = glGetUniformLocation(g_state.program, "u_angle");
  g_state.aspectLoc = glGetUniformLocation(g_state.program, "u_aspect");
  g_state.startTime = emscripten_get_now() / 1000.0;

  // Interleaved position (xy) + color (rgb), one vertex per line.
  constexpr float vertices[] = {
      0.0f,  0.62f,  0.96f, 0.65f, 0.14f,  // top: orange
      -0.6f, -0.44f, 0.24f, 0.66f, 0.85f,  // left: cyan
      0.6f,  -0.44f, 0.55f, 0.83f, 0.30f,  // right: green
  };
  GLuint vao = 0, vbo = 0;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        reinterpret_cast<void*>(2 * sizeof(float)));

  emscripten_set_main_loop(frame, 0, /*simulate_infinite_loop=*/false);
  return 0;
}
