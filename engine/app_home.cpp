// Стартовый экран PrismLab: список проектов с превью, создание, переименование, копия, удаление.
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>

#include "app.h"
#include "log.h"

namespace eng {

static bool readAll(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}
static bool writeAll(const std::string& path, const void* d, size_t n) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write((const char*)d, (std::streamsize)n);
    return (bool)f;
}

static std::string cleanName(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') o += '_';
        else o += c;
    }
    while (!o.empty() && (o.back() == ' ' || o.back() == '.')) o.pop_back();
    return o.empty() ? "Проект" : o;
}

static std::string dateText(long long t) {
    static const char* mon[12] = {"янв", "фев", "мар", "апр", "мая", "июн", "июл", "авг", "сен", "окт", "ноя", "дек"};
    time_t tt = (time_t)t;
    struct tm lt;
    localtime_r(&tt, &lt);
    char b[48];
    std::snprintf(b, sizeof b, "%d %s %d, %02d:%02d", lt.tm_mday, mon[lt.tm_mon % 12], lt.tm_year + 1900, lt.tm_hour, lt.tm_min);
    return b;
}

static std::string sizeText(long long n) {
    char b[32];
    if (n < 1024) std::snprintf(b, sizeof b, "%lld Б", n);
    else if (n < 1024 * 1024) std::snprintf(b, sizeof b, "%.0f КБ", n / 1024.0);
    else std::snprintf(b, sizeof b, "%.1f МБ", n / 1048576.0);
    return b;
}

void App::freeThumbs() {
    for (auto& p : homeList_) if (p.tex) { glDeleteTextures(1, &p.tex); p.tex = 0; }
    homeList_.clear();
}

void App::refreshHome() {
    freeThumbs();
    for (const std::string& n : listProjects()) {
        ProjInfo pi;
        pi.name = n;
        struct stat st;
        if (stat((projectsDir() + "/" + n + ".prismlab").c_str(), &st) == 0) { pi.mtime = (long long)st.st_mtime; pi.size = (long long)st.st_size; }
        std::string th = projectsDir() + "/" + n + ".thm";
        if (stat(th.c_str(), &st) == 0) pi.size += (long long)st.st_size;
        homeList_.push_back(pi);
    }
    std::sort(homeList_.begin(), homeList_.end(), [](const ProjInfo& a, const ProjInfo& b) { return a.mtime > b.mtime; });
    struct stat st;
    hasAutosave_ = !plat.dataDir.empty() && stat((plat.dataDir + "/autosave.prismlab").c_str(), &st) == 0 && st.st_size > 20;
    homeDirty_ = false;
}

void App::makeThumb(const std::string& name) {
    if (plat.dataDir.empty() || !renderer.ready()) return;
    const int w = 320, h = 180;
    View v;
    const Object* cam = scene.find(scene.activeCamera);
    if (cam) v = Renderer::cameraView(*cam, (float)w / h);
    else v = currentView((float)w / h);
    std::vector<uint8_t> rgba;
    if (!renderer.renderStill(scene, v, w, h, 1, rgba) || rgba.size() < (size_t)w * h * 4) return;
    renderer.freeStillTargets();
    std::vector<uint8_t> out(8 + (size_t)w * h * 3);
    out[0] = 'T'; out[1] = 'H'; out[2] = 'M'; out[3] = '1';
    out[4] = w & 255; out[5] = w >> 8; out[6] = h & 255; out[7] = h >> 8;
    for (int i = 0; i < w * h; i++) { out[8 + i * 3] = rgba[i * 4]; out[9 + i * 3] = rgba[i * 4 + 1]; out[10 + i * 3] = rgba[i * 4 + 2]; }
    writeAll(projectsDir() + "/" + name + ".thm", out.data(), out.size());
    homeDirty_ = true;
}

void App::deleteProject(const std::string& name) {
    std::remove((projectsDir() + "/" + name + ".prismlab").c_str());
    std::remove((projectsDir() + "/" + name + ".thm").c_str());
    homeDirty_ = true;
}

