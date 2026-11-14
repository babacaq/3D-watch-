// Декодеры PNG / JPEG / BMP / TGA. Все декодеры устойчивы к битым данным (проверки границ, лимиты размеров).
#include "image.h"

#include <zlib.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <exception>

namespace eng {
namespace {

bool setErr(std::string* e, const char* m) {
    if (e) *e = m;
    return false;
}

bool checkDims(int64_t w, int64_t h, std::string* err) {
    if (w <= 0 || h <= 0) return setErr(err, "image has zero size");
    if (w > kMaxImageSide || h > kMaxImageSide) return setErr(err, "image too large (side > 16384)");
    if ((uint64_t)w * (uint64_t)h * 4u > kMaxImageBytes) return setErr(err, "image too large (> 200 MB)");
    return true;
}

bool allocImage(Image& out, int w, int h, std::string* err) {
    if (!checkDims(w, h, err)) return false;
    out.w = w;
    out.h = h;
    out.rgba.assign((size_t)w * h * 4, 0);
    return true;
}

inline uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
inline uint32_t le32(const uint8_t* p) { return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0]; }
inline uint32_t le16(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
inline uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// ============================================================ PNG
struct PngPass { int x0, y0, dx, dy; };
const PngPass kAdam7[7] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};

bool decodePng(const uint8_t* d, size_t n, Image& out, std::string* err) {
    if (n < 33 || std::memcmp(d, "\x89PNG\r\n\x1a\n", 8) != 0) return setErr(err, "not a PNG");
    size_t pos = 8;
    int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0;
    bool haveHdr = false;
    uint8_t pal[256 * 3];
    uint8_t palA[256];
    std::memset(pal, 0, sizeof pal);
    std::memset(palA, 255, sizeof palA);
    int palN = 0;
    bool haveTrnsKey = false;
    unsigned trnsKey[3] = {0, 0, 0};
    std::vector<uint8_t> idat;
    while (pos + 8 <= n) {
        uint32_t len = be32(d + pos);
        const uint8_t* type = d + pos + 4;
        size_t body = pos + 8;
        size_t avail = n - body;
        bool trunc = len > avail;
        size_t clen = trunc ? avail : len;
        if (!std::memcmp(type, "IHDR", 4)) {
            if (clen < 13) return setErr(err, "bad IHDR");
            w = (int)std::min<uint32_t>(be32(d + body), 1u << 30);
            h = (int)std::min<uint32_t>(be32(d + body + 4), 1u << 30);
            depth = d[body + 8];
            ctype = d[body + 9];
            interlace = d[body + 12];
            haveHdr = true;
        } else if (!std::memcmp(type, "PLTE", 4)) {
            palN = (int)std::min<size_t>(clen / 3, 256);
            std::memcpy(pal, d + body, (size_t)palN * 3);
        } else if (!std::memcmp(type, "tRNS", 4)) {
            if (!haveHdr) return setErr(err, "tRNS before IHDR");
            if (ctype == 3) {
                for (size_t i = 0; i < clen && i < 256; i++) palA[i] = d[body + i];
            } else if (ctype == 0 && clen >= 2) {
                haveTrnsKey = true;
                trnsKey[0] = (d[body] << 8) | d[body + 1];
            } else if (ctype == 2 && clen >= 6) {
                haveTrnsKey = true;
                for (int i = 0; i < 3; i++) trnsKey[i] = (d[body + i * 2] << 8) | d[body + i * 2 + 1];
            }
        } else if (!std::memcmp(type, "IDAT", 4)) {
            if (idat.size() + clen > ((size_t)1 << 31)) return setErr(err, "PNG data too large");
            idat.insert(idat.end(), d + body, d + body + clen);
        } else if (!std::memcmp(type, "IEND", 4)) {
            break;
        }
        if (trunc) break;
        pos = body + len + 4;  // + CRC
    }
    if (!haveHdr) return setErr(err, "PNG without IHDR");
    int chans;
    switch (ctype) {
        case 0: chans = 1; if (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16) chans = 0; break;
        case 2: chans = (depth == 8 || depth == 16) ? 3 : 0; break;
        case 3: chans = (depth == 1 || depth == 2 || depth == 4 || depth == 8) ? 1 : 0; break;
        case 4: chans = (depth == 8 || depth == 16) ? 2 : 0; break;
        case 6: chans = (depth == 8 || depth == 16) ? 4 : 0; break;
        default: chans = 0;
    }
    if (!chans) return setErr(err, "bad PNG colour type / bit depth");
    if (interlace > 1) return setErr(err, "bad PNG interlace");
    if (!checkDims(w, h, err)) return false;
    if (ctype == 3 && palN == 0) return setErr(err, "PNG palette missing");
    const int bitsPP = chans * depth;
    const int bpp = (bitsPP + 7) / 8;

    // размеры проходов
    struct PassInfo { int pw, ph; size_t rowBytes; size_t off; };
    PassInfo pi[7];
    int npass = interlace ? 7 : 1;
    size_t total = 0;
    for (int p = 0; p < npass; p++) {
        if (interlace) {
            const PngPass& a = kAdam7[p];
            pi[p].pw = (w - a.x0 + a.dx - 1) / a.dx;
            pi[p].ph = (h - a.y0 + a.dy - 1) / a.dy;
            if (w <= a.x0) pi[p].pw = 0;
            if (h <= a.y0) pi[p].ph = 0;
        } else {
            pi[p].pw = w;
            pi[p].ph = h;
        }
        pi[p].rowBytes = (pi[p].pw > 0 && pi[p].ph > 0) ? ((size_t)pi[p].pw * bitsPP + 7) / 8 : 0;
        pi[p].off = total;
        if (pi[p].rowBytes) total += (pi[p].rowBytes + 1) * (size_t)pi[p].ph;
    }
    if (total > ((size_t)1 << 30)) return setErr(err, "PNG too large");
    std::vector<uint8_t> raw(total, 0);

    z_stream zs;
    std::memset(&zs, 0, sizeof zs);
    if (inflateInit(&zs) != Z_OK) return setErr(err, "zlib init failed");
    size_t inPos = 0, outPos = 0;
    int zr = Z_OK;
    while (outPos < total && zr == Z_OK) {
        if (zs.avail_in == 0) {
            if (inPos >= idat.size()) break;
            size_t chunk = std::min<size_t>(idat.size() - inPos, 1u << 30);
            zs.next_in = idat.data() + inPos;
            zs.avail_in = (uInt)chunk;
            inPos += chunk;
        }
        zs.next_out = raw.data() + outPos;
        zs.avail_out = (uInt)std::min<size_t>(total - outPos, 1u << 30);
        size_t before = zs.avail_out;
        zr = inflate(&zs, Z_NO_FLUSH);
        outPos += before - zs.avail_out;
        if (zr == Z_BUF_ERROR) break;
    }
    inflateEnd(&zs);
    if (outPos == 0 && total > 0) return setErr(err, "PNG: no image data");

    if (!allocImage(out, w, h, err)) return false;
    // фон для недекодированной части: непрозрачный чёрный
    for (size_t i = 3; i < out.rgba.size(); i += 4) out.rgba[i] = 255;

    std::vector<uint8_t> zeroRow;
    const unsigned maxv = (1u << (depth < 16 ? depth : 8)) - 1;
    for (int p = 0; p < npass; p++) {
        if (!pi[p].rowBytes) continue;
        const size_t rb = pi[p].rowBytes;
        zeroRow.assign(rb, 0);
        const uint8_t* prev = zeroRow.data();
        for (int y = 0; y < pi[p].ph; y++) {
            uint8_t* line = raw.data() + pi[p].off + (size_t)y * (rb + 1);
            uint8_t ft = line[0];
            uint8_t* row = line + 1;
            switch (ft) {
                case 1: for (size_t i = bpp; i < rb; i++) row[i] = (uint8_t)(row[i] + row[i - bpp]); break;
                case 2: for (size_t i = 0; i < rb; i++) row[i] = (uint8_t)(row[i] + prev[i]); break;
                case 3:
                    for (size_t i = 0; i < rb; i++) {
                        int a = i >= (size_t)bpp ? row[i - bpp] : 0;
                        row[i] = (uint8_t)(row[i] + ((a + prev[i]) >> 1));
                    }
                    break;
                case 4:
                    for (size_t i = 0; i < rb; i++) {
                        int a = i >= (size_t)bpp ? row[i - bpp] : 0, b = prev[i], c = i >= (size_t)bpp ? prev[i - bpp] : 0;
                        int pp = a + b - c, pa = std::abs(pp - a), pb = std::abs(pp - b), pc = std::abs(pp - c);
                        int pr = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                        row[i] = (uint8_t)(row[i] + pr);
                    }
                    break;
                default: break;
            }
            prev = row;
            // раскладка пикселей
            int oy, ox0, dx;
            if (interlace) { oy = kAdam7[p].y0 + y * kAdam7[p].dy; ox0 = kAdam7[p].x0; dx = kAdam7[p].dx; }
            else { oy = y; ox0 = 0; dx = 1; }
            uint8_t* o = out.rgba.data() + ((size_t)oy * w + ox0) * 4;
            const size_t ostep = (size_t)dx * 4;
            const int pw = pi[p].pw;
            if (depth == 8 && ctype == 6) {
                for (int x = 0; x < pw; x++, o += ostep) std::memcpy(o, row + (size_t)x * 4, 4);
                continue;
            }
            for (int x = 0; x < pw; x++, o += ostep) {
                uint8_t r, g, b, a = 255;
                if (depth < 8) {
                    size_t bit = (size_t)x * depth;
                    unsigned s = (row[bit >> 3] >> (8 - depth - (bit & 7))) & maxv;
                    if (ctype == 3) {
                        if ((int)s >= palN) { r = g = b = 0; }
                        else { r = pal[s * 3]; g = pal[s * 3 + 1]; b = pal[s * 3 + 2]; }
                        a = palA[s];
                    } else {
                        r = g = b = (uint8_t)(s * 255 / maxv);
                        if (haveTrnsKey && s == trnsKey[0]) a = 0;
                    }
                } else if (depth == 8) {
                    switch (ctype) {
                        case 0: r = g = b = row[x]; if (haveTrnsKey && row[x] == trnsKey[0]) a = 0; break;
                        case 2:
                            r = row[x * 3]; g = row[x * 3 + 1]; b = row[x * 3 + 2];
                            if (haveTrnsKey && r == trnsKey[0] && g == trnsKey[1] && b == trnsKey[2]) a = 0;
                            break;
                        case 3: {
                            unsigned s = row[x];
                            if ((int)s >= palN) { r = g = b = 0; }
                            else { r = pal[s * 3]; g = pal[s * 3 + 1]; b = pal[s * 3 + 2]; }
                            a = palA[s];
                            break;
                        }
                        default: r = g = b = row[x * 2]; a = row[x * 2 + 1]; break;  // 4
                    }
                } else {  // 16 бит: берём старший байт
                    const uint8_t* q = row + (size_t)x * chans * 2;
                    switch (ctype) {
                        case 0: r = g = b = q[0]; if (haveTrnsKey && (unsigned)((q[0] << 8) | q[1]) == trnsKey[0]) a = 0; break;
                        case 2:
                            r = q[0]; g = q[2]; b = q[4];
                            if (haveTrnsKey && (unsigned)((q[0] << 8) | q[1]) == trnsKey[0] &&
                                (unsigned)((q[2] << 8) | q[3]) == trnsKey[1] && (unsigned)((q[4] << 8) | q[5]) == trnsKey[2])
                                a = 0;
                            break;
                        case 4: r = g = b = q[0]; a = q[2]; break;
                        default: r = q[0]; g = q[2]; b = q[4]; a = q[6]; break;  // 6
                    }
                }
                o[0] = r; o[1] = g; o[2] = b; o[3] = a;
            }
        }
    }
    return true;
}

// ============================================================ BMP
bool decodeBmp(const uint8_t* d, size_t n, Image& out, std::string* err) {
    if (n < 26 || d[0] != 'B' || d[1] != 'M') return setErr(err, "not a BMP");
    uint32_t offBits = le32(d + 10), dib = le32(d + 14);
    int64_t w, h;
    int bpp, comp = 0;
    uint32_t clrUsed = 0;
    if (dib == 12) {
        w = (int16_t)le16(d + 18);
        h = (int16_t)le16(d + 20);
        bpp = (int)le16(d + 24);
    } else if (dib >= 40 && n >= 14 + 40) {
        w = (int32_t)le32(d + 18);
        h = (int32_t)le32(d + 22);
        bpp = (int)le16(d + 28);
        comp = (int)le32(d + 30);
        clrUsed = le32(d + 46);
    } else {
        return setErr(err, "unsupported BMP header");
    }
    bool topDown = h < 0;
    if (h < 0) h = -h;
    if (comp != 0 && comp != 3 && comp != 6) return setErr(err, "compressed BMP not supported");
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) return setErr(err, "unsupported BMP bit depth");
    if (!checkDims(w, h, err)) return false;
    // палитра
    uint32_t pal[256];
    std::memset(pal, 0, sizeof pal);
    int palN = 0;
    if (bpp <= 8) {
        palN = clrUsed ? (int)std::min<uint32_t>(clrUsed, 256) : (1 << bpp);
        if (palN > (1 << bpp)) palN = 1 << bpp;
        size_t pp = 14 + (size_t)dib, es = dib == 12 ? 3 : 4;
        for (int i = 0; i < palN; i++) {
            size_t o = pp + (size_t)i * es;
            if (o + es > n) break;
            pal[i] = (uint32_t)d[o + 2] | ((uint32_t)d[o + 1] << 8) | ((uint32_t)d[o] << 16);  // 0xBBGGRR из B,G,R
        }
    }
    // маски каналов
    uint32_t mask[4] = {0, 0, 0, 0};
    if (bpp == 16 || bpp == 32) {
        if (bpp == 32) { mask[0] = 0x00FF0000; mask[1] = 0x0000FF00; mask[2] = 0x000000FF; mask[3] = 0xFF000000; }
        else { mask[0] = 0x7C00; mask[1] = 0x03E0; mask[2] = 0x001F; }
        if ((comp == 3 || comp == 6) && dib >= 40 && n >= 14 + 52 + 0) {
            mask[0] = le32(d + 54); mask[1] = le32(d + 58); mask[2] = le32(d + 62);
            mask[3] = (dib >= 56 && n >= 14 + 56) ? le32(d + 66) : 0;
            if (dib == 40 && comp == 3) mask[3] = 0;
        }
    }
    if (offBits >= n) return setErr(err, "BMP pixel data missing");
    if (!allocImage(out, (int)w, (int)h, err)) return false;
    size_t stride = (size_t)(((uint64_t)w * bpp + 31) / 32) * 4;
    bool anyAlpha = false;
    for (int row = 0; row < h; row++) {
        int y = topDown ? row : (int)h - 1 - row;
        size_t ro = (size_t)offBits + (size_t)row * stride;
        uint8_t* o = out.rgba.data() + (size_t)y * w * 4;
        if (ro >= n) { for (int x = 0; x < w; x++) o[x * 4 + 3] = 255; continue; }
        size_t avail = n - ro;
        auto need = [&](size_t bytes) { return bytes <= avail; };
        for (int x = 0; x < w; x++, o += 4) {
            o[3] = 255;
            if (bpp <= 8) {
                size_t bit = (size_t)x * bpp;
                if (!need((bit >> 3) + 1)) continue;
                unsigned s = (d[ro + (bit >> 3)] >> (8 - bpp - (bit & 7))) & ((1u << bpp) - 1);
                uint32_t c = pal[s];
                o[0] = (uint8_t)(c >> 16); o[1] = (uint8_t)(c >> 8); o[2] = (uint8_t)c;
            } else if (bpp == 24) {
                if (!need((size_t)x * 3 + 3)) continue;
                const uint8_t* q = d + ro + (size_t)x * 3;
                o[0] = q[2]; o[1] = q[1]; o[2] = q[0];
            } else {
                size_t bs = bpp / 8;
                if (!need((size_t)x * bs + bs)) continue;
                const uint8_t* q = d + ro + (size_t)x * bs;
                uint32_t v = bpp == 32 ? le32(q) : le16(q);
                for (int c = 0; c < 3; c++) {
                    uint32_t m = mask[c];
                    if (!m) continue;
                    int sh = __builtin_ctz(m);
                    uint32_t bits = __builtin_popcount(m), val = (v & m) >> sh;
                    o[c] = (uint8_t)(bits >= 8 ? val >> (bits - 8) : val * 255 / ((1u << bits) - 1));
                }
                if (mask[3]) {
                    int sh = __builtin_ctz(mask[3]);
                    uint32_t bits = __builtin_popcount(mask[3]), val = (v & mask[3]) >> sh;
                    o[3] = (uint8_t)(bits >= 8 ? val >> (bits - 8) : val * 255 / ((1u << bits) - 1));
                    if (o[3]) anyAlpha = true;
                }
            }
        }
    }
    if (bpp == 32 && mask[3] && !anyAlpha)  // XRGB с нулями в альфа-байте
        for (size_t i = 3; i < out.rgba.size(); i += 4) out.rgba[i] = 255;
    return true;
}

