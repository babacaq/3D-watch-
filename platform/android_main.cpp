// Android: NativeActivity + EGL + OpenGL ES 3.0. Java (MainActivity) даёт выбор файлов, сохранение в галерею и кодер MP4.
#include <android/input.h>
#include <android/keycodes.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <sys/stat.h>
#include <time.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "app.h"
#include "importer.h"
#include "log.h"

using namespace eng;

namespace {

struct Host {
    android_app* app = nullptr;
    App papp;
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLSurface surf = EGL_NO_SURFACE;
    EGLContext ctx = EGL_NO_CONTEXT;
    EGLConfig cfg = nullptr;
    bool glOk = false, focused = false, started = false;
    int w = 0, h = 0;
    float density = 2.7f;
    JNIEnv* env = nullptr;
    jmethodID midOpen = nullptr, midTake = nullptr, midSavePng = nullptr, midSaveFile = nullptr, midToast = nullptr;
    jmethodID midVBegin = nullptr, midVFrame = nullptr, midVEnd = nullptr, midVCancel = nullptr;
};

std::string jstr(JNIEnv* env, jstring js) {
    if (!js) return "";
    const char* cs = env->GetStringUTFChars(js, nullptr);
    std::string s = cs ? cs : "";
    if (cs) env->ReleaseStringUTFChars(js, cs);
    return s;
}

bool clearExc(JNIEnv* env) {
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); return true; }
    return false;
}

bool attachJni(Host& a) {
    JavaVM* vm = a.app->activity->vm;
    if (vm->AttachCurrentThread(&a.env, nullptr) != JNI_OK) return false;
    jclass cls = a.env->GetObjectClass(a.app->activity->clazz);
    a.midOpen = a.env->GetMethodID(cls, "openPicker", "()V");
    a.midTake = a.env->GetMethodID(cls, "takePending", "()Ljava/lang/String;");
    a.midSavePng = a.env->GetMethodID(cls, "savePng", "(Ljava/lang/String;[B)Z");
    a.midSaveFile = a.env->GetMethodID(cls, "saveFile", "(Ljava/lang/String;[BLjava/lang/String;)Z");
    a.midToast = a.env->GetMethodID(cls, "toast", "(Ljava/lang/String;)V");
    a.midVBegin = a.env->GetMethodID(cls, "videoBegin", "(IIIILjava/lang/String;)Z");
    a.midVFrame = a.env->GetMethodID(cls, "videoFrame", "(Ljava/nio/ByteBuffer;III)Z");
    a.midVEnd = a.env->GetMethodID(cls, "videoEnd", "()Z");
    a.midVCancel = a.env->GetMethodID(cls, "videoCancel", "()V");
    a.env->DeleteLocalRef(cls);
    if (clearExc(a.env)) return false;
    return true;
}

void setupPlatform(Host& a) {
    Platform& p = a.papp.plat;
    const char* ext = a.app->activity->externalDataPath;
    const char* in = a.app->activity->internalDataPath;
    p.dataDir = ext ? ext : (in ? in : "/data/local/tmp");
    mkdir(p.dataDir.c_str(), 0755);
    Host* h = &a;
    p.importModels = [h]() {
        if (!h->env || !h->midOpen) return;
        h->env->CallVoidMethod(h->app->activity->clazz, h->midOpen);
        clearExc(h->env);
    };
    p.savePng = [h](const std::string& name, const std::vector<uint8_t>& png) {
        if (!h->env || !h->midSavePng) return false;
        jstring jn = h->env->NewStringUTF(name.c_str());
        jbyteArray arr = h->env->NewByteArray((jsize)png.size());
        if (!arr) { h->env->DeleteLocalRef(jn); clearExc(h->env); return false; }
        h->env->SetByteArrayRegion(arr, 0, (jsize)png.size(), (const jbyte*)png.data());
        jboolean ok = h->env->CallBooleanMethod(h->app->activity->clazz, h->midSavePng, jn, arr);
        h->env->DeleteLocalRef(jn);
        h->env->DeleteLocalRef(arr);
        return !clearExc(h->env) && ok;
    };
    p.saveFile = [h](const std::string& name, const std::vector<uint8_t>& data, const std::string& mime) {
        if (!h->env || !h->midSaveFile) return false;
        jstring jn = h->env->NewStringUTF(name.c_str());
        jstring jm = h->env->NewStringUTF(mime.c_str());
        jbyteArray arr = h->env->NewByteArray((jsize)data.size());
        if (!arr) { h->env->DeleteLocalRef(jn); h->env->DeleteLocalRef(jm); clearExc(h->env); return false; }
        h->env->SetByteArrayRegion(arr, 0, (jsize)data.size(), (const jbyte*)data.data());
        jboolean ok = h->env->CallBooleanMethod(h->app->activity->clazz, h->midSaveFile, jn, arr, jm);
        h->env->DeleteLocalRef(jn);
        h->env->DeleteLocalRef(jm);
        h->env->DeleteLocalRef(arr);
        return !clearExc(h->env) && ok;
    };
    p.videoBegin = [h](int w, int hh, int fps, int mbps, const std::string& name) {
        if (!h->env || !h->midVBegin) return false;
        jstring jn = h->env->NewStringUTF(name.c_str());
        jboolean ok = h->env->CallBooleanMethod(h->app->activity->clazz, h->midVBegin, w, hh, fps, mbps, jn);
        h->env->DeleteLocalRef(jn);
        return !clearExc(h->env) && ok;
    };
    p.videoFrame = [h](const uint8_t* rgba, int w, int hh, int index) {
        if (!h->env || !h->midVFrame) return false;
        jobject bb = h->env->NewDirectByteBuffer((void*)rgba, (jlong)w * hh * 4);
        jboolean ok = h->env->CallBooleanMethod(h->app->activity->clazz, h->midVFrame, bb, w, hh, index);
        h->env->DeleteLocalRef(bb);
        return !clearExc(h->env) && ok;
    };
    p.videoEnd = [h]() {
        if (!h->env || !h->midVEnd) return false;
        jboolean ok = h->env->CallBooleanMethod(h->app->activity->clazz, h->midVEnd);
        return !clearExc(h->env) && ok;
    };
    p.videoCancel = [h]() {
        if (!h->env || !h->midVCancel) return;
        h->env->CallVoidMethod(h->app->activity->clazz, h->midVCancel);
        clearExc(h->env);
    };
}

