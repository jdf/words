#pragma once

#include <string>

#include "scene.h"
#include "word.h"

namespace words {

// Serializes the scene's geometry — word outlines (filled, even-odd), root
// bounding boxes, and the shape prop — as a standalone SVG document in
// scene coordinates. Float formatting is fixed-precision so output is
// byte-stable for golden-file comparison, and the result is a viewable
// image in its own right.
std::string toSvg(const Scene& scene);

// Debug rendering of one word's hierarchical bounding box: the filled
// outlines with every HBB box stroked on top (stroke thins with depth, ink
// leaves tinted). Viewable — this is how you eyeball construction quality.
std::string hbbDebugSvg(const Word& word);

}  // namespace words
