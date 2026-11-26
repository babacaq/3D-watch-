#include "ui.h"

#include <zlib.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "math3d.h"

namespace eng {

namespace theme {
Col bg, panel, panel2, widget, widgetHot, accent, accent2, text, textDim, danger, ok, keyCol, line, orange;
Col onAccent, accentEnd, overlay, shade, popup, homeTop, thumb;
static int cur = 0;
const char* name(int id) {
    static const char* n[kCount] = {"Чёрная", "Белая", "GTA 6"};
    return n[id < 0 || id >= kCount ? 0 : id];
}
bool gradient() { return cur == 2; }
void apply(int id) {
    if (id < 0 || id >= kCount) id = 0;
    cur = id;
    if (id == 0) {  // нейтральный графит и оранжевый акцент, без фиолетового
        bg = Col::hex(0x0A0A0B); panel = Col::hex(0x141416); panel2 = Col::hex(0x1C1C1F); widget = Col::hex(0x2A2A2E); widgetHot = Col::hex(0x3A3A40);
        accent = Col::hex(0xFF7A1A); accent2 = Col::hex(0x4DB8FF); text = Col::hex(0xF2F2F2); textDim = Col::hex(0x9A9AA2);
        danger = Col::hex(0xE5484D); ok = Col::hex(0x3DD68C); keyCol = Col::hex(0xFFC83D); line = Col::hex(0x2E2E33); orange = Col::hex(0xFFB020);
        onAccent = Col::hex(0x141414); accentEnd = accent; overlay = Col(0.04f, 0.04f, 0.045f, 0.62f); shade = Col(0, 0, 0, 0.7f);
        popup = Col(0.06f, 0.06f, 0.07f, 0.92f); homeTop = Col::hex(0x161618); thumb = Col::hex(0x1E1E21);
    } else if (id == 1) {
        bg = Col::hex(0xEDEDF0); panel = Col::hex(0xFFFFFF); panel2 = Col::hex(0xF5F5F7); widget = Col::hex(0xE3E3E8); widgetHot = Col::hex(0xD2D2DA);
        accent = Col::hex(0x1F6FEB); accent2 = Col::hex(0x0B6BC9); text = Col::hex(0x16161A); textDim = Col::hex(0x666672);
        danger = Col::hex(0xD92D4A); ok = Col::hex(0x138A5B); keyCol = Col::hex(0xE09A00); line = Col::hex(0xD6D6DD); orange = Col::hex(0xE8710A);
        onAccent = Col::hex(0xFFFFFF); accentEnd = accent; overlay = Col(1, 1, 1, 0.78f); shade = Col(0.1f, 0.1f, 0.14f, 0.45f);
        popup = Col(1, 1, 1, 0.96f); homeTop = Col::hex(0xFFFFFF); thumb = Col::hex(0xE0E0E6);
    } else {  // закат Вайс-Сити: ночной фиолет, розовый и оранжевый, бирюзовые акценты
        bg = Col::hex(0x0D0714); panel = Col::hex(0x170C24); panel2 = Col::hex(0x20112F); widget = Col::hex(0x2E1A44); widgetHot = Col::hex(0x44275F);
        accent = Col::hex(0xFF3E8A); accent2 = Col::hex(0x2DE2E6); text = Col::hex(0xFFF3E8); textDim = Col::hex(0xB59ACB);
        danger = Col::hex(0xFF4D4D); ok = Col::hex(0x4DF0A8); keyCol = Col::hex(0xFFD23D); line = Col::hex(0x4A2A63); orange = Col::hex(0xFFA13D);
        onAccent = Col::hex(0xFFFFFF); accentEnd = Col::hex(0xFF9B3D); overlay = Col(0.07f, 0.03f, 0.12f, 0.66f); shade = Col(0.03f, 0.0f, 0.06f, 0.74f);
        popup = Col(0.09f, 0.04f, 0.15f, 0.95f); homeTop = Col::hex(0x3A1250); thumb = Col::hex(0x2A1740);
    }
}
namespace {
const bool kThemeInit = (apply(0), true);
}
}  // namespace theme

#include "font_data.inc"

// ============================================================ шейдер
static const char* kVS = R"(#version 300 es
layout(location=0) in vec2 a_pos;
layout(location=1) in vec2 a_uv;
layout(location=2) in vec4 a_col;
layout(location=3) in vec4 a_prm;
layout(location=4) in float a_sw;
uniform vec2 u_res;
out vec2 v_uv; out vec4 v_col; out vec4 v_prm; out float v_sw;
void main(){
  v_uv = a_uv; v_col = a_col; v_prm = a_prm; v_sw = a_sw;
  vec2 p = a_pos / u_res * 2.0 - 1.0;
  gl_Position = vec4(p.x, -p.y, 0.0, 1.0);
}
)";
static const char* kFS = R"(#version 300 es
precision highp float; precision highp sampler2D;
in vec2 v_uv; in vec4 v_col; in vec4 v_prm; in float v_sw;
uniform sampler2D u_atlas;
out vec4 o;
float sdBox(vec2 p, vec2 b, float r){ vec2 q = abs(p) - b + r; return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }
void main(){
  float a = 1.0;
  float mode = v_prm.w;
  if (mode > 2.5) { vec4 t = texture(u_atlas, v_uv); float al3 = v_col.a; o = vec4(t.rgb * al3, al3); return; }
  if (mode < 0.5) { float d = sdBox(v_uv, v_prm.xy, v_prm.z); a = clamp(0.5 - d, 0.0, 1.0); }
  else if (mode < 1.5) { a = texture(u_atlas, v_uv).r; a = clamp((a - 0.5) * 1.35 + 0.5, 0.0, 1.0); }
  else if (mode < 2.5) { float d = sdBox(v_uv, v_prm.xy, v_prm.z); d = abs(d + v_sw * 0.5) - v_sw * 0.5; a = clamp(0.5 - d, 0.0, 1.0); }
  float al = v_col.a * a;
  o = vec4(v_col.rgb * al, al);
}
)";

