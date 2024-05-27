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
  NVGL_DELETE_NO_GL = 1<<1,  // don't call GL functions when deleting
  NVGL_DEBUG = 1<<2,  // This value is hardcoded in Write's config - don't change!
  NVGL_DEFER_INIT = 1<<3,  // don't make any GL calls until (beginning of) first frame
  NVGL_TILE_SIZE_MASK = 0x70  // 000 - default/auto
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
#  define NANOVG_GL_USE_UNIFORMBUFFER 0  //... slower, at least on desktop and iPhone 6S
#endif

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
  GLNVG_LOC_FRAG,
  GLNVG_LOC_EDGES,
  GLNVG_LOC_NEDGES,
  GLNVG_LOC_OFFSET,
  GLNVG_MAX_LOCS
};

enum GLNVGshaderType {
  GLNVG_FILL_SIMPLE = 1,
  GLNVG_FILL_SOLID = 2,
  GLNVG_FILL_GRAD = 3,
  GLNVG_FILL_IMG = 4,
  GLNVG_FILL_TEXT = 5
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
  int fillOffset;
  int fillCount;
  int triangleOffset;
  int triangleCount;
  int uniformOffset;
  GLNVGblend blendFunc;
};
typedef struct GLNVGcall GLNVGcall;

#define TILE_TEX_WIDTH 256

// used as temporary data structure when splitting large paths into tiles
struct GLNVGtile {
  float* edges;
  int nedges;
  int cedges;
};
typedef struct GLNVGtile GLNVGtile;

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
  GLuint vertBuf;  // VBO
  GLuint vertArr;  // VAO
  GLuint fragBuf;  // UBO
  GLuint texEdges;

  float devicePixelRatio;
  int fragSize;
  int flags;
  int tilesize;

  // Per frame buffers
  GLNVGcall* calls;
  int ccalls;
  int ncalls;
  NVGvertex* verts;
  int cverts;
  int nverts;
  unsigned char* uniforms;
  int cuniforms;
  int nuniforms;
  NVGvertex* edges;
  int cedges;
  int nedges;
  // temporary buffers used for tiling large fills
  GLNVGtile* tiles;
  int ntiles;
};
typedef struct GLNVGcontext GLNVGcontext;

#ifndef NVG_LOG
#include <stdio.h>
#define NVG_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

static float glnvg__maxf(float a, float b) { return a < b ? b : a; }
static float glnvg__minf(float a, float b) { return a < b ? a : b; }
static float glnvg__clampf(float a, float mn, float mx) { return a < mn ? mn : (a > mx ? mx : a); }
static int glnvg__maxi(int a, int b) { return a < b ? b : a; }
static int glnvg__mini(int a, int b) { return a < b ? a : b; }
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
  glBindTexture(GL_TEXTURE_2D, tex);
}

static void glnvg__blendFuncSeparate(GLNVGcontext* gl, const GLNVGblend* blend)
{
  glBlendFuncSeparate(blend->srcRGB, blend->dstRGB, blend->srcAlpha,blend->dstAlpha);
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
  shader->loc[GLNVG_LOC_TEX] = glGetUniformLocation(shader->prog, "imageTex");
  shader->loc[GLNVG_LOC_EDGES] = glGetUniformLocation(shader->prog, "edgeTex");
  shader->loc[GLNVG_LOC_TYPE] = glGetUniformLocation(shader->prog, "type");
  shader->loc[GLNVG_LOC_NEDGES] = glGetUniformLocation(shader->prog, "nedges");
  shader->loc[GLNVG_LOC_OFFSET] = glGetUniformLocation(shader->prog, "offset");

#if NANOVG_GL_USE_UNIFORMBUFFER
  shader->loc[GLNVG_LOC_FRAG] = glGetUniformBlockIndex(shader->prog, "frag");
#else
  shader->loc[GLNVG_LOC_FRAG] = glGetUniformLocation(shader->prog, "frag");
#endif
}

