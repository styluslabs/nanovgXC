/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 */

#define SK_GL
#include "glad.h"
#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "SDL.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkSurface.h"
#include "include/utils/SkRandom.h"
#include "include/effects/SkGradientShader.h"

#include "include/gpu/gl/GrGLInterface.h"
#include "src/gpu/gl/GrGLDefines.h"

#if defined(SK_BUILD_FOR_ANDROID)
#include <GLES/gl.h>
#elif defined(SK_BUILD_FOR_UNIX)
#include <GL/gl.h>
#elif defined(SK_BUILD_FOR_MAC)
#include <OpenGL/gl.h>
#elif defined(SK_BUILD_FOR_IOS)
#include <OpenGLES/ES2/gl.h>
#endif

#define PLATFORM_LOG(...) fprintf(stderr, __VA_ARGS__)

/*
 * This application is a simple example of how to combine SDL and Skia it demonstrates:
 *   how to setup gpu rendering to the main window
 *   how to perform cpu-side rendering and draw the result to the gpu-backed screen
 *   draw simple primitives (rectangles)
 *   draw more complex primitives (star)
 */

// hack to fix link error - from https://github.com/microsoft/STL/blob/main/stl/src/vector_algorithms.cpp
extern "C" {
  const void* __stdcall __std_find_trivial_4(
    const void* const _First, const void* const _Last, const uint32_t _Val) noexcept;

  void SetProcessDPIAware();
}

const void* __stdcall __std_find_trivial_4(
    const void* const _First, const void* const _Last, const uint32_t _Val) noexcept {
  auto _Ptr = static_cast<const uint32_t*>(_First);
  while (_Ptr != _Last && *_Ptr != _Val) {
      ++_Ptr;
  }
  return _Ptr;
}

template<typename ... Args>
std::string fstring(const char* format, Args ... args)
{
    int size_s = std::snprintf( nullptr, 0, format, args ... ) + 1; // Extra space for '\0'
    if( size_s <= 0 ) return "";
    auto size = static_cast<size_t>( size_s );
    std::unique_ptr<char[]> buf( new char[ size ] );
    std::snprintf( buf.get(), size, format, args ... );
    return std::string( buf.get(), buf.get() + size - 1 ); // We don't want the '\0' inside
}

// cut and paste from tools/trace/SkDebugfTracer.cpp since that doesn't seem to be included in our skia.lib
#include "include/utils/SkEventTracer.h"
#include "src/core/SkTraceEvent.h"

class SkDebugfTracer : public SkEventTracer {
public:
    SkDebugfTracer() {}

    SkEventTracer::Handle addTraceEvent(char phase,
                                        const uint8_t* categoryEnabledFlag,
                                        const char* name,
                                        uint64_t id,
                                        int numArgs,
                                        const char** argNames,
                                        const uint8_t* argTypes,
                                        const uint64_t* argValues,
                                        uint8_t flags) override;

    void updateTraceEventDuration(const uint8_t* categoryEnabledFlag,
                                  const char* name,
                                  SkEventTracer::Handle handle) override;

    const uint8_t* getCategoryGroupEnabled(const char* name) override {
        static uint8_t yes = 0xFF;
        return &yes;  //return fCategories.getCategoryGroupEnabled(name);
    }

    const char* getCategoryGroupName(const uint8_t* categoryEnabledFlag) override {
        return "dummy";  //fCategories.getCategoryGroupName(categoryEnabledFlag);
    }

private:
    SkString fIndent;
    int fCnt = 0;
    //SkEventTracingCategories fCategories;
};

