// Рендерер PrismLab: HDR, PBR, мягкие тени, SSAO, bloom, киношный тон-маппинг, накопление для финального кадра.
#pragma once
#include <functional>
#include <vector>

#include "gl_api.h"
#include "scene.h"

namespace eng {

struct View {
    Mat4 view, proj;
    Vec3 eye;
};

struct RectI {
    int x = 0, y = 0, w = 1, h = 1;
};

struct Overlay {
    bool grid = true, icons = true, selection = true, bones = false;
};

// Линии постоянной толщины в пикселях (сетка, значки, рамки, гизмо).
class LineBatch {
public:
    bool init();
    void release();
    void clear() { v_.clear(); }
    void add(Vec3 a, Vec3 b, const float* rgba, float widthPx);
    void flush(const Mat4& vp, int w, int h, float outScale, bool depthTest);
    size_t count() const { return v_.size() / (13 * 6); }

private:
    std::vector<float> v_;
    GLuint prog_ = 0, vao_ = 0, vbo_ = 0;
    GLint uVP_ = -1, uRes_ = -1, uOut_ = -1;
    size_t cap_ = 0;
};

class Renderer {
public:
    bool init();
    void release();
    bool ready() const { return ready_; }
    bool hdrFloat() const { return hdrFloat_; }

    // Реалтайм: рисует сцену в targetFbo внутри прямоугольника r (GL-координаты, начало слева снизу).
    void renderView(Scene& sc, const View& v, GLuint targetFbo, RectI r, const Overlay& ov, float scale = 1.f);
    // Финальный кадр в память (RGBA8, строки сверху вниз). samples - число проходов накопления.
    bool renderStill(Scene& sc, const View& v, int w, int h, int samples, std::vector<uint8_t>& rgba);
    void freeStillTargets();

    LineBatch& lines() { return lines_; }
    void setPixelScale(float s) { pixelScale_ = s; }
    static View cameraView(const Object& cam, float aspect);
    static int maxExportSide();

private:
    struct Target {
        GLuint fbo = 0, color = 0, depth = 0;
        int w = 0, h = 0;
    };
    struct TargetSet {
        Target hdr, accum, ldr;
        Target ao[2];
        std::vector<Target> bloom;
        bool hasAccum = false;
    };
    struct Locs {
        GLint vp = -1, model = -1, nrm = -1, joints = -1, skinned = -1;
        GLint tex = -1, texMR = -1, texN = -1, texE = -1, texO = -1, shadow = -1;
        GLint hasTex = -1, hasMR = -1, hasN = -1, hasE = -1, hasO = -1, alphaMode = -1, cutoff = -1;
        GLint metal = -1, rough = -1, nScale = -1, emis = -1, tint = -1, useVC = -1;
        GLint cam = -1, L = -1, sunCol = -1, lightVP = -1, texel = -1, shadowSoft = -1, shadowOn = -1, pass = -1;
        GLint skyTop = -1, skyHor = -1, skyGnd = -1, skyBright = -1, pl = -1, plc = -1, nPl = -1, outScale = -1, isFloor = -1, fogCol = -1;
    };
    struct FrameLights {
        bool hasSun = false;
        Vec3 sunDir{0.4f, 0.8f, 0.4f}, sunCol{0, 0, 0};
        float pl[32] = {0}, plc[24] = {0};
        int n = 0;
    };

    bool buildPrograms();
    GLuint compile(const char* vs, const char* fs, const char* tag);
    bool makeTarget(Target& t, int w, int h, GLenum colorFmt, bool depthTex, bool filter);
    void freeTarget(Target& t);
    void ensureSet(TargetSet& s, int w, int h, bool accum);
    void freeSet(TargetSet& s);
    bool testFloatTargets();

    void gatherLights(Scene& sc, FrameLights& fl);
    void shadowPass(Scene& sc, const FrameLights& fl, int pass, Mat4& lightVP, float softness);
    void scenePass(Scene& sc, TargetSet& s, const View& v, const Mat4& lightVP, const FrameLights& fl, int pass, bool realtime, const Overlay* ov);
    void drawModels(Scene& sc, int pass, const Locs& L, const Vec3& eye);  // 0 тень, 1 непрозрачное, 2 прозрачное
    void drawFloor(Scene& sc, const Locs& L);
    void drawSky(Scene& sc, const View& v, const FrameLights& fl);
    void buildOverlay(Scene& sc, const View& v, const Overlay& ov);
    void ssaoPass(TargetSet& s, const View& v, int pass, float strength);
    void bloomPass(TargetSet& s, GLuint srcTex, float threshold);
    void tonemap(TargetSet& s, GLuint hdrTex, bool useAo, GLuint dstFbo, RectI r, Scene& sc, bool fxaa);
    void accumulate(TargetSet& s, float weight, bool useAo);
    void fullscreen();

    bool ready_ = false, hdrFloat_ = true;
    float outScale_ = 1.f, inScale_ = 1.f;
    GLuint pbr_ = 0, depthP_ = 0, sky_ = 0, ssao_ = 0, blur_ = 0, down_ = 0, up_ = 0, tone_ = 0, fxaa_ = 0, acc_ = 0;
    Locs L_, dL_;
    GLuint emptyVao_ = 0;
    GLuint shadowTex_ = 0, shadowFbo_ = 0;
    int shadowSize_ = 2048;
    GLuint whiteTex_ = 0;
    MeshData floorMesh_;
    TargetSet rt_, st_;
    LineBatch lines_;
    Mat4 lastLightVP_;
    Vec3 sceneCenter_{0, 1, 0};
    float sceneRadius_ = 4.f;
    float pixelScale_ = 1.f;
    Scene* fxScene_ = nullptr;
    GLint uSky_[8] = {0};
    // uniform-локации постобработки
    GLint tU_[24] = {0}, sU_[16] = {0}, bU_[8] = {0}, dU_[8] = {0}, uU_[8] = {0}, fU_[4] = {0}, aU_[4] = {0}, skU_[12] = {0};
};

}  // namespace eng
