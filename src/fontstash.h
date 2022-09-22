//
// Copyright (c) 2020 Stylus Labs - see LICENSE.txt
//   based on nanovg:
// Copyright (c) 2013 Mikko Mononen memon@inside.org
//
#ifndef FONS_H
#define FONS_H

#define FONS_INVALID -1

enum FONSflags {
  FONS_ZERO_TOPLEFT = 1,
  FONS_ZERO_BOTTOMLEFT = 2,
  FONS_DELAY_LOAD = 4,
};

enum FONSalign {
  // Horizontal align
  FONS_ALIGN_LEFT 	= 1<<0,	// Default
  FONS_ALIGN_CENTER 	= 1<<1,
  FONS_ALIGN_RIGHT 	= 1<<2,
  // Vertical align
  FONS_ALIGN_TOP 		= 1<<3,
  FONS_ALIGN_MIDDLE	= 1<<4,
  FONS_ALIGN_BOTTOM	= 1<<5,
  FONS_ALIGN_BASELINE	= 1<<6, // Default
};

enum FONSglyphBitmap {
  FONS_GLYPH_BITMAP_OPTIONAL = 1,
  FONS_GLYPH_BITMAP_REQUIRED = 2,
};

enum FONSerrorCode {
  // Font atlas is full.
  FONS_ATLAS_FULL = 1,
  // Scratch memory used to render glyphs is full, requested size reported in 'val', you may need to bump up FONS_SCRATCH_BUF_SIZE.
  FONS_SCRATCH_FULL = 2,
  // Calls to fonsPushState has created too large stack, if you need deep state stack bump up FONS_MAX_STATES.
  FONS_STATES_OVERFLOW = 3,
  // Trying to pop too many states fonsPopState().
  FONS_STATES_UNDERFLOW = 4,
};

struct FONSparams {
  //int width, height;
  //int cellw, cellh;
  //int atlasFontSize;
  unsigned char flags;
  void* userPtr;
  int (*renderCreate)(void* uptr, int width, int height);
  int (*renderResize)(void* uptr, int width, int height);
  void (*renderUpdate)(void* uptr, int* rect, const void* data);
  void (*renderDraw)(void* uptr, const float* verts, const float* tcoords, const unsigned int* colors, int nverts);
  void (*renderDelete)(void* uptr);
};
typedef struct FONSparams FONSparams;

struct FONSquad
{
  float x0,y0,s0,t0;
  float x1,y1,s1,t1;
};
typedef struct FONSquad FONSquad;

struct FONStextIter {
  float x, y, nextx, nexty, spacing;  //scale
  unsigned int codepoint;
  short isize, iblur;
  int font; //struct FONSfont* font;
  int prevGlyphFont;
  int prevGlyphIndex;
  const char* str;
  const char* next;
  const char* end;
  unsigned int utf8state;
  int bitmapOption;
};
typedef struct FONStextIter FONStextIter;

typedef struct FONScontext FONScontext;

// Constructor and destructor.
FONScontext* fonsCreateInternal(FONSparams* params);
void fonsDeleteInternal(FONScontext* s);

void fonsSetErrorCallback(FONScontext* s, void (*callback)(void* uptr, int error, int val), void* uptr);
// Returns current atlas size.
void fonsGetAtlasSize(FONScontext* s, int* width, int* height, int* atlasFontPx);
// Expands the atlas size.
int fonsExpandAtlas(FONScontext* s, int width, int height);
// Resets the whole stash.
int fonsResetAtlas(FONScontext* stash, int width, int height, int cellw, int cellh, int atlasFontPx);

// Add fonts
int fonsAddFont(FONScontext* s, const char* name, const char* path);
int fonsAddFontMem(FONScontext* s, const char* name, unsigned char* data, int ndata, int freeData);
int fonsGetFontByName(FONScontext* s, const char* name);
void* fonsGetFontImpl(FONScontext* stash, int font);

// State handling
void fonsPushState(FONScontext* s);
void fonsPopState(FONScontext* s);
void fonsClearState(FONScontext* s);

// State setting
// internally, we work in pixel sizes (based on total glyph height: ascent - descent) but font sizes are
//  usually specified in em units (roughly height of 'M'); the ratio between these varies between fonts
// Because of how the summed text works, we have to use the same pixel size for all text in a given draw call
//  even if these corresponds to slightly different em sizes for fallback glyphs
void fonsSetSize(FONScontext* s, float size);
void fonsSetColor(FONScontext* s, unsigned int color);
void fonsSetSpacing(FONScontext* s, float spacing);
void fonsSetBlur(FONScontext* s, float blur);
void fonsSetAlign(FONScontext* s, int align);
void fonsSetFont(FONScontext* s, int font);
// get the font height (size used by fontstash) for a given em size (the standard "font size")
float fonsEmSizeToSize(FONScontext* s, float emsize);
// get the current font height
float fonsGetSize(FONScontext* s);

// Draw text
float fonsDrawText(FONScontext* s, float x, float y, const char* string, const char* end);

// Measure text
float fonsTextBounds(FONScontext* s, float x, float y, const char* string, const char* end, float* bounds);
void fonsLineBounds(FONScontext* s, float y, float* miny, float* maxy);
void fonsVertMetrics(FONScontext* s, float* ascender, float* descender, float* lineh);

// Text iterator
int fonsTextIterInit(FONScontext* stash, FONStextIter* iter, float x, float y, const char* str, const char* end, int bitmapOption);
int fonsTextIterNext(FONScontext* stash, FONStextIter* iter, struct FONSquad* quad);

// Pull texture changes
const void* fonsGetTextureData(FONScontext* stash, int* width, int* height);
int fonsValidateTexture(FONScontext* s, int* dirty);

// Draws the stash texture for debugging
void fonsDrawDebug(FONScontext* s, float x, float y);

#endif // FONTSTASH_H


#ifdef FONTSTASH_IMPLEMENTATION

#define FONS_NOTUSED(v)  (void)sizeof(v)

// basic idea of "summed" rendering is to add up values from glyph coverage bitmap to create a cumulative
//  coverage float32 texture s.t. the coverage inside any rectangle can be calculated by sampling at just
//  the four corners (using linear interpolation); this seems hacky, but results looks pretty good
//  with arbitrary subpixel positioning up to at least 50% of the atlas font size ... given that this is
//  16x more data than a usual atlas, probably not that impressive
// Larger size text can be drawn directly as paths (seems like the reasonable thing to do even with usual
//  font atlas approach), and of course if atlas font size is too big, numerical issues will appear!

#ifdef FONS_SUMMED
typedef float FONStexel;
#else
typedef unsigned char FONStexel;
#endif

#ifdef FONS_USE_FREETYPE

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H
#include <math.h>

struct FONSttFontImpl {
  FT_Face font;
};
typedef struct FONSttFontImpl FONSttFontImpl;

static FT_Library ftLibrary;

int fons__tt_init(FONScontext *context)
{
  FT_Error ftError;
  FONS_NOTUSED(context);
  ftError = FT_Init_FreeType(&ftLibrary);
  return ftError == 0;
}

