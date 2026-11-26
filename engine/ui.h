// 2D-интерфейс PrismLab: рисование (прямоугольники, текст с кириллицей, значки) и виджеты для касаний.
#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "gl_api.h"

namespace eng {

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    Rect() = default;
    Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
    float r() const { return x + w; }
    float b() const { return y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
    bool has(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    Rect inset(float d) const { return {x + d, y + d, std::max(0.f, w - 2 * d), std::max(0.f, h - 2 * d)}; }
    Rect inset(float dx, float dy) const { return {x + dx, y + dy, std::max(0.f, w - 2 * dx), std::max(0.f, h - 2 * dy)}; }
    Rect sub(float fx, float fw) const { return {x + w * fx, y, w * fw, h}; }  // доля по ширине
    Rect intersect(const Rect& o) const {
        float x0 = std::max(x, o.x), y0 = std::max(y, o.y), x1 = std::min(r(), o.r()), y1 = std::min(b(), o.b());
        return {x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0)};
    }
};

struct Col {
    float r = 1, g = 1, b = 1, a = 1;
    Col() = default;
    Col(float r_, float g_, float b_, float a_ = 1) : r(r_), g(g_), b(b_), a(a_) {}
    static Col hex(uint32_t rgb, float a = 1.f) { return {((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a}; }
    Col alpha(float na) const { return {r, g, b, a * na}; }
    Col mix(const Col& o, float t) const { return {r + (o.r - r) * t, g + (o.g - g) * t, b + (o.b - b) * t, a + (o.a - a) * t}; }
};

namespace theme {
extern Col bg, panel, panel2, widget, widgetHot, accent, accent2, text, textDim, danger, ok, keyCol, line, orange;
// onAccent - текст на акцентных кнопках; accentEnd - конец градиента главной кнопки (у плоских тем равен accent);
// overlay - плашки поверх 3D-окна; shade - затемнение под окнами; popup - всплывающие подписи; homeTop - верх главного экрана; thumb - фон превью.
extern Col onAccent, accentEnd, overlay, shade, popup, homeTop, thumb;
constexpr int kCount = 3;
const char* name(int id);          // «Чёрная», «Белая», «GTA 6»
void apply(int id);                // 0 чёрная, 1 белая, 2 в стиле GTA 6
bool gradient();                   // у темы градиентные главные кнопки
}

enum class Icon {
    Eye, EyeOff, Plus, Trash, Copy, Undo, Redo, Play, Pause, Prev, Next, ToStart, ToEnd, Key, KeyAdd, Move, Rotate, Scale,
    Camera, Sun, Bulb, Cube, Flame, Menu, ChevDown, ChevUp, Close, Link, Unlink, Film, Image, Check, Bone, Snap, Lock, Folder, Save, Gear, Smoke, Bolt, Sparks, Model, Ball, Drop, Home, Back, Layers, Dots3, Rig, Palette
};

class Gfx {
public:
    bool init();
    void release();
    void begin(int w, int h);
    void end();
    int width() const { return w_; }
    int height() const { return h_; }

    void rect(const Rect& r, const Col& c, float radius = 0.f);
    void rectGrad(const Rect& r, const Col& top, const Col& bottom, float radius = 0.f);
    void stroke(const Rect& r, const Col& c, float radius, float width);
    void circle(float cx, float cy, float rad, const Col& c) { rect({cx - rad, cy - rad, rad * 2, rad * 2}, c, rad); }
    void ring(float cx, float cy, float rad, float width, const Col& c) { stroke({cx - rad, cy - rad, rad * 2, rad * 2}, c, rad, width); }
    void line(float x0, float y0, float x1, float y1, float width, const Col& c);
    void tri(float x0, float y0, float x1, float y1, float x2, float y2, const Col& c);
    void icon(Icon ic, const Rect& r, const Col& c);
    void image(GLuint tex, const Rect& r, float alpha = 1.f);  // готовая RGB-текстура (превью проектов)
    static GLuint makeTexture(const uint8_t* rgb, int w, int h);

    // size - высота em в пикселях; y - базовая линия
    void text(const std::string& s, float x, float baselineY, float size, const Col& c);
    float textWidth(const std::string& s, float size) const;
    void textIn(const Rect& r, const std::string& s, float size, const Col& c, int align = 1);  // 0 лево, 1 центр, 2 право
    std::string fit(const std::string& s, float size, float maxW) const;

    void pushClip(const Rect& r);
    void popClip();
    Rect clip() const { return clips_.empty() ? Rect(0, 0, (float)w_, (float)h_) : clips_.back(); }

private:
    struct Run { size_t start; Rect clip; bool hasClip; GLuint tex; };
    void vtx(float x, float y, float u, float v, const Col& c, float hx, float hy, float rad, float mode, float sw);
    void quad(const Rect& r, float margin, const Col& top, const Col& bot, float hx, float hy, float rad, float mode, float sw);
    int findGlyph(uint32_t code) const;

    GLuint prog_ = 0, vao_ = 0, vbo_ = 0, atlas_ = 0;
    GLint uRes_ = -1, uAtlas_ = -1;
    int w_ = 1, h_ = 1;
    std::vector<float> v_;
    std::vector<Run> runs_;
    std::vector<Rect> clips_;
    size_t cap_ = 0;
    std::vector<int> ascii_;
};

// ---------------- ввод и виджеты ----------------
struct PointerState {
    bool down = false;
    float x = 0, y = 0;
};

struct ScrollState {
    float off = 0, vel = 0;
};

enum class WRes { None, Changed, Tapped };

class UI {
public:
    Gfx g;
    float S = 1.f;  // пикселей на dp
    float dt = 1.f / 60;

    void begin(int w, int h, float density, const PointerState& p, float dtSec);
    void end();
    float dp(float v) const { return v * S; }

    bool record = false;                  // для тестов: запоминать прямоугольники виджетов по подписи
    std::map<std::string, Rect> rects;
    bool blocked = false;  // модальное окно закрывает всё, что под ним
    bool pointerDown() const { return down_; }
    float px() const { return px_; }
    float py() const { return py_; }
    bool pressedNow() const { return pressEdge_; }
    bool releasedNow() const { return releaseEdge_; }
    float downX() const { return downX_; }
    float downY() const { return downY_; }
    bool inputInside(const Rect& r) const;     // касание началось в r и r не закрыт clip-ом
    void claimPointer() { owner_ = 1u << 30; }  // viewport и т.п. забирает касание
    bool ownerIsOther(uint32_t id) const { return owner_ != 0 && owner_ != id; }

    // виджеты
    bool button(const Rect& r, const std::string& label, int style = 0, float fontDp = 14.f);
    bool iconBtn(const Rect& r, Icon ic, bool active = false, const Col* tint = nullptr, bool filled = true);
    bool iconLabelBtn(const Rect& r, Icon ic, const std::string& label, bool active = false, float fontDp = 12.f);
    void label(const Rect& r, const std::string& s, float fontDp, const Col& c, int align = 0);
    bool toggle(const Rect& r, const std::string& label, bool& v);
    WRes slider(const Rect& r, const std::string& label, float& v, float lo, float hi, const char* fmt, bool bar = true, float dragPerPx = 0.f);
    int segmented(const Rect& r, const std::vector<std::string>& items, int sel);
    bool listRow(const Rect& r, bool selected, bool* tappedOnly = nullptr);  // фон строки, возвращает true по тапу

    void beginScroll(const Rect& view, ScrollState& s, float contentH, bool horizontal = false);
    void endScroll();

    // для кастомных зон (таймлайн, видовой экран): возвращает true, если касание началось внутри
    uint32_t newId() { return ++idCounter_; }
    uint32_t zone(const Rect& r) { uint32_t id = newId(); hit(id, r); return id; }
    uint32_t candidate() const { return candidate_; }
    bool pressedId(uint32_t id) const { return pressedId_ == id; }
    void setCandidate(uint32_t id) { candidate_ = id; }
    bool tapOn(uint32_t id, const Rect& r) const;
    bool dragOn(uint32_t id) const { return down_ && pressedId_ == id; }
    bool hitTestAllowed(const Rect& r) const;
    void takeOwner(uint32_t id) { if (!owner_) owner_ = id; }
    uint32_t owner() const { return owner_; }
    int axis() const { return axis_; }
    bool moved() const { return moved_; }
    float slop() const { return 10.f * S; }
    float dragDx() const { return px_ - lastX_; }
    float dragDy() const { return py_ - lastY_; }

private:
    bool hit(uint32_t id, const Rect& r);

    bool down_ = false, pressEdge_ = false, releaseEdge_ = false, prevDown_ = false;
    float px_ = 0, py_ = 0, lastX_ = 0, lastY_ = 0, downX_ = 0, downY_ = 0;
    bool moved_ = false;
    int axis_ = 0;
    uint32_t idCounter_ = 0, pressedId_ = 0, candidate_ = 0, owner_ = 0;
    struct ScrollCtx { Rect view; ScrollState* s; float contentH; uint32_t id; bool horiz; };
    std::vector<ScrollCtx> scrolls_;
    float sliderBase_ = 0;
    int w_ = 1, h_ = 1;
};

}  // namespace eng
