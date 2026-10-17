#include "renderer.h"
#include <cstdio>
#include <cstring>

namespace mb {

namespace {
constexpr int CH = 64;            // cells per terrain chunk
constexpr int NCH = Terrain::N / CH;
constexpr int SHADOW = 2048;

const char* kHeader = R"(#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
precision highp sampler2DShadow;
)";

const char* kCommon = R"(
uniform vec3 uSunDir;
uniform vec3 uCamPos;
uniform float uTime;
uniform sampler2DShadow uShadow;
uniform mat4 uLightVP;
float hash(vec2 p){ p = fract(p*vec2(123.34,456.21)); p += dot(p,p+45.32); return fract(p.x*p.y); }
float vnoise(vec2 p){ vec2 i=floor(p), f=fract(p); vec2 u=f*f*(3.0-2.0*f);
  return mix(mix(hash(i),hash(i+vec2(1,0)),u.x), mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),u.x), u.y); }
float fbm(vec2 p){ float s=0.0, a=0.5; for(int i=0;i<4;i++){ s+=a*vnoise(p); p*=2.03; a*=0.5; } return s; }
float shadowAt(vec3 wp, vec3 n){
  vec4 lp = uLightVP*vec4(wp + n*0.06, 1.0);
  vec3 c = lp.xyz/lp.w*0.5+0.5;
  if(c.x<0.0||c.x>1.0||c.y<0.0||c.y>1.0||c.z>1.0) return 1.0;
  c.z -= 0.0012;
  float s = 0.0; vec2 t = vec2(1.0/2048.0);
  for(int x=-1;x<=1;x++) for(int y=-1;y<=1;y++) s += texture(uShadow, vec3(c.xy+vec2(x,y)*t*1.5, c.z));
  return s/9.0;
}
vec3 skyColor(vec3 d){
  float t = clamp(d.y, 0.0, 1.0);
  vec3 zen = vec3(0.04,0.20,0.72), hor = vec3(0.36,0.56,0.90);
  vec3 c = mix(hor, zen, pow(t, 0.45));
  float s = max(dot(d, uSunDir), 0.0);
  c += vec3(1.0,0.92,0.75)*pow(s, 900.0)*20.0 + vec3(1.0,0.85,0.6)*pow(s, 6.0)*0.12;
  if(d.y < 0.0) c = mix(hor, vec3(0.35,0.33,0.28), clamp(-d.y*4.0, 0.0, 1.0));
  return c;
}
vec3 shade(vec3 alb, vec3 n, vec3 wp, float wet, float sh){
  vec3 L = uSunDir; float ndl = max(dot(n, L), 0.0);
  vec3 V = normalize(uCamPos - wp); vec3 H = normalize(L + V);
  float spec = pow(max(dot(n, H), 0.0), mix(12.0, 140.0, wet)) * mix(0.03, 0.9, wet);
  vec3 sun = vec3(1.0, 0.93, 0.82) * 3.1;
  vec3 amb = mix(vec3(0.32,0.27,0.20), vec3(0.40,0.55,0.85), n.y*0.5+0.5) * 0.62;
  return alb*(sun*ndl*sh + amb) + sun*spec*sh*ndl;
}
vec3 finalize(vec3 c, vec3 wp){
  float d = length(wp - uCamPos);
  float f = 1.0 - exp(-d*0.0022);
  vec3 fc = skyColor(normalize(vec3(wp.x-uCamPos.x, 0.03, wp.z-uCamPos.z)));
  c = mix(c, fc, f*0.65);
  c = c*(2.51*c+0.03)/(c*(2.43*c+0.59)+0.14);
  return pow(clamp(c, 0.0, 1.0), vec3(1.0/2.2));
}
)";

const char* kTerrainVS = R"(
layout(location=0) in vec3 aGrid;
uniform ivec2 uChunk;
uniform sampler2D uHeight;
uniform sampler2D uState;
uniform vec2 uOrigin;
uniform float uCell;
uniform mat4 uVP;
uniform int uWaterPass;
out vec3 vWorld; out vec3 vN; out vec4 vState; out float vDepth;
float H(ivec2 c){ c = clamp(c, ivec2(0), ivec2(767)); return texelFetch(uHeight, c, 0).r; }
void main(){
  ivec2 c = clamp(uChunk + ivec2(aGrid.xz), ivec2(0), ivec2(767));
  vec2 hw = texelFetch(uHeight, c, 0).rg;
  float h = hw.r;
  vN = normalize(vec3(H(c-ivec2(1,0)) - H(c+ivec2(1,0)), 2.0*uCell, H(c-ivec2(0,1)) - H(c+ivec2(0,1))));
  vState = texelFetch(uState, c, 0);
  float y = h - aGrid.y*0.6;
  vDepth = hw.g - h;
  if(uWaterPass == 1){ y = hw.g > -900.0 ? hw.g : h - 0.05; vDepth = hw.g > -900.0 ? hw.g - h : -1.0; }
  vWorld = vec3(uOrigin.x + float(c.x)*uCell, y, uOrigin.y + float(c.y)*uCell);
  gl_Position = uVP*vec4(vWorld, 1.0);
}
)";

