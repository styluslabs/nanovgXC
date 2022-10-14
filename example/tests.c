// usage: #include "tests.c"

int g_hasGLError = 0;
#ifndef NDEBUG
int _checkGLError(const char* call, const char* file, int line)
{
  GLenum err;
  const char* error = NULL;
  while((err = glGetError()) != GL_NO_ERROR) {
    switch(err) {
      case GL_INVALID_OPERATION:  error="INVALID_OPERATION";  break;
      case GL_INVALID_ENUM:  error="INVALID_ENUM";  break;
      case GL_INVALID_VALUE:  error="INVALID_VALUE";  break;
      case GL_OUT_OF_MEMORY:  error="OUT_OF_MEMORY";  break;
      case GL_INVALID_FRAMEBUFFER_OPERATION:  error="INVALID_FRAMEBUFFER_OPERATION";  break;
      default: error = "???";
    }
    fprintf(stderr, "GL_%s - %s at %s:%d\n", error, call, file, line);
  }
  return error != NULL;
}

// ... an easier way to track down errors is `export MESA_DEBUG=1` - see https://www.mesa3d.org/envvars.html
#define checkGLError(s) _checkGLError(#s, __FILE__, __LINE__)
#define GL_CHECK(stmt) do { stmt; g_hasGLError = checkGLError(#stmt) || g_hasGLError; } while(0)
#else
#define GL_CHECK(stmt) stmt
#endif

static void fillRect(NVGcontext* vg, float x, float y, float w, float h, NVGcolor color)
{
  nvgFillColor(vg, color);
  nvgBeginPath(vg);
  nvgRect(vg, x, y, w, h);
  nvgFill(vg);
}

static void renderingTests(NVGcontext* vg)
{
  fillRect(vg, 50, 50, 100, 400, nvgRGBA(0,255,0,128));
  fillRect(vg, 200, 500, 100, 100, nvgRGBA(0,0,255,128));

  //1334 x 750 (667 x 375)
  nvgFillColor(vg, nvgRGBA(255,255,0,255));
  nvgText(vg, 50, 50, "50,50", NULL);
  nvgText(vg, 200, 200, "200,200", NULL);
  nvgText(vg, 300, 600, "300, 600", NULL);

  nvgFillColor(vg, nvgRGBA(255,255,255,255));
  nvgBeginPath(vg);
  nvgRect(vg, 50, 50, 1000, 1000);
  nvgFill(vg);

  //nvgRotate(vg, 30*NVG_PI/180.0f);
  nvgFillColor(vg, nvgRGBA(0,0,255,255));
  nvgBeginPath(vg);
  nvgRect(vg, 100, 100.5, 200, 2);
  nvgRect(vg, 100, 105.5, 200, 1.5);
  nvgRect(vg, 100, 110.5, 200, 1);
  nvgRect(vg, 100, 115.5, 200, 0.75);
  nvgRect(vg, 100, 120.5, 200, 0.5);
  nvgRect(vg, 100, 125.5, 200, 0.25);
  nvgFill(vg);

  nvgShapeAntiAlias(vg, 0);
  nvgBeginPath(vg);
  nvgRect(vg, 310, 100.52f, 200, 2);
  nvgRect(vg, 310, 105.52f, 200, 1.5);
  nvgRect(vg, 310, 110.52f, 200, 1);
  nvgRect(vg, 310, 115.52f, 200, 0.75);
  nvgRect(vg, 310, 120.52f, 200, 0.5);
  nvgRect(vg, 310, 125.52f, 200, 0.25);
  nvgFill(vg);

  nvgBeginPath(vg);
  nvgRect(vg, 520, 100.00f, 200, 1);
  nvgRect(vg, 520, 105.01f, 200, 1);
  nvgRect(vg, 520, 110.49f, 200, 1);
  nvgRect(vg, 520, 115.50f, 200, 1);
  nvgRect(vg, 520, 120.51f, 200, 1);
  nvgRect(vg, 520, 125.99f, 200, 1);
  nvgFill(vg);

  nvgBeginPath(vg);
  nvgRect(vg, 730, 100.000f, 200, 1.01f);
  nvgRect(vg, 730, 105.010f, 200, 1.01f);
  nvgRect(vg, 730, 110.495f, 200, 1.01f);
  nvgRect(vg, 730, 115.500f, 200, 1.01f);
  nvgRect(vg, 730, 120.510f, 200, 1.01f);
  nvgRect(vg, 730, 125.990f, 200, 1.01f);
  nvgFill(vg);
  nvgShapeAntiAlias(vg, 1);


  nvgTranslate(vg, 100, 100);
  nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
  nvgBeginPath(vg);
  nvgRect(vg, -5, -5, 100, 100);
  nvgFill(vg);

  nvgFillColor(vg, nvgRGBA(0,0,0,255));
  nvgBeginPath(vg);
  nvgRect(vg,  0, 0, 2, 2);  // critical value between 99.998 and 99.9975
  nvgRect(vg,  5, 0, 1.5, 1.5);
  nvgRect(vg, 10, 0, 1, 1);
  nvgRect(vg, 15, 0, 0.75, 0.75);
  nvgRect(vg, 20, 0, 0.5, 0.5);
  nvgRect(vg, 25, 0, 0.25, 0.25);

  nvgRect(vg,  0.5, 5.5, 2, 2);
  nvgRect(vg,  5.5, 5.5, 1.5, 1.5);
  nvgRect(vg, 10.5, 5.5, 1, 1);
  nvgRect(vg, 15.5, 5.5, 0.75, 0.75);
  nvgRect(vg, 20.5, 5.5, 0.5, 0.5);
  nvgRect(vg, 25.5, 5.5, 0.25, 0.25);
  nvgFill(vg);


  nvgBeginPath(vg);
  nvgRect(vg, 40, -0.01f, 2, 2);
  nvgRect(vg, 45, 0, 1.5, 1.5);
  nvgRect(vg, 50, 0, 1, 1);
  nvgRect(vg, 55, 0, 0.75, 0.75);
  nvgRect(vg, 60, 0, 0.5, 0.5);
  nvgRect(vg, 65, 0, 0.25, 0.25);
  nvgFill(vg);
}

