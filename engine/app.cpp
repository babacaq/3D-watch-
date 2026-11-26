#include "app.h"
#include "glb_export.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>

#include "log.h"
#include "png_writer.h"

namespace eng {

static bool readWhole(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}
static std::string dirOf(const std::string& p) { size_t i = p.find_last_of('/'); return i == std::string::npos ? "" : p.substr(0, i + 1); }
static std::string baseOf(const std::string& p) { size_t i = p.find_last_of('/'); return i == std::string::npos ? p : p.substr(i + 1); }
static std::string stemOf(const std::string& p) { std::string b = baseOf(p); size_t i = b.find_last_of('.'); return i == std::string::npos ? b : b.substr(0, i); }

static Mat4 axisAngle(Vec3 n, float a) {
    n = normalize(n);
    float s = std::sin(a * 0.5f);
    return fromQuat(Quat(n.x * s, n.y * s, n.z * s, std::cos(a * 0.5f)));
}
static Mat4 translation(Vec3 t) {
    Mat4 m;
    m.m[12] = t.x; m.m[13] = t.y; m.m[14] = t.z;
    return m;
}
static float distPointSeg(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    float t = l2 > 1e-6f ? clampf(((px - ax) * dx + (py - ay) * dy) / l2, 0.f, 1.f) : 0.f;
    float cx = ax + dx * t - px, cy = ay + dy * t - py;
    return std::sqrt(cx * cx + cy * cy);
}

static std::string urlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else o += s[i];
    }
    return o;
}

// Читатель файлов рядом с моделью: допускает обратные слэши, %-кодировку и «плоскую» копию из выбора файлов.
static FileReader makeReader(const std::string& dir) {
    return [dir](const std::string& rel, std::vector<uint8_t>& o) {
        std::string r = rel;
        for (char& c : r) if (c == '\\') c = '/';
        if (readWhole(dir + r, o)) return true;
        std::string d = urlDecode(r);
        if (readWhole(dir + d, o)) return true;
        return readWhole(dir + baseOf(d), o);
    };
}

// ============================================================ инициализация
bool App::initGL(int w, int h, float density) {
    W_ = w; H_ = h; density_ = density;
    loadSettings();
    if (!renderer.init()) { ENG_LOG("renderer init failed"); return false; }
    if (!ui.g.init()) { ENG_LOG("ui init failed"); return false; }
    scene.modelLoader = [this](const std::string& src) -> std::unique_ptr<Model> {
        std::vector<uint8_t> data;
        if (!readWhole(src, data)) return nullptr;
        std::string err;
        return importModel(baseOf(src), data.data(), data.size(), makeReader(dirOf(src)), &err);
    };
    scene.evaluate(1.f);
    gl_ = true;
    return true;
}

void App::shutdown() {
    if (!gl_) return;
    for (auto& m : scene.models) if (m.model) m.model->release();
    scene.releaseGL();
    scene.particles().release();
    renderer.release();
    ui.g.release();
    gl_ = false;
}

void App::resize(int w, int h, float density) { W_ = w; H_ = h; density_ = density; }

void App::toast(const std::string& s) {
    toastMsg_ = s;
    toastT_ = 3.0f;
    if (plat.toast) plat.toast(s);
}

void App::uploadPending() {
    for (auto& m : scene.models) if (m.model && !m.model->uploaded) m.model->upload();
}

// ============================================================ импорт и проекты
bool App::importModelData(const std::string& displayName, const uint8_t* d, size_t n, const FileReader& rd, std::string* err) {
    std::string e;
    auto m = importModel(displayName, d, n, rd, &e);
    if (!m) { if (err) *err = e; return false; }
    float maxDur = 0;
    for (auto& c : m->clips) maxDur = std::max(maxDur, c.duration);
    std::string nm = stemOf(displayName);
    int id = scene.addModel(std::move(m), plat.dataDir + "/" + displayName, nm);
    uploadPending();
    if (maxDur > 0) {
        int need = (int)std::ceil(maxDur * scene.proj.fps) + scene.proj.frameStart;
        if (need > scene.proj.frameEnd) scene.proj.frameEnd = std::min(need, 3000);
    }
    scene.evaluate((float)frameF_);
    scene.selected = id;
    focusSelected();
    autoSaveDirty_ = true;
    return true;
}

bool App::importModelPath(const std::string& path, std::string* err) {
    std::vector<uint8_t> data;
    if (!readWhole(path, data)) { if (err) *err = "не удалось прочитать файл"; return false; }
    std::string e;
    auto m = importModel(baseOf(path), data.data(), data.size(), makeReader(dirOf(path)), &e);
    if (!m) { if (err) *err = e; return false; }
    float maxDur = 0;
    for (auto& c : m->clips) maxDur = std::max(maxDur, c.duration);
    int id = scene.addModel(std::move(m), path, stemOf(path));
    uploadPending();
    if (maxDur > 0) {
        int need = (int)std::ceil(maxDur * scene.proj.fps) + scene.proj.frameStart;
        if (need > scene.proj.frameEnd) scene.proj.frameEnd = std::min(need, 3000);
    }
    scene.evaluate((float)frameF_);
    scene.selected = id;
    focusSelected();
    autoSaveDirty_ = true;
    return true;
}

