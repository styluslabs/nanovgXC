//
// Copyright (c) 2020 Stylus Labs - see LICENSE.txt
//   based on nanovg:
// Copyright (c) 2013 Mikko Mononen memon@inside.org
//
#ifndef NANOVG_GL_H
#define NANOVG_GL_H

#ifdef IDE_INCLUDES
// defines and includes to make IDE useful
#include "../example/platform.h"
#define NANOVG_GLES3_IMPLEMENTATION
#include "nanovg.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Create flags

enum NVGLcreateFlags {
  // Flag indicating that additional debug checks are done.
  NVGL_DEBUG = 1<<2,  // This value is hardcoded in Write's config - don't change!
  // disable GL_EXT_shader_framebuffer_fetch (FB fetch is slower than FB swap on Intel GPUs)
  NVGL_NO_FB_FETCH = 1<<4,
  // disable GL_OES_shader_image_atomic/GL_ARB_shader_image_load_store
  NVGL_NO_IMG_ATOMIC = 1<<5,
};

//#if defined NANOVG_GL2_IMPLEMENTATION
//#  define NANOVG_GL2 1
//#  define NANOVG_GL_IMPLEMENTATION 1
//#elif defined NANOVG_GLES2_IMPLEMENTATION
//#  define NANOVG_GLES2 1
//#  define NANOVG_GL_IMPLEMENTATION 1
#if defined NANOVG_GL3_IMPLEMENTATION
#  define NANOVG_GL3 1
#  define NANOVG_GL_IMPLEMENTATION 1
#  define NANOVG_GLU_IMPLEMENTATION 1
#  define NANOVG_GL_USE_UNIFORMBUFFER 1  // uniform buffers avail in OpenGL 3.1+
#elif defined NANOVG_GLES3_IMPLEMENTATION
#  define NANOVG_GLES3 1
#  define NANOVG_GL_IMPLEMENTATION 1
#  define NANOVG_GLU_IMPLEMENTATION 1
#  define NANOVG_GL_USE_UNIFORMBUFFER 0  // uniform buffer slower, at least on desktop and iPhone 6S
#endif

#ifndef NVG_HAS_EXT
#ifdef __glad_h_
#define NVG_HAS_EXT(ext) (GLAD_GL_##ext)
#elif defined(GLEW_VERSION)
#define NVG_HAS_EXT(ext) (GLEW_##ext)
#elif defined(__APPLE__) && (TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE)
// assumes GL_##ext is defined
#define NVG_HAS_EXT(ext) (1)
#else
#define NVG_HAS_EXT(ext) (strstr((const char*)glGetString(GL_EXTENSIONS), #ext) != NULL)
#endif
#endif

#define NANOVG_GL_USE_STATE_FILTER (0)

// Create a NanoVG context; flags should be combination of the create flags above.
NVGcontext* nvglCreate(int flags);
void nvglDelete(NVGcontext* ctx);

int nvglCreateImageFromHandle(NVGcontext* ctx, GLuint textureId, int w, int h, int flags);
GLuint nvglImageHandle(NVGcontext* ctx, int image);

// These are additional flags on top of NVGimageFlags.
enum NVGimageFlagsGL {
  NVG_IMAGE_NODELETE			= 1<<16,	// Do not delete GL texture handle.
};

#ifdef __cplusplus
}
#endif

#endif /* NANOVG_GL_H */

#ifdef NANOVG_GL_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "nanovg.h"

enum GLNVGuniformLoc {
  GLNVG_LOC_VIEWSIZE,
  GLNVG_LOC_TEX,
  GLNVG_LOC_TYPE,
  GLNVG_LOC_WINDINGTEX,
  GLNVG_LOC_WINDINGYMIN,
  GLNVG_LOC_FRAG,
  GLNVG_LOC_IMGPARAMS,
  GLNVG_LOC_WINDINGIMG,
  GLNVG_MAX_LOCS
};

enum GLNVGshaderType {
  NSVG_SHADER_WINDINGCLEAR = 0,
  NSVG_SHADER_WINDINGCALC = 1,
  NSVG_SHADER_FILLSOLID = 2,
  NSVG_SHADER_FILLGRAD = 3,
  NSVG_SHADER_FILLIMG = 4,
  NSVG_SHADER_TEXT = 5
};

enum GLNVGrenderMethod {
  NVGL_RENDER_FB_SWAP = 0,
  NVGL_RENDER_FB_FETCH = 1,
  NVGL_RENDER_IMG_ATOMIC = 2
};

#if NANOVG_GL_USE_UNIFORMBUFFER
enum GLNVGuniformBindings {
  GLNVG_FRAG_BINDING = 0,
};
#endif

struct GLNVGshader {
  GLuint prog;
  GLuint frag;
  GLuint vert;
  GLint loc[GLNVG_MAX_LOCS];
};
typedef struct GLNVGshader GLNVGshader;

struct GLNVGtexture {
  int id;
  GLuint tex;
  int width, height;
  int type;
  int flags;
};
typedef struct GLNVGtexture GLNVGtexture;

struct GLNVGblend
{
  GLenum srcRGB;
  GLenum dstRGB;
  GLenum srcAlpha;
  GLenum dstAlpha;
};
typedef struct GLNVGblend GLNVGblend;

enum GLNVGcallType {
  GLNVG_NONE = 0,
  GLNVG_FILL,
  GLNVG_CONVEXFILL,
  GLNVG_TRIANGLES,
};

struct GLNVGcall {
  int type;
  int fragType;
  int image;
  int pathOffset;
  int pathCount;
  int triangleOffset;
  int triangleCount;
  int uniformOffset;
  int imgOffset;
  int bounds[4];
  GLNVGblend blendFunc;
};
typedef struct GLNVGcall GLNVGcall;

struct GLNVGpath {
  int fillOffset;
  int fillCount;
  float yMin;  // for winding number calc
};
typedef struct GLNVGpath GLNVGpath;

struct NVGLcolor {
  float r,g,b,a;
};
typedef struct NVGLcolor NVGLcolor;

struct GLNVGfragUniforms {
  #if NANOVG_GL_USE_UNIFORMBUFFER
    float scissorMat[12]; // matrices are actually 3 vec4s
    float paintMat[12];
    NVGLcolor innerCol;
    NVGLcolor outerCol;
    float scissorExt[2];
    float scissorScale[2];
    float extent[2];
    float radius;
    float feather;
    float strokeMult;
    float strokeThr;
    int texType;
    int fillMode;
  #else
    // note: after modifying layout or size of uniform array,
    // don't forget to also update the fragment shader source!
    #define NANOVG_GL_UNIFORMARRAY_SIZE 11
    union {
      struct {
        float scissorMat[12]; // matrices are actually 3 vec4s
        float paintMat[12];
        NVGLcolor innerCol;
        NVGLcolor outerCol;
        float scissorExt[2];
        float scissorScale[2];
        float extent[2];
        float radius;
        float feather;
        float strokeMult;
        float strokeThr;
        float texType;
        float fillMode;
      };
      float uniformArray[NANOVG_GL_UNIFORMARRAY_SIZE][4];
    };
  #endif
};
typedef struct GLNVGfragUniforms GLNVGfragUniforms;

struct GLNVGcontext {
  GLNVGshader shader;
  GLNVGtexture* textures;
  float view[2];
  int ntextures;
  int ctextures;
  int textureId;
  GLuint vertBuf;
#if defined NANOVG_GL3
  GLuint vertArr;
#endif
#if NANOVG_GL_USE_UNIFORMBUFFER
  GLuint fragBuf;
#endif

  GLuint fbUser;
  GLuint fbWinding;
  GLuint texWinding;
  GLuint texColor;  // only for GL_EXT_shader_framebuffer_fetch case
  float devicePixelRatio;

  int fragSize;
  int flags;
  int renderMethod;

  // Per frame buffers
  GLNVGcall* calls;
  int ccalls;
  int ncalls;
  GLNVGpath* paths;
  int cpaths;
  int npaths;
  struct NVGvertex* verts;
  int cverts;
  int nverts;
  unsigned char* uniforms;
  int cuniforms;
  int nuniforms;

  int imgWindingOffset;
  int imgWindingSize;

  // cached state
  #if NANOVG_GL_USE_STATE_FILTER
  GLuint boundTexture;
  GLNVGblend blendFunc;
  #endif
};
typedef struct GLNVGcontext GLNVGcontext;

#ifndef NVG_LOG
#include <stdio.h>
#define NVG_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

static int glnvg__maxi(int a, int b) { return a > b ? a : b; }
static int glnvg__clampi(int a, int mn, int mx) { return a < mn ? mn : (a > mx ? mx : a); }

#ifdef NANOVG_GLES2
static unsigned int glnvg__nearestPow2(unsigned int num)
{
  unsigned n = num > 0 ? num - 1 : 0;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n++;
  return n;
}
#endif

static void glnvg__bindTexture(GLNVGcontext* gl, GLuint tex)
{
#if NANOVG_GL_USE_STATE_FILTER
  if (gl->boundTexture != tex) {
    gl->boundTexture = tex;
    glBindTexture(GL_TEXTURE_2D, tex);
  }
#else
  glBindTexture(GL_TEXTURE_2D, tex);
#endif
}

static void glnvg__blendFuncSeparate(GLNVGcontext* gl, const GLNVGblend* blend)
{
#if NANOVG_GL_USE_STATE_FILTER
  if ((gl->blendFunc.srcRGB != blend->srcRGB) ||
    (gl->blendFunc.dstRGB != blend->dstRGB) ||
    (gl->blendFunc.srcAlpha != blend->srcAlpha) ||
    (gl->blendFunc.dstAlpha != blend->dstAlpha)) {

    gl->blendFunc = *blend;
    glBlendFuncSeparate(blend->srcRGB, blend->dstRGB, blend->srcAlpha,blend->dstAlpha);
  }
#else
  glBlendFuncSeparate(blend->srcRGB, blend->dstRGB, blend->srcAlpha,blend->dstAlpha);
#endif
}

