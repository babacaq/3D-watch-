// Минимальные объявления GLES3/EGL для тестовой сборки на Linux без dev-пакетов.
// Используются только функции, которые нужны движку. Линкуется с libGLESv2 и libEGL.
#pragma once
#include <cstddef>
#include <cstdint>

extern "C" {
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NONE 0
#define GL_NO_ERROR 0
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_LINES 0x0001
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_STREAM_DRAW 0x88E0
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_BLEND 0x0BE2
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_BACK 0x0405
#define GL_FRONT 0x0404
#define GL_LESS 0x0201
#define GL_LEQUAL 0x0203
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_REPEAT 0x2901
#define GL_ONE 1
#define GL_FUNC_ADD 0x8006
#define GL_RGB 0x1907
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_VERTEX_PROGRAM_POINT_SIZE 0x8642
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_RED 0x1903
#define GL_R8 0x8229
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_COMPONENT16 0x81A5
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
#define GL_MAX_VERTEX_UNIFORM_VECTORS 0x8DFB

GLenum glGetError(void);
const GLubyte* glGetString(GLenum name);
void glGetIntegerv(GLenum pname, GLint* data);
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h);
void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glClear(GLbitfield mask);
void glEnable(GLenum cap);
void glDisable(GLenum cap);
void glDepthFunc(GLenum f);
void glDepthMask(GLboolean flag);
void glCullFace(GLenum mode);
void glBlendFunc(GLenum s, GLenum d);
void glVertexAttribDivisor(GLuint index, GLuint divisor);
void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instances);
void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances);
void glPolygonOffset(GLfloat factor, GLfloat units);
void glPixelStorei(GLenum pname, GLint param);
void glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* data);

GLuint glCreateShader(GLenum type);
void glShaderSource(GLuint s, GLsizei count, const GLchar* const* str, const GLint* len);
void glCompileShader(GLuint s);
void glGetShaderiv(GLuint s, GLenum pname, GLint* p);
void glGetShaderInfoLog(GLuint s, GLsizei max, GLsizei* len, GLchar* log);
void glDeleteShader(GLuint s);
GLuint glCreateProgram(void);
void glAttachShader(GLuint p, GLuint s);
void glLinkProgram(GLuint p);
void glGetProgramiv(GLuint p, GLenum pname, GLint* v);
void glGetProgramInfoLog(GLuint p, GLsizei max, GLsizei* len, GLchar* log);
void glDeleteProgram(GLuint p);
void glUseProgram(GLuint p);
GLint glGetUniformLocation(GLuint p, const GLchar* name);
void glUniform1i(GLint loc, GLint v);
void glUniform1f(GLint loc, GLfloat v);
void glUniform2f(GLint loc, GLfloat a, GLfloat b);
void glUniform3f(GLint loc, GLfloat a, GLfloat b, GLfloat c);
void glUniform4fv(GLint loc, GLsizei count, const GLfloat* v);
void glUniform3fv(GLint loc, GLsizei count, const GLfloat* v);
void glUniform4f(GLint loc, GLfloat a, GLfloat b, GLfloat c, GLfloat d);
void glUniformMatrix4fv(GLint loc, GLsizei count, GLboolean transpose, const GLfloat* v);

void glGenBuffers(GLsizei n, GLuint* b);
void glBindBuffer(GLenum target, GLuint b);
void glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
void glDeleteBuffers(GLsizei n, const GLuint* b);
void glGenVertexArrays(GLsizei n, GLuint* a);
void glBindVertexArray(GLuint a);
void glDeleteVertexArrays(GLsizei n, const GLuint* a);
void glEnableVertexAttribArray(GLuint i);
void glDisableVertexAttribArray(GLuint i);
void glVertexAttribPointer(GLuint i, GLint size, GLenum type, GLboolean norm, GLsizei stride, const void* p);
void glVertexAttribIPointer(GLuint i, GLint size, GLenum type, GLsizei stride, const void* p);
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* idx);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);

void glGenTextures(GLsizei n, GLuint* t);
void glBindTexture(GLenum target, GLuint t);
void glActiveTexture(GLenum unit);
void glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum fmt,
                  GLenum type, const void* data);
void glGenerateMipmap(GLenum target);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
void glDeleteTextures(GLsizei n, const GLuint* t);

void glGenFramebuffers(GLsizei n, GLuint* f);
void glBindFramebuffer(GLenum target, GLuint f);
void glFramebufferTexture2D(GLenum target, GLenum att, GLenum textarget, GLuint tex, GLint level);
void glFramebufferRenderbuffer(GLenum target, GLenum att, GLenum rbtarget, GLuint rb);
GLenum glCheckFramebufferStatus(GLenum target);
void glDeleteFramebuffers(GLsizei n, const GLuint* f);
void glGenRenderbuffers(GLsizei n, GLuint* r);
void glBindRenderbuffer(GLenum target, GLuint r);
void glRenderbufferStorage(GLenum target, GLenum ifmt, GLsizei w, GLsizei h);
void glDeleteRenderbuffers(GLsizei n, const GLuint* r);
void glDrawBuffers(GLsizei n, const GLenum* bufs);
void glReadBuffer(GLenum src);

// ---- EGL ----
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLContext;
typedef void* EGLSurface;
typedef int32_t EGLint;
typedef unsigned int EGLBoolean;
typedef unsigned int EGLenum;
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_NONE 0x3038
#define EGL_SURFACE_TYPE 0x3033
#define EGL_PBUFFER_BIT 0x0001
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES3_BIT 0x0040
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_DEPTH_SIZE 0x3025
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_OPENGL_ES_API 0x30A0
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
EGLint eglGetError(void);
EGLBoolean eglInitialize(EGLDisplay d, EGLint* major, EGLint* minor);
EGLBoolean eglChooseConfig(EGLDisplay d, const EGLint* attribs, EGLConfig* configs, EGLint size, EGLint* num);
EGLBoolean eglBindAPI(EGLenum api);
EGLContext eglCreateContext(EGLDisplay d, EGLConfig c, EGLContext share, const EGLint* attribs);
EGLSurface eglCreatePbufferSurface(EGLDisplay d, EGLConfig c, const EGLint* attribs);
EGLBoolean eglMakeCurrent(EGLDisplay d, EGLSurface draw, EGLSurface read, EGLContext ctx);
void* eglGetProcAddress(const char* name);
}
