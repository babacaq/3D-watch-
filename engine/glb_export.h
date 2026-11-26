// Экспорт скелетной анимации в GLB (glTF 2.0) для игровых движков (Godot 4, Unity, Unreal): модель + скелет + запечённая анимация.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "scene.h"

namespace eng {

struct GlbOptions {
    int f0 = 1, f1 = 120;     // диапазон кадров
    int fps = 24;
    bool mesh = true;         // сетка, кожа, материалы и текстуры (иначе только скелет и анимация)
    bool rootMotion = true;   // узел Root с движением объекта по сцене; иначе модель в исходном масштабе на месте
    std::string animName = "PrismLab";
};

// Каждый кадр диапазона запекается из текущей сцены: клипы, ключи костей и рэгдолл. Ткань (вершинная) в glTF не попадает.
// Сцена временно перематывается и возвращается на прежний кадр.
bool exportSkeletalGlb(Scene& sc, int objectId, const GlbOptions& opt, std::vector<uint8_t>& out, std::string& err);

}  // namespace eng