// Overview of algorithm for antialiased fills:
// One GL draw call for bounding quad for each path - iterates over edges stored in RGBA32F texture, summing
//  covered area calculated with areaEdge2 - see nanovg_gl.h for more explanation (incl ASCII art).
// For large fills, bounding quad is broken up into tiles and only edges affecting the tile are included, with
//  edges entirely above tile being merged
// Alternate approach of combining edges from all paths for tile and making only one draw call per tile per
//  frame was implemented in nanovg_tile.h, removed 2022/10/27 - performance was slightly worse than the
//  current approach and there were remaining issues w/ multiple images on a tile and atlas text.
// We should deduplicate frag shader code shared w/ nanovg_gl!

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
\n
\n  attribute vec2 va_in;
\n  attribute vec2 vb_in;
\n
\n  varying vec2 va;
\n  varying vec2 vb;
\n
\n  void main()
\n  {
\n    // nanovg passes vertices in screen coords!
\n    va = va_in;
\n    vb = vb_in;
\n
\n    // convert from screen coords to clip coords
\n    vec2 pos_ex = va_in;
\n    gl_Position = vec4(2.0f*pos_ex.x/viewSize.x - 1.0f, 1.0f - 2.0f*pos_ex.y/viewSize.y, 0.0f, 1.0f);
\n  }
  );

  // Use github.com/KhronosGroup/glslang (apt install glslang-tools) to validate shader source
  static const char* fillFragShader = NVG_QUOTE
   (
\n
\n  #ifdef GL_ES
\n  precision highp float;
\n  precision highp int;
\n  precision highp sampler2D;
\n  precision highp sampler2DArray;
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
\n  #if defined(NANOVG_GL3)
\n  layout (location = 0) out vec4 outColor;
\n  #else
\n  #define outColor gl_FragData[0]
\n  #endif
\n
\n  #define TILE_TEX_WIDTH 256
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
\n
\n  uniform sampler2D imageTex;
\n  uniform sampler2DArray edgeTex;
\n  uniform vec2 viewSize;
\n  uniform int type;
\n  uniform int nedges;
\n  uniform int offset;
\n
\n  varying vec2 va;
\n  varying vec2 vb;
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
\n    if((fillMode & NVG_PATH_NO_AA) != 0)
\n      return v1.x < v0.x ? coversCenter(v1, v0) : -coversCenter(v0, v1);
\n    if(v0.y < -0.5f && v1.y < -0.5f)  // entirely below pixel - note that this isn't useful for nanovg_gl
\n      return 0.0f;
\n    vec2 window = clamp(vec2(v0.x, v1.x), -0.5f, 0.5f);
\n    float width = window.y - window.x;
\n    if(width == 0.0f)  // entirely left or right
\n      return 0.0f;
\n    if(v0.y > 0.5f && v1.y > 0.5f)  // entirely above pixel
\n      return -width;
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
\n    return area * width;
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
\n  vec4 edgeFetch(int idx)
\n  {
\n    // col += 1; if(col >= TILE_TEX_WIDTH) { col = 0; row += 1; if(row >= TILE_TEX_WIDTH) { row = 0; layer += 1; } } -- no detectable improvement
\n    int idx0 = idx + offset;
\n    int layer = idx0/(TILE_TEX_WIDTH*TILE_TEX_WIDTH);
\n    int idx1 = idx0 - layer*(TILE_TEX_WIDTH*TILE_TEX_WIDTH);
\n    int row = idx1/TILE_TEX_WIDTH;
\n    int col = idx1 - row*TILE_TEX_WIDTH;
\n    return texelFetch(edgeTex, ivec3(col, row, layer), 0);
\n }
\n
\n  float coverage(float W)
\n  {
\n    if((fillMode & NVG_PATH_CONVEX) != 0)
\n      return 1.0f;
\n    if((fillMode & NVG_PATH_EVENODD) != 0)
\n      return 1.0f - abs(mod(W, 2.0f) - 1.0f);
\n    return min(abs(W), 1.0f);  // non-zero fill
\n  }
\n
\n  void main(void)
\n  {
\n    vec4 result;
\n    vec2 fpos = vec2(gl_FragCoord.x, viewSize.y - gl_FragCoord.y);
\n    float W = 0.0f;
\n    for(int ii = 0; ii < nedges; ++ii) {
\n      vec4 edge = edgeFetch(ii);
\n      W += areaEdge2(edge.zw - fpos, edge.xy - fpos);  //noAA ? coversCenter(vb, va) :
\n    }
\n    float cov = coverage(W);
\n    if (type == 0) {  // not used
\n      result = vec4(1.0f, 0, 0, 1.0f);
\n    } else if (type == 1) {  // no scissor
\n      result = innerCol*cov;
\n    } else if (type == 2) {  // Solid color
\n      result = innerCol*(scissorMask(fpos)*cov);
\n    } else if (type == 3) {  // Gradient
\n      // Calculate gradient color using box gradient
\n      vec2 pt = (paintMat * vec3(fpos,1.0)).xy;
\n      float d = clamp((sdroundrect(pt, extent, radius) + feather*0.5) / feather, 0.0, 1.0);
\n      vec4 color = texType > 0 ? texture2D(imageTex, vec2(d,0)) : mix(innerCol,outerCol,d);
\n      if (texType == 1) color = vec4(color.rgb*color.a, color.a);
\n      // Combine alpha
\n      result = color*(scissorMask(fpos)*cov);
\n    } else if (type == 4) {  // Image
\n      // Calculate color from texture
\n      vec2 pt = (paintMat * vec3(fpos,1.0)).xy / extent;
\n      vec4 color = texture2D(imageTex, pt);
\n      if (texType == 1) color = vec4(color.rgb*color.a,color.a);
\n      else if (texType == 2) color = vec4(color.r);
\n      // Apply color tint and alpha.
\n      color *= innerCol;
\n      // Combine alpha
\n      result = color*(scissorMask(fpos)*cov);
\n    } else if (type == 5) {  // Textured tris - only used for text, so no need for coverage()
\n  #ifdef USE_SDF_TEXT
\n      float tcov = scissorMask(fpos)*superSDF(imageTex, ftcoord);
\n  #else
\n      float tcov = scissorMask(fpos)*summedTextCov(imageTex, ftcoord);
\n  #endif
\n      result = vec4(tcov) * innerCol;
\n    }
\n    outColor = result;
\n  }
  );

  GLNVGcontext* gl = (GLNVGcontext*)uptr;
  int align = 4;
  int tileflag = (gl->flags & NVGL_TILE_SIZE_MASK) >> 4;
  const char* sdfDef = (gl->flags & NVG_SDF_TEXT) ? "#define USE_SDF_TEXT 1\n" : "";
  const char* vertsrc[] = { shaderHeader, sdfDef, fillVertShader };
  const char* fragsrc[] = { shaderHeader, sdfDef, fillFragShader };

  if (gl->flags & NVGL_DEFER_INIT) return 1;
  glnvg__checkError(gl, "init");

  if(glnvg__createProgram(&gl->shader, vertsrc, 3, fragsrc, 3) == 0)
    return 0;

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

  glGenTextures(1, &gl->texEdges);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, gl->texEdges);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

  gl->tilesize = tileflag > 0 ? 1 << (tileflag + 1) : 32;

  NVG_LOG("nvg2: GL vector texture renderer (%d x %d)\n", gl->tilesize, gl->tilesize);
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
  gl->nedges = 0;
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
  gl->view[0] = width;
  gl->view[1] = height;
  if (gl->flags & NVGL_DEFER_INIT) {
    gl->flags &= ~NVGL_DEFER_INIT;  // clear flag
    glnvg__renderCreate(uptr);
  }
  // renderViewport is called at start of frame, so reset
  glnvg__renderCancel(uptr);
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