static void noAATest(NVGcontext* vg)
{
  //nvgTranslate(vg, 800, 0);
  //nvgShapeAntiAlias(vg, 0);
  //performanceTests(vg, fbWidth, fbHeight);
  //nvgShapeAntiAlias(vg, 1);

  nvgShapeAntiAlias(vg, 0);
  nvgFillColor(vg, nvgRGBA(255,255,255,255));
  nvgBeginPath(vg);
  nvgRect(vg, 0, 0, 400, 400);
  nvgFill(vg);

  nvgTranslate(vg, 100.5f, 100.5f);
  nvgFillColor(vg, nvgRGBA(255,0,0,128));
  nvgPathWinding(vg, NVG_CCW);
  nvgBeginPath(vg);
  nvgRect(vg, 0, 0, 100, 100);
  nvgFill(vg);

  nvgFillColor(vg, nvgRGBA(0,255,0,128));
  nvgPathWinding(vg, NVG_CW);
  nvgBeginPath(vg);
  nvgRect(vg, 100, 0, 100, 100);
  nvgFill(vg);

  nvgFillColor(vg, nvgRGBA(0,0,255,128));
  nvgPathWinding(vg, NVG_CW);
  nvgBeginPath(vg);
  nvgRect(vg, 0, 100, 100, 100);
  nvgFill(vg);

  nvgFillColor(vg, nvgRGBA(255,255,0,128));
  nvgPathWinding(vg, NVG_CCW);
  nvgBeginPath(vg);
  nvgRect(vg, 100, 100, 100, 100);
  nvgFill(vg);
  nvgShapeAntiAlias(vg, 1);
}

// gamma correction: best match between 50/50 B&W dither (1-pixel checkerboard) and #7F7F7F linear
//  (= #BABABA sRGB) fill is w/ gamma = 2.2 or sRGB framebuffer; w/ sRGB FB, we just have to be sure to
//  load images into sRGB textures; w/o correction (gamma = 1), the fill is much darker than the 50/50 dither
// To use sRGB framebuffer, pass NVG_IMAGE_SRGB to nvgluCreateFramebuffer and nvgluSetFramebufferSize and
//  call glEnable(GL_FRAMEBUFFER_SRGB) to enable automatic conversion
//type == 2 ? vec4(pow(result.a*result.rgb + (1.0f - result.a), vec3(1.0/2.2)), 1.0f) : result;  //result;
static void gammaTests(NVGcontext* vg)
{
  static int imgw, imgh;
  static int img = -1;
  static NVGpaint imgpaint;
  if(img < 0) {
    img = nvgCreateImage(vg, DATA_PATH("dither.png"), NVG_IMAGE_SRGB);
    //int img = nvgCreateImage(vg, "../../../temp/woodgrain.jpg", NVG_IMAGE_SRGB);
    nvgImageSize(vg, img, &imgw, &imgh);
    imgpaint = nvgImagePattern(vg, 0, 0, imgw, imgh, 0, img, 1.0f);
  }

  // 50/50 B/W dither vs. #7F7F7F rectangle
  nvgBeginPath(vg);
  nvgRect(vg, 0, 0, imgw, imgh);
  nvgFillPaint(vg, imgpaint);
  nvgFill(vg);

  fillRect(vg, 25, 300, 150, 150, nvgRGBA(255,255,255,255));
  fillRect(vg, 25, 300, 150, 150, nvgRGBA(0,0,0,128));
  fillRect(vg, 200, 300, 150, 150, nvgRGBA(128,128,128,255));
  fillRect(vg, 375, 300, 150, 150, nvgRGBA(188,188,188,255));

  //    nvgStrokeWidth(vg, 1.5f);
  //    nvgBeginPath(vg);
  //    nvgMoveTo(vg, 100, 100);
  //    nvgLineTo(vg, 500, 120);
  //    nvgStroke(vg);

  //    nvgBeginPath(vg);
  //    nvgMoveTo(vg, 100, 150);
  //    nvgLineTo(vg, 500, 160);
  //    nvgStroke(vg);


  //    nvgTranslate(vg, 0.4f, 0.4f);
  //    nvgFillColor(vg, nvgRGBA(255,255,0,128));
  //    nvgShapeAntiAlias(vg, 0);
  //    //nvgFillRule(vg, NVG_EVENODD);
  //    nvgBeginPath(vg);

  //    nvgMoveTo(vg, 100, 100);
  //    nvgLineTo(vg, 200, 100);
  //    nvgLineTo(vg, 200, 200);
  //    nvgLineTo(vg, 100, 200);
  //    nvgClosePath(vg);

  //    nvgMoveTo(vg, 150, 150);
  //    nvgLineTo(vg, 150, 250);
  //    nvgLineTo(vg, 250, 250);
  //    nvgLineTo(vg, 250, 150);
  //    nvgClosePath(vg);

  //    nvgFill(vg);
  //    nvgShapeAntiAlias(vg, 1);
}