const char* kTerrainFS = R"(
in vec3 vWorld; in vec3 vN; in vec4 vState; in float vDepth;
uniform int uWaterPass;
out vec4 frag;
void main(){
  vec3 n = normalize(vN);
  if(uWaterPass == 1){
    if(vDepth < 0.004) discard;
    vec2 q = vWorld.xz*1.3;
    vec3 wn = normalize(vec3((vnoise(q+uTime*0.6)-0.5)*0.25, 1.0, (vnoise(q.yx-uTime*0.5)-0.5)*0.25));
    vec3 V = normalize(uCamPos - vWorld);
    float fres = 0.04 + 0.96*pow(1.0 - max(dot(wn, V), 0.0), 5.0);
    vec3 refl = skyColor(reflect(-V, wn));
    vec3 body = pow(vec3(0.33,0.27,0.19), vec3(2.2)) * (0.85 + 0.3*vnoise(q*0.3));
    float sh = shadowAt(vWorld, wn);
    vec3 c = body*(0.6 + 1.5*sh);
    c = mix(c, refl*0.7, fres*0.45);
    c += vec3(1.0,0.9,0.75)*pow(max(dot(reflect(-uSunDir, wn), V), 0.0), 400.0)*2.0*sh;
    float a = clamp(0.62 + vDepth*1.8, 0.62, 0.95);
    frag = vec4(finalize(c, vWorld), a);
    return;
  }
  float moist = vState.r, mud = vState.g, grass = vState.b, track = vState.a;
  vec2 p = vWorld.xz;
  float n1 = fbm(p*0.18), n2 = fbm(p*0.9+7.0), n3 = vnoise(p*6.0);
  vec3 gA = vec3(0.25,0.33,0.14), gB = vec3(0.47,0.45,0.27), gold = vec3(0.60,0.53,0.22);
  vec3 grassC = mix(gA, gB, smoothstep(0.35, 0.7, n1));
  grassC = mix(grassC, gold, smoothstep(0.72, 0.86, n2)*0.45);
  grassC *= 0.85 + 0.3*n3;
  vec3 dirt = vec3(0.47,0.40,0.30)*(0.9+0.2*n3);
  vec3 mudDry = vec3(0.40,0.33,0.24), mudWet = vec3(0.19,0.145,0.10);
  float wetness = smoothstep(0.35, 0.95, moist);
  vec3 mudC = mix(mudDry, mudWet, wetness)*(0.88 + 0.24*n2);
  float mudAmt = smoothstep(0.0, 0.05, mud)*smoothstep(0.25, 0.55, moist);
  vec3 soil = mix(dirt, mudC, max(mudAmt, wetness*0.6));
  vec3 alb = mix(soil, grassC, smoothstep(0.1, 0.8, grass));
  alb *= 1.0 - 0.35*track*(1.0 - grass);
  alb = pow(alb, vec3(2.2));
  float wet = mix(0.0, 0.75, wetness*(1.0-grass)) + 0.2*track*wetness;
  float sh = shadowAt(vWorld, n);
  frag = vec4(finalize(shade(alb, n, vWorld, wet, sh), vWorld), 1.0);
}
)";

const char* kMeshVS = R"(
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNormal; layout(location=2) in vec3 aColor;
layout(location=3) in vec4 iM0; layout(location=4) in vec4 iM1; layout(location=5) in vec4 iM2; layout(location=6) in vec4 iM3;
layout(location=7) in vec4 iTint;
uniform mat4 uModel; uniform mat4 uVP;
out vec3 vWorld; out vec3 vN; out vec3 vColor; out vec3 vLocal;
void main(){
  mat4 M = uModel*mat4(iM0, iM1, iM2, iM3);
  vec4 w = M*vec4(aPos, 1.0);
  vWorld = w.xyz; vN = mat3(M)*aNormal; vColor = aColor*iTint.rgb; vLocal = aPos;
  gl_Position = uVP*w;
}
)";

