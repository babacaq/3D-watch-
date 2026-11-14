#include <chrono>
#include <cstdio>
#include <cstdlib>
#include "physics.h"
using namespace eng;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)
static void run(PhysicsWorld& w, float sec) { for (int i = 0; i < (int)(sec * 60); i++) w.step(1.f / 60); }
static BodyDesc box(Vec3 p, Vec3 h, float m = 1) { BodyDesc d; d.shape = ShapeType::Box; d.half = h; d.mass = m; d.pos = p; return d; }
static BodyDesc ball(Vec3 p, float r, float m = 1) { BodyDesc d; d.radius = r; d.mass = m; d.pos = p; return d; }

int main() {
    { // 1 bounce
        PhysicsWorld w; BodyDesc d = ball({0, 2, 0}, 0.1f); d.restitution = 0.6f; int id = w.addBody(d);
        float apex = 0; bool hit = false; float prevVy = 0;
        for (int i = 0; i < 60 * 4; i++) { w.step(1.f / 60); float vy = w.bodies()[id].vel.y;
            if (!hit && prevVy < -1 && vy > 0) hit = true;
            if (hit) apex = std::max(apex, w.bodies()[id].pos.y - 0.1f); if (hit && vy < 0 && apex > 0) hit = hit; prevVy = vy; if (i == 60) {} }
        std::printf("[1] first-bounce apex above rest: %.2f m (drop 1.9)\n", apex);
        CHECK(apex > 0.38f && apex < 0.95f, "bounce apex");
        run(w, 6); CHECK(w.bodies()[id].sleeping && std::fabs(w.bodies()[id].pos.y - 0.1f) < 0.01f, "ball sleeps on ground y=%.3f", w.bodies()[id].pos.y);
    }
    { // 2 box rests / lands flat
        PhysicsWorld w; BodyDesc d = box({0, 1, 0}, {0.3f, 0.2f, 0.25f}); d.rot = quatEuler(0.7f, 0.4f, 0.9f); d.angVel = {2, 1, 3}; int id = w.addBody(d);
        run(w, 5); const Body& b = w.bodies()[id];
        Vec3 up = {0, 1, 0};
        float best = 0; for (int k = 0; k < 3; k++) { Vec3 ax = k == 0 ? Vec3(1, 0, 0) : k == 1 ? Vec3(0, 1, 0) : Vec3(0, 0, 1); Vec3 a(0,0,0); Quat q = b.rot; Vec3 u(q.x, q.y, q.z); Vec3 t = cross(u, ax) * 2.f; a = ax + t * q.w + cross(u, t); best = std::max(best, std::fabs(dot(a, up))); }
        float h = (b.half.x * 0 ); (void)h;
        std::printf("[2] box y=%.3f sleeping=%d alignment=%.4f\n", b.pos.y, b.sleeping, best);
        CHECK(b.sleeping, "box should sleep"); CHECK(best > 0.9994f, "box lies flat");
        float yy = b.pos.y; bool okh = std::fabs(yy - 0.2f) < 0.012f || std::fabs(yy - 0.25f) < 0.012f || std::fabs(yy - 0.3f) < 0.012f; CHECK(okh, "box rest height %.3f", yy);
    }
    { // 3 stack of 6
        PhysicsWorld w; int ids[6];
        for (int i = 0; i < 6; i++) ids[i] = w.addBody(box({0, 0.25f + i * 0.5f + 0.001f * i, 0}, {0.25f, 0.25f, 0.25f}));
        run(w, 15); float drift = 0, dh = 0; for (int i = 0; i < 6; i++) { const Body& b = w.bodies()[ids[i]]; drift = std::max(drift, std::hypot(b.pos.x, b.pos.z)); dh = std::max(dh, std::fabs(b.pos.y - (0.25f + i * 0.5f))); }
        std::printf("[3] stack6 drift=%.4f dh=%.4f\n", drift, dh);
        CHECK(drift < 0.05f, "stack drift"); CHECK(dh < 0.02f, "stack height");
    }
    { // 3b pyramid
        PhysicsWorld w; std::vector<int> ids; std::vector<Vec3> ideal;
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4 - r; c++) { Vec3 p((c - (3 - r) * 0.5f) * 0.52f, 0.25f + r * 0.5f, 0); ids.push_back(w.addBody(box(p, {0.25f, 0.25f, 0.25f}))); ideal.push_back(p); }
        run(w, 10); float dd = 0; for (size_t i = 0; i < ids.size(); i++) dd = std::max(dd, length(w.bodies()[ids[i]].pos - ideal[i]));
        std::printf("[3b] pyramid max displacement=%.3f\n", dd);
        CHECK(dd < 0.15f, "pyramid stable");
    }
    // ramp mesh helper
    auto ramp = [](float deg, std::vector<Vec3>& v, std::vector<uint32_t>& t) {
        float a = deg * 3.14159265f / 180; float L = 10, W = 4; float c = std::cos(a), s = std::sin(a);
        // surface descends toward +x: y = -x*tan(a)
        v = {{-L * c, L * s, -W}, {L * c, -L * s, -W}, {L * c, -L * s, W}, {-L * c, L * s, W}}; t = {0, 2, 1, 0, 3, 2};
    };
    { // 4 friction on ramp
        for (float mu : {0.8f, 0.1f}) {
            PhysicsWorld w; w.setGroundEnabled(false); std::vector<Vec3> v; std::vector<uint32_t> t; ramp(20, v, t); w.setStaticMesh(v, t);
            float a = 20 * 3.14159265f / 180; Quat q = quatAxisAngle({0, 0, 1}, -a);
            Vec3 n(std::sin(a), std::cos(a), 0); BodyDesc d = box(n * 0.2f, {0.2f, 0.2f, 0.2f}); d.rot = q; d.friction = mu; d.restitution = 0; int id = w.addBody(d);
            run(w, 1.0f); Vec3 p1 = w.bodies()[id].pos; run(w, 1.0f); Vec3 p2 = w.bodies()[id].pos;
            float dist = length(p2 - p1);
            if (mu > 0.5f) { std::printf("[4] mu=0.8 moved %.3f m in 1s\n", dist); CHECK(dist < 0.05f, "box should hold on ramp"); }
            else { float vel = dist; float expect = 9.81f * (std::sin(a) - mu * std::cos(a)) * 1.5f; float rel = std::fabs(vel - expect) / expect; std::printf("[4] mu=0.1 speed~%.2f m/s expected %.2f err %.0f%%\n", vel, expect, rel * 100); CHECK(rel < 0.2f, "slide accel"); }
        }
    }
    { // 5 ball rolling on terrain & box resting on grid terrain
        PhysicsWorld w; w.setGroundEnabled(false);
        std::vector<Vec3> v; std::vector<uint32_t> t; int N = 50;
        for (int z = 0; z <= N; z++) for (int x = 0; x <= N; x++) { float X = (x - N / 2) * 0.2f, Z = (z - N / 2) * 0.2f; v.push_back({X, 0.15f * std::exp(-(X * X + Z * Z) / 2) , Z}); }
        for (int z = 0; z < N; z++) for (int x = 0; x < N; x++) { uint32_t i = z * (N + 1) + x; t.insert(t.end(), {i, i + N + 1, i + 1, i + 1, i + N + 1, i + N + 2}); }
        w.setStaticMesh(v, t);
        int bx = w.addBody(box({1.5f, 0.5f, 1.2f}, {0.2f, 0.15f, 0.2f}));
        int bl = w.addBody(ball({-3, 1, -2}, 0.15f));
        run(w, 6);
        const Body& b = w.bodies()[bx];
        std::printf("[5] terrain box y=%.3f speed=%.4f sleeping=%d | ball y=%.3f\n", b.pos.y, length(b.vel), b.sleeping, w.bodies()[bl].pos.y);
        CHECK(b.pos.y > 0.1f && b.pos.y < 0.45f && (b.sleeping || length(b.vel) < 0.02f), "box on terrain");
        CHECK(w.bodies()[bl].pos.y > 0.05f && w.bodies()[bl].pos.y < 0.6f, "ball stays on terrain");
    }
    { // 6 bullets
        PhysicsWorld w; w.addStaticBox({0, 1, 5}, Quat(), {2, 1, 0.01f});
        BodyDesc d = ball({0, 1, 0}, 0.02f, 0.01f); d.vel = {0, 0, 120}; d.bullet = true; d.restitution = 0; int id = w.addBody(d);
        run(w, 1); std::printf("[6] bullet vs 2cm wall z=%.3f\n", w.bodies()[id].pos.z); CHECK(w.bodies()[id].pos.z < 5.0f, "bullet through static box");
        PhysicsWorld w2; w2.setGroundEnabled(false); std::vector<Vec3> v = {{-2, 0, 5}, {2, 0, 5}, {2, 2, 5}, {-2, 2, 5}}; w2.setStaticMesh(v, {0, 1, 2, 0, 2, 3});
        d.pos = {0, 1, 0}; int id2 = w2.addBody(d); w2.setGravity({0, 0, 0}); run(w2, 1); std::printf("[6] bullet vs quad z=%.3f\n", w2.bodies()[id2].pos.z); CHECK(w2.bodies()[id2].pos.z < 5.0f, "bullet through quad");
        PhysicsWorld w3; int tb = w3.addBody(box({0, 0.25f, 5}, {0.25f, 0.25f, 0.25f}, 2)); d.pos = {0, 0.25f, 0}; d.mass = 0.05f; w3.addBody(d); run(w3, 0.6f);
        std::printf("[6] target box z=%.3f vz=%.3f\n", w3.bodies()[tb].pos.z, w3.bodies()[tb].vel.z); CHECK(w3.bodies()[tb].pos.z > 5.01f, "momentum transfer");
    }
    { // 7 explosion + chain
        PhysicsWorld w; int ids[12]; for (int i = 0; i < 12; i++) { float a = i * 6.2831853f / 12; ids[i] = w.addBody(box({std::cos(a) * 1.0f, 0.2f, std::sin(a) * 1.0f}, {0.2f, 0.2f, 0.2f})); }
        run(w, 2); w.explosion({0, 0.1f, 0}, 4, 25); run(w, 0.5f); float rad = 0; for (int i = 0; i < 12; i++) rad += length(w.bodies()[ids[i]].pos - Vec3(0, 0.2f, 0)); rad /= 12;
        std::printf("[7] explosion mean radius %.2f\n", rad); CHECK(rad > 1.5f, "explosion pushes outward");
        PhysicsWorld c; c.setGroundY(-100); int L[10]; for (int i = 0; i < 10; i++) { BodyDesc d = ball({0.1f * (i + 1), 3, 0}, 0.04f, 0.2f); L[i] = c.addBody(d); }
        c.addBallJoint(L[0], -1, {0.05f, 3, 0}); for (int i = 1; i < 10; i++) c.addBallJoint(L[i - 1], L[i], {0.1f * i + 0.05f, 3, 0});
        run(c, 5); float maxErr = 0; for (int i = 1; i < 10; i++) maxErr = std::max(maxErr, std::fabs(length(c.bodies()[L[i]].pos - c.bodies()[L[i - 1]].pos) - 0.1f) / 0.1f);
        std::printf("[7] chain max link stretch %.1f%%\n", maxErr * 100); CHECK(maxErr < 0.08f, "chain stretch");
    }
    { // 8 raycast + events
        PhysicsWorld w; RayHit h; CHECK(w.raycast({0, 5, 0}, {0, -1, 0}, 100, h) && std::fabs(h.t - 5) < 1e-3f, "ray ground");
        int b = w.addBody(box({0, 1, 0}, {0.5f, 0.5f, 0.5f})); (void)b; CHECK(w.raycast({0, 5, 0}, {0, -1, 0}, 100, h) && std::fabs(h.t - 3.5f) < 1e-3f && h.body == 0, "ray box t=%.3f", h.t);
        PhysicsWorld w2; w2.addBody(ball({0, 3, 0}, 0.1f)); float sp = 0; for (int i = 0; i < 120; i++) { w2.step(1.f / 60); for (auto& e : w2.events()) sp = std::max(sp, e.speed); }
        std::printf("[8] impact event speed %.2f\n", sp); CHECK(sp > 5, "impact event");
    }
    { // 9 perf
        PhysicsWorld w; for (int i = 0; i < 300; i++) { float x = (i % 10) * 0.45f - 2, z = ((i / 10) % 6) * 0.45f - 1, y = 0.3f + (i / 60) * 0.7f; if (i % 3) w.addBody(box({x, y, z}, {0.2f, 0.2f, 0.2f})); else w.addBody(ball({x, y, z}, 0.2f)); }
        double tot = 0, worst = 0; for (int i = 0; i < 300; i++) { auto t0 = std::chrono::steady_clock::now(); w.step(1.f / 60); double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); tot += ms; worst = std::max(worst, ms); }
        std::printf("[9] 300 bodies: avg %.2f ms worst %.2f ms/step awake=%d\n", tot / 300, worst, w.awakeCount());
    }
    std::printf(fails ? "\n%d FAILED\n" : "\nALL OK\n", fails);
    return fails ? 1 : 0;
}