std::vector<std::string> App::listProjects() const {
    std::vector<std::string> out;
    DIR* d = opendir(projectsDir().c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        const std::string ext = ".prismlab";
        if (n.size() > ext.size() && n.compare(n.size() - ext.size(), ext.size(), ext) == 0) out.push_back(n.substr(0, n.size() - ext.size()));
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

static std::string sanitizeName(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') o += '_';
        else o += c;
    }
    while (!o.empty() && (o.back() == ' ' || o.back() == '.')) o.pop_back();
    return o.empty() ? "Project" : o;
}

bool App::saveProjectAs(const std::string& nameIn, bool quiet) {
    std::string name = sanitizeName(nameIn);
    mkdir(plat.dataDir.c_str(), 0755);
    mkdir(projectsDir().c_str(), 0755);
    std::string path = projectsDir() + "/" + name + ".prismlab";
    std::ofstream f(path, std::ios::binary);
    if (!f) { toast("Не удалось сохранить проект"); return false; }
    scene.name = name;
    std::string s = scene.saveProject();
    f.write(s.data(), (std::streamsize)s.size());
    if (!quiet) { toast("Проект «" + name + "» сохранён"); thumbPending_ = name; }
    homeDirty_ = true;
    return true;
}

bool App::openProject(const std::string& name) {
    std::vector<uint8_t> data;
    if (!readWhole(projectsDir() + "/" + name + ".prismlab", data)) { toast("Проект не найден"); return false; }
    std::string txt((const char*)data.data(), data.size()), err;
    for (auto& m : scene.models) if (m.model) m.model->release();
    if (!scene.loadProject(txt, &err)) { toast("Ошибка проекта: " + err); return false; }
    scene.name = name;
    uploadPending();
    playing_ = false;
    selBone_ = -1;
    tab_ = Tab::None;
    home_ = false;
    gotoFrame(scene.proj.frameStart);
    scene.resetEffects();
    scene.simulateTo(scene.proj.frameStart);
    target_ = {0, 0.9f, 0}; yaw_ = 0.55f; pitch_ = 0.32f; dist_ = 7.5f;
    toast("Открыт проект «" + name + "»");
    return true;
}

void App::newProject() {
    if (!plat.dataDir.empty() && !home_ && !scene.name.empty() && scene.name != "Untitled") saveProjectAs(scene.name, true);
    for (auto& m : scene.models) if (m.model) m.model->release();
    scene.clear();
    playing_ = false;
    selBone_ = -1;
    tab_ = Tab::None;
    frameF_ = 1.0;
    scene.evaluate(1.f);
    target_ = {0, 0.9f, 0}; yaw_ = 0.55f; pitch_ = 0.32f; dist_ = 7.5f;
    home_ = false;
    if (!plat.dataDir.empty()) {
        std::string nn;
        for (int i = 1; i < 1000; i++) {
            nn = "Проект " + std::to_string(i);
            struct stat st;
            if (stat((projectsDir() + "/" + nn + ".prismlab").c_str(), &st) != 0) break;
        }
        scene.name = nn;
        saveProjectAs(nn, true);
        thumbPending_ = nn;
    }
}

void App::loadAutosave() {
    std::vector<uint8_t> data;
    if (!readWhole(plat.dataDir + "/autosave.prismlab", data)) return;
    std::string txt((const char*)data.data(), data.size()), err;
    if (scene.loadProject(txt, &err)) {
        uploadPending();
        frameF_ = scene.proj.frameStart;
        scene.evaluate((float)frameF_);
        scene.simulateTo((int)frameF_);
    }
}

void App::onPause() {
    if (plat.dataDir.empty() || !gl_) return;
    if (!home_ && autoSaveDirty_ && !scene.name.empty() && scene.name != "Untitled") { saveProjectAs(scene.name, true); autoSaveDirty_ = false; }
    mkdir(plat.dataDir.c_str(), 0755);
    std::ofstream f(plat.dataDir + "/autosave.prismlab", std::ios::binary);
    if (f) { std::string s = scene.saveProject(); f.write(s.data(), (std::streamsize)s.size()); }
}

// ============================================================ время
void App::setFrame(int f) { gotoFrame(f); }

void App::gotoFrame(int f) {
    f = std::max(scene.proj.frameStart, std::min(f, scene.proj.frameEnd));
    frameF_ = f;
    scene.simulateTo(f);
    lastSimFrame_ = f;
}

void App::advancePlayback(double dt) {
    if (!playing_) return;
    double fps = std::max(1, scene.proj.fps);
    frameF_ += dt * fps;
    int a = scene.proj.frameStart, b = scene.proj.frameEnd;
    if (frameF_ > b + 0.999) {
        frameF_ = a + std::fmod(frameF_ - a, (double)(b - a + 1));
        scene.resetEffects();
        lastSimFrame_ = -1000;
    }
    int f = (int)std::floor(frameF_);
    if (f != lastSimFrame_) {
        scene.simulateTo(f);
        lastSimFrame_ = f;
    }
}

// ============================================================ касания
void App::touch(int action, int id, float x, float y) { events_.push_back({action, id, x, y}); }

bool App::onBack() {
    if (settingsOpen_ && modal_.kind == Modal::None) { settingsOpen_ = false; return true; }
    if (modal_.kind != Modal::None) {
        if (exp_.kind == Export::None) modal_ = Modal();
        return true;
    }
    if (home_) return false;
    if (tab_ != Tab::None) { tab_ = Tab::None; return true; }
    if (camView_) { camView_ = false; return true; }
    goHome();
    return true;
}

// ============================================================ раскладка
void App::layout() {
    float S = ui.S;
    land_ = W_ > H_ * 1.15f;
    if (home_) {
        rTop_ = Rect(0, 0, (float)W_, 0);
        rVP_ = Rect(0, 0, (float)W_, (float)H_);
        rTab_ = rTL_ = rPanel_ = Rect();
        return;
    }
    float topH = (land_ ? 52 : 58) * S;
    rTop_ = Rect(0, 0, (float)W_, topH);
    bool open = tab_ != Tab::None;
    if (!land_) {
        float barH = 78 * S, tlH = (open ? 116 : 132) * S;
        float panelH = open ? std::min(H_ * 0.36f, 330.f * S) : 0.f;
        rTab_ = Rect(0, H_ - barH, (float)W_, barH);
        rTL_ = Rect(0, rTab_.y - tlH, (float)W_, tlH);
        rPanel_ = Rect(0, rTL_.y - panelH, (float)W_, panelH);
        rVP_ = Rect(0, topH, (float)W_, rPanel_.y - topH);
    } else {
        float barW = 88 * S, tlH = 112 * S;
        float panelW = open ? std::min(W_ * 0.42f, 380.f * S) : 0.f;
        rTab_ = Rect(W_ - barW, topH, barW, H_ - topH);
        rPanel_ = Rect(rTab_.x - panelW, topH, panelW, H_ - topH);
        float mainW = rPanel_.x;
        rTL_ = Rect(0, H_ - tlH, mainW, tlH);
        rVP_ = Rect(0, topH, mainW, rTL_.y - topH);
    }
}

Rect App::cameraViewRect() const {
    float asp = (float)scene.render.exportW / std::max(1, scene.render.exportH);
    float w = rVP_.w, h = w / asp;
    if (h > rVP_.h) { h = rVP_.h; w = h * asp; }
    return Rect(rVP_.cx() - w * 0.5f, rVP_.cy() - h * 0.5f, w, h);
}

View App::currentView(float aspect) const {
    View v;
    Vec3 e = target_ + Vec3(std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), std::cos(pitch_) * std::cos(yaw_)) * dist_;
    v.eye = e;
    v.view = lookAt(e, target_, {0, 1, 0});
    // ближняя и дальняя плоскости следуют за расстоянием: вблизи без «срезания», вдали без обрезки сцены
    float nearP = clampf(dist_ * 0.015f, 0.03f, 0.5f), farP = std::max(150.f, dist_ * 40.f);
    v.proj = perspective(fov_ * kPi / 180.f, aspect, nearP, farP);
    return v;
}

