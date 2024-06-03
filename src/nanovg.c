//
// Copyright (c) 2020 Stylus Labs - see LICENSE.txt
//   based on nanovg:
// Copyright (c) 2013 Mikko Mononen memon@inside.org
//
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <memory.h>

#include "nanovg.h"
//#define FONTSTASH_IMPLEMENTATION
#include "fontstash.h"
#include "stb_truetype.h"  // for textAsPaths
//#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifdef _MSC_VER
#pragma warning(disable: 4100)  // unreferenced formal parameter
#pragma warning(disable: 4127)  // conditional expression is constant
#pragma warning(disable: 4204)  // nonstandard extension used : non-constant aggregate initializer
#pragma warning(disable: 4706)  // assignment within conditional expression
#endif

#define NVG_INIT_FONTIMAGE_SIZE  512
#define NVG_MAX_FONTIMAGE_SIZE   2048
#define NVG_MAX_FONTIMAGES       4

#define NVG_INIT_COMMANDS_SIZE 256
#define NVG_INIT_POINTS_SIZE 128
#define NVG_INIT_PATHS_SIZE 16
#define NVG_INIT_VERTS_SIZE 256
#define NVG_MAX_STATES 32

#define NVG_KAPPA90 0.5522847493f	// Length proportional to radius of a cubic bezier handle for 90deg arcs.

#define NVG_COUNTOF(arr) (sizeof(arr) / sizeof(0[arr]))


enum NVGcommands {
  NVG_MOVETO = 0,
  NVG_LINETO = 1,
  NVG_BEZIERTO = 2,
  NVG_CLOSE = 3,  // commands >= NVG_CLOSE do not affect position
  NVG_WINDING = 4,
  NVG_RESTART = 5,
};

struct NVGstate {
  NVGcompositeOperationState compositeOperation;
  int shapeAntiAlias;
  int fillRule;
  NVGpaint fill;
  NVGpaint stroke;
  float strokeWidth;
  float miterLimit;
  int lineJoin;
  int lineCap;
  float* dashArray;
  float dashOffset;
  float alpha;
  float xform[6];
  NVGscissor scissor;
  float scissorBounds[4];
  float fontSize;
  float letterSpacing;
  float lineHeight;
  float fontBlur;
  int textAlign;
  int fontId;
};
typedef struct NVGstate NVGstate;

struct NVGpoint {
  float x,y;
};
typedef struct NVGpoint NVGpoint;

struct NVGpathCache {
  NVGpoint* points;
  int npoints;  // current count
  int cpoints;  // capacity
  NVGpath* paths;
  int npaths;
  int cpaths;
  NVGvertex* verts;
  int nverts;
  int cverts;
  float bounds[4];
};
typedef struct NVGpathCache NVGpathCache;

struct NVGcontext {
  NVGparams params;
  float* commands;
  int ccommands;
  int ncommands;
  float commandx, commandy;
  NVGstate states[NVG_MAX_STATES];
  int nstates;
  NVGpathCache* cache;
  float tessTol;
  float distTol;
  float atlasTextThresh;
  int defaultWinding;
  float devicePxRatio;
  FONScontext* fs;
  int fontImages[NVG_MAX_FONTIMAGES];
  int fontImageIdx;
};

static float nvg__sqrtf(float a) { return sqrtf(a); }
static float nvg__modf(float a, float b) { return fmodf(a, b); }
static float nvg__sinf(float a) { return sinf(a); }
static float nvg__cosf(float a) { return cosf(a); }
static float nvg__tanf(float a) { return tanf(a); }
static float nvg__atan2f(float a,float b) { return atan2f(a, b); }
static float nvg__acosf(float a) { return acosf(a); }

static int nvg__mini(int a, int b) { return a < b ? a : b; }
static int nvg__maxi(int a, int b) { return a > b ? a : b; }
static int nvg__clampi(int a, int mn, int mx) { return a < mn ? mn : (a > mx ? mx : a); }
static float nvg__minf(float a, float b) { return a < b ? a : b; }
static float nvg__maxf(float a, float b) { return a > b ? a : b; }
static float nvg__absf(float a) { return a >= 0.0f ? a : -a; }
static float nvg__signf(float a) { return a >= 0.0f ? 1.0f : -1.0f; }
static float nvg__clampf(float a, float mn, float mx) { return a < mn ? mn : (a > mx ? mx : a); }
static float nvg__cross(float dx0, float dy0, float dx1, float dy1) { return dx1*dy0 - dx0*dy1; }

static float nvg__normalize(float *x, float* y)
{
  float d = nvg__sqrtf((*x)*(*x) + (*y)*(*y));
  if (d > 1e-6f) {
    float id = 1.0f / d;
    *x *= id;
    *y *= id;
  }
  return d;
}


static void nvg__deletePathCache(NVGpathCache* c)
{
  if (c == NULL) return;
  if (c->points != NULL) free(c->points);
  if (c->paths != NULL) free(c->paths);
  if (c->verts != NULL) free(c->verts);
  free(c);
}

static NVGpathCache* nvg__allocPathCache(void)
{
  NVGpathCache* c = (NVGpathCache*)malloc(sizeof(NVGpathCache));
  if (c == NULL) goto error;
  memset(c, 0, sizeof(NVGpathCache));

  c->points = (NVGpoint*)malloc(sizeof(NVGpoint)*NVG_INIT_POINTS_SIZE);
  if (!c->points) goto error;
  c->npoints = 0;
  c->cpoints = NVG_INIT_POINTS_SIZE;

  c->paths = (NVGpath*)malloc(sizeof(NVGpath)*NVG_INIT_PATHS_SIZE);
  if (!c->paths) goto error;
  c->npaths = 0;
  c->cpaths = NVG_INIT_PATHS_SIZE;

  c->verts = (NVGvertex*)malloc(sizeof(NVGvertex)*NVG_INIT_VERTS_SIZE);
  if (!c->verts) goto error;
  c->nverts = 0;
  c->cverts = NVG_INIT_VERTS_SIZE;

  return c;
error:
  nvg__deletePathCache(c);
  return NULL;
}

static void nvg__setDevicePixelRatio(NVGcontext* ctx, float ratio)
{
  ctx->tessTol = 0.25f / ratio;
  ctx->distTol = 0.01f / ratio;
  ctx->devicePxRatio = ratio;
}

// note that src color is multiplied by src alpha in frag shader,
//  so, e.g., "source over" uses GL_ONE instead of GL_SRC_ALPHA
static NVGcompositeOperationState nvg__compositeOperationState(int op)
{
  int sfactor, dfactor;

  if (op == NVG_SOURCE_OVER) {
    sfactor = NVG_ONE;
    dfactor = NVG_ONE_MINUS_SRC_ALPHA;
  } else if (op == NVG_SOURCE_IN) {
    sfactor = NVG_DST_ALPHA;
    dfactor = NVG_ZERO;
  } else if (op == NVG_SOURCE_OUT) {
    sfactor = NVG_ONE_MINUS_DST_ALPHA;
    dfactor = NVG_ZERO;
  } else if (op == NVG_ATOP) {
    sfactor = NVG_DST_ALPHA;
    dfactor = NVG_ONE_MINUS_SRC_ALPHA;
  } else if (op == NVG_DESTINATION_OVER) {
    sfactor = NVG_ONE_MINUS_DST_ALPHA;
    dfactor = NVG_ONE;
  } else if (op == NVG_DESTINATION_IN) {
    sfactor = NVG_ZERO;
    dfactor = NVG_SRC_ALPHA;
  } else if (op == NVG_DESTINATION_OUT) {
    sfactor = NVG_ZERO;
    dfactor = NVG_ONE_MINUS_SRC_ALPHA;
  } else if (op == NVG_DESTINATION_ATOP) {
    sfactor = NVG_ONE_MINUS_DST_ALPHA;
    dfactor = NVG_SRC_ALPHA;
  } else if (op == NVG_LIGHTER) {
    sfactor = NVG_ONE;
    dfactor = NVG_ONE;
  } else if (op == NVG_COPY) {
    sfactor = NVG_ONE;
    dfactor = NVG_ZERO;
  } else if (op == NVG_XOR) {
    sfactor = NVG_ONE_MINUS_DST_ALPHA;
    dfactor = NVG_ONE_MINUS_SRC_ALPHA;
  } else {
    sfactor = NVG_ONE;
    dfactor = NVG_ZERO;
  }

  NVGcompositeOperationState state;
  state.srcRGB = sfactor;
  state.dstRGB = dfactor;
  state.srcAlpha = sfactor;
  state.dstAlpha = dfactor;
  return state;
}

static NVGstate* nvg__getState(NVGcontext* ctx)
{
  return &ctx->states[ctx->nstates-1];
}

NVGcontext* nvgCreateInternal(NVGparams* params)
{
  FONSparams fontParams;
  NVGcontext* ctx = (NVGcontext*)malloc(sizeof(NVGcontext));
  int i;
  if (ctx == NULL) goto error;
  memset(ctx, 0, sizeof(NVGcontext));

  ctx->params = *params;
  for (i = 0; i < NVG_MAX_FONTIMAGES; i++)
    ctx->fontImages[i] = 0;

  ctx->commands = (float*)malloc(sizeof(float)*NVG_INIT_COMMANDS_SIZE);
  if (!ctx->commands) goto error;
  ctx->ncommands = 0;
  ctx->ccommands = NVG_INIT_COMMANDS_SIZE;

  ctx->cache = nvg__allocPathCache();
  if (ctx->cache == NULL) goto error;

  ctx->defaultWinding = params->flags & NVG_AUTOW_DEFAULT ? NVG_AUTOW : NVG_CCW;

  nvgSave(ctx);
  nvgReset(ctx);

  nvg__setDevicePixelRatio(ctx, 1.0f);

  if (ctx->params.renderCreate(ctx->params.userPtr) == 0) goto error;

  // Init font rendering
  if (!(ctx->params.flags & NVG_NO_FONTSTASH)) {
    memset(&fontParams, 0, sizeof(fontParams));
    //fontParams.width = NVG_INIT_FONTIMAGE_SIZE;
    //fontParams.height = NVG_INIT_FONTIMAGE_SIZE;
    fontParams.flags = FONS_ZERO_TOPLEFT | FONS_DELAY_LOAD;
    fontParams.flags |= (ctx->params.flags & NVG_SDF_TEXT) ? FONS_SDF : FONS_SUMMED;
    // these must match values in shader
    fontParams.sdfPadding = 4;
    fontParams.sdfPixelDist = 32.0f;
    ctx->fs = fonsCreateInternal(&fontParams);
    if (ctx->fs == NULL) goto error;
  }
  ctx->fontImageIdx = -1;
  return ctx;

error:
  nvgDeleteInternal(ctx);
  return 0;
}

void nvgSetFontStash(NVGcontext* ctx, FONScontext* fs)
{
  ctx->fs = fs;
}

NVGparams* nvgInternalParams(NVGcontext* ctx)
{
  return &ctx->params;
}

void nvgDeleteInternal(NVGcontext* ctx)
{
  int i;
  if (ctx == NULL) return;
  if (ctx->commands != NULL) free(ctx->commands);
  if (ctx->cache != NULL) nvg__deletePathCache(ctx->cache);

  if (ctx->fs && !(ctx->params.flags & NVG_NO_FONTSTASH))
    fonsDeleteInternal(ctx->fs);

  for (i = 0; i < NVG_MAX_FONTIMAGES; i++) {
    if (ctx->fontImages[i] != 0) {
      nvgDeleteImage(ctx, ctx->fontImages[i]);
      ctx->fontImages[i] = 0;
    }
  }

  if (ctx->params.renderDelete != NULL)
    ctx->params.renderDelete(ctx->params.userPtr);

  free(ctx);
}

static void nvg__freeFontImages(NVGcontext* ctx)
{
  if (ctx->fontImageIdx > 0) {
    // fix from https://github.com/memononen/nanovg/pull/626 has been applied
    int fontImage = ctx->fontImages[ctx->fontImageIdx];
    int i, j, iw, ih;
    // delete images that smaller than current one
    if (fontImage == 0)
      return;
    ctx->fontImages[ctx->fontImageIdx] = 0;
    nvgImageSize(ctx, fontImage, &iw, &ih);
    for (i = j = 0; i < ctx->fontImageIdx; i++) {
      if (ctx->fontImages[i] != 0) {
        int nw, nh;
        int image = ctx->fontImages[i];
        ctx->fontImages[i] = 0;
        nvgImageSize(ctx, image, &nw, &nh);
        if (nw < iw || nh < ih)
          nvgDeleteImage(ctx, image);
        else
          ctx->fontImages[j++] = image;
      }
    }
    // make current font image to first
    ctx->fontImages[j] = ctx->fontImages[0];
    ctx->fontImages[0] = fontImage;
    ctx->fontImageIdx = 0;
  }
}

void nvgBeginFrame(NVGcontext* ctx, float windowWidth, float windowHeight, float devicePixelRatio)
{
  // moved from end of nvgEndFrame()
  nvg__freeFontImages(ctx);
  ctx->nstates = 0;
  nvgSave(ctx);
  nvgReset(ctx);

  nvg__setDevicePixelRatio(ctx, devicePixelRatio);

  ctx->params.renderViewport(ctx->params.userPtr, windowWidth, windowHeight, devicePixelRatio);
}

void nvgCancelFrame(NVGcontext* ctx)
{
  ctx->params.renderCancel(ctx->params.userPtr);
}