static void gammaTests2(NVGcontext* vg, int testNum)
{
  float sw = (testNum%4)*0.5f + 0.5f;
  nvgFillColor(vg, nvgRGBA(0,0,0,255));
  nvgStrokeColor(vg, nvgRGBA(255,255,255,255));

  //nvgFillColor(vg, nvgRGBA(255,255,255,255));
  //nvgStrokeColor(vg, nvgRGBA(0,0,0,255));
  nvgBeginPath(vg);
  nvgRect(vg, 0, 0, 800, 800);
  nvgFill(vg);

  // shallow angle lines
  nvgStrokeWidth(vg, sw);
  //nvgBeginPath(vg);
  for(int ii = -10; ii <= 10; ++ii) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, 100, 400 + 2*ii);
    nvgLineTo(vg, 700, 400 + 20*ii);
    nvgStroke(vg);
  }
  //nvgStroke(vg);

  nvgTranslate(vg, 0, 800);
  nvgFillColor(vg, nvgRGBA(0,0,0,255));
  nvgStrokeColor(vg, nvgRGBA(255,255,255,255));
  nvgBeginPath(vg);
  nvgRect(vg, 0, 0, 800, 800);
  nvgFill(vg);

  //nvgTranslate(vg, -400, 0);
  //nvgStrokePaint(vg, nvgLinearGradient(vg, 100, 0, 700, 0, nvgRGBA(0,0,0,255), nvgRGBA(255,255,255,255)));

  nvgStrokeWidth(vg, 0.05f);  //(testNum%4)*0.125f + 0.125f);
  nvgBeginPath(vg);
  nvgMoveTo(vg, 100.5, 280);
  nvgLineTo(vg, 100.5, 250);
  //nvgMoveTo(vg, 102.5, 280);
  //nvgLineTo(vg, 102.5, 250);
  //nvgMoveTo(vg,  98.5, 280);
  //nvgLineTo(vg,  98.5, 250);
  nvgStroke(vg);

  //nvgTranslate(vg, 0, -800);
  float dx = (testNum%10)*0.01f;
  nvgFillColor(vg, nvgRGBA(255,255,255,255));
  nvgBeginPath(vg);
  nvgMoveTo(vg, 100-dx, 400 - 2);
  nvgLineTo(vg, 100+dx, 400);
  nvgLineTo(vg, 700, 300 - 18);
  nvgLineTo(vg, 700, 300 - 20);
  nvgClosePath(vg);
  nvgFill(vg);

  /*
  // shallow angle lines
  nvgStrokeWidth(vg, sw);
  //nvgBeginPath(vg);
  for(int ii = -1; ii <= -1; ++ii) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, 100, 400 + 2*ii);
    nvgLineTo(vg, 700, 400 + 20*ii);
    nvgStroke(vg);
  }
  //nvgStroke(vg);
   */
}

// some small differences for convex fill vs. normal, but doesn't seem like a big problem
static void convexFillTest(NVGcontext* vg)
{
  nvgShapeAntiAlias(vg, 0);

  fillRect(vg, 100, 100, 200, 2, nvgRGBA(0,0,0,255));
  fillRect(vg, 100, 200, 200, 1, nvgRGBA(0,0,0,255));
  fillRect(vg, 100, 300, 200, 0.75, nvgRGBA(0,0,0,255));
  fillRect(vg, 100, 400, 200, 0.51, nvgRGBA(0,0,0,255));
  fillRect(vg, 100, 500, 200, 0.5, nvgRGBA(0,0,0,255));
  fillRect(vg, 100, 500, 200, 0.49, nvgRGBA(0,0,0,255));

  // won't use convex fill - all on same path
  nvgFillColor(vg, nvgRGBA(0,0,0,255));
  nvgBeginPath(vg);
  nvgRect(vg, 350, 100, 200, 2);
  nvgRect(vg, 350, 200, 200, 1);
  nvgRect(vg, 350, 300, 200, 0.75);
  nvgRect(vg, 350, 400, 200, 0.51);
  nvgRect(vg, 350, 500, 200, 0.5);
  nvgRect(vg, 350, 500, 200, 0.49);
  nvgFill(vg);
}

// nanosvg
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

static struct NVGcolor nvgColUint(unsigned int col)
{
  struct NVGcolor c = {col};
  //c.r = (col & 0xff) / 255.0f;
  //c.g = ((col >> 8) & 0xff) / 255.0f;
  //c.b = ((col >> 16) & 0xff) / 255.0f;
  //c.a = ((col >> 24) & 0xff) / 255.0f;
  return c;
}