bool Gfx::init() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER), fs = glCreateShader(GL_FRAGMENT_SHADER);
    const char* v = kVS; const char* f = kFS;
    glShaderSource(vs, 1, &v, nullptr); glCompileShader(vs);
    glShaderSource(fs, 1, &f, nullptr); glCompileShader(fs);
    GLint ok1 = 0, ok2 = 0;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok1); glGetShaderiv(fs, GL_COMPILE_STATUS, &ok2);
    if (!ok1 || !ok2) {
        char b[1024]; GLsizei n = 0;
        glGetShaderInfoLog(ok1 ? fs : vs, 1024, &n, b);
        ENG_LOG("ui shader: %s", b);
        return false;
    }
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs); glAttachShader(prog_, fs); glLinkProgram(prog_);
    glDeleteShader(vs); glDeleteShader(fs);
    glGetProgramiv(prog_, GL_LINK_STATUS, &ok1);
    if (!ok1) return false;
    uRes_ = glGetUniformLocation(prog_, "u_res");
    uAtlas_ = glGetUniformLocation(prog_, "u_atlas");
    glGenVertexArrays(1, &vao_); glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const GLsizei stride = 13 * 4;
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (const void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (const void*)8);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (const void*)16);
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (const void*)32);
    glEnableVertexAttribArray(4); glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, (const void*)48);
    glBindVertexArray(0);

    std::vector<uint8_t> raw((size_t)kFontAtlasW * kFontAtlasH);
    uLongf dl = (uLongf)raw.size();
    if (uncompress(raw.data(), &dl, kFontZ, kFontZSize) != Z_OK) { ENG_LOG("font atlas decode failed"); return false; }
    glGenTextures(1, &atlas_);
    glBindTexture(GL_TEXTURE_2D, atlas_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kFontAtlasW, kFontAtlasH, 0, GL_RED, GL_UNSIGNED_BYTE, raw.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    ascii_.assign(128, -1);
    for (int i = 0; i < kGlyphCount; i++) if (kGlyphs[i].code < 128) ascii_[kGlyphs[i].code] = i;
    return true;
}

void Gfx::release() {
    if (prog_) glDeleteProgram(prog_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (atlas_) glDeleteTextures(1, &atlas_);
    prog_ = vao_ = vbo_ = atlas_ = 0;
    cap_ = 0;
}

void Gfx::begin(int w, int h) {
    w_ = w; h_ = h;
    v_.clear(); runs_.clear(); clips_.clear();
    runs_.push_back({0, Rect(), false, 0});
}

void Gfx::end() {
    if (v_.empty() || !prog_) return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(prog_);
    glUniform2f(uRes_, (float)w_, (float)h_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_);
    glUniform1i(uAtlas_, 0);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    size_t bytes = v_.size() * sizeof(float);
    if (bytes > cap_) { cap_ = bytes * 2; glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cap_, nullptr, GL_STREAM_DRAW); }
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, v_.data(), GL_STREAM_DRAW);
    size_t total = v_.size() / 13;
    for (size_t i = 0; i < runs_.size(); i++) {
        size_t a = runs_[i].start, b = i + 1 < runs_.size() ? runs_[i + 1].start : total;
        if (b <= a) continue;
        if (runs_[i].hasClip) {
            const Rect& c = runs_[i].clip;
            glEnable(GL_SCISSOR_TEST);
            glScissor((GLint)std::floor(c.x), (GLint)std::floor(h_ - c.b()), (GLsizei)std::ceil(c.w) + 1, (GLsizei)std::ceil(c.h) + 1);
        } else glDisable(GL_SCISSOR_TEST);
        glBindTexture(GL_TEXTURE_2D, runs_[i].tex ? runs_[i].tex : atlas_);
        glDrawArrays(GL_TRIANGLES, (GLint)a, (GLsizei)(b - a));
    }
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    v_.clear();
}

void Gfx::pushClip(const Rect& r) {
    Rect c = clips_.empty() ? r.intersect(Rect(0, 0, (float)w_, (float)h_)) : r.intersect(clips_.back());
    clips_.push_back(c);
    runs_.push_back({v_.size() / 13, c, true, 0});
}
void Gfx::popClip() {
    if (clips_.empty()) return;
    clips_.pop_back();
    if (clips_.empty()) runs_.push_back({v_.size() / 13, Rect(), false, 0});
    else runs_.push_back({v_.size() / 13, clips_.back(), true, 0});
}

void Gfx::vtx(float x, float y, float u, float v, const Col& c, float hx, float hy, float rad, float mode, float sw) {
    float d[13] = {x, y, u, v, c.r, c.g, c.b, c.a, hx, hy, rad, mode, sw};
    v_.insert(v_.end(), d, d + 13);
}

void Gfx::quad(const Rect& r, float m, const Col& top, const Col& bot, float hx, float hy, float rad, float mode, float sw) {
    float x0 = r.x - m, y0 = r.y - m, x1 = r.r() + m, y1 = r.b() + m;
    float cx = r.cx(), cy = r.cy();
    float lx0 = x0 - cx, lx1 = x1 - cx, ly0 = y0 - cy, ly1 = y1 - cy;
    vtx(x0, y0, lx0, ly0, top, hx, hy, rad, mode, sw);
    vtx(x1, y0, lx1, ly0, top, hx, hy, rad, mode, sw);
    vtx(x1, y1, lx1, ly1, bot, hx, hy, rad, mode, sw);
    vtx(x0, y0, lx0, ly0, top, hx, hy, rad, mode, sw);
    vtx(x1, y1, lx1, ly1, bot, hx, hy, rad, mode, sw);
    vtx(x0, y1, lx0, ly1, bot, hx, hy, rad, mode, sw);
}

