#!/usr/bin/env python3
"""Генерирует engine/font_data.inc: атлас шрифта Inter Medium (латиница + кириллица), сжатый zlib.
Лицензия шрифта: SIL OFL 1.1 (Inter, The Inter Project Authors)."""
import zlib, sys
from PIL import Image, ImageDraw, ImageFont

SIZE = 44
FONT = '/usr/share/fonts/opentype/inter/Inter-Medium.otf'
chars = [chr(c) for c in range(32, 127)]
chars += [chr(c) for c in range(0x410, 0x450)] + ['Ё', 'ё']
chars += list('°×±…—·№«»’–•←→↑↓−⌫▸▾')
f = ImageFont.truetype(FONT, SIZE)
asc, desc = f.getmetrics()
W = 1024
x = y = 2
rowh = 0
glyphs = []
pad = 2
img = Image.new('L', (W, 1024), 0)
for ch in chars:
    adv = f.getlength(ch)
    bb = f.getbbox(ch, anchor='ls')  # относительно базовой линии
    if ch == ' ' or bb[2] <= bb[0]:
        glyphs.append((ord(ch), 0, 0, 0, 0, 0, 0, adv)); continue
    gw, gh = bb[2] - bb[0], bb[3] - bb[1]
    if x + gw + pad > W:
        x = 2; y += rowh + pad; rowh = 0
    d = ImageDraw.Draw(img)
    d.text((x - bb[0], y - bb[1]), ch, font=f, fill=255, anchor='ls')
    glyphs.append((ord(ch), x, y, gw, gh, bb[0], bb[1], adv))
    x += gw + pad; rowh = max(rowh, gh)
H = y + rowh + 2
H = (H + 3) // 4 * 4
img = img.crop((0, 0, W, H))
raw = img.tobytes()
comp = zlib.compress(raw, 9)
with open('engine/font_data.inc', 'w') as o:
    o.write('// Сгенерировано tools/gen_font.py. Inter Medium (SIL OFL 1.1), размер %d px.\n' % SIZE)
    o.write('static const int kFontAtlasW = %d, kFontAtlasH = %d, kFontSize = %d;\n' % (W, H, SIZE))
    o.write('static const float kFontAscent = %.2ff, kFontDescent = %.2ff;\n' % (asc, desc))
    o.write('struct GlyphInfo { int code; short x, y, w, h; float xoff, yoff, adv; };\n')
    o.write('static const GlyphInfo kGlyphs[] = {\n')
    for g in glyphs:
        o.write('  {%d,%d,%d,%d,%d,%.1ff,%.1ff,%.3ff},\n' % (g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7]))
    o.write('};\nstatic const int kGlyphCount = %d;\n' % len(glyphs))
    o.write('static const unsigned kFontZSize = %d;\nstatic const unsigned char kFontZ[] = {' % len(comp))
    for i, b in enumerate(comp):
        if i % 24 == 0: o.write('\n')
        o.write('%d,' % b)
    o.write('\n};\n')
img.save('/tmp/atlas_dbg.png')
print('atlas', W, H, 'glyphs', len(glyphs), 'compressed', len(comp))