// ============================================================ TGA
bool tgaPlausible(const uint8_t* d, size_t n) {
    if (n < 18) return false;
    int cmapType = d[1], type = d[2], bpp = d[16];
    int w = (int)le16(d + 12), h = (int)le16(d + 14);
    if (w == 0 || h == 0) return false;
    if (cmapType > 1) return false;
    if (type != 1 && type != 2 && type != 3 && type != 9 && type != 10 && type != 11) return false;
    if ((type == 1 || type == 9) && cmapType != 1) return false;
    if (cmapType == 1) {
        int es = d[7];
        if (es != 15 && es != 16 && es != 24 && es != 32) return false;
    }
    if (type == 2 || type == 10) return bpp == 15 || bpp == 16 || bpp == 24 || bpp == 32;
    if (type == 3 || type == 11) return bpp == 8 || bpp == 16;
    return bpp == 8 || bpp == 16;
}

bool decodeTga(const uint8_t* d, size_t n, Image& out, std::string* err) {
    if (!tgaPlausible(d, n)) return setErr(err, "unknown image format");
    int idLen = d[0], type = d[2];
    int cmapFirst = (int)le16(d + 3), cmapLen = (int)le16(d + 5), cmapBits = d[7];
    int w = (int)le16(d + 12), h = (int)le16(d + 14), bpp = d[16], desc = d[17];
    if (!checkDims(w, h, err)) return false;
    bool rle = type >= 9;
    bool palType = (type == 1 || type == 9);
    bool grayType = (type == 3 || type == 11);
    size_t pos = 18 + (size_t)idLen;
    uint32_t pal[256];
    std::memset(pal, 0, sizeof pal);
    bool palAlpha = false;
    if (d[1] == 1) {
        size_t es = (cmapBits + 7) / 8;
        for (int i = 0; i < cmapLen; i++) {
            size_t o = pos + (size_t)i * es;
            if (o + es > n) break;
            int idx = cmapFirst + i;
            if (idx < 0 || idx > 255) continue;
            const uint8_t* q = d + o;
            uint32_t r, g, b, a = 255;
            if (es == 2) { uint32_t v = q[0] | (q[1] << 8); r = ((v >> 10) & 31) * 255 / 31; g = ((v >> 5) & 31) * 255 / 31; b = (v & 31) * 255 / 31; }
            else { b = q[0]; g = q[1]; r = q[2]; if (es == 4) { a = q[3]; palAlpha |= a != 0; } }
            pal[idx] = r | (g << 8) | (b << 16) | (a << 24);
        }
        pos += (size_t)cmapLen * es;
    }
    if (palType && bpp != 8) return setErr(err, "unsupported TGA palette depth");
    if (!allocImage(out, w, h, err)) return false;
    const size_t bs = (size_t)(bpp + 7) / 8;
    const size_t npix = (size_t)w * h;
    const bool alphaBits = (desc & 0x0F) != 0;
    const bool topFirst = (desc & 0x20) != 0, rightFirst = (desc & 0x10) != 0;
    size_t i = 0;  // индекс пикселя в порядке файла
    auto put = [&](size_t idx, const uint8_t* q) {
        size_t r = idx / (size_t)w, c = idx % (size_t)w;
        size_t y = topFirst ? r : (size_t)h - 1 - r;
        size_t x = rightFirst ? (size_t)w - 1 - c : c;
        uint8_t* o = out.rgba.data() + (y * w + x) * 4;
        if (palType) {
            uint32_t p = pal[q[0]];
            o[0] = (uint8_t)p; o[1] = (uint8_t)(p >> 8); o[2] = (uint8_t)(p >> 16); o[3] = palAlpha ? (uint8_t)(p >> 24) : 255;
        } else if (grayType) {
            o[0] = o[1] = o[2] = q[0];
            o[3] = bs == 2 ? q[1] : 255;
        } else if (bs == 2) {
            uint32_t v = q[0] | (q[1] << 8);
            o[0] = (uint8_t)(((v >> 10) & 31) * 255 / 31); o[1] = (uint8_t)(((v >> 5) & 31) * 255 / 31); o[2] = (uint8_t)((v & 31) * 255 / 31);
            o[3] = (alphaBits && !(v & 0x8000)) ? 0 : 255;
        } else {
            o[0] = q[2]; o[1] = q[1]; o[2] = q[0];
            o[3] = (bs == 4 && alphaBits) ? q[3] : 255;
        }
    };
    if (!rle) {
        for (; i < npix && pos + bs <= n; i++, pos += bs) put(i, d + pos);
    } else {
        while (i < npix && pos < n) {
            uint8_t hdr = d[pos++];
            size_t cnt = (size_t)(hdr & 0x7F) + 1;
            if (hdr & 0x80) {
                if (pos + bs > n) break;
                for (size_t k = 0; k < cnt && i < npix; k++, i++) put(i, d + pos);
                pos += bs;
            } else {
                for (size_t k = 0; k < cnt && i < npix; k++, i++, pos += bs) {
                    if (pos + bs > n) { i = npix; break; }
                    put(i, d + pos);
                }
            }
        }
    }
    return true;
}