const char* kMeshFS = R"(
in vec3 vWorld; in vec3 vN; in vec3 vColor; in vec3 vLocal;
uniform float uDirt; uniform vec2 uDirtRange; uniform float uAlpha; uniform float uSeed; uniform int uMirror;
out vec4 frag;
void main(){
  vec3 alb = pow(clamp(vColor, 0.0, 1.0), vec3(2.2));
  float wet = 0.08;
  if(uDirt > 0.001){
    float line = mix(uDirtRange.x, uDirtRange.y, uDirt);
    float n = fbm(vLocal.xz*2.6 + vLocal.y*1.7 + uSeed);
    float m = smoothstep(line + 0.12, line - 0.12, vLocal.y + (n - 0.5)*0.7);
    float spl = step(0.80 - uDirt*0.32, fbm(vLocal.xz*8.0 + vLocal.yy*6.0 + uSeed*3.0));
    m = max(m, spl*min(1.0, uDirt*1.3));
    vec3 mudc = pow(vec3(0.37,0.29,0.20)*(0.7 + 0.5*n), vec3(2.2));
    alb = mix(alb, mudc, m);
    wet = mix(wet, 0.5, m*(0.5 + 0.5*n));
  }
  vec3 n = normalize(vN);
  if(gl_FrontFacing == (uMirror == 1)) n = -n;
  float sh = shadowAt(vWorld, n);
  frag = vec4(finalize(shade(alb, n, vWorld, wet, sh), vWorld), uAlpha);
}
)";

const char* kShadowFS = R"(
out vec4 frag;
void main(){ frag = vec4(1.0); }
)";

const char* kSkyVS = R"(
out vec2 vNdc;
void main(){ vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2)*2.0 - 1.0; vNdc = p; gl_Position = vec4(p, 0.9999, 1.0); }
)";
const char* kSkyFS = R"(
in vec2 vNdc; uniform mat4 uInvVP; out vec4 frag;
void main(){
  vec4 a = uInvVP*vec4(vNdc, -1.0, 1.0), b = uInvVP*vec4(vNdc, 1.0, 1.0);
  vec3 d = normalize(b.xyz/b.w - a.xyz/a.w);
  vec3 c = skyColor(d);
  c = c*(2.51*c+0.03)/(c*(2.43*c+0.59)+0.14);
  frag = vec4(pow(clamp(c,0.0,1.0), vec3(1.0/2.2)), 1.0);
}
)";

const char* kParticleVS = R"(
layout(location=0) in vec2 aCorner;
layout(location=1) in vec4 iPos;
layout(location=2) in vec4 iCol;
layout(location=3) in float iType;
uniform mat4 uVP; uniform vec3 uRight; uniform vec3 uUp;
out vec2 vUV; out vec4 vCol; out float vType; out vec3 vWorld; out float vSeed;
void main(){
  vec3 p = iPos.xyz + (uRight*aCorner.x + uUp*aCorner.y)*iPos.w;
  vUV = aCorner; vCol = iCol; vType = iType; vWorld = p; vSeed = fract(iPos.x*7.13 + iPos.z*3.71);
  gl_Position = uVP*vec4(p, 1.0);
}
)";
const char* kParticleFS = R"(
in vec2 vUV; in vec4 vCol; in float vType; in vec3 vWorld; in float vSeed;
uniform int uSoft; out vec4 frag;
void main(){
  float d = length(vUV);
  bool soft = vType > 2.5 && vType < 4.5;   // mist, smoke
  if((uSoft == 1) != soft) discard;
  float a = vCol.a;
  if(soft){ a *= smoothstep(1.0, 0.2, d); if(a < 0.01) discard; }
  else {
    float edge = 0.55 + 0.45*vnoise(vUV*2.5 + vSeed*17.0);
    if(d > edge) discard;
  }
  vec3 alb = pow(vCol.rgb, vec3(2.2));
  vec3 n = normalize(vec3(vUV*0.6, 1.0));
  vec3 c = alb*(vec3(1.0,0.93,0.82)*2.2*(0.5+0.5*max(dot(uSunDir, vec3(0,1,0)),0.0)) + vec3(0.35,0.45,0.65)*0.6);
  if(vType > 1.5 && vType < 2.5) c += vec3(0.25)*pow(max(0.0, 1.0-d), 3.0);  // water sparkle
  frag = vec4(finalize(c, vWorld), soft ? a : 1.0);
}
)";

const char* kFenceVS = R"(
layout(location=0) in vec3 aPos; layout(location=1) in vec2 aUV; layout(location=2) in vec3 aNormal;
uniform mat4 uVP; out vec3 vWorld; out vec2 vUV; out vec3 vN;
void main(){ vWorld = aPos; vUV = aUV; vN = aNormal; gl_Position = uVP*vec4(aPos, 1.0); }
)";
const char* kFenceFS = R"(
in vec3 vWorld; in vec2 vUV; in vec3 vN; uniform int uShadowPass; out vec4 frag;
void main(){
  vec2 cell = vec2(vUV.x/0.105, vUV.y/0.05);
  vec2 f = fract(cell + vec2(floor(cell.y)*0.5, 0.0)) - 0.5;
  if((f.x*f.x)/(0.36*0.36) + (f.y*f.y)/(0.29*0.29) < 1.0) discard;
  if(vUV.y < 0.0 || vUV.y > 1.12) discard;
  if(uShadowPass == 1){ frag = vec4(1.0); return; }
  vec3 n = normalize(vN); if(!gl_FrontFacing) n = -n;
  vec3 alb = pow(vec3(1.0, 0.36, 0.06), vec3(2.2));
  float sh = shadowAt(vWorld, n);
  frag = vec4(finalize(shade(alb, n, vWorld, 0.15, sh)*0.9 + alb*0.25, vWorld), 1.0);
}
)";