void nvgEndFrame(NVGcontext* ctx)
{
  ctx->params.renderFlush(ctx->params.userPtr);
  //nvg__freeFontImages(ctx);
}

// Color
NVGcolor nvgRGB(unsigned char r, unsigned char g, unsigned char b) { return nvgRGBA(r,g,b,255); }
NVGcolor nvgRGBf(float r, float g, float b) { return nvgRGBAf(r,g,b,1.0f); }
NVGcolor nvgTransRGBA(NVGcolor c, unsigned char a) { c.a = a; return c; }
NVGcolor nvgTransRGBAf(NVGcolor c, float a) { c.a = (unsigned char)(a*255.0f + 0.5f); return c; }
NVGcolor nvgHSL(float h, float s, float l) { return nvgHSLA(h,s,l,255); }

NVGcolor nvgRGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
  NVGcolor color;
  // Use longer initialization to suppress warning.
  color.r = r;  // / 255.0f;
  color.g = g;  // / 255.0f;
  color.b = b;  // / 255.0f;
  color.a = a;  // / 255.0f;
  return color;
}

float nvgSRGBtoLinear(unsigned char c)
{
  //float c = srgb / 255.0f;
  //return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);  -- from sRGB definition
  // from stb_image_resize.h
  static float sRGBtoLinearLUT[256] = {
    0.000000f, 0.000304f, 0.000607f, 0.000911f, 0.001214f, 0.001518f, 0.001821f, 0.002125f, 0.002428f, 0.002732f, 0.003035f,
    0.003347f, 0.003677f, 0.004025f, 0.004391f, 0.004777f, 0.005182f, 0.005605f, 0.006049f, 0.006512f, 0.006995f, 0.007499f,
    0.008023f, 0.008568f, 0.009134f, 0.009721f, 0.010330f, 0.010960f, 0.011612f, 0.012286f, 0.012983f, 0.013702f, 0.014444f,
    0.015209f, 0.015996f, 0.016807f, 0.017642f, 0.018500f, 0.019382f, 0.020289f, 0.021219f, 0.022174f, 0.023153f, 0.024158f,
    0.025187f, 0.026241f, 0.027321f, 0.028426f, 0.029557f, 0.030713f, 0.031896f, 0.033105f, 0.034340f, 0.035601f, 0.036889f,
    0.038204f, 0.039546f, 0.040915f, 0.042311f, 0.043735f, 0.045186f, 0.046665f, 0.048172f, 0.049707f, 0.051269f, 0.052861f,
    0.054480f, 0.056128f, 0.057805f, 0.059511f, 0.061246f, 0.063010f, 0.064803f, 0.066626f, 0.068478f, 0.070360f, 0.072272f,
    0.074214f, 0.076185f, 0.078187f, 0.080220f, 0.082283f, 0.084376f, 0.086500f, 0.088656f, 0.090842f, 0.093059f, 0.095307f,
    0.097587f, 0.099899f, 0.102242f, 0.104616f, 0.107023f, 0.109462f, 0.111932f, 0.114435f, 0.116971f, 0.119538f, 0.122139f,
    0.124772f, 0.127438f, 0.130136f, 0.132868f, 0.135633f, 0.138432f, 0.141263f, 0.144128f, 0.147027f, 0.149960f, 0.152926f,
    0.155926f, 0.158961f, 0.162029f, 0.165132f, 0.168269f, 0.171441f, 0.174647f, 0.177888f, 0.181164f, 0.184475f, 0.187821f,
    0.191202f, 0.194618f, 0.198069f, 0.201556f, 0.205079f, 0.208637f, 0.212231f, 0.215861f, 0.219526f, 0.223228f, 0.226966f,
    0.230740f, 0.234551f, 0.238398f, 0.242281f, 0.246201f, 0.250158f, 0.254152f, 0.258183f, 0.262251f, 0.266356f, 0.270498f,
    0.274677f, 0.278894f, 0.283149f, 0.287441f, 0.291771f, 0.296138f, 0.300544f, 0.304987f, 0.309469f, 0.313989f, 0.318547f,
    0.323143f, 0.327778f, 0.332452f, 0.337164f, 0.341914f, 0.346704f, 0.351533f, 0.356400f, 0.361307f, 0.366253f, 0.371238f,
    0.376262f, 0.381326f, 0.386430f, 0.391573f, 0.396755f, 0.401978f, 0.407240f, 0.412543f, 0.417885f, 0.423268f, 0.428691f,
    0.434154f, 0.439657f, 0.445201f, 0.450786f, 0.456411f, 0.462077f, 0.467784f, 0.473532f, 0.479320f, 0.485150f, 0.491021f,
    0.496933f, 0.502887f, 0.508881f, 0.514918f, 0.520996f, 0.527115f, 0.533276f, 0.539480f, 0.545725f, 0.552011f, 0.558340f,
    0.564712f, 0.571125f, 0.577581f, 0.584078f, 0.590619f, 0.597202f, 0.603827f, 0.610496f, 0.617207f, 0.623960f, 0.630757f,
    0.637597f, 0.644480f, 0.651406f, 0.658375f, 0.665387f, 0.672443f, 0.679543f, 0.686685f, 0.693872f, 0.701102f, 0.708376f,
    0.715694f, 0.723055f, 0.730461f, 0.737911f, 0.745404f, 0.752942f, 0.760525f, 0.768151f, 0.775822f, 0.783538f, 0.791298f,
    0.799103f, 0.806952f, 0.814847f, 0.822786f, 0.830770f, 0.838799f, 0.846873f, 0.854993f, 0.863157f, 0.871367f, 0.879622f,
    0.887923f, 0.896269f, 0.904661f, 0.913099f, 0.921582f, 0.930111f, 0.938686f, 0.947307f, 0.955974f, 0.964686f, 0.973445f,
    0.982251f, 0.991102f, 1.0f
  };

  return sRGBtoLinearLUT[c];
}

NVGcolor nvgRGBAf(float r, float g, float b, float a)
{
  NVGcolor color;
  // Use longer initialization to suppress warning.
  color.r = (unsigned char)(r*255.0f + 0.5f);  // r;
  color.g = (unsigned char)(g*255.0f + 0.5f);  // g;
  color.b = (unsigned char)(b*255.0f + 0.5f);  // b;
  color.a = (unsigned char)(a*255.0f + 0.5f);  // a;
  return color;
}

NVGcolor nvgLerpRGBA(NVGcolor c0, NVGcolor c1, float u)
{
  int i;
  float oneminu;
  NVGcolor cint = {{0}};

  u = nvg__clampf(u, 0.0f, 1.0f);
  oneminu = 1.0f - u;
  for (i = 0; i < 4; i++)
    cint.rgba[i] = (unsigned char)((c0.rgba[i] * oneminu + c1.rgba[i] * u) + 0.5f);

  return cint;
}

static float nvg__hue(float h, float m1, float m2)
{
  if (h < 0) h += 1;
  if (h > 1) h -= 1;
  if (h < 1.0f/6.0f)
    return m1 + (m2 - m1) * h * 6.0f;
  else if (h < 3.0f/6.0f)
    return m2;
  else if (h < 4.0f/6.0f)
    return m1 + (m2 - m1) * (2.0f/3.0f - h) * 6.0f;
  return m1;
}

NVGcolor nvgHSLA(float h, float s, float l, unsigned char a)
{
  float m1, m2, r, g, b;
  h = nvg__modf(h, 1.0f);
  if (h < 0.0f) h += 1.0f;
  s = nvg__clampf(s, 0.0f, 1.0f);
  l = nvg__clampf(l, 0.0f, 1.0f);
  m2 = l <= 0.5f ? (l * (1 + s)) : (l + s - l * s);
  m1 = 2 * l - m2;
  r = nvg__clampf(nvg__hue(h + 1.0f/3.0f, m1, m2), 0.0f, 1.0f);
  g = nvg__clampf(nvg__hue(h, m1, m2), 0.0f, 1.0f);
  b = nvg__clampf(nvg__hue(h - 1.0f/3.0f, m1, m2), 0.0f, 1.0f);
  return nvgRGBAf(r, g, b, a/255.0f);
}

// Transforms
void nvgTransformIdentity(float* t)
{
  t[0] = 1.0f; t[1] = 0.0f;
  t[2] = 0.0f; t[3] = 1.0f;
  t[4] = 0.0f; t[5] = 0.0f;
}

void nvgTransformTranslate(float* t, float tx, float ty)
{
  t[0] = 1.0f; t[1] = 0.0f;
  t[2] = 0.0f; t[3] = 1.0f;
  t[4] = tx; t[5] = ty;
}

void nvgTransformScale(float* t, float sx, float sy)
{
  t[0] = sx; t[1] = 0.0f;
  t[2] = 0.0f; t[3] = sy;
  t[4] = 0.0f; t[5] = 0.0f;
}

void nvgTransformRotate(float* t, float a)
{
  float cs = nvg__cosf(a), sn = nvg__sinf(a);
  t[0] = cs; t[1] = sn;
  t[2] = -sn; t[3] = cs;
  t[4] = 0.0f; t[5] = 0.0f;
}

void nvgTransformSkewX(float* t, float a)
{
  t[0] = 1.0f; t[1] = 0.0f;
  t[2] = nvg__tanf(a); t[3] = 1.0f;
  t[4] = 0.0f; t[5] = 0.0f;
}

void nvgTransformSkewY(float* t, float a)
{
  t[0] = 1.0f; t[1] = nvg__tanf(a);
  t[2] = 0.0f; t[3] = 1.0f;
  t[4] = 0.0f; t[5] = 0.0f;
}

void nvgTransformMultiply(float* t, const float* s)
{
  float t0 = t[0] * s[0] + t[1] * s[2];
  float t2 = t[2] * s[0] + t[3] * s[2];
  float t4 = t[4] * s[0] + t[5] * s[2] + s[4];
  t[1] = t[0] * s[1] + t[1] * s[3];
  t[3] = t[2] * s[1] + t[3] * s[3];
  t[5] = t[4] * s[1] + t[5] * s[3] + s[5];
  t[0] = t0;
  t[2] = t2;
  t[4] = t4;
}

void nvgTransformPremultiply(float* t, const float* s)
{
  float s2[6];
  memcpy(s2, s, sizeof(float)*6);
  nvgTransformMultiply(s2, t);
  memcpy(t, s2, sizeof(float)*6);
}

int nvgTransformInverse(float* inv, const float* t)
{
  double invdet, det = (double)t[0] * t[3] - (double)t[2] * t[1];
  if (det > -1e-6 && det < 1e-6) {
    nvgTransformIdentity(inv);
    return 0;
  }
  invdet = 1.0 / det;
  inv[0] = (float)(t[3] * invdet);
  inv[2] = (float)(-t[2] * invdet);
  inv[4] = (float)(((double)t[2] * t[5] - (double)t[3] * t[4]) * invdet);
  inv[1] = (float)(-t[1] * invdet);
  inv[3] = (float)(t[0] * invdet);
  inv[5] = (float)(((double)t[1] * t[4] - (double)t[0] * t[5]) * invdet);
  return 1;
}

void nvgTransformPoint(float* dx, float* dy, const float* t, float sx, float sy)
{
  *dx = sx*t[0] + sy*t[2] + t[4];
  *dy = sx*t[1] + sy*t[3] + t[5];
}

float nvgDegToRad(float deg)
{
  return deg / 180.0f * NVG_PI;
}

float nvgRadToDeg(float rad)
{
  return rad / NVG_PI * 180.0f;
}

static void nvg__setPaintColor(NVGpaint* p, NVGcolor color)
{
  memset(p, 0, sizeof(*p));
  nvgTransformIdentity(p->xform);
  p->radius = 0.0f;
  p->feather = 1.0f;
  p->innerColor = color;
  p->outerColor = color;
}

// State handling
void nvgSave(NVGcontext* ctx)
{
  if (ctx->nstates >= NVG_MAX_STATES)
    return;
  if (ctx->nstates > 0)
    memcpy(&ctx->states[ctx->nstates], &ctx->states[ctx->nstates-1], sizeof(NVGstate));
  ctx->nstates++;
}

void nvgRestore(NVGcontext* ctx)
{
  if (ctx->nstates <= 1)
    return;
  ctx->nstates--;
}

void nvgReset(NVGcontext* ctx)
{
  NVGstate* state = nvg__getState(ctx);
  memset(state, 0, sizeof(*state));

  nvg__setPaintColor(&state->fill, nvgRGBA(255,255,255,255));
  nvg__setPaintColor(&state->stroke, nvgRGBA(0,0,0,255));
  state->compositeOperation = nvg__compositeOperationState(NVG_SOURCE_OVER);
  state->shapeAntiAlias = 1;
  state->fillRule = NVG_NONZERO;
  state->strokeWidth = 1.0f;
  state->miterLimit = 10.0f;
  state->lineCap = NVG_BUTT;
  state->lineJoin = NVG_MITER;
  state->alpha = 1.0f;
  nvgTransformIdentity(state->xform);

  state->scissor.extent[0] = -1.0f;
  state->scissor.extent[1] = -1.0f;

  state->fontSize = 16.0f;
  state->letterSpacing = 0.0f;
  state->lineHeight = 1.0f;
  state->fontBlur = 0.0f;
  state->textAlign = NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE;
  state->fontId = 0;
}

