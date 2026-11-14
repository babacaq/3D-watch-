#include "engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "geometry.h"
#include "log.h"

namespace eng {

// ============================================================ шейдеры
static const char* kSceneVS = R"(#version 300 es
layout(location=0) in vec3 a_pos;
layout(location=1) in vec3 a_nrm;
layout(location=2) in vec4 a_col;
layout(location=3) in uvec4 a_j;
layout(location=4) in vec4 a_w;
layout(location=5) in vec2 a_uv;
uniform mat4 u_vp; uniform mat4 u_model; uniform mat4 u_joints[64]; uniform int u_skinned;
out vec3 v_n; out vec3 v_w; out vec4 v_c; out vec2 v_uv;
void main(){
  vec4 p = vec4(a_pos, 1.0); vec3 n = a_nrm;
  if (u_skinned != 0) {
    mat4 s = a_w.x*u_joints[a_j.x] + a_w.y*u_joints[a_j.y] + a_w.z*u_joints[a_j.z] + a_w.w*u_joints[a_j.w];
    p = s * p; n = mat3(s) * n;
  }
  vec4 w = u_model * p;
  v_w = w.xyz; v_n = mat3(u_model) * n; v_c = a_col; v_uv = a_uv;
  gl_Position = u_vp * w;
}
)";

static const char* kSceneFS = R"(#version 300 es
precision highp float; precision highp sampler2DShadow;
in vec3 v_n; in vec3 v_w; in vec4 v_c; in vec2 v_uv;
uniform sampler2DShadow u_shadow; uniform sampler2D u_tex;
uniform mat4 u_lightVP; uniform vec3 u_lightDir; uniform vec3 u_cam; uniform vec3 u_bg; uniform vec4 u_tint;
uniform int u_shadowOn; uniform int u_checker; uniform int u_hasTex; uniform int u_alphaMode;
uniform float u_scroll; uniform float u_shin; uniform float u_texel; uniform float u_cutoff;
uniform vec4 u_pl[4]; uniform vec3 u_plc[4];
out vec4 o;
float shadowAt(vec3 wp, vec3 n){
  if (u_shadowOn == 0) return 1.0;
  vec4 lp = u_lightVP * vec4(wp + n * 0.025, 1.0);
  vec3 c = lp.xyz / lp.w * 0.5 + 0.5;
  if (c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0 || c.z > 1.0) return 1.0;
  float s = 0.0;
  for (int i = -1; i <= 1; i++) for (int j = -1; j <= 1; j++)
    s += texture(u_shadow, vec3(c.xy + vec2(float(i), float(j)) * u_texel, c.z - 0.0012));
  return s / 9.0;
}
void main(){
  vec3 N = normalize(v_n); if (!gl_FrontFacing) N = -N;
  vec3 V = normalize(u_cam - v_w); vec3 L = normalize(u_lightDir);
  vec4 col = v_c * u_tint;
  vec3 base = col.rgb; float alpha = col.a;
  if (u_hasTex != 0) { vec4 t = texture(u_tex, v_uv); base *= pow(t.rgb, vec3(2.2)); alpha *= t.a; }
  if (u_alphaMode == 1 && alpha < u_cutoff) discard;
  float shin = u_shin;
  if (u_checker != 0) {
    vec2 g = floor(vec2(v_w.x, v_w.z + u_scroll) * 1.0);
    float ch = mod(g.x + g.y, 2.0);
    base = mix(vec3(0.05, 0.06, 0.08), vec3(0.09, 0.10, 0.13), ch);
    vec2 f = abs(fract(vec2(v_w.x, v_w.z + u_scroll)) - 0.5);
    base += vec3(0.02) * smoothstep(0.47, 0.5, max(f.x, f.y));
    shin = 8.0;
  }
  float ndl = max(dot(N, L), 0.0);
  float sh = shadowAt(v_w, N);
  vec3 hemi = mix(vec3(0.20, 0.18, 0.18), vec3(0.42, 0.50, 0.62), N.y * 0.5 + 0.5);
  vec3 H = normalize(L + V);
  float spec = pow(max(dot(N, H), 0.0), shin) * step(0.0, ndl) * (u_checker != 0 ? 0.1 : 0.4);
  float rim = u_checker != 0 ? 0.0 : pow(1.0 - max(dot(N, V), 0.0), 3.0) * 0.25;
  vec3 lit = base * (hemi + vec3(1.0, 0.95, 0.88) * ndl * sh * 1.1) + vec3(spec * sh) + vec3(0.30, 0.45, 0.70) * rim;
  for (int i = 0; i < 4; i++) {
    vec3 d = u_pl[i].xyz - v_w; float r = u_pl[i].w;
    float att = clamp(1.0 - dot(d, d) / (r * r), 0.0, 1.0); att *= att;
    vec3 Lp = normalize(d); float nl = max(dot(N, Lp), 0.0);
    float sp = pow(max(dot(N, normalize(Lp + V)), 0.0), shin) * step(0.0, nl);
    lit += u_plc[i] * att * (base * nl + vec3(sp * 0.4));
  }
  lit = 1.0 - exp(-lit * 1.15);
  lit = pow(lit, vec3(1.0 / 2.2));
  float dist = length(u_cam - v_w);
  float fog = (u_checker != 0) ? smoothstep(10.0, 38.0, length(v_w.xz)) : 0.0;
  o = vec4(mix(lit, u_bg, fog), alpha);
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

static const char* kFlatVS = R"(#version 300 es
layout(location=0) in vec3 a_pos; layout(location=1) in vec4 a_col;
uniform mat4 u_vp; out vec4 v_c;
void main(){ v_c = a_col; gl_Position = u_vp * vec4(a_pos, 1.0); }
)";
static const char* kFlatFS = R"(#version 300 es
precision mediump float; in vec4 v_c; out vec4 o; void main(){ o = v_c; }
)";

static const char* kUIVS = R"(#version 300 es
layout(location=0) in vec2 a_p; layout(location=1) in vec2 a_uv; layout(location=2) in vec4 a_c;
uniform vec2 u_res; out vec2 v_uv; out vec4 v_c;
void main(){ v_uv = a_uv; v_c = a_c; gl_Position = vec4(a_p.x / u_res.x * 2.0 - 1.0, 1.0 - a_p.y / u_res.y * 2.0, 0.0, 1.0); }
)";
static const char* kUIFS = R"(#version 300 es
precision mediump float; in vec2 v_uv; in vec4 v_c; uniform sampler2D u_tex; out vec4 o;
void main(){ o = vec4(v_c.rgb, v_c.a * texture(u_tex, v_uv).r); }
)";

