//
// Copyright (c) 2020 Stylus Labs - see LICENSE.txt
//   based on nanovg:
// Copyright (c) 2013 Mikko Mononen memon@inside.org
//
#ifndef NANOVG_GL_UTILS_H
#define NANOVG_GL_UTILS_H

#ifdef IDE_INCLUDES
// defines and includes to make IDE useful
#include "../example/platform.h"
#define NANOVG_GLES3_IMPLEMENTATION
#include "nanovg_gl.h"
#endif

typedef struct NVGLUframebuffer NVGLUframebuffer;

// Helper function to create GL frame buffer to render to.
NVGLUframebuffer* nvgluCreateFramebuffer(NVGcontext* ctx, int w, int h, int imageFlags);
int nvgluBindFramebuffer(NVGLUframebuffer* fb);
void nvgluSetFramebufferSRGB(int enable);
void nvgluSetFramebufferSize(NVGLUframebuffer* fb, int w, int h, int imageFlags);
void nvgluDeleteFramebuffer(NVGLUframebuffer* fb);
int nvgluGetImageHandle(NVGLUframebuffer* fb);
unsigned int nvgluGetTexture(NVGLUframebuffer* fb);
void nvgluBlitFramebuffer(NVGLUframebuffer* fb, int destFBO);
void nvgluReadPixels(NVGLUframebuffer* fb, void* dest);

// these are provided so that nanovg + nanovg_gl_utils can be used to draw w/o including GL headers
void nvgluBindFBO(int fbo);
void nvgluSetViewport(int x, int y, int w, int h);
void nvgluSetScissor(int x, int y, int w, int h);
void nvgluClear(NVGcolor color);

#define NVGLU_SAMPLES_SHIFT 26

enum NVGimageFlagsGLU {
  NVGLU_NO_NVG_IMAGE = 1<<24,	// do not create a nanovg image for the texture
  NVGLU_FB_DEPTH = 1<<25,
  NVGLU_SAMPLES = 0x0F<<NVGLU_SAMPLES_SHIFT,
};

#endif // NANOVG_GL_UTILS_H

#ifdef NANOVG_GLU_IMPLEMENTATION

struct NVGLUframebuffer {
  NVGcontext* ctx;
  GLuint fbo;
  GLuint texture, rbcolor, rbdepth;
  int image;
  int width;
  int height;
};

// we'll assume FBO functionality is available (as nanovgXC doesn't work without it)

//static const GLenum drawBuffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };

NVGLUframebuffer* nvgluCreateFramebuffer(NVGcontext* ctx, int w, int h, int imageFlags)
{
  GLint defaultFBO;
  NVGLUframebuffer* fb = NULL;

  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &defaultFBO);

  fb = (NVGLUframebuffer*)malloc(sizeof(NVGLUframebuffer));
  if (!fb) return NULL;
  memset(fb, 0, sizeof(NVGLUframebuffer));
  fb->ctx = ctx;
  // frame buffer object
  glGenFramebuffers(1, &fb->fbo);

  if (imageFlags & NVGLU_NO_NVG_IMAGE) {
    fb->image = -1;
    if (w <= 0 || h <= 0)
      return fb;

    glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
    nvgluSetFramebufferSize(fb, w, h, imageFlags);
  } else {
#ifdef NANOVG_GL_IMPLEMENTATION
    glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
    fb->width = w;
    fb->height = h;
    fb->image = nvgCreateImageRGBA(ctx, w, h, imageFlags | NVG_IMAGE_FLIPY | NVG_IMAGE_PREMULTIPLIED, NULL);
    fb->texture = nvglImageHandle(ctx, fb->image);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb->texture, 0);
#endif
  }
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    nvgluDeleteFramebuffer(fb);
    fb = NULL;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, defaultFBO);
  return fb;
}

// returns previously bound FBO
int nvgluBindFramebuffer(NVGLUframebuffer* fb)
{
  int prevFBO = -1;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
  return prevFBO;
}

// enable or disable automatic sRGB conversion *if* writing to sRGB framebuffer on desktop GL; for GLES,
//  GL_FRAMEBUFFER_SRGB is not available and sRGB conversion is enabled iff framebuffer is sRGB
void nvgluSetFramebufferSRGB(int enable)
{
#if defined(NANOVG_GL2) || defined(NANOVG_GL3)
  enable ? glEnable(GL_FRAMEBUFFER_SRGB) : glDisable(GL_FRAMEBUFFER_SRGB);
#endif
}

