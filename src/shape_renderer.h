#pragma once

#include <GLES3/gl3.h>

#include <vector>

#include "scene.h"
#include "shape.h"

namespace words {

// Draws a Shape with WebGL2, one color per base vertex (interpolated across
// the polygon). Rotation and translation are applied in the vertex shader in
// isotropic scene pixels, then mapped to NDC, so the shape never distorts.
class ShapeRenderer {
 public:
  // Uploads the shape's base polygon. `colors` must have one entry per
  // base-path vertex. Requires a current GL context.
  bool init(const Shape& shape, const std::vector<Color>& colors);

  void draw(const Shape& shape, int width, int height);

 private:
  GLuint program_ = 0;
  GLint angleLoc_ = -1;
  GLint sceneLoc_ = -1;
  GLint offsetLoc_ = -1;
  GLuint vao_ = 0;
  GLsizei vertexCount_ = 0;
};

}  // namespace words
