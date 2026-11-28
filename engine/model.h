// Данные модели: узлы, меши, скины, анимационные клипы.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "gl_api.h"
#include "math3d.h"

namespace eng {

constexpr int kMaxJoints = 64;  // лимит костей на один скин (uniform-массив в шейдере)

struct Vertex {
    float p[3];
    float n[3];
    float uv[2];   // текстурные координаты (v вниз, как в glTF; для OBJ нужно инвертировать v)
    uint8_t c[4];  // цвет вершины (RGBA, нормализованный); умножается на текстуру
    uint8_t j[4];  // индексы костей
    float w[4];    // веса костей
};
static_assert(sizeof(Vertex) == 56, "Vertex layout");

// Текстура на CPU (RGBA8) + GL-объект.
struct Texture {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    bool srgb = false;  // цветовая текстура (база/свечение) хранится в sRGB
    GLuint id = 0;
    void upload();
    void release();
};

struct MeshData {
    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;
    int skin = -1;  // индекс скина или -1 для статичного меша
    float shininess = 32.f;
    bool doubleSided = false;
    int texture = -1;        // индекс в Model::textures или -1 (только цвет вершин)
    int alphaMode = 0;       // 0 непрозрачный, 1 mask (alphaCutoff), 2 blend
    float alphaCutoff = 0.5f;
    // PBR (metallic-roughness)
    float metallic = 0.f, roughness = 0.6f, normalScale = 1.f;
    float emissive[3] = {0, 0, 0};
    int texMR = -1;        // G = roughness, B = metallic
    int texNormal = -1;
    int texEmissive = -1;
    int texOcclusion = -1; // R
    std::string name;
    GLuint vao = 0, vbo = 0, ibo = 0;
    bool dynamic = false;  // вершины меняются каждый кадр (ткань): буфер GL_DYNAMIC_DRAW
    bool stale = false;    // вершины на CPU новее, чем в GL-буфере
    bool modelSpace = false;  // вершины уже в пространстве модели (ткань на скелетном меше): узел меша не применяется

    void upload();
    void updateVerts();    // залить вершины в GL-буфер, если они изменились
    void release();
    void draw() const;
    size_t triangles() const { return indices.size() / 3; }
};

struct Node {
    std::string name;
    int parent = -1;
    Vec3 t;
    Quat r;
    Vec3 s{1, 1, 1};
    std::vector<int> meshes;
    int skin = -1;
};

struct Skin {
    std::vector<int> joints;    // индексы узлов
    std::vector<Mat4> invBind;  // обратные bind-матрицы
};

enum class Path { Translation = 0, Rotation = 1, Scale = 2 };

struct Channel {
    int node = 0;
    Path path = Path::Translation;
    bool step = false;
    std::vector<float> times;
    std::vector<float> values;  // 3 или 4 float на ключ
};

struct Clip {
    std::string name;
    float duration = 0;
    bool loop = true;
    float travel = 0;  // м/с: скорость "бега" земли под моделью
    std::vector<Channel> channels;
};

struct Model {
    std::string name;
    std::vector<Node> nodes;
    std::vector<int> order;  // родители раньше детей
    std::vector<MeshData> meshes;
    std::vector<Skin> skins;
    std::vector<Clip> clips;
    std::vector<Texture> textures;
    Vec3 bmin{0, 0, 0}, bmax{0, 1, 0};
    bool uploaded = false;

    void finalize();  // порядок обхода + границы в bind-позе
    void upload();
    void release();
    size_t triangles() const;
    size_t jointCount() const;
    Vec3 center() const { return (bmin + bmax) * 0.5f; }
    float radius() const { return length(bmax - bmin) * 0.5f; }
};

}  // namespace eng