// assumes nvgluBindFramebuffer() has already been called on fb
void nvgluSetFramebufferSize(NVGLUframebuffer* fb, int w, int h, int imageFlags)
{
  if (w <= 0 || h <= 0 || (w == fb->width && h == fb->height))
    return;
  if (fb->image >= 0) {
    NVG_LOG("nvgluSetFramebufferSize() can only be used with framebuffer created with NVGLU_NO_NVG_IMAGE.");
    return;
  }
  if (fb->texture > 0) { glDeleteTextures(1, &fb->texture); fb->texture = 0; }
  if (fb->rbcolor > 0) { glDeleteRenderbuffers(1, &fb->rbcolor); fb->rbcolor = 0; }
  if (fb->rbdepth > 0) { glDeleteRenderbuffers(1, &fb->rbdepth); fb->rbdepth = 0; }

  GLint internalfmt = (imageFlags & NVG_IMAGE_SRGB) ? GL_SRGB8_ALPHA8 : GL_RGBA8;
  int samples = (imageFlags & NVGLU_SAMPLES) >> NVGLU_SAMPLES_SHIFT;
  if (samples > 1 || (imageFlags & NVGLU_FB_DEPTH)) {
    glGenRenderbuffers(1, &fb->rbcolor);
    glBindRenderbuffer(GL_RENDERBUFFER, fb->rbcolor);
    if (samples > 1)
      glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, internalfmt, w, h);
    else
      glRenderbufferStorage(GL_RENDERBUFFER, internalfmt, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, fb->rbcolor);

    if(imageFlags & NVGLU_FB_DEPTH) {
      glGenRenderbuffers(1, &fb->rbdepth);
      glBindRenderbuffer(GL_RENDERBUFFER, fb->rbdepth);
      if (samples > 1)
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, w, h);
      else
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb->rbdepth);
    }
    else
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);

    glBindRenderbuffer(GL_RENDERBUFFER, 0);
  }
  else {
    glGenTextures(1, &fb->texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fb->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, internalfmt, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    //glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb->texture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
  }
  fb->width = w;
  fb->height = h;
}

// assumes FBO (source) is already bound; destFBO is bound on return
void nvgluBlitFramebuffer(NVGLUframebuffer* fb, int destFBO)
{
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destFBO);
  glBlitFramebuffer(0, 0, fb->width, fb->height, 0, 0, fb->width, fb->height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  //glInvalidateFramebuffer(GL_READ_FRAMEBUFFER, 1, drawBuffers);
  glBindFramebuffer(GL_FRAMEBUFFER, destFBO);
}

void nvgluReadPixels(NVGLUframebuffer* fb, void* dest)
{
  // for desktop GL, we could use glGetTexImage
  glReadPixels(0, 0, fb->width, fb->height, GL_RGBA, GL_UNSIGNED_BYTE, dest);
}

void nvgluBindFBO(int fbo)
{
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
}

void nvgluSetViewport(int x, int y, int w, int h)
{
  glViewport(x, y, w, h);
}

void nvgluSetScissor(int x, int y, int w, int h)
{
  if (w <= 0 || h <= 0)
    glDisable(GL_SCISSOR_TEST);
  else {
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, w, h);
  }
}

void nvgluClear(NVGcolor color)
{
  glClearColor(color.r/255.0f, color.g/255.0f, color.b/255.0f, color.a/255.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

int nvgluGetImageHandle(NVGLUframebuffer* fb)
{
  return fb->image;
}

unsigned int nvgluGetTexture(NVGLUframebuffer* fb)
{
  return fb->texture;
}

void nvgluDeleteFramebuffer(NVGLUframebuffer* fb)
{
  if (!fb) { return; }
  if (fb->fbo > 0) { glDeleteFramebuffers(1, &fb->fbo); }
  if (fb->image >= 0) { nvgDeleteImage(fb->ctx, fb->image); }
  if (fb->texture > 0) { glDeleteTextures(1, &fb->texture); }
  if (fb->rbcolor > 0) { glDeleteRenderbuffers(1, &fb->rbcolor); }
  if (fb->rbdepth > 0) { glDeleteRenderbuffers(1, &fb->rbdepth); }
  memset(fb, 0, sizeof(NVGLUframebuffer));
  fb->image = -1;
  free(fb);
}

#endif // NANOVG_GL_IMPLEMENTATION