// State setting
void nvgShapeAntiAlias(NVGcontext* ctx, int enabled)
{
  NVGstate* state = nvg__getState(ctx);
  state->shapeAntiAlias = enabled;
}

// fill rule is a path property in Skia and Qt, but a painter state here and in cairo
void nvgFillRule(NVGcontext* ctx, int rule)
{
  NVGstate* state = nvg__getState(ctx);
  state->fillRule = rule;
}

void nvgStrokeWidth(NVGcontext* ctx, float width)
{
  NVGstate* state = nvg__getState(ctx);
  state->strokeWidth = width;
}

void nvgMiterLimit(NVGcontext* ctx, float limit)
{
  NVGstate* state = nvg__getState(ctx);
  state->miterLimit = limit;
}

void nvgLineCap(NVGcontext* ctx, int cap)
{
  NVGstate* state = nvg__getState(ctx);
  state->lineCap = cap;
}

void nvgLineJoin(NVGcontext* ctx, int join)
{
  NVGstate* state = nvg__getState(ctx);
  state->lineJoin = join;
}

void nvgDashArray(NVGcontext* ctx, float* dashes)
{
  NVGstate* state = nvg__getState(ctx);
  state->dashArray = dashes;
}

void nvgDashOffset(NVGcontext* ctx, float offset)
{
  NVGstate* state = nvg__getState(ctx);
  state->dashOffset = offset;
}

void nvgGlobalAlpha(NVGcontext* ctx, float alpha)
{
  NVGstate* state = nvg__getState(ctx);
  state->alpha = alpha;
}

void nvgTransform(NVGcontext* ctx, float a, float b, float c, float d, float e, float f)
{
  NVGstate* state = nvg__getState(ctx);
  float t[6] = { a, b, c, d, e, f };
  nvgTransformPremultiply(state->xform, t);
}

void nvgResetTransform(NVGcontext* ctx)
{
  NVGstate* state = nvg__getState(ctx);
  nvgTransformIdentity(state->xform);
}

void nvgTranslate(NVGcontext* ctx, float x, float y)
{
  NVGstate* state = nvg__getState(ctx);
  float t[6];
  nvgTransformTranslate(t, x,y);
  nvgTransformPremultiply(state->xform, t);
}

void nvgRotate(NVGcontext* ctx, float angle)
{
  NVGstate* state = nvg__getState(ctx);
  float t[6];
  nvgTransformRotate(t, angle);
  nvgTransformPremultiply(state->xform, t);
}

void nvgSkewX(NVGcontext* ctx, float angle)
{
  NVGstate* state = nvg__getState(ctx);
  float t[6];
  nvgTransformSkewX(t, angle);
  nvgTransformPremultiply(state->xform, t);
}

void nvgSkewY(NVGcontext* ctx, float angle)
{
  NVGstate* state = nvg__getState(ctx);
  float t[6];
  nvgTransformSkewY(t, angle);
  nvgTransformPremultiply(state->xform, t);
}

void nvgScale(NVGcontext* ctx, float x, float y)
{
  NVGstate* state = nvg__getState(ctx);
  float t[6];
  nvgTransformScale(t, x,y);
  nvgTransformPremultiply(state->xform, t);
}

void nvgCurrentTransform(NVGcontext* ctx, float* xform)
{
  NVGstate* state = nvg__getState(ctx);
  if (xform == NULL) return;
  memcpy(xform, state->xform, sizeof(float)*6);
}

void nvgStrokeColor(NVGcontext* ctx, NVGcolor color)
{
  NVGstate* state = nvg__getState(ctx);
  nvg__setPaintColor(&state->stroke, color);
}

void nvgStrokePaint(NVGcontext* ctx, NVGpaint paint)
{
  NVGstate* state = nvg__getState(ctx);
  state->stroke = paint;
  nvgTransformMultiply(state->stroke.xform, state->xform);
}

void nvgFillColor(NVGcontext* ctx, NVGcolor color)
{
  NVGstate* state = nvg__getState(ctx);
  nvg__setPaintColor(&state->fill, color);
}

void nvgFillPaint(NVGcontext* ctx, NVGpaint paint)
{
  NVGstate* state = nvg__getState(ctx);
  state->fill = paint;
  nvgTransformMultiply(state->fill.xform, state->xform);
}

int nvgCreateImage(NVGcontext* ctx, const char* filename, int imageFlags)
{
  int w, h, n, image;
  unsigned char* img;
  stbi_set_unpremultiply_on_load(1);
  stbi_convert_iphone_png_to_rgb(1);
  img = stbi_load(filename, &w, &h, &n, 4);
  if (img == NULL) {
//		printf("Failed to load %s - %s\n", filename, stbi_failure_reason());
    return 0;
  }
  image = nvgCreateImageRGBA(ctx, w, h, imageFlags, img);
  stbi_image_free(img);
  return image;
}

int nvgCreateImageMem(NVGcontext* ctx, int imageFlags, unsigned char* data, int ndata)
{
  int w, h, n, image;
  unsigned char* img = stbi_load_from_memory(data, ndata, &w, &h, &n, 4);
  if (img == NULL) {
//		printf("Failed to load %s - %s\n", filename, stbi_failure_reason());
    return 0;
  }
  image = nvgCreateImageRGBA(ctx, w, h, imageFlags, img);
  stbi_image_free(img);
  return image;
}

int nvgCreateImageRGBA(NVGcontext* ctx, int w, int h, int imageFlags, const unsigned char* data)
{
  return ctx->params.renderCreateTexture(ctx->params.userPtr, NVG_TEXTURE_RGBA, w, h, imageFlags, data);
}

void nvgUpdateImage(NVGcontext* ctx, int image, const unsigned char* data)
{
  int w, h;
  ctx->params.renderGetTextureSize(ctx->params.userPtr, image, &w, &h);
  ctx->params.renderUpdateTexture(ctx->params.userPtr, image, 0,0, w,h, data);
}

void nvgImageSize(NVGcontext* ctx, int image, int* w, int* h)
{
  ctx->params.renderGetTextureSize(ctx->params.userPtr, image, w, h);
}

void nvgDeleteImage(NVGcontext* ctx, int image)
{
  ctx->params.renderDeleteTexture(ctx->params.userPtr, image);
}

NVGpaint nvgLinearGradient(NVGcontext* ctx,
    float sx, float sy, float ex, float ey, NVGcolor icol, NVGcolor ocol)
{
  NVGpaint p;
  float dx, dy, d;
  const float large = 1e5;
  NVG_NOTUSED(ctx);
  memset(&p, 0, sizeof(p));
  // Calculate transform aligned to the line
  dx = ex - sx;
  dy = ey - sy;
  d = nvg__sqrtf(dx*dx + dy*dy);
  if (d > 0.0001f) {
    dx /= d;
    dy /= d;
  } else {
    dx = 0;
    dy = 1;
  }
  p.xform[0] = dy; p.xform[1] = -dx;
  p.xform[2] = dx; p.xform[3] = dy;
  p.xform[4] = sx - dx*large; p.xform[5] = sy - dy*large;
  p.extent[0] = large;
  p.extent[1] = large + d*0.5f;
  p.radius = 0.0f;
  p.feather = nvg__maxf(1.0f, d);
  p.innerColor = icol;
  p.outerColor = ocol;
  return p;
}

NVGpaint nvgRadialGradient(NVGcontext* ctx,
    float cx, float cy, float inr, float outr, NVGcolor icol, NVGcolor ocol)
{
  NVGpaint p;
  float r = (inr+outr)*0.5f;
  float f = (outr-inr);
  NVG_NOTUSED(ctx);
  memset(&p, 0, sizeof(p));
  nvgTransformIdentity(p.xform);
  p.xform[4] = cx;
  p.xform[5] = cy;
  p.extent[0] = r;
  p.extent[1] = r;
  p.radius = r;
  p.feather = nvg__maxf(1.0f, f);
  p.innerColor = icol;
  p.outerColor = ocol;
  return p;
}

NVGpaint nvgBoxGradient(NVGcontext* ctx,
    float x, float y, float w, float h, float r, float f, NVGcolor icol, NVGcolor ocol)
{
  NVGpaint p;
  NVG_NOTUSED(ctx);
  memset(&p, 0, sizeof(p));
  nvgTransformIdentity(p.xform);
  p.xform[4] = x+w*0.5f;
  p.xform[5] = y+h*0.5f;
  p.extent[0] = w*0.5f;
  p.extent[1] = h*0.5f;
  p.radius = r;
  p.feather = nvg__maxf(1.0f, f);
  p.innerColor = icol;
  p.outerColor = ocol;
  return p;
}

NVGpaint nvgImagePattern(NVGcontext* ctx,
    float cx, float cy, float w, float h, float angle, int image, float alpha)
{
  NVGpaint p;
  NVG_NOTUSED(ctx);
  memset(&p, 0, sizeof(p));
  nvgTransformRotate(p.xform, angle);
  p.xform[4] = cx;
  p.xform[5] = cy;
  p.extent[0] = w;
  p.extent[1] = h;
  p.image = image;
  p.innerColor = p.outerColor = nvgRGBAf(1,1,1,alpha);
  return p;
}

int nvgMultiGradient(NVGcontext* ctx, int imageFlags, float* stops, NVGcolor* colors, int nstops)
{
  NVGcolor color;
  int pidx, sidx, w = 256, handle = 0;
  float fstep, f = 0;
  unsigned int* img;
  if (nstops < 2) return 0;
  //float mindelta = stops[0] > 0 ? stops[0] : 1;
  //for (sidx = 0; sidx < nstops - 1; ++sidx) {
  //  mindelta = nvg__minf(mindelta, stops[sidx+1] - stops[sidx]);
  //}
  //w = mindelta >= 0.04f ? 256 : mindelta >= 0.02f ? 512 : mindelta >= 0.01f ? 1024 : 2048;
  img = (unsigned int*)malloc(w*4);
  if (img == NULL) return 0;
  fstep = 1.0f/(w - 1);
  for (pidx = 0, sidx = 0; pidx < w; ++pidx) {
    while (f > stops[sidx+1] && sidx < nstops - 2) { ++sidx; }
    color = nvgLerpRGBA(colors[sidx], colors[sidx+1], (f - stops[sidx])/(stops[sidx+1] - stops[sidx]));
    img[pidx] = color.c;
    f += fstep;
  }
  handle = nvgCreateImageRGBA(ctx, w, 1, imageFlags, (unsigned char*)img);
  free(img);
  return handle;
}

// Scissoring
void nvgScissor(NVGcontext* ctx, float x, float y, float w, float h)
{
  NVGstate* state = nvg__getState(ctx);
  float* sxform = state->scissor.xform;
  float ex = nvg__maxf(0.0f, w)*0.5f;
  float ey = nvg__maxf(0.0f, h)*0.5f;
  float tex, tey;

  nvgTransformIdentity(sxform);
  sxform[4] = x+ex;  //w*0.5f;
  sxform[5] = y+ey;  //h*0.5f;
  nvgTransformMultiply(sxform, state->xform);

  state->scissor.extent[0] = ex;  //w*0.5f;
  state->scissor.extent[1] = ey;  //h*0.5f;

  // path bounds will be clipped to AABB of scissor
  tex = ex*nvg__absf(sxform[0]) + ey*nvg__absf(sxform[2]);
  tey = ex*nvg__absf(sxform[1]) + ey*nvg__absf(sxform[3]);
  state->scissorBounds[0] = sxform[4]-tex;
  state->scissorBounds[1] = sxform[5]-tey;
  state->scissorBounds[2] = sxform[4]+tex;
  state->scissorBounds[3] = sxform[5]+tey;
}

static void nvg__isectRects(float* dst,
              float ax, float ay, float aw, float ah,
              float bx, float by, float bw, float bh)
{
  float minx = nvg__maxf(ax, bx);
  float miny = nvg__maxf(ay, by);
  float maxx = nvg__minf(ax+aw, bx+bw);
  float maxy = nvg__minf(ay+ah, by+bh);
  dst[0] = minx;
  dst[1] = miny;
  dst[2] = nvg__maxf(0.0f, maxx - minx);
  dst[3] = nvg__maxf(0.0f, maxy - miny);
}

void nvgIntersectScissor(NVGcontext* ctx, float x, float y, float w, float h)
{
  NVGstate* state = nvg__getState(ctx);
  float pxform[6], invxorm[6];
  float rect[4];
  float ex, ey, tex, tey;

  // If no previous scissor has been set, set the scissor as current scissor.
  if (state->scissor.extent[0] < 0) {
    nvgScissor(ctx, x, y, w, h);
    return;
  }

  // Transform the current scissor rect into current transform space.
  // If there is difference in rotation, this will be approximation.
  memcpy(pxform, state->scissor.xform, sizeof(float)*6);
  ex = state->scissor.extent[0];
  ey = state->scissor.extent[1];
  nvgTransformInverse(invxorm, state->xform);
  nvgTransformMultiply(pxform, invxorm);
  tex = ex*nvg__absf(pxform[0]) + ey*nvg__absf(pxform[2]);
  tey = ex*nvg__absf(pxform[1]) + ey*nvg__absf(pxform[3]);

  // Intersect rects.
  nvg__isectRects(rect, pxform[4]-tex,pxform[5]-tey,tex*2,tey*2, x,y,w,h);

  nvgScissor(ctx, rect[0], rect[1], rect[2], rect[3]);
}

