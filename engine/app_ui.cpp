// Экраны PrismLab: верхняя панель, таймлайн, вкладки, панели свойств, окна ввода.
#include <cmath>
#include <cstdio>

#include "app.h"
#include "log.h"

namespace eng {

static const char* kFxNames[] = {"Огонь", "Дым", "Вспышка выстрела", "Взрыв", "Искры", "Кровь", "Разбитое стекло"};

static Icon typeIcon(const Object& o) {
    switch (o.type) {
        case ObjType::Model: return Icon::Cube;
        case ObjType::Light: return o.light == LightKind::Sun ? Icon::Sun : (o.light == LightKind::Spot ? Icon::Bolt : Icon::Bulb);
        case ObjType::Camera: return Icon::Camera;
        case ObjType::Effect:
            switch (o.fx) {
                case FxKind::Fire: return Icon::Flame;
                case FxKind::Smoke: return Icon::Smoke;
                case FxKind::Muzzle: return Icon::Bolt;
                case FxKind::Explosion: return Icon::Flame;
                case FxKind::Blood: return Icon::Drop;
                case FxKind::Glass: return Icon::Glass;
                default: return Icon::Sparks;
            }
    }
    return Icon::Cube;
}

static std::string fmtNum(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.4f", v);
    std::string s = b;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s.empty() || s == "-") s = "0";
    return s;
}

static uint32_t upperCp(uint32_t c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0x430 && c <= 0x44F) return c - 0x20;
    if (c == 0x451) return 0x401;
    return c;
}
static std::string utf8(uint32_t c) {
    std::string s;
    if (c < 0x80) s += (char)c;
    else if (c < 0x800) { s += (char)(0xC0 | (c >> 6)); s += (char)(0x80 | (c & 63)); }
    else { s += (char)(0xE0 | (c >> 12)); s += (char)(0x80 | ((c >> 6) & 63)); s += (char)(0x80 | (c & 63)); }
    return s;
}
static void popUtf8(std::string& s) {
    while (!s.empty() && (((unsigned char)s.back()) & 0xC0) == 0x80) s.pop_back();
    if (!s.empty()) s.pop_back();
}

// ============================================================ формы
Rect App::row(float hDp) {
    Rect r(form_.x, form_.y, form_.w, ui.dp(hDp));
    form_.y += ui.dp(hDp + 6.f);
    return r;
}

Rect App::cell(const Rect& r, int i, int n, float gapDp) {
    float g = ui.dp(gapDp), w = (r.w - g * (n - 1)) / n;
    return Rect(r.x + i * (w + g), r.y, w, r.h);
}

void App::sectionLabel(const std::string& s) {
    form_.y += ui.dp(4);
    Rect r = row(22);
    ui.label(r, s, 12, theme::accent2, 0);
    ui.g.rect(Rect(r.x, r.b() + ui.dp(1), r.w, ui.dp(1)), theme::line, 0);
    form_.y += ui.dp(2);
}

void App::beginEdit() {
    if (!editStarted_) { scene.pushUndo(); editStarted_ = true; }
    autoSaveDirty_ = true;
}

bool App::slide(const Rect& r, const std::string& label, float& v, float lo, float hi, const char* fmt, float perDp, std::function<void(float)> apply) {
    WRes res = ui.slider(r, label, v, lo, hi, fmt, hi > lo, perDp / std::max(0.1f, ui.S));
    if (res == WRes::Tapped) { editNumber(label, v, apply); return false; }
    return res == WRes::Changed;
}

void App::propF(Object& o, const std::string& label, std::function<float&(Object&)> ref, float lo, float hi, const char* fmt, float perDp) {
    float v = ref(o);
    int id = o.id;
    auto apply = [this, id, ref, lo, hi](float nv) {
        Object* ob = scene.find(id);
        if (!ob) return;
        beginEdit();
        if (hi > lo) nv = clampf(nv, lo, hi);
        ref(*ob) = nv;
    };
    Rect r = row(38);
    float rng = hi > lo ? (hi - lo) : 0.f;
    if (slide(r, label, v, lo, hi, fmt, perDp > 0 ? perDp : rng / std::max(1.f, r.w / ui.S), apply)) apply(v);
}

void App::editNumber(const std::string& title, float cur, std::function<void(float)> cb) {
    modal_ = Modal();
    modal_.kind = Modal::Keypad;
    modal_.title = title;
    modal_.buf = fmtNum(cur);
    modal_.first = true;
    modal_.onText = [cb](const std::string& s) { cb((float)std::atof(s.c_str())); };
}

void App::editText(const std::string& title, const std::string& cur, std::function<void(const std::string&)> cb) {
    modal_ = Modal();
    modal_.kind = Modal::Text;
    modal_.title = title;
    modal_.buf = cur;
    modal_.first = true;
    modal_.onText = cb;
}

void App::pickList(const std::string& title, const std::vector<std::string>& items, std::function<void(int)> cb, const std::vector<Icon>& icons) {
    modal_ = Modal();
    modal_.kind = Modal::List;
    modal_.title = title;
    modal_.items = items;
    modal_.icons = icons;
    modal_.onPick = cb;
}

void App::confirm(const std::string& title, const std::string& msg, std::function<void()> yes, const std::string& yesLabel) {
    modal_ = Modal();
    modal_.kind = Modal::Confirm;
    modal_.title = title;
    modal_.msg = msg;
    modal_.onYes = yes;
    modal_.yesLabel = yesLabel;
}

// ============================================================ меню
void App::openMainMenu() {
    pickList("PrismLab", {"Новый проект", "Импортировать модель…", "Настройки и темы…", "О PrismLab"},
             [this](int i) {
                 switch (i) {
                     case 0: newProject(); break;
                     case 1: if (plat.importModels) { newProject(); plat.importModels(); } else toast("Импорт недоступен"); break;
                     case 2: openSettings(); break;
                     default:
                         modal_ = Modal();
                         modal_.kind = Modal::Info;
                         modal_.title = "PrismLab";
                         modal_.msg = "Сцены и анимация на телефоне: импорт моделей OBJ, GLB/glTF и STL, ключевые кадры, кости и позы, физика, огонь, дым, кровь, вспышки выстрелов и взрывы, экспорт PNG и MP4.\nПроект .proj из Prisma 3D не открывается напрямую: в Prisma 3D выберите экспорт glTF (.gltf вместе с .bin и текстурами) или OBJ и импортируйте сюда.";
                         break;
                 }
             },
             {Icon::Plus, Icon::Model, Icon::Palette, Icon::Gear});
}

void App::openAddMenu() {
    pickList("Добавить",
             {"Модель из файла…", "Куб", "Сфера", "Цилиндр", "Конус", "Плоскость", "Солнце", "Точечный свет", "Камера", "Огонь", "Дым", "Искры",
              "Выстрел: пистолет", "Выстрел: дробовик", "Выстрел: автомат (очередь)", "Взрыв", "Кровь: брызги", "Кровь: течёт", "Разбить стекло (эффект)", "Фонарик", "Стекло (панель)"},
             [this](int i) { addItem(i); },
             {Icon::Folder, Icon::Cube, Icon::Cube, Icon::Cube, Icon::Cube, Icon::Cube, Icon::Sun, Icon::Bulb, Icon::Camera, Icon::Flame, Icon::Smoke, Icon::Sparks,
              Icon::Bolt, Icon::Bolt, Icon::Bolt, Icon::Flame, Icon::Drop, Icon::Drop, Icon::Glass, Icon::Bolt, Icon::Glass});
}

void App::addFxMenu() {
    pickList("Добавить эффект",
             {"Огонь", "Дым", "Искры", "Выстрел: пистолет", "Выстрел: дробовик", "Выстрел: автомат (очередь)", "Взрыв", "Кровь: брызги", "Кровь: течёт", "Разбить стекло"},
             [this](int k) { addItem(9 + k); },
             {Icon::Flame, Icon::Smoke, Icon::Sparks, Icon::Bolt, Icon::Bolt, Icon::Bolt, Icon::Flame, Icon::Drop, Icon::Drop, Icon::Glass});
}

void App::addItem(int i) {
    scene.pushUndo();
    autoSaveDirty_ = true;
    int id = -1;
    switch (i) {
        case 0: if (plat.importModels) plat.importModels(); else toast("Импорт недоступен"); return;
        case 1: id = scene.addPrimitive("cube"); break;
        case 2: id = scene.addPrimitive("sphere"); break;
        case 3: id = scene.addPrimitive("cylinder"); break;
        case 4: id = scene.addPrimitive("cone"); break;
        case 5: id = scene.addPrimitive("plane"); break;
        case 6: id = scene.addLight(LightKind::Sun); break;
        case 7: id = scene.addLight(LightKind::Point); break;
        case 8: id = scene.addCamera(); break;
        case 9: id = scene.addEffect(FxKind::Fire); break;
        case 10: id = scene.addEffect(FxKind::Smoke); break;
        case 11: id = scene.addEffect(FxKind::Sparks); break;
        case 12: case 13: case 14: {
            id = scene.addEffect(FxKind::Muzzle);
            if (Object* e = scene.find(id)) {
                e->fxVariant = i - 12;
                e->fxRate = i == 14 ? 3 : 0;
                e->fxEnd = e->fxStart + (i == 14 ? 18 : 8);
            }
            break;
        }
        case 15: id = scene.addEffect(FxKind::Explosion); break;
        case 16: case 17: {
            id = scene.addEffect(FxKind::Blood);
            if (Object* e = scene.find(id)) {
                e->fxVariant = i - 16;
                e->fxEnd = e->fxStart + (i == 17 ? 36 : 14);
            }
            break;
        }
        case 18: id = scene.addEffect(FxKind::Glass); break;
        case 19: id = scene.addLight(LightKind::Spot); break;
        case 20: id = scene.addGlass(); break;
    }
    uploadPending();
    if (id >= 0) {
        // новые объекты появляются в точке, на которую смотрит камера вида
        Object* o = scene.find(id);
        if (o && i != 8 && i != 6 && i != 18) {
            Vec3 t, r, s;
            scene.localTRS(*o, t, r, s);
            if (o->type == ObjType::Model || o->type == ObjType::Effect || o->type == ObjType::Light) {
                t.x = target_.x; t.z = target_.z;
                if (o->type == ObjType::Light) t.y = target_.y + 2.f;
                scene.setLocal(id, t, r, s);
            }
        }
        scene.selected = id;
        scene.resetEffects();
        scene.simulateTo(curFrame());
        if (i == 8) { Object* c = scene.find(id); if (c) { View v = currentView(1.f); scene.setWorld(id, inverse(v.view)); } }
        if (o && o->type == ObjType::Effect) tab_ = Tab::Fx;
    }
}

// ============================================================ верх, вьюпорт
void App::drawTopBar() {
    Gfx& g = ui.g;
    float S = ui.S;
    g.rect(rTop_, theme::panel, 0);
    g.rect(Rect(0, rTop_.b() - 1, rTop_.w, 1), theme::line, 0);
    float pad = 8 * S, bs = (land_ ? 40 : 44) * S, y = rTop_.y + (rTop_.h - bs) * 0.5f;
    if (ui.iconBtn(Rect(pad, y, bs, bs), Icon::Home, false, nullptr, true)) { goHome(); return; }
    float xr = rTop_.r() - pad;
    Rect exp(xr - (land_ ? 108 : 92) * S, y, (land_ ? 108 : 92) * S, bs);
    if (ui.button(exp, "Экспорт", 1, 14)) {
        int n = scene.proj.frameEnd - scene.proj.frameStart + 1;
        pickList("Экспорт из камеры",
                 {"Кадр PNG (текущий)", "Видео MP4 (" + std::to_string(n) + " кадров)", "Анимация скелета GLB (для Godot)…", "Настройки рендера…"},
                 [this](int i) {
                     if (i == 0) startStillExport();
                     else if (i == 1) startVideoExport();
                     else if (i == 2) skeletalExportMenu();
                     else tab_ = Tab::Render;
                 },
                 {Icon::Image, Icon::Film, Icon::Rig, Icon::Gear});
    }
    xr = exp.x - 6 * S;
    Rect rd(xr - bs, y, bs, bs), ud(rd.x - bs - 4 * S, y, bs, bs);
    Col dim = theme::textDim;
    if (ui.iconBtn(rd, Icon::Redo, false, scene.canRedo() ? nullptr : &dim, false)) { if (scene.redo()) { scene.resetEffects(); scene.simulateTo(curFrame()); } }
    if (ui.iconBtn(ud, Icon::Undo, false, scene.canUndo() ? nullptr : &dim, false)) { if (scene.undo()) { scene.resetEffects(); scene.simulateTo(curFrame()); } }
    // имя проекта
    float nx = pad + bs + 8 * S, nw = ud.x - nx - 6 * S;
    std::string nm = (scene.name.empty() || scene.name == "Untitled") ? "Без названия" : scene.name;
    Rect nr(nx, y, std::max(40 * S, nw), bs);
    g.textIn(nr, g.fit(nm, 16 * S, nr.w - 8 * S), 16 * S, theme::text, 1);
    if (ui.button(nr, "", 3, 16)) {
        editText("Имя проекта", nm == "Без названия" ? "Проект 1" : nm, [this](const std::string& s) {
            std::string old = scene.name;
            if (!old.empty() && old != "Untitled" && old != s) {
                renameProject(old, s);
                scene.name = s;
            }
            saveProjectAs(s);
        });
    }
}