// ============================================================ JPEG
const uint8_t kZigzag[64 + 16] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                                  41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                                  30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
                                  63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63};

struct JHuff {
    bool valid = false;
    uint16_t lut[512];
    int maxcode[18];
    int mincode[17];
    int valptr[17];
    uint8_t vals[256];
    int nvals = 0;

    bool build(const uint8_t* counts, const uint8_t* syms, int n) {
        std::memset(lut, 0, sizeof lut);
        nvals = n;
        std::memcpy(vals, syms, (size_t)n);
        int code = 0, k = 0;
        for (int len = 1; len <= 16; len++) {
            valptr[len] = k;
            mincode[len] = code;
            for (int i = 0; i < counts[len - 1]; i++, k++, code++) {
                if (len <= 9) {
                    int base = code << (9 - len), cnt = 1 << (9 - len);
                    if (base + cnt > 512) return false;
                    for (int j = 0; j < cnt; j++) lut[base + j] = (uint16_t)((len << 8) | syms[k]);
                }
            }
            if (code > (1 << len)) return false;
            maxcode[len] = counts[len - 1] ? code - 1 : -1;
            code <<= 1;
        }
        maxcode[17] = INT_MAX;
        valid = true;
        return true;
    }
};

struct BitReader {
    const uint8_t* p;
    const uint8_t* end;
    uint32_t acc = 0;
    int cnt = 0;
    bool marker = false;
    int pad = 0;
    void fill() {
        while (cnt <= 24) {
            uint32_t b = 0;
            if (!marker && p < end) {
                b = *p;
                if (b == 0xFF) {
                    const uint8_t* q = p + 1;
                    while (q < end && *q == 0xFF) q++;
                    if (q >= end) { p = end; b = 0; pad++; }
                    else if (*q == 0) { p = q + 1; }
                    else { marker = true; p = q - 1; b = 0; pad++; }
                } else {
                    p++;
                }
            } else {
                pad++;
            }
            acc |= b << (24 - cnt);
            cnt += 8;
        }
    }
    int bits(int n) {
        if (n == 0) return 0;
        if (cnt < n) fill();
        int v = (int)(acc >> (32 - n));
        acc <<= n;
        cnt -= n;
        return v;
    }
    int decode(const JHuff& h) {
        if (cnt < 16) fill();
        unsigned e = h.lut[acc >> 23];
        if (e) {
            int len = (int)(e >> 8);
            acc <<= len;
            cnt -= len;
            return (int)(e & 255);
        }
        unsigned v = acc >> 16;
        for (int len = 10; len <= 16; len++) {
            int code = (int)(v >> (16 - len));
            if (h.maxcode[len] >= 0 && code <= h.maxcode[len]) {
                int idx = h.valptr[len] + code - h.mincode[len];
                if (idx < 0 || idx >= h.nvals) return -1;
                acc <<= len;
                cnt -= len;
                return h.vals[idx];
            }
        }
        return -1;
    }
};

