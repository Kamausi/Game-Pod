#include "renderer.h"
#include <cstdio>
#include <algorithm>
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
uniform int uQuality;
uniform sampler2DShadow uShadow;
uniform mat4 uLightVP;
uniform vec3 uSunColor;
uniform vec4 uWeather;   // x cloud cover, y ground wetness (rain soak), z rain intensity, w golden hour
uniform float uFlash;    // lightning flash 0..1
uniform float uNight;    // 0 day .. 1 night (moonlight, dark ambient, lights on)
float gStars = 0.0;      // only the sky dome itself draws stars (not reflections)
// Local lights at night: light-tower floods and headlights (spots). pos.w = range,
// col.rgb = intensity, dir.xyz = spot axis, dir.w = cos of the cone edge (-1 = point light).
uniform int uNumLights;
uniform vec4 uLightPos[16];
uniform vec4 uLightCol[16];
uniform vec4 uLightDir[16];
float hash(vec2 p){ p = fract(p*vec2(123.34,456.21)); p += dot(p,p+45.32); return fract(p.x*p.y); }
vec2 hash2(vec2 p){ return vec2(hash(p), hash(p+19.19)); }
float vnoise(vec2 p){ vec2 i=floor(p), f=fract(p); vec2 u=f*f*(3.0-2.0*f);
  return mix(mix(hash(i),hash(i+vec2(1,0)),u.x), mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),u.x), u.y); }
float fbm(vec2 p){ float s=0.0, a=0.5; for(int i=0;i<4;i++){ s+=a*vnoise(p); p*=2.03; a*=0.5; } return s; }
float fbm2(vec2 p){ return 0.66*vnoise(p) + 0.34*vnoise(p*2.07+3.1); }
// Cellular noise: x = distance to nearest feature point, y = distance to the cell border.
vec2 voronoi(vec2 p){
  vec2 n = floor(p), f = fract(p), mg = vec2(0), mr = vec2(0); float md = 8.0;
  for(int j=-1;j<=1;j++) for(int i=-1;i<=1;i++){
    vec2 g = vec2(float(i),float(j)); vec2 r = g + hash2(n+g) - f; float d = dot(r,r);
    if(d < md){ md = d; mr = r; mg = g; } }
  float be = 8.0;
  for(int j=-1;j<=1;j++) for(int i=-1;i<=1;i++){
    vec2 g = mg + vec2(float(i),float(j)); vec2 r = g + hash2(n+g) - f;
    if(dot(mr-r,mr-r) > 1e-5) be = min(be, dot(0.5*(mr+r), normalize(r-mr))); }
  return vec2(sqrt(md), be);
}
// Fair-weather cumulus on a plane 600 m up. The sky samples it along the view ray and the
// ground along the sun ray, so the shadows sweeping the field match the clouds overhead.
const float kCloudH = 600.0;
float cloudDensity(vec2 q){
  q = q*0.0016 + vec2(uTime*0.004, uTime*0.0015);
  float f = 0.5*vnoise(q) + 0.25*vnoise(q*2.03 + 1.7) + 0.125*vnoise(q*4.1 + 3.3) + 0.0625*vnoise(q*8.3);
  float cov = uWeather.x;
  return smoothstep(0.42 - 0.40*cov, 0.66 - 0.24*cov, f/0.9375);
}
float cloudShadow(vec3 wp){
  if(uQuality == 0) return 1.0;
  vec2 q = wp.xz + uSunDir.xz/max(uSunDir.y, 0.1)*(kCloudH - wp.y);
  return 1.0 - 0.62*cloudDensity(q);
}
float shadowAt(vec3 wp, vec3 n){
  float cs = cloudShadow(wp);
  vec4 lp = uLightVP*vec4(wp + n*0.06, 1.0);
  vec3 c = lp.xyz/lp.w*0.5+0.5;
  if(c.x<0.0||c.x>1.0||c.y<0.0||c.y>1.0||c.z>1.0) return cs;
  c.z -= 0.0012;
  float s = 0.0; vec2 t = vec2(1.0/2048.0);
  for(int x=-1;x<=1;x++) for(int y=-1;y<=1;y++) s += texture(uShadow, vec3(c.xy+vec2(x,y)*t*1.5, c.z));
  return s/9.0*cs;
}
// Clouds over the sky gradient for view direction d (from the camera).
vec3 withClouds(vec3 c, vec3 d){
  if(d.y <= 0.0) return c;
  vec2 q = uCamPos.xz + d.xz/max(d.y, 0.02)*(kCloudH - uCamPos.y);
  float den = cloudDensity(q);
  if(den <= 0.0) return c;
  float toward = cloudDensity(q + uSunDir.xz*55.0);              // self-shadowing toward the sun
  float lit = clamp(1.0 - (toward - den)*1.6 - den*0.35, 0.0, 1.0);
  vec3 col = mix(vec3(0.50,0.53,0.60), vec3(1.05,1.01,0.95), lit)*(1.0 - 0.5*uWeather.x)*(1.0 - 0.94*uNight);
  col = mix(col, col*vec3(1.3,0.95,0.75), uWeather.w);
  col += vec3(1.0,0.9,0.7)*pow(max(dot(d, uSunDir), 0.0), 12.0)*0.5*(1.0 - den);   // silver lining
  float fade = smoothstep(0.015, 0.16, d.y);
  return mix(c, col, den*fade*0.95);
}
vec3 skyColor(vec3 d){
  float t = clamp(d.y, 0.0, 1.0);
  vec3 zen = vec3(0.06,0.23,0.68), hor = vec3(0.45,0.61,0.88);
  vec3 c = mix(hor, zen, pow(t, 0.5));
  float s = max(dot(d, uSunDir), 0.0);
  c = mix(c, c*vec3(1.35,0.92,0.62) + vec3(0.12,0.04,0.0), uWeather.w*(1.0 - pow(t, 0.35)));   // golden-hour horizon
  c += (vec3(1.0,0.92,0.75)*pow(s, 900.0)*20.0 + vec3(1.0,0.85,0.6)*pow(s, 6.0)*(0.14 + 0.3*uWeather.w))*(1.0 - uWeather.x)*(1.0 - uNight);
  c = mix(c, vec3(0.55,0.58,0.63)*(1.0 - 0.32*uWeather.z), uWeather.x*0.85);              // overcast / rain
  c += vec3(0.75,0.8,0.95)*uFlash*1.6;
  if(d.y < 0.0) c = mix(hor, vec3(0.35,0.33,0.28), clamp(-d.y*4.0, 0.0, 1.0));
  if(uNight > 0.0){
    vec3 nightSky = d.y < 0.0 ? vec3(0.02,0.022,0.03) : mix(vec3(0.035,0.045,0.08), vec3(0.006,0.009,0.025), pow(t, 0.4));
    vec2 sp = d.xz/(d.y + 0.25)*260.0;
    float star = gStars*step(0.9975, hash(floor(sp)))*smoothstep(0.02, 0.2, d.y)*(1.0 - uWeather.x);
    nightSky += vec3(0.9,0.92,1.0)*star*0.8 + vec3(0.8,0.85,0.95)*pow(s, 1800.0)*6.0;   // stars + moon
    c = mix(c, nightSky, uNight);
  }
  return c;
}
#define kSun uSunColor
// Pre-blurred environment: sky above, sunlit fall field below, blurrier with roughness.
vec3 envColor(vec3 d, float rough){
  float band = 0.04 + rough*0.6;
  vec3 ground = vec3(0.19,0.17,0.10)*(0.55 + 0.45*max(uSunDir.y, 0.0));
  vec3 sky = skyColor(normalize(vec3(d.x, max(d.y, 0.0) + rough*0.35, d.z)));
  sky = mix(sky, vec3(0.42,0.55,0.80), rough*0.7);
  // Ragged dark treeline just above the horizon: gives paint, glass and puddles the horizon
  // line you see reflected in real panels. Blurs out as the surface gets rougher.
  float az = atan(d.z, d.x);
  float tl = 0.035 + 0.05*vnoise(vec2(az*9.0, 1.3)) + 0.03*vnoise(vec2(az*31.0, 4.7));
  vec3 trees = mix(vec3(0.07,0.09,0.05), vec3(0.16,0.15,0.09), uWeather.w)*(0.35 + 0.65*max(uSunDir.y, 0.15))*(1.0 - 0.85*uNight);
  float inTrees = (1.0 - smoothstep(tl - band*0.5, tl + band*0.5, d.y))*smoothstep(-band, band*0.5, d.y);
  sky = mix(sky, trees, inTrees*0.85);
  return mix(ground, sky, smoothstep(-band, band, d.y));
}
// Field palette shared by the terrain and the grass blades so they blend seamlessly.
vec3 grassColor(vec2 p){
  float n1 = fbm(p*0.18), n2 = fbm(p*0.9+7.0);
  vec3 gA = vec3(0.25,0.33,0.14), gB = vec3(0.47,0.45,0.27), gold = vec3(0.60,0.53,0.22);
  vec3 c = mix(gA, gB, smoothstep(0.35, 0.7, n1));
  return mix(c, gold, smoothstep(0.72, 0.86, n2)*0.45);
}
float D_GGX(float NoH, float a){ float a2 = a*a; float d = NoH*NoH*(a2-1.0)+1.0; return a2/(3.14159*d*d); }
float V_Smith(float NoV, float NoL, float a){ float k = a*0.5; return 0.25/((NoV*(1.0-k)+k)*(NoL*(1.0-k)+k)); }
vec3 F_Schlick(vec3 f0, float VoH){ return f0 + (1.0-f0)*pow(1.0-VoH, 5.0); }
// Point/spot lights (no shadows): Lambert + GGX, smooth range falloff.
vec3 localLights(vec3 diffAlb, vec3 f0, vec3 n, vec3 wp, vec3 V, float a){
  vec3 sum = vec3(0.0);
  for(int i = 0; i < 16; i++){
    if(i >= uNumLights) break;
    vec3 Lv = uLightPos[i].xyz - wp;
    float d2 = dot(Lv, Lv), r = uLightPos[i].w;
    if(d2 > r*r) continue;
    vec3 L = Lv*inversesqrt(d2);
    float NoL = max(dot(n, L), 0.0);
    if(NoL <= 0.0) continue;
    float fall = 1.0 - d2/(r*r); fall *= fall;
    float spot = uLightDir[i].w < -0.5 ? 1.0 : smoothstep(uLightDir[i].w, uLightDir[i].w + 0.08, dot(-L, uLightDir[i].xyz));
    vec3 H = normalize(L + V);
    vec3 spec = D_GGX(max(dot(n, H), 0.0), a)*V_Smith(max(dot(n, V), 1e-3), NoL, a)*F_Schlick(f0, max(dot(V, H), 0.0));
    sum += (diffAlb + spec*3.14159)*uLightCol[i].rgb*NoL*fall*spot/(1.0 + d2*0.05);
  }
  return sum;
}
// One shading model for every surface. rough is perceptual roughness, ao attenuates indirect.
vec3 pbr(vec3 alb, vec3 n, vec3 wp, float rough, float metal, float sh, float ao){
  vec3 V = normalize(uCamPos - wp), L = uSunDir, H = normalize(L + V);
  float NoL = max(dot(n, L), 0.0), NoV = max(dot(n, V), 1e-3), NoH = max(dot(n, H), 0.0), VoH = max(dot(V, H), 0.0);
  float a = max(rough*rough, 0.002);
  vec3 f0 = mix(vec3(0.04), alb, metal);
  vec3 F = F_Schlick(f0, VoH);
  vec3 spec = D_GGX(NoH, a)*V_Smith(NoV, NoL, a)*F;
  vec3 diff = alb*(1.0-metal)*(1.0-F)/3.14159;
  vec3 direct = (diff*3.14159 + spec*3.14159)*kSun*NoL*sh;
  vec3 amb = mix(vec3(0.30,0.25,0.18), vec3(0.42,0.55,0.82), n.y*0.5+0.5)*0.6*mix(1.0, 0.06, uNight);
  direct += localLights(alb*(1.0-metal), f0, n, wp, V, a);
  // Grazing-angle reflection fades with roughness (microfacet masking) so satin surfaces don't mirror the sky.
  vec3 Fr = f0 + (1.0 - f0)*pow(1.0-NoV, 5.0)*(1.0-rough)*(1.0-rough);
  vec3 R = reflect(-V, n);
  float horizon = clamp(1.0 + dot(R, n), 0.0, 1.0);
  vec3 env = envColor(R, rough)*Fr*horizon*mix(0.35, 1.0, sh*0.5+0.5);
  return direct + (alb*(1.0-metal)*amb + env)*ao;
}
vec3 shade(vec3 alb, vec3 n, vec3 wp, float wet, float sh){
  return pbr(alb, n, wp, mix(0.85, 0.18, wet), 0.0, sh, 1.0);
}
vec3 finalize(vec3 c, vec3 wp){
  float d = length(wp - uCamPos);
  float f = 1.0 - exp(-d*(0.0022 + 0.007*uWeather.z));
  vec3 fc = skyColor(normalize(vec3(wp.x-uCamPos.x, 0.03, wp.z-uCamPos.z)));
  c = mix(c, fc, f*0.65);
  c += vec3(0.55,0.6,0.75)*uFlash*0.3;
  c = c*(2.51*c+0.03)/(c*(2.43*c+0.59)+0.14);
  return pow(clamp(c, 0.0, 1.0), vec3(1.0/2.2));
}
)";