void App::renderViewport() {
    if (!renderer.ready() || rVP_.w < 8 || rVP_.h < 8) return;
    const Object* cam = camView_ ? scene.find(scene.activeCamera) : nullptr;
    if (camView_ && !cam) camView_ = false;
    Overlay ov;
    if (cam) {
        viewRect_ = cameraViewRect();
        view_ = Renderer::cameraView(*cam, viewRect_.w / viewRect_.h);
        ov.grid = false; ov.icons = false; ov.selection = true; ov.bones = false;
    } else {
        viewRect_ = rVP_;
        view_ = currentView(viewRect_.w / viewRect_.h);
        ov.grid = true; ov.icons = true; ov.selection = true; ov.bones = false;
    }
    eye_ = view_.eye;
    renderer.setPixelScale(ui.S * 0.8f * viewScale_);
    RectI r{(int)viewRect_.x, (int)(H_ - viewRect_.b()), (int)viewRect_.w, (int)viewRect_.h};
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo_);
    glViewport(0, 0, W_, H_);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(theme::bg.r, theme::bg.g, theme::bg.b, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    renderer.renderView(scene, view_, targetFbo_, r, ov, viewScale_);
}

bool App::project(const Vec3& w, float& sx, float& sy) const {
    Mat4 vp = view_.proj * view_.view;
    const float* m = vp.m;
    float cx = m[0] * w.x + m[4] * w.y + m[8] * w.z + m[12];
    float cy = m[1] * w.x + m[5] * w.y + m[9] * w.z + m[13];
    float cw = m[3] * w.x + m[7] * w.y + m[11] * w.z + m[15];
    if (cw < 0.02f) return false;
    sx = viewRect_.x + (cx / cw * 0.5f + 0.5f) * viewRect_.w;
    sy = viewRect_.y + (1.f - (cy / cw * 0.5f + 0.5f)) * viewRect_.h;
    return true;
}

void App::rayFromScreen(float x, float y, Vec3& ro, Vec3& rd) const {
    float nx = (x - viewRect_.x) / viewRect_.w * 2.f - 1.f, ny = 1.f - (y - viewRect_.y) / viewRect_.h * 2.f;
    Mat4 inv = inverse(view_.proj * view_.view);
    auto un = [&](float z) {
        const float* m = inv.m;
        float px = m[0] * nx + m[4] * ny + m[8] * z + m[12], py = m[1] * nx + m[5] * ny + m[9] * z + m[13];
        float pz = m[2] * nx + m[6] * ny + m[10] * z + m[14], pw = m[3] * nx + m[7] * ny + m[11] * z + m[15];
        return Vec3(px / pw, py / pw, pz / pw);
    };
    Vec3 a = un(-1.f), b = un(1.f);
    ro = view_.eye;
    rd = normalize(b - a);
}

// ============================================================ главный кадр
void App::frame(double now) {
    if (!gl_) return;
    now_ = now;
    dt_ = lastTime_ < 0 ? 1.f / 60 : (float)std::min(0.1, std::max(0.0, now - lastTime_));
    lastTime_ = now;
    if (toastT_ > 0) toastT_ -= dt_;

    // события касаний: не больше одного фронта (нажатие/отпускание) за кадр
    while (!events_.empty()) {
        Ev e = events_.front();
        events_.pop_front();
        bool edge = false;
        if (e.action == 0) {
            ptrs_.push_back({e.id, e.x, e.y});
            if (primaryId_ < 0) { primaryId_ = e.id; primary_.down = true; primary_.x = e.x; primary_.y = e.y; edge = true; }
        } else if (e.action == 1) {
            for (auto& p : ptrs_) if (p.id == e.id) { p.x = e.x; p.y = e.y; }
            if (e.id == primaryId_) { primary_.x = e.x; primary_.y = e.y; }
        } else {
            ptrs_.erase(std::remove_if(ptrs_.begin(), ptrs_.end(), [&](const Ptr& p) { return p.id == e.id; }), ptrs_.end());
            if (e.id == primaryId_) { primary_.x = e.x; primary_.y = e.y; primary_.down = false; primaryId_ = -1; edge = true; }
        }
        if (edge) break;
    }

    uploadPending();
    ui.S = density_ * settings.uiScale;
    updateCameraMotion();
    if (!thumbPending_.empty()) { std::string n = thumbPending_; thumbPending_.clear(); if (!home_ || true) makeThumb(n); }
    layout();
    if (home_) {
        glBindFramebuffer(GL_FRAMEBUFFER, targetFbo_);
        glViewport(0, 0, W_, H_);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(theme::bg.r, theme::bg.g, theme::bg.b, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ui.begin(W_, H_, density_ * settings.uiScale, primary_, dt_);
        ui.blocked = modal_.kind != Modal::None || settingsOpen_;
        drawHome();
        ui.blocked = modal_.kind != Modal::None;
        glViewport(0, 0, W_, H_);
        drawSettings();
        ui.blocked = false;
        drawModal();
        drawToast();
        ui.end();
        return;
    }
    advancePlayback(dt_);
    if (!playing_ && exp_.kind == Export::None && scene.frame() != (float)(int)std::lround(frameF_)) scene.evaluate((float)(int)std::lround(frameF_));

    renderViewport();

    ui.begin(W_, H_, density_ * settings.uiScale, primary_, dt_);
    ui.blocked = modal_.kind != Modal::None || settingsOpen_;
    vpId_ = ui.zone(rVP_);
    drawTopBar();
    drawViewportOverlay();
    drawTimeline();
    drawPanel();
    drawTabs();
    if (!ui.blocked) viewportGestures();
    ui.blocked = modal_.kind != Modal::None;
    glViewport(0, 0, W_, H_);
    drawSettings();
    ui.blocked = false;
    drawModal();
    drawToast();
    bool rel = ui.releasedNow();
    ui.end();
    if (rel) editStarted_ = false;
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo_);
    stepExport();

    // автосохранение
    if (now - lastAutosave_ > 30.0) { lastAutosave_ = now; if (!plat.dataDir.empty()) onPause(); }
}

