// Mobile3D: рендерер, сцена, камера, физика, эффекты и интерфейс. Платформенный код (EGL, касания, файлы) снаружи.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "animator.h"
#include "gl_api.h"
#include "importer.h"
#include "model.h"
#include "particles.h"
#include "physics.h"

namespace eng {

class Engine {
public:
    Engine();
    ~Engine();

    // GL (вызывать при активном контексте; после потери контекста - releaseGL() и снова initGL()).
    bool initGL(int width, int height);
    void releaseGL();
    void resize(int width, int height);
    void setTargetFramebuffer(GLuint fbo) { targetFbo_ = fbo; }

    // Модели: имя файла нужно для определения формата; reader читает соседние файлы (.mtl, .bin, текстуры).
    bool addModelFile(const std::string& fileName, const uint8_t* data, size_t size, const FileReader& reader, std::string* err = nullptr);
    int addModel(std::unique_ptr<Model> m);
    void selectModel(int index);
    int modelCount() const { return (int)models_.size(); }
    int currentModel() const { return cur_; }
    const Model* model(int i) const { return models_[i].get(); }

    void setHint(const std::string& text) { hint_ = text; }
    void setStatus(const std::string& text) { status_ = text; statusT_ = 5.f; }
    std::function<void()> onOpenRequest;  // кнопка OPEN

    void update(float dt);
    void render();
    void touch(int action, int pointerId, float x, float y);  // 0 down, 1 move, 2 up, 3 cancel

    // Управление / тесты
    void playClip(int index) { anim_.play(index); }
    void setCamera(float yaw, float pitch, float distanceScale);
    void setShadows(bool on) { shadows_ = on; }
    void setBones(bool on) { bones_ = on; }
    void setShowUI(bool on) { showUI_ = on; }
    bool pressButton(const std::string& label);
    void fire();                                  // выстрел: вспышка, дым, искры, пуля
    void spawnBody(bool box, Vec3 pos);
    void explode(Vec3 pos);
    void addFire(Vec3 pos);
    PhysicsWorld& physics() { return phys_; }
    Particles& effects() { return fx_; }
    float fps() const { return fps_; }
    Vec3 muzzlePos() const { return muzzle_; }

private:
    struct Locs {
        GLint vp = -1, model = -1, skinned = -1, joints = -1, tex = -1, hasTex = -1, alphaMode = -1, cutoff = -1;
        GLint lightVP = -1, shadow = -1, lightDir = -1, cam = -1, shadowOn = -1, checker = -1, scroll = -1;
        GLint shin = -1, texel = -1, tint = -1, bg = -1, pl = -1, plc = -1;
    };
    struct Btn { int id; std::string label; float x, y, w, h; bool on; };
    struct UIVert { float x, y, u, v; uint8_t c[4]; };
    struct Ptr { int id; float x, y, sx, sy; double t0; int btn; bool camera; };
    enum Tool { T_MUZZLE = 0, T_BOX, T_BALL, T_FIRE, T_SMOKE, T_BOOM, T_COUNT };

    GLuint compile(const char* vs, const char* fs, const char* tag);
    bool buildPrograms();
    bool buildShadowMap();
    void buildFont();
    Locs getLocs(GLuint prog);

    void onModelChanged();
    void drawModel(int pass, const Locs& L);  // 0 тень, 1 непрозрачное, 2 прозрачное
    void drawBodies(bool shadowPass, const Locs& L);
    void drawBones(const Mat4& vp, Vec3 right, Vec3 up, Vec3 cam);
    Mat4 viewMatrix(Vec3* eye) const;
    bool pickRay(float x, float y, Vec3& o, Vec3& d) const;
    void defaultMuzzle();
    Vec3 axisVec() const;
    void buildCollider();
    void handleTap(float x, float y);

    void buildUI();
    void uiRect(float x, float y, float w, float h, const float* rgba);
    void uiText(float x, float y, const std::string& s, int scale, const float* rgba, bool shadow = true);
    float textWidth(const std::string& s, int scale) const { return (float)s.size() * 6 * scale; }
    void flushUI();
    int hitButton(float x, float y) const;
    void trigger(int id);

    int w_ = 1, h_ = 1;
    GLuint targetFbo_ = 0;
    bool glReady_ = false;
    std::vector<std::unique_ptr<Model>> models_;
    int cur_ = -1;
    Animator anim_;
    Mat4 xf_;                       // нормализация модели: масштаб + привязка к земле
    Vec3 wmin_{-1, 0, -1}, wmax_{1, 2, 1};
    float scroll_ = 0, time_ = 0, fps_ = 60, shake_ = 0;
    bool shadows_ = true, bones_ = false, showUI_ = true;
    std::string hint_, status_;
    float statusT_ = 0;
    std::string colliderNote_;

    // камера
    float yaw_ = 0.6f, pitch_ = 0.25f, dist_ = 6.f, baseDist_ = 6.f;
    Vec3 target_{0, 1, 0};
    float fovy_ = 0.8f;
    Mat4 lastVP_;

    // GL
    GLuint progScene_ = 0, progDepth_ = 0, progFlat_ = 0, progUI_ = 0;
    Locs sL_, dL_;
    GLint fVP_ = 0, uiRes_ = 0;
    GLuint shadowTex_ = 0, shadowFbo_ = 0;
    int shadowSize_ = 2048;
    GLuint fontTex_ = 0, whiteTex_ = 0;
    GLuint boneVao_ = 0, boneVbo_ = 0, uiVao_ = 0, uiVbo_ = 0;
    MeshData ground_, cube_, sphere_;
    std::vector<UIVert> uiVerts_;
    std::vector<float> boneVerts_;
    Mat4 lightVP_;
    Vec3 lightDir_{0.45f, 0.85f, 0.5f};

    // физика и эффекты
    PhysicsWorld phys_;
    Particles fx_;
    struct BodyVis { Vec3 color; };
    std::vector<BodyVis> vis_;
    Vec3 muzzle_{0, 1, 0};
    bool muzzleSet_ = false;
    int axis_ = 0;                // 0:+Z 1:-Z 2:+X 3:-X 4:+Y 5:-Y
    int tool_ = T_BOX;
    uint32_t rng_ = 987654321u;
    float fireCool_ = 0;

    // ввод
    std::vector<Ptr> ptrs_;
    std::vector<Btn> btns_;
    int speedIdx_ = 1;
    int pressedBtn_ = -1;
    bool paused_ = false;
};

}  // namespace eng
