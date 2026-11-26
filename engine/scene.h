// Сцена PrismLab: объекты, ключевые кадры, прицепление/отцепление, эффекты, сохранение проекта.
// Здесь нет OpenGL: сцену можно тестировать без графики.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "animator.h"
#include "math3d.h"
#include "model.h"
#include "particles.h"

namespace eng {

struct Json;

enum class ObjType { Model = 0, Light = 1, Camera = 2, Effect = 3 };
enum class LightKind { Sun = 0, Point = 1 };
enum class FxKind { Fire = 0, Smoke = 1, Muzzle = 2, Explosion = 3, Sparks = 4, Blood = 5 };
constexpr int kFxKinds = 6;
// Вспышка выстрела: 0 пистолет, 1 дробовик, 2 автомат. Кровь: 0 брызги (удар), 1 течёт (рана).
constexpr int kMuzzleVariants = 3;

struct TKey {
    int frame = 1;
    Vec3 t, r, s{1, 1, 1};  // r - углы Эйлера XYZ в радианах (как в Blender)
    int ease = 1;           // интерполяция до следующего ключа: 0 линейная, 1 плавная, 2 ступенька
};
struct ParentKey {
    int frame = 1;
    int parent = -1;  // id объекта-родителя, -1 = отцеплен
    int node = -1;    // узел (кость) модели-родителя или -1
    Mat4 offset;      // положение относительно точки прицепления
};
struct ClipKey {
    int frame = 1;
    int clip = -1;  // -1 = поза покоя
    float speed = 1.f;
    bool loop = true;
};
struct VisKey {
    int frame = 1;
    bool visible = true;
};

// Поза кости: поправка к позе клипа (или позе покоя) в пространстве родителя кости.
struct BoneKey {
    int frame = 1;
    Vec3 t;
    Quat r;
    int ease = 1;
};
struct BoneTrack {
    int node = -1;
    std::vector<BoneKey> keys;
};

// Физика твёрдого тела: включается с выбранного кадра, до него объект идёт по ключам.
struct PhysProps {
    bool enabled = false;
    int kind = 0;        // 0 падает и сталкивается, 1 неподвижное препятствие (пол, стена)
    int shape = 0;       // 0 коробка по габаритам модели, 1 шар
    float mass = 1.f, bounce = 0.3f, friction = 0.6f;
    int start = 1;       // кадр включения
    bool inherit = true; // подхватить скорость, с которой объект двигался по ключам
    Vec3 vel;            // добавочная начальная скорость, м/с
    Vec3 spin;           // добавочное вращение, рад/с
};

// Рэгдолл: кости скелета становятся твёрдыми телами на шарнирах и падают с выбранного кадра.
struct RagdollProps {
    bool enabled = false;
    int start = 1;          // кадр включения
    float mass = 60.f;      // общая масса, кг
    float friction = 0.8f, bounce = 0.05f;
    float limit = 70.f;     // предел сгиба суставов, градусы
    float thick = 1.f;      // толщина тел-костей
    bool inherit = true;    // подхватить скорость, с которой шла анимация
    Vec3 push;              // добавочный толчок, м/с (мировые оси)
};

// Вторичная физика: предмет, прицепленный к кости (оружие на спине, сумка), качается по инерции.
struct SecondaryProps {
    bool enabled = false;
    float stiffness = 40.f;  // возвращающая сила
    float damping = 5.f;
    float gravity = 0.5f;    // провисание вниз
    float amount = 1.f;      // доля размаха
};

// Ткань / пружины вершин для меша без костей (плащ, волосы, флаг).
struct ClothProps {
    bool enabled = false;
    int pinSide = 0;         // 0 закреплена верхняя часть, 1 нижняя
    float pin = 0.12f;       // доля высоты, которая закреплена
    float stiffness = 0.8f;  // жёсткость связей 0..1
    float shape = 0.12f;     // возврат к исходной форме (пружины вершин)
    float damping = 0.03f;
    float gravity = 1.f;
    float wind = 0.f;        // м/с
    float windDir = 0.f;     // направление ветра, градусы вокруг Y
};

// Деформированные копии мешей объекта (ткань); GL-буферы заводит и обновляет рендерер.
struct ClothRt {
    std::vector<MeshData> meshes;  // по индексу меша модели; без вершин = не ткань
    bool dirty = false;
    ClothRt() = default;
    ClothRt(const ClothRt&) = delete;
    ClothRt& operator=(const ClothRt&) = delete;
    ~ClothRt() { for (auto& m : meshes) m.release(); }
};

struct Object {
    int id = 0;
    std::string name;
    ObjType type = ObjType::Model;
    bool visible = true;
    bool locked = false;
    Vec3 t, r, s{1, 1, 1};  // базовое положение (если нет ключей)
    std::vector<TKey> keys;
    std::vector<ParentKey> parents;
    std::vector<ClipKey> clipKeys;
    std::vector<VisKey> visKeys;