SkEventTracer::Handle SkDebugfTracer::addTraceEvent(char phase,
                                                    const uint8_t* categoryEnabledFlag,
                                                    const char* name,
                                                    uint64_t id,
                                                    int numArgs,
                                                    const char** argNames,
                                                    const uint8_t* argTypes,
                                                    const uint64_t* argValues,
                                                    uint8_t flags) {
    SkString args;
    for (int i = 0; i < numArgs; ++i) {
        if (i > 0) {
            args.append(", ");
        } else {
            args.append(" ");
        }
        skia::tracing_internals::TraceValueUnion value;
        value.as_uint = argValues[i];
        switch (argTypes[i]) {
            case TRACE_VALUE_TYPE_BOOL:
                args.appendf("%s=%s", argNames[i], value.as_bool ? "true" : "false");
                break;
            case TRACE_VALUE_TYPE_UINT:
                args.appendf("%s=%u", argNames[i], static_cast<uint32_t>(argValues[i]));
                break;
            case TRACE_VALUE_TYPE_INT:
                args.appendf("%s=%d", argNames[i], static_cast<int32_t>(argValues[i]));
                break;
            case TRACE_VALUE_TYPE_DOUBLE:
                args.appendf("%s=%g", argNames[i], value.as_double);
                break;
            case TRACE_VALUE_TYPE_POINTER:
                args.appendf("%s=0x%p", argNames[i], value.as_pointer);
                break;
            case TRACE_VALUE_TYPE_STRING:
            case TRACE_VALUE_TYPE_COPY_STRING: {
                static constexpr size_t kMaxLen = 20;
                SkString string(value.as_string);
                size_t truncAt = string.size();
                size_t newLineAt = SkStrFind(string.c_str(), "\n");
                if (newLineAt > 0) {
                    truncAt = newLineAt;
                }
                truncAt = std::min(truncAt, kMaxLen);
                if (truncAt < string.size()) {
                    string.resize(truncAt);
                    string.append("...");
                }
                args.appendf("%s=\"%s\"", argNames[i], string.c_str());
                break;
            }
            default:
                args.appendf("%s=<unknown type>", argNames[i]);
                break;
        }
    }
    bool open = (phase == TRACE_EVENT_PHASE_COMPLETE);
    if (open) {
        const char* category = "???";  //this->getCategoryGroupName(categoryEnabledFlag);
        SkDebugf("[% 2d]%s <%s> %s%s #%d {\n", fIndent.size(), fIndent.c_str(), category, name,
                 args.c_str(), fCnt);
        fIndent.append(" ");
    } else {
        SkDebugf("%s%s #%d\n", name, args.c_str(), fCnt);
    }
    ++fCnt;
    return 0;
}

void SkDebugfTracer::updateTraceEventDuration(const uint8_t* categoryEnabledFlag,
                                              const char* name,
                                              SkEventTracer::Handle handle) {
    fIndent.resize(fIndent.size() - 1);
    SkDebugf("[% 2d]%s } %s\n", fIndent.size(), fIndent.c_str(), name);
}

 // nanosvg
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

static SkColor swapRB(uint32_t c)
{
  static constexpr int SHIFT_A = 24;
  static constexpr int SHIFT_R = 0;
  static constexpr int SHIFT_G = 8;
  static constexpr int SHIFT_B = 16;
  // masks
  static constexpr uint32_t A = 0xFFu << SHIFT_A;
  static constexpr uint32_t R = 0xFFu << SHIFT_R;
  static constexpr uint32_t G = 0xFFu << SHIFT_G;
  static constexpr uint32_t B = 0xFFu << SHIFT_B;
  return (c & A) | ((c & B) >> 16) | (c & G) | ((c & R) << 16);
}

