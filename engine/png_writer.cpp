#include "png_writer.h"

#include <zlib.h>

#include <cstring>

namespace eng {

static void put32(std::vector<uint8_t>& o, uint32_t v) {
    o.push_back(v >> 24); o.push_back(v >> 16); o.push_back(v >> 8); o.push_back(v);
}
static void chunk(std::vector<uint8_t>& o, const char* type, const uint8_t* d, size_t n) {
    put32(o, (uint32_t)n);
    size_t start = o.size();
    o.insert(o.end(), type, type + 4);
    if (n) o.insert(o.end(), d, d + n);
    uLong crc = crc32(0, o.data() + start, (uInt)(n + 4));
    put32(o, (uint32_t)crc);
}

bool encodePNG(const uint8_t* rgba, int w, int h, std::vector<uint8_t>& out, bool dropAlpha) {
    if (w <= 0 || h <= 0 || !rgba) return false;
    int bpp = dropAlpha ? 3 : 4;
    size_t stride = (size_t)w * bpp;
    std::vector<uint8_t> raw((stride + 1) * h);
    for (int y = 0; y < h; y++) {
        uint8_t* row = &raw[(stride + 1) * y];
        row[0] = 0;
        const uint8_t* src = rgba + (size_t)y * w * 4;
        if (dropAlpha) for (int x = 0; x < w; x++) { row[1 + x * 3] = src[x * 4]; row[2 + x * 3] = src[x * 4 + 1]; row[3 + x * 3] = src[x * 4 + 2]; }
        else std::memcpy(row + 1, src, stride);
    }
    uLongf cl = compressBound((uLong)raw.size());
    std::vector<uint8_t> comp(cl);
    if (compress2(comp.data(), &cl, raw.data(), (uLong)raw.size(), 6) != Z_OK) return false;
    out.clear();
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    out.insert(out.end(), sig, sig + 8);
    uint8_t ihdr[13];
    ihdr[0] = w >> 24; ihdr[1] = w >> 16; ihdr[2] = w >> 8; ihdr[3] = w;
    ihdr[4] = h >> 24; ihdr[5] = h >> 16; ihdr[6] = h >> 8; ihdr[7] = h;
    ihdr[8] = 8; ihdr[9] = dropAlpha ? 2 : 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(out, "IHDR", ihdr, 13);
    chunk(out, "IDAT", comp.data(), cl);
    chunk(out, "IEND", nullptr, 0);
    return true;
}

}  // namespace eng