// Шрифт 5x7 (ASCII 32..95), столбцы, младший бит - верхняя строка.
static const uint8_t kFont[64][5] = {
    {0, 0, 0, 0, 0}, {0, 0, 0x5F, 0, 0}, {0, 7, 0, 7, 0}, {0x14, 0x7F, 0x14, 0x7F, 0x14}, {0x24, 0x2A, 0x7F, 0x2A, 0x12},
    {0x23, 0x13, 0x08, 0x64, 0x62}, {0x36, 0x49, 0x56, 0x20, 0x50}, {0, 0x08, 0x07, 0x03, 0}, {0, 0x1C, 0x22, 0x41, 0},
    {0, 0x41, 0x22, 0x1C, 0}, {0x2A, 0x1C, 0x7F, 0x1C, 0x2A}, {0x08, 0x08, 0x3E, 0x08, 0x08}, {0, 0x40, 0x30, 0, 0},
    {0x08, 0x08, 0x08, 0x08, 0x08}, {0, 0, 0x60, 0x60, 0}, {0x20, 0x10, 0x08, 0x04, 0x02}, {0x3E, 0x51, 0x49, 0x45, 0x3E},
    {0, 0x42, 0x7F, 0x40, 0}, {0x72, 0x49, 0x49, 0x49, 0x46}, {0x21, 0x41, 0x49, 0x4D, 0x33}, {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x31}, {0x41, 0x21, 0x11, 0x09, 0x07}, {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x46, 0x49, 0x49, 0x29, 0x1E}, {0, 0, 0x14, 0, 0}, {0, 0x40, 0x34, 0, 0}, {0, 0x08, 0x14, 0x22, 0x41},
    {0x14, 0x14, 0x14, 0x14, 0x14}, {0, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x59, 0x09, 0x06}, {0x3E, 0x41, 0x5D, 0x59, 0x4E},
    {0x7C, 0x12, 0x11, 0x12, 0x7C}, {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22}, {0x7F, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01}, {0x3E, 0x41, 0x41, 0x51, 0x73}, {0x7F, 0x08, 0x08, 0x08, 0x7F},
    {0, 0x41, 0x7F, 0x41, 0}, {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x1C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x09, 0x09, 0x09, 0x06},
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46}, {0x26, 0x49, 0x49, 0x49, 0x32}, {0x03, 0x01, 0x7F, 0x01, 0x03},
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x03, 0x04, 0x78, 0x04, 0x03}, {0x61, 0x59, 0x49, 0x4D, 0x43}, {0, 0x7F, 0x41, 0x41, 0}, {0x02, 0x04, 0x08, 0x10, 0x20},
    {0, 0x41, 0x41, 0x7F, 0}, {0x04, 0x02, 0x01, 0x02, 0x04}, {0x40, 0x40, 0x40, 0x40, 0x40}};

// Кнопки
enum { B_OPEN = 100, B_MODEL, B_PAUSE, B_SPEED, B_SHADOW, B_BONES, B_SHOOT, B_AXIS, B_RESET, B_TOOL = 200, B_CLIP = 300 };
static const char* kToolNames[] = {"MUZZLE", "BOX", "BALL", "FIRE", "SMOKE", "BOOM"};
static const char* kAxisNames[] = {"+Z", "-Z", "+X", "-X", "+Y", "-Y"};
static const float kSpeeds[] = {0.25f, 0.5f, 1.f, 2.f};

static Vec3 xformP(const Mat4& m, Vec3 p, float w, float* wOut) {
    float x = m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12] * w;
    float y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13] * w;
    float z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14] * w;
    float ww = m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15] * w;
    if (wOut) *wOut = ww;
    return {x, y, z};
}

// ============================================================ жизненный цикл
Engine::Engine() {}
Engine::~Engine() {}

GLuint Engine::compile(const char* vs, const char* fs, const char* tag) {
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

Engine::Locs Engine::getLocs(GLuint p) {
    Locs L;
    auto u = [&](const char* n) { return glGetUniformLocation(p, n); };
    L.vp = u("u_vp"); L.model = u("u_model"); L.skinned = u("u_skinned"); L.joints = u("u_joints"); L.tex = u("u_tex");
    L.hasTex = u("u_hasTex"); L.alphaMode = u("u_alphaMode"); L.cutoff = u("u_cutoff"); L.lightVP = u("u_lightVP");
    L.shadow = u("u_shadow"); L.lightDir = u("u_lightDir"); L.cam = u("u_cam"); L.shadowOn = u("u_shadowOn");
    L.checker = u("u_checker"); L.scroll = u("u_scroll"); L.shin = u("u_shin"); L.texel = u("u_texel"); L.tint = u("u_tint");
    L.bg = u("u_bg"); L.pl = u("u_pl"); L.plc = u("u_plc");
    return L;
}

bool Engine::buildPrograms() {
    progScene_ = compile(kSceneVS, kSceneFS, "scene");
    progDepth_ = compile(kDepthVS, kDepthFS, "depth");
    progFlat_ = compile(kFlatVS, kFlatFS, "flat");
    progUI_ = compile(kUIVS, kUIFS, "ui");
    if (!progScene_ || !progDepth_ || !progFlat_ || !progUI_) return false;
    sL_ = getLocs(progScene_);
    dL_ = getLocs(progDepth_);
    fVP_ = glGetUniformLocation(progFlat_, "u_vp");
    uiRes_ = glGetUniformLocation(progUI_, "u_res");
    return true;
}

bool Engine::buildShadowMap() {
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
        glBindFramebuffer(GL_FRAMEBUFFER, targetFbo_);
        if (ok) return true;
        glDeleteFramebuffers(1, &shadowFbo_); glDeleteTextures(1, &shadowTex_);
        shadowFbo_ = shadowTex_ = 0;
    }
    return false;
}

