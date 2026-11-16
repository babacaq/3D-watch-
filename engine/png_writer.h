// Кодирование PNG (RGBA8, без чересстрочности) через zlib.
#pragma once
#include <cstdint>
#include <vector>

namespace eng {
// rgba: строки сверху вниз, w*h*4 байт
bool encodePNG(const uint8_t* rgba, int w, int h, std::vector<uint8_t>& out, bool dropAlpha = true);
}  // namespace eng
