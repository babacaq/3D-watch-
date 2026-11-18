// Тест рендерера на ПК: EGL surfaceless (Mesa) -> PPM. Не часть APK.
// Использование: render_test <outdir> <assets>
#include <cstdio>
#include <cstdlib>
#include <fstream>

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
    const int W = 720, H = 1280;
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

    GLuint fbo, color;
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &color); glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);

    Renderer rd;
    if (!rd.init()) { fprintf(stderr, "renderer init failed\n"); return 1; }
    printf("hdrFloat=%d\n", rd.hdrFloat());

    Scene sc;
    auto prim = [&](const char* k, Vec3 t, Vec3 s, float r, float g, float bl, float met, float rough) {
        int id = sc.addPrimitive(k);
        for (auto& m : sc.models) if (m.model && !m.model->uploaded) m.model->upload();
        Object* o = sc.find(id);
        o->t = t; o->s = s; o->matOverride = true; o->color[0] = r; o->color[1] = g; o->color[2] = bl; o->metallic = met; o->roughness = rough;
        return id;
    };
    prim("cube", {-1.2f, 0.5f, 0}, {1, 1, 1}, 0.8f, 0.2f, 0.15f, 0.f, 0.5f);
    prim("sphere", {0.2f, 0.5f, 0.2f}, {1, 1, 1}, 0.9f, 0.8f, 0.3f, 1.f, 0.25f);
    prim("sphere", {1.5f, 0.5f, -0.3f}, {1, 1, 1}, 0.2f, 0.5f, 0.9f, 0.f, 0.15f);
    prim("cylinder", {0.f, 0.5f, -2.0f}, {1, 1, 1}, 0.3f, 0.8f, 0.4f, 0.f, 0.8f);
    int sk = sc.addModel(loadFile(assets + "/test_skinned.glb"), assets + "/test_skinned.glb", "skinned");
    if (sc.find(sk)) { sc.find(sk)->t = {-0.3f, 0, 1.6f}; }
    int fire = sc.addEffect(FxKind::Fire);
    sc.find(fire)->t = {2.2f, 0.f, 1.2f};
    int mz = sc.addEffect(FxKind::Muzzle);
    sc.find(mz)->t = {-2.f, 1.f, 1.5f};
    sc.find(mz)->r = {0, 1.2f, 0};
    sc.find(mz)->fxRate = 6;
    int light = sc.addLight(LightKind::Point);
    sc.find(light)->t = {-2.f, 2.f, 1.f};
    sc.find(light)->energy = 6.f;
    // камера
    Object* cam = sc.find(sc.activeCamera);
    cam->t = {4.5f, 3.2f, 6.f};
    Mat4 la = lookAt(cam->t, {0, 0.6f, 0}, {0, 1, 0});
    cam->r = Scene::eulerFromMat(inverse(la));

    sc.simulateTo(30);
    sc.evaluate(30);
    View v = Renderer::cameraView(*cam, (float)W / H);
    if (getenv("NOBLOOM")) sc.render.bloom = false;
    if (getenv("NOAO")) sc.render.ao = false;
    if (getenv("NOSH")) sc.render.shadows = false;
    Overlay ov; ov.grid = true; ov.icons = true;
    sc.selected = sk;
    rd.renderView(sc, v, fbo, RectI{0, 0, W, H}, ov, 1.f);
    std::vector<uint8_t> px((size_t)W * H * 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    savePPM(out + "/rt.ppm", px, W, H, true);
    GLenum er = glGetError(); if (er) fprintf(stderr, "GL error 0x%x after realtime\n", er);

    if (getenv("DUMPAO")) {
        glBindFramebuffer(GL_FRAMEBUFFER, rd.rt_.ao[1].fbo);
        int aw = rd.rt_.ao[1].w, ah = rd.rt_.ao[1].h;
        std::vector<uint8_t> ap((size_t)aw * ah * 4);
        glReadPixels(0, 0, aw, ah, GL_RGBA, GL_UNSIGNED_BYTE, ap.data());
        savePPM(out + "/ao.ppm", ap, aw, ah, true);
    }
    if (getenv("DUMPZ")) {
        glBindFramebuffer(GL_FRAMEBUFFER, rd.rt_.hdr.fbo);
        std::vector<float> z((size_t)W * H);
        glReadPixels(0, 0, W, H, GL_DEPTH_COMPONENT, GL_FLOAT, z.data());
        printf("glerr 0x%x\n", glGetError());
        for (int y = 100; y < 160; y++) printf("%d %.9f\n", y, z[(size_t)y * W + 360]);
    }
    if (getenv("DUMPZ")) {
        const char* fs = R"(#version 300 es
precision highp float;
in vec2 v_uv; out vec4 o; uniform sampler2D u_depth; uniform mat4 u_invProj;
void main(){ float d = texture(u_depth, v_uv).r; vec4 p = u_invProj * vec4(v_uv*2.0-1.0, d*2.0-1.0, 1.0); float z = -p.z/p.w; o = vec4(fract(z*4.0), d >= 0.99999 ? 1.0 : 0.0, 0.0, 1.0); })";
        const char* vs = "#version 300 es\nout vec2 v_uv;\nvoid main(){ vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)); v_uv = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }";
        GLuint pr = rd.compile(vs, fs, "dbg");
        glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, W, H);
        glUseProgram(pr); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, rd.rt_.hdr.depth);
        glUniform1i(glGetUniformLocation(pr, "u_depth"), 0);
        Mat4 ip = inverse(v.proj);
        glUniformMatrix4fv(glGetUniformLocation(pr, "u_invProj"), 1, GL_FALSE, ip.m);
        glDisable(GL_DEPTH_TEST);
        rd.fullscreen();
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        savePPM(out + "/z.ppm", px, W, H, true);
    }
    std::vector<uint8_t> still;
    sc.selected = -1;
    if (!rd.renderStill(sc, v, 540, 960, 16, still)) { fprintf(stderr, "still failed\n"); return 1; }
    savePPM(out + "/still.ppm", still, 540, 960, false);
    er = glGetError(); if (er) fprintf(stderr, "GL error 0x%x after still\n", er);
    if (getenv("FX")) {
        // кадры эффектов: вспышки, взрывы разной силы, кровь
        struct Shot { const char* name; FxKind k; int variant; float power; int frame; float rate; };
        const Shot shots[] = {
            {"fx_pistol", FxKind::Muzzle, 0, 1.f, 2, 0}, {"fx_shotgun", FxKind::Muzzle, 1, 1.f, 2, 0}, {"fx_rifle", FxKind::Muzzle, 2, 1.f, 3, 3},
            {"fx_boom_small", FxKind::Explosion, 0, 0.5f, 8, 0}, {"fx_boom_mid", FxKind::Explosion, 0, 1.f, 8, 0}, {"fx_boom_big", FxKind::Explosion, 0, 3.f, 8, 0},
            {"fx_boom_big_late", FxKind::Explosion, 0, 3.f, 30, 0}, {"fx_blood_burst", FxKind::Blood, 0, 1.f, 12, 0}, {"fx_blood_burst_late", FxKind::Blood, 0, 1.f, 60, 0},
            {"fx_blood_flow", FxKind::Blood, 1, 1.f, 50, 0}};
        for (const Shot& sh : shots) {
            Scene s2;
            rd.fxScene_ = nullptr;
            if (getenv("NOBLOOM")) s2.render.bloom = false;
            if (getenv("NOAO")) s2.render.ao = false;
            if (getenv("NOSH")) s2.render.shadows = false;
            if (getenv("NOCONTACT")) s2.render.contactShadows = false;
            Object* c2 = s2.find(s2.activeCamera);
            c2->t = {3.2f, 1.6f, 4.2f};
            Mat4 l2 = lookAt(c2->t, {0.f, 0.8f, 0.f}, {0, 1, 0});
            c2->r = Scene::eulerFromMat(inverse(l2));
            int e = s2.addEffect(sh.k);
            Object* eo = s2.find(e);
            eo->t = {-0.6f, 1.0f, 0.f};
            eo->r = {0, 1.5708f, 0};  // ось Z смотрит вдоль +X
            eo->fxStart = 1; eo->fxEnd = sh.k == FxKind::Blood && sh.variant == 1 ? 90 : 24;
            eo->fxVariant = sh.variant; eo->fxPower = sh.power; eo->fxRate = (int)sh.rate;
            s2.evaluate(1);
            s2.simulateTo(sh.frame);
            s2.evaluate((float)sh.frame);
            View v2 = Renderer::cameraView(*s2.find(s2.activeCamera), 1.f);
            std::vector<uint8_t> px2;
            if (!rd.renderStill(s2, v2, 540, 540, 4, px2)) { fprintf(stderr, "fx still failed %s\n", sh.name); continue; }
            savePPM(out + "/" + sh.name + ".ppm", px2, 540, 540, false);
            if (getenv("DBGHDR")) {
                glBindFramebuffer(GL_FRAMEBUFFER, rd.st_.accum.fbo);
                int aw = rd.st_.accum.w, ah = rd.st_.accum.h;
                std::vector<float> f((size_t)aw * ah * 4);
                glReadPixels(0, 0, aw, ah, GL_RGBA, GL_FLOAT, f.data());
                int nn = 0, ni = 0; float mx = 0; int mxi = -1;
                for (size_t i = 0; i < f.size(); i += 4) {
                    for (int c = 0; c < 3; c++) { float x = f[i + c]; if (x != x) { if (nn < 3) printf("  nan at (%d,%d) ch %d\n", (int)(i / 4) % aw, (int)(i / 4) / aw, c); nn++; } else if (std::isinf(x)) ni++; else if (x > mx) { mx = x; mxi = (int)(i / 4); } }
                }
                printf("%s: nan=%d inf=%d max=%g at (%d,%d)\n", sh.name, nn, ni, mx, mxi % aw, mxi / aw);
            }
        }
    }
    printf("done\n");
    return 0;
}