inline int extend(int v, int t) { return v < (1 << (t - 1)) ? v - (1 << t) + 1 : v; }

struct JComp {
    int id = 0, h = 1, v = 1, tq = 0;
    int bw = 0, bh = 0;  // выделенные блоки (с учётом MCU-паддинга)
    int td = 0, ta = 0;
    int pred = 0;
    bool qLatched = false;
    uint16_t q[64];
    std::vector<int16_t> coef;
    std::vector<uint8_t> plane;
};

inline uint8_t idctClamp(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

#define F2F(x) ((int)((x)*4096 + 0.5))
#define FSH(x) ((x)*4096)
#define IDCT_1D(s0, s1, s2, s3, s4, s5, s6, s7)                \
    int t0, t1, t2, t3, p1, p2, p3, p4, p5, x0, x1, x2, x3;    \
    p2 = s2; p3 = s6;                                          \
    p1 = (p2 + p3) * F2F(0.5411961f);                          \
    t2 = p1 + p3 * F2F(-1.847759065f);                         \
    t3 = p1 + p2 * F2F(0.765366865f);                          \
    p2 = s0; p3 = s4;                                          \
    t0 = FSH(p2 + p3);                                         \
    t1 = FSH(p2 - p3);                                         \
    x0 = t0 + t3; x3 = t0 - t3; x1 = t1 + t2; x2 = t1 - t2;   \
    t0 = s7; t1 = s5; t2 = s3; t3 = s1;                        \
    p3 = t0 + t2; p4 = t1 + t3; p1 = t0 + t3; p2 = t1 + t2;   \
    p5 = (p3 + p4) * F2F(1.175875602f);                        \
    t0 = t0 * F2F(0.298631336f);                               \
    t1 = t1 * F2F(2.053119869f);                               \
    t2 = t2 * F2F(3.072711026f);                               \
    t3 = t3 * F2F(1.501321110f);                               \
    p1 = p5 + p1 * F2F(-0.899976223f);                         \
    p2 = p5 + p2 * F2F(-2.562915447f);                         \
    p3 = p3 * F2F(-1.961570560f);                              \
    p4 = p4 * F2F(-0.390180644f);                              \
    t3 += p1 + p4; t2 += p2 + p3; t1 += p2 + p4; t0 += p1 + p3;

// Целочисленное ОДКП 8x8 (точный "islow"-вариант). Коэффициенты зажаты, чтобы переполнение int исключалось.
void idctBlock(uint8_t* out, int stride, const int16_t* coef, const uint16_t* q) {
    int data[64], val[64];
    for (int i = 0; i < 64; i++) {
        int v = (int)coef[i] * (int)q[i];
        data[i] = v < -2048 ? -2048 : (v > 2047 ? 2047 : v);
    }
    int* v = val;
    const int* d = data;
    for (int i = 0; i < 8; ++i, ++d, ++v) {
        if (d[8] == 0 && d[16] == 0 && d[24] == 0 && d[32] == 0 && d[40] == 0 && d[48] == 0 && d[56] == 0) {
            int dc = d[0] * 4;
            v[0] = v[8] = v[16] = v[24] = v[32] = v[40] = v[48] = v[56] = dc;
        } else {
            IDCT_1D(d[0], d[8], d[16], d[24], d[32], d[40], d[48], d[56])
            x0 += 512; x1 += 512; x2 += 512; x3 += 512;
            v[0] = (x0 + t3) >> 10;  v[56] = (x0 - t3) >> 10;
            v[8] = (x1 + t2) >> 10;  v[48] = (x1 - t2) >> 10;
            v[16] = (x2 + t1) >> 10; v[40] = (x2 - t1) >> 10;
            v[24] = (x3 + t0) >> 10; v[32] = (x3 - t0) >> 10;
        }
    }
    for (int i = 0; i < 64; i++) val[i] = val[i] < -16384 ? -16384 : (val[i] > 16384 ? 16384 : val[i]);
    uint8_t* o = out;
    for (int i = 0; i < 8; ++i, o += stride) {
        const int* vv = val + i * 8;
        IDCT_1D(vv[0], vv[1], vv[2], vv[3], vv[4], vv[5], vv[6], vv[7])
        x0 += 65536 + (128 << 17); x1 += 65536 + (128 << 17); x2 += 65536 + (128 << 17); x3 += 65536 + (128 << 17);
        o[0] = idctClamp((x0 + t3) >> 17); o[7] = idctClamp((x0 - t3) >> 17);
        o[1] = idctClamp((x1 + t2) >> 17); o[6] = idctClamp((x1 - t2) >> 17);
        o[2] = idctClamp((x2 + t1) >> 17); o[5] = idctClamp((x2 - t1) >> 17);
        o[3] = idctClamp((x3 + t0) >> 17); o[4] = idctClamp((x3 - t0) >> 17);
    }
}

inline int floorDiv(int a, int b) { int q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }

struct UpTable {
    std::vector<int> i0, i1, wt;  // для каждого выходного индекса
    void build(int outN, int num, int den, int srcN) {  // src = (x+0.5)*num/den - 0.5
        i0.resize(outN); i1.resize(outN); wt.resize(outN);
        for (int x = 0; x < outN; x++) {
            int pos = floorDiv(((2 * x + 1) * num - den) * 128, den);
            int a = pos >> 8, f = pos & 255, b = a + 1;
            a = std::min(std::max(a, 0), srcN - 1);
            b = std::min(std::max(b, 0), srcN - 1);
            i0[x] = a; i1[x] = b; wt[x] = f;
        }
    }
};

bool decodeJpeg(const uint8_t* d, size_t n, Image& out, std::string* err) {
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return setErr(err, "not a JPEG");
    uint16_t qt[4][64];
    bool qtValid[4] = {false, false, false, false};
    JHuff dcT[4], acT[4];
    JComp comps[3];
    int nc = 0, W = 0, H = 0, hmax = 1, vmax = 1, mcux = 0, mcuy = 0;
    bool haveSof = false, progressive = false, anyScan = false;
    int ri = 0, adobe = -1;
    size_t pos = 2;

    while (pos < n) {
        while (pos < n && d[pos] != 0xFF) pos++;
        while (pos < n && d[pos] == 0xFF) pos++;
        if (pos >= n) break;
        int m = d[pos++];
        if (m == 0 || m == 0x01 || (m >= 0xD0 && m <= 0xD8)) continue;
        if (m == 0xD9) break;
        if (pos + 2 > n) break;
        size_t len = ((size_t)d[pos] << 8) | d[pos + 1];
        if (len < 2) break;
        if (pos + len > n) {
            if (m != 0xDA) break;
            len = n - pos;  // усечённый SOS: попробуем разобрать, что есть
        }
        const uint8_t* s = d + pos + 2;
        size_t sl = len - 2;
        size_t next = pos + len;
        switch (m) {
            case 0xDB: {  // DQT
                size_t i = 0;
                while (i < sl) {
                    int pq = s[i] >> 4, tq = s[i] & 15;
                    i++;
                    if (tq > 3 || pq > 1) return setErr(err, "bad DQT");
                    size_t need = pq ? 128 : 64;
                    if (i + need > sl) return setErr(err, "truncated DQT");
                    for (int k = 0; k < 64; k++) {
                        unsigned v = pq ? (unsigned)((s[i + k * 2] << 8) | s[i + k * 2 + 1]) : s[i + k];
                        qt[tq][kZigzag[k]] = (uint16_t)(v ? v : 1);
                    }
                    qtValid[tq] = true;
                    i += need;
                }
                break;
            }
            case 0xC4: {  // DHT
                size_t i = 0;
                while (i + 17 <= sl) {
                    int tc = s[i] >> 4, th = s[i] & 15;
                    const uint8_t* counts = s + i + 1;
                    int total = 0;
                    for (int k = 0; k < 16; k++) total += counts[k];
                    i += 17;
                    if (tc > 1 || th > 3 || total > 256 || i + (size_t)total > sl) return setErr(err, "bad DHT");
                    JHuff& hf = tc ? acT[th] : dcT[th];
                    if (!hf.build(counts, s + i, total)) return setErr(err, "bad Huffman table");
                    i += total;
                }
                break;
            }
            case 0xDD:
                if (sl >= 2) ri = (s[0] << 8) | s[1];
                break;
            case 0xEE:
                if (sl >= 12 && !std::memcmp(s, "Adobe", 5)) adobe = s[11];
                break;
            case 0xC0: case 0xC1: case 0xC2: {
                if (haveSof) return setErr(err, "multiple SOF");
                if (sl < 6) return setErr(err, "bad SOF");
                if (s[0] != 8) return setErr(err, "unsupported JPEG precision");
                H = (s[1] << 8) | s[2];
                W = (s[3] << 8) | s[4];
                nc = s[5];
                if (nc != 1 && nc != 3) return setErr(err, "unsupported JPEG component count");
                if (sl < 6 + (size_t)nc * 3) return setErr(err, "bad SOF");
                if (H == 0) return setErr(err, "JPEG with DNL not supported");
                if (!checkDims(W, H, err)) return false;
                progressive = (m == 0xC2);
                hmax = vmax = 1;
                for (int c = 0; c < nc; c++) {
                    comps[c].id = s[6 + c * 3];
                    comps[c].h = s[7 + c * 3] >> 4;
                    comps[c].v = s[7 + c * 3] & 15;
                    comps[c].tq = s[8 + c * 3];
                    if (nc == 1) comps[c].h = comps[c].v = 1;
                    if (comps[c].h < 1 || comps[c].h > 4 || comps[c].v < 1 || comps[c].v > 4 || comps[c].tq > 3)
                        return setErr(err, "bad JPEG sampling factors");
                    hmax = std::max(hmax, comps[c].h);
                    vmax = std::max(vmax, comps[c].v);
                }
                mcux = (W + 8 * hmax - 1) / (8 * hmax);
                mcuy = (H + 8 * vmax - 1) / (8 * vmax);
                uint64_t bytes = 0;
                for (int c = 0; c < nc; c++) {
                    comps[c].bw = mcux * comps[c].h;
                    comps[c].bh = mcuy * comps[c].v;
                    bytes += (uint64_t)comps[c].bw * comps[c].bh * 64 * 3;
                }
                if (bytes > ((uint64_t)640 << 20)) return setErr(err, "JPEG too large");
                for (int c = 0; c < nc; c++) comps[c].coef.assign((size_t)comps[c].bw * comps[c].bh * 64, 0);
                haveSof = true;
                break;
            }
            case 0xC3: case 0xC5: case 0xC6: case 0xC7: case 0xC9: case 0xCA: case 0xCB: case 0xCD: case 0xCE: case 0xCF:
                return setErr(err, "unsupported JPEG type (lossless/arithmetic/hierarchical)");
            case 0xDA: {  // SOS
                if (!haveSof) return setErr(err, "SOS before SOF");
                if (sl < 1) return setErr(err, "bad SOS");
                int ns = s[0];
                if (ns < 1 || ns > nc || sl < 1 + (size_t)ns * 2 + 3) return setErr(err, "bad SOS");
                JComp* sc[3];
                for (int i = 0; i < ns; i++) {
                    int cid = s[1 + i * 2], tt = s[2 + i * 2];
                    JComp* c = nullptr;
                    for (int k = 0; k < nc; k++) if (comps[k].id == cid) c = &comps[k];
                    if (!c) return setErr(err, "bad SOS component");
                    for (int k = 0; k < i; k++) if (sc[k] == c) return setErr(err, "bad SOS component");
                    c->td = tt >> 4; c->ta = tt & 15;
                    if (c->td > 3 || c->ta > 3) return setErr(err, "bad SOS table");
                    if (!c->qLatched) {
                        if (!qtValid[c->tq]) return setErr(err, "missing quantisation table");
                        std::memcpy(c->q, qt[c->tq], sizeof c->q);
                        c->qLatched = true;
                    }
                    sc[i] = c;
                }
                int Ss = s[1 + ns * 2], Se = s[2 + ns * 2], Ah = s[3 + ns * 2] >> 4, Al = s[3 + ns * 2] & 15;
                if (!progressive) { Ss = 0; Se = 63; Ah = Al = 0; }
                else {
                    if (Ss > Se || Se > 63 || Al > 13 || Ah > 13) return setErr(err, "bad progressive scan");
                    if (Ss == 0 && Se != 0) return setErr(err, "bad progressive scan");
                    if (Ss > 0 && ns != 1) return setErr(err, "bad progressive scan");
                }
                // таблицы
                for (int i = 0; i < ns; i++) {
                    bool needDc = !progressive || (Ss == 0 && Ah == 0);
                    bool needAc = !progressive || Ss > 0;
                    if (needDc && !dcT[sc[i]->td].valid) return setErr(err, "missing DC Huffman table");
                    if (needAc && !acT[sc[i]->ta].valid) return setErr(err, "missing AC Huffman table");
                }
                BitReader br;
                br.p = d + next;
                br.end = d + n;
                int eobrun = 0;
                bool stop = false;
                for (int i = 0; i < ns; i++) sc[i]->pred = 0;
                int mcuCount = 0;

                auto doRestart = [&]() -> bool {
                    br.acc = 0; br.cnt = 0;
                    const uint8_t* q = br.p;
                    while (q + 1 < br.end && !(q[0] == 0xFF && q[1] >= 0xD0 && q[1] <= 0xD7)) {
                        if (q[0] == 0xFF && q[1] != 0 && q[1] != 0xFF) return false;  // другой маркер
                        q++;
                    }
                    if (q + 1 >= br.end) return false;
                    br.p = q + 2;
                    br.marker = false;
                    br.pad = 0;
                    for (int i = 0; i < ns; i++) sc[i]->pred = 0;
                    eobrun = 0;
                    return true;
                };

                auto decodeBlock = [&](JComp& c, int16_t* blk) -> bool {
                    if (!progressive) {
                        int t = br.decode(dcT[c.td]);
                        if (t < 0 || t > 15) return false;
                        int diff = t ? extend(br.bits(t), t) : 0;
                        c.pred += diff;
                        blk[0] = (int16_t)std::max(-32768, std::min(32767, c.pred));
                        const JHuff& ac = acT[c.ta];
                        for (int k = 1; k < 64;) {
                            int rs = br.decode(ac);
                            if (rs < 0) return false;
                            int r = rs >> 4, sz = rs & 15;
                            if (sz == 0) {
                                if (r == 15) { k += 16; continue; }
                                break;
                            }
                            k += r;
                            if (k > 63) return false;
                            blk[kZigzag[k]] = (int16_t)extend(br.bits(sz), sz);
                            k++;
                        }
                        return true;
                    }
                    if (Ss == 0) {
                        if (Ah == 0) {
                            int t = br.decode(dcT[c.td]);
                            if (t < 0 || t > 15) return false;
                            int diff = t ? extend(br.bits(t), t) : 0;
                            c.pred += diff;
                            blk[0] = (int16_t)std::max(-32768, std::min(32767, c.pred * (1 << Al)));
                        } else if (br.bits(1)) {
                            blk[0] = (int16_t)(blk[0] | (1 << Al));
                        }
                        return true;
                    }
                    const JHuff& ac = acT[c.ta];
                    if (Ah == 0) {  // AC first
                        if (eobrun > 0) { eobrun--; return true; }
                        for (int k = Ss; k <= Se;) {
                            int rs = br.decode(ac);
                            if (rs < 0) return false;
                            int r = rs >> 4, sz = rs & 15;
                            if (sz == 0) {
                                if (r < 15) {
                                    eobrun = (1 << r) - 1;
                                    if (r) eobrun += br.bits(r);
                                    break;
                                }
                                k += 16;
                                continue;
                            }
                            k += r;
                            if (k > 63) return false;
                            blk[kZigzag[k]] = (int16_t)(extend(br.bits(sz), sz) * (1 << Al));
                            k++;
                        }
                        return true;
                    }
                    // AC refinement
                    const int p1 = 1 << Al, m1 = -(1 << Al);
                    int k = Ss;
                    auto refineNonZero = [&](int16_t* cf) {
                        if (br.bits(1) && (*cf & p1) == 0) *cf = (int16_t)(*cf + (*cf >= 0 ? p1 : m1));
                    };
                    if (eobrun == 0) {
                        for (; k <= Se; k++) {
                            int rs = br.decode(ac);
                            if (rs < 0) return false;
                            int r = rs >> 4, sz = rs & 15, val = 0;
                            if (sz) {
                                if (sz != 1) return false;
                                val = br.bits(1) ? p1 : m1;
                            } else if (r != 15) {
                                eobrun = 1 << r;
                                if (r) eobrun += br.bits(r);
                                break;
                            }
                            do {
                                int16_t* cf = &blk[kZigzag[k]];
                                if (*cf != 0) refineNonZero(cf);
                                else if (--r < 0) break;
                                k++;
                            } while (k <= Se);
                            if (val) {
                                if (k > Se) return false;
                                blk[kZigzag[k]] = (int16_t)val;
                            }
                        }
                    }
                    if (eobrun > 0) {
                        for (; k <= Se; k++) {
                            int16_t* cf = &blk[kZigzag[k]];
                            if (*cf != 0) refineNonZero(cf);
                        }
                        eobrun--;
                    }
                    return true;
                };

                auto checkStop = [&]() { if (br.pad > 24) stop = true; };
                if (ns == 1) {
                    JComp& c = *sc[0];
                    int nbx = (((W * c.h + hmax - 1) / hmax) + 7) / 8, nby = (((H * c.v + vmax - 1) / vmax) + 7) / 8;
                    nbx = std::min(nbx, c.bw); nby = std::min(nby, c.bh);
                    for (int by = 0; by < nby && !stop; by++)
                        for (int bx = 0; bx < nbx && !stop; bx++) {
                            if (ri && mcuCount && mcuCount % ri == 0 && !doRestart()) { stop = true; break; }
                            mcuCount++;
                            if (!decodeBlock(c, &c.coef[((size_t)by * c.bw + bx) * 64])) { stop = true; break; }
                            checkStop();
                        }
                } else {
                    for (int my = 0; my < mcuy && !stop; my++)
                        for (int mx = 0; mx < mcux && !stop; mx++) {
                            if (ri && mcuCount && mcuCount % ri == 0 && !doRestart()) { stop = true; break; }
                            mcuCount++;
                            for (int i = 0; i < ns && !stop; i++) {
                                JComp& c = *sc[i];
                                for (int vy = 0; vy < c.v && !stop; vy++)
                                    for (int hx = 0; hx < c.h; hx++) {
                                        size_t bi = ((size_t)(my * c.v + vy) * c.bw + (size_t)(mx * c.h + hx)) * 64;
                                        if (!decodeBlock(c, &c.coef[bi])) { stop = true; break; }
                                    }
                            }
                            checkStop();
                        }
                }
                anyScan = true;
                // продолжаем после данных скана
                next = br.p > d + next ? (size_t)(br.p - d) : next;
                if (stop && br.pad > 24) next = n;  // данные кончились
                break;
            }
            default: break;  // APPn, COM и пр.
        }
        pos = next;
    }
    if (!haveSof) return setErr(err, "JPEG without frame header");
    if (!anyScan) return setErr(err, "JPEG without scan data");

    // ОДКП
    for (int c = 0; c < nc; c++) {
        JComp& cm = comps[c];
        if (!cm.qLatched) {
            if (!qtValid[cm.tq]) return setErr(err, "missing quantisation table");
            std::memcpy(cm.q, qt[cm.tq], sizeof cm.q);
        }
        int pw = cm.bw * 8;
        cm.plane.assign((size_t)pw * cm.bh * 8, 0);
        for (int by = 0; by < cm.bh; by++)
            for (int bx = 0; bx < cm.bw; bx++)
                idctBlock(&cm.plane[((size_t)by * 8) * pw + (size_t)bx * 8], pw, &cm.coef[((size_t)by * cm.bw + bx) * 64], cm.q);
        std::vector<int16_t>().swap(cm.coef);
    }

    if (!allocImage(out, W, H, err)) return false;
    uint8_t* o = out.rgba.data();
    if (nc == 1) {
        const JComp& c = comps[0];
        int pw = c.bw * 8;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++, o += 4) {
                o[0] = o[1] = o[2] = c.plane[(size_t)y * pw + x];
                o[3] = 255;
            }
        return true;
    }
    bool rgb = (adobe == 0) || (comps[0].id == 'R' && comps[1].id == 'G' && comps[2].id == 'B');
    // апсэмплинг цветоразностных компонент (билинейный, как "fancy upsampling" libjpeg)
    UpTable ux[3], uy[3];
    int cw[3], chh[3];
    for (int c = 0; c < 3; c++) {
        cw[c] = (W * comps[c].h + hmax - 1) / hmax;
        chh[c] = (H * comps[c].v + vmax - 1) / vmax;
        if (comps[c].h != hmax) ux[c].build(W, comps[c].h, hmax, cw[c]);
        if (comps[c].v != vmax) uy[c].build(H, comps[c].v, vmax, chh[c]);
    }
    std::vector<uint8_t> rows[3];
    for (auto& r : rows) r.resize((size_t)W);
    for (int y = 0; y < H; y++) {
        for (int c = 0; c < 3; c++) {
            const JComp& cm = comps[c];
            int pw = cm.bw * 8;
            uint8_t* dst = rows[c].data();
            bool sx = cm.h != hmax, sy = cm.v != vmax;
            if (!sx && !sy) { std::memcpy(dst, &cm.plane[(size_t)y * pw], (size_t)W); continue; }
            int ya = y, yb = y, wy = 0;
            if (sy) { ya = uy[c].i0[y]; yb = uy[c].i1[y]; wy = uy[c].wt[y]; }
            const uint8_t* ra = &cm.plane[(size_t)ya * pw];
            const uint8_t* rb = &cm.plane[(size_t)yb * pw];
            if (sx) {
                for (int x = 0; x < W; x++) {
                    int a = ux[c].i0[x], b = ux[c].i1[x], wx = ux[c].wt[x];
                    int top = ra[a] * (256 - wx) + ra[b] * wx, bot = rb[a] * (256 - wx) + rb[b] * wx;
                    dst[x] = (uint8_t)((top * (256 - wy) + bot * wy + 32768) >> 16);
                }
            } else {
                for (int x = 0; x < W; x++) dst[x] = (uint8_t)((ra[x] * (256 - wy) + rb[x] * wy + 128) >> 8);
            }
        }
        const uint8_t *Y = rows[0].data(), *Cb = rows[1].data(), *Cr = rows[2].data();
        if (rgb) {
            for (int x = 0; x < W; x++, o += 4) { o[0] = Y[x]; o[1] = Cb[x]; o[2] = Cr[x]; o[3] = 255; }
        } else {
            for (int x = 0; x < W; x++, o += 4) {
                int yy = Y[x], cb = Cb[x] - 128, cr = Cr[x] - 128;
                o[0] = clamp8(yy + ((91881 * cr + 32768) >> 16));
                o[1] = clamp8(yy - ((22554 * cb + 46802 * cr + 32768) >> 16));
                o[2] = clamp8(yy + ((116130 * cb + 32768) >> 16));
                o[3] = 255;
            }
        }
    }
    return true;
}

}  // namespace

