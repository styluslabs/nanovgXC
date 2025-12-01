//
// Copyright (c) 2020 Stylus Labs - see LICENSE.txt
//   based on nanovg:
// Copyright (c) 2013 Mikko Mononen memon@inside.org
//
#ifndef FONS_H
#define FONS_H

#ifdef __cplusplus
extern "C" {
#endif

#define FONS_INVALID -1

enum FONSflags {
  FONS_ZERO_TOPLEFT = 1<<0,
  FONS_ZERO_BOTTOMLEFT = 1<<1,
  FONS_DELAY_LOAD = 1<<2,
  FONS_SUMMED = 1<<3,
  FONS_SDF = 1<<4,
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

enum FONSgetGlyphFlags {
  FONS_GLYPH_BITMAP_OPTIONAL = 0,
  FONS_GLYPH_BITMAP_REQUIRED = 1<<1,
};

enum FONSerrorCode {
  // Font atlas is full.
  FONS_ATLAS_FULL = 1,
  // Scratch memory used to render glyphs is full, requested size reported in 'val', you may need to bump up FONS_SCRATCH_BUF_SIZE.
  FONS_SCRATCH_FULL = 2,
};

struct FONSparams {
  unsigned int flags;
  int sdfPadding;
  float sdfPixelDist;
  // since only a single atlas is supported, it may be necessary to split into multiple fixed-size textures
  //  for GPU; atlasBlockHeight should be set to the height of these textures so that no glyphs will be
  //  split between two textures
  int atlasBlockHeight;
  // codepoint to be used for missing glyphs
  unsigned int notDefCodePt;

  void* userPtr;
  void (*userSDFRender)(void* uptr, void* fontimpl, unsigned char* output,
      int outWidth, int outHeight, int outStride, float scale, int padding, int glyph);
  void (*userDelete)(void* uptr);
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
  float size, blur;
  int font;
  int prevGlyphFont;
  int prevGlyphIndex;
  const char* str;
  const char* next;
  const char* end;
  unsigned int utf8state;
  int bitmapOption;
};
typedef struct FONStextIter FONStextIter;

struct FONStextRow {
  const char* start;	// Pointer to the input text where the row starts.
  const char* end;	// Pointer to the input text where the row ends (one past the last character).
  const char* next;	// Pointer to the beginning of the next row.
  float width;		// Logical width of the row.
  float minx, maxx;	// Actual bounds of the row. Logical with and bounds can differ because of kerning and some parts over extending.
  float miny, maxy;
};
typedef struct FONStextRow FONStextRow;

typedef struct FONScontext FONScontext;

struct FONSstate
{
  FONScontext* context;
  int font;
  int align;
  float size;
  float blur;
  float spacing;
};
typedef struct FONSstate FONSstate;

// Constructor and destructor.
FONScontext* fonsCreateInternal(FONSparams* params);
void fonsDeleteInternal(FONScontext* s);
FONSparams* fonsInternalParams(FONScontext* ctx);

void fonsSetErrorCallback(FONScontext* s, void (*callback)(void* uptr, int error, int val), void* uptr);
// Returns current atlas size.
void fonsGetAtlasSize(FONScontext* s, int* width, int* height, int* atlasFontPx);
// Expands the atlas size.
int fonsExpandAtlas(FONScontext* s, int width, int height);
// Resets the whole stash.
int fonsResetAtlas(FONScontext* stash, int width, int height, int atlasFontPx);

// Add fonts
int fonsAddFont(FONScontext* s, const char* name, const char* path);
int fonsAddFontMem(FONScontext* s, const char* name, unsigned char* data, int ndata, int freeData);
int fonsGetFontByName(FONScontext* s, const char* name);
int fonsAddFallbackFont(FONScontext* stash, int base, int fallback);
void* fonsGetFontImpl(FONScontext* stash, int font);

// State handling
void fonsInitState(FONScontext* stash, FONSstate* state);

// State setting
// internally, we work in pixel sizes (based on total glyph height: ascent - descent) but font sizes are
//  usually specified in em units (roughly height of 'M'); the ratio between these varies between fonts
// Because of how the summed text works, we have to use the same pixel size for all text in a given draw call
//  even if these corresponds to slightly different em sizes for fallback glyphs
void fonsSetSize(FONSstate* s, float size);
void fonsSetSpacing(FONSstate* s, float spacing);
void fonsSetBlur(FONSstate* s, float blur);
void fonsSetAlign(FONSstate* s, int align);
int fonsSetFont(FONSstate* s, int font);  // return -1 if font is missing or corrupt
// get the font height (size used by fontstash) for a given em size (the standard "font size")
float fonsEmSizeToSize(FONSstate* s, float emsize);
// get the current font height
float fonsGetSize(FONSstate* s);

// Measure text
float fonsTextBounds(FONSstate* state, float x, float y, const char* string, const char* end, float* bounds);
void fonsLineBounds(FONSstate* state, float y, float* miny, float* maxy);
void fonsVertMetrics(FONSstate* state, float* ascender, float* descender, float* lineh);

// Text iterator
int fonsTextIterInit(FONSstate* state, FONStextIter* iter, float x, float y, const char* str, const char* end, int bitmapOption);
int fonsTextIterNext(FONSstate* state, FONStextIter* iter, struct FONSquad* quad);

// Break text into upto maxLines lines of width breakRowWidth; if breakRowWidth < 0, break at -breakRowWidth characters instead
int fonsBreakLines(FONSstate* state, const char* string, const char* end, float breakRowWidth, FONStextRow* rows, int maxRows);

// Pull texture changes
const void* fonsGetTextureData(FONScontext* stash, int* width, int* height);
int fonsValidateTexture(FONScontext* s, int* dirty);

#ifdef __cplusplus
}
#endif

#endif // FONTSTASH_H


#ifdef FONTSTASH_IMPLEMENTATION

#include <stdio.h>  // font file loading

#define FONS_NOTUSED(v)  (void)sizeof(v)

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

// basic idea of "summed" rendering is to add up values from glyph coverage bitmap to create a cumulative
//  coverage float32 texture s.t. the coverage inside any rectangle can be calculated by sampling at just
//  the four corners (using linear interpolation); this seems hacky, but results looks pretty good
//  with arbitrary subpixel positioning up to at least 50% of the atlas font size ... given that this is
//  16x more data than a usual atlas, probably not that impressive
// Larger size text can be drawn directly as paths (seems like the reasonable thing to do even with usual
//  font atlas approach), and of course if atlas font size is too big, numerical issues will appear!

typedef float FONStexelF;
typedef unsigned char FONStexelU8;

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
//#define STBTT_STATIC
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
    FONStexelU8 *output, int outWidth, int outHeight, int outStride, float scale, int glyph)
{
  stbtt_MakeGlyphBitmap(&font->font, output, outWidth, outHeight, outStride, scale, scale, glyph);
}