void Gfx::rect(const Rect& r, const Col& c, float radius) {
    if (r.w <= 0 || r.h <= 0 || c.a <= 0) return;
    quad(r, 1.f, c, c, r.w * 0.5f, r.h * 0.5f, std::min(radius, std::min(r.w, r.h) * 0.5f), 0, 0);
}
void Gfx::rectGrad(const Rect& r, const Col& top, const Col& bot, float radius) {
    if (r.w <= 0 || r.h <= 0) return;
    quad(r, 1.f, top, bot, r.w * 0.5f, r.h * 0.5f, std::min(radius, std::min(r.w, r.h) * 0.5f), 0, 0);
}
void Gfx::stroke(const Rect& r, const Col& c, float radius, float width) {
    if (r.w <= 0 || r.h <= 0 || c.a <= 0) return;
    quad(r, 1.f, c, c, r.w * 0.5f, r.h * 0.5f, std::min(radius, std::min(r.w, r.h) * 0.5f), 2, width);
}

void Gfx::image(GLuint tex, const Rect& r, float alpha) {
    if (!tex || r.w <= 0 || r.h <= 0) return;
    Run prev = runs_.back();
    runs_.push_back({v_.size() / 13, prev.clip, prev.hasClip, tex});
    Col c(1, 1, 1, alpha);
    float x0 = r.x, y0 = r.y, x1 = r.r(), y1 = r.b();
    vtx(x0, y0, 0, 0, c, 0, 0, 0, 3, 0); vtx(x1, y0, 1, 0, c, 0, 0, 0, 3, 0); vtx(x1, y1, 1, 1, c, 0, 0, 0, 3, 0);
    vtx(x0, y0, 0, 0, c, 0, 0, 0, 3, 0); vtx(x1, y1, 1, 1, c, 0, 0, 0, 3, 0); vtx(x0, y1, 0, 1, c, 0, 0, 0, 3, 0);
    runs_.push_back({v_.size() / 13, prev.clip, prev.hasClip, 0});
}

GLuint Gfx::makeTexture(const uint8_t* rgb, int w, int h) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

void Gfx::line(float x0, float y0, float x1, float y1, float w, const Col& c) {
    float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) { circle(x0, y0, w * 0.5f, c); return; }
    float ax = dx / len, ay = dy / len, nx = -ay, ny = ax;
    float hx = len * 0.5f + w * 0.5f, hy = w * 0.5f, m = 1.f;
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    float ex = hx + m, ey = hy + m;
    float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int k = 0; k < 6; k++) {
        int i = idx[k];
        float lx = sx[i] * ex, ly = sy[i] * ey;
        vtx(cx + ax * lx + nx * ly, cy + ay * lx + ny * ly, lx, ly, c, hx, hy, hy, 0, 0);
    }
}

void Gfx::tri(float x0, float y0, float x1, float y1, float x2, float y2, const Col& c) {
    // плоский треугольник без сглаживания границ + тонкая обводка для сглаживания
    float xs[3] = {x0, x1, x2}, ys[3] = {y0, y1, y2};
    for (int i = 0; i < 3; i++) vtx(xs[i], ys[i], 0, 0, c, 1e5f, 1e5f, 0, 0, 0);
    float w = 1.2f;
    for (int i = 0; i < 3; i++) { int j = (i + 1) % 3; line(xs[i], ys[i], xs[j], ys[j], w, c); }
}

// ---------------- текст ----------------
int Gfx::findGlyph(uint32_t code) const {
    if (code < 128) return ascii_[code];
    for (int i = 0; i < kGlyphCount; i++) if ((uint32_t)kGlyphs[i].code == code) return i;
    return -1;
}

static uint32_t nextCp(const std::string& s, size_t& i) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80) { i++; return c; }
    if ((c >> 5) == 6 && i + 1 < s.size()) { uint32_t cp = ((c & 31) << 6) | (s[i + 1] & 63); i += 2; return cp; }
    if ((c >> 4) == 14 && i + 2 < s.size()) { uint32_t cp = ((c & 15) << 12) | ((s[i + 1] & 63) << 6) | (s[i + 2] & 63); i += 3; return cp; }
    i++;
    return '?';
}

float Gfx::textWidth(const std::string& s, float size) const {
    float k = size / kFontSize, w = 0;
    for (size_t i = 0; i < s.size();) {
        int gi = findGlyph(nextCp(s, i));
        if (gi < 0) gi = findGlyph('?');
        if (gi >= 0) w += kGlyphs[gi].adv * k;
    }
    return w;
}

void Gfx::text(const std::string& s, float x, float by, float size, const Col& c) {
    float k = size / kFontSize;
    float cx = x;
    for (size_t i = 0; i < s.size();) {
        int gi = findGlyph(nextCp(s, i));
        if (gi < 0) gi = findGlyph('?');
        if (gi < 0) continue;
        const GlyphInfo& gl = kGlyphs[gi];
        if (gl.w > 0) {
            float x0 = cx + gl.xoff * k, y0 = by + gl.yoff * k, x1 = x0 + gl.w * k, y1 = y0 + gl.h * k;
            float u0 = (float)gl.x / kFontAtlasW, v0 = (float)gl.y / kFontAtlasH, u1 = (float)(gl.x + gl.w) / kFontAtlasW, v1 = (float)(gl.y + gl.h) / kFontAtlasH;
            vtx(x0, y0, u0, v0, c, 0, 0, 0, 1, 0); vtx(x1, y0, u1, v0, c, 0, 0, 0, 1, 0); vtx(x1, y1, u1, v1, c, 0, 0, 0, 1, 0);
            vtx(x0, y0, u0, v0, c, 0, 0, 0, 1, 0); vtx(x1, y1, u1, v1, c, 0, 0, 0, 1, 0); vtx(x0, y1, u0, v1, c, 0, 0, 0, 1, 0);
        }
        cx += gl.adv * k;
    }
}