const char* kTerrainVS = R"(
layout(location=0) in vec3 aGrid;
uniform ivec2 uChunk;
uniform sampler2D uHeight;
uniform sampler2D uState;
uniform sampler2D uTrackDir;
uniform vec2 uOrigin;
uniform float uCell;
uniform mat4 uVP;
uniform int uWaterPass;
out vec3 vWorld; out vec3 vN; out vec4 vState; out float vDepth; out vec2 vTrackGrad; out vec2 vTrackDir; out vec3 vSoil; out float vGravel;
// Soil tint per type (loam, clay gumbo, silt, wet sand, peat muck) - matches kSoils in terramechanics.h.
const vec3 kSoilTint[6] = vec3[6](vec3(1.0), vec3(0.86,0.84,0.82), vec3(1.10,1.06,1.00), vec3(1.30,1.18,0.92), vec3(0.52,0.46,0.40), vec3(1.0));
float H(ivec2 c){ c = clamp(c, ivec2(0), ivec2(TN)); return texelFetch(uHeight, c, 0).r; }
float T(ivec2 c){ c = clamp(c, ivec2(0), ivec2(TN)); return texelFetch(uState, c, 0).a; }
void main(){
  ivec2 c = clamp(uChunk + ivec2(aGrid.xz), ivec2(0), ivec2(TN));
  vec2 hw = texelFetch(uHeight, c, 0).rg;
  float h = hw.r;
  vN = normalize(vec3(H(c-ivec2(1,0)) - H(c+ivec2(1,0)), 2.0*uCell, H(c-ivec2(0,1)) - H(c+ivec2(0,1))));
  vState = texelFetch(uState, c, 0);
  vTrackGrad = vec2(T(c+ivec2(1,0)) - T(c-ivec2(1,0)), T(c+ivec2(0,1)) - T(c-ivec2(0,1)));
  // Tire heading as a doubled angle so opposite directions interpolate cleanly; untracked cells add nothing.
  vec2 td = texelFetch(uTrackDir, c, 0).rg;
  float ang = td.r * 6.2831853;
  int soilId = clamp(int(td.g*255.0/40.0 + 0.5), 0, 5);
  vSoil = kSoilTint[soilId];
  vGravel = soilId == 5 ? 1.0 : 0.0;
  vTrackDir = vec2(cos(ang), sin(ang))*step(0.03, vState.a);
  float y = h - aGrid.y*0.6;
  vDepth = hw.g - h;
  if(uWaterPass == 1){ y = hw.g > -900.0 ? hw.g : h - 0.05; vDepth = hw.g > -900.0 ? hw.g - h : -1.0; }
  vWorld = vec3(uOrigin.x + float(c.x)*uCell, y, uOrigin.y + float(c.y)*uCell);
  gl_Position = uVP*vec4(vWorld, 1.0);
}
)";

