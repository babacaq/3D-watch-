// Десктопный тест: EGL surfaceless (Mesa) + FBO, рендер кадров в PPM. Не часть APK.
// Использование: desktop_test <outdir> <model> [model...]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "engine.h"

using namespace eng;

static bool readFile(const std::string& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}
static std::string dirOf(const std::string& p) { size_t i = p.find_last_of('/'); return i == std::string::npos ? "" : p.substr(0, i + 1); }
static std::string baseOf(const std::string& p) { size_t i = p.find_last_of('/'); return i == std::string::npos ? p : p.substr(i + 1); }

static void savePPM(const std::string& path, int w, int h) {
    std::vector<uint8_t> px((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    FILE* f = fopen(path.c_str(), "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--)
        for (int x = 0; x < w; x++) fwrite(&px[((size_t)y * w + x) * 4], 1, 3, f);
    fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s outdir model...\n", argv[0]); return 2; }
    std::string out = argv[1];
    const int W = 720, H = 1280;

    typedef EGLDisplay (*PFNGETPLATFORMDISPLAY)(EGLenum, void*, const EGLint*);
    auto getPD = (PFNGETPLATFORMDISPLAY)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = getPD ? getPD(EGL_PLATFORM_SURFACELESS_MESA, nullptr, nullptr) : EGL_NO_DISPLAY;
    if (dpy == EGL_NO_DISPLAY) { fprintf(stderr, "no display\n"); return 1; }
    EGLint maj, min;
    if (!eglInitialize(dpy, &maj, &min)) { fprintf(stderr, "eglInitialize failed 0x%x\n", eglGetError()); return 1; }
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint ca[] = {EGL_SURFACE_TYPE, 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
    EGLConfig cfg; EGLint n = 0;
    if (!eglChooseConfig(dpy, ca, &cfg, 1, &n) || n < 1) { fprintf(stderr, "no config 0x%x\n", eglGetError()); return 1; }
    const EGLint cx[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cx);
    if (ctx == EGL_NO_CONTEXT) { fprintf(stderr, "no context 0x%x\n", eglGetError()); return 1; }
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) { fprintf(stderr, "makecurrent failed 0x%x\n", eglGetError()); return 1; }

    GLuint fbo, color, depth;
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &color); glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    glGenRenderbuffers(1, &depth); glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "fbo incomplete\n"); return 1; }

    Engine e;
    e.setTargetFramebuffer(fbo);
    if (!e.initGL(W, H)) { fprintf(stderr, "initGL failed\n"); return 1; }
    for (int i = 2; i < argc; i++) {
        std::string p = argv[i], d = dirOf(p);
        std::vector<uint8_t> data;
        if (!readFile(p, data)) { fprintf(stderr, "cannot read %s\n", p.c_str()); return 1; }
        FileReader rd = [d](const std::string& rel, std::vector<uint8_t>& o) { return readFile(d + rel, o); };
        std::string err;
        if (!e.addModelFile(baseOf(p), data.data(), data.size(), rd, &err)) fprintf(stderr, "import error: %s\n", err.c_str());
    }
    auto frame = [&](const char* name, int steps, float dt = 1.f / 60) {
        for (int i = 0; i < steps; i++) e.update(dt);
        e.render();
        savePPM(out + "/" + name + ".ppm", W, H);
        GLenum er = glGetError();
        if (er) fprintf(stderr, "GL error 0x%x after %s\n", er, name);
    };
    frame("01_start", 30);
    e.pressButton("BOX");
    for (int i = 0; i < 6; i++) e.spawnBody(i % 2 == 0, {-0.6f + 0.25f * i, 2.5f + 0.4f * i, 0.5f});
    frame("02_bodies", 60);
    e.fire(); e.fire();
    frame("03_muzzle", 3);
    e.explode({1.0f, 0.2f, 0.8f});
    e.addFire({-1.2f, 0.f, 0.8f});
    e.effects().addEmitter({1.6f, 0.f, -0.6f}, 1);
    frame("04_boom", 12);
    frame("05_fire_smoke", 100);
    e.setCamera(2.2f, 0.4f, 0.8f);
    e.pressButton("BONES");
    frame("06_bones", 20);
    // касание: тап по сцене и нажатие кнопки SHOOT
    e.touch(0, 1, W * 0.5f, H * 0.5f); e.touch(2, 1, W * 0.5f, H * 0.5f);
    e.pressButton("SHOOT");
    frame("07_after_tap", 4);
    // скелетная модель: клип, три момента времени, кости
    e.physics().clearBodies();
    e.selectModel(0); e.playClip(0); e.setCamera(0.7f, 0.3f, 1.f);
    frame("08_skin_a", 1); frame("09_skin_b", 30); frame("10_skin_c", 30);
    e.pressButton("BONES"); e.pressButton("SHOOT");
    frame("11_skin_bones", 3);
    printf("fps(sim)=%.1f bodies=%d fx=%d\n", e.fps(), e.physics().aliveCount(), e.effects().count());
    return 0;
}