bool App::boneScreenPos(const Object& o, int node, float& sx, float& sy) const {
    if (!o.anim || node < 0 || node >= (int)o.anim->world.size()) return false;
    return project(transformPoint(o.world * o.fit * o.anim->world[node], {0, 0, 0}), sx, sy);
}

bool App::boneAtScreen(float x, float y, int& node) const {
    const Object* o = scene.find(scene.selected);
    if (!o || o->type != ObjType::Model || !o->anim) return false;
    float best = ui.dp(30);
    node = -1;
    for (int n : scene.boneNodes(*o)) {
        float sx, sy;
        if (!boneScreenPos(*o, n, sx, sy)) continue;
        float d = std::hypot(sx - x, sy - y);
        if (d < best) { best = d; node = n; }
    }
    return node >= 0;
}

void App::drawBoneOverlay() {
    const Object* o = scene.find(scene.selected);
    if (!o || o->type != ObjType::Model || !o->anim || !o->visibleNow) return;
    if (!showBones_ && tab_ != Tab::Bones) return;
    const Model& m = *scene.models[o->model].model;
    std::vector<int> bones = scene.boneNodes(*o);
    if (bones.empty()) return;
    Gfx& g = ui.g;
    float S = ui.S;
    g.pushClip(viewRect_);
    std::vector<char> isBone(m.nodes.size(), 0);
    for (int n : bones) isBone[n] = 1;
    int cf = curFrame();
    // кости: по умолчанию только точки; линии к родителю включаются в настройках, у выбранной кости линия есть всегда
    for (int n : bones) {
        if (!settings.boneLines && n != selBone_) continue;
        float sx, sy;
        if (!boneScreenPos(*o, n, sx, sy)) continue;
        int p = m.nodes[n].parent;
        while (p >= 0 && !isBone[p]) p = m.nodes[p].parent;
        if (p >= 0) {
            float px, py;
            if (boneScreenPos(*o, p, px, py)) {
                float dx = sx - px, dy = sy - py, len = std::hypot(dx, dy);
                if (len > 1.f) {
                    bool selB = n == selBone_;
                    Col c = selB ? theme::orange : Col(0.3f, 0.85f, 1.f, 0.9f);
                    g.line(px, py, sx, sy, (selB ? 1.6f : 1.1f) * S, c.alpha(selB ? 0.8f : 0.45f));
                }
            }
        }
    }
    for (int n : bones) {
        float sx, sy;
        if (!boneScreenPos(*o, n, sx, sy)) continue;
        bool sel = n == selBone_;
        bool keyed = scene.boneHasKey(*o, n, cf);
        bool any = false;
        for (auto& tr : o->boneTracks) if (tr.node == n && !tr.keys.empty()) any = true;
        float r = (sel ? 8.f : 4.2f) * S;
        Col fill = sel ? theme::orange : (keyed ? theme::keyCol : (any ? Col(1.f, 0.85f, 0.4f, 0.95f) : Col(0.25f, 0.85f, 1.f, 0.95f)));
        g.circle(sx, sy, r + 1.2f * S, Col(0.02f, 0.02f, 0.03f, sel ? 0.85f : 0.55f));
        g.circle(sx, sy, r, sel ? fill : fill.alpha(0.8f));
        if (sel) g.ring(sx, sy, r + 6 * S, 1.8f * S, theme::orange.alpha(0.9f));
    }
    // подпись выбранной кости
    if (selBone_ >= 0 && selBone_ < (int)m.nodes.size()) {
        float sx, sy;
        if (boneScreenPos(*o, selBone_, sx, sy)) {
            std::string nm = m.nodes[selBone_].name.empty() ? "кость " + std::to_string(selBone_) : m.nodes[selBone_].name;
            float fs = 12 * S, w = g.textWidth(nm, fs) + 16 * S;
            Rect r(std::min(std::max(viewRect_.x + 4 * S, sx - w * 0.5f), viewRect_.r() - w - 4 * S), sy - 40 * S, w, 22 * S);
            g.rect(r, theme::overlay.alpha(1.2f), 11 * S);
            g.textIn(r, nm, fs, theme::text, 1);
        }
    }
    g.popClip();
}

void App::drawViewportOverlay() {
    Gfx& g = ui.g;
    float S = ui.S;
    if (camView_) {
        Rect cv = cameraViewRect();
        g.rect(Rect(rVP_.x, rVP_.y, rVP_.w, cv.y - rVP_.y), theme::bg, 0);
        g.rect(Rect(rVP_.x, cv.b(), rVP_.w, rVP_.b() - cv.b()), theme::bg, 0);
        g.rect(Rect(rVP_.x, cv.y, cv.x - rVP_.x, cv.h), theme::bg, 0);
        g.rect(Rect(cv.r(), cv.y, rVP_.r() - cv.r(), cv.h), theme::bg, 0);
        g.stroke(cv, theme::accent2.alpha(0.7f), 0, 1.5f * S);
    } else {
        drawBoneOverlay();
    }
    float bs = (land_ ? 40 : 46) * S, gap = 6 * S;
    int maxRows = std::max(1, (int)((rVP_.h - 16 * S) / (bs + gap)));
    auto slot = [&](bool left, int k) {
        int col = k / maxRows, rw = k % maxRows;
        float x = left ? rVP_.x + 8 * S + col * (bs + gap) : rVP_.r() - 8 * S - bs - col * (bs + gap);
        return Rect(x, rVP_.y + 8 * S + rw * (bs + gap), bs, bs);
    };
    const Col back(0.04f, 0.03f, 0.1f, 0.55f);
    const Icon ic[3] = {Icon::Move, Icon::Rotate, Icon::Scale};
    for (int i = 0; i < 3; i++) {
        Rect r = slot(true, i);
        g.rect(r, back, 10 * S);
        if (ui.iconBtn(r, ic[i], gizmoEff() == i, nullptr, gizmoEff() == i)) gizmoMode_ = i;
    }
    {
        Rect sr = slot(true, 3);
        g.rect(sr, back, 10 * S);
        if (ui.iconBtn(sr, Icon::Snap, snap_, nullptr, snap_)) snap_ = !snap_;
    }
    int k = 0;
    auto side = [&](Icon icn, bool act, const Col* tint = nullptr) {
        Rect r = slot(false, k++);
        g.rect(r, back, 10 * S);
        return ui.iconBtn(r, icn, act, tint, act);
    };
    if (side(Icon::Camera, camView_)) camView_ = !camView_;
    if (side(Icon::Rig, showBones_ || tab_ == Tab::Bones)) {
        showBones_ = !showBones_;
        if (!showBones_ && tab_ == Tab::Bones) tab_ = Tab::None;
    }
    if (side(Icon::Eye, false)) focusSelected();
    if (side(Icon::Layers, false)) viewPresetMenu();
    const Object* sel = scene.find(scene.selected);
    if (sel && sel->type == ObjType::Camera) {
        float w = 150 * S;
        Rect r(rVP_.r() - 8 * S - w, rVP_.b() - 100 * S, w, 40 * S);
        if (ui.button(r, "Камера = вид", 0, 12.5f)) alignCameraToView();
        if (sel->id != scene.activeCamera) {
            Rect r2(r.x, r.y + 46 * S, w, 40 * S);
            if (ui.button(r2, "Сделать активной", 1, 12.5f)) { scene.pushUndo(); scene.activeCamera = sel->id; }
        }
    }
    // подпись внизу
    std::string cap;
    if (camView_) cap = "Вид из камеры · тяните: поворот, щипок: приближение";
    else if (sel) cap = sel->name;
    if (!cap.empty()) {
        float fs = 13 * S, w = std::min(rVP_.w - 16 * S, g.textWidth(cap, fs) + 24 * S);
        Rect r(rVP_.x + 8 * S, rVP_.b() - 36 * S, w, 28 * S);
        g.rect(r, theme::overlay, 14 * S);
        g.textIn(r, g.fit(cap, fs, w - 14 * S), fs, theme::text, 1);
    }
}

// ============================================================ таймлайн
// ---- Таймлайн: строки-дорожки (тело, кости по отдельности, события, эффект); ключи можно тянуть ----
namespace {
struct TLRow {
    int kind = 0;  // 0 всё тело, 1 свёртка костей, 2 кость, 3 эффект, 4 события (прицепы, клипы, видимость)
    int node = -1;
    std::string name;
    std::vector<int> fr;
    bool dim = false;
};
}  // namespace

bool App::tlMoveKey(int kind, int node, int from, int to) {
    Object* o = scene.find(scene.selected);
    if (!o || from == to) return false;
    to = std::max(scene.proj.frameStart, std::min(scene.proj.frameEnd, to));
    if (to == from) return false;
    if (kind == 0) {
        for (auto& k : o->keys) if (k.frame == to) return false;
        for (auto& k : o->keys) if (k.frame == from) { k.frame = to; break; }
        std::sort(o->keys.begin(), o->keys.end(), [](const TKey& a, const TKey& b) { return a.frame < b.frame; });
    } else if (kind == 2) {
        for (auto& tr : o->boneTracks) {
            if (tr.node != node) continue;
            for (auto& k : tr.keys) if (k.frame == to) return false;
            for (auto& k : tr.keys) if (k.frame == from) { k.frame = to; break; }
            std::sort(tr.keys.begin(), tr.keys.end(), [](const BoneKey& a, const BoneKey& b) { return a.frame < b.frame; });
        }
    } else return false;
    scene.evaluate(scene.frame());
    return true;
}

void App::tlJumpKey(int dir) {
    Object* o = scene.find(scene.selected);
    if (!o) return;
    std::vector<int> fr;
    bool boneMode = o->type == ObjType::Model && selBone_ >= 0 && (tab_ == Tab::Bones || tlTarget_ == 2);
    if (boneMode) { for (auto& tr : o->boneTracks) if (tr.node == selBone_) for (auto& k : tr.keys) fr.push_back(k.frame); }
    else for (auto& k : o->keys) fr.push_back(k.frame);
    int cf = curFrame(), best = -1;
    for (int f : fr) {
        if (dir < 0 && f < cf && (best < 0 || f > best)) best = f;
        if (dir > 0 && f > cf && (best < 0 || f < best)) best = f;
    }
    if (best < 0) { toast(dir < 0 ? "Ключей слева нет" : "Ключей справа нет"); return; }
    playing_ = false;
    gotoFrame(best);
}