const char* kTerrainFS = R"(
in vec3 vWorld; in vec3 vN; in vec4 vState; in float vDepth; in vec2 vTrackGrad; in vec2 vTrackDir; in vec3 vSoil; in float vGravel;
uniform int uWaterPass;
uniform vec4 uOcc[8];   // vehicle contact occlusion: x, z, yaw, length (0 = unused)
out vec4 frag;
// Small-scale relief of churned mud: clods, tire-pressed slick ruts and the berms between them.
float mudRelief(vec2 p, float churn){
  float clods = fbm2(p*3.1)*0.6 + vnoise(p*11.0)*0.28 + vnoise(p*27.0)*0.12;
  return clods*churn;
}
float contactAO(vec2 p){
  float ao = 1.0;
  for(int i = 0; i < 8; i++){
    vec4 o = uOcc[i];
    if(o.w <= 0.0) continue;
    vec2 d = p - o.xy;
    float c = cos(o.z), s = sin(o.z);
    vec2 l = vec2(c*d.x - s*d.y, s*d.x + c*d.y);   // x across, y along the truck
    vec2 e = l / vec2(o.w*0.28, o.w*0.62);
    ao *= 1.0 - 0.55*exp(-dot(e, e)*1.6);
  }
  return ao;
}
void main(){
  vec3 n = normalize(vN);
  vec2 p = vWorld.xz;
  if(uWaterPass == 1){
    if(vDepth < 0.004) discard;
    vec2 q = p*1.3;
    float t = uTime;
    // Ripples: a few directional sine waves (analytic slopes) wobbled by low-frequency noise,
    // faded with distance so they never alias into a pattern.
    float wob = vnoise(p*0.35 + t*0.05)*6.2831;
    vec2 d1 = vec2(0.8, 0.6), d2 = vec2(-0.5, 0.86), d3 = vec2(0.96, -0.28);
    vec2 g = d1*cos(dot(p, d1)*3.1 + t*1.7 + wob)*0.5 + d2*cos(dot(p, d2)*4.3 - t*2.1)*0.3 + d3*cos(dot(p, d3)*6.7 + t*2.9 + wob*0.5)*0.2;
    float far = 1.0/(1.0 + length(vWorld - uCamPos)*0.06);
    vec3 wn = normalize(vec3(g.x*0.12*far, 1.0, g.y*0.12*far));
    vec3 V = normalize(uCamPos - vWorld);
    float fres = 0.02 + 0.98*pow(1.0 - max(dot(wn, V), 0.0), 5.0);
    vec3 rd = reflect(-V, wn);
    vec3 refl = envColor(rd, 0.22);
    if(uQuality > 0) refl = mix(refl, withClouds(refl, rd), 0.7);
    // Suspended clay makes bog water an opaque milky brown; sediment swirls drift slowly.
    float silt = fbm(p*0.45 + vec2(t*0.02, -t*0.015));
    vec3 body = pow(vec3(0.40,0.31,0.20)*(0.8 + 0.35*silt)*mix(1.0, 0.6, smoothstep(0.15, 0.8, vDepth)), vec3(2.2));
    float sh = shadowAt(vWorld, wn);
    vec3 c = body*(kSun*max(uSunDir.y, 0.0)*0.55*sh + vec3(0.32,0.36,0.42)*mix(1.0, 0.06, uNight)) + localLights(body, vec3(0.02), wn, vWorld, V, 0.05);
    c = mix(c, refl, fres*0.55);
    vec3 H = normalize(uSunDir + V);
    c += kSun*pow(max(dot(wn, H), 0.0), 600.0)*1.4*sh;
    // Scum and froth collect along the shoreline.
    float edge = 1.0 - smoothstep(0.0, 0.04, vDepth);
    float froth = smoothstep(0.55, 0.8, vnoise(p*9.0 + t*0.1)*0.6 + vnoise(p*23.0)*0.4) * edge;
    c = mix(c, pow(vec3(0.62,0.55,0.44), vec3(2.2))*(kSun*0.35*sh + 0.4), froth*0.8);
    float a = clamp(0.78 + vDepth*3.0, 0.78, 0.97) * (1.0 - edge*0.35);
    frag = vec4(finalize(c, vWorld), a);
    return;
  }
  float moist = vState.r + (1.0 - vState.r)*uWeather.y*0.55*(1.0 - 0.7*vGravel), mud = vState.g, grass = vState.b, track = vState.a;
  float n1 = fbm(p*0.18), n2 = fbm(p*0.9+7.0), n3 = vnoise(p*6.0);
  vec3 grassC = grassColor(p);
  float blades = vnoise(p*23.0)*vnoise(p*7.0 + 2.0);
  grassC *= 0.82 + 0.22*n3 + 0.18*blades;

  float wetness = smoothstep(0.35, 0.95, moist);
  float mudAmt = smoothstep(0.0, 0.05, mud)*smoothstep(0.25, 0.55, moist);
  float churn = clamp(max(mudAmt, wetness*0.7) + track*0.6, 0.0, 1.0) * (1.0 - grass);
  // Rut walls (track gradient) push up berms of loose clods; rut floors are pressed slick.
  float berm = clamp(length(vTrackGrad)*1.4, 0.0, 1.0);
  float slick = track*(1.0 - berm);

  float film = smoothstep(0.9, 0.98, moist)*smoothstep(0.7, 0.8, fbm(p*0.5 + 11.0))*(1.0 - grass);
  // --- relief normal (finite differences of the procedural height) ---
  vec3 nd = n;
  if(uQuality > 0 && churn > 0.02){
    float amp = 0.05*(0.45 + berm*1.4 - slick*0.35);
    float e = 0.02;
    float h0 = mudRelief(p, churn), hx = mudRelief(p + vec2(e, 0.0), churn), hz = mudRelief(p + vec2(0.0, e), churn);
    nd = normalize(n + vec3(-(hx - h0)/e, 0.0, -(hz - h0)/e)*amp);
  }
  // --- tread imprint: staggered lug bars pressed across the rut floor along the tire's heading ---
  float coher = length(vTrackDir);
  float tread = 0.0;
  if(uQuality > 0 && coher > 0.25 && slick > 0.05){
    float th = 0.5*atan(vTrackDir.y, vTrackDir.x);
    vec2 dir = vec2(cos(th), sin(th)), lat = vec2(-dir.y, dir.x);
    float along = dot(p, dir)/0.105, side = dot(p, lat)/0.16;
    float stagger = step(0.5, fract(side))*0.5;
    float ph = fract(along + stagger + side*0.18);            // slight chevron skew
    float bar = smoothstep(0.08, 0.18, ph) - smoothstep(0.48, 0.58, ph);
    float w = smoothstep(0.25, 0.7, coher)*smoothstep(0.05, 0.5, slick)*(1.0 - film);
    tread = bar*w;
    // Bar walls tilt the normal along the heading (rising edge faces back, falling edge forward).
    float slope = step(0.08, ph)*step(ph, 0.18) - step(0.48, ph)*step(ph, 0.58);
    nd = normalize(nd - vec3(dir.x, 0.0, dir.y)*slope*0.45*w);
  }

  vec3 dirt = vec3(0.47,0.40,0.30)*(0.9+0.2*n3);
  vec3 mudDry = vec3(0.43,0.36,0.27), mudWet = vec3(0.23,0.17,0.115);
  vec3 mudC = mix(mudDry, mudWet, wetness)*(0.84 + 0.3*n2);
  // Grey-blue clay streaks turned up where tires dig.
  mudC = mix(mudC, vec3(0.33,0.30,0.25)*(0.6 + 0.4*wetness), smoothstep(0.62, 0.85, fbm2(p*1.7 + 3.0))*churn*0.25);
  // Loose clods on berms catch light; their tops dry out lighter.
  float clod = smoothstep(0.55, 0.85, vnoise(p*11.0)*0.7 + vnoise(p*27.0)*0.3);
  mudC = mix(mudC, mudC*1.45, clod*berm*0.7);
  vec3 soil = mix(dirt, mudC, max(mudAmt, wetness*0.6));
  soil = clamp(soil*vSoil, 0.0, 1.0);
  // Crushed-stone roads and pads: grey speckled aggregate, mud tracked over it where wet.
  if(vGravel > 0.01){
    float st = vnoise(p*14.0)*0.6 + vnoise(p*41.0)*0.4;
    vec3 grav = vec3(0.50,0.48,0.45)*(0.72 + 0.5*st);
    grav = mix(grav, vec3(0.62,0.60,0.56), smoothstep(0.72, 0.9, vnoise(p*23.0 + 5.0))*0.6);
    soil = mix(soil, grav, vGravel*(1.0 - 0.75*max(mudAmt, wetness*0.5)));
  }

  // Drying crust: polygonal cracks where mud sits on drier ground (pit edges, splash zones).
  float crust = mudAmt*smoothstep(0.62, 0.42, moist)*(1.0 - track);
  if(crust > 0.01 && uQuality > 0){
    vec2 vc = voronoi(p*7.0 + vnoise(p*13.0)*0.5);
    float crack = 1.0 - smoothstep(0.02, 0.07, vc.y);
    soil = mix(soil, soil*vec3(1.12,1.08,1.02), crust*0.6);
    soil = mix(soil, soil*0.62, crack*crust*0.8);
  }
  vec3 alb = mix(soil, grassC, smoothstep(0.1, 0.8, grass));
  alb *= 1.0 - 0.28*slick*(1.0 - grass);
  alb *= 1.0 - 0.22*tread;
  alb = pow(alb, vec3(2.2));

  // Wet mud is glossy; pressed ruts and standing films are glossier still.
  // Wet clay is satin, not mirror: only pressed rut floors and scattered films get really glossy.
  float rough = mix(0.92, 0.55, wetness*(1.0 - grass));
  rough = mix(rough, 0.38, slick*wetness);
  rough = mix(rough, 0.75, clod*berm);
  rough = mix(rough, 0.2, film);
  nd = normalize(mix(nd, n, film*0.7));
  alb *= 1.0 - film*0.45;

  float sh = shadowAt(vWorld, n);
  float ao = contactAO(p);
  vec3 col = pbr(alb, nd, vWorld, rough, 0.0, sh*ao, ao);
  frag = vec4(finalize(col, vWorld), 1.0);
}
)";

const char* kMeshVS = R"(
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNormal; layout(location=2) in vec3 aColor;
layout(location=3) in vec4 iM0; layout(location=4) in vec4 iM1; layout(location=5) in vec4 iM2; layout(location=6) in vec4 iM3;
layout(location=7) in vec4 iTint; layout(location=8) in float aMat;
uniform mat4 uModel; uniform mat4 uVP;
out vec3 vWorld; out vec3 vN; out vec3 vColor; out vec3 vLocal; out vec3 vLocalN; flat out int vMat; out float vInstDirt;
void main(){
  mat4 M = uModel*mat4(iM0, iM1, iM2, iM3);
  vec4 w = M*vec4(aPos, 1.0);
  vWorld = w.xyz; vN = mat3(M)*aNormal; vColor = aColor*iTint.rgb; vLocal = aPos; vLocalN = aNormal;
  vMat = int(aMat + 0.5);
  vInstDirt = iTint.a;   // per-instance mud for static props (parked rigs)
  gl_Position = uVP*w;
}
)";

