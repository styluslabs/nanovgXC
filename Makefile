# simple C/C++ makefile
# - generates files in ./Release (default) or ./Debug (with DEBUG=1 passed to make)

TARGET = demo2_sdl
SOURCES = src/nanovg.c example/perf.c example/demo.c example/example_sdl.c
INC = src example glad
INCSYS = example/stb

#DEFS += FONS_SDF  -- to use SDF text rendering
# to enable threading for SW renderer (uses std::thread, etc)
#FORCECPP = example/example_sdl.c

# machine specific or private configuration not committed to git (e.g. iOS signing info)
-include Makefile.local

ifneq ($(windir),)
# Windows

SOURCES += glad/glad.c
INCSYS += ../SDL/include
DEFS += _USE_MATH_DEFINES UNICODE NOMINMAX

# only dependencies under this path will be tracked in .d files; note [\\] must be used for "\"
# ensure that no paths containing spaces are included
DEPENDBASE ?= c:[\\]temp[\\]styluslabs

# for dynamically linked Windows SDL libraries, both SDL2.lib and SDL2main.lib are needed
LIBS = \
  ../SDL/Release/SDL2.lib \
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
