#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "geometry.h"
#include "log.h"

namespace eng {

// ============================================================ шейдеры
static const char* kFullVS = R"(#version 300 es
out vec2 v_uv;
void main(){ vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)); v_uv = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }
)";

static const char* kPbrVS = R"(#version 300 es
layout(location=0) in vec3 a_pos;
layout(location=1) in vec3 a_nrm;
layout(location=2) in vec4 a_col;
layout(location=3) in uvec4 a_j;
layout(location=4) in vec4 a_w;
layout(location=5) in vec2 a_uv;
uniform mat4 u_vp; uniform mat4 u_model; uniform mat3 u_nrm; uniform mat4 u_joints[64]; uniform int u_skinned;
out vec3 v_n; out vec3 v_w; out vec4 v_c; out vec2 v_uv;
void main(){
  vec4 p = vec4(a_pos, 1.0); vec3 n = a_nrm;
  if (u_skinned != 0) {
    mat4 s = a_w.x*u_joints[a_j.x] + a_w.y*u_joints[a_j.y] + a_w.z*u_joints[a_j.z] + a_w.w*u_joints[a_j.w];
    p = s * p; n = mat3(s) * n;
  }
  vec4 w = u_model * p;
  v_w = w.xyz; v_n = u_nrm * n; v_c = a_col; v_uv = a_uv;
  gl_Position = u_vp * w;
}
)";

static const char* kPbrFS = R"(#version 300 es
precision highp float; precision highp sampler2D; precision highp sampler2DShadow;
in vec3 v_n; in vec3 v_w; in vec4 v_c; in vec2 v_uv;
uniform sampler2D u_tex; uniform sampler2D u_texMR; uniform sampler2D u_texN; uniform sampler2D u_texE; uniform sampler2D u_texO;
uniform sampler2DShadow u_shadow; uniform sampler2D u_shadowRaw;
uniform float u_uvPerM, u_shRange;
uniform int u_hasTex, u_hasMR, u_hasN, u_hasE, u_hasO, u_alphaMode, u_shadowOn, u_useVC, u_nPl, u_isFloor;
uniform float u_cutoff, u_metal, u_rough, u_nScale, u_texel, u_shadowSoft, u_pass, u_skyBright, u_outScale;
uniform vec3 u_emis, u_cam, u_L, u_sunCol, u_skyTop, u_skyHor, u_skyGnd;
uniform vec4 u_tint; uniform mat4 u_lightVP;
uniform vec4 u_pl[8]; uniform vec3 u_plc[8]; uniform vec4 u_sp[8]; uniform float u_spIn[8]; uniform int u_glass;
out vec4 o;
const float PI = 3.14159265;
const vec2 POIS[16] = vec2[](vec2(-0.94201624,-0.39906216), vec2(0.94558609,-0.76890725), vec2(-0.094184101,-0.92938870), vec2(0.34495938,0.29387760),
  vec2(-0.91588581,0.45771432), vec2(-0.81544232,-0.87912464), vec2(-0.38277543,0.27676845), vec2(0.97484398,0.75648379),
  vec2(0.44323325,-0.97511554), vec2(0.53742981,-0.47373420), vec2(-0.26496911,-0.41893023), vec2(0.79197514,0.19090188),
  vec2(-0.24188840,0.99706507), vec2(-0.81409955,0.91437590), vec2(0.19984126,0.78641367), vec2(0.14383161,-0.14100790));
float hash(vec2 p){ return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }
vec3 skyAt(vec3 d){
  float h = d.y;
  vec3 up = mix(u_skyHor, u_skyTop, pow(clamp(h, 0.0, 1.0), 0.6));
  vec3 dn = mix(u_skyHor, u_skyGnd, smoothstep(0.0, 0.25, -h));
  return (h >= 0.0 ? up : dn) * u_skyBright;
}
vec3 irradiance(vec3 n){
  vec3 up = mix(u_skyHor, u_skyTop, clamp(n.y, 0.0, 1.0));
  return mix(u_skyGnd, up, smoothstep(-0.4, 0.4, n.y)) * u_skyBright;
}
vec2 envBRDF(float r, float nv){
  const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
  const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
  vec4 rr = r * c0 + c1;
  float a004 = min(rr.x * rr.x, exp2(-9.28 * nv)) * rr.x + rr.y;
  return vec2(-1.04, 1.04) * a004 + rr.zw;
}
float shadowAt(vec3 wp, vec3 n, float ndl){
  if (u_shadowOn == 0) return 1.0;
  vec4 lp = u_lightVP * vec4(wp + n * (0.012 + 0.045 * (1.0 - ndl)), 1.0);
  vec3 c = lp.xyz / lp.w * 0.5 + 0.5;
  if (c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0 || c.z > 1.0) return 1.0;
  float ang = hash(gl_FragCoord.xy + u_pass * 7.31) * 6.2831853;
  mat2 R = mat2(cos(ang), sin(ang), -sin(ang), cos(ang));
  // 1) поиск перекрывающих: насколько далеко от поверхности стоит предмет, отбрасывающий тень
  float searchR = u_texel * (5.0 + 5.0 * u_shadowSoft);
  float sum = 0.0, cnt = 0.0;
  for (int i = 0; i < 8; i++) {
    float d = texture(u_shadowRaw, c.xy + R * POIS[i] * searchR).r;
    if (d < c.z - 0.0008) { sum += d; cnt += 1.0; }
  }
  if (cnt < 0.5) return 1.0;
  float dist = (c.z - sum / cnt) * u_shRange;                     // метры между предметом и тенью
  // 2) ширина полутени растёт с расстоянием: у ног тень резкая, дальше мягкая
  float rad = clamp(dist * 0.045 * u_shadowSoft * u_uvPerM, u_texel * 1.3, u_texel * 26.0);
  float s = 0.0;
  for (int i = 0; i < 16; i++) s += texture(u_shadow, vec3(c.xy + R * POIS[i] * rad, c.z - 0.0005));
  return s / 16.0;
}
mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv){
  vec3 dp1 = dFdx(p), dp2 = dFdy(p); vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
  vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
  vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
  vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
  float invmax = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-12));
  return mat3(T * invmax, B * invmax, N);
}
vec3 brdf(vec3 N, vec3 V, vec3 L, vec3 diff, vec3 F0, float a2, float NoV){
  vec3 H = normalize(V + L + vec3(1e-5));
  float NoL = clamp(dot(N, L), 0.0, 1.0), NoH = clamp(dot(N, H), 0.0, 1.0), VoH = clamp(dot(V, H), 0.0, 1.0);
  float d = NoH * NoH * (a2 - 1.0) + 1.0;
  float D = a2 / (PI * d * d + 1e-6);
  float Vis = 0.5 / (NoL * sqrt(NoV * NoV * (1.0 - a2) + a2) + NoV * sqrt(NoL * NoL * (1.0 - a2) + a2) + 1e-4);
  vec3 F = F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
  return diff / PI * (1.0 - F) + D * Vis * F;
}
void main(){
  vec3 Ng = normalize(v_n);
  vec3 N = Ng; if (!gl_FrontFacing) { N = -N; Ng = -Ng; }
  vec4 baseT = u_hasTex != 0 ? texture(u_tex, v_uv) : vec4(1.0);
  vec4 col = (u_useVC != 0 ? v_c : vec4(1.0)) * u_tint * baseT;
  if (u_alphaMode == 1 && col.a < u_cutoff) discard;
  if (u_hasN != 0) {
    vec3 m = texture(u_texN, v_uv).xyz * 2.0 - 1.0;
    m.xy *= u_nScale;
    N = normalize(cotangentFrame(N, v_w, v_uv) * m);
  }
  vec3 V = normalize(u_cam - v_w);
  float NoV = max(dot(N, V), 1e-3);
  float rough = u_rough, metal = u_metal;
  if (u_hasMR != 0) { vec3 mr = texture(u_texMR, v_uv).rgb; rough *= mr.g; metal *= mr.b; }
  rough = clamp(rough, 0.045, 1.0);
  float a = rough * rough, a2 = a * a;
  vec3 albedo = col.rgb;
  vec3 F0 = mix(vec3(0.04), albedo, metal);
  vec3 diff = albedo * (1.0 - metal);
  float ao = u_hasO != 0 ? texture(u_texO, v_uv).r : 1.0;
  vec3 emis = u_emis;
  if (u_hasE != 0) emis *= texture(u_texE, v_uv).rgb;

  vec3 Lo = vec3(0.0);
  vec3 L = normalize(u_L);
  float NoLs = max(dot(N, L), 0.0);
  if (NoLs > 0.0 && dot(u_sunCol, vec3(1.0)) > 0.0) {
    float sh = shadowAt(v_w, Ng, max(dot(Ng, L), 0.0));
    Lo += brdf(N, V, L, diff, F0, a2, NoV) * u_sunCol * NoLs * sh;
  }
  for (int i = 0; i < 8; i++) {
    if (i >= u_nPl) break;
    vec3 d = u_pl[i].xyz - v_w; float r = u_pl[i].w;
    float d2 = dot(d, d);
    float w = clamp(1.0 - (d2 * d2) / (r * r * r * r), 0.0, 1.0);
    vec3 Lp = d * inversesqrt(max(d2, 1e-6));
    float nl = max(dot(N, Lp), 0.0);
    float cone = 1.0;
    if (u_sp[i].w > -1.5) { float cd = dot(-Lp, u_sp[i].xyz); cone = smoothstep(u_sp[i].w, u_spIn[i], cd); cone *= cone; }
    Lo += brdf(N, V, Lp, diff, F0, a2, NoV) * u_plc[i] * (w * w / (d2 + 0.05)) * nl * cone;
  }
  vec3 irr = irradiance(N);
  vec3 R = reflect(-V, N);
  vec3 env = mix(skyAt(R), irradiance(R), smoothstep(0.08, 0.85, rough));
  vec2 ab = envBRDF(rough, NoV);
  vec3 amb = (diff * irr + env * (F0 * ab.x + ab.y)) * ao;
  vec3 lit = Lo + amb + emis;
  if (u_isFloor != 0) {
    float dist = length(v_w - u_cam);
    vec3 dir = normalize(v_w - u_cam);
    lit = mix(lit, skyAt(vec3(dir.x, 0.0, dir.z)), smoothstep(14.0, 36.0, dist));
  }
  if (u_glass != 0) {
    // стекло: отражение неба и бликов поверх, прозрачность растёт на скользящем угле (Френель)
    float fr = pow(1.0 - NoV, 4.0);
    float F = mix(0.04, 1.0, fr);
    vec3 spec = Lo * 0.0;
    vec3 Lg = normalize(u_L);
    vec3 Hg = normalize(V + Lg);
    float nh = max(dot(N, Hg), 0.0);
    float sp = pow(nh, 180.0) * 2.5 + pow(nh, 28.0) * 0.12;
    vec3 sunc = dot(u_sunCol, vec3(1.0)) > 0.0 ? u_sunCol : vec3(0.0);
    spec += sunc * sp * max(dot(N, Lg), 0.0);
    vec3 envG = skyAt(R);
    spec += envG * (0.05 + F * 0.95);
    for (int i = 0; i < 8; i++) {
      if (i >= u_nPl) break;
      vec3 d = u_pl[i].xyz - v_w; float r = u_pl[i].w; float d2 = dot(d, d);
      float w = clamp(1.0 - (d2 * d2) / (r * r * r * r), 0.0, 1.0);
      vec3 Lp = d * inversesqrt(max(d2, 1e-6));
      float cone = 1.0;
      if (u_sp[i].w > -1.5) { float cd = dot(-Lp, u_sp[i].xyz); cone = smoothstep(u_sp[i].w, u_spIn[i], cd); cone *= cone; }
      float nhp = max(dot(N, normalize(V + Lp)), 0.0);
      spec += u_plc[i] * (w * w / (d2 + 0.05)) * (pow(nhp, 120.0) * 1.2 + 0.03 * max(dot(N, Lp), 0.0)) * cone;
    }
    float a = clamp(col.a + F * (1.0 - col.a), 0.0, 1.0);
    vec3 tintc = col.rgb * irr * col.a * 0.8;
    o = vec4((spec + tintc) * u_outScale, a);
    return;
  }
  o = vec4(lit * u_outScale, u_alphaMode == 2 ? col.a : 1.0);
}
)";