void nvgResetScissor(NVGcontext* ctx)
{
  NVGstate* state = nvg__getState(ctx);
  memset(state->scissor.xform, 0, sizeof(state->scissor.xform));
  state->scissor.extent[0] = -1.0f;
  state->scissor.extent[1] = -1.0f;
}

// Global composite operation.
void nvgGlobalCompositeOperation(NVGcontext* ctx, int op)
{
  NVGstate* state = nvg__getState(ctx);
  state->compositeOperation = nvg__compositeOperationState(op);
}

void nvgGlobalCompositeBlendFunc(NVGcontext* ctx, int sfactor, int dfactor)
{
  nvgGlobalCompositeBlendFuncSeparate(ctx, sfactor, dfactor, sfactor, dfactor);
}

void nvgGlobalCompositeBlendFuncSeparate(NVGcontext* ctx, int srcRGB, int dstRGB, int srcAlpha, int dstAlpha)
{
  NVGcompositeOperationState op;
  op.srcRGB = srcRGB;
  op.dstRGB = dstRGB;
  op.srcAlpha = srcAlpha;
  op.dstAlpha = dstAlpha;

  NVGstate* state = nvg__getState(ctx);
  state->compositeOperation = op;
}

static int nvg__ptEquals(float x1, float y1, float x2, float y2, float tol)
{
  float dx = x2 - x1;
  float dy = y2 - y1;
  return dx*dx + dy*dy < tol*tol;
}

static float nvg__distPtSeg(float x, float y, float px, float py, float qx, float qy)
{
  float pqx, pqy, dx, dy, d, t;
  pqx = qx-px;
  pqy = qy-py;
  dx = x-px;
  dy = y-py;
  d = pqx*pqx + pqy*pqy;
  t = pqx*dx + pqy*dy;
  if (d > 0) t /= d;
  if (t < 0) t = 0;
  else if (t > 1) t = 1;
  dx = px + t*pqx - x;
  dy = py + t*pqy - y;
  return dx*dx + dy*dy;
}

static void nvg__appendCommands(NVGcontext* ctx, float* vals, int nvals)
{
  NVGstate* state = nvg__getState(ctx);
  int i;

  if (ctx->ncommands+nvals > ctx->ccommands) {
    float* commands;
    int ccommands = ctx->ncommands+nvals + ctx->ccommands/2;
    commands = (float*)realloc(ctx->commands, sizeof(float)*ccommands);
    if (commands == NULL) return;
    ctx->commands = commands;
    ctx->ccommands = ccommands;
  }

  if ((int)vals[0] < NVG_CLOSE) {
    ctx->commandx = vals[nvals-2];
    ctx->commandy = vals[nvals-1];
  }

  // transform commands
  i = 0;
  while (i < nvals) {
    int cmd = (int)vals[i];
    switch (cmd) {
    case NVG_MOVETO:
      nvgTransformPoint(&vals[i+1],&vals[i+2], state->xform, vals[i+1],vals[i+2]);
      i += 3;
      break;
    case NVG_LINETO:
      nvgTransformPoint(&vals[i+1],&vals[i+2], state->xform, vals[i+1],vals[i+2]);
      i += 3;
      break;
    case NVG_BEZIERTO:
      nvgTransformPoint(&vals[i+1],&vals[i+2], state->xform, vals[i+1],vals[i+2]);
      nvgTransformPoint(&vals[i+3],&vals[i+4], state->xform, vals[i+3],vals[i+4]);
      nvgTransformPoint(&vals[i+5],&vals[i+6], state->xform, vals[i+5],vals[i+6]);
      i += 7;
      break;
    case NVG_CLOSE:
      i++;
      break;
    case NVG_WINDING:
      i += 2;
      break;
    default:  // NVG_RESTART
      i++;
    }
  }

  memcpy(&ctx->commands[ctx->ncommands], vals, nvals*sizeof(float));

  ctx->ncommands += nvals;
}

static void nvg__clearPathCache(NVGcontext* ctx)
{
  ctx->cache->npoints = 0;
  ctx->cache->npaths = 0;
}

static NVGpath* nvg__lastPath(NVGcontext* ctx)
{
  if (ctx->cache->npaths > 0)
    return &ctx->cache->paths[ctx->cache->npaths-1];
  return NULL;
}

static void nvg__addPath(NVGcontext* ctx)
{
  NVGpath* path;
  if (ctx->cache->npaths+1 > ctx->cache->cpaths) {
    NVGpath* paths;
    int cpaths = ctx->cache->npaths+1 + ctx->cache->cpaths/2;
    paths = (NVGpath*)realloc(ctx->cache->paths, sizeof(NVGpath)*cpaths);
    if (paths == NULL) return;
    ctx->cache->paths = paths;
    ctx->cache->cpaths = cpaths;
  }
  path = &ctx->cache->paths[ctx->cache->npaths];
  memset(path, 0, sizeof(*path));
  path->first = ctx->cache->npoints;
  path->winding = ctx->defaultWinding;

  ctx->cache->npaths++;
}

static NVGpoint* nvg__lastPoint(NVGcontext* ctx)
{
  if (ctx->cache->npoints > 0)
    return &ctx->cache->points[ctx->cache->npoints-1];
  return NULL;
}

static void nvg__addPoint(NVGcontext* ctx, float x, float y) //, int flags)
{
  NVGpath* path = nvg__lastPath(ctx);
  NVGpoint* pt;
  if (path == NULL) return;

  if (path->count > 0 && ctx->cache->npoints > 0) {
    pt = nvg__lastPoint(ctx);
    if (nvg__ptEquals(pt->x,pt->y, x,y, ctx->distTol)) {
      //pt->flags |= flags;
      return;
    }
  }

  if (ctx->cache->npoints+1 > ctx->cache->cpoints) {
    NVGpoint* points;
    int cpoints = ctx->cache->npoints+1 + ctx->cache->cpoints/2;
    points = (NVGpoint*)realloc(ctx->cache->points, sizeof(NVGpoint)*cpoints);
    if (points == NULL) return;
    ctx->cache->points = points;
    ctx->cache->cpoints = cpoints;
  }

  pt = &ctx->cache->points[ctx->cache->npoints];
  //memset(pt, 0, sizeof(*pt));
  pt->x = x;
  pt->y = y;
  //pt->flags = (unsigned char)flags;

  ctx->cache->npoints++;
  path->count++;
}

static float nvg__getAverageScale(float *t)
{
  float sx = nvg__sqrtf(t[0]*t[0] + t[2]*t[2]);
  float sy = nvg__sqrtf(t[1]*t[1] + t[3]*t[3]);
  return nvg__sqrtf(sx*sy);  //return (sx + sy) * 0.5f;
}

static NVGvertex* nvg__allocTempVerts(NVGcontext* ctx, int nverts)
{
  if (nverts > ctx->cache->cverts) {
    NVGvertex* verts;
    int cverts = (nverts + 0xff) & ~0xff; // Round up to prevent allocations when things change just slightly.
    verts = (NVGvertex*)realloc(ctx->cache->verts, sizeof(NVGvertex)*cverts);
    if (verts == NULL) return NULL;
    ctx->cache->verts = verts;
    ctx->cache->cverts = cverts;
  }

  return ctx->cache->verts;
}

static float nvg__triarea2(float ax, float ay, float bx, float by, float cx, float cy)
{
  float abx = bx - ax;
  float aby = by - ay;
  float acx = cx - ax;
  float acy = cy - ay;
  return acx*aby - abx*acy;
}

static float nvg__polyArea(NVGpoint* pts, int npts)
{
  int i;
  float area = 0;
  for (i = 2; i < npts; i++) {
    NVGpoint* a = &pts[0];
    NVGpoint* b = &pts[i-1];
    NVGpoint* c = &pts[i];
    area += nvg__triarea2(a->x,a->y, b->x,b->y, c->x,c->y);
  }
  return area * 0.5f;
}

static void nvg__polyReverse(NVGpoint* pts, int npts)
{
  NVGpoint tmp;
  int i = 0, j = npts-1;
  while (i < j) {
    tmp = pts[i];
    pts[i] = pts[j];
    pts[j] = tmp;
    i++;
    j--;
  }
}

static void nvg__vset2(NVGvertex* vtx, float x0, float y0, float x1, float y1, float u, float v)
{
  vtx->x0 = x0;
  vtx->y0 = y0;
  vtx->x1 = x1;
  vtx->y1 = y1;
  //vtx->u = u;
  //vtx->v = v;
}

static void nvg__vset(NVGvertex* vtx, float x, float y, float u, float v)
{
  nvg__vset2(vtx, x, y, u, v, 0, 0);
}

static void nvg__segment(NVGvertex* vtx, float x0, float y0, float x1, float y1)
{
  //fprintf(stderr, "Segment: (%f, %f) to (%f, %f)\n", x0, y0, x1, y1);
  // final vertices are now created in backend
  nvg__vset2(vtx, x0, y0, x1, y1, 0, 0);
}

static void nvg__tesselateBezier(NVGcontext* ctx,
                 float x1, float y1, float x2, float y2,
                 float x3, float y3, float x4, float y4,
                 int level) //, int type)
{
  float dx = x4 - x1;
  float dy = y4 - y1;
  float d2 = nvg__absf(((x2 - x4) * dy - (y2 - y4) * dx));
  float d3 = nvg__absf(((x3 - x4) * dy - (y3 - y4) * dx));

  if ((d2 + d3)*(d2 + d3) < ctx->tessTol * (dx*dx + dy*dy) || level >= 9)
    nvg__addPoint(ctx, x4, y4); //, type);
  else {  //if (level < 9)  -- this was the previous logic (so if level reached 10, no point was added!)
    float x12 = (x1+x2)*0.5f;
    float y12 = (y1+y2)*0.5f;
    float x23 = (x2+x3)*0.5f;
    float y23 = (y2+y3)*0.5f;
    float x34 = (x3+x4)*0.5f;
    float y34 = (y3+y4)*0.5f;
    float x123 = (x12+x23)*0.5f;
    float y123 = (y12+y23)*0.5f;
    float x234 = (x23+x34)*0.5f;
    float y234 = (y23+y34)*0.5f;
    float x1234 = (x123+x234)*0.5f;
    float y1234 = (y123+y234)*0.5f;

    nvg__tesselateBezier(ctx, x1,y1, x12,y12, x123,y123, x1234,y1234, level+1); //, 0);
    nvg__tesselateBezier(ctx, x1234,y1234, x234,y234, x34,y34, x4,y4, level+1); //, type);
  }
}

static void nvg__flattenPaths(NVGcontext* ctx)
{
  NVGpathCache* cache = ctx->cache;
  NVGpoint* last;
  NVGpoint* p0;
  NVGpoint* p1;
  NVGpoint* pts;
  NVGpath* path;
  int i, j;
  float* cp1;
  float* cp2;
  float* p;

  if (cache->npaths > 0)
    return;

  // Flatten
  i = 0;
  while (i < ctx->ncommands) {
    int cmd = (int)ctx->commands[i];
    switch (cmd) {
    case NVG_MOVETO:
      // skip extraneous MOVETO ... this allows us to avoid removing a Mx,x l0,0 path
      if (i+3 < ctx->ncommands && (int)ctx->commands[i+3] != NVG_MOVETO) {
        nvg__addPath(ctx);
        p = &ctx->commands[i+1];
        nvg__addPoint(ctx, p[0], p[1]);
      }
      i += 3;
      break;
    case NVG_LINETO:
      p = &ctx->commands[i+1];
      nvg__addPoint(ctx, p[0], p[1]);  // note that p is not added if equal to previous point (w/in distTol)
      i += 3;
      break;
    case NVG_BEZIERTO:
      last = nvg__lastPoint(ctx);
      if (last != NULL) {
        cp1 = &ctx->commands[i+1];
        cp2 = &ctx->commands[i+3];
        p = &ctx->commands[i+5];
        nvg__tesselateBezier(ctx, last->x,last->y, cp1[0],cp1[1], cp2[0],cp2[1], p[0],p[1], 0);
      }
      i += 7;
      break;
    case NVG_CLOSE:
      path = nvg__lastPath(ctx);
      if (path)
        path->closed = 1;
      i++;
      break;
    case NVG_WINDING:
      path = nvg__lastPath(ctx);
      if (path)
        path->winding = (unsigned char)ctx->commands[i+1];
      i += 2;
      break;
    case NVG_RESTART:
      path = nvg__lastPath(ctx);
      if (path)
        path->restart = 1;
      i++;
      break;
    default:
      i++;
    }
  }

  // check for closed paths and adjust winding direction if requested
  for (j = 0; j < cache->npaths; j++) {
    path = &cache->paths[j];
    if (path->count <= 1)
      continue;

    pts = &cache->points[path->first];
    // If the first and last points are the same, remove the last, mark as closed path.
    p0 = &pts[path->count-1];
    p1 = &pts[0];
    if (nvg__ptEquals(p0->x,p0->y, p1->x,p1->y, ctx->distTol)) {
      path->count--;
      path->closed = 1;
    }

    // Enforce winding.
    if (path->winding != NVG_AUTOW && path->count > 2) {
      float area = nvg__polyArea(pts, path->count);
      if (path->winding == NVG_CCW && area < 0.0f)
        nvg__polyReverse(pts, path->count);
      if (path->winding == NVG_CW && area > 0.0f)
        nvg__polyReverse(pts, path->count);
    }
  }
  // this is where we could store or print area info, i.e. bbox area/sum(polyArea) = overdraw ratio
}