static void nsvgRender(SkCanvas* canvas, NSVGimage* image, float bounds[4])
{
  static SkPaint::Cap nsvgCap[] = {SkPaint::kButt_Cap, SkPaint::kRound_Cap, SkPaint::kSquare_Cap};
  static SkPaint::Join nsvgJoin[] = {SkPaint::kMiter_Join, SkPaint::kRound_Join, SkPaint::kBevel_Join};

  float w = bounds[2] - bounds[0];
  float h = bounds[3] - bounds[1];
  float s = nsvg__minf(w / image->width, h / image->height);
  canvas->save();
  // map SVG viewBox to window
  canvas->translate(bounds[0], bounds[1]);
  canvas->scale(s, s);
  //nvgTransform(vg, s, 0, 0, s, bounds[0], bounds[1]);
  // now map SVG content to SVG viewBox (by applying viewBox transform)
  float* m = image->viewXform;
  //canvas->concat(SkMatrix::MakeAll(m[0], m[1], m[2], m[3], m[4], m[5], 0, 0, 0));
  for(NSVGshape* shape = image->shapes; shape != NULL; shape = shape->next) {
    if(shape->fill.type == NSVG_PAINT_NONE && shape->stroke.type == NSVG_PAINT_NONE) continue;
    if(shape->paths) {
      SkPath skPath;
      for(NSVGpath* path = shape->paths; path != NULL; path = path->next) {
        skPath.moveTo(path->pts[0], path->pts[1]);
        for (int i = 1; i < path->npts-1; i += 3) {
          float* p = &path->pts[i*2];
          if(fabsf((p[2] - p[0]) * (p[5] - p[1]) - (p[4] - p[0]) * (p[3] - p[1])) < 0.001f)
            skPath.lineTo(p[4],p[5]);
          else
            skPath.cubicTo(p[0],p[1], p[2],p[3], p[4],p[5]);
        }
        if(path->closed)
          skPath.close();
      }
      if(shape->fill.type != NSVG_PAINT_NONE) {
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setStyle(SkPaint::kFill_Style);
        if(shape->fill.type == NSVG_PAINT_COLOR)
          paint.setColor(swapRB(shape->fill.color));
        else if(shape->fill.type == NSVG_PAINT_LINEAR_GRADIENT) {
          // we only support simple (2-stop) gradients for now
          NSVGgradient* g = shape->fill.gradient;
          NSVGgradientStop* s0 = &g->stops[0];
          NSVGgradientStop* s1 = &g->stops[1];
          float* t = g->xform;
          float x0 = t[4], y0 = t[5], dx = t[2], dy = t[3];
          SkColor colors[] = { swapRB(s0->color), swapRB(s1->color) };
          SkScalar positions[] = { 0.0, 1.0 };
          SkPoint pts[] = { {x0 + s0->offset*dx, y0 + s0->offset*dy}, {x0 + s1->offset*dx, y0 + s1->offset*dy} };
          auto lg = SkGradientShader::MakeLinear(pts, colors, positions, 2, SkTileMode::kMirror);
          paint.setShader(lg);
        }
        canvas->drawPath(skPath, paint);
      }
      if (shape->stroke.type == NSVG_PAINT_COLOR) {
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(shape->strokeWidth);
        paint.setStrokeCap(nsvgCap[(int)shape->strokeLineCap]);
        paint.setStrokeJoin(nsvgJoin[(int)shape->strokeLineJoin]);
        paint.setColor(swapRB(shape->stroke.color));
        canvas->drawPath(skPath, paint);
      }
    }
  }
  canvas->restore();
}

static void svgTest(SkCanvas* canvas, const char* filename, int fbWidth, int fbHeight)
{
  static NSVGimage* image = NULL;
  if(!image) {
    image = nsvgParseFromFile(filename, "px", 96.0f);
    if(!image) {
      //NVG_LOG("Error loading %s\n", filename);
      exit(-1);
    }
  }

  float bounds[4] = {0, 0, fbWidth, fbHeight};
  nsvgRender(canvas, image, bounds);
}


struct ApplicationState {
    ApplicationState() : fQuit(false) {}
    // Storage for the user created rectangles. The last one may still be being edited.
    SkTArray<SkRect> fRects;
    bool fQuit;
};

static void handle_error() {
    const char* error = SDL_GetError();
    SkDebugf("SDL Error: %s\n", error);
    SDL_ClearError();
}