static const char* kDepthVS = R"(#version 300 es
layout(location=0) in vec3 a_pos;
layout(location=2) in vec4 a_col;
layout(location=3) in uvec4 a_j;
layout(location=4) in vec4 a_w;
layout(location=5) in vec2 a_uv;
uniform mat4 u_vp; uniform mat4 u_model; uniform mat4 u_joints[64]; uniform int u_skinned;
out vec2 v_uv; out vec4 v_c;
void main(){
  vec4 p = vec4(a_pos, 1.0);
  if (u_skinned != 0) {
    mat4 s = a_w.x*u_joints[a_j.x] + a_w.y*u_joints[a_j.y] + a_w.z*u_joints[a_j.z] + a_w.w*u_joints[a_j.w];
    p = s * p;
  }
  v_uv = a_uv; v_c = a_col;
  gl_Position = u_vp * (u_model * p);
}
)";
static const char* kDepthFS = R"(#version 300 es
precision mediump float;
in vec2 v_uv; in vec4 v_c;
uniform sampler2D u_tex; uniform int u_hasTex; uniform int u_alphaMode; uniform float u_cutoff;
void main(){
  if (u_alphaMode == 1) { float a = v_c.a * (u_hasTex != 0 ? texture(u_tex, v_uv).a : 1.0); if (a < u_cutoff) discard; }
}
)";

static const char* kSkyFS = R"(#version 300 es
precision highp float; precision highp sampler2D;
in vec2 v_uv; out vec4 o;
uniform mat4 u_invVP; uniform vec3 u_cam, u_L, u_sunCol, u_skyTop, u_skyHor, u_skyGnd; uniform float u_skyBright, u_outScale, u_stars, u_moon;
float h3(vec3 p){ return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719))) * 43758.5453); }
void main(){
  vec2 ndc = v_uv * 2.0 - 1.0;
  vec4 w = u_invVP * vec4(ndc, 1.0, 1.0);
  vec3 d = normalize(w.xyz / w.w - u_cam);
  float h = d.y;
  vec3 up = mix(u_skyHor, u_skyTop, pow(clamp(h, 0.0, 1.0), 0.6));
  vec3 dn = mix(u_skyHor, u_skyGnd, smoothstep(0.0, 0.25, -h));
  vec3 c = (h >= 0.0 ? up : dn) * u_skyBright;
  float sd = max(dot(d, normalize(u_L)), 0.0);
  if (u_moon > 0.5) {
    c += vec3(0.85, 0.92, 1.1) * (smoothstep(0.99935, 0.99965, sd) * 1.1 + pow(sd, 400.0) * 0.06 + pow(sd, 40.0) * 0.015);
  } else {
    c += u_sunCol * (pow(sd, 1500.0) * 8.0 + pow(sd, 40.0) * 0.06);
  }
  if (u_stars > 0.001 && h > -0.02) {
    vec3 g = d * 150.0; vec3 id = floor(g); vec3 f = fract(g) - 0.5;
    float hv = h3(id);
    float st = step(0.985, hv) * smoothstep(0.42, 0.0, length(f)) * (0.35 + 0.65 * h3(id + 7.0));
    vec3 sc = mix(vec3(1.0, 0.85, 0.7), vec3(0.7, 0.8, 1.0), h3(id + 3.0));
    c += sc * st * u_stars * 3.0 * smoothstep(-0.02, 0.15, h);
  }
  o = vec4(c * u_outScale, 1.0);
}
)";
static const char* kSkyVS = R"(#version 300 es
out vec2 v_uv;
void main(){ vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)); v_uv = p; gl_Position = vec4(p * 2.0 - 1.0, 1.0, 1.0); }
)";


static const char* kBeamVS = R"(#version 300 es
uniform mat4 u_vp; uniform vec3 u_pos, u_dir, u_rx, u_ry; uniform float u_len, u_tan;
out float v_t; out vec3 v_n; out vec3 v_w;
void main(){
  const int N = 28;
  int tri = gl_VertexID / 3, vi = gl_VertexID - tri * 3;
  float a0 = 6.2831853 * float(tri) / float(N), a1 = 6.2831853 * float(tri + 1) / float(N);
  float am = 0.5 * (a0 + a1);
  vec3 p; vec3 r;
  if (vi == 0) { r = u_rx * cos(am) + u_ry * sin(am); p = u_pos; v_t = 0.0; }
  else { float a = vi == 1 ? a0 : a1; r = u_rx * cos(a) + u_ry * sin(a); p = u_pos + u_dir * u_len + r * (u_len * u_tan); v_t = 1.0; }
  v_n = normalize(r - u_dir * u_tan);
  v_w = p;
  gl_Position = u_vp * vec4(p, 1.0);
}
)";
static const char* kBeamFS = R"(#version 300 es
precision highp float;
in float v_t; in vec3 v_n; in vec3 v_w;
uniform vec3 u_cam, u_col, u_dir; uniform float u_k, u_outScale;
out vec4 o;
void main(){
  vec3 V = normalize(u_cam - v_w);
  float f = abs(dot(normalize(v_n), V));
  float axl = abs(dot(V, u_dir));
  float a = u_k * (0.3 + 0.7 * pow(max(f, 0.0), 1.6) + 1.2 * pow(axl, 6.0)) * pow(max(1.0 - v_t, 0.0), 1.4) * smoothstep(0.0, 0.03, v_t);
  a = min(a, 0.35);
  o = vec4(u_col * a * u_outScale, 1.0);
}
)";

static const char* kSsaoFS = R"(#version 300 es
precision highp float; precision highp sampler2D;
in vec2 v_uv; out vec4 o;
uniform sampler2D u_depth; uniform mat4 u_invProj; uniform mat4 u_proj; uniform vec2 u_texel; uniform float u_radius, u_pass, u_strength, u_contact; uniform vec3 u_sunV;
vec3 viewPos(vec2 uv){
  float d = texture(u_depth, uv).r;
  vec4 p = u_invProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
  return p.xyz / p.w;
}
float hash(vec2 p){ return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }
void main(){
  float d0 = texture(u_depth, v_uv).r;
  if (d0 >= 0.99999) { o = vec4(1.0); return; }
  vec3 P = viewPos(v_uv);
  vec3 Pr = viewPos(v_uv + vec2(u_texel.x, 0.0)), Pl = viewPos(v_uv - vec2(u_texel.x, 0.0));
  vec3 Pu = viewPos(v_uv + vec2(0.0, u_texel.y)), Pd = viewPos(v_uv - vec2(0.0, u_texel.y));
  vec3 dx = abs(Pr.z - P.z) < abs(P.z - Pl.z) ? Pr - P : P - Pl;
  vec3 dy = abs(Pu.z - P.z) < abs(P.z - Pd.z) ? Pu - P : P - Pd;
  vec3 N = normalize(cross(dx, dy));
  if (dot(N, P) > 0.0) N = -N;
  vec3 T = normalize(cross(N, abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
  vec3 B = cross(N, T);
  float ang = hash(gl_FragCoord.xy + u_pass * 13.1) * 6.2831853;
  float occ = 0.0;
  for (int i = 0; i < 12; i++) {
    float fi = float(i) + 0.5;
    float r = sqrt(fi / 12.0), th = fi * 2.39996 + ang;
    vec3 s = vec3(cos(th) * r, sin(th) * r, sqrt(max(0.0, 1.0 - r * r)));
    float sc = mix(0.25, 1.0, fract(fi * 0.618 + u_pass * 0.37));
    vec3 sp = P + (T * s.x + B * s.y + N * s.z) * u_radius * sc;
    vec4 pp = u_proj * vec4(sp, 1.0);
    vec2 suv = pp.xy / pp.w * 0.5 + 0.5;
    float sd = viewPos(suv).z;
    float range = smoothstep(0.0, 1.0, u_radius / max(abs(P.z - sd), 1e-4));
    occ += (sd >= sp.z + (0.03 * u_radius + 0.002 * (-P.z)) ? 1.0 : 0.0) * range;
  }
  float fade = 1.0 - smoothstep(10.0, 28.0, -P.z);
  // контактная тень: короткий луч к солнцу по буферу глубины
  float contact = 0.0;
  if (u_contact > 0.0 && dot(N, u_sunV) > 0.05) {
    float jit = hash(gl_FragCoord.xy * 1.7 + u_pass * 3.1);
    for (int i = 0; i < 10; i++) {
      float t = (float(i) + 0.5 + jit * 0.7) / 10.0;
      vec3 sp = P + u_sunV * (0.6 * t * t + 0.02) + N * 0.008;
      vec4 pp = u_proj * vec4(sp, 1.0);
      vec2 suv = pp.xy / pp.w * 0.5 + 0.5;
      if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) break;
      float diff = viewPos(suv).z - sp.z;
      if (diff > 0.012 + 0.004 * (-P.z) && diff < 0.35) { contact = 1.0; break; }
    }
  }
  float ao = 1.0 - occ / 12.0 * u_strength * fade;
  ao *= 1.0 - contact * u_contact * 0.6 * fade;
  o = vec4(ao);
}
)";