static NVGpaint linearGradToNVGpaint(NVGcontext* vg, NSVGgradient* g)
{
  // we only support simple (2-stop) gradients
  NSVGgradientStop* s0 = &g->stops[0];
  NSVGgradientStop* s1 = &g->stops[1];
  float* t = g->xform;
  float x0 = t[4], y0 = t[5], dx = t[2], dy = t[3];
  return nvgLinearGradient(vg, x0 + s0->offset*dx, y0 + s0->offset*dy,
      x0 + s1->offset*dx, y0 + s1->offset*dy, nvgColUint(s0->color), nvgColUint(s1->color));
}

static void nvgTransformArray(struct NVGcontext* vg, float* t)
{
  nvgTransform(vg, t[0], t[1], t[2], t[3], t[4], t[5]);
}

static void nsvgRender(NVGcontext* vg, NSVGimage* image, float bounds[4])
{
  static int nsvgCap[] = {NVG_BUTT, NVG_ROUND, NVG_SQUARE};
  static int nsvgJoin[] = {NVG_MITER, NVG_ROUND, NVG_BEVEL};

  float w = bounds[2] - bounds[0];
  float h = bounds[3] - bounds[1];
  float s = nsvg__minf(w / image->width, h / image->height);
  nvgSave(vg);
  // map SVG viewBox to window
  nvgTransform(vg, s, 0, 0, s, bounds[0], bounds[1]);
  // now map SVG content to SVG viewBox (by applying viewBox transform)
  nvgTransformArray(vg, image->viewXform);
  for(NSVGshape* shape = image->shapes; shape != NULL; shape = shape->next) {
    if(shape->fill.type == NSVG_PAINT_NONE && shape->stroke.type == NSVG_PAINT_NONE) continue;
    if(shape->paths) {
      nvgBeginPath(vg);
      for(NSVGpath* path = shape->paths; path != NULL; path = path->next) {
        nvgMoveTo(vg, path->pts[0], path->pts[1]);
        for (int i = 1; i < path->npts-1; i += 3) {
          float* p = &path->pts[i*2];
          if(fabsf((p[2] - p[0]) * (p[5] - p[1]) - (p[4] - p[0]) * (p[3] - p[1])) < 0.001f)
            nvgLineTo(vg, p[4],p[5]);
          else
            nvgBezierTo(vg, p[0],p[1], p[2],p[3], p[4],p[5]);
        }
        if(path->closed)
          nvgClosePath(vg);
        nvgPathWinding(vg, NVG_AUTOW);
      }
      if(shape->fill.type != NSVG_PAINT_NONE) {
        if(shape->fill.type == NSVG_PAINT_COLOR)
          nvgFillColor(vg, nvgColUint(shape->fill.color));  // fill.color includes opacity
        else if(shape->fill.type == NSVG_PAINT_LINEAR_GRADIENT)
          nvgFillPaint(vg, linearGradToNVGpaint(vg, shape->fill.gradient));
        nvgFill(vg);
      }
      if (shape->stroke.type == NSVG_PAINT_COLOR) {
        nvgStrokeColor(vg, nvgColUint(shape->stroke.color));
        nvgStrokeWidth(vg, shape->strokeWidth);
        nvgLineCap(vg, nsvgCap[(int)shape->strokeLineCap]);
        nvgLineJoin(vg, nsvgJoin[(int)shape->strokeLineJoin]);
        nvgStroke(vg);
      }
    }
  }
  nvgRestore(vg);
}

#ifdef __ANDROID__
#include "Opt_page1.c"
#endif

static void svgTest(NVGcontext* vg, const char* filename, int fbWidth, int fbHeight)
{
  static NSVGimage* image = NULL;

#ifdef __ANDROID__
  if(!image) {
    char* str = malloc(strlen(Opt_page1_svg) + 1);
    strcpy(str, Opt_page1_svg);
    image = nsvgParse(str, "px", 96.0f); //nsvgParseFromFile(filename, "px", 96.0f);
    free(str);
  }
#else
  //static const char* filename = DATA_PATH("Opt_page1.svg");  //argc > 1 ? argv[1] :
  if(!image) {
    image = nsvgParseFromFile(filename, "px", 96.0f);
    if(!image) {
      NVG_LOG("Error loading %s\n", filename);
      exit(-1);
    }
  }
#endif

  //uint64_t t0 = SDL_GetPerformanceCounter();
  float bounds[4] = {0, 0, fbWidth, fbHeight};
  nsvgRender(vg, image, bounds);
  //fprintf(stderr, "svgTest setup took %f ms\n", (SDL_GetPerformanceCounter() - t0)/1E6);

  //static NSVGrasterizer* rast = NULL;
  //static unsigned char* img = NULL;
  //if(!img) img = malloc(fbWidth*fbHeight*4);
  //if(!rast) rast = nsvgCreateRasterizer();
  //nsvgRasterize(rast, image, 0,0,1, img, fbWidth, fbHeight, fbWidth*4);
}

// benchmark results moved to end of file
static void smallPathsTest(NVGcontext* vg, int fbWidth, int fbHeight)
{
  //uint64_t t0 = SDL_GetPerformanceCounter();
  nvgFillColor(vg, nvgRGBA(0,0,255,128));
  for(int y = 0; y < fbHeight; y += 20) {  // was fbHeight
    for(int x = 0; x < fbWidth; x += 20) {  // was fbWidth
      nvgBeginPath(vg);
      //nvgTranslate(vg, -x, -y);  -- to see if offseting to origin of winding texture would help w/ cache performance
      nvgMoveTo(vg, x, y);
      for(int dy = 0; dy < 50; dy += 3) {
        nvgLineTo(vg, x + 4*(dy%2), y + dy);
      }
      nvgLineTo(vg, x + 8, y + 50);
      nvgLineTo(vg, x + 8, y);
      nvgClosePath(vg);
      nvgFill(vg);
    }
  }
  //fprintf(stderr, "smallPaths setup took %f ms\n", (SDL_GetPerformanceCounter() - t0)/1E6);
}