std::string src(const char* body) { return std::string(kHeader) + kCommon + body; }

void extractPlanes(const Mat4& m, float planes[6][4]) {
    for (int i = 0; i < 4; i++) {
        float r3 = m.at(3, i);
        planes[0][i] = r3 + m.at(0, i);
        planes[1][i] = r3 - m.at(0, i);
        planes[2][i] = r3 + m.at(1, i);
        planes[3][i] = r3 - m.at(1, i);
        planes[4][i] = r3 + m.at(2, i);
        planes[5][i] = r3 - m.at(2, i);
    }
}
bool aabbVisible(const float pl[6][4], Vec3 mn, Vec3 mx) {
    for (int p = 0; p < 6; p++) {
        Vec3 v{pl[p][0] >= 0 ? mx.x : mn.x, pl[p][1] >= 0 ? mx.y : mn.y, pl[p][2] >= 0 ? mx.z : mn.z};
        if (pl[p][0] * v.x + pl[p][1] * v.y + pl[p][2] * v.z + pl[p][3] < 0) return false;
    }
    return true;
}
}  // namespace

bool Renderer::init(int w, int h) {
    width = w;
    height = h;
    progTerrain = compileProgram("terrain", src(kTerrainVS), src(kTerrainFS));
    progMesh = compileProgram("mesh", src(kMeshVS), src(kMeshFS));
    progShadow = compileProgram("shadow", src(kMeshVS), std::string(kHeader) + kShadowFS);
    progSky = compileProgram("sky", std::string(kHeader) + kSkyVS, src(kSkyFS));
    progParticle = compileProgram("particle", src(kParticleVS), src(kParticleFS));
    progFence = compileProgram("fence", src(kFenceVS), src(kFenceFS));
    glGenVertexArrays(1, &skyVao);

    // Terrain chunk grid shared by all chunks: 65x65 + skirt rings.
    std::vector<float> verts;
    auto addV = [&](float i, float skirt, float j) { verts.push_back(i); verts.push_back(skirt); verts.push_back(j); };
    for (int j = 0; j <= CH; j++)
        for (int i = 0; i <= CH; i++) addV(float(i), 0, float(j));
    const uint32_t S0 = (CH + 1) * (CH + 1), S1 = S0 + CH + 1, S2 = S1 + CH + 1, S3 = S2 + CH + 1;
    for (int i = 0; i <= CH; i++) addV(float(i), 1, 0);
    for (int i = 0; i <= CH; i++) addV(float(i), 1, float(CH));
    for (int j = 0; j <= CH; j++) addV(0, 1, float(j));
    for (int j = 0; j <= CH; j++) addV(float(CH), 1, float(j));
    auto V = [](int i, int j) { return uint32_t(j * (CH + 1) + i); };
    std::vector<uint32_t> idx;
    const int steps[3] = {1, 2, 4};
    for (int l = 0; l < 3; l++) {
        int s = steps[l];
        lods[l].offset = GLuint(idx.size() * 4);
        for (int j = 0; j < CH; j += s)
            for (int i = 0; i < CH; i += s) {
                uint32_t a = V(i, j), b = V(i + s, j), c = V(i, j + s), d = V(i + s, j + s);
                idx.insert(idx.end(), {a, c, b, b, c, d});
            }
        for (int k = 0; k < CH; k += s) {
            idx.insert(idx.end(), {V(k, 0), V(k + s, 0), S0 + k, V(k + s, 0), S0 + k + s, S0 + k});
            idx.insert(idx.end(), {V(k, CH), S1 + k, V(k + s, CH), V(k + s, CH), S1 + k, S1 + k + s});
            idx.insert(idx.end(), {V(0, k), S2 + k, V(0, k + s), V(0, k + s), S2 + k, S2 + k + s});
            idx.insert(idx.end(), {V(CH, k), V(CH, k + s), S3 + k, V(CH, k + s), S3 + k + s, S3 + k});
        }
        lods[l].count = GLsizei(idx.size() - lods[l].offset / 4);
    }
    glGenVertexArrays(1, &chunkVao);
    glBindVertexArray(chunkVao);
    glGenBuffers(1, &chunkVbo);
    glBindBuffer(GL_ARRAY_BUFFER, chunkVbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * 4), verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
    glGenBuffers(1, &chunkIbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunkIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx.size() * 4), idx.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);

    // Particles: unit quad + per-instance stream.
    glGenVertexArrays(1, &particleVao);
    glBindVertexArray(particleVao);
    const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glGenBuffers(1, &particleQuad);
    glBindBuffer(GL_ARRAY_BUFFER, particleQuad);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
    glGenBuffers(1, &particleInst);
    glBindBuffer(GL_ARRAY_BUFFER, particleInst);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 36, (void*)0);
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 36, (void*)16);
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 36, (void*)32);
    glVertexAttribDivisor(3, 1);
    glBindVertexArray(0);

    glGenVertexArrays(1, &fenceVao);
    glBindVertexArray(fenceVao);
    glGenBuffers(1, &fenceVbo);
    glBindBuffer(GL_ARRAY_BUFFER, fenceVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 32, (void*)12);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 32, (void*)20);
    glBindVertexArray(0);

    // Shadow map
    glGenTextures(1, &shadowTex);
    glBindTexture(GL_TEXTURE_2D, shadowTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW, SHADOW, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &shadowFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex, 0);
    GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // 1x1 dummy depth texture on unit 3 so shadow samplers are always complete.
    GLuint dummy;
    glGenTextures(1, &dummy);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, dummy);
    unsigned one = 0xFFFFFFFFu;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 1, 1, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, &one);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glActiveTexture(GL_TEXTURE0);

    sunDir = normalize(Vec3{-0.45f, 0.62f, -0.55f});  // mid-afternoon October sun from the south-west
    return true;
}

