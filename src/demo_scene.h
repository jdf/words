#pragma once

#include <string>

#include "scene.h"

namespace words {

// Builds the demo world: a word cloud of project-flavored vocabulary,
// weighted, colored from a small palette, mixed horizontal/vertical, laid
// out by the spiral+quadtree engine — plus the rotating-triangle prop that
// exercises the collision machinery. Deterministic for a fixed font.
Scene buildCloudScene(const std::string& fontPath);

}  // namespace words