static GLNVGtexture* glnvg__allocTexture(GLNVGcontext* gl)
{
  GLNVGtexture* tex = NULL;
  int i;

  for (i = 0; i < gl->ntextures; i++) {
    if (gl->textures[i].id == 0) {
      tex = &gl->textures[i];
      break;
    }
  }
  if (tex == NULL) {
    if (gl->ntextures+1 > gl->ctextures) {
      GLNVGtexture* textures;
      int ctextures = glnvg__maxi(gl->ntextures+1, 4) +  gl->ctextures/2; // 1.5x Overallocate
      textures = (GLNVGtexture*)realloc(gl->textures, sizeof(GLNVGtexture)*ctextures);
      if (textures == NULL) return NULL;
      gl->textures = textures;
      gl->ctextures = ctextures;
    }
    tex = &gl->textures[gl->ntextures++];
  }
  memset(tex, 0, sizeof(*tex));
  tex->id = ++gl->textureId;
  return tex;
}

static GLNVGtexture* glnvg__findTexture(GLNVGcontext* gl, int id)
{
  int i;
  if (!id) return NULL;
  for (i = 0; i < gl->ntextures; i++)
    if (gl->textures[i].id == id)
      return &gl->textures[i];
  return NULL;
}

static int glnvg__deleteTexture(GLNVGcontext* gl, int id)
{
  int i;
  for (i = 0; i < gl->ntextures; i++) {
    if (gl->textures[i].id == id) {
      if (gl->textures[i].tex != 0 && (gl->textures[i].flags & NVG_IMAGE_NODELETE) == 0)
        glDeleteTextures(1, &gl->textures[i].tex);
      memset(&gl->textures[i], 0, sizeof(gl->textures[i]));
      return 1;
    }
  }
  return 0;
}

static void glnvg__checkError(GLNVGcontext* gl, const char* str)
{
  GLenum err;
  if ((gl->flags & NVGL_DEBUG) == 0) return;
  while ((err = glGetError()) != GL_NO_ERROR) {
    NVG_LOG("Error 0x%08x after %s\n", err, str);
  }
}

static int glnvg__compileShader(GLuint shader, const char* source[], int nsource)
{
  GLint status;
  glShaderSource(shader, nsource, source, 0);
  glCompileShader(shader);
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if(status != GL_TRUE) {
    GLchar str[512+1];
    GLsizei len = 0;
    glGetShaderInfoLog(shader, 512, &len, str);
    str[len > 512 ? 512 : len] = '\0';
    NVG_LOG("Shader error:\n%s\n", str);
#ifndef NDEBUG
    NVG_LOG("Shader source:\n");
    for(int ii = 0; ii < nsource; ++ii)
      NVG_LOG(source[ii]);
#endif
    return 0;
  }
  return 1;
}

static int glnvg__createProgram(GLNVGshader* shader, const char* vertsrc[], int nvertsrc, const char* fragsrc[], int nfragsrc)
{
  GLint status;

  shader->prog = glCreateProgram();
  shader->vert = glCreateShader(GL_VERTEX_SHADER);
  shader->frag = glCreateShader(GL_FRAGMENT_SHADER);
  if(glnvg__compileShader(shader->vert, vertsrc, nvertsrc) == 0)
    return 0;
  if(glnvg__compileShader(shader->frag, fragsrc, nfragsrc) == 0)
    return 0;

  glAttachShader(shader->prog, shader->vert);
  glAttachShader(shader->prog, shader->frag);

  glBindAttribLocation(shader->prog, 0, "va_in");
  glBindAttribLocation(shader->prog, 1, "vb_in");

  glLinkProgram(shader->prog);
  glGetProgramiv(shader->prog, GL_LINK_STATUS, &status);
  if(status != GL_TRUE) {
    GLchar str[512+1];
    GLsizei len = 0;
    glGetProgramInfoLog(shader->prog, 512, &len, str);
    str[len > 512 ? 512 : len] = '\0';
    NVG_LOG("Program error:\n%s\n", str);
    return 0;
  }
  return 1;
}

static void glnvg__deleteShader(GLNVGshader* shader)
{
  if (shader->prog != 0)
    glDeleteProgram(shader->prog);
  if (shader->vert != 0)
    glDeleteShader(shader->vert);
  if (shader->frag != 0)
    glDeleteShader(shader->frag);
}

static void glnvg__getUniforms(GLNVGshader* shader)
{
  shader->loc[GLNVG_LOC_VIEWSIZE] = glGetUniformLocation(shader->prog, "viewSize");
  shader->loc[GLNVG_LOC_TEX] = glGetUniformLocation(shader->prog, "tex");
  shader->loc[GLNVG_LOC_TYPE] = glGetUniformLocation(shader->prog, "type");
  shader->loc[GLNVG_LOC_WINDINGTEX] = glGetUniformLocation(shader->prog, "windingTex");
  shader->loc[GLNVG_LOC_WINDINGYMIN] = glGetUniformLocation(shader->prog, "pathYMin");
  shader->loc[GLNVG_LOC_IMGPARAMS] = glGetUniformLocation(shader->prog, "imgParams");
  shader->loc[GLNVG_LOC_WINDINGIMG] = glGetUniformLocation(shader->prog, "windingImg");

#if NANOVG_GL_USE_UNIFORMBUFFER
  shader->loc[GLNVG_LOC_FRAG] = glGetUniformBlockIndex(shader->prog, "frag");
#else
  shader->loc[GLNVG_LOC_FRAG] = glGetUniformLocation(shader->prog, "frag");
#endif
}

/* Consider a directed line segment va -> vb of the path being rendered:

^ Y      v=(v0.x - 0.55, ymax + 0.55)
| ymax   ^       v1
|  n=(-1,1) XXXXX/| n=(1,1) -> v=(v1.x + 0.55, ymax + 0.55)
|           XXXX/#|
|           XXX/##|
|           XX/###|
|           X/####|   v0,v1 = va,vb if va.x < vb.x else vb,va
|           /#####|
|         v0|#####|
|           |#####|
| pathYmin__|#####|__
|   n=(-1,-1)     n=(1,-1) -> v=(v1.x + 0.55, pathYmin - 0.55)
|          ^ v=(v0.x - 0.55, pathYmin - 0.55)
+---------------------> X

nanovg.c converts all paths to polygons by converting strokes to fills and approximating curves w/ line
 segments.  For each path, the minimum y value (i.e. bottom of AABB) is passed to the VS as pathYmin.

VS: for each line segment of the polygon, 1 quad = 2 triangles = 6 vertices (4 unique) are generated; the
 vertices are distinguished by "normal_in" in the VS (n=... in the figure).  The VS outputs vertices (v=... in
 the figure) to create the quad '#' + 'X', expanding the edges of the quad by 0.55px (0.5px wasn't sufficient
 in some cases on iOS ... why?) to ensure partially convered pixels are included (triangle must cover center
 of a pixel for the pixel to be included by OpenGL rasterization).  Note that the expansion will not be
 considered part of '#' + 'X' in the discussion below.

1st FS pass: With fpos being the center of the pixel, areaEdge2(vb - fpos, va - fpos) in the fragment shader
 calculates the signed area of the intersection of trapezoid '#' with the 1px x 1px square for the pixel.
 The area will be zero if va.x == vb.x or if the pixel lies entirely in the 'X' region.  Otherwise, it will
 be positive if va.x > vb.x and negative of va.x < vb.x.  This is converted to a fixed point value and added
 to the accumulated area from all trapezoids (i.e., edges) for the pixel, stored in the framebuffer for
 framebuffer fetch or swap, or in an iimage2D for image load/store.  In the latter case, pixels of the
 bounding boxes of each polygon are mapped to sequential runs of pixels in the iimage2D.

2nd FS pass: The intersection area (i.e. pixel coverage) is read using framebuffer fetch or image load/store
 and multiplied by the input RGBA color (solid color, gradient, or color from image - usually not antialiased!)
*/

// makes editing easier, but disadvantage is that commas can only be used inside ()
#define NVG_QUOTE(s) #s