static void bigPathsTest(NVGcontext* vg, int xpaths, int ypaths, int fbWidth, int fbHeight)
{
  int xstep = (fbWidth - 650)/xpaths;
  int ystep = (fbHeight - 1250)/ypaths;
  nvgFillColor(vg, nvgRGBA(0,0,255,128));
  for(int ii = 0; ii < xpaths; ++ii) {
    for(int jj = 0; jj < ypaths; ++jj) {
      nvgBeginPath(vg);
      nvgRect(vg, 50 + xstep*ii, 50 + ystep*jj, 600, 1200);
      nvgFill(vg);
    }
  }
}

static int freadall(const char* filename, char** bufferout)
{
  FILE* f = fopen(filename, "rb");
  if(!f)
    return 0;
  // obtain file size
  fseek(f, 0, SEEK_END);
  int size = ftell(f);
  rewind(f);
  char* buf = (char*)malloc(size+1);
  if(!buf)
    return 0;
  *bufferout = buf;
  // copy the file into the buffer:
  int bytesread = fread(buf, 1, size, f);
  fclose(f);
  buf[bytesread] = '\0';
  return bytesread;
}

static void textPerformance(NVGcontext* vg, int testNum, float fontsize, float fontblur)
{
  int asPaths = testNum % 2;
  //int invert = testNum % 2;

  nvgAtlasTextThreshold(vg, asPaths ? 0.0f : 48.0f);
  nvgFontFace(vg, "sans");
  nvgFontHeight(vg, fontsize);
  nvgFontBlur(vg, fontblur);
  nvgFillColor(vg, nvgRGBA(0,0,0,255));
//  if(testNum % 3 == 0)
//    nvgFillColor(vg, nvgRGBA(0,0,0,255));    // B on W
//  else if(testNum % 3 == 1) {
//    fillRect(vg, 0, 0, 1400, 1400, nvgRGBA(0,255,0,255));
//    nvgFillColor(vg, nvgRGBA(255,255,255,255));  // W on green
//  }
//  else {
//    fillRect(vg, 0, 0, 1400, 1400, nvgRGBA(0,0,0,255));
//    nvgFillColor(vg, nvgRGBA(255,255,255,255));  // W on B
//  }
  //const char* longstr = "WXYZIALMTV WXYZIALMTV WXYZIALMTV WXYZIALMTV WXYZIALMTV WXYZIALMTV WXYZIALMTV WXYZIALMTV WXYZIALMTV WXYZIALMTV WXYZIALMTV";
  char longstr[96];
  for(char c = 32; c < 127; ++c)
    longstr[(int)c-32] = c;
  longstr[95] = '\0';

  for(int y = 20; y < 2200; y += fontsize - 4)
    nvgText(vg, 250, y, longstr, NULL);
  // reset blur
  nvgFontBlur(vg, 0);
}

static void textTests(NVGcontext* vg)
{
  /*
   unsigned char c = 0;  //testNum % 2 ? 255 : 0;
   int asPaths = testNum % 2;

   nvgAtlasTextThreshold(vg, asPaths ? 0.0f : 24.0f);
   //nvgTextLetterSpacing(vg, 6.0f);
   nvgTranslate(vg, fbWidth/2, fbHeight/2);
   nvgScale(vg, 1.0f + shiftx, 1.0f + shifty);
   nvgRotate(vg, angle);
   //nvgTranslate(vg, shiftx, shifty);
   nvgFontFace(vg, "menlo");
   nvgFontHeight(vg, fontsize);
   textPerformance(vg);
   nvgTextLetterSpacing(vg, 0.0f);
   */

  /*
   fillRect(vg, 0, 100, 300, 100, nvgRGBA(c,c,c,255));
   fillRect(vg, 0, 0, 300, 100, nvgRGBA(255-c,255-c,255-c,255));

   nvgFillColor(vg, nvgRGBA(c,c,c,255));
   asPaths ? drawText(vg, 50+shiftx, 90, fontsize, "123ABC") :  nvgText(vg, 50+shiftx, 90, "123ABC", NULL);

   nvgFillColor(vg, nvgRGBA(255-c,255-c,255-c,255));
   asPaths ? drawText(vg, 50+shiftx, 130, fontsize, "123ABC") : nvgText(vg, 50+shiftx, 130, "123ABC", NULL);

   fillRect(vg, 0, 200, 800, 500, nvgRGBA(c,c,c,255));

   nvgStrokeColor(vg, nvgRGBA(255-c,255-c,255-c,255));
   float sw = 0.5f;
   for(int ii = -10; ii <= 10; ++ii) {
   nvgStrokeWidth(vg, sw);
   nvgBeginPath(vg);
   nvgMoveTo(vg, 100, 400 + 2*ii);
   nvgLineTo(vg, 700, 400 + 20*ii);
   nvgStroke(vg);
   sw += 0.1f;
   }
   */

  /*
   fillRect(vg, 0, 0, 100, 100, nvgRGBA(0,0,0,64));
   fillRect(vg, 100, 0, 100, 100, nvgRGBA(0,0,0,128));
   fillRect(vg, 200, 0, 100, 100, nvgRGBA(0,0,0,192));
   fillRect(vg, 0, 150, 300, 100, nvgRGBA(0,0,0,255));
   fillRect(vg, 0, 150, 100, 150, nvgRGBA(255,255,255,192));
   fillRect(vg, 100, 150, 100, 150, nvgRGBA(255,255,255,128));
   fillRect(vg, 200, 150, 100, 150, nvgRGBA(255,255,255,64));
   */

   fillRect(vg, 0, 350, 300, 100, nvgRGBA(0,0,0,255));

   nvgFontFace(vg, "sans");
   nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
   nvgFontHeight(vg, 12.0f);
   nvgText(vg, 10, 340, "123ABC", NULL);
   nvgFontHeight(vg, 20.0f);
   nvgText(vg, 50, 340, "123ABC", NULL);
   nvgFontHeight(vg, 48.0f);
   nvgText(vg, 150, 340, "123ABC", NULL);


   nvgFillColor(vg, nvgRGBA(255,255,255,255));
   nvgFontHeight(vg, 12.0f);
   nvgText(vg, 10, 380, "123ABC", NULL);
   nvgFontHeight(vg, 20.0f);
   nvgText(vg, 50, 380, "123ABC", NULL);
   nvgFontHeight(vg, 48.0f);
   nvgText(vg, 150, 380, "123ABC", NULL);
}