static void glnvg__renderFlush(void* uptr)
{
  GLNVGcontext* gl = (GLNVGcontext*)uptr;
  int i;

  if (gl->ncalls <= 0) {
    glnvg__renderCancel(uptr);
    return;
  }
#ifdef NVGL_STATS
  double npix = 0, npixedges = 0;
  for(i = 0; i < gl->ncalls; i++) {
    GLNVGcall* call = &gl->calls[i];
    NVGvertex* lt = &gl->verts[call->triangleOffset];
    NVGvertex* rb = &gl->verts[call->triangleOffset+3];
    double callpix = (rb->x0 - lt->x0)*(rb->y0 - lt->y0);
    npix += callpix;
    npixedges += call->fillCount*callpix;
  }
  NVG_LOG("renderFlush: %d calls, %d uniform blocks, %d edges, %.0f pixels, %.0f edges*pixels\n",
      gl->ncalls, gl->nuniforms, gl->nedges, npix, npixedges);
#endif
  glUseProgram(gl->shader.prog);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  //glDisable(GL_SCISSOR_TEST);
  //glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

  // Upload vertex data
#if defined NANOVG_GL3
  glBindVertexArray(gl->vertArr);
#endif
  glBindBuffer(GL_ARRAY_BUFFER, gl->vertBuf);
  glBufferData(GL_ARRAY_BUFFER, gl->nverts * sizeof(NVGvertex), gl->verts, GL_STREAM_DRAW);
  // 0 = va_in, 1 = vb_in as set by glBindAttribLocation in glnvg__createShader
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(NVGvertex), (const GLvoid*)(size_t)0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(NVGvertex), (const GLvoid*)(0 + 2*sizeof(float)));  //offsetof(NVGVertex, x1)

  int layersize = TILE_TEX_WIDTH*TILE_TEX_WIDTH;
  int ntilelayers = gl->nedges/layersize + (gl->nedges % layersize != 0);
  glActiveTexture(GL_TEXTURE0 + 1);
  glBindTexture(GL_TEXTURE_2D_ARRAY, gl->texEdges);
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA32F, TILE_TEX_WIDTH, TILE_TEX_WIDTH, ntilelayers, 0, GL_RGBA, GL_FLOAT, gl->edges);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);

  // Set view and texture locations just once per frame.
  glUniform2fv(gl->shader.loc[GLNVG_LOC_VIEWSIZE], 1, gl->view);
  glUniform1i(gl->shader.loc[GLNVG_LOC_TEX], 0);
  glUniform1i(gl->shader.loc[GLNVG_LOC_EDGES], 1);  // edge data texture array