int fons__tt_done(FONScontext *context)
{
  FT_Error ftError;
  FONS_NOTUSED(context);
  ftError = FT_Done_FreeType(ftLibrary);
  return ftError == 0;
}

int fons__tt_loadFont(FONScontext *context, FONSttFontImpl *font, unsigned char *data, int dataSize)
{
  FT_Error ftError;
  FONS_NOTUSED(context);

  //font->font.userdata = stash;
  ftError = FT_New_Memory_Face(ftLibrary, (const FT_Byte*)data, dataSize, 0, &font->font);
  return ftError == 0;
}

void fons__tt_getFontVMetrics(FONSttFontImpl *font, int *ascent, int *descent, int *lineGap)
{
  *ascent = font->font->ascender;
  *descent = font->font->descender;
  *lineGap = font->font->height - (*ascent - *descent);
}

float fons__tt_getPixelHeightScale(FONSttFontImpl *font, float size)
{
  return size / (font->font->ascender - font->font->descender);
}

int fons__tt_getGlyphIndex(FONSttFontImpl *font, int codepoint)
{
  return FT_Get_Char_Index(font->font, codepoint);
}

int fons__tt_buildGlyphBitmap(FONSttFontImpl *font, int glyph, float size, float scale,
                int *advance, int *lsb, int *x0, int *y0, int *x1, int *y1)
{
  FT_Error ftError;
  FT_GlyphSlot ftGlyph;
  FT_Fixed advFixed;
  FONS_NOTUSED(scale);

  ftError = FT_Set_Pixel_Sizes(font->font, 0, (FT_UInt)(size * (float)font->font->units_per_EM / (float)(font->font->ascender - font->font->descender)));
  if (ftError) return 0;
  ftError = FT_Load_Glyph(font->font, glyph, FT_LOAD_RENDER | FT_LOAD_FORCE_AUTOHINT);
  if (ftError) return 0;
  ftError = FT_Get_Advance(font->font, glyph, FT_LOAD_NO_SCALE, &advFixed);
  if (ftError) return 0;
  ftGlyph = font->font->glyph;
  *advance = (int)advFixed;
  *lsb = (int)ftGlyph->metrics.horiBearingX;
  *x0 = ftGlyph->bitmap_left;
  *x1 = *x0 + ftGlyph->bitmap.width;
  *y0 = -ftGlyph->bitmap_top;
  *y1 = *y0 + ftGlyph->bitmap.rows;
  return 1;
}

void fons__tt_renderGlyphBitmap(FONSttFontImpl *font,
    unsigned char *output, int outWidth, int outHeight, int outStride, float scale, int glyph)
{
  FT_GlyphSlot ftGlyph = font->font->glyph;
  int ftGlyphOffset = 0;
  int x, y;
  FONS_NOTUSED(outWidth);
  FONS_NOTUSED(outHeight);
  FONS_NOTUSED(scale);
  FONS_NOTUSED(glyph);	// glyph has already been loaded by fons__tt_buildGlyphBitmap

  for ( y = 0; y < ftGlyph->bitmap.rows; y++ ) {
    for ( x = 0; x < ftGlyph->bitmap.width; x++ ) {
      output[(y * outStride) + x] = ftGlyph->bitmap.buffer[ftGlyphOffset++];
    }
  }
}

int fons__tt_getGlyphKernAdvance(FONSttFontImpl *font, int glyph1, int glyph2)
{
  FT_Vector ftKerning;
  FT_Get_Kerning(font->font, glyph1, glyph2, FT_KERNING_DEFAULT, &ftKerning);
  return (int)((ftKerning.x + 32) >> 6);  // Round up and convert to integer
}

#else

#define STB_TRUETYPE_IMPLEMENTATION
static void* fons__tmpalloc(size_t size, void* up);
static void fons__tmpfree(void* ptr, void* up);
#define STBTT_malloc(x,u)    fons__tmpalloc(x,u)
#define STBTT_free(x,u)      fons__tmpfree(x,u)
#define STBTT_STATIC
#include "stb_truetype.h"

struct FONSttFontImpl {
  stbtt_fontinfo font;
};
typedef struct FONSttFontImpl FONSttFontImpl;

int fons__tt_init(FONScontext *context)
{
  FONS_NOTUSED(context);
  return 1;
}

int fons__tt_done(FONScontext *context)
{
  FONS_NOTUSED(context);
  return 1;
}

int fons__tt_loadFont(FONScontext *context, FONSttFontImpl *font, unsigned char *data, int dataSize)
{
  int stbError;
  FONS_NOTUSED(dataSize);

  font->font.userdata = context;
  stbError = stbtt_InitFont(&font->font, data, 0);
  return stbError;
}

void fons__tt_getFontVMetrics(FONSttFontImpl *font, int *ascent, int *descent, int *lineGap)
{
  stbtt_GetFontVMetrics(&font->font, ascent, descent, lineGap);
}

float fons__tt_getPixelHeightScale(FONSttFontImpl *font, float size)
{
  return stbtt_ScaleForPixelHeight(&font->font, size);
}

float fons__tt_getEmToPixelsScale(FONSttFontImpl *font, float size)
{
  return stbtt_ScaleForMappingEmToPixels(&font->font, size);
}

int fons__tt_getGlyphIndex(FONSttFontImpl *font, int codepoint)
{
  return stbtt_FindGlyphIndex(&font->font, codepoint);
}

int fons__tt_buildGlyphBitmap(FONSttFontImpl *font, int glyph, float size, float scale,
                int *advance, int *lsb, int *x0, int *y0, int *x1, int *y1)
{
  FONS_NOTUSED(size);
  stbtt_GetGlyphHMetrics(&font->font, glyph, advance, lsb);
  stbtt_GetGlyphBitmapBox(&font->font, glyph, scale, scale, x0, y0, x1, y1);
  return 1;
}

void fons__tt_renderGlyphBitmap(FONSttFontImpl *font,
    FONStexel *output, int outWidth, int outHeight, int outStride, float scale, int glyph)
{
#ifdef FONS_SUMMED
  int x, y;
  unsigned char* bitmap = (unsigned char*)malloc(outWidth*outHeight);
  stbtt_MakeGlyphBitmap(&font->font, bitmap, outWidth, outHeight, outWidth, scale, scale, glyph);
  for(y = 0; y < outHeight; ++y) {
    for(x = 0; x < outWidth; ++x) {
      FONStexel s10 = y > 0 ? output[(y-1)*outStride + x] : 0;
      FONStexel s01 = x > 0 ? output[y*outStride + (x-1)] : 0;
      FONStexel s00 = x > 0 && y > 0 ? output[(y-1)*outStride + (x-1)] : 0;
      FONStexel t11 = bitmap[y*outWidth + x]; // /255.0f
      output[y*outStride + x] = t11 + s10 + (s01 - s00);
    }
  }
  free(bitmap);
#elif defined (FONS_SDF)
  int x, y, w, h;  //x0, y0 -- offset from GetGlyphSDF is just the value from GetGlyphBitmapBox
  // ..., padding=4, onedge_value=127 (0-255), pixel_dist_scale=32 (1 pix = 32 distance units)
  unsigned char* bitmap = stbtt_GetGlyphSDF(&font->font, scale, glyph, 4, 127, 32.0, &w, &h, NULL, NULL);
  if (!bitmap) return;
  for (y = 0; y < h; ++y) {
    for (x = 0; x < w; ++x) {
      output[y*outStride + x] = bitmap[y*w + x];
    }
  }
  stbtt_FreeSDF(bitmap, NULL);  // note that FONScontext.scratch is reset for each glyph
#else
  stbtt_MakeGlyphBitmap(&font->font, output, outWidth, outHeight, outStride, scale, scale, glyph);
#endif
}