static void textTest2(NVGcontext* vg, int testNum)
{
  int asPaths = testNum % 2;
  nvgAtlasTextThreshold(vg, asPaths ? 0.0f : 48.0f);
  nvgFontFace(vg, "sans");
  nvgFillColor(vg, nvgRGBA(0,0,0,255));
  char longstr[96];
  for(char c = 32; c < 127; ++c)
    longstr[(int)c-32] = c;
  longstr[95] = '\0';

  float fonth = 6;
  for(int y = 20; y < 1400; y += 1.25*fonth) {
    nvgFontHeight(vg, fonth);
    nvgText(vg, 250, y, longstr, NULL);
    fonth += 0.5;
  }

  //fillRect(vg, 50, 50, 600, 600, nvgRGBA(0, 0, 0, 255));
  //nvgFillColor(vg, nvgRGBA(255,255,255,255));
  //nvgFontFace(vg, "sans");
  //nvgFontHeight(vg, 48);  //24 + (2*testNum)%30);
  //nvgText(vg, 100, 300, "^ . M # ' / W i j", NULL);
  //textPerformance(vg, testNum, fontsize);

  //if(testNum % 2) {
  //  nvgAtlasTextThreshold(vg, 0.0f);
  //  textTests(vg);
  //} else {
  //  nvgAtlasTextThreshold(vg, 48.0f);
  //  textTests(vg);
  //}
}

static void scissorTest(NVGcontext* vg)
{
//    if(testNum % 2) {
//      fillRect(vg, 0, 0, 300, 300, nvgRGBA(255,255,255,255));
//      fillRect(vg, 0, 0, 300, 300, nvgRGBA(0,0,0,255));
//      glEnable(GL_SCISSOR_TEST);
//      glScissor(100, fbHeight - 200, 1, 100);
//      nvgEndFrame(vg);
//      glDisable(GL_SCISSOR_TEST);
//    }
//    else {
//      nvgScissor(vg, 100, 100, 1, 100);
//      fillRect(vg, 0, 0, 300, 300, nvgRGBA(255,255,255,255));
//      fillRect(vg, 0, 0, 300, 300, nvgRGBA(0,0,0,255));
//      nvgEndFrame(vg);
//    }
}

static void linearGradTest(NVGcontext* vg)
{
  float m[6];
  nvgCurrentTransform(vg, &m[0]);
  nvgTranslate(vg, 100, 100);
  nvgScale(vg, 600, 100);
  nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, 1, 0, nvgRGBA(255,0,0,255), nvgRGBA(0,0,255,255)));
  nvgResetTransform(vg);
  nvgTransform(vg, m[0], m[1], m[2], m[3], m[4], m[5]);
  nvgBeginPath(vg);
  nvgRect(vg, 100, 100, 700, 200);
  nvgFill(vg);
}

static void imageTest(NVGcontext* vg, int imgHandle, float x, float y, float w, float h)
{
  int imgw, imgh;
  nvgImageSize(vg, imgHandle, &imgw, &imgh);
  if(w <= 0 && h <= 0) {
    w = imgw;
    h = imgh;
  }
  else if(w <= 0) w = imgw*h/imgh;
  else if(h <= 0) h = imgh*w/imgw;
  NVGpaint imgpaint = nvgImagePattern(vg, x, y, w, h, 0, imgHandle, 1.0f);
  nvgBeginPath(vg);
  //nvgRoundedRect(vg, x, y, w, h, 5);
  nvgRect(vg, x, y, w, h);
  nvgFillPaint(vg, imgpaint);
  nvgFill(vg);
}