static const char* kBlurFS = R"(#version 300 es
precision highp float; precision highp sampler2D;
in vec2 v_uv; out vec4 o;
uniform sampler2D u_ao; uniform sampler2D u_depth; uniform mat4 u_invProj; uniform vec2 u_texel;
float lin(vec2 uv){
  float d = texture(u_depth, uv).r;
  vec4 p = u_invProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
  return -p.z / p.w;
}
void main(){
  float zc = lin(v_uv);
  float sum = 0.0, wsum = 0.0;
  for (int y = -2; y <= 2; y++) for (int x = -2; x <= 2; x++) {
    vec2 uv = v_uv + vec2(float(x), float(y)) * u_texel;
    float z = lin(uv);
    float w = exp(-abs(z - zc) / (0.04 * zc + 0.02));
    sum += texture(u_ao, uv).r * w; wsum += w;
  }
  o = vec4(sum / max(wsum, 1e-4));
}
)";

static const char* kDownFS = R"(#version 300 es
precision highp float; precision highp sampler2D;
in vec2 v_uv; out vec4 o;
uniform sampler2D u_src; uniform vec2 u_texel; uniform float u_thresh, u_inScale; uniform int u_first;
vec3 s(vec2 d){ return texture(u_src, v_uv + d * u_texel).rgb * u_inScale; }
void main(){
  vec3 c = s(vec2(0.0)) * 0.5 + (s(vec2(-1.0,-1.0)) + s(vec2(1.0,-1.0)) + s(vec2(-1.0,1.0)) + s(vec2(1.0,1.0))) * 0.125;
  if (!(c.r < 1e6 && c.g < 1e6 && c.b < 1e6)) c = vec3(0.0);   // NaN/Inf не должны расползаться по мипам
  if (u_first != 0) {
    float l = max(c.r, max(c.g, c.b));
    c *= clamp(l - u_thresh, 0.0, 2.5) / max(l, 1e-4);
  }
  o = vec4(c, 1.0);
}
)";

static const char* kUpFS = R"(#version 300 es
precision highp float; precision highp sampler2D;
in vec2 v_uv; out vec4 o;
uniform sampler2D u_src; uniform vec2 u_texel;
vec3 s(vec2 d){ return texture(u_src, v_uv + d * u_texel).rgb; }
void main(){
  vec3 c = s(vec2(0.0)) * 4.0 + (s(vec2(-1.0,0.0)) + s(vec2(1.0,0.0)) + s(vec2(0.0,-1.0)) + s(vec2(0.0,1.0))) * 2.0
         + s(vec2(-1.0,-1.0)) + s(vec2(1.0,-1.0)) + s(vec2(-1.0,1.0)) + s(vec2(1.0,1.0));
  o = vec4(c / 16.0, 1.0);
}
)";

static const char* kToneFS = R"(#version 300 es
precision highp float; precision highp sampler2D;
in vec2 v_uv; out vec4 o;
uniform sampler2D u_hdr; uniform sampler2D u_ao; uniform sampler2D u_bloom;
uniform int u_useAo, u_useBloom;
uniform float u_exposure, u_bloomStr, u_contrast, u_sat, u_vig, u_inScale;
vec3 aces(vec3 x){ return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
float hash(vec2 p){ return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }
void main(){
  vec3 c = texture(u_hdr, v_uv).rgb * u_inScale;
  if (!(c.r < 1e6 && c.g < 1e6 && c.b < 1e6)) c = vec3(0.0);
  if (u_useAo != 0) c *= texture(u_ao, v_uv).r;
  if (u_useBloom != 0) c += texture(u_bloom, v_uv).rgb * u_bloomStr;
  c *= u_exposure;
  c = aces(c * 0.62);
  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
  c = mix(vec3(l), c, u_sat);
  c = mix(c * 12.92, 1.055 * pow(max(c, 0.0), vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
  c = (c - 0.5) * u_contrast + 0.5;
  vec2 q = v_uv - 0.5;
  c *= 1.0 - u_vig * smoothstep(0.25, 0.95, length(q) * 1.15);
  c += (hash(gl_FragCoord.xy) - 0.5) / 255.0;
  o = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";

static const char* kFxaaFS = R"(#version 300 es
precision highp float; precision highp sampler2D;
in vec2 v_uv; out vec4 o;
uniform sampler2D u_src; uniform vec2 u_texel;
float luma(vec3 c){ return dot(c, vec3(0.299, 0.587, 0.114)); }
vec3 t(vec2 uv){ return texture(u_src, uv).rgb; }
void main(){
  vec3 rgbNW = t(v_uv + vec2(-1.0,-1.0) * u_texel), rgbNE = t(v_uv + vec2(1.0,-1.0) * u_texel);
  vec3 rgbSW = t(v_uv + vec2(-1.0,1.0) * u_texel), rgbSE = t(v_uv + vec2(1.0,1.0) * u_texel);
  vec3 rgbM = t(v_uv);
  float lNW = luma(rgbNW), lNE = luma(rgbNE), lSW = luma(rgbSW), lSE = luma(rgbSE), lM = luma(rgbM);
  float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE))), lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
  vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));
  float dirReduce = max((lNW + lNE + lSW + lSE) * 0.25 * 0.125, 1.0 / 128.0);
  float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
  dir = clamp(dir * rcp, vec2(-8.0), vec2(8.0)) * u_texel;
  vec3 rgbA = 0.5 * (t(v_uv + dir * (1.0 / 3.0 - 0.5)) + t(v_uv + dir * (2.0 / 3.0 - 0.5)));
  vec3 rgbB = rgbA * 0.5 + 0.25 * (t(v_uv + dir * -0.5) + t(v_uv + dir * 0.5));
  float lB = luma(rgbB);
  o = vec4((lB < lMin || lB > lMax) ? rgbA : rgbB, 1.0);
}
)";

static const char* kAccFS = R"(#version 300 es
precision highp float; precision highp sampler2D;
in vec2 v_uv; out vec4 o;
uniform sampler2D u_hdr; uniform sampler2D u_ao; uniform float u_weight, u_inScale; uniform int u_useAo;
void main(){
  vec3 c = texture(u_hdr, v_uv).rgb * u_inScale;
  if (!(c.r < 1e6 && c.g < 1e6 && c.b < 1e6)) c = vec3(0.0);   // защита от NaN/Inf в одном проходе
  if (u_useAo != 0) c *= texture(u_ao, v_uv).r;
  o = vec4(c * u_weight, 1.0);
}
)";

static const char* kLineVS = R"(#version 300 es
layout(location=0) in vec3 a_p0;
layout(location=1) in vec3 a_p1;
layout(location=2) in vec2 a_corner;   // x: 0/1 вдоль отрезка, y: -1/+1 в сторону
layout(location=3) in vec4 a_col;
layout(location=4) in float a_w;
uniform mat4 u_vp; uniform vec2 u_res;
out vec4 v_c;
void main(){
  vec4 c0 = u_vp * vec4(a_p0, 1.0), c1 = u_vp * vec4(a_p1, 1.0);
  const float nearW = 0.02;
  v_c = a_col;
  if (c0.w < nearW && c1.w < nearW) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
  if (c0.w < nearW) { float t = (nearW - c0.w) / (c1.w - c0.w); c0 = mix(c0, c1, t); }
  if (c1.w < nearW) { float t = (nearW - c1.w) / (c0.w - c1.w); c1 = mix(c1, c0, t); }
  vec2 s0 = c0.xy / c0.w * u_res * 0.5, s1 = c1.xy / c1.w * u_res * 0.5;
  vec2 d = s1 - s0; float len = length(d);
  d = len > 1e-4 ? d / len : vec2(1.0, 0.0);
  vec2 n = vec2(-d.y, d.x);
  vec2 s = mix(s0, s1, a_corner.x) + n * a_corner.y * a_w * 0.5 + d * (a_corner.x * 2.0 - 1.0) * a_w * 0.25;
  vec4 c = mix(c0, c1, a_corner.x);
  float z = c.z / c.w;
  gl_Position = vec4(s / (u_res * 0.5), z, 1.0);
}
)";
static const char* kLineFS = R"(#version 300 es
precision mediump float;
in vec4 v_c; uniform float u_outScale; out vec4 o;
void main(){ o = vec4(v_c.rgb * u_outScale * v_c.a, v_c.a); }
)";

// ============================================================ LineBatch
bool LineBatch::init() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER), fs = glCreateShader(GL_FRAGMENT_SHADER);
    const char* v = kLineVS; const char* f = kLineFS;
    glShaderSource(vs, 1, &v, nullptr); glCompileShader(vs);
    glShaderSource(fs, 1, &f, nullptr); glCompileShader(fs);
    GLint ok = 0, ok2 = 0;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok); glGetShaderiv(fs, GL_COMPILE_STATUS, &ok2);
    if (!ok || !ok2) {
        char b[1024]; GLsizei n = 0;
        glGetShaderInfoLog(ok ? fs : vs, 1024, &n, b);
        ENG_LOG("line shader error: %s", b);
        return false;
    }
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs); glAttachShader(prog_, fs); glLinkProgram(prog_);
    glDeleteShader(vs); glDeleteShader(fs);
    glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) return false;
    uVP_ = glGetUniformLocation(prog_, "u_vp"); uRes_ = glGetUniformLocation(prog_, "u_res"); uOut_ = glGetUniformLocation(prog_, "u_outScale");
    glGenVertexArrays(1, &vao_); glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const GLsizei stride = 13 * 4;
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (const void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (const void*)24);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (const void*)32);
    glEnableVertexAttribArray(4); glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, (const void*)48);
    glBindVertexArray(0);
    return true;
}

void LineBatch::release() {
    if (prog_) glDeleteProgram(prog_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    prog_ = vao_ = vbo_ = 0;
    cap_ = 0;
}

void LineBatch::add(Vec3 a, Vec3 b, const float* c, float w) {
    static const float cx[6] = {0, 1, 1, 0, 1, 0}, cy[6] = {-1, -1, 1, -1, 1, 1};
    for (int i = 0; i < 6; i++) {
        float vtx[13] = {a.x, a.y, a.z, b.x, b.y, b.z, cx[i], cy[i], c[0], c[1], c[2], c[3], w};
        v_.insert(v_.end(), vtx, vtx + 13);
    }
}

void LineBatch::flush(const Mat4& vp, int w, int h, float outScale, bool depthTest) {
    if (v_.empty() || !prog_) { v_.clear(); return; }
    glUseProgram(prog_);
    glUniformMatrix4fv(uVP_, 1, GL_FALSE, vp.m);
    glUniform2f(uRes_, (float)w, (float)h);
    glUniform1f(uOut_, outScale);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    size_t bytes = v_.size() * sizeof(float);
    if (bytes > cap_) { cap_ = bytes * 2; glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cap_, nullptr, GL_STREAM_DRAW); }
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, v_.data(), GL_STREAM_DRAW);
    if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v_.size() / 13));
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    v_.clear();
}

