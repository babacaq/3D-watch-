// Декодеры изображений без внешних библиотек (кроме zlib для PNG): PNG, JPEG (baseline+progressive), BMP, TGA.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace eng {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;  // RGBA8, строка 0 = верх изображения
};

constexpr int kMaxImageSide = 16384;
constexpr size_t kMaxImageBytes = 200u * 1024u * 1024u;

// Формат определяется по сигнатуре (PNG/JPEG/BMP; TGA - по правдоподобному заголовку).
// Не бросает исключений и не падает на битых данных: возвращает false либо частично декодированное изображение.
bool decodeImage(const uint8_t* data, size_t size, Image& out, std::string* err = nullptr);

// Уменьшает изображение (box-фильтр с учётом альфы) так, чтобы большая сторона была <= maxDim.
void downscaleToMax(Image& img, int maxDim);

}  // namespace eng
