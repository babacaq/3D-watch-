// Импорт моделей: .glb .gltf .obj .stl (+ вспомогательные функции для отдельных загрузчиков).
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "model.h"

namespace eng {

// Читает файл по пути, относительному каталогу файла модели (соседние .bin/.mtl/текстуры).
// Может быть пустым (nullptr): тогда внешние файлы недоступны.
using FileReader = std::function<bool(const std::string& relPath, std::vector<uint8_t>& out)>;

// Единая точка входа. Диспетчеризация по расширению + проверка сигнатуры. Возвращает nullptr и err при ошибке.
std::unique_ptr<Model> importModel(const std::string& fileName, const uint8_t* data, size_t size,
                                   const FileReader& reader, std::string* err);
// По расширению (без учёта регистра): .glb .gltf .obj .stl
bool isSupportedModelFile(const std::string& fileName);

// ---- отдельные форматы (обычно вызываются через importModel) ----
std::unique_ptr<Model> importObj(const std::string& name, const uint8_t* data, size_t size, const FileReader& reader,
                                 std::string* err);
std::unique_ptr<Model> importStl(const std::string& name, const uint8_t* data, size_t size, std::string* err);
// true, если данные похожи на бинарный STL (по размеру).
bool looksLikeBinaryStl(const uint8_t* data, size_t size);

// ---- вспомогательное для загрузчиков ----
constexpr int kMaxTextureDim = 2048;
// Декодирует картинку, ограничивает до kMaxTextureDim, добавляет в model.textures. Возвращает индекс или -1.
int addTextureFromMemory(Model& model, const uint8_t* data, size_t size, std::string* err = nullptr);
// Сглаженные нормали (взвешенные по площади; вершины с одной позицией объединяются).
// onlyZero=true: записать только в вершины с нулевой нормалью.
void computeSmoothNormals(MeshData& m, bool onlyZero);
// Убирает циклы в иерархии узлов (parent=-1 для узла, замыкающего цикл).
void breakNodeCycles(Model& model);

}  // namespace eng