const char* kMeshFS = R"(
in vec3 vWorld; in vec3 vN; in vec3 vColor; in vec3 vLocal; in vec3 vLocalN; flat in int vMat; in float vInstDirt;
uniform float uDirtU; uniform vec2 uDirtRange; uniform float uAlpha; uniform float uSeed; uniform int uMirror;
uniform vec4 uWheels[4];   // body: wheel centers in body space (x, y, z, radius); w = 0 unused
uniform float uTireR;      // wheel items: outer tread radius, 0 otherwise
uniform int uGlass;        // 0 draw everything, 1 skip glass (opaque pass), 2 glass only (blended pass)
out vec4 frag;
float mudHeight(vec3 q){ return fbm2(q.xz*7.0 + q.y*5.0 + uSeed) * 0.7 + vnoise(q.zy*23.0 + q.x*9.0)*0.3; }
void main(){
  if(uGlass == 1 && vMat == 2) discard;
  if(uGlass == 2 && vMat != 2) discard;
  float uDirt = max(uDirtU, vInstDirt);
  vec3 alb = pow(clamp(vColor, 0.0, 1.0), vec3(2.2));
  vec3 n = normalize(vN);
  if(gl_FrontFacing == (uMirror == 1)) n = -n;
  vec3 ln = normalize(vLocalN);
  float rough = 0.5, metal = 0.0, coat = 0.0, glow = 0.0;
  if(vMat == 1){ rough = 0.42; coat = 1.0; }                                  // paint: base + clearcoat
  else if(vMat == 2){ alb = alb*0.35; rough = 0.04; coat = 1.0; }              // glass
  else if(vMat == 3){ rough = 0.12; metal = 1.0; alb = max(alb, vec3(0.55)); } // chrome
  else if(vMat == 4){ rough = 0.88; alb *= 0.9; }                              // rubber
  else if(vMat == 5){ rough = 0.06; coat = 1.0; glow = 0.35; }                 // lamp lens
  else if(vMat == 6){ rough = 0.92; }                                          // matte
  else if(vMat == 7){ rough = 0.42; metal = 0.85; }                            // steel
  else if(vMat == 8){ rough = 0.7; }                                           // baked-on mud
  float leafT = 0.0;
  if(vMat == 9){                                                               // foliage
    rough = 0.72;
    vec3 V0 = normalize(uCamPos - vWorld);
    float lf = vnoise(vWorld.xz*6.0 + vWorld.y*4.3) *0.6 + vnoise(vWorld.xy*13.0 + vWorld.z*7.0)*0.4;
    // Fine leaf / needle-clump scale: dark gaps between clumps, and it frays the silhouette.
    float lf2 = vnoise(vWorld.xz*31.0 + vWorld.y*23.0)*0.55 + vnoise(vWorld.zy*47.0 + vWorld.x*19.0)*0.45;
    // Ragged leafy outline: eat away grazing parts of each cluster with noise.
    float edge = 1.0 - abs(dot(n, V0));
    if(mix(lf, lf2, 0.45) < (edge - 0.45)*1.9) discard;
    alb *= (0.62 + 0.7*lf)*(0.55 + 0.75*smoothstep(0.15, 0.75, lf2));
    n = normalize(n + (vec3(lf2, lf, lf2*lf) - 0.45)*0.6);   // clumpy normals break up the smooth shading
    leafT = 1.0;
  }
  float ao = 0.62 + 0.38*smoothstep(-0.9, 0.5, n.y);

  // ---- mud: splash coat + spray fans thrown up behind each tire + specks, drips, packed tread ----
  float m = 0.0, thick = 0.0, streak = 0.5;
  if(uDirt > 0.001){
    vec3 q = vLocal + vec3(uSeed);
    float n0 = fbm(q.xz*2.2 + q.y*1.3);
    float nf = vnoise(q.zy*15.0 + q.x*11.0);
    float line = mix(uDirtRange.x, uDirtRange.y, uDirt);
    // Tops of panels (hood, roof) catch far less than the sides the tires spray.
    float edgeY = vLocal.y + max(ln.y, 0.0)*0.3 + (n0 - 0.5)*0.5 + (nf - 0.5)*0.1;
    m = smoothstep(line + 0.03, line - 0.03, edgeY);
    if(uTireR <= 0.0){
      for(int i = 0; i < 4; i++){
        vec4 w = uWheels[i];
        if(w.w <= 0.0) continue;
        vec2 d = vec2(vLocal.z - w.z, vLocal.y - w.y);
        vec2 dd = vec2(d.x*(d.x < 0.0 ? 0.5 : 1.0), d.y*0.85);   // fan biased up and rearward
        float ang = atan(d.y, d.x);
        float ray = vnoise(vec2(ang*9.0 + uSeed, length(d)*1.2));   // radial streaks off the tread
        float r = length(dd) - ray*0.35*w.w;
        float fan = 1.0 - smoothstep(w.w*1.05, w.w*(1.45 + uDirt*1.5), r);
        float side = smoothstep(0.2, 0.75, vLocal.x*sign(w.x)/max(abs(w.x), 0.1));
        float hit = step(0.5, fan + (n0 - 0.5)*0.5)*side*smoothstep(0.05, 0.5, uDirt);
        m = max(m, hit);
        streak = mix(streak, ray, hit*0.7);
      }
      m = max(m, smoothstep(-0.1, -0.6, ln.y)*min(1.0, uDirt*2.5));   // underside is always plastered
    } else {
      // Mud packs between the lugs first, then cakes patchily over the sidewall and rim.
      float rr = length(vLocal.yz);
      float groove = 1.0 - smoothstep(uTireR - 0.032, uTireR - 0.014, rr);
      m *= smoothstep(0.35, 0.6, n0 + uDirt*0.25);
      m = max(m, groove*smoothstep(0.06, 0.45, uDirt)*smoothstep(uTireR*0.6, uTireR*0.8, rr));
    }
    // Specks: dense just above the coat edge, thinning out with height.
    float above = edgeY - line;
    float sn = vnoise(q.zy*vec2(21.0, 27.0) + q.x*23.0);
    float thr = 0.78 + 0.18*smoothstep(0.0, 0.8, above) - uDirt*0.1;
    m = max(m, smoothstep(thr, thr + 0.04, sn)*step(0.0, above)*min(1.0, uDirt*1.6));
    // Drips run down from the caked zone.
    float drip = smoothstep(0.78, 0.9, vnoise(vec2(vLocal.x*29.0 + vLocal.z*27.0, vLocal.y*1.6 + uSeed)));
    m = max(m, drip*smoothstep(line + 0.3, line, vLocal.y)*step(0.0, above)*uDirt);
    if(vMat == 2) m = smoothstep(line + 0.3, line - 0.3, vLocal.y)*(0.3 + 0.25*n0);   // glass: thin smeared film
    m = clamp(m, 0.0, 1.0);
    thick = m*(0.55 + 0.45*fbm2(q.zy*6.0 + q.x*5.0));
    thick *= mix(1.0, 0.55, smoothstep(uDirtRange.x, uDirtRange.y, vLocal.y));   // thinner up high
  }
  if(m > 0.01){
    // Bumpy cake: perturb the normal from the mud height field, chunkier where thick.
    if(uQuality > 0){
      float e = 0.012, h0 = mudHeight(vLocal);
      vec3 g = vec3(mudHeight(vLocal + vec3(e,0,0)) - h0, mudHeight(vLocal + vec3(0,e,0)) - h0, mudHeight(vLocal + vec3(0,0,e)) - h0)/e;
      n = normalize(n - (g - dot(g, n)*n)*0.035*thick);
    }
    // Wet and dark low down and where thick; dries to pale grey-tan up high and in thin films.
    float hgt = smoothstep(uDirtRange.x, uDirtRange.y, vLocal.y);
    float wetM = clamp(smoothstep(0.35, 0.85, thick)*(1.0 - hgt*0.6), 0.0, 1.0);
    vec3 mudDry = vec3(0.52,0.47,0.38), mudWet = vec3(0.25,0.20,0.15);
    float vstreak = vnoise(vec2(vLocal.x*31.0 + vLocal.z*31.0, vLocal.y*3.0));
    vec3 mc = mix(mudDry, mudWet, wetM)*(0.86 + 0.16*vstreak + 0.12*(streak - 0.5));
    mc = pow(mc, vec3(2.2));
    alb = mix(alb, mc, m);
    rough = mix(rough, mix(0.95, 0.35, wetM), m);
    metal = mix(metal, 0.0, m);
    coat *= 1.0 - m;
    glow *= 1.0 - m;
  }
  float sh = shadowAt(vWorld, n);
  vec3 c = pbr(alb, n, vWorld, rough, metal, sh, ao);
  if(coat > 0.0){
    // Clearcoat lobe over paint/glass: sharp sun glint + mirror-like sky reflection at grazing angles.
    vec3 V = normalize(uCamPos - vWorld), H = normalize(uSunDir + V);
    float NoV = max(dot(n, V), 1e-3), NoL = max(dot(n, uSunDir), 0.0);
    float Fc = 0.04 + 0.96*pow(1.0 - NoV, 5.0);
    vec3 R = reflect(-V, n);
    c += coat*(envColor(R, 0.04)*Fc*clamp(1.0 + dot(R, n), 0.0, 1.0)*ao*0.75 +
               kSun*D_GGX(max(dot(n, H), 0.0), 0.06*0.06)*V_Smith(NoV, NoL, 0.0036)*Fc*NoL*sh*3.14159);
  }
  c += alb*glow*(1.0 + 7.0*uNight);
  if(leafT > 0.0){
    // Light through thin leaves when looking toward the sun.
    vec3 V1 = normalize(uCamPos - vWorld);
    c += alb*kSun*pow(max(dot(-V1, uSunDir), 0.0), 3.0)*0.55*sh;
  }
  float outA = uAlpha;
  if(uGlass == 2){
    // Tinted see-through glass: more opaque at grazing angles and where mud films it.
    float NoV = abs(dot(n, normalize(uCamPos - vWorld)));
    outA = clamp(0.58 + 0.38*pow(1.0 - NoV, 3.0) + 0.6*m, 0.0, 0.97)*uAlpha;
  }
  frag = vec4(finalize(c, vWorld), outA);
}
)";

const char* kGrassVS = R"(
layout(location=0) in vec4 aBlade;   // xyz local position, w = height fraction (0 root .. 1 tip)
uniform sampler2D uHeight;
uniform sampler2D uState;
uniform vec2 uOrigin;
uniform float uCell;
uniform mat4 uVP;
uniform vec2 uGridOrigin;
uniform float uSpacing;
uniform int uGrid;
uniform float uRadius;
uniform vec4 uOcc[8];
out vec3 vWorld; out float vT; out vec3 vTint; out vec3 vN;
void main(){
  ivec2 g = ivec2(gl_InstanceID % uGrid, gl_InstanceID / uGrid);
  vec2 cellW = uGridOrigin + vec2(g)*uSpacing;
  vec2 id = floor(cellW/uSpacing + 0.5);
  vec2 rnd = hash2(id*0.731);
  vec2 wxz = cellW + (rnd - 0.5)*uSpacing;
  ivec2 tc = clamp(ivec2((wxz - uOrigin)/uCell + 0.5), ivec2(0), ivec2(TN));
  vec2 hw = texelFetch(uHeight, tc, 0).rg;
  vec4 st = texelFetch(uState, tc, 0);
  float cover = smoothstep(0.3, 0.75, st.b)*(1.0 - st.a);       // bare where worn or rutted
  cover *= 0.35 + 0.65*smoothstep(0.25, 0.65, vnoise(wxz*0.8 + 5.0));  // clumpy, uneven sward
  if(hw.g > hw.r - 0.02) cover = 0.0;                           // under water
  float dist = length(wxz - uCamPos.xz);
  float sc = cover*(0.65 + 0.7*hash(id + 3.1))*(1.0 - smoothstep(uRadius*0.6, uRadius, dist));
  // Pressed flat under vehicles.
  for(int i = 0; i < 8; i++){
    vec4 o = uOcc[i];
    if(o.w <= 0.0) continue;
    vec2 d = wxz - o.xy; float c = cos(o.z), s = sin(o.z);
    vec2 l = vec2(c*d.x - s*d.y, s*d.x + c*d.y)/vec2(o.w*0.26, o.w*0.6);
    sc *= mix(0.25, 1.0, smoothstep(0.7, 1.1, length(l)));
  }
  float yaw = hash(id + 7.7)*6.2831853;
  float cy = cos(yaw), sy = sin(yaw);
  vec3 lp = aBlade.xyz*vec3(1.0, sc, 1.0);
  lp = vec3(cy*lp.x - sy*lp.z, lp.y, sy*lp.x + cy*lp.z);
  // Wind: gusts travel across the field; tips bend most.
  float gust = vnoise(wxz*0.15 + vec2(uTime*0.6, uTime*0.25));
  float sway = (0.04 + 0.12*gust)*(0.6 + 0.4*sin(uTime*2.3 + rnd.x*6.28));
  lp.xz += vec2(0.8, 0.45)*sway*aBlade.w*aBlade.w*sc*2.0;
  vec3 w = vec3(wxz.x, hw.r, wxz.y) + lp;
  vWorld = w; vT = aBlade.w;
  vTint = grassColor(wxz)*(0.85 + 0.3*hash(id + 1.3));
  vN = normalize(vec3(-sy, 0.6, cy));
  gl_Position = sc < 0.04 ? vec4(2.0, 2.0, 2.0, 1.0) : uVP*vec4(w, 1.0);
}
)";
const char* kGrassFS = R"(
in vec3 vWorld; in float vT; in vec3 vTint; in vec3 vN;
out vec4 frag;
void main(){
  // Darker, greener roots; sun-bleached tips. Blades are thin, so they glow when backlit.
  vec3 alb = pow(mix(vTint*vec3(0.42,0.48,0.36), vTint*vec3(1.1,1.04,0.86), vT), vec3(2.2));
  vec3 n = normalize(vN); if(!gl_FrontFacing) n = -n;
  vec3 V = normalize(uCamPos - vWorld);
  float sh = shadowAt(vWorld, vec3(0.0, 1.0, 0.0));
  float ndl = abs(dot(n, uSunDir))*0.6 + 0.4*max(uSunDir.y, 0.0);
  float trans = pow(max(dot(-V, uSunDir), 0.0), 4.0)*0.6;
  vec3 amb = vec3(0.36,0.42,0.50)*0.65*(0.45 + 0.55*vT)*mix(1.0, 0.06, uNight);
  vec3 c = alb*(kSun*(ndl + trans)*sh + amb) + localLights(alb, vec3(0.04), vec3(0.0, 1.0, 0.0), vWorld, V, 0.6);
  frag = vec4(finalize(c, vWorld), 1.0);
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
  gStars = 1.0;
  vec3 c = withClouds(skyColor(d), d);
  c = c*(2.51*c+0.03)/(c*(2.43*c+0.59)+0.14);
  frag = vec4(pow(clamp(c,0.0,1.0), vec3(1.0/2.2)), 1.0);
}
)";