std::string Gfx::fit(const std::string& s, float size, float maxW) const {
    if (textWidth(s, size) <= maxW) return s;
    std::string out;
    float dots = textWidth("…", size);
    for (size_t i = 0; i < s.size();) {
        size_t j = i;
        nextCp(s, j);
        std::string t = out + s.substr(i, j - i);
        if (textWidth(t, size) + dots > maxW) break;
        out = t; i = j;
    }
    return out + "…";
}

void Gfx::textIn(const Rect& r, const std::string& s, float size, const Col& c, int align) {
    float w = textWidth(s, size);
    float x = align == 0 ? r.x : (align == 1 ? r.cx() - w * 0.5f : r.r() - w);
    text(s, x, r.cy() + size * 0.36f, size, c);
}

// ---------------- значки ----------------
void Gfx::icon(Icon ic, const Rect& rr, const Col& c) {
    float side = std::min(rr.w, rr.h);
    float ox = rr.cx() - side * 0.5f, oy = rr.cy() - side * 0.5f;
    float lw = std::max(1.5f, side * 0.085f);
    auto X = [&](float u) { return ox + u * side; };
    auto Y = [&](float v) { return oy + v * side; };
    auto L = [&](float a, float b, float cc, float d) { line(X(a), Y(b), X(cc), Y(d), lw, c); };
    auto T = [&](float a, float b, float cc, float d, float e, float f) { tri(X(a), Y(b), X(cc), Y(d), X(e), Y(f), c); };
    auto R = [&](float cx, float cy, float rad) { ring(X(cx), Y(cy), rad * side, lw, c); };
    auto F = [&](float cx, float cy, float rad) { circle(X(cx), Y(cy), rad * side, c); };
    auto SR = [&](float a, float b, float w, float h, float rad) { stroke({X(a), Y(b), w * side, h * side}, c, rad * side, lw); };
    auto FR = [&](float a, float b, float w, float h, float rad) { rect({X(a), Y(b), w * side, h * side}, c, rad * side); };
    auto ARC = [&](float cx, float cy, float rad, float a0, float a1) {
        int n = 14;
        for (int i = 0; i < n; i++) {
            float t0 = a0 + (a1 - a0) * i / n, t1 = a0 + (a1 - a0) * (i + 1) / n;
            L(cx + rad * std::cos(t0), cy + rad * std::sin(t0), cx + rad * std::cos(t1), cy + rad * std::sin(t1));
        }
    };
    const float PI = 3.14159265f;
    switch (ic) {
        case Icon::Eye:
        case Icon::EyeOff: {
            for (int i = 0; i < 12; i++) {
                float t0 = i / 12.f, t1 = (i + 1) / 12.f;
                float x0 = 0.08f + 0.84f * t0, x1 = 0.08f + 0.84f * t1;
                float h0 = 0.26f * std::sin(PI * t0), h1 = 0.26f * std::sin(PI * t1);
                L(x0, 0.5f - h0, x1, 0.5f - h1); L(x0, 0.5f + h0, x1, 0.5f + h1);
            }
            F(0.5f, 0.5f, 0.12f);
            if (ic == Icon::EyeOff) L(0.15f, 0.85f, 0.85f, 0.15f);
            break;
        }
        case Icon::Plus: L(0.5f, 0.2f, 0.5f, 0.8f); L(0.2f, 0.5f, 0.8f, 0.5f); break;
        case Icon::Trash:
            L(0.18f, 0.28f, 0.82f, 0.28f); L(0.4f, 0.17f, 0.6f, 0.17f); L(0.4f, 0.17f, 0.4f, 0.28f); L(0.6f, 0.17f, 0.6f, 0.28f);
            L(0.26f, 0.28f, 0.32f, 0.85f); L(0.32f, 0.85f, 0.68f, 0.85f); L(0.68f, 0.85f, 0.74f, 0.28f);
            L(0.44f, 0.4f, 0.45f, 0.72f); L(0.56f, 0.4f, 0.55f, 0.72f);
            break;
        case Icon::Copy: SR(0.16f, 0.3f, 0.45f, 0.54f, 0.06f); SR(0.4f, 0.14f, 0.45f, 0.54f, 0.06f); break;
        case Icon::Undo:
        case Icon::Redo: {
            bool re = ic == Icon::Redo;
            auto XX = [&](float u) { return re ? 1.f - u : u; };
            int n = 14;
            for (int i = 0; i < n; i++) {
                float t0 = 0.f + (-PI * 1.05f) * i / n, t1 = (-PI * 1.05f) * (i + 1) / n;
                L(XX(0.5f + 0.3f * std::cos(t0)), 0.62f + 0.3f * std::sin(t0), XX(0.5f + 0.3f * std::cos(t1)), 0.62f + 0.3f * std::sin(t1));
            }
            T(XX(0.08f), 0.5f, XX(0.34f), 0.5f, XX(0.21f), 0.78f);
            break;
        }
        case Icon::Play: T(0.3f, 0.18f, 0.3f, 0.82f, 0.82f, 0.5f); break;
        case Icon::Pause: FR(0.25f, 0.2f, 0.17f, 0.6f, 0.03f); FR(0.58f, 0.2f, 0.17f, 0.6f, 0.03f); break;
        case Icon::Prev: FR(0.22f, 0.22f, 0.1f, 0.56f, 0.02f); T(0.8f, 0.22f, 0.8f, 0.78f, 0.36f, 0.5f); break;
        case Icon::Next: FR(0.68f, 0.22f, 0.1f, 0.56f, 0.02f); T(0.2f, 0.22f, 0.2f, 0.78f, 0.64f, 0.5f); break;
        case Icon::ToStart: FR(0.14f, 0.22f, 0.1f, 0.56f, 0.02f); T(0.52f, 0.22f, 0.52f, 0.78f, 0.26f, 0.5f); T(0.86f, 0.22f, 0.86f, 0.78f, 0.58f, 0.5f); break;
        case Icon::ToEnd: FR(0.76f, 0.22f, 0.1f, 0.56f, 0.02f); T(0.48f, 0.22f, 0.48f, 0.78f, 0.74f, 0.5f); T(0.14f, 0.22f, 0.14f, 0.78f, 0.42f, 0.5f); break;
        case Icon::Key: T(0.5f, 0.1f, 0.9f, 0.5f, 0.5f, 0.9f); T(0.5f, 0.1f, 0.5f, 0.9f, 0.1f, 0.5f); break;
        case Icon::KeyAdd: T(0.42f, 0.1f, 0.78f, 0.46f, 0.42f, 0.82f); T(0.42f, 0.1f, 0.42f, 0.82f, 0.06f, 0.46f); L(0.78f, 0.62f, 0.78f, 0.92f); L(0.63f, 0.77f, 0.93f, 0.77f); break;
        case Icon::Move:
            L(0.5f, 0.12f, 0.5f, 0.88f); L(0.12f, 0.5f, 0.88f, 0.5f);
            T(0.5f, 0.04f, 0.38f, 0.2f, 0.62f, 0.2f); T(0.5f, 0.96f, 0.38f, 0.8f, 0.62f, 0.8f);
            T(0.04f, 0.5f, 0.2f, 0.38f, 0.2f, 0.62f); T(0.96f, 0.5f, 0.8f, 0.38f, 0.8f, 0.62f);
            break;
        case Icon::Rotate: ARC(0.5f, 0.5f, 0.32f, -0.3f, PI * 1.45f); T(0.88f, 0.28f, 0.62f, 0.26f, 0.84f, 0.5f); break;
        case Icon::Scale: SR(0.12f, 0.4f, 0.48f, 0.48f, 0.04f); L(0.5f, 0.5f, 0.86f, 0.14f); L(0.64f, 0.14f, 0.86f, 0.14f); L(0.86f, 0.14f, 0.86f, 0.36f); break;
        case Icon::Camera: SR(0.1f, 0.28f, 0.58f, 0.44f, 0.06f); T(0.7f, 0.5f, 0.92f, 0.32f, 0.92f, 0.68f); break;
        case Icon::Sun: R(0.5f, 0.5f, 0.17f); for (int i = 0; i < 8; i++) { float a = PI * 2 * i / 8; L(0.5f + 0.27f * std::cos(a), 0.5f + 0.27f * std::sin(a), 0.5f + 0.4f * std::cos(a), 0.5f + 0.4f * std::sin(a)); } break;
        case Icon::Bulb: R(0.5f, 0.4f, 0.26f); L(0.4f, 0.74f, 0.6f, 0.74f); L(0.42f, 0.85f, 0.58f, 0.85f); break;
        case Icon::Cube:
        case Icon::Model:
            L(0.5f, 0.1f, 0.88f, 0.3f); L(0.88f, 0.3f, 0.88f, 0.7f); L(0.88f, 0.7f, 0.5f, 0.9f); L(0.5f, 0.9f, 0.12f, 0.7f); L(0.12f, 0.7f, 0.12f, 0.3f); L(0.12f, 0.3f, 0.5f, 0.1f);
            L(0.12f, 0.3f, 0.5f, 0.5f); L(0.88f, 0.3f, 0.5f, 0.5f); L(0.5f, 0.5f, 0.5f, 0.9f);
            break;
        case Icon::Flame: T(0.5f, 0.06f, 0.2f, 0.62f, 0.8f, 0.62f); F(0.5f, 0.64f, 0.27f); break;
        case Icon::Smoke: R(0.3f, 0.62f, 0.2f); R(0.58f, 0.5f, 0.24f); R(0.72f, 0.72f, 0.16f); break;
        case Icon::Bolt: T(0.58f, 0.06f, 0.2f, 0.56f, 0.5f, 0.56f); T(0.5f, 0.56f, 0.42f, 0.94f, 0.82f, 0.4f); T(0.5f, 0.56f, 0.82f, 0.4f, 0.58f, 0.4f); break;
        case Icon::Sparks: for (int i = 0; i < 6; i++) { float a = PI * 2 * i / 6; L(0.5f + 0.08f * std::cos(a), 0.5f + 0.08f * std::sin(a), 0.5f + 0.42f * std::cos(a), 0.5f + 0.42f * std::sin(a)); } break;
        case Icon::Menu: F(0.5f, 0.2f, 0.075f); F(0.5f, 0.5f, 0.075f); F(0.5f, 0.8f, 0.075f); break;
        case Icon::ChevDown: L(0.2f, 0.36f, 0.5f, 0.66f); L(0.5f, 0.66f, 0.8f, 0.36f); break;
        case Icon::ChevUp: L(0.2f, 0.66f, 0.5f, 0.36f); L(0.5f, 0.36f, 0.8f, 0.66f); break;
        case Icon::Close: L(0.22f, 0.22f, 0.78f, 0.78f); L(0.78f, 0.22f, 0.22f, 0.78f); break;
        case Icon::Link: SR(0.08f, 0.34f, 0.5f, 0.32f, 0.16f); SR(0.42f, 0.34f, 0.5f, 0.32f, 0.16f); break;
        case Icon::Unlink: SR(0.06f, 0.34f, 0.4f, 0.32f, 0.16f); SR(0.54f, 0.34f, 0.4f, 0.32f, 0.16f); L(0.3f, 0.86f, 0.7f, 0.14f); break;
        case Icon::Film: SR(0.14f, 0.1f, 0.72f, 0.8f, 0.05f); for (int i = 0; i < 4; i++) { FR(0.2f, 0.18f + i * 0.19f, 0.09f, 0.09f, 0.01f); FR(0.71f, 0.18f + i * 0.19f, 0.09f, 0.09f, 0.01f); } break;
        case Icon::Image: SR(0.1f, 0.18f, 0.8f, 0.64f, 0.06f); T(0.18f, 0.76f, 0.42f, 0.42f, 0.62f, 0.76f); T(0.5f, 0.76f, 0.68f, 0.52f, 0.84f, 0.76f); F(0.72f, 0.34f, 0.07f); break;
        case Icon::Check: L(0.2f, 0.52f, 0.42f, 0.74f); L(0.42f, 0.74f, 0.82f, 0.28f); break;
        case Icon::Bone: F(0.24f, 0.76f, 0.1f); F(0.76f, 0.24f, 0.1f); L(0.24f, 0.76f, 0.76f, 0.24f); break;
        case Icon::Snap: ARC(0.5f, 0.5f, 0.3f, PI, PI * 2); L(0.2f, 0.5f, 0.2f, 0.82f); L(0.8f, 0.5f, 0.8f, 0.82f); L(0.12f, 0.82f, 0.28f, 0.82f); L(0.72f, 0.82f, 0.88f, 0.82f); break;
        case Icon::Lock: SR(0.22f, 0.44f, 0.56f, 0.42f, 0.06f); ARC(0.5f, 0.44f, 0.2f, PI, PI * 2); break;
        case Icon::Folder: SR(0.08f, 0.26f, 0.84f, 0.56f, 0.06f); L(0.08f, 0.26f, 0.2f, 0.16f); L(0.2f, 0.16f, 0.42f, 0.16f); L(0.42f, 0.16f, 0.5f, 0.26f); break;
        case Icon::Save: SR(0.14f, 0.14f, 0.72f, 0.72f, 0.06f); SR(0.3f, 0.14f, 0.34f, 0.24f, 0.02f); SR(0.28f, 0.54f, 0.44f, 0.32f, 0.02f); break;
        case Icon::Ball: R(0.5f, 0.42f, 0.26f); L(0.2f, 0.86f, 0.8f, 0.86f); L(0.34f, 0.76f, 0.66f, 0.76f); break;
        case Icon::Drop: T(0.5f, 0.1f, 0.24f, 0.56f, 0.76f, 0.56f); F(0.5f, 0.62f, 0.26f); break;
        case Icon::Home: L(0.1f, 0.5f, 0.5f, 0.14f); L(0.5f, 0.14f, 0.9f, 0.5f); SR(0.2f, 0.46f, 0.6f, 0.4f, 0.04f); break;
        case Icon::Back: L(0.62f, 0.2f, 0.28f, 0.5f); L(0.28f, 0.5f, 0.62f, 0.8f); break;
        case Icon::Layers: SR(0.16f, 0.16f, 0.5f, 0.5f, 0.05f); SR(0.34f, 0.34f, 0.5f, 0.5f, 0.05f); break;
        case Icon::Dots3: F(0.2f, 0.5f, 0.075f); F(0.5f, 0.5f, 0.075f); F(0.8f, 0.5f, 0.075f); break;
        case Icon::Rig: F(0.5f, 0.16f, 0.09f); L(0.5f, 0.25f, 0.5f, 0.6f); L(0.5f, 0.34f, 0.22f, 0.52f); L(0.5f, 0.34f, 0.78f, 0.52f); L(0.5f, 0.6f, 0.3f, 0.9f); L(0.5f, 0.6f, 0.7f, 0.9f); break;
        case Icon::Palette: R(0.5f, 0.5f, 0.38f); F(0.34f, 0.4f, 0.06f); F(0.52f, 0.28f, 0.06f); F(0.68f, 0.42f, 0.06f); break;
        case Icon::Gear: R(0.5f, 0.5f, 0.2f); for (int i = 0; i < 8; i++) { float a = PI * 2 * i / 8; L(0.5f + 0.24f * std::cos(a), 0.5f + 0.24f * std::sin(a), 0.5f + 0.38f * std::cos(a), 0.5f + 0.38f * std::sin(a)); } break;
    }
}