static void handle_events(ApplicationState* state, SkCanvas* canvas) {
    SDL_Event event;
    while(SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_MOUSEMOTION:
                if (event.motion.state == SDL_PRESSED) {
                    SkRect& rect = state->fRects.back();
                    rect.fRight = event.motion.x;
                    rect.fBottom = event.motion.y;
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.state == SDL_PRESSED) {
                    state->fRects.push_back() = SkRect::MakeLTRB(SkIntToScalar(event.button.x),
                                                                 SkIntToScalar(event.button.y),
                                                                 SkIntToScalar(event.button.x),
                                                                 SkIntToScalar(event.button.y));
                }
                break;
            case SDL_KEYDOWN: {
                SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_ESCAPE) {
                    state->fQuit = true;
                }
                break;
            }
            case SDL_QUIT:
                state->fQuit = true;
                break;
            default:
                break;
        }
    }
}

// Creates a star type shape using a SkPath
static SkPath create_star() {
    static const int kNumPoints = 5;
    SkPath concavePath;
    SkPoint points[kNumPoints] = {{0, SkIntToScalar(-50)} };
    SkMatrix rot;
    rot.setRotate(SkIntToScalar(360) / kNumPoints);
    for (int i = 1; i < kNumPoints; ++i) {
        rot.mapPoints(points + i, points + i - 1, 1);
    }
    concavePath.moveTo(points[0]);
    for (int i = 0; i < kNumPoints; ++i) {
        concavePath.lineTo(points[(2 * i) % kNumPoints]);
    }
    concavePath.setFillType(SkPathFillType::kEvenOdd);
    SkASSERT(!concavePath.isConvex());
    concavePath.close();
    return concavePath;
}

#if defined(SK_BUILD_FOR_ANDROID)
int SDL_main(int argc, char** argv) {
#else
int main(int argc, char** argv) {
#endif
  // If you want multisampling, set this > 0
  int kMsaaSampleCount = 0; //4;
  static const int kStencilBits = 8;  // Skia needs 8 stencil bits

  int doTrace = 0;
  int swRender = 0;
  const char* svgFile = NULL;
  for(int argi = 1; argi < argc; ++argi) {
    if(strcmp(argv[argi], "--sw") == 0 && ++argi < argc)
      swRender = atoi(argv[argi]);
    if(strcmp(argv[argi], "--msaa") == 0 && ++argi < argc)
      kMsaaSampleCount = atoi(argv[argi]);
    if(strcmp(argv[argi], "--trace") == 0 && ++argi < argc)
      doTrace = atoi(argv[argi]);
    if(strcmp(argv[argi], "--svg") == 0 && ++argi < argc)
      svgFile = argv[argi];
  }

#ifdef _WIN32
  SetProcessDPIAware();
#endif

  int dw, dh;
  uint32_t windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#if defined(SK_BUILD_FOR_ANDROID) || defined(SK_BUILD_FOR_IOS)
  windowFlags |= SDL_WINDOW_BORDERLESS | SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
  SDL_GLContext glContext = nullptr;
  if(swRender == 0) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);

#if defined(SK_BUILD_FOR_ANDROID) || defined(SK_BUILD_FOR_IOS)
    // For Android/iOS we need to set up for OpenGL ES and we make the window hi res & full screen
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    // For all other clients we use the core profile and operate in a window
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    windowFlags |= SDL_WINDOW_OPENGL;
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, kStencilBits);

    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);

    if(kMsaaSampleCount > 0) {
      SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
      SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, kMsaaSampleCount);
    }
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
      handle_error();
      return 1;
  }

  // Setup window
  // This code will create a window with the same resolution as the user's desktop.
  SDL_DisplayMode dm;
  if (SDL_GetDesktopDisplayMode(0, &dm) != 0) {
      handle_error();
      return 1;
  }

  SDL_Window* sdlWindow = SDL_CreateWindow("SDL Window",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, dm.w, dm.h, windowFlags);

  if (!sdlWindow) {
      handle_error();
      return 1;
  }

  // To go fullscreen
  // SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);

  sk_sp<const GrGLInterface> interface;
  sk_sp<GrDirectContext> grContext;
  sk_sp<SkSurface> skSurface;
  std::unique_ptr<GrBackendRenderTarget> target;
  SDL_Surface* sdlSurface = NULL;
  if(swRender == 0) {
    // try and setup a GL context
    glContext = SDL_GL_CreateContext(sdlWindow);
    if (!glContext) {
        handle_error();
        return 1;
    }

#ifdef __glad_h_
    gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress);
