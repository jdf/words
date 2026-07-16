#include "word_renderer.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "box.h"
#include "gl_util.h"
#include "scene.h"
#include "word.h"

namespace words {

namespace {

constexpr const char* kVS = R"(#version 300 es
uniform mat2 u_mat;
uniform vec2 u_offset;
layout(location = 0) in vec2 a_position;
void main() {
  gl_Position = vec4(u_mat * a_position + u_offset, 0.0, 1.0);
}
)";

constexpr const char* kFS = R"(#version 300 es
precision mediump float;
uniform vec4 u_color;
out vec4 frag;
void main() {
  frag = u_color;
}
)";

}  // namespace

bool WordRenderer::init(const Scene& scene) {
  program_ = linkProgram(kVS, kFS);
  if (!program_) return false;
  matLoc_ = glGetUniformLocation(program_, "u_mat");
  offsetLoc_ = glGetUniformLocation(program_, "u_offset");
  colorLoc_ = glGetUniformLocation(program_, "u_color");

  words_.clear();
  for (const Scene::Entry& e : scene.entries()) {
    PerWord pw;

    std::vector<float> data;
    for (const auto& path : e.word.localPaths()) {
      Contour c;
      c.first = static_cast<GLint>(data.size() / 2);
      c.count = static_cast<GLsizei>(path.size());
      pw.contours.push_back(c);
      for (const auto& p : path) {
        data.push_back(static_cast<float>(p.x));
        data.push_back(static_cast<float>(p.y));
      }
    }
    if (data.empty()) continue;

    glGenVertexArrays(1, &pw.fillVao);
    glBindVertexArray(pw.fillVao);
    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    const Box& b = e.word.localBounds();
    const float quad[] = {
        // [0,4): cover quad as a triangle strip
        static_cast<float>(b.minX), static_cast<float>(b.minY),
        static_cast<float>(b.maxX), static_cast<float>(b.minY),
        static_cast<float>(b.minX), static_cast<float>(b.maxY),
        static_cast<float>(b.maxX), static_cast<float>(b.maxY),
        // [4,8): box outline as a line loop
        static_cast<float>(b.minX), static_cast<float>(b.minY),
        static_cast<float>(b.maxX), static_cast<float>(b.minY),
        static_cast<float>(b.maxX), static_cast<float>(b.maxY),
        static_cast<float>(b.minX), static_cast<float>(b.maxY),
    };
    glGenVertexArrays(1, &pw.quadVao);
    glBindVertexArray(pw.quadVao);
    GLuint quadVbo = 0;
    glGenBuffers(1, &quadVbo);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    words_.push_back(std::move(pw));
  }
  return true;
}

void WordRenderer::draw(const Scene& scene, int width, int height) {
  if (words_.empty()) return;
  glUseProgram(program_);

  // Scene px → NDC, uniform on screen; rotation/scale are baked into each
  // word's geometry, so the shared matrix is a pure (anisotropic-in-NDC,
  // shape-preserving-on-screen) scale and per-word state is a translation.
  double s = std::min(width / scene.width(), height / scene.height());
  double ndcX = s * 2.0 / width;
  double ndcY = s * 2.0 / height;
  const float mat[4] = {static_cast<float>(ndcX), 0.0f, 0.0f,
                        static_cast<float>(ndcY)};
  glUniformMatrix2fv(matLoc_, 1, GL_FALSE, mat);

  const auto& entries = scene.entries();
  size_t n = std::min(words_.size(), entries.size());

  glEnable(GL_STENCIL_TEST);
  for (size_t i = 0; i < n; ++i) {
    const Scene::Entry& e = entries[i];
    const PerWord& pw = words_[i];
    glUniform2f(offsetLoc_, static_cast<float>(e.word.x() * ndcX),
                static_cast<float>(e.word.y() * ndcY));

    // Pass 1: even-odd coverage into the stencil buffer, no color writes.
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
    glBindVertexArray(pw.fillVao);
    for (const Contour& c : pw.contours) {
      glDrawArrays(GL_TRIANGLE_FAN, c.first, c.count);
    }

    // Pass 2: paint the cover quad wherever the stencil ended up odd,
    // zeroing consumed bits so overlapping words never see each other.
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_EQUAL, 1, 0x1);
    glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);
    glUniform4f(colorLoc_, e.color.r, e.color.g, e.color.b, 1.0f);
    glBindVertexArray(pw.quadVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  }
  glDisable(GL_STENCIL_TEST);
}

}  // namespace words