const char* kParticleVS = R"(
layout(location=0) in vec2 aCorner;
layout(location=1) in vec4 iPos;
layout(location=2) in vec4 iCol;
layout(location=3) in float iType;
layout(location=4) in vec3 iVel;
uniform mat4 uVP; uniform vec3 uRight; uniform vec3 uUp;
out vec2 vUV; out vec4 vCol; out float vType; out vec3 vWorld; out float vSeed;
void main(){
  vec3 rgt = uRight, up = uUp;
  float stretch = 1.0;
  // Spray and water droplets streak along their velocity (motion blur in a ~1/30 s exposure).
  if(iType > 5.5){   // rain: long thin streaks along the fall direction
    vec3 v = iVel - dot(iVel, cross(uRight, uUp))*cross(uRight, uUp);
    float sp = max(length(v), 0.1);
    up = v/sp; rgt = normalize(cross(up, cross(uRight, uUp))); stretch = 16.0;
  } else if(iType > 0.5 && iType < 2.5){
    vec3 v = iVel - dot(iVel, cross(uRight, uUp))*cross(uRight, uUp);
    float sp = length(v);
    if(sp > 0.5){ up = v/sp; rgt = normalize(cross(up, cross(uRight, uUp))); stretch = 1.0 + min(sp*0.035/max(iPos.w, 0.01), 4.0); }
  }
  vec3 p = iPos.xyz + (rgt*aCorner.x + up*aCorner.y*stretch)*iPos.w;
  vUV = aCorner; vCol = iCol; vType = iType; vWorld = p; vSeed = fract(iPos.x*7.13 + iPos.z*3.71);
  gl_Position = uVP*vec4(p, 1.0);
}
)";
const char* kParticleFS = R"(
in vec2 vUV; in vec4 vCol; in float vType; in vec3 vWorld; in float vSeed;
uniform int uSoft; uniform vec3 uRight; uniform vec3 uUp; out vec4 frag;
void main(){
  float d = length(vUV);
  bool rain = vType > 5.5;
  bool soft = (vType > 2.5 && vType < 4.5) || rain;   // mist, smoke, rain
  if((uSoft == 1) != soft) discard;
  if(rain){
    float a = vCol.a*0.7*(1.0 - abs(vUV.x))*(1.0 - smoothstep(0.6, 1.0, abs(vUV.y)));
    if(a < 0.01) discard;
    vec3 c = vec3(0.62,0.66,0.72)*(0.6 + 0.4*max(uSunDir.y, 0.0));
    frag = vec4(finalize(pow(c, vec3(2.2)), vWorld), a);
    return;
  }
  float a = vCol.a;
  vec3 alb = pow(vCol.rgb, vec3(2.2));
  if(soft){
    a *= smoothstep(1.0, 0.2, d)*(0.7 + 0.3*vnoise(vUV*3.0 + vSeed*9.0));
    if(a < 0.01) discard;
    vec3 c = alb*(kSun*0.45*(0.5 + 0.5*max(uSunDir.y, 0.0)) + vec3(0.35,0.42,0.55)*0.7*mix(1.0, 0.08, uNight));
    frag = vec4(finalize(c, vWorld), a);
    return;
  }
  float edge = 0.55 + 0.45*vnoise(vUV*2.5 + vSeed*17.0);
  if(d > edge) discard;
  // Lit as a lumpy sphere: wet mud and water droplets get a sharp highlight.
  vec2 q = vUV/edge;
  float z = sqrt(max(0.0, 1.0 - dot(q, q)));
  vec3 fwd = cross(uRight, uUp);
  vec3 lumps = vec3(vnoise(vUV*4.0 + vSeed*31.0) - 0.5, vnoise(vUV*4.0 + 7.0 + vSeed*13.0) - 0.5, 0.0);
  vec3 nl = normalize(vec3(q, z) + lumps*(vType < 0.5 ? 0.9 : 0.2));
  vec3 n = normalize(uRight*nl.x + uUp*nl.y - fwd*nl.z);
  bool water = vType > 1.5 && vType < 2.5;
  float rough = water ? 0.12 : (vType < 0.5 ? 0.65 : 0.5);
  float sh = shadowAt(vWorld, n);
  vec3 c = pbr(alb, n, vWorld, rough, 0.0, sh, 0.8 + 0.2*z);
  if(water) c = mix(c, envColor(reflect(normalize(vWorld - uCamPos), n), 0.05), 0.35);
  frag = vec4(finalize(c, vWorld), 1.0);
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

const char* kPostVS = R"(
out vec2 vUV;
void main(){ vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2); vUV = p; gl_Position = vec4(p*2.0-1.0, 0.0, 1.0); }
)";
// Bloom: mode 0 = bright pass + downsample from the scene, 1 = separable 9-tap gaussian.
const char* kBloomFS = R"(
in vec2 vUV;
uniform sampler2D uSrc;
uniform int uMode;
uniform vec2 uTexel, uDir;
out vec4 frag;
void main(){
  if(uMode == 0){
    vec3 c = vec3(0.0);
    for(int y = -1; y <= 1; y += 2) for(int x = -1; x <= 1; x += 2) c += texture(uSrc, vUV + vec2(x, y)*uTexel).rgb;
    c *= 0.25;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    frag = vec4(c*smoothstep(0.62, 0.95, l), 1.0);
    return;
  }
  const float w[5] = float[5](0.227, 0.194, 0.122, 0.054, 0.016);
  vec3 c = texture(uSrc, vUV).rgb*w[0];
  for(int i = 1; i < 5; i++){
    vec2 o = uDir*uTexel*float(i)*1.4;
    c += (texture(uSrc, vUV + o).rgb + texture(uSrc, vUV - o).rgb)*w[i];
  }
  frag = vec4(c, 1.0);
}
)";
// Half-res pass: r = ambient occlusion from the depth buffer (view-space hemisphere samples),
// g = sun shafts (radial blur of the visible sky toward the sun's screen position).
const char* kAoFS = R"(
in vec2 vUV;
uniform sampler2D uDepth;
uniform sampler2D uScene;
uniform vec2 uNF;       // near, far
uniform vec2 uTanH;     // tan(fov/2)*aspect, tan(fov/2)
uniform vec2 uTexel;    // full-res texel
uniform int uAoOn;
uniform vec3 uSunUV;    // xy screen pos, z strength (0 = sun behind or off)
out vec4 frag;
float lin(float d){ float z = d*2.0 - 1.0; return 2.0*uNF.x*uNF.y/(uNF.y + uNF.x - z*(uNF.y - uNF.x)); }
vec3 viewPos(vec2 uv){ float z = lin(texture(uDepth, uv).r); return vec3((uv*2.0 - 1.0)*uTanH*z, -z); }
float h21(vec2 p){ p = fract(p*vec2(233.34, 851.73)); p += dot(p, p+23.45); return fract(p.x*p.y); }
void main(){
  float ao = 1.0, shaft = 0.0;
  float d0 = texture(uDepth, vUV).r;
  if(uAoOn == 1 && d0 < 0.9999){
    vec3 P = viewPos(vUV);
    vec3 Px = viewPos(vUV + vec2(uTexel.x*2.0, 0.0)), Py = viewPos(vUV + vec2(0.0, uTexel.y*2.0));
    vec3 N = normalize(cross(Px - P, Py - P));
    if(N.z < 0.0) N = -N;
    float dist = -P.z;
    float rad = clamp(0.35 + dist*0.012, 0.35, 1.2);
    // 4x4 interleaved rotations; the post pass blurs over exactly that footprint so it averages out.
    ivec2 ip = ivec2(gl_FragCoord.xy) & 3;
    const float bayer[16] = float[16](0.0,8.0,2.0,10.0, 12.0,4.0,14.0,6.0, 3.0,11.0,1.0,9.0, 15.0,7.0,13.0,5.0);
    float a0 = (bayer[ip.y*4 + ip.x] + 0.5)/16.0*6.2832, occ = 0.0;
    vec3 T = normalize(abs(N.y) < 0.9 ? cross(N, vec3(0,1,0)) : cross(N, vec3(1,0,0))), B = cross(N, T);
    for(int i = 0; i < 10; i++){
      float fi = float(i) + 0.5;
      float a = a0 + fi*2.39996, r = sqrt(fi/10.0);
      vec3 s = P + N*(0.02 + dist*0.002) + (T*cos(a)*r + B*sin(a)*r + N*(0.25 + 0.75*fract(fi*0.618)))*rad*r;
      vec2 suv = (s.xy/(-s.z))/uTanH*0.5 + 0.5;
      if(suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;
      float sd = lin(texture(uDepth, suv).r);
      float diff = (-s.z) - sd;
      // Thresholds grow with distance (depth precision) and ignore blade-thin occluders.
      occ += smoothstep(0.06 + dist*0.008, 0.14 + dist*0.016, diff)*smoothstep(rad*2.5, rad*0.5, diff);
    }
    ao = pow(1.0 - occ/10.0, 1.4);
    ao = mix(1.0, ao, smoothstep(140.0, 60.0, dist));
  }
  if(uSunUV.z > 0.0){
    vec2 dir = (uSunUV.xy - vUV)/24.0;
    vec2 uv = vUV + dir*h21(gl_FragCoord.yx);
    float w = 1.0;
    for(int i = 0; i < 24; i++){
      if(uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
      float sky = step(0.9999, texture(uDepth, uv).r);
      vec3 c = texture(uScene, uv).rgb;
      shaft += sky*smoothstep(0.55, 1.0, dot(c, vec3(0.3, 0.5, 0.2)))*w;
      w *= 0.93;
      uv += dir;
    }
    float ds = length((uSunUV.xy - vUV)*vec2(uTanH.x/uTanH.y, 1.0));
    shaft *= uSunUV.z/10.0*(1.0 - smoothstep(0.08, 0.75, ds))*0.8;   // fade with screen distance from the sun
  }
  frag = vec4(ao, shaft, 0.0, 1.0);
}
)";
const char* kPostFS = R"(
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uBloomK;
uniform vec4 uSplats[16];
uniform int uSplatN;
uniform vec2 uRes;
uniform sampler2D uAo;
uniform sampler2D uDepth;
uniform vec3 uShaftCol;
uniform int uDebug;   // 1 = show AO, 2 = show sun shafts
uniform int uAoOn;
out vec4 frag;
float h21(vec2 p){ p = fract(p*vec2(233.34, 851.73)); p += dot(p, p+23.45); return fract(p.x*p.y); }
void main(){
  vec2 uv = vUV;
  float aspect = uRes.x/uRes.y;
  float mud = 0.0; vec2 refr = vec2(0.0); float shade = 0.0;
  for(int i = 0; i < 16; i++){
    if(i >= uSplatN) break;
    vec4 s = uSplats[i];
    vec2 d = (uv - s.xy)*vec2(aspect, 1.0);
    float ang = atan(d.y, d.x), fi = float(i);
    float edge = s.z*(0.78 + 0.16*sin(ang*5.0 + fi*1.7) + 0.08*sin(ang*11.0 + fi*3.1));
    float dist = length(d);
    float m = 1.0 - smoothstep(edge*0.82, edge, dist);
    if(s.w > 0.0){ mud = max(mud, m*s.w); shade += (1.0 - smoothstep(edge, edge*1.25, dist))*s.w*0.25; }
    else refr += normalize(d + 1e-5)*m*(-s.w)*(dist/max(edge,1e-4))*s.z*0.5;
  }
  vec2 suv = clamp(uv - refr, 0.001, 0.999);
  vec3 col = texture(uScene, suv).rgb;
  // Light unsharp mask to win back detail the MSAA resolve softens.
  vec2 px = 1.0/uRes;
  vec3 blur = (texture(uScene, suv + vec2(px.x, 0.0)).rgb + texture(uScene, suv - vec2(px.x, 0.0)).rgb +
               texture(uScene, suv + vec2(0.0, px.y)).rgb + texture(uScene, suv - vec2(0.0, px.y)).rgb)*0.25;
  col = max(col + (col - blur)*0.35, 0.0);
  // Ambient occlusion: depth-aware 3x3 upsample of the half-res term so it doesn't bleed across edges.
  {
    float sum = 1.0, wsum = 1.0;
    if(uAoOn == 1){
      // Depth-aware 4x4 blur in half-res texels: matches the AO pass's 4x4 rotation pattern.
      float dc = texture(uDepth, suv).r;
      vec2 aot = 2.0/uRes;
      sum = 0.0; wsum = 0.0;
      for(int y = 0; y < 4; y++) for(int x = 0; x < 4; x++){
        vec2 o = (vec2(x, y) - 1.5)*aot;
        float wgt = 1.0/(1.0 + abs(texture(uDepth, suv + o).r - dc)*4000.0);
        sum += texture(uAo, suv + o).r*wgt; wsum += wgt;
      }
    }
    col *= mix(1.0, sum/wsum, 0.85);
    col += uShaftCol*texture(uAo, suv).g;
    if(uDebug == 1){ frag = vec4(vec3(sum/wsum), 1.0); return; }
    if(uDebug == 2){ frag = vec4(vec3(texture(uAo, suv).g), 1.0); return; }
  }
  // Bloom: screen-blend so highlights glow without blowing out.
  vec3 b = texture(uBloom, suv).rgb*uBloomK;
  col = 1.0 - (1.0 - col)*(1.0 - b);
  col *= 1.0 - clamp(shade, 0.0, 0.35);
  vec3 mudCol = vec3(0.24, 0.18, 0.12)*(0.75 + 0.5*h21(floor(uv*uRes/2.5)));
  col = mix(col, mudCol, clamp(mud, 0.0, 0.94));
  vec2 q = uv - 0.5;
  col *= 1.0 - dot(q, q)*0.6;                      // vignette
  col = mix(col, col*vec3(1.05, 1.0, 0.93), 0.6);  // warm late-afternoon grade
  float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));
  col = mix(vec3(luma), col, 1.08);                 // a touch more saturation
  col = mix(col, col*col*(3.0 - 2.0*col), 0.18);    // gentle S-curve contrast
  frag = vec4(col, 1.0);
}
)";

