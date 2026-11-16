#include "particles.h"

#include <algorithm>
#include <cmath>

#include "log.h"

namespace eng {

static const char* kVS = R"(#version 300 es
layout(location=0) in vec2 a_c;
layout(location=1) in vec4 i_ps;   // xyz, size
layout(location=2) in vec4 i_vr;   // vel xyz, rot
layout(location=3) in vec4 i_m;    // life01, type, seed, alpha
uniform mat4 u_vp; uniform vec3 u_right; uniform vec3 u_up; uniform vec3 u_cam;
out vec2 v_uv; out vec4 v_m;
void main(){
  float sz = i_ps.w; vec3 p;
  if (i_m.y > 2.5 && i_m.y < 3.5) {            // искра: вытянута вдоль скорости
    vec3 v = i_vr.xyz; float sp = length(v);
    vec3 dir = v / max(sp, 1e-3);
    vec3 vd = normalize(i_ps.xyz - u_cam);
    vec3 side = normalize(cross(vd, dir));
    p = i_ps.xyz + side * a_c.x * sz * 0.5 + dir * a_c.y * (sz + sp * 0.025);
  } else {
    float c = cos(i_vr.w), s = sin(i_vr.w);
    vec2 q = vec2(c*a_c.x - s*a_c.y, s*a_c.x + c*a_c.y);
    p = i_ps.xyz + (u_right*q.x + u_up*q.y) * sz;
  }
  gl_Position = u_vp * vec4(p, 1.0);
  v_uv = a_c; v_m = i_m;
}
)";

static const char* kFS = R"(#version 300 es
precision mediump float;
in vec2 v_uv; in vec4 v_m;
uniform float u_outScale; uniform float u_light;
out vec4 o;
float h(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453); }
float vn(vec2 p){ vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
  return mix(mix(h(i),h(i+vec2(1,0)),f.x), mix(h(i+vec2(0,1)),h(i+vec2(1,1)),f.x), f.y); }
float fbm(vec2 p){ return 0.5*vn(p) + 0.25*vn(p*2.03+7.1) + 0.125*vn(p*4.1+3.7) + 0.0625*vn(p*8.3+1.3); }
vec3 flame(float k){   // k: 0 тёмный .. 1 белый
  vec3 a = vec3(0.35,0.03,0.0), b = vec3(1.0,0.35,0.04), c = vec3(1.0,0.8,0.25), d = vec3(1.0,0.97,0.8);
  return k < 0.4 ? mix(a,b,k/0.4) : (k < 0.75 ? mix(b,c,(k-0.4)/0.35) : mix(c,d,(k-0.75)/0.25));
}
void shade(){
  float t = v_m.x, type = v_m.y, seed = v_m.z, am = v_m.w;
  vec2 p = v_uv; float r = length(p);
  if (r > 1.0) discard;
  if (type < 0.5) {                                   // ОГОНЬ (аддитивный)
    float n = fbm(p*1.6 + vec2(seed*13.1, seed*7.0 - t*2.5));
    float d = r + (n - 0.5) * 0.9 + p.y * 0.15;
    float a = smoothstep(1.0, 0.1, d);
    float heat = clamp(a * (1.15 - t) * (0.5 + n), 0.0, 1.0);
    o = vec4(flame(heat) * a * (1.0 - t*0.85) * 1.4 * am, 0.0);
  } else if (type < 1.5 || type > 3.5) {              // ДЫМ / ПЫЛЬ (альфа)
    float n = fbm(p*2.2 + vec2(seed*10.0, t*0.6));
    float d = r + (n - 0.5) * 0.85;
    float a = smoothstep(1.0, 0.3, d) * smoothstep(1.0, 0.7, r) * (1.0 - t) * (1.0 - t) * 0.75 * am;
    vec3 col = type < 1.5 ? mix(vec3(0.22,0.22,0.24), vec3(0.72,0.72,0.76), n * (1.0 - t*0.3)) : mix(vec3(0.35,0.28,0.2), vec3(0.62,0.55,0.45), n);
    o = vec4(col * u_light * a, a);
  } else if (type < 2.5) {                            // ВСПЫШКА (звезда)
    float ang = atan(p.y, p.x);
    float spikes = pow(abs(cos(ang * 2.5 + seed * 6.28)), 10.0) + 0.6 * pow(abs(cos(ang * 4.0 + seed * 3.0)), 24.0);
    float core = exp(-r * r * 7.0);
    float ray = (1.0 - r) * (1.0 - r) * spikes * 1.6;
    vec3 col = mix(vec3(1.0,0.55,0.15), vec3(1.0,0.95,0.8), core);
    o = vec4(col * (core * 2.2 + ray) * (1.0 - t) * am, 0.0);
  } else {                                            // ИСКРА (штрих)
    float a = smoothstep(1.0, 0.0, abs(p.x)) * smoothstep(1.0, 0.2, abs(p.y));
    o = vec4(mix(vec3(1.0,0.9,0.6), vec3(1.0,0.35,0.05), t) * a * (1.0 - t*0.6) * 2.0 * am, 0.0);
  }
}
void main(){ shade(); o.rgb *= u_outScale; }
)";

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char buf[1024]; GLsizei n = 0; glGetShaderInfoLog(s, 1024, &n, buf); ENG_LOG("particle shader error: %s", buf); glDeleteShader(s); return 0; }
    return s;
}

