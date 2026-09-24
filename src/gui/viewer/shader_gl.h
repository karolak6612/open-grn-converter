#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <string>

namespace grn {

GLuint compileShader(QOpenGLFunctions_3_3_Core* gl, GLenum type, const char* src);
GLuint linkProgram(QOpenGLFunctions_3_3_Core* gl, const char* vs_src, const char* fs_src);

GLuint createMeshProgram(QOpenGLFunctions_3_3_Core* gl);
GLuint createWireProgram(QOpenGLFunctions_3_3_Core* gl);
GLuint createGridProgram(QOpenGLFunctions_3_3_Core* gl);
GLuint createUIProgram(QOpenGLFunctions_3_3_Core* gl);
GLuint createMatcapTexture(QOpenGLFunctions_3_3_Core* gl);

} // namespace grn