void fons__tt_renderGlyphBitmapSummed(FONSttFontImpl *font,
    FONStexelF *output, int outWidth, int outHeight, int outStride, float scale, int glyph)
{
  int x, y;
  unsigned char* bitmap = (unsigned char*)malloc(outWidth*outHeight);
  stbtt_MakeGlyphBitmap(&font->font, bitmap, outWidth, outHeight, outWidth, scale, scale, glyph);
  for(y = 0; y < outHeight; ++y) {
    for(x = 0; x < outWidth; ++x) {
      FONStexelF s10 = y > 0 ? output[(y-1)*outStride + x] : 0;
      FONStexelF s01 = x > 0 ? output[y*outStride + (x-1)] : 0;
      FONStexelF s00 = x > 0 && y > 0 ? output[(y-1)*outStride + (x-1)] : 0;
      FONStexelF t11 = bitmap[y*outWidth + x]; // /255.0f
      output[y*outStride + x] = t11 + s10 + (s01 - s00);
    }
  }
  free(bitmap);
}

void fons__tt_renderGlyphBitmapSDF(FONSttFontImpl *font, FONStexelU8 *output,
    int outWidth, int outHeight, int outStride, float scale, int padding, float pixel_dist, int glyph)
{
  int x, y, w, h;  //x0, y0 -- offset from GetGlyphSDF is just the value from GetGlyphBitmapBox
  // ..., padding=4, on_edge_value=127 (0-255), pixel_dist_scale=32 (1 pix = 32 distance units)
  unsigned char* bitmap = stbtt_GetGlyphSDF(
      &font->font, scale, glyph, padding, 127, pixel_dist, &w, &h, NULL, NULL);
  if (!bitmap) return;
  if (w < outWidth) outWidth = w;
  if (h < outHeight) outHeight = h;
  for (y = 0; y < outHeight; ++y) {
    for (x = 0; x < outWidth; ++x) {
      output[y*outStride + x] = bitmap[y*w + x];
    }
  }
  stbtt_FreeSDF(bitmap, NULL);  // note that FONScontext.scratch is reset for each glyph
}

int fons__tt_getGlyphKernAdvance(FONSttFontImpl *font, int glyph1, int glyph2)
{
  return stbtt_GetGlyphKernAdvance(&font->font, glyph1, glyph2);
}

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

static int fons__mini(int a, int b) { return a < b ? a : b; }
static int fons__maxi(int a, int b) { return a > b ? a : b; }
static float fons__minf(float a, float b) { return a < b ? a : b; }
static float fons__maxf(float a, float b) { return a > b ? a : b; }

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

struct FONSatlas
{
  int width, height;
  //int cellw, cellh;
  int rowh;
  int nextx, nexty;
  int nglyphs;
};
typedef struct FONSatlas FONSatlas;

