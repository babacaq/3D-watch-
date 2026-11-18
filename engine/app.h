// Приложение PrismLab: экран, жесты, гизмо, панели, таймлайн, экспорт.
#pragma once
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "importer.h"
#include "renderer.h"
#include "scene.h"
#include "ui.h"

namespace eng {

// То, что приложению даёт платформа (Android / тест на ПК).
struct Platform {
    std::string dataDir;                                                                  // папка для проектов и моделей
    std::function<void()> importModels;                                                   // открыть выбор файлов
    std::function<bool(const std::string& name, const std::vector<uint8_t>& png)> savePng;  // сохранить в галерею
    std::function<bool(int w, int h, int fps, int mbps, const std::string& name)> videoBegin;
    std::function<bool(const uint8_t* rgba, int w, int h, int index)> videoFrame;
    std::function<bool()> videoEnd;
    std::function<void()> videoCancel;
    std::function<void(const std::string&)> toast;
};

enum class Tab { None, Scene, Object, Bones, Physics, Fx, Anim, Render };

struct Modal {
    enum Kind { None, Keypad, Text, List, Confirm, Info, Progress } kind = None;
    std::string title, buf, msg;
    std::vector<std::string> items;
    std::vector<Icon> icons;
    std::function<void(const std::string&)> onText;
    std::function<void(int)> onPick;
    std::function<void()> onYes;
    ScrollState scroll;
    bool shift = false, ru = true, first = true;
    std::string yesLabel = "Да";
};

class App {
public:
    Platform plat;
    Scene scene;
    Renderer renderer;
    UI ui;

    bool initGL(int w, int h, float density);
    void shutdown();
    void resize(int w, int h, float density);
    void touch(int action, int id, float x, float y);  // 0 вниз, 1 движение, 2 вверх, 3 отмена
    void frame(double nowSec);
    bool onBack();
    void onPause();
    void setTargetFbo(GLuint f) { targetFbo_ = f; }

    bool importModelData(const std::string& displayName, const uint8_t* d, size_t n, const FileReader& rd, std::string* err);
    bool importModelPath(const std::string& path, std::string* err = nullptr);
    void loadAutosave();
    bool saveProjectAs(const std::string& name, bool quiet = false);
    bool openProject(const std::string& name);
    void newProject();
    void toast(const std::string& s);

    // для тестов
    void setTab(Tab t) { tab_ = t; }
    void startEditor() { home_ = false; }
    bool atHome() const { return home_; }
    bool landscape() const { return land_; }
    int selectedBone() const { return selBone_; }
    void selectBone(int n) { selBone_ = n; }
    void enableBones(bool b) { showBones_ = b; }
    Rect toolbarRect() const { return rTab_; }
    void goHome();
    void addItem(int i);
    void focusSel() { focusSelected(); }
    void setCamView(bool b) { camView_ = b; }
    bool boneXY(int node, float& x, float& y) const { const Object* o = scene.find(scene.selected); return o && boneScreenPos(*o, node, x, y); }
    void setFrame(int f);
    Tab tab() const { return tab_; }
    void openAddMenu();
    void openMainMenu();
    void startStillExport();
    void startVideoExport();
    bool exporting() const { return exp_.kind != Export::None; }
    Rect viewportRect() const { return rVP_; }
    int gizmoMode() const { return gizmoMode_; }
    std::string lastExportInfo;
    bool project(const Vec3& w, float& sx, float& sy) const;
    Rect timelineRect() const { return rTL_; }
    Rect panelRect() const { return rPanel_; }
    int frameNow() const { return (int)std::lround(frameF_); }
    bool modalOpen() const { return modal_.kind != Modal::None; }
    const Modal& modal() const { return modal_; }

private:
    // ---- раскладка и кадр ----
    void layout();
    void renderViewport();
    View currentView(float aspect) const;
    Rect cameraViewRect() const;

    // ---- интерфейс ----
    void drawHome();
    void refreshHome();
    void freeThumbs();
    void makeThumb(const std::string& name);
    void deleteProject(const std::string& name);
    void duplicateProject(const std::string& name);
    void renameProject(const std::string& from, const std::string& to);
    void drawTopBar();
    void drawBoneOverlay();
    bool boneAtScreen(float x, float y, int& node) const;
    bool boneScreenPos(const Object& o, int node, float& sx, float& sy) const;
    void boneDrag(float dx, float dy);
    void boneKeyNow();
    void panelBones(const Rect& r);
    void panelPhysics(const Rect& r);
    void panelFx(const Rect& r);
    void addFxMenu();
    void propSim(Object& o, const std::string& label, std::function<float&(Object&)> ref, float lo, float hi, const char* fmt, float perDp);
    void drawViewportOverlay();
    void drawTimeline();
    void drawTabs();
    void drawPanel();
    void panelScene(const Rect& r);
    void panelObject(const Rect& r);
    void panelAnim(const Rect& r);
    void panelRender(const Rect& r);
    void drawModal();
    void drawToast();
    void drawKeypad(const Rect& card);
    void drawKeyboard();
    void sectionLabel(const std::string& s);