static void nvg__calcBounds(NVGcontext* ctx)
{
  NVGstate* state = nvg__getState(ctx);
  NVGpathCache* cache = ctx->cache;
  int i,j;
  cache->bounds[0] = cache->bounds[1] = 1e6f;
  cache->bounds[2] = cache->bounds[3] = -1e6f;

  for (j = 0; j < cache->npaths; j++) {
    NVGpath* path = &cache->paths[j];
    path->bounds[0] = path->bounds[1] = 1e6f;
    path->bounds[2] = path->bounds[3] = -1e6f;

    for(i = 0; i < path->nfill; ++i) {
      NVGvertex* v = &path->fill[i];
      path->bounds[0] = nvg__minf(path->bounds[0], v->x1);
      path->bounds[1] = nvg__minf(path->bounds[1], v->y1);
      path->bounds[2] = nvg__maxf(path->bounds[2], v->x1);
      path->bounds[3] = nvg__maxf(path->bounds[3], v->y1);
    }
    cache->bounds[0] = nvg__minf(cache->bounds[0], path->bounds[0]);
    cache->bounds[1] = nvg__minf(cache->bounds[1], path->bounds[1]);
    cache->bounds[2] = nvg__maxf(cache->bounds[2], path->bounds[2]);
    cache->bounds[3] = nvg__maxf(cache->bounds[3], path->bounds[3]);
  }
  // clip bounds to scissor
  if (state->scissor.extent[0] >= 0) {
    cache->bounds[0] = nvg__maxf(cache->bounds[0], state->scissorBounds[0]);
    cache->bounds[1] = nvg__maxf(cache->bounds[1], state->scissorBounds[1]);
    cache->bounds[2] = nvg__minf(cache->bounds[2], state->scissorBounds[2]);
    cache->bounds[3] = nvg__minf(cache->bounds[3], state->scissorBounds[3]);
  }
}

static int nvg__curveDivs(float r, float arc, float tol)
{
  float da = acosf(r / (r + tol)) * 2.0f;
  return nvg__maxi(2, (int)ceilf(arc / da));
}

// approximate CCW arc from p0 to p1 w/ center pc and radius w, using ncap line segments per 180 deg
static NVGvertex* nvg__arcJoin(NVGvertex* dst, float x0, float y0, float x1, float y1, float xc, float yc, float w, int ncap, int dir)
{
  int i, n;
  float ax, ay;
  float a0 = atan2f(y0 - yc, x0 - xc);
  float a1 = atan2f(y1 - yc, x1 - xc);
  if (a1 > a0) a1 -= NVG_PI*2;  // enforce CCW sweep (negative sweep angle in screen coords, where y is down)

  n = nvg__clampi((int)ceilf(((a0 - a1) / NVG_PI) * ncap), 2, ncap);
  for (i = 0; i < n; i++) {
    float u = i/(float)(n-1);
    float a = a0 + u*(a1-a0);
    float rx = xc + cosf(a) * w;
    float ry = yc + sinf(a) * w;
    if(i > 0)
      dir < 0 ? nvg__segment(--dst, ax, ay, rx, ry) : nvg__segment(dst++, ax, ay, rx, ry);
    ax = rx;  ay = ry;
  }
  return dst;
}

// miter limit ref: https://www.w3.org/TR/SVG11/painting.html#StrokeMiterlimitProperty
static int nvg__expandStroke(NVGcontext* ctx, float strokeWidth, int lineCap, int lineJoin, float miterLimit)
{
  NVGpathCache* cache = ctx->cache;
  NVGvertex* verts, *vertsend, *dst, *rdst;
  float w = 0.5f * strokeWidth;
  int i, j, csegs = 0;
  int ncap = nvg__curveDivs(w, NVG_PI, ctx->tessTol);	// Calculate divisions per half circle.
  float mlimsq = w*w*miterLimit*miterLimit;  // (miter_length_limit/2)^2, since w = stroke_width/2

  // Calculate max vertex usage.
  for (i = 0; i < cache->npaths; ++i) {
    NVGpath* path = &cache->paths[i];
    if (lineJoin == NVG_ROUND || lineCap == NVG_ROUND)
      csegs += (path->count*(ncap+2) + 1) * 2; // plus one for loop
    else
      csegs += (path->count*2 + 1) * 2; // plus one for loop
  }

  verts = nvg__allocTempVerts(ctx, 6*csegs);
  vertsend = verts + 6*csegs;
  if (verts == NULL) return 0;

  for (i = 0; i < cache->npaths; ++i) {
    NVGpath* path = &cache->paths[i];
    NVGpoint* pts = &cache->points[path->first];
    NVGpoint* p0;
    NVGpoint* p1;
    NVGpoint* p2;
    NVGpoint temppt;
    int closed = path->closed && path->count > 2;
    float lx, ly, rx, ry;  // current position of left and right paths
    float d01x, d01y, n01x, n01y;  // d01 is p0 -> p1 vector; n01 is the normal to d01
    float l00x, l00y, r00x, r00y;  // start positions for closed loop case

    if(path->count == 0)  // single move-to case
      continue;

    if (closed) {
      // path is a closed loop
      // initial: p0 = n-2, p1 = n-1, p2 = 0  ... l,r inited at p0
      // final: p0 = n-3 p1 = n-2, p2 = n-1 ... loop n times
      p0 = &pts[path->count-2];
      p1 = &pts[path->count-1];
      p2 = &pts[0];
    } else if(path->count == 1) {
      // this is the case of move-to (x,y) + line-to (x,y) (same point)
      if(lineCap == NVG_BUTT)
        continue;
      p0 = &pts[0];
      temppt.x = p0->x + w/256; temppt.y = p0->y;  // offset slightly so calc of normal vector works
      p1 = &temppt;
    } else {
      // path is not closed - add end caps
      // cap 0
      // initial: p0 = 0, p1 = 1, p2 = 2  ... l,r inited at p0
      // final: p0 = n-3, p1 = n-2, p2 = n-1 ... loop n-2 times
      // after loop finishes: p0 = n-2, p1 = n-1, p2 invalid
      // cap n-1
      p0 = &pts[0];
      p1 = &pts[1];
      p2 = &pts[2];  // never used if count == 2, although we should probably still set to NULL
    }

    dst = verts;
    rdst = vertsend;
    path->fill = dst;
    // init left and right paths at p0
    d01x = p1->x - p0->x;  d01y = p1->y - p0->y;
    n01x = -d01y;  n01y = d01x;  // left normal to segment - +90 deg CCW rotation
    nvg__normalize(&n01x, &n01y);

    if (closed == 0) {
      lx = p0->x + w*n01x;  ly = p0->y + w*n01y;
      rx = p0->x - w*n01x;  ry = p0->y - w*n01y;
      // start cap
      if (lineCap == NVG_BUTT)
        nvg__segment(dst++, rx, ry, lx, ly);
      else if (lineCap == NVG_SQUARE) {
        float cdx = n01y*w, cdy = -n01x*w;  // = w*normalized(d01);
        nvg__segment(dst++, rx, ry, rx - cdx, ry - cdy);
        nvg__segment(dst++, rx - cdx, ry - cdy, lx - cdx, ly - cdy);
        nvg__segment(dst++, lx - cdx, ly - cdy, lx, ly);
      }
      else if (lineCap == NVG_ROUND)
        dst = nvg__arcJoin(dst, rx, ry, lx, ly, p0->x, p0->y, w, ncap, 1);
    }

    for (j = closed ? 0 : 2; j < path->count; ++j) {
      float l01x, l01y, r01x, r01y, l12x, l12y, r12x, r12y;
      float d12x = p2->x - p1->x, d12y = p2->y - p1->y;
      float n12x = -d12y, n12y = d12x;  // left normal to segment (+90 deg CCW rotation)
      float _len = nvg__normalize(&n12x, &n12y);  // throwaway so we don't need separate decl. for vars below

      // these are only needed for miter join
      float miter_denom = nvg__maxf(1e-6f, 1 + (n01x*n12x + n01y*n12y));  // 1 + dot(n01,n12)
      float ex = (n01x + n12x)/miter_denom;
      float ey = (n01y + n12y)/miter_denom;
      float mlensq = w*w*(ex*ex + ey*ey);  // (miter_length/2)^2
      float len01sq = d01x*d01x + d01y*d01y;
      float len12sq = d12x*d12x + d12y*d12y;

      int left = d01x*d12y - d01y*d12x > 0;  // left turn or right turn?
      int join = (lineJoin == NVG_MITER && mlensq <= mlimsq && miter_denom > 1e-6f) ? NVG_MITER : NVG_BEVEL;
      int outerjoin = lineJoin == NVG_ROUND ? NVG_ROUND : join;
      // force bevel join on inner corner if miter longer than segment
      int innerjoin = (len01sq < mlensq || len12sq < mlensq) ? NVG_BEVEL : join;
      int ljoin = left ? innerjoin : outerjoin;
      int rjoin = left ? outerjoin : innerjoin;
      NVG_NOTUSED(_len);

      // this code was cleaner when less factored ... but got too messy handling the closed loop case
      // left side path
      if(ljoin == NVG_MITER) {
        l12x = l01x = p1->x + w*ex; l12y = l01y = p1->y + w*ey;
      } else {
        l01x = p1->x + w*n01x; l01y = p1->y + w*n01y;
        l12x = p1->x + w*n12x; l12y = p1->y + w*n12y;
      }
      if(j > 0)
        nvg__segment(dst++, lx, ly, l01x, l01y);  // first segment (and only for miter join)
      else {
        l00x = l01x;
        l00y = l01y;
      }
      if(ljoin == NVG_ROUND)
        dst = nvg__arcJoin(dst, l01x, l01y, l12x, l12y, p1->x, p1->y, w, ncap, 1);
      else if(ljoin == NVG_BEVEL)
        nvg__segment(dst++, l01x, l01y, l12x, l12y);
      lx = l12x; ly = l12y;

      // now right side path - note segment directions are reversed
      if(rjoin == NVG_MITER) {
        r12x = r01x = p1->x - w*ex; r12y = r01y = p1->y - w*ey;
      } else {
        r01x = p1->x - w*n01x; r01y = p1->y - w*n01y;
        r12x = p1->x - w*n12x; r12y = p1->y - w*n12y;
      }
      if(j > 0)
        nvg__segment(--rdst, r01x, r01y, rx, ry);  // first segment (and only for miter join)
      else {
        r00x = r01x;
        r00y = r01y;
      }
      if(rjoin == NVG_ROUND)
        rdst = nvg__arcJoin(rdst, r12x, r12y, r01x, r01y, p1->x, p1->y, w, ncap, -1);
      else if(rjoin == NVG_BEVEL)
        nvg__segment(--rdst, r12x, r12y, r01x, r01y);
      rx = r12x; ry = r12y;

      p0 = p1;
      p1 = p2++;
      d01x = d12x;  d01y = d12y;  n01x = n12x;  n01y = n12y;
    }

    if (closed == 0) {
      // final segments
      float l01x = p1->x + w*n01x, l01y = p1->y + w*n01y;
      float r01x = p1->x - w*n01x, r01y = p1->y - w*n01y;
      nvg__segment(dst++, lx, ly, l01x, l01y);
      lx = l01x; ly = l01y;
      nvg__segment(--rdst, r01x, r01y, rx, ry);
      rx = r01x; ry = r01y;

      // end cap
      if (lineCap == NVG_BUTT)
        nvg__segment(dst++, lx, ly, rx, ry);
      else if (lineCap == NVG_SQUARE) {
        float cdx = n01y*w, cdy = -n01x*w;  // = w*normalized(d01);
        nvg__segment(dst++, lx, ly, lx + cdx, ly + cdy);
        nvg__segment(dst++, lx + cdx, ly + cdy, rx + cdx, ry + cdy);
        nvg__segment(dst++, rx + cdx, ry + cdy, rx, ry);
      }
      else if (lineCap == NVG_ROUND)
        dst = nvg__arcJoin(dst, lx, ly, rx, ry, p1->x, p1->y, w, ncap, 1);
    }
    else {
      // close loop
      nvg__segment(dst++, lx, ly, l00x, l00y);
      nvg__segment(--rdst, r00x, r00y, rx, ry);
    }

    // segments for "right" side of path are added in reverse order at end of buffer, then copied to end of
    //  "left" side segments so that segments are in order, not interleaved (for vtex renderer optimization)
    memmove(dst, rdst, (vertsend - rdst)*sizeof(NVGvertex));
    dst += vertsend - rdst;
    path->nfill = (int)(dst - verts);
    verts = dst;
  }

  return 1;
}