// ============================================================ жесты
void App::orbitGesture(float dx, float dy, float zoom, float panX, float panY) {
    float sens = zoom > 0.f || panX != 0.f || panY != 0.f ? 1.f : clampf(settings.orbitSens, 0.2f, 3.f);
    if (dx != 0.f || dy != 0.f) goalOn_ = false;
    yaw_ -= dx * 0.006f * sens;
    pitch_ = clampf(pitch_ + dy * 0.006f * sens * (settings.invertY ? -1.f : 1.f), -1.45f, 1.5f);
    if (zoom > 0.f) dist_ = clampf(dist_ * zoom, 0.4f, 80.f);
    if (panX != 0.f || panY != 0.f) {
        float k = dist_ * 2.f * std::tan(fov_ * kPi / 360.f) / std::max(1.f, viewRect_.h);
        Vec3 right(view_.view.m[0], view_.view.m[4], view_.view.m[8]), up(view_.view.m[1], view_.view.m[5], view_.view.m[9]);
        target_ = target_ - right * (panX * k) + up * (panY * k);
    }
}

void App::cameraGesture(float dyaw, float dpitch, float dolly, float panX, float panY) {
    Object* c = scene.find(scene.activeCamera);
    if (!c) return;
    Vec3 fwd = normalize(-Vec3(c->world.m[8], c->world.m[9], c->world.m[10]));
    Vec3 right = normalize(Vec3(c->world.m[0], c->world.m[1], c->world.m[2]));
    Vec3 up = normalize(Vec3(c->world.m[4], c->world.m[5], c->world.m[6]));
    float pitch = std::asin(clampf(fwd.y, -1.f, 1.f)), yaw = std::atan2(-fwd.x, -fwd.z);
    yaw += dyaw;
    pitch = clampf(pitch + dpitch, -1.5f, 1.5f);
    Vec3 pos(c->world.m[12], c->world.m[13], c->world.m[14]);
    Mat4 R = axisAngle({0, 1, 0}, yaw) * axisAngle({1, 0, 0}, pitch);
    Vec3 nf = normalize(-Vec3(R.m[8], R.m[9], R.m[10]));
    Vec3 nr = normalize(Vec3(R.m[0], R.m[1], R.m[2])), nu = normalize(Vec3(R.m[4], R.m[5], R.m[6]));
    (void)right; (void)up;
    pos = pos + nf * dolly + nr * panX + nu * panY;
    R.m[12] = pos.x; R.m[13] = pos.y; R.m[14] = pos.z;
    scene.setWorld(c->id, R);
}