bool Particles::init() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, kVS), fs = compileShader(GL_FRAGMENT_SHADER, kFS);
    if (!vs || !fs) return false;
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs); glAttachShader(prog_, fs);
    glLinkProgram(prog_);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) { ENG_LOG("particle program link failed"); return false; }
    uVP_ = glGetUniformLocation(prog_, "u_vp"); uRight_ = glGetUniformLocation(prog_, "u_right");
    uUp_ = glGetUniformLocation(prog_, "u_up"); uCam_ = glGetUniformLocation(prog_, "u_cam");
    uOut_ = glGetUniformLocation(prog_, "u_outScale"); uLight_ = glGetUniformLocation(prog_, "u_light");
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glGenBuffers(1, &quad_);
    glBindBuffer(GL_ARRAY_BUFFER, quad_);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
    glGenBuffers(1, &ibuf_);
    glBindBuffer(GL_ARRAY_BUFFER, ibuf_);
    ibufCap_ = 0;
    for (int i = 0; i < 3; i++) {
        glEnableVertexAttribArray(1 + i);
        glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE, 48, (const void*)(size_t)(i * 16));
        glVertexAttribDivisor(1 + i, 1);
    }
    glBindVertexArray(0);
    return true;
}

void Particles::release() {
    if (prog_) glDeleteProgram(prog_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (quad_) glDeleteBuffers(1, &quad_);
    if (ibuf_) glDeleteBuffers(1, &ibuf_);
    prog_ = vao_ = quad_ = ibuf_ = 0;
    ibufCap_ = 0;
}

void Particles::clear() { ps_.clear(); emitters_.clear(); lights_.clear(); steady_.clear(); time_ = 0; }

void Particles::emitFire(Vec3 pos, float s, float dt, float* acc) {
    acc[0] += dt * 70.f * s;
    while (acc[0] >= 1.f) {
        acc[0] -= 1.f;
        Particle p; p.type = P_FIRE; p.pos = pos + Vec3(rr(-0.09f, 0.09f), rr(0, 0.05f), rr(-0.09f, 0.09f)) * s;
        p.vel = Vec3(rr(-0.15f, 0.15f), rr(0.9f, 1.8f), rr(-0.15f, 0.15f)) * s;
        p.maxLife = rr(0.55f, 1.0f); p.size0 = rr(0.22f, 0.34f) * s; p.size1 = 0.06f * s; p.drag = 0.6f; p.rotVel = rr(-1, 1); p.seed = rnd();
        add(p);
    }
}
void Particles::emitSmoke(Vec3 pos, float s, float dt, float* acc) {
    acc[1] += dt * 14.f * s;
    while (acc[1] >= 1.f) {
        acc[1] -= 1.f;
        smokePuff(pos + Vec3(rr(-0.05f, 0.05f), 0, rr(-0.05f, 0.05f)) * s, rr(0.8f, 1.2f) * s, rr(2.5f, 3.8f));
    }
}
void Particles::emitSparks(Vec3 pos, Vec3 dir, float s, float dt, float* acc) {
    acc[2] += dt * 18.f * s;
    dir = normalize(dir);
    while (acc[2] >= 1.f) {
        acc[2] -= 1.f;
        Particle p; p.type = P_SPARK; p.pos = pos;
        p.vel = normalize(dir + rndDir() * 0.7f) * rr(1.5f, 5.f) * s; p.maxLife = rr(0.5f, 1.4f); p.size0 = 0.02f * s; p.drag = 0.2f; p.seed = rnd();
        add(p);
    }
}
void Particles::fireLight(Vec3 pos, float s, float phase) {
    float fl = 1.f + 0.3f * std::sin(phase * 23.f + pos.x * 5.f) + 0.2f * std::sin(phase * 37.f + pos.z * 3.f);
    PointLight l; l.pos = pos + Vec3(0, 0.35f * s, 0); l.color = {1.f, 0.5f, 0.15f}; l.radius = 5.f * s; l.maxLife = 1; l.life = 0; l.intensity = 2.6f * fl * s;
    steady_.push_back(l);
}

void Particles::addLight(Vec3 pos, Vec3 color, float radius, float life, float intensity) {
    PointLight l;
    l.pos = pos; l.color = color; l.radius = radius; l.life = 0; l.maxLife = life; l.intensity = intensity;
    if (lights_.size() >= 8) lights_.erase(lights_.begin());
    lights_.push_back(l);
}

int Particles::lights(PointLight* out, int maxN) const {
    // самые яркие источники
    std::vector<PointLight> v = lights_;
    v.insert(v.end(), steady_.begin(), steady_.end());
    for (const Emitter& e : emitters_) {
        PointLight l;
        float fl = 1.f + 0.35f * std::sin(time_ * 23.f + e.pos.x * 5.f) + 0.25f * std::sin(time_ * 37.f + e.pos.z * 3.f);
        l.pos = e.pos + Vec3(0, 0.35f, 0); l.color = {1.f, 0.5f, 0.15f}; l.radius = 5.f; l.maxLife = 1; l.life = 0;
        l.intensity = e.kind == 0 ? 2.6f * fl : 0.f;
        if (l.intensity > 0) v.push_back(l);
    }
    auto power = [](const PointLight& l) { float k = 1.f - l.life / std::max(l.maxLife, 1e-3f); return l.intensity * k * k; };
    std::sort(v.begin(), v.end(), [&](const PointLight& a, const PointLight& b) { return power(a) > power(b); });
    int n = 0;
    for (auto& l : v) {
        if (n >= maxN) break;
        PointLight o = l;
        o.intensity = power(l);
        if (o.intensity < 0.02f) continue;
        out[n++] = o;
    }
    return n;
}

int Particles::addEmitter(Vec3 pos, int kind) {
    if (emitters_.size() >= 8) emitters_.erase(emitters_.begin());
    Emitter e; e.pos = pos; e.kind = kind;
    emitters_.push_back(e);
    return (int)emitters_.size() - 1;
}

void Particles::muzzleFlash(Vec3 pos, Vec3 dir, float s) {
    dir = normalize(dir);
    Vec3 tip = pos + dir * (0.03f * s);
    for (int k = 0; k < 2; k++) {
        Particle p; p.type = P_FLASH; p.pos = tip; p.maxLife = k ? 0.05f : 0.08f;
        p.size0 = (k ? 0.22f : 0.4f) * s; p.size1 = p.size0 * 1.5f; p.rot = rr(0, 6.28f); p.seed = rnd();
        add(p);
    }
    for (int i = 0; i < 12; i++) {
        Particle p; p.type = P_SPARK; p.pos = tip;
        p.vel = normalize(dir + rndDir() * 0.35f) * rr(6, 22) * s;
        p.maxLife = rr(0.15f, 0.45f); p.size0 = 0.025f * s; p.drag = 0.5f; p.seed = rnd();
        add(p);
    }
    for (int i = 0; i < 5; i++) {
        Particle p; p.type = P_SMOKE; p.pos = tip + dir * rr(0, 0.06f);
        p.vel = dir * rr(0.6f, 2.2f) * s + rndDir() * 0.15f + Vec3(0, 0.25f, 0);
        p.maxLife = rr(0.9f, 1.8f); p.size0 = 0.05f * s; p.size1 = rr(0.25f, 0.5f) * s; p.drag = 1.6f; p.buoy = 0.25f;
        p.rotVel = rr(-0.8f, 0.8f); p.seed = rnd(); p.alpha = 0.6f;
        add(p);
    }
    addLight(tip + dir * 0.1f, {1.f, 0.75f, 0.4f}, 6.f * std::max(0.5f, s), 0.09f, 9.f);
}

void Particles::explosion(Vec3 pos, float s) {
    for (int i = 0; i < 46; i++) {
        Particle p; p.type = P_FIRE; Vec3 d = rndDir();
        p.pos = pos + d * rr(0, 0.25f) * s; p.vel = (d + Vec3(0, 0.3f, 0)) * rr(1.5f, 6.5f) * s;
        p.maxLife = rr(0.45f, 1.0f); p.size0 = rr(0.5f, 0.9f) * s; p.size1 = rr(1.1f, 1.7f) * s;
        p.drag = 2.2f; p.buoy = 1.5f; p.rotVel = rr(-2, 2); p.seed = rnd();
        add(p);
    }
    for (int i = 0; i < 22; i++) {
        Particle p; p.type = P_SMOKE; Vec3 d = rndDir();
        p.pos = pos + d * rr(0.1f, 0.5f) * s; p.vel = (d * rr(0.6f, 3.0f) + Vec3(0, 1.3f, 0)) * s;
        p.maxLife = rr(2.2f, 4.2f); p.size0 = rr(0.5f, 0.8f) * s; p.size1 = rr(1.8f, 2.8f) * s;
        p.drag = 1.1f; p.buoy = 0.5f; p.rotVel = rr(-0.5f, 0.5f); p.seed = rnd(); p.alpha = 0.95f;
        add(p);
    }
    for (int i = 0; i < 70; i++) {
        Particle p; p.type = P_SPARK; p.pos = pos; p.vel = (rndDir() + Vec3(0, 0.6f, 0)) * rr(5, 18) * s;
        p.maxLife = rr(0.5f, 1.4f); p.size0 = 0.03f * s; p.drag = 0.25f; p.seed = rnd();
        add(p);
    }
    for (int i = 0; i < 18; i++) {
        float a = rr(0, 6.28f);
        Particle p; p.type = P_DUST; p.pos = pos + Vec3(0, 0.05f, 0);
        p.vel = Vec3(std::cos(a), 0.05f, std::sin(a)) * rr(2, 6) * s;
        p.maxLife = rr(1.2f, 2.4f); p.size0 = 0.3f * s; p.size1 = rr(0.9f, 1.5f) * s; p.drag = 2.f; p.seed = rnd(); p.alpha = 0.7f;
        add(p);
    }
    Particle fl; fl.type = P_FLASH; fl.pos = pos; fl.maxLife = 0.14f; fl.size0 = 2.2f * s; fl.size1 = 3.f * s; fl.seed = rnd();
    add(fl);
    addLight(pos + Vec3(0, 0.4f, 0), {1.f, 0.6f, 0.25f}, 9.f * s, 0.6f, 14.f);
}

void Particles::impact(Vec3 pos, Vec3 n, float speed, bool dust) {
    int ns = std::min(10, (int)(speed * 0.6f));
    for (int i = 0; i < ns; i++) {
        Particle p; p.type = P_SPARK; p.pos = pos;
        p.vel = normalize(n + rndDir() * 0.8f) * rr(1.5f, 0.55f * speed + 2.f);
        p.maxLife = rr(0.25f, 0.7f); p.size0 = 0.018f; p.drag = 0.3f; p.seed = rnd();
        add(p);
    }
    if (dust && speed > 4.f) {
        for (int i = 0; i < 3; i++) {
            Particle p; p.type = P_DUST; p.pos = pos + n * 0.02f; p.vel = n * rr(0.3f, 1.0f) + rndDir() * 0.5f;
            p.maxLife = rr(0.8f, 1.5f); p.size0 = 0.08f; p.size1 = rr(0.3f, 0.55f); p.drag = 2.f; p.seed = rnd(); p.alpha = 0.55f;
            add(p);
        }
    }
}

void Particles::smokePuff(Vec3 pos, float size, float life) {
    Particle p; p.type = P_SMOKE; p.pos = pos; p.vel = Vec3(rr(-0.1f, 0.1f), rr(0.3f, 0.8f), rr(-0.1f, 0.1f));
    p.maxLife = life; p.size0 = size * 0.4f; p.size1 = size; p.drag = 1.f; p.buoy = 0.3f; p.seed = rnd(); p.alpha = 0.7f; p.rotVel = rr(-0.5f, 0.5f);
    add(p);
}

void Particles::trail(Vec3 pos, Vec3 vel) {
    Particle p; p.type = P_SPARK; p.pos = pos; p.vel = vel * 0.15f; p.maxLife = 0.12f; p.size0 = 0.02f; p.seed = rnd();
    add(p);
    if (rnd() < 0.35f) smokePuff(pos, 0.12f, 0.8f);
}

void Particles::update(float dt, float groundY) {
    time_ += dt;
    for (auto& l : lights_) l.life += dt;
    lights_.erase(std::remove_if(lights_.begin(), lights_.end(), [](const PointLight& l) { return l.life >= l.maxLife; }), lights_.end());

    for (Emitter& e : emitters_) {
        if (e.kind == 0) {
            e.accFire += dt * 70.f; e.accSmoke += dt * 16.f; e.accSpark += dt * 4.f;
            while (e.accFire >= 1.f) {
                e.accFire -= 1.f;
                Particle p; p.type = P_FIRE; p.pos = e.pos + Vec3(rr(-0.09f, 0.09f), rr(0, 0.05f), rr(-0.09f, 0.09f));
                p.vel = Vec3(rr(-0.15f, 0.15f), rr(0.9f, 1.8f), rr(-0.15f, 0.15f));
                p.maxLife = rr(0.55f, 1.0f); p.size0 = rr(0.22f, 0.34f); p.size1 = 0.06f; p.drag = 0.6f; p.rotVel = rr(-1, 1); p.seed = rnd();
                add(p);
            }
            while (e.accSmoke >= 1.f) {
                e.accSmoke -= 1.f;
                Particle p; p.type = P_SMOKE; p.pos = e.pos + Vec3(rr(-0.06f, 0.06f), 0.45f, rr(-0.06f, 0.06f));
                p.vel = Vec3(0.25f + rr(-0.1f, 0.1f), rr(0.5f, 0.9f), 0.1f + rr(-0.1f, 0.1f));
                p.maxLife = rr(2.4f, 3.8f); p.size0 = 0.14f; p.size1 = rr(0.7f, 1.0f); p.drag = 0.4f; p.buoy = 0.15f; p.rotVel = rr(-0.4f, 0.4f); p.seed = rnd(); p.alpha = 0.65f;
                add(p);
            }
            while (e.accSpark >= 1.f) {
                e.accSpark -= 1.f;
                Particle p; p.type = P_SPARK; p.pos = e.pos + Vec3(0, 0.1f, 0);
                p.vel = Vec3(rr(-1.2f, 1.2f), rr(2.f, 4.5f), rr(-1.2f, 1.2f)); p.maxLife = rr(0.7f, 1.6f); p.size0 = 0.02f; p.drag = 0.2f; p.seed = rnd();
                add(p);
            }
        } else {
            e.accSmoke += dt * 14.f;
            while (e.accSmoke >= 1.f) { e.accSmoke -= 1.f; smokePuff(e.pos + Vec3(rr(-0.05f, 0.05f), 0, rr(-0.05f, 0.05f)), rr(0.8f, 1.2f), rr(2.5f, 3.8f)); }
        }
    }

    for (Particle& p : ps_) {
        p.life += dt;
        if (p.type == P_SPARK) p.vel += Vec3(0, -9.81f, 0) * dt;
        p.vel.y += p.buoy * dt;
        float d = 1.f / (1.f + p.drag * dt);
        p.vel = p.vel * d;
        p.pos += p.vel * dt;
        p.rot += p.rotVel * dt;
        if (p.type == P_SPARK && p.pos.y < groundY + 0.01f) { p.pos.y = groundY + 0.01f; p.vel.y = -p.vel.y * 0.4f; p.vel.x *= 0.7f; p.vel.z *= 0.7f; }
        if (p.type == P_DUST && p.pos.y < groundY + 0.03f) { p.pos.y = groundY + 0.03f; p.vel.y = 0; }
    }
    ps_.erase(std::remove_if(ps_.begin(), ps_.end(), [](const Particle& p) { return p.life >= p.maxLife; }), ps_.end());
}

void Particles::render(const Mat4& vp, Vec3 camRight, Vec3 camUp, Vec3 camPos) {
    if (!prog_ || ps_.empty()) return;
    std::vector<const Particle*> alpha, add;
    for (const Particle& p : ps_) ((p.type == P_SMOKE || p.type == P_DUST) ? alpha : add).push_back(&p);
    Vec3 fwd = normalize(cross(camRight, camUp)) * -1.f;
    std::sort(alpha.begin(), alpha.end(), [&](const Particle* a, const Particle* b) { return dot(a->pos - camPos, fwd) > dot(b->pos - camPos, fwd); });

    glUseProgram(prog_);
    glUniformMatrix4fv(uVP_, 1, GL_FALSE, vp.m);
    glUniform3f(uRight_, camRight.x, camRight.y, camRight.z);
    glUniform3f(uUp_, camUp.x, camUp.y, camUp.z);
    glUniform3f(uCam_, camPos.x, camPos.y, camPos.z);
    glUniform1f(uOut_, outScale_);
    glUniform1f(uLight_, lightScale_);
    glBindVertexArray(vao_);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);

    auto draw = [&](const std::vector<const Particle*>& list, bool additive) {
        if (list.empty()) return;
        inst_.clear();
        for (const Particle* p : list) {
            float t = p->life / p->maxLife;
            float size = p->size0 + (p->size1 - p->size0) * t;
            if (p->type == P_FLASH || p->type == P_SPARK) size = p->size0 + (p->size1 > 0 ? (p->size1 - p->size0) * t : 0);
            float v[12] = {p->pos.x, p->pos.y, p->pos.z, size, p->vel.x, p->vel.y, p->vel.z, p->rot, t, (float)p->type, p->seed, p->alpha};
            inst_.insert(inst_.end(), v, v + 12);
        }
        glBindBuffer(GL_ARRAY_BUFFER, ibuf_);
        size_t bytes = inst_.size() * sizeof(float);
        if (bytes > ibufCap_) { ibufCap_ = bytes * 2; glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)ibufCap_, nullptr, GL_STREAM_DRAW); }
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, inst_.data(), GL_STREAM_DRAW);
        glBlendFunc(GL_ONE, additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)list.size());
    };
    draw(alpha, false);
    draw(add, true);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

}  // namespace eng