void pollPending(Host& a) {
    if (!a.env || !a.midTake) return;
    jstring js = (jstring)a.env->CallObjectMethod(a.app->activity->clazz, a.midTake);
    if (clearExc(a.env) || !js) return;
    std::string all = jstr(a.env, js);
    a.env->DeleteLocalRef(js);
    size_t pos = 0;
    int ok = 0, bad = 0;
    std::string lastErr;
    while (pos < all.size()) {
        size_t nl = all.find('\n', pos);
        if (nl == std::string::npos) nl = all.size();
        std::string p = all.substr(pos, nl - pos);
        pos = nl + 1;
        if (p.empty() || !isSupportedModelFile(p)) continue;
        std::string err;
        if (a.papp.importModelPath(p, &err)) ok++;
        else { bad++; lastErr = err; }
    }
    if (ok) a.papp.toast(ok == 1 ? "Модель добавлена" : "Моделей добавлено: " + std::to_string(ok));
    if (bad) a.papp.toast("Не удалось открыть модель: " + lastErr);
    if (!ok && !bad) a.papp.toast("Среди выбранных файлов нет моделей (OBJ, GLB, GLTF, STL)");
}

// --- EGL ---
bool initEgl(Host& a) {
    a.dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (a.dpy == EGL_NO_DISPLAY || !eglInitialize(a.dpy, nullptr, nullptr)) return false;
    const EGLint ca[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8,
                         EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE};
    EGLint n = 0;
    if (!eglChooseConfig(a.dpy, ca, &a.cfg, 1, &n) || n < 1) {
        const EGLint cb[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8,
                             EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
        if (!eglChooseConfig(a.dpy, cb, &a.cfg, 1, &n) || n < 1) return false;
    }
    const EGLint cx[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    a.ctx = eglCreateContext(a.dpy, a.cfg, EGL_NO_CONTEXT, cx);
    return a.ctx != EGL_NO_CONTEXT;
}

bool createSurface(Host& a) {
    a.surf = eglCreateWindowSurface(a.dpy, a.cfg, a.app->window, nullptr);
    if (a.surf == EGL_NO_SURFACE) return false;
    if (!eglMakeCurrent(a.dpy, a.surf, a.surf, a.ctx)) return false;
    eglQuerySurface(a.dpy, a.surf, EGL_WIDTH, &a.w);
    eglQuerySurface(a.dpy, a.surf, EGL_HEIGHT, &a.h);
    eglSwapInterval(a.dpy, 1);
    return true;
}

void destroySurface(Host& a) {
    if (a.dpy == EGL_NO_DISPLAY) return;
    eglMakeCurrent(a.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (a.surf != EGL_NO_SURFACE) eglDestroySurface(a.dpy, a.surf);
    a.surf = EGL_NO_SURFACE;
}

void shutdownEgl(Host& a) {
    destroySurface(a);
    if (a.ctx != EGL_NO_CONTEXT) eglDestroyContext(a.dpy, a.ctx);
    if (a.dpy != EGL_NO_DISPLAY) eglTerminate(a.dpy);
    a.ctx = EGL_NO_CONTEXT;
    a.dpy = EGL_NO_DISPLAY;
}

float queryDensity(Host& a, int w) {
    // dp-масштаб из плотности экрана; интерфейс рассчитан примерно на 360-440 dp по ширине
    int32_t dpi = AConfiguration_getDensity(a.app->config);
    float byWidth = w / 400.f;
    if (dpi <= 0 || dpi >= 1000) return byWidth;
    float d = dpi / 160.f;
    return std::max(1.f, std::min(d, byWidth * 1.15f));
}

void onWindowReady(Host& a) {
    if (!a.app->window) return;
    if (a.dpy == EGL_NO_DISPLAY && !initEgl(a)) { ENG_LOG("EGL init failed"); return; }
    if (!createSurface(a)) { ENG_LOG("surface failed"); return; }
    a.density = queryDensity(a, std::min(a.w, a.h));
    if (!a.papp.initGL(a.w, a.h, a.density)) {
        ENG_LOG("PrismLab initGL failed");
        a.papp.toast("Не удалось запустить OpenGL ES 3.0");
        return;
    }
    a.glOk = true;
    if (!a.started) {
        a.started = true;
        a.papp.loadAutosave();
    }
}

void onWindowLost(Host& a) {
    if (a.glOk) { a.papp.onPause(); a.papp.shutdown(); a.glOk = false; }
    destroySurface(a);
}

void handleCmd(android_app* app, int32_t cmd) {
    Host& a = *(Host*)app->userData;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW: onWindowReady(a); break;
        case APP_CMD_TERM_WINDOW: onWindowLost(a); break;
        case APP_CMD_GAINED_FOCUS: a.focused = true; break;
        case APP_CMD_LOST_FOCUS: a.focused = false; break;
        case APP_CMD_PAUSE: if (a.glOk) a.papp.onPause(); break;
        default: break;
    }
}

int32_t handleInput(android_app* app, AInputEvent* ev) {
    Host& a = *(Host*)app->userData;
    int type = AInputEvent_getType(ev);
    if (type == AINPUT_EVENT_TYPE_KEY) {
        if (AKeyEvent_getKeyCode(ev) == AKEYCODE_BACK) {
            if (AKeyEvent_getAction(ev) == AKEY_EVENT_ACTION_UP && a.glOk) return a.papp.onBack() ? 1 : 0;
            return a.glOk ? 1 : 0;  // нажатие поглощаем, решаем при отпускании
        }
        return 0;
    }
    if (type != AINPUT_EVENT_TYPE_MOTION || !a.glOk) return 0;
    int32_t act = AMotionEvent_getAction(ev);
    int32_t kind = act & AMOTION_EVENT_ACTION_MASK;
    int idx = (act & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    auto send = [&](int action, int i) {
        a.papp.touch(action, AMotionEvent_getPointerId(ev, i), AMotionEvent_getX(ev, i), AMotionEvent_getY(ev, i));
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
    Host a;
    a.app = app;
    app->userData = &a;
    app->onAppCmd = handleCmd;
    app->onInputEvent = handleInput;
    bool jni = attachJni(a);
    if (!jni) ENG_LOG("JNI bridge unavailable");
    setupPlatform(a);

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
        if ((frame++ & 15) == 0) pollPending(a);
        EGLint w = 0, h = 0;
        eglQuerySurface(a.dpy, a.surf, EGL_WIDTH, &w);
        eglQuerySurface(a.dpy, a.surf, EGL_HEIGHT, &h);
        if (w > 0 && h > 0 && (w != a.w || h != a.h)) {
            a.w = w; a.h = h;
            a.density = queryDensity(a, std::min(w, h));
            a.papp.resize(w, h, a.density);
        }
        a.papp.frame(nowSec());
        if (!eglSwapBuffers(a.dpy, a.surf)) {
            EGLint err = eglGetError();
            if (err == EGL_BAD_SURFACE || err == EGL_BAD_NATIVE_WINDOW) { ENG_LOG("surface lost"); }
            else if (err == EGL_CONTEXT_LOST) {
                ENG_LOG("context lost: recreating");
                a.papp.shutdown();
                a.glOk = false;
                shutdownEgl(a);
                onWindowReady(a);
            }
        }
    }
    if (a.glOk) { a.papp.onPause(); a.papp.shutdown(); }
    shutdownEgl(a);
    if (a.env) app->activity->vm->DetachCurrentThread();
}