    // ---- форма внутри прокрутки ----
    struct Form {
        float x = 0, y = 0, w = 0, y0 = 0;
    };
    Rect row(float hDp = 40.f);
    Rect cell(const Rect& r, int i, int n, float gapDp = 6.f);
    void space(float dpv) { form_.y += ui.dp(dpv); }
    bool slide(const Rect& r, const std::string& label, float& v, float lo, float hi, const char* fmt, float perDp, std::function<void(float)> apply);
    void propF(Object& o, const std::string& label, std::function<float&(Object&)> ref, float lo, float hi, const char* fmt, float perDp);
    void transformRows(Object& o);
    int curFrame() const { return (int)std::lround(frameF_); }
    void editNumber(const std::string& title, float cur, std::function<void(float)> cb);
    void editText(const std::string& title, const std::string& cur, std::function<void(const std::string&)> cb);
    void pickList(const std::string& title, const std::vector<std::string>& items, std::function<void(int)> cb, const std::vector<Icon>& icons = {});
    void confirm(const std::string& title, const std::string& msg, std::function<void()> yes, const std::string& yesLabel = "Да");
    void beginEdit();

    // ---- жесты и гизмо ----
    void viewportGestures();
    void pickAt(float x, float y);
    void gizmoGeometry(Vec3& origin, float& L, Vec3 axes[3]) const;
    int gizmoHit(float x, float y);
    void gizmoDrag(float x, float y);
    void drawGizmo();
    void cameraGesture(float dyaw, float dpitch, float dolly, float panX, float panY);
    void orbitGesture(float dx, float dy, float zoomRatio, float panX, float panY);
    void focusSelected();
    void alignCameraToView();
    void rayFromScreen(float x, float y, Vec3& ro, Vec3& rd) const;

    // ---- время ----
    void advancePlayback(double dt);
    void gotoFrame(int f);

    // ---- экспорт ----
    void stepExport();
    void finishExport(bool ok, const std::string& msg);
    std::string projectsDir() const { return plat.dataDir + "/projects"; }
    std::vector<std::string> listProjects() const;
    void uploadPending();

    // ---- состояние ----
    GLuint targetFbo_ = 0;
    bool gl_ = false;
    int W_ = 1, H_ = 1;
    float density_ = 1.f;
    double lastTime_ = -1;
    float dt_ = 1.f / 60;
    Rect rTop_, rVP_, rTL_, rPanel_, rTab_;
    Tab tab_ = Tab::None;
    ScrollState scrollScene_, scrollObj_, scrollAnim_, scrollRender_, scrollBones_, scrollPhys_, scrollFx_, scrollBar_, scrollHome_;
    float contentScene_ = 0, contentObj_ = 0, contentAnim_ = 0, contentRender_ = 0, contentBones_ = 0, contentPhys_ = 0, contentFx_ = 0, contentBar_ = 0, contentHome_ = 0;
    bool land_ = false;

    // стартовый экран
    bool home_ = true;
    struct ProjInfo {
        std::string name;
        long long mtime = 0, size = 0;
        GLuint tex = 0;
        bool thumbTried = false;
    };
    std::vector<ProjInfo> homeList_;
    bool homeDirty_ = true;
    std::string thumbPending_;
    bool hasAutosave_ = false;

    // кости
    int selBone_ = -1;
    bool boneDrag_ = false;
    Form form_;

    // события касаний
    struct Ev { int action, id; float x, y; };
    std::deque<Ev> events_;
    struct Ptr { int id; float x, y; };
    std::vector<Ptr> ptrs_;
    PointerState primary_;
    int primaryId_ = -1;

    // камера вьюпорта
    Vec3 target_{0, 0.9f, 0};
    float yaw_ = 0.55f, pitch_ = 0.32f, dist_ = 7.5f, fov_ = 45.f;
    bool camView_ = false, showBones_ = false;
    float viewScale_ = 1.f;
    Vec3 eye_;
    View view_;
    Rect viewRect_;  // область, куда реально рисуется 3D (с учётом полос вида из камеры)

    // воспроизведение
    bool playing_ = false;
    double frameF_ = 1.0;
    int lastSimFrame_ = -1;
    bool autoSaveDirty_ = false;
    double lastAutosave_ = 0;

    // жесты
    enum class Gest { None, Armed, Orbit, Pinch, Gizmo, Timeline, Bone } gest_ = Gest::None;
    uint32_t vpId_ = 0, tlId_ = 0;
    int gizmoMode_ = 0;
    bool snap_ = false;
    int gizmoActive_ = -1;
    Mat4 gW0_;                 // матрица объекта в момент нажатия
    Vec3 gO0_, gAxis_;
    float gT0_ = 0, gAng0_ = 0, gAngAcc_ = 0, gAngPrev_ = 0;
    Vec3 gHit0_;
    float gStartX_ = 0, gStartY_ = 0;
    float pinchD0_ = 0, pinchPrevD_ = 0;
    float pinchMx_ = 0, pinchMy_ = 0;
    bool editStarted_ = false;

    Modal modal_;
    std::string toastMsg_;
    float toastT_ = 0;

    struct Export {
        enum Kind { None, Still, Video } kind = None;
        int frame = 0, first = 1, last = 1, index = 0, ticks = 0;
        int savedFrame = 1;
        std::string name;
        bool begun = false;
    } exp_;
};

}  // namespace eng