#if NANOVG_GL_USE_UNIFORMBUFFER
  // Upload ubo for frag shaders
  glBindBuffer(GL_UNIFORM_BUFFER, gl->fragBuf);
  glBufferData(GL_UNIFORM_BUFFER, gl->nuniforms * gl->fragSize, gl->uniforms, GL_STREAM_DRAW);
#endif
  glnvg__checkError(gl, "renderFlush setup");

  for (i = 0; i < gl->ncalls; i++) {
    GLNVGcall* call = &gl->calls[i];
    GLNVGblend* blend = &call->blendFunc;
    GLNVGblend* prevblend = i > 0 ? &gl->calls[i-1].blendFunc : NULL;
    if(i == 0 || call->uniformOffset != gl->calls[i-1].uniformOffset || call->image != gl->calls[i-1].image)
      glnvg__setUniforms(gl, call->uniformOffset, call->image);
    if(!prevblend || blend->srcRGB != prevblend->srcRGB || blend->srcAlpha != prevblend->srcAlpha
        || blend->dstRGB != prevblend->dstRGB || blend->dstAlpha != prevblend->dstAlpha)
      glnvg__blendFuncSeparate(gl, &call->blendFunc);
    glUniform1i(gl->shader.loc[GLNVG_LOC_TYPE], call->fragType);
    glUniform1i(gl->shader.loc[GLNVG_LOC_NEDGES], call->fillCount);  // will be zero except for GLNVG_FILL
    glUniform1i(gl->shader.loc[GLNVG_LOC_OFFSET], call->fillOffset);  // avoid problems due to undef uniforms
    if (call->type == GLNVG_FILL)
      glDrawArrays(GL_TRIANGLE_STRIP, call->triangleOffset, call->triangleCount);
    else if (call->type == GLNVG_CONVEXFILL)
      glDrawArrays(GL_TRIANGLE_FAN, call->triangleOffset, call->triangleCount);
    else if (call->type == GLNVG_TRIANGLES)
      glDrawArrays(GL_TRIANGLES, call->triangleOffset, call->triangleCount);
    glnvg__checkError(gl, "renderFlush call");
  }

  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
#if defined NANOVG_GL3
  glBindVertexArray(0);
#endif
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glUseProgram(0);
  glnvg__bindTexture(gl, 0);
  glActiveTexture(GL_TEXTURE0 + 1);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
  glnvg__checkError(gl, "renderFlush cleanup");

  // Reset calls
  //glnvg__renderCancel(uptr);  -- we now reset at start of frame to allow frame to be drawn more than once
}

static int glnvg__allocCalls(GLNVGcontext* gl, int n)
{
  if (gl->ncalls+n > gl->ccalls) {
    int ccalls = glnvg__maxi(gl->ncalls+n, 128) + gl->ccalls/2; // 1.5x Overallocate
    GLNVGcall* calls = (GLNVGcall*)realloc(gl->calls, sizeof(GLNVGcall) * ccalls);
    if (calls == NULL) return -1;
    gl->calls = calls;
    gl->ccalls = ccalls;
  }
  memset(&gl->calls[gl->ncalls], 0, sizeof(GLNVGcall)*n);
  gl->ncalls += n;
  return gl->ncalls - n;
}

