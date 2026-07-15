#pragma once

#include <string>

#include "scene.h"

namespace words {

// Serializes the scene's geometry — word outlines (filled, even-odd), root
// bounding boxes, and the shape prop — as a standalone SVG document in
// scene coordinates. Float formatting is fixed-precision so output is
// byte-stable for golden-file comparison, and the result is a viewable
// image in its own right.
std::string toSvg(const Scene& scene);

}  // namespace words
