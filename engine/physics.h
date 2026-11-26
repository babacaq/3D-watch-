// Rigid-body physics for the mobile 3D engine.
// Self-contained C++17: sequential-impulse solver, warm starting, split-impulse position
// correction, SAT box-box / box-triangle manifolds, BVH static mesh, swept (continuous)
// collision for bullets, ball joints, island sleeping, collision events.
// Units: meters, +Y up. Contact/event normals point from "B" (the other body / the static
// world) towards "A" (bodyA), i.e. away from the surface that was hit.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "math3d.h"

namespace eng {

enum class ShapeType { Sphere, Box };

struct BodyDesc {
    ShapeType shape = ShapeType::Sphere;
    float radius = 0.1f;            // sphere
    Vec3 half{0.1f, 0.1f, 0.1f};    // box half extents
    float mass = 1.f;               // <=0 => static (immovable) body
    float friction = 0.6f, restitution = 0.25f;
    Vec3 pos;
    Quat rot;
    Vec3 vel, angVel;
    float lifetime = -1.f;          // seconds until auto-removal, <0 = forever
    int tag = 0;                    // user data (e.g. 1 = bullet)
    bool bullet = false;            // enable continuous collision (swept) for this body
};

struct Body {  // read-only to callers
    ShapeType shape = ShapeType::Sphere;
    float radius = 0.1f;
    Vec3 half{0.1f, 0.1f, 0.1f};
    Vec3 pos;
    Quat rot;
    Vec3 vel, angVel;
    float invMass = 0.f;
    bool sleeping = false;
    bool alive = false;
    int tag = 0;
    float age = 0.f;
    float lifetime = -1.f;
    Mat4 transform() const;  // T*R (no scale)
};

struct ContactEvent {
    Vec3 point, normal;  // normal: from the other surface towards bodyA
    float impulse;       // total normal impulse applied that substep
    float speed;         // approach speed (m/s)
    int bodyA, bodyB;    // bodyB = -1 => static world (ground / mesh / static box)
};

struct RayHit {
    float t = 0.f;
    Vec3 point, normal;
    int body = -1;  // -1 = static world
};

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void setGroundY(float y);
    void setGroundEnabled(bool on);
    void setGravity(Vec3 g);

    int addBody(const BodyDesc& d);  // returns id (or -1 if the input is unusable)
    void removeBody(int id);
    void clearBodies();  // keeps static mesh / ground / static boxes / (removes joints)

    int addStaticBox(Vec3 pos, Quat rot, Vec3 half);
    void clearStaticBoxes();

    // World-space triangles; two-sided thin surfaces. Degenerate / out-of-range triangles are skipped.
    void setStaticMesh(const std::vector<Vec3>& verts, const std::vector<uint32_t>& tris);
    void clearStaticMesh();
    int staticTriangleCount() const;

    void step(float dt);  // fixed 1/120 s substeps, accumulator, max 4 substeps per call

    void applyImpulse(int id, Vec3 impulse, Vec3 worldPoint);
    void explosion(Vec3 center, float radius, float impulse);

    // coneLimit > 0: предельный угол (рад) поворота тела A относительно B от исходного взаимного положения
    int addBallJoint(int bodyA, int bodyB /* -1 = world anchor */, Vec3 worldAnchor, float coneLimit = -1.f);
    void clearJoints();
    void ignorePair(int bodyA, int bodyB);  // эта пара тел не сталкивается (соседние кости рэгдолла)

    bool raycast(Vec3 origin, Vec3 dir, float maxT, RayHit& hit) const;

    // extras
    void wakeAll();
    void wakeBody(int id);

    const std::vector<Body>& bodies() const;
    const std::vector<ContactEvent>& events() const;
    int aliveCount() const;
    int awakeCount() const;
    int contactCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eng