void App::drawTimeline() {
    Gfx& g = ui.g;
    float S = ui.S;
    g.rect(rTL_, theme::panel, 0);
    g.rect(Rect(rTL_.x, rTL_.y, rTL_.w, 1), theme::line, 0);
    // автоматика: в костях ключи «по частям», вне костей - раскрытие по желанию
    if (tab_ != tlPrevTab_) {
        if (tab_ == Tab::Bones) { tlBodyOpen_ = false; tlBonesOpen_ = true; tlTarget_ = 2; }
        else if (tlPrevTab_ == Tab::Bones) { tlBodyOpen_ = true; tlBonesOpen_ = false; tlTarget_ = 0; }
        tlPrevTab_ = tab_;
    }
    float pad = 8 * S, bs = 34 * S, y = rTL_.y + 5 * S, x = rTL_.x + pad;
    bool wide = rTL_.w > 560 * S;
    int cf = curFrame();
    int a = scene.proj.frameStart, b = scene.proj.frameEnd;
    Object* sel = scene.find(scene.selected);
    bool isModel = sel && sel->type == ObjType::Model && sel->anim;
    bool boneMode = isModel && selBone_ >= 0 && (tab_ == Tab::Bones || tlTarget_ == 2);

    // ---- кнопки: слева управление временем ----
    if (wide) {
        if (ui.iconBtn(Rect(x, y, bs, bs), Icon::ToStart, false, nullptr, false)) { playing_ = false; gotoFrame(a); }
        x += bs + 2 * S;
    }
    if (ui.iconBtn(Rect(x, y, bs, bs), Icon::Prev, false, nullptr, false)) { playing_ = false; gotoFrame(cf - 1); }
    x += bs + 2 * S;
    if (ui.iconBtn(Rect(x, y, bs + 8 * S, bs), playing_ ? Icon::Pause : Icon::Play, true, nullptr, true)) {
        playing_ = !playing_;
        if (playing_ && cf >= b) gotoFrame(a);
        lastSimFrame_ = curFrame();
    }
    x += bs + 10 * S;
    if (ui.iconBtn(Rect(x, y, bs, bs), Icon::Next, false, nullptr, false)) { playing_ = false; gotoFrame(cf + 1); }
    x += bs + 2 * S;
    if (wide) {
        if (ui.iconBtn(Rect(x, y, bs, bs), Icon::ToEnd, false, nullptr, false)) { playing_ = false; gotoFrame(b); }
        x += bs + 2 * S;
    }
    // ---- справа: ключи ----
    float rx = rTL_.r() - pad;
    Rect tallR(rx - 28 * S, y, 28 * S, bs);
    if (ui.iconBtn(tallR, tlTall_ ? Icon::ChevDown : Icon::ChevUp, false, nullptr, false)) tlTall_ = !tlTall_;
    Rect autoR(tallR.x - 48 * S - 2 * S, y, 48 * S, bs);
    if (ui.button(autoR, "Авто", scene.autoKey ? 1 : 0, 12)) scene.autoKey = !scene.autoKey;
    Rect delR(autoR.x - bs - 2 * S, y, bs, bs), addR(delR.x - bs - 2 * S, y, bs, bs);
    Rect nkR(addR.x - bs - 6 * S, y, bs, bs), pkR(nkR.x - bs - 2 * S, y, bs, bs);
    if (ui.iconBtn(addR, Icon::KeyAdd, false, nullptr, true) && sel) {
        beginEdit();
        if (boneMode) boneKeyNow();
        else if (!scene.insertKey(sel->id, cf)) toast("Объект прицеплен: его положение задаётся относительно родителя");
        editStarted_ = false;
    }
    if (ui.iconBtn(delR, Icon::Trash, false, nullptr, true) && sel) {
        beginEdit();
        bool ok = boneMode ? scene.deleteBoneKey(sel->id, selBone_, cf) : scene.deleteKey(sel->id, cf);
        if (!ok) toast("На этом кадре нет ключа");
        editStarted_ = false;
    }
    if (pkR.x > x + 56 * S) {
        if (ui.iconBtn(pkR, Icon::PrevKey, false, nullptr, false)) tlJumpKey(-1);
        if (ui.iconBtn(nkR, Icon::NextKey, false, nullptr, false)) tlJumpKey(+1);
    }

    // ---- таблица ----
    float labW = (land_ ? 104 : 86) * S;
    float rulerY = y + bs + 4 * S, rulerH = 22 * S;
    float rowH = 26 * S;
    Rect rulerL(rTL_.x + pad, rulerY, labW, rulerH);
    Rect track(rTL_.x + pad + labW + 4 * S, rulerY, rTL_.r() - pad - (rTL_.x + pad + labW + 4 * S), rulerH);
    Rect rowsView(rTL_.x + pad, rulerY + rulerH + 2 * S, rTL_.w - 2 * pad, std::max(rowH, rTL_.b() - 4 * S - (rulerY + rulerH + 2 * S)));
    float span = (float)std::max(1, b - a);
    float inL = track.x + 10 * S, inW = std::max(1.f, track.w - 20 * S);
    auto fxp = [&](float f) { return inL + (f - a) / span * inW; };
    auto frameAt = [&](float xx) { return a + (int)std::lround(clampf((xx - inL) / inW, 0.f, 1.f) * span); };
    float pxPer = inW / span;

    // кадр / всего
    {
        char buf[48];
        std::snprintf(buf, sizeof buf, "Кадр %d / %d", cf, b);
        g.rect(rulerL, theme::widget, 6 * S);
        g.textIn(rulerL, buf, 11.5f * S, theme::text, 1);
    }
    // линейка
    g.rect(track, theme::widget, 6 * S);
    int steps[] = {1, 2, 5, 10, 20, 25, 50, 100, 200, 500, 1000};
    int step = 1000;
    for (int s : steps) if (pxPer * s >= 34 * S) { step = s; break; }
    for (int f = a; f <= b; f++) {
        bool major = ((f - a) % step) == 0;
        if (!major && pxPer < 6 * S) continue;
        float xx = fxp((float)f);
        g.rect(Rect(xx - 0.5f * S, track.y + (major ? 3 : 14) * S, 1.f * S, (major ? 9 : 5) * S), theme::textDim.alpha(major ? 0.7f : 0.35f), 0);
        if (major) g.textIn(Rect(xx - 18 * S, track.y + 11 * S, 36 * S, 11 * S), std::to_string(f), 9.5f * S, theme::textDim, 1);
    }
    uint32_t rulerZ = ui.zone(track);
    if (ui.pointerDown() && ui.pressedId(rulerZ) && !ui.blocked) {
        ui.takeOwner(rulerZ);
        int f = frameAt(ui.px());
        if (f != cf) { playing_ = false; gotoFrame(f); }
    }

    // ---- строки ----
    std::vector<TLRow> rows;
    if (sel) {
        const Model* m = (isModel && sel->model >= 0 && sel->model < (int)scene.models.size()) ? scene.models[sel->model].model.get() : nullptr;
        auto boneName = [&](int n) {
            if (m && n >= 0 && n < (int)m->nodes.size() && !m->nodes[n].name.empty()) return m->nodes[n].name;
            return "кость " + std::to_string(n);
        };
        TLRow body;
        body.kind = 0;
        body.name = isModel ? "Всё тело" : "Положение";
        for (auto& k : sel->keys) body.fr.push_back(k.frame);
        if (isModel && !tlBodyOpen_) body.dim = true;
        if (sel->type != ObjType::Effect || !sel->keys.empty()) rows.push_back(body);
        if (isModel) {
            std::vector<int> nodes;
            for (auto& tr : sel->boneTracks) if (!tr.keys.empty()) nodes.push_back(tr.node);
            if (selBone_ >= 0 && tab_ == Tab::Bones && std::find(nodes.begin(), nodes.end(), selBone_) == nodes.end()) nodes.push_back(selBone_);
            std::sort(nodes.begin(), nodes.end());
            if (!nodes.empty()) {
                bool open = tlBonesOpen_;
                TLRow hdr;
                hdr.kind = 1;
                hdr.name = std::string(open ? "Кости (" : "Кости (") + std::to_string(nodes.size()) + ")";
                hdr.dim = !open;
                for (auto& tr : sel->boneTracks) for (auto& k : tr.keys) hdr.fr.push_back(k.frame);
                rows.push_back(hdr);
                if (open)
                    for (int n : nodes) {
                        TLRow br;
                        br.kind = 2; br.node = n; br.name = boneName(n);
                        for (auto& tr : sel->boneTracks) if (tr.node == n) for (auto& k : tr.keys) br.fr.push_back(k.frame);
                        rows.push_back(br);
                    }
            }
        }
        if (!sel->parents.empty() || !sel->clipKeys.empty() || !sel->visKeys.empty()) {
            TLRow ev;
            ev.kind = 4; ev.name = "События";
            rows.push_back(ev);
        }
        if (sel->type == ObjType::Effect) { TLRow fr; fr.kind = 3; fr.name = "Эффект"; rows.push_back(fr); }
    }
    // «тело» свернуто: оставляем тонкую строку-заголовок без ключей
    float contentH = rows.size() * rowH;
    g.rect(rowsView, theme::widget.alpha(0.45f), 6 * S);
    ui.beginScroll(rowsView, scrollTL_, contentH);
    uint32_t rowsZ = ui.zone(rowsView);
    float ry0 = rowsView.y - scrollTL_.off;
    float ks = 6.f * S;
    auto diamond = [&](float xx, float yy, const Col& c, float k) {
        g.tri(xx, yy - k, xx + k, yy, xx, yy + k, c);
        g.tri(xx, yy - k, xx, yy + k, xx - k, yy, c);
    };
    if (!sel) {
        g.textIn(rowsView, "Выберите объект, чтобы увидеть его ключи", 12 * S, theme::textDim, 1);
    }
    Rect barR, handleL, handleR;
    bool haveBar = false;
    for (size_t i = 0; i < rows.size(); i++) {
        const TLRow& r = rows[i];
        Rect rr(rowsView.x, ry0 + i * rowH, rowsView.w, rowH);
        if (rr.b() < rowsView.y || rr.y > rowsView.b()) continue;
        bool isSelRow = (r.kind == 2 && r.node == selBone_) || (r.kind == 0 && tlTarget_ == 0 && !r.dim && !(isModel && tab_ == Tab::Bones));
        if (i & 1) g.rect(rr, Col(1, 1, 1, 0.025f), 0);
        if (isSelRow) g.rect(rr, theme::accent.alpha(0.14f), 0);
        // подпись
        Rect lab(rr.x + 4 * S, rr.y, labW - 4 * S, rr.h);
        Col tc = r.dim ? theme::textDim : (isSelRow ? theme::text : theme::text.alpha(0.88f));
        float indent = r.kind == 2 ? 10.f * S : 0.f;
        if (r.kind == 1 || (r.kind == 0 && isModel)) {
            bool open = r.kind == 1 ? tlBonesOpen_ : tlBodyOpen_;
            g.icon(open ? Icon::ChevDown : Icon::ChevUp, Rect(lab.x - 2 * S, lab.cy() - 7 * S, 14 * S, 14 * S), theme::textDim);
            indent = 12 * S;
        }
        g.textIn(Rect(lab.x + indent, lab.y, lab.w - indent, lab.h), g.fit(r.name, 11.5f * S, lab.w - indent - 2 * S), 11.5f * S, tc, 0);
        g.rect(Rect(rr.x + labW + 2 * S, rr.y + 3 * S, 1, rr.h - 6 * S), theme::line, 0);
        float cy = rr.cy();
        if (r.dim && r.kind == 0) {
            g.rect(Rect(track.x + 6 * S, cy - 0.5f * S, track.w - 12 * S, 1.f * S), theme::textDim.alpha(0.25f), 0);
            continue;
        }
        g.rect(Rect(track.x + 4 * S, cy - 0.5f * S, track.w - 8 * S, 1.f * S), theme::line.alpha(0.6f), 0);
        if (r.kind == 4) {
            for (auto& k : sel->parents) g.circle(fxp((float)k.frame), cy, 4 * S, k.parent >= 0 ? theme::accent2 : Col(1, 0.5f, 0.3f, 1));
            for (auto& k : sel->clipKeys) g.rect(Rect(fxp((float)k.frame) - 3 * S, cy - 5 * S, 6 * S, 10 * S), theme::ok, 2 * S);
            for (auto& k : sel->visKeys) g.rect(Rect(fxp((float)k.frame) - 3 * S, cy - 5 * S, 6 * S, 10 * S), k.visible ? theme::text : theme::danger, 2 * S);
        } else if (r.kind == 3) {
            float x0 = fxp((float)sel->fxStart), x1 = fxp((float)sel->fxEnd);
            barR = Rect(x0, cy - 7 * S, std::max(10.f * S, x1 - x0), 14 * S);
            handleL = Rect(x0 - 16 * S, barR.y - 6 * S, 32 * S, barR.h + 12 * S);
            handleR = Rect(barR.r() - 16 * S, barR.y - 6 * S, 32 * S, barR.h + 12 * S);
            haveBar = true;
            g.rect(barR, theme::orange.alpha(0.85f), 5 * S);
            g.rect(Rect(barR.x, barR.y, 4 * S, barR.h), Col(1, 1, 1, 0.9f), 2 * S);
            g.rect(Rect(barR.r() - 4 * S, barR.y, 4 * S, barR.h), Col(1, 1, 1, 0.9f), 2 * S);
        } else {
            Col base = r.kind == 2 ? Col(0.3f, 0.9f, 1.f, 1.f) : (r.kind == 1 ? Col(0.55f, 0.6f, 0.7f, 0.9f) : theme::keyCol);
            for (int f : r.fr) {
                bool on = f == cf;
                bool dragging = tlDrag_.state == 1 && tlDrag_.kind == r.kind && tlDrag_.node == r.node && tlDrag_.frame == f;
                diamond(fxp((float)f), cy, on || dragging ? theme::text : base, on || dragging ? ks * 1.25f : ks);
            }
        }
    }

    // ---- касания таблицы ----
    {
        if (ui.pressedNow()) tlDrag_ = TlDrag();
        auto rowAt = [&](float yy) { return (int)std::floor((yy - ry0) / rowH); };
        auto keyNear = [&](int ri, float xx, int& frame) {
            if (ri < 0 || ri >= (int)rows.size()) return false;
            float best = 14 * S;
            bool ok = false;
            for (int f : rows[ri].fr) {
                float d = std::fabs(fxp((float)f) - xx);
                if (d <= best) { best = d; frame = f; ok = true; }
            }
            return ok;
        };
        // подписи: раскрытие и выбор кости
        Rect labZone(rowsView.x, rowsView.y, labW, rowsView.h);
        uint32_t labZ = ui.zone(labZone);
        if (ui.tapOn(labZ, labZone)) {
            int ri = rowAt(ui.py());
            if (ri >= 0 && ri < (int)rows.size()) {
                const TLRow& r = rows[ri];
                if (r.kind == 1) tlBonesOpen_ = !tlBonesOpen_;
                else if (r.kind == 0) { if (isModel) tlBodyOpen_ = !tlBodyOpen_; tlTarget_ = 0; }
                else if (r.kind == 2) { selBone_ = r.node; tlTarget_ = 2; }
            }
        }
        bool bar = haveBar && sel;
        uint32_t idM = 0, idL = 0, idR = 0;
        if (bar) { idM = ui.zone(barR.inset(0, -3 * S)); idL = ui.zone(handleL); idR = ui.zone(handleR); }
        bool onBar = bar && (ui.pressedId(idM) || ui.pressedId(idL) || ui.pressedId(idR));
        if (!ui.blocked && ui.pointerDown() && ui.pressedId(rowsZ) && !onBar) {
            if (ui.moved() && ui.axis() == 1 && tlDrag_.state == 0) {
                int ri = rowAt(ui.downY()), kf = 0;
                if (keyNear(ri, ui.downX(), kf) && (rows[ri].kind == 0 || rows[ri].kind == 2)) {
                    tlDrag_.state = 1; tlDrag_.kind = rows[ri].kind; tlDrag_.node = rows[ri].node; tlDrag_.frame = kf;
                    tlDrag_.grab = kf - frameAt(ui.downX());
                    if (rows[ri].kind == 2) { selBone_ = rows[ri].node; tlTarget_ = 2; } else tlTarget_ = 0;
                } else tlDrag_.state = 2;
                ui.takeOwner(rowsZ);
            }
            if (ui.owner() == rowsZ) {
                if (tlDrag_.state == 1) {
                    int to = frameAt(ui.px()) + tlDrag_.grab;
                    if (to != tlDrag_.frame) {
                        beginEdit();
                        if (tlMoveKey(tlDrag_.kind, tlDrag_.node, tlDrag_.frame, to)) tlDrag_.frame = std::max(a, std::min(b, to));
                    }
                } else if (tlDrag_.state == 2) {
                    int f = frameAt(ui.px());
                    if (f != cf) { playing_ = false; gotoFrame(f); }
                }
            }
        }
        if (ui.tapOn(rowsZ, rowsView) && !onBar) {
            int ri = rowAt(ui.py()), kf = 0;
            bool hit = keyNear(ri, ui.px(), kf);
            if (ri >= 0 && ri < (int)rows.size()) {
                if (rows[ri].kind == 2) { selBone_ = rows[ri].node; tlTarget_ = 2; }
                else if (rows[ri].kind == 0) tlTarget_ = 0;
            }
            playing_ = false;
            gotoFrame(hit ? kf : frameAt(ui.px()));
        }
        if (!ui.pointerDown()) editStarted_ = false;
        // полоса эффекта: тянуть за края - длительность, за середину - сдвиг по времени
        if (bar) {
            static float acc = 0;
            if (ui.pressedNow()) acc = 0;
            bool changed = false;
            if (!ui.blocked && ui.pointerDown()) {
                if (ui.pressedId(idL) && ui.moved()) {
                    ui.takeOwner(idL); beginEdit();
                    sel->fxStart = std::min(frameAt(ui.px()), sel->fxEnd - 1); changed = true;
                } else if (ui.pressedId(idR) && ui.moved()) {
                    ui.takeOwner(idR); beginEdit();
                    sel->fxEnd = std::max(frameAt(ui.px()), sel->fxStart + 1); changed = true;
                } else if (ui.pressedId(idM) && ui.moved()) {
                    ui.takeOwner(idM); beginEdit();
                    acc += ui.dragDx() / std::max(0.1f, pxPer);
                    int d = (int)std::lround(acc);
                    if (d != 0) { acc -= d; sel->fxStart += d; sel->fxEnd += d; if (sel->fxStart < 0) { sel->fxEnd -= sel->fxStart; sel->fxStart = 0; } changed = true; }
                }
            }
            if (changed) { scene.resetEffects(); scene.simulateTo(curFrame()); }
        }
    }
    ui.endScroll();

    // линия текущего кадра поверх всего
    float px = fxp((float)cf);
    g.rect(Rect(px - 1 * S, track.y + 1 * S, 2 * S, rowsView.b() - track.y - 2 * S), theme::accent2.alpha(0.9f), 0);
    g.circle(px, track.y + 5 * S, 5 * S, theme::accent2);
}

