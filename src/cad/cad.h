// mbcad - a tiny parametric CAD kernel. Part files (*.mbcad) live in the cad/ "repo" and are
// the single source of truth for every in-game asset: the game builds meshes from them at
// load time, and the mbcad CLI exports the same parts to OBJ (Blender) and OpenSCAD (.scad).
//
// Syntax (one feature per line):
//   param tire_d = 0.889          # overridable parameter
//   set r = tire_d / 2            # derived value
//   color 0.1 0.1 0.1             # or: color #1a1a1a
//   box   size=1,0.5,2 pos=0,0.25,0 rot=0,15,0 mirror=x
//   cyl   r=0.2 h=0.3 axis=x seg=24 [r2=0.1]
//   sphere r=0.5 scale=1,0.6,1 seg=16
//   wedge size=1,0.4,0.8
//   lathe pts=r0,y0,r1,y1,... seg=24 axis=y
//   extrude pts=z0,y0,z1,y1,... depth=1.7 axis=x     # side-profile polygon, extruded
//   tire  r=0.44 w=0.32 rim=0.19 lugs=18 depth=0.03 tread=mt
//   rim   r=0.19 w=0.25 spokes=5
//   use   wheels/steel.mbcad pos=... rot=... scale=... key=value ...
//   material paint|glass|chrome|rubber|lens|matte|steel|mud|leaf|plastic   # sticky, like color
// box and extrude take bevel=r (rounded edges, smooth normals). Inside pts=, arc(zc,yc,r,a0,a1,n)
// expands to n+1 points on a circular arc (degrees, 0 = +z, 90 = +y): wheel arches, rounded noses.
// Expressions support + - * / ^ ( ), unary minus, and sin cos sqrt min max abs.
#pragma once
#include <map>
#include <string>
#include <vector>
#include "../core/math.h"

namespace mb {

// Surface materials, carried per vertex so one mesh can mix paint, glass, chrome and rubber.
enum CadMaterial : int { MatPlastic = 0, MatPaint, MatGlass, MatChrome, MatRubber, MatLens, MatMatte, MatSteel, MatMud, MatLeaf };
int cadMaterialId(const std::string& name);  // -1 if unknown

struct CadVertex {
    float px, py, pz, nx, ny, nz, r, g, b;
    float mat;  // CadMaterial
};

struct CadMesh {
    std::vector<CadVertex> verts;
    std::vector<uint32_t> idx;
    void append(const CadMesh& o, const Mat4& m, bool flipWinding = false);
    Vec3 boundsMin() const;
    Vec3 boundsMax() const;
};

using CadParams = std::map<std::string, float>;

struct CadResult {
    CadMesh mesh;
    std::string scad;   // OpenSCAD translation
    std::string error;  // empty on success
};

// Root directory part paths resolve against ("/cad" in the browser, "cad/parts" natively).
void cadSetRoot(const std::string& root);
const std::string& cadRoot();

CadResult cadBuild(const std::string& partPath, const CadParams& overrides = {});
CadResult cadBuildSource(const std::string& source, const CadParams& overrides, const std::string& name);
std::string cadToObj(const CadMesh& m, const std::string& name);

// Expression evaluation (exposed for tests).
float cadEval(const std::string& expr, const CadParams& vars, std::string* err = nullptr);

// Primitive generators (exposed so procedural code can share the kernel).
namespace cadprim {
CadMesh box(Vec3 size, Vec3 color);
CadMesh roundedBox(Vec3 size, float bevel, int seg, Vec3 color);
CadMesh extrudeBevel(const std::vector<Vec2>& poly, float depth, float bevel, int seg, Vec3 color);
CadMesh cylinder(float r, float r2, float h, int seg, Vec3 color);  // along +Y, centered
CadMesh sphere(float r, int seg, Vec3 color);
CadMesh lathe(const std::vector<Vec2>& profile, int seg, Vec3 color);  // profile (radius, y)
CadMesh extrude(const std::vector<Vec2>& poly, float depth, Vec3 color);  // poly in (z,y), along X
CadMesh wedge(Vec3 size, Vec3 color);
// Open inward-facing partial cylinder along X (wheelhouse liner), angles in degrees from +Z toward +Y.
CadMesh liner(float r, float w, float a0, float a1, int seg, Vec3 color, float rb = 0, float pe = 2);  // rb/pe: superellipse
// Smooth tube swept along a 3D path (parallel-transport frames, capped ends).
CadMesh tube(const std::vector<Vec3>& path, float r, int seg, Vec3 color);
// Smooth lofted shell through half-profiles (x >= 0, bottom centre round to top centre, mirrored
// to close) at increasing z stations; Catmull-Rom between stations (n) and round the section (m).
CadMesh loft(const std::vector<float>& z, const std::vector<std::vector<Vec2>>& half, int n, int m, bool caps, Vec3 color,
             bool full = false);   // full: sections are complete closed loops (no mirroring)
CadMesh tire(float r, float w, float rim, int lugs, float depth, int tread, Vec3 color);  // axle along X
CadMesh rim(float r, float w, int spokes, Vec3 color);
}  // namespace cadprim

}  // namespace mb
