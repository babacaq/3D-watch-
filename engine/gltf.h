// Загрузчик glTF 2.0: .glb и .gltf (внешние .bin/текстуры через FileReader, data: URI).
// Поддержка: меши (triangles/strip/fan), скины, иерархия, анимации (T/R/S, LINEAR/STEP; CUBICSPLINE упрощается),
// baseColorFactor/baseColorTexture, TEXCOORD_0, COLOR_0, alphaMode, doubleSided, sparse-аксессоры не поддерживаются.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "importer.h"
#include "model.h"

namespace eng {

std::unique_ptr<Model> loadGlb(const uint8_t* data, size_t size, const std::string& name, std::string* err,
                               const FileReader& reader = nullptr);
std::unique_ptr<Model> loadGltf(const uint8_t* json, size_t size, const std::string& name, std::string* err,
                                const FileReader& reader = nullptr);

}  // namespace eng