// ============================================================ UI: ввод
void UI::begin(int w, int h, float density, const PointerState& p, float dtSec) {
    w_ = w; h_ = h; S = density; dt = dtSec;
    g.begin(w, h);
    idCounter_ = 0;
    candidate_ = 0;
    if (record) rects.clear();
    blocked = false;
    prevDown_ = down_;
    down_ = p.down;
    lastX_ = px_; lastY_ = py_;
    px_ = p.x; py_ = p.y;
    pressEdge_ = down_ && !prevDown_;
    releaseEdge_ = !down_ && prevDown_;
    if (pressEdge_) {
        downX_ = px_; downY_ = py_; lastX_ = px_; lastY_ = py_;
        moved_ = false; axis_ = 0; owner_ = 0; pressedId_ = 0;
    }
    if (down_ && !pressEdge_) {
        float dx = px_ - downX_, dy = py_ - downY_;
        if (!moved_ && dx * dx + dy * dy > slop() * slop()) moved_ = true;
        if (moved_ && axis_ == 0) axis_ = std::fabs(dx) > std::fabs(dy) ? 1 : 2;
    }
    if (releaseEdge_) { lastX_ = px_; lastY_ = py_; }
}

void UI::end() {
    if (pressEdge_) pressedId_ = candidate_;
    if (releaseEdge_) { pressedId_ = 0; owner_ = 0; moved_ = false; axis_ = 0; }
    g.end();
}

