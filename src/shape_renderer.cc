#include "shape_renderer.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "gl_util.h"
#include "scene.h"
#include "shape.h"

namespace words {

namespace {

constexpr const char* kVS = R"(#version 300 es
uniform float u_angle;
uniform vec2 u_scene;
uniform vec2 u_offset;
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec3 a_color;
out vec3 v_color;
void main() {
  float c = cos(u_angle), s = sin(u_angle);
  vec2 p = mat2(c, s, -s, c) * a_position + u_offset;
  gl_Position = vec4(p * u_scene, 0.0, 1.0);
  v_color = a_color;
}
)";

constexpr const char* kFS = R"(#version 300 es
precision mediump float;
in vec3 v_color;
out vec4 frag;
void main() {
  frag = vec4(v_color, 1.0);
}
)";

}  // namespace

bool ShapeRenderer::init(const Shape& shape, const std::vector<Color>& colors) {
  program_ = linkProgram(kVS, kFS);
  if (!program_) return false;
  angleLoc_ = glGetUniformLocation(program_, "u_angle");
  sceneLoc_ = glGetUniformLocation(program_, "u_scene");
  offsetLoc_ = glGetUniformLocation(program_, "u_offset");

  const auto& base = shape.basePath();
  vertexCount_ = static_cast<GLsizei>(base.size());

  // Interleaved position (xy, scene px) + color (rgb).
  std::vector<float> data;
  data.reserve(base.size() * 5);
  for (size_t i = 0; i < base.size(); ++i) {
    Color color = i < colors.size() ? colors[i] : Color{};
    data.push_back(static_cast<float>(base[i].x));
    data.push_back(static_cast<float>(base[i].y));
    data.push_back(color.r);
    data.push_back(color.g);
    data.push_back(color.b);
  }

  GLuint vbo = 0;
  glGenVertexArrays(1, &vao_);
  glBindVertexArray(vao_);
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(),
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        reinterpret_cast<void*>(2 * sizeof(float)));
  return true;
}

void ShapeRenderer::draw(const Shape& shape, int width, int height) {
  if (!vertexCount_) return;
  glUseProgram(program_);
  glUniform1f(angleLoc_, static_cast<float>(shape.angle()));
  glUniform2f(offsetLoc_, static_cast<float>(shape.x()),
              static_cast<float>(shape.y()));
  // Rotation happens in isotropic scene pixels; the per-axis NDC mapping
  // afterward is shape-preserving on screen.
  double s = std::min(width / Scene::kWidth, height / Scene::kHeight);
  glUniform2f(sceneLoc_, static_cast<float>(s * 2.0 / width),
              static_cast<float>(s * 2.0 / height));
  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLE_FAN, 0, vertexCount_);
}

}  // namespace words