// ============================================================ нижняя панель инструментов
void App::drawTabs() {
    Gfx& g = ui.g;
    float S = ui.S;
    g.rect(rTab_, theme::panel, 0);
    if (land_) g.rect(Rect(rTab_.x, rTab_.y, 1, rTab_.h), theme::line, 0);
    else g.rect(Rect(0, rTab_.y, rTab_.w, 1), theme::line, 0);
    struct T { Tab t; Icon i; const char* n; };
    static const T tabs[] = {
        {Tab::None, Icon::Plus, "Добавить"}, {Tab::Scene, Icon::Layers, "Сцена"}, {Tab::Object, Icon::Move, "Объект"},
        {Tab::Bones, Icon::Rig, "Кости"},    {Tab::Physics, Icon::Ball, "Физика"}, {Tab::Fx, Icon::Flame, "Эффекты"},
        {Tab::Anim, Icon::Film, "Анимация"}, {Tab::Render, Icon::Palette, "Рендер"}};
    const int N = 8;
    float iw = land_ ? rTab_.w : std::max(80 * S, rTab_.w / N), ih = land_ ? 74 * S : rTab_.h;
    float total = land_ ? ih * N : iw * N;
    ScrollState& sc = scrollBar_;
    ui.zone(rTab_);
    ui.beginScroll(rTab_, sc, total, !land_);
    for (int i = 0; i < N; i++) {
        Rect r = land_ ? Rect(rTab_.x, rTab_.y + i * ih - sc.off, iw, ih) : Rect(rTab_.x + i * iw - sc.off, rTab_.y, iw, ih);
        bool act = tabs[i].t != Tab::None && tab_ == tabs[i].t;
        if (i == 0) {
            Rect b = r.inset(8 * S, 10 * S);
            if (ui.button(b, "", 1, 12)) openAddMenu();
            g.icon(Icon::Plus, Rect(b.cx() - 13 * S, b.y + 6 * S, 26 * S, 26 * S), theme::onAccent);
            g.textIn(Rect(b.x, b.y + b.h * 0.62f, b.w, b.h * 0.3f), "Добавить", 11.5f * S, theme::onAccent, 1);
            continue;
        }
        if (ui.iconLabelBtn(r, tabs[i].i, tabs[i].n, act, 11.5f)) {
            tab_ = act ? Tab::None : tabs[i].t;
            if (tab_ == Tab::Bones) showBones_ = true;
        }
    }
    ui.endScroll();
    contentBar_ = total;
}

static const char* tabTitle(Tab t) {
    switch (t) {
        case Tab::Scene: return "Сцена";
        case Tab::Object: return "Объект";
        case Tab::Bones: return "Кости";
        case Tab::Physics: return "Физика";
        case Tab::Fx: return "Эффекты";
        case Tab::Anim: return "Анимация";
        case Tab::Render: return "Рендер и экспорт";
        default: return "";
    }
}

void App::drawPanel() {
    if (tab_ == Tab::None) return;
    Gfx& g = ui.g;
    float S = ui.S;
    g.rect(rPanel_, theme::panel, 0);
    if (land_) g.rect(Rect(rPanel_.x, rPanel_.y, 1, rPanel_.h), theme::line, 0);
    else g.rect(Rect(0, rPanel_.y, rPanel_.w, 1), theme::line, 0);
    ui.zone(rPanel_);  // панель перекрывает вьюпорт
    float hh = 44 * S;
    g.text(tabTitle(tab_), rPanel_.x + 16 * S, rPanel_.y + hh * 0.5f + 7 * S, 18 * S, theme::text);
    if (ui.iconBtn(Rect(rPanel_.r() - 44 * S, rPanel_.y + 2 * S, 40 * S, 40 * S), Icon::ChevDown, false, nullptr, false)) { tab_ = Tab::None; return; }
    Rect content(rPanel_.x, rPanel_.y + hh, rPanel_.w, rPanel_.h - hh);
    switch (tab_) {
        case Tab::Scene: panelScene(content); break;
        case Tab::Object: panelObject(content); break;
        case Tab::Bones: panelBones(content); break;
        case Tab::Physics: panelPhysics(content); break;
        case Tab::Fx: panelFx(content); break;
        case Tab::Anim: panelAnim(content); break;
        case Tab::Render: panelRender(content); break;
        default: break;
    }
}

