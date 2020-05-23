//
// Copyright (c) 2020 Stylus Labs - see LICENSE.txt
//
#ifndef NANOVG_SW_UTILS_H
#define NANOVG_SW_UTILS_H

// TODO: make this "private" and provide getWidth, getHeight fns
struct NVGSWUblitter {
  GLuint vert, frag, prog;
  GLuint vao;
  GLuint vbo;
  GLuint tex;
  int width, height;
};
typedef struct NVGSWUblitter NVGSWUblitter;

NVGSWUblitter* nvgswuCreateBlitter();
// width, height specify total size of pixels; x,y,w,h specify region to update
void nvgswuBlit(NVGSWUblitter* ctx, void* pixels, int width, int height, int x, int y, int w, int h);
void nvgswuDeleteBlitter(NVGSWUblitter* ctx);

#endif // NANOVG_SW_UTILS_H

#ifdef NANOVG_SW_IMPLEMENTATION

#define NVGSWU_QUOTE(s) #s

// TODO: support GL2
static const char* screenVert = NVGSWU_QUOTE(
\n #version 330 core
\n #define attribute in
\n #define varying out
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
\n #version 330 core
\n #define texture2D texture
\n #define varying in
\n layout (location = 0) out vec4 outColor;
\n //#define outColor gl_FragColor
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
// note that v (y texture coord) is flipped to flip the image being blitted
static GLfloat quadVertices[][4] = {
  { 1.0f,  1.0f,  1.0f, 0.0f},  // x y u v
  { 1.0f, -1.0f,  1.0f, 1.0f},
  {-1.0f,  1.0f,  0.0f, 0.0f},

  {-1.0f, -1.0f,  0.0f, 1.0f},
  {-1.0f,  1.0f,  0.0f, 0.0f},
  { 1.0f, -1.0f,  1.0f, 1.0f}
};

static void checkGLError(const char* str)
{
  GLenum err;
  while ((err = glGetError()) != GL_NO_ERROR)
    NVG_LOG("Error 1 0x%08x after %s\n", err, str);
}

NVGSWUblitter* nvgswuCreateBlitter()
{
  //GLint status;
  NVGSWUblitter* ctx = (NVGSWUblitter*)calloc(1, sizeof(NVGSWUblitter));

  ctx->prog = glCreateProgram();
  ctx->vert = glCreateShader(GL_VERTEX_SHADER);
  ctx->frag = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(ctx->vert, 1, &screenVert, 0);
  glShaderSource(ctx->frag, 1, &screenFrag, 0);
  glCompileShader(ctx->vert);  //
  //glGetShaderiv(vert, GL_COMPILE_STATUS, &status); if (status != GL_TRUE) dumpShaderError(vert, "vert");
  glCompileShader(ctx->frag);
  //glGetShaderiv(frag, GL_COMPILE_STATUS, &status); if (status != GL_TRUE) dumpShaderError(frag, "frag");
  glAttachShader(ctx->prog, ctx->vert);
  glAttachShader(ctx->prog, ctx->frag);
  // locations for glVertexAttribPointer
  glBindAttribLocation(ctx->prog, 0, "position_in");
  glBindAttribLocation(ctx->prog, 1, "texcoord_in");
  glLinkProgram(ctx->prog);
  //glGetProgramiv(prog, GL_LINK_STATUS, &status); if (status != GL_TRUE) PLATFORM_LOG("Link error\n");

  glGenBuffers(1, &ctx->vbo);
  glGenVertexArrays(1, &ctx->vao);
  checkGLError("nvgswuCreateBlitter");
  return ctx;
}

void nvgswuDeleteBlitter(NVGSWUblitter* ctx)
{
  if (!ctx) return;
  glDeleteTextures(1, &ctx->tex);
  glDeleteVertexArrays(1, &ctx->vao);
  glDeleteProgram(ctx->prog);
  glDeleteShader(ctx->vert);
  glDeleteShader(ctx->frag);
}

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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    ctx->width = width; ctx->height = height;
  } else { //update existing texture
    if(w <= 0) w = ctx->width;
    if(h <= 0) h = ctx->height;
    glBindTexture(GL_TEXTURE_2D, ctx->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, ctx->width);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, x);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, y);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  }

  glUseProgram(ctx->prog);
  glUniform1i(glGetUniformLocation(ctx->prog, "texFramebuffer"), 0);
  glBindVertexArray(ctx->vao);
  glBindBuffer(GL_ARRAY_BUFFER, ctx->vbo);
  glBufferData(GL_ARRAY_BUFFER, 6*4*sizeof(GLfloat), quadVertices, GL_STREAM_DRAW);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(GLfloat), 0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(GLfloat), (const GLvoid*)(2*sizeof(GLfloat)));
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); //glDrawArrays(GL_TRIANGLES, 0, 6);
  checkGLError("nvgswuBlit");
}

#endif