bool decodeImage(const uint8_t* data, size_t size, Image& out, std::string* err) {
    out = Image();
    if (!data || size < 4) return setErr(err, "image data too short");
    try {
        bool ok;
        if (size >= 8 && !std::memcmp(data, "\x89PNG\r\n\x1a\n", 8)) ok = decodePng(data, size, out, err);
        else if (data[0] == 0xFF && data[1] == 0xD8) ok = decodeJpeg(data, size, out, err);
        else if (data[0] == 'B' && data[1] == 'M') ok = decodeBmp(data, size, out, err);
        else ok = decodeTga(data, size, out, err);
        if (!ok) out = Image();
        return ok;
    } catch (const std::exception& e) {
        out = Image();
        return setErr(err, "image decoding failed (out of memory?)");
    } catch (...) {
        out = Image();
        return setErr(err, "image decoding failed");
    }
}

void downscaleToMax(Image& img, int maxDim) {
    if (maxDim < 1 || img.w <= 0 || img.h <= 0 || (img.w <= maxDim && img.h <= maxDim)) return;
    if (img.rgba.size() < (size_t)img.w * img.h * 4) return;
    double sc = (double)maxDim / std::max(img.w, img.h);
    int nw = std::max(1, (int)std::lround(img.w * sc)), nh = std::max(1, (int)std::lround(img.h * sc));
    nw = std::min(nw, maxDim);
    nh = std::min(nh, maxDim);
    const double sx = (double)img.w / nw, sy = (double)img.h / nh;
    // 7 float на пиксель: [0..2] сумма c*a, [3] сумма a, [4..6] сумма c (обычное среднее для прозрачных областей)
    std::vector<float> hrow((size_t)nw * 7), acc((size_t)nw * 7);
    Image res;
    res.w = nw;
    res.h = nh;
    res.rgba.assign((size_t)nw * nh * 4, 0);
    auto horiz = [&](int srcY) {
        const uint8_t* row = &img.rgba[(size_t)srcY * img.w * 4];
        for (int ox = 0; ox < nw; ox++) {
            double l = ox * sx, r = (ox + 1) * sx;
            int a = (int)l, b = std::min((int)std::ceil(r), img.w);
            double v[7] = {0, 0, 0, 0, 0, 0, 0}, sw = 0;
            for (int x = a; x < b; x++) {
                double wgt = std::min(r, (double)x + 1) - std::max(l, (double)x);
                if (wgt <= 0) continue;
                const uint8_t* p = row + (size_t)x * 4;
                double al = p[3] * wgt;
                for (int c = 0; c < 3; c++) { v[c] += p[c] * al; v[4 + c] += p[c] * wgt; }
                v[3] += al;
                sw += wgt;
            }
            float* o = &hrow[(size_t)ox * 7];
            double inv = sw > 0 ? 1.0 / sw : 0;
            for (int c = 0; c < 7; c++) o[c] = (float)(v[c] * inv);
        }
    };
    for (int oy = 0; oy < nh; oy++) {
        std::fill(acc.begin(), acc.end(), 0.f);
        double t = oy * sy, bt = (oy + 1) * sy, wsum = 0;
        int a = (int)t, b = std::min((int)std::ceil(bt), img.h);
        for (int y = a; y < b; y++) {
            double wgt = std::min(bt, (double)y + 1) - std::max(t, (double)y);
            if (wgt <= 0) continue;
            horiz(y);
            for (size_t i = 0; i < acc.size(); i++) acc[i] += (float)(hrow[i] * wgt);
            wsum += wgt;
        }
        double inv = wsum > 0 ? 1.0 / wsum : 0;
        uint8_t* o = &res.rgba[(size_t)oy * nw * 4];
        for (int ox = 0; ox < nw; ox++, o += 4) {
            const float* q = &acc[(size_t)ox * 7];
            double A = q[3];
            for (int c = 0; c < 3; c++)
                o[c] = clamp8((int)std::lround(A > 1e-3 ? q[c] / A : q[4 + c] * inv));
            o[3] = clamp8((int)std::lround(A * inv));
        }
    }
    img = std::move(res);
}

}  // namespace eng