// ============================================================ панель «Сцена»
void App::panelScene(const Rect& r) {
    float S = ui.S;
    ScrollState& sc = scrollScene_;
    form_.x = r.x + 12 * S; form_.w = r.w - 24 * S; form_.y = r.y + 8 * S - sc.off;
    float top = r.y - sc.off;
    ui.beginScroll(r, sc, contentScene_);
    Rect hdr = row(40);
    Object* sel = scene.find(scene.selected);
    if (ui.button(cell(hdr, 0, 4), "Добавить", 1, 13)) openAddMenu();
    if (ui.button(cell(hdr, 1, 4), "Копия", 0, 13) && sel) { scene.pushUndo(); scene.duplicate(sel->id); scene.resetEffects(); scene.simulateTo(curFrame()); }
    if (ui.button(cell(hdr, 2, 4), "Имя", 0, 13) && sel) {
        int id = sel->id;
        editText("Имя объекта", sel->name, [this, id](const std::string& s) { scene.pushUndo(); scene.rename(id, s); });
    }
    if (ui.button(cell(hdr, 3, 4), "Удалить", 2, 13) && sel) {
        int id = sel->id;
        confirm("Удалить объект", "«" + sel->name + "» будет удалён.", [this, id]() { scene.pushUndo(); scene.remove(id); scene.resetEffects(); scene.simulateTo(curFrame()); }, "Удалить");
    }
    int cf = curFrame();
    for (auto& op : scene.objects) {
        Object& o = *op;
        Rect rr = row(46);
        bool tap = false;
        ui.listRow(rr, o.id == scene.selected, &tap);
        if (tap) scene.selected = o.id;
        Col ic = o.visible ? theme::text : theme::textDim;
        ui.g.icon(typeIcon(o), Rect(rr.x + 8 * S, rr.y + 10 * S, 26 * S, 26 * S), o.id == scene.selected ? theme::accent2 : ic);
        float tx = rr.x + 44 * S, fs = 14 * S;
        std::string sub;
        if (const ParentKey* pk = scene.activeParent(o, (float)cf)) {
            const Object* p = scene.find(pk->parent);
            sub = "→ " + (p ? p->name : std::string("?"));
        }
        if (o.id == scene.activeCamera) sub += (sub.empty() ? "" : "  ") + std::string("активная камера");
        if (!sub.empty()) {
            ui.g.text(ui.g.fit(o.name, fs, rr.w - 110 * S), tx, rr.y + 19 * S, fs, ic);
            ui.g.text(ui.g.fit(sub, 11 * S, rr.w - 110 * S), tx, rr.y + 36 * S, 11 * S, theme::textDim);
        } else ui.g.text(ui.g.fit(o.name, fs, rr.w - 110 * S), tx, rr.cy() + fs * 0.36f, fs, ic);
        Rect eye(rr.r() - 44 * S, rr.y + 5 * S, 36 * S, 36 * S);
        Col dimc = theme::textDim;
        if (ui.iconBtn(eye, o.visible ? Icon::Eye : Icon::EyeOff, false, o.visible ? nullptr : &dimc, false)) { scene.pushUndo(); o.visible = !o.visible; scene.evaluate(frameF_); }
    }
    contentScene_ = form_.y - top + 16 * S;
    ui.endScroll();
}

// ============================================================ панель «Объект»
void App::transformRows(Object& o) {
    Vec3 t, rr, s;
    scene.localTRS(o, t, rr, s);
    bool attached = scene.isAttached(o, (float)curFrame());
    sectionLabel(attached ? "Смещение относительно родителя" : "Положение");
    int id = o.id;
    const char* ax[3] = {"X", "Y", "Z"};
    auto setComp = [](Vec3& v, int i, float x) { if (i == 0) v.x = x; else if (i == 1) v.y = x; else v.z = x; };
    auto getComp = [](const Vec3& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); };
    for (int group = 0; group < 3; group++) {
        if (group == 2 && (o.type == ObjType::Light || o.type == ObjType::Camera)) break;
        if (group == 1) { Rect l = row(20); ui.label(l, "Поворот (градусы)", 11.5f, theme::textDim, 0); }
        if (group == 2) { Rect l = row(20); ui.label(l, "Масштаб", 11.5f, theme::textDim, 0); }
        Rect line = row(38);
        for (int i = 0; i < 3; i++) {
            Rect c = cell(line, i, 3, 4);
            Vec3 cur = group == 0 ? t : (group == 1 ? rr : s);
            float v = getComp(cur, i);
            if (group == 1) v *= 180.f / kPi;
            auto apply = [this, id, i, group, setComp](float nv) {
                Object* ob = scene.find(id);
                if (!ob) return;
                beginEdit();
                Vec3 tt, r2, ss;
                scene.localTRS(*ob, tt, r2, ss);
                if (group == 0) setComp(tt, i, nv);
                else if (group == 1) setComp(r2, i, nv * kPi / 180.f);
                else setComp(ss, i, std::max(0.01f, nv));
                scene.setLocal(id, tt, r2, ss);
                scene.resetEffects();
                scene.simulateTo(curFrame());
            };
            const char* fmt = group == 1 ? "%.1f" : "%.2f";
            if (slide(c, ax[i], v, 0, 0, fmt, group == 0 ? 0.02f : (group == 1 ? 0.6f : 0.01f), apply)) apply(v);
        }
    }
}

void App::panelObject(const Rect& r) {
    float S = ui.S;
    ScrollState& sc = scrollObj_;
    form_.x = r.x + 12 * S; form_.w = r.w - 24 * S; form_.y = r.y + 8 * S - sc.off;
    float top = r.y - sc.off;
    ui.beginScroll(r, sc, contentObj_);
    Object* o = scene.find(scene.selected);
    if (!o) {
        Rect l = row(60);
        ui.label(l, "Выберите объект в списке «Сцена» или тапом по экрану", 13, theme::textDim, 1);
    } else {
        int id = o->id;
        Rect h = row(30);
        ui.g.icon(typeIcon(*o), Rect(h.x, h.y + 2 * S, 26 * S, 26 * S), theme::accent2);
        ui.g.text(ui.g.fit(o->name, 17 * S, h.w - 40 * S), h.x + 36 * S, h.cy() + 6 * S, 17 * S, theme::text);

        transformRows(*o);

        if (o->type == ObjType::Model) {
            sectionLabel("Материал");
            Rect tg = row(38);
            bool gl = o->glass;
            if (ui.toggle(tg, "Стекло (прозрачный материал)", gl)) {
                beginEdit(); o->glass = gl;
                if (gl) { o->matOverride = true; o->castShadow = false; o->roughness = std::min(o->roughness, 0.05f); }
                else o->castShadow = true;
            }
            if (o->glass) {
                propF(*o, "Оттенок: красный", [](Object& x) -> float& { return x.color[0]; }, 0, 1, "%.2f", 0);
                propF(*o, "Оттенок: зелёный", [](Object& x) -> float& { return x.color[1]; }, 0, 1, "%.2f", 0);
                propF(*o, "Оттенок: синий", [](Object& x) -> float& { return x.color[2]; }, 0, 1, "%.2f", 0);
                propF(*o, "Прозрачность (больше — мутнее)", [](Object& x) -> float& { return x.glassAlpha; }, 0.f, 0.8f, "%.2f", 0);
                Rect hint = row(44);
                ui.label(hint, "Чтобы разбить: добавьте «Разбить стекло (эффект)» и поставьте на нужный кадр. Стекло исчезнет, осколки разлетятся и упадут на пол.", 11, theme::textDim, 0);
            }
            Rect t1 = row(38);
            bool mo = o->matOverride;
            if (!o->glass && ui.toggle(t1, "Свой материал (вместо модели)", mo)) { beginEdit(); o->matOverride = mo; }
            if (o->matOverride && !o->glass) {
                propF(*o, "Красный", [](Object& x) -> float& { return x.color[0]; }, 0, 1, "%.2f", 0);
                propF(*o, "Зелёный", [](Object& x) -> float& { return x.color[1]; }, 0, 1, "%.2f", 0);
                propF(*o, "Синий", [](Object& x) -> float& { return x.color[2]; }, 0, 1, "%.2f", 0);
                propF(*o, "Металличность", [](Object& x) -> float& { return x.metallic; }, 0, 1, "%.2f", 0);
                propF(*o, "Шероховатость", [](Object& x) -> float& { return x.roughness; }, 0, 1, "%.2f", 0);
                propF(*o, "Свечение", [](Object& x) -> float& { return x.emission; }, 0, 10, "%.1f", 0);
            }
            Rect t2 = row(38);
            bool cs = o->castShadow;
            if (ui.toggle(t2, "Отбрасывает тень", cs)) { beginEdit(); o->castShadow = cs; }
        } else if (o->type == ObjType::Light) {
            sectionLabel("Свет");
            Rect seg = row(38);
            int k = ui.segmented(seg, {"Солнце", "Точечный", "Фонарик"}, o->light == LightKind::Sun ? 0 : (o->light == LightKind::Spot ? 2 : 1));
            if (k >= 0) { beginEdit(); o->light = k == 0 ? LightKind::Sun : (k == 2 ? LightKind::Spot : LightKind::Point); }
            propF(*o, "Красный", [](Object& x) -> float& { return x.lcolor[0]; }, 0, 1, "%.2f", 0);
            propF(*o, "Зелёный", [](Object& x) -> float& { return x.lcolor[1]; }, 0, 1, "%.2f", 0);
            propF(*o, "Синий", [](Object& x) -> float& { return x.lcolor[2]; }, 0, 1, "%.2f", 0);
            propF(*o, "Сила", [](Object& x) -> float& { return x.energy; }, 0, o->light == LightKind::Sun ? 15.f : 200.f, "%.1f", 0);
            if (o->light != LightKind::Sun) propF(*o, "Радиус действия", [](Object& x) -> float& { return x.lradius; }, 0.5f, 40.f, "%.1f", 0);
            if (o->light == LightKind::Spot) {
                propF(*o, "Угол луча, °", [](Object& x) -> float& { return x.spotAngle; }, 5.f, 80.f, "%.0f", 0);
                propF(*o, "Мягкость края", [](Object& x) -> float& { return x.spotSoft; }, 0.f, 0.95f, "%.2f", 0);
                propF(*o, "Видимый луч", [](Object& x) -> float& { return x.beam; }, 0.f, 1.f, "%.2f", 0);
                Rect hint = row(44);
                ui.label(hint, "Фонарик светит по направлению конуса на значке. Вращайте его гизмо, чтобы навести луч. Можно прицепить к руке или голове.", 11, theme::textDim, 0);
            }
        } else if (o->type == ObjType::Camera) {
            sectionLabel("Камера");
            propF(*o, "Угол обзора", [](Object& x) -> float& { return x.fovDeg; }, 10.f, 120.f, "%.0f°", 0);
            Rect b1 = row(40);
            if (ui.button(cell(b1, 0, 2), "Камера = вид", 0, 13)) alignCameraToView();
            if (ui.button(cell(b1, 1, 2), o->id == scene.activeCamera ? "Активная ✓" : "Сделать активной", o->id == scene.activeCamera ? 0 : 1, 13)) { scene.pushUndo(); scene.activeCamera = id; }
        } else if (o->type == ObjType::Effect) {
            sectionLabel(std::string("Эффект: ") + kFxNames[(int)o->fx]);
            Rect bb = row(44);
            if (ui.button(bb, "Настройки эффекта…", 1, 14)) tab_ = Tab::Fx;
            Rect hint = row(34);
            ui.label(hint, "Эффект можно двигать как обычный объект и прицеплять к оружию, руке или кости.", 11, theme::textDim, 0);
        }

        // ----- прицепление -----
        sectionLabel("Прицепление");
        int cf = curFrame();
        const ParentKey* pk = scene.activeParent(*o, (float)cf);
        if (pk) {
            const Object* p = scene.find(pk->parent);
            std::string desc = "Прицеплён к: " + (p ? p->name : std::string("?"));
            if (p && pk->node >= 0 && p->type == ObjType::Model && p->model >= 0 && p->model < (int)scene.models.size() && scene.models[p->model].model) {
                const Model& m = *scene.models[p->model].model;
                if (pk->node < (int)m.nodes.size()) desc += " · " + (m.nodes[pk->node].name.empty() ? "кость " + std::to_string(pk->node) : m.nodes[pk->node].name);
            }
            Rect l = row(24);
            ui.g.icon(Icon::Link, Rect(l.x, l.y, 22 * S, 22 * S), theme::accent2);
            ui.g.text(ui.g.fit(desc, 13 * S, l.w - 30 * S), l.x + 28 * S, l.cy() + 5 * S, 13 * S, theme::text);
            Rect b = row(42);
            if (ui.button(cell(b, 0, 2), "Отцепить (кадр " + std::to_string(cf) + ")", 2, 13)) {
                scene.pushUndo();
                if (scene.detach(id, cf)) toast("Отцеплено: предмет останется на месте и продолжит двигаться сам");
                scene.resetEffects(); scene.simulateTo(cf);
            }
            if (ui.button(cell(b, 1, 2), "Сменить прицеп…", 0, 13)) attachFlow(id);
            Rect b2 = row(38);
            if (ui.button(b2, "Поставить в точку кости (сброс смещения)", 0, 12.5f) && p) {
                scene.pushUndo();
                int nd = pk->node, pid = pk->parent;
                scene.attach(id, pid, nd, cf, true);
                scene.resetEffects(); scene.simulateTo(cf);
                toast("Предмет перенесён в точку кости");
            }
        } else {
            Rect b = row(42);
            if (ui.button(b, "Прицепить к…", 1, 14)) attachFlow(id);
        }
        if (!o->parents.empty()) {
            Rect l = row(20);
            ui.label(l, "События прицепления", 11.5f, theme::textDim, 0);
            for (size_t i = 0; i < o->parents.size(); i++) {
                const ParentKey& k = o->parents[i];
                Rect rr = row(34);
                ui.g.rect(rr, theme::panel2, 8 * S);
                const Object* p = scene.find(k.parent);
                std::string t = "кадр " + std::to_string(k.frame) + (k.parent >= 0 ? " → " + (p ? p->name : std::string("?")) : std::string(": отцеплен"));
                ui.g.text(ui.g.fit(t, 13 * S, rr.w - 60 * S), rr.x + 10 * S, rr.cy() + 5 * S, 13 * S, k.parent >= 0 ? theme::accent2 : theme::orange);
                if (ui.iconBtn(Rect(rr.r() - 36 * S, rr.y + 2 * S, 30 * S, 30 * S), Icon::Close, false, nullptr, false)) {
                    scene.pushUndo();
                    o->parents.erase(o->parents.begin() + i);
                    scene.evaluate(frameF_);
                    scene.resetEffects(); scene.simulateTo(curFrame());
                    break;
                }
            }
        }
    }
    contentObj_ = form_.y - top + 24 * S;
    ui.endScroll();
}

