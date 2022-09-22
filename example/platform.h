#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

#if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>

#define DATA_PATH(x) (x)

#elif defined(__ANDROID__)

#include <GLES3/gl31.h>
#include <GLES3/gl3ext.h>

// hack to load system fonts
#define DATA_PATH(x) ("/system/" x)

#include <android/log.h>
#define NVG_LOG(...) __android_log_print(ANDROID_LOG_VERBOSE, "nanovg-2 demo",  __VA_ARGS__)

#else
//#define GLEW_STATIC
//#include <GL/glew.h>
//#include <GLFW/glfw3.h>
#include "glad.h"

#define DATA_PATH(x) ("example/" x)
#endif

//#ifdef _MSC_VER
//#define snprintf _snprintf
//#elif !defined(__MINGW32__)
//#include <iconv.h>
//#endif

#endif
