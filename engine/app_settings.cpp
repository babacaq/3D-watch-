// Настройки приложения: темы, масштаб интерфейса, управление видом. Хранятся в settings.cfg в папке данных.
#include <cstdio>
#include <cstdlib>
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
    }
    s.theme = std::max(0, std::min(theme::kCount - 1, s.theme));
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
      << "\ninvertY=" << (settings.invertY ? 1 : 0) << "\ndoubleTapFocus=" << (settings.doubleTapFocus ? 1 : 0) << "\n";
}

// Полноэкранные настройки поверх главного экрана или редактора.
void App::drawSettings() {
    if (!settingsOpen_) return;
    Gfx& g = ui.g;
    float S = ui.S;
    Rect full(0, 0, (float)W_, (float)H_);
    g.rect(full, theme::bg, 0);
    ui.zone(full);

    float cw = std::min((float)W_ - 16 * S, 640 * S);
    float x0 = ((float)W_ - cw) * 0.5f;
    float topH = 56 * S + (home_ ? 0.f : 0.f);
    g.rect(Rect(0, 0, (float)W_, topH), theme::panel, 0);
    g.rect(Rect(0, topH - 1, (float)W_, 1), theme::line, 0);
    if (ui.iconBtn(Rect(x0, 6 * S, 44 * S, 44 * S), Icon::Back, false, nullptr, true)) { settingsOpen_ = false; return; }
    g.text("Настройки", x0 + 56 * S, 34 * S, 20 * S, theme::text);

    Rect view(0, topH, (float)W_, (float)H_ - topH);
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
            uint32_t id = ui.newId();
            ui.zone(c);
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

    sectionLabel("О приложении");
    {
        Rect hint = row(60);
        ui.label(hint, "PrismLab: сцены и анимация на телефоне. Настройки хранятся на устройстве и не требуют сети.", 12, theme::textDim, 0);
    }
    contentSettings_ = form_.y - top + 24 * S;
    ui.endScroll();
}

}  // namespace eng
