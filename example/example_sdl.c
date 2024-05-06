//
// Copyright (c) 2020 Stylus Labs - see LICENSE.txt
//   based on nanovg:
// Copyright (c) 2013 Mikko Mononen memon@inside.org
//
#include <stdio.h>

#include "platform.h"
#include <SDL.h>
#ifdef main
#undef main
#else
#define SDL_main main
#endif

#ifdef _WIN32
#define USE_DESKTOP_GL
#endif

#ifdef USE_DESKTOP_GL
#define NANOVG_GL3_IMPLEMENTATION
#else
#define NANOVG_GLES3_IMPLEMENTATION
#endif

//#define FONS_SDF
#define NANOVG_SW_IMPLEMENTATION
#define NVGSWU_GLES3

#include "nanovg.h"

//#include "nanovg_gl.h"
#include "nanovg_vtex.h"

#include "nanovg_gl_utils.h"
#include "nanovg_sw.h"
#include "nanovg_sw_utils.h"
#include "demo.h"
#include "perf.h"
#define NVG_GL 1

#define FONTSTASH_IMPLEMENTATION
#include "fontstash.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifndef _WIN32
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include "tests.c"

#ifndef NO_THREADING
#include "threadpool.h"
#endif

typedef struct { NVGcontext* vg; float* fbuff; int fbuffw, fbuffh; float sdfScale, sdfOffset; } SDFcontext;

static const float INITIAL_SDF_DIST = 1E6f;

static void sdfRender(void* uptr, void* fontimpl, unsigned char* output,
      int outWidth, int outHeight, int outStride, float scale, int padding, int glyph)
{
  SDFcontext* ctx = (SDFcontext*)uptr;

  nvgBeginFrame(ctx->vg, 0, 0, 1);
  nvgDrawSTBTTGlyph(ctx->vg, (stbtt_fontinfo*)fontimpl, scale, padding, glyph);
  nvgEndFrame(ctx->vg);

  for(int iy = 0; iy < outHeight; ++iy) {
    for(int ix = 0; ix < outWidth; ++ix) {
      float sd = ctx->fbuff[ix + iy*ctx->fbuffw] * ctx->sdfScale + ctx->sdfOffset;
      output[ix + iy*outStride] = (unsigned char)(0.5f + glnvg__minf(glnvg__maxf(sd, 0.f), 255.f));
      ctx->fbuff[ix + iy*ctx->fbuffw] = INITIAL_SDF_DIST;   // will get clamped to 255
    }
  }
}

static void sdfDelete(void* uptr)
{
  SDFcontext* ctx = (SDFcontext*)uptr;
  nvgswDelete(ctx->vg);
  free(ctx->fbuff);
  free(ctx);
}

FONScontext* createFontstash(int nvgFlags, int maxAtlasFontPx)
{
  FONSparams params;
  memset(&params, 0, sizeof(FONSparams));
  params.flags = FONS_ZERO_TOPLEFT | FONS_DELAY_LOAD;
  params.flags |= (nvgFlags & NVG_SDF_TEXT) ? FONS_SDF : FONS_SUMMED;
  params.sdfPadding = 4;
  params.sdfPixelDist = 32.0f;

  SDFcontext* ctx = malloc(sizeof(SDFcontext));
  ctx->fbuffh = ctx->fbuffw = maxAtlasFontPx + 2*params.sdfPadding + 16;
  // we use dist < 0.0f inside glyph; but for scale > 0, stbtt uses >on_edge_value for inside
  ctx->sdfScale = -params.sdfPixelDist;
  ctx->sdfOffset = 127;  // stbtt on_edge_value

  ctx->fbuff = malloc(ctx->fbuffw*ctx->fbuffh*sizeof(float));
  for(size_t ii = 0; ii < ctx->fbuffw*ctx->fbuffh; ++ii)
    ctx->fbuff[ii] = INITIAL_SDF_DIST;
  ctx->vg = nvgswCreate(NVG_AUTOW_DEFAULT | NVG_NO_FONTSTASH | NVGSW_PATHS_XC | NVGSW_SDFGEN);
  nvgswSetFramebuffer(ctx->vg, ctx->fbuff, ctx->fbuffw, ctx->fbuffh, params.sdfPadding, 0,0,0);

  params.userPtr = ctx;
  params.userSDFRender = sdfRender;
  params.userDelete = sdfDelete;
  return fonsCreateInternal(&params);
}