static int glnvg__renderCreate(void* uptr)
{
  static const char* shaderHeader =
#if defined NANOVG_GL2
    "#define NANOVG_GL2 1\n"
#elif defined NANOVG_GL3
    "#version 330 core\n"  //"#version 150 core\n"
    "#define NANOVG_GL3 1\n"
#elif defined NANOVG_GLES2
    "#version 100\n"
    "#define NANOVG_GL2 1\n"
#elif defined NANOVG_GLES3 && defined GL_ES_VERSION_3_1
    "#version 310 es\n"
    "#define NANOVG_GL3 1\n"
#elif defined NANOVG_GLES3
    "#version 300 es\n"
    "#define NANOVG_GL3 1\n"
#endif
// append uniform buffer string
#if NANOVG_GL_USE_UNIFORMBUFFER
  "#define USE_UNIFORMBUFFER 1\n"
#else
  "#define UNIFORMARRAY_SIZE 11\n"
#endif
  "\n";

  static const char* fillVertShader = NVG_QUOTE(
\n  #ifdef NANOVG_GL3
\n  #define attribute in
\n  #define varying out
\n  #endif
\n
\n  uniform vec2 viewSize;
\n  uniform float pathYMin;
\n  uniform int type;
\n
\n  //attribute vec2 normal_in;
\n  attribute vec2 va_in;
\n  attribute vec2 vb_in;
\n
\n  varying vec2 va;
\n  varying vec2 vb;
\n
\n  const vec2 qverts[6] = vec2[](
\n    vec2(-1.0f,  1.0f),
\n    vec2( 1.0f,  1.0f),
\n    vec2( 1.0f, -1.0f),
\n    vec2(-1.0f,  1.0f),
\n    vec2( 1.0f, -1.0f),
\n    vec2(-1.0f, -1.0f));
\n
\n  void main()
\n  {
\n    // nanovg passes vertices in screen coords!
\n    va = va_in;  //0.5f*view_wh*(vec2(1.0f) + va_in);
\n    vb = vb_in;  //0.5f*view_wh*(vec2(1.0f) + vb_in);
\n
\n    vec2 norm = qverts[gl_VertexID % 6];
\n
\n    // normal.x > 0: we are right, otherwise left
\n    vec2 pos = (norm.x > 0.0f) == (va_in.x > vb_in.x) ? va_in : vb_in;
\n
\n    // trapezoid will have less overdraw in some (most?) cases, but more complicated calc, so make a rectangle
\n    float y_max = max(va_in.y, vb_in.y);
\n    pos.y = norm.y > 0.0f ? y_max : pathYMin;
\n
\n    // expand and handle special case: normal == 0 -> va is position, vb is texture coord
\n    vec2 pos_ex = (type != 1) ? va_in : (pos + 0.55f*norm);
\n
\n    // convert from screen coords to clip coords
\n    //gl_Position = vec4(2.0f*pos_ex/viewSize - 1.0f, 0.0f, 1.0f);
\n    gl_Position = vec4(2.0f*pos_ex.x/viewSize.x - 1.0f, 1.0f - 2.0f*pos_ex.y/viewSize.y, 0.0f, 1.0f);
\n  }
  );

  // Use github.com/KhronosGroup/glslang (apt install glslang-tools) to validate shader source
  static const char* fillFragShader = NVG_QUOTE
   (
\n  #ifdef USE_FRAMEBUFFER_FETCH
\n  // #extension must come before any other statements, including precision
\n  #extension GL_EXT_shader_framebuffer_fetch : require
\n  #elif defined(USE_IMAGE_LOADSTORE)
\n  #ifdef GL_ES
\n  #extension GL_OES_shader_image_atomic : require
\n  #else
\n  #extension GL_ARB_shader_image_load_store : require
\n  #endif
\n  #endif
\n
\n  #ifdef GL_ES
\n  #if defined(GL_FRAGMENT_PRECISION_HIGH) || defined(NANOVG_GL3)
\n  precision highp float;
\n  precision highp int;
\n  precision highp sampler2D;
\n  #else
\n  precision mediump float;
\n  #endif
\n  #endif
\n
\n  #ifdef NANOVG_GL3
\n  #define texture2D texture
\n  #define varying in
\n  #endif
\n
\n  // these must match NVGpathFlags
\n  #define NVG_PATH_EVENODD 0x1
\n  #define NVG_PATH_NO_AA 0x2
\n  #define NVG_PATH_CONVEX 0x4
\n
\n  #ifdef USE_FRAMEBUFFER_FETCH
\n  layout (location = 0) inout vec4 outColor;
\n  layout (location = 1) inout int inoutWinding;
\n  #elif defined(NANOVG_GL3)
\n  layout (location = 0) out vec4 outColor;
\n  #else
\n  #define outColor gl_FragData[0]
\n  #endif
\n
\n  #define WINDING_SCALE 65536.0
\n  #ifdef USE_IMAGE_LOADSTORE
\n  #ifdef GL_ES
\n  layout(binding = 0, r32i) coherent uniform highp iimage2D windingImg;
\n  #else
\n  layout(r32i) coherent uniform highp iimage2D windingImg;  // binding = 0 ... GLES 3.1 or GL 4.2+ only
\n  #endif
\n  uniform ivec4 imgParams;
\n  #define imgOffset imgParams.x
\n  #define imgQuadStride imgParams.y
\n  #define imgStride imgParams.z
\n  #define disableAA (imgParams.w != 0)
\n  #elif !defined(USE_FRAMEBUFFER_FETCH)
\n  uniform sampler2D windingTex;
\n  #endif
\n
\n  #ifdef USE_UNIFORMBUFFER
\n    layout(std140) uniform frag {
\n      mat3 scissorMat;
\n      mat3 paintMat;
\n      vec4 innerCol;
\n      vec4 outerCol;
\n      vec2 scissorExt;
\n      vec2 scissorScale;
\n      vec2 extent;
\n      float radius;
\n      float feather;
\n      float strokeMult;
\n      float strokeThr;
\n      int texType;
\n      int fillMode;
\n    };
\n  #else
\n    uniform vec4 frag[UNIFORMARRAY_SIZE];
\n    #define scissorMat mat3(frag[0].xyz, frag[1].xyz, frag[2].xyz)
\n    #define paintMat mat3(frag[3].xyz, frag[4].xyz, frag[5].xyz)
\n    #define innerCol frag[6]
\n    #define outerCol frag[7]
\n    #define scissorExt frag[8].xy
\n    #define scissorScale frag[8].zw
\n    #define extent frag[9].xy
\n    #define radius frag[9].z
\n    #define feather frag[9].w
\n    #define strokeMult frag[10].x
\n    #define strokeThr frag[10].y
\n    #define texType int(frag[10].z)
\n    #define fillMode int(frag[10].w)
\n  #endif
\n  #ifndef USE_IMAGE_LOADSTORE
\n  #define disableAA ((fillMode & NVG_PATH_NO_AA) != 0)
\n  #endif
\n  uniform sampler2D tex;
\n  uniform vec2 viewSize;
\n  uniform int type;
\n
\n  varying vec2 va;
\n  varying vec2 vb;
\n  //#define fpos va
\n  #define ftcoord vb
\n
\n  float coversCenter(vec2 v0, vec2 v1)
\n  {
\n    // no AA - just determine if center of pixel (0,0) is inside trapezoid
\n    if(v1.x <= 0.0f || v0.x > 0.0f || v0.x == v1.x)
\n      return 0.0f;
\n    return v0.y*(v1.x - v0.x) - v0.x*(v1.y - v0.y) > 0.0f ? 1.0f : 0.0f;
\n  }
\n
\n  // unlike areaEdge(), this assumes pixel center is (0,0), not (0.5, 0.5)
\n  float areaEdge2(vec2 v0, vec2 v1)
\n  {
\n    if(disableAA)
\n      return v1.x < v0.x ? coversCenter(v1, v0) : -coversCenter(v0, v1);
\n    vec2 window = clamp(vec2(v0.x, v1.x), -0.5f, 0.5f);
\n    float width = window.y - window.x;
\n    //if(v0.y > 0.5f && v1.y > 0.5f)  -- didn't see a significant effect on Windows
\n    //  return -width;
\n    vec2 dv = v1 - v0;
\n    float slope = dv.y/dv.x;
\n    float midx = 0.5f*(window.x + window.y);
\n    float y = v0.y + (midx - v0.x)*slope;  // y value at middle of window
\n    float dy = abs(slope*width);
\n    // credit for this to https://git.sr.ht/~eliasnaur/gio/tree/master/gpu/shaders/stencil.frag
\n    // if width == 1 (so midx == 0), the components of sides are: y crossing of right edge of frag, y crossing
\n    //  of left edge, x crossing of top edge, x crossing of bottom edge.  Since we only consider positive slope
\n    //  (note abs() above), there are five cases (below, bottom-right, left-right, left-top, above) - the area
\n    //  formula below reduces to these cases thanks to the clamping of the other values to 0 or 1.
\n    // I haven't thought carefully about the width < 1 case, but experimentally it matches areaEdge()
\n    vec4 sides = vec4(y + 0.5f*dy, y - 0.5f*dy, (0.5f - y)/dy, (-0.5f - y)/dy);  //ry, ly, tx, bx
\n    sides = clamp(sides + 0.5f, 0.0f, 1.0f);  // shift from -0.5..0.5 to 0..1 for area calc
\n    float area = 0.5f*(sides.z - sides.z*sides.y - 1.0f - sides.x + sides.x*sides.w);
\n    return width == 0.0f ? 0.0f : area * width;
\n  }
\n
\n  #ifdef USE_IMAGE_LOADSTORE
\n  // imageBuffer image type allows for 1D buffer, but max size may be very small on some GPUs
\n  ivec2 imgCoords()
\n  {
\n    int idx = imgOffset + int(gl_FragCoord.x) + int(gl_FragCoord.y)*imgQuadStride;
\n    int y = idx/imgStride;
\n    return ivec2(idx - y*imgStride, y);
\n  }
\n  #endif
\n
\n  float coverage()
\n  {
\n    if((fillMode & NVG_PATH_CONVEX) != 0)
\n      return 1.0f;
\n  #ifdef USE_FRAMEBUFFER_FETCH
\n    float W = float(inoutWinding)/WINDING_SCALE;
\n  #elif defined(USE_IMAGE_LOADSTORE)
\n    float W = float(imageAtomicExchange(windingImg, imgCoords(), 0))/WINDING_SCALE;
\n  #else
\n    float W = texelFetch(windingTex, ivec2(gl_FragCoord.xy), 0).r;
\n    //float W = texture2D(windingTex, gl_FragCoord.xy/viewSize).r;  // note .r (first) component
\n  #endif
\n    // even-odd fill if bit 0 set, otherwise winding fill
\n    return ((fillMode & NVG_PATH_EVENODD) == 0) ? min(abs(W), 1.0f) : (1.0f - abs(mod(W, 2.0f) - 1.0f));
\n    // previous incorrect calculation for no AA case wrapped these in round(x) := floor(x + 0.5f)
\n  }
\n
\n  float sdroundrect(vec2 pt, vec2 ext, float rad)
\n  {
\n    vec2 ext2 = ext - vec2(rad,rad);
\n    vec2 d = abs(pt) - ext2;
\n    return min(max(d.x,d.y),0.0) + length(max(d,0.0)) - rad;
\n  }
\n
\n  // Scissoring
\n  float scissorMask(vec2 p)
\n  {
\n    vec2 sc = (abs((scissorMat * vec3(p,1.0)).xy) - scissorExt);
\n    sc = vec2(0.5,0.5) - sc * scissorScale;
\n    return clamp(sc.x,0.0,1.0) * clamp(sc.y,0.0,1.0);
\n  }
\n
\n  #ifdef USE_SDF_TEXT
\n  // Super-sampled SDF text rendering - super-sampling gives big improvement at very small sizes; quality is
\n  //  comparable to summed text; w/ supersamping, FPS is actually slightly lower
\n  float sdfCov(float D, float sdfscale)
\n  {
\n    // Could we use derivative info (and/or distance at pixel center) to improve?
\n    return D > 0.0f ? clamp((D - 0.5f)/sdfscale + radius, 0.0f, 1.0f) : 0.0f;  //+ 0.25f
\n  }
\n
\n  float superSDF(sampler2D tex, vec2 st)
\n  {
\n    vec2 tex_wh = vec2(textureSize(tex, 0));  // convert from ivec2 to vec2
\n    //st = st + vec2(4.0)/tex_wh;  // account for 4 pixel padding in SDF
\n    float s = (32.0f/255.0f)*paintMat[0][0];  // 32/255 is STBTT pixel_dist_scale
\n    //return sdfCov(texture2D(tex, st).r, s);  // single sample
\n    s = 0.5f*s;  // we're sampling 4 0.5x0.5 subpixels
\n    float dx = paintMat[0][0]/tex_wh.x/4.0f;
\n    float dy = paintMat[1][1]/tex_wh.y/4.0f;
\n
\n    //vec2 stextent = extent/tex_wh;  ... clamping doesn't seem to be necessary
\n    //vec2 stmin = floor(st*stextent)*stextent;
\n    //vec2 stmax = stmin + stextent - vec2(1.0f);
\n    float d11 = texture2D(tex, st + vec2(dx, dy)).r;  // clamp(st + ..., stmin, stmax)
\n    float d10 = texture2D(tex, st + vec2(dx,-dy)).r;
\n    float d01 = texture2D(tex, st + vec2(-dx, dy)).r;
\n    float d00 = texture2D(tex, st + vec2(-dx,-dy)).r;
\n    return 0.25f*(sdfCov(d11, s) + sdfCov(d10, s) + sdfCov(d01, s) + sdfCov(d00, s));
\n  }
\n  #else
\n  // artifacts w/ GL_LINEAR on Intel GPU and GLES doesn't support texture filtering for f32, so do it ourselves
\n  // also, min/mag filter must be set to GL_NEAREST for float texture or texelFetch() will fail on Mali GPUs
\n  float texFetchLerp(sampler2D texture, vec2 ij, vec2 ijmin, vec2 ijmax)
\n  {
\n    vec2 ij00 = clamp(ij, ijmin, ijmax);
\n    vec2 ij11 = clamp(ij + vec2(1.0f), ijmin, ijmax);
\n    float t00 = texelFetch(texture, ivec2(ij00.x, ij00.y), 0).r;  // implicit floor()
\n    float t10 = texelFetch(texture, ivec2(ij11.x, ij00.y), 0).r;
\n    float t01 = texelFetch(texture, ivec2(ij00.x, ij11.y), 0).r;
\n    float t11 = texelFetch(texture, ivec2(ij11.x, ij11.y), 0).r;
\n    vec2 f = ij - floor(ij);
\n    //return mix(mix(t00, t10, f.x), mix(t01, t11, f.x), f.y);
\n    float t0 = t00 + f.x*(t10 - t00);
\n    float t1 = t01 + f.x*(t11 - t01);
\n    return t0 + f.y*(t1 - t0);
\n  }
\n
\n  float summedTextCov(sampler2D texture, vec2 st)
\n  {
\n    ivec2 tex_wh = textureSize(texture, 0);
\n    vec2 ij = st*vec2(tex_wh);  // - vec2(1.0f)  -- now done after finding ijmin,max
\n    vec2 ijmin = floor(ij/extent)*extent;
\n    vec2 ijmax = ijmin + extent - vec2(1.0f);
\n    // for some reason, we need to shift by an extra (-0.5, -0.5) for summed case (here or in fons__getQuad)
\n    ij -= vec2(0.999999f);
\n    float dx = paintMat[0][0]/2.0f;
\n    float dy = paintMat[1][1]/2.0f;
\n    float s11 = texFetchLerp(texture, ij + vec2(dx, dy), ijmin, ijmax);
\n    float s01 = texFetchLerp(texture, ij + vec2(-dx, dy), ijmin, ijmax);
\n    float s10 = texFetchLerp(texture, ij + vec2(dx,-dy), ijmin, ijmax);
\n    float s00 = texFetchLerp(texture, ij + vec2(-dx,-dy), ijmin, ijmax);
\n    float cov = (s11 - s01 - s10 + s00)/(255.0f*4.0f*dx*dy);
\n    return clamp(cov, 0.0f, 1.0f);
\n  }
\n  #endif
\n
\n  void main(void)
\n  {
\n    vec4 result;
\n    vec2 fpos = vec2(gl_FragCoord.x, viewSize.y - gl_FragCoord.y);
\n    int winding = 0;
\n    if (type == 0) {  // clear winding number (not used in USE_FRAMEBUFFER_FETCH case)
\n      result = vec4(0.0f);
\n    } else if (type == 1) {  // calculate winding
\n      float W = areaEdge2(vb - fpos, va - fpos);
\n  #ifdef USE_FRAMEBUFFER_FETCH
\n      result = vec4(0.0f);
\n      winding = int(W*WINDING_SCALE) + inoutWinding;
\n  #elif defined(USE_IMAGE_LOADSTORE)
\n      result = vec4(0.0f);
\n      imageAtomicAdd(windingImg, imgCoords(), int(W*WINDING_SCALE));
\n      //discard;  -- doesn't seem to affect performance
\n  #else
\n      result = vec4(W);
\n  #endif
\n    } else if (type == 2) {  // Solid color
\n      result = innerCol*(scissorMask(fpos)*coverage());
\n    } else if (type == 3) {  // Gradient
\n      // Calculate gradient color using box gradient
\n      vec2 pt = (paintMat * vec3(fpos,1.0)).xy;
\n      float d = clamp((sdroundrect(pt, extent, radius) + feather*0.5) / feather, 0.0, 1.0);
\n      vec4 color = texType > 0 ? texture2D(tex, vec2(d,0)) : mix(innerCol,outerCol,d);
\n      if (texType == 1) color = vec4(color.rgb*color.a, color.a);
\n      // Combine alpha
\n      result = color*(scissorMask(fpos)*coverage());
\n    } else if (type == 4) {  // Image
\n      // Calculate color from texture
\n      vec2 pt = (paintMat * vec3(fpos,1.0)).xy / extent;
\n      vec4 color = texture2D(tex, pt);
\n      if (texType == 1) color = vec4(color.rgb*color.a, color.a);
\n      else if (texType == 2) color = vec4(color.r);
\n      // Apply color tint and alpha.
\n      color *= innerCol;
\n      // Combine alpha
\n      result = color*(scissorMask(fpos)*coverage());
\n    } else if (type == 5) {  // Textured tris - only used for text, so no need for coverage()
\n  #ifdef USE_SDF_TEXT
\n      float cov = scissorMask(fpos)*superSDF(tex, ftcoord);
\n  #else
\n      float cov = scissorMask(fpos)*summedTextCov(tex, ftcoord);
\n  #endif
\n      result = vec4(cov) * innerCol;
\n      // this is wrong - see alternative outColor calc below for correct text gamma handling
\n      //result = innerCol*pow(vec4(cov), vec4(1.5,1.5,1.5,0.5));
\n    }
\n  #ifdef USE_FRAMEBUFFER_FETCH
\n    outColor = result + (1.0f - result.a)*outColor;
\n    //outColor.rgb = pow(pow(result.rgb, vec3(1.5/2.2)) + (1.0f - result.a)*pow(outColor.rgb, vec3(1.5/2.2)), vec3(2.2/1.5));
\n    inoutWinding = winding;
\n  #else
\n    outColor = result;
\n  #endif
\n  }
  );

  GLNVGcontext* gl = (GLNVGcontext*)uptr;
  int align = 4;
  const char* shaderOpts = "";

  glnvg__checkError(gl, "init");
  gl->renderMethod = NVGL_RENDER_FB_SWAP;  // default
#if defined(GL_EXT_shader_framebuffer_fetch)
  if (!(gl->flags & NVGL_NO_FB_FETCH)) {
    if (NVG_HAS_EXT(EXT_shader_framebuffer_fetch))
      gl->renderMethod = NVGL_RENDER_FB_FETCH;
  }
#endif
// image load/store is faster than FB fetch on desktop (at least Intel GPUs)
  if(!(gl->flags & NVGL_NO_IMG_ATOMIC)) {
#ifdef GL_ARB_shader_image_load_store  // desktop GL only
    if(NVG_HAS_EXT(ARB_shader_image_load_store))
      gl->renderMethod = NVGL_RENDER_IMG_ATOMIC;
#elif defined(GL_ES_VERSION_3_1)  //defined(GL_OES_shader_image_atomic) || defined(GL_ES_VERSION_3_2)
    // basically every GLES3.1 impl seems to include GL_OES_shader_image_atomic
    // I think we may want to prefer FB fetch over image load/store on mobile, but some Qualcomm GPUs (Adreno)
    //  apparently don't implement it correctly, so give priority to image load/store for now
    gl->renderMethod = NVGL_RENDER_IMG_ATOMIC;
#else
#define NANOVG_GL_NO_IMG_ATOMIC 1  // disable code if no possibility of support on current platform
#endif
  }

  if(gl->renderMethod == NVGL_RENDER_FB_FETCH) {
    shaderOpts = "#define USE_FRAMEBUFFER_FETCH\n";
    NVG_LOG("nvg2: using GL_EXT_shader_framebuffer_fetch\n");
  } else if(gl->renderMethod == NVGL_RENDER_IMG_ATOMIC) {
    shaderOpts = "#define USE_IMAGE_LOADSTORE\n";
    NVG_LOG("nvg2: using GL_ARB_shader_image_load_store/GL_OES_shader_image_atomic\n");
  } else {
    NVG_LOG("nvg2: no extensions in use\n");
  }

  {
    const char* sdfDef = (gl->flags & NVG_SDF_TEXT) ? "#define USE_SDF_TEXT 1\n" : "";
    const char* vertsrc[] = { shaderHeader, shaderOpts, sdfDef, fillVertShader };
    const char* fragsrc[] = { shaderHeader, shaderOpts, sdfDef, fillFragShader };

    if(glnvg__createProgram(&gl->shader, vertsrc, 4, fragsrc, 4) == 0)
      return 0;
  }

  glnvg__checkError(gl, "uniform locations");
  glnvg__getUniforms(&gl->shader);

  // Create dynamic vertex array
#if defined NANOVG_GL3
  glGenVertexArrays(1, &gl->vertArr);
#endif
  glGenBuffers(1, &gl->vertBuf);

#if NANOVG_GL_USE_UNIFORMBUFFER
  // Create UBOs
  glUniformBlockBinding(gl->shader.prog, gl->shader.loc[GLNVG_LOC_FRAG], GLNVG_FRAG_BINDING);
  glGenBuffers(1, &gl->fragBuf);
  glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align);