void Engine::buildFont() {
    std::vector<uint8_t> px(128 * 64, 0);
    for (int c = 32; c <= 95; c++) {
        int cx = c % 16, cy = c / 16;
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if ((kFont[c - 32][col] >> row) & 1) px[(cy * 8 + row) * 128 + cx * 8 + col] = 255;
    }
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) px[(7 * 8 + y) * 128 + 15 * 8 + x] = 255;  // сплошная ячейка 127
    glGenTextures(1, &fontTex_);
    glBindTexture(GL_TEXTURE_2D, fontTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 128, 64, 0, GL_RED, GL_UNSIGNED_BYTE, px.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    uint8_t white[4] = {255, 255, 255, 255};
    glGenTextures(1, &whiteTex_);
    glBindTexture(GL_TEXTURE_2D, whiteTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

bool Engine::initGL(int w, int h) {
    w_ = std::max(1, w); h_ = std::max(1, h);
    ENG_LOG("GL_VERSION=%s RENDERER=%s", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));
    if (!buildPrograms()) return false;
    if (!buildShadowMap()) { ENG_LOG("shadow map unavailable"); shadows_ = false; }
    buildFont();
    if (!fx_.init()) ENG_LOG("particles unavailable");
    if (ground_.verts.empty()) {
        ground_.skin = -1;
        addBox(ground_, {0, -0.05f, 0}, {60, 0.05f, 60}, Color(1, 1, 1), -1);
        addBox(cube_, {0, 0, 0}, {0.5f, 0.5f, 0.5f}, Color(1, 1, 1), -1);
        addSphere(sphere_, {0, 0, 0}, {1, 1, 1}, Color(1, 1, 1), -1, 20, 14);
    }
    ground_.upload(); cube_.upload(); sphere_.upload();
    for (auto& m : models_) m->upload();
    glGenVertexArrays(1, &boneVao_); glGenBuffers(1, &boneVbo_);
    glBindVertexArray(boneVao_);
    glBindBuffer(GL_ARRAY_BUFFER, boneVbo_);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 28, nullptr);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 28, (const void*)12);
    glGenVertexArrays(1, &uiVao_); glGenBuffers(1, &uiVbo_);
    glBindVertexArray(uiVao_);
    glBindBuffer(GL_ARRAY_BUFFER, uiVbo_);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UIVert), nullptr);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UIVert), (const void*)8);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(UIVert), (const void*)16);
    glBindVertexArray(0);
    glReady_ = true;
    if (cur_ < 0 && !models_.empty()) selectModel(0); else onModelChanged();
    return true;
}

void Engine::releaseGL() {
    for (auto& m : models_) m->release();
    ground_.release(); cube_.release(); sphere_.release();
    fx_.release();
    GLuint progs[4] = {progScene_, progDepth_, progFlat_, progUI_};
    for (GLuint p : progs) if (p) glDeleteProgram(p);
    progScene_ = progDepth_ = progFlat_ = progUI_ = 0;
    if (shadowFbo_) glDeleteFramebuffers(1, &shadowFbo_);
    if (shadowTex_) glDeleteTextures(1, &shadowTex_);
    if (fontTex_) glDeleteTextures(1, &fontTex_);
    if (whiteTex_) glDeleteTextures(1, &whiteTex_);
    if (boneVao_) glDeleteVertexArrays(1, &boneVao_);
    if (boneVbo_) glDeleteBuffers(1, &boneVbo_);
    if (uiVao_) glDeleteVertexArrays(1, &uiVao_);
    if (uiVbo_) glDeleteBuffers(1, &uiVbo_);
    shadowFbo_ = shadowTex_ = fontTex_ = whiteTex_ = boneVao_ = boneVbo_ = uiVao_ = uiVbo_ = 0;
    glReady_ = false;
}

void Engine::resize(int w, int h) { w_ = std::max(1, w); h_ = std::max(1, h); }

// ============================================================ модели
int Engine::addModel(std::unique_ptr<Model> m) {
    if (glReady_) m->upload();
    models_.push_back(std::move(m));
    return (int)models_.size() - 1;
}

bool Engine::addModelFile(const std::string& fileName, const uint8_t* data, size_t size, const FileReader& reader, std::string* err) {
    std::string e;
    auto m = importModel(fileName, data, size, reader, &e);
    if (!m) { if (err) *err = e; setStatus("LOAD FAILED: " + e); ENG_LOG("import %s failed: %s", fileName.c_str(), e.c_str()); return false; }
    ENG_LOG("imported %s: %zu meshes, %zu tris, %zu textures, %zu clips", fileName.c_str(), m->meshes.size(), m->triangles(), m->textures.size(), m->clips.size());
    int idx = addModel(std::move(m));
    selectModel(idx);
    return true;
}

void Engine::selectModel(int i) {
    if (i < 0 || i >= (int)models_.size()) return;
    cur_ = i;
    if (glReady_ && !models_[i]->uploaded) models_[i]->upload();
    anim_.setModel(models_[i].get());
    onModelChanged();
}

void Engine::onModelChanged() {
    if (cur_ < 0 || cur_ >= (int)models_.size()) { target_ = {0, 1, 0}; baseDist_ = dist_ = 6.f; return; }
    const Model& m = *models_[cur_];
    Vec3 size = m.bmax - m.bmin;
    float mx = std::max({size.x, size.y, size.z, 1e-4f});
    float s = 2.0f / mx;
    Vec3 c = m.center();
    xf_ = trs(Vec3(-s * c.x, -s * m.bmin.y, -s * c.z), Quat(), Vec3(s, s, s));
    wmin_ = {-size.x * s * 0.5f, 0, -size.z * s * 0.5f};
    wmax_ = {size.x * s * 0.5f, size.y * s, size.z * s * 0.5f};
    target_ = (wmin_ + wmax_) * 0.5f;
    float radius = length(wmax_ - wmin_) * 0.5f;
    float aspect = (float)w_ / (float)h_;
    float hf = 2.f * std::atan(std::tan(fovy_ * 0.5f) * aspect);
    baseDist_ = dist_ = std::max(2.f, radius / std::sin(std::min(fovy_, hf) * 0.5f) * 1.15f);
    muzzleSet_ = false;
    defaultMuzzle();
    buildCollider();
    fx_.clearEmitters();
}

