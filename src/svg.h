#pragma once

#include <string>

#include "layout.h"
#include "scene.h"
#include "word.h"

namespace words {

// Serializes the scene's geometry — word outlines (filled, even-odd) —
// as a standalone SVG document in scene coordinates. Float formatting is
// fixed-precision so output is byte-stable for golden-file comparison,
// and the result is a viewable image in its own right (it is also the
// source for every user-facing export). `background` false omits the
// backdrop rect for a transparent export.
std::string toSvg(const Scene& scene, bool background = true);

// toSvg plus a debug overlay: the traced word's search spiral as a blue
// polyline, with a hollow circle at the start (scene center) and a solid
// dot where the word came to rest.
std::string toSvg(const Scene& scene, const LayoutDebug& debug);

// Debug rendering of one word's hierarchical bounding box: the filled
// outlines with every HBB box stroked on top (stroke thins with depth, ink
// leaves tinted). Viewable — this is how you eyeball construction quality.
std::string hbbDebugSvg(const Word& word);

}  // namespace words
