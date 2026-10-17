// Forward renderer: sky, skirted-LOD terrain chunks displaced from vertex textures (height +
// water + mud state), 2048^2 PCF sun shadow map, instanced CAD props, dynamic barrels /
// leaning fence posts / procedural orange safety fence, mud-caked vehicle shading, water
// surface and particles. ACES tonemapping + fog in-shader.
#pragma once
#include <map>
#include <string>
#include <vector>
#include "../game/particles.h"
#include "../game/world.h"
#include "gl.h"

namespace mb {

struct DrawItem {
    const GpuMesh* mesh = nullptr;
    Mat4 model;
    float dirt = 0;
    float dirtLo = -0.5f, dirtHi = 1.2f;  // local-space Y range the mud line sweeps over
    float alpha = 1;
    float seed = 0;
    bool mirror = false;
};

struct RenderFrame {
    Vec3 camPos, camTarget;
    float fov = 1.0f;
    float time = 0;
    std::vector<DrawItem> items;
    const std::vector<Particle>* particles = nullptr;
    // Lens splats in screen space: x, y (0..1), radius, opacity (>0 mud, <0 water drop).
    std::vector<float> lens;
};

class Renderer {
public:
    bool init(int w, int h);
    void resize(int w, int h);
    void loadWorld(World& w);
    const GpuMesh* mesh(const std::string& part, const CadParams& params = {});
    void render(World& w, const RenderFrame& f);
    int width = 1280, height = 720;
    int quality = 2;  // 0 low, 1 medium, 2 high
    bool postEnabled = true;
    Vec3 sunDirection() const { return sunDir; }
    void setSun(float azimuthDeg, float elevationDeg);
    Mat4 viewProj;

private:
    void uploadTerrain(Terrain& t, bool full);
    void drawTerrain(bool water, const Mat4& vp);
    void drawStatic(GLuint prog);
    void drawDynamic(World& w, const RenderFrame& f, GLuint prog, bool shadowPass, bool transparent = false);
    void buildFence(World& w);
    void ensureTargets();
    void postPass(const RenderFrame& f);
    GLuint progPost = 0, msFbo = 0, msColor = 0, msDepth = 0, resolveFbo = 0, resolveTex = 0, postVao = 0;
    int targetW = 0, targetH = 0;

    GLuint progTerrain = 0, progMesh = 0, progShadow = 0, progSky = 0, progParticle = 0, progFence = 0;
    GLuint texHeight = 0, texState = 0, shadowTex = 0, shadowFbo = 0;
    GLuint chunkVao = 0, chunkVbo = 0, chunkIbo = 0, skyVao = 0;
    struct Lod { GLuint offset; GLsizei count; };
    Lod lods[3];
    std::vector<float> chunkMin, chunkMax;
    std::vector<bool> chunkWater;
    GLuint particleVao = 0, particleQuad = 0, particleInst = 0;
    GLuint fenceVao = 0, fenceVbo = 0;
    GLsizei fenceVerts = 0;
    std::map<std::string, GpuMesh> meshes;
    struct Batch { GpuMesh mesh; int count = 0; };
    std::vector<Batch> batches;
    const GpuMesh *barrelMesh[2] = {nullptr, nullptr}, *postMesh = nullptr;
    GpuMesh barrelInst[2], postInst;
    Mat4 lightVP;
    Vec3 sunDir;
    std::vector<float> scratch;
    std::vector<uint8_t> scratch8;
};

}  // namespace mb
