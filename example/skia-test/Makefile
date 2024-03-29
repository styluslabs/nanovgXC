# simple C/C++ makefile
# - generates files in ./Release (default) or ./Debug (with DEBUG=1 passed to make)

## skia.lib obtained from https://github.com/rust-skia/skia-binaries/releases
## specifically: https://github.com/rust-skia/skia-binaries/releases/download/0.55.0/skia-binaries-94c343245161df4a8f67-x86_64-pc-windows-msvc-gl.tar.gz
## skia source (for headers) from git clone --depth 1 https://github.com/google/skia.git

## make, then run, e.g., "Release\skia_sdl.exe --svg ..\svg\tiger.svg" to test Skia GPU
## - add "--sw 1" to test Skia CPU

TARGET = skia_sdl
SOURCES = SkiaSDLTest.cpp
INC = ..
INCSYS = skia

# machine specific or private configuration not committed to git (e.g. iOS signing info)
-include Makefile.local

ifneq ($(windir),)
# Windows

SOURCES += ../../glad/glad.c
INCSYS += ../../glad ../../../SDL/include
DEFS += _USE_MATH_DEFINES UNICODE NOMINMAX

# need to set TOPDIR since we have .. paths in SOURCES
TOPDIR = example/skia-test

# only dependencies under this path will be tracked in .d files; note [\\] must be used for "\"
# ensure that no paths containing spaces are included
DEPENDBASE ?= c:[\\]temp[\\]styluslabs

# for dynamically linked Windows SDL libraries, both SDL2.lib and SDL2main.lib are needed
LIBS = \
  ../../../SDL/Release/SDL2-MD.lib \
  skia.lib \
  glu32.lib \
  opengl32.lib \
  gdi32.lib \
  user32.lib \
  shell32.lib \
  winmm.lib \
  ole32.lib \
  oleaut32.lib \
  advapi32.lib \
  setupapi.lib \
  imm32.lib \
  version.lib

RESOURCES =

# skia.lib uses /MD, so override our /MT default
CFLAGS = /MD

include Makefile.msvc

else ifneq ($(XPC_FLAGS),)
# iOS (XPC_FLAGS seems to be defined on macOS; for now we assume macOS means iOS build)

APPDIR = $(TARGET).app
DEFS += GLES_SILENCE_DEPRECATION
#iOS/Icon.png
IOSRES += iOS/Info.plist example/images example/fonts example/svg

# Makefile.local should define PROVISIONING_PROFILE (path to .mobileprovision), SIGNING_ID (for codesign), TEAM_ID, and BUNDLE_ID
XCENT = iOS/Dev.app.xcent
CODESIGN = /usr/bin/codesign --force --sign $(SIGNING_ID) --timestamp=none

INCSYS += ../SDL/include
LIBS = ../SDL/Release/libSDL2.a
# not all these may be needed depending on how SDL was built
FRAMEWORKS = AVFoundation GameController CoreMotion Foundation UIKit CoreGraphics OpenGLES QuartzCore CoreAudio AudioToolbox Metal

include Makefile.ios

else ifneq ($(BUILD_SHARED_LIBRARY),)
# Android
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

# SDLActivity is hardcoded to load "libmain.so"
LOCAL_MODULE := main

ALL_INC := $(INC) $(INCSYS) ../SDL/include
LOCAL_C_INCLUDES := $(addprefix $(LOCAL_PATH)/, $(ALL_INC))

LOCAL_CFLAGS := $(addprefix -D, $(DEFS))
LOCAL_CPPFLAGS := -std=c++14 -Wno-unused -Wno-error=format-security

LOCAL_SRC_FILES := $(addprefix $(LOCAL_PATH)/, $(SOURCES))
LOCAL_SHARED_LIBRARIES := SDL2
LOCAL_LDLIBS := -lGLESv3 -llog -ljnigraphics

include $(BUILD_SHARED_LIBRARY)

else
# Linux

SOURCES += glad/glad.c
SDL_INC ?= /usr/include/SDL2
INCSYS += $(SDL_INC)
SDL_LIB ?= -lSDL2
LIBS = -lpthread -ldl -lm -lGL $(SDL_LIB)
PKGS =

include Makefile.unix

endif