#endif
  gl->fragSize = sizeof(GLNVGfragUniforms) + align - sizeof(GLNVGfragUniforms) % align;

  // alloc framebuffer for winding accum; will be inited in renderViewport when viewport size is known
  glGenFramebuffers(1, &gl->fbWinding);
  glnvg__checkError(gl, "create done");
  glFinish();
  return 1;
}

static int glnvg__renderCreateTexture(void* uptr, int type, int w, int h, int imageFlags, const void* data)
{
  GLNVGcontext* gl = (GLNVGcontext*)uptr;
  GLNVGtexture* tex = glnvg__allocTexture(gl);
  GLint magfilt, mipfilt, minfilt, wrapx, wrapy;
  if (tex == NULL) return 0;

#ifdef NANOVG_GLES2
  // Check for non-power of 2.
  if (glnvg__nearestPow2(w) != (unsigned int)w || glnvg__nearestPow2(h) != (unsigned int)h) {
    // No repeat
    if ((imageFlags & NVG_IMAGE_REPEATX) != 0 || (imageFlags & NVG_IMAGE_REPEATY) != 0) {
      NVG_LOG("Repeat X/Y is not supported for non power-of-two textures (%d x %d)\n", w, h);
      imageFlags &= ~(NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY);
    }
    // No mips.
    if (imageFlags & NVG_IMAGE_GENERATE_MIPMAPS) {
      NVG_LOG("Mip-maps is not support for non power-of-two textures (%d x %d)\n", w, h);
      imageFlags &= ~NVG_IMAGE_GENERATE_MIPMAPS;
    }
  }
#endif

  glGenTextures(1, &tex->tex);
  tex->width = w;
  tex->height = h;
  tex->type = type;
  tex->flags = imageFlags;
  glnvg__bindTexture(gl, tex->tex);

  glPixelStorei(GL_UNPACK_ALIGNMENT,1);
#ifndef NANOVG_GLES2
  glPixelStorei(GL_UNPACK_ROW_LENGTH, tex->width);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
#endif

#if defined (NANOVG_GL2)
  // GL 1.4 and later has support for generating mipmaps using a tex parameter.
  if (imageFlags & NVG_IMAGE_GENERATE_MIPMAPS) {
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
  }
#endif

  if (type == NVG_TEXTURE_RGBA) {
    GLint internalfmt = imageFlags & NVG_IMAGE_SRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    glTexImage2D(GL_TEXTURE_2D, 0, internalfmt, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
  } else if (type == NVG_TEXTURE_FLOAT) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, data);
  } else {
#if defined(NANOVG_GLES2) || defined (NANOVG_GL2)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, data);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, data);
#endif
  }

  magfilt = imageFlags & NVG_IMAGE_NEAREST ? GL_NEAREST : GL_LINEAR;
  mipfilt = imageFlags & NVG_IMAGE_NEAREST ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
  minfilt = imageFlags & NVG_IMAGE_GENERATE_MIPMAPS ? mipfilt : magfilt;
  wrapx = imageFlags & NVG_IMAGE_REPEATX ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  wrapy = imageFlags & NVG_IMAGE_REPEATY ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minfilt);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magfilt);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapx);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapy);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