int fons__tt_getGlyphKernAdvance(FONSttFontImpl *font, int glyph1, int glyph2)
{
  return stbtt_GetGlyphKernAdvance(&font->font, glyph1, glyph2);
}

#endif

#ifndef FONS_SCRATCH_BUF_SIZE
#	define FONS_SCRATCH_BUF_SIZE 96000
#endif
#ifndef FONS_HASH_LUT_SIZE
#	define FONS_HASH_LUT_SIZE 256
#endif
#ifndef FONS_INIT_FONTS
#	define FONS_INIT_FONTS 4
#endif
#ifndef FONS_INIT_GLYPHS
#	define FONS_INIT_GLYPHS 256
#endif
#ifndef FONS_VERTEX_COUNT
#	define FONS_VERTEX_COUNT 1024
#endif
#ifndef FONS_MAX_STATES
#	define FONS_MAX_STATES 20
#endif
#ifndef FONS_MAX_FALLBACKS
#	define FONS_MAX_FALLBACKS 20
#endif
#ifndef FONS_DEFAULT_PX
#	define FONS_DEFAULT_PX 48
#endif

static unsigned int fons__hashint(unsigned int a)
{
  a += ~(a<<15);
  a ^=  (a>>10);
  a +=  (a<<3);
  a ^=  (a>>6);
  a += ~(a<<11);
  a ^=  (a>>16);
  return a;
}

static int fons__mini(int a, int b)
{
  return a < b ? a : b;
}

static int fons__maxi(int a, int b)
{
  return a > b ? a : b;
}

struct FONSglyph
{
  unsigned int codepoint;
  int font;
  int index;
  int next;
  float xadv;
  //short size, blur;
  short x0,y0,x1,y1;
  short xoff,yoff;
};
typedef struct FONSglyph FONSglyph;

struct FONSfont
{
  FONSttFontImpl font;
  char name[64];
  unsigned char* data;
  int dataSize;
  unsigned char freeData;
  float ascender;
  float descender;
  float lineh;
  FONSglyph* glyphs;
  int cglyphs;
  int nglyphs;
  int lut[FONS_HASH_LUT_SIZE];
  int fallbacks[FONS_MAX_FALLBACKS];
  int nfallbacks;
  int notDef;  // index in glyphs of notdef glyph
};
typedef struct FONSfont FONSfont;

struct FONSstate
{
  int font;
  int align;
  float size;
  unsigned int color;
  float blur;
  float spacing;
};
typedef struct FONSstate FONSstate;

struct FONSatlas
{
  int width, height;
  int cellw, cellh;
  int nextx, nexty;
};
typedef struct FONSatlas FONSatlas;

struct FONScontext
{
  FONSparams params;
  float itw,ith;
  FONStexel* texData;
  int dirtyRect[4];
  FONSfont** fonts;
  FONSatlas* atlas;
  int atlasFontPx;
  int cfonts;
  int nfonts;
  float verts[FONS_VERTEX_COUNT*2];
  float tcoords[FONS_VERTEX_COUNT*2];
  unsigned int colors[FONS_VERTEX_COUNT];
  int nverts;
  unsigned char* scratch;
  int nscratch;
  int fallbacks[FONS_MAX_FALLBACKS];
  int nfallbacks;
  FONSstate states[FONS_MAX_STATES];
  int nstates;
  void (*handleError)(void* uptr, int error, int val);
  void* errorUptr;
};

#ifdef STB_TRUETYPE_IMPLEMENTATION

static void* fons__tmpalloc(size_t size, void* up)
{
  unsigned char* ptr;
  FONScontext* stash = (FONScontext*)up;

  // 16-byte align the returned pointer
  size = (size + 0xf) & ~0xf;

  if (stash->nscratch+(int)size > FONS_SCRATCH_BUF_SIZE) {
    if (stash->handleError)
      stash->handleError(stash->errorUptr, FONS_SCRATCH_FULL, stash->nscratch+(int)size);
    return NULL;
  }
  ptr = stash->scratch + stash->nscratch;
  stash->nscratch += (int)size;
  return ptr;
}

static void fons__tmpfree(void* ptr, void* up)
{
  (void)ptr;
  (void)up;
  // empty
}

#endif // STB_TRUETYPE_IMPLEMENTATION

// Copyright (c) 2008-2010 Bjoern Hoehrmann <bjoern@hoehrmann.de>
// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.

#define FONS_UTF8_ACCEPT 0
#define FONS_UTF8_REJECT 12

static unsigned int fons__decutf8(unsigned int* state, unsigned int* codep, unsigned int byte)
{
  static const unsigned char utf8d[] = {
    // The first part of the table maps bytes to character classes that
    // to reduce the size of the transition table and create bitmasks.
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

    // The second part is a transition table that maps a combination
    // of a state of the automaton and a character class to a state.
    0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
    12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
    12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
    12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
    12,36,12,12,12,12,12,12,12,12,12,12,
    };

  unsigned int type = utf8d[byte];

    *codep = (*state != FONS_UTF8_ACCEPT) ?
    (byte & 0x3fu) | (*codep << 6) :
    (0xff >> type) & (byte);

  *state = utf8d[256 + *state + type];
  return *state;
}

static void fons__atlasReset(FONSatlas* atlas, int w, int h, int cellw, int cellh)
{
  atlas->width = w;
  atlas->height = h;
  atlas->cellw = cellw;
  atlas->cellh = cellh;
  atlas->nextx = 0;
  atlas->nexty = 0;
}

static int fons__atlasAddCell(FONSatlas* atlas, int* x, int* y)
{
  *x = atlas->nextx;
  *y = atlas->nexty;
  atlas->nextx += atlas->cellw;
  if(atlas->nextx + atlas->cellw > atlas->width) {
    atlas->nextx = 0;
    atlas->nexty += atlas->cellh;
  }
  return (atlas->nexty + atlas->cellh > atlas->height) ? 0 : 1;
}

