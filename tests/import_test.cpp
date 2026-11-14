// Проверка импортёров без GL. Сборка: g++ -std=c++17 -g -fsanitize=address,undefined -Iengine tests/import_test.cpp engine/{importer,gltf,image,model}.cpp -lz
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "importer.h"

using namespace eng;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

static bool readFile(const std::string& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

static std::unique_ptr<Model> load(const std::string& dir, const std::string& name, std::string& err) {
    std::vector<uint8_t> d;
    if (!readFile(dir + name, d)) { err = "no file"; return nullptr; }
    FileReader rd = [dir](const std::string& rel, std::vector<uint8_t>& o) { return readFile(dir + rel, o); };
    return importModel(name, d.data(), d.size(), rd, &err);
}

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "tests/assets/";
    if (dir.back() != '/') dir += '/';
    std::string err;

    auto glb = load(dir, "test_skinned.glb", err);
    CHECK(glb, "glb: %s", err.c_str());
    if (glb) {
        CHECK(glb->meshes.size() == 1 && glb->meshes[0].indices.size() == 72, "glb mesh");
        CHECK(glb->skins.size() == 1 && glb->skins[0].joints.size() == 2, "glb skin");
        CHECK(glb->clips.size() == 1 && glb->clips[0].name == "Wave", "glb clip");
        CHECK(glb->textures.size() == 1 && glb->textures[0].w == 32 && glb->textures[0].h == 32, "glb texture %zu", glb->textures.size());
        CHECK(glb->bmax.y > 1.99f && glb->bmin.y < 0.01f, "glb bounds %f %f", glb->bmin.y, glb->bmax.y);
        if (!glb->textures.empty()) {
            const auto& t = glb->textures[0];
            CHECK(t.rgba.size() == 32 * 32 * 4 && t.rgba[0] == 230 && t.rgba[2] == 60, "png pixel %d %d %d", t.rgba[0], t.rgba[1], t.rgba[2]);
        }
    }
    auto gl = load(dir, "test_cube.gltf", err);
    CHECK(gl, "gltf: %s", err.c_str());
    if (gl) CHECK(gl->triangles() == 12, "gltf tris %zu", gl->triangles());
    auto obj = load(dir, "test_box.obj", err);
    CHECK(obj, "obj: %s", err.c_str());
    if (obj) {
        CHECK(obj->triangles() == 12, "obj tris %zu", obj->triangles());
        CHECK(obj->textures.size() == 1, "obj textures %zu", obj->textures.size());
        CHECK(obj->meshes.size() >= 1 && obj->meshes[0].texture == 0, "obj mat texture");
    }
    auto stl = load(dir, "test_tri.stl", err);
    CHECK(stl, "stl: %s", err.c_str());
    if (stl) CHECK(stl->triangles() == 2, "stl tris %zu", stl->triangles());

    // неподдерживаемые и битые входы не должны падать
    std::vector<uint8_t> junk = {1, 2, 3};
    err.clear();
    CHECK(!importModel("x.fbx", junk.data(), junk.size(), nullptr, &err) && !err.empty(), "fbx must fail with message");
    err.clear();
    CHECK(!importModel("x.glb", junk.data(), junk.size(), nullptr, &err), "junk glb");

    // фаззинг: порча байтов валидных файлов
    std::mt19937 rng(1234);
    const char* files[] = {"test_skinned.glb", "test_cube.gltf", "test_box.obj", "test_tri.stl", "test_box.png"};
    for (const char* fn : files) {
        std::vector<uint8_t> orig;
        if (!readFile(dir + fn, orig) || orig.empty()) continue;
        std::string name = fn;
        for (int it = 0; it < 400; it++) {
            std::vector<uint8_t> d = orig;
            int muts = 1 + (int)(rng() % 6);
            for (int m = 0; m < muts; m++) d[rng() % d.size()] = (uint8_t)rng();
            if (rng() % 4 == 0) d.resize(rng() % d.size());
            std::string e2;
            FileReader rd = [&](const std::string& rel, std::vector<uint8_t>& o) { return readFile(dir + rel, o); };
            if (name.size() > 4 && name.substr(name.size() - 4) == ".png") {
                Model m; addTextureFromMemory(m, d.data(), d.size(), &e2);
            } else importModel(name, d.data(), d.size(), rd, &e2);
        }
    }
    // изображения: BMP / TGA вручную
    {
        std::vector<uint8_t> bmp(54 + 4 * 3, 0);
        bmp[0] = 'B'; bmp[1] = 'M'; bmp[10] = 54; bmp[14] = 40; bmp[18] = 2; bmp[22] = 2; bmp[26] = 1; bmp[28] = 24;
        uint8_t px[12] = {0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255};  // BGR, строки по 8 байт (6+2 паддинг)
        std::vector<uint8_t> body = {0, 0, 255, 0, 255, 0, 0, 0, 255, 0, 0, 255, 255, 255, 0, 0};
        bmp.resize(54); bmp.insert(bmp.end(), body.begin(), body.end());
        (void)px;
        Model m; std::string e;
        int ti = addTextureFromMemory(m, bmp.data(), bmp.size(), &e);
        CHECK(ti == 0 && m.textures[0].w == 2 && m.textures[0].h == 2, "bmp decode: %s", e.c_str());
        if (ti == 0) {  // BMP хранится снизу вверх: верхний левый пиксель - синий, нижний левый - красный
            const auto& px2 = m.textures[0].rgba;
            CHECK(px2[0] == 0 && px2[2] == 255, "bmp top-left should be blue: %d %d %d", px2[0], px2[1], px2[2]);
            CHECK(px2[8] == 255 && px2[10] == 0, "bmp bottom-left should be red: %d %d %d", px2[8], px2[9], px2[10]);
        }
        std::vector<uint8_t> tga = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 24, 0x20, 10, 20, 30};
        Model m2; std::string e2;
        int t2 = addTextureFromMemory(m2, tga.data(), tga.size(), &e2);
        CHECK(t2 == 0 && m2.textures[0].rgba[0] == 30 && m2.textures[0].rgba[2] == 10, "tga decode: %s", e2.c_str());
    }
    printf(fails ? "IMPORT TESTS FAILED: %d\n" : "import tests OK\n", fails);
    return fails ? 1 : 0;
}