bool UI::hitTestAllowed(const Rect& r) const {
    return !blocked && r.has(px_, py_) && g.clip().has(px_, py_);
}
bool UI::inputInside(const Rect& r) const { return !blocked && r.has(downX_, downY_) && g.clip().has(downX_, downY_); }

bool UI::hit(uint32_t id, const Rect& r) {
    if (pressEdge_ && hitTestAllowed(r)) candidate_ = id;
    return true;
}

bool UI::tapOn(uint32_t id, const Rect& r) const {
    return !blocked && releaseEdge_ && pressedId_ == id && !moved_ && r.has(px_, py_) && (owner_ == 0 || owner_ == id);
}

// ============================================================ UI: виджеты
bool UI::button(const Rect& r, const std::string& label, int style, float fontDp) {
    uint32_t id = newId();
    if (record) rects.emplace(label, r);
    hit(id, r);
    bool pr = !blocked && down_ && pressedId_ == id && !moved_ && r.has(px_, py_);
    Col bg = theme::widget;
    Col tc = theme::text;
    if (style == 1) { bg = theme::accent; tc = theme::onAccent; }
    else if (style == 2) { bg = theme::danger.alpha(0.9f); tc = Col(1, 1, 1, 1); }
    else if (style == 3) { bg = Col(0, 0, 0, 0); tc = theme::textDim; }
    if (pr) bg = style == 3 ? theme::widget : bg.mix(Col(1, 1, 1, 1), 0.18f);
    if (style == 1 && theme::gradient() && !pr) g.rectGrad(r, theme::accent, theme::accentEnd, dp(8));
    else g.rect(r, bg, dp(8));
    float fs = dp(fontDp);
    g.textIn(r, g.fit(label, fs, r.w - dp(8)), fs, tc, 1);
    return tapOn(id, r);
}