static void dashTest(NVGcontext* vg)
{
  float dashes[] = {100, 20, 20, 20, -1};
  nvgDashArray(vg, dashes);
  nvgStrokeWidth(vg, 8);
  nvgBeginPath(vg);
  nvgMoveTo(vg, 100, 100);
  nvgLineTo(vg, 600, 100);
  nvgLineTo(vg, 600, 600);
  nvgLineTo(vg, 100, 600);
  //nvgClosePath(vg);
  nvgStroke(vg);
}

static void swRenderTest(NVGcontext* vg)
{
  //nvgScissor(vg, 150, 50, 50, 1000);
//    nvgFillPaint(vg, nvgLinearGradient(vg, 100, 0, 700, 0, nvgRGBA(255,0,0,255), nvgRGBA(0,0,255,255)));
//    nvgBeginPath(vg);
//    nvgRect(vg, 100, 100, 700, 200);
//    nvgFill(vg);

//    imageTest(vg, data.images[0], 400, 350, 600, 600);
//    imageTest(vg, img1, 100, 800, 0, 300);
//    textTests(vg);
//    nvgAtlasTextThreshold(vg, 0.0f);
//    nvgTranslate(vg, 0, 200);
//    textTests(vg);
//    nvgAtlasTextThreshold(vg, 48.0f);
}


