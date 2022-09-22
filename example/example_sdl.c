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
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"
#include "nanovg_sw.h"
#include "nanovg_sw_utils.h"
#include "demo.h"
#include "perf.h"
#define NVG_GL 1

#ifndef _WIN32
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include "tests.c"

#ifndef NO_THREADING
#include "threadpool.h"
#endif

int SDL_main(int argc, char* argv[])
{
  DemoData data;
  PerfGraph fps;
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
  int useFramebuffer = 0;
  int numThreads = 0;
  int testSet = 0;
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

  initGraph(&fps, GRAPH_RENDER_FPS, "Frame Time");

  int nvgFlags = NVG_SRGB;  //NVG_NO_FB_FETCH); //NVG_DEBUG); // | NVG_AUTOW_DEFAULT);
  for(int argi = 1; argi < argc; ++argi) {
    if(strcmp(argv[argi], "--flags") == 0 && ++argi < argc)
      nvgFlags = atoi(argv[argi]);
    if(strcmp(argv[argi], "--sw") == 0 && ++argi < argc)
      swRender = atoi(argv[argi]);
    if(strcmp(argv[argi], "--fps") == 0 && ++argi < argc)
      contFPS = atoi(argv[argi]);
    if(strcmp(argv[argi], "--threads") == 0 && ++argi < argc)
      numThreads = atoi(argv[argi]);
    if(strcmp(argv[argi], "--test") == 0 && ++argi < argc)
      testSet = atoi(argv[argi]);
    if(strcmp(argv[argi], "--svg") == 0 && ++argi < argc)
      svgFile = argv[argi];
  }
  if(nvgFlags & NVG_SRGB) {
    fbFlags |= NVG_IMAGE_SRGB;
    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, swRender ? 0 : 1);  // needed for sRGB on iOS
  }

  sdlWindow = SDL_CreateWindow("NanoVG SDL", 0, 0, 1000, 600,
      SDL_WINDOW_RESIZABLE|SDL_WINDOW_MAXIMIZED|(swRender ? 0 : SDL_WINDOW_OPENGL)|SDL_WINDOW_ALLOW_HIGHDPI);
  if (!sdlWindow) {
    SDL_Quit();
    return -1;
  }
  double countToSec = 1.0/SDL_GetPerformanceFrequency();

  // SW renderer
  SDL_Surface* sdlSurface = NULL;
  if(swRender != 0) {
    vg = nvgswCreate(nvgFlags);
#ifndef NO_THREADING
    if(numThreads == 0)
      numThreads = numCPUCores();
    if(numThreads > 1) {
      poolInit(numThreads);
      nvgswSetThreading(vg, numThreads/2, 2, poolSubmit, poolWait);
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

    useFramebuffer = 1;
    if(useFramebuffer) {
      nvgFB = nvgluCreateFramebuffer(vg, 0, 0, NVGLU_NO_NVG_IMAGE | fbFlags);
      nvgluSetFramebufferSRGB(fbFlags & NVG_IMAGE_SRGB);
    }
#endif
  }

  int imgflags = (fbFlags & NVG_IMAGE_SRGB);  // | NVG_IMAGE_NEAREST;
  if (loadDemoData(vg, &data, imgflags) == -1)
    printf("Error loading demo data in ../example/: please run from build directory.\n");  //return -1;

  int img1 = nvgCreateImage(vg, DATA_PATH("dither.png"), imgflags);

  //nvgCreateFont(vg, "menlo", "../../../.fonts/Menlo-Regular.ttf");
  //nvgCreateFont(vg, "mono", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf");
  //nvgCreateFont(vg, "fallback", "../../../temp/DroidSansFallbackFull.ttf");
  nvgAtlasTextThreshold(vg, 48.0f);  // initialize atlas

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

    t = SDL_GetPerformanceCounter()*countToSec;
    dt = t - prevt;
    prevt = t;
    updateGraph(&fps, dt);
    //NVG_LOG("Frame time: %f ms; CPU time before nvgEndFrame: %f ms\n", dt*1000, cpudt*1000);

    SDL_GetMouseState(&mx, &my);
    SDL_GetWindowSize(sdlWindow, &winWidth, &winHeight);
    // Calculate pixel ratio for hi-dpi devices.
    pxRatio = 1.0f;  //(float)fbWidth / (float)winWidth;

    if (swRender == 0) {
#ifdef NVG_GL
      SDL_GL_GetDrawableSize(sdlWindow, &fbWidth, &fbHeight);
      if(useFramebuffer) {
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
    if(testSet == 2)
      bigPathsTest(vg, 1, 1, fbWidth, fbHeight);
    else if(testSet == 1)
      textPerformance(vg, testNum, fontsize, fontblur);
    else {
      // this is our standard performance test set
      if (testNum % 4 == 0)
        renderDemo(vg, mx,my, fbWidth, fbHeight, t - t0, blowup, &data);
      else if (testNum % 4 == 1)
        bigPathsTest(vg, 5, 4, fbWidth, fbHeight);
      else if (testNum % 4 == 2)
        smallPathsTest(vg, fbWidth, fbHeight);
      else
        svgTest(vg, svgFile, fbWidth, fbHeight);
    }

    nvgResetTransform(vg);
    nvgTranslate(vg, 0, fbHeight - 50);  // move to bottom away from status bar
    if(contFPS)
      renderGraph(vg, 5,5, &fps);

    cpudt = SDL_GetPerformanceCounter()*countToSec - prevt;
    //glEnable(GL_SCISSOR_TEST);
    //glScissor(0, 0, 400, 200);  //fbHeight - 200, 400, 200);
    nvgEndFrame(vg);
    //glDisable(GL_SCISSOR_TEST);

#ifdef NVG_GL
    if (useFramebuffer)
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
          ++testNum;
          initGraph(&fps, GRAPH_RENDER_FPS, "Frame Time");  // reset FPS graph
        }
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
      }
      else if(event.type == SDL_WINDOWEVENT) {
        if(event.window.event != SDL_WINDOWEVENT_EXPOSED && event.window.event != SDL_WINDOWEVENT_MOVED
            && event.window.event != SDL_WINDOWEVENT_RESIZED)
          continue;
      }
      else if(event.type == SDL_FINGERDOWN) { // || event.type == SDL_MOUSEBUTTONDOWN)
        ++testNum;
        initGraph(&fps, GRAPH_RENDER_FPS, "Frame Time");
      }
      else
        continue;
      dirty = 1;
    }

  } // end while(run)

  freeDemoData(vg, &data);
#ifdef NVG_GL
  if(useFramebuffer)
    nvgluDeleteFramebuffer(nvgFB);

  if(sdlContext) {
    nvglDelete(vg);
    SDL_GL_DeleteContext(sdlContext);
  }
#endif
  SDL_Quit();
  return 0;
}