std::string src(const char* body) {
    return std::string(kHeader) + "#define TN " + std::to_string(Terrain::N - 1) + "\n" + kCommon + body;
}

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
    progPost = compileProgram("post", std::string(kHeader) + kPostVS, std::string(kHeader) + kPostFS);
    progBloom = compileProgram("bloom", std::string(kHeader) + kPostVS, std::string(kHeader) + kBloomFS);
    progAo = compileProgram("ao", std::string(kHeader) + kPostVS, std::string(kHeader) + kAoFS);
    progGrass = compileProgram("grass", src(kGrassVS), src(kGrassFS));
    {
        // One tuft: fourteen thin single-triangle blades spread over the cell, leaning outward.
        std::vector<float> gv;
        Rng r(77);
        for (int b = 0; b < 14; b++) {
            float a = r.range(0.f, 2 * PI), h = r.range(0.10f, 0.30f), wd = r.range(0.008f, 0.014f);
            float lean = r.range(0.02f, 0.10f), ox = r.range(-0.2f, 0.2f), oz = r.range(-0.2f, 0.2f);
            Vec3 side{std::cos(a), 0, std::sin(a)}, out{-side.z, 0, side.x};
            auto v = [&](float s, float t) {
                Vec3 p = Vec3{ox, 0, oz} + side * (s * wd * (1.f - t)) + out * (lean * t) + Vec3{0, h * t, 0};
                gv.insert(gv.end(), {p.x, p.y, p.z, t});
            };
            v(-1, 0); v(1, 0); v(0, 1);
        }
        grassVerts = GLsizei(gv.size() / 4);
        glGenVertexArrays(1, &grassVao);
        glBindVertexArray(grassVao);
        glGenBuffers(1, &grassVbo);
        glBindBuffer(GL_ARRAY_BUFFER, grassVbo);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(gv.size() * 4), gv.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 16, nullptr);
        glBindVertexArray(0);
    }
    glGenVertexArrays(1, &postVao);
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
    const int steps[4] = {1, 2, 4, 8};
    for (int l = 0; l < 4; l++) {
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
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 48, (void*)0);
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 48, (void*)16);
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 48, (void*)32);
    glVertexAttribDivisor(3, 1);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, 48, (void*)36);
    glVertexAttribDivisor(4, 1);
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

void Renderer::setSun(float az, float el) {
    sunDir = normalize(Vec3{std::cos(el * DEG) * std::sin(az * DEG), std::sin(el * DEG), std::cos(el * DEG) * std::cos(az * DEG)});
}

void Renderer::ensureTargets() {
    if (targetW == width && targetH == height && msFbo) return;
    targetW = width;
    targetH = height;
    if (!msFbo) {
        glGenFramebuffers(1, &msFbo);
        glGenRenderbuffers(1, &msColor);
        glGenRenderbuffers(1, &msDepth);
        glGenFramebuffers(1, &resolveFbo);
        glGenTextures(1, &resolveTex);
    }
    GLint maxS = 4;
    glGetIntegerv(GL_MAX_SAMPLES, &maxS);
    GLsizei samples = std::min<GLint>(4, maxS);
    glBindRenderbuffer(GL_RENDERBUFFER, msColor);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, msDepth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth);
    glBindTexture(GL_TEXTURE_2D, resolveTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!resolveDepth) glGenTextures(1, &resolveDepth);
    glBindTexture(GL_TEXTURE_2D, resolveDepth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, resolveDepth, 0);
    aoW = std::max(1, width / 2);
    aoH = std::max(1, height / 2);
    if (!aoTex) { glGenTextures(1, &aoTex); glGenFramebuffers(1, &aoFbo); }
    glBindTexture(GL_TEXTURE_2D, aoTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, aoW, aoH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, aoFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, aoTex, 0);
    bloomW = std::max(1, width / 4);
    bloomH = std::max(1, height / 4);
    for (int i = 0; i < 2; i++) {
        if (!bloomTex[i]) { glGenTextures(1, &bloomTex[i]); glGenFramebuffers(1, &bloomFbo[i]); }
        glBindTexture(GL_TEXTURE_2D, bloomTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, bloomW, bloomH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::postPass(const RenderFrame& f) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(postVao);
    // Half-res AO + sun shafts.
    {
        float aspect = float(width) / float(std::max(1, height));
        float th = std::tan(f.fov * 0.5f);
        Vec3 sp = f.camPos + sunDir * 500.f;
        float cx = viewProj.at(0, 0) * sp.x + viewProj.at(0, 1) * sp.y + viewProj.at(0, 2) * sp.z + viewProj.at(0, 3);
        float cy = viewProj.at(1, 0) * sp.x + viewProj.at(1, 1) * sp.y + viewProj.at(1, 2) * sp.z + viewProj.at(1, 3);
        float cw = viewProj.at(3, 0) * sp.x + viewProj.at(3, 1) * sp.y + viewProj.at(3, 2) * sp.z + viewProj.at(3, 3);
        float shaftK = 0;
        Vec3 su{0.5f, 0.5f, 0};
        if (cw > 0.f && quality >= 1) {
            su.x = cx / cw * 0.5f + 0.5f;
            su.y = cy / cw * 0.5f + 0.5f;
            float onScreen = clampf(1.6f - std::max(std::fabs(su.x - 0.5f), std::fabs(su.y - 0.5f)) * 1.6f, 0.f, 1.f);
            shaftK = onScreen * (1.f - weather[0]) * (1.f - night) * (0.35f + 0.65f * weather[3]);
        }
        su.z = shaftK;
        glViewport(0, 0, aoW, aoH);
        glBindFramebuffer(GL_FRAMEBUFFER, aoFbo);
        glUseProgram(progAo);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, resolveDepth);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, resolveTex);
        glUniform1i(glGetUniformLocation(progAo, "uDepth"), 5);
        glUniform1i(glGetUniformLocation(progAo, "uScene"), 1);
        glUniform2f(glGetUniformLocation(progAo, "uNF"), 0.15f, 600.f);
        glUniform2f(glGetUniformLocation(progAo, "uTanH"), th * aspect, th);
        glUniform2f(glGetUniformLocation(progAo, "uTexel"), 1.f / float(width), 1.f / float(height));
        glUniform1i(glGetUniformLocation(progAo, "uAoOn"), quality >= 2 ? 1 : 0);
        glUniform3f(glGetUniformLocation(progAo, "uSunUV"), su.x, su.y, su.z);
        if (quality >= 2 || shaftK > 0.f) glDrawArrays(GL_TRIANGLES, 0, 3);
        else { glClearColor(1.f, 0.f, 0.f, 1.f); glClear(GL_COLOR_BUFFER_BIT); }   // both effects off: skip the pass
        Vec3 sc = weather[3] > 0.f ? Vec3{1.0f, 0.72f, 0.42f} : Vec3{1.0f, 0.93f, 0.78f};
        shaftCol = sc * 0.55f;
    }
    // Bloom chain at quarter resolution: bright pass, then horizontal + vertical blur.
    glViewport(0, 0, bloomW, bloomH);
    glUseProgram(progBloom);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(progBloom, "uSrc"), 0);
    glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo[0]);
    glBindTexture(GL_TEXTURE_2D, resolveTex);
    glUniform1i(glGetUniformLocation(progBloom, "uMode"), 0);
    glUniform2f(glGetUniformLocation(progBloom, "uTexel"), 1.f / float(width), 1.f / float(height));
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glUniform1i(glGetUniformLocation(progBloom, "uMode"), 1);
    glUniform2f(glGetUniformLocation(progBloom, "uTexel"), 1.f / float(bloomW), 1.f / float(bloomH));
    for (int pass = 0; pass < 2; pass++) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo[1]);
        glBindTexture(GL_TEXTURE_2D, bloomTex[0]);
        glUniform2f(glGetUniformLocation(progBloom, "uDir"), 1.f, 0.f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo[0]);
        glBindTexture(GL_TEXTURE_2D, bloomTex[1]);
        glUniform2f(glGetUniformLocation(progBloom, "uDir"), 0.f, 1.f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glUseProgram(progPost);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomTex[0]);
    glUniform1i(glGetUniformLocation(progPost, "uBloom"), 1);
    glUniform1f(glGetUniformLocation(progPost, "uBloomK"), 0.45f + 0.6f * night);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, resolveTex);
    glUniform1i(glGetUniformLocation(progPost, "uScene"), 0);
    int n = std::min<int>(16, int(f.lens.size() / 4));
    glUniform1i(glGetUniformLocation(progPost, "uSplatN"), n);
    if (n) glUniform4fv(glGetUniformLocation(progPost, "uSplats"), n, f.lens.data());
    glUniform2f(glGetUniformLocation(progPost, "uRes"), float(width), float(height));
    // Units 4/5: 2 and 3 stay bound to the shadow-compare maps the scene shaders expect.
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, aoTex);
    glUniform1i(glGetUniformLocation(progPost, "uAo"), 4);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, resolveDepth);
    glUniform1i(glGetUniformLocation(progPost, "uDepth"), 5);
    glActiveTexture(GL_TEXTURE0);
    glUniform3f(glGetUniformLocation(progPost, "uShaftCol"), shaftCol.x, shaftCol.y, shaftCol.z);
    glUniform1i(glGetUniformLocation(progPost, "uDebug"), debugView);
    glUniform1i(glGetUniformLocation(progPost, "uAoOn"), quality >= 2 ? 1 : 0);
    glBindVertexArray(postVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);
}

