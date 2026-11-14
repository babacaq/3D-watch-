cmake_minimum_required(VERSION 3.22.1)
project(mobile3d CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)
set(GLUE ${ANDROID_NDK}/sources/android/native_app_glue)

file(GLOB ENGINE_SRC ${ROOT}/engine/*.cpp)
add_library(app_glue STATIC ${GLUE}/android_native_app_glue.c)
target_include_directories(app_glue PUBLIC ${GLUE})

add_library(mobile3d SHARED ${ENGINE_SRC} ${ROOT}/platform/android_main.cpp)
target_include_directories(mobile3d PRIVATE ${ROOT}/engine)
target_compile_options(mobile3d PRIVATE -Wall)
# ANativeActivity_onCreate должен остаться в библиотеке
set_target_properties(mobile3d PROPERTIES LINK_FLAGS "-u ANativeActivity_onCreate")
target_link_libraries(mobile3d app_glue android EGL GLESv3 log z)