void App::duplicateProject(const std::string& name) {
    std::vector<uint8_t> d;
    if (!readAll(projectsDir() + "/" + name + ".prismlab", d)) { toast("Не удалось скопировать"); return; }
    std::string nn = name + " копия";
    for (int i = 2; i < 100; i++) {
        struct stat st;
        if (stat((projectsDir() + "/" + nn + ".prismlab").c_str(), &st) != 0) break;
        nn = name + " копия " + std::to_string(i);
    }
    writeAll(projectsDir() + "/" + nn + ".prismlab", d.data(), d.size());
    std::vector<uint8_t> t;
    if (readAll(projectsDir() + "/" + name + ".thm", t)) writeAll(projectsDir() + "/" + nn + ".thm", t.data(), t.size());
    homeDirty_ = true;
}

void App::renameProject(const std::string& from, const std::string& toRaw) {
    std::string to = cleanName(toRaw);
    if (to == from) return;
    struct stat st;
    if (stat((projectsDir() + "/" + to + ".prismlab").c_str(), &st) == 0) { toast("Проект с таким именем уже есть"); return; }
    std::rename((projectsDir() + "/" + from + ".prismlab").c_str(), (projectsDir() + "/" + to + ".prismlab").c_str());
    std::rename((projectsDir() + "/" + from + ".thm").c_str(), (projectsDir() + "/" + to + ".thm").c_str());
    homeDirty_ = true;
}

void App::goHome() {
    playing_ = false;
    modal_ = Modal();
    tab_ = Tab::None;
    selBone_ = -1;
    if (!plat.dataDir.empty()) {
        if (scene.name.empty() || scene.name == "Untitled") {
            std::string nn;
            for (int i = 1; i < 1000; i++) {
                nn = "Проект " + std::to_string(i);
                struct stat st;
                if (stat((projectsDir() + "/" + nn + ".prismlab").c_str(), &st) != 0) break;
            }
            scene.name = nn;
        }
        saveProjectAs(scene.name, true);
        thumbPending_ = scene.name;
        onPause();
    }
    home_ = true;
    homeDirty_ = true;
}

