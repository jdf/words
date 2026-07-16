#include "gl_util.h"

#include <GLES3/gl3.h>
#include <cstdio>

namespace words {

namespace {

GLuint compileShader(GLenum type, const char* source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(shader, sizeof log, nullptr, log);
    std::printf("shader compile failed: %s\n", log);
  }
  return shader;
}

}  // namespace

GLuint linkProgram(const char* vs, const char* fs) {
  GLuint program = glCreateProgram();
  glAttachShader(program, compileShader(GL_VERTEX_SHADER, vs));
  glAttachShader(program, compileShader(GL_FRAGMENT_SHADER, fs));
  glLinkProgram(program);
  GLint ok = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetProgramInfoLog(program, sizeof log, nullptr, log);
    std::printf("program link failed: %s\n", log);
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

}  // namespace words