bool UI::iconBtn(const Rect& r, Icon ic, bool active, const Col* tint, bool filled) {
    uint32_t id = newId();
    hit(id, r);
    bool pr = !blocked && down_ && pressedId_ == id && !moved_ && r.has(px_, py_);
    Col bg = active ? theme::accent : theme::widget;
    if (pr) bg = bg.mix(Col(1, 1, 1, 1), 0.2f);
    if (filled) g.rect(r, bg, dp(8)); else if (pr || active) g.rect(r, bg.alpha(0.5f), dp(8));
    Col c = tint ? *tint : (active && filled ? theme::onAccent : theme::text);
    g.icon(ic, r.inset(std::min(r.w, r.h) * 0.2f), c);
    return tapOn(id, r);
}

bool UI::iconLabelBtn(const Rect& r, Icon ic, const std::string& label, bool active, float fontDp) {
    uint32_t id = newId();
    if (record) rects.emplace(label, r);
    hit(id, r);
    bool pr = !blocked && down_ && pressedId_ == id && !moved_ && r.has(px_, py_);
    Col c = active ? theme::accent2 : theme::textDim;
    if (pr) c = theme::text;
    float isz = std::min(r.h * 0.46f, dp(24));
    g.icon(ic, Rect(r.cx() - isz * 0.5f, r.y + r.h * 0.08f, isz, isz), c);
    float fs = dp(fontDp);
    g.textIn(Rect(r.x, r.y + r.h * 0.62f, r.w, r.h * 0.3f), g.fit(label, fs, r.w - dp(4)), fs, c, 1);
    if (active) g.rect(Rect(r.x + r.w * 0.28f, r.y, r.w * 0.44f, dp(3)), theme::accent2, dp(2));
    return tapOn(id, r);
}

// Однострочная подпись обрезается «…»; если строка не помещается, а рамка высотой хотя бы в две строки — переносится по словам.
void UI::label(const Rect& r, const std::string& s, float fontDp, const Col& c, int align) {
    float fs = dp(fontDp);
    float lh = fs * 1.32f;
    if (g.textWidth(s, fs) <= r.w || r.h < lh * 1.7f) {
        g.textIn(r, g.fit(s, fs, r.w), fs, c, align);
        return;
    }
    std::vector<std::string> lines;
    std::string cur;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find(' ', i);
        std::string word = s.substr(i, j == std::string::npos ? std::string::npos : j - i);
        i = j == std::string::npos ? s.size() : j + 1;
        std::string t = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && g.textWidth(t, fs) > r.w) { lines.push_back(cur); cur = word; }
        else cur = t;
    }
    if (!cur.empty()) lines.push_back(cur);
    int maxLines = std::max(1, (int)std::floor(r.h / lh + 0.2f));
    if ((int)lines.size() > maxLines) {
        lines.resize((size_t)maxLines);
        lines.back() = g.fit(lines.back() + " …", fs, r.w);
    }
    float y0 = r.y + (r.h - lh * lines.size()) * 0.5f;
    if (align == 0) y0 = r.y;
    for (size_t k = 0; k < lines.size(); k++) {
        Rect lr(r.x, y0 + lh * k, r.w, lh);
        g.textIn(lr, g.fit(lines[k], fs, r.w), fs, c, align);
    }
}

