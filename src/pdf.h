#pragma once

#include <string>

#include "scene.h"

namespace words {

// Serializes the scene as a single-page vector PDF: the same outline
// geometry the SVG export uses, drawn as PDF path operators (even-odd
// fill, like the renderer), with the scene background as a full-page
// rect. The page is `pointWidth` points wide and preserves the scene's
// aspect ratio. Content is Flate-compressed; the result is binary bytes
// (may contain NULs).
std::string toPdf(const Scene& scene, double pointWidth);

}  // namespace words
