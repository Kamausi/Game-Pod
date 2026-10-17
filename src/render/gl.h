// Thin WebGL2 / GLES3 helpers.
#pragma once
#include <GLES3/gl3.h>
#include <string>
#include "../cad/cad.h"

namespace mb {

struct GpuMesh {
    GLuint vao = 0, vbo = 0, ibo = 0, instVbo = 0;
    GLsizei count = 0;
    Vec3 bmin, bmax;
};

GLuint compileProgram(const char* name, const std::string& vs, const std::string& fs);
// Uploads a CAD mesh. Attribute layout: 0 pos, 1 normal, 2 color, 3-6 instance matrix, 7 tint, 8 material.
GpuMesh uploadMesh(const CadMesh& m);
// Per-instance data: mat4 (16 floats) + tint vec4 (4 floats) = 20 floats.
void setInstances(GpuMesh& m, const float* data, int count, bool dynamic);
void drawMesh(const GpuMesh& m, int instances);

Mat4 inverse(const Mat4& m);

}  // namespace mb
