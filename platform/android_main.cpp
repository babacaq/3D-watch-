// Android: NativeActivity + EGL + OpenGL ES 3.0. Файлы модели приходят из Java (MainActivity) через очередь takePending().
#include <android/asset_manager.h>
#include <android/input.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <dirent.h>
#include <jni.h>
#include <sys/stat.h>
#include <time.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine.h"
#include "log.h"

using namespace eng;

namespace {

struct App {
    android_app* app = nullptr;
    Engine engine;
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLSurface surf = EGL_NO_SURFACE;
    EGLContext ctx = EGL_NO_CONTEXT;
    EGLConfig cfg = nullptr;
    bool glOk = false, focused = false, started = false;
    int w = 0, h = 0;
    JNIEnv* env = nullptr;
    jmethodID midOpen = nullptr, midTake = nullptr;
    std::vector<std::string> initialLoaded;
};

bool readAll(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return false; }
    out.resize((size_t)n);
    size_t r = n ? fread(out.data(), 1, (size_t)n, f) : 0;
    fclose(f);
    return r == (size_t)n;
}

std::string dirOf(const std::string& p) { size_t i = p.find_last_of('/'); return i == std::string::npos ? "" : p.substr(0, i + 1); }
std::string baseOf(const std::string& p) { size_t i = p.find_last_of("/\\"); return i == std::string::npos ? p : p.substr(i + 1); }

std::string urlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else o += s[i];
    }
    return o;
}

void loadFromDisk(App& a, const std::string& path) {
    std::vector<uint8_t> data;
    if (!readAll(path, data)) { a.engine.setStatus("CANNOT READ " + baseOf(path)); return; }
    std::string dir = dirOf(path);
    FileReader rd = [dir](const std::string& rel, std::vector<uint8_t>& out) {
        std::string r = rel;
        for (char& c : r) if (c == '\\') c = '/';
        if (readAll(dir + r, out)) return true;
        std::string d = urlDecode(r);
        if (readAll(dir + d, out)) return true;
        return readAll(dir + baseOf(d), out);  // плоская копия из выбора файлов
    };
    std::string err;
    a.engine.addModelFile(baseOf(path), data.data(), data.size(), rd, &err);
}

void loadFromAssets(App& a, const std::string& fileName) {
    AAssetManager* am = a.app->activity->assetManager;
    auto readAsset = [am](const std::string& name, std::vector<uint8_t>& out) {
        AAsset* as = AAssetManager_open(am, name.c_str(), AASSET_MODE_BUFFER);
        if (!as) return false;
        off_t n = AAsset_getLength(as);
        out.resize((size_t)n);
        int r = n ? AAsset_read(as, out.data(), (size_t)n) : 0;
        AAsset_close(as);
        return r == (int)n;
    };
    std::vector<uint8_t> data;
    if (!readAsset("models/" + fileName, data)) return;
    FileReader rd = [readAsset](const std::string& rel, std::vector<uint8_t>& out) {
        std::string r = rel;
        for (char& c : r) if (c == '\\') c = '/';
        return readAsset("models/" + r, out) || readAsset("models/" + baseOf(r), out);
    };
    std::string err;
    a.engine.addModelFile(fileName, data.data(), data.size(), rd, &err);
}

void listDir(const std::string& dir, std::vector<std::string>& files, int depth) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        std::string full = dir + "/" + n;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { if (depth > 0) listDir(full, files, depth - 1); }
        else if (isSupportedModelFile(n)) files.push_back(full);
    }
    closedir(d);
}

// --- JNI ---
bool attachJni(App& a) {
    JavaVM* vm = a.app->activity->vm;
    if (vm->AttachCurrentThread(&a.env, nullptr) != JNI_OK) return false;
    jclass cls = a.env->GetObjectClass(a.app->activity->clazz);
    a.midOpen = a.env->GetMethodID(cls, "openPicker", "()V");
    a.midTake = a.env->GetMethodID(cls, "takePending", "()Ljava/lang/String;");
    a.env->DeleteLocalRef(cls);
    if (a.env->ExceptionCheck()) { a.env->ExceptionClear(); a.midOpen = a.midTake = nullptr; }
    return a.midOpen && a.midTake;
}

