//
// Copyright (c) 2020 Stylus Labs - see LICENSE.txt
//
#ifndef NANOVG_SW_UTILS_H
#define NANOVG_SW_UTILS_H

typedef struct NVGSWUblitter NVGSWUblitter;

NVGSWUblitter* nvgswuCreateBlitter();
void nvgswuSetBlend(int blend);
// width, height specify total size of pixels; x,y,w,h specify region to update
void nvgswuBlit(NVGSWUblitter* ctx, void* pixels, int width, int height, int x, int y, int w, int h);
void nvgswuBlitTex(NVGSWUblitter* ctx, unsigned int tex, int flipped);
void nvgswuDeleteBlitter(NVGSWUblitter* ctx);

#endif // NANOVG_SW_UTILS_H

#ifdef NANOVG_SW_IMPLEMENTATION

struct NVGSWUblitter {
  GLuint vert, frag, prog;
  GLuint vao;
  GLuint vbo;
  GLuint tex;
  int width, height;
};

#define NVGSWU_QUOTE(s) #s

static const char* screenVert = NVGSWU_QUOTE(
\n #ifdef NVGSWU_GL3
\n #define attribute in
\n #define varying out
\n #endif
\n
\n attribute vec2 position_in;
\n attribute vec2 texcoord_in;
\n
\n varying vec2 texcoord;
\n
\n void main()
\n {
\n   texcoord = texcoord_in;
\n   gl_Position = vec4(position_in, 0.0, 1.0);
\n }
);

static const char* screenFrag = NVGSWU_QUOTE(
\n #ifdef NVGSWU_GL3
\n #define texture2D texture
\n #define varying in
\n layout (location = 0) out vec4 outColor;
\n #else
\n #define outColor gl_FragColor
\n #endif
\n
\n varying vec2 texcoord;
\n uniform sampler2D texFramebuffer;
\n
\n void main()
\n {
\n   outColor = texture2D(texFramebuffer, texcoord);
\n }
);

// these can be drawn as 6 GL_TRIANGLES verts or 4 GL_TRIANGLE_STRIP verts
static GLfloat flippedQuad[][4] = {
  { 1.0f,  1.0f,  1.0f, 0.0f},  // x y u v
  { 1.0f, -1.0f,  1.0f, 1.0f},
  {-1.0f,  1.0f,  0.0f, 0.0f},

  {-1.0f, -1.0f,  0.0f, 1.0f},
  {-1.0f,  1.0f,  0.0f, 0.0f},
  { 1.0f, -1.0f,  1.0f, 1.0f}
};

static GLfloat uprightQuad[][4] = {
  { 1.0f,  1.0f,  1.0f, 1.0f},  // x y u v
  { 1.0f, -1.0f,  1.0f, 0.0f},
  {-1.0f,  1.0f,  0.0f, 1.0f},

  {-1.0f, -1.0f,  0.0f, 0.0f},
  {-1.0f,  1.0f,  0.0f, 1.0f},
  { 1.0f, -1.0f,  1.0f, 0.0f}
};

static void checkGLError(const char* str)
{
  GLenum err;
  while ((err = glGetError()) != GL_NO_ERROR)
    NVG_LOG("Error 0x%08x after %s\n", err, str);
}

NVGSWUblitter* nvgswuCreateBlitter()
{
  GLint status;
  NVGSWUblitter* ctx = (NVGSWUblitter*)calloc(1, sizeof(NVGSWUblitter));

  const char* shaderVer =
#ifdef NVGSWU_GL2
    "#version 110";
#elif defined NVGSWU_GL3
    "#version 330 core\n#define NVGSWU_GL3 1";
#elif defined NVGSWU_GLES2
    "#version 100\nprecision mediump float;";
#elif defined NVGSWU_GLES3
    "#version 300 es\n#define NVGSWU_GL3 1\nprecision mediump float;";
#else
    "";
  #error "GL version not specified"
#endif
  const char* vertSrc[] = {shaderVer, screenVert};
  const char* fragSrc[] = {shaderVer, screenFrag};

  NVG_LOG("nvg2: using nvgswu blitter\n");
  ctx->prog = glCreateProgram();
  ctx->vert = glCreateShader(GL_VERTEX_SHADER);
  ctx->frag = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(ctx->vert, 2, vertSrc, 0);
  glShaderSource(ctx->frag, 2, fragSrc, 0);
  glCompileShader(ctx->vert);
  glGetShaderiv(ctx->vert, GL_COMPILE_STATUS, &status);
  if (status != GL_TRUE) { NVG_LOG("nvgswuCreateBlitter: error compiling vert shader"); }
  glCompileShader(ctx->frag);
  glGetShaderiv(ctx->frag, GL_COMPILE_STATUS, &status);
  if (status != GL_TRUE) { NVG_LOG("nvgswuCreateBlitter: error compiling frag shader"); }
  glAttachShader(ctx->prog, ctx->vert);
  glAttachShader(ctx->prog, ctx->frag);
  // locations for glVertexAttribPointer
  glBindAttribLocation(ctx->prog, 0, "position_in");
  glBindAttribLocation(ctx->prog, 1, "texcoord_in");
  glLinkProgram(ctx->prog);
  glGetProgramiv(ctx->prog, GL_LINK_STATUS, &status);
  if (status != GL_TRUE) { NVG_LOG("nvgswuCreateBlitter: error linking shader"); }

  glGenBuffers(1, &ctx->vbo);
#if defined(NVGSWU_GL3) || defined(NVGSWU_GLES3)
  glGenVertexArrays(1, &ctx->vao);
#endif
  checkGLError("nvgswuCreateBlitter");
  return ctx;
}

