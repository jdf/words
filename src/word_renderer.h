#pragma once

#include <GLES3/gl3.h>

#include <vector>

#include "scene.h"

namespace words {

// Draws a Scene's words with WebGL2: each word's local outline geometry is
// stencil-filled (two-pass even-odd) at its current position.
//
// The GPU draws exactly the polygons the collision code computes with —
// word-local geometry uploaded once at init, positioned per frame by a
// translate-only transform. Requires a current GL context with a stencil
// buffer.
class WordRenderer {
 public:
  // Uploads per-word buffers. Call after the scene's words are built;
  // calling again (e.g. after a relayout) releases the previous scene's
  // GPU objects first.
  bool init(const Scene& scene);

  // Releases all GPU objects. Safe to call repeatedly.
  void destroy();

  // Draws all words, then hit boxes. `width`/`height` are the framebuffer
  // size in device pixels.
  void draw(const Scene& scene, int width, int height);

 private:
  struct Contour {
    GLint first = 0;
    GLsizei count = 0;
  };
  struct PerWord {
    GLuint fillVao = 0;  // flattened contours, word-local coordinates
    GLuint quadVao = 0;  // 8 verts: cover quad strip [0,4), box loop [4,8)
    std::vector<Contour> contours;
  };

  GLuint program_ = 0;
  GLint matLoc_ = -1;
  GLint offsetLoc_ = -1;
  GLint colorLoc_ = -1;
  std::vector<PerWord> words_;
  std::vector<GLuint> buffers_;  // every VBO, for destroy()
};

}  // namespace words