// ============================================================ Renderer: инициализация
GLuint Renderer::compile(const char* vs, const char* fs, const char* tag) {
    auto sh = [&](GLenum t, const char* src) {
        GLuint s = glCreateShader(t);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) { char b[2048]; GLsizei n = 0; glGetShaderInfoLog(s, 2048, &n, b); ENG_LOG("shader %s error: %s", tag, b); glDeleteShader(s); return (GLuint)0; }
        return s;
    };
    GLuint v = sh(GL_VERTEX_SHADER, vs), f = sh(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char b[2048]; GLsizei n = 0; glGetProgramInfoLog(p, 2048, &n, b); ENG_LOG("link %s error: %s", tag, b); glDeleteProgram(p); return 0; }
    return p;
}

bool Renderer::buildPrograms() {
    pbr_ = compile(kPbrVS, kPbrFS, "pbr");
    depthP_ = compile(kDepthVS, kDepthFS, "depth");
    sky_ = compile(kSkyVS, kSkyFS, "sky");
    beam_ = compile(kBeamVS, kBeamFS, "beam");
    ssao_ = compile(kFullVS, kSsaoFS, "ssao");
    blur_ = compile(kFullVS, kBlurFS, "blur");
    down_ = compile(kFullVS, kDownFS, "down");
    up_ = compile(kFullVS, kUpFS, "up");
    tone_ = compile(kFullVS, kToneFS, "tone");
    fxaa_ = compile(kFullVS, kFxaaFS, "fxaa");
    acc_ = compile(kFullVS, kAccFS, "acc");
    if (!pbr_ || !depthP_ || !sky_ || !ssao_ || !blur_ || !down_ || !up_ || !tone_ || !fxaa_ || !acc_) return false;
    auto locs = [](GLuint p) {
        Locs L;
        auto u = [&](const char* n) { return glGetUniformLocation(p, n); };
        L.vp = u("u_vp"); L.model = u("u_model"); L.nrm = u("u_nrm"); L.joints = u("u_joints"); L.skinned = u("u_skinned");
        L.tex = u("u_tex"); L.texMR = u("u_texMR"); L.texN = u("u_texN"); L.texE = u("u_texE"); L.texO = u("u_texO"); L.shadow = u("u_shadow"); L.shadowRaw = u("u_shadowRaw"); L.uvPerM = u("u_uvPerM"); L.shRange = u("u_shRange");
        L.hasTex = u("u_hasTex"); L.hasMR = u("u_hasMR"); L.hasN = u("u_hasN"); L.hasE = u("u_hasE"); L.hasO = u("u_hasO");
        L.alphaMode = u("u_alphaMode"); L.cutoff = u("u_cutoff"); L.metal = u("u_metal"); L.rough = u("u_rough"); L.nScale = u("u_nScale");
        L.emis = u("u_emis"); L.tint = u("u_tint"); L.useVC = u("u_useVC");
        L.cam = u("u_cam"); L.L = u("u_L"); L.sunCol = u("u_sunCol"); L.lightVP = u("u_lightVP"); L.texel = u("u_texel");
        L.shadowSoft = u("u_shadowSoft"); L.shadowOn = u("u_shadowOn"); L.pass = u("u_pass");
        L.skyTop = u("u_skyTop"); L.skyHor = u("u_skyHor"); L.skyGnd = u("u_skyGnd"); L.skyBright = u("u_skyBright");
        L.sp = u("u_sp"); L.spIn = u("u_spIn"); L.glass = u("u_glass"); L.pl = u("u_pl"); L.plc = u("u_plc"); L.nPl = u("u_nPl"); L.outScale = u("u_outScale"); L.isFloor = u("u_isFloor");
        return L;
    };
    L_ = locs(pbr_);
    dL_ = locs(depthP_);
    return true;
}

bool Renderer::makeTarget(Target& t, int w, int h, GLenum fmt, bool depthTex, bool filter) {
    t.w = w; t.h = h;
    GLenum format = GL_RGBA, type = GL_UNSIGNED_BYTE;
    if (fmt == GL_RGBA16F) type = GL_HALF_FLOAT;
    else if (fmt == GL_R8) format = GL_RED;
    glGenTextures(1, &t.color);
    glBindTexture(GL_TEXTURE_2D, t.color);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)fmt, w, h, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color, 0);
    if (depthTex) {
        glGenTextures(1, &t.depth);
        glBindTexture(GL_TEXTURE_2D, t.depth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, t.depth, 0);
    }
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}

void Renderer::freeTarget(Target& t) {
    if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
    if (t.color) glDeleteTextures(1, &t.color);
    if (t.depth) glDeleteTextures(1, &t.depth);
    t = Target();
}

bool Renderer::testFloatTargets() {
    Target t;
    bool ok = makeTarget(t, 4, 4, GL_RGBA16F, false, true);
    freeTarget(t);
    while (glGetError() != GL_NO_ERROR) {}
    return ok;
}

void Renderer::freeSet(TargetSet& s) {
    freeTarget(s.hdr); freeTarget(s.accum); freeTarget(s.ldr);
    freeTarget(s.ao[0]); freeTarget(s.ao[1]);
    for (auto& b : s.bloom) freeTarget(b);
    s.bloom.clear();
    s.hasAccum = false;
}

void Renderer::ensureSet(TargetSet& s, int w, int h, bool accum) {
    if (s.hdr.w == w && s.hdr.h == h && (!accum || s.hasAccum)) return;
    freeSet(s);
    GLenum hf = hdrFloat_ ? GL_RGBA16F : GL_RGBA8;
    makeTarget(s.hdr, w, h, hf, true, true);
    makeTarget(s.ldr, w, h, GL_RGBA8, false, true);
    int aw = std::max(1, w / 2), ah = std::max(1, h / 2);
    makeTarget(s.ao[0], aw, ah, GL_R8, false, true);
    makeTarget(s.ao[1], aw, ah, GL_R8, false, true);
    int bw = w / 2, bh = h / 2;
    for (int i = 0; i < 5 && bw >= 2 && bh >= 2; i++) {
        Target b;
        makeTarget(b, bw, bh, hf, false, true);
        s.bloom.push_back(b);
        bw /= 2; bh /= 2;
    }
    if (accum) { makeTarget(s.accum, w, h, hdrFloat_ ? GL_RGBA16F : GL_RGBA8, false, true); s.hasAccum = true; }
}

int Renderer::maxExportSide() { GLint m = 4096; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m); return std::min<int>(m, 4096); }