Vec3 Engine::axisVec() const {
    static const Vec3 a[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    return a[axis_ % 6];
}

void Engine::defaultMuzzle() {
    if (muzzleSet_) return;
    Vec3 c = (wmin_ + wmax_) * 0.5f, half = (wmax_ - wmin_) * 0.5f, a = axisVec();
    muzzle_ = c + Vec3(a.x * half.x, a.y * half.y, a.z * half.z);
}

void Engine::buildCollider() {
    phys_.setGroundY(0);
    phys_.clearStaticMesh();
    colliderNote_.clear();
    if (cur_ < 0) return;
    const Model& m = *models_[cur_];
    if (m.triangles() > 400000) { colliderNote_ = "COLLIDER OFF (TOO MANY TRIS)"; return; }
    std::vector<Vec3> verts;
    std::vector<uint32_t> idx;
    for (int ni : m.order) {
        for (int mi : m.nodes[ni].meshes) {
            const MeshData& md = m.meshes[mi];
            if (md.alphaMode == 2 && md.verts.size() < 8) continue;
            Mat4 M = md.skin >= 0 ? xf_ : xf_ * anim_.world[ni];
            uint32_t base = (uint32_t)verts.size();
            for (const Vertex& v : md.verts) verts.push_back(transformPoint(M, {v.p[0], v.p[1], v.p[2]}));
            for (uint32_t i : md.indices) idx.push_back(base + i);
        }
    }
    phys_.setStaticMesh(verts, idx);
}

// ============================================================ камера
void Engine::setCamera(float yaw, float pitch, float distScale) { yaw_ = yaw; pitch_ = pitch; dist_ = baseDist_ * distScale; }

Mat4 Engine::viewMatrix(Vec3* eyeOut) const {
    float cp = std::cos(pitch_);
    Vec3 off(cp * std::sin(yaw_), std::sin(pitch_), cp * std::cos(yaw_));
    Vec3 eye = target_ + off * dist_;
    if (shake_ > 0.001f) eye += Vec3(std::sin(time_ * 91.f), std::sin(time_ * 77.f + 1.f), std::sin(time_ * 103.f + 2.f)) * (shake_ * 0.06f);
    if (eyeOut) *eyeOut = eye;
    return lookAt(eye, target_, {0, 1, 0});
}

bool Engine::pickRay(float x, float y, Vec3& o, Vec3& d) const {
    Mat4 inv = inverse(lastVP_);
    float nx = x / w_ * 2.f - 1.f, ny = 1.f - y / h_ * 2.f, w0, w1;
    Vec3 a = xformP(inv, {nx, ny, -1.f}, 1.f, &w0), b = xformP(inv, {nx, ny, 1.f}, 1.f, &w1);
    if (std::fabs(w0) < 1e-9f || std::fabs(w1) < 1e-9f) return false;
    a = a * (1.f / w0); b = b * (1.f / w1);
    o = a; d = normalize(b - a);
    return true;
}

// ============================================================ эффекты и физика
void Engine::spawnBody(bool box, Vec3 p) {
    rng_ = rng_ * 1664525u + 1013904223u;
    float r1 = (float)((rng_ >> 8) & 0xFFFF) / 65535.f;
    rng_ = rng_ * 1664525u + 1013904223u;
    float r2 = (float)((rng_ >> 8) & 0xFFFF) / 65535.f;
    rng_ = rng_ * 1664525u + 1013904223u;
    float r3 = (float)((rng_ >> 8) & 0xFFFF) / 65535.f;
    BodyDesc d;
    d.pos = p; d.lifetime = 60.f;
    d.rot = quatEuler(r1 * 6.f, r2 * 6.f, r3 * 6.f);
    d.angVel = {(r1 - 0.5f) * 3.f, (r2 - 0.5f) * 3.f, (r3 - 0.5f) * 3.f};
    if (box) { float s = 0.1f + 0.12f * r1; d.shape = ShapeType::Box; d.half = {s, s * (0.7f + 0.6f * r2), s}; d.mass = 8.f * s * s * s * 100.f / 0.8f * 0.3f; d.friction = 0.6f; d.restitution = 0.15f; }
    else { d.shape = ShapeType::Sphere; d.radius = 0.09f + 0.1f * r1; d.mass = 4.19f * d.radius * d.radius * d.radius * 400.f; d.friction = 0.5f; d.restitution = 0.55f; }
    int alive = 0, oldest = -1; float oa = -1;
    const auto& bs = phys_.bodies();
    for (int i = 0; i < (int)bs.size(); i++) if (bs[i].alive && bs[i].tag == 0) { alive++; if (bs[i].age > oa) { oa = bs[i].age; oldest = i; } }
    if (alive >= 120 && oldest >= 0) phys_.removeBody(oldest);
    int id = phys_.addBody(d);
    if (id < 0) return;
    if ((int)vis_.size() <= id) vis_.resize(id + 1);
    static const Vec3 pal[] = {{0.9f, 0.25f, 0.2f}, {0.2f, 0.55f, 0.95f}, {0.95f, 0.75f, 0.15f}, {0.3f, 0.8f, 0.4f}, {0.8f, 0.4f, 0.9f}};
    vis_[id].color = pal[(int)(r3 * 4.99f)];
}

void Engine::explode(Vec3 p) {
    fx_.explosion(p, 1.f);
    phys_.explosion(p, 5.f, 14.f);
    shake_ = std::max(shake_, 0.6f);
}
void Engine::addFire(Vec3 p) { fx_.addEmitter(p, 0); }

void Engine::fire() {
    if (fireCool_ > 0) return;
    fireCool_ = 0.09f;
    defaultMuzzle();
    Vec3 dir = axisVec();
    fx_.muzzleFlash(muzzle_, dir, 1.f);
    BodyDesc d;
    d.shape = ShapeType::Sphere; d.radius = 0.025f; d.mass = 0.05f; d.pos = muzzle_ + dir * 0.06f;
    rng_ = rng_ * 1664525u + 1013904223u;
    float sx = ((rng_ >> 8) & 0xFFFF) / 65535.f - 0.5f;
    rng_ = rng_ * 1664525u + 1013904223u;
    float sy = ((rng_ >> 8) & 0xFFFF) / 65535.f - 0.5f;
    d.vel = normalize(dir + Vec3(sx, sy, sx * 0.5f) * 0.02f) * 80.f;
    d.bullet = true; d.tag = 1; d.lifetime = 3.5f; d.restitution = 0.3f; d.friction = 0.3f;
    int id = phys_.addBody(d);
    if (id >= 0) { if ((int)vis_.size() <= id) vis_.resize(id + 1); vis_[id].color = {1.f, 0.85f, 0.4f}; }
    shake_ = std::max(shake_, 0.18f);
}

void Engine::handleTap(float x, float y) {
    Vec3 o, d;
    if (!pickRay(x, y, o, d)) return;
    RayHit h;
    if (!phys_.raycast(o, d, 300.f, h)) return;
    switch (tool_) {
        case T_MUZZLE: muzzle_ = h.point + h.normal * 0.01f; muzzleSet_ = true; fx_.smokePuff(muzzle_, 0.15f, 0.6f); break;
        case T_BOX: spawnBody(true, h.point + Vec3(0, 1.4f, 0)); break;
        case T_BALL: spawnBody(false, h.point + Vec3(0, 1.4f, 0)); break;
        case T_FIRE: addFire(h.point + h.normal * 0.02f); break;
        case T_SMOKE: fx_.addEmitter(h.point + h.normal * 0.05f, 1); break;
        case T_BOOM: explode(h.point + h.normal * 0.15f); break;
    }
}

// ============================================================ обновление
void Engine::update(float dt) {
    dt = clampf(dt, 0.0005f, 0.05f);
    time_ += dt;
    fps_ = fps_ * 0.92f + (1.f / dt) * 0.08f;
    if (statusT_ > 0) statusT_ -= dt;
    shake_ = std::max(0.f, shake_ - dt * 1.8f);
    fireCool_ = std::max(0.f, fireCool_ - dt);
    anim_.update(dt);
    scroll_ += anim_.travelSpeed() * dt;
    phys_.step(dt);
    for (const auto& e : phys_.events()) {
        const auto& bs = phys_.bodies();
        bool bullet = e.bodyA >= 0 && e.bodyA < (int)bs.size() && bs[e.bodyA].tag == 1;
        if (e.speed > 3.f) fx_.impact(e.point, e.normal, bullet ? e.speed * 0.3f : e.speed, e.bodyB < 0);
        if (bullet && e.speed > 20.f) fx_.smokePuff(e.point, 0.25f, 0.9f);
    }
    for (const Body& b : phys_.bodies()) if (b.alive && b.tag == 1) fx_.trail(b.pos, b.vel);
    fx_.update(dt, 0.f);
}

// ============================================================ отрисовка
void Engine::drawModel(int pass, const Locs& L) {
    if (cur_ < 0) return;
    const Model& m = *models_[cur_];
    int lastSkin = -2;
    for (int ni : m.order) {
        for (int mi : m.nodes[ni].meshes) {
            const MeshData& md = m.meshes[mi];
            bool blend = md.alphaMode == 2;
            if ((pass == 2) != blend && pass != 0) continue;
            if (pass == 0 && blend) continue;
            if (md.skin >= 0 && md.skin < (int)anim_.jointMat.size()) {
                Mat4 M = xf_;
                glUniformMatrix4fv(L.model, 1, GL_FALSE, M.m);
                glUniform1i(L.skinned, 1);
                if (lastSkin != md.skin) {
                    const auto& jm = anim_.jointMat[md.skin];
                    glUniformMatrix4fv(L.joints, (GLsizei)std::min<size_t>(jm.size(), kMaxJoints), GL_FALSE, jm[0].m);
                    lastSkin = md.skin;
                }
            } else {
                Mat4 M = xf_ * anim_.world[ni];
                glUniformMatrix4fv(L.model, 1, GL_FALSE, M.m);
                glUniform1i(L.skinned, 0);
            }
            bool tex = md.texture >= 0 && md.texture < (int)m.textures.size() && m.textures[md.texture].id;
            glUniform1i(L.hasTex, tex ? 1 : 0);
            glUniform1i(L.alphaMode, md.alphaMode);
            glUniform1f(L.cutoff, md.alphaCutoff);
            if (L.shin >= 0) glUniform1f(L.shin, md.shininess);
            if (tex) { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m.textures[md.texture].id); }
            if (pass != 0) { if (md.doubleSided) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE); }
            md.draw();
        }
    }
}