void Renderer::resize(int w, int h) { width = w; height = h; }

const GpuMesh* Renderer::mesh(const std::string& part, const CadParams& params) {
    std::string key = part;
    char buf[48];
    for (auto& kv : params) { std::snprintf(buf, sizeof buf, ";%s=%.3f", kv.first.c_str(), kv.second); key += buf; }
    auto it = meshes.find(key);
    if (it != meshes.end()) return &it->second;
    CadResult r = cadBuild(part, params);
    if (!r.error.empty()) std::printf("[cad] %s\n", r.error.c_str());
    GpuMesh g = uploadMesh(r.mesh);
    float ident[20] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1};
    setInstances(g, ident, 1, false);
    return &(meshes[key] = g);
}

void Renderer::uploadTerrain(Terrain& t, bool full) {
    int x0 = 0, z0 = 0, x1 = Terrain::N - 1, z1 = Terrain::N - 1;
    if (!full) {
        if (t.dirty.empty()) return;
        x0 = std::max(0, t.dirty.x0 - 1); z0 = std::max(0, t.dirty.z0 - 1);
        x1 = std::min(Terrain::N - 1, t.dirty.x1 + 1); z1 = std::min(Terrain::N - 1, t.dirty.z1 + 1);
    }
    t.dirty.clear();
    int w = x1 - x0 + 1, h = z1 - z0 + 1;
    scratch.resize(size_t(w) * h * 2);
    scratch8.resize(size_t(w) * h * 4);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int k = t.idx(x0 + i, z0 + j), o = j * w + i;
            scratch[o * 2] = t.h[k];
            scratch[o * 2 + 1] = t.water[k];
            scratch8[o * 4] = uint8_t(clampf(t.moist[k], 0, 1) * 255);
            scratch8[o * 4 + 1] = uint8_t(clampf((t.h[k] - t.base[k]) / 1.5f, 0, 1) * 255);
            scratch8[o * 4 + 2] = t.grass[k];
            scratch8[o * 4 + 3] = t.track[k];
        }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (full) {
        if (!texHeight) { glGenTextures(1, &texHeight); glGenTextures(1, &texState); }
        glBindTexture(GL_TEXTURE_2D, texHeight);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, w, h, 0, GL_RG, GL_FLOAT, scratch.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, texState);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, scratch8.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    } else {
        glBindTexture(GL_TEXTURE_2D, texHeight);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x0, z0, w, h, GL_RG, GL_FLOAT, scratch.data());
        glBindTexture(GL_TEXTURE_2D, texState);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x0, z0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, scratch8.data());
    }
}