#ifndef NANOVG_GLES2
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
#endif

  // The new way to build mipmaps on GLES and GL3
#if !defined(NANOVG_GL2)
  if (imageFlags & NVG_IMAGE_GENERATE_MIPMAPS) {
    glGenerateMipmap(GL_TEXTURE_2D);
  }
#endif
  glnvg__checkError(gl, "create tex");
  glnvg__bindTexture(gl, 0);
  return tex->id;
}

static int glnvg__renderDeleteTexture(void* uptr, int image)
{
  GLNVGcontext* gl = (GLNVGcontext*)uptr;
  return glnvg__deleteTexture(gl, image);
}

static int glnvg__renderUpdateTexture(void* uptr, int image, int x, int y, int w, int h, const void* data)
{
  GLNVGcontext* gl = (GLNVGcontext*)uptr;
  GLNVGtexture* tex = glnvg__findTexture(gl, image);

  if (tex == NULL) return 0;
  glnvg__bindTexture(gl, tex->tex);

  glPixelStorei(GL_UNPACK_ALIGNMENT,1);

#ifndef NANOVG_GLES2
  glPixelStorei(GL_UNPACK_ROW_LENGTH, tex->width);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, x);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, y);
#else
  // No support for all of skip, need to update a whole row at a time.
  if (tex->type == NVG_TEXTURE_RGBA)
    data += y*tex->width*4;
  else
    data += y*tex->width;
  x = 0;
  w = tex->width;
#endif

  if (tex->type == NVG_TEXTURE_RGBA)
    glTexSubImage2D(GL_TEXTURE_2D, 0, x,y, w,h, GL_RGBA, GL_UNSIGNED_BYTE, data);
  else if (tex->type == NVG_TEXTURE_FLOAT)
    glTexSubImage2D(GL_TEXTURE_2D, 0, x,y, w,h, GL_RED, GL_FLOAT, data);
  else
#if defined(NANOVG_GLES2) || defined(NANOVG_GL2)
    glTexSubImage2D(GL_TEXTURE_2D, 0, x,y, w,h, GL_LUMINANCE, GL_UNSIGNED_BYTE, data);
#else
    glTexSubImage2D(GL_TEXTURE_2D, 0, x,y, w,h, GL_RED, GL_UNSIGNED_BYTE, data);
#endif

  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
#ifndef NANOVG_GLES2
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
#endif
  glnvg__bindTexture(gl, 0);
  return 1;
}

static int glnvg__renderGetTextureSize(void* uptr, int image, int* w, int* h)
{
  GLNVGcontext* gl = (GLNVGcontext*)uptr;
  GLNVGtexture* tex = glnvg__findTexture(gl, image);
  if (tex == NULL) return 0;
  *w = tex->width;
  *h = tex->height;
  return 1;
}

static GLNVGfragUniforms* nvg__fragUniformPtr(GLNVGcontext* gl, int i)
{
  return (GLNVGfragUniforms*)&gl->uniforms[i];
}

static void glnvg__setUniforms(GLNVGcontext* gl, int uniformOffset, int image)
{
#if NANOVG_GL_USE_UNIFORMBUFFER
  glBindBufferRange(GL_UNIFORM_BUFFER, GLNVG_FRAG_BINDING, gl->fragBuf, uniformOffset, sizeof(GLNVGfragUniforms));
#else
  GLNVGfragUniforms* frag = nvg__fragUniformPtr(gl, uniformOffset);
  glUniform4fv(gl->shader.loc[GLNVG_LOC_FRAG], NANOVG_GL_UNIFORMARRAY_SIZE, &(frag->uniformArray[0][0]));
#endif

  if (image != 0) {
    GLNVGtexture* tex = glnvg__findTexture(gl, image);
    glnvg__bindTexture(gl, tex != NULL ? tex->tex : 0);
    glnvg__checkError(gl, "tex paint tex");
  } else {
    glnvg__bindTexture(gl, 0);
  }
}

static void glnvg__renderCancel(void* uptr)
{
  int i;
  GLNVGcontext* gl = (GLNVGcontext*)uptr;
  gl->nverts = 0;
  gl->npaths = 0;
  gl->ncalls = 0;
  gl->nuniforms = 0;

  // clear temporary textures (e.g., for which user didn't save handle)
  for (i = 0; i < gl->ntextures; i++) {
    if (gl->textures[i].flags & NVG_IMAGE_DISCARD) {
      if (gl->textures[i].tex != 0 && (gl->textures[i].flags & NVG_IMAGE_NODELETE) == 0)
        glDeleteTextures(1, &gl->textures[i].tex);
      memset(&gl->textures[i], 0, sizeof(gl->textures[i]));
    }
  }
}

static void glnvg__renderViewport(void* uptr, float width, float height, float devicePixelRatio)
{
  GLNVGcontext* gl = (GLNVGcontext*)uptr;

  gl->devicePixelRatio = devicePixelRatio;

  // if we want to only recreate textures if viewport becomes larger (but not smaller), we could pass
  //  winding texture size as separate uniform (not needed for EXT_shader_framebuffer_fetch)
  if (gl->view[0] != width || gl->view[1] != height) {
    if(gl->texWinding > 0)
      glDeleteTextures(1, &gl->texWinding);
    if(gl->texColor > 0)
      glDeleteTextures(1, &gl->texColor);
    gl->texWinding = 0;
    gl->texColor = 0;
  }
  gl->view[0] = width;
  gl->view[1] = height;

  glnvg__renderCancel(uptr);  // reset
}

// Apparently, rasterization for pixels exactly on edges can differ between a FBO and default framebuffer (in
//  the y direction, likely due to OpenGL origin at lower left vs. the usual upper left), so using the same
//  quad for clear pass and fill pass can result in artifacts w/ overlapping paths when quad y0 has
//  fract(y0) = 0.5 due to some pixels not being cleared.
// A safe, proper solution would be to use a separate, slightly larger (i.e. +/-0.51 px) quad for clear pass
// ... but for now, we'll just require that user draw into FBO
// Also: could we use glBlendFunci, glColorMaski for FB swap?
static void glnvg__fill(GLNVGcontext* gl, GLNVGcall* call)
{
  GLNVGpath* paths = &gl->paths[call->pathOffset];
  int i, npaths = call->pathCount;
  glnvg__setUniforms(gl, call->uniformOffset, call->image); // NOTE: this binds/unbinds texture!

  if (gl->renderMethod == NVGL_RENDER_FB_SWAP) {
    glBindFramebuffer(GL_FRAMEBUFFER, gl->fbWinding);
    // clear needed region of winding texture
    glBlendFunc(GL_ZERO, GL_ZERO);
    glUniform1i(gl->shader.loc[GLNVG_LOC_TYPE], NSVG_SHADER_WINDINGCLEAR);
    glDrawArrays(GL_TRIANGLE_STRIP, call->triangleOffset, call->triangleCount);
    glnvg__checkError(gl, "fill: clear winding");
  }

  // accumulate winding
  if (gl->renderMethod == NVGL_RENDER_FB_SWAP)
    glBlendFunc(GL_ONE, GL_ONE);
  glUniform1i(gl->shader.loc[GLNVG_LOC_TYPE], NSVG_SHADER_WINDINGCALC);
  for (i = 0; i < npaths; i++) {
    glUniform1f(gl->shader.loc[GLNVG_LOC_WINDINGYMIN], paths[i].yMin < 0 ? 0 : paths[i].yMin);
    glDrawArrays(GL_TRIANGLES, paths[i].fillOffset, paths[i].fillCount);
  }
  glnvg__checkError(gl, "fill: accumulate winding");

  // read from winding texture to fill
  glUniform1i(gl->shader.loc[GLNVG_LOC_TYPE], call->fragType);
  // restore blend func
  if (gl->renderMethod != NVGL_RENDER_FB_FETCH)
    glnvg__blendFuncSeparate(gl, &call->blendFunc);

  if (gl->renderMethod == NVGL_RENDER_FB_SWAP) {
    glBindFramebuffer(GL_FRAMEBUFFER, gl->fbUser);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, gl->texWinding);
  }

  glDrawArrays(GL_TRIANGLE_STRIP, call->triangleOffset, call->triangleCount);
  glnvg__checkError(gl, "fill: fill");

  if (gl->renderMethod == NVGL_RENDER_FB_SWAP) {
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
  }
}