void App::focusSelected() {
    const Object* o = scene.find(scene.selected);
    if (!o) return;
    Vec3 p = transformPoint(o->world, {0, 0, 0});
    float size = 1.5f;
    Vec3 lo, hi;
    if (scene.localBox(*o, lo, hi)) {
        Mat4 M = o->world * o->fit;
        Vec3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
        for (int i = 0; i < 8; i++) {
            Vec3 q = transformPoint(M, {(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z});
            mn = {std::min(mn.x, q.x), std::min(mn.y, q.y), std::min(mn.z, q.z)};
            mx = {std::max(mx.x, q.x), std::max(mx.y, q.y), std::max(mx.z, q.z)};
        }
        p = (mn + mx) * 0.5f;
        size = std::max(0.5f, length(mx - mn));
    }
    goalTarget_ = p;
    goalDist_ = clampf(size * 1.7f, 1.5f, 60.f);
    goalOn_ = true; goalAng_ = false;
}

void App::viewPreset(int i) {
    // 0 спереди, 1 сзади, 2 слева, 3 справа, 4 сверху, 5 перспектива
    static const float yaw[6] = {0.f, kPi, -kPi * 0.5f, kPi * 0.5f, 0.f, 0.55f};
    static const float pit[6] = {0.04f, 0.04f, 0.04f, 0.04f, 1.45f, 0.32f};
    i = std::max(0, std::min(5, i));
    // кратчайший путь по углу
    float d = yaw[i] - yaw_;
    while (d > kPi) d -= 2 * kPi;
    while (d < -kPi) d += 2 * kPi;
    goalYaw_ = yaw_ + d; goalPitch_ = pit[i];
    goalTarget_ = target_; goalDist_ = dist_;
    goalOn_ = true; goalAng_ = true;
    oVelX_ = oVelY_ = 0;
}

void App::viewPresetMenu() {
    pickList("Вид", {"Спереди", "Сзади", "Слева", "Справа", "Сверху", "Перспектива"}, [this](int i) { viewPreset(i); },
             {Icon::Camera, Icon::Camera, Icon::Camera, Icon::Camera, Icon::Camera, Icon::Cube});
}

// Плавный переход к цели и инерция после броска.
void App::updateCameraMotion() {
    if (goalOn_) {
        float k = 1.f - std::exp(-dt_ * 9.f);
        target_ = lerp(target_, goalTarget_, k);
        dist_ += (goalDist_ - dist_) * k;
        if (goalAng_) { yaw_ += (goalYaw_ - yaw_) * k; pitch_ += (goalPitch_ - pitch_) * k; }
        bool done = length(target_ - goalTarget_) < 1e-3f && std::fabs(dist_ - goalDist_) < 1e-3f &&
                    (!goalAng_ || (std::fabs(yaw_ - goalYaw_) < 2e-3f && std::fabs(pitch_ - goalPitch_) < 2e-3f));
        if (done) { target_ = goalTarget_; dist_ = goalDist_; if (goalAng_) { yaw_ = goalYaw_; pitch_ = goalPitch_; } goalOn_ = false; }
    }
    if ((oVelX_ != 0.f || oVelY_ != 0.f) && gest_ != Gest::Orbit && gest_ != Gest::Pinch && !camView_) {
        orbitGesture(oVelX_ * dt_, oVelY_ * dt_, 0, 0, 0);
        float damp = std::exp(-dt_ * 4.5f);
        oVelX_ *= damp; oVelY_ *= damp;
        if (std::hypot(oVelX_, oVelY_) < 12.f) oVelX_ = oVelY_ = 0.f;
    }
}

void App::alignCameraToView() {
    Object* c = scene.find(scene.selected);
    if (!c || c->type != ObjType::Camera) return;
    View v = currentView(1.f);
    beginEdit();
    scene.setWorld(c->id, inverse(v.view));
}

void App::pickAt(float x, float y) {
    {
        const Object* so = scene.find(scene.selected);
        if (!camView_ && so && so->type == ObjType::Model && (showBones_ || tab_ == Tab::Bones)) {
            int node;
            if (boneAtScreen(x, y, node)) { selBone_ = node; return; }
            selBone_ = -1;  // нажали мимо костей - снимаем выбор кости
        }
    }
    Vec3 ro, rd;
    rayFromScreen(x, y, ro, rd);
    float mt = 1e30f;
    int mid = scene.pick(ro, rd, &mt);
    int best = -1;
    float bestD = ui.dp(34);
    for (auto& op : scene.objects) {
        const Object& o = *op;
        if (o.type == ObjType::Model || !o.visibleNow) continue;
        float sx, sy;
        if (!project(transformPoint(o.world, {0, 0, 0}), sx, sy)) continue;
        float d = std::hypot(sx - x, sy - y);
        if (d < bestD) { bestD = d; best = o.id; }
    }
    int sel = best >= 0 ? best : mid;
    if (sel != scene.selected) selBone_ = -1;
    scene.selected = sel;
    if (sel >= 0 && tab_ == Tab::None) {}
}

// ---------------- гизмо ----------------
// Гизмо работает на кости, если выбрана кость (иначе двигалось бы всё тело).
bool App::boneGizmo() const {
    if (camView_ || selBone_ < 0) return false;
    if (!(showBones_ || tab_ == Tab::Bones)) return false;
    const Object* o = scene.find(scene.selected);
    if (!o || o->type != ObjType::Model || !o->anim || !o->visibleNow) return false;
    Vec3 p;
    return scene.boneWorldPos(*o, selBone_, p);
}
// Режим гизмо с учётом кости: у кости нет масштаба, поэтому «масштаб» там работает как поворот.
int App::gizmoEff() const { return boneGizmo() ? (gizmoMode_ == 0 ? 0 : 1) : gizmoMode_; }

void App::gizmoGeometry(Vec3& origin, float& L, Vec3 axes[3]) const {
    const Object* o = scene.find(scene.selected);
    const bool bone = boneGizmo();
    if (bone) scene.boneWorldPos(*o, selBone_, origin);
    else origin = transformPoint(o->world, {0, 0, 0});
    if (!bone && gizmoMode_ == 2) {
        axes[0] = normalize(Vec3(o->world.m[0], o->world.m[1], o->world.m[2]));
        axes[1] = normalize(Vec3(o->world.m[4], o->world.m[5], o->world.m[6]));
        axes[2] = normalize(Vec3(o->world.m[8], o->world.m[9], o->world.m[10]));
    } else { axes[0] = {1, 0, 0}; axes[1] = {0, 1, 0}; axes[2] = {0, 0, 1}; }
    Vec3 f = normalize(-Vec3(view_.view.m[2], view_.view.m[6], view_.view.m[10]));
    float depth = std::max(0.2f, dot(origin - view_.eye, f));
    float pxPerUnit = viewRect_.h / (2.f * depth * std::tan(fov_ * kPi / 360.f));
    if (camView_) {
        const Object* c = scene.find(scene.activeCamera);
        if (c) pxPerUnit = viewRect_.h / (2.f * depth * std::tan(c->fovDeg * kPi / 360.f));
    }
    L = ui.dp(88) / std::max(1e-3f, pxPerUnit);
}

int App::gizmoHit(float x, float y) {
    const Object* o = scene.find(scene.selected);
    if (!o || !o->visibleNow) return -1;
    const bool bone = boneGizmo();
    if (tab_ == Tab::Bones && !bone) return -1;
    const int gm = gizmoEff();
    if (gm == 2 && o->type != ObjType::Model && o->type != ObjType::Effect) return -1;
    Vec3 O, ax[3];
    float L;
    gizmoGeometry(O, L, ax);
    float ox, oy;
    if (!project(O, ox, oy)) return -1;
    float best = ui.dp(24);
    int hitId = -1;
    if (std::hypot(ox - x, oy - y) < ui.dp(20) && gm != 1) { hitId = 3; best = std::hypot(ox - x, oy - y) - ui.dp(2); }
    for (int i = 0; i < 3; i++) {
        if (gm == 1) {
            Vec3 u = ax[(i + 1) % 3], v = ax[(i + 2) % 3];
            float px0 = 0, py0 = 0;
            bool have = false;
            for (int k = 0; k <= 48; k++) {
                float a = 6.2831853f * k / 48.f;
                float sx, sy;
                if (!project(O + (u * std::cos(a) + v * std::sin(a)) * (L * 0.85f), sx, sy)) { have = false; continue; }
                if (have) { float d = distPointSeg(x, y, px0, py0, sx, sy); if (d < best) { best = d; hitId = i; } }
                px0 = sx; py0 = sy; have = true;
            }
        } else {
            float sx, sy;
            if (!project(O + ax[i] * L, sx, sy)) continue;
            float d = distPointSeg(x, y, ox, oy, sx, sy);
            if (d < best) { best = d; hitId = i; }
        }
    }
    if (hitId < 0) return -1;
    // начало перетаскивания
    gW0_ = o->world;
    gO0_ = O;
    if (bone) {
        gBoneT0_ = Vec3(); gBoneD0_ = Quat();
        if (!scene.boneDeltaAt(*o, selBone_, (float)curFrame(), gBoneT0_, gBoneD0_)) { gBoneT0_ = Vec3(); gBoneD0_ = Quat(); }
    }
    gStartX_ = x; gStartY_ = y;
    Vec3 ro, rd;
    rayFromScreen(x, y, ro, rd);
    if (hitId < 3) gAxis_ = ax[hitId];
    if (gm == 1) {
        Vec3 n = gAxis_;
        float den = dot(n, rd);
        Vec3 h = std::fabs(den) > 0.08f ? ro + rd * (dot(O - ro, n) / den) : O;
        Vec3 u = std::fabs(n.y) < 0.9f ? normalize(cross({0, 1, 0}, n)) : Vec3(1, 0, 0);
        Vec3 v = cross(n, u);
        gAng0_ = std::atan2(dot(h - O, v), dot(h - O, u));
        gAngPrev_ = gAng0_;
        gAngAcc_ = 0;
    } else if (hitId < 3) {
        Vec3 a = gAxis_, w0 = O - ro;
        float b = dot(a, rd), d = dot(a, w0), e = dot(rd, w0), den = 1.f - b * b;
        gT0_ = std::fabs(den) > 1e-4f ? (b * e - d) / den : 0.f;
    } else {
        Vec3 f = normalize(-Vec3(view_.view.m[2], view_.view.m[6], view_.view.m[10]));
        Vec3 n = std::fabs(f.y) > 0.3f ? Vec3(0, 1, 0) : normalize(Vec3(f.x, 0, f.z));
        gAxis_ = n;
        float den = dot(n, rd);
        gHit0_ = std::fabs(den) > 1e-3f ? ro + rd * (dot(O - ro, n) / den) : O;
        gT0_ = y;
    }
    return hitId;
}

void App::gizmoDrag(float x, float y) {
    Object* o = scene.find(scene.selected);
    if (!o || gizmoActive_ < 0) return;
    beginEdit();
    Vec3 ro, rd;
    rayFromScreen(x, y, ro, rd);
    Vec3 O = gO0_;
    Mat4 W;
    const bool bone = boneGizmo();
    const int gm = gizmoEff();
    Vec3 boneMove;
    Quat boneRot;
    if (gm == 0) {
        Vec3 delta;
        if (gizmoActive_ < 3) {
            Vec3 a = gAxis_, w0 = O - ro;
            float b = dot(a, rd), d = dot(a, w0), e = dot(rd, w0), den = 1.f - b * b;
            if (std::fabs(den) < 1e-4f) return;
            float t = (b * e - d) / den - gT0_;
            if (snap_) t = std::round(t / 0.25f) * 0.25f;
            delta = a * t;
        } else {
            Vec3 n = gAxis_;
            float den = dot(n, rd);
            if (std::fabs(den) < 1e-3f) return;
            Vec3 h = ro + rd * (dot(O - ro, n) / den);
            delta = h - gHit0_;
            if (snap_) delta = {std::round(delta.x / 0.25f) * 0.25f, std::round(delta.y / 0.25f) * 0.25f, std::round(delta.z / 0.25f) * 0.25f};
            if (length(delta) > 200.f) return;
        }
        W = translation(delta) * gW0_;
        boneMove = delta;
    } else if (gm == 1) {
        Vec3 n = gAxis_;
        float den = dot(n, rd), ang;
        if (std::fabs(den) > 0.08f) {
            Vec3 h = ro + rd * (dot(O - ro, n) / den);
            Vec3 u = std::fabs(n.y) < 0.9f ? normalize(cross({0, 1, 0}, n)) : Vec3(1, 0, 0);
            Vec3 v = cross(n, u);
            float a = std::atan2(dot(h - O, v), dot(h - O, u));
            float d = a - gAngPrev_;
            while (d > kPi) d -= 2 * kPi;
            while (d < -kPi) d += 2 * kPi;
            gAngAcc_ += d;
            gAngPrev_ = a;
            ang = gAngAcc_;
        } else ang = (x - gStartX_) * 0.01f;
        if (snap_) ang = std::round(ang / (kPi / 12.f)) * (kPi / 12.f);
        W = translation(O) * axisAngle(n, ang) * translation(-O) * gW0_;
        boneRot = quatAxisAngle(n, ang);
    } else {
        float f;
        Mat4 S;
        if (gizmoActive_ < 3) {
            Vec3 a = gAxis_, w0 = O - ro;
            float b = dot(a, rd), d = dot(a, w0), e = dot(rd, w0), den = 1.f - b * b;
            if (std::fabs(den) < 1e-4f) return;
            float t = (b * e - d) / den;
            float Lw = 1.f;
            Vec3 oo, aa[3];
            gizmoGeometry(oo, Lw, aa);
            f = 1.f + (t - gT0_) / std::max(1e-3f, Lw);
        } else {
            f = std::exp((gT0_ - y) * 0.006f);
        }
        f = clampf(f, 0.02f, 50.f);
        if (snap_) f = std::max(0.1f, std::round(f * 10.f) / 10.f);
        float sx = 1, sy = 1, sz = 1;
        if (gizmoActive_ == 0) sx = f; else if (gizmoActive_ == 1) sy = f; else if (gizmoActive_ == 2) sz = f; else sx = sy = sz = f;
        S.m[0] = sx; S.m[5] = sy; S.m[10] = sz;
        W = gW0_ * S;
    }
    if (bone) {
        Vec3 t;
        Quat d;
        if (scene.boneFromWorld(*o, selBone_, boneRot, boneMove, gBoneT0_, gBoneD0_, t, d)) writeBonePose(scene, *o, selBone_, curFrame(), t, d);
        return;
    }
    scene.setWorld(o->id, W);
    scene.resetEffects();
}

void App::drawGizmo() {
    const Object* o = scene.find(scene.selected);
    if (!o || !o->visibleNow || gest_ == Gest::Orbit || gest_ == Gest::Pinch) return;
    const bool bone = boneGizmo();
    if (tab_ == Tab::Bones && !bone) return;
    const int gm = gizmoEff();
    if (gm == 2 && o->type != ObjType::Model && o->type != ObjType::Effect) return;
    Vec3 O, ax[3];
    float L;
    gizmoGeometry(O, L, ax);
    LineBatch& lb = renderer.lines();
    const float cols[3][4] = {{0.95f, 0.3f, 0.3f, 1.f}, {0.4f, 0.9f, 0.35f, 1.f}, {0.35f, 0.55f, 1.f, 1.f}};
    const float act[4] = {1.f, 0.9f, 0.2f, 1.f}, white[4] = {1, 1, 1, 0.95f};
    float w = ui.dp(3.2f);
    Vec3 f = normalize(-Vec3(view_.view.m[2], view_.view.m[6], view_.view.m[10]));
    for (int i = 0; i < 3; i++) {
        const float* c = gizmoActive_ == i ? act : cols[i];
        if (gm == 1) {
            Vec3 u = ax[(i + 1) % 3], v = ax[(i + 2) % 3];
            for (int k = 0; k < 48; k++) {
                float a0 = 6.2831853f * k / 48.f, a1 = 6.2831853f * (k + 1) / 48.f;
                lb.add(O + (u * std::cos(a0) + v * std::sin(a0)) * (L * 0.85f), O + (u * std::cos(a1) + v * std::sin(a1)) * (L * 0.85f), c, w);
            }
        } else {
            Vec3 tip = O + ax[i] * L;
            lb.add(O, tip, c, w);
            if (gm == 0) {
                Vec3 s = normalize(cross(ax[i], f));
                Vec3 s2 = cross(ax[i], s);
                float h = L * 0.14f, r = L * 0.055f;
                Vec3 base = tip - ax[i] * h;
                lb.add(tip, base + s * r, c, w); lb.add(tip, base - s * r, c, w);
                lb.add(tip, base + s2 * r, c, w); lb.add(tip, base - s2 * r, c, w);
                lb.add(base + s * r, base + s2 * r, c, w * 0.7f); lb.add(base + s2 * r, base - s * r, c, w * 0.7f);
                lb.add(base - s * r, base - s2 * r, c, w * 0.7f); lb.add(base - s2 * r, base + s * r, c, w * 0.7f);
            } else {
                Vec3 s = normalize(cross(ax[i], f)), s2 = cross(ax[i], s);
                float r = L * 0.06f;
                Vec3 p0 = tip - ax[i] * r, p1 = tip + ax[i] * r;
                lb.add(p0 + s * r, p1 + s * r, c, w); lb.add(p0 - s * r, p1 - s * r, c, w);
                lb.add(p0 + s * r, p0 - s * r, c, w); lb.add(p1 + s * r, p1 - s * r, c, w);
                lb.add(p0 + s2 * r, p1 + s2 * r, c, w * 0.6f); lb.add(p0 - s2 * r, p1 - s2 * r, c, w * 0.6f);
            }
        }
    }
    if (gm != 1) {
        const float* c = gizmoActive_ == 3 ? act : white;
        Vec3 r1 = Vec3(view_.view.m[0], view_.view.m[4], view_.view.m[8]) * (L * 0.1f), u1 = Vec3(view_.view.m[1], view_.view.m[5], view_.view.m[9]) * (L * 0.1f);
        lb.add(O - r1 - u1, O + r1 - u1, c, w); lb.add(O + r1 - u1, O + r1 + u1, c, w);
        lb.add(O + r1 + u1, O - r1 + u1, c, w); lb.add(O - r1 + u1, O - r1 - u1, c, w);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo_);
    glViewport((GLint)viewRect_.x, (GLint)(H_ - viewRect_.b()), (GLsizei)viewRect_.w, (GLsizei)viewRect_.h);
    glEnable(GL_SCISSOR_TEST);
    glScissor((GLint)viewRect_.x, (GLint)(H_ - viewRect_.b()), (GLsizei)viewRect_.w, (GLsizei)viewRect_.h);
    lb.flush(view_.proj * view_.view, (int)viewRect_.w, (int)viewRect_.h, 1.f, false);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, W_, H_);
}