static void nvg__dashStroke(NVGcontext* ctx, float scale, float strokeWidth)
{
  NVGstate* state = nvg__getState(ctx);
  NVGpathCache* cache = ctx->cache;
  float* dashes = state->dashArray;
  int i, j, ndashes, idash0 = 0;
  float allDashLen = 0, dashOffset;
  // dashed segments will be added to the same cache, so need to save initial path (and point) count
  int npaths = cache->npaths;

  // get length of dash array
  for (ndashes = 0; dashes[ndashes] >= 0; ++ndashes) {}
  // Figure out dash offset.
  for (j = 0; j < ndashes; j++)
    allDashLen += dashes[j];
  if (ndashes & 1)
    allDashLen *= 2.0f;
  // Find location inside pattern
  dashOffset = fmodf(state->dashOffset, allDashLen);
  if (dashOffset < 0.0f)
    dashOffset += allDashLen;
  while (dashOffset > dashes[idash0]) {
    dashOffset -= dashes[idash0];
    idash0 = (idash0 + 1) % ndashes;
  }

  for (i = 0; i < npaths; ++i) {
    NVGpath* path0 = &cache->paths[i];  // stupid name to avoid shadowing
    int dashState = 1;
    int idash = idash0;
    int jlim = path0->closed ? path0->count + 1 : path0->count;
    float totalDist = 0;
    float dashLen = (dashes[idash] - dashOffset) * scale;
    NVGpoint cur = cache->points[path0->first];
    nvg__addPath(ctx);
    nvg__addPoint(ctx, cur.x, cur.y);  // initial state is dash (not gap)

    for (j = 1; j < jlim; ) {
      // cache->path/points could be moved by realloc after any addPath(), addPoint()
      NVGpath* path = &cache->paths[i];
      NVGpoint* points = &cache->points[path->first];
      int k = j % path->count;
      float dx = points[k].x - cur.x;
      float dy = points[k].y - cur.y;
      float dist = nvg__sqrtf(dx*dx + dy*dy);

      if (totalDist + dist > dashLen) {
        // Calculate intermediate point
        float d = (dashLen - totalDist) / dist;
        float x = cur.x + dx * d;
        float y = cur.y + dy * d;
        if (!dashState)
          nvg__addPath(ctx);  // starting dash
        nvg__addPoint(ctx, x, y);

        // Advance dash pattern
        dashState = !dashState;
        idash = (idash+1) % ndashes;
        dashLen = dashes[idash] * scale;
        // Restart
        cur.x = x;
        cur.y = y;
        totalDist = 0.0f;
      } else {
        totalDist += dist;
        cur = points[k];
        if (dashState)
          nvg__addPoint(ctx, cur.x, cur.y);
        j++;
      }
    }
  }
}

static int nvg__expandFill(NVGcontext* ctx)  //, float w, int lineJoin, float miterLimit)
{
  NVGpathCache* cache = ctx->cache;
  NVGvertex* verts;
  int i, j, csegs = 0;

  // Calculate max vertex usage.
  for (i = 0; i < cache->npaths; i++) {
    NVGpath* path = &cache->paths[i];
    csegs += path->count;
  }

  verts = nvg__allocTempVerts(ctx, csegs);
  if (verts == NULL) return 0;

  // if path is explicitly closed, nvg__flattenPaths removes last point and sets path->closed - so for fill
  //  we always add the closing segment
  for (i = 0; i < cache->npaths; i++) {
    NVGpath* path = &cache->paths[i];
    NVGpoint* p1 = &cache->points[path->first];
    NVGpoint* p0 = p1 + (path->count - 1);

    path->fill = verts;
    for (j = 0; j < path->count; ++j) {
      nvg__segment(verts++, p0->x, p0->y, p1->x, p1->y);
      p0 = p1++;
    }
    path->nfill = (int)(verts - path->fill);
  }
  // determine if path can be drawn as a single triangle fan from first point (non-AA paths only)
  // should we assume a path with many points is unlikely to be convex?: cache->paths[0].nfill < 16
  // Note that this is done only for fills, not strokes since stroke segments are out-of-order!
  if(!nvg__getState(ctx)->shapeAntiAlias && cache->npaths == 1 && cache->paths[0].nfill > 2) {
    NVGpath* path = &cache->paths[0];
    NVGvertex* v0 = &path->fill[0];
    NVGvertex* v = &path->fill[1];
    int sgn = nvg__triarea2(v0->x0, v0->y0, v->x0, v->y0, v->x1, v->y1) > 0;
    path->convex = 1;
    for(i = 2; i < path->nfill - 1 && path->convex; ++i) {
      v = &path->fill[i];
      path->convex = (nvg__triarea2(v0->x0, v0->y0, v->x0, v->y0, v->x1, v->y1) > 0) == sgn;
    }
  }
  return 1;
}

// Draw
void nvgBeginPath(NVGcontext* ctx)
{
  ctx->ncommands = 0;
  nvg__clearPathCache(ctx);
}

void nvgMoveTo(NVGcontext* ctx, float x, float y)
{
  float vals[] = { NVG_MOVETO, x, y };
  nvg__appendCommands(ctx, vals, NVG_COUNTOF(vals));
}

void nvgLineTo(NVGcontext* ctx, float x, float y)
{
  float vals[] = { NVG_LINETO, x, y };
  nvg__appendCommands(ctx, vals, NVG_COUNTOF(vals));
}

void nvgBezierTo(NVGcontext* ctx, float c1x, float c1y, float c2x, float c2y, float x, float y)
{
  float vals[] = { NVG_BEZIERTO, c1x, c1y, c2x, c2y, x, y };
  nvg__appendCommands(ctx, vals, NVG_COUNTOF(vals));
}

void nvgQuadTo(NVGcontext* ctx, float cx, float cy, float x, float y)
{
  float x0 = ctx->commandx;
  float y0 = ctx->commandy;
  float vals[] = { NVG_BEZIERTO,
      x0 + 2.0f/3.0f*(cx - x0), y0 + 2.0f/3.0f*(cy - y0),
      x + 2.0f/3.0f*(cx - x), y + 2.0f/3.0f*(cy - y),
      x, y };
  nvg__appendCommands(ctx, vals, NVG_COUNTOF(vals));
}

void nvgArcTo(NVGcontext* ctx, float x1, float y1, float x2, float y2, float radius)
{
  float x0 = ctx->commandx;
  float y0 = ctx->commandy;
  float dx0,dy0, dx1,dy1, a, d, cx,cy, a0,a1;
  int dir;
  if (ctx->ncommands == 0) return;

  // Handle degenerate cases.
  if (nvg__ptEquals(x0,y0, x1,y1, ctx->distTol) ||
    nvg__ptEquals(x1,y1, x2,y2, ctx->distTol) ||
    nvg__distPtSeg(x1,y1, x0,y0, x2,y2) < ctx->distTol*ctx->distTol ||
    radius < ctx->distTol) {
    nvgLineTo(ctx, x1,y1);
    return;
  }

  // Calculate tangential circle to lines (x0,y0)-(x1,y1) and (x1,y1)-(x2,y2).
  dx0 = x0-x1;
  dy0 = y0-y1;
  dx1 = x2-x1;
  dy1 = y2-y1;
  nvg__normalize(&dx0,&dy0);
  nvg__normalize(&dx1,&dy1);
  a = nvg__acosf(dx0*dx1 + dy0*dy1);
  d = radius / nvg__tanf(a/2.0f);

//	printf("a=%f° d=%f\n", a/NVG_PI*180.0f, d);

  if (d > 10000.0f) {
    nvgLineTo(ctx, x1,y1);
    return;
  }

  if (nvg__cross(dx0,dy0, dx1,dy1) > 0.0f) {
    cx = x1 + dx0*d + dy0*radius;
    cy = y1 + dy0*d + -dx0*radius;
    a0 = nvg__atan2f(dx0, -dy0);
    a1 = nvg__atan2f(-dx1, dy1);
    dir = NVG_CW;
//		printf("CW c=(%f, %f) a0=%f° a1=%f°\n", cx, cy, a0/NVG_PI*180.0f, a1/NVG_PI*180.0f);
  } else {
    cx = x1 + dx0*d + -dy0*radius;
    cy = y1 + dy0*d + dx0*radius;
    a0 = nvg__atan2f(-dx0, dy0);
    a1 = nvg__atan2f(dx1, -dy1);
    dir = NVG_CCW;
//		printf("CCW c=(%f, %f) a0=%f° a1=%f°\n", cx, cy, a0/NVG_PI*180.0f, a1/NVG_PI*180.0f);
  }

  nvgArc(ctx, cx, cy, radius, a0, a1, dir);
}

void nvgClosePath(NVGcontext* ctx)
{
  float vals[] = { NVG_CLOSE };
  nvg__appendCommands(ctx, vals, NVG_COUNTOF(vals));
}

void nvgPathWinding(NVGcontext* ctx, int dir)
{
  float vals[] = { NVG_WINDING, (float)dir };
  nvg__appendCommands(ctx, vals, NVG_COUNTOF(vals));
}

void nvgArc(NVGcontext* ctx, float cx, float cy, float r, float a0, float a1, int dir)
{
  float a = 0, da = 0, hda = 0, kappa = 0;
  float dx = 0, dy = 0, x = 0, y = 0, tanx = 0, tany = 0;
  float px = 0, py = 0, ptanx = 0, ptany = 0;
  float vals[3 + 5*7 + 100];
  int i, ndivs, nvals;
  int move = ctx->ncommands > 0 ? NVG_LINETO : NVG_MOVETO;

  // Clamp angles
  da = a1 - a0;
  if (dir == NVG_CW) {
    if (nvg__absf(da) >= NVG_PI*2) {
      da = NVG_PI*2;
    } else {
      while (da < 0.0f) da += NVG_PI*2;
    }
  } else {
    if (nvg__absf(da) >= NVG_PI*2) {
      da = -NVG_PI*2;
    } else {
      while (da > 0.0f) da -= NVG_PI*2;
    }
  }

  // Split arc into max 90 degree segments.
  ndivs = nvg__maxi(1, nvg__mini((int)(nvg__absf(da) / (NVG_PI*0.5f) + 0.5f), 5));
  hda = (da / (float)ndivs) / 2.0f;
  kappa = nvg__absf(4.0f / 3.0f * (1.0f - nvg__cosf(hda)) / nvg__sinf(hda));

  if (dir == NVG_CCW)
    kappa = -kappa;

  nvals = 0;
  for (i = 0; i <= ndivs; i++) {
    a = a0 + da * (i/(float)ndivs);
    dx = nvg__cosf(a);
    dy = nvg__sinf(a);
    x = cx + dx*r;
    y = cy + dy*r;
    tanx = -dy*r*kappa;
    tany = dx*r*kappa;

    if (i == 0) {
      vals[nvals++] = (float)move;
      vals[nvals++] = x;
      vals[nvals++] = y;
    } else {
      vals[nvals++] = NVG_BEZIERTO;
      vals[nvals++] = px+ptanx;
      vals[nvals++] = py+ptany;
      vals[nvals++] = x-tanx;
      vals[nvals++] = y-tany;
      vals[nvals++] = x;
      vals[nvals++] = y;
    }
    px = x;
    py = y;
    ptanx = tanx;
    ptany = tany;
  }

  nvg__appendCommands(ctx, vals, nvals);
}

void nvgRect(NVGcontext* ctx, float x, float y, float w, float h)
{
  float vals[] = {
    NVG_MOVETO, x,y,
    NVG_LINETO, x,y+h,
    NVG_LINETO, x+w,y+h,
    NVG_LINETO, x+w,y,
    NVG_CLOSE
  };
  nvg__appendCommands(ctx, vals, NVG_COUNTOF(vals));
}

void nvgRoundedRect(NVGcontext* ctx, float x, float y, float w, float h, float r)
{
  nvgRoundedRectVarying(ctx, x, y, w, h, r, r, r, r);
}

void nvgRoundedRectVarying(NVGcontext* ctx, float x, float y, float w, float h, float radTopLeft, float radTopRight, float radBottomRight, float radBottomLeft)
{
  if(radTopLeft < 0.1f && radTopRight < 0.1f && radBottomRight < 0.1f && radBottomLeft < 0.1f) {
    nvgRect(ctx, x, y, w, h);
    return;
  } else {
    float halfw = nvg__absf(w)*0.5f;
    float halfh = nvg__absf(h)*0.5f;
    float rxBL = nvg__minf(radBottomLeft, halfw) * nvg__signf(w), ryBL = nvg__minf(radBottomLeft, halfh) * nvg__signf(h);
    float rxBR = nvg__minf(radBottomRight, halfw) * nvg__signf(w), ryBR = nvg__minf(radBottomRight, halfh) * nvg__signf(h);
    float rxTR = nvg__minf(radTopRight, halfw) * nvg__signf(w), ryTR = nvg__minf(radTopRight, halfh) * nvg__signf(h);
    float rxTL = nvg__minf(radTopLeft, halfw) * nvg__signf(w), ryTL = nvg__minf(radTopLeft, halfh) * nvg__signf(h);
    float vals[] = {
      NVG_MOVETO, x, y + ryTL,
      NVG_LINETO, x, y + h - ryBL,
      NVG_BEZIERTO, x, y + h - ryBL*(1 - NVG_KAPPA90), x + rxBL*(1 - NVG_KAPPA90), y + h, x + rxBL, y + h,
      NVG_LINETO, x + w - rxBR, y + h,
      NVG_BEZIERTO, x + w - rxBR*(1 - NVG_KAPPA90), y + h, x + w, y + h - ryBR*(1 - NVG_KAPPA90), x + w, y + h - ryBR,
      NVG_LINETO, x + w, y + ryTR,
      NVG_BEZIERTO, x + w, y + ryTR*(1 - NVG_KAPPA90), x + w - rxTR*(1 - NVG_KAPPA90), y, x + w - rxTR, y,
      NVG_LINETO, x + rxTL, y,
      NVG_BEZIERTO, x + rxTL*(1 - NVG_KAPPA90), y, x, y + ryTL*(1 - NVG_KAPPA90), x, y + ryTL,
      NVG_CLOSE
    };
    nvg__appendCommands(ctx, vals, NVG_COUNTOF(vals));
  }
}