struct FONScontext
{
  FONSparams params;
  float itw,ith;
  void* texData;
  int dirtyRect[4];
  FONSfont** fonts;
  FONSatlas* atlas;
  int atlasFontPx;
  int cfonts;
  int nfonts;
  unsigned char* scratch;
  int nscratch;
  int fallbacks[FONS_MAX_FALLBACKS];
  int nfallbacks;
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

static void fons__atlasReset(FONSatlas* atlas, int w, int h)  //, int cellw, int cellh)
{
  memset(atlas, 0, sizeof(FONSatlas));
  atlas->width = w;
  atlas->height = h;
}

static int fons__atlasAddCell(FONScontext* stash, int w, int h, int* x, int* y)
{
  FONSatlas* atlas = stash->atlas;
  int blockh = stash->params.atlasBlockHeight;
  if (atlas->nextx + w > atlas->width) {
    atlas->nextx = 0;
    atlas->nexty += atlas->rowh;
    atlas->rowh = 0;
  }
  if (blockh > 0 && (atlas->nexty + h)/blockh != atlas->nexty/blockh) {
    atlas->nextx = 0;
    atlas->nexty = (atlas->nexty/blockh + 1)*blockh;
    atlas->rowh = 0;
  }
  if (atlas->nexty + h > atlas->height)
    return 0;

  *x = atlas->nextx;
  *y = atlas->nexty;
  atlas->nextx += w;
  atlas->rowh = fons__maxi(atlas->rowh, h);
  ++atlas->nglyphs;
  return 1;
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

  //fonsPushState(stash);
  //fonsClearState(stash);

  return stash;

error:
  fonsDeleteInternal(stash);
  return NULL;
}

FONSparams* fonsInternalParams(FONScontext* ctx)
{
  return &ctx->params;
}

int fonsAddFallbackFont(FONScontext* stash, int base, int fallback)
{
  if (fallback < 0 || fallback >= stash->nfonts || base >= stash->nfonts) return 0;
  if (base >= 0) {
    FONSfont* baseFont = stash->fonts[base];
    if (baseFont->nfallbacks >= FONS_MAX_FALLBACKS)
      return 0;
    baseFont->fallbacks[baseFont->nfallbacks++] = fallback;
    return 1;
  }
  // base < 0 indicates global fallback
  if (stash->nfallbacks >= FONS_MAX_FALLBACKS)
    return 0;
  stash->fallbacks[stash->nfallbacks++] = fallback;
  return 1;
}

float fonsEmSizeToSize(FONSstate* state, float emsize)
{
  FONScontext* stash = state->context;
  FONSfont* font;
  if (stash == NULL) { return 0; }
  if (state->font < 0 || state->font >= stash->nfonts) { return 0; }
  font = stash->fonts[state->font];
  if (font->data == NULL) { return 0; }
  return fons__tt_getEmToPixelsScale(&font->font, emsize)/fons__tt_getPixelHeightScale(&font->font, 1.0f);
}

void fonsSetSize(FONSstate* state, float size) { state->size = size; }
float fonsGetSize(FONSstate* state) { return state->size; }
void fonsSetSpacing(FONSstate* state, float spacing) { state->spacing = spacing; }
void fonsSetBlur(FONSstate* state, float blur) { state->blur = blur; }
void fonsSetAlign(FONSstate* state, int align) { state->align = align; }
//void fonsSetColor(FONSstate* state, unsigned int color) { state->color = color; }

void fonsInitState(FONScontext* stash, FONSstate* state)
{
  state->context = stash;
  state->size = 12.0f;
  //state->color = 0xffffffff;
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
  font->notDef = -1;
  stash->fonts[stash->nfonts++] = font;
  return stash->nfonts-1;

error:
  fons__freeFont(font);
  return FONS_INVALID;
}

int fonsAddFont(FONScontext* stash, const char* name, const char* path)
{
#ifdef FONS_WPATH
  size_t len = (wcslen((const wchar_t*)path) + 1)*sizeof(wchar_t);
#else
  size_t len = strlen(path) + 1;
#endif
  unsigned char* path2 = (unsigned char*)malloc(len);
  memcpy(path2, path, len);
  return fonsAddFontMem(stash, name, path2, 0, 1);
}

static unsigned char* fons__readFile(const char* path, int* sizeout)
{
  int dataSize = 0;
  size_t nread = 0;
  unsigned char* data = NULL;
#ifdef FONS_WPATH
  FILE* fp = _wfopen((const wchar_t*)path, L"rb");
#else
  FILE* fp = fopen(path, "rb");
#endif
  if (fp == NULL) goto error;
  fseek(fp,0,SEEK_END);
  dataSize = (int)ftell(fp);
  fseek(fp,0,SEEK_SET);
  data = (unsigned char*)malloc(dataSize);
  if (data == NULL) goto error;
  nread = fread(data, 1, dataSize, fp);
  if ((int)nread != dataSize) goto error;
  fclose(fp);
  *sizeout = dataSize;
  return data;

error:
  if (data) free(data);
  if (fp) fclose(fp);
  return NULL;
}

static void fons__readFont(FONSfont* font)
{
  unsigned char* fontdata = fons__readFile((const char*)font->data, &font->dataSize);
  if (font->freeData)
    free(font->data);
  font->data = fontdata;
  font->freeData = 1;
}

static int fons__loadFont(FONScontext* stash, int idx)
{
  int i, ascent, descent, fh, lineGap;
  FONSfont* font = stash->fonts[idx];
  if (font->glyphs) {
    // font already loaded
    return idx;
  }

  if (!font->data || !fons__tt_loadFont(stash, &font->font, font->data, font->dataSize)) goto error;

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
  if (stash->params.flags & FONS_DELAY_LOAD) {
    return idx;
  } else {
    if (font->dataSize == 0) fons__readFont(font);
    return fons__loadFont(stash, idx);
  }
}

int fonsSetFont(FONSstate* state, int font)
{
  // delayed loading
  FONScontext* stash = state->context;
  if (stash == NULL) { return FONS_INVALID; }
  if (font < 0 || font >= stash->nfonts) { font = FONS_INVALID; }
  else if(stash->fonts[font]->data) {
    //if(stash->lockStash) stash->lockStash(stash->lockUptr, FONS_UNLOCK_READ | FONS_LOCK_WRITE);
    // dataSize == 0 indicates data contains path to font file
    if (stash->fonts[font]->dataSize == 0)
      fons__readFont(stash->fonts[font]);
    fons__loadFont(stash, font);
    //if(stash->lockStash) stash->lockStash(stash->lockUptr, FONS_UNLOCK_WRITE | FONS_LOCK_READ);
  }
  if(!stash->fonts[font]->data)
    font = FONS_INVALID;

  state->font = font;
  return font;
}

int fonsGetFontByName(FONScontext* stash, const char* name)
{
  int i;
  if (!stash || !name) return FONS_INVALID;
  for (i = 0; i < stash->nfonts; i++) {
    if (strcmp(stash->fonts[i]->name, name) == 0)
      break;
  }
  return i < stash->nfonts ? i : FONS_INVALID;
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

static FONSglyph* fons__getGlyph(FONScontext* stash, int fontid, unsigned int codepoint, int flags)
{
  int i, g, added, advance, lsb, x0, y0, x1, y1, gx, gy;
  float scale;
  FONSfont* font = stash->fonts[fontid];
  FONSglyph* glyph = NULL;
  unsigned int h;
  float size = stash->atlasFontPx > 0 ? stash->atlasFontPx : FONS_DEFAULT_PX;
  int pad = stash->params.flags & FONS_SDF ? stash->params.sdfPadding + 1 : 2;
  int cellw = stash->atlasFontPx, cellh = stash->atlasFontPx;  // default for summed text
  int renderFontId = fontid;
  unsigned int notdefcp = stash->params.notDefCodePt ? stash->params.notDefCodePt : 0xFFFD;
  // reset allocator - used for stbtt_GetGlyphShape (for text as paths), not just bitmap!
  stash->nscratch = 0;

  // Find code point and size.
  h = fons__hashint(codepoint) & (FONS_HASH_LUT_SIZE-1);
  for (i = font->lut[h]; i != -1; i = font->glyphs[i].next) {
    if (font->glyphs[i].codepoint == codepoint) {
      glyph = &font->glyphs[i];
      if (glyph->index < 0)  // && i != font->notDef)
        glyph = &font->glyphs[font->notDef];
      if (!(flags & FONS_GLYPH_BITMAP_REQUIRED) || (glyph->x0 >= 0 && glyph->y0 >= 0))
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
      if(stash->fonts[renderFontId]->data) {
        // delayed loading
        if (stash->fonts[renderFontId]->dataSize == 0)
          fons__readFont(stash->fonts[renderFontId]);
        fons__loadFont(stash, renderFontId);
        g = fons__tt_getGlyphIndex(&stash->fonts[renderFontId]->font, codepoint);
      }
    }
    // global fallbacks
    for (i = 0; g == 0 && i < stash->nfallbacks; ++i) {
      renderFontId = stash->fallbacks[i];
      if(stash->fonts[renderFontId]->data) {
        if (stash->fonts[renderFontId]->dataSize == 0)
          fons__readFont(stash->fonts[renderFontId]);
        fons__loadFont(stash, renderFontId);
        g = fons__tt_getGlyphIndex(&stash->fonts[renderFontId]->font, codepoint);
      }
    }
  } else {
    g = glyph->index;
    renderFontId = glyph->font;
  }

  if (g != 0 && renderFontId != fontid) {
    // glyph was found in fallback font
    FONSglyph* fallbackGlyph = fons__getGlyph(stash, renderFontId, codepoint, flags);
    if (!fallbackGlyph) { glyph = NULL; goto done; }  // this can happen if atlas is full
    int next = glyph ? glyph->next : font->lut[h];
    if (!glyph) {
      glyph = fons__allocGlyph(font);
      font->lut[h] = font->nglyphs-1;
    }
    else {
      assert(glyph->index == fallbackGlyph->index && glyph->font == fallbackGlyph->font);
    }
    *glyph = *fallbackGlyph;  //memcpy(glyph, fallbackGlyph, sizeof(FONSglyph));
    glyph->next = next;  // restore
    glyph->codepoint = codepoint;  // in case we used replacement char glyph
    goto done;
  }
  // at this point, g == 0 means glyph was not found anywhere

  // first, get bitmap info so we can check for empty glyph 0
  scale = fons__tt_getPixelHeightScale(&font->font, size);
  fons__tt_buildGlyphBitmap(&font->font, g, size, scale, &advance, &lsb, &x0, &y0, &x1, &y1);
  if (stash->params.flags & FONS_SDF) {
    cellw = x1-x0 + 2*pad;
    cellh = y1-y0 + 2*pad;
  }

  // 0xFFFF is an invalid codepoint, only used to create notdef glyph
  // try "replacement character" glyph if glyph 0 is empty
  if (codepoint == 0xFFFF && font->notDef < 0 && (x0 == x1 || y0 == y1 || stash->params.notDefCodePt)) {
    glyph = fons__getGlyph(stash, fontid, notdefcp, flags);
    goto done;
  }

  // we use glyph.index = -1 to have glyph reference notdef glyph so we only need a single notdef bitmap
  if (g == 0 && codepoint != 0xFFFF && codepoint != notdefcp) {
    if (font->notDef < 0) {  // notdef not created yet
      FONSglyph* notdefGlyph = fons__getGlyph(stash, fontid, 0xFFFF, flags);
      if (!notdefGlyph) { glyph = NULL; goto done; }  // this can happen if atlas is full
      font->notDef = notdefGlyph - font->glyphs;
      assert(font->notDef >= 0 && font->notDef < font->nglyphs);
    }
    g = -1;  // this glyph will reference notDef
  }

  // Determines the spot to draw glyph in the atlas.
  if (flags & FONS_GLYPH_BITMAP_REQUIRED) {
    added = fons__atlasAddCell(stash, cellw, cellh, &gx, &gy);
    if (added == 0 && stash->handleError != NULL) {
      // Atlas is full, let the user resize the atlas (or not), and try again.
      stash->handleError(stash->errorUptr, FONS_ATLAS_FULL, 0);
      added = fons__atlasAddCell(stash, cellw, cellh, &gx, &gy);
    }
    if (added == 0) { glyph = NULL; goto done; }
  } else {
    // Negative coordinate indicates there is no bitmap data created.
    gx = -(pad+1);
    gy = -(pad+1);
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
  glyph->x0 = (short)(gx + pad);  // note padding no longer included in FONSglyph bounds
  glyph->y0 = (short)(gy + pad);
  glyph->x1 = (short)(glyph->x0 + (x1-x0));
  glyph->y1 = (short)(glyph->y0 + (y1-y0));
  glyph->xoff = (short)x0;
  glyph->yoff = (short)y0;

  if (g < 0) {
    glyph = &font->glyphs[font->notDef];  // created new LUT entry referencing notDef; now return notDef
    goto done;
  }
  if (flags & FONS_GLYPH_BITMAP_REQUIRED) {
    // Rasterize if not empty glyph; we assume texData for the cell has been cleared to all zeros
    if (x1 > x0 && y1 > y0) {
      if (stash->params.flags & FONS_SUMMED) {
        FONStexelF* dst = (FONStexelF*)stash->texData + (gx+pad + (gy+pad)*stash->atlas->width);
        fons__tt_renderGlyphBitmapSummed(&font->font, dst, cellw - pad, cellh - pad, stash->atlas->width, scale, g);
      } else if (stash->params.flags & FONS_SDF) {
        FONStexelU8* dst = (FONStexelU8*)stash->texData + (gx + gy*stash->atlas->width);
        if (stash->params.userSDFRender)
          stash->params.userSDFRender(stash->params.userPtr,
              &font->font, dst, cellw, cellh, stash->atlas->width, scale, pad, g);
        else
          fons__tt_renderGlyphBitmapSDF(&font->font, dst, cellw, cellh,
              stash->atlas->width, scale, pad, stash->params.sdfPixelDist, g);
      } else {
        FONStexelU8* dst = (FONStexelU8*)stash->texData + (gx+pad + (gy+pad)*stash->atlas->width);
        fons__tt_renderGlyphBitmap(&font->font, dst, cellw - pad, cellh - pad, stash->atlas->width, scale, g);
      }
    }

    stash->dirtyRect[0] = fons__mini(stash->dirtyRect[0], gx);
    stash->dirtyRect[1] = fons__mini(stash->dirtyRect[1], gy);
    stash->dirtyRect[2] = fons__maxi(stash->dirtyRect[2], gx + cellw);
    stash->dirtyRect[3] = fons__maxi(stash->dirtyRect[3], gy + cellh);
  }
done:
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

static void fons__getQuad(FONScontext* stash, FONSglyph* glyph, float size, float blur, float x, float y, FONSquad* q)
{
  float expand = stash->params.flags & FONS_SDF ? blur + 1 : 1;
  float xoff = glyph->xoff - expand;
  float yoff = glyph->yoff - expand;
  float x0 = glyph->x0 - expand;
  float y0 = glyph->y0 - expand;
  float x1 = glyph->x1 + expand;
  float y1 = glyph->y1 + expand;
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

static float fons__getVertAlign(FONScontext* stash, FONSfont* font, int align, float size)
{
  float sgn = stash->params.flags & FONS_ZERO_TOPLEFT ? 1.0f : -1.0f;
  if (align & FONS_ALIGN_TOP)
    return sgn * font->ascender * size;
  if (align & FONS_ALIGN_MIDDLE)
    return sgn * (font->ascender + font->descender) / 2.0f * size;
  if (align & FONS_ALIGN_BOTTOM)
    return sgn * font->descender * size;
  return 0.0f;  // align & FONS_ALIGN_BASELINE (default)
}

int fonsTextIterInit(FONSstate* state, FONStextIter* iter,
           float x, float y, const char* str, const char* end, int bitmapOption)
{
  float width;
  FONScontext* stash = state->context;
  memset(iter, 0, sizeof(*iter));
  if (stash == NULL) return 0;

  if (end == NULL)
    end = str + strlen(str);

  // Align horizontally
  if (state->align & FONS_ALIGN_LEFT) {
    // empty
  } else if (state->align & FONS_ALIGN_RIGHT) {
    width = fonsTextBounds(state, x,y, str, end, NULL);
    x -= width;
  } else if (state->align & FONS_ALIGN_CENTER) {
    width = fonsTextBounds(state, x,y, str, end, NULL);
    x -= width * 0.5f;
  }

  if (state->font < 0 || state->font >= stash->nfonts) return 0;
  iter->font = state->font;  //stash->fonts[state->font];
  if (stash->fonts[state->font]->data == NULL) return 0;
  if (stash->atlasFontPx <= 0 && bitmapOption != FONS_GLYPH_BITMAP_OPTIONAL) return 0;

  iter->size = state->size;
  iter->blur = stash->params.flags & FONS_SDF ? state->blur : 0;
  if (iter->blur > stash->params.sdfPadding) { iter->blur = stash->params.sdfPadding; }

  // Align vertically.
  y += fons__getVertAlign(stash, stash->fonts[iter->font], state->align, iter->size);

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

int fonsTextIterNext(FONSstate* state, FONStextIter* iter, FONSquad* quad)
{
  FONScontext* stash = state->context;
  FONSglyph* glyph = NULL;
  const char* str = iter->next;
  if (stash == NULL) return 0;
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
      float scale = fons__tt_getPixelHeightScale(&font->font, iter->size);
      iter->nextx += fons__getKern(kernFont, iter->prevGlyphIndex, glyph, scale, iter->spacing);
      iter->x = iter->nextx;
      iter->y = iter->nexty;
      if (quad)
        fons__getQuad(stash, glyph, iter->size, iter->blur, iter->x, iter->y, quad);
      iter->nextx += FONS_ROUNDADV(scale*glyph->xadv);
    }
    iter->prevGlyphIndex = glyph != NULL ? glyph->index : -1;
    iter->prevGlyphFont = glyph != NULL ? glyph->font : -1;
    break;
  }
  iter->next = str;

  return 1;
}

float fonsTextBounds(FONSstate* state, float x, float y, const char* str, const char* end, float* bounds)
{
  FONScontext* stash = state->context;
  int align = state->align;
  float advance, minx = x, miny = y, maxx = x, maxy = y;
  FONStextIter iter;
  FONSquad q;

  state->align |= FONS_ALIGN_LEFT;  // prevent infinite recursion (fonsTextIterInit calling fonsTextBounds)
  fonsTextIterInit(state, &iter, x, y, str, end, FONS_GLYPH_BITMAP_OPTIONAL);
  while (fonsTextIterNext(state, &iter, bounds ? &q : NULL)) {
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

void fonsVertMetrics(FONSstate* state, float* ascender, float* descender, float* lineh)
{
  FONSfont* font;
  FONScontext* stash = state->context;
  if (stash == NULL) return;

  if (state->font < 0 || state->font >= stash->nfonts) return;
  font = stash->fonts[state->font];
  if (font->data == NULL) return;

  if (ascender)
    *ascender = font->ascender*state->size;
  if (descender)
    *descender = font->descender*state->size;
  if (lineh)
    *lineh = font->lineh*state->size;
}

void fonsLineBounds(FONSstate* state, float y, float* miny, float* maxy)
{
  FONSfont* font;
  FONScontext* stash = state->context;
  if (stash == NULL) return;

  if (state->font < 0 || state->font >= stash->nfonts) return;
  font = stash->fonts[state->font];
  if (font->data == NULL) return;

  y += fons__getVertAlign(stash, font, state->align, state->size);

  if (stash->params.flags & FONS_ZERO_TOPLEFT) {
    *miny = y - font->ascender * state->size;
    *maxy = *miny + font->lineh * state->size;
  } else {
    *maxy = y + font->descender * state->size;
    *miny = *maxy - font->lineh * state->size;
  }
}

const void* fonsGetTextureData(FONScontext* stash, int* width, int* height)
{
  if (stash == NULL) return NULL;
  if (width != NULL)
    *width = stash->atlas->width;
  if (height != NULL)
    *height = stash->atlas->height;
  void* data = stash->texData;
  return data;
}

int fonsValidateTexture(FONScontext* stash, int* dirty)
{
  if (stash == NULL) return 0;
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

  if (stash->params.userDelete)
    stash->params.userDelete(stash->params.userPtr);

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
  unsigned char* data = NULL;
  int texelBytes;
  if (stash == NULL) return 0;

  width = fons__maxi(width, stash->atlas->width);
  height = fons__maxi(height, stash->atlas->height);

  if (width == stash->atlas->width && height == stash->atlas->height)
    return 1;
  // can only increase height since we've discarded the rect packing logic
  if (width != stash->atlas->width)
    return 0;

  // Copy old texture data over.
  texelBytes = stash->params.flags & FONS_SUMMED ? sizeof(FONStexelF) : sizeof(FONStexelU8);
  data = (unsigned char*)realloc(stash->texData, width * height * texelBytes);
  if (data == NULL)
    return 0;
  if (height > stash->atlas->height)
    memset(&data[stash->atlas->height * width * texelBytes], 0, (height - stash->atlas->height) * width * texelBytes);
  stash->texData = data;

  // nothing is dirtied
  //stash->dirtyRect[0] = 0;
  //stash->dirtyRect[1] = fons__mini(stash->dirtyRect[1], stash->atlas->height);
  //stash->dirtyRect[2] = stash->atlas->width;
  //stash->dirtyRect[3] = height;

  stash->atlas->width = width;
  stash->atlas->height = height;
  stash->itw = 1.0f/stash->atlas->width;
  stash->ith = 1.0f/stash->atlas->height;

  return 1;
}

int fonsResetAtlas(FONScontext* stash, int width, int height, int atlasFontPx)
{
  int i, j;
  size_t nbytes;
  if (stash == NULL) return 0;

  // Reset atlas
  fons__atlasReset(stash->atlas, width, height);  //, cellw, cellh);

  // Clear texture data.
  nbytes = width * height * (stash->params.flags & FONS_SUMMED ? sizeof(FONStexelF) : sizeof(FONStexelU8));
  stash->texData = realloc(stash->texData, nbytes);
  if (stash->texData == NULL) return 0;
  memset(stash->texData, 0, nbytes);

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

// moved here from nanovg.c
int fonsBreakLines(FONSstate* state, const char* string, const char* end, float breakRowWidth, FONStextRow* rows, int maxRows)
{
  enum CodepointType { FONS_SPACE, FONS_NEWLINE, FONS_CHAR, FONS_CJK_CHAR, FONS_DASH };
  FONStextIter iter;
  FONSquad q;
  int nrows = 0;
  float rowStartX = 0;  // absolute
  float rowWidth = 0;
  float rowMinX = 0;  // relative to rowStartX
  float rowMaxX = 0;  // relative to rowStartX
  const char* rowStart = NULL;
  const char* rowEnd = NULL;
  const char* wordStart = NULL;
  float wordStartX = 0;  // absolute
  float wordMinX = 0;  // absolute
  float wordMinY = 1e6, wordMaxY = -1e6, rowMinY = 1e6, rowMaxY = -1e6;
  const char* breakEnd = NULL;
  float breakWidth = 0;
  float breakMaxX = 0;
  int type = FONS_SPACE, ptype = FONS_SPACE;
  unsigned int pcodepoint = 0;
  int rowChars = 0;
  int wordStartChars = 0;
  int maxChars = 0x7fffffff;

  if (maxRows == 0) return 0;
  if (state->font == FONS_INVALID) return 0;
  if (end == NULL)
    end = string + strlen(string);
  if (string == end) return 0;
  if (breakRowWidth < 0) {
    maxChars = -breakRowWidth;
    breakRowWidth = 1e6f;
  }

  fonsTextIterInit(state, &iter, 0, 0, string, end, FONS_GLYPH_BITMAP_OPTIONAL);
  while (fonsTextIterNext(state, &iter, &q)) {
    switch (iter.codepoint) {
      case 9:			// \t
      case 11:		// \v
      case 12:		// \f
      case 32:		// space
      case 0x00a0:	// NBSP
        type = FONS_SPACE;
        break;
      case 10:		// \n
        type = pcodepoint == 13 ? FONS_SPACE : FONS_NEWLINE;
        break;
      case 13:		// \r
        type = pcodepoint == 10 ? FONS_SPACE : FONS_NEWLINE;
        break;
      case 0x0085:	// NEL
        type = FONS_NEWLINE;
        break;
      // breakable and printable chars
      case 45:  // '-'
      case 47:  // '/'
      case 0x2013:  // endash
        type = FONS_DASH;
        break;
      default:
        if ((iter.codepoint >= 0x4E00 && iter.codepoint <= 0x9FFF) ||
          (iter.codepoint >= 0x3000 && iter.codepoint <= 0x30FF) ||
          (iter.codepoint >= 0xFF00 && iter.codepoint <= 0xFFEF) ||
          (iter.codepoint >= 0x1100 && iter.codepoint <= 0x11FF) ||
          (iter.codepoint >= 0x3130 && iter.codepoint <= 0x318F) ||
          (iter.codepoint >= 0xAC00 && iter.codepoint <= 0xD7AF))
          type = FONS_CJK_CHAR;
        else
          type = FONS_CHAR;
        break;
    }

    if (type == FONS_NEWLINE) {
      rowMinY = fons__minf(rowMinY, wordMinY);
      rowMaxY = fons__maxf(rowMaxY, wordMaxY);
      // Always handle new lines.
      rows[nrows].start = rowStart != NULL ? rowStart : iter.str;
      rows[nrows].end = rowEnd != NULL ? rowEnd : iter.str;
      rows[nrows].width = rowWidth;
      rows[nrows].minx = rowMinX;
      rows[nrows].maxx = rowMaxX;
      rows[nrows].next = iter.next;
      rows[nrows].miny = rowMinY;  // stash->params.flags & FONS_ZERO_TOPLEFT ? rowMinY : rowMaxY
      rows[nrows].maxy = rowMaxY;
      nrows++;
      if (nrows >= maxRows)
        return nrows;
      // Set null break point
      breakEnd = rowStart;
      breakWidth = 0.0;
      breakMaxX = 0.0;
      rowChars = 0;
      // Indicate to skip the white space at the beginning of the row.
      rowStart = NULL;
      rowEnd = NULL;
      rowWidth = 0;
      rowMinX = rowMaxX = 0;
      // new row, new word
      wordMinY = rowMinY = 1e6;
      wordMaxY = rowMaxY = -1e6;
    } else {
      if (rowStart == NULL) {
        // Skip white space until the beginning of the line
        if (type == FONS_CHAR || type == FONS_CJK_CHAR || type == FONS_DASH) {
          // The current char is the row so far
          rowStartX = iter.x;
          rowStart = iter.str;
          rowEnd = iter.next;
          rowWidth = iter.nextx - rowStartX; // q.x1 - rowStartX;
          rowMinX = q.x0 - rowStartX;
          rowMaxX = q.x1 - rowStartX;
          wordStart = iter.str;
          wordStartX = iter.x;
          wordMinX = q.x0; // - rowStartX;
          // Set null break point
          breakEnd = rowStart;
          breakWidth = 0.0;
          breakMaxX = 0.0;
          rowChars = 1;
          // add char vert bounds
          wordMinY = q.y0;
          wordMaxY = q.y1;
        }
      } else {
        float nextWidth = iter.nextx - rowStartX;
        ++rowChars;

        // track last end of a word
        if (((ptype == FONS_CHAR || ptype == FONS_CJK_CHAR) && type == FONS_SPACE)
            || (type == FONS_CJK_CHAR && ptype != FONS_SPACE) || ptype == FONS_DASH) {
          breakEnd = iter.str;
          breakWidth = rowWidth;
          breakMaxX = rowMaxX;
          // word will be on current line
          rowMinY = fons__minf(rowMinY, wordMinY);
          rowMaxY = fons__maxf(rowMaxY, wordMaxY);
          wordMinY = 1e6;
          wordMaxY = -1e6;
        }

        // track last beginning of a word
        if (((ptype == FONS_SPACE || ptype == FONS_DASH) && type == FONS_CHAR) || type == FONS_CJK_CHAR) {
          wordStart = iter.str;
          wordStartX = iter.x;
          wordMinX = q.x0;
          wordStartChars = rowChars;
        }

        // Break to new line when a character is beyond break width.
        if (type == FONS_CHAR || type == FONS_CJK_CHAR || type == FONS_DASH) {
          if (nextWidth > breakRowWidth || rowChars > maxChars) {
            if (breakEnd == rowStart) {
              // The current word is longer than the row length, just break it from here.
              rows[nrows].start = rowStart;
              rows[nrows].end = iter.str;
              rows[nrows].width = rowWidth;
              rows[nrows].minx = rowMinX;
              rows[nrows].maxx = rowMaxX;
              rows[nrows].next = iter.str;
              rows[nrows].miny = wordMinY;
              rows[nrows].maxy = wordMaxY;
              if (++nrows >= maxRows)
                return nrows;
              rowStartX = iter.x;
              rowStart = iter.str;
              rowMinX = q.x0 - rowStartX;
              wordStart = iter.str;
              wordStartX = iter.x;
              wordMinX = q.x0;
              rowChars = 0;
              // reset Y
              rowMinY = wordMinY = q.y0;
              rowMaxY = wordMaxY = q.y1;
            } else {
              // Break the line from the end of the last word, and start new line from the beginning of the new.
              rows[nrows].start = rowStart;
              rows[nrows].end = breakEnd;
              rows[nrows].width = breakWidth;
              rows[nrows].minx = rowMinX;
              rows[nrows].maxx = breakMaxX;
              rows[nrows].next = wordStart;
              rows[nrows].miny = rowMinY;
              rows[nrows].maxy = rowMaxY;
              if (++nrows >= maxRows)
                return nrows;
              rowStartX = wordStartX;
              rowStart = wordStart;
              rowMinX = wordMinX - rowStartX;
              rowChars -= wordStartChars;
              // current word will be on next row, so don't touch wordMin/MaxY
              rowMinY = wordMinY;
              rowMaxY = wordMaxY;
              // No change to the word start
            }
            // Set null break point
            breakEnd = rowStart;
            breakWidth = 0.0;
            breakMaxX = 0.0;
          }
          // track last non-white space character
          rowEnd = iter.next;
          rowWidth = iter.nextx - rowStartX;
          rowMaxX = q.x1 - rowStartX;
          // add char vert bounds
          wordMinY = fons__minf(wordMinY, q.y0);
          wordMaxY = fons__maxf(wordMaxY, q.y1);
        }
      }
    }
    pcodepoint = iter.codepoint;
    ptype = type;
  }

  // Break the line from the end of the last word, and start new line from the beginning of the new.
  if (rowStart != NULL) {
    rows[nrows].start = rowStart;
    rows[nrows].end = rowEnd;
    rows[nrows].width = rowWidth;
    rows[nrows].minx = rowMinX;
    rows[nrows].maxx = rowMaxX;
    rows[nrows].next = end;
    rows[nrows].miny = fons__minf(rowMinY, wordMinY);
    rows[nrows].maxy = fons__maxf(rowMaxY, wordMaxY);
    nrows++;
  }
  return nrows;
}

#endif  // FONTSTASH_IMPLEMENTATION

// g++ -x c++ -DFONTSTASH_TEST -DFONTSTASH_IMPLEMENTATION -I stb -o fonstest ../src/fontstash.h
#ifdef FONTSTASH_TEST
#include <string>
#include <random>
#include <locale>         // std::wstring_convert
#include <codecvt>        // std::codecvt_utf8

inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// note these are not thread-safe because wstring_convert is static; try thread_local!
static std::string utf32_to_utf8(const std::u32string& str32)
{
  static std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> cv;
  return cv.to_bytes(str32);
}

static std::u32string utf8_to_utf32(const char* str8)
{
  static std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> cv;
  return cv.from_bytes(str8);
}

static std::mt19937 randGen;
unsigned int randpp() { return randGen(); }

//static std::string defaultRandomChars = "0123456789" "ABCDEFGHIJKLMNOPQRSTUVWXYZ" "abcdefghijklmnopqrstuvwxyz";
static std::string randomStr(const unsigned int len, const std::u32string& charset)
{
  std::u32string s(len, 0);
  for(unsigned int ii = 0; ii < len; ++ii)
    s[ii] = charset[randpp() % (charset.size() - 1)];
  return utf32_to_utf8(s);
}

static bool epsEq(float a, float b, float eps = 0.01) { return std::abs(a - b) < eps; }

int main(int argc, char* argv[])
{
  std::u32string charset = utf8_to_utf32("\n\t      abcdefghijklmnopqrstuvwxyz-/新北大都會公園\u2013");
  //std::u32string charset = utf8_to_utf32("\n\t      abcdefghijklmnopqrstuvwxyz-/");

  FONSparams fonsParams = {0};
  //fonsParams.sdfPadding = 4;
  //fonsParams.sdfPixelDist = 32;
  fonsParams.flags = FONS_ZERO_TOPLEFT;  //| FONS_DELAY_LOAD | FONS_SUMMED;
  fonsParams.atlasBlockHeight = 0;
  FONScontext* fons = fonsCreateInternal(&fonsParams);
  const char* sanspath = argc > 1 ? argv[1] : "fonts/Roboto-Regular.ttf";
  int sans = fonsAddFont(fons, "sans", sanspath);
  if(sans < 0) { printf("Unable to load font %s\n", sanspath); return -1; }

  FONSstate state;
  fonsInitState(fons, &state);
  fonsSetFont(&state, sans);
  fonsSetSize(&state, 12);

  int rowWidth = 15;

  FONStextRow rows[100];
  float bounds[4];
  while(true) {
    std::string wrapped;
    std::string str = randomStr(rowWidth + (randpp() % (4*rowWidth)) - 1, charset);
    //std::string str = "ntf c新z北-s i北 園np \n新qlo zz z z te\tlqebq北會yinnnijvt";
    const char* start = str.c_str();
    const char* end = start + str.size();

    size_t nrows = fonsBreakLines(&state, start, end, -rowWidth, rows, 100);
    const char* curr = start;
    bool ok = true;
    for(size_t ii = 0; ii < nrows; ++ii) {
      auto& row = rows[ii];
      unsigned int utf8state = 0, codepoint = 0, ncodepts = 0;
      for (const char* s = row.start; s < row.end; ++s) {
        if (fons__decutf8(&utf8state, &codepoint, *(const unsigned char*)s))
          continue;
        ++ncodepts;
      }
      //if(ncodepts > rowWidth) { printf("ERROR: row %d excess width %d\n", ii, ncodepts); ok = false;}
      //if((ii > 0 && isSpace(row.start[0])) || isSpace(row.end[-1])) { printf("ERROR: row %d not trimmed for: %s\n", ii, start); }

      float adv = fonsTextBounds(&state, 0, 0, row.start, row.end, bounds);
      if(!epsEq(bounds[0], row.minx) || !epsEq(bounds[1], row.miny)
          || !epsEq(bounds[2], row.maxx) || !epsEq(bounds[3], row.maxy)) {
        printf("ERROR: incorrect bounds for row %d\n", ii); ok = false;
      }

      for(; curr < row.start; ++curr) {
        if(!isSpace(*curr)) { printf("ERROR: row %d missing character(s)\n", ii); ok = false; break; }
      }
      curr = row.end;
      wrapped.append(row.start, row.end).append("\n");
    }
    for(; ok && curr < end; ++curr) {
      if(!isSpace(*curr)) { printf("ERROR: last row missing character(s)\n"); ok = false; }
    }

    if(!ok) { printf("ORIGINAL:\n'%s'\nWRAPPED:\n'%s'\n", start, wrapped.c_str()); }
    else { printf("OK\n"); }
  }

  return 0;
}
#endif