static int glnvg__allocEdges(GLNVGcontext* gl, int n)
{
  if (gl->nedges + n > gl->cedges) {
    int layersize = TILE_TEX_WIDTH*TILE_TEX_WIDTH;
    int cedges = glnvg__maxi(gl->nedges + n, 128) + gl->cedges/2; // 1.5x Overallocate
    cedges = (cedges/layersize + (cedges % layersize != 0)) * layersize;
    void* edges = realloc(gl->edges, sizeof(NVGvertex) * cedges);
    if (!edges) return -1;
    gl->cedges = cedges;
    gl->edges = (NVGvertex*)edges;
  }
  gl->nedges += n;
  return gl->nedges - n;
}

static int glnvg__allocVerts(GLNVGcontext* gl, int n)
{
  if (gl->nverts+n > gl->cverts) {
    int cverts = glnvg__maxi(gl->nverts + n, 4096) + gl->cverts/2; // 1.5x Overallocate
    NVGvertex* verts = (NVGvertex*)realloc(gl->verts, sizeof(NVGvertex) * cverts);
    if (verts == NULL) return -1;
    gl->verts = verts;
    gl->cverts = cverts;
  }
  gl->nverts += n;
  return gl->nverts - n;
}

static int glnvg__allocFragUniforms(GLNVGcontext* gl, int n)
{
  int ret = 0, structSize = gl->fragSize;
  if (gl->nuniforms+n > gl->cuniforms) {
    int cuniforms = glnvg__maxi(gl->nuniforms+n, 128) + gl->cuniforms/2; // 1.5x Overallocate
    unsigned char* uniforms = (unsigned char*)realloc(gl->uniforms, structSize * cuniforms);
    if (uniforms == NULL) return -1;
    gl->uniforms = uniforms;
    gl->cuniforms = cuniforms;
  }
  ret = gl->nuniforms * structSize;
  gl->nuniforms += n;
  return ret;
}

static void glnvg__vset2(NVGvertex* vtx, float x0, float y0, float x1, float y1)   //, float u, float v)
{
  vtx->x0 = x0;
  vtx->y0 = y0;
  vtx->x1 = x1;
  vtx->y1 = y1;
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

  int offset = glnvg__allocFragUniforms(gl, 1);
  if (offset == -1) return -1;
  GLNVGfragUniforms* frag = nvg__fragUniformPtr(gl, offset);
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
    call->fragType = GLNVG_FILL_SIMPLE;  // will be overridden if image or gradient present
  } else {
    nvgTransformInverse(invxform, scissor->xform);
    glnvg__xformToMat3x4(frag->scissorMat, invxform);
    frag->scissorExt[0] = scissor->extent[0];
    frag->scissorExt[1] = scissor->extent[1];
    frag->scissorScale[0] = sqrtf(scissor->xform[0]*scissor->xform[0] + scissor->xform[2]*scissor->xform[2])*gl->devicePixelRatio;
    frag->scissorScale[1] = sqrtf(scissor->xform[1]*scissor->xform[1] + scissor->xform[3]*scissor->xform[3])*gl->devicePixelRatio;
    call->fragType = GLNVG_FILL_SOLID;
  }

  memcpy(frag->extent, paint->extent, sizeof(frag->extent));

  // texture used for images and complex gradients
  if(paint->image != 0) {
    call->image = paint->image;
    tex = glnvg__findTexture(gl, paint->image);
    if (tex == NULL) return -1;
    if (tex->type == NVG_TEXTURE_RGBA)
      frag->texType = (tex->flags & NVG_IMAGE_PREMULTIPLIED) ? 3 : 1;
    else
      frag->texType = 2;
  }

  if (paint->innerColor.c != paint->outerColor.c) {
    call->fragType = GLNVG_FILL_GRAD;
    frag->radius = paint->radius;
    frag->feather = paint->feather;
    nvgTransformInverse(invxform, paint->xform);
    glnvg__xformToMat3x4(frag->paintMat, invxform);
  } else if (paint->image != 0) {
    call->fragType = GLNVG_FILL_IMG;
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
    frag->radius = paint->radius + 0.5f;  // radius used for SDF text weight
  }

  // reuse uniforms from last call if identical
  if(offset > 0 && memcmp(frag, nvg__fragUniformPtr(gl, offset - gl->fragSize), sizeof(GLNVGfragUniforms)) == 0) {
    offset -= gl->fragSize;
    gl->nuniforms -= 1;
  }

  return offset;
}