void Engine::drawBodies(bool shadowPass, const Locs& L) {
    glUniform1i(L.skinned, 0);
    glUniform1i(L.hasTex, 0);
    glUniform1i(L.alphaMode, 0);
    if (L.shin >= 0) glUniform1f(L.shin, 40.f);
    const auto& bs = phys_.bodies();
    for (int i = 0; i < (int)bs.size(); i++) {
        const Body& b = bs[i];
        if (!b.alive) continue;
        Vec3 col = i < (int)vis_.size() ? vis_[i].color : Vec3(0.8f, 0.8f, 0.8f);
        if (!shadowPass && L.tint >= 0) glUniform4f(L.tint, col.x, col.y, col.z, 1.f);
        Mat4 M;
        if (b.shape == ShapeType::Box) M = b.transform() * scaleM({b.half.x * 2, b.half.y * 2, b.half.z * 2});
        else M = b.transform() * scaleM({b.radius, b.radius, b.radius});
        glUniformMatrix4fv(L.model, 1, GL_FALSE, M.m);
        (b.shape == ShapeType::Box ? cube_ : sphere_).draw();
    }
}

void Engine::drawBones(const Mat4& vp, Vec3 right, Vec3 up, Vec3 cam) {
    if (cur_ < 0) return;
    const Model& m = *models_[cur_];
    boneVerts_.clear();
    auto quad = [&](Vec3 a, Vec3 b, float w, const float* c) {
        Vec3 d = b - a, vd = normalize((a + b) * 0.5f - cam);
        Vec3 s = normalize(cross(d, vd)) * w;
        Vec3 p[4] = {a - s, a + s, b + s, b - s};
        int ix[6] = {0, 1, 2, 0, 2, 3};
        for (int i : ix) { boneVerts_.insert(boneVerts_.end(), {p[i].x, p[i].y, p[i].z, c[0], c[1], c[2], c[3]}); }
    };
    const float cyan[4] = {0.2f, 0.9f, 1.f, 1.f}, orange[4] = {1.f, 0.55f, 0.1f, 1.f};
    for (const Skin& sk : m.skins) {
        for (int j : sk.joints) {
            Vec3 pj = transformPoint(xf_ * anim_.world[j], {0, 0, 0});
            int par = m.nodes[j].parent;
            if (par >= 0) {
                Vec3 pp = transformPoint(xf_ * anim_.world[par], {0, 0, 0});
                if (length(pj - pp) > 1e-4f) quad(pp, pj, 0.008f, cyan);
            }
            float r = 0.02f;
            Vec3 pts[4] = {pj - right * r - up * r, pj + right * r - up * r, pj + right * r + up * r, pj - right * r + up * r};
            int ix[6] = {0, 1, 2, 0, 2, 3};
            for (int i : ix) boneVerts_.insert(boneVerts_.end(), {pts[i].x, pts[i].y, pts[i].z, orange[0], orange[1], orange[2], 1.f});
        }
    }
    if (boneVerts_.empty()) return;
    glUseProgram(progFlat_);
    glUniformMatrix4fv(fVP_, 1, GL_FALSE, vp.m);
    glBindVertexArray(boneVao_);
    glBindBuffer(GL_ARRAY_BUFFER, boneVbo_);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(boneVerts_.size() * sizeof(float)), boneVerts_.data(), GL_STREAM_DRAW);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(boneVerts_.size() / 7));
    glEnable(GL_DEPTH_TEST);
}