bool Renderer::init() {
    if (!buildPrograms()) return false;
    hdrFloat_ = testFloatTargets();
    outScale_ = hdrFloat_ ? 1.f : 0.25f;
    inScale_ = 1.f / outScale_;
    if (!hdrFloat_) ENG_LOG("RGBA16F render targets unavailable: using RGBA8 fallback");
    glGenVertexArrays(1, &emptyVao_);
    // карта теней
    for (GLenum fmt : {(GLenum)GL_DEPTH_COMPONENT24, (GLenum)GL_DEPTH_COMPONENT16}) {
        glGenTextures(1, &shadowTex_);
        glBindTexture(GL_TEXTURE_2D, shadowTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)fmt, shadowSize_, shadowSize_, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glGenFramebuffers(1, &shadowFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0);
        const GLenum none = GL_NONE;
        glDrawBuffers(1, &none);
        glReadBuffer(GL_NONE);
        bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (ok) break;
        glDeleteFramebuffers(1, &shadowFbo_); glDeleteTextures(1, &shadowTex_);
        shadowFbo_ = shadowTex_ = 0;
    }
    if (shadowTex_) {  // тот же буфер глубины без сравнения: для поиска перекрывающих
        glGenSamplers(1, &rawSampler_);
        glSamplerParameteri(rawSampler_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glSamplerParameteri(rawSampler_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glSamplerParameteri(rawSampler_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(rawSampler_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(rawSampler_, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    }
    uint8_t white[4] = {255, 255, 255, 255};
    glGenTextures(1, &whiteTex_);
    glBindTexture(GL_TEXTURE_2D, whiteTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    floorMesh_ = MeshData();
    // пол из мелких плиток: огромные треугольники дают ступеньки глубины
    for (int ix = 0; ix < 20; ix++)
        for (int iz = 0; iz < 20; iz++)
            addBox(floorMesh_, {(ix - 9.5f) * 4.f, -0.05f, (iz - 9.5f) * 4.f}, {2.f, 0.05f, 2.f}, Color(1, 1, 1), -1);
    floorMesh_.skin = -1;
    floorMesh_.upload();
    if (!lines_.init()) return false;
    ready_ = true;
    return true;
}

void Renderer::release() {
    fxScene_ = nullptr;
    freeSet(rt_); freeSet(st_);
    floorMesh_.release();
    lines_.release();
    GLuint progs[] = {pbr_, depthP_, beam_, sky_, ssao_, blur_, down_, up_, tone_, fxaa_, acc_};
    for (GLuint p : progs) if (p) glDeleteProgram(p);
    pbr_ = depthP_ = beam_ = sky_ = ssao_ = blur_ = down_ = up_ = tone_ = fxaa_ = acc_ = 0;
    if (rawSampler_) glDeleteSamplers(1, &rawSampler_);
    rawSampler_ = 0;
    if (shadowFbo_) glDeleteFramebuffers(1, &shadowFbo_);
    if (shadowTex_) glDeleteTextures(1, &shadowTex_);
    if (whiteTex_) glDeleteTextures(1, &whiteTex_);
    if (emptyVao_) glDeleteVertexArrays(1, &emptyVao_);
    shadowFbo_ = shadowTex_ = whiteTex_ = emptyVao_ = 0;
    ready_ = false;
}

void Renderer::freeStillTargets() { freeSet(st_); }

void Renderer::fullscreen() {
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

// ============================================================ свет и тени
View Renderer::cameraView(const Object& cam, float aspect) {
    View v;
    Mat4 w = cam.world;
    // убрать масштаб из матрицы камеры
    Vec3 c0{w.m[0], w.m[1], w.m[2]}, c1{w.m[4], w.m[5], w.m[6]}, c2{w.m[8], w.m[9], w.m[10]};
    c0 = normalize(c0); c1 = normalize(c1); c2 = normalize(c2);
    Mat4 r;
    r.m[0] = c0.x; r.m[1] = c0.y; r.m[2] = c0.z; r.m[4] = c1.x; r.m[5] = c1.y; r.m[6] = c1.z; r.m[8] = c2.x; r.m[9] = c2.y; r.m[10] = c2.z;
    r.m[12] = w.m[12]; r.m[13] = w.m[13]; r.m[14] = w.m[14];
    v.view = inverse(r);
    v.proj = perspective(cam.fovDeg * kPi / 180.f, aspect, 0.1f, 150.f);
    v.eye = {w.m[12], w.m[13], w.m[14]};
    return v;
}

// Небо и солнце по времени суток: полдень, закат и ночь с луной и звёздами.
void Renderer::applyTimeOfDay(const RenderSettings& r, FrameLights& fl, bool sunObjExists) {
    float t = std::fmod(std::max(0.f, r.tod), 24.f);
    float s = std::sin(3.14159265f * (t - 6.f) / 12.f);  // -1 ночь, 0 горизонт, 1 полдень
    struct K { float s; float top[3], hor[3], gnd[3], br; float sun[3]; float sunK; };
    static const K ks[] = {
        {-0.35f, {0.008f, 0.012f, 0.05f}, {0.03f, 0.045f, 0.11f}, {0.015f, 0.018f, 0.03f}, 0.55f, {0.5f, 0.62f, 1.0f}, 0.f},
        {-0.10f, {0.05f, 0.06f, 0.20f}, {0.40f, 0.22f, 0.32f}, {0.05f, 0.04f, 0.06f}, 0.45f, {1.0f, 0.45f, 0.25f}, 0.f},
        {0.05f, {0.22f, 0.26f, 0.52f}, {1.00f, 0.50f, 0.28f}, {0.16f, 0.11f, 0.09f}, 0.55f, {1.0f, 0.5f, 0.25f}, 0.55f},
        {0.30f, {0.24f, 0.36f, 0.72f}, {0.95f, 0.78f, 0.62f}, {0.19f, 0.17f, 0.15f}, 0.6f, {1.0f, 0.82f, 0.6f}, 0.85f},
        {0.65f, {0.22f, 0.42f, 0.85f}, {0.78f, 0.84f, 0.92f}, {0.20f, 0.19f, 0.18f}, 0.6f, {1.0f, 0.95f, 0.9f}, 1.f},
    };
    const int n = (int)(sizeof ks / sizeof ks[0]);
    int i = 0;
    while (i < n - 2 && s > ks[i + 1].s) i++;
    float u = clampf((s - ks[i].s) / (ks[i + 1].s - ks[i].s), 0.f, 1.f);
    auto mx = [&](float a, float b) { return a + (b - a) * u; };
    for (int c = 0; c < 3; c++) {
        fl.skyTop[c] = mx(ks[i].top[c], ks[i + 1].top[c]);
        fl.skyHor[c] = mx(ks[i].hor[c], ks[i + 1].hor[c]);
        fl.skyGnd[c] = mx(ks[i].gnd[c], ks[i + 1].gnd[c]);
    }
    fl.skyBright = mx(ks[i].br, ks[i + 1].br) * r.skyBrightness / 0.6f;
    float sunK = mx(ks[i].sunK, ks[i + 1].sunK);
    Vec3 sunC(mx(ks[i].sun[0], ks[i + 1].sun[0]), mx(ks[i].sun[1], ks[i + 1].sun[1]), mx(ks[i].sun[2], ks[i + 1].sun[2]));
    fl.stars = clampf((-s - 0.06f) / 0.3f, 0.f, 1.f);
    // направление: азимут берём у солнца сцены, высота идёт по времени
    float az = std::atan2(fl.sunDir.z, fl.sunDir.x);
    if (!sunObjExists) az = 0.8f;
    float el = s * 1.05f;  // до 60 градусов в полдень
    bool night = s < 0.0f;
    const float energy = 3.5f;
    if (night) {
        el = std::max(0.35f, -s * 1.1f);
        az += 3.14159265f;
        fl.moon = 1.f;
        fl.hasSun = true;
        fl.sunCol = Vec3(0.45f, 0.55f, 0.95f) * (0.45f * clampf(-s * 3.f, 0.f, 1.f));
    } else {
        el = std::max(0.04f, el);
        fl.hasSun = true;
        fl.sunCol = sunC * ((sunObjExists ? fl.sunBase : energy) * sunK);
    }
    fl.sunDir = normalize(Vec3(std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az)));
}

void Renderer::gatherLights(Scene& sc, FrameLights& fl) {
    fl = FrameLights();
    const RenderSettings& rs = sc.render;
    std::memcpy(fl.skyTop, rs.skyTop, sizeof fl.skyTop);
    std::memcpy(fl.skyHor, rs.skyHorizon, sizeof fl.skyHor);
    std::memcpy(fl.skyGnd, rs.skyGround, sizeof fl.skyGnd);
    fl.skyBright = rs.skyBrightness;
    for (int i = 0; i < 8; i++) fl.sp[i * 4 + 3] = -2.f;
    for (auto& op : sc.objects) {
        const Object& o = *op;
        if (o.type != ObjType::Light || !o.visibleNow) continue;
        if (o.light == LightKind::Sun) {
            if (fl.hasSun) continue;
            fl.hasSun = true;
            fl.sunDir = normalize(transformDir(o.world, {0, 0, 1}));
            fl.sunCol = Vec3(o.lcolor[0], o.lcolor[1], o.lcolor[2]) * o.energy;
            fl.sunBase = o.energy;
        } else if (fl.n < 6) {
            Vec3 p = transformPoint(o.world, {0, 0, 0});
            fl.pl[fl.n * 4] = p.x; fl.pl[fl.n * 4 + 1] = p.y; fl.pl[fl.n * 4 + 2] = p.z; fl.pl[fl.n * 4 + 3] = std::max(0.5f, o.lradius);
            fl.plc[fl.n * 3] = o.lcolor[0] * o.energy; fl.plc[fl.n * 3 + 1] = o.lcolor[1] * o.energy; fl.plc[fl.n * 3 + 2] = o.lcolor[2] * o.energy;
            if (o.light == LightKind::Spot) {
                Vec3 d = normalize(transformDir(o.world, {0, 0, -1}));
                float ang = clampf(o.spotAngle, 5.f, 85.f) * 3.14159265f / 180.f;
                float inner = ang * (1.f - clampf(o.spotSoft, 0.f, 0.95f));
                fl.sp[fl.n * 4] = d.x; fl.sp[fl.n * 4 + 1] = d.y; fl.sp[fl.n * 4 + 2] = d.z; fl.sp[fl.n * 4 + 3] = std::cos(ang);
                fl.spIn[fl.n] = std::cos(inner);
                if (o.beam > 0.01f) fl.beams.push_back({p, d, Vec3(o.lcolor[0], o.lcolor[1], o.lcolor[2]), std::max(1.f, o.lradius * 0.8f), std::tan(ang), o.beam});
            }
            fl.n++;
        }
    }
    if (rs.todOn) applyTimeOfDay(rs, fl, fl.sunBase > 0.f);
    PointLight fx[8];
    int nf = sc.particles().lights(fx, 8 - fl.n);
    for (int i = 0; i < nf && fl.n < 8; i++) {
        fl.pl[fl.n * 4] = fx[i].pos.x; fl.pl[fl.n * 4 + 1] = fx[i].pos.y; fl.pl[fl.n * 4 + 2] = fx[i].pos.z; fl.pl[fl.n * 4 + 3] = fx[i].radius;
        fl.plc[fl.n * 3] = fx[i].color.x * fx[i].intensity; fl.plc[fl.n * 3 + 1] = fx[i].color.y * fx[i].intensity; fl.plc[fl.n * 3 + 2] = fx[i].color.z * fx[i].intensity;
        fl.n++;
    }
    // границы видимых моделей: для карты теней
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    bool any = false;
    for (auto& op : sc.objects) {
        const Object& o = *op;
        Vec3 bl, bh;
        if (!o.visibleNow || !o.castShadow || !sc.localBox(o, bl, bh)) continue;
        Mat4 M = o.world * o.fit;
        for (int i = 0; i < 8; i++) {
            Vec3 p = transformPoint(M, {(i & 1) ? bh.x : bl.x, (i & 2) ? bh.y : bl.y, (i & 4) ? bh.z : bl.z});
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
            any = true;
        }
    }
    if (any) {
        sceneCenter_ = (lo + hi) * 0.5f;
        sceneRadius_ = std::max(2.5f, length(hi - lo) * 0.5f * 1.08f);
    } else { sceneCenter_ = {0, 0.5f, 0}; sceneRadius_ = 4.f; }
}

void Renderer::shadowPass(Scene& sc, const FrameLights& fl, int pass, Mat4& lightVP, float jitterCone) {
    Vec3 L = fl.sunDir;
    if (jitterCone > 0.f) {
        // сдвиг направления света внутри конуса: мягкие тени при накоплении
        auto halton = [](int i, int b) { float f = 1.f, r = 0.f; while (i > 0) { f /= b; r += f * (i % b); i /= b; } return r; };
        float a = halton(pass + 1, 2) * 6.2831853f, rr = std::sqrt(halton(pass + 1, 3)) * jitterCone;
        Vec3 up = std::fabs(L.y) > 0.95f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
        Vec3 t = normalize(cross(up, L)), b = cross(L, t);
        L = normalize(L + (t * std::cos(a) + b * std::sin(a)) * rr);
    }
    float R = sceneRadius_;
    Vec3 up = std::fabs(L.y) > 0.95f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    lightVP = ortho(-R, R, -R, R, 0.1f, R * 6.f) * lookAt(sceneCenter_ + L * (R * 3.f), sceneCenter_, up);
    lastLightVP_ = lightVP;
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, shadowSize_, shadowSize_);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.f, 4.f);
    glUseProgram(depthP_);
    glUniformMatrix4fv(dL_.vp, 1, GL_FALSE, lightVP.m);
    glUniform1i(dL_.tex, 0);
    drawModels(sc, 0, dL_, sceneCenter_);
    glDisable(GL_POLYGON_OFFSET_FILL);
}

// ============================================================ отрисовка объектов
void Renderer::drawModels(Scene& sc, int pass, const Locs& L, const Vec3& eye) {
    // порядок для прозрачных: дальние сначала
    std::vector<Object*> list;
    for (auto& op : sc.objects) {
        Object& o = *op;
        if (o.type != ObjType::Model || !o.visibleNow || o.model < 0 || o.model >= (int)sc.models.size() || !sc.models[o.model].model || !o.anim) continue;
        if (pass == 0 && (!o.castShadow || o.glass)) continue;
        if (pass == 1 && o.glass) { bool anyOpaque = false; (void)anyOpaque; }
        list.push_back(&o);
    }
    if (pass == 2)
        std::sort(list.begin(), list.end(), [&](Object* a, Object* b) {
            return length(transformPoint(a->world, {0, 0, 0}) - eye) > length(transformPoint(b->world, {0, 0, 0}) - eye);
        });
    for (Object* op : list) {
        Object& o = *op;
        const Model& m = *sc.models[o.model].model;
        Mat4 base = o.world * o.fit;
        int lastSkin = -2;
        for (int ni : m.order) {
            for (int mi : m.nodes[ni].meshes) {
                const MeshData* mp = &m.meshes[mi];
                if (o.clothRt && (size_t)mi < o.clothRt->meshes.size() && !o.clothRt->meshes[(size_t)mi].verts.empty()) {
                    MeshData& cm = o.clothRt->meshes[(size_t)mi];  // ткань: деформированная копия меша
                    if (!cm.vao) cm.upload(); else if (cm.stale) cm.updateVerts();
                    mp = &cm;
                }
                const MeshData& md = *mp;
                bool blend = md.alphaMode == 2 || o.glass;
                if ((pass == 2) != blend && pass != 0) continue;
                Mat4 M;
                bool skinned = md.skin >= 0 && md.skin < (int)o.anim->jointMat.size();
                if (skinned) {
                    M = base;
                    if (lastSkin != md.skin) {
                        const auto& jm = o.anim->jointMat[md.skin];
                        glUniformMatrix4fv(L.joints, (GLsizei)std::min<size_t>(jm.size(), kMaxJoints), GL_FALSE, jm[0].m);
                        lastSkin = md.skin;
                    }
                } else M = md.modelSpace ? base : base * o.anim->world[ni];
                glUniformMatrix4fv(L.model, 1, GL_FALSE, M.m);
                glUniform1i(L.skinned, skinned ? 1 : 0);
                bool tex = md.texture >= 0 && md.texture < (int)m.textures.size() && m.textures[md.texture].id;
                glUniform1i(L.hasTex, tex ? 1 : 0);
                glUniform1i(L.alphaMode, md.alphaMode);
                glUniform1f(L.cutoff, md.alphaCutoff);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, tex ? m.textures[md.texture].id : whiteTex_);
                if (pass != 0) {
                    Mat4 inv = inverse(M);
                    float n3[9];
                    for (int c = 0; c < 3; c++) for (int r = 0; r < 3; r++) n3[c * 3 + r] = inv.m[r * 4 + c];
                    glUniformMatrix3fv(L.nrm, 1, GL_FALSE, n3);
                    auto bind = [&](int unit, int idx, GLint has) {
                        bool ok = idx >= 0 && idx < (int)m.textures.size() && m.textures[idx].id;
                        glUniform1i(has, ok ? 1 : 0);
                        glActiveTexture(GL_TEXTURE0 + unit);
                        glBindTexture(GL_TEXTURE_2D, ok ? m.textures[idx].id : whiteTex_);
                    };
                    bind(1, md.texMR, L.hasMR); bind(2, md.texNormal, L.hasN); bind(3, md.texEmissive, L.hasE); bind(4, md.texOcclusion, L.hasO);
                    glActiveTexture(GL_TEXTURE0);
                    if (o.matOverride) {
                        glUniform4f(L.tint, o.color[0], o.color[1], o.color[2], 1.f);
                        glUniform1i(L.useVC, 0);
                        glUniform1f(L.metal, o.metallic); glUniform1f(L.rough, o.roughness);
                        glUniform3f(L.emis, o.color[0] * o.emission, o.color[1] * o.emission, o.color[2] * o.emission);
                    } else {
                        glUniform4f(L.tint, 1.f, 1.f, 1.f, 1.f);
                        glUniform1i(L.useVC, 1);
                        glUniform1f(L.metal, md.metallic); glUniform1f(L.rough, md.roughness);
                        glUniform3f(L.emis, md.emissive[0], md.emissive[1], md.emissive[2]);
                    }
                    glUniform1f(L.nScale, md.normalScale);
                    glUniform1i(L.glass, o.glass ? 1 : 0);
                    if (o.glass) {
                        glUniform4f(L.tint, o.color[0], o.color[1], o.color[2], o.glassAlpha);
                        glUniform1i(L.useVC, 0);
                        glUniform1f(L.metal, 0.f); glUniform1f(L.rough, 0.03f);
                        glUniform3f(L.emis, 0.f, 0.f, 0.f);
                    }
                    if (pass == 2) glBlendFunc(o.glass ? GL_ONE : GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    if (md.doubleSided || o.glass) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
                }
                md.draw();
            }
        }
    }
}

void Renderer::drawFloor(Scene& sc, const Locs& L) {
    const RenderSettings& r = sc.render;
    Mat4 I;
    glUniformMatrix4fv(L.model, 1, GL_FALSE, I.m);
    float n3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    glUniformMatrix3fv(L.nrm, 1, GL_FALSE, n3);
    glUniform1i(L.skinned, 0); glUniform1i(L.hasTex, 0); glUniform1i(L.hasMR, 0); glUniform1i(L.hasN, 0); glUniform1i(L.hasE, 0); glUniform1i(L.hasO, 0);
    glUniform1i(L.alphaMode, 0); glUniform1i(L.useVC, 0);
    glUniform4f(L.tint, r.floorColor[0], r.floorColor[1], r.floorColor[2], 1.f);
    glUniform1f(L.metal, 0.f); glUniform1f(L.rough, r.floorRoughness); glUniform3f(L.emis, 0, 0, 0);
    glUniform1i(L.isFloor, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, whiteTex_);
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
    floorMesh_.draw();
    glUniform1i(L.isFloor, 0);
}

void Renderer::drawSky(Scene& sc, const View& v, const FrameLights& fl) {
    glUseProgram(sky_);
    Mat4 inv = inverse(v.proj * v.view);
    glUniformMatrix4fv(glGetUniformLocation(sky_, "u_invVP"), 1, GL_FALSE, inv.m);
    glUniform3f(glGetUniformLocation(sky_, "u_cam"), v.eye.x, v.eye.y, v.eye.z);
    glUniform3f(glGetUniformLocation(sky_, "u_L"), fl.sunDir.x, fl.sunDir.y, fl.sunDir.z);
    glUniform3f(glGetUniformLocation(sky_, "u_sunCol"), fl.sunCol.x * 0.15f, fl.sunCol.y * 0.15f, fl.sunCol.z * 0.15f);
    glUniform3f(glGetUniformLocation(sky_, "u_skyTop"), fl.skyTop[0], fl.skyTop[1], fl.skyTop[2]);
    glUniform3f(glGetUniformLocation(sky_, "u_skyHor"), fl.skyHor[0], fl.skyHor[1], fl.skyHor[2]);
    glUniform3f(glGetUniformLocation(sky_, "u_skyGnd"), fl.skyGnd[0], fl.skyGnd[1], fl.skyGnd[2]);
    glUniform1f(glGetUniformLocation(sky_, "u_skyBright"), fl.skyBright);
    glUniform1f(glGetUniformLocation(sky_, "u_stars"), fl.stars);
    glUniform1f(glGetUniformLocation(sky_, "u_moon"), fl.moon);
    glUniform1f(glGetUniformLocation(sky_, "u_outScale"), outScale_);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    fullscreen();
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}


// Видимый луч фонарика: прозрачный конус, ярче по краям вдоль луча и тает к концу.
void Renderer::drawBeams(const FrameLights& fl, const View& v) {
    if (fl.beams.empty() || !beam_) return;
    glUseProgram(beam_);
    Mat4 vp = v.proj * v.view;
    glUniformMatrix4fv(glGetUniformLocation(beam_, "u_vp"), 1, GL_FALSE, vp.m);
    glUniform3f(glGetUniformLocation(beam_, "u_cam"), v.eye.x, v.eye.y, v.eye.z);
    glUniform1f(glGetUniformLocation(beam_, "u_outScale"), outScale_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(emptyVao_ ? emptyVao_ : 0);
    for (auto& b : fl.beams) {
        Vec3 up = std::fabs(b.dir.y) > 0.95f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
        Vec3 rx = normalize(cross(b.dir, up)), ry = cross(b.dir, rx);
        glUniform3f(glGetUniformLocation(beam_, "u_pos"), b.pos.x, b.pos.y, b.pos.z);
        glUniform3f(glGetUniformLocation(beam_, "u_dir"), b.dir.x, b.dir.y, b.dir.z);
        glUniform3f(glGetUniformLocation(beam_, "u_rx"), rx.x, rx.y, rx.z);
        glUniform3f(glGetUniformLocation(beam_, "u_ry"), ry.x, ry.y, ry.z);
        glUniform3f(glGetUniformLocation(beam_, "u_col"), b.col.x, b.col.y, b.col.z);
        glUniform1f(glGetUniformLocation(beam_, "u_len"), b.len);
        glUniform1f(glGetUniformLocation(beam_, "u_tan"), b.ang);
        glUniform1f(glGetUniformLocation(beam_, "u_k"), b.k * 0.5f);
        glDrawArrays(GL_TRIANGLES, 0, 28 * 3);
    }
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
}

void Renderer::scenePass(Scene& sc, TargetSet& s, const View& v, const Mat4& lightVP, const FrameLights& fl, int pass, bool realtime, const Overlay* ov) {
    const RenderSettings& r = sc.render;
    glBindFramebuffer(GL_FRAMEBUFFER, s.hdr.fbo);
    glViewport(0, 0, s.hdr.w, s.hdr.h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawSky(sc, v, fl);

    Mat4 vp = v.proj * v.view;
    glUseProgram(pbr_);
    glUniformMatrix4fv(L_.vp, 1, GL_FALSE, vp.m);
    glUniformMatrix4fv(L_.lightVP, 1, GL_FALSE, lightVP.m);
    glUniform3f(L_.cam, v.eye.x, v.eye.y, v.eye.z);
    glUniform3f(L_.L, fl.sunDir.x, fl.sunDir.y, fl.sunDir.z);
    glUniform3f(L_.sunCol, fl.sunCol.x, fl.sunCol.y, fl.sunCol.z);
    glUniform3f(L_.skyTop, fl.skyTop[0], fl.skyTop[1], fl.skyTop[2]);
    glUniform3f(L_.skyHor, fl.skyHor[0], fl.skyHor[1], fl.skyHor[2]);
    glUniform3f(L_.skyGnd, fl.skyGnd[0], fl.skyGnd[1], fl.skyGnd[2]);
    glUniform1f(L_.skyBright, fl.skyBright);
    glUniform1i(L_.glass, 0);
    glUniform1i(L_.shadowOn, (r.shadows && fl.hasSun && shadowFbo_) ? 1 : 0);
    glUniform1f(L_.texel, 1.f / shadowSize_);
    glUniform1f(L_.shadowSoft, r.shadowSoft);
    glUniform1f(L_.pass, (float)pass);
    glUniform1f(L_.outScale, outScale_);
    glUniform1i(L_.tex, 0); glUniform1i(L_.texMR, 1); glUniform1i(L_.texN, 2); glUniform1i(L_.texE, 3); glUniform1i(L_.texO, 4); glUniform1i(L_.shadow, 5);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, shadowTex_ ? shadowTex_ : whiteTex_);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, shadowTex_ ? shadowTex_ : whiteTex_);
    if (rawSampler_) glBindSampler(6, rawSampler_);
    glUniform1i(L_.shadowRaw, 6);
    glUniform1f(L_.uvPerM, 1.f / (2.f * std::max(0.5f, sceneRadius_)));
    glUniform1f(L_.shRange, std::max(0.5f, sceneRadius_) * 6.f);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(L_.nPl, fl.n);
    float pl[32], plc[24];
    std::memcpy(pl, fl.pl, sizeof pl);
    std::memcpy(plc, fl.plc, sizeof plc);
    for (int i = fl.n; i < 8; i++) { pl[i * 4 + 3] = 1.f; }
    glUniform4fv(L_.pl, 8, pl);
    glUniform3fv(L_.plc, 8, plc);
    {
        float spA[32], spI[8];
        std::memcpy(spA, fl.sp, sizeof spA);
        std::memcpy(spI, fl.spIn, sizeof spI);
        for (int i = fl.n; i < 8; i++) spA[i * 4 + 3] = -2.f;
        glUniform4fv(L_.sp, 8, spA);
        glUniform1fv(L_.spIn, 8, spI);
    }
    glUniform1i(L_.isFloor, 0);

    if (r.floor) drawFloor(sc, L_);
    drawModels(sc, 1, L_, v.eye);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    drawModels(sc, 2, L_, v.eye);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);

    drawBeams(fl, v);

    // эффекты
    Vec3 right(v.view.m[0], v.view.m[4], v.view.m[8]), up(v.view.m[1], v.view.m[5], v.view.m[9]);
    sc.particles().setOutputScale(outScale_);
    float skyL = 0.35f + 0.25f * (fl.sunCol.x + fl.sunCol.y + fl.sunCol.z) / 3.f + 0.4f * fl.skyBright;
    sc.particles().setLightScale(skyL);
    sc.particles().setSun(fl.sunDir);
    sc.particles().render(vp, right, up, v.eye);

    if (realtime && ov) {
        buildOverlay(sc, v, *ov);
        lines_.flush(vp, s.hdr.w, s.hdr.h, outScale_, true);
    }
}

// ============================================================ значки и рамки
void Renderer::buildOverlay(Scene& sc, const View& v, const Overlay& ov) {
    (void)v;
    LineBatch& L = lines_;
    const float gray[4] = {0.55f, 0.57f, 0.62f, 0.22f}, axX[4] = {0.9f, 0.25f, 0.25f, 0.8f}, axZ[4] = {0.3f, 0.5f, 0.95f, 0.8f};
    const float orange[4] = {1.f, 0.55f, 0.1f, 1.f}, white[4] = {0.85f, 0.85f, 0.88f, 0.9f}, dim[4] = {0.6f, 0.6f, 0.65f, 0.75f};
    float px = pixelScale_;
    if (ov.grid && sc.render.floor) {
        for (int i = -10; i <= 10; i++) {
            if (i != 0) {
                L.add({(float)i, 0.002f, -10}, {(float)i, 0.002f, 10}, gray, 1.2f * px);
                L.add({-10, 0.002f, (float)i}, {10, 0.002f, (float)i}, gray, 1.2f * px);
            }
        }
        L.add({-10, 0.003f, 0}, {10, 0.003f, 0}, axX, 1.8f * px);
        L.add({0, 0.003f, -10}, {0, 0.003f, 10}, axZ, 1.8f * px);
    }
    auto circle = [&](Vec3 c, Vec3 a, Vec3 b, float r, const float* col, float w) {
        const int N = 20;
        for (int i = 0; i < N; i++) {
            float t0 = 6.2831853f * i / N, t1 = 6.2831853f * (i + 1) / N;
            L.add(c + a * (std::cos(t0) * r) + b * (std::sin(t0) * r), c + a * (std::cos(t1) * r) + b * (std::sin(t1) * r), col, w);
        }
    };
    auto box = [&](const Mat4& M, Vec3 lo, Vec3 hi, const float* col, float w) {
        Vec3 p[8];
        for (int i = 0; i < 8; i++) p[i] = transformPoint(M, {(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z});
        static const int e[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (auto& ed : e) L.add(p[ed[0]], p[ed[1]], col, w);
    };
    for (auto& op : sc.objects) {
        const Object& o = *op;
        if (!o.visibleNow) continue;
        bool sel = o.id == sc.selected;
        const float* col = sel ? orange : (o.type == ObjType::Model ? dim : white);
        Vec3 pos = transformPoint(o.world, {0, 0, 0});
        Vec3 ax = normalize(transformDir(o.world, {1, 0, 0})), ay = normalize(transformDir(o.world, {0, 1, 0})), az = normalize(transformDir(o.world, {0, 0, 1}));
        if (o.type == ObjType::Model) {
            Vec3 lo, hi;
            if (sel && ov.selection && sc.localBox(o, lo, hi)) box(o.world * o.fit, lo, hi, orange, 1.8f * px);
            if (sel && ov.bones && o.anim) {
                const Model& m = *sc.models[o.model].model;
                const float cyan[4] = {0.25f, 0.9f, 1.f, 0.95f};
                for (const Skin& sk : m.skins)
                    for (int j : sk.joints) {
                        Vec3 pj = transformPoint(o.world * o.fit * o.anim->world[j], {0, 0, 0});
                        int par = m.nodes[j].parent;
                        if (par >= 0) L.add(transformPoint(o.world * o.fit * o.anim->world[par], {0, 0, 0}), pj, cyan, 2.f * px);
                        float k = 0.03f;
                        L.add(pj - ax * k, pj + ax * k, orange, 2.f * px); L.add(pj - ay * k, pj + ay * k, orange, 2.f * px); L.add(pj - az * k, pj + az * k, orange, 2.f * px);
                    }
            }
            continue;
        }
        if (!ov.icons) continue;
        float lw = (sel ? 2.4f : 1.6f) * px;
        if (o.type == ObjType::Camera) {
            float asp = (float)sc.render.exportW / std::max(1, sc.render.exportH);
            float hh = std::tan(o.fovDeg * kPi / 360.f) * 0.8f, hw = hh * asp;
            Vec3 c = pos - az * 0.8f;
            Vec3 q[4] = {c + ax * hw + ay * hh, c - ax * hw + ay * hh, c - ax * hw - ay * hh, c + ax * hw - ay * hh};
            for (int i = 0; i < 4; i++) { L.add(pos, q[i], col, lw); L.add(q[i], q[(i + 1) % 4], col, lw); }
            Vec3 t0 = c + ay * (hh * 1.25f), tl = c + ax * (-hw * 0.5f) + ay * (hh * 1.05f), tr = c + ax * (hw * 0.5f) + ay * (hh * 1.05f);
            L.add(tl, t0, col, lw); L.add(t0, tr, col, lw); L.add(tr, tl, col, lw);
            if (o.id == sc.activeCamera) { L.add(q[0], q[2], col, lw * 0.6f); L.add(q[1], q[3], col, lw * 0.6f); }
        } else if (o.type == ObjType::Light) {
            circle(pos, ax, ay, 0.22f, col, lw); circle(pos, ay, az, 0.22f, col, lw);
            if (o.light == LightKind::Sun) {
                for (int i = 0; i < 8; i++) {
                    float a = 6.2831853f * i / 8;
                    Vec3 d = ax * std::cos(a) + ay * std::sin(a);
                    L.add(pos + d * 0.3f, pos + d * 0.5f, col, lw);
                }
                L.add(pos, pos - az * 2.2f, col, lw);
            } else if (o.light == LightKind::Spot) {
                Vec3 fwd = az * -1.f;
                float ang = clampf(o.spotAngle, 5.f, 85.f) * 3.14159265f / 180.f, len = 1.4f, rr = len * std::tan(ang);
                Vec3 c = pos + fwd * len;
                circle(c, ax, ay, rr, col, lw);
                for (int i = 0; i < 4; i++) {
                    float a = 1.5707963f * i;
                    L.add(pos, c + ax * (std::cos(a) * rr) + ay * (std::sin(a) * rr), col, lw);
                }
            } else {
                circle(pos, ax, az, 0.22f, col, lw);
                L.add(pos, {pos.x, 0.f, pos.z}, dim, 1.f * px);
            }
        } else if (o.type == ObjType::Effect) {
            static const float fxc[7][4] = {{1.f, 0.5f, 0.1f, 1.f}, {0.7f, 0.7f, 0.75f, 1.f}, {1.f, 0.9f, 0.3f, 1.f}, {1.f, 0.3f, 0.2f, 1.f}, {1.f, 0.85f, 0.5f, 1.f}, {0.8f, 0.05f, 0.1f, 1.f}, {0.6f, 0.9f, 1.f, 1.f}};
            const float* c2 = sel ? orange : fxc[(int)o.fx];
            float k = 0.2f;
            L.add(pos - ax * k, pos + ax * k, c2, lw); L.add(pos - ay * k, pos + ay * k, c2, lw); L.add(pos - az * k, pos + az * k, c2, lw);
            circle(pos, ax, ay, k, c2, lw);
            if (o.fx == FxKind::Muzzle || o.fx == FxKind::Sparks) {
                Vec3 tip = pos + az * 0.7f;
                L.add(pos, tip, c2, lw); L.add(tip, tip - az * 0.15f + ax * 0.08f, c2, lw); L.add(tip, tip - az * 0.15f - ax * 0.08f, c2, lw);
            }
        }
        // связь с родителем
        if (const ParentKey* pk = sc.activeParent(o, sc.frame())) {
            if (const Object* p = sc.find(pk->parent)) {
                const float blue[4] = {0.4f, 0.6f, 1.f, 0.8f};
                L.add(pos, transformPoint(p->world, {0, 0, 0}), blue, 1.f * px);
            }
        }
    }
    // связь с родителем для моделей
    for (auto& op : sc.objects) {
        const Object& o = *op;
        if (o.type != ObjType::Model || !o.visibleNow) continue;
        if (const ParentKey* pk = sc.activeParent(o, sc.frame())) {
            if (const Object* p = sc.find(pk->parent)) {
                const float blue[4] = {0.4f, 0.6f, 1.f, 0.8f};
                Vec3 pp = transformPoint(p->world, {0, 0, 0});
                if (pk->node >= 0 && p->anim && pk->node < (int)p->anim->world.size()) pp = transformPoint(p->world * p->fit * p->anim->world[pk->node], {0, 0, 0});
                L.add(transformPoint(o.world, {0, 0, 0}), pp, blue, 1.f * px);
            }
        }
    }
}

// ============================================================ постобработка
void Renderer::ssaoPass(TargetSet& s, const View& v, int pass, float strength) {
    Mat4 invP = inverse(v.proj);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, s.ao[0].fbo);
    glViewport(0, 0, s.ao[0].w, s.ao[0].h);
    glUseProgram(ssao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s.hdr.depth);
    glUniform1i(glGetUniformLocation(ssao_, "u_depth"), 0);
    glUniformMatrix4fv(glGetUniformLocation(ssao_, "u_invProj"), 1, GL_FALSE, invP.m);
    glUniformMatrix4fv(glGetUniformLocation(ssao_, "u_proj"), 1, GL_FALSE, v.proj.m);
    glUniform2f(glGetUniformLocation(ssao_, "u_texel"), 1.f / s.hdr.w, 1.f / s.hdr.h);
    glUniform1f(glGetUniformLocation(ssao_, "u_radius"), 0.6f);
    glUniform1f(glGetUniformLocation(ssao_, "u_pass"), (float)pass);
    glUniform1f(glGetUniformLocation(ssao_, "u_strength"), strength);
    {
        Vec3 sv = hasSun_ && contactOn_ ? normalize(transformDir(v.view, sunDir_)) : Vec3(0, 1, 0);
        glUniform1f(glGetUniformLocation(ssao_, "u_contact"), hasSun_ && contactOn_ ? 1.f : 0.f);
        glUniform3f(glGetUniformLocation(ssao_, "u_sunV"), sv.x, sv.y, sv.z);
    }
    fullscreen();
    // размытие с учётом глубины
    glBindFramebuffer(GL_FRAMEBUFFER, s.ao[1].fbo);
    glUseProgram(blur_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s.ao[0].color);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s.hdr.depth);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(blur_, "u_ao"), 0);
    glUniform1i(glGetUniformLocation(blur_, "u_depth"), 1);
    glUniformMatrix4fv(glGetUniformLocation(blur_, "u_invProj"), 1, GL_FALSE, invP.m);
    glUniform2f(glGetUniformLocation(blur_, "u_texel"), 1.f / s.ao[0].w, 1.f / s.ao[0].h);
    fullscreen();
}

void Renderer::bloomPass(TargetSet& s, GLuint src, float threshold) {
    if (s.bloom.empty()) return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(down_);
    glUniform1i(glGetUniformLocation(down_, "u_src"), 0);
    GLint uTex = glGetUniformLocation(down_, "u_texel"), uTh = glGetUniformLocation(down_, "u_thresh"), uIn = glGetUniformLocation(down_, "u_inScale"), uFirst = glGetUniformLocation(down_, "u_first");
    GLuint from = src;
    int fw = s.hdr.w, fh = s.hdr.h;
    if (s.hasAccum && src == s.accum.color) { fw = s.accum.w; fh = s.accum.h; }
    for (size_t i = 0; i < s.bloom.size(); i++) {
        Target& t = s.bloom[i];
        glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        glViewport(0, 0, t.w, t.h);
        glBindTexture(GL_TEXTURE_2D, from);
        glUniform2f(uTex, 1.f / fw, 1.f / fh);
        glUniform1f(uTh, threshold);
        glUniform1f(uIn, i == 0 ? (src == s.accum.color ? 1.f : inScale_) : 1.f);
        glUniform1i(uFirst, i == 0 ? 1 : 0);
        fullscreen();
        from = t.color;
        fw = t.w; fh = t.h;
    }
    glUseProgram(up_);
    glUniform1i(glGetUniformLocation(up_, "u_src"), 0);
    GLint uT2 = glGetUniformLocation(up_, "u_texel");
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int i = (int)s.bloom.size() - 1; i > 0; i--) {
        Target& small = s.bloom[i];
        Target& big = s.bloom[i - 1];
        glBindFramebuffer(GL_FRAMEBUFFER, big.fbo);
        glViewport(0, 0, big.w, big.h);
        glBindTexture(GL_TEXTURE_2D, small.color);
        glUniform2f(uT2, 1.f / small.w, 1.f / small.h);
        fullscreen();
    }
    glDisable(GL_BLEND);
}

void Renderer::tonemap(TargetSet& s, GLuint hdrTex, bool useAo, GLuint dstFbo, RectI r, Scene& sc, bool fxaa) {
    (void)fxaa;
    const RenderSettings& rs = sc.render;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
    glViewport(r.x, r.y, r.w, r.h);
    glUseProgram(tone_);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, hdrTex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, s.ao[1].color);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, s.bloom.empty() ? whiteTex_ : s.bloom[0].color);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(tone_, "u_hdr"), 0);
    glUniform1i(glGetUniformLocation(tone_, "u_ao"), 1);
    glUniform1i(glGetUniformLocation(tone_, "u_bloom"), 2);
    glUniform1i(glGetUniformLocation(tone_, "u_useAo"), useAo ? 1 : 0);
    glUniform1i(glGetUniformLocation(tone_, "u_useBloom"), (rs.bloom && !s.bloom.empty()) ? 1 : 0);
    glUniform1f(glGetUniformLocation(tone_, "u_exposure"), std::pow(2.f, rs.exposure));
    glUniform1f(glGetUniformLocation(tone_, "u_bloomStr"), rs.bloomStrength);
    glUniform1f(glGetUniformLocation(tone_, "u_contrast"), rs.contrast);
    glUniform1f(glGetUniformLocation(tone_, "u_sat"), rs.saturation);
    glUniform1f(glGetUniformLocation(tone_, "u_vig"), rs.vignette ? 0.35f : 0.f);
    glUniform1f(glGetUniformLocation(tone_, "u_inScale"), hdrTex == s.accum.color && s.accum.color ? 1.f : inScale_);
    fullscreen();
}

