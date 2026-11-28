// Настройки приложения: темы, масштаб интерфейса, управление видом. Хранятся в settings.cfg в папке данных.
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <sstream>

#include "app.h"

namespace eng {

static std::string settingsPath(const Platform& p) { return p.dataDir.empty() ? std::string() : p.dataDir + "/settings.cfg"; }

void App::loadSettings() {
    Settings s;
    std::string path = settingsPath(plat);
    std::ifstream f(path);
    std::string line;
    while (path.size() && f && std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        double d = std::atof(v.c_str());
        if (k == "theme") s.theme = (int)d;
        else if (k == "uiScale") s.uiScale = (float)d;
        else if (k == "orbitSens") s.orbitSens = (float)d;
        else if (k == "inertia") s.inertia = d != 0;
        else if (k == "invertY") s.invertY = d != 0;
        else if (k == "doubleTapFocus") s.doubleTapFocus = d != 0;
        else if (k == "wallpaper") s.wallpaper = (int)d;
        else if (k == "boneLines") s.boneLines = d != 0;
    }
    s.theme = std::max(0, std::min(theme::kCount - 1, s.theme));
    s.wallpaper = std::max(0, std::min(5, s.wallpaper));
    s.uiScale = clampf(s.uiScale, 0.8f, 1.4f);
    s.orbitSens = clampf(s.orbitSens, 0.3f, 3.f);
    settings = s;
    theme::apply(settings.theme);
}

void App::saveSettings() const {
    std::string path = settingsPath(plat);
    if (path.empty()) return;
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    f << "theme=" << settings.theme << "\nuiScale=" << settings.uiScale << "\norbitSens=" << settings.orbitSens << "\ninertia=" << (settings.inertia ? 1 : 0)
      << "\ninvertY=" << (settings.invertY ? 1 : 0) << "\ndoubleTapFocus=" << (settings.doubleTapFocus ? 1 : 0) << "\nwallpaper=" << settings.wallpaper << "\nboneLines=" << (settings.boneLines ? 1 : 0) << "\n";
}

// Полноэкранные настройки поверх главного экрана или редактора.
void App::drawSettings() {
    if (!settingsOpen_) return;
    Gfx& g = ui.g;
    float S = ui.S;
    Rect full(0, 0, (float)W_, (float)H_);
    drawWallpaper(full);
    ui.zone(full);

    float cw = std::min((float)W_ - 16 * S, 640 * S);
    float x0 = ((float)W_ - cw) * 0.5f;
    float topH = 56 * S + (home_ ? 0.f : 0.f);
    g.rect(Rect(0, 0, (float)W_, topH), theme::panel, 0);
    g.rect(Rect(0, topH - 1, (float)W_, 1), theme::line, 0);
    if (ui.iconBtn(Rect(x0, 6 * S, 44 * S, 44 * S), Icon::Back, false, nullptr, true)) { settingsOpen_ = false; return; }
    g.text("Настройки", x0 + 56 * S, 34 * S, 20 * S, theme::text);

    Rect view(0, topH, (float)W_, (float)H_ - topH);
    g.rect(Rect(x0 - 4 * S, topH, cw + 8 * S, view.h), theme::bg.alpha(0.86f), 0);
    ScrollState& sc = scrollSettings_;
    form_.x = x0 + 4 * S; form_.w = cw - 8 * S; form_.y = view.y + 8 * S - sc.off;
    float top = view.y - sc.off;
    ui.beginScroll(view, sc, contentSettings_);

    // ---- тема ----
    sectionLabel("Тема оформления");
    {
        Rect r = row(86);
        for (int i = 0; i < theme::kCount; i++) {
            Rect c = cell(r, i, theme::kCount, 8);
            bool sel = settings.theme == i;
            // превью: рисуем цвета темы, не меняя текущую
            theme::apply(i);
            Col pbg = theme::bg, ppanel = theme::panel, pwid = theme::widget, pacc = theme::accent, pacc2 = theme::accentEnd, ptext = theme::text, pdim = theme::textDim,
                pline = theme::line, ptop = theme::homeTop, pa2 = theme::accent2;
            theme::apply(settings.theme);
            uint32_t id = ui.zone(c);
            g.rectGrad(c, ptop, pbg, 10 * S);
            g.rect(Rect(c.x + 8 * S, c.y + 8 * S, c.w - 16 * S, 14 * S), ppanel, 4 * S);
            g.rect(Rect(c.x + 8 * S, c.y + 28 * S, (c.w - 16 * S) * 0.55f, 10 * S), pwid, 4 * S);
            if (i == 2) g.rectGrad(Rect(c.x + 8 * S, c.y + 44 * S, c.w - 16 * S, 14 * S), pacc, pacc2, 5 * S);
            else g.rect(Rect(c.x + 8 * S, c.y + 44 * S, c.w - 16 * S, 14 * S), pacc, 5 * S);
            g.circle(c.r() - 14 * S, c.y + 33 * S, 4 * S, pa2);
            g.textIn(Rect(c.x, c.b() - 24 * S, c.w, 22 * S), theme::name(i), 13 * S, ptext, 1);
            (void)pdim;
            g.stroke(c, sel ? theme::accent : pline, 10 * S, (sel ? 2.5f : 1.f) * S);
            if (ui.tapOn(id, c) && !sel) {
                settings.theme = i;
                theme::apply(i);
                saveSettings();
            }
        }
    }

    // ---- обои ----
    sectionLabel("Обои главного экрана");
    {
        static const char* wn[6] = {"Как тема", "Призмы", "Звёзды", "Закат", "Сетка", "Простой"};
        for (int rw = 0; rw < 2; rw++) {
            Rect r = row(70);
            for (int c3 = 0; c3 < 3; c3++) {
                int i = rw * 3 + c3;
                Rect c = cell(r, c3, 3, 8);
                bool sel = settings.wallpaper == i;
                uint32_t id = ui.zone(c);
                drawWallpaper(c, i);
                g.rect(Rect(c.x, c.b() - 22 * S, c.w, 22 * S), Col(0, 0, 0, 0.45f), 0);
                g.textIn(Rect(c.x, c.b() - 22 * S, c.w, 22 * S), wn[i], 12.5f * S, Col(1, 1, 1, 1), 1);
                g.stroke(c, sel ? theme::accent : theme::line, 8 * S, (sel ? 2.5f : 1.f) * S);
                if (ui.tapOn(id, c) && !sel) { settings.wallpaper = i; saveSettings(); }
            }
            space(6);
        }
    }

    // ---- интерфейс ----
    sectionLabel("Интерфейс");
    {
        float v = settings.uiScale * 100.f;
        Rect r = row(40);
        auto ap = [this](float nv) { settings.uiScale = clampf(nv / 100.f, 0.8f, 1.4f); saveSettings(); };
        if (slide(r, "Размер элементов, %", v, 80, 140, "%.0f", 0.5f, ap)) ap(v);
        Rect hint = row(30);
        ui.label(hint, "Если кнопки мелкие или крупные для вашего экрана, измените размер.", 11, theme::textDim, 0);
    }

    // ---- управление видом ----
    sectionLabel("Камера в окне (вид)");
    {
        float v = settings.orbitSens;
        Rect r = row(40);
        auto ap = [this](float nv) { settings.orbitSens = clampf(nv, 0.3f, 3.f); saveSettings(); };
        if (slide(r, "Чувствительность вращения", v, 0.3f, 3.f, "%.2f", 0.01f, ap)) ap(v);
        bool in = settings.inertia;
        if (ui.toggle(row(40), "Плавная инерция после броска", in)) { settings.inertia = in; saveSettings(); }
        bool iy = settings.invertY;
        if (ui.toggle(row(40), "Инвертировать наклон", iy)) { settings.invertY = iy; saveSettings(); }
        bool dt = settings.doubleTapFocus;
        if (ui.toggle(row(40), "Двойной тап: показать выбранное", dt)) { settings.doubleTapFocus = dt; saveSettings(); }
        Rect hint = row(60);
        ui.label(hint, "Кнопка с глазом показывает выбранный объект, кнопка со слоями открывает виды: спереди, сзади, слева, справа, сверху.", 11, theme::textDim, 0);
    }

    sectionLabel("Скелет в окне");
    {
        bool bl = settings.boneLines;
        if (ui.toggle(row(40), "Показывать линии костей", bl)) { settings.boneLines = bl; saveSettings(); }
        Rect hint = row(44);
        ui.label(hint, "Выключено: видны только точки суставов, они не закрывают модель. У выбранной кости линия к родителю есть всегда.", 11, theme::textDim, 0);
    }

    sectionLabel("Папка экспорта");
    {
        std::string cur = plat.exportDirName ? plat.exportDirName() : std::string();
        Rect r0 = row(34);
        ui.label(r0, cur.empty() ? "Сейчас: Загрузки/PrismLab (по умолчанию); картинки в Pictures/PrismLab" : "Сейчас: " + cur, 12.5f, theme::text, 0);
        Rect r = row(44);
        if (ui.button(cell(r, 0, 2), "Выбрать папку", 1)) {
            if (plat.pickExportDir) plat.pickExportDir(); else toast("Выбор папки недоступен на этом устройстве");
        }
        if (ui.button(cell(r, 1, 2), "По умолчанию", 0)) {
            if (plat.resetExportDir) plat.resetExportDir();
            toast("Папка экспорта: по умолчанию");
        }
        Rect hint = row(44);
        ui.label(hint, "Картинки PNG, видео MP4 и модели GLB сохраняются в выбранную папку. Если папка недоступна, файл попадёт в стандартное место.", 11, theme::textDim, 0);
    }

    sectionLabel("Сброс");
    {
        Rect r = row(44);
        if (ui.button(r, "Сбросить настройки по умолчанию", 2)) {
            confirm("Сбросить настройки?", "Тема, обои, размер элементов, камера и скелет вернутся к значениям по умолчанию. Проекты не затрагиваются.", [this]() { resetSettings(); toast("Настройки сброшены"); }, "Сбросить");
        }
    }

    sectionLabel("О приложении");
    {
        Rect hint = row(60);
        ui.label(hint, "PrismLab: сцены и анимация на телефоне. Настройки хранятся на устройстве и не требуют сети.", 12, theme::textDim, 0);
    }
    contentSettings_ = form_.y - top + 24 * S;
    ui.endScroll();
}


void App::resetSettings() {
    settings = Settings();
    theme::apply(settings.theme);
    if (plat.resetExportDir) plat.resetExportDir();
    saveSettings();
}

// Обои: рисуются в заданном прямоугольнике (в списке выбора - маленькие превью).
void App::drawWallpaper(const Rect& r, int style) {
    Gfx& g = ui.g;
    if (style < 0) style = settings.wallpaper;
    bool dark = (theme::bg.r + theme::bg.g + theme::bg.b) < 1.5f;
    float t = (float)now_;
    float u = std::min(r.w, r.h);
    auto hs = [](int i) {
        uint32_t x = (uint32_t)i * 2654435761u + 12345u;
        x ^= x >> 15; x *= 2246822519u; x ^= x >> 13; x *= 3266489917u; x ^= x >> 16;
        return (float)(x & 0xffff) / 65535.f;
    };
    g.pushClip(r);
    if (style == 5) {
        g.rect(r, theme::bg, 0);
    } else if (style == 0) {
        g.rectGrad(r, theme::homeTop, theme::bg, 0);
    } else if (style == 1) {  // призмы
        g.rectGrad(r, theme::homeTop, theme::bg, 0);
        for (int i = 0; i < 11; i++) {
            float sz = u * (0.07f + 0.13f * hs(i + 100));
            float sp = 0.012f + 0.02f * hs(i + 200);
            float cx = r.x + hs(i) * r.w;
            float cy = r.y + std::fmod(hs(i + 50) + t * sp, 1.f) * (r.h + sz * 2) - sz;
            float a = t * (0.15f + 0.2f * hs(i + 300)) * (i & 1 ? 1.f : -1.f) + hs(i + 400) * 6.28f;
            float x0 = cx + std::cos(a) * sz, y0 = cy + std::sin(a) * sz;
            float x1 = cx + std::cos(a + 2.094f) * sz, y1 = cy + std::sin(a + 2.094f) * sz;
            float x2 = cx + std::cos(a + 4.189f) * sz, y2 = cy + std::sin(a + 4.189f) * sz;
            Col c = (i % 3 == 0 ? theme::accent2 : theme::accent).alpha(dark ? 0.16f : 0.14f);
            g.tri(x0, y0, x1, y1, x2, y2, c);
            g.tri(x0, y0, x1, y1, cx, cy, c.alpha(1.6f));
        }
    } else if (style == 2) {  // звёзды
        Col top = dark ? Col(0.01f, 0.01f, 0.06f) : Col(0.72f, 0.82f, 0.95f);
        Col bot = dark ? theme::bg.mix(Col(0.05f, 0.03f, 0.14f), 0.7f) : Col(0.93f, 0.95f, 1.f);
        g.rectGrad(r, top, bot, 0);
        for (int i = 0; i < 110; i++) {
            float x = r.x + hs(i) * r.w, y = r.y + hs(i + 500) * r.h;
            float sz = (0.5f + 1.4f * hs(i + 900)) * std::max(0.6f, u / 400.f);
            float tw = 0.55f + 0.45f * std::sin(t * (0.6f + 2.f * hs(i + 700)) + hs(i + 800) * 6.28f);
            g.circle(x, y, sz, (dark ? Col(1, 1, 1, 1) : Col(0.2f, 0.3f, 0.6f, 1)).alpha(tw * 0.9f));
        }
    } else if (style == 3) {  // закат
        Col a = dark ? Col(0.10f, 0.06f, 0.26f) : Col(0.55f, 0.6f, 0.9f);
        Col b = Col(0.95f, 0.42f, 0.35f);
        Col c = dark ? Col(0.04f, 0.02f, 0.08f) : Col(0.3f, 0.2f, 0.4f);
        float hz = r.y + r.h * 0.62f;
        g.rectGrad(Rect(r.x, r.y, r.w, hz - r.y), a, b, 0);
        g.rectGrad(Rect(r.x, hz, r.w, r.b() - hz), c.mix(b, 0.25f), c, 0);
        float sr = u * 0.16f;
        g.circle(r.cx(), hz - sr * 0.15f, sr, Col(1.f, 0.78f, 0.45f, 0.85f));
        g.rect(Rect(r.x, hz, r.w, std::max(1.f, u * 0.004f)), Col(1.f, 0.7f, 0.5f, 0.6f), 0);
    } else if (style == 4) {  // сетка
        g.rectGrad(r, theme::homeTop, theme::bg, 0);
        float hz = r.y + r.h * 0.5f;
        Col lc = theme::accent2.alpha(dark ? 0.35f : 0.3f);
        float lw = std::max(1.f, u / 300.f);
        for (int k = -10; k <= 10; k++) g.line(r.cx() + k * u * 0.03f, hz, r.cx() + k * r.w * 0.16f, r.b(), lw, lc);
        float ph = std::fmod(t * 0.15f, 1.f);
        for (int i = 0; i < 9; i++) {
            float q = (i + ph) / 9.f;
            float y = hz + (r.b() - hz) * q * q;
            g.line(r.x, y, r.r(), y, lw, lc.alpha(0.4f + q * 0.8f));
        }
        g.rect(Rect(r.x, hz, r.w, std::max(1.f, u * 0.004f)), theme::accent.alpha(0.6f), 0);
    }
    g.popClip();
}

}  // namespace eng