static void fons__addWhiteRect(FONScontext* stash, int w, int h)
{
  int x, y, gx, gy;
  unsigned char* dst;
  if (fons__atlasAddCell(stash->atlas, &gx, &gy) == 0)
    return;

  // Rasterize
  dst = &stash->texData[gx + gy * stash->atlas->width];
  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++)
      dst[x] = 0xff;
    dst += stash->atlas->width;
  }

  stash->dirtyRect[0] = fons__mini(stash->dirtyRect[0], gx);
  stash->dirtyRect[1] = fons__mini(stash->dirtyRect[1], gy);
  stash->dirtyRect[2] = fons__maxi(stash->dirtyRect[2], gx+w);
  stash->dirtyRect[3] = fons__maxi(stash->dirtyRect[3], gy+h);
}

// to use font atlas, user must call fonsResetAtlas after this (not necessary if not using atlas, e.g. if
//  drawing text as paths)
FONScontext* fonsCreateInternal(FONSparams* params)
{
  FONScontext* stash = NULL;

  // Allocate memory for the font stash.
  stash = (FONScontext*)malloc(sizeof(FONScontext));
  if (stash == NULL) goto error;
  memset(stash, 0, sizeof(FONScontext));

  stash->params = *params;

  // Allocate scratch buffer.
  stash->scratch = (unsigned char*)malloc(FONS_SCRATCH_BUF_SIZE);
  if (stash->scratch == NULL) goto error;

  // Initialize implementation library
  if (!fons__tt_init(stash)) goto error;

  stash->atlas = (FONSatlas*)malloc(sizeof(FONSatlas));
  if (stash->atlas == NULL) goto error;
  memset(stash->atlas, 0, sizeof(FONSatlas));

  // Allocate space for fonts.
  stash->fonts = (FONSfont**)malloc(sizeof(FONSfont*) * FONS_INIT_FONTS);
  if (stash->fonts == NULL) goto error;
  memset(stash->fonts, 0, sizeof(FONSfont*) * FONS_INIT_FONTS);
  stash->cfonts = FONS_INIT_FONTS;
  stash->nfonts = 0;

  fonsPushState(stash);
  fonsClearState(stash);

  return stash;

error:
  fonsDeleteInternal(stash);
  return NULL;
}

static FONSstate* fons__getState(FONScontext* stash)
{
  return &stash->states[stash->nstates-1];
}

int fonsAddFallbackFont(FONScontext* stash, int base, int fallback)
{
  if (base >= 0) {
    FONSfont* baseFont = stash->fonts[base];
    if (baseFont->nfallbacks >= FONS_MAX_FALLBACKS)
      return 0;
    baseFont->fallbacks[baseFont->nfallbacks++] = fallback;
    return 1;
  }
  // global fallback
  if (stash->nfallbacks >= FONS_MAX_FALLBACKS)
    return 0;
  stash->fallbacks[stash->nfallbacks++] = fallback;
  return 1;
}

float fonsEmSizeToSize(FONScontext* stash, float emsize)
{
  FONSstate* state = fons__getState(stash);
  FONSfont* font;
  if(state->font < 0 || state->font >= stash->nfonts) return 0;
  font = stash->fonts[state->font];
  return fons__tt_getEmToPixelsScale(&font->font, emsize)/fons__tt_getPixelHeightScale(&font->font, 1.0f);
}

void fonsSetSize(FONScontext* stash, float size)
{
  fons__getState(stash)->size = size;
}

float fonsGetSize(FONScontext* stash)
{
  return fons__getState(stash)->size;
}

void fonsSetColor(FONScontext* stash, unsigned int color)
{
  fons__getState(stash)->color = color;
}

void fonsSetSpacing(FONScontext* stash, float spacing)
{
  fons__getState(stash)->spacing = spacing;
}

void fonsSetBlur(FONScontext* stash, float blur)
{
  fons__getState(stash)->blur = blur;
}

void fonsSetAlign(FONScontext* stash, int align)
{
  fons__getState(stash)->align = align;
}

void fonsPushState(FONScontext* stash)
{
  if (stash->nstates >= FONS_MAX_STATES) {
    if (stash->handleError)
      stash->handleError(stash->errorUptr, FONS_STATES_OVERFLOW, 0);
    return;
  }
  if (stash->nstates > 0)
    memcpy(&stash->states[stash->nstates], &stash->states[stash->nstates-1], sizeof(FONSstate));
  stash->nstates++;
}

void fonsPopState(FONScontext* stash)
{
  if (stash->nstates <= 1) {
    if (stash->handleError)
      stash->handleError(stash->errorUptr, FONS_STATES_UNDERFLOW, 0);
    return;
  }
  stash->nstates--;
}

void fonsClearState(FONScontext* stash)
{
  FONSstate* state = fons__getState(stash);
  state->size = 12.0f;
  state->color = 0xffffffff;
  state->font = 0;
  state->blur = 0;
  state->spacing = 0;
  state->align = FONS_ALIGN_LEFT | FONS_ALIGN_BASELINE;
}

static void fons__freeFont(FONSfont* font)
{
  if (font == NULL) return;
  if (font->glyphs) free(font->glyphs);
  if (font->freeData && font->data) free(font->data);
  free(font);
}

static int fons__allocFont(FONScontext* stash)
{
  FONSfont* font = NULL;
  if (stash->nfonts+1 > stash->cfonts) {
    stash->cfonts = stash->cfonts == 0 ? 8 : stash->cfonts * 2;
    stash->fonts = (FONSfont**)realloc(stash->fonts, sizeof(FONSfont*) * stash->cfonts);
    if (stash->fonts == NULL)
      return -1;
  }
  font = (FONSfont*)malloc(sizeof(FONSfont));
  if (font == NULL) goto error;
  memset(font, 0, sizeof(FONSfont));

  // now done in fons__loadFont
  //font->glyphs = (FONSglyph*)malloc(sizeof(FONSglyph) * FONS_INIT_GLYPHS);
  //if (font->glyphs == NULL) goto error;
  //font->cglyphs = FONS_INIT_GLYPHS;
  //font->nglyphs = 0;
  font->notDef = -1;

  stash->fonts[stash->nfonts++] = font;
  return stash->nfonts-1;

error:
  fons__freeFont(font);
  return FONS_INVALID;
}

int fonsAddFont(FONScontext* stash, const char* name, const char* path)
{
  char* path2 = malloc(strlen(path) + 1);
  return fonsAddFontMem(stash, name, strcpy(path2, path), 0, 1);
}

static unsigned char* fons__readFile(const char* path, int* sizeout)
{
  int dataSize = 0;
  size_t nread = 0;
  unsigned char* data = NULL;
  FILE* fp = fopen(path, "rb");
  if (fp == NULL) goto error;
  fseek(fp,0,SEEK_END);
  dataSize = (int)ftell(fp);
  fseek(fp,0,SEEK_SET);
  data = (unsigned char*)malloc(dataSize);
  if (data == NULL) goto error;
  nread = fread(data, 1, dataSize, fp);
  if (nread != dataSize) goto error;
  fclose(fp);
  *sizeout = dataSize;
  return data;

error:
  if (data) free(data);
  if (fp) fclose(fp);
  return NULL;
}