static void glnvg__quad(NVGvertex* quad, const float* bounds, float pad)
{
  glnvg__vset2(&quad[0], bounds[2] + pad, bounds[3] + pad, 0, 0);
  glnvg__vset2(&quad[1], bounds[2] + pad, bounds[1] - pad, 0, 0);
  glnvg__vset2(&quad[2], bounds[0] - pad, bounds[3] + pad, 0, 0);
  glnvg__vset2(&quad[3], bounds[0] - pad, bounds[1] - pad, 0, 0);
  //return 4;  //quad + 4;
}

static void glnvg__renderFill(void* uptr, NVGpaint* paint, NVGcompositeOperationState compOp,
    NVGscissor* scissor, int flags, const float* bounds, const NVGpath* paths, int npaths)
{
  GLNVGcontext* gl = (GLNVGcontext*)uptr;
  int i, edgeidx, triidx, callidx, ncalls = 0, nedges = 0;
  float ltrb[4];
  for (i = 0; i < npaths; ++i) {
    nedges += paths[i].nfill;
    if(i == 0 || paths[i].restart) ++ncalls;
  }
  if (nedges == 0) return;
  ltrb[0] = glnvg__clampf(bounds[0], 0, gl->view[0]);
  ltrb[1] = glnvg__clampf(bounds[1], 0, gl->view[1]);
  ltrb[2] = glnvg__clampf(bounds[2], 0, gl->view[0]);
  ltrb[3] = glnvg__clampf(bounds[3], 0, gl->view[1]);
  float callw = ltrb[2] - ltrb[0], callh = ltrb[3] - ltrb[1];
  if (callw <= 0 || callh <= 0) return;  // offscreen path
  // must set convex flag before glnvg__convertPaint (due to reuse of identical uniform blocks)
  if (npaths == 1 && paths[0].convex && paths[0].nfill > 2)
    flags |= NVG_PATH_CONVEX;

  int call0 = glnvg__allocCalls(gl, 1);
  if (call0 < 0) return;
  GLNVGcall* call = &gl->calls[call0];
  call->type = GLNVG_FILL;
  call->blendFunc = glnvg__blendCompositeOperation(compOp);
  call->uniformOffset = glnvg__convertPaint(gl, call, paint, scissor, flags);
  if (call->uniformOffset == -1) goto error;

  // convex actually means fannable and non-antialiased - this is ~2x faster for rectangles (worth keeping?)
  if (flags & NVG_PATH_CONVEX) {
    const NVGpath* path = &paths[0];
    call->type = GLNVG_CONVEXFILL;
    call->fillOffset = 0;
    call->fillCount = 0;
    call->triangleCount = path->nfill;
    triidx = glnvg__allocVerts(gl, call->triangleCount);
    if (triidx == -1) goto error;
    call->triangleOffset = triidx;
    // vertices for triangle fan
    memcpy(&gl->verts[triidx], path->fill, path->nfill*sizeof(NVGvertex));
  } else if (ncalls == 1 && nedges > 16 && (callw > 2*gl->tilesize || callh > 2*gl->tilesize)) {
    // break large paths into tiles - note that some tiles may be empty
    // expand bounds to include all touched pixels
    ltrb[0] = floorf(ltrb[0]); ltrb[1] = floorf(ltrb[1]); ltrb[2] = ceilf(ltrb[2]); ltrb[3] = ceilf(ltrb[3]);
    callw = ltrb[2] - ltrb[0]; callh = ltrb[3] - ltrb[1];
    int xtiles = ceilf(callw/gl->tilesize), ytiles = ceilf(callh/gl->tilesize);
    int ntiles = xtiles*ytiles;
    int tilew = ceilf(callw/xtiles), tileh = ceilf(callh/ytiles);
    if (ntiles > gl->ntiles) {
      gl->tiles = (GLNVGtile*)realloc(gl->tiles, ntiles*sizeof(GLNVGtile));
      memset(gl->tiles + gl->ntiles, 0, (ntiles - gl->ntiles)*sizeof(GLNVGtile));
      gl->ntiles = ntiles;
    }
    // reuse ncalls, nedges
    nedges = 0;
    ncalls = 0;
    for (i = 0; i < npaths; i++) {
      const NVGpath* path = &paths[i];
      if (path->nfill == 0) continue;
      int pymin = glnvg__clampi(((int)(path->bounds[1] - ltrb[1] - 0.5f))/tileh, 0, ytiles-1);
      for (int j = 0; j < path->nfill; ++j) {
        float x0 = path->fill[j].x0, y0 = path->fill[j].y0, x1 = path->fill[j].x1, y1 = path->fill[j].y1;
        //if((x0 < -0.5f && x1 < -0.5f) || (x0 > gl->view[0]+0.5f && x1 > gl->view[0]+0.5f)) continue;
        if (x0 == x1) continue;  // skip vertical edges
        // find range of tiles overlapping edge
        int vxmin = glnvg__clampi(((int)(glnvg__minf(x0, x1) - ltrb[0] - 0.5f))/tilew, 0, xtiles-1);
        int vxmax = glnvg__clampi(((int)(glnvg__maxf(x0, x1) - ltrb[0] + 0.5f))/tilew, 0, xtiles-1);
        int vymax = glnvg__clampi(((int)(glnvg__maxf(y0, y1) - ltrb[1] + 0.5f))/tileh, 0, ytiles-1);
        // write edge to each tile
        for (int ix = vxmin; ix <= vxmax; ++ix) {
          for (int iy = pymin; iy <= vymax; ++iy) {
            GLNVGtile* tile = &gl->tiles[xtiles*iy + ix];
            if (tile->nedges + 1 > tile->cedges) {
              tile->cedges = glnvg__maxi(2*tile->cedges, 16);
              tile->edges = (float*)realloc(tile->edges, 4*sizeof(float)*tile->cedges);
            }
            float* vtx = tile->edges + 4*tile->nedges;
            if (tile->nedges > 0) {
              // combine (flatten) connected edges lying entirely above this tile
              float tymax = (iy+1)*tileh + ltrb[1];
              if (y0 > tymax && y1 > tymax && vtx[-3] > tymax && x0 == vtx[-2] && y0 == vtx[-1]) {
                vtx[-2] = x1; vtx[-1] = y1;
                continue;
              }
            }
            else
              ++ncalls;
            *vtx++ = x0; *vtx++ = y0; *vtx++ = x1; *vtx++ = y1;
            ++tile->nedges;
            ++nedges;
          }
        }
      }
    }

    edgeidx = glnvg__allocEdges(gl, nedges);
    if (edgeidx < 0) goto error;
    triidx = glnvg__allocVerts(gl, 4*ncalls);
    if (triidx < 0) goto error;
    // create call for each (non-empty) tile
    if (ncalls > 1 && glnvg__allocCalls(gl, ncalls-1) < 0) goto error;
    callidx = call0;

    for (int ix = 0; ix < xtiles; ++ix) {
      for (int iy = 0; iy < ytiles; ++iy) {
        GLNVGtile* tile = &gl->tiles[xtiles*iy + ix];
        if (tile->nedges == 0) continue;
        call = &gl->calls[callidx];
        float tilebounds[4] = {ltrb[0] + ix*tilew, ltrb[1] + iy*tileh,
            glnvg__minf(ltrb[2], ltrb[0] + (ix+1)*tilew), glnvg__minf(ltrb[3], ltrb[1] + (iy+1)*tileh)};
        memcpy(&gl->edges[edgeidx], tile->edges, sizeof(NVGvertex) * tile->nedges);
        if (callidx != call0)
          memcpy(call, &gl->calls[call0], sizeof(GLNVGcall));
        call->fillOffset = edgeidx;
        call->fillCount = tile->nedges;
        call->triangleOffset = triidx;
        call->triangleCount = 4;
        glnvg__quad(&gl->verts[triidx], tilebounds, 0);
        edgeidx += call->fillCount;
        triidx += call->triangleCount;
        ++callidx;
        tile->nedges = 0;  // reset tile
      }
    }
  } else if(ncalls == 1) {
    edgeidx = glnvg__allocEdges(gl, nedges);
    if (edgeidx < 0) goto error;
    triidx = glnvg__allocVerts(gl, 4);
    if (triidx < 0) goto error;
    call->fillOffset = edgeidx;
    call->fillCount = nedges;
    call->triangleOffset = triidx;
    call->triangleCount = 4;
    for (i = 0; i < npaths; i++) {
      memcpy(&gl->edges[edgeidx], paths[i].fill, sizeof(NVGvertex) * paths[i].nfill);
      edgeidx += paths[i].nfill;
    }
    glnvg__quad(&gl->verts[triidx], ltrb, 0.5f);  // ncalls > 1 ? ltrb : bounds
  } else {
    float callbnds[4] = {1e6f, 1e6f, -1e6f, -1e6f};
    edgeidx = glnvg__allocEdges(gl, nedges);
    if (edgeidx < 0) goto error;
    triidx = glnvg__allocVerts(gl, 4*ncalls);
    if (triidx < 0) goto error;
    if (ncalls > 1 && glnvg__allocCalls(gl, ncalls - 1) < 0) goto error;
    callidx = call0;

    nedges = 0;
    for (i = 0; i < npaths; i++) {
      memcpy(&gl->edges[edgeidx], paths[i].fill, sizeof(NVGvertex) * paths[i].nfill);
      edgeidx += paths[i].nfill;
      nedges += paths[i].nfill;
      callbnds[0] = glnvg__minf(callbnds[0], paths[i].bounds[0]);
      callbnds[1] = glnvg__minf(callbnds[1], paths[i].bounds[1]);
      callbnds[2] = glnvg__maxf(callbnds[2], paths[i].bounds[2]);
      callbnds[3] = glnvg__maxf(callbnds[3], paths[i].bounds[3]);

      if (i + 1 == npaths || paths[i+1].restart) {
        callbnds[0] = glnvg__maxf(ltrb[0], callbnds[0]);
        callbnds[1] = glnvg__maxf(ltrb[1], callbnds[1]);
        callbnds[2] = glnvg__minf(ltrb[2], callbnds[2]);
        callbnds[3] = glnvg__minf(ltrb[3], callbnds[3]);
        if (callbnds[0] >= callbnds[2] || callbnds[1] >= callbnds[3]) {
          gl->ncalls--;
          gl->nverts -= 4;
          gl->nedges -= nedges;
          edgeidx -= nedges;
        } else {
          call = &gl->calls[callidx];
          if (callidx != call0)
            memcpy(call, &gl->calls[call0], sizeof(GLNVGcall));
          call->fillOffset = edgeidx - nedges;
          call->fillCount = nedges;
          call->triangleOffset = triidx;
          call->triangleCount = 4;
          glnvg__quad(&gl->verts[triidx], callbnds, 0.5f);  // ncalls > 1 ? ltrb : bounds
          triidx += call->triangleCount;
          ++callidx;
        }
        nedges = 0;
        callbnds[0] = callbnds[1] = 1e6f;
        callbnds[2] = callbnds[3] = -1e6f;
      }
    }
  }
  return;
error:
  // We get here if first call alloc was ok, but something went wrong before alloc of any additional calls
  if (gl->ncalls > 0) gl->ncalls--;
}

