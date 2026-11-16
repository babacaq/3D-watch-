cmake_minimum_required(VERSION 3.22.1)
project(prismlab CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)
set(GLUE ${ANDROID_NDK}/sources/android/native_app_glue)

file(GLOB ENGINE_SRC ${ROOT}/engine/*.cpp)
add_library(app_glue STATIC ${GLUE}/android_native_app_glue.c)
target_include_directories(app_glue PUBLIC ${GLUE})

add_library(prismlab SHARED ${ENGINE_SRC} ${ROOT}/platform/android_main.cpp)
target_include_directories(prismlab PRIVATE ${ROOT}/engine)
target_compile_options(prismlab PRIVATE -Wall)
# ANativeActivity_onCreate должен остаться в библиотеке
set_target_properties(prismlab PROPERTIES LINK_FLAGS "-u ANativeActivity_onCreate")
target_link_libraries(prismlab app_glue android EGL GLESv3 log z)