static int fons__loadFont(FONScontext* stash, int idx)
{
  int i, ascent, descent, fh, lineGap;
  FONSfont* font = stash->fonts[idx];
  // dataSize == 0 indicates data contains path to font file
  if (font->dataSize == 0) {
    unsigned char* fontdata = fons__readFile(font->data, &font->dataSize);
    if (font->freeData)
      free(font->data);
    font->data = fontdata;
    font->freeData = 1;
  }

  if (!fons__tt_loadFont(stash, &font->font, font->data, font->dataSize)) goto error;

  font->glyphs = (FONSglyph*)malloc(sizeof(FONSglyph) * FONS_INIT_GLYPHS);
  if (font->glyphs == NULL) goto error;
  font->cglyphs = FONS_INIT_GLYPHS;

  stash->nscratch = 0;
  for (i = 0; i < FONS_HASH_LUT_SIZE; ++i)
    font->lut[i] = -1;

  // Store normalized line height. The real line height is found by multiplying the lineh by font size.
  fons__tt_getFontVMetrics(&font->font, &ascent, &descent, &lineGap);
  ascent += lineGap;
  fh = ascent - descent;
  font->ascender = (float)ascent / (float)fh;
  font->descender = (float)descent / (float)fh;
  font->lineh = font->ascender - font->descender;
  return idx;

error:
  if (font->freeData) free(font->data);
  font->data = NULL;
  return FONS_INVALID;
}

int fonsAddFontMem(FONScontext* stash, const char* name, unsigned char* data, int dataSize, int freeData)
{
  FONSfont* font;
  int idx = fons__allocFont(stash);
  if (idx == FONS_INVALID)
    return FONS_INVALID;

  font = stash->fonts[idx];
  strncpy(font->name, name, sizeof(font->name));
  font->name[sizeof(font->name)-1] = '\0';
  font->dataSize = dataSize;
  font->data = data;
  font->freeData = (unsigned char)freeData;
  return (stash->params.flags & FONS_DELAY_LOAD) ? idx : fons__loadFont(stash, idx);
}

void fonsSetFont(FONScontext* stash, int font)
{
  // delayed loading
  if(stash->fonts[font]->data && !stash->fonts[font]->dataSize)
    fons__loadFont(stash, font);

  fons__getState(stash)->font = font;
}

int fonsGetFontByName(FONScontext* s, const char* name)
{
  int i;
  for (i = 0; i < s->nfonts; i++) {
    if (strcmp(s->fonts[i]->name, name) == 0)
      return i;
  }
  return FONS_INVALID;
}

void* fonsGetFontImpl(FONScontext* stash, int font)
{
  return font >= 0 && font < stash->nfonts ? &stash->fonts[font]->font.font : NULL;
}

static FONSglyph* fons__allocGlyph(FONSfont* font)
{
  if (font->nglyphs+1 > font->cglyphs) {
    font->cglyphs = font->cglyphs == 0 ? 8 : font->cglyphs * 2;
    font->glyphs = (FONSglyph*)realloc(font->glyphs, sizeof(FONSglyph) * font->cglyphs);
    if (font->glyphs == NULL) return NULL;
  }
  font->nglyphs++;
  return &font->glyphs[font->nglyphs-1];
}

static FONSglyph* fons__getGlyph(FONScontext* stash, int fontid, unsigned int codepoint, int bitmapOption)
{
  int i, g, added, advance, lsb, x0, y0, x1, y1, gw, gh, gx, gy;
  float scale;
  FONSfont* font = stash->fonts[fontid];
  FONSglyph* glyph = NULL;
  unsigned int h;
  float size = stash->atlasFontPx > 0 ? stash->atlasFontPx : FONS_DEFAULT_PX;  //isize/10.0f;
  int pad = 2;
  int cellw = stash->atlas->cellw;
  int cellh = stash->atlas->cellh;
  int renderFontId = fontid;

  // Reset allocator.
  stash->nscratch = 0;

  // Find code point and size.
  h = fons__hashint(codepoint) & (FONS_HASH_LUT_SIZE-1);
  for (i = font->lut[h]; i != -1; i = font->glyphs[i].next) {
    if (font->glyphs[i].codepoint == codepoint) {
      glyph = &font->glyphs[i];
      if (glyph->index < 0)  // && i != font->notDef)
        glyph = &font->glyphs[font->notDef];
      if (bitmapOption == FONS_GLYPH_BITMAP_OPTIONAL || (glyph->x0 >= 0 && glyph->y0 >= 0))
        return glyph;
      // At this point, glyph exists but the bitmap data is not yet created.
      break;
    }
  }

  if (!glyph) {
    g = fons__tt_getGlyphIndex(&font->font, codepoint);
    // font specific fallbacks
    for (i = 0; g == 0 && i < font->nfallbacks; ++i) {
      renderFontId = font->fallbacks[i];
      // delayed loading
      if (stash->fonts[renderFontId]->data && !stash->fonts[renderFontId]->dataSize)
        fons__loadFont(stash, renderFontId);
      g = fons__tt_getGlyphIndex(&stash->fonts[renderFontId]->font, codepoint);
    }
    // global fallbacks
    for (i = 0; g == 0 && i < stash->nfallbacks; ++i) {
      renderFontId = stash->fallbacks[i];
      if (stash->fonts[renderFontId]->data && !stash->fonts[renderFontId]->dataSize)
        fons__loadFont(stash, renderFontId);
      g = fons__tt_getGlyphIndex(&stash->fonts[renderFontId]->font, codepoint);
    }
  } else {
    g = glyph->index;
    renderFontId = glyph->font;
  }

  if (g != 0 && renderFontId != fontid) {
    // glyph was found in fallback font
    FONSglyph* fallbackGlyph = fons__getGlyph(stash, renderFontId, codepoint, bitmapOption);
    if(!fallbackGlyph)
      return NULL;  // this can happen if atlas is full
    int next = glyph ? glyph->next : font->lut[h];
    if (!glyph) {
      glyph = fons__allocGlyph(font);
      font->lut[h] = font->nglyphs-1;
    }
    *glyph = *fallbackGlyph;  //memcpy(glyph, fallbackGlyph, sizeof(FONSglyph));
    glyph->next = next;  // restore
    glyph->codepoint = codepoint;  // in case we used replacement char glyph
    return glyph;
  }
  // at this point, g == 0 means glyph was not found anywhere

  // first, get bitmap info so we can check for empty glyph 0
  scale = fons__tt_getPixelHeightScale(&font->font, size);
  fons__tt_buildGlyphBitmap(&font->font, g, size, scale, &advance, &lsb, &x0, &y0, &x1, &y1);
  gw = x1-x0 + pad*2;
  gh = y1-y0 + pad*2;

  // 0xFFFF is an invalid codepoint, only used to create notdef glyph
  // try "replacement character" glyph if glyph 0 is empty
  if (codepoint == 0xFFFF && font->notDef < 0 && (x0 == x1 || y0 == y1))
    return fons__getGlyph(stash, fontid, 0xFFFD, bitmapOption);

  // we use glyph.index = -1 to have glyph reference notdef glyph so we only need a single notdef bitmap
  if (g == 0 && codepoint != 0xFFFF && codepoint != 0xFFFD) {
    if (font->notDef < 0) {  // notdef not created yet
      fons__getGlyph(stash, fontid, 0xFFFF, bitmapOption);
      font->notDef = font->nglyphs-1;
    }
    g = -1;  // this glyph will reference notDef
  }

  // Determines the spot to draw glyph in the atlas.
  if (bitmapOption == FONS_GLYPH_BITMAP_REQUIRED) {
    added = fons__atlasAddCell(stash->atlas, &gx, &gy);
    if (added == 0 && stash->handleError != NULL) {
      // Atlas is full, let the user resize the atlas (or not), and try again.
      stash->handleError(stash->errorUptr, FONS_ATLAS_FULL, 0);
      added = fons__atlasAddCell(stash->atlas, &gx, &gy);
    }
    if (added == 0) return NULL;
  } else {
    // Negative coordinate indicates there is no bitmap data created.
    gx = -1;
    gy = -1;
  }

  // Init glyph.
  if (glyph == NULL) {
    glyph = fons__allocGlyph(font);
    glyph->codepoint = codepoint;
    glyph->font = fontid;
    glyph->index = g;
    // Insert char to hash lookup.
    glyph->next = font->lut[h];
    font->lut[h] = font->nglyphs-1;
  }
  glyph->xadv = advance;  // note xadv is in unscaled units
  glyph->x0 = (short)gx;
  glyph->y0 = (short)gy;
  glyph->x1 = (short)(glyph->x0+gw);
  glyph->y1 = (short)(glyph->y0+gh);
  glyph->xoff = (short)(x0 - pad);
  glyph->yoff = (short)(y0 - pad);

  if (g < 0)
    return &font->glyphs[font->notDef];  // created new LUT entry referencing notDef; now return notDef
  if (bitmapOption == FONS_GLYPH_BITMAP_OPTIONAL)
    return glyph;

  // Rasterize if not empty glyph; we assume texData for the cell has been cleared to all zeros
  if (x1 > x0 && y1 > y0) {
    FONStexel* dst = &stash->texData[(glyph->x0+pad) + (glyph->y0+pad) * stash->atlas->width];
    fons__tt_renderGlyphBitmap(&font->font, dst, cellw - pad, cellh - pad, stash->atlas->width, scale, g);
  }

  stash->dirtyRect[0] = fons__mini(stash->dirtyRect[0], glyph->x0);
  stash->dirtyRect[1] = fons__mini(stash->dirtyRect[1], glyph->y0);
  stash->dirtyRect[2] = fons__maxi(stash->dirtyRect[2], glyph->x0 + cellw);
  stash->dirtyRect[3] = fons__maxi(stash->dirtyRect[3], glyph->y0 + cellh);

  return glyph;
}