// Прицепление: выбрать объект, затем кость, затем «оставить на месте» или «поставить в точку кости».
void App::attachFlow(int id) {
    std::vector<std::string> names;
    std::vector<int> ids;
    std::vector<Icon> icons;
    for (auto& op : scene.objects) {
        if (op->id == id) continue;
        names.push_back(op->name);
        ids.push_back(op->id);
        icons.push_back(typeIcon(*op));
    }
    if (ids.empty()) { toast("Нет объектов, к которым можно прицепить"); return; }
    pickList("Прицепить к объекту", names, [this, id, ids](int k) {
        int pid = ids[k];
        Object* p = scene.find(pid);
        std::vector<int> nodes;
        std::vector<std::string> nn{"Объект целиком"};
        if (p && p->type == ObjType::Model && p->model >= 0 && p->model < (int)scene.models.size() && scene.models[p->model].model) {
            const Model& m = *scene.models[p->model].model;
            nodes = scene.boneNodes(*p);  // только настоящие суставы
            for (int j : nodes) nn.push_back("кость: " + (m.nodes[j].name.empty() ? std::to_string(j) : m.nodes[j].name));
        }
        auto doAttach = [this, id, pid](int node, bool snap) {
            scene.pushUndo();
            int f = curFrame();
            if (scene.attach(id, pid, node, f, snap)) {
                toast("Прицеплено на кадре " + std::to_string(f) + ". Чтобы отцепить, выберите кадр и нажмите «Отцепить»");
            } else toast("Нельзя прицепить: получится замкнутая цепочка");
            scene.resetEffects(); scene.simulateTo(f);
        };
        auto askSnap = [this, doAttach](int node) {
            pickList("Как прицепить", {"Оставить на месте (как сейчас)", "Поставить в точку кости / объекта"},
                     [doAttach, node](int q) { doAttach(node, q == 1); }, {Icon::Link, Icon::Bone});
        };
        if (nodes.empty()) askSnap(-1);
        else pickList("Куда прицепить", nn, [askSnap, nodes](int q) { askSnap(q == 0 ? -1 : nodes[q - 1]); });
    }, icons);
}

// ============================================================ панель «Анимация»
void App::panelAnim(const Rect& r) {
    float S = ui.S;
    ScrollState& sc = scrollAnim_;
    form_.x = r.x + 12 * S; form_.w = r.w - 24 * S; form_.y = r.y + 8 * S - sc.off;
    float top = r.y - sc.off;
    ui.beginScroll(r, sc, contentAnim_);
    sectionLabel("Проект");
    {
        Rect seg = row(38);
        int fpsv[4] = {12, 24, 30, 60};
        int cur = -1;
        for (int i = 0; i < 4; i++) if (scene.proj.fps == fpsv[i]) cur = i;
        int k = ui.segmented(seg, {"12 к/с", "24 к/с", "30 к/с", "60 к/с"}, cur);
        if (k >= 0) { scene.pushUndo(); scene.proj.fps = fpsv[k]; scene.resetEffects(); scene.simulateTo(curFrame()); }
        Rect line = row(38);
        float a = (float)scene.proj.frameStart, b = (float)scene.proj.frameEnd;
        auto applyA = [this](float nv) { beginEdit(); scene.proj.frameStart = std::max(0, std::min((int)std::lround(nv), scene.proj.frameEnd - 1)); };
        auto applyB = [this](float nv) { beginEdit(); scene.proj.frameEnd = std::max(scene.proj.frameStart + 1, std::min((int)std::lround(nv), 6000)); };
        if (slide(cell(line, 0, 2), "Первый кадр", a, 0, 0, "%.0f", 0.15f, applyA)) applyA(a);
        if (slide(cell(line, 1, 2), "Последний кадр", b, 0, 0, "%.0f", 0.3f, applyB)) applyB(b);
    }
    Object* o = scene.find(scene.selected);
    int cf = curFrame();
    if (o && o->type == ObjType::Model && o->model >= 0 && o->model < (int)scene.models.size() && scene.models[o->model].model) {
        const Model& m = *scene.models[o->model].model;
        sectionLabel("Анимация модели «" + o->name + "»");
        if (m.clips.empty()) {
            Rect l = row(30);
            ui.label(l, "В этой модели нет анимаций. Двигайте её ключами.", 12, theme::textDim, 0);
        } else {
            Rect l = row(20);
            ui.label(l, "Тап: включить клип с текущего кадра (с плавным переходом)", 11, theme::textDim, 0);
            int activeClip = -2;
            for (auto& k : o->clipKeys) if (k.frame <= cf) activeClip = k.clip;
            {
                Rect rr = row(40);
                if (ui.listRow(rr, activeClip == -1)) { scene.pushUndo(); scene.setClip(o->id, cf, -1, 1.f, true); }
                ui.g.text("Поза покоя", rr.x + 12 * S, rr.cy() + 5 * S, 14 * S, theme::text);
            }
            for (size_t i = 0; i < m.clips.size(); i++) {
                Rect rr = row(40);
                if (ui.listRow(rr, activeClip == (int)i)) { scene.pushUndo(); scene.setClip(o->id, cf, (int)i, 1.f, true); scene.resetEffects(); scene.simulateTo(cf); }
                std::string nm = m.clips[i].name.empty() ? "Клип " + std::to_string(i + 1) : m.clips[i].name;
                char d[32];
                std::snprintf(d, sizeof d, "%.1f с", m.clips[i].duration);
                ui.g.text(ui.g.fit(nm, 14 * S, rr.w - 90 * S), rr.x + 12 * S, rr.cy() + 5 * S, 14 * S, theme::text);
                ui.g.textIn(Rect(rr.r() - 80 * S, rr.y, 70 * S, rr.h), d, 12 * S, theme::textDim, 2);
            }
            // ключи клипов
            if (!o->clipKeys.empty()) {
                Rect l2 = row(20);
                ui.label(l2, "Ключи клипов", 11.5f, theme::textDim, 0);
                for (size_t i = 0; i < o->clipKeys.size(); i++) {
                    ClipKey& k = o->clipKeys[i];
                    Rect rr = row(40);
                    ui.g.rect(rr, theme::panel2, 8 * S);
                    std::string cn = k.clip < 0 ? "поза покоя" : (k.clip < (int)m.clips.size() ? (m.clips[k.clip].name.empty() ? "клип " + std::to_string(k.clip + 1) : m.clips[k.clip].name) : "?");
                    ui.g.text(ui.g.fit("кадр " + std::to_string(k.frame) + ": " + cn, 13 * S, rr.w * 0.45f), rr.x + 10 * S, rr.cy() + 5 * S, 13 * S, theme::text);
                    Rect sp(rr.x + rr.w * 0.5f, rr.y + 4 * S, rr.w * 0.34f, rr.h - 8 * S);
                    float spd = k.speed;
                    int idx = (int)i, oid = o->id;
                    auto ap = [this, oid, idx](float nv) { Object* ob = scene.find(oid); if (!ob || idx >= (int)ob->clipKeys.size()) return; beginEdit(); ob->clipKeys[idx].speed = clampf(nv, 0.1f, 4.f); };
                    if (slide(sp, "×", spd, 0.1f, 4.f, "%.2f", 0.01f, ap)) ap(spd);
                    if (ui.iconBtn(Rect(rr.r() - 36 * S, rr.y + 5 * S, 30 * S, 30 * S), Icon::Close, false, nullptr, false)) {
                        scene.pushUndo();
                        o->clipKeys.erase(o->clipKeys.begin() + i);
                        scene.evaluate(frameF_);
                        break;
                    }
                }
            }
        }
    }
    if (o) {
        sectionLabel("Видимость");
        Rect b = row(40);
        if (ui.button(cell(b, 0, 2), "Показать на кадре " + std::to_string(cf), 0, 12.5f)) {
            scene.pushUndo();
            bool done = false;
            for (auto& k : o->visKeys) if (k.frame == cf) { k.visible = true; done = true; }
            if (!done) { VisKey k; k.frame = cf; k.visible = true; o->visKeys.push_back(k); }
            std::sort(o->visKeys.begin(), o->visKeys.end(), [](const VisKey& a, const VisKey& c) { return a.frame < c.frame; });
            scene.evaluate(frameF_);
        }
        if (ui.button(cell(b, 1, 2), "Скрыть на кадре " + std::to_string(cf), 0, 12.5f)) {
            scene.pushUndo();
            bool done = false;
            for (auto& k : o->visKeys) if (k.frame == cf) { k.visible = false; done = true; }
            if (!done) { VisKey k; k.frame = cf; k.visible = false; o->visKeys.push_back(k); }
            std::sort(o->visKeys.begin(), o->visKeys.end(), [](const VisKey& a, const VisKey& c) { return a.frame < c.frame; });
            scene.evaluate(frameF_);
        }
        if (!o->visKeys.empty()) {
            for (size_t i = 0; i < o->visKeys.size(); i++) {
                Rect rr = row(34);
                ui.g.rect(rr, theme::panel2, 8 * S);
                ui.g.text("кадр " + std::to_string(o->visKeys[i].frame) + (o->visKeys[i].visible ? ": показан" : ": скрыт"), rr.x + 10 * S, rr.cy() + 5 * S, 13 * S, theme::text);
                if (ui.iconBtn(Rect(rr.r() - 36 * S, rr.y + 2 * S, 30 * S, 30 * S), Icon::Close, false, nullptr, false)) {
                    scene.pushUndo();
                    o->visKeys.erase(o->visKeys.begin() + i);
                    scene.evaluate(frameF_);
                    break;
                }
            }
        }
        if (!o->keys.empty()) {
            sectionLabel("Ключи положения");
            for (size_t i = 0; i < o->keys.size(); i++) {
                TKey& k = o->keys[i];
                Rect rr = row(40);
                ui.g.rect(rr, theme::panel2, 8 * S);
                ui.g.text("кадр " + std::to_string(k.frame), rr.x + 10 * S, rr.cy() + 5 * S, 14 * S, theme::text);
                static const char* en[3] = {"линейно", "плавно", "ступенька"};
                Rect eb(rr.x + rr.w * 0.38f, rr.y + 4 * S, rr.w * 0.46f, rr.h - 8 * S);
                if (ui.button(eb, en[std::max(0, std::min(2, k.ease))], 0, 12)) { scene.pushUndo(); k.ease = (k.ease + 1) % 3; scene.evaluate(frameF_); }
                if (ui.iconBtn(Rect(rr.r() - 36 * S, rr.y + 5 * S, 30 * S, 30 * S), Icon::Close, false, nullptr, false)) {
                    scene.pushUndo();
                    o->keys.erase(o->keys.begin() + i);
                    scene.evaluate(frameF_);
                    break;
                }
            }
        }
    }
    contentAnim_ = form_.y - top + 24 * S;
    ui.endScroll();
}