void Engine::render() {
    if (!glReady_) return;
    // --- свет и тень ---
    Vec3 c = target_;
    float R = std::max(length(wmax_ - wmin_) * 0.7f + 0.8f, 4.5f);
    Vec3 L = normalize(lightDir_);
    lightVP_ = ortho(-R, R, -R, R, 0.1f, R * 7.f) * lookAt(c + L * (R * 3.5f), c, {0, 1, 0});
    if (shadows_ && shadowFbo_) {
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
        glViewport(0, 0, shadowSize_, shadowSize_);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glClear(GL_DEPTH_BUFFER_BIT);
        glDisable(GL_CULL_FACE);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.f, 4.f);
        glUseProgram(progDepth_);
        glUniformMatrix4fv(dL_.vp, 1, GL_FALSE, lightVP_.m);
        glUniform1i(dL_.tex, 0);
        drawModel(0, dL_);
        drawBodies(true, dL_);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    // --- основной проход ---
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo_);
    glViewport(0, 0, w_, h_);
    const float bg[3] = {0.035f, 0.045f, 0.065f};
    glClearColor(bg[0], bg[1], bg[2], 1.f);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Vec3 eye;
    Mat4 view = viewMatrix(&eye);
    Mat4 proj = perspective(fovy_, (float)w_ / h_, 0.05f, 200.f);
    Mat4 vp = proj * view;
    lastVP_ = vp;
    Vec3 right(view.m[0], view.m[4], view.m[8]), up(view.m[1], view.m[5], view.m[9]);

    glUseProgram(progScene_);
    glUniformMatrix4fv(sL_.vp, 1, GL_FALSE, vp.m);
    glUniformMatrix4fv(sL_.lightVP, 1, GL_FALSE, lightVP_.m);
    glUniform3f(sL_.lightDir, L.x, L.y, L.z);
    glUniform3f(sL_.cam, eye.x, eye.y, eye.z);
    glUniform3f(sL_.bg, std::pow(bg[0], 1.f / 2.2f), std::pow(bg[1], 1.f / 2.2f), std::pow(bg[2], 1.f / 2.2f));
    glUniform1i(sL_.shadowOn, (shadows_ && shadowFbo_) ? 1 : 0);
    glUniform1f(sL_.texel, 1.f / shadowSize_);
    glUniform1i(sL_.tex, 0);
    glUniform1i(sL_.shadow, 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowTex_ ? shadowTex_ : whiteTex_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, whiteTex_);
    float pl[16] = {0}, plc[12] = {0};
    PointLight lights[4];
    int nl = fx_.lights(lights, 4);
    for (int i = 0; i < 4; i++) {
        if (i < nl) {
            pl[i * 4] = lights[i].pos.x; pl[i * 4 + 1] = lights[i].pos.y; pl[i * 4 + 2] = lights[i].pos.z; pl[i * 4 + 3] = lights[i].radius;
            plc[i * 3] = lights[i].color.x * lights[i].intensity; plc[i * 3 + 1] = lights[i].color.y * lights[i].intensity; plc[i * 3 + 2] = lights[i].color.z * lights[i].intensity;
        } else pl[i * 4 + 3] = 1.f;
    }
    glUniform4fv(sL_.pl, 4, pl);
    glUniform3fv(sL_.plc, 4, plc);

    // земля
    glUniform4f(sL_.tint, 1, 1, 1, 1);
    glUniform1i(sL_.checker, 1);
    glUniform1f(sL_.scroll, scroll_);
    Mat4 I;
    glUniformMatrix4fv(sL_.model, 1, GL_FALSE, I.m);
    glUniform1i(sL_.skinned, 0); glUniform1i(sL_.hasTex, 0); glUniform1i(sL_.alphaMode, 0);
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
    ground_.draw();
    glUniform1i(sL_.checker, 0);
    // модель
    glUniform4f(sL_.tint, 1, 1, 1, 1);
    drawModel(1, sL_);
    glUniform1i(sL_.alphaMode, 0);
    glEnable(GL_CULL_FACE);
    drawBodies(false, sL_);
    // прозрачные части модели
    glUniform4f(sL_.tint, 1, 1, 1, 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    drawModel(2, sL_);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);

    // эффекты и кости
    fx_.render(vp, right, up, eye);
    if (bones_) drawBones(vp, right, up, eye);

    if (showUI_) { buildUI(); flushUI(); }
}

// ============================================================ интерфейс
void Engine::uiRect(float x, float y, float w, float h, const float* c) {
    uint8_t col[4] = {(uint8_t)(c[0] * 255), (uint8_t)(c[1] * 255), (uint8_t)(c[2] * 255), (uint8_t)(c[3] * 255)};
    float u = (15 * 8 + 4) / 128.f, v = (7 * 8 + 4) / 64.f;
    UIVert q[6];
    float xs[6] = {x, x + w, x + w, x, x + w, x}, ys[6] = {y, y, y + h, y, y + h, y + h};
    for (int i = 0; i < 6; i++) { q[i] = {xs[i], ys[i], u, v, {col[0], col[1], col[2], col[3]}}; uiVerts_.push_back(q[i]); }
}