const GpuMesh* Renderer::mesh(const std::string& part, const CadParams& params) {
    std::string key = part;
    char buf[48];
    for (auto& kv : params) { std::snprintf(buf, sizeof buf, ";%s=%.3f", kv.first.c_str(), kv.second); key += buf; }
    auto it = meshes.find(key);
    if (it != meshes.end()) return &it->second;
    CadResult r = cadBuild(part, params);
    if (!r.error.empty()) std::printf("[cad] %s\n", r.error.c_str());
    GpuMesh g = uploadMesh(r.mesh);
    float ident[20] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0};
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
    scratchDir.resize(size_t(w) * h * 2);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int k = t.idx(x0 + i, z0 + j), o = j * w + i;
            scratch[o * 2] = t.h[k];
            scratch[o * 2 + 1] = t.water[k];
            scratch8[o * 4] = uint8_t(clampf(t.moist[k], 0, 1) * 255);
            scratch8[o * 4 + 1] = uint8_t(clampf((t.h[k] - t.base[k]) / 1.5f, 0, 1) * 255);
            scratch8[o * 4 + 2] = t.grass[k];
            scratch8[o * 4 + 3] = t.track[k];
            scratchDir[size_t(o) * 2] = t.trackDir[k];
            scratchDir[size_t(o) * 2 + 1] = uint8_t(t.soil[k] * 40);   // soil type for the tint
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
        if (!texTrackDir) glGenTextures(1, &texTrackDir);
        glBindTexture(GL_TEXTURE_2D, texTrackDir);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, w, h, 0, GL_RG, GL_UNSIGNED_BYTE, scratchDir.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    } else {
        glBindTexture(GL_TEXTURE_2D, texHeight);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x0, z0, w, h, GL_RG, GL_FLOAT, scratch.data());
        glBindTexture(GL_TEXTURE_2D, texState);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x0, z0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, scratch8.data());
        glBindTexture(GL_TEXTURE_2D, texTrackDir);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x0, z0, w, h, GL_RG, GL_UNSIGNED_BYTE, scratchDir.data());
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
        groups[key].push_back(&p);  // mud amount rides in the instance tint, so it doesn't split batches
    }
    for (auto& g : groups) {
        CadResult r = cadBuild(g.second[0]->part, g.second[0]->params);
        if (!r.error.empty()) { std::printf("[cad] %s\n", r.error.c_str()); continue; }
        Batch b;
        b.mesh = uploadMesh(r.mesh);
        // Glass drawn see-through (blended, last) for parts with something behind it.
        const std::string& part = g.second[0]->part;
        // Lot cars (props/car_*, *_parked) have cabins behind their glass too.
        const bool lotCar = part.rfind("props/car_", 0) == 0 || part.find("_parked.mbcad") != std::string::npos;
        b.seeThrough = part.rfind("vehicles/", 0) == 0 || part.find("announcer_tower") != std::string::npos || lotCar;
        std::vector<float> inst;
        for (auto* p : g.second) {
            Mat4 m = Mat4::trs(p->pos, Quat::axisAngle({0, 1, 0}, p->yaw), {p->scale, p->scale, p->scale});
            inst.insert(inst.end(), m.m, m.m + 16);
            inst.insert(inst.end(), {1, 1, 1, p->dirt});
        }
        setInstances(b.mesh, inst.data(), int(g.second.size()), true);
        // Distance LOD: plentiful parts that declare a 'lod' param get a cheap far version.
        // Lot cars are split into small groups by paint colour, so they qualify at any count.
        if (g.second.size() >= 20 || lotCar) {
            CadParams lp = g.second[0]->params;
            lp["lod"] = 1;
            CadResult lr = cadBuild(g.second[0]->part, lp);
            if (lr.error.empty() && lr.mesh.verts.size() < r.mesh.verts.size()) {
                b.lodMesh = uploadMesh(lr.mesh);
                b.hasLod = true;
            }
        }
        b.count = int(g.second.size());
        b.inst = inst;
        Vec3 ext = b.mesh.bmax - b.mesh.bmin;
        b.radius = 0.6f * length(ext);
        for (auto* p : g.second) b.centers.push_back(p->pos + Vec3{0, 0.5f * (b.mesh.bmin.y + b.mesh.bmax.y) * p->scale, 0});
        for (auto* p : g.second) b.scales.push_back(p->scale);
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
            int lod = d < 34.f ? 0 : (d < 80.f ? 1 : (d < 150.f ? 2 : 3));
            if (quality == 0) lod = std::min(3, lod + 1);
            glUniform2i(locChunk, cx * CH, cz * CH);
            glDrawElements(GL_TRIANGLES, lods[lod].count, GL_UNSIGNED_INT, (void*)(intptr_t)lods[lod].offset);
        }
}

// Static props are culled per instance (distance + frustum) and re-uploaded each pass: the map has
// thousands of trees but only a few hundred are ever in view.
void Renderer::cullStatic(const Mat4& vp, Vec3 eye, float maxDist) {
    float planes[6][4];
    extractPlanes(vp, planes);
    const float lodD2 = sq(quality >= 2 ? 60.f : (quality == 1 ? 45.f : 30.f));
    for (auto& b : batches) {
        scratch.clear();
        scratchFar.clear();
        int n = 0, nf = 0;
        for (size_t k = 0; k < b.centers.size(); k++) {
            const Vec3& c = b.centers[k];
            float r = b.radius * b.scales[k];
            float d2 = sq(c.x - eye.x) + sq(c.z - eye.z);
            if (d2 > sq(maxDist + r)) continue;
            bool vis = true;
            for (int pl = 0; pl < 6 && vis; pl++)
                if (planes[pl][0] * c.x + planes[pl][1] * c.y + planes[pl][2] * c.z + planes[pl][3] < -r * std::sqrt(planes[pl][0] * planes[pl][0] + planes[pl][1] * planes[pl][1] + planes[pl][2] * planes[pl][2])) vis = false;
            if (!vis) continue;
            if (b.hasLod && d2 > lodD2) {
                scratchFar.insert(scratchFar.end(), b.inst.begin() + long(k * 20), b.inst.begin() + long(k * 20 + 20));
                nf++;
                continue;
            }
            scratch.insert(scratch.end(), b.inst.begin() + long(k * 20), b.inst.begin() + long(k * 20 + 20));
            n++;
        }
        b.visible = n;
        b.visibleFar = nf;
        if (n) setInstances(b.mesh, scratch.data(), n, true);
        if (nf) setInstances(b.lodMesh, scratchFar.data(), nf, true);
    }
}