static void glnvg__convexFill(GLNVGcontext* gl, GLNVGcall* call)
{
  glnvg__blendFuncSeparate(gl, &call->blendFunc);
  glUniform1i(gl->shader.loc[GLNVG_LOC_TYPE], call->fragType);
  glnvg__setUniforms(gl, call->uniformOffset, call->image);
  //for (i = 0; i < npaths; i++)
  glDrawArrays(GL_TRIANGLE_FAN, call->triangleOffset, call->triangleCount);
  glnvg__checkError(gl, "convex fill");
}

static void glnvg__triangles(GLNVGcontext* gl, GLNVGcall* call)
{
  glnvg__blendFuncSeparate(gl, &call->blendFunc);
  glUniform1i(gl->shader.loc[GLNVG_LOC_TYPE], call->fragType);
  glnvg__setUniforms(gl, call->uniformOffset, call->image);
  glDrawArrays(GL_TRIANGLES, call->triangleOffset, call->triangleCount);
  glnvg__checkError(gl, "triangles fill");
}

static GLenum glnvg_convertBlendFuncFactor(int factor)
{
  switch(factor) {
    case NVG_ZERO:                return GL_ZERO;
    case NVG_ONE:                 return GL_ONE;
    case NVG_SRC_COLOR:           return GL_SRC_COLOR;
    case NVG_ONE_MINUS_SRC_COLOR: return GL_ONE_MINUS_SRC_COLOR;
    case NVG_DST_COLOR:           return GL_DST_COLOR;
    case NVG_ONE_MINUS_DST_COLOR: return GL_ONE_MINUS_DST_COLOR;
    case NVG_SRC_ALPHA:           return GL_SRC_ALPHA;
    case NVG_ONE_MINUS_SRC_ALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    case NVG_DST_ALPHA:           return GL_DST_ALPHA;
    case NVG_ONE_MINUS_DST_ALPHA: return GL_ONE_MINUS_DST_ALPHA;
    case NVG_SRC_ALPHA_SATURATE:  return GL_SRC_ALPHA_SATURATE;
    default:                      return GL_INVALID_ENUM;
  }
}

static GLNVGblend glnvg__blendCompositeOperation(NVGcompositeOperationState op)
{
  GLNVGblend blend;
  blend.srcRGB = glnvg_convertBlendFuncFactor(op.srcRGB);
  blend.dstRGB = glnvg_convertBlendFuncFactor(op.dstRGB);
  blend.srcAlpha = glnvg_convertBlendFuncFactor(op.srcAlpha);
  blend.dstAlpha = glnvg_convertBlendFuncFactor(op.dstAlpha);
  if (blend.srcRGB == GL_INVALID_ENUM || blend.dstRGB == GL_INVALID_ENUM || blend.srcAlpha == GL_INVALID_ENUM || blend.dstAlpha == GL_INVALID_ENUM)
  {
    blend.srcRGB = GL_ONE;
    blend.dstRGB = GL_ONE_MINUS_SRC_ALPHA;
    blend.srcAlpha = GL_ONE;
    blend.dstAlpha = GL_ONE_MINUS_SRC_ALPHA;
  }
  return blend;
}

#ifndef NANOVG_GL_NO_IMG_ATOMIC
#ifdef GL_ES_VERSION_3_1
#define glMemoryBarrier glMemoryBarrierByRegion
#endif

static void glnvg__imgAtomicFlush(GLNVGcontext* gl)
{
  int fillidx = 0, accumidx = 0;
  // newer Intel Windows graphics drivers (v26 but not v21) freeze if uniforms are not initialized, w/
  //  "Display driver igfx stopped responding and has successfully recovered." in Windows Event Viewer
  glnvg__setUniforms(gl, 0, 0);
  while(fillidx < gl->ncalls) {
    for (; accumidx < gl->ncalls; ++accumidx) {
      GLNVGcall* call = &gl->calls[accumidx];
      if(call->type == GLNVG_FILL) {
        GLNVGpath* paths = &gl->paths[call->pathOffset];
        int i, npaths = call->pathCount;
        int disableAA = (int)nvg__fragUniformPtr(gl, call->uniformOffset)->fillMode & NVG_PATH_NO_AA ? 1 : 0;
        // setup params for converting gl_FragCoord.xy to image index
        int w = call->bounds[2] - call->bounds[0] + 1;  // +1 because bounds are inclusive
        int minidx = call->bounds[0] + (gl->view[1] - call->bounds[3] - 1)*w;
        int maxidx = call->bounds[2] + (gl->view[1] - call->bounds[1] - 1)*w;
        int nextOffset = gl->imgWindingOffset + (maxidx - minidx) + 1;
        // if not enough room in imgWinding for this call, run fill passes and restart
        if(nextOffset > gl->imgWindingSize)
          break;
        call->imgOffset = gl->imgWindingOffset - minidx;
        gl->imgWindingOffset = nextOffset;
        // offset, quad stride, imgWinding stride, disable AA flag
        glUniform4i(gl->shader.loc[GLNVG_LOC_IMGPARAMS], call->imgOffset, w, gl->view[0], disableAA);

        // accumulate winding
        glUniform1i(gl->shader.loc[GLNVG_LOC_TYPE], NSVG_SHADER_WINDINGCALC);
        for (i = 0; i < npaths; i++) {
          glUniform1f(gl->shader.loc[GLNVG_LOC_WINDINGYMIN], paths[i].yMin < 0 ? 0 : paths[i].yMin);
          glDrawArrays(GL_TRIANGLES, paths[i].fillOffset, paths[i].fillCount);
        }
        glnvg__checkError(gl, "fill: accumulate winding");
      }
    }
    // GL_FRAMEBUFFER_BARRIER_BIT needed for Intel Xe graphics for some reason
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
    gl->imgWindingOffset = 0;
    for (; fillidx < accumidx; ++fillidx) {
      GLNVGcall* call = &gl->calls[fillidx];
      if (call->type == GLNVG_FILL) {
        int w = call->bounds[2] - call->bounds[0] + 1;  // +1 because bounds are inclusive
        glnvg__setUniforms(gl, call->uniformOffset, call->image);  // NOTE: this binds/unbinds texture!
        glUniform4i(gl->shader.loc[GLNVG_LOC_IMGPARAMS], call->imgOffset, w, gl->view[0], 0);
        // read from winding texture to fill
        glUniform1i(gl->shader.loc[GLNVG_LOC_TYPE], call->fragType);
        glnvg__blendFuncSeparate(gl, &call->blendFunc);
        glDrawArrays(GL_TRIANGLE_STRIP, call->triangleOffset, call->triangleCount);
        glnvg__checkError(gl, "fill: fill");
      } else if (call->type == GLNVG_TRIANGLES) {
        glnvg__triangles(gl, call);
      } else if (call->type == GLNVG_CONVEXFILL) {
        glnvg__convexFill(gl, call);
      }
    }
    // have to wait for imgWinding to be cleared before we can start writing again
    if (fillidx < gl->ncalls)
      glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
  }
}
#endif

static void glnvg__renderFlush(void* uptr)
{
  static const GLenum drawBuffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
  static const float clearColor[] = {0.0f, 0.0f, 0.0f, 0.0f};
  GLNVGcontext* gl = (GLNVGcontext*)uptr;
  int i;

  if (gl->ncalls <= 0) {
    glnvg__renderCancel(uptr);
    return;
  }

  // save id of user's destination framebuffer (we assume same FB for draw and read)
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, (GLint*)&gl->fbUser);
  // (re)create winding accum texture
  if (gl->texWinding == 0) {
    glGenTextures(1, &gl->texWinding);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, gl->texWinding);
    // w/ gl_LastFragData or glTextureBarrier, we could use a single RGBA16F texture for color and winding
    //glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, NULL);
    if (gl->renderMethod == NVGL_RENDER_IMG_ATOMIC)
#ifdef GL_ES_VERSION_3_0
      glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32I, gl->view[0], gl->view[1]);
#else
      glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, gl->view[0], gl->view[1], 0, GL_RED_INTEGER, GL_INT, NULL);
#endif
    else if(gl->renderMethod == NVGL_RENDER_FB_FETCH)
      glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, gl->view[0], gl->view[1], 0, GL_RED_INTEGER, GL_INT, NULL);
    else // FB swap -- must use float since blending doesn't work with ints
      glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, gl->view[0], gl->view[1], 0, GL_RED, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, gl->fbWinding);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl->texWinding, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      NVG_LOG("Error creating framebuffer for winding accumulation\n");
    glClearBufferfv(GL_COLOR, 0, clearColor);
    // for FB fetch, unbind from attachment 0
    if (gl->renderMethod != NVGL_RENDER_FB_SWAP)
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, gl->fbUser);
    glnvg__checkError(gl, "winding accum FB setup");
  }

  if (gl->renderMethod == NVGL_RENDER_FB_FETCH) {
    // When using framebuffer fetch, user is responsible for creating and binding framebuffer before calling
    //  nvgEndFrame(), e.g., using nvgluCreateFramebuffer() and nvgluBindFramebuffer()
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gl->texWinding, 0);
    glDrawBuffers(2, drawBuffers);
#ifndef NANOVG_GL_NO_IMG_ATOMIC
  } else if (gl->renderMethod == NVGL_RENDER_IMG_ATOMIC) {
    glBindImageTexture(0, gl->texWinding, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32I);
    gl->imgWindingOffset = 0;
    gl->imgWindingSize = gl->view[0] * gl->view[1];
#endif
  }
  glnvg__checkError(gl, "windingTex setup");

  glUseProgram(gl->shader.prog);
  glDisable(GL_CULL_FACE);
  // GL_BLEND works with FB fetch (since we changed from float to int accum), but a tiny bit slower (and
  //  not well-tested on other devices), so we'll leave disabled for now
  if (gl->renderMethod == NVGL_RENDER_FB_FETCH)
    glDisable(GL_BLEND);
  else
    glEnable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  //glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  #if NANOVG_GL_USE_STATE_FILTER
  gl->boundTexture = 0;
  gl->blendFunc.srcRGB = GL_INVALID_ENUM;
  gl->blendFunc.srcAlpha = GL_INVALID_ENUM;
  gl->blendFunc.dstRGB = GL_INVALID_ENUM;
  gl->blendFunc.dstAlpha = GL_INVALID_ENUM;
  #endif

