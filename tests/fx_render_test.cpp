// Тест рендерера на ПК: EGL surfaceless (Mesa) -> PPM. Не часть APK.
// Использование: render_test <outdir> <assets>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include <cmath>
#include "importer.h"
#define private public
#include "renderer.h"
#undef private

using namespace eng;

static std::unique_ptr<Model> loadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    std::string err, dir = path.substr(0, path.find_last_of('/') + 1);
    FileReader rd = [dir](const std::string& rel, std::vector<uint8_t>& o) {
        std::ifstream g(dir + rel, std::ios::binary);
        if (!g) return false;
        o.assign(std::istreambuf_iterator<char>(g), {});
        return true;
    };
    auto m = importModel(path.substr(path.find_last_of('/') + 1), d.data(), d.size(), rd, &err);
    if (m) m->upload();
    return m;
}
static void savePPM(const std::string& path, const std::vector<uint8_t>& px, int w, int h, bool flip) {
    FILE* f = fopen(path.c_str(), "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++) {
        int yy = flip ? h - 1 - y : y;
        for (int x = 0; x < w; x++) fwrite(&px[((size_t)yy * w + x) * 4], 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::string out = argv[1], assets = argv[2];
    const int W = 540, H = 960;
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
    Renderer rd;
    if (!rd.init()) { fprintf(stderr, "renderer init failed\n"); return 1; }

    Scene sc;
    auto prim = [&](const char* k, Vec3 t, Vec3 s, float r, float g, float bl, float met, float rough) {
        int id = sc.addPrimitive(k);
        for (auto& m : sc.models) if (m.model && !m.model->uploaded) m.model->upload();
        Object* o = sc.find(id);
        o->t = t; o->s = s; o->matOverride = true; o->color[0] = r; o->color[1] = g; o->color[2] = bl; o->metallic = met; o->roughness = rough;
        return id;
    };
    prim("cube", {-1.2f, 0.5f, 0}, {1, 1, 1}, 0.8f, 0.2f, 0.15f, 0.f, 0.5f);
    prim("sphere", {1.0f, 0.5f, -0.5f}, {1, 1, 1}, 0.9f, 0.8f, 0.3f, 1.f, 0.25f);
    int gl = sc.addGlass();
    for (auto& m : sc.models) if (m.model && !m.model->uploaded) m.model->upload();
    sc.find(gl)->t = {0.f, 1.0f, 1.2f};
    int sp = sc.addLight(LightKind::Spot);
    sc.find(sp)->t = {2.5f, 1.8f, 3.5f};
    sc.find(sp)->r = Scene::eulerFromMat(inverse(lookAt({2.5f, 1.8f, 3.5f}, {0, 0.6f, 0}, {0, 1, 0})));
    int br = sc.addEffect(FxKind::Glass);
    sc.find(br)->fxStart = 20; sc.find(br)->fxEnd = 120; sc.find(br)->fxPower = 1.f;
    sc.find(br)->t = {0.15f, 1.2f, 1.8f}; sc.find(br)->r = {0, 3.14159265f, 0};
    Object* cam = sc.find(sc.activeCamera);
    cam->t = {3.2f, 2.0f, 5.6f};
    Mat4 la = lookAt(cam->t, {0, 0.7f, 0.5f}, {0, 1, 0});
    cam->r = Scene::eulerFromMat(inverse(la));
    View v = Renderer::cameraView(*cam, (float)W / H);

    struct Case { const char* name; float tod; bool on; int frame; bool spot; };
    Case cases[] = {{"day_glass", 12.f, false, 10, false}, {"day_broken", 12.f, false, 22, false}, {"day_broken_late", 12.f, false, 60, false},
                    {"sunset", 17.7f, true, 10, false}, {"night_spot", 23.5f, true, 10, true}, {"night_broken", 23.5f, true, 40, true}, {"morning", 7.f, true, 10, false}, {"night_nospot", 23.5f, true, 10, false}};
    for (auto& c : cases) {
        sc.render.todOn = c.on; sc.render.tod = c.tod;
        sc.find(sp)->visible = c.spot;
        if (getenv("NOBEAM")) sc.find(sp)->beam = 0.f;
        if (getenv("NOBLOOM")) sc.render.bloom = false;
        if (getenv("NOAO")) sc.render.ao = false;
        if (getenv("NOSH")) sc.render.shadows = false;
        if (getenv("SPX")) { float x = (float)atof(getenv("SPX")); sc.find(sp)->t = {x, 1.8f, 3.5f}; sc.find(sp)->r = Scene::eulerFromMat(inverse(lookAt({x, 1.8f, 3.5f}, {0, 0.6f, 0}, {0, 1, 0}))); }
        sc.resetEffects();
        sc.simulateTo(c.frame);
        if (getenv("NANSCAN") && std::string(c.name) == "night_spot") {
            GLuint fbo2, col2;
            glGenFramebuffers(1, &fbo2); glBindFramebuffer(GL_FRAMEBUFFER, fbo2);
            glGenTextures(1, &col2); glBindTexture(GL_TEXTURE_2D, col2);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, col2, 0);
            Overlay ov;
            rd.renderView(sc, v, fbo2, RectI{0, 0, W, H}, ov, 1.f);
            glBindFramebuffer(GL_FRAMEBUFFER, rd.rt_.hdr.fbo);
            int hw = rd.rt_.hdr.w, hh = rd.rt_.hdr.h;
            std::vector<float> f((size_t)hw * hh * 4);
            glReadPixels(0, 0, hw, hh, GL_RGBA, GL_FLOAT, f.data());
            int cnt = 0, x0 = 1 << 30, x1 = -1, y0 = 1 << 30, y1 = -1;
            for (int y = 0; y < hh; y++) for (int x = 0; x < hw; x++) { float* q = &f[((size_t)y * hw + x) * 4]; if (std::isnan(q[0]) || std::isnan(q[1]) || std::isnan(q[2]) || std::isinf(q[0])) { cnt++; x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); } }
            printf("NaN px=%d box x[%d,%d] y[%d,%d] of %dx%d\n", cnt, x0, x1, y0, y1, hw, hh);
        }
        std::vector<uint8_t> still;
        sc.selected = -1;
        if (!rd.renderStill(sc, v, W, H, 8, still)) { fprintf(stderr, "still failed\n"); return 1; }
        savePPM(out + "/" + c.name + ".ppm", still, W, H, false);
        GLenum er = glGetError(); if (er) fprintf(stderr, "GL error 0x%x after %s\n", er, c.name);
        printf("%s shards=%d\n", c.name, sc.particles().shardCount());
    }
    return 0;
}