void Engine::uiText(float x, float y, const std::string& s, int sc, const float* c, bool shadow) {
    if (shadow) { const float k[4] = {0, 0, 0, 0.8f * c[3]}; uiText(x + sc, y + sc, s, sc, k, false); }
    uint8_t col[4] = {(uint8_t)(c[0] * 255), (uint8_t)(c[1] * 255), (uint8_t)(c[2] * 255), (uint8_t)(c[3] * 255)};
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 32);
        if (ch < 32 || ch > 95) ch = '?';
        if (ch == ' ') continue;
        int cx = ch % 16, cy = ch / 16;
        float u0 = cx * 8 / 128.f, u1 = (cx * 8 + 6) / 128.f, v0 = cy * 8 / 64.f, v1 = (cy * 8 + 8) / 64.f;
        float x0 = x + (float)i * 6 * sc, y0 = y, x1 = x0 + 6 * sc, y1 = y0 + 8 * sc;
        UIVert q[6] = {{x0, y0, u0, v0, {col[0], col[1], col[2], col[3]}}, {x1, y0, u1, v0, {col[0], col[1], col[2], col[3]}},
                       {x1, y1, u1, v1, {col[0], col[1], col[2], col[3]}}, {x0, y0, u0, v0, {col[0], col[1], col[2], col[3]}},
                       {x1, y1, u1, v1, {col[0], col[1], col[2], col[3]}}, {x0, y1, u0, v1, {col[0], col[1], col[2], col[3]}}};
        for (auto& v : q) uiVerts_.push_back(v);
    }
}

void Engine::flushUI() {
    if (uiVerts_.empty()) return;
    glUseProgram(progUI_);
    glUniform2f(uiRes_, (float)w_, (float)h_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fontTex_);
    glUniform1i(glGetUniformLocation(progUI_, "u_tex"), 0);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(uiVao_);
    glBindBuffer(GL_ARRAY_BUFFER, uiVbo_);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(uiVerts_.size() * sizeof(UIVert)), uiVerts_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)uiVerts_.size());
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Engine::buildUI() {
    btns_.clear();
    uiVerts_.clear();
    const float u = std::min(w_, h_) / 360.f;
    const int ts = std::max(2, (int)(u * 1.1f + 0.5f));
    const float pad = ts * 3.f, gap = ts * 2.f, bh = 8 * ts + 2 * pad, margin = ts * 4.f, top = ts * 7.f;
    const float maxW = w_ - 2 * margin;

    struct Item { int id; std::string label; bool on; };
    auto flow = [&](const std::vector<Item>& items, float y0, bool place) {  // возвращает высоту
        float x = margin, y = y0, rowH = bh;
        for (const Item& it : items) {
            float bw = textWidth(it.label, ts) + 2 * pad;
            if (x + bw > margin + maxW && x > margin) { x = margin; y += bh + gap; }
            if (place) btns_.push_back({it.id, it.label, x, y, bw, bh, it.on});
            x += bw + gap;
        }
        return y + rowH - y0;
    };

    char buf[64];
    std::vector<Item> topRow;
    topRow.push_back({B_OPEN, "OPEN", false});
    if (models_.size() > 1) topRow.push_back({B_MODEL, "MODEL " + std::to_string(cur_ + 1) + "/" + std::to_string(models_.size()), false});
    if (cur_ >= 0 && !models_[cur_]->clips.empty()) {
        topRow.push_back({B_PAUSE, paused_ ? "PLAY" : "PAUSE", paused_});
        std::snprintf(buf, sizeof buf, "SPEED %gX", kSpeeds[speedIdx_]);
        topRow.push_back({B_SPEED, buf, false});
    }
    topRow.push_back({B_SHADOW, "SHADOW", shadows_});
    if (cur_ >= 0 && models_[cur_]->skins.size()) topRow.push_back({B_BONES, "BONES", bones_});
    float topH = flow(topRow, top, true);

    std::vector<Item> toolRow;
    for (int t = 0; t < T_COUNT; t++) toolRow.push_back({B_TOOL + t, kToolNames[t], tool_ == t});
    std::vector<Item> actRow = {{B_SHOOT, "SHOOT", false}, {B_AXIS, std::string("AIM ") + kAxisNames[axis_], false}, {B_RESET, "RESET", false}};
    std::vector<Item> clipRow;
    if (cur_ >= 0) for (size_t i = 0; i < models_[cur_]->clips.size(); i++) {
        std::string n = models_[cur_]->clips[i].name;
        if (n.size() > 12) n.resize(12);
        clipRow.push_back({B_CLIP + (int)i, n, (int)i == anim_.current()});
    }
    float hAct = flow(actRow, 0, false), hTool = flow(toolRow, 0, false), hClip = clipRow.empty() ? 0.f : flow(clipRow, 0, false);
    float y = h_ - margin - hAct;
    flow(actRow, y, true);
    y -= gap + hTool; flow(toolRow, y, true);
    if (!clipRow.empty()) { y -= gap + hClip; flow(clipRow, y, true); }

    // рисуем кнопки
    for (const Btn& b : btns_) {
        bool pressed = pressedBtn_ == b.id;
        float bgc[4] = {0.08f, 0.10f, 0.14f, 0.78f};
        if (b.on) { bgc[0] = 0.10f; bgc[1] = 0.42f; bgc[2] = 0.85f; bgc[3] = 0.9f; }
        if (b.id == B_SHOOT) { bgc[0] = 0.85f; bgc[1] = 0.30f; bgc[2] = 0.10f; bgc[3] = 0.92f; }
        if (pressed) { bgc[0] += 0.15f; bgc[1] += 0.15f; bgc[2] += 0.15f; }
        uiRect(b.x, b.y, b.w, b.h, bgc);
        const float line[4] = {1, 1, 1, 0.18f};
        uiRect(b.x, b.y, b.w, ts * 0.5f, line);
        const float white[4] = {1, 1, 1, 1};
        uiText(b.x + pad, b.y + pad, b.label, ts, white, false);
    }
    // статус
    const float txt[4] = {0.85f, 0.92f, 1.f, 0.95f}, dim[4] = {0.7f, 0.78f, 0.9f, 0.75f};
    float ty = top + topH + gap;
    std::string info;
    if (cur_ >= 0) {
        const Model& m = *models_[cur_];
        info = m.name + "  " + (m.triangles() < 1000 ? std::to_string(m.triangles()) + " TRIS  " : std::to_string(m.triangles() / 1000) + "." + std::to_string(m.triangles() % 1000 / 100) + "K TRIS  ") + std::to_string(m.jointCount()) + " BONES";
    } else info = "NO MODEL";
    uiText(margin, ty, info, ts, txt);
    std::snprintf(buf, sizeof buf, "FPS %d  BODIES %d  FX %d", (int)(fps_ + 0.5f), phys_.aliveCount(), fx_.count());
    uiText(margin, ty + 10 * ts, buf, ts, dim);
    if (cur_ >= 0 && anim_.current() >= 0) {
        std::snprintf(buf, sizeof buf, "CLIP %s  %.2f/%.2fS", models_[cur_]->clips[anim_.current()].name.substr(0, 14).c_str(), anim_.time(), anim_.duration());
        uiText(margin, ty + 20 * ts, buf, ts, dim);
    }
    float ey = ty + 30 * ts;
    if (!colliderNote_.empty()) { uiText(margin, ey, colliderNote_, ts, txt); ey += 10 * ts; }
    if (statusT_ > 0) { const float st[4] = {1.f, 0.85f, 0.4f, std::min(1.f, statusT_)}; uiText(margin, ey, status_, ts, st); }
    else if (cur_ < 0) {
        const float hc[4] = {1, 1, 1, 0.9f};
        std::string l1 = "TAP OPEN TO LOAD A MODEL", l2 = "OBJ  GLB  GLTF  STL";
        uiText((w_ - textWidth(l1, ts + 1)) * 0.5f, h_ * 0.35f, l1, ts + 1, hc);
        uiText((w_ - textWidth(l2, ts + 1)) * 0.5f, h_ * 0.35f + 12 * (ts + 1), l2, ts + 1, dim);
        if (!hint_.empty()) uiText(margin, h_ * 0.35f + 26 * (ts + 1), hint_, ts, dim);
    }
    // прицел для выбранного инструмента
    if (tool_ == T_MUZZLE) {
        Vec3 d = muzzle_; float w1;
        Vec3 pc = xformP(lastVP_, d, 1.f, &w1);
        if (w1 > 0.01f) {
            float sx = (pc.x / w1 * 0.5f + 0.5f) * w_, sy = (1.f - (pc.y / w1 * 0.5f + 0.5f)) * h_;
            const float mc[4] = {1.f, 0.35f, 0.1f, 0.95f};
            uiRect(sx - ts * 4, sy - ts * 0.5f, ts * 8, ts, mc);
            uiRect(sx - ts * 0.5f, sy - ts * 4, ts, ts * 8, mc);
        }
    }
}