#if NANOVG_GL_USE_UNIFORMBUFFER
  // Upload ubo for frag shaders
  glBindBuffer(GL_UNIFORM_BUFFER, gl->fragBuf);
  glBufferData(GL_UNIFORM_BUFFER, gl->nuniforms * gl->fragSize, gl->uniforms, GL_STREAM_DRAW);
#endif

  // Upload vertex data
#if defined NANOVG_GL3
  glBindVertexArray(gl->vertArr);
#endif
  glBindBuffer(GL_ARRAY_BUFFER, gl->vertBuf);
  glBufferData(GL_ARRAY_BUFFER, gl->nverts * sizeof(NVGvertex), gl->verts, GL_STREAM_DRAW);
  // 0 = va_in, 1 = vb_in, 2 = normal_in as set by glBindAttribLocation in glnvg__createShader
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  //glEnableVertexAttribArray(2);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(NVGvertex), (const GLvoid*)(size_t)0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(NVGvertex), (const GLvoid*)(0 + 2*sizeof(float)));  //offsetof(NVGVertex, x1)
  //glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(NVGvertex), (const GLvoid*)(0 + 4*sizeof(float)));  //offsetof(NVGVertex, u);

  // Set view and texture locations just once per frame.
  glUniform2fv(gl->shader.loc[GLNVG_LOC_VIEWSIZE], 1, gl->view);
  glUniform1i(gl->shader.loc[GLNVG_LOC_TEX], 0);
  if (gl->renderMethod == NVGL_RENDER_FB_SWAP)
    glUniform1i(gl->shader.loc[GLNVG_LOC_WINDINGTEX], 1);
#ifndef NANOVG_GLES3
  // in GLES, image unit location can't be set w/ glUniform, only queried - so we use binding = 0 in shader
  else if(gl->renderMethod == NVGL_RENDER_IMG_ATOMIC)
    glUniform1i(gl->shader.loc[GLNVG_LOC_WINDINGIMG], 0);
#endif

#if NANOVG_GL_USE_UNIFORMBUFFER
  glBindBuffer(GL_UNIFORM_BUFFER, gl->fragBuf);
#endif
  glnvg__checkError(gl, "renderFlush setup");

#ifndef NANOVG_GL_NO_IMG_ATOMIC
  if(gl->renderMethod == NVGL_RENDER_IMG_ATOMIC)
    glnvg__imgAtomicFlush(gl);
  else
#endif
  {
    for (i = 0; i < gl->ncalls; i++) {
      GLNVGcall* call = &gl->calls[i];
      if (call->type == GLNVG_FILL)
        glnvg__fill(gl, call);
      else if (call->type == GLNVG_CONVEXFILL)
        glnvg__convexFill(gl, call);
      else if (call->type == GLNVG_TRIANGLES)
        glnvg__triangles(gl, call);
    }
  }

  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  //glDisableVertexAttribArray(2);
#if defined NANOVG_GL3
  glBindVertexArray(0);
#endif
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glUseProgram(0);
  glnvg__bindTexture(gl, 0);

  // unbind our winding texture
  if (gl->renderMethod == NVGL_RENDER_FB_FETCH) {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 0, 0);
    glDrawBuffers(1, drawBuffers);
#ifndef NANOVG_GL_NO_IMG_ATOMIC
  } else if (gl->renderMethod == NVGL_RENDER_IMG_ATOMIC) {
    glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32I);
#endif
  }
  glnvg__checkError(gl, "windingTex unbind");

  //glnvg__renderCancel(uptr);  -- we now reset at start of frame to allow frame to be drawn more than once
}

static GLNVGcall* glnvg__allocCall(GLNVGcontext* gl)
{
  GLNVGcall* ret = NULL;
  if (gl->ncalls+1 > gl->ccalls) {
    GLNVGcall* calls;
    int ccalls = glnvg__maxi(gl->ncalls+1, 128) + gl->ccalls/2; // 1.5x Overallocate
    calls = (GLNVGcall*)realloc(gl->calls, sizeof(GLNVGcall) * ccalls);
    if (calls == NULL) return NULL;
    gl->calls = calls;
    gl->ccalls = ccalls;
  }
  ret = &gl->calls[gl->ncalls++];
  memset(ret, 0, sizeof(GLNVGcall));
  return ret;
}

static int glnvg__allocPaths(GLNVGcontext* gl, int n)
{
  int ret = 0;
  if (gl->npaths+n > gl->cpaths) {
    GLNVGpath* paths;
    int cpaths = glnvg__maxi(gl->npaths + n, 128) + gl->cpaths/2; // 1.5x Overallocate
    paths = (GLNVGpath*)realloc(gl->paths, sizeof(GLNVGpath) * cpaths);
    if (paths == NULL) return -1;
    gl->paths = paths;
    gl->cpaths = cpaths;
  }
  ret = gl->npaths;
  gl->npaths += n;
  return ret;
}

static int glnvg__allocVerts(GLNVGcontext* gl, int n)
{
  int ret = 0;
  // round n to multiple of 6 so that edge vertices are always aligned for gl_VertexID
  n = 6*((n+5)/6);
  if (gl->nverts+n > gl->cverts) {
    NVGvertex* verts;
    int cverts = glnvg__maxi(gl->nverts + n, 4096) + gl->cverts/2; // 1.5x Overallocate
    verts = (NVGvertex*)realloc(gl->verts, sizeof(NVGvertex) * cverts);
    if (verts == NULL) return -1;
    gl->verts = verts;
    gl->cverts = cverts;
  }
  ret = gl->nverts;
  gl->nverts += n;
  return ret;
}

static int glnvg__allocFragUniforms(GLNVGcontext* gl, int n)
{
  int ret = 0, structSize = gl->fragSize;
  if (gl->nuniforms+n > gl->cuniforms) {
    unsigned char* uniforms;
    int cuniforms = glnvg__maxi(gl->nuniforms+n, 128) + gl->cuniforms/2; // 1.5x Overallocate
    uniforms = (unsigned char*)realloc(gl->uniforms, structSize * cuniforms);
    if (uniforms == NULL) return -1;
    gl->uniforms = uniforms;
    gl->cuniforms = cuniforms;
  }
  ret = gl->nuniforms * structSize;
  gl->nuniforms += n;
  return ret;
}

static void glnvg__vset2(NVGvertex* vtx, float x0, float y0, float x1, float y1)  //, float u, float v)
{
  vtx->x0 = x0;
  vtx->y0 = y0;
  vtx->x1 = x1;
  vtx->y1 = y1;
  //vtx->u = u;
  //vtx->v = v;
}

static void glnvg__xformToMat3x4(float* m3, float* t)
{
  m3[0] = t[0]; m3[1] = t[1]; m3[2] = 0.0f; m3[3] = 0.0f;
  m3[4] = t[2]; m3[5] = t[3]; m3[6] = 0.0f; m3[7] = 0.0f;
  m3[8] = t[4]; m3[9] = t[5]; m3[10] = 1.0f; m3[11] = 0.0f;
}

static NVGLcolor glnvg__convertColor(GLNVGcontext* gl, NVGcolor c)
{
  NVGLcolor cout;
  cout.a = c.a/255.0f;
  cout.r = cout.a*(gl->flags & NVG_SRGB ? nvgSRGBtoLinear(c.r) : c.r/255.0f);
  cout.g = cout.a*(gl->flags & NVG_SRGB ? nvgSRGBtoLinear(c.g) : c.g/255.0f);
  cout.b = cout.a*(gl->flags & NVG_SRGB ? nvgSRGBtoLinear(c.b) : c.b/255.0f);
  return cout;
}

static int glnvg__convertPaint(GLNVGcontext* gl, GLNVGcall* call, NVGpaint* paint, NVGscissor* scissor, int flags)
{
  GLNVGtexture* tex = NULL;
  float invxform[6];

  GLNVGfragUniforms* frag = nvg__fragUniformPtr(gl, call->uniformOffset);
  memset(frag, 0, sizeof(*frag));

  frag->fillMode = flags;  // NVGpathFlags
  frag->innerCol = glnvg__convertColor(gl, paint->innerColor);
  frag->outerCol = glnvg__convertColor(gl, paint->outerColor);

  if (scissor->extent[0] < -0.5f || scissor->extent[1] < -0.5f) {
    memset(frag->scissorMat, 0, sizeof(frag->scissorMat));
    frag->scissorExt[0] = 1.0f;
    frag->scissorExt[1] = 1.0f;
    frag->scissorScale[0] = 1.0f;
    frag->scissorScale[1] = 1.0f;
  } else {
    nvgTransformInverse(invxform, scissor->xform);
    glnvg__xformToMat3x4(frag->scissorMat, invxform);
    frag->scissorExt[0] = scissor->extent[0];
    frag->scissorExt[1] = scissor->extent[1];
    frag->scissorScale[0] = sqrtf(scissor->xform[0]*scissor->xform[0] + scissor->xform[2]*scissor->xform[2])*gl->devicePixelRatio;
    frag->scissorScale[1] = sqrtf(scissor->xform[1]*scissor->xform[1] + scissor->xform[3]*scissor->xform[3])*gl->devicePixelRatio;
  }

  memcpy(frag->extent, paint->extent, sizeof(frag->extent));

  // texture used for images and complex gradients
  if(paint->image != 0) {
    call->image = paint->image;
    tex = glnvg__findTexture(gl, paint->image);
    if (tex == NULL) return 0;
#if NANOVG_GL_USE_UNIFORMBUFFER
    if (tex->type == NVG_TEXTURE_RGBA)
      frag->texType = (tex->flags & NVG_IMAGE_PREMULTIPLIED) ? 3 : 1;
    else
      frag->texType = 2;
    #else
    if (tex->type == NVG_TEXTURE_RGBA)
      frag->texType = (tex->flags & NVG_IMAGE_PREMULTIPLIED) ? 3.0f : 1.0f;
    else
      frag->texType = 2.0f;
#endif
  }

  if (paint->innerColor.c != paint->outerColor.c) {
    call->fragType = NSVG_SHADER_FILLGRAD;
    frag->radius = paint->radius;
    frag->feather = paint->feather;
    nvgTransformInverse(invxform, paint->xform);
    glnvg__xformToMat3x4(frag->paintMat, invxform);
  } else if (paint->image != 0) {
    call->fragType = NSVG_SHADER_FILLIMG;
    frag->radius = paint->radius + 0.5f;  // radius used for SDF text weight
    if ((tex->flags & NVG_IMAGE_FLIPY) != 0) {
      float m1[6], m2[6];
      nvgTransformTranslate(m1, 0.0f, frag->extent[1] * 0.5f);
      nvgTransformMultiply(m1, paint->xform);
      nvgTransformScale(m2, 1.0f, -1.0f);
      nvgTransformMultiply(m2, m1);
      nvgTransformTranslate(m1, 0.0f, -frag->extent[1] * 0.5f);
      nvgTransformMultiply(m1, m2);
      nvgTransformInverse(invxform, m1);
    } else {
      nvgTransformInverse(invxform, paint->xform);
    }
    glnvg__xformToMat3x4(frag->paintMat, invxform);
  } else {
    call->fragType = NSVG_SHADER_FILLSOLID;
  }
  return 1;
}

