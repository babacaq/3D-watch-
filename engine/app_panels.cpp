// Панели PrismLab: кости и позы, физика, эффекты.
#include <cmath>
#include <cstdio>

#include "app.h"
#include "log.h"

namespace eng {

static Quat conj(const Quat& q) { return Quat(-q.x, -q.y, -q.z, q.w); }

static Quat worldRotOf(const Mat4& m) {
    Vec3 t, s;
    Quat q;
    decompose(m, t, q, s);
    return normalize(q);
}

static std::string boneName(const Model& m, int n) {
    if (n < 0 || n >= (int)m.nodes.size()) return "?";
    return m.nodes[n].name.empty() ? "кость " + std::to_string(n) : m.nodes[n].name;
}

// Поза кости на кадре. Если дорожки ещё не было, а кадр не первый, сначала ставим нулевую позу на первом кадре,
// чтобы поза не «протекала» назад на всю анимацию.
void writeBonePose(Scene& sc, Object& o, int node, int frame, const Vec3& t, const Quat& r) {
    bool has = false;
    for (auto& tr : o.boneTracks) if (tr.node == node && !tr.keys.empty()) has = true;
    if (!has && frame > sc.proj.frameStart) sc.setBoneKey(o.id, node, sc.proj.frameStart, Vec3(), Quat());
    sc.setBoneKey(o.id, node, frame, t, r);
}

void App::boneKeyNow() {
    Object* o = scene.find(scene.selected);
    if (!o || selBone_ < 0) return;
    Vec3 t;
    Quat r;
    if (!scene.boneDeltaAt(*o, selBone_, (float)curFrame(), t, r)) { t = Vec3(); r = Quat(); }
    writeBonePose(scene, *o, selBone_, curFrame(), t, r);
}

// Поворот выбранной кости перетаскиванием: как «трекбол» относительно камеры вида.
void App::boneDrag(float dx, float dy) {
    Object* o = scene.find(scene.selected);
    if (!o || o->type != ObjType::Model || !o->anim || selBone_ < 0) return;
    const Model& m = *scene.models[o->model].model;
    if (selBone_ >= (int)m.nodes.size()) return;
    Vec3 right(view_.view.m[0], view_.view.m[4], view_.view.m[8]), up(view_.view.m[1], view_.view.m[5], view_.view.m[9]);
    float k = 0.012f / std::max(0.5f, ui.S);
    Quat rw = quatAxisAngle(up, dx * k * ui.S) * quatAxisAngle(right, dy * k * ui.S);
    int p = m.nodes[selBone_].parent;
    Mat4 pw = o->world * o->fit;
    if (p >= 0 && p < (int)o->anim->world.size()) pw = pw * o->anim->world[p];
    Quat pq = worldRotOf(pw);
    Vec3 t;
    Quat d;
    if (!scene.boneDeltaAt(*o, selBone_, (float)curFrame(), t, d)) { t = Vec3(); d = Quat(); }
    Quat nd = normalize(conj(pq) * rw * pq * d);
    beginEdit();
    writeBonePose(scene, *o, selBone_, curFrame(), t, nd);
}

// ============================================================ панель «Кости»
void App::panelBones(const Rect& r) {
    float S = ui.S;
    ScrollState& sc = scrollBones_;
    form_.x = r.x + 12 * S; form_.w = r.w - 24 * S; form_.y = r.y + 4 * S - sc.off;
    float top = r.y - sc.off;
    ui.beginScroll(r, sc, contentBones_);
    Object* o = scene.find(scene.selected);
    bool isModel = o && o->type == ObjType::Model && o->model >= 0 && o->model < (int)scene.models.size() && scene.models[o->model].model;
    std::vector<int> bones;
    if (isModel) bones = scene.boneNodes(*o);
    if (!isModel) {
        Rect l = row(70);
        ui.label(l, "Выберите модель с костями: тапом по ней или в списке «Сцена»", 13, theme::textDim, 1);
    } else if (bones.empty()) {
        Rect l = row(70);
        ui.label(l, "У этой модели нет скелета. Двигайте её целиком или импортируйте glTF с костями.", 13, theme::textDim, 1);
    } else {
        const Model& m = *scene.models[o->model].model;
        int cf = curFrame();
        Rect hint = row(36);
        ui.label(hint, "Тап по точке кости — выбрать. Потяните выбранную кость пальцем — повернуть, поза запишется ключом.", 11, theme::textDim, 0);
        bool valid = selBone_ >= 0 && std::find(bones.begin(), bones.end(), selBone_) != bones.end();
        if (!valid) selBone_ = -1;
        if (selBone_ >= 0) {
            int id = o->id, node = selBone_;
            sectionLabel("Кость: " + boneName(m, node));
            Vec3 t;
            Quat q;
            if (!scene.boneDeltaAt(*o, node, (float)cf, t, q)) { t = Vec3(); q = Quat(); }
            Vec3 e = Scene::eulerFromMat(fromQuat(q));
            Rect line = row(38);
            const char* ax[3] = {"X°", "Y°", "Z°"};
            for (int i = 0; i < 3; i++) {
                float v = (i == 0 ? e.x : (i == 1 ? e.y : e.z)) * 180.f / kPi;
                auto apply = [this, id, node, i](float nv) {
                    Object* ob = scene.find(id);
                    if (!ob) return;
                    beginEdit();
                    Vec3 tt;
                    Quat qq;
                    if (!scene.boneDeltaAt(*ob, node, (float)curFrame(), tt, qq)) { tt = Vec3(); qq = Quat(); }
                    Vec3 ee = Scene::eulerFromMat(fromQuat(qq));
                    float rad = nv * kPi / 180.f;
                    if (i == 0) ee.x = rad; else if (i == 1) ee.y = rad; else ee.z = rad;
                    writeBonePose(scene, *ob, node, curFrame(), tt, quatEuler(ee.x, ee.y, ee.z));
                };
                if (slide(cell(line, i, 3, 4), ax[i], v, 0, 0, "%.1f", 0.6f, apply)) apply(v);
            }
            Rect b1 = row(42);
            bool keyed = scene.boneHasKey(*o, node, cf);
            if (ui.button(cell(b1, 0, 3), keyed ? "Обновить ключ" : "Ключ позы", 1, 12.5f)) { beginEdit(); boneKeyNow(); editStarted_ = false; }
            if (ui.button(cell(b1, 1, 3), "Сбросить ключ", 0, 12.5f)) {
                beginEdit();
                writeBonePose(scene, *o, node, cf, Vec3(), Quat());
                editStarted_ = false;
            }
            if (ui.button(cell(b1, 2, 3), "Убрать кость", 2, 12.5f)) {
                scene.pushUndo();
                scene.clearBone(id, node);
                scene.evaluate((float)frameF_);
            }
        }
        sectionLabel("Все кости (" + std::to_string(bones.size()) + ")");
        // глубина для отступа
        auto depth = [&](int n) {
            int d = 0;
            for (int p = m.nodes[n].parent; p >= 0 && d < 12; p = m.nodes[p].parent) d++;
            return d;
        };
        for (int n : bones) {
            Rect rr = row(40);
            bool tap = false;
            ui.listRow(rr, n == selBone_, &tap);
            if (tap) selBone_ = n;
            float ind = std::min(depth(n), 6) * 12.f * S;
            bool any = false;
            for (auto& tr : o->boneTracks) if (tr.node == n && !tr.keys.empty()) any = true;
            ui.g.circle(rr.x + 14 * S + ind, rr.cy(), 5 * S, any ? theme::keyCol : theme::accent2);
            ui.g.text(ui.g.fit(boneName(m, n), 13.5f * S, rr.w - 50 * S - ind), rr.x + 28 * S + ind, rr.cy() + 5 * S, 13.5f * S, n == selBone_ ? theme::accent2 : theme::text);
        }
        if (!o->boneTracks.empty()) {
            sectionLabel("Анимация костей");
            Rect b = row(42);
            if (ui.button(b, "Удалить все позы костей", 2, 13)) {
                int id = o->id;
                confirm("Удалить позы", "Все ключи костей этой модели будут удалены.", [this, id]() {
                    Object* ob = scene.find(id);
                    if (!ob) return;
                    scene.pushUndo();
                    ob->boneTracks.clear();
                    scene.evaluate((float)frameF_);
                }, "Удалить");
            }
        }
    }
    {
        Rect gb = row(44);
        if (ui.button(gb, "Экспорт анимации скелета (GLB)", 0, 14)) skeletalExportMenu();
    }
    contentBones_ = form_.y - top + 24 * S;
    ui.endScroll();
}

// ============================================================ общая строка-ползунок с пересчётом эффектов/физики
void App::propSim(Object& o, const std::string& label, std::function<float&(Object&)> ref, float lo, float hi, const char* fmt, float perDp) {
    float v = ref(o);
    int id = o.id;
    auto apply = [this, id, ref, lo, hi](float nv) {
        Object* ob = scene.find(id);
        if (!ob) return;
        beginEdit();
        ref(*ob) = clampf(nv, lo, hi);
        scene.resetEffects();
        scene.evaluate((float)frameF_);
        scene.simulateTo(curFrame());
    };
    Rect r = row(38);
    if (slide(r, label, v, lo, hi, fmt, perDp > 0 ? perDp : (hi - lo) / std::max(1.f, r.w / ui.S), apply)) apply(v);
}

// ============================================================ панель «Физика»
void App::panelPhysics(const Rect& r) {
    float S = ui.S;
    ScrollState& sc = scrollPhys_;
    form_.x = r.x + 12 * S; form_.w = r.w - 24 * S; form_.y = r.y + 4 * S - sc.off;
    float top = r.y - sc.off;
    ui.beginScroll(r, sc, contentPhys_);
    Object* o = scene.find(scene.selected);
    if (!o || o->type != ObjType::Model) {
        Rect l = row(80);
        ui.label(l, "Выберите модель или фигуру: физику можно включить любому предмету. Взрывы и кровь настраиваются во вкладке «Эффекты».", 13, theme::textDim, 1);
    } else {
        int id = o->id;
        int cf = curFrame();
        PhysProps& ph = o->phys;
        auto refresh = [this]() { scene.resetEffects(); scene.evaluate((float)frameF_); scene.simulateTo(curFrame()); };
        {
            Rect t = row(42);
            bool v = ph.enabled;
            if (ui.toggle(t, "Физика включена", v)) {
                beginEdit();
                ph.enabled = v;
                if (v && ph.kind == 0 && ph.start <= scene.proj.frameStart) ph.start = std::max(cf, scene.proj.frameStart);
                refresh();
                editStarted_ = false;
            }
        }
        if (!ph.enabled) {
            Rect hint = row(60);
            ui.label(hint, "Включите физику, затем выберите кадр, с которого предмет начнёт падать и сталкиваться. До этого кадра он идёт по ключам.", 11.5f, theme::textDim, 0);
            Rect b = row(44);
            if (ui.button(b, "Включить с кадра " + std::to_string(cf), 1, 14)) {
                beginEdit();
                ph.enabled = true; ph.kind = 0; ph.start = cf;
                refresh();
                editStarted_ = false;
            }
        } else {
            Rect seg = row(40);
            int k = ui.segmented(seg, {"Падает и сталкивается", "Неподвижный (пол, стена)"}, ph.kind);
            if (k >= 0 && k != ph.kind) { beginEdit(); ph.kind = k; refresh(); editStarted_ = false; }
            Rect seg2 = row(40);
            int k2 = ui.segmented(seg2, {"Коробка", "Шар"}, ph.shape);
            if (k2 >= 0 && k2 != ph.shape) { beginEdit(); ph.shape = k2; refresh(); editStarted_ = false; }
            if (ph.kind == 0) {
                sectionLabel("Когда включить");
                Rect b = row(44);
                if (ui.button(b, "Начать с текущего кадра (" + std::to_string(cf) + ")", 1, 13.5f)) { beginEdit(); ph.start = cf; refresh(); editStarted_ = false; }
                {
                    float st = (float)ph.start;
                    Rect line = row(38);
                    auto ap = [this, id, refresh](float nv) { Object* ob = scene.find(id); if (!ob) return; beginEdit(); ob->phys.start = std::max(0, (int)std::lround(nv)); scene.resetEffects(); scene.evaluate((float)frameF_); scene.simulateTo(curFrame()); };
                    if (slide(line, "Кадр включения", st, 0, 0, "%.0f", 0.2f, ap)) ap(st);
                }
                sectionLabel("Свойства");
                propSim(*o, "Масса", [](Object& x) -> float& { return x.phys.mass; }, 0.1f, 100.f, "%.1f", 0);
                propSim(*o, "Упругость (отскок)", [](Object& x) -> float& { return x.phys.bounce; }, 0.f, 1.f, "%.2f", 0);
                propSim(*o, "Трение", [](Object& x) -> float& { return x.phys.friction; }, 0.f, 1.5f, "%.2f", 0);
                {
                    Rect t = row(40);
                    bool v = ph.inherit;
                    if (ui.toggle(t, "Сохранить скорость движения", v)) { beginEdit(); ph.inherit = v; refresh(); editStarted_ = false; }
                }
                sectionLabel("Толчок в момент включения");
                propSim(*o, "Вперёд-назад (Z), м/с", [](Object& x) -> float& { return x.phys.vel.z; }, -25.f, 25.f, "%.1f", 0);
                propSim(*o, "Вбок (X), м/с", [](Object& x) -> float& { return x.phys.vel.x; }, -25.f, 25.f, "%.1f", 0);
                propSim(*o, "Вверх (Y), м/с", [](Object& x) -> float& { return x.phys.vel.y; }, -25.f, 25.f, "%.1f", 0);
                propSim(*o, "Вращение вокруг Y, рад/с", [](Object& x) -> float& { return x.phys.spin.y; }, -20.f, 20.f, "%.1f", 0);
                Rect hint = row(40);
                ui.label(hint, "Земля на высоте 0. Неподвижные предметы с включённой физикой работают как препятствия для падающих.", 11, theme::textDim, 0);
            } else {
                Rect hint = row(50);
                ui.label(hint, "Этот предмет не двигается и держит падающие: поставьте его как пол, стену или стол. Размер берётся по габаритам модели.", 11.5f, theme::textDim, 0);
            }
            Rect b = row(44);
            if (ui.button(b, "Смотреть с начала", 0, 14)) { playing_ = true; gotoFrame(scene.proj.frameStart); lastSimFrame_ = curFrame(); }
            Rect b2 = row(42);
            if (ui.button(b2, "Выключить физику", 2, 13.5f)) { beginEdit(); ph.enabled = false; refresh(); editStarted_ = false; }
        }
        panelSimExtras(*o);
    }
    contentPhys_ = form_.y - top + 24 * S;
    ui.endScroll();
}

// Рэгдолл по костям, инерция предмета на кости и ткань: общие переключатели и ползунки.
void App::panelSimExtras(Object& o) {
    int id = o.id;
    int cf = curFrame();
    auto refresh = [this]() { scene.resetEffects(); scene.evaluate((float)frameF_); scene.simulateTo(curFrame()); };
    auto intSlider = [this, id](const std::string& label, int cur, float lo, float hi, std::function<void(Object&, int)> set) {
        float v = (float)cur;
        Rect line = row(38);
        auto ap = [this, id, set, lo, hi](float nv) {
            Object* ob = scene.find(id);
            if (!ob) return;
            beginEdit();
            set(*ob, (int)std::lround(clampf(nv, lo, hi)));
            scene.resetEffects(); scene.evaluate((float)frameF_); scene.simulateTo(curFrame());
        };
        if (slide(line, label, v, lo, hi, "%.0f", 0.2f, ap)) ap(v);
    };

    // ---------- рэгдолл ----------
    sectionLabel("Рэгдолл: тело падает по костям");
    {
        bool rigged = scene.boneCount(o) >= 2;
        if (!rigged) {
            Rect hint = row(44);
            ui.label(hint, "У этой модели нет скелета, рэгдолл недоступен. Для обычных предметов включите физику выше.", 11.5f, theme::textDim, 0);
        } else {
            RagdollProps& rg = o.rag;
            Rect t = row(42);
            bool v = rg.enabled;
            if (ui.toggle(t, "Рэгдолл включён", v)) {
                beginEdit();
                rg.enabled = v;
                if (v) rg.start = std::max(cf, scene.proj.frameStart);
                refresh();
                editStarted_ = false;
            }
            if (rg.enabled) {
                Rect b = row(44);
                if (ui.button(b, "Начать с текущего кадра (" + std::to_string(cf) + ")", 1, 13.5f)) { beginEdit(); rg.start = cf; refresh(); editStarted_ = false; }
                intSlider("Кадр включения", rg.start, 0, 100000, [](Object& x, int n) { x.rag.start = n; });
                propSim(o, "Масса тела, кг", [](Object& x) -> float& { return x.rag.mass; }, 5.f, 200.f, "%.0f", 0);
                propSim(o, "Трение", [](Object& x) -> float& { return x.rag.friction; }, 0.f, 1.5f, "%.2f", 0);
                propSim(o, "Упругость", [](Object& x) -> float& { return x.rag.bounce; }, 0.f, 0.8f, "%.2f", 0);
                propSim(o, "Предел сгиба суставов, °", [](Object& x) -> float& { return x.rag.limit; }, 10.f, 150.f, "%.0f", 0);
                propSim(o, "Толщина тела", [](Object& x) -> float& { return x.rag.thick; }, 0.4f, 2.5f, "%.2f", 0);
                Rect tt = row(40);
                bool inh = rg.inherit;
                if (ui.toggle(tt, "Сохранить скорость анимации", inh)) { beginEdit(); rg.inherit = inh; refresh(); editStarted_ = false; }
                propSim(o, "Толчок вперёд (Z), м/с", [](Object& x) -> float& { return x.rag.push.z; }, -20.f, 20.f, "%.1f", 0);
                propSim(o, "Толчок вбок (X), м/с", [](Object& x) -> float& { return x.rag.push.x; }, -20.f, 20.f, "%.1f", 0);
                propSim(o, "Толчок вверх (Y), м/с", [](Object& x) -> float& { return x.rag.push.y; }, -10.f, 20.f, "%.1f", 0);
                Rect hint = row(54);
                ui.label(hint, std::string(scene.ragdollBaked(id) ? "Расчёт готов: " : "Расчёт ещё не готов: ") + "до кадра включения тело идёт по анимации, затем падает и ложится на пол. Ключи костей после этого кадра не действуют.", 11, theme::textDim, 0);
            }
        }
    }

    // ---------- инерция предмета ----------
    sectionLabel("Предмет качается (оружие на спине, сумка)");
    {
        SecondaryProps& sp = o.sec;
        Rect t = row(42);
        bool v = sp.enabled;
        if (ui.toggle(t, "Инерция включена", v)) { beginEdit(); sp.enabled = v; refresh(); editStarted_ = false; }
        if (sp.enabled) {
            propSim(o, "Жёсткость крепления", [](Object& x) -> float& { return x.sec.stiffness; }, 4.f, 150.f, "%.0f", 0);
            propSim(o, "Затухание", [](Object& x) -> float& { return x.sec.damping; }, 0.2f, 20.f, "%.1f", 0);
            propSim(o, "Вес (провисание)", [](Object& x) -> float& { return x.sec.gravity; }, 0.f, 2.f, "%.2f", 0);
            propSim(o, "Сила качания", [](Object& x) -> float& { return x.sec.amount; }, 0.f, 3.f, "%.2f", 0);
        }
        Rect hint = row(54);
        ui.label(hint, "Сначала прицепите предмет к кости персонажа (вкладка «Объект» → «Прицепить»). Когда персонаж двигается, предмет запаздывает и качается.", 11, theme::textDim, 0);
    }

    // ---------- ткань ----------
    sectionLabel("Ткань: плащ, юбка, волосы");
    {
        // выбор частей: ткань действует только на отмеченные меши, остальное тело остаётся как есть
        std::vector<int> cand = scene.clothCandidates(o);
        if (!cand.empty()) {
            const Model& mm = *scene.models[o.model].model;
            Rect hl = row(34);
            ui.label(hl, "Части модели с тканью (отметьте нужные):", 12, theme::textDim, 0);
            for (int mi : cand) {
                const MeshData& md = mm.meshes[(size_t)mi];
                std::string nm = md.name;
                if (nm.empty()) for (size_t ni = 0; ni < mm.nodes.size(); ni++) for (int q : mm.nodes[ni].meshes) if (q == mi && !mm.nodes[ni].name.empty()) nm = mm.nodes[ni].name;
                if (nm.empty()) nm = "часть " + std::to_string(mi);
                nm += md.skin >= 0 ? "  · на скелете" : "  · жёсткая";
                Rect tr = row(38);
                bool on = scene.clothMeshOn(o, mi);
                if (ui.toggle(tr, nm, on)) {
                    beginEdit();
                    std::vector<int> sel;
                    for (int c : cand) {
                        bool cur = scene.clothMeshOn(o, c);
                        if (c == mi) cur = !cur;
                        if (cur) sel.push_back(c);
                    }
                    if (sel.empty()) sel.push_back(-1);  // -1: ни одной части
                    o.cloth.meshes = sel;
                    refresh();
                    editStarted_ = false;
                }
            }
            Rect bb = row(38);
            if (ui.button(cell(bb, 0, 2), "Все части", 0, 12.5f)) { beginEdit(); o.cloth.meshes = cand; refresh(); editStarted_ = false; }
            if (ui.button(cell(bb, 1, 2), "Сбросить выбор", 0, 12.5f)) { beginEdit(); o.cloth.meshes.clear(); refresh(); editStarted_ = false; }
        }
    }
    {
        std::string why;
        if (!scene.clothSupported(o, &why)) {
            Rect hint = row(54);
            ui.label(hint, why.empty() ? "Для этой модели ткань недоступна." : why, 11.5f, theme::textDim, 0);
        } else {
            ClothProps& cl = o.cloth;
            Rect t = row(42);
            bool v = cl.enabled;
            if (ui.toggle(t, "Ткань включена", v)) { beginEdit(); cl.enabled = v; refresh(); editStarted_ = false; }
            if (cl.enabled) {
                Rect seg = row(40);
                int k = ui.segmented(seg, {"Закреплён верх", "Закреплён низ"}, cl.pinSide);
                if (k >= 0 && k != cl.pinSide) { beginEdit(); cl.pinSide = k; refresh(); editStarted_ = false; }
                propSim(o, "Закреплённая доля", [](Object& x) -> float& { return x.cloth.pin; }, 0.f, 0.8f, "%.2f", 0);
                propSim(o, "Жёсткость ткани", [](Object& x) -> float& { return x.cloth.stiffness; }, 0.1f, 1.f, "%.2f", 0);
                propSim(o, "Возврат к форме", [](Object& x) -> float& { return x.cloth.shape; }, 0.f, 1.f, "%.2f", 0);
                propSim(o, "Затухание", [](Object& x) -> float& { return x.cloth.damping; }, 0.f, 0.3f, "%.3f", 0);
                propSim(o, "Гравитация", [](Object& x) -> float& { return x.cloth.gravity; }, 0.f, 2.f, "%.2f", 0);
                propSim(o, "Ветер, м/с", [](Object& x) -> float& { return x.cloth.wind; }, 0.f, 15.f, "%.1f", 0);
                propSim(o, "Направление ветра, °", [](Object& x) -> float& { return x.cloth.windDir; }, 0.f, 360.f, "%.0f", 0);
                Rect hint = row(54);
                ui.label(hint, std::string(scene.clothBaked(id) ? "Расчёт готов. " : "Расчёт ещё не готов. ") + "Ткань считается по кадрам от начала проекта; выше закреплённый край остаётся на месте.", 11, theme::textDim, 0);
            }
        }
    }
}

// ============================================================ панель «Эффекты»
void App::panelFx(const Rect& r) {
    float S = ui.S;
    ScrollState& sc = scrollFx_;
    form_.x = r.x + 12 * S; form_.w = r.w - 24 * S; form_.y = r.y + 4 * S - sc.off;
    float top = r.y - sc.off;
    ui.beginScroll(r, sc, contentFx_);
    Object* o = scene.find(scene.selected);
    {
        Rect b = row(46);
        if (ui.button(b, "Добавить эффект…", 1, 14.5f)) addFxMenu();
    }
    if (!o || o->type != ObjType::Effect) {
        Rect l = row(70);
        ui.label(l, "Выберите эффект в списке «Сцена» или тапом по значку в окне, чтобы настроить его: силу, время, положение.", 12.5f, theme::textDim, 1);
    } else {
        int id = o->id;
        static const char* names[] = {"Огонь", "Дым", "Вспышка выстрела", "Взрыв", "Искры", "Кровь", "Разбитое стекло"};
        sectionLabel(std::string(names[std::min(6, (int)o->fx)]) + " — " + o->name);
        auto resim = [this]() { scene.resetEffects(); scene.simulateTo(curFrame()); };
        if (o->fx == FxKind::Muzzle) {
            Rect seg = row(42);
            int k = ui.segmented(seg, {"Пистолет", "Дробовик", "Автомат"}, o->fxVariant);
            if (k >= 0 && k != o->fxVariant) { beginEdit(); o->fxVariant = k; resim(); editStarted_ = false; }
            propSim(*o, "Яркость вспышки", [](Object& x) -> float& { return x.fxPower; }, 0.3f, 3.f, "%.2f", 0);
            float rate = (float)o->fxRate;
            Rect l2 = row(38);
            auto ap = [this, id](float nv) { Object* ob = scene.find(id); if (!ob) return; beginEdit(); ob->fxRate = std::max(0, (int)std::lround(nv)); scene.resetEffects(); scene.simulateTo(curFrame()); };
            if (slide(l2, "Очередь: каждые N кадров (0 — один выстрел)", rate, 0, 24, "%.0f", 0.05f, ap)) ap(rate);
        } else if (o->fx == FxKind::Explosion) {
            propSim(*o, "Сила взрыва", [](Object& x) -> float& { return x.fxPower; }, 0.3f, 5.f, "%.2f", 0);
            Rect t = row(40);
            bool v = o->fxPush;
            if (ui.toggle(t, "Толкает предметы с физикой", v)) { beginEdit(); o->fxPush = v; scene.evaluate((float)frameF_); resim(); editStarted_ = false; }
            Rect hint = row(40);
            ui.label(hint, "Чем больше сила, тем крупнее огонь, выше столб дыма и дальше летят осколки и предметы.", 11, theme::textDim, 0);
        } else if (o->fx == FxKind::Glass) {
            Rect tb = row(42);
            const Object* tg = scene.find(o->fxTarget);
            std::string tl = tg ? "Стекло: " + tg->name : "Стекло: не выбрано (условная панель)";
            if (ui.button(tb, tl, 0, 13)) {
                std::vector<std::string> names2{"Не выбрано (условная панель)"};
                std::vector<int> ids{-1};
                for (auto& q : scene.objects) if (q->glass) { names2.push_back(q->name); ids.push_back(q->id); }
                pickList("Какое стекло разбить", names2, [this, id, ids](int k) {
                    Object* ob = scene.find(id);
                    if (!ob || k < 0 || k >= (int)ids.size()) return;
                    beginEdit(); ob->fxTarget = ids[k]; scene.resetEffects(); scene.simulateTo(curFrame()); editStarted_ = false;
                });
            }
            propSim(*o, "Сила удара", [](Object& x) -> float& { return x.fxPower; }, 0.2f, 4.f, "%.2f", 0);
            Rect hint = row(60);
            ui.label(hint, "Точка удара — положение эффекта, направление удара — ось Z (синяя). Чем сильнее удар, тем дальше летят осколки. Размер — крупность осколков: меньше значение — больше мелких.", 11, theme::textDim, 0);
        } else if (o->fx == FxKind::Blood) {
            Rect seg = row(42);
            int k = ui.segmented(seg, {"Брызги", "Течёт"}, o->fxVariant);
            if (k >= 0 && k != o->fxVariant) { beginEdit(); o->fxVariant = k; resim(); editStarted_ = false; }
            propSim(*o, "Сила (давление)", [](Object& x) -> float& { return x.fxPower; }, 0.3f, 3.f, "%.2f", 0);
            Rect hint = row(40);
            ui.label(hint, "Капли падают по физике и оставляют пятна на полу. Направление — вдоль оси Z эффекта.", 11, theme::textDim, 0);
        }
        propSim(*o, "Размер", [](Object& x) -> float& { return x.fxScale; }, 0.1f, 6.f, "%.2f", 0);
        sectionLabel("Время");
        {
            float st = (float)o->fxStart, en = (float)o->fxEnd;
            Rect line = row(38);
            auto aS = [this, id](float nv) { Object* ob = scene.find(id); if (!ob) return; beginEdit(); ob->fxStart = std::max(0, std::min((int)std::lround(nv), ob->fxEnd - 1)); scene.resetEffects(); scene.simulateTo(curFrame()); };
            auto aE = [this, id](float nv) { Object* ob = scene.find(id); if (!ob) return; beginEdit(); ob->fxEnd = std::max(ob->fxStart + 1, (int)std::lround(nv)); scene.resetEffects(); scene.simulateTo(curFrame()); };
            if (slide(cell(line, 0, 2), "Начало, кадр", st, 0, 0, "%.0f", 0.15f, aS)) aS(st);
            if (slide(cell(line, 1, 2), "Конец, кадр", en, 0, 0, "%.0f", 0.15f, aE)) aE(en);
            Rect b = row(42);
            if (ui.button(cell(b, 0, 2), "Начать здесь", 0, 13)) { beginEdit(); int len = o->fxEnd - o->fxStart; o->fxStart = curFrame(); o->fxEnd = o->fxStart + std::max(1, len); resim(); editStarted_ = false; }
            if (ui.button(cell(b, 1, 2), "Смотреть", 1, 13)) { playing_ = true; gotoFrame(std::max(scene.proj.frameStart, o->fxStart - 6)); scene.resetEffects(); lastSimFrame_ = curFrame(); }
            Rect hint = row(34);
            ui.label(hint, "Оранжевую полосу в таймлайне можно тянуть: за края — длительность, за середину — сдвиг.", 11, theme::textDim, 0);
        }
        sectionLabel("Положение");
        {
            Rect b = row(42);
            if (ui.button(cell(b, 0, 2), "В центр вида", 0, 13)) {
                scene.pushUndo();
                Vec3 t, rr, ss;
                scene.localTRS(*o, t, rr, ss);
                t.x = target_.x; t.z = target_.z;
                scene.setLocal(id, t, rr, ss);
                resim();
            }
            if (ui.button(cell(b, 1, 2), "Прицепить…", 0, 13)) tab_ = Tab::Object;
            Rect hint = row(50);
            ui.label(hint, "Двигайте эффект гизмо в окне. Чтобы он следовал за оружием или рукой — «Прицепить»; чтобы лететь самому — ключи положения на таймлайне.", 11, theme::textDim, 0);
        }
    }
    contentFx_ = form_.y - top + 24 * S;
    ui.endScroll();
}

}  // namespace eng