int SDL_main(int argc, char* argv[])
{
  DemoData demoData;
  PerfGraph fpsGraph, gpuGraph, cpuGraph;
  SDL_Window* sdlWindow = NULL;
  SDL_GLContext sdlContext = NULL;
  NVGcontext* vg = NULL;
#ifdef NVG_GL
  NVGLUframebuffer* nvgFB = NULL;
  int fbFlags = 0;
#endif
  void* swFB = NULL;
  NVGSWUblitter* swBlitter = NULL;

  int swRender = 0;
  int run = 1;
  int blowup = 0;
  int screenshot = 0;
  int premult = 0;
  int testNum = 0;
  int dirty = 1;
  int contFPS = 1;
  int useFramebuffer = 1;
  int showCpuGraph = 0;
  int numThreads = 0;
  int testSet = 0;
  int sRGBaware = 1;
  float shiftx = 0;
  float shifty = 0;
  float scale = 1.0f;
  float radians = 0;
  float angle = 0;
  float fontsize = 24.0f;
  float fontblur = 0;
  double prevt = 0;
  // default file for svg test
  const char* svgFile = DATA_PATH("svg/tiger.svg");  //"svg/Opt_page1.svg");

#ifdef _WIN32
  SetProcessDPIAware();
#endif

  // no OpenGL whatsoever for SW renderer
  SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
  SDL_Init(SDL_INIT_VIDEO);
#ifdef USE_DESKTOP_GL
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
#if defined(GL_ES_VERSION_3_1)  // Android
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
#endif
  //glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_FALSE);  replace swap buffers w/ glFlush();
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);  // SDL docs say this gives speed up on iOS
  SDL_GL_SetAttribute(SDL_GL_RETAINED_BACKING, 0);  // SDL docs say this gives speed up on iOS
  // SDL defaults to RGB565 on iOS - I think we want more quality (e.g. for smooth gradients)
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  //SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

  int nvgFlags = NVG_NO_FONTSTASH;  //NVG_NO_FB_FETCH  NVG_AUTOW_DEFAULT  NVGSW_PATHS_XC
#ifndef NDEBUG
  nvgFlags |= NVGL_DEBUG;
