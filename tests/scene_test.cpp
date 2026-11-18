// Тесты сцены без GL: ключи, прицепление/отцепление (в т.ч. к кости), эффекты, сохранение, отмена.
#include <cmath>
#include <cstdio>
#include <fstream>

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

    // --- выбор лучом ---
    sc.evaluate(1);
    int pk = sc.pick({10, 0.5f, 0}, {-1, 0, 0});
    printf("pick id=%d (cube=%d)\n", pk, cube);

    printf(fails ? "SCENE TESTS FAILED: %d\n" : "scene tests OK\n", fails);
    return fails ? 1 : 0;
}