void App::viewportGestures() {
    const Ptr* second = nullptr;
    for (auto& p : ptrs_) if (p.id != primaryId_) { second = &p; break; }
    if (ui.pressedNow() && ui.candidate() == vpId_ && rVP_.has(ui.downX(), ui.downY())) {
        // Гизмо кости/объекта имеет приоритет. Если гизмо не задето, а палец у выбранной кости в режиме поворота —
        // вращаем кость «трекболом».
        oVelX_ = oVelY_ = 0.f;  // новое касание гасит инерцию вида
        int h = gizmoHit(ui.downX(), ui.downY());
        bool boneGrab = false;
        if (h < 0 && gizmoEff() == 1 && boneGizmo()) {
            const Object* so = scene.find(scene.selected);
            float bx, by;
            if (boneScreenPos(*so, selBone_, bx, by) && std::hypot(bx - ui.downX(), by - ui.downY()) < ui.dp(34)) boneGrab = true;
        }
        if (boneGrab) { gest_ = Gest::Bone; ui.claimPointer(); }
        else if (h >= 0) { gest_ = Gest::Gizmo; gizmoActive_ = h; ui.claimPointer(); }
        else gest_ = Gest::Armed;
    }
    if (gest_ == Gest::Bone) {
        if (ui.pointerDown() && ui.moved()) { ui.claimPointer(); boneDrag(ui.dragDx(), ui.dragDy()); }
    } else if (gest_ == Gest::Gizmo) {
        if (ui.pointerDown()) gizmoDrag(ui.px(), ui.py());
    } else if (gest_ == Gest::Armed || gest_ == Gest::Orbit) {
        if (second && ui.pointerDown()) {
            gest_ = Gest::Pinch;
            pinchPrevD_ = std::hypot(ui.px() - second->x, ui.py() - second->y);
            pinchMx_ = (ui.px() + second->x) * 0.5f; pinchMy_ = (ui.py() + second->y) * 0.5f;
            ui.claimPointer();
        } else if (ui.pointerDown() && ui.moved()) {
            if (gest_ == Gest::Armed) gest_ = Gest::Orbit;
            ui.claimPointer();
            float dx = ui.dragDx(), dy = ui.dragDy();
            if (camView_) cameraGesture(dx * 0.004f, dy * 0.004f, 0, 0, 0);
            else {
                orbitGesture(dx, dy, 0, 0, 0);
                float inv = 1.f / std::max(0.004f, dt_);
                oVelX_ = oVelX_ * 0.55f + dx * inv * 0.45f;
                oVelY_ = oVelY_ * 0.55f + dy * inv * 0.45f;
            }
            if (camView_) beginEdit();
        }
    } else if (gest_ == Gest::Pinch) {
        if (second && ui.pointerDown()) {
            float d = std::hypot(ui.px() - second->x, ui.py() - second->y);
            float mx = (ui.px() + second->x) * 0.5f, my = (ui.py() + second->y) * 0.5f;
            if (pinchPrevD_ > 10.f && d > 10.f) {
                if (camView_) {
                    float k = (pinchPrevD_ - d) * 0.01f;
                    cameraGesture(0, 0, -k * 0.5f, -(mx - pinchMx_) * 0.01f, (my - pinchMy_) * 0.01f);
                    beginEdit();
                } else orbitGesture(0, 0, pinchPrevD_ / d, mx - pinchMx_, my - pinchMy_);
            }
            pinchPrevD_ = d; pinchMx_ = mx; pinchMy_ = my;
        }
    }
    if (ui.releasedNow()) {
        if (gest_ == Gest::Orbit && (!settings.inertia || std::hypot(oVelX_, oVelY_) < ui.dp(90))) oVelX_ = oVelY_ = 0.f;
        if (gest_ == Gest::Armed && !ui.moved()) {
            pickAt(ui.px(), ui.py());
            if (settings.doubleTapFocus && !camView_ && now_ - lastTapT_ < 0.32 && std::hypot(ui.px() - lastTapX_, ui.py() - lastTapY_) < ui.dp(36)) {
                if (scene.selected >= 0) focusSelected();
                lastTapT_ = -10;
            } else { lastTapT_ = now_; lastTapX_ = ui.px(); lastTapY_ = ui.py(); }
        }
        if (gest_ == Gest::Gizmo || gest_ == Gest::Bone) { scene.simulateTo((int)std::lround(frameF_)); }
        gest_ = Gest::None;
        gizmoActive_ = -1;
        editStarted_ = false;
    }
    if (!ui.pointerDown() && !ui.releasedNow()) { gest_ = Gest::None; gizmoActive_ = -1; }
    if (!ui.blocked) drawGizmo();
}