// snapping advance to integers doesn't make sense w/ summed text approach
#define FONS_ROUNDADV(x) (x)  //((int)(x + 0.5f))

static float fons__getKern(FONSfont* font, int prevGlyphIndex, FONSglyph* glyph, float scale, float spacing)
{
  if (prevGlyphIndex == -1)
    return 0;
  float adv = font ? fons__tt_getGlyphKernAdvance(&font->font, prevGlyphIndex, glyph->index) * scale : 0;
  return FONS_ROUNDADV(adv + spacing);
}

static void fons__getQuad(FONScontext* stash, FONSglyph* glyph, float size, float x, float y, FONSquad* q)
{
  // Each glyph has 2px border to allow good interpolation,
  // one pixel to prevent leaking, and one to allow good interpolation for rendering.
  // Inset the texture region by one pixel for correct interpolation.
  float xoff = (short)(glyph->xoff+1);
  float yoff = (short)(glyph->yoff+1);
  float x0 = (float)(glyph->x0+1);
  float y0 = (float)(glyph->y0+1);
  float x1 = (float)(glyph->x1-1);
  float y1 = (float)(glyph->y1-1);
  float sgny = stash->params.flags & FONS_ZERO_TOPLEFT ? 1.0f : -1.0f;
  float scale = size/(stash->atlasFontPx > 0 ? stash->atlasFontPx : FONS_DEFAULT_PX);

  q->x0 = x + scale*xoff;
  q->y0 = y + scale*sgny*yoff;
  q->x1 = q->x0 + scale*(x1 - x0);
  q->y1 = q->y0 + scale*sgny*(y1 - y0);

  q->s0 = x0 * stash->itw;
  q->t0 = y0 * stash->ith;
  q->s1 = x1 * stash->itw;
  q->t1 = y1 * stash->ith;
}

static void fons__flush(FONScontext* stash)
{
  // Flush texture
  if (stash->dirtyRect[0] < stash->dirtyRect[2] && stash->dirtyRect[1] < stash->dirtyRect[3]) {
    if (stash->params.renderUpdate != NULL)
      stash->params.renderUpdate(stash->params.userPtr, stash->dirtyRect, stash->texData);
    // Reset dirty rect
    stash->dirtyRect[0] = stash->atlas->width;
    stash->dirtyRect[1] = stash->atlas->height;
    stash->dirtyRect[2] = 0;
    stash->dirtyRect[3] = 0;
  }

  // Flush triangles
  if (stash->nverts > 0) {
    if (stash->params.renderDraw != NULL)
      stash->params.renderDraw(stash->params.userPtr, stash->verts, stash->tcoords, stash->colors, stash->nverts);
    stash->nverts = 0;
  }
}

static __inline void fons__vertex(FONScontext* stash, float x, float y, float s, float t, unsigned int c)
{
  stash->verts[stash->nverts*2+0] = x;
  stash->verts[stash->nverts*2+1] = y;
  stash->tcoords[stash->nverts*2+0] = s;
  stash->tcoords[stash->nverts*2+1] = t;
  stash->colors[stash->nverts] = c;
  stash->nverts++;
}

static float fons__getVertAlign(FONScontext* stash, FONSfont* font, int align, short isize)
{
  float sgn = stash->params.flags & FONS_ZERO_TOPLEFT ? 1.0f : -1.0f;
  if (align & FONS_ALIGN_TOP)
    return sgn * font->ascender * (float)isize/10.0f;
  if (align & FONS_ALIGN_MIDDLE)
    return sgn * (font->ascender + font->descender) / 2.0f * (float)isize/10.0f;
  if (align & FONS_ALIGN_BOTTOM)
    return sgn * font->descender * (float)isize/10.0f;
  return 0.0f;  // align & FONS_ALIGN_BASELINE (default)
}