void Renderer::loadWorld(World& w) {
    uploadTerrain(w.terrain, true);
    chunkMin.assign(NCH * NCH, 1e9f);
    chunkMax.assign(NCH * NCH, -1e9f);
    chunkWater.assign(NCH * NCH, false);
    for (int j = 0; j < Terrain::N; j++)
        for (int i = 0; i < Terrain::N; i++) {
            int c = (j / CH) * NCH + (i / CH), k = w.terrain.idx(i, j);
            chunkMin[c] = std::min(chunkMin[c], w.terrain.base[k]);
            chunkMax[c] = std::max(chunkMax[c], w.terrain.h[k]);
            if (w.terrain.water[k] > Terrain::NO_WATER + 1) chunkWater[c] = true;
        }
    // Static prop batches (instanced per unique part+params).
    batches.clear();
    std::map<std::string, std::vector<const PropInstance*>> groups;
    for (auto& p : w.props) {
        std::string key = p.part;
        char buf[48];
        for (auto& kv : p.params) { std::snprintf(buf, sizeof buf, ";%s=%.2f", kv.first.c_str(), kv.second); key += buf; }
        groups[key].push_back(&p);
    }
    for (auto& g : groups) {
        CadResult r = cadBuild(g.second[0]->part, g.second[0]->params);
        if (!r.error.empty()) { std::printf("[cad] %s\n", r.error.c_str()); continue; }
        Batch b;
        b.mesh = uploadMesh(r.mesh);
        std::vector<float> inst;
        for (auto* p : g.second) {
            Mat4 m = Mat4::trs(p->pos, Quat::axisAngle({0, 1, 0}, p->yaw), {p->scale, p->scale, p->scale});
            inst.insert(inst.end(), m.m, m.m + 16);
            inst.insert(inst.end(), {1, 1, 1, 1});
        }
        setInstances(b.mesh, inst.data(), int(g.second.size()), false);
        b.count = int(g.second.size());
        batches.push_back(b);
    }
    CadResult bb = cadBuild("props/barrel.mbcad");
    CadResult bl = cadBuild("props/barrel.mbcad", {{"body_r", 0.13f}, {"body_g", 0.27f}, {"body_b", 0.55f}, {"band", 0}});
    CadResult tp = cadBuild("props/tpost.mbcad");
    barrelInst[0] = uploadMesh(bb.mesh);
    barrelInst[1] = uploadMesh(bl.mesh);
    postInst = uploadMesh(tp.mesh);
}

void Renderer::buildFence(World& w) {
    scratch.clear();
    auto push = [&](Vec3 p, float u, float v, Vec3 n) {
        scratch.insert(scratch.end(), {p.x, p.y, p.z, u, v, n.x, n.y, n.z});
    };
    for (auto& s : w.fenceSegs) {
        const FencePost &a = w.posts[size_t(s.first)], &b = w.posts[size_t(s.second)];
        Vec3 a0 = a.base + Vec3{0, 0.04f, 0}, b0 = b.base + Vec3{0, 0.04f, 0};
        Vec3 a1 = a.top(), b1 = b.top();
        float len = length(b0 - a0);
        Vec3 n = normalize(cross(b0 - a0, Vec3{0, 1, 0}));
        push(a0, 0, 0, n); push(b0, len, 0, n); push(b1, len, 1.1f, n);
        push(a0, 0, 0, n); push(b1, len, 1.1f, n); push(a1, 0, 1.1f, n);
    }
    glBindBuffer(GL_ARRAY_BUFFER, fenceVbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(scratch.size() * 4), scratch.data(), GL_STREAM_DRAW);
    fenceVerts = GLsizei(scratch.size() / 8);
}

void Renderer::drawTerrain(bool water, const Mat4& vp) {
    glUseProgram(progTerrain);
    glUniformMatrix4fv(glGetUniformLocation(progTerrain, "uVP"), 1, GL_FALSE, vp.m);
    glUniform1i(glGetUniformLocation(progTerrain, "uWaterPass"), water ? 1 : 0);
    float planes[6][4];
    extractPlanes(vp, planes);
    glBindVertexArray(chunkVao);
    GLint locChunk = glGetUniformLocation(progTerrain, "uChunk");
    const Terrain* T = nullptr;
    (void)T;
    float ox = -Terrain::N * Terrain::CELL * 0.5f;
    Vec3 cam;
    {
        Mat4 inv = inverse(vp);
        cam = inv.transformPoint({0, 0, -1});
    }
    for (int cz = 0; cz < NCH; cz++)
        for (int cx = 0; cx < NCH; cx++) {
            int c = cz * NCH + cx;
            if (water && !chunkWater[c]) continue;
            Vec3 mn{ox + cx * CH * Terrain::CELL, chunkMin[c] - 1.f, ox + cz * CH * Terrain::CELL};
            Vec3 mx{mn.x + CH * Terrain::CELL, chunkMax[c] + 2.f, mn.z + CH * Terrain::CELL};
            if (!aabbVisible(planes, mn, mx)) continue;
            Vec3 ctr = (mn + mx) * 0.5f;
            float d = length(Vec3{ctr.x - cam.x, 0, ctr.z - cam.z});
            int lod = d < 34.f ? 0 : (d < 80.f ? 1 : 2);
            if (quality == 0) lod = std::min(2, lod + 1);
            glUniform2i(locChunk, cx * CH, cz * CH);
            glDrawElements(GL_TRIANGLES, lods[lod].count, GL_UNSIGNED_INT, (void*)(intptr_t)lods[lod].offset);
        }
}