// ============================================================ панель «Рендер»
void App::panelRender(const Rect& r) {
    float S = ui.S;
    ScrollState& sc = scrollRender_;
    form_.x = r.x + 12 * S; form_.w = r.w - 24 * S; form_.y = r.y + 8 * S - sc.off;
    float top = r.y - sc.off;
    ui.beginScroll(r, sc, contentRender_);
    RenderSettings& rs = scene.render;
    auto sl = [&](const char* label, float& field, float lo, float hi, const char* fmt) {
        float v = field;
        Rect rr = row(38);
        float* fp = &field;
        auto apply = [this, fp, lo, hi](float nv) { beginEdit(); *fp = clampf(nv, lo, hi); };
        if (slide(rr, label, v, lo, hi, fmt, (hi - lo) / std::max(1.f, rr.w / S), apply)) apply(v);
    };
    sectionLabel("Экспорт");
    Rect eb = row(46);
    if (ui.button(cell(eb, 0, 2), "Кадр PNG", 1, 15)) startStillExport();
    if (ui.button(cell(eb, 1, 2), "Видео MP4", 1, 15)) startVideoExport();
    {
        Rect gb = row(44);
        if (ui.button(gb, "Анимация скелета GLB (Godot 4)", 0, 14)) skeletalExportMenu();
        Rect gh = row(34);
        ui.label(gh, "Запекает движение костей по кадрам: клип, ключи, рэгдолл. Файл появится в «Загрузки/PrismLab». Ткань в GLB не попадает.", 11, theme::textDim, 0);
    }
    {
        sectionLabel("Формат кадра и видео");
        Rect fr = row(40);
        int cur = rs.exportW == rs.exportH ? 2 : (rs.exportH > rs.exportW ? 0 : 1);
        int k = ui.segmented(fr, {"Вертикальный 9:16", "Горизонтальный 16:9", "Квадрат"}, cur);
        if (k >= 0 && k != cur) {
            scene.pushUndo();
            int longSide = std::max(rs.exportW, rs.exportH), shortSide = std::min(rs.exportW, rs.exportH);
            auto even = [](float v) { int n = (int)std::lround(v); return std::max(2, n - (n & 1)); };
            if (k == 0) { rs.exportH = longSide; rs.exportW = even(longSide * 9.f / 16.f); }
            else if (k == 1) { rs.exportW = longSide; rs.exportH = even(longSide * 9.f / 16.f); }
            else rs.exportW = rs.exportH = even((float)(cur == 0 ? shortSide : longSide * 9.f / 16.f));
            autoSaveDirty_ = true;
        }
        Rect sw = row(40);
        if (ui.button(sw, "Повернуть кадр (поменять ширину и высоту)", 0, 13)) { scene.pushUndo(); std::swap(rs.exportW, rs.exportH); autoSaveDirty_ = true; }
    }
    {
        Rect pr = row(40);
        char b[64];
        std::snprintf(b, sizeof b, "Размер: %d × %d", rs.exportW, rs.exportH);
        if (ui.button(pr, b, 0, 14)) {
            static const int pw[] = {720, 1080, 1080, 1280, 1920, 2160};
            static const int ph[] = {1280, 1920, 1080, 720, 1080, 3840};
            pickList("Размер кадра", {"720 × 1280 (9:16)", "1080 × 1920 (9:16)", "1080 × 1080 (1:1)", "1280 × 720 (16:9)", "1920 × 1080 (16:9)", "2160 × 3840 (4K, 9:16)"},
                     [this](int i) { scene.pushUndo(); scene.render.exportW = pw[i]; scene.render.exportH = ph[i]; });
        }
        Rect q1 = row(38);
        int sv[3] = {8, 24, 64}, cur = rs.stillSamples <= 8 ? 0 : (rs.stillSamples <= 24 ? 1 : 2);
        Rect l = q1;
        (void)l;
        int k = ui.segmented(q1, {"Кадр: быстро", "Кадр: хорошо", "Кадр: макс."}, cur);
        if (k >= 0) { beginEdit(); rs.stillSamples = sv[k]; editStarted_ = false; }
        Rect q2 = row(38);
        int vs[3] = {1, 3, 6}, curv = rs.videoSamples <= 1 ? 0 : (rs.videoSamples <= 3 ? 1 : 2);
        int k2 = ui.segmented(q2, {"Видео: быстро", "Видео: хорошо", "Видео: макс."}, curv);
        if (k2 >= 0) { beginEdit(); rs.videoSamples = vs[k2]; editStarted_ = false; }
        Rect q3 = row(38);
        int bv[3] = {8, 16, 30}, curb = rs.videoBitrateMbps <= 8 ? 0 : (rs.videoBitrateMbps <= 16 ? 1 : 2);
        int k3 = ui.segmented(q3, {"8 Мбит/с", "16 Мбит/с", "30 Мбит/с"}, curb);
        if (k3 >= 0) { beginEdit(); rs.videoBitrateMbps = bv[k3]; editStarted_ = false; }
    }
    sectionLabel("Небо и время суток");
    {
        Rect t = row(38);
        bool v = rs.todOn;
        if (ui.toggle(t, "Небо и свет по времени суток", v)) { beginEdit(); rs.todOn = v; editStarted_ = false; }
        if (rs.todOn) {
            Rect pr = row(42);
            static const char* nm[4] = {"Утро", "День", "Закат", "Ночь"};
            static const float hrs[4] = {7.f, 12.f, 17.7f, 23.5f};
            for (int i = 0; i < 4; i++) {
                bool on = std::fabs(rs.tod - hrs[i]) < 0.75f;
                if (ui.button(cell(pr, i, 4, 4), nm[i], on ? 1 : 0, 12.5f)) { scene.pushUndo(); rs.tod = hrs[i]; autoSaveDirty_ = true; }
            }
            float hh = rs.tod;
            Rect sr = row(38);
            char lab[48];
            int hi = (int)hh % 24, mi = (int)((hh - std::floor(hh)) * 60.f);
            std::snprintf(lab, sizeof lab, "Время: %02d:%02d", hi, mi);
            auto ap = [this](float nv) { beginEdit(); scene.render.tod = clampf(nv, 0.f, 24.f); };
            if (slide(sr, lab, hh, 0.f, 24.f, "", 24.f / std::max(1.f, sr.w / S), ap)) ap(hh);
            Rect hint = row(56);
            ui.label(hint, "Ночью появляются луна и звёзды, на закате небо оранжевое. Направление солнца берётся из объекта «Солнце» (по горизонтали), высота — по времени. Для ночных сцен добавьте «Фонарик».", 11, theme::textDim, 0);
        } else {
            Rect hint = row(40);
            ui.label(hint, "Выключено: цвета неба задаются вручную. Включите, чтобы выбрать утро, день, закат или ночь.", 11, theme::textDim, 0);
        }
    }
    sectionLabel("Свет и тени");
    sl("Яркость неба", rs.skyBrightness, 0.f, 3.f, "%.2f");
    {
        Rect t = row(38);
        bool v = rs.shadows;
        if (ui.toggle(t, "Тени от солнца", v)) { beginEdit(); rs.shadows = v; editStarted_ = false; }
    }
    sl("Мягкость теней", rs.shadowSoft, 0.f, 3.f, "%.2f");
    {
        Rect t = row(38);
        bool v = rs.contactShadows;
        if (ui.toggle(t, "Контактные тени (у основания)", v)) { beginEdit(); rs.contactShadows = v; editStarted_ = false; }
    }
    {
        Rect t = row(38);
        bool v = rs.ao;
        if (ui.toggle(t, "Объёмное затенение (SSAO)", v)) { beginEdit(); rs.ao = v; editStarted_ = false; }
    }
    sl("Сила затенения", rs.aoStrength, 0.f, 2.f, "%.2f");
    sectionLabel("Цвет и плёнка");
    sl("Экспозиция (EV)", rs.exposure, -3.f, 3.f, "%.2f");
    sl("Контраст", rs.contrast, 0.6f, 1.6f, "%.2f");
    sl("Насыщенность", rs.saturation, 0.f, 2.f, "%.2f");
    {
        Rect t = row(38);
        bool v = rs.bloom;
        if (ui.toggle(t, "Свечение (bloom)", v)) { beginEdit(); rs.bloom = v; editStarted_ = false; }
    }
    sl("Сила свечения", rs.bloomStrength, 0.f, 1.5f, "%.2f");
    {
        Rect t = row(38);
        bool v = rs.vignette;
        if (ui.toggle(t, "Виньетка", v)) { beginEdit(); rs.vignette = v; editStarted_ = false; }
    }
    sectionLabel("Окружение");
    {
        Rect t = row(38);
        bool v = rs.floor;
        if (ui.toggle(t, "Пол", v)) { beginEdit(); rs.floor = v; editStarted_ = false; }
    }
    sl("Яркость пола", rs.floorColor[0], 0.02f, 1.f, "%.2f");
    rs.floorColor[1] = rs.floorColor[0]; rs.floorColor[2] = rs.floorColor[0] * 1.04f;
    sl("Шероховатость пола", rs.floorRoughness, 0.05f, 1.f, "%.2f");
    sectionLabel("Редактор");
    {
        Rect q = row(38);
        int cur = viewScale_ >= 0.99f ? 2 : (viewScale_ >= 0.7f ? 1 : 0);
        int k = ui.segmented(q, {"Вьюпорт 50%", "75%", "100%"}, cur);
        if (k >= 0) viewScale_ = k == 0 ? 0.5f : (k == 1 ? 0.75f : 1.f);
        Rect hint = row(34);
        ui.label(hint, "Снижайте качество вьюпорта, если телефон греется. Экспорт всегда в полном качестве.", 11, theme::textDim, 0);
    }
    contentRender_ = form_.y - top + 24 * S;
    ui.endScroll();
}

// ============================================================ окна
static std::vector<std::string> wrapText(const Gfx& g, const std::string& s, float size, float maxW) {
    std::vector<std::string> lines;
    std::string cur, word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        std::string t = cur.empty() ? word : cur + " " + word;
        if (g.textWidth(t, size) > maxW && !cur.empty()) { lines.push_back(cur); cur = word; }
        else cur = t;
        word.clear();
    };
    for (char c : s) {
        if (c == '\n') { flushWord(); lines.push_back(cur); cur.clear(); }
        else if (c == ' ') flushWord();
        else word += c;
    }
    flushWord();
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

void App::drawKeypad(const Rect& card) {
    float S = ui.S;
    Gfx& g = ui.g;
    ui.label(Rect(card.x + 14 * S, card.y + 8 * S, card.w - 28 * S, 28 * S), modal_.title, 14, theme::textDim, 0);
    Rect disp(card.x + 14 * S, card.y + 40 * S, card.w - 28 * S, 52 * S);
    g.rect(disp, theme::bg, 10 * S);
    if (modal_.first) g.rect(disp.inset(3 * S), theme::accent.alpha(0.22f), 8 * S);
    ui.label(disp.inset(12 * S, 0), modal_.buf.empty() ? "0" : modal_.buf, 22, theme::text, 2);
    const char* keys[16] = {"7", "8", "9", "←", "4", "5", "6", "С", "1", "2", "3", "−", "0", ".", "Отмена", "OK"};
    float kw = (card.w - 28 * S - 3 * 6 * S) / 4, kh = 48 * S;
    for (int i = 0; i < 16; i++) {
        int rr = i / 4, cc = i % 4;
        Rect kr(card.x + 14 * S + cc * (kw + 6 * S), disp.b() + 10 * S + rr * (kh + 6 * S), kw, kh);
        std::string k = keys[i];
        int style = k == "OK" ? 1 : 0;
        if (ui.button(kr, k, style, k == "Отмена" ? 12.f : 18.f)) {
            if (k == "OK") {
                auto cb = modal_.onText;
                std::string v = modal_.buf;
                modal_ = Modal();
                if (cb) cb(v);
            } else if (k == "Отмена") modal_ = Modal();
            else if (k == "←") { popUtf8(modal_.buf); modal_.first = false; }
            else if (k == "С") { modal_.buf.clear(); modal_.first = false; }
            else if (k == "−") {
                if (modal_.first) modal_.buf = "";
                modal_.first = false;
                if (!modal_.buf.empty() && modal_.buf[0] == '-') modal_.buf.erase(0, 1); else modal_.buf = "-" + modal_.buf;
            } else {
                if (modal_.first) { modal_.buf.clear(); modal_.first = false; }
                if (k == "." && modal_.buf.find('.') != std::string::npos) continue;
                if (modal_.buf.size() < 12) modal_.buf += k;
            }
        }
    }
}