/* Benchmarks:
X1 Yoga Gen 6 - Windows (4K full screen):
image atomic: 94 / 19 / 120 / 5 (big / small / Opt_page1 / paris-30k)
 w/ instanced draw: 94 / 19 / 127 / 5
FB fetch: 109 / 8 / 48 / 3.3
SW: 12.9 / 6 / 48 / 2.5(?)

Multithreaded SW renderer (half-screen):

image atomic - SW 1x - 2x - 4x - 8x - 16x
demo 280 - ~20 (varies) - 24 - 30 - 50 - 67
small paths 40 - 15 - 27 - 30 - 42 - 42
tiger 60 - 27 - 40 - 68 - 76 - 81

image atomic vs SW 8x:
paris-30k (half-screen): 7 / 9.4
Opt_page1 (half-screen): 140 / 105

XC 8 threads, big/small/tiger/Opt_page1/paris-30k: 12/50/50/90/8.5

8 threads, half-screen, text/big/small/tiger/Opt_page1/paris-30k:
non-XC:  25/27/32/61/92/7.6  (less than above due to other tasks?)
XC:      52/20/42/47/94/7.7
diff-XC: 55/26/43/60/106/8.0
lim-XC:  53/26/43/78/106/9.3  (diff-XC with per-scanline x limits)

1 thread, Linux VM, no sRGB, big/small/tiger/text:
non-XC: 4.9/6.7/8.3/6.4
XC:     4.9/9.5/8.3/14
diff-XC: 6.5/8.5/11.5/14.5

multi-threaded SW on iPad: some improvement over single thread (best w/ 2 threads) but still slower than GPU
1x: 12.5 / 40 / 17 / 12.4 (small / Opt_page1 / text summed / text paths)
2x: 20.7 / 54 / 30 / 19.4
4x: 19.5 / 50 / 26 / 13.2
FB fetch: 23.5 / 60 (sync limit) / 60 / 31

Below from X1 Yoga Gen 2:

May 2020 benchmarks (SW renderer, areaEdge2):
big = bigPathsTest(), small = smallPathsTest()

glDrawElements (draw-elements branch): 36 floats per segment (6 verts * 3 vec2) -> 16 (4 verts * 2 vec2)
- desktop: maybe slightly faster on paris-30k (4 FPS vs 3.75) ... mostly due to CPU?
- iPad: no difference for small paths test (or big paths test)

iPhone 6S big/small/tiger: 36/33/51; GL_BLEND: 33.7 / 30.6 / 49.2; blend outColor.a: 35.2 / 32 / 50.8

4x larger image for image atomic: 36 FPS (vs 26 FPS) for big paths test; no change for small paths
... significant improvement! paris-30k: 4.2 FPS vs 3.75 (just increase image height in glnvg__renderFlush)
... but no improvement on Intel Xe!

DEBUG=0 performance tests (half screen in Linux VM):
- GL: 37 FPS / 9.2 FPS (big / small)
- SW: 7.2 FPS / 5.8 FPS
- SW w/ tight bounds: 8 FPS / 23 FPS
- Opt_page1: SW: 29 FPS (w/ sRGB - 26 FPS) (sRGB + blend32: 32 FPS) (+ faster sort: 35 FPS)
- valgrind: renderFlush (w/ faster sort): 3457/38 = 90.9/frame

Android (Nexus 5X)
GL, AA, areaEdge: 7.2 / 5.4 / 13.2  (big / small / Opt_page1)
GL, no AA, areaEdge: big paths limited to 60 FPS
GL, AA, areaEdge2: 7.3 / 6.3 / 14.2
(early exit for areaEdge2: minimial difference, wrong anyway)
SW: 2.9 / 5.4 / 17.9

Windows (Image atomic full screen):
areaEdge: 27 / 25 / 52 (big / small / Opt_page1)
areaEdge2: 29 / 26 / 54 ... too noisy to draw conclusions
SW: 6 / 10.6 / 49

Windows w/ 32 bit accums ... no major diff (and FB swap, fetch aren't used by default anyway):
image atomic: 27 / 24 / 57
FB fetch (R32I): 27 / 7.3 / 27
FB swap (R32F): 50 / 12.3 / 38

iPad:
areaEdge: 60 / 15.8 / 56
areaEdge2: 60 / 24 / 60 - 100 big paths: 15 FPS
SW render (GL blit): 9.5 / 13.4 - 100 big paths: 1.94 FPS

iPhone 6S:
NOTE: iPhone FPS depends on battery level! test at 100%!
areaEdge2: 37.3 / 33.8 / 44 (low batt: 19.8 / 20.15 / 24.0)
- R32I: 36.2 / 32.6 / 44
areaEdge: 34.6 / 24.0 / 33  (low batt: 18.1 / 12.2 / 18.5)
- remove convex fill branch: 35.5 / 24 / 33
- remove highp int: 36.2 / 24.6
SW: ... resolution is wrong
retest Jan 6 2020 version:  36.3 (but occasionally 31) / 26.7 (low batt: 31 / 20.9)

Older:

Android performance (Nexus 5X):
FB swap: big paths 9 FPS, small paths (full screen 1080x1920) <<1 FPS
Image load/store (not atomic - incorrect rendering): big paths 9 FPS, small paths 5.5 FPS
Image atomic (not correct, but looks OK): big ~6 FPS, small ~6 FPS
- Opt_page_1.svg: 12 - 14 FPS  (same as non-atomic load/store); nsvgrast: ~2 FPS
Image atomic + 1x glMemoryBarrier (still wrong...): big 7.7 FPS, small 3.2 FPS, Opt_page1: 8.2 FPS
Image atomic batched (correct): big 7.2 FPS, small 5.7 FPS, Opt_page1: 11.3 - 13.6 FPS

Desktop (make sure nothing else is using CPU to compare!):
FB swap: big paths 50 FPS, small paths (full screen 2560x1440) 10.5 FPS
FB fetch: 27 FPS / 6.5 FPS
Image atomic + 2x glMemoryBarrier (1x barrier is WRONG): 37 FPS / 12.5 FPS
Image atomic batched: 27 FPS / 27 FPS
Opt_page1: FB swap 38 FPS, FB fetch 21 FPS, Image atomic batched 64 FPS
Image atomic + glMemoryBarrier: 37 FPS / 18.5 FPS (w/o barrier, 40 FPS / 33 FPS but incorrect rendering)

Desktop w/ updated Intel Graphics 26.20.100.6913 (old version was 21.20.16.4727)
image atomic: 31 FPS / 26.5 FPS
FB fetch (--flags 32): 32 FPS / 7 FPS
FB swap (--flags 48): 53 FPS / 12 FPS

more iPhone optimization (Nov 2019):
- note NVG_DEBUG not set//
Baseline FPS (big; small stuck at 33.33): 29.55-29.70
No scissor for accumulated pass: 31.41
Disable gradient support: 37.24
- innerCol != outerCol: 33.90
- add another case in main(): 36.80 (decrease vs. 37.24 is due to extra case)
... final: 36.80
Disable scissor: 38.9
Precision highp sampler2D: 37.0
GL_R32I accum: 36.14 (/2^20); 36.8 (unstable) (floatBitsToInt)
Uniform buffer: 37.5 (faster), 28.X (slower, unstable)Small paths w/ fbWidth,fbHeight:
Baseline: 25.0 FPS ... 26.0 ... not stable
UBO: 24.4
Remove setUniforms: 26.1

iPad: big: 60 FPS; small (full screen): 18.8 FPS
expand by 0.55 instead of 0.5: 16.5 FPS ... but if we add a translate(0.25, 0.25) so that extra 0.05
doesn't cover more pixels, both 0.50 and 0.55 give 18.5 FPS
2 Mar 2020: big w/ 100 paths: 14.4 FPS
- R32I accum: big (100x): 14.2 FPS; small 18.8 FPS (vs. 19 FPS w/ R16F)

using fbWidth, fbHeight (instead of fixed values):
Win 10 host (GL3):
- framebuffer fetch: 8 FPS; 75 ms total time - CPU time per frame
- framebuffer swap: 10 FPS; 0 ms total time - CPU time (probably because framebuffer swap forces flush); 90 ms GPU time (GL_TIME_ELAPSED)
- old nanovg: 11 FPS, but only 20 ms GPU time
iPhone 6S (1334x750):
- FB fetch w/ single RGBA16F attachment: 20.75 FPS
- areaEdge = 1.0f: 21.5 FPS; coverage = 1.0f: 21.8FPS (seems CPU limited)
- FB swap: < 1 FPS

iPhone 6S:
- FB fetch w/ single RGBA16F attachment: 25 FPS
 - areaEdge = 1.0f: 35 FPS; coverage = 1.0f: 25 FPS
- FB fetch w/ RGBA16F color + 16F winding: 25.2 FPS
- FB fetch w/ RGBA8 color + 16F winding: 24.7 FPS
 - w/ GL_BLEND enabled: paths don't appear; 24.5 FPS
 - removing type == 0 case: 24.8 FPS (note FPS numbers are very stable)
 - remove scissor and gradient (just result = innerCol): 36 FPS (!)
 - areaEdge = 1.0: 31.4 FPS; coverage = 1.0: 31.5 FPS
- FB fetch w/ RGBA8 color + 16I winding (GL_BLEND enabled): 22.5 FPS
- FB swap: 25 FPS (but demo is only 11 FPS)
*/