void nvgEllipse(NVGcontext* ctx, float cx, float cy, float rx, float ry)
{
  float vals[] = {
    NVG_MOVETO, cx-rx, cy,
    NVG_BEZIERTO, cx-rx, cy+ry*NVG_KAPPA90, cx-rx*NVG_KAPPA90, cy+ry, cx, cy+ry,
    NVG_BEZIERTO, cx+rx*NVG_KAPPA90, cy+ry, cx+rx, cy+ry*NVG_KAPPA90, cx+rx, cy,
    NVG_BEZIERTO, cx+rx, cy-ry*NVG_KAPPA90, cx+rx*NVG_KAPPA90, cy-ry, cx, cy-ry,
    NVG_BEZIERTO, cx-rx*NVG_KAPPA90, cy-ry, cx-rx, cy-ry*NVG_KAPPA90, cx-rx, cy,
    NVG_CLOSE
  };
  nvg__appendCommands(ctx, vals, NVG_COUNTOF(vals));
}

void nvgCircle(NVGcontext* ctx, float cx, float cy, float r)
{
  nvgEllipse(ctx, cx,cy, r,r);
}

void nvgDebugDumpPathCache(NVGcontext* ctx)
{
  const NVGpath* path;
  int i, j;

  printf("Dumping %d cached paths\n", ctx->cache->npaths);
  for (i = 0; i < ctx->cache->npaths; i++) {
    path = &ctx->cache->paths[i];
    printf(" - Path %d\n", i);
    if (path->nfill) {
      printf("   - fill: %d\n", path->nfill);
      for (j = 0; j < path->nfill; j++)
        printf("%f\t%f\n", path->fill[j].x1, path->fill[j].y1);
    }
  }
}

void nvgFill(NVGcontext* ctx)
{
  NVGstate* state = nvg__getState(ctx);
  NVGpaint fillPaint = state->fill;
  int flags = (state->shapeAntiAlias ? 0 : NVG_PATH_NO_AA) | (state->fillRule == NVG_NONZERO ? 0 : NVG_PATH_EVENODD);

  // Apply global alpha
  fillPaint.innerColor.a *= state->alpha;
  fillPaint.outerColor.a *= state->alpha;

  nvg__flattenPaths(ctx);
  nvg__expandFill(ctx);
  nvg__calcBounds(ctx);

  ctx->params.renderFill(ctx->params.userPtr, &fillPaint, state->compositeOperation, &state->scissor, flags,
      ctx->cache->bounds, ctx->cache->paths, ctx->cache->npaths);

  // clear convex flag so it isn't erroneously applied to a subsequent stroke
  if(ctx->cache->npaths == 1) ctx->cache->paths[0].convex = 0;
}

void nvgStroke(NVGcontext* ctx)
{
  NVGstate* state = nvg__getState(ctx);
  NVGpathCache* cache = ctx->cache;
  float scale = nvg__getAverageScale(state->xform);
  float strokeWidth = nvg__maxf(state->strokeWidth * scale, 0.0f);   //nvg__clampf(..., 200.0f);
  NVGpaint strokePaint = state->stroke;
  NVGpath* paths0 = NULL;
  int npaths0, npoints0;
  int flags = (state->shapeAntiAlias ? 0 : NVG_PATH_NO_AA);  // stroke fill always uses non-zero fill rule

  // we'll take stroke-width == 0 to indicate a non-scaling stroke
  if(strokeWidth == 0.0f) strokeWidth = 1.0f;

  // Apply global alpha
  strokePaint.innerColor.a *= state->alpha;
  strokePaint.outerColor.a *= state->alpha;

  nvg__flattenPaths(ctx);
  // this is a bit hacky, but a separate path cache for dashed stroke pieces would be worse
  npaths0 = cache->npaths;
  npoints0 = cache->npoints;
  paths0 = cache->paths;
  if(state->dashArray && state->dashArray[0] >= 0) {
    nvg__dashStroke(ctx, scale, strokeWidth);
    paths0 = cache->paths;  // maybe have been realloced in nvg__dashStroke
     // value of npaths before nvg__dashStroke call is offset into cache->paths of dashed pieces
    cache->paths += npaths0;
    cache->npaths -= npaths0;
  }
  nvg__expandStroke(ctx, strokeWidth, state->lineCap, state->lineJoin, state->miterLimit);
  nvg__calcBounds(ctx);

  ctx->params.renderFill(ctx->params.userPtr, &strokePaint,
      state->compositeOperation, &state->scissor, flags, cache->bounds, cache->paths, cache->npaths);
  // restore path cache
  cache->npaths = npaths0;
  cache->npoints = npoints0;
  cache->paths = paths0;
}

// Add fonts
int nvgCreateFont(NVGcontext* ctx, const char* name, const char* path)
{
  return fonsAddFont(ctx->fs, name, path);
}

int nvgCreateFontMem(NVGcontext* ctx, const char* name, unsigned char* data, int ndata, int freeData)
{
  return fonsAddFontMem(ctx->fs, name, data, ndata, freeData);
}

int nvgFindFont(NVGcontext* ctx, const char* name)
{
  return fonsGetFontByName(ctx->fs, name);
}

int nvgAddFallbackFontId(NVGcontext* ctx, int baseFont, int fallbackFont)
{
  if(fallbackFont == -1) return 0;
  return fonsAddFallbackFont(ctx->fs, baseFont, fallbackFont);
}

int nvgAddFallbackFont(NVGcontext* ctx, const char* baseFont, const char* fallbackFont)
{
  return nvgAddFallbackFontId(ctx, nvgFindFont(ctx, baseFont), nvgFindFont(ctx, fallbackFont));
}

// State setting
void nvgFontSize(NVGcontext* ctx, float size)
{
  NVGstate* state = nvg__getState(ctx);
  state->fontSize = size;
}

void nvgFontBlur(NVGcontext* ctx, float blur)
{
  NVGstate* state = nvg__getState(ctx);
  state->fontBlur = blur;
}

void nvgTextLetterSpacing(NVGcontext* ctx, float spacing)
{
  NVGstate* state = nvg__getState(ctx);
  state->letterSpacing = spacing;
}

void nvgTextLineHeight(NVGcontext* ctx, float lineHeight)
{
  NVGstate* state = nvg__getState(ctx);
  state->lineHeight = lineHeight;
}

void nvgTextAlign(NVGcontext* ctx, int align)
{
  NVGstate* state = nvg__getState(ctx);
  state->textAlign = align;
}

int nvgFontFace(NVGcontext* ctx, const char* font)
{
  return nvgFontFaceId(ctx, fonsGetFontByName(ctx->fs, font));
}

void nvgAtlasTextThreshold(NVGcontext* ctx, float px)
{
  int atlasFontPx = (int)(2*px + 0.5f);  // fonsEmSizeToSize(fons, state->fontSize) -- this would be better
  int currAtlasFontPx;
  fonsGetAtlasSize(ctx->fs, NULL, NULL, &currAtlasFontPx);
  if (atlasFontPx > currAtlasFontPx) {
    int w = NVG_INIT_FONTIMAGE_SIZE, h = NVG_INIT_FONTIMAGE_SIZE;
    fonsResetAtlas(ctx->fs, w, h, atlasFontPx);

    if (ctx->fontImageIdx < 0) {
      int type = (ctx->params.flags & NVG_SDF_TEXT) ? NVG_TEXTURE_ALPHA : NVG_TEXTURE_FLOAT;
      int flag = (ctx->params.flags & NVG_SDF_TEXT) ? 0 : NVG_IMAGE_NEAREST;
      ctx->fontImages[0] = ctx->params.renderCreateTexture(ctx->params.userPtr, type, w, h, flag, NULL);
      if (ctx->fontImages[0] == 0) return;
      ctx->fontImageIdx = 0;
    }
  }
  ctx->atlasTextThresh = px;
}

static void nvg__fonsSetup(NVGcontext* ctx, FONSstate* fons)  //, float scale)
{
  NVGstate* state = nvg__getState(ctx);
  fonsInitState(ctx->fs, fons);
  fonsSetFont(fons, state->fontId);
  fonsSetSize(fons, fonsEmSizeToSize(fons, state->fontSize));  //*scale));
  fonsSetSpacing(fons, state->letterSpacing);  //*scale);
  fonsSetBlur(fons, state->fontBlur);  //*scale);
  fonsSetAlign(fons, state->textAlign);
}

// this is for compatibility with the old "font size"
void nvgFontHeight(NVGcontext* ctx, float height)
{
  NVGstate* state = nvg__getState(ctx);
  FONSstate fons;
  nvg__fonsSetup(ctx, &fons);
  fonsSetFont(&fons, state->fontId);
  state->fontSize = height/fonsEmSizeToSize(&fons, 1.0f);
}

int nvgFontFaceId(NVGcontext* ctx, int font)
{
  NVGstate* state = nvg__getState(ctx);
  FONSstate fons;
  nvg__fonsSetup(ctx, &fons);
  // previously fonsSetFont() wasn't called until nvgText(), but we now do it here due to delayed font loading
  if(fonsSetFont(&fons, font) == FONS_INVALID)
    return -1;
  state->fontId = font;
  return 0;
}

static void nvg__flushTextTexture(NVGcontext* ctx)
{
  int dirty[4];

  if (fonsValidateTexture(ctx->fs, dirty)) {
    int fontImage = ctx->fontImages[ctx->fontImageIdx];
    // Update texture
    if (fontImage != 0) {
      int iw, ih;
      const void* data = fonsGetTextureData(ctx->fs, &iw, &ih);
      int x = dirty[0];
      int y = dirty[1];
      int w = dirty[2] - dirty[0];
      int h = dirty[3] - dirty[1];
      ctx->params.renderUpdateTexture(ctx->params.userPtr, fontImage, x,y, w,h, data);
    }
  }
}

static int nvg__allocTextAtlas(NVGcontext* ctx)
{
  int iw, ih;
  int atlasFontPx;
  nvg__flushTextTexture(ctx);
  if (ctx->fontImageIdx >= NVG_MAX_FONTIMAGES-1)
    return 0;
  // if next fontImage already have a texture
  if (ctx->fontImages[ctx->fontImageIdx+1] != 0)
    nvgImageSize(ctx, ctx->fontImages[ctx->fontImageIdx+1], &iw, &ih);
  else { // calculate the new font image size and create it.
    nvgImageSize(ctx, ctx->fontImages[ctx->fontImageIdx], &iw, &ih);
    if (iw > ih)
      ih *= 2;
    else
      iw *= 2;
    if (iw > NVG_MAX_FONTIMAGE_SIZE || ih > NVG_MAX_FONTIMAGE_SIZE)
      iw = ih = NVG_MAX_FONTIMAGE_SIZE;

    int type = (ctx->params.flags & NVG_SDF_TEXT) ? NVG_TEXTURE_ALPHA : NVG_TEXTURE_FLOAT;
    int flag = (ctx->params.flags & NVG_SDF_TEXT) ? 0 : NVG_IMAGE_NEAREST;
    ctx->fontImages[ctx->fontImageIdx+1] =
        ctx->params.renderCreateTexture(ctx->params.userPtr, type, iw, ih, flag, NULL);
  }
  ++ctx->fontImageIdx;
  fonsGetAtlasSize(ctx->fs, NULL, NULL, &atlasFontPx);
  fonsResetAtlas(ctx->fs, iw, ih, atlasFontPx);
  return 1;
}

static void nvg__renderText(NVGcontext* ctx, FONSstate* fons, NVGvertex* verts, int nverts)
{
  NVGstate* state = nvg__getState(ctx);
  NVGpaint paint = state->fill;
  float* t = state->xform;
  float scale;
  int atlasFontPx;

  // paint xform is used to pass dx, dy, and atlas cell size to shader
  fonsGetAtlasSize(ctx->fs, NULL, NULL, &atlasFontPx);
  scale = atlasFontPx/fonsGetSize(fons);
  // this doesn't handle skew transform properly, but that's fine for now (just draw skewed texts as paths?)
  paint.xform[0] = nvg__sqrtf(t[0]*t[0] + t[2]*t[2])/scale;
  paint.xform[3] = nvg__sqrtf(t[1]*t[1] + t[3]*t[3])/scale;
  // atlas cell size
  paint.extent[0] = atlasFontPx;
  paint.extent[1] = atlasFontPx;
  // Render triangles.
  paint.image = ctx->fontImages[ctx->fontImageIdx];
  // Apply global alpha
  paint.innerColor.a *= state->alpha;
  paint.outerColor.a *= state->alpha;
  // feather is used a flag to enable gamma adjust for text
  //paint.feather = ctx->sRGBTextAdj ? 1 : 0;
  paint.radius = state->fontBlur;
  ctx->params.renderTriangles(ctx->params.userPtr, &paint, state->compositeOperation, &state->scissor, verts, nverts);
}