static void glnvg__renderTriangles(void* uptr, NVGpaint* paint, NVGcompositeOperationState compOp,
    NVGscissor* scissor, const NVGvertex* verts, int nverts)
{
  GLNVGcontext* gl = (GLNVGcontext*)uptr;
  int callidx = glnvg__allocCalls(gl, 1);
  if (callidx < 0) return;
  GLNVGcall* call = &gl->calls[callidx];

  call->type = GLNVG_TRIANGLES;
  call->blendFunc = glnvg__blendCompositeOperation(compOp);
  // Allocate vertices for all the paths.
  call->triangleOffset = glnvg__allocVerts(gl, nverts);
  if (call->triangleOffset == -1) goto error;
  call->triangleCount = nverts;
  memcpy(&gl->verts[call->triangleOffset], verts, sizeof(NVGvertex) * nverts);

  // Fill shader
  call->uniformOffset = glnvg__convertPaint(gl, call, paint, scissor, 0);
  if (call->uniformOffset == -1) goto error;
  call->fragType = GLNVG_FILL_TEXT;
  return;
error:
  if (gl->ncalls > 0) gl->ncalls--;  // skip call if allocation error
}

static void glnvg__renderDelete(void* uptr)
{
  GLNVGcontext* gl = (GLNVGcontext*)uptr;
  int i;
  if (gl == NULL) return;

  if (!(gl->flags & NVGL_DELETE_NO_GL)) {
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

    glDeleteTextures(1, &gl->texEdges);

    for (i = 0; i < gl->ntextures; i++) {
      if (gl->textures[i].tex != 0 && (gl->textures[i].flags & NVG_IMAGE_NODELETE) == 0)
        glDeleteTextures(1, &gl->textures[i].tex);
    }
  }

  for(i = 0; i < gl->ntiles; ++i)
    free(gl->tiles[i].edges);
  free(gl->tiles);
  free(gl->textures);
  free(gl->edges);
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
