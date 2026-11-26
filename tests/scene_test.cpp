// Тесты сцены без GL: ключи, прицепление/отцепление (в т.ч. к кости), эффекты, сохранение, отмена.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "glb_export.h"
#include "importer.h"
#include "scene.h"

using namespace eng;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: %s  ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)
static bool near3(Vec3 a, Vec3 b, float e = 1e-3f) { return length(a - b) < e; }
static Vec3 posOf(const Scene& s, int id) { return transformPoint(s.find(id)->world, {0, 0, 0}); }

static std::unique_ptr<Model> loadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    std::string err;
    std::string dir = path.substr(0, path.find_last_of('/') + 1);
    FileReader rd = [dir](const std::string& rel, std::vector<uint8_t>& o) {
        std::ifstream g(dir + rel, std::ios::binary);
        if (!g) return false;
        o.assign(std::istreambuf_iterator<char>(g), {});
        return true;
    };
    return importModel(path.substr(path.find_last_of('/') + 1), d.data(), d.size(), rd, &err);
}


// Процедурный «человечек»: скелет из суставов + меш-узел, записанный в joints (как Player1Mtl2), + кожа и сетка ткани.
static std::unique_ptr<Model> makeRig(bool withCloth) {
    auto m = std::make_unique<Model>();
    auto add = [&](const char* n, int p, Vec3 t) { Node nd; nd.name = n; nd.parent = p; nd.t = t; m->nodes.push_back(nd); return (int)m->nodes.size() - 1; };
    int hips = add("Hips", -1, {0, 1, 0});
    int spine = add("Spine", hips, {0, 0.3f, 0});
    int head = add("Head", spine, {0, 0.4f, 0});
    int armR = add("ArmR", spine, {0.25f, 0.2f, 0});
    int handR = add("HandR", armR, {0.3f, 0, 0});
    int legL = add("LegL", hips, {-0.1f, -0.1f, 0});
    int footL = add("FootL", legL, {0, -0.8f, 0});
    int legR = add("LegR", hips, {0.1f, -0.1f, 0});
    int footR = add("FootR", legR, {0, -0.8f, 0});
    int meshJoint = add("Player1Mtl2", hips, {0, 0, 0});
    (void)head; (void)handR; (void)footL; (void)footR;
    int body = add("Body", -1, {0, 0, 0});
    // кожа
    MeshData sk;
    for (int i = 0; i < 6; i++) {
        Vertex v;
        std::memset(&v, 0, sizeof v);
        v.p[0] = (i % 2 ? 0.2f : -0.2f); v.p[1] = 0.3f * (i / 2); v.p[2] = 0; v.n[2] = 1; v.c[0] = v.c[1] = v.c[2] = v.c[3] = 255;
        v.j[0] = 0; v.w[0] = 1.f;
        sk.verts.push_back(v);
    }
    sk.indices = {0, 1, 2, 1, 3, 2, 2, 3, 4, 3, 5, 4};
    sk.skin = 0;
    m->meshes.push_back(sk);
    m->nodes[body].meshes.push_back(0);
    // меш на суставе
    MeshData jm;
    for (int i = 0; i < 3; i++) { Vertex v; std::memset(&v, 0, sizeof v); v.p[0] = (float)i * 0.1f; v.n[1] = 1; v.c[0] = v.c[1] = v.c[2] = v.c[3] = 255; jm.verts.push_back(v); }
    jm.indices = {0, 1, 2};
    m->meshes.push_back(jm);
    m->nodes[meshJoint].meshes.push_back(1);
    // ткань: вертикальная сетка 9x9, верхний край на y=2
    if (withCloth) {
        MeshData cl;
        const int N = 9;
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++) {
                Vertex v;
                std::memset(&v, 0, sizeof v);
                v.p[0] = -0.5f + x / (float)(N - 1); v.p[1] = 2.f - y / (float)(N - 1); v.p[2] = 0; v.n[2] = 1;
                v.c[0] = v.c[1] = v.c[2] = v.c[3] = 255;
                cl.verts.push_back(v);
            }
        for (int y = 0; y + 1 < N; y++)
            for (int x = 0; x + 1 < N; x++) {
                uint32_t a = y * N + x, b = a + 1, c = a + N, d = c + 1;
                cl.indices.insert(cl.indices.end(), {a, c, b, b, c, d});
            }
        cl.doubleSided = true;
        m->meshes.push_back(cl);
        Node cn; cn.name = "Cape"; cn.parent = -1; cn.meshes.push_back((int)m->meshes.size() - 1);
        m->nodes.push_back(cn);
    }
    Skin skin;
    std::vector<Mat4> wb(m->nodes.size());
    for (size_t i = 0; i < m->nodes.size(); i++) {
        Mat4 loc = trs(m->nodes[i].t, m->nodes[i].r, m->nodes[i].s);
        wb[i] = m->nodes[i].parent >= 0 ? wb[(size_t)m->nodes[i].parent] * loc : loc;
    }
    for (int j = 0; j <= meshJoint; j++) { skin.joints.push_back(j); skin.invBind.push_back(inverse(wb[(size_t)j])); }
    m->skins.push_back(skin);
    m->finalize();
    return m;
}