static void glnvg__renderFill(void* uptr, NVGpaint* paint, NVGcompositeOperationState compositeOperation,
    NVGscissor* scissor, int flags, const float* bounds, const NVGpath* paths, int npaths)
{
  GLNVGcontext* gl = (GLNVGcontext*)uptr;
  NVGvertex* vtx;
  int i, j, offset, maxverts = 0;
  GLNVGcall* call = NULL;
  for (i = 0; i < npaths; ++i)
    maxverts += paths[i].nfill;
  if (maxverts == 0) return;
  call = glnvg__allocCall(gl);
  if (call == NULL) return;

  // convex actually means fannable and non-antialiased
  if (npaths == 1 && paths[0].convex && paths[0].nfill > 2) {
    const NVGpath* path = &paths[0];
    flags |= NVG_PATH_CONVEX;
    call->type = GLNVG_CONVEXFILL;
    call->triangleCount = path->nfill;
    offset = glnvg__allocVerts(gl, call->triangleCount);
    if (offset == -1) goto error;
    call->triangleOffset = offset;
    vtx = &gl->verts[offset];
    // vertices for triangle fan
    for(i = 0; i < path->nfill; ++i)
      glnvg__vset2(vtx++, path->fill[i].x0, path->fill[i].y0, 0, 0);
  } else {
    NVGvertex* quad;
    call->type = GLNVG_FILL;
    call->triangleCount = 4;
    call->pathOffset = glnvg__allocPaths(gl, npaths);
    if (call->pathOffset == -1) goto error;
    call->pathCount = npaths;
    offset = glnvg__allocVerts(gl, 6*maxverts + call->triangleCount);
    if (offset == -1) goto error;

    for (i = 0; i < npaths; i++) {
      GLNVGpath* copy = &gl->paths[call->pathOffset + i];
      const NVGpath* path = &paths[i];
      memset(copy, 0, sizeof(GLNVGpath));
      if (path->nfill > 0) {
        vtx = &gl->verts[offset];
        copy->fillOffset = offset;
        copy->fillCount = 6*path->nfill;
        for(j = 0; j < path->nfill; ++j) {
          float x0 = path->fill[j].x0, y0 = path->fill[j].y0, x1 = path->fill[j].x1, y1 = path->fill[j].y1;
          // two triangles for quad to be filled with winding number from this segment
          glnvg__vset2(vtx++, x0, y0, x1, y1);  //, -1,  1);
          glnvg__vset2(vtx++, x0, y0, x1, y1);  //,  1,  1);
          glnvg__vset2(vtx++, x0, y0, x1, y1);  //,  1, -1);
          glnvg__vset2(vtx++, x0, y0, x1, y1);  //, -1,  1);
          glnvg__vset2(vtx++, x0, y0, x1, y1);  //,  1, -1);
          glnvg__vset2(vtx++, x0, y0, x1, y1);  //, -1, -1);
        }
        //memcpy(&gl->verts[offset], path->fill, sizeof(NVGvertex) * path->nfill);
        offset += copy->fillCount;
      }
      copy->yMin = path->bounds[1];
    }

    // bounds - note these are *inclusive*
    call->bounds[0] = glnvg__clampi((int)(bounds[0] - 0.001f), 0, gl->view[0] - 1);
    call->bounds[1] = glnvg__clampi((int)(bounds[1] - 0.001f), 0, gl->view[1] - 1);
    call->bounds[2] = glnvg__clampi((int)(bounds[2] + 1.001f), 0, gl->view[0] - 1);
    call->bounds[3] = glnvg__clampi((int)(bounds[3] + 1.001f), 0, gl->view[1] - 1);

    // Quad
    call->triangleOffset = offset;
    quad = &gl->verts[call->triangleOffset];
    glnvg__vset2(&quad[0], bounds[2] + 0.5f, bounds[3] + 0.5f, 0, 0);  //, 0, 0);
    glnvg__vset2(&quad[1], bounds[2] + 0.5f, bounds[1] - 0.5f, 0, 0);  //, 0, 0);
    glnvg__vset2(&quad[2], bounds[0] - 0.5f, bounds[3] + 0.5f, 0, 0);  //, 0, 0);
    glnvg__vset2(&quad[3], bounds[0] - 0.5f, bounds[1] - 0.5f, 0, 0);  //, 0, 0);
  }

  call->blendFunc = glnvg__blendCompositeOperation(compositeOperation);
  call->uniformOffset = glnvg__allocFragUniforms(gl, 1);
  if (call->uniformOffset == -1) goto error;
  glnvg__convertPaint(gl, call, paint, scissor, flags);
  return;
error:
  // We get here if call alloc was ok, but something else is not.
  // Roll back the last call to prevent drawing it.
  if (gl->ncalls > 0) gl->ncalls--;
}

static void glnvg__renderTriangles(void* uptr, NVGpaint* paint, NVGcompositeOperationState compositeOperation,
    NVGscissor* scissor, const NVGvertex* verts, int nverts)
{
  GLNVGcontext* gl = (GLNVGcontext*)uptr;
  GLNVGcall* call = glnvg__allocCall(gl);
  if (call == NULL) return;

  call->type = GLNVG_TRIANGLES;
  call->blendFunc = glnvg__blendCompositeOperation(compositeOperation);
  // Allocate vertices for all the paths.
  call->triangleOffset = glnvg__allocVerts(gl, nverts);
  if (call->triangleOffset == -1) goto error;
  call->triangleCount = nverts;
  memcpy(&gl->verts[call->triangleOffset], verts, sizeof(NVGvertex) * nverts);

  // Fill shader
  call->uniformOffset = glnvg__allocFragUniforms(gl, 1);
  if (call->uniformOffset == -1) goto error;
  //frag = nvg__fragUniformPtr(gl, call->uniformOffset);
  glnvg__convertPaint(gl, call, paint, scissor, 0); //, 1.0f, 1.0f, -1.0f);
  call->fragType = NSVG_SHADER_TEXT;
  return;
error:
  if (gl->ncalls > 0) gl->ncalls--;  // skip call if allocation error
}

static void glnvg__renderDelete(void* uptr)
{
  GLNVGcontext* gl = (GLNVGcontext*)uptr;
  int i;
  if (gl == NULL) return;

  glnvg__deleteShader(&gl->shader);

#if NANOVG_GL3
#if NANOVG_GL_USE_UNIFORMBUFFER
  if (gl->fragBuf != 0)
    glDeleteBuffers(1, &gl->fragBuf);
#endif
  if (gl->vertArr != 0)
    glDeleteVertexArrays(1, &gl->vertArr);
#endif
  if (gl->vertBuf != 0)
    glDeleteBuffers(1, &gl->vertBuf);

  for (i = 0; i < gl->ntextures; i++) {
    if (gl->textures[i].tex != 0 && (gl->textures[i].flags & NVG_IMAGE_NODELETE) == 0)
      glDeleteTextures(1, &gl->textures[i].tex);
  }
  free(gl->textures);
  free(gl->paths);
  free(gl->verts);
  free(gl->uniforms);
  free(gl->calls);
  free(gl);
}

NVGcontext* nvglCreate(int flags)
{
  NVGparams params;
  NVGcontext* ctx = NULL;
  GLNVGcontext* gl = (GLNVGcontext*)malloc(sizeof(GLNVGcontext));
  if (gl == NULL) goto error;
  memset(gl, 0, sizeof(GLNVGcontext));
  gl->flags = flags | NVG_IS_GPU;

  memset(&params, 0, sizeof(params));
  params.renderCreate = glnvg__renderCreate;
  params.renderCreateTexture = glnvg__renderCreateTexture;
  params.renderDeleteTexture = glnvg__renderDeleteTexture;
  params.renderUpdateTexture = glnvg__renderUpdateTexture;
  params.renderGetTextureSize = glnvg__renderGetTextureSize;
  params.renderViewport = glnvg__renderViewport;
  params.renderCancel = glnvg__renderCancel;
  params.renderFlush = glnvg__renderFlush;
  params.renderFill = glnvg__renderFill;
  params.renderTriangles = glnvg__renderTriangles;
  params.renderDelete = glnvg__renderDelete;
  params.userPtr = gl;
  params.flags = gl->flags;

  ctx = nvgCreateInternal(&params);
  if (ctx == NULL) goto error;
  return ctx;
error:
  // 'gl' is freed by nvgDeleteInternal.
  if (ctx != NULL) nvgDeleteInternal(ctx);
  return NULL;
}

void nvglDelete(NVGcontext* ctx)
{
  nvgDeleteInternal(ctx);
}

int nvglCreateImageFromHandle(NVGcontext* ctx, GLuint textureId, int w, int h, int imageFlags)
{
  GLNVGcontext* gl = (GLNVGcontext*)nvgInternalParams(ctx)->userPtr;
  GLNVGtexture* tex = glnvg__allocTexture(gl);
  if (tex == NULL) return 0;

  tex->type = NVG_TEXTURE_RGBA;
  tex->tex = textureId;
  tex->flags = imageFlags;
  tex->width = w;
  tex->height = h;
  return tex->id;
}

GLuint nvglImageHandle(NVGcontext* ctx, int image)
{
  GLNVGcontext* gl = (GLNVGcontext*)nvgInternalParams(ctx)->userPtr;
  GLNVGtexture* tex = glnvg__findTexture(gl, image);
  return tex->tex;
}

#endif /* NANOVG_GL_IMPLEMENTATION */
