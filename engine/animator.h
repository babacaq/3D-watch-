// Проигрыватель скелетной анимации: сэмплирование клипов, кроссфейд, матрицы костей.
#pragma once
#include <vector>

#include "model.h"

namespace eng {

struct Pose {
    std::vector<Vec3> t, s;
    std::vector<Quat> r;
};

// Поправка позы кости (в пространстве родителя): вращение домножается слева, смещение прибавляется.
struct BoneDelta {
    int node = 0;
    Vec3 t;
    Quat r;
};

class Animator {
public:
    std::vector<BoneDelta> deltas;  // применяются после клипа, до расчёта матриц
    // Принудительные матрицы узлов в пространстве модели (рэгдолл): узлы с ovrHas[i] берут матрицу из ovrMat[i].
    const std::vector<Mat4>* ovrMat = nullptr;
    const std::vector<char>* ovrHas = nullptr;
    void setModel(const Model* m);
    // Переключиться на клип с плавным переходом (blend секунд).
    void play(int clip, float blend = 0.25f);
    void update(float dt);
    // Произвольный доступ по времени (для таймлайна): клип A в момент timeA, при w>0 плавно смешивается с клипом B.
    // clipA < 0 - поза покоя.
    void evaluate(int clipA, float timeA, int clipB = -1, float timeB = 0, float w = 0);

    int current() const { return cur_.clip; }
    float time() const { return cur_.time; }
    float duration() const;
    float speed = 1.f;
    bool paused = false;

    std::vector<Mat4> world;                  // мировые матрицы всех узлов
    std::vector<std::vector<Mat4>> jointMat;  // по скинам: world[joint] * invBind
    float travelSpeed() const;

private:
    struct State {
        int clip = -1;
        float time = 0;
    };
    const Model* model_ = nullptr;
    State cur_, prev_;
    float blendT_ = 1.f, blendDur_ = 0.25f;
    Pose pose_, poseA_;

    void restPose(Pose& p) const;
    void sample(const Clip& c, float time, Pose& p) const;
    void advance(State& s, float dt) const;
    void computeMatrices();
};

}  // namespace eng
