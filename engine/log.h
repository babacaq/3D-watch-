#pragma once
#include <cstdio>

#if defined(__ANDROID__)
#include <android/log.h>
#define ENG_LOG(...) __android_log_print(ANDROID_LOG_INFO, "Mobile3D", __VA_ARGS__)
#else
#define ENG_LOG(...) (std::fprintf(stderr, "[engine] " __VA_ARGS__), std::fprintf(stderr, "\n"))
#endif