int fonsTextIterInit(FONScontext* stash, FONStextIter* iter,
           float x, float y, const char* str, const char* end, int bitmapOption)
{
  FONSstate* state = fons__getState(stash);
  float width;

  memset(iter, 0, sizeof(*iter));

  if (stash == NULL) return 0;
  if (state->font < 0 || state->font >= stash->nfonts) return 0;
  iter->font = state->font;  //stash->fonts[state->font];
  if (stash->fonts[state->font]->data == NULL) return 0;
  if (stash->atlasFontPx <= 0 && bitmapOption != FONS_GLYPH_BITMAP_OPTIONAL) return 0;

  iter->isize = (short)(state->size*10.0f);
  iter->iblur = (short)state->blur;

  // Align horizontally
  if (state->align & FONS_ALIGN_LEFT) {
    // empty
  } else if (state->align & FONS_ALIGN_RIGHT) {
    width = fonsTextBounds(stash, x,y, str, end, NULL);
    x -= width;
  } else if (state->align & FONS_ALIGN_CENTER) {
    width = fonsTextBounds(stash, x,y, str, end, NULL);
    x -= width * 0.5f;
  }
  // Align vertically.
  y += fons__getVertAlign(stash, stash->fonts[iter->font], state->align, iter->isize);

  if (end == NULL)
    end = str + strlen(str);

  iter->x = iter->nextx = x;
  iter->y = iter->nexty = y;
  iter->spacing = state->spacing;
  iter->str = str;
  iter->next = str;
  iter->end = end;
  iter->codepoint = 0;
  iter->prevGlyphIndex = -1;
  iter->prevGlyphFont = -1;
  iter->bitmapOption = bitmapOption;

  return 1;
}

int fonsTextIterNext(FONScontext* stash, FONStextIter* iter, FONSquad* quad)
{
  FONSglyph* glyph = NULL;
  const char* str = iter->next;
  iter->str = iter->next;

  if (str == iter->end)
    return 0;

  for (; str != iter->end; str++) {
    if (fons__decutf8(&iter->utf8state, &iter->codepoint, *(const unsigned char*)str))
      continue;
    str++;
    // Get glyph and quad
    glyph = fons__getGlyph(stash, iter->font, iter->codepoint, iter->bitmapOption);
    // If the iterator was initialized with FONS_GLYPH_BITMAP_OPTIONAL, then the UV coordinates of the quad
    //  will be invalid.
    if (glyph != NULL) {
      FONSfont* font = stash->fonts[glyph->font];
      FONSfont* kernFont = iter->prevGlyphFont == glyph->font ? font : NULL;
      float scale = fons__tt_getPixelHeightScale(&font->font, (float)iter->isize/10.0f);
      iter->nextx += fons__getKern(kernFont, iter->prevGlyphIndex, glyph, scale, iter->spacing);
      iter->x = iter->nextx;
      iter->y = iter->nexty;
      if (quad)
        fons__getQuad(stash, glyph, iter->isize/10.0f, iter->x, iter->y, quad);
      iter->nextx += FONS_ROUNDADV(scale*glyph->xadv);
    }
    iter->prevGlyphIndex = glyph != NULL ? glyph->index : -1;
    iter->prevGlyphFont = glyph != NULL ? glyph->font : -1;
    break;
  }
  iter->next = str;

  return 1;
}

float fonsDrawText(FONScontext* stash, float x, float y, const char* str, const char* end)
{
  FONSstate* state = fons__getState(stash);
  FONStextIter iter;
  FONSquad q;

  fonsTextIterInit(stash, &iter, x, y, str, end, FONS_GLYPH_BITMAP_REQUIRED);
  while (fonsTextIterNext(stash, &iter, &q)) {
    if (stash->nverts+6 > FONS_VERTEX_COUNT)
      fons__flush(stash);

    fons__vertex(stash, q.x0, q.y0, q.s0, q.t0, state->color);
    fons__vertex(stash, q.x1, q.y1, q.s1, q.t1, state->color);
    fons__vertex(stash, q.x1, q.y0, q.s1, q.t0, state->color);

    fons__vertex(stash, q.x0, q.y0, q.s0, q.t0, state->color);
    fons__vertex(stash, q.x0, q.y1, q.s0, q.t1, state->color);
    fons__vertex(stash, q.x1, q.y1, q.s1, q.t1, state->color);
  }
  fons__flush(stash);
  return iter.nextx;
}

void fonsDrawDebug(FONScontext* stash, float x, float y)
{
  int w = stash->atlas->width;
  int h = stash->atlas->height;
  float u = w == 0 ? 0 : (1.0f / w);
  float v = h == 0 ? 0 : (1.0f / h);

  if (stash->nverts+6+6 > FONS_VERTEX_COUNT)
    fons__flush(stash);

  // Draw background
  fons__vertex(stash, x+0, y+0, u, v, 0x0fffffff);
  fons__vertex(stash, x+w, y+h, u, v, 0x0fffffff);
  fons__vertex(stash, x+w, y+0, u, v, 0x0fffffff);

  fons__vertex(stash, x+0, y+0, u, v, 0x0fffffff);
  fons__vertex(stash, x+0, y+h, u, v, 0x0fffffff);
  fons__vertex(stash, x+w, y+h, u, v, 0x0fffffff);

  // Draw texture
  fons__vertex(stash, x+0, y+0, 0, 0, 0xffffffff);
  fons__vertex(stash, x+w, y+h, 1, 1, 0xffffffff);
  fons__vertex(stash, x+w, y+0, 1, 0, 0xffffffff);

  fons__vertex(stash, x+0, y+0, 0, 0, 0xffffffff);
  fons__vertex(stash, x+0, y+h, 0, 1, 0xffffffff);
  fons__vertex(stash, x+w, y+h, 1, 1, 0xffffffff);

  fons__flush(stash);
}

float fonsTextBounds(FONScontext* stash, float x, float y, const char* str, const char* end, float* bounds)
{
  FONSstate* state = fons__getState(stash);
  int align = state->align;
  float advance, minx = x, miny = y, maxx = x, maxy = y;
  FONStextIter iter;
  FONSquad q;

  state->align |= FONS_ALIGN_LEFT;  // prevent infinite recursion (fonsTextIterInit calling fonsTextBounds)
  fonsTextIterInit(stash, &iter, x, y, str, end, FONS_GLYPH_BITMAP_OPTIONAL);
  while (fonsTextIterNext(stash, &iter, bounds ? &q : NULL)) {
    if (!bounds) continue;
    if (q.x0 < minx) minx = q.x0;
    if (q.x1 > maxx) maxx = q.x1;
    if (stash->params.flags & FONS_ZERO_TOPLEFT) {
      if (q.y0 < miny) miny = q.y0;
      if (q.y1 > maxy) maxy = q.y1;
    } else {
      if (q.y1 < miny) miny = q.y1;
      if (q.y0 > maxy) maxy = q.y0;
    }
  }
  advance = iter.nextx - x;  //advance = x - startx;

  state->align = align;  // restore
  // Align horizontally
  if (bounds) {
    if (state->align & FONS_ALIGN_LEFT) {
      // empty
    } else if (state->align & FONS_ALIGN_RIGHT) {
      minx -= advance;
      maxx -= advance;
    } else if (state->align & FONS_ALIGN_CENTER) {
      minx -= advance * 0.5f;
      maxx -= advance * 0.5f;
    }
    bounds[0] = minx;
    bounds[1] = miny;
    bounds[2] = maxx;
    bounds[3] = maxy;
  }

  return advance;
}