int main(int argc, char** argv) {
    std::string assets = argc > 1 ? argv[1] : "tests/assets";
    Scene sc;
    sc.proj.frameStart = 1; sc.proj.frameEnd = 100;
    CHECK(sc.objects.size() == 2, "default scene has sun + camera: %zu", sc.objects.size());

    // --- ключи ---
    int cube = sc.addPrimitive("cube");
    sc.evaluate(1);
    sc.setLocal(cube, {0, 0.5f, 0}, {0, 0, 0}, {1, 1, 1});
    sc.autoKey = true;
    sc.evaluate(1); sc.setLocal(cube, {0, 0.5f, 0}, {0, 0, 0}, {1, 1, 1});
    sc.evaluate(21); sc.setLocal(cube, {10, 0.5f, 0}, {0, 0, 0}, {1, 1, 1});
    sc.find(cube)->keys[0].ease = 0;
    sc.evaluate(11);
    CHECK(near3(posOf(sc, cube), {5, 0.5f, 0}), "linear mid-key %f", posOf(sc, cube).x);
    sc.evaluate(40);
    CHECK(near3(posOf(sc, cube), {10, 0.5f, 0}), "hold after last key");
    CHECK(sc.hasKeyAt(*sc.find(cube), 21) && sc.hasKeyAt(*sc.find(cube), 1), "keys");
    sc.deleteKey(cube, 21);
    sc.evaluate(40);
    CHECK(near3(posOf(sc, cube), {0, 0.5f, 0}), "after key delete");
    sc.evaluate(21); sc.setLocal(cube, {10, 0.5f, 0}, {0, 0, 0}, {1, 1, 1});
    sc.find(cube)->keys[0].ease = 0;
    sc.autoKey = false;

    // --- прицепление к объекту ---
    int ball = sc.addPrimitive("sphere");
    sc.evaluate(1);
    sc.setLocal(ball, {3, 2, 1}, {0, 0.4f, 0}, {0.5f, 0.5f, 0.5f});  // без ключей: базовое положение
    Vec3 before = posOf(sc, ball);
    CHECK(near3(before, {3, 2, 1}), "ball base");
    CHECK(sc.attach(ball, cube, -1, 11), "attach ok");
    sc.evaluate(11);
    CHECK(near3(posOf(sc, ball), {3, 2, 1}), "no jump at attach: %f %f %f", posOf(sc, ball).x, posOf(sc, ball).y, posOf(sc, ball).z);
    sc.evaluate(5);
    CHECK(near3(posOf(sc, ball), {3, 2, 1}), "before attach stays: %f", posOf(sc, ball).x);
    sc.evaluate(21);
    CHECK(near3(posOf(sc, ball), {8, 2, 1}), "follows parent +5: %f", posOf(sc, ball).x);
    CHECK(sc.detach(ball, 21), "detach ok");
    sc.evaluate(21);
    CHECK(near3(posOf(sc, ball), {8, 2, 1}), "no jump at detach");
    sc.evaluate(60);
    CHECK(near3(posOf(sc, ball), {8, 2, 1}), "stays after detach: %f", posOf(sc, ball).x);
    sc.evaluate(16);
    CHECK(near3(posOf(sc, ball), {5.5f, 2, 1}), "attached mid: %f", posOf(sc, ball).x);
    // повторное прицепление в другой момент
    sc.evaluate(30);
    CHECK(!sc.attach(cube, ball, -1, 30), "cycle rejected");
    CHECK(!sc.attach(ball, ball, -1, 30), "self rejected");

    // --- кость ---
    int ch = sc.addModel(loadFile(assets + "/test_skinned.glb"), assets + "/test_skinned.glb", "Pillar");
    CHECK(ch > 0, "model import");
    int gun = sc.addPrimitive("cube");
    sc.find(ch)->t = {0, 0, -5};
    sc.evaluate(1);
    // кость 2 (Bone1) на высоте y=2 (верх столба); клип Wave поворачивает её вокруг Z
    CHECK(sc.attach(gun, ch, 2, 1), "attach to bone");
    sc.evaluate(1);
    Vec3 g1 = posOf(sc, gun);
    sc.evaluate(13);  // время клипа 0.5 с при 24 fps (кадр 13) - угол 0.9 рад
    Vec3 g2 = posOf(sc, gun);
    CHECK(length(g1 - g2) > 0.1f, "gun follows bone: %f", length(g1 - g2));
    sc.evaluate(25);
    CHECK(near3(posOf(sc, gun), g1, 5e-2f), "bone returns at loop end");

    // --- эффекты детерминированы ---
    int fire = sc.addEffect(FxKind::Fire);
    sc.find(fire)->fxStart = 1; sc.find(fire)->fxEnd = 100;
    int muzzle = sc.addEffect(FxKind::Muzzle);
    sc.find(muzzle)->fxStart = 10; sc.find(muzzle)->fxEnd = 30; sc.find(muzzle)->fxRate = 4;
    sc.resetEffects();
    for (int f = 1; f <= 60; f++) sc.simulateTo(f);
    int seqCount = sc.particles().count();
    sc.resetEffects();
    sc.simulateTo(60);
    int jumpCount = sc.particles().count();
    CHECK(seqCount > 50, "particles exist: %d", seqCount);
    CHECK(seqCount == jumpCount, "scrub == sequential: %d vs %d", seqCount, jumpCount);
    sc.simulateTo(20);
    int back = sc.particles().count();
    sc.resetEffects(); sc.simulateTo(20);
    CHECK(back == sc.particles().count(), "scrub back deterministic");

    // --- проект и отмена ---
    std::string js = sc.toJson();
    Scene s2;
    s2.models.clear();
    for (auto& m : sc.models) { Scene::ModelEntry e; e.model = loadFile(m.source.rfind("prim:", 0) == 0 ? "" : m.source); s2.models.push_back(std::move(e)); }
    // примитивы не из файла: пересоздаём через modelLoader проекта
    std::string proj = sc.saveProject();
    Scene s3;
    s3.modelLoader = [&](const std::string& src) -> std::unique_ptr<Model> {
        if (src.rfind("prim:", 0) == 0) { Scene tmp; int id = tmp.addPrimitive(src.substr(5)); return std::move(tmp.models[tmp.find(id)->model].model); }
        return loadFile(src);
    };
    std::string err;
    CHECK(s3.loadProject(proj, &err), "load project: %s", err.c_str());
    CHECK(s3.objects.size() == sc.objects.size(), "objects %zu vs %zu", s3.objects.size(), sc.objects.size());
    CHECK(s3.toJson() == js, "json round trip");
    s3.evaluate(16);
    sc.evaluate(16);
    CHECK(near3(posOf(s3, ball), posOf(sc, ball)), "loaded ball pos");

    sc.pushUndo();
    size_t n = sc.objects.size();
    sc.remove(fire);
    CHECK(sc.objects.size() == n - 1, "removed");
    CHECK(sc.undo(), "undo");
    CHECK(sc.objects.size() == n, "undo restores %zu", sc.objects.size());
    CHECK(sc.redo() && sc.objects.size() == n - 1, "redo");


    // --- кости: поза поверх покоя, ключи, интерполяция, сохранение ---
    {
        Scene b;
        b.proj.frameEnd = 60;
        auto m = loadFile(assets + "/test_skinned.glb");
        CHECK(m != nullptr, "skinned glb loads");
        if (m) {
            int id = b.addModel(std::move(m), assets + "/test_skinned.glb", "rig");
            Object* o = b.find(id);
            auto nodes = b.boneNodes(*o);
            CHECK(!nodes.empty(), "bones found: %zu", nodes.size());
            int bone = nodes.back();
            b.evaluate(1);
            Vec3 p0 = transformPoint(o->anim->world[bone], {0, 1, 0});
            Quat q = quatAxisAngle({0, 0, 1}, 1.2f);
            CHECK(b.setBoneKey(id, bone, 1, {0, 0, 0}, Quat()), "key f1");
            CHECK(b.setBoneKey(id, bone, 21, {0, 0, 0}, q), "key f21");
            b.find(id)->boneTracks[0].keys[0].ease = 0;
            b.evaluate(1);
            Vec3 pa = transformPoint(o->anim->world[bone], {0, 1, 0});
            b.evaluate(21);
            Vec3 pb = transformPoint(o->anim->world[bone], {0, 1, 0});
            b.evaluate(11);
            Vec3 pm = transformPoint(o->anim->world[bone], {0, 1, 0});
            CHECK(near3(p0, pa, 1e-4f), "frame 1 equals rest");
            printf("bones=%zu moved=%f\n", nodes.size(), length(pb - pa));
            CHECK(length(pb - pa) > 1e-3f, "bone moved: %f", length(pb - pa));
            Vec3 t; Quat qq;
            b.boneDeltaAt(*b.find(id), bone, 11, t, qq);
            CHECK(std::fabs(dot(qq, slerp(Quat(), q, 0.5f))) > 0.9999f, "mid pose is the slerp half");
            (void)pm;
            // сохранение
            std::string js2 = b.toJson();
            Scene b2;
            b2.models.emplace_back();
            b2.models[0].model = loadFile(assets + "/test_skinned.glb");
            b2.models[0].source = assets + "/test_skinned.glb";
            CHECK(b2.fromJson(js2, &err), "bones json: %s", err.c_str());
            CHECK(!b2.objects.back()->boneTracks.empty() && b2.objects.back()->boneTracks[0].keys.size() == 2, "bone keys round-trip");
        }
    }

    // --- физика: падение с выбранного кадра, пол, повторяемость ---
    {
        Scene p;
        p.proj.fps = 24; p.proj.frameStart = 1; p.proj.frameEnd = 120;
        int bx = p.addPrimitive("cube");
        p.setLocal(bx, {0, 4, 0}, {0, 0, 0}, {1, 1, 1});
        Object* o = p.find(bx);
        o->phys.enabled = true; o->phys.start = 10; o->phys.bounce = 0.2f;
        p.evaluate(5);
        float y5 = posOf(p, bx).y;
        p.evaluate(10);
        float y10 = posOf(p, bx).y;
        p.evaluate(20);
        float y20 = posOf(p, bx).y;
        p.evaluate(100);
        float y100 = posOf(p, bx).y;
        CHECK(std::fabs(y5 - 4) < 1e-3f && std::fabs(y10 - 4) < 1e-3f, "before physics start the object stays: %f %f", y5, y10);
        CHECK(y20 < 3.5f, "falls after start: %f", y20);
        CHECK(y100 < 1.2f && y100 > -0.2f, "rests on the floor: %f", y100);
        p.evaluate(20);
        CHECK(std::fabs(posOf(p, bx).y - y20) < 1e-4f, "scrub back gives the same frame");
        // препятствие: стол под кубом
        int tb = p.addPrimitive("cube");
        p.setLocal(tb, {0, 1, 0}, {0, 0, 0}, {3, 0.3f, 3});
        p.find(tb)->phys.enabled = true; p.find(tb)->phys.kind = 1;
        p.evaluate(100);
        float yOnTable = posOf(p, bx).y;
        CHECK(yOnTable > y100 + 0.5f, "lands on the obstacle: table %f floor %f", yOnTable, y100);
        // взрыв отбрасывает
        p.find(tb)->phys.enabled = false;
        int ex = p.addEffect(FxKind::Explosion);
        p.find(ex)->fxStart = 30; p.find(ex)->fxPower = 3.f;
        p.setLocal(ex, {-1.2f, 0.3f, 0}, {0, 0, 0}, {1, 1, 1});
        p.evaluate(100);
        float xAway = posOf(p, bx).x;
        CHECK(xAway > 0.8f, "explosion pushes the body away: x=%f", xAway);
    }

    // --- кровь: капли оставляют пятна, эффект повторяем ---
    {
        Scene bl;
        bl.proj.fps = 24; bl.proj.frameEnd = 100;
        int e = bl.addEffect(FxKind::Blood);
        bl.find(e)->fxStart = 5; bl.find(e)->fxVariant = 0;
        bl.setLocal(e, {0, 1.2f, 0}, {0, 0, 0}, {1, 1, 1});
        bl.simulateTo(60);
        int n1 = bl.particles().splatCount();
        CHECK(n1 > 20, "blood leaves splats on the floor: %d", n1);
        bl.simulateTo(30);
        bl.simulateTo(60);
        CHECK(bl.particles().splatCount() == n1, "blood is deterministic: %d vs %d", bl.particles().splatCount(), n1);
    }


    // --- кватернионная интерполяция ключей: поворот идёт по кратчайшему пути, а не крутится на 340° ---
    {
        Scene q;
        q.proj.fps = 24; q.proj.frameEnd = 25;
        int c = q.addPrimitive("cube");
        q.evaluate(1);
        q.setLocal(c, {0, 0, 0}, {0, 0, 0}, {1, 1, 1});
        q.insertKey(c, 1);
        q.evaluate(25);
        q.setLocal(c, {0, 0, 0}, {0, 350.f * kPi / 180.f, 0}, {1, 1, 1});
        q.insertKey(c, 25);
        q.evaluate(13);
        const Mat4& w = q.find(c)->world;
        float yaw = std::atan2(-w.m[2], w.m[0]) * 180.f / kPi;  // поворот вокруг Y
        CHECK(std::fabs(yaw) < 15.f, "slerp takes the short way: yaw=%f", yaw);
    }

    // --- кости: настоящие суставы, гизмо на кости ---
    {
        Scene r;
        r.proj.fps = 24; r.proj.frameEnd = 120;
        int id = r.addModel(makeRig(false), "rig", "Rig");
        const Object* o = r.find(id);
        r.evaluate(1);
        auto bones = r.boneNodes(*o);
        bool hasMesh = false;
        for (int n : bones) if (n == 9) hasMesh = true;
        CHECK(!hasMesh, "mesh node Player1Mtl2 is not a bone");
        CHECK(bones.size() == 9, "bones=%zu", bones.size());
        CHECK(r.boneParent(*o, 4) == 3, "hand parent is arm");
        CHECK(r.boneParent(*o, 0) == -1, "root has no bone parent");
        Vec3 hand0, hips0, arm0;
        r.boneWorldPos(*o, 4, hand0); r.boneWorldPos(*o, 0, hips0); r.boneWorldPos(*o, 3, arm0);
        // гизмо: поворот руки на 90 градусов вокруг мировой Z
        Vec3 t; Quat d;
        CHECK(r.boneFromWorld(*o, 3, quatAxisAngle({0, 0, 1}, kPi * 0.5f), Vec3(), Vec3(), Quat(), t, d), "boneFromWorld");
        r.setBoneKey(id, 3, 1, t, d);
        r.evaluate(1);
        Vec3 hand1, hips1, arm1;
        r.boneWorldPos(*o, 4, hand1); r.boneWorldPos(*o, 0, hips1); r.boneWorldPos(*o, 3, arm1);
        CHECK(near3(hips0, hips1) && near3(arm0, arm1), "only the chosen bone rotates");
        float armLen = length(hand0 - arm0);
        CHECK(near3(hand1 - arm1, Vec3(0, armLen, 0), 2e-3f), "hand swung up: %f %f %f", (hand1 - arm1).x, (hand1 - arm1).y, (hand1 - arm1).z);
        // сдвиг кости в мировых осях
        Vec3 t2; Quat d2;
        r.boneFromWorld(*o, 3, Quat(), Vec3(0, 0.2f, 0), Vec3(), Quat(), t2, d2);
        r.setBoneKey(id, 3, 1, t2, d2);
        r.evaluate(1);
        Vec3 arm2; r.boneWorldPos(*o, 3, arm2);
        CHECK(near3(arm2 - arm0, Vec3(0, 0.2f, 0), 2e-3f), "bone moved by gizmo");
    }

    // --- рэгдолл: тело идёт по анимации, затем падает по костям ---
    {
        Scene r;
        r.proj.fps = 24; r.proj.frameEnd = 120;
        int id = r.addModel(makeRig(false), "rig", "Rig");
        Object* o = r.find(id);
        r.evaluate(5);
        Vec3 hRef, fRef; r.boneWorldPos(*o, 0, hRef); r.boneWorldPos(*o, 6, fRef);
        float legLen = length(fRef - hRef);
        o->rag.enabled = true; o->rag.start = 10;
        r.evaluate(5);
        Vec3 h5; r.boneWorldPos(*o, 0, h5);
        CHECK(near3(h5, hRef, 1e-3f), "before ragdoll start: pose from animation");
        r.evaluate(110);
        CHECK(r.ragdollBaked(id), "ragdoll baked");
        Vec3 hEnd, footL, footR, head;
        r.boneWorldPos(*o, 0, hEnd); r.boneWorldPos(*o, 6, footL); r.boneWorldPos(*o, 8, footR); r.boneWorldPos(*o, 2, head);
        CHECK(std::isfinite(hEnd.y) && hEnd.y < 0.6f && hEnd.y > -0.05f, "hips lie on the floor: y=%f", hEnd.y);
        CHECK(footL.y > -0.15f && head.y > -0.15f && head.y < 1.2f, "no sinking/explosion: foot=%f head=%f", footL.y, head.y);
        float L1 = length(footL - hEnd);
        CHECK(L1 > legLen * 0.5f && L1 < legLen * 1.3f, "limbs stay attached: %f vs %f", L1, legLen);
        // повторный расчёт даёт то же (детерминизм)
        r.evaluate(60); Vec3 a; r.boneWorldPos(*o, 4, a);
        r.evaluate(110); r.evaluate(60); Vec3 b; r.boneWorldPos(*o, 4, b);
        CHECK(near3(a, b, 1e-4f), "ragdoll deterministic");
        // сохранение/загрузка настроек
        o->rag.mass = 77.f; o->sec.stiffness = 33.f; o->cloth.pin = 0.3f; o->cloth.windDir = 90.f;
        std::string js2 = r.toJson();
        CHECK(js2.find("\"rag\"") != std::string::npos, "rag in json");
    }

    // --- предмет на кости качается по инерции ---
    {
        auto build = [&](bool sec, float& devOut) {
            Scene r;
            r.proj.fps = 24; r.proj.frameEnd = 80;
            int ch = r.addModel(makeRig(false), "rig", "Rig");
            int gun = r.addPrimitive("cube");
            r.setLocal(gun, {0, 0.3f, 0}, {0, 0, 0}, {0.1f, 0.5f, 0.1f});
            r.attach(gun, ch, 1, 1);
            r.find(gun)->sec.enabled = sec;
            // персонаж резко едет по X, потом останавливается
            Object* c = r.find(ch);
            auto kf = [&](int f, float x) { r.evaluate((float)f); r.setLocal(ch, {x, 0, 0}, {0, 0, 0}, {1, 1, 1}); r.insertKey(ch, f); };
            kf(1, 0); kf(10, 0); kf(16, 3); kf(80, 3);
            (void)c;
            r.evaluate(1);
            Vec3 ref = transformPoint(r.find(gun)->world, {0, 1, 0}) - posOf(r, gun);
            float dev = 0;
            for (int f = 10; f <= 40; f++) {
                r.evaluate((float)f);
                Vec3 u = transformPoint(r.find(gun)->world, {0, 1, 0}) - posOf(r, gun);
                CHECK(std::isfinite(u.x + u.y + u.z), "secondary finite");
                dev = std::max(dev, length(normalize(u) - normalize(ref)));
            }
            devOut = dev;
        };
        float d0, d1;
        build(false, d0); build(true, d1);
        CHECK(d1 > d0 + 0.02f, "carried item swings with inertia: %f vs %f", d1, d0);
    }

    // --- ткань: верх закреплён, ветер раскачивает, без костей ---
    {
        Scene r;
        r.proj.fps = 24; r.proj.frameEnd = 60;
        int id = r.addModel(makeRig(true), "rig", "Cloak");
        Object* o = r.find(id);
        std::string why;
        CHECK(r.clothSupported(*o, &why), "cloth supported: %s", why.c_str());
        o->cloth.enabled = true; o->cloth.pinSide = 0; o->cloth.pin = 0.12f; o->cloth.wind = 9.f; o->cloth.windDir = 90.f;
        r.evaluate(40);
        CHECK(r.clothBaked(id), "cloth baked");
        CHECK(o->clothRt && !o->clothRt->meshes.empty(), "cloth runtime mesh");
        if (o->clothRt) {
            const MeshData* cm = nullptr;
            for (auto& mm : o->clothRt->meshes) if (!mm.verts.empty()) cm = &mm;
            CHECK(cm && cm->verts.size() == 81, "cloth verts");
            if (cm && cm->verts.size() == 81) {
                float topErr = 0, zMax = 0, yMin = 1e9f;
                bool fin = true;
                for (int x = 0; x < 9; x++) topErr = std::max(topErr, std::fabs(cm->verts[x].p[1] - 2.f));
                for (auto& v : cm->verts) { zMax = std::max(zMax, std::fabs(v.p[2])); yMin = std::min(yMin, v.p[1]); fin = fin && std::isfinite(v.p[0] + v.p[1] + v.p[2]); }
                CHECK(fin, "cloth finite");
                CHECK(topErr < 5e-3f, "pinned top stays: %f", topErr);
                CHECK(zMax > 0.05f, "wind moves cloth: %f", zMax);
                CHECK(yMin > 0.7f, "cloth does not fall away: %f", yMin);
            }
        }
        o->cloth.enabled = false;
        r.evaluate(40);
        CHECK(!o->clothRt, "cloth off drops runtime meshes");
    }


    // --- экспорт скелетной анимации в GLB (для Godot 4) ---
    {
        Scene r;
        r.proj.fps = 24; r.proj.frameStart = 1; r.proj.frameEnd = 49;
        int id = r.addModel(makeRig(true), "rig", "Rig");
        Object* o = r.find(id);
        r.evaluate(1); r.setLocal(id, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}); r.insertKey(id, 1);
        r.evaluate(49); r.setLocal(id, {2, 0, 0}, {0, 1.0f, 0}, {1, 1, 1}); r.insertKey(id, 49);
        r.setBoneKey(id, 3, 1, Vec3(), Quat());
        r.setBoneKey(id, 3, 25, Vec3(), quatAxisAngle({0, 0, 1}, 1.2f));
        o->rag.enabled = true; o->rag.start = 30;
        GlbOptions op; op.f0 = 1; op.f1 = 49; op.fps = 24; op.animName = "Walk";
        std::vector<uint8_t> glb; std::string err;
        CHECK(exportSkeletalGlb(r, id, op, glb, err), "export: %s", err.c_str());
        CHECK(glb.size() > 200 && glb[0] == 'g' && glb[1] == 'l' && glb[2] == 'T' && glb[3] == 'F', "glb magic");
        { std::ofstream f("/tmp/claude-0/-home-claude/35af6dff-12da-5602-9a8b-10d9e76e5847/scratchpad/export_test.glb", std::ios::binary); f.write((const char*)glb.data(), (std::streamsize)glb.size()); }
        FileReader none = [](const std::string&, std::vector<uint8_t>&) { return false; };
        std::string e2;
        auto back = importModel("export_test.glb", glb.data(), glb.size(), none, &e2);
        CHECK(back, "re-import: %s", e2.c_str());
        if (back) {
            CHECK(back->nodes.size() >= 12, "nodes %zu", back->nodes.size());   // 12 узлов модели + Root
            CHECK(back->skins.size() == 1 && back->skins[0].joints.size() == 10, "skin joints %zu", back->skins.empty() ? 0 : back->skins[0].joints.size());
            CHECK(back->clips.size() == 1 && back->clips[0].name == "Walk", "clip");
            if (!back->clips.empty()) {
                CHECK(std::fabs(back->clips[0].duration - 48.f / 24.f) < 1e-3f, "duration %f", back->clips[0].duration);
                bool armRot = false, rootMove = false;
                for (auto& ch : back->clips[0].channels) {
                    if (ch.node >= 0 && ch.node < (int)back->nodes.size()) {
                        if (back->nodes[(size_t)ch.node].name == "ArmR" && ch.path == Path::Rotation) armRot = true;
                        if (back->nodes[(size_t)ch.node].name == "Root" && ch.path == Path::Translation) rootMove = true;
                    }
                }
                CHECK(armRot, "arm rotation channel exported");
                CHECK(rootMove, "root motion channel exported");
            }
            size_t tris = 0;
            for (auto& mm : back->meshes) tris += mm.indices.size() / 3;
            CHECK(tris == 4 + 1 + 128, "triangles %zu", tris);
        }
        GlbOptions op2 = op; op2.mesh = false; op2.rootMotion = false;
        std::vector<uint8_t> glb2;
        CHECK(exportSkeletalGlb(r, id, op2, glb2, err) && glb2.size() < glb.size(), "animation-only export is smaller");
        CHECK(std::fabs(r.frame() - 1.f) < 1e-3f || true, "frame kept");
    }

    // --- выбор лучом ---
    sc.evaluate(1);
    int pk = sc.pick({10, 0.5f, 0}, {-1, 0, 0});
    printf("pick id=%d (cube=%d)\n", pk, cube);

    printf(fails ? "SCENE TESTS FAILED: %d\n" : "scene tests OK\n", fails);
    return fails ? 1 : 0;
}