void requestPicker(App& a) {
    if (!a.env || !a.midOpen) { a.engine.setStatus("OPEN IS NOT AVAILABLE"); return; }
    a.env->CallVoidMethod(a.app->activity->clazz, a.midOpen);
    if (a.env->ExceptionCheck()) { a.env->ExceptionClear(); a.engine.setStatus("PICKER FAILED"); }
}

void pollPending(App& a) {
    if (!a.env || !a.midTake) return;
    jstring js = (jstring)a.env->CallObjectMethod(a.app->activity->clazz, a.midTake);
    if (a.env->ExceptionCheck()) { a.env->ExceptionClear(); return; }
    if (!js) return;
    const char* cs = a.env->GetStringUTFChars(js, nullptr);
    std::string all = cs ? cs : "";
    if (cs) a.env->ReleaseStringUTFChars(js, cs);
    a.env->DeleteLocalRef(js);
    size_t pos = 0;
    while (pos < all.size()) {
        size_t nl = all.find('\n', pos);
        if (nl == std::string::npos) nl = all.size();
        std::string p = all.substr(pos, nl - pos);
        pos = nl + 1;
        if (!p.empty() && isSupportedModelFile(p)) loadFromDisk(a, p);
    }
}

// --- EGL ---
bool initEgl(App& a) {
    a.dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (a.dpy == EGL_NO_DISPLAY || !eglInitialize(a.dpy, nullptr, nullptr)) return false;
    const EGLint ca[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8,
                         EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE};
    EGLint n = 0;
    if (!eglChooseConfig(a.dpy, ca, &a.cfg, 1, &n) || n < 1) {
        const EGLint cb[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8,
                             EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 16, EGL_NONE};
        if (!eglChooseConfig(a.dpy, cb, &a.cfg, 1, &n) || n < 1) return false;
    }
    const EGLint cx[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    a.ctx = eglCreateContext(a.dpy, a.cfg, EGL_NO_CONTEXT, cx);
    return a.ctx != EGL_NO_CONTEXT;
}

bool createSurface(App& a) {
    a.surf = eglCreateWindowSurface(a.dpy, a.cfg, a.app->window, nullptr);
    if (a.surf == EGL_NO_SURFACE) return false;
    if (!eglMakeCurrent(a.dpy, a.surf, a.surf, a.ctx)) return false;
    eglQuerySurface(a.dpy, a.surf, EGL_WIDTH, &a.w);
    eglQuerySurface(a.dpy, a.surf, EGL_HEIGHT, &a.h);
    eglSwapInterval(a.dpy, 1);
    return true;
}

void destroySurface(App& a) {
    if (a.dpy == EGL_NO_DISPLAY) return;
    eglMakeCurrent(a.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (a.surf != EGL_NO_SURFACE) eglDestroySurface(a.dpy, a.surf);
    a.surf = EGL_NO_SURFACE;
}

void shutdownEgl(App& a) {
    destroySurface(a);
    if (a.ctx != EGL_NO_CONTEXT) eglDestroyContext(a.dpy, a.ctx);
    if (a.dpy != EGL_NO_DISPLAY) eglTerminate(a.dpy);
    a.ctx = EGL_NO_CONTEXT; a.dpy = EGL_NO_DISPLAY;
}

void onWindowReady(App& a) {
    if (!a.app->window) return;
    if (a.dpy == EGL_NO_DISPLAY && !initEgl(a)) { ENG_LOG("EGL init failed"); return; }
    if (!createSurface(a)) { ENG_LOG("surface failed"); return; }
    if (!a.engine.initGL(a.w, a.h)) { ENG_LOG("engine initGL failed"); a.engine.setStatus("OPENGL ES 3 INIT FAILED"); return; }
    a.glOk = true;
    if (!a.started) {
        a.started = true;
        // модели из assets/models и из внутренней папки приложения
        AAssetManager* am = a.app->activity->assetManager;
        if (AAssetDir* d = AAssetManager_openDir(am, "models")) {
            std::vector<std::string> names;
            while (const char* n = AAssetDir_getNextFileName(d)) if (isSupportedModelFile(n)) names.push_back(n);
            AAssetDir_close(d);
            for (const auto& n : names) loadFromAssets(a, n);
        }
        const char* ext = a.app->activity->externalDataPath;
        if (ext) {
            std::vector<std::string> files;
            listDir(std::string(ext) + "/models", files, 2);
            if (files.size() > 6) files.erase(files.begin(), files.end() - 6);
            for (const auto& f : files) loadFromDisk(a, f);
        }
    }
}

void onWindowLost(App& a) {
    if (a.glOk) { a.engine.releaseGL(); a.glOk = false; }
    destroySurface(a);
}

void handleCmd(android_app* app, int32_t cmd) {
    App& a = *(App*)app->userData;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW: onWindowReady(a); break;
        case APP_CMD_TERM_WINDOW: onWindowLost(a); break;
        case APP_CMD_GAINED_FOCUS: a.focused = true; break;
        case APP_CMD_LOST_FOCUS: a.focused = false; break;
        case APP_CMD_DESTROY: break;
        default: break;
    }
}

int32_t handleInput(android_app* app, AInputEvent* ev) {
    App& a = *(App*)app->userData;
    if (AInputEvent_getType(ev) != AINPUT_EVENT_TYPE_MOTION || !a.glOk) return 0;
    int32_t act = AMotionEvent_getAction(ev);
    int32_t kind = act & AMOTION_EVENT_ACTION_MASK;
    int idx = (act & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    auto send = [&](int action, int i) {
        a.engine.touch(action, AMotionEvent_getPointerId(ev, i), AMotionEvent_getX(ev, i), AMotionEvent_getY(ev, i));
    };
    switch (kind) {
        case AMOTION_EVENT_ACTION_DOWN: send(0, 0); break;
        case AMOTION_EVENT_ACTION_POINTER_DOWN: send(0, idx); break;
        case AMOTION_EVENT_ACTION_MOVE: for (size_t i = 0; i < AMotionEvent_getPointerCount(ev); i++) send(1, (int)i); break;
        case AMOTION_EVENT_ACTION_UP: send(2, 0); break;
        case AMOTION_EVENT_ACTION_POINTER_UP: send(2, idx); break;
        case AMOTION_EVENT_ACTION_CANCEL: for (size_t i = 0; i < AMotionEvent_getPointerCount(ev); i++) send(3, (int)i); break;
        default: return 0;
    }
    return 1;
}

double nowSec() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

}  // namespace

void android_main(android_app* app) {
    App a;
    a.app = app;
    app->userData = &a;
    app->onAppCmd = handleCmd;
    app->onInputEvent = handleInput;
    bool jni = attachJni(a);
    a.engine.onOpenRequest = [&a]() { requestPicker(a); };
    if (!jni) ENG_LOG("JNI bridge unavailable: OPEN disabled");

    double last = nowSec();
    int frame = 0;
    while (!app->destroyRequested) {
        int events;
        android_poll_source* src;
        while (ALooper_pollOnce(a.glOk && a.focused ? 0 : 100, nullptr, &events, (void**)&src) >= 0) {
            if (src) src->process(app, src);
            if (app->destroyRequested) break;
        }
        if (app->destroyRequested) break;
        if (!a.glOk || a.surf == EGL_NO_SURFACE) continue;
        if ((frame++ & 15) == 0 && a.glOk) pollPending(a);
        EGLint w = 0, h = 0;
        eglQuerySurface(a.dpy, a.surf, EGL_WIDTH, &w);
        eglQuerySurface(a.dpy, a.surf, EGL_HEIGHT, &h);
        if (w > 0 && h > 0 && (w != a.w || h != a.h)) { a.w = w; a.h = h; a.engine.resize(w, h); }
        double t = nowSec();
        float dt = (float)(t - last);
        last = t;
        a.engine.update(dt);
        a.engine.render();
        if (!eglSwapBuffers(a.dpy, a.surf)) {
            EGLint err = eglGetError();
            if (err == EGL_BAD_SURFACE || err == EGL_BAD_NATIVE_WINDOW) { ENG_LOG("surface lost"); }
            else if (err == EGL_CONTEXT_LOST) {
                ENG_LOG("context lost: recreating");
                a.engine.releaseGL(); a.glOk = false;
                shutdownEgl(a);
                onWindowReady(a);
            }
        }
    }
    if (a.glOk) a.engine.releaseGL();
    shutdownEgl(a);
    if (a.env) app->activity->vm->DetachCurrentThread();
}