#endif
  for(int argi = 1; argi < argc; ++argi) {
    if(strcmp(argv[argi], "--flags") == 0 && ++argi < argc)
      nvgFlags = strtoul(argv[argi], 0, 0);  // support hex
    else if(strcmp(argv[argi], "--orflags") == 0 && ++argi < argc)
      nvgFlags |= strtoul(argv[argi], 0, 0);
    else if(strcmp(argv[argi], "--sdf") == 0 && ++argi < argc)
      nvgFlags |= atoi(argv[argi]) ? NVG_SDF_TEXT : 0;
    else if(strcmp(argv[argi], "--srgb") == 0 && ++argi < argc)
      sRGBaware = atoi(argv[argi]);
    else if(strcmp(argv[argi], "--fb") == 0 && ++argi < argc)
      useFramebuffer = atoi(argv[argi]);
    else if(strcmp(argv[argi], "--sw") == 0 && ++argi < argc)
      swRender = atoi(argv[argi]);
    else if(strcmp(argv[argi], "--fps") == 0 && ++argi < argc)
      contFPS = atoi(argv[argi]);
    else if(strcmp(argv[argi], "--threads") == 0 && ++argi < argc)
      numThreads = atoi(argv[argi]);
    else if(strcmp(argv[argi], "--test") == 0 && ++argi < argc)
      testSet = atoi(argv[argi]);
    else if(strcmp(argv[argi], "--svg") == 0 && ++argi < argc) {
      svgFile = argv[argi];
      testNum = 3;  // if svg file specified, show it immediately
    }
  }
  if(sRGBaware) {
    nvgFlags |= NVG_SRGB;
    fbFlags |= NVG_IMAGE_SRGB;
    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, swRender ? 0 : 1);  // needed for sRGB on iOS
  }

  sdlWindow = SDL_CreateWindow("NanoVG SDL", 0, 0, 1000, 600,
      SDL_WINDOW_RESIZABLE|SDL_WINDOW_MAXIMIZED|(swRender == 1 ? 0 : SDL_WINDOW_OPENGL)|SDL_WINDOW_ALLOW_HIGHDPI);
  if (!sdlWindow) {
    SDL_Quit();
    return -1;
  }
  double countToSec = 1.0/SDL_GetPerformanceFrequency();

  // estimate DPI
  SDL_Rect dispBounds;
  int disp = SDL_GetWindowDisplayIndex(sdlWindow);
  SDL_GetDisplayBounds(disp < 0 ? 0 : disp, &dispBounds);
  // 12.3in diag (Surface Pro) => 10.2in width; 14 in diag (X1 yoga) => 12.2in width
  float DPI = (dispBounds.h > dispBounds.w ? dispBounds.h : dispBounds.w)/11.2f;

  // SW renderer
  SDL_Surface* sdlSurface = NULL;
  if(swRender != 0) {
    vg = nvgswCreate(nvgFlags);
#ifndef NO_THREADING
    if(numThreads == 0)
      numThreads = numCPUCores();  // * (PLATFORM_MOBILE ? 1 : 2)
    if(numThreads > 1) {
      int xthreads = dispBounds.h > dispBounds.w ? 2 : numThreads/2;  // prefer square-like tiles
      poolInit(numThreads);
      nvgswSetThreading(vg, xthreads, numThreads/xthreads, poolSubmit, poolWait);
    }
#endif
    if(swRender == 1) {
      sdlSurface = SDL_GetWindowSurface(sdlWindow);
      SDL_PixelFormat* fmt = sdlSurface->format;
      // have to set pixel format before loading any images
      nvgswSetFramebuffer(vg, sdlSurface->pixels, sdlSurface->w, sdlSurface->h, fmt->Rshift, fmt->Gshift, fmt->Bshift, 24);
    } else if(swRender == 2) {
      sdlContext = SDL_GL_CreateContext(sdlWindow);
  #ifdef __glad_h_
      gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress);
  #endif
      swBlitter = nvgswuCreateBlitter();
      nvgswSetFramebuffer(vg, NULL, 800, 800, 0, 8, 16, 24);
    }
  } else {
#ifdef NVG_GL
    sdlContext = SDL_GL_CreateContext(sdlWindow);
#ifdef __glad_h_
    gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress);
#elif defined(GLEW_VERSION)
    glewInit();
#endif
    //SDL_GL_SetSwapInterval(0);  -- no effect on iOS

    vg = nvglCreate(nvgFlags);
    if (vg == NULL) {
      printf("Could not init nanovg.\n");
      return -1;
    }

    // drawing to screen on iOS is accomplished with EAGLContext::presentRenderbuffer - there is no default
    //  framebuffer (FB 0 is "incomplete"); SDL creates a framebuffer for us, so we need to keep track of it
    // FB handle can also be obtained from SDL_GetWindowWMInfo
    //GLint fbSDL = 0;
    //glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbSDL);

    if(useFramebuffer) {
      nvgFB = nvgluCreateFramebuffer(vg, 0, 0, NVGLU_NO_NVG_IMAGE | fbFlags);
      nvgluSetFramebufferSRGB(fbFlags & NVG_IMAGE_SRGB);
    }
#endif
  }
  nvgSetFontStash(vg, createFontstash(nvgFlags, 2*48));

  // Android: copy assets out of APK
#if 0 //defined __ANDROID__
  const char* assets[] = {"svg/tiger.svg" };

  for(int ii = 0; ii < sizeof(assets)/sizeof(assets[0]); ++ii) {
    SDL_RWops *io = SDL_RWFromFile(assets[ii], "rb");
    if (io != NULL) {
        char name[256];
        if (SDL_RWread(io, name, sizeof (name), 1) > 0) {
            printf("Hello, %s!\n", name);
        }
        SDL_RWclose(io);
    }
  }