// ---------------------------------------------------------------- экран
void App::drawHome() {
    Gfx& g = ui.g;
    float S = ui.S;
    if (homeDirty_) refreshHome();
    g.rectGrad(Rect(0, 0, (float)W_, (float)H_), theme::homeTop, theme::bg, 0);

    float cw = std::min((float)W_ - 24 * S, 680 * S);
    float x0 = (W_ - cw) * 0.5f;
    float topPad = 30 * S;
    // шапка
    g.text("PrismLab", x0, topPad + 36 * S, 34 * S, theme::text);
    g.text("Сцены и анимация в кармане", x0, topPad + 58 * S, 13.5f * S, theme::textDim);
    Rect info(x0 + cw - 48 * S, topPad + 8 * S, 48 * S, 48 * S);
    if (ui.iconBtn(info, Icon::Gear, false, nullptr, true)) openMainMenu();

    Rect list(0, topPad + 84 * S, (float)W_, H_ - (topPad + 84 * S));
    uint32_t zone = ui.zone(list);
    (void)zone;
    float y = list.y - scrollHome_.off;
    float top = list.y - scrollHome_.off;
    ui.beginScroll(list, scrollHome_, contentHome_);
    auto bigCard = [&](Icon ic, const std::string& title, const std::string& sub, bool primary) {
        Rect r(x0, y, cw, 78 * S);
        y += 78 * S + 12 * S;
        uint32_t id = ui.newId();
        ui.zone(r);
        (void)id;
        bool tap = false;
        ui.listRow(r, false, &tap);
        if (primary) g.rectGrad(r, theme::accent, theme::accentEnd, 14 * S);
        else g.rect(r, theme::panel2, 14 * S);
        g.rect(Rect(r.x + 14 * S, r.cy() - 22 * S, 44 * S, 44 * S), primary ? theme::onAccent.alpha(0.18f) : theme::text.alpha(0.08f), 12 * S);
        g.icon(ic, Rect(r.x + 14 * S, r.cy() - 22 * S, 44 * S, 44 * S).inset(10 * S), primary ? theme::onAccent : theme::text);
        g.text(title, r.x + 74 * S, r.cy() - (sub.empty() ? -6 : 2) * S, 18 * S, primary ? theme::onAccent : theme::text);
        if (!sub.empty()) g.text(g.fit(sub, 12.5f * S, r.w - 90 * S), r.x + 74 * S, r.cy() + 20 * S, 12.5f * S, primary ? theme::onAccent.alpha(0.78f) : theme::textDim);
        return tap;
    };
    if (bigCard(Icon::Plus, "Новый проект", "Пустая сцена с камерой и светом", true)) {
        newProject();
        home_ = false;
    }
    if (hasAutosave_) {
        if (bigCard(Icon::Film, "Продолжить", "Последняя сессия (автосохранение)", false)) {
            loadAutosave();
            home_ = false;
        }
    }
    if (bigCard(Icon::Model, "Импортировать модель", "GLB, glTF, OBJ, STL", false)) {
        if (plat.importModels) {
            newProject();
            home_ = false;
            plat.importModels();
        } else toast("Импорт недоступен");
    }
    y += 6 * S;
    g.text("Проекты", x0 + 4 * S, y + 18 * S, 15 * S, theme::textDim);
    y += 32 * S;
    if (homeList_.empty()) {
        g.textIn(Rect(x0, y, cw, 60 * S), "Сохранённых проектов пока нет", 13.5f * S, theme::textDim, 1);
        y += 60 * S;
    }
    std::string actName, actKind;
    for (size_t i = 0; i < homeList_.size(); i++) {
        ProjInfo& p = homeList_[i];
        Rect r(x0, y, cw, 98 * S);
        y += 98 * S + 10 * S;
        if (r.b() < list.y - 4 * S || r.y > list.b() + 4 * S) continue;
        bool tap = false;
        ui.listRow(r, false, &tap);
        g.rect(r, theme::panel2, 14 * S);
        // превью
        Rect th(r.x + 10 * S, r.y + 10 * S, 128 * S, 78 * S);
        g.rect(th, theme::thumb, 10 * S);
        if (!p.tex && !p.thumbTried) {
            p.thumbTried = true;
            std::vector<uint8_t> d;
            if (readAll(projectsDir() + "/" + p.name + ".thm", d) && d.size() > 8 && d[0] == 'T' && d[1] == 'H' && d[2] == 'M' && d[3] == '1') {
                int tw = d[4] | (d[5] << 8), tht = d[6] | (d[7] << 8);
                if (tw > 0 && tht > 0 && d.size() >= 8 + (size_t)tw * tht * 3) p.tex = Gfx::makeTexture(d.data() + 8, tw, tht);
            }
        }
        if (p.tex) {
            // вписать 16:9 в рамку
            g.image(p.tex, th.inset(1.5f * S), 1.f);
        } else g.icon(Icon::Cube, th.inset(24 * S), theme::textDim.alpha(0.6f));
        float tx = th.r() + 14 * S;
        float maxW = r.r() - tx - 60 * S;
        g.text(g.fit(p.name, 16 * S, maxW), tx, r.y + 36 * S, 16 * S, theme::text);
        g.text(g.fit(dateText(p.mtime), 12 * S, maxW), tx, r.y + 58 * S, 12 * S, theme::textDim);
        g.text(sizeText(p.size), tx, r.y + 76 * S, 12 * S, theme::textDim);
        Rect dots(r.r() - 52 * S, r.cy() - 24 * S, 48 * S, 48 * S);
        if (ui.iconBtn(dots, Icon::Menu, false, nullptr, false)) {
            actName = p.name;
            actKind = "menu";
        } else if (tap) {
            actName = p.name;
            actKind = "open";
        }
    }
    contentHome_ = y - top + 40 * S;
    ui.endScroll();

    if (actKind == "open") {
        if (openProject(actName)) home_ = false;
    } else if (actKind == "menu") {
        std::string nm = actName;
        pickList(nm, {"Открыть", "Переименовать", "Дублировать", "Удалить"},
                 [this, nm](int k) {
                     if (k == 0) { if (openProject(nm)) home_ = false; }
                     else if (k == 1) editText("Новое имя", nm, [this, nm](const std::string& s) { renameProject(nm, s); });
                     else if (k == 2) duplicateProject(nm);
                     else confirm("Удалить проект", "«" + nm + "» будет удалён без возможности восстановления.", [this, nm]() { deleteProject(nm); }, "Удалить");
                 },
                 {Icon::Folder, Icon::Save, Icon::Copy, Icon::Trash});
    }
}

}  // namespace eng
