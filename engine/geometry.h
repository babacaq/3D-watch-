// Генераторы геометрии: коробки, сферы, плавно скинованные трубки.
#pragma once
#include <functional>
#include <vector>

#include "model.h"

namespace eng {

struct Color {
    float r, g, b, a;
    Color(float r_ = 1, float g_ = 1, float b_ = 1, float a_ = 1) : r(r_), g(g_), b(b_), a(a_) {}
};

// bone < 0: статичная вершина (вес 1 на кость 0, mesh.skin должен быть -1).
void addBox(MeshData& m, Vec3 center, Vec3 half, Color col, int bone);
void addSphere(MeshData& m, Vec3 center, Vec3 radii, Color col, int bone, int segs = 18, int rings = 12);
// Прямой цилиндр/конус между двумя точками (жёстко привязан к кости).
void addCylinder(MeshData& m, Vec3 p0, Vec3 p1, float r0, float r1, Color col, int bone, int sides = 14,
                 bool caps = true);

// Трубка вдоль ломаной pts. bones[i] - кость, чей pivot лежит в pts[i].
// Вокруг каждого сустава (±blend по длине) веса плавно переходят между костями.
struct TubeDesc {
    std::vector<Vec3> pts;
    std::vector<int> bones;
    std::vector<float> radii;
    float blend = 0.06f;  // полуширина зоны сглаживания у сустава
    float step = 0.04f;   // шаг колец по длине
    int sides = 14;
    int capRings = 4;
    bool capStart = true, capEnd = true;
    Color color;
    std::function<Color(float)> colorFn;  // необязательно: цвет по длине L
};
void addTube(MeshData& m, const TubeDesc& d);

// Доля треугольников, у которых геометрическая нормаль совпадает с нормалью вершин (для самопроверки).
float windingScore(const MeshData& m);

}  // namespace eng