void Renderer::drawStatic(GLuint prog) {
    Mat4 I = Mat4::identity();
    glUniformMatrix4fv(glGetUniformLocation(prog, "uModel"), 1, GL_FALSE, I.m);
    glUniform1f(glGetUniformLocation(prog, "uDirt"), 0.f);
    glUniform1f(glGetUniformLocation(prog, "uAlpha"), 1.f);
    for (auto& b : batches) drawMesh(b.mesh, b.count);
}

void Renderer::drawDynamic(World& w, const RenderFrame& f, GLuint prog, bool shadowPass) {
    GLint locModel = glGetUniformLocation(prog, "uModel"), locDirt = glGetUniformLocation(prog, "uDirt");
    GLint locRange = glGetUniformLocation(prog, "uDirtRange"), locAlpha = glGetUniformLocation(prog, "uAlpha");
    GLint locSeed = glGetUniformLocation(prog, "uSeed"), locMirror = glGetUniformLocation(prog, "uMirror");
    glUniform1i(locMirror, 0);
    Mat4 I = Mat4::identity();
    // Barrels
    glUniformMatrix4fv(locModel, 1, GL_FALSE, I.m);
    glUniform1f(locDirt, 0.15f);
    glUniform2f(locRange, 0.f, 0.9f);
    glUniform1f(locAlpha, 1.f);
    for (int v = 0; v < 2; v++) {
        scratch.clear();
        int n = 0;
        for (auto& b : w.barrels) {
            if (b.variant != v) continue;
            Mat4 m = Mat4::trs(b.pos, Quat::axisAngle({0, 1, 0}, b.yaw)) *
                     Mat4::trs({0, 0.29f * std::sin(b.tilt), 0}, Quat::axisAngle({1, 0, 0}, b.tilt) * Quat::axisAngle({0, 1, 0}, b.spin));
            scratch.insert(scratch.end(), m.m, m.m + 16);
            scratch.insert(scratch.end(), {1, 1, 1, 1});
            n++;
        }
        if (n) { setInstances(barrelInst[v], scratch.data(), n, true); drawMesh(barrelInst[v], n); }
    }
    // T-posts with plastic lean
    scratch.clear();
    for (auto& p : w.posts) {
        Vec3 axis = cross(Vec3{0, 1, 0}, p.leanDir);
        Quat q = p.lean > 0.001f ? Quat::axisAngle(axis, p.lean) : Quat{};
        Mat4 m = Mat4::trs(p.base, q);
        scratch.insert(scratch.end(), m.m, m.m + 16);
        scratch.insert(scratch.end(), {1, 1, 1, 1});
    }
    glUniform1f(locDirt, 0.f);
    if (!w.posts.empty()) { setInstances(postInst, scratch.data(), int(w.posts.size()), true); drawMesh(postInst, int(w.posts.size())); }
    // Vehicles, wheels, etc.
    for (const DrawItem& it : f.items) {
        if (!it.mesh) continue;
        if (it.alpha < 0.99f && shadowPass) continue;
        glUniformMatrix4fv(locModel, 1, GL_FALSE, it.model.m);
        glUniform1f(locDirt, it.dirt);
        glUniform2f(locRange, it.dirtLo, it.dirtHi);
        glUniform1f(locAlpha, it.alpha);
        glUniform1f(locSeed, it.seed);
        glUniform1i(locMirror, it.mirror ? 1 : 0);
        drawMesh(*it.mesh, 1);
        glUniform1i(locMirror, 0);
    }
}

