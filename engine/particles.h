// 3D-эффекты: огонь, дым, вспышка выстрела, искры, пыль, взрыв. Процедурные шейдеры, текстуры не нужны.
#pragma once
#include <vector>

#include "gl_api.h"
#include "math3d.h"

namespace eng {

enum PType { P_FIRE = 0, P_SMOKE = 1, P_FLASH = 2, P_SPARK = 3, P_DUST = 4, P_BLOOD = 5 };

struct Particle {
    Vec3 pos, vel;
    float life = 0, maxLife = 1;
    float size0 = 0.1f, size1 = 0.1f;
    float rot = 0, rotVel = 0;
    float seed = 0, drag = 0, buoy = 0, alpha = 1;
    int type = P_FIRE;
};

struct Shard {
    Vec3 pos, vel, spin, v[3];
    Quat q;
    float seed = 0;
    bool rest = false;
};

struct PointLight {
    Vec3 pos, color;
    float radius = 5, life = 0, maxLife = 0.1f, intensity = 5;
};

struct Emitter {
    Vec3 pos;
    int kind = 0;  // 0 огонь (+дым+искры), 1 только дым
    float accFire = 0, accSmoke = 0, accSpark = 0;
};

class Particles {
public:
    bool init();
    void release();
    void clear();
    void seed(uint32_t s) { seed_ = s * 2654435761u + 12345u; rnd(); rnd(); }
    // Эффекты сцены (вызываются каждый шаг симуляции). acc - накопители частиц эффекта (3 числа).
    void emitFire(Vec3 pos, float scale, float dt, float* acc);
    void emitSmoke(Vec3 pos, float scale, float dt, float* acc);
    void emitSparks(Vec3 pos, Vec3 dir, float scale, float dt, float* acc);
    void fireLight(Vec3 pos, float scale, float phase);  // постоянный свет костра на этот кадр
    void beginStep() { steady_.clear(); }
    void update(float dt, float groundY);
    void setOutputScale(float s) { outScale_ = s; }  // множитель яркости (запас для LDR-буфера)
    void setLightScale(float s) { lightScale_ = s; }  // освещённость дыма и пыли
    void render(const Mat4& vp, Vec3 camRight, Vec3 camUp, Vec3 camPos);

    // готовые эффекты
    // variant: 0 пистолет, 1 дробовик, 2 автомат; power - яркость и сила
    void muzzleFlash(Vec3 pos, Vec3 dir, float scale = 1.f, int variant = 0, float power = 1.f);
    // power - сила взрыва: больше огня, дыма, осколков и дальше разлёт
    void explosion(Vec3 pos, float scale = 1.f, float power = 1.f);
    // кровь: брызги (один раз) и течение (каждый шаг); капли падают и оставляют пятна на земле
    void bloodBurst(Vec3 pos, Vec3 dir, float scale, float power);
    void bloodFlow(Vec3 pos, Vec3 dir, float scale, float power, float dt, float* acc);
    int splatCount() const { return (int)splats_.size(); }
    // стекло: осколки по плоскости (центр c, оси u, v, нормаль n, полуразмеры hu, hv); удар в точке impact толкает вдоль push
    void glassBreak(Vec3 c, Vec3 u, Vec3 v, Vec3 n, float hu, float hv, Vec3 impact, Vec3 push, float power, float shardScale);
    int shardCount() const { return (int)shards_.size(); }
    void setSun(Vec3 d) { sun_ = d; }
    void impact(Vec3 pos, Vec3 normal, float speed, bool dust);
    void smokePuff(Vec3 pos, float size, float life);
    void trail(Vec3 pos, Vec3 vel);
    int addEmitter(Vec3 pos, int kind);
    void clearEmitters() { emitters_.clear(); }
    int emitterCount() const { return (int)emitters_.size(); }

    // динамические источники света от эффектов (вспышки, костры)
    int lights(PointLight* out, int maxN) const;
    int count() const { return (int)ps_.size(); }

private:
    float rnd() { seed_ = seed_ * 1664525u + 1013904223u; return (float)((seed_ >> 8) & 0xFFFF) / 65535.f; }
    float rr(float a, float b) { return a + (b - a) * rnd(); }
    Vec3 rndDir() {
        float z = rr(-1, 1), a = rr(0, 6.2831853f), r = std::sqrt(std::max(0.f, 1 - z * z));
        return {r * std::cos(a), z, r * std::sin(a)};
    }
    void add(const Particle& p) { if (ps_.size() < 4000) ps_.push_back(p); }
    void addLight(Vec3 pos, Vec3 color, float radius, float life, float intensity);

    struct Splat { float x, z, r, seed; };
    std::vector<Shard> shards_;
    GLuint shProg_ = 0, shVao_ = 0, shBuf_ = 0;
    GLint shVP_ = -1, shCam_ = -1, shSun_ = -1, shLight_ = -1, shOut_ = -1;
    Vec3 sun_{0.4f, 0.8f, 0.4f};
    void drawShards(const Mat4& vp, Vec3 cam);
    void updateShards(float dt, float groundY);
    std::vector<Splat> splats_;
    GLuint splatProg_ = 0, splatVao_ = 0, splatBuf_ = 0;
    GLint sVP_ = -1, sLight_ = -1, sOut_ = -1, sY_ = -1;
    void drawSplats(const Mat4& vp);
    std::vector<Particle> ps_;
    std::vector<Emitter> emitters_;
    std::vector<PointLight> lights_;
    std::vector<PointLight> steady_;
    std::vector<float> inst_;
    uint32_t seed_ = 12345;
    float time_ = 0;
    GLuint prog_ = 0, vao_ = 0, quad_ = 0, ibuf_ = 0;
    GLint uVP_ = 0, uRight_ = 0, uUp_ = 0, uCam_ = 0, uOut_ = -1, uLight_ = -1;
    float outScale_ = 1.f, lightScale_ = 1.f;
    size_t ibufCap_ = 0;
};

}  // namespace eng