static void nvg__drawSTBTTGlyph(NVGcontext* ctx, stbtt_fontinfo* font, int glyph)
{
  stbtt_vertex* points;
  int n_points = stbtt_GetGlyphShape(font, glyph, &points);
  for (int i = 0; i < n_points; i++) {
    if (points[i].type == STBTT_vmove) {
      nvgMoveTo(ctx, points[i].x, points[i].y);
      nvgPathWinding(ctx, NVG_AUTOW);
      if(i == 0) {
        float restart[] = { NVG_RESTART };  // flag indicating start of new path (and not just subpath)
        nvg__appendCommands(ctx, restart, 1);
      }
    }
    else if (points[i].type == STBTT_vline)
      nvgLineTo(ctx, points[i].x, points[i].y);
    else if (points[i].type == STBTT_vcurve)
      nvgQuadTo(ctx, points[i].cx, points[i].cy, points[i].x, points[i].y);
    else if (points[i].type == STBTT_vcubic)
      nvgBezierTo(ctx, points[i].cx, points[i].cy, points[i].cx1, points[i].cy1, points[i].x, points[i].y);
  }
  stbtt_FreeShape(font, points);
}

// we expect that nvg__fonsSetup() has already been called
static float nvg__textAsPaths(NVGcontext* ctx, FONSstate* fons, float x, float y, const char* string, const char* end)
{
  NVGstate* state = nvg__getState(ctx);
  FONStextIter iter;
  FONSquad q;
  float xform[6];
  float scale, pxsize = fonsGetSize(fons);

  memcpy(xform, state->xform, sizeof(float)*6);
  fonsTextIterInit(fons, &iter, x, y, string, end, FONS_GLYPH_BITMAP_OPTIONAL);
  // put all glyphs into a single path for faster rendering - there should not be any overlap between
  //  glyph paths, so coverage from any glyphs sharing a pixel (at small font size) should be added instead
  //  of blended anyway
  nvgBeginPath(ctx);
  while (fonsTextIterNext(fons, &iter, &q)) {
    stbtt_fontinfo* font = (stbtt_fontinfo*)fonsGetFontImpl(ctx->fs, iter.prevGlyphFont);
    if (!font)
      continue;  // missing glyph
    scale = stbtt_ScaleForPixelHeight(font, pxsize);  // this is fast
    nvgTransform(ctx, scale, 0, 0, -scale, iter.x, iter.y);
    nvg__drawSTBTTGlyph(ctx, font, iter.prevGlyphIndex);
    memcpy(state->xform, xform, sizeof(float)*6);  // restore transform
  }
  //nvgFill(ctx); -- need to support stoked text too!
  return iter.nextx;
}

void nvgDrawSTBTTGlyph(NVGcontext* ctx, stbtt_fontinfo* font, float scale, int pad, int glyph)
{
  NVGstate* state = nvg__getState(ctx);
  int ix0, iy0, ix1, iy1;
  float xform[6];
  memcpy(xform, state->xform, sizeof(float)*6);
  //float scale = stbtt_ScaleForPixelHeight(font, pxsize);
  stbtt_GetGlyphBitmapBoxSubpixel(font, glyph, scale, scale, 0.0f,0.0f, &ix0,&iy0,&ix1,&iy1);
  //nvgTransform(ctx, scale, 0, 0, -scale, pad+ix0, pad-iy0);
  nvgTransform(ctx, scale, 0, 0, -scale, pad-ix0, pad-iy0);
  nvgBeginPath(ctx);
  nvg__drawSTBTTGlyph(ctx, font, glyph);
  nvgFill(ctx);
  memcpy(state->xform, xform, sizeof(float)*6);  // restore transform
}

static float nvg__textFromAtlas(NVGcontext* ctx, FONSstate* fons, float x, float y, const char* string, const char* end)
{
  NVGstate* state = nvg__getState(ctx);
  FONStextIter iter, prevIter;
  FONSquad q;
  NVGvertex* verts;
  float* tf = state->xform;
  int cverts = 0;
  int nverts = 0;
  // flag to reverse order of triangle vertices to ensure CCW winding (front face)
  // not sure if this is the correct criterion in general to determine if we need to reverse order
  int rev = (tf[0] * tf[3] < 0) ? 1 : 0;
  if(state->fontId == FONS_INVALID) return x;
  if (end == NULL)
    end = string + strlen(string);

  cverts = nvg__maxi(2, (int)(end - string)) * 6; // conservative estimate.
  verts = nvg__allocTempVerts(ctx, cverts);
  if (verts == NULL) return x;

  fonsTextIterInit(fons, &iter, x, y, string, end, FONS_GLYPH_BITMAP_REQUIRED);
  prevIter = iter;
  while (fonsTextIterNext(fons, &iter, &q)) {
    float c[4*2];
    if (iter.prevGlyphIndex == -1) { // can not retrieve glyph?
      if (nverts != 0) {
        nvg__renderText(ctx, fons, verts, nverts);
        nverts = 0;
      }
      if (!nvg__allocTextAtlas(ctx))
        break; // no memory :(
      iter = prevIter;
      fonsTextIterNext(fons, &iter, &q); // try again
      if (iter.prevGlyphIndex == -1) // still can not find glyph?
        break;
    }
    prevIter = iter;
    // Transform corners.
    nvgTransformPoint(&c[0],&c[1], tf, q.x0, q.y0);
    nvgTransformPoint(&c[2],&c[3], tf, q.x1, q.y0);
    nvgTransformPoint(&c[4],&c[5], tf, q.x1, q.y1);
    nvgTransformPoint(&c[6],&c[7], tf, q.x0, q.y1);
    // Create triangles
    if (nverts+6 <= cverts) {
      nvg__vset(&verts[nverts], c[0], c[1], q.s0, q.t0);
      nvg__vset(&verts[nverts+1+rev], c[4], c[5], q.s1, q.t1);
      nvg__vset(&verts[nverts+2-rev], c[2], c[3], q.s1, q.t0);
      nvg__vset(&verts[nverts+3], c[0], c[1], q.s0, q.t0);
      nvg__vset(&verts[nverts+4+rev], c[6], c[7], q.s0, q.t1);
      nvg__vset(&verts[nverts+5-rev], c[4], c[5], q.s1, q.t1);
      nverts += 6;
    }
  }
  // TODO: add back-end bit to do this just once per frame.
  nvg__flushTextTexture(ctx);
  nvg__renderText(ctx, fons, verts, nverts);
  return iter.nextx;
}

float nvgText(NVGcontext* ctx, float x, float y, const char* string, const char* end)
{
  NVGstate* state = nvg__getState(ctx);
  FONSstate fons;
  float* t = state->xform;
  float pxsize;

  nvg__fonsSetup(ctx, &fons);
  pxsize = fonsGetSize(&fons);
  if(ctx->atlasTextThresh <= 0
      || nvg__sqrtf(t[0]*t[0] + t[2]*t[2])*pxsize > ctx->atlasTextThresh
      || nvg__sqrtf(t[1]*t[1] + t[3]*t[3])*pxsize > ctx->atlasTextThresh
      || ((ctx->params.flags & NVG_ROTATED_TEXT_AS_PATHS) && (t[1] != 0.0f || t[2] != 0.0f))) {
    float nextx = nvg__textAsPaths(ctx, &fons, x, y, string, end);
    nvgFill(ctx);
    return nextx;
  }
  return nvg__textFromAtlas(ctx, &fons, x, y, string, end);
}

float nvgTextAsPaths(NVGcontext* ctx, float x, float y, const char* string, const char* end)
{
  FONSstate fons;
  nvg__fonsSetup(ctx, &fons);
  return nvg__textAsPaths(ctx, &fons, x, y, string, end);
}

void nvgTextBox(NVGcontext* ctx, float x, float y, float breakRowWidth, const char* string, const char* end)
{
  NVGstate* state = nvg__getState(ctx);
  FONStextRow rows[2];
  int nrows = 0, i;
  int oldAlign = state->textAlign;
  int halign = state->textAlign & (NVG_ALIGN_LEFT | NVG_ALIGN_CENTER | NVG_ALIGN_RIGHT);
  int valign = state->textAlign & (NVG_ALIGN_TOP | NVG_ALIGN_MIDDLE | NVG_ALIGN_BOTTOM | NVG_ALIGN_BASELINE);
  float lineh = 0;

  if (state->fontId == FONS_INVALID) return;
  nvgTextMetrics(ctx, NULL, NULL, &lineh);
  state->textAlign = NVG_ALIGN_LEFT | valign;

  while ((nrows = nvgTextBreakLines(ctx, string, end, breakRowWidth, rows, 2))) {
    for (i = 0; i < nrows; i++) {
      FONStextRow* row = &rows[i];
      if (halign & NVG_ALIGN_LEFT)
        nvgText(ctx, x, y, row->start, row->end);
      else if (halign & NVG_ALIGN_CENTER)
        nvgText(ctx, x + breakRowWidth*0.5f - row->width*0.5f, y, row->start, row->end);
      else if (halign & NVG_ALIGN_RIGHT)
        nvgText(ctx, x + breakRowWidth - row->width, y, row->start, row->end);
      y += lineh * state->lineHeight;
    }
    string = rows[nrows-1].next;
  }

  state->textAlign = oldAlign;
}

int nvgTextGlyphPositions(NVGcontext* ctx, float x, float y, const char* string, const char* end, NVGglyphPosition* positions, int maxPositions)
{
  NVGstate* state = nvg__getState(ctx);
  FONSstate fons;
  FONStextIter iter;
  FONSquad q;
  int npos = 0;

  if (state->fontId == FONS_INVALID) return 0;
  if (end == NULL)
    end = string + strlen(string);
  if (string == end)  return 0;

  nvg__fonsSetup(ctx, &fons);
  fonsTextIterInit(&fons, &iter, x, y, string, end, FONS_GLYPH_BITMAP_OPTIONAL);
  while (fonsTextIterNext(&fons, &iter, &q)) {
    positions[npos].str = iter.str;
    positions[npos].x = iter.x;
    positions[npos].minx = nvg__minf(iter.x, q.x0);
    positions[npos].maxx = nvg__maxf(iter.nextx, q.x1);
    npos++;
    if (npos >= maxPositions)
      break;
  }

  return npos;
}

int nvgTextBreakLines(NVGcontext* ctx, const char* string, const char* end, float breakRowWidth, FONStextRow* rows, int maxRows)
{
  FONSstate fons;
  nvg__fonsSetup(ctx, &fons);
  return fonsBreakLines(&fons, string, end, breakRowWidth, rows, maxRows);
}

float nvgTextBounds(NVGcontext* ctx, float x, float y, const char* string, const char* end, float* bounds)
{
  NVGstate* state = nvg__getState(ctx);
  FONSstate fons;
  float width;
  if (state->fontId == FONS_INVALID) return 0;
  nvg__fonsSetup(ctx, &fons);
  width = fonsTextBounds(&fons, x, y, string, end, bounds);
  // Use line bounds for height.
  if (bounds != NULL)
    fonsLineBounds(&fons, y, &bounds[1], &bounds[3]);
  return width;
}

void nvgTextBoxBounds(NVGcontext* ctx, float x, float y, float breakRowWidth, const char* string, const char* end, float* bounds)
{
  NVGstate* state = nvg__getState(ctx);
  FONStextRow rows[2];
  FONSstate fons;
  int nrows = 0, i;
  int oldAlign = state->textAlign;
  int halign = state->textAlign & (NVG_ALIGN_LEFT | NVG_ALIGN_CENTER | NVG_ALIGN_RIGHT);
  int valign = state->textAlign & (NVG_ALIGN_TOP | NVG_ALIGN_MIDDLE | NVG_ALIGN_BOTTOM | NVG_ALIGN_BASELINE);
  float lineh = 0, rminy = 0, rmaxy = 0;
  float minx, miny, maxx, maxy;

  if (state->fontId == FONS_INVALID) {
    if (bounds != NULL)
      bounds[0] = bounds[1] = bounds[2] = bounds[3] = 0.0f;
    return;
  }
  nvgTextMetrics(ctx, NULL, NULL, &lineh);
  state->textAlign = NVG_ALIGN_LEFT | valign;
  minx = maxx = x;
  miny = maxy = y;
  nvg__fonsSetup(ctx, &fons);
  fonsLineBounds(&fons, 0, &rminy, &rmaxy);

  while ((nrows = nvgTextBreakLines(ctx, string, end, breakRowWidth, rows, 2))) {
    for (i = 0; i < nrows; i++) {
      FONStextRow* row = &rows[i];
      float rminx, rmaxx, dx = 0;
      // Horizontal bounds
      if (halign & NVG_ALIGN_LEFT)
        dx = 0;
      else if (halign & NVG_ALIGN_CENTER)
        dx = breakRowWidth*0.5f - row->width*0.5f;
      else if (halign & NVG_ALIGN_RIGHT)
        dx = breakRowWidth - row->width;
      rminx = x + row->minx + dx;
      rmaxx = x + row->maxx + dx;
      minx = nvg__minf(minx, rminx);
      maxx = nvg__maxf(maxx, rmaxx);
      // Vertical bounds.
      miny = nvg__minf(miny, y + rminy);
      maxy = nvg__maxf(maxy, y + rmaxy);

      y += lineh * state->lineHeight;
    }
    string = rows[nrows-1].next;
  }

  state->textAlign = oldAlign;

  if (bounds != NULL) {
    bounds[0] = minx;
    bounds[1] = miny;
    bounds[2] = maxx;
    bounds[3] = maxy;
  }
}

void nvgTextMetrics(NVGcontext* ctx, float* ascender, float* descender, float* lineh)
{
  NVGstate* state = nvg__getState(ctx);
  FONSstate fons;
  if (state->fontId == FONS_INVALID) return;
  nvg__fonsSetup(ctx, &fons);
  fonsVertMetrics(&fons, ascender, descender, lineh);
}