void Renderer::drawStatic(GLuint prog, int glassMode) {
    Mat4 I = Mat4::identity();
    glUniformMatrix4fv(glGetUniformLocation(prog, "uModel"), 1, GL_FALSE, I.m);
    glUniform1f(glGetUniformLocation(prog, "uDirtU"), 0.f);
    glUniform2f(glGetUniformLocation(prog, "uDirtRange"), -0.45f, 1.0f);
    glUniform1f(glGetUniformLocation(prog, "uSeed"), 7.f);
    glUniform1f(glGetUniformLocation(prog, "uAlpha"), 1.f);
    GLint locGlass = glGetUniformLocation(prog, "uGlass");
    for (auto& b : batches) {
        if (glassMode == 2 && !b.seeThrough) continue;
        if (locGlass >= 0) glUniform1i(locGlass, glassMode == 0 ? 0 : (b.seeThrough ? glassMode : 0));
        if (b.visible) drawMesh(b.mesh, b.visible);
        if (b.visibleFar && glassMode != 2) drawMesh(b.lodMesh, b.visibleFar);
    }
    if (locGlass >= 0) glUniform1i(locGlass, 0);
}

void Renderer::drawDynamic(World& w, const RenderFrame& f, GLuint prog, bool shadowPass, bool transparent) {
    GLint locModel = glGetUniformLocation(prog, "uModel"), locDirt = glGetUniformLocation(prog, "uDirtU");
    GLint locRange = glGetUniformLocation(prog, "uDirtRange"), locAlpha = glGetUniformLocation(prog, "uAlpha");
    GLint locSeed = glGetUniformLocation(prog, "uSeed"), locMirror = glGetUniformLocation(prog, "uMirror");
    GLint locWheels = glGetUniformLocation(prog, "uWheels"), locTire = glGetUniformLocation(prog, "uTireR");
    GLint locGlass = glGetUniformLocation(prog, "uGlass");
    const float noWheels[16] = {0};
    glUniform1i(locGlass, 0);
    glUniform1i(locMirror, 0);
    glUniform4fv(locWheels, 4, noWheels);
    glUniform1f(locTire, 0.f);
    auto itemUniforms = [&](const DrawItem& it) {
        glUniformMatrix4fv(locModel, 1, GL_FALSE, it.model.m);
        glUniform1f(locDirt, it.dirt);
        glUniform2f(locRange, it.dirtLo, it.dirtHi);
        glUniform1f(locAlpha, it.alpha);
        glUniform1f(locSeed, it.seed);
        glUniform1i(locMirror, it.mirror ? 1 : 0);
        glUniform4fv(locWheels, 4, it.wheels);
        glUniform1f(locTire, it.tireR);
    };
    Mat4 I = Mat4::identity();
    if (transparent) {
        for (const DrawItem& it : f.items) {
            if (!it.mesh || it.alpha >= 0.99f) continue;
            itemUniforms(it);
            drawMesh(*it.mesh, 1);
        }
        // Vehicle glass last, blended over the cab interiors drawn in the opaque pass.
        glUniform1i(locGlass, 2);
        for (const DrawItem& it : f.items) {
            if (!it.mesh || !it.glass || it.alpha < 0.99f) continue;
            itemUniforms(it);
            drawMesh(*it.mesh, 1);
        }
        glUniform1i(locGlass, 0);
        glUniform1i(locMirror, 0);
        glUniform4fv(locWheels, 4, noWheels);
        glUniform1f(locTire, 0.f);
        return;
    }
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
            scratch.insert(scratch.end(), {1, 1, 1, 0});
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
        scratch.insert(scratch.end(), {1, 1, 1, 0});
    }
    glUniform1f(locDirt, 0.f);
    if (!w.posts.empty()) { setInstances(postInst, scratch.data(), int(w.posts.size()), true); drawMesh(postInst, int(w.posts.size())); }
    // Vehicles, wheels, etc.
    for (const DrawItem& it : f.items) {
        if (!it.mesh) continue;
        if (it.alpha < 0.99f) continue;  // drawn in the transparent pass
        itemUniforms(it);
        glUniform1i(locGlass, (it.glass && !shadowPass) ? 1 : 0);
        drawMesh(*it.mesh, 1);
    }
    glUniform1i(locGlass, 0);
    glUniform1i(locMirror, 0);
    glUniform4fv(locWheels, 4, noWheels);
    glUniform1f(locTire, 0.f);
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
        glUniform1i(glGetUniformLocation(p, "uQuality"), quality);
        glUniform3f(glGetUniformLocation(p, "uSunColor"), sunColor.x, sunColor.y, sunColor.z);
        glUniform4f(glGetUniformLocation(p, "uWeather"), weather[0], weather[1], weather[2], weather[3]);
        glUniform1f(glGetUniformLocation(p, "uFlash"), flash);
        glUniform1f(glGetUniformLocation(p, "uNight"), night);
        int nl = int(std::min<size_t>(16, lights.size()));
        glUniform1i(glGetUniformLocation(p, "uNumLights"), nl);
        if (nl) {
            float lp[64], lc[64], ld[64];
            for (int i = 0; i < nl; i++) {
                const Light& L = lights[size_t(i)];
                lp[i * 4] = L.pos.x; lp[i * 4 + 1] = L.pos.y; lp[i * 4 + 2] = L.pos.z; lp[i * 4 + 3] = L.range;
                lc[i * 4] = L.color.x; lc[i * 4 + 1] = L.color.y; lc[i * 4 + 2] = L.color.z; lc[i * 4 + 3] = 0;
                ld[i * 4] = L.dir.x; ld[i * 4 + 1] = L.dir.y; ld[i * 4 + 2] = L.dir.z; ld[i * 4 + 3] = L.cosCone;
            }
            glUniform4fv(glGetUniformLocation(p, "uLightPos"), nl, lp);
            glUniform4fv(glGetUniformLocation(p, "uLightCol"), nl, lc);
            glUniform4fv(glGetUniformLocation(p, "uLightDir"), nl, ld);
        }
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
    if (quality >= 1) {
        cullStatic(lightVP, center, ext * 1.5f);
        drawStatic(progShadow);
    }
    drawDynamic(w, f, progShadow, true);
    glUseProgram(progFence);
    glUniformMatrix4fv(glGetUniformLocation(progFence, "uVP"), 1, GL_FALSE, lightVP.m);
    glUniform1i(glGetUniformLocation(progFence, "uShadowPass"), 1);
    glUniform1i(glGetUniformLocation(progFence, "uShadow"), 3);
    glBindVertexArray(fenceVao);
    glDrawArrays(GL_TRIANGLES, 0, fenceVerts);
    glDisable(GL_POLYGON_OFFSET_FILL);

    // ---- main pass (into the MSAA target when post-processing is on) ----
    if (postEnabled) ensureTargets();
    glBindFramebuffer(GL_FRAMEBUFFER, postEnabled ? msFbo : 0);
    glViewport(0, 0, width, height);
    glClearColor(0.5f, 0.65f, 0.9f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, shadowTex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texHeight);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texState);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, texTrackDir);
    glActiveTexture(GL_TEXTURE0);

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
    glUniform1i(glGetUniformLocation(progTerrain, "uTrackDir"), 4);
    glUniform2f(glGetUniformLocation(progTerrain, "uOrigin"), w.terrain.originX(), w.terrain.originZ());
    glUniform1f(glGetUniformLocation(progTerrain, "uCell"), Terrain::CELL);
    {
        float occ[32] = {0};
        size_t n = std::min<size_t>(32, f.occluders.size());
        std::copy(f.occluders.begin(), f.occluders.begin() + long(n), occ);
        glUniform4fv(glGetUniformLocation(progTerrain, "uOcc"), 8, occ);
    }
    drawTerrain(false, viewProj);

    // Grass tufts around the camera (instanced, positioned from the heightfield in the shader)
    if (quality >= 1) {
        const int G = quality >= 2 ? 84 : 56;
        const float spacing = 0.36f;
        setCommon(progGrass);
        glUniform1i(glGetUniformLocation(progGrass, "uHeight"), 0);
        glUniform1i(glGetUniformLocation(progGrass, "uState"), 1);
        glUniform2f(glGetUniformLocation(progGrass, "uOrigin"), w.terrain.originX(), w.terrain.originZ());
        glUniform1f(glGetUniformLocation(progGrass, "uCell"), Terrain::CELL);
        glUniformMatrix4fv(glGetUniformLocation(progGrass, "uVP"), 1, GL_FALSE, viewProj.m);
        // Grid centred a little ahead of the camera, snapped so tufts never swim.
        Vec3 ahead = f.camPos + fwd * (G * spacing * 0.3f);
        float gx = std::floor(ahead.x / spacing) * spacing - G * spacing * 0.5f;
        float gz = std::floor(ahead.z / spacing) * spacing - G * spacing * 0.5f;
        glUniform2f(glGetUniformLocation(progGrass, "uGridOrigin"), gx, gz);
        glUniform1f(glGetUniformLocation(progGrass, "uSpacing"), spacing);
        glUniform1i(glGetUniformLocation(progGrass, "uGrid"), G);
        glUniform1f(glGetUniformLocation(progGrass, "uRadius"), G * spacing * 0.5f);
        float occ[32] = {0};
        size_t n = std::min<size_t>(32, f.occluders.size());
        std::copy(f.occluders.begin(), f.occluders.begin() + long(n), occ);
        glUniform4fv(glGetUniformLocation(progGrass, "uOcc"), 8, occ);
        glDisable(GL_CULL_FACE);
        glBindVertexArray(grassVao);
        glDrawArraysInstanced(GL_TRIANGLES, 0, grassVerts, G * G);
    }

    // Meshes
    setCommon(progMesh);
    glUniformMatrix4fv(glGetUniformLocation(progMesh, "uVP"), 1, GL_FALSE, viewProj.m);
    cullStatic(viewProj, f.camPos, quality >= 2 ? 260.f : (quality == 1 ? 200.f : 150.f));
    drawStatic(progMesh, 1);
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
            scratch.insert(scratch.end(), {p.p.x, p.p.y, p.p.z, p.size, p.r, p.g, p.b, p.a, float(int(p.type)), p.v.x, p.v.y, p.v.z});
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
    setCommon(progMesh);
    glUniformMatrix4fv(glGetUniformLocation(progMesh, "uVP"), 1, GL_FALSE, viewProj.m);
    glDepthMask(GL_FALSE);
    drawDynamic(w, f, progMesh, false, true);
    drawStatic(progMesh, 2);   // see-through glass on parked vehicles and the announcer booth
    glDepthMask(GL_TRUE);
    if (np) {
        glDepthMask(GL_FALSE);
        setCommon(progParticle);
        glUniform1i(glGetUniformLocation(progParticle, "uSoft"), 1);
        glBindVertexArray(particleVao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, np);
        glDepthMask(GL_TRUE);
    }
    glDisable(GL_BLEND);
    if (postEnabled) postPass(f);
    glBindVertexArray(0);
}

}  // namespace mb
