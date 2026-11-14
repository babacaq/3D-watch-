// Единая точка подключения OpenGL ES 3.0.
// На Android берутся настоящие заголовки NDK, на ПК (тестовая сборка) - минимальные объявления.
#pragma once
#if defined(__ANDROID__)
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#else
#include "gl_compat.h"
#endif
