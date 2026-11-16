// Десктопный тест приложения PrismLab: EGL surfaceless + симуляция касаний, скриншоты в PPM.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "app.h"

using namespace eng;

static GLuint gFbo = 0;
static void savePPM(const std::string& path, int w, int h) {
    std::vector<uint8_t> px((size_t)w * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, gFbo);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    FILE* f = fopen(path.c_str(), "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--)
        for (int x = 0; x < w; x++) fwrite(&px[((size_t)y * w + x) * 4], 1, 3, f);
    fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s outdir assets [script]\n", argv[0]); return 2; }
    std::string out = argv[1], assets = argv[2];
    const int W = 1080, H = 2400;
    typedef EGLDisplay (*PFN)(EGLenum, void*, const EGLint*);
    auto getPD = (PFN)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = getPD(EGL_PLATFORM_SURFACELESS_MESA, nullptr, nullptr);
    EGLint a, b; eglInitialize(dpy, &a, &b); eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint ca[] = {EGL_SURFACE_TYPE, 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
    EGLConfig cfg; EGLint n;
    eglChooseConfig(dpy, ca, &cfg, 1, &n);
    const EGLint cx[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cx);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
    GLuint fbo, color, depth;
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &color); glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    glGenRenderbuffers(1, &depth); glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);

    gFbo = fbo;
    App app;
    app.plat.dataDir = out + "/data";
    std::vector<std::vector<uint8_t>> pngs;
    app.plat.savePng = [&](const std::string& name, const std::vector<uint8_t>& png) {
        std::ofstream f(out + "/" + name, std::ios::binary);
        f.write((const char*)png.data(), (std::streamsize)png.size());
        return true;
    };
    app.plat.toast = [](const std::string& s) { printf("toast: %s\n", s.c_str()); };
    app.setTargetFbo(fbo);
    if (!app.initGL(W, H, W / 400.f)) { fprintf(stderr, "initGL failed\n"); return 1; }
    double t = 0;
    auto run = [&](int frames) { for (int i = 0; i < frames; i++) { t += 1.0 / 60; app.frame(t); } };
    auto shot = [&](const char* name) { run(2); savePPM(out + "/" + name + ".ppm", W, H); GLenum e = glGetError(); if (e) fprintf(stderr, "GL error 0x%x after %s\n", e, name); };
    auto tap = [&](float x, float y) { app.touch(0, 1, x, y); run(2); app.touch(2, 1, x, y); run(3); };
    auto drag = [&](float x0, float y0, float x1, float y1, int steps = 12) {
        app.touch(0, 1, x0, y0); run(2);
        for (int i = 1; i <= steps; i++) { app.touch(1, 1, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps); run(1); }
        app.touch(2, 1, x1, y1); run(3);
    };
    float S = W / 400.f;

    int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: %s  ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)
    app.ui.record = true;
    std::string err;
    app.importModelPath(assets + "/test_skinned.glb", &err);
    if (!err.empty()) fprintf(stderr, "import: %s\n", err.c_str());
    run(3);
    Scene& sc = app.scene;
    CHECK(sc.selected > 0, "model selected after import");
    int mid = sc.selected;
    auto widget = [&](const std::string& k) { auto it = app.ui.rects.find(k); if (it == app.ui.rects.end()) { printf("no widget '%s'\n", k.c_str()); return Rect(0, 0, 1, 1); } return it->second; };
    auto tapW = [&](const std::string& k) { Rect r = widget(k); tap(r.cx(), r.cy()); };
    auto reveal = [&](const std::string& k) {
        for (int i = 0; i < 8; i++) {
            auto it = app.ui.rects.find(k);
            Rect pr = app.panelRect();
            if (it != app.ui.rects.end() && it->second.y > pr.y && it->second.b() < pr.b()) return true;
            drag(W / 2, pr.b() - 40, W / 2, pr.y + 40, 8);
            run(40);
        }
        return false;
    };
    shot("01_start");

    // --- выбор тапом: пустое место снимает выбор, модель выбирает
    tap(W * 0.9f, app.viewportRect().y + 400);
    CHECK(sc.selected < 0, "deselect by tap on empty: %d", sc.selected);
    float sx, sy;
    app.project(transformPoint(sc.find(mid)->world, {0, 1.0f, 0}), sx, sy);
    tap(sx, sy);
    CHECK(sc.selected == mid, "select by tap: %d", sc.selected);

    // --- гизмо: тянем по оси X
    {
        float ox, oy, ax, ay;
        app.project(transformPoint(sc.find(mid)->world, {0, 0, 0}), ox, oy);
        app.project(transformPoint(sc.find(mid)->world, {0.3f, 0, 0}), ax, ay);
        float dx = ax - ox, dy = ay - oy, l = std::hypot(dx, dy);
        dx /= l; dy /= l;
        float before = sc.find(mid)->t.x;
        drag(ox + dx * S * 40, oy + dy * S * 40, ox + dx * S * 120, oy + dy * S * 120);
        float after = sc.find(mid)->t.x;
        CHECK(after > before + 0.2f, "gizmo move X: %f -> %f", before, after);
        sc.undo();
        sc.evaluate(1);
    }
    // --- вращение орбитой
    {
        drag(W * 0.8f, app.viewportRect().y + 500, W * 0.5f, app.viewportRect().y + 500);
        shot("02_orbit");
    }
    // --- таймлайн
    {
        Rect tl = app.timelineRect();
        tap(tl.x + tl.w * 0.5f, tl.y + 80 * S);
        int f = app.frameNow();
        CHECK(f > 40 && f < 80, "scrub to middle: %d", f);
    }
    // --- панель Объект: ввод числа через клавиатуру
    app.setTab(Tab::Object); run(3);
    {
        Rect r = widget("X");
        tap(r.cx(), r.cy());
        CHECK(app.modalOpen() && app.modal().kind == Modal::Keypad, "keypad opened");
        shot("03_keypad");
        Rect kb = Rect(0, 0, 0, 0);
        (void)kb;
        // нажать 2 . 5 OK
        auto key = [&](const char* k) { tapW(k); };
        key("2"); key("."); key("5"); key("OK");
        CHECK(!app.modalOpen(), "keypad closed");
        CHECK(std::fabs(sc.find(mid)->t.x - 2.5f) < 1e-3f || sc.find(mid)->keys.size() > 0, "typed value applied %f", sc.find(mid)->t.x);
    }
    // --- добавить куб и прицепить его к кости
    app.openAddMenu(); run(2);
    tapW("item:Куб");
    int cube = sc.selected;
    CHECK(cube != mid && cube > 0, "cube added");
    CHECK(reveal("Прицепить к…"), "reveal attach button");
    tapW("Прицепить к…");
    CHECK(app.modalOpen(), "attach list open");
    shot("04_attach_list");
    tapW("item:test_skinned");
    CHECK(app.modalOpen(), "bone list open");
    shot("05_bone_list");
    {
        int k = 0;
        for (auto& kv : app.ui.rects) if (kv.first.rfind("item:кость", 0) == 0) { k++; }
        CHECK(k >= 1, "bones listed: %d", k);
        Rect lastBone; std::string lastName;
        for (auto& kv : app.ui.rects) if (kv.first.rfind("item:кость", 0) == 0) { lastBone = kv.second; lastName = kv.first; }
        printf("bones: last=%s\n", lastName.c_str());
        tap(lastBone.cx(), lastBone.cy());
    }
    CHECK(!app.modalOpen(), "attach done");
    CHECK(sc.isAttached(*sc.find(cube), (float)app.frameNow()), "cube attached");
    shot("06_attached");
    // двигаем кадр: куб должен следовать за костью
    app.setFrame(65); run(2);
    Mat4 p10 = sc.find(cube)->world;
    app.setFrame(100); run(2);
    Mat4 p30 = sc.find(cube)->world;
    float md = 0;
    for (int i = 0; i < 16; i++) md += std::fabs(p10.m[i] - p30.m[i]);
    CHECK(md > 0.01f, "attached cube follows bone: %f", md);
    // отцепить
    app.setTab(Tab::Object); run(3);
    CHECK(reveal("Отцепить на кадре 100"), "reveal detach");
    tapW("Отцепить на кадре 100");
    CHECK(!sc.isAttached(*sc.find(cube), 100.f), "detached");
    shot("07_detached");

    // --- огонь и вспышка
    app.openAddMenu(); run(2);
    tapW("item:Огонь");
    app.openAddMenu(); run(2);
    tapW("item:Вспышка выстрела");
    app.setFrame(40); run(5);
    app.setTab(Tab::Scene); shot("08_scene_list");
    app.setTab(Tab::Anim); shot("09_anim_panel");

    // --- текстовая клавиатура: переименование
    app.setTab(Tab::Scene); run(3);
    tapW("Имя"); run(2);
    CHECK(app.modalOpen() && app.modal().kind == Modal::Text, "text modal");
    shot("10_keyboard");
    app.setTab(Tab::None);
    // --- вид из камеры и экспорт кадра
    app.onBack(); run(2);
    app.setTab(Tab::None);
    app.setFrame(40);
    {
        float x = app.viewportRect().r() - 8 * S - 42 * S * 0.5f, y = app.viewportRect().y + 8 * S + (42 * S + 6 * S) * 1.5f;
        tap(x, y);  // кнопка камеры
    }
    shot("11_camview");
    app.startStillExport();
    for (int i = 0; i < 12 && app.exporting(); i++) run(1);
    CHECK(!app.exporting(), "still export finished: %s", app.lastExportInfo.c_str());
    printf("export: %s\n", app.lastExportInfo.c_str());
    printf(fails ? "FAILED %d\n" : "app tests OK\n", fails);
    return fails ? 1 : 0;
}