bool UI::toggle(const Rect& r, const std::string& label, bool& v) {
    uint32_t id = newId();
    if (record) rects.emplace(label, r);
    hit(id, r);
    float fs = dp(14);
    g.textIn(Rect(r.x, r.y, r.w - dp(56), r.h), g.fit(label, fs, r.w - dp(64)), fs, theme::text, 0);
    Rect sw(r.r() - dp(48), r.cy() - dp(13), dp(48), dp(26));
    g.rect(sw, v ? theme::accent : theme::widget, dp(13));
    g.circle(v ? sw.r() - dp(13) : sw.x + dp(13), sw.cy(), dp(10), v ? theme::onAccent : theme::text.alpha(0.85f));
    if (tapOn(id, r)) { v = !v; return true; }
    return false;
}

WRes UI::slider(const Rect& r, const std::string& label, float& v, float lo, float hi, const char* fmt, bool bar, float dragPerPx) {
    uint32_t id = newId();
    if (record) rects.emplace(label, r);
    hit(id, r);
    WRes res = WRes::None;
    if (!blocked && down_ && pressedId_ == id && axis_ == 1 && (owner_ == 0 || owner_ == id)) {
        owner_ = id;
        float dx = px_ - lastX_;
        if (dx != 0) {
            float per = dragPerPx > 0 ? dragPerPx : (hi - lo) / std::max(1.f, r.w);
            v += dx * per;
            if (bar || hi > lo) v = clampf(v, lo, hi);
            res = WRes::Changed;
        }
    }
    bool pr = !blocked && down_ && pressedId_ == id && (owner_ == id || !moved_);
    g.rect(r, pr ? theme::widgetHot : theme::widget, dp(8));
    if (bar && hi > lo) {
        float f = clampf((v - lo) / (hi - lo), 0.f, 1.f);
        if (f > 0.001f) {
            g.pushClip(Rect(r.x, r.y, r.w * f, r.h));
            g.rect(r, theme::accent.alpha(0.75f), dp(8));
            g.popClip();
        }
    }
    char buf[48];
    std::snprintf(buf, sizeof buf, fmt, v);
    float fs = dp(13);
    g.textIn(Rect(r.x + dp(10), r.y, r.w - dp(20), r.h), g.fit(label, fs, r.w * 0.6f), fs, theme::text, 0);
    g.textIn(Rect(r.x + dp(10), r.y, r.w - dp(20), r.h), buf, fs, theme::text, 2);
    if (tapOn(id, r)) return WRes::Tapped;
    return res;
}

int UI::segmented(const Rect& r, const std::vector<std::string>& items, int sel) {
    int n = (int)items.size();
    if (n == 0) return -1;
    g.rect(r, theme::widget, dp(8));
    float w = r.w / n;
    int res = -1;
    for (int i = 0; i < n; i++) {
        Rect c(r.x + w * i, r.y, w, r.h);
        uint32_t id = newId();
        hit(id, c);
        if (record) rects.emplace(items[i], c);
        if (i == sel) g.rect(c.inset(dp(2)), theme::accent, dp(7));
        float fs = dp(12.5f);
        g.textIn(c, g.fit(items[i], fs, w - dp(6)), fs, i == sel ? theme::onAccent : theme::textDim, 1);
        if (tapOn(id, c)) res = i;
    }
    return res;
}

bool UI::listRow(const Rect& r, bool selected, bool* tappedOnly) {
    uint32_t id = newId();
    hit(id, r);
    bool pr = !blocked && down_ && pressedId_ == id && !moved_ && r.has(px_, py_);
    Col bg = selected ? theme::accent.alpha(0.35f) : (pr ? theme::widgetHot : theme::panel2);
    g.rect(r, bg, dp(8));
    if (selected) g.stroke(r, theme::accent, dp(8), dp(1.5f));
    bool t = tapOn(id, r);
    if (tappedOnly) *tappedOnly = t;
    return t;
}

// ---------------- прокрутка ----------------
void UI::beginScroll(const Rect& view, ScrollState& s, float contentH, bool horizontal) {
    ScrollCtx c{view, &s, contentH, newId(), horizontal};
    float maxOff = std::max(0.f, contentH - (horizontal ? view.w : view.h));
    s.off = clampf(s.off, 0.f, maxOff);
    scrolls_.push_back(c);
    g.pushClip(view);
}

void UI::endScroll() {
    if (scrolls_.empty()) return;
    ScrollCtx c = scrolls_.back();
    scrolls_.pop_back();
    g.popClip();
    ScrollState& s = *c.s;
    float viewLen = c.horiz ? c.view.w : c.view.h;
    float maxOff = std::max(0.f, c.contentH - viewLen);
    if (!blocked && down_ && axis_ == (c.horiz ? 1 : 2) && inputInside(c.view) && (owner_ == 0 || owner_ == c.id)) {
        owner_ = c.id;
    }
    if (owner_ == c.id && down_) {
        float dy = c.horiz ? px_ - lastX_ : py_ - lastY_;
        s.off -= dy;
        float inst = dt > 1e-4f ? -dy / dt : 0.f;
        s.vel = s.vel * 0.6f + inst * 0.4f;
    } else {
        s.off += s.vel * dt;
        s.vel *= std::exp(-4.5f * dt);
        if (std::fabs(s.vel) < 8.f) s.vel = 0;
    }
    if (s.off < 0) { s.off = 0; s.vel = 0; }
    if (s.off > maxOff) { s.off = maxOff; s.vel = 0; }
    if (maxOff > 1.f && !c.horiz) {
        float th = std::max(dp(24), c.view.h * c.view.h / c.contentH);
        float ty = c.view.y + (c.view.h - th) * (s.off / maxOff);
        g.rect(Rect(c.view.r() - dp(4), ty, dp(3), th), theme::textDim.alpha(0.5f), dp(2));
    }
}

}  // namespace eng