#endif

  int imgflags = (fbFlags & NVG_IMAGE_SRGB);  // | NVG_IMAGE_NEAREST;
  if (loadDemoData(vg, &demoData, imgflags) == -1)
    NVG_LOG("Error loading demo data in ../example/: please run from build directory.\n");  //return -1;

  int img1 = nvgCreateImage(vg, DATA_PATH("dither.png"), imgflags);

  //nvgCreateFont(vg, "menlo", "../../../.fonts/Menlo-Regular.ttf");
  //nvgCreateFont(vg, "mono", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf");
  //nvgCreateFont(vg, "fallback", "../../../temp/DroidSansFallbackFull.ttf");
  nvgAtlasTextThreshold(vg, 48.0f);  // initialize atlas

  GPUtimer gpuTimer;
  if(!swRender)
    initGPUTimer(&gpuTimer);
  gpuTimer.supported = 0;  // disable GPU graph initially

  double t0 = SDL_GetPerformanceCounter()*countToSec;
  prevt = t0;

  while (run && !g_hasGLError)
  {
    double t, dt, cpudt;
    int mx, my;
    int winWidth, winHeight;
    int fbWidth = 0, fbHeight = 0;
    float pxRatio;
    int prevFBO;

    if(dirty) {
      initGraph(&fpsGraph, GRAPH_RENDER_FPS, "Frame Time");
      initGraph(&gpuGraph, GRAPH_RENDER_MS, "GPU Time");
      initGraph(&cpuGraph, GRAPH_RENDER_MS, "CPU Time");
    }

    t = SDL_GetPerformanceCounter()*countToSec;
    dt = t - prevt;
    prevt = t;
    updateGraph(&fpsGraph, dt);
    //NVG_LOG("Frame time: %f ms; CPU time before nvgEndFrame: %f ms\n", dt*1000, cpudt*1000);
    if(gpuTimer.supported)
      startGPUTimer(&gpuTimer);

    SDL_GetMouseState(&mx, &my);
    SDL_GetWindowSize(sdlWindow, &winWidth, &winHeight);
    // Calculate pixel ratio for hi-dpi devices.
    pxRatio = 1.0f;  //(float)fbWidth / (float)winWidth;

    if (swRender == 0) {
#ifdef NVG_GL
      SDL_GL_GetDrawableSize(sdlWindow, &fbWidth, &fbHeight);
      if(nvgFB) {
        prevFBO = nvgluBindFramebuffer(nvgFB);
        nvgluSetFramebufferSize(nvgFB, fbWidth, fbHeight, fbFlags);
      }
      // Update and render
      nvgluSetViewport(0, 0, fbWidth, fbHeight);
      if (premult)
        nvgluClear(nvgRGBAf(0, 0, 0, 0));
      else if(nvgFlags & NVG_SRGB)
        nvgluClear(nvgRGBAf(1.0f, 1.0f, 1.0f, 0.0f));  //nvgluClear(nvgRGBAf(0.1f, 0.1f, 0.1f, 0.0f));
      else
        nvgluClear(nvgRGBAf(0.3f, 0.3f, 0.3f, 0.0f));
#endif
    } else if (swRender == 1) {
      sdlSurface = SDL_GetWindowSurface(sdlWindow);  // has to be done every frame in case window resizes
      fbWidth = sdlSurface->w;
      fbHeight = sdlSurface->h;
      SDL_PixelFormat* fmt = sdlSurface->format;
      nvgswSetFramebuffer(vg, sdlSurface->pixels, fbWidth, fbHeight, fmt->Rshift, fmt->Gshift, fmt->Bshift, 24);
      SDL_FillRect(sdlSurface, NULL, SDL_MapRGB(sdlSurface->format, 255, 255, 255));
      SDL_LockSurface(sdlSurface);
    } else if (swRender == 2) {
      SDL_GL_GetDrawableSize(sdlWindow, &fbWidth, &fbHeight);
      if(!swFB || fbWidth != swBlitter->width || fbHeight != swBlitter->height)
        swFB = realloc(swFB, fbWidth*fbHeight*4);
      memset(swFB, (int)(0.3f*255), fbWidth*fbHeight*4);
      nvgswSetFramebuffer(vg, swFB, fbWidth, fbHeight, 0, 8, 16, 24);
    }

    // if we want to use win dimensions (i.e. physical units) instead of framebuffer dimensions (pixels), so
    //  that nanovg handles all the DPI scaling, we'll need to address the use of gl_FragCoord, which is
    //  always in pixels of course
    // if(dirty || !reuseFrame) -- reusing frame doesn't seem to increase FPS - as long as CPU time is less
    //  than GPU time, I guess they are just parallelized?
    nvgBeginFrame(vg, fbWidth, fbHeight, pxRatio);

    nvgScale(vg, scale, scale);
    nvgTranslate(vg, shiftx, shifty);
    nvgRotate(vg, radians);

    //renderDemo(vg, mx,my, fbWidth, fbHeight, t - t0, blowup, &data);  //t = 16.180936; //50*mx/winWidth;
    //renderingTests(vg);
    //noAATest(vg);
    //gammaTests(vg);
    //gammaTests2(vg, testNum);
    //svgTest(vg, "Opt_page1.svg", fbWidth, fbHeight);

    //fillRect(vg, 100, 100, 200, 200, nvgRGBA(0,0,255,128));
    //nvgShapeAntiAlias(vg, 0);
    //fillRect(vg, 100, 400, 200, 200, nvgRGBA(0,0,255,128));
    //fillRect(vg, 250, 550, 200, 200, nvgRGBA(0,0,255,128));
    //fillRect(vg, 250, 250, 200, 200, nvgRGBA(0,0,255,128));

    //textTest2(vg, testNum);
    if(testSet % 3 == 2) {
      nvgShapeAntiAlias(vg, 0);
      bigPathsTest(vg, 20, 20, fbWidth, fbHeight);
      nvgShapeAntiAlias(vg, 1);
      //nvgFillColor(vg, nvgRGBA(0, 0, 255, 128));
      //nvgScissor(vg, 100, 100, 200, 200);
      //NVGpaint paint = nvgLinearGradient(vg, 50, 50, 50, 1200, nvgRGBA(255, 0, 0, 255), nvgRGBA(0, 0, 255, 255));
      //NVGpaint paint = nvgImagePattern(vg, 100, 100, 200, 200, 0.0f, demoData.images[0], 1.0f);
      //nvgTranslate(vg, 500, 500);
      //nvgBeginPath(vg);
      //nvgRect(vg, 50, 50, 600, 1200);
      //nvgFillPaint(vg, paint);
      //nvgFill(vg);
      //nvgResetScissor(vg);
    }
    else if(testSet % 3 == 1)
      textPerformance(vg, testNum, fontsize, fontblur);
    else {
      // this is our standard performance test set
      if(testNum % 4 == 0) {
        if (swRender == 0)
          nvgluClear(nvgRGBAf(0.3f, 0.3f, 0.3f, 0.0f));
        float s = (PLATFORM_MOBILE ? (fbWidth > fbHeight ? fbWidth : fbHeight)/11.2f : DPI)/192.f;
        nvgScale(vg, s, s);
        renderDemo(vg, mx, my, fbWidth/s, fbHeight/s, t - t0, blowup, &demoData);
      }
      else if(testNum % 4 == 1)
        bigPathsTest(vg, 5, 4, fbWidth, fbHeight);
      else if(testNum % 4 == 2)
        smallPathsTest(vg, fbWidth, fbHeight);
      else
        svgTest(vg, svgFile, fbWidth, fbHeight);
    }

    nvgResetTransform(vg);
    nvgTranslate(vg, 0, fbHeight - 100);  // move to bottom away from status bar
    nvgScale(vg, 2, 2);  // make FPS graph bigger
    if(contFPS) {
      renderGraph(vg, 5, 5, &fpsGraph);
      if(gpuTimer.supported)
        renderGraph(vg, 5+200+5, 5, &gpuGraph);
      if(showCpuGraph)
        renderGraph(vg, 5+200+5+200+5, 5, &cpuGraph);
    }

    cpudt = SDL_GetPerformanceCounter()*countToSec - prevt;
    updateGraph(&cpuGraph, cpudt);

    //glEnable(GL_SCISSOR_TEST);  //glScissor(0, 0, 400, 200);  //glDisable(GL_SCISSOR_TEST);
    nvgEndFrame(vg);

    if(gpuTimer.supported) {
      float gpuTimes[3];
      int n = stopGPUTimer(&gpuTimer, gpuTimes, 3);
      for (int i = 0; i < n; i++)
        updateGraph(&gpuGraph, gpuTimes[i]);
    }

#ifdef NVG_GL
    if(nvgFB)
      nvgluBlitFramebuffer(nvgFB, prevFBO);  // blit to prev FBO and rebind it
#endif

    if (screenshot) {
      screenshot = 0;
      saveScreenShot(fbWidth, fbHeight, premult, "dump.png");
    }

    if (swRender == 0) {
      SDL_GL_SwapWindow(sdlWindow);
    } else if (swRender == 1) {
      SDL_UnlockSurface(sdlSurface);
      SDL_UpdateWindowSurface(sdlWindow);
    } else if (swRender == 2) {
      nvgswuBlit(swBlitter, swFB, fbWidth, fbHeight, 0, 0, fbWidth, fbHeight);
      SDL_GL_SwapWindow(sdlWindow);
    }
    //glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, (GLenum[]){GL_COLOR});  //???

    dirty = 0;
    SDL_Event event;
    // use loop to empty event queue before rendering frame, so that user's most recent input is accounted for
    while(run && ((dirty || contFPS) ? SDL_PollEvent(&event) : SDL_WaitEvent(&event))) {
      if (event.type == SDL_QUIT)
        run = 0;
      else if (event.type == SDL_KEYDOWN) {
        SDL_Keycode key = event.key.keysym.sym;
        float step = event.key.keysym.mod & KMOD_ALT ? 0.01f : 0.1f;
        if(key == SDLK_ESCAPE)
          run = 0;
        else if(key == SDLK_SPACE)
          blowup = !blowup;
        else if (key == SDLK_s)
          screenshot = 1;
        else if (key == SDLK_p)
          premult = !premult;
        else if (key == SDLK_t) {
          if(event.key.keysym.mod & KMOD_SHIFT) ++testSet;
          else ++testNum;
        }
        else if (key == SDLK_g)
          gpuTimer.supported = !gpuTimer.supported && !swRender;
        else if (key == SDLK_c)
          showCpuGraph = !showCpuGraph;
        else if (key == SDLK_LEFTBRACKET)
          radians += step;
        else if (key == SDLK_RIGHTBRACKET)
          radians -= step;
        else if (key == SDLK_EQUALS)
          scale += step;
        else if (key == SDLK_MINUS)
          scale -= step;
        else if (key == SDLK_RIGHT)
          shiftx += step;
        else if (key == SDLK_LEFT)
          shiftx -= step;
        else if (key == SDLK_DOWN)
          shifty += step;
        else if (key == SDLK_UP)
          shifty -= step;
        else if (key == SDLK_PAGEDOWN)
          angle += step;
        else if (key == SDLK_PAGEUP)
          angle -= step;
        else if (key == SDLK_QUOTE) {
          if(event.key.keysym.mod & KMOD_SHIFT) fontblur += step;
          else fontsize += 1;
        }
        else if (key == SDLK_SEMICOLON){
          if(event.key.keysym.mod & KMOD_SHIFT) fontblur -= step;
          else fontsize -= 1;
        }
        else if (key == SDLK_F12)
          contFPS = !contFPS;
        else if (key == SDLK_HOME) {
          shiftx = 0;
          shifty = 0;
          scale = 1;
          radians = 0;
        }
        else
          continue;
#if PLATFORM_MOBILE
        if(SDL_IsTextInputActive())
          SDL_StopTextInput();
#endif
      }
      else if(event.type == SDL_WINDOWEVENT) {
        if(event.window.event != SDL_WINDOWEVENT_EXPOSED && event.window.event != SDL_WINDOWEVENT_MOVED
            && event.window.event != SDL_WINDOWEVENT_RESIZED)
          continue;
      }
      else if(event.type == SDL_FINGERDOWN) {// || event.type == SDL_MOUSEBUTTONDOWN)
#if PLATFORM_MOBILE
        if(!SDL_IsTextInputActive())
          SDL_StartTextInput();
#endif
        //++testNum;
      }
      else
        continue;
      dirty = 1;
    }

  } // end while(run)
  // cleanup
  freeDemoData(vg, &demoData);
  swRender ? nvgswDelete(vg) : nvglDelete(vg);
#ifdef NVG_GL
  if(nvgFB)
    nvgluDeleteFramebuffer(nvgFB);
  if(sdlContext)
    SDL_GL_DeleteContext(sdlContext);
#endif
  SDL_Quit();
  return 0;
}