void App::drawKeyboard() {
    float S = ui.S;
    Gfx& g = ui.g;
    float kh = 50 * S, gap = 5 * S;
    float sheetH = 56 * S + 52 * S + 5 * (kh + gap) + 14 * S;
    Rect sheet(0, H_ - sheetH, (float)W_, sheetH);
    g.rect(sheet, theme::panel, 0);
    ui.zone(sheet);
    ui.label(Rect(14 * S, sheet.y + 6 * S, W_ - 28 * S, 24 * S), modal_.title, 12.5f, theme::textDim, 0);
    Rect field(12 * S, sheet.y + 30 * S, W_ - 24 * S, 46 * S);
    g.rect(field, theme::bg, 10 * S);
    if (modal_.first) g.rect(field.inset(3 * S), theme::accent.alpha(0.2f), 8 * S);
    ui.label(field.inset(12 * S, 0), modal_.buf, 17, theme::text, 0);
    static const char* rowsRu[3] = {"йцукенгшщзхъ", "фывапролджэ", "ячсмитьбю"};
    static const char* rowsEn[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    static const char* nums = "1234567890";
    auto splitCp = [](const std::string& s) {
        std::vector<std::string> out;
        for (size_t i = 0; i < s.size();) {
            unsigned char c = (unsigned char)s[i];
            size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : 3;
            out.push_back(s.substr(i, n));
            i += n;
        }
        return out;
    };
    float y = field.b() + 8 * S;
    auto drawRow = [&](const std::vector<std::string>& keys, float yy, float indent) {
        float kw = (W_ - 12 * S - gap * 11) / 12.f;
        float total = keys.size() * kw + (keys.size() - 1) * gap;
        float x0 = (W_ - total) * 0.5f + indent;
        for (size_t i = 0; i < keys.size(); i++) {
            Rect kr(x0 + i * (kw + gap), yy, kw, kh);
            std::string label = keys[i];
            if (modal_.shift) {
                size_t j = 0;
                unsigned char c = (unsigned char)label[0];
                uint32_t cp = c < 0x80 ? c : (c >> 5) == 6 ? (((c & 31) << 6) | (label[1] & 63)) : '?';
                (void)j;
                label = utf8(upperCp(cp));
            }
            if (ui.button(kr, label, 0, 18)) {
                if (modal_.first) { modal_.buf.clear(); modal_.first = false; }
                if (modal_.buf.size() < 60) modal_.buf += label;
                modal_.shift = false;
            }
        }
    };
    drawRow(splitCp(nums), y, 0); y += kh + gap;
    for (int r = 0; r < 3; r++) { drawRow(splitCp(modal_.ru ? rowsRu[r] : rowsEn[r]), y, 0); y += kh + gap; }
    float kw = (W_ - 12 * S - gap * 11) / 12.f;
    float x0 = 6 * S;
    if (ui.button(Rect(x0, y, kw * 1.6f, kh), "Aa", modal_.shift ? 1 : 0, 18)) modal_.shift = !modal_.shift;
    if (ui.button(Rect(x0 + kw * 1.6f + gap, y, kw * 1.6f, kh), modal_.ru ? "RU" : "EN", 0, 15)) modal_.ru = !modal_.ru;
    float sx = x0 + (kw * 1.6f + gap) * 2;
    float spaceW = W_ - 6 * S - sx - (kw * 1.6f + gap) * 2;
    if (ui.button(Rect(sx, y, spaceW, kh), "пробел", 0, 14)) { if (modal_.first) { modal_.buf.clear(); modal_.first = false; } modal_.buf += " "; }
    if (ui.button(Rect(sx + spaceW + gap, y, kw * 1.6f, kh), "⌫", 0, 18)) { popUtf8(modal_.buf); modal_.first = false; }
    if (ui.button(Rect(sx + spaceW + gap + kw * 1.6f + gap, y, kw * 1.6f, kh), "OK", 1, 15)) {
        auto cb = modal_.onText;
        std::string v = modal_.buf;
        modal_ = Modal();
        if (cb && !v.empty()) cb(v);
    }
}

void App::drawModal() {
    if (modal_.kind == Modal::None) return;
    Gfx& g = ui.g;
    float S = ui.S;
    Rect full(0, 0, (float)W_, (float)H_);
    g.rect(full, theme::shade, 0);
    uint32_t dimId = ui.zone(full);
    bool dimTap = ui.tapOn(dimId, full);
    bool closable = modal_.kind != Modal::Progress;
    float cw = std::min(W_ - 32 * S, 380 * S);
    if (modal_.kind == Modal::Text) {
        if (dimTap) { modal_ = Modal(); return; }
        drawKeyboard();
        return;
    }
    if (modal_.kind == Modal::Keypad) {
        Rect card((W_ - cw) * 0.5f, H_ * 0.5f - 190 * S, cw, 352 * S);
        g.rect(card, theme::panel, 16 * S);
        g.stroke(card, theme::line, 16 * S, 1.5f * S);
        ui.zone(card);
        drawKeypad(card);
        if (dimTap && closable) modal_ = Modal();
        return;
    }
    if (modal_.kind == Modal::List) {
        int n = (int)modal_.items.size();
        float rowH = 48 * S;
        float ch = std::min(H_ * 0.78f, 64 * S + n * (rowH + 4 * S) + 64 * S);
        Rect card((W_ - cw) * 0.5f, (H_ - ch) * 0.5f, cw, ch);
        g.rect(card, theme::panel, 16 * S);
        g.stroke(card, theme::line, 16 * S, 1.5f * S);
        ui.zone(card);
        ui.label(Rect(card.x + 16 * S, card.y + 8 * S, card.w - 32 * S, 40 * S), modal_.title, 16, theme::text, 0);
        Rect view(card.x + 8 * S, card.y + 52 * S, card.w - 16 * S, ch - 52 * S - 60 * S);
        float contentH = n * (rowH + 4 * S);
        ui.beginScroll(view, modal_.scroll, contentH);
        int pick = -1;
        for (int i = 0; i < n; i++) {
            Rect rr(view.x, view.y + i * (rowH + 4 * S) - modal_.scroll.off, view.w - 6 * S, rowH);
            bool tap = false;
            ui.listRow(rr, false, &tap);
            if (ui.record) ui.rects.emplace("item:" + modal_.items[i], rr);
            float tx = rr.x + 14 * S;
            if (i < (int)modal_.icons.size()) { g.icon(modal_.icons[i], Rect(rr.x + 10 * S, rr.y + 10 * S, 28 * S, 28 * S), theme::accent2); tx = rr.x + 50 * S; }
            ui.label(Rect(tx, rr.y, rr.r() - tx - 8 * S, rr.h), modal_.items[i], 15, theme::text, 0);
            if (tap) pick = i;
        }
        ui.endScroll();
        if (ui.button(Rect(card.x + 12 * S, card.b() - 54 * S, card.w - 24 * S, 44 * S), "Отмена", 0, 14)) { modal_ = Modal(); return; }
        if (pick >= 0) {
            auto cb = modal_.onPick;
            modal_ = Modal();
            if (cb) cb(pick);
            return;
        }
        if (dimTap) modal_ = Modal();
        return;
    }
    if (modal_.kind == Modal::Confirm || modal_.kind == Modal::Info) {
        float fs = 14 * S;
        auto lines = wrapText(g, modal_.msg, fs, cw - 32 * S);
        float ch = 56 * S + lines.size() * 22 * S + 80 * S;
        Rect card((W_ - cw) * 0.5f, (H_ - ch) * 0.5f, cw, ch);
        g.rect(card, theme::panel, 16 * S);
        g.stroke(card, theme::line, 16 * S, 1.5f * S);
        ui.zone(card);
        ui.label(Rect(card.x + 16 * S, card.y + 10 * S, card.w - 32 * S, 36 * S), modal_.title, 17, theme::text, 0);
        for (size_t i = 0; i < lines.size(); i++) g.text(lines[i], card.x + 16 * S, card.y + 62 * S + i * 22 * S, fs, theme::textDim);
        float by = card.b() - 58 * S;
        if (modal_.kind == Modal::Info) {
            if (ui.button(Rect(card.x + 12 * S, by, card.w - 24 * S, 46 * S), "OK", 1, 15)) modal_ = Modal();
        } else {
            if (ui.button(Rect(card.x + 12 * S, by, card.w / 2 - 18 * S, 46 * S), "Отмена", 0, 15)) { modal_ = Modal(); return; }
            if (ui.button(Rect(card.x + card.w / 2 + 6 * S, by, card.w / 2 - 18 * S, 46 * S), modal_.yesLabel, 2, 15)) {
                auto cb = modal_.onYes;
                modal_ = Modal();
                if (cb) cb();
                return;
            }
        }
        if (dimTap && closable) modal_ = Modal();
        return;
    }
    if (modal_.kind == Modal::Progress) {
        Rect card((W_ - cw) * 0.5f, H_ * 0.5f - 90 * S, cw, 190 * S);
        g.rect(card, theme::panel, 16 * S);
        g.stroke(card, theme::accent, 16 * S, 1.5f * S);
        ui.zone(card);
        ui.label(Rect(card.x + 16 * S, card.y + 12 * S, card.w - 32 * S, 36 * S), modal_.title, 17, theme::text, 0);
        std::string m = modal_.msg;
        float frac = 0.f;
        if (exp_.kind == Export::Video) {
            int total = exp_.last - exp_.first + 1;
            frac = total > 0 ? clampf((float)exp_.index / total, 0.f, 1.f) : 0.f;
            m = "Кадр " + std::to_string(std::min(exp_.index + 1, total)) + " из " + std::to_string(total);
        } else frac = 0.5f;
        ui.label(Rect(card.x + 16 * S, card.y + 54 * S, card.w - 32 * S, 24 * S), m, 13, theme::textDim, 0);
        Rect bar(card.x + 16 * S, card.y + 88 * S, card.w - 32 * S, 12 * S);
        g.rect(bar, theme::widget, 6 * S);
        g.rect(Rect(bar.x, bar.y, bar.w * frac, bar.h), theme::accent, 6 * S);
        if (exp_.kind == Export::Video) {
            if (ui.button(Rect(card.x + 16 * S, card.b() - 56 * S, card.w - 32 * S, 44 * S), "Отмена", 2, 14)) {
                if (plat.videoCancel) plat.videoCancel();
                finishExport(false, "Экспорт отменён");
            }
        }
    }
}

}  // namespace eng

namespace eng {
void App::drawToast() {
    if (toastT_ <= 0 || toastMsg_.empty()) return;
    Gfx& g = ui.g;
    float S = ui.S, fs = 13 * S, maxW = W_ - 40 * S;
    auto lines = wrapText(g, toastMsg_, fs, maxW - 28 * S);
    if (lines.size() > 3) lines.resize(3);
    float w = 0;
    for (auto& l : lines) w = std::max(w, g.textWidth(l, fs));
    w += 28 * S;
    float h = lines.size() * 19 * S + 18 * S;
    Rect r((W_ - w) * 0.5f, home_ ? H_ - h - 40 * S : std::max(rTop_.b() + 8 * S, rTL_.y - h - 10 * S), w, h);
    float a = std::min(1.f, toastT_ * 3.f);
    g.rect(r, theme::popup.alpha(a), 12 * S);
    g.stroke(r, theme::accent.alpha(a), 12 * S, 1.2f * S);
    for (size_t i = 0; i < lines.size(); i++) g.text(lines[i], r.x + 14 * S, r.y + 20 * S + i * 19 * S, fs, theme::text.alpha(a));
}
}  // namespace eng
