#pragma once

#include <GLES3/gl3.h>

namespace words {

// Compiles vs/fs and links them; logs and returns 0 on failure.
GLuint linkProgram(const char* vs, const char* fs);

}  // namespace words