int Engine::hitButton(float x, float y) const {
    for (const Btn& b : btns_) if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) return b.id;
    return -1;
}

void Engine::trigger(int id) {
    if (id == B_OPEN) { if (onOpenRequest) onOpenRequest(); else setStatus("OPEN IS NOT AVAILABLE HERE"); }
    else if (id == B_MODEL) selectModel((cur_ + 1) % std::max<int>(1, (int)models_.size()));
    else if (id == B_PAUSE) { paused_ = !paused_; anim_.paused = paused_; }
    else if (id == B_SPEED) { speedIdx_ = (speedIdx_ + 1) % 4; anim_.speed = kSpeeds[speedIdx_]; }
    else if (id == B_SHADOW) shadows_ = !shadows_;
    else if (id == B_BONES) bones_ = !bones_;
    else if (id == B_SHOOT) fire();
    else if (id == B_AXIS) { axis_ = (axis_ + 1) % 6; muzzleSet_ = false; defaultMuzzle(); }
    else if (id == B_RESET) { phys_.clearBodies(); vis_.clear(); fx_.clear(); }
    else if (id >= B_TOOL && id < B_TOOL + T_COUNT) tool_ = id - B_TOOL;
    else if (id >= B_CLIP) anim_.play(id - B_CLIP);
}

bool Engine::pressButton(const std::string& label) {
    buildUI();
    for (const Btn& b : btns_) if (b.label == label) { trigger(b.id); return true; }
    return false;
}

// ============================================================ касания
void Engine::touch(int action, int id, float x, float y) {
    const float tapDist = std::min(w_, h_) / 360.f * 14.f;
    auto find = [&]() -> int { for (size_t i = 0; i < ptrs_.size(); i++) if (ptrs_[i].id == id) return (int)i; return -1; };
    if (action == 0) {
        Ptr p{id, x, y, x, y, time_, -1, false};
        int b = showUI_ ? hitButton(x, y) : -1;
        p.btn = b;
        p.camera = b < 0;
        if (b >= 0) pressedBtn_ = b;
        ptrs_.push_back(p);
    } else if (action == 1) {
        int i = find();
        if (i < 0) return;
        Ptr& p = ptrs_[i];
        float dx = x - p.x, dy = y - p.y;
        if (p.camera) {
            int others = 0; Ptr* o = nullptr;
            for (auto& q : ptrs_) if (q.camera && q.id != id) { others++; o = &q; }
            if (others == 0) {
                yaw_ -= dx * 0.006f;
                pitch_ = clampf(pitch_ + dy * 0.006f, -0.15f, 1.45f);
            } else if (o) {
                float d0 = std::hypot(p.x - o->x, p.y - o->y), d1 = std::hypot(x - o->x, y - o->y);
                if (d0 > 5 && d1 > 5) dist_ = clampf(dist_ * d0 / d1, baseDist_ * 0.25f, baseDist_ * 5.f);
                target_.y = clampf(target_.y - dy * 0.004f * dist_ * 0.5f, 0.f, 6.f);
            }
        }
        p.x = x; p.y = y;
    } else {
        int i = find();
        if (i < 0) return;
        Ptr p = ptrs_[i];
        ptrs_.erase(ptrs_.begin() + i);
        if (p.btn >= 0) {
            if (action == 2 && hitButton(x, y) == p.btn) trigger(p.btn);
            pressedBtn_ = -1;
        } else if (action == 2 && p.camera && std::hypot(x - p.sx, y - p.sy) < tapDist && ptrs_.empty()) {
            handleTap(x, y);
        }
    }
}

}  // namespace eng