void Renderer::accumulate(TargetSet& s, float weight, bool useAo) {
    glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, s.accum.fbo);
    glViewport(0, 0, s.accum.w, s.accum.h);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glUseProgram(acc_);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, s.hdr.color);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, s.ao[1].color);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(acc_, "u_hdr"), 0);
    glUniform1i(glGetUniformLocation(acc_, "u_ao"), 1);
    glUniform1f(glGetUniformLocation(acc_, "u_weight"), weight);
    glUniform1f(glGetUniformLocation(acc_, "u_inScale"), inScale_);
    glUniform1i(glGetUniformLocation(acc_, "u_useAo"), useAo ? 1 : 0);
    fullscreen();
    glDisable(GL_BLEND);
}

// ============================================================ точки входа
void Renderer::renderView(Scene& sc, const View& v, GLuint targetFbo, RectI r, const Overlay& ov, float scale) {
    if (!ready_ || r.w < 4 || r.h < 4) return;
    if (fxScene_ != &sc) { sc.particles().init(); fxScene_ = &sc; }
    glDisable(GL_SCISSOR_TEST);
    scale = clampf(scale, 0.25f, 1.f);
    int rw = std::max(4, (int)(r.w * scale)), rh = std::max(4, (int)(r.h * scale));
    FrameLights fl;
    gatherLights(sc, fl);
    hasSun_ = fl.hasSun; sunDir_ = fl.sunDir; contactOn_ = sc.render.contactShadows;
    Mat4 lvp;
    bool sh = sc.render.shadows && fl.hasSun && shadowFbo_;
    if (sh) shadowPass(sc, fl, 0, lvp, 0.f);
    ensureSet(rt_, rw, rh, false);
    scenePass(sc, rt_, v, sh ? lvp : Mat4(), fl, 0, true, &ov);
    bool useAo = sc.render.ao;
    if (useAo) ssaoPass(rt_, v, 0, sc.render.aoStrength);
    if (sc.render.bloom) bloomPass(rt_, rt_.hdr.color, 1.2f);
    tonemap(rt_, rt_.hdr.color, useAo, rt_.ldr.fbo, RectI{0, 0, rw, rh}, sc, false);
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(r.x, r.y, r.w, r.h);
    glUseProgram(fxaa_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rt_.ldr.color);
    glUniform1i(glGetUniformLocation(fxaa_, "u_src"), 0);
    glUniform2f(glGetUniformLocation(fxaa_, "u_texel"), 1.f / rw, 1.f / rh);
    glDisable(GL_DEPTH_TEST);
    fullscreen();
    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
}