#elif defined(GLEW_VERSION)
    glewInit();
#endif

    int success =  SDL_GL_MakeCurrent(sdlWindow, glContext);
    if (success != 0) {
        handle_error();
        return success;
    }

    uint32_t windowFormat = SDL_GetWindowPixelFormat(sdlWindow);
    int contextType;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &contextType);

    SDL_GL_GetDrawableSize(sdlWindow, &dw, &dh);

    glViewport(0, 0, dw, dh);
    glClearColor(1, 1, 1, 1);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    // setup GrContext
    interface = GrGLMakeNativeInterface();

    // setup contexts
    grContext = GrDirectContext::MakeGL(interface);
    SkASSERT(grContext);

    // Wrap the frame buffer object attached to the screen in a Skia render target so Skia can
    // render to it
    GrGLint buffer;
    //GR_GL_GetIntegerv(interface.get(), GR_GL_FRAMEBUFFER_BINDING, &buffer);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &buffer);
    GrGLFramebufferInfo info;
    info.fFBOID = (GrGLuint) buffer;
    SkColorType colorType;

    //SkDebugf("%s", SDL_GetPixelFormatName(windowFormat));
    // TODO: the windowFormat is never any of these?
    if (SDL_PIXELFORMAT_RGBA8888 == windowFormat) {
        info.fFormat = GR_GL_RGBA8;
        colorType = kRGBA_8888_SkColorType;
    } else {
        colorType = kBGRA_8888_SkColorType;
        if (SDL_GL_CONTEXT_PROFILE_ES == contextType) {
            info.fFormat = GR_GL_BGRA8;
        } else {
            // We assume the internal format is RGBA8 on desktop GL
            info.fFormat = GR_GL_RGBA8;
        }
    }

    target.reset(new GrBackendRenderTarget(dw, dh, kMsaaSampleCount, kStencilBits, info));

    // setup SkSurface
    // To use distance field text, use commented out SkSurfaceProps instead
    // SkSurfaceProps props(SkSurfaceProps::kUseDeviceIndependentFonts_Flag,
    //                      SkSurfaceProps::kUnknown_SkPixelGeometry);
    // what about trying SkSurfaceProps::kDynamicMSAA_Flag?
    SkSurfaceProps props;

    skSurface = SkSurface::MakeFromBackendRenderTarget(
        grContext.get(), *target, kBottomLeft_GrSurfaceOrigin, colorType, nullptr, &props);
    //grContext->auditTrail().setEnabled(true);
  }
  else { // swRender > 0
    sdlSurface = SDL_GetWindowSurface(sdlWindow);
    dw = sdlSurface->w;
    dh = sdlSurface->h;
    // or kRGBA_8888_SkColorType ; or kPremul_SkAlphaType
    auto imageInfo = SkImageInfo::Make(dw, dh, kBGRA_8888_SkColorType, kUnpremul_SkAlphaType);
    skSurface = SkSurface::MakeRasterDirect(imageInfo, sdlSurface->pixels, sdlSurface->pitch);
  }
  PLATFORM_LOG("%d x %d surface created\n", dw, dh);

  SkCanvas* canvas = skSurface->getCanvas();
  //canvas->scale((float)dw/dm.w, (float)dh/dm.h);

  ApplicationState state;

  const char* helpMessage = "Click and drag to create rects.  Press esc to quit.";
  std::string fpsMessage = "0 FPS";

  // would GrAuditTrail give more info?
  if(doTrace)
    SkEventTracer::SetInstance(new SkDebugfTracer());

  SkPaint paint;
  paint.setAntiAlias(true);

  // create a surface for CPU rasterization
  /*
  sk_sp<SkSurface> cpuSurface(SkSurface::MakeRaster(canvas->imageInfo()));
  SkCanvas* offscreen = cpuSurface->getCanvas();
  offscreen->save();
  offscreen->translate(50.0f, 50.0f);
  offscreen->drawPath(create_star(), paint);
  offscreen->restore();
  sk_sp<SkImage> image = cpuSurface->makeImageSnapshot();
  */

  // FPS counter
  constexpr int FRAME_HISTORY_COUNT = 100;
  size_t framecnt = 0;
  //double framedt[FRAME_HISTORY_COUNT];
  std::vector<double> framedt(FRAME_HISTORY_COUNT, 0);
  double countToSec = 1.0/SDL_GetPerformanceFrequency();
  double prevt = SDL_GetPerformanceCounter()*countToSec;

  int rotation = 0;
  SkFont font;
  font.setSize(32);
  while (!state.fQuit) {
    /// TODO: recreate SkSurface if window resizes
    //int newdw = 0, newdh = 0;
    //SDL_GL_GetDrawableSize(sdlWindow, &newdw, &newdh);
    //if(0) {  //newdw != dw || newdh != dh) {
    //  dw = newdw;
    //  dh = newdh;
    //  glViewport(0, 0, dw, dh);
    //  target = GrBackendRenderTarget(dw, dh, kMsaaSampleCount, kStencilBits, info);
    //  skSurface = SkSurface::MakeFromBackendRenderTarget(grContext.get(), target,
    //      kBottomLeft_GrSurfaceOrigin, colorType, nullptr, &props);
    //  canvas = surface->getCanvas();
    //  //canvas->scale((float)dw/dm.w, (float)dh/dm.h);
    //}
    if(swRender != 0)
      SDL_LockSurface(sdlSurface);

    SkRandom rand;
    canvas->clear(SK_ColorWHITE);
    handle_events(&state, canvas);

    if(svgFile)
      svgTest(canvas, svgFile, dw, dh);
    else {
      for (int i = 0; i < state.fRects.count(); i++) {
          paint.setColor(rand.nextU() | 0x44808080);
          canvas->drawRect(state.fRects[i], paint);
      }

      // draw offscreen canvas
      //canvas->save();
      //canvas->translate(dm.w / 2.0, dm.h / 2.0);
      //canvas->rotate(rotation++);
      //canvas->drawImage(image, -50.0f, -50.0f);
      //canvas->restore();
    }

    paint.setColor(SK_ColorRED);
    canvas->drawString(fpsMessage.c_str(), 5.0f, 36.0f, font, paint);

    canvas->flush();

    if(swRender == 0) {
      SDL_GL_SwapWindow(sdlWindow);
    }
    else {
      SDL_UnlockSurface(sdlSurface);
      SDL_UpdateWindowSurface(sdlWindow);
    }

    //auto t1 = std::chrono::high_resolution_clock::now();
    //fpsMessage = fstring("FPS: %.2f", 1.0/std::chrono::duration<float>(t1 - t0).count());
    double t = SDL_GetPerformanceCounter()*countToSec;
    framedt[framecnt++ % framedt.size()] = t - prevt;
    prevt = t;
    double sumdt = 0;
    int numdt = 0;
    for(double dt : framedt) {
      sumdt += dt;
      if(dt > 0) ++numdt;
    }
    fpsMessage = fstring("FPS: %.2f", numdt/sumdt);
    if(doTrace) break;
  }

  // shutdown
  SDL_GL_DeleteContext(glContext);
  SDL_DestroyWindow(sdlWindow);
  SDL_Quit();
  exit(0);  // Skia doesn't want to exit
  return 0;
}