void Renderer::render(World& w, const RenderFrame& f) {
    uploadTerrain(w.terrain, false);
    buildFence(w);

    float aspect = float(width) / float(std::max(1, height));
    Mat4 proj = Mat4::perspective(f.fov, aspect, 0.15f, 600.f);
    Mat4 view = Mat4::lookAt(f.camPos, f.camTarget, {0, 1, 0});
    viewProj = proj * view;

    // Shadow cascade centered ahead of the camera.
    Vec3 fwd = normalize(Vec3{f.camTarget.x - f.camPos.x, 0, f.camTarget.z - f.camPos.z});
    Vec3 center = f.camTarget + fwd * 14.f;
    const float ext = quality >= 2 ? 42.f : 30.f;
    Mat4 lv = Mat4::lookAt(center + sunDir * 120.f, center, {0, 1, 0});
    lightVP = Mat4::ortho(-ext, ext, -ext, ext, 1.f, 260.f) * lv;

    auto setCommon = [&](GLuint p) {
        glUseProgram(p);
        glUniform3f(glGetUniformLocation(p, "uSunDir"), sunDir.x, sunDir.y, sunDir.z);
        glUniform3f(glGetUniformLocation(p, "uCamPos"), f.camPos.x, f.camPos.y, f.camPos.z);
        glUniform1f(glGetUniformLocation(p, "uTime"), f.time);
        glUniformMatrix4fv(glGetUniformLocation(p, "uLightVP"), 1, GL_FALSE, lightVP.m);
        glUniform1i(glGetUniformLocation(p, "uShadow"), 2);
    };

    // ---- shadow pass ----
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
    glViewport(0, 0, SHADOW, SHADOW);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 3.f);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(progShadow);
    glUniformMatrix4fv(glGetUniformLocation(progShadow, "uVP"), 1, GL_FALSE, lightVP.m);
    if (quality >= 1) drawStatic(progShadow);
    drawDynamic(w, f, progShadow, true);
    glUseProgram(progFence);
    glUniformMatrix4fv(glGetUniformLocation(progFence, "uVP"), 1, GL_FALSE, lightVP.m);
    glUniform1i(glGetUniformLocation(progFence, "uShadowPass"), 1);
    glUniform1i(glGetUniformLocation(progFence, "uShadow"), 3);
    glBindVertexArray(fenceVao);
    glDrawArrays(GL_TRIANGLES, 0, fenceVerts);
    glDisable(GL_POLYGON_OFFSET_FILL);

    // ---- main pass ----
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glClearColor(0.5f, 0.65f, 0.9f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, shadowTex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texHeight);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texState);

    // Sky
    setCommon(progSky);
    Mat4 inv = inverse(viewProj);
    glUniformMatrix4fv(glGetUniformLocation(progSky, "uInvVP"), 1, GL_FALSE, inv.m);
    glDepthMask(GL_FALSE);
    glBindVertexArray(skyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);

    // Terrain
    setCommon(progTerrain);
    glUniform1i(glGetUniformLocation(progTerrain, "uHeight"), 0);
    glUniform1i(glGetUniformLocation(progTerrain, "uState"), 1);
    glUniform2f(glGetUniformLocation(progTerrain, "uOrigin"), w.terrain.originX(), w.terrain.originZ());
    glUniform1f(glGetUniformLocation(progTerrain, "uCell"), Terrain::CELL);
    drawTerrain(false, viewProj);

    // Meshes
    setCommon(progMesh);
    glUniformMatrix4fv(glGetUniformLocation(progMesh, "uVP"), 1, GL_FALSE, viewProj.m);
    drawStatic(progMesh);
    drawDynamic(w, f, progMesh, false);

    // Fence (alpha-tested, double-sided)
    setCommon(progFence);
    glUniformMatrix4fv(glGetUniformLocation(progFence, "uVP"), 1, GL_FALSE, viewProj.m);
    glUniform1i(glGetUniformLocation(progFence, "uShadowPass"), 0);
    glBindVertexArray(fenceVao);
    glDrawArrays(GL_TRIANGLES, 0, fenceVerts);

    // Particles
    int np = 0;
    if (f.particles && !f.particles->empty()) {
        scratch.clear();
        for (const Particle& p : *f.particles) {
            scratch.insert(scratch.end(), {p.p.x, p.p.y, p.p.z, p.size, p.r, p.g, p.b, p.a, float(int(p.type))});
            np++;
        }
        glBindBuffer(GL_ARRAY_BUFFER, particleInst);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(scratch.size() * 4), scratch.data(), GL_STREAM_DRAW);
        setCommon(progParticle);
        glUniformMatrix4fv(glGetUniformLocation(progParticle, "uVP"), 1, GL_FALSE, viewProj.m);
        Vec3 right{view.at(0, 0), view.at(0, 1), view.at(0, 2)}, up{view.at(1, 0), view.at(1, 1), view.at(1, 2)};
        glUniform3f(glGetUniformLocation(progParticle, "uRight"), right.x, right.y, right.z);
        glUniform3f(glGetUniformLocation(progParticle, "uUp"), up.x, up.y, up.z);
        glUniform1i(glGetUniformLocation(progParticle, "uSoft"), 0);
        glBindVertexArray(particleVao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, np);
    }

    // Transparent: water, then soft particles, then ghost vehicles.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    setCommon(progTerrain);
    drawTerrain(true, viewProj);
    if (np) {
        glDepthMask(GL_FALSE);
        setCommon(progParticle);
        glUniform1i(glGetUniformLocation(progParticle, "uSoft"), 1);
        glBindVertexArray(particleVao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, np);
        glDepthMask(GL_TRUE);
    }
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

}  // namespace mb