    // Model
    int model = -1;
    Mat4 fit;  // нормализация модели (2 м по наибольшему размеру, ноги на земле)
    bool matOverride = false;
    float color[3] = {0.8f, 0.8f, 0.8f};
    float metallic = 0.f, roughness = 0.5f, emission = 0.f;
    bool castShadow = true;

    // Light
    LightKind light = LightKind::Point;
    float lcolor[3] = {1.f, 0.95f, 0.9f};
    float energy = 20.f;
    float lradius = 8.f;

    // Camera
    float fovDeg = 40.f;

    // Effect
    FxKind fx = FxKind::Fire;
    int fxStart = 1, fxEnd = 120;
    float fxScale = 1.f;
    int fxRate = 0;  // вспышка: интервал между выстрелами в кадрах (0 = один выстрел)
    int fxVariant = 0;    // вспышка: пистолет/дробовик/автомат; кровь: брызги/течёт
    float fxPower = 1.f;  // сила взрыва, давление крови, яркость вспышки
    bool fxPush = true;   // взрыв толкает объекты с физикой

    // Физика и кости
    PhysProps phys;
    RagdollProps rag;
    SecondaryProps sec;
    ClothProps cloth;
    std::vector<BoneTrack> boneTracks;

    // ---- вычисляется при evaluate() ----
    Mat4 world;
    bool visibleNow = true;
    std::unique_ptr<Animator> anim;
    std::unique_ptr<ClothRt> clothRt;
    float fxAcc[3] = {0, 0, 0};
};

struct RenderSettings {
    float exposure = 0.f;  // EV
    float contrast = 1.05f, saturation = 1.08f;
    bool ao = true;
    float aoStrength = 1.f;
    bool bloom = true;
    float bloomStrength = 0.25f;
    bool shadows = true;
    float shadowSoft = 1.f;
    bool contactShadows = true;  // мелкие тени у самой земли и в щелях
    int shadowRes = 2048;
    float skyBrightness = 0.6f;
    float skyTop[3] = {0.22f, 0.42f, 0.85f};
    float skyHorizon[3] = {0.78f, 0.84f, 0.92f};
    float skyGround[3] = {0.20f, 0.19f, 0.18f};
    bool floor = true;
    float floorColor[3] = {0.36f, 0.36f, 0.38f};
    float floorRoughness = 0.7f;
    bool vignette = true;
    int exportW = 1080, exportH = 1920;  // формат кадра: вертикальный по умолчанию, можно горизонтальный
    int stillSamples = 24;  // проходов накопления для скриншота
    int videoSamples = 3;   // проходов накопления на кадр видео
    int videoBitrateMbps = 16;
};

struct ProjectSettings {
    int fps = 24;
    int frameStart = 1, frameEnd = 120;
};

class Scene {
public:
    Scene();

    // ---- данные ----
    struct ModelEntry {
        std::unique_ptr<Model> model;
        std::string source;  // путь к файлу или "prim:cube"
    };
    std::vector<ModelEntry> models;
    std::vector<std::unique_ptr<Object>> objects;
    ProjectSettings proj;
    RenderSettings render;
    int selected = -1;
    int activeCamera = -1;
    std::string name = "Untitled";
    bool autoKey = false;
    std::function<std::unique_ptr<Model>(const std::string& source)> modelLoader;  // для загрузки проекта

    Object* find(int id);
    const Object* find(int id) const;
    int indexOf(int id) const;

    // ---- создание ----
    void clear();         // новая сцена: солнце + камера
    void initDefault();
    int addModel(std::unique_ptr<Model> m, const std::string& source, const std::string& name);
    int addPrimitive(const std::string& kind);  // cube, sphere, plane, cylinder, cone
    static std::unique_ptr<Model> primitiveModel(const std::string& kind, std::string* title = nullptr);
    void setWorld(int id, const Mat4& W);       // положение в мире (с учётом родителя), с ключами
    int addLight(LightKind k);
    int addCamera();
    int addEffect(FxKind k);
    int duplicate(int id);
    void remove(int id);
    void rename(int id, const std::string& name);

    // ---- вычисление кадра ----
    void evaluate(float frame);  // положения, позы скелетов, видимость
    float frame() const { return frame_; }
    const Mat4& modelMatrix(const Object& o, Mat4& tmp) const { tmp = o.world * o.fit; return tmp; }

    // ---- ключи и прицепление ----
    bool insertKey(int id, int frame);
    bool deleteKey(int id, int frame);
    bool hasKeyAt(const Object& o, int frame) const;
    void setLocal(int id, Vec3 t, Vec3 r, Vec3 s);  // правка с авто-ключом; для прицеплённых - смещение относительно родителя
    void localTRS(const Object& o, Vec3& t, Vec3& r, Vec3& s) const;  // то, что показывается в панели
    bool attach(int child, int parent, int node, int frame);
    bool detach(int child, int frame);
    bool isAttached(const Object& o, float frame) const;
    const ParentKey* activeParent(const Object& o, float frame) const;
    void setClip(int id, int frame, int clip, float speed, bool loop);