// ============================================================ экспорт
static std::string stamp() {
    std::time_t t = std::time(nullptr);
    char b[32];
    std::strftime(b, sizeof b, "%Y%m%d_%H%M%S", std::localtime(&t));
    return b;
}

static std::string safeName(std::string s) {
    for (char& ch : s) if (!(std::isalnum((unsigned char)ch) || ch == '_' || ch == '-' || (unsigned char)ch >= 0x80)) ch = '_';
    return s.empty() ? "scene" : s;
}

void App::skeletalExportMenu() {
    const Object* o = scene.find(scene.selected);
    if (!o || o->type != ObjType::Model || scene.boneCount(*o) < 2) { toast("Выберите модель со скелетом"); return; }
    pickList("Экспорт анимации скелета (GLB)",
             {"Модель + скелет + анимация", "То же, но на месте (без движения по сцене)", "Только скелет и анимация (без сетки)"},
             [this](int i) { exportSkeletalAnimation(i); }, {Icon::Rig, Icon::Rig, Icon::Bone});
}

// Запекает позы всех костей по кадрам (клип, ключи костей, рэгдолл) и сохраняет GLB для Godot 4, Unity, Unreal, Blender.
void App::exportSkeletalAnimation(int mode) {
    const Object* o = scene.find(scene.selected);
    if (!o || o->type != ObjType::Model || scene.boneCount(*o) < 2) { toast("Выберите модель со скелетом"); return; }
    if (!plat.saveFile) { toast("Сохранение файлов недоступно на этом устройстве"); return; }
    GlbOptions op;
    op.f0 = scene.proj.frameStart; op.f1 = scene.proj.frameEnd; op.fps = scene.proj.fps;
    op.mesh = mode != 2; op.rootMotion = mode == 0;
    op.animName = safeName(o->name);
    std::string nm = safeName(o->name);
    std::vector<uint8_t> glb;
    std::string err;
    int id = o->id;
    if (!exportSkeletalGlb(scene, id, op, glb, err)) { toast("Не удалось экспортировать: " + err); return; }
    scene.resetEffects();
    scene.evaluate((float)frameF_);
    scene.simulateTo(curFrame());
    std::string file = nm + "_" + stamp() + ".glb";
    if (plat.saveFile(file, glb, "model/gltf-binary")) toast("Сохранено: Загрузки/PrismLab/" + file + " (" + std::to_string(glb.size() / 1024) + " КБ)");
    else toast("Не удалось сохранить файл");
}