void fonsVertMetrics(FONScontext* stash, float* ascender, float* descender, float* lineh)
{
  FONSfont* font;
  FONSstate* state = fons__getState(stash);
  short isize;

  if (stash == NULL) return;
  if (state->font < 0 || state->font >= stash->nfonts) return;
  font = stash->fonts[state->font];
  isize = (short)(state->size*10.0f);
  if (font->data == NULL) return;

  if (ascender)
    *ascender = font->ascender*isize/10.0f;
  if (descender)
    *descender = font->descender*isize/10.0f;
  if (lineh)
    *lineh = font->lineh*isize/10.0f;
}

void fonsLineBounds(FONScontext* stash, float y, float* miny, float* maxy)
{
  FONSfont* font;
  FONSstate* state = fons__getState(stash);
  short isize;

  if (stash == NULL) return;
  if (state->font < 0 || state->font >= stash->nfonts) return;
  font = stash->fonts[state->font];
  isize = (short)(state->size*10.0f);
  if (font->data == NULL) return;

  y += fons__getVertAlign(stash, font, state->align, isize);

  if (stash->params.flags & FONS_ZERO_TOPLEFT) {
    *miny = y - font->ascender * (float)isize/10.0f;
    *maxy = *miny + font->lineh*isize/10.0f;
  } else {
    *maxy = y + font->descender * (float)isize/10.0f;
    *miny = *maxy - font->lineh*isize/10.0f;
  }
}

const void* fonsGetTextureData(FONScontext* stash, int* width, int* height)
{
  if (width != NULL)
    *width = stash->atlas->width;
  if (height != NULL)
    *height = stash->atlas->height;
  return stash->texData;
}

int fonsValidateTexture(FONScontext* stash, int* dirty)
{
  if (stash->dirtyRect[0] < stash->dirtyRect[2] && stash->dirtyRect[1] < stash->dirtyRect[3]) {
    dirty[0] = stash->dirtyRect[0];
    dirty[1] = stash->dirtyRect[1];
    dirty[2] = stash->dirtyRect[2];
    dirty[3] = stash->dirtyRect[3];
    // Reset dirty rect
    stash->dirtyRect[0] = stash->atlas->width;
    stash->dirtyRect[1] = stash->atlas->height;
    stash->dirtyRect[2] = 0;
    stash->dirtyRect[3] = 0;
    return 1;
  }
  return 0;
}

void fonsDeleteInternal(FONScontext* stash)
{
  int i;
  if (stash == NULL) return;

  if (stash->params.renderDelete)
    stash->params.renderDelete(stash->params.userPtr);

  for (i = 0; i < stash->nfonts; ++i)
    fons__freeFont(stash->fonts[i]);

  if (stash->atlas) free(stash->atlas);  //fons__deleteAtlas(stash->atlas);
  if (stash->fonts) free(stash->fonts);
  if (stash->texData) free(stash->texData);
  if (stash->scratch) free(stash->scratch);
  fons__tt_done(stash);
  free(stash);
}

void fonsSetErrorCallback(FONScontext* stash, void (*callback)(void* uptr, int error, int val), void* uptr)
{
  if (stash == NULL) return;
  stash->handleError = callback;
  stash->errorUptr = uptr;
}

void fonsGetAtlasSize(FONScontext* stash, int* width, int* height, int* atlasFontPx)
{
  if (stash == NULL) return;
  if (width) *width = stash->atlas->width;
  if (height) *height = stash->atlas->height;
  if (atlasFontPx) *atlasFontPx = stash->atlasFontPx;
}

int fonsExpandAtlas(FONScontext* stash, int width, int height)
{
  int i;
  FONStexel* data = NULL;
  if (stash == NULL) return 0;

  width = fons__maxi(width, stash->atlas->width);
  height = fons__maxi(height, stash->atlas->height);

  if (width == stash->atlas->width && height == stash->atlas->height)
    return 1;

  // Flush pending glyphs.
  fons__flush(stash);

  // Create new texture
  if (stash->params.renderResize != NULL) {
    if (stash->params.renderResize(stash->params.userPtr, width, height) == 0)
      return 0;
  }
  // Copy old texture data over.
  data = (FONStexel*)malloc(width * height * sizeof(FONStexel));
  if (data == NULL)
    return 0;
  for (i = 0; i < stash->atlas->height; i++) {
    FONStexel* dst = &data[i*width];
    FONStexel* src = &stash->texData[i*stash->atlas->width];
    memcpy(dst, src, stash->atlas->width * sizeof(FONStexel));
    if (width > stash->atlas->width)
      memset(dst+stash->atlas->width, 0, (width - stash->atlas->width) * sizeof(FONStexel));
  }
  if (height > stash->atlas->height)
    memset(&data[stash->atlas->height * width], 0, (height - stash->atlas->height) * width);

  free(stash->texData);
  stash->texData = data;

  // Increase atlas size
  stash->atlas->width = width;
  stash->atlas->height = height;
  stash->itw = 1.0f/stash->atlas->width;
  stash->ith = 1.0f/stash->atlas->height;

  stash->dirtyRect[0] = 0;
  stash->dirtyRect[1] = 0;
  stash->dirtyRect[2] = stash->atlas->width;
  stash->dirtyRect[3] = stash->atlas->height;

  return 1;
}

int fonsResetAtlas(FONScontext* stash, int width, int height, int cellw, int cellh, int atlasFontPx)
{
  int i, j;
  if (stash == NULL) return 0;

  // Flush pending glyphs.
  fons__flush(stash);

  // Create new texture
  if (!stash->texData && stash->params.renderCreate) {
    if (stash->params.renderCreate(stash->params.userPtr, width, height) == 0)
      return 0;
  }
  else if (stash->texData && stash->params.renderResize) {
    if (stash->params.renderResize(stash->params.userPtr, width, height) == 0)
      return 0;
  }

  // Reset atlas
  fons__atlasReset(stash->atlas, width, height, cellw, cellh);

  // Clear texture data.
  stash->texData = (FONStexel*)realloc(stash->texData, width * height * sizeof(FONStexel));
  if (stash->texData == NULL) return 0;
  memset(stash->texData, 0, width * height * sizeof(FONStexel));

  // Reset dirty rect
  stash->dirtyRect[0] = width;
  stash->dirtyRect[1] = height;
  stash->dirtyRect[2] = 0;
  stash->dirtyRect[3] = 0;

  // Reset cached glyphs
  for (i = 0; i < stash->nfonts; i++) {
    FONSfont* font = stash->fonts[i];
    font->nglyphs = 0;
    font->notDef = -1;
    for (j = 0; j < FONS_HASH_LUT_SIZE; j++)
      font->lut[j] = -1;
  }

  stash->atlas->width = width;
  stash->atlas->height = height;
  stash->itw = 1.0f/stash->atlas->width;
  stash->ith = 1.0f/stash->atlas->height;
  stash->atlasFontPx = atlasFontPx;

  // Add white rect at 0,0 for debug drawing.
  //fons__addWhiteRect(stash, 2,2);

  return 1;
}

#endif