    // ---- кости ----
    bool boneDeltaAt(const Object& o, int node, float f, Vec3& t, Quat& r) const;
    bool boneHasKey(const Object& o, int node, int frame) const;
    bool setBoneKey(int id, int node, int frame, Vec3 t, Quat r);
    bool deleteBoneKey(int id, int node, int frame);
    void clearBone(int id, int node);
    int boneCount(const Object& o) const;                        // сколько костей в скелете модели
    std::vector<int> boneNodes(const Object& o) const;           // только настоящие суставы: узлы с геометрией не считаются
    int boneParent(const Object& o, int node) const;             // ближайший предок-сустав или -1
    // Поза кости по движению гизмо в мировых осях: вращение rw и сдвиг worldMove относительно позы (t0, d0) в момент нажатия.
    bool boneFromWorld(const Object& o, int node, const Quat& rw, const Vec3& worldMove, const Vec3& t0, const Quat& d0, Vec3& tOut, Quat& dOut) const;
    bool boneWorldPos(const Object& o, int node, Vec3& p) const;  // положение сустава в мире

    // ---- физика ----
    void bakePhysics();                       // пересчитывается сама при изменении сцены (твёрдые тела, рэгдолл, маятник, ткань)
    bool ragdollBaked(int id) const { auto it = rag_.find(id); return it != rag_.end() && !it->second.mats.empty(); }
    bool clothBaked(int id) const { auto it = cloth_.find(id); return it != cloth_.end() && !it->second.meshes.empty(); }
    bool clothSupported(const Object& o, std::string* why = nullptr) const;  // есть меши без скелета и их не слишком много
    void releaseGL();                         // освободить GL-буферы ткани (до удаления контекста)
    bool physicsActive(const Object& o, float f) const;
    int physicsFrames() const { return phys_.last - phys_.first + 1; }

    // ---- эффекты ----
    Particles& particles() { return fx_; }
    void simulateTo(int frame);  // детерминированная симуляция частиц до кадра
    void resetEffects();

    // ---- выбор ----
    int pick(const Vec3& origin, const Vec3& dir, float* distOut = nullptr) const;  // по габаритным коробкам моделей
    bool localBox(const Object& o, Vec3& lo, Vec3& hi) const;

    // ---- проект и отмена ----
    std::string toJson() const;
    bool fromJson(const std::string& text, std::string* err = nullptr);  // объекты и настройки; модели берутся по индексам
    std::string saveProject() const;                                      // + список моделей
    bool loadProject(const std::string& text, std::string* err = nullptr);
    void pushUndo();
    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }

    static Vec3 eulerFromMat(const Mat4& m);
    static Mat4 eulerMat(Vec3 r) { return fromQuat(quatEuler(r.x, r.y, r.z)); }

private:
    bool applyScene(const Json& j, std::string* err);
    void evalLocal(const Object& o, float f, Vec3& t, Vec3& r, Vec3& s) const;
    Quat evalLocalQ(const Object& o, float f, Vec3& t, Vec3& s) const;  // поворот между ключами - SLERP кватернионов
    Mat4 attachFrom(const Object& parent, int node) const;
    Mat4 worldOf(Object& o, float f, std::vector<char>& state);
    void evalAnimation(Object& o, float f);
    void stepEffects(int frame);
    void insertKeyRaw(Object& o, int frame, Vec3 t, Vec3 r, Vec3 s);
    std::string uniqueName(const std::string& base) const;
    bool wouldCycle(int child, int parent) const;

    struct PhysBake {
        std::map<int, std::vector<Mat4>> mats;  // по id объекта: положение на каждом кадре first..last
        int first = 0, last = -1;
        uint64_t sig = 0;
        bool valid = false;
    } phys_;
    bool physOff_ = false, baking_ = false, ragOff_ = false, secOff_ = false, clothOff_ = false;
    uint64_t physSignature() const;
    bool boxOfObject(const Object& o, Vec3& centerW, Quat& q, Vec3& half, Vec3& localC) const;
    void bakeRigid();
    void bakeRagdolls();
    void bakeSecondary();
    void bakeCloth();
    void applyCloth(Object& o, float f);

    // рэгдолл: на каждом кадре от first - матрицы узлов в пространстве модели (для костей-тел)
    struct RagBake {
        int first = 0, last = -1;
        std::vector<std::vector<Mat4>> mats;
        std::vector<char> has;
    };
    std::map<int, RagBake> rag_;
    struct SecBake {
        int first = 0;
        std::vector<Quat> q;
    };
    std::map<int, SecBake> sec_;
    struct ClothMeshBake {
        int mesh = 0;
        int particles = 0;
        std::vector<int> vmap;      // вершина меша -> частица
        std::vector<Vec3> pos;      // кадры × частицы, в пространстве узла меша
    };
    struct ClothBake {
        int first = 0, last = -1;
        std::vector<ClothMeshBake> meshes;
    };
    std::map<int, ClothBake> cloth_;

    float frame_ = 1.f;
    int nextId_ = 1;
    Particles fx_;
    int simFrame_ = -1000000;
    std::vector<std::string> undo_, redo_;
};

}  // namespace eng