bool Renderer::renderStill(Scene& sc, const View& v, int w, int h, int samples, std::vector<uint8_t>& rgba) {
    if (!ready_) return false;
    if (fxScene_ != &sc) { sc.particles().init(); fxScene_ = &sc; }
    w = std::max(16, std::min(w, maxExportSide()));
    h = std::max(16, std::min(h, maxExportSide()));
    samples = std::max(1, samples);
    glDisable(GL_SCISSOR_TEST);
    ensureSet(st_, w, h, true);
    if (!st_.hdr.fbo || !st_.accum.fbo || !st_.ldr.fbo) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, st_.accum.fbo);
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    FrameLights fl;
    gatherLights(sc, fl);
    hasSun_ = fl.hasSun; sunDir_ = fl.sunDir; contactOn_ = sc.render.contactShadows;
    bool sh = sc.render.shadows && fl.hasSun && shadowFbo_;
    auto halton = [](int i, int b) { float f = 1.f, r = 0.f; while (i > 0) { f /= b; r += f * (i % b); i /= b; } return r; };
    for (int i = 0; i < samples; i++) {
        View jv = v;
        float jx = (halton(i + 1, 2) - 0.5f) * (samples > 1 ? 1.f : 0.f), jy = (halton(i + 1, 3) - 0.5f) * (samples > 1 ? 1.f : 0.f);
        jv.proj.m[8] += jx * 2.f / w;
        jv.proj.m[9] += jy * 2.f / h;
        Mat4 lvp;
        if (sh) shadowPass(sc, fl, i, lvp, samples > 1 ? 0.02f * std::max(0.2f, sc.render.shadowSoft) : 0.f);
        scenePass(sc, st_, jv, sh ? lvp : Mat4(), fl, i, false, nullptr);
        if (sc.render.ao) ssaoPass(st_, jv, i, sc.render.aoStrength);
        accumulate(st_, 1.f / samples, sc.render.ao);
    }
    if (sc.render.bloom) bloomPass(st_, st_.accum.color, 1.2f);
    tonemap(st_, st_.accum.color, false, st_.ldr.fbo, RectI{0, 0, w, h}, sc, false);
    rgba.assign((size_t)w * h * 4, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, st_.ldr.fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    // перевернуть строки (GL: снизу вверх)
    std::vector<uint8_t> row((size_t)w * 4);
    for (int y = 0; y < h / 2; y++) {
        uint8_t* a = &rgba[(size_t)y * w * 4];
        uint8_t* b = &rgba[(size_t)(h - 1 - y) * w * 4];
        std::memcpy(row.data(), a, row.size());
        std::memcpy(a, b, row.size());
        std::memcpy(b, row.data(), row.size());
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

}  // namespace eng