void nvgswuDeleteBlitter(NVGSWUblitter* ctx)
{
  if (!ctx) return;
  glDeleteTextures(1, &ctx->tex);
#if defined(NVGSWU_GL3) || defined(NVGSWU_GLES3)
  glDeleteVertexArrays(1, &ctx->vao);
#endif
  glDeleteProgram(ctx->prog);
  glDeleteShader(ctx->vert);
  glDeleteShader(ctx->frag);
  free(ctx);
}

void nvgswuSetBlend(int blend)
{
  if(blend) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }
  else
    glDisable(GL_BLEND);
}

#ifndef GL_RGBA8
#define GL_RGBA8 GL_RGBA
#endif

void nvgswuBlit(NVGSWUblitter* ctx, void* pixels, int width, int height, int x, int y, int w, int h)
{
  glActiveTexture(GL_TEXTURE0);
  if (width != ctx->width || height != ctx->height) {
    if (ctx->tex > 0)
      glDeleteTextures(1, &ctx->tex);
    glGenTextures(1, &ctx->tex);
    glBindTexture(GL_TEXTURE_2D, ctx->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    ctx->width = width; ctx->height = height;
  } else if(w > 0 && h > 0) { // update existing texture
    //if(w <= 0) w = ctx->width;
    //if(h <= 0) h = ctx->height;
    glBindTexture(GL_TEXTURE_2D, ctx->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
#ifdef NVGSWU_GLES2
    // GLES2 doesn't support GL_UNPACK_ROW_LENGTH, etc., so update entire width
    glTexSubImage2D(GL_TEXTURE_2D, 0,
        0, y, ctx->width, h, GL_RGBA, GL_UNSIGNED_BYTE, (uint8_t*)pixels + y*ctx->width*4);
#else
    glPixelStorei(GL_UNPACK_ROW_LENGTH, ctx->width);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, x);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, y);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
#endif
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  }
  else
    glBindTexture(GL_TEXTURE_2D, ctx->tex);

  glViewport(0, 0, width, height);  // this is needed in case window size changes!
  nvgswuBlitTex(ctx, 0, 1);
}

void nvgswuBlitTex(NVGSWUblitter* ctx, GLuint tex, int flipped)
{
  if(tex > 0) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
  }
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_CULL_FACE);
  glUseProgram(ctx->prog);
  glUniform1i(glGetUniformLocation(ctx->prog, "texFramebuffer"), 0);
#if defined(NVGSWU_GL3) || defined(NVGSWU_GLES3)
  glBindVertexArray(ctx->vao);
#endif
  glBindBuffer(GL_ARRAY_BUFFER, ctx->vbo);
  glBufferData(GL_ARRAY_BUFFER, 6*4*sizeof(GLfloat), flipped ? flippedQuad : uprightQuad, GL_STREAM_DRAW);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(GLfloat), 0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(GLfloat), (const GLvoid*)(2*sizeof(GLfloat)));
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); //glDrawArrays(GL_TRIANGLES, 0, 6);
  checkGLError("nvgswuBlit");
  // clear OpenGL state
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
#if defined(NVGSWU_GL3) || defined(NVGSWU_GLES3)
  glBindVertexArray(0);
#endif
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glUseProgram(0);
  glBindTexture(GL_TEXTURE_2D, 0);
}

#endif