void App::startStillExport() {
    if (exp_.kind != Export::None) return;
    if (!scene.find(scene.activeCamera)) { toast("Нет камеры: добавьте камеру"); return; }
    exp_ = Export();
    exp_.kind = Export::Still;
    exp_.savedFrame = (int)std::lround(frameF_);
    exp_.name = "PrismLab_" + stamp() + ".png";
    playing_ = false;
    modal_ = Modal();
    modal_.kind = Modal::Progress;
    modal_.title = "Рендер кадра…";
    modal_.msg = std::to_string(scene.render.exportW) + "×" + std::to_string(scene.render.exportH) + ", проходов: " + std::to_string(scene.render.stillSamples);
}

void App::startVideoExport() {
    if (exp_.kind != Export::None) return;
    if (!scene.find(scene.activeCamera)) { toast("Нет камеры: добавьте камеру"); return; }
    if (!plat.videoBegin) { toast("Экспорт видео недоступен на этом устройстве"); return; }
    exp_ = Export();
    exp_.kind = Export::Video;
    exp_.savedFrame = (int)std::lround(frameF_);
    exp_.first = scene.proj.frameStart;
    exp_.last = scene.proj.frameEnd;
    exp_.frame = exp_.first;
    exp_.name = "PrismLab_" + stamp() + ".mp4";
    playing_ = false;
    modal_ = Modal();
    modal_.kind = Modal::Progress;
    modal_.title = "Рендер видео…";
}

void App::finishExport(bool ok, const std::string& msg) {
    gotoFrame(exp_.savedFrame);
    scene.resetEffects();
    scene.simulateTo(exp_.savedFrame);
    exp_ = Export();
    modal_ = Modal();
    renderer.freeStillTargets();
    lastExportInfo = msg;
    toast(msg);
    (void)ok;
}

void App::stepExport() {
    if (exp_.kind == Export::None) return;
    RenderSettings& rs = scene.render;
    int w = std::max(16, rs.exportW & ~1), h = std::max(16, rs.exportH & ~1);
    w = std::min(w, Renderer::maxExportSide()); h = std::min(h, Renderer::maxExportSide());
    exp_.ticks++;
    if (exp_.ticks < 3) return;  // сначала нарисуем окно прогресса
    const Object* cam = scene.find(scene.activeCamera);
    if (!cam) { finishExport(false, "Нет камеры"); return; }
    std::vector<uint8_t> rgba;
    if (exp_.kind == Export::Still) {
        scene.evaluate((float)exp_.savedFrame);
        scene.simulateTo(exp_.savedFrame);
        cam = scene.find(scene.activeCamera);
        View v = Renderer::cameraView(*cam, (float)w / h);
        if (!renderer.renderStill(scene, v, w, h, rs.stillSamples, rgba)) { finishExport(false, "Не удалось отрисовать кадр"); return; }
        std::vector<uint8_t> png;
        if (!encodePNG(rgba.data(), w, h, png)) { finishExport(false, "Не удалось закодировать PNG"); return; }
        bool ok = plat.savePng ? plat.savePng(exp_.name, png) : false;
        finishExport(ok, ok ? "Кадр сохранён в галерею (Pictures/PrismLab)" : "Не удалось сохранить PNG");
        return;
    }
    // видео: по одному кадру за вызов
    if (!exp_.begun) {
        if (!plat.videoBegin(w, h, scene.proj.fps, rs.videoBitrateMbps, exp_.name)) { finishExport(false, "Кодер видео не запустился"); return; }
        exp_.begun = true;
        scene.resetEffects();
        scene.simulateTo(exp_.first);
    }
    if (exp_.frame > exp_.last) {
        bool ok = plat.videoEnd ? plat.videoEnd() : false;
        finishExport(ok, ok ? "Видео сохранено в галерею (Movies/PrismLab)" : "Не удалось завершить видео");
        return;
    }
    scene.simulateTo(exp_.frame);
    cam = scene.find(scene.activeCamera);
    View v = Renderer::cameraView(*cam, (float)w / h);
    if (!renderer.renderStill(scene, v, w, h, rs.videoSamples, rgba)) { if (plat.videoCancel) plat.videoCancel(); finishExport(false, "Не удалось отрисовать кадр"); return; }
    if (!plat.videoFrame(rgba.data(), w, h, exp_.index)) { if (plat.videoCancel) plat.videoCancel(); finishExport(false, "Ошибка кодера видео"); return; }
    exp_.index++;
    exp_.frame++;
}

}  // namespace eng
