#pragma once

#include <string>

#include "scene.h"

namespace words {

// Builds the demo world — six renderings of one shaped word at assorted
// sizes/rotations plus the rotating-triangle prop — from the font at
// `fontPath`. Shared by the wasm app and the geometry tests so both
// exercise identical geometry.
Scene buildDemoScene(const std::string& fontPath);

}  // namespace words
