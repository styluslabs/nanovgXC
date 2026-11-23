// nanovg render backend on Qt's QRhi (Direct3D 11/12, Vulkan, Metal, OpenGL).
//
// Port of nanovgXC's single pass "vector texture" OpenGL backend
// (nanovg_vtex.h): every fill is one draw call over its bounding box (large
// fills over 32 px tiles), and the fragment shader sums the exact coverage over
// the path's edges, read from a RGBA32F texture. Unlike nanovg_gl.h it needs no
// second framebuffer, no image atomics and no blending into float targets, so
// it runs on every QRhi backend, including the software ones (WARP, llvmpipe,
// lavapipe). Shaders: shaders/nvg.vert, shaders/nvg.frag; compiled by qsb
// into the Qt resources :/nanoplot/shaders/nvg.vert.qsb and nvg.frag.qsb.
//
// Single header, like nanovg_gl.h: in exactly one C++ file of the application
//   #define NANOVG_RHI_IMPLEMENTATION
//   #include "nanovg_rhi.h"
// QRhi is C++, so unlike the other backends this one needs a C++ compiler.

#ifndef NANOVG_RHI_H
#define NANOVG_RHI_H

#include <functional>

struct NVGcontext;
class QRhi;
class QRhiCommandBuffer;
class QRhiRenderTarget;
class QRhiResourceUpdateBatch;

/// Creates a nanovg context drawing with @p rhi; nullptr if the shaders or the
/// needed texture formats are not available.
NVGcontext* nvgRhiCreate(QRhi* rhi, int flags);
void nvgRhiDelete(NVGcontext* ctx);

/// Target of the next nvgEndFrame(): the frame is recorded as one render pass
/// on @p cb into @p rt, cleared to @p clearColor (premultiplied RGBA). Pass
/// nullptrs after the frame so no stale pointers are kept.
void nvgRhiSetTarget(NVGcontext* ctx, QRhiCommandBuffer* cb, QRhiRenderTarget* rt, const float clearColor[4]);

/// Resource updates that go into the frame's render pass (recorded before it).
QRhiResourceUpdateBatch* nvgRhiResourceBatch(NVGcontext* ctx);

/// Records @p draw at this point of the frame: it runs inside the frame's render
/// pass, in order with the nanovg draws, which bind their pipeline again after it.
void nvgRhiCustom(NVGcontext* ctx, std::function<void(QRhiCommandBuffer*, QRhiRenderTarget*)> draw);

#endif // NANOVG_RHI_H

#ifdef NANOVG_RHI_IMPLEMENTATION

#include <QColor>
#include <QFile>
#include <QHash>
#include <QVector>
#include <rhi/qrhi.h>

#include <cmath>
#include <cstring>

#include "nanovg.h"

// Structure and the fill/tiling logic follow nanovgXC's nanovg_vtex.h (zlib
// licence, Stylus Labs); the GL calls are replaced by QRhi. Differences:
// - strips and fans become triangle lists (QRhi has no fans),
// - the per call values the GL version sets as plain uniforms (type, view size)
//   live in the uniform block, one block per call,
// - the edge range (offset, count) of a fill quad travels in its vertices, so one
//   call draws all tiles of a path instead of one call per tile,
// - edges are stored in a 2D RGBA32F texture EDGE_TEX_WIDTH wide instead of a
//   256x256 texture array,
// - the frame is recorded into the target given by nvgRhiSetTarget().

namespace {

const int EDGE_TEX_WIDTH = 1024;
const int TILE_SIZE = 32; // nanovg_vtex.h default

// mirrors the std140 block in shaders/nvg.vert and nvg.frag
struct RhiUniforms {
	float scissorMat[12]; // mat3 = 3 x vec4
	float paintMat[12];
	float innerCol[4];
	float outerCol[4];
	float scissorExt[2];
	float scissorScale[2];
	float extent[2];
	float radius;
	float feather;
	float strokeMult;
	float strokeThr;
	qint32 texType;
	qint32 fillMode;
	float viewSize[2];
	float fbYUp;
	float ndcYSign;
	qint32 type;
	qint32 nedges; ///< unused, the edge range comes with the vertices (appendQuad)
	qint32 edgeOffset; ///< unused, see nedges
	qint32 edgeTexWidth;
};
static_assert(sizeof(RhiUniforms) == 208, "RhiUniforms must match the std140 block of the shaders");

enum FragType { FillSimple = 1, FillSolid = 2, FillGrad = 3, FillImg = 4, FillText = 5 };

struct RhiTexture {
	int id = 0;
	QRhiTexture* tex = nullptr;
	QRhiSampler* sampler = nullptr;
	int width = 0, height = 0;
	int type = 0;
	int flags = 0;
};

struct RhiCall {
	int image = 0;
	int vertOffset = 0;
	int vertCount = 0;
	int uniform = 0; ///< index into RhiContext::uniforms
	quint32 blend = 0; ///< four QRhiGraphicsPipeline::BlendFactor, 8 bits each
	int custom = -1; ///< index into RhiContext::customs: a draw from nvgRhiCustom() instead
};

struct RhiTile {
	QVector<NVGvertex> edges;
};

struct RhiContext {
	QRhi* rhi = nullptr;
	int flags = 0;
	QShader vs, fs;

	QVector<RhiTexture> textures;
	int textureId = 0;
	float view[2] = { 0, 0 };
	float dpr = 1.f;

	// per frame
	QVector<RhiCall> calls;
	QVector<NVGvertex> verts;
	QVector<RhiUniforms> uniforms;
	QVector<NVGvertex> edges;
	QVector<RhiTile> tiles;
	QVector<std::function<void(QRhiCommandBuffer*, QRhiRenderTarget*)>> customs;

	// GPU resources
	QRhiBuffer* vbuf = nullptr;
	QRhiBuffer* ubuf = nullptr;
	quint32 ubufStride = 0;
	QRhiTexture* edgeTex = nullptr;
	int edgeTexRows = 0;
	QRhiTexture* dummyTex = nullptr;
	QRhiSampler* nearest = nullptr;
	QHash<int, QRhiShaderResourceBindings*> srbs; ///< by image id, 0 = none
	QHash<quint32, QRhiGraphicsPipeline*> pipelines; ///< by blend key; nullptr = creation failed
	QRhiRenderPassDescriptor* rpDesc = nullptr; ///< the pipelines were made for this one
	QRhiResourceUpdateBatch* pending = nullptr; ///< texture uploads between frames

	// target of the next flush
	QRhiCommandBuffer* cb = nullptr;
	QRhiRenderTarget* rt = nullptr;
	QColor clear;

	QRhiResourceUpdateBatch* batch()
	{
		if(pending == nullptr) pending = rhi->nextResourceUpdateBatch();
		return pending;
	}
};

RhiContext* ctxOf(void* uptr) { return static_cast<RhiContext*>(uptr); }

float clampf(float a, float mn, float mx) { return a < mn ? mn : (a > mx ? mx : a); }
int clampi(int a, int mn, int mx) { return a < mn ? mn : (a > mx ? mx : a); }

void appendN(QVector<NVGvertex>& to, const NVGvertex* from, int n)
{
	if(n > 0) to.append(QVector<NVGvertex>(from, from + n));
}

QShader loadShader(const QString& name)
{
	QFile f(name);
	if(!f.open(QIODevice::ReadOnly)) {
		qWarning("nvg rhi: shader %s missing", qPrintable(name));
		return QShader();
	}
	return QShader::fromSerialized(f.readAll());
}

// ---- textures ------------------------------------------------------------------

RhiTexture* findTexture(RhiContext* c, int id)
{
	if(id == 0) return nullptr;
	for(RhiTexture& t : c->textures)
		if(t.id == id) return &t;
	return nullptr;
}

int bytesPerPixel(int type) { return type == NVG_TEXTURE_ALPHA ? 1 : 4; }

void dropSrb(RhiContext* c, int image)
{
	auto it = c->srbs.find(image);
	if(it != c->srbs.end()) {
		(*it)->deleteLater();
		c->srbs.erase(it);
	}
}

void dropAllSrbs(RhiContext* c)
{
	for(QRhiShaderResourceBindings* srb : std::as_const(c->srbs))
		srb->deleteLater();
	c->srbs.clear();
}

void uploadRect(RhiContext* c, RhiTexture* t, int x, int y, int w, int h, const void* data)
{
	const int bpp = bytesPerPixel(t->type);
	QByteArray bytes(w * h * bpp, Qt::Uninitialized);
	if(data != nullptr) {
		const char* src = static_cast<const char*>(data);
		for(int row = 0; row < h; ++row)
			memcpy(bytes.data() + row * w * bpp, src + ((y + row) * t->width + x) * bpp, size_t(w * bpp));
	} else {
		bytes.fill(0);
	}
	QRhiTextureSubresourceUploadDescription sub(bytes);
	sub.setDestinationTopLeft(QPoint(x, y));
	sub.setSourceSize(QSize(w, h));
	c->batch()->uploadTexture(t->tex, QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, sub)));
	if(t->flags & NVG_IMAGE_GENERATE_MIPMAPS) c->batch()->generateMips(t->tex);
}

int renderCreateTexture(void* uptr, int type, int w, int h, int imageFlags, const void* data)
{
	RhiContext* c = ctxOf(uptr);
	QRhiTexture::Format format = type == NVG_TEXTURE_RGBA ? QRhiTexture::RGBA8 : (type == NVG_TEXTURE_FLOAT ? QRhiTexture::R32F : QRhiTexture::R8);
	QRhiTexture::Flags tflags;
	if(type == NVG_TEXTURE_RGBA && (imageFlags & NVG_IMAGE_SRGB)) tflags |= QRhiTexture::sRGB;
	if(imageFlags & NVG_IMAGE_GENERATE_MIPMAPS) tflags |= QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips;

	RhiTexture t;
	t.id = ++c->textureId;
	t.width = w;
	t.height = h;
	t.type = type;
	t.flags = imageFlags;
	t.tex = c->rhi->newTexture(format, QSize(w, h), 1, tflags);
	if(!t.tex->create()) {
		delete t.tex;
		return 0;
	}
	const QRhiSampler::Filter filter = (imageFlags & NVG_IMAGE_NEAREST) || type == NVG_TEXTURE_FLOAT ? QRhiSampler::Nearest : QRhiSampler::Linear;
	const QRhiSampler::Filter mip = (imageFlags & NVG_IMAGE_GENERATE_MIPMAPS) ? filter : QRhiSampler::None;
	t.sampler = c->rhi->newSampler(filter, filter, mip, (imageFlags & NVG_IMAGE_REPEATX) ? QRhiSampler::Repeat : QRhiSampler::ClampToEdge,
		(imageFlags & NVG_IMAGE_REPEATY) ? QRhiSampler::Repeat : QRhiSampler::ClampToEdge);
	t.sampler->create();
	c->textures.append(t);
	// also without data: the atlas is filled piece by piece, start from zeros
	uploadRect(c, &c->textures.last(), 0, 0, w, h, data);
	return t.id;
}

int renderDeleteTexture(void* uptr, int image)
{
	RhiContext* c = ctxOf(uptr);
	for(int i = 0; i < c->textures.size(); ++i) {
		if(c->textures[i].id == image) {
			dropSrb(c, image);
			c->textures[i].tex->deleteLater();
			c->textures[i].sampler->deleteLater();
			c->textures.removeAt(i);
			return 1;
		}
	}
	return 0;
}

int renderUpdateTexture(void* uptr, int image, int x, int y, int w, int h, const void* data)
{
	RhiContext* c = ctxOf(uptr);
	RhiTexture* t = findTexture(c, image);
	if(t == nullptr || w <= 0 || h <= 0) return 0;
	uploadRect(c, t, x, y, w, h, data);
	return 1;
}

int renderGetTextureSize(void* uptr, int image, int* w, int* h)
{
	RhiTexture* t = findTexture(ctxOf(uptr), image);
	if(t == nullptr) return 0;
	*w = t->width;
	*h = t->height;
	return 1;
}

// ---- pipeline state ----------------------------------------------------------------

QRhiGraphicsPipeline::BlendFactor blendFactor(int factor, QRhiGraphicsPipeline::BlendFactor fallback)
{
	switch(factor) {
		case NVG_ZERO: return QRhiGraphicsPipeline::Zero;
		case NVG_ONE: return QRhiGraphicsPipeline::One;
		case NVG_SRC_COLOR: return QRhiGraphicsPipeline::SrcColor;
		case NVG_ONE_MINUS_SRC_COLOR: return QRhiGraphicsPipeline::OneMinusSrcColor;
		case NVG_DST_COLOR: return QRhiGraphicsPipeline::DstColor;
		case NVG_ONE_MINUS_DST_COLOR: return QRhiGraphicsPipeline::OneMinusDstColor;
		case NVG_SRC_ALPHA: return QRhiGraphicsPipeline::SrcAlpha;
		case NVG_ONE_MINUS_SRC_ALPHA: return QRhiGraphicsPipeline::OneMinusSrcAlpha;
		case NVG_DST_ALPHA: return QRhiGraphicsPipeline::DstAlpha;
		case NVG_ONE_MINUS_DST_ALPHA: return QRhiGraphicsPipeline::OneMinusDstAlpha;
		case NVG_SRC_ALPHA_SATURATE: return QRhiGraphicsPipeline::SrcAlphaSaturate;
	}
	return fallback;
}

quint32 blendKey(NVGcompositeOperationState op)
{
	const quint32 srcRGB = blendFactor(op.srcRGB, QRhiGraphicsPipeline::One);
	const quint32 dstRGB = blendFactor(op.dstRGB, QRhiGraphicsPipeline::OneMinusSrcAlpha);
	const quint32 srcA = blendFactor(op.srcAlpha, QRhiGraphicsPipeline::One);
	const quint32 dstA = blendFactor(op.dstAlpha, QRhiGraphicsPipeline::OneMinusSrcAlpha);
	return srcRGB | dstRGB << 8 | srcA << 16 | dstA << 24;
}

QRhiShaderResourceBindings* srbFor(RhiContext* c, int image)
{
	auto it = c->srbs.constFind(image);
	if(it != c->srbs.constEnd()) return *it;
	RhiTexture* t = findTexture(c, image);
	QRhiShaderResourceBindings* srb = c->rhi->newShaderResourceBindings();
	const auto stages = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
	srb->setBindings({
		QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(0, stages, c->ubuf, sizeof(RhiUniforms)),
		QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, t ? t->tex : c->dummyTex, t ? t->sampler : c->nearest),
		QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, c->edgeTex, c->nearest),
	});
	if(!srb->create()) qWarning("nvg rhi: shader resource bindings failed (image %d)", image);
	c->srbs.insert(image, srb);
	return srb;
}

QRhiGraphicsPipeline* pipelineFor(RhiContext* c, quint32 blend)
{
	auto it = c->pipelines.constFind(blend);
	if(it != c->pipelines.constEnd()) return *it;
	QRhiGraphicsPipeline* ps = c->rhi->newGraphicsPipeline();
	QRhiGraphicsPipeline::TargetBlend tb;
	tb.enable = true;
	tb.srcColor = QRhiGraphicsPipeline::BlendFactor(blend & 0xff);
	tb.dstColor = QRhiGraphicsPipeline::BlendFactor((blend >> 8) & 0xff);
	tb.srcAlpha = QRhiGraphicsPipeline::BlendFactor((blend >> 16) & 0xff);
	tb.dstAlpha = QRhiGraphicsPipeline::BlendFactor((blend >> 24) & 0xff);
	ps->setTargetBlends({ tb });
	ps->setTopology(QRhiGraphicsPipeline::Triangles);
	ps->setCullMode(QRhiGraphicsPipeline::None);
	ps->setShaderStages({ { QRhiShaderStage::Vertex, c->vs }, { QRhiShaderStage::Fragment, c->fs } });
	QRhiVertexInputLayout layout;
	layout.setBindings({ { sizeof(NVGvertex) } });
	layout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float2, 0 }, { 0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float) } });
	ps->setVertexInputLayout(layout);
	ps->setShaderResourceBindings(srbFor(c, 0)); // all srbs share this layout
	ps->setRenderPassDescriptor(c->rpDesc);
	if(!ps->create()) {
		// Remembered until the pipelines are dropped: retrying compiled the shaders again for
		// every draw call (seen with Wine's HLSL compiler, thousands of times a minute).
		qWarning("nvg rhi: pipeline creation failed (blend %08x)", blend);
		delete ps;
		c->pipelines.insert(blend, nullptr);
		return nullptr;
	}
	c->pipelines.insert(blend, ps);
	return ps;
}

void dropPipelines(RhiContext* c)
{
	for(QRhiGraphicsPipeline* ps : std::as_const(c->pipelines))
		if(ps != nullptr) ps->deleteLater();
	c->pipelines.clear();
}

// ---- per frame recording ---------------------------------------------------------

void renderCancel(void* uptr)
{
	RhiContext* c = ctxOf(uptr);
	c->calls.clear();
	c->verts.clear();
	c->uniforms.clear();
	c->edges.clear();
	c->customs.clear();
}

void renderViewport(void* uptr, float width, float height, float devicePixelRatio)
{
	RhiContext* c = ctxOf(uptr);
	c->view[0] = width;
	c->view[1] = height;
	c->dpr = devicePixelRatio;
	renderCancel(uptr); // start of a frame
}

void xformToMat3x4(float* m3, const float* t)
{
	m3[0] = t[0];
	m3[1] = t[1];
	m3[2] = 0.0f;
	m3[3] = 0.0f;
	m3[4] = t[2];
	m3[5] = t[3];
	m3[6] = 0.0f;
	m3[7] = 0.0f;
	m3[8] = t[4];
	m3[9] = t[5];
	m3[10] = 1.0f;
	m3[11] = 0.0f;
}

void convertColor(float* out, NVGcolor col)
{
	const float a = col.a / 255.0f;
	out[0] = a * col.r / 255.0f;
	out[1] = a * col.g / 255.0f;
	out[2] = a * col.b / 255.0f;
	out[3] = a;
}

/// Fills the uniform block for a call like glnvg__convertPaint(); false if the
/// paint refers to an unknown image.
bool convertPaint(RhiContext* c, RhiUniforms& u, int& image, const NVGpaint* paint, const NVGscissor* scissor, int flags)
{
	float invxform[6];
	memset(&u, 0, sizeof(u));
	u.fillMode = flags;
	convertColor(u.innerCol, paint->innerColor);
	convertColor(u.outerCol, paint->outerColor);

	if(scissor->extent[0] < -0.5f || scissor->extent[1] < -0.5f) {
		u.scissorExt[0] = 1.0f;
		u.scissorExt[1] = 1.0f;
		u.scissorScale[0] = 1.0f;
		u.scissorScale[1] = 1.0f;
		u.type = FillSimple; // overridden below for images and gradients
	} else {
		nvgTransformInverse(invxform, scissor->xform);
		xformToMat3x4(u.scissorMat, invxform);
		u.scissorExt[0] = scissor->extent[0];
		u.scissorExt[1] = scissor->extent[1];
		u.scissorScale[0] = sqrtf(scissor->xform[0] * scissor->xform[0] + scissor->xform[2] * scissor->xform[2]) * c->dpr;
		u.scissorScale[1] = sqrtf(scissor->xform[1] * scissor->xform[1] + scissor->xform[3] * scissor->xform[3]) * c->dpr;
		u.type = FillSolid;
	}
	u.extent[0] = paint->extent[0];
	u.extent[1] = paint->extent[1];

	RhiTexture* tex = nullptr;
	image = 0;
	if(paint->image != 0) {
		tex = findTexture(c, paint->image);
		if(tex == nullptr) return false;
		image = paint->image;
		if(tex->type == NVG_TEXTURE_RGBA)
			u.texType = (tex->flags & NVG_IMAGE_PREMULTIPLIED) ? 3 : 1;
		else
			u.texType = 2;
	}

	if(paint->innerColor.c != paint->outerColor.c) {
		u.type = FillGrad;
		u.radius = paint->radius;
		u.feather = paint->feather;
		nvgTransformInverse(invxform, paint->xform);
		xformToMat3x4(u.paintMat, invxform);
	} else if(tex != nullptr) {
		u.type = FillImg;
		if(tex->flags & NVG_IMAGE_FLIPY) {
			float m1[6], m2[6];
			nvgTransformTranslate(m1, 0.0f, u.extent[1] * 0.5f);
			nvgTransformMultiply(m1, paint->xform);
			nvgTransformScale(m2, 1.0f, -1.0f);
			nvgTransformMultiply(m2, m1);
			nvgTransformTranslate(m1, 0.0f, -u.extent[1] * 0.5f);
			nvgTransformMultiply(m1, m2);
			nvgTransformInverse(invxform, m1);
		} else {
			nvgTransformInverse(invxform, paint->xform);
		}
		xformToMat3x4(u.paintMat, invxform);
		u.radius = paint->radius + 0.5f;
	}
	return true;
}

/// Two triangles for the rectangle @p ltrb (left, top, right, bottom) grown by @p pad,
/// carrying the edges [@p edgeOffset, +@p nedges) for the fragment shader (exact in a
/// float up to 2^24 edges per frame).
void appendQuad(RhiContext* c, const float* ltrb, float pad, int edgeOffset, int nedges)
{
	const float l = ltrb[0] - pad, t = ltrb[1] - pad, r = ltrb[2] + pad, b = ltrb[3] + pad;
	const float e = float(edgeOffset), n = float(nedges);
	c->verts.append({ r, b, e, n });
	c->verts.append({ r, t, e, n });
	c->verts.append({ l, b, e, n });
	c->verts.append({ l, b, e, n });
	c->verts.append({ r, t, e, n });
	c->verts.append({ l, t, e, n });
}

/// A call with the uniform block @p base and no quads yet, see appendQuad().
void beginFillCall(RhiContext* c, const RhiCall& proto, const RhiUniforms& base)
{
	RhiCall call = proto;
	call.uniform = c->uniforms.size();
	c->uniforms.append(base);
	call.vertOffset = c->verts.size();
	call.vertCount = 0;
	c->calls.append(call);
}

/// Closes the call of beginFillCall() over the quads appended since; drops it without any.
void endFillCall(RhiContext* c)
{
	RhiCall& call = c->calls.last();
	call.vertCount = c->verts.size() - call.vertOffset;
	if(call.vertCount == 0) {
		c->calls.removeLast();
		c->uniforms.removeLast();
	}
}

void addFillCall(RhiContext* c, const RhiCall& proto, const RhiUniforms& base, int edgeOffset, int nedges, const float* ltrb, float pad)
{
	beginFillCall(c, proto, base);
	appendQuad(c, ltrb, pad, edgeOffset, nedges);
	endFillCall(c);
}

void renderFill(
	void* uptr, NVGpaint* paint, NVGcompositeOperationState compOp, NVGscissor* scissor, int flags, const float* bounds, const NVGpath* paths, int npaths)
{
	RhiContext* c = ctxOf(uptr);
	int ncalls = 0, nedges = 0;
	for(int i = 0; i < npaths; ++i) {
		nedges += paths[i].nfill;
		if(i == 0 || paths[i].restart) ++ncalls;
	}
	if(nedges == 0) return;
	float ltrb[4] = { clampf(bounds[0], 0, c->view[0]), clampf(bounds[1], 0, c->view[1]), clampf(bounds[2], 0, c->view[0]), clampf(bounds[3], 0, c->view[1]) };
	float callw = ltrb[2] - ltrb[0], callh = ltrb[3] - ltrb[1];
	if(callw <= 0 || callh <= 0) return; // offscreen
	if(npaths == 1 && paths[0].convex && paths[0].nfill > 2) flags |= NVG_PATH_CONVEX;

	RhiCall proto;
	proto.blend = blendKey(compOp);
	RhiUniforms base;
	if(!convertPaint(c, base, proto.image, paint, scissor, flags)) return;

	if(flags & NVG_PATH_CONVEX) {
		// fannable and not antialiased: triangles straight from the outline
		const NVGpath* path = &paths[0];
		RhiCall call = proto;
		call.uniform = c->uniforms.size();
		c->uniforms.append(base);
		call.vertOffset = c->verts.size();
		for(int i = 1; i + 1 < path->nfill; ++i) {
			c->verts.append({ path->fill[0].x0, path->fill[0].y0, 0, 0 });
			c->verts.append({ path->fill[i].x0, path->fill[i].y0, 0, 0 });
			c->verts.append({ path->fill[i + 1].x0, path->fill[i + 1].y0, 0, 0 });
		}
		call.vertCount = c->verts.size() - call.vertOffset;
		c->calls.append(call);
	} else if(ncalls == 1 && nedges > 16 && (callw > 2 * TILE_SIZE || callh > 2 * TILE_SIZE)) {
		// large path: tiles, each with only the edges that affect it (nanovg_vtex.h)
		ltrb[0] = floorf(ltrb[0]);
		ltrb[1] = floorf(ltrb[1]);
		ltrb[2] = ceilf(ltrb[2]);
		ltrb[3] = ceilf(ltrb[3]);
		callw = ltrb[2] - ltrb[0];
		callh = ltrb[3] - ltrb[1];
		const int xtiles = int(ceilf(callw / TILE_SIZE)), ytiles = int(ceilf(callh / TILE_SIZE));
		const int tilew = int(ceilf(callw / xtiles)), tileh = int(ceilf(callh / ytiles));
		if(c->tiles.size() < xtiles * ytiles) c->tiles.resize(xtiles * ytiles);
		for(int i = 0; i < npaths; i++) {
			const NVGpath* path = &paths[i];
			if(path->nfill == 0) continue;
			const int pymin = clampi(int(path->bounds[1] - ltrb[1] - 0.5f) / tileh, 0, ytiles - 1);
			for(int j = 0; j < path->nfill; ++j) {
				const float x0 = path->fill[j].x0, y0 = path->fill[j].y0, x1 = path->fill[j].x1, y1 = path->fill[j].y1;
				if(x0 == x1) continue; // vertical edges contribute nothing
				const int vxmin = clampi(int(std::min(x0, x1) - ltrb[0] - 0.5f) / tilew, 0, xtiles - 1);
				const int vxmax = clampi(int(std::max(x0, x1) - ltrb[0] + 0.5f) / tilew, 0, xtiles - 1);
				const int vymax = clampi(int(std::max(y0, y1) - ltrb[1] + 0.5f) / tileh, 0, ytiles - 1);
				for(int ix = vxmin; ix <= vxmax; ++ix) {
					for(int iy = pymin; iy <= vymax; ++iy) {
						QVector<NVGvertex>& te = c->tiles[xtiles * iy + ix].edges;
						if(!te.isEmpty()) {
							// merge connected edges lying entirely below this tile
							NVGvertex& last = te.last();
							const float tymax = (iy + 1) * tileh + ltrb[1];
							if(y0 > tymax && y1 > tymax && last.y0 > tymax && x0 == last.x1 && y0 == last.y1) {
								last.x1 = x1;
								last.y1 = y1;
								continue;
							}
						}
						te.append({ x0, y0, x1, y1 });
					}
				}
			}
		}
		// one call, one quad per tile with its own edge range
		beginFillCall(c, proto, base);
		for(int ix = 0; ix < xtiles; ++ix) {
			for(int iy = 0; iy < ytiles; ++iy) {
				QVector<NVGvertex>& te = c->tiles[xtiles * iy + ix].edges;
				if(te.isEmpty()) continue;
				const float tb[4] = { ltrb[0] + ix * tilew, ltrb[1] + iy * tileh, std::min(ltrb[2], ltrb[0] + (ix + 1) * tilew),
					std::min(ltrb[3], ltrb[1] + (iy + 1) * tileh) };
				const int offset = c->edges.size();
				c->edges.append(te);
				appendQuad(c, tb, 0.f, offset, te.size());
				te.clear();
			}
		}
		endFillCall(c);
	} else if(ncalls == 1) {
		const int offset = c->edges.size();
		for(int i = 0; i < npaths; i++)
			appendN(c->edges, paths[i].fill, paths[i].nfill);
		addFillCall(c, proto, base, offset, nedges, ltrb, 0.5f);
	} else {
		// several independent sub paths ("restart"): one quad each, over its own bounds
		beginFillCall(c, proto, base);
		float cb[4] = { 1e6f, 1e6f, -1e6f, -1e6f };
		int offset = c->edges.size(), count = 0;
		for(int i = 0; i < npaths; i++) {
			appendN(c->edges, paths[i].fill, paths[i].nfill);
			count += paths[i].nfill;
			cb[0] = std::min(cb[0], paths[i].bounds[0]);
			cb[1] = std::min(cb[1], paths[i].bounds[1]);
			cb[2] = std::max(cb[2], paths[i].bounds[2]);
			cb[3] = std::max(cb[3], paths[i].bounds[3]);
			if(i + 1 == npaths || paths[i + 1].restart) {
				cb[0] = std::max(ltrb[0], cb[0]);
				cb[1] = std::max(ltrb[1], cb[1]);
				cb[2] = std::min(ltrb[2], cb[2]);
				cb[3] = std::min(ltrb[3], cb[3]);
				if(cb[0] >= cb[2] || cb[1] >= cb[3])
					c->edges.resize(offset); // invisible part
				else
					appendQuad(c, cb, 0.5f, offset, count);
				offset = c->edges.size();
				count = 0;
				cb[0] = cb[1] = 1e6f;
				cb[2] = cb[3] = -1e6f;
			}
		}
		endFillCall(c);
	}
}

void renderTriangles(void* uptr, NVGpaint* paint, NVGcompositeOperationState compOp, NVGscissor* scissor, const NVGvertex* verts, int nverts)
{
	RhiContext* c = ctxOf(uptr);
	RhiCall call;
	call.blend = blendKey(compOp);
	RhiUniforms u;
	if(!convertPaint(c, u, call.image, paint, scissor, 0)) return;
	u.type = FillText;
	call.uniform = c->uniforms.size();
	c->uniforms.append(u);
	call.vertOffset = c->verts.size();
	call.vertCount = nverts;
	appendN(c->verts, verts, nverts);
	c->calls.append(call);
}

bool ensureBuffer(RhiContext* c, QRhiBuffer*& buf, QRhiBuffer::UsageFlags usage, quint32 size)
{
	if(buf != nullptr && buf->size() >= size) return true;
	quint32 cap = 4096;
	while(cap < size)
		cap *= 2;
	if(buf != nullptr) buf->deleteLater();
	buf = c->rhi->newBuffer(QRhiBuffer::Dynamic, usage, cap);
	if(!buf->create()) {
		delete buf;
		buf = nullptr;
		return false;
	}
	return true;
}

bool ensureEdgeTexture(RhiContext* c, int rows)
{
	if(c->edgeTex != nullptr && c->edgeTexRows >= rows) return true;
	int cap = 16;
	while(cap < rows)
		cap *= 2;
	if(c->edgeTex != nullptr) c->edgeTex->deleteLater();
	c->edgeTex = c->rhi->newTexture(QRhiTexture::RGBA32F, QSize(EDGE_TEX_WIDTH, cap));
	if(!c->edgeTex->create()) {
		delete c->edgeTex;
		c->edgeTex = nullptr;
		return false;
	}
	c->edgeTexRows = cap;
	dropAllSrbs(c); // they point to the old texture
	dropPipelines(c); // built against an srb
	return true;
}

void renderFlush(void* uptr)
{
	RhiContext* c = ctxOf(uptr);
	if(c->cb == nullptr || c->rt == nullptr) {
		renderCancel(uptr);
		return;
	}
	QRhiResourceUpdateBatch* u = c->batch();
	c->pending = nullptr;
	const QSize px = c->rt->pixelSize();

	if(c->rpDesc != c->rt->renderPassDescriptor()) {
		dropPipelines(c);
		c->rpDesc = c->rt->renderPassDescriptor();
	}

	bool ok = !c->calls.isEmpty();
	// edges: rows of EDGE_TEX_WIDTH texels, zero padded
	const int rows = std::max(1, int((c->edges.size() + EDGE_TEX_WIDTH - 1) / EDGE_TEX_WIDTH));
	ok = ok && ensureEdgeTexture(c, rows);
	if(ok && !c->edges.isEmpty()) {
		QByteArray bytes(rows * EDGE_TEX_WIDTH * int(sizeof(NVGvertex)), 0);
		memcpy(bytes.data(), c->edges.constData(), size_t(c->edges.size()) * sizeof(NVGvertex));
		QRhiTextureSubresourceUploadDescription sub(bytes);
		sub.setSourceSize(QSize(EDGE_TEX_WIDTH, rows));
		u->uploadTexture(c->edgeTex, QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, sub)));
	}
	// vertices
	const quint32 vbytes = quint32(c->verts.size() * sizeof(NVGvertex));
	ok = ok && ensureBuffer(c, c->vbuf, QRhiBuffer::VertexBuffer, vbytes);
	if(ok) u->updateDynamicBuffer(c->vbuf, 0, vbytes, c->verts.constData());
	// uniforms, one aligned block per call
	c->ubufStride = c->rhi->ubufAligned(sizeof(RhiUniforms));
	const quint32 ubytes = quint32(c->uniforms.size()) * c->ubufStride;
	QRhiBuffer* oldUbuf = c->ubuf;
	ok = ok && ensureBuffer(c, c->ubuf, QRhiBuffer::UniformBuffer, ubytes);
	if(ok && c->ubuf != oldUbuf) {
		dropAllSrbs(c);
		dropPipelines(c);
	}
	if(ok) {
		QByteArray bytes(int(ubytes), 0);
		const float fbYUp = c->rhi->isYUpInFramebuffer() ? 1.f : 0.f;
		const float ndcYSign = c->rhi->isYUpInNDC() ? 1.f : -1.f;
		for(int i = 0; i < c->uniforms.size(); ++i) {
			RhiUniforms& uu = c->uniforms[i];
			uu.viewSize[0] = c->view[0];
			uu.viewSize[1] = c->view[1];
			uu.fbYUp = fbYUp;
			uu.ndcYSign = ndcYSign;
			uu.edgeTexWidth = EDGE_TEX_WIDTH;
			memcpy(bytes.data() + i * c->ubufStride, &uu, sizeof(RhiUniforms));
		}
		u->updateDynamicBuffer(c->ubuf, 0, ubytes, bytes.constData());
	}

	c->cb->beginPass(c->rt, c->clear, { 1.0f, 0 }, u);
	if(ok) {
		const QRhiCommandBuffer::VertexInput vin(c->vbuf, 0);
		QRhiGraphicsPipeline* current = nullptr;
		for(const RhiCall& call : std::as_const(c->calls)) {
			if(call.custom >= 0) {
				c->customs[call.custom](c->cb, c->rt);
				current = nullptr; // it bound its own pipeline
				continue;
			}
			QRhiGraphicsPipeline* ps = pipelineFor(c, call.blend);
			if(ps == nullptr) continue;
			if(ps != current) {
				// the viewport belongs after the pipeline (Vulkan drew nothing otherwise)
				c->cb->setGraphicsPipeline(ps);
				c->cb->setViewport(QRhiViewport(0, 0, px.width(), px.height()));
				current = ps;
			}
			const QRhiCommandBuffer::DynamicOffset dyn(0, quint32(call.uniform) * c->ubufStride);
			c->cb->setShaderResources(srbFor(c, call.image), 1, &dyn);
			c->cb->setVertexInput(0, 1, &vin);
			c->cb->draw(quint32(call.vertCount), 1, quint32(call.vertOffset), 0);
		}
	}
	c->cb->endPass();
}

int renderCreate(void* uptr)
{
	RhiContext* c = ctxOf(uptr);
	c->vs = loadShader(QStringLiteral(":/nanoplot/shaders/nvg.vert.qsb"));
	c->fs = loadShader(QStringLiteral(":/nanoplot/shaders/nvg.frag.qsb"));
	if(!c->vs.isValid() || !c->fs.isValid()) return 0;
	if(!c->rhi->isTextureFormatSupported(QRhiTexture::RGBA32F) || !c->rhi->isTextureFormatSupported(QRhiTexture::R32F)) {
		qWarning("nvg rhi: float textures not supported");
		return 0;
	}
	c->nearest = c->rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
	c->nearest->create();
	c->dummyTex = c->rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1));
	c->dummyTex->create();
	return ensureEdgeTexture(c, 1) && ensureBuffer(c, c->ubuf, QRhiBuffer::UniformBuffer, 4096) && ensureBuffer(c, c->vbuf, QRhiBuffer::VertexBuffer, 4096);
}

void renderDelete(void* uptr)
{
	RhiContext* c = ctxOf(uptr);
	if(c == nullptr) return;
	dropPipelines(c);
	dropAllSrbs(c);
	for(RhiTexture& t : c->textures) {
		delete t.tex;
		delete t.sampler;
	}
	delete c->edgeTex;
	delete c->dummyTex;
	delete c->nearest;
	delete c->vbuf;
	delete c->ubuf;
	if(c->pending != nullptr) c->pending->release();
	delete c;
}

} // namespace

NVGcontext* nvgRhiCreate(QRhi* rhi, int flags)
{
	RhiContext* c = new RhiContext;
	c->rhi = rhi;
	c->flags = flags | NVG_IS_GPU;

	NVGparams params;
	memset(&params, 0, sizeof(params));
	params.renderCreate = renderCreate;
	params.renderCreateTexture = renderCreateTexture;
	params.renderDeleteTexture = renderDeleteTexture;
	params.renderUpdateTexture = renderUpdateTexture;
	params.renderGetTextureSize = renderGetTextureSize;
	params.renderViewport = renderViewport;
	params.renderCancel = renderCancel;
	params.renderFlush = renderFlush;
	params.renderFill = renderFill;
	params.renderTriangles = renderTriangles;
	params.renderDelete = renderDelete;
	params.userPtr = c;
	params.flags = c->flags;

	NVGcontext* ctx = nvgCreateInternal(&params); // frees c via renderDelete on failure
	return ctx;
}

void nvgRhiDelete(NVGcontext* ctx)
{
	if(ctx != nullptr) nvgDeleteInternal(ctx);
}

void nvgRhiSetTarget(NVGcontext* ctx, QRhiCommandBuffer* cb, QRhiRenderTarget* rt, const float clearColor[4])
{
	RhiContext* c = ctxOf(nvgInternalParams(ctx)->userPtr);
	c->cb = cb;
	c->rt = rt;
	c->clear = QColor::fromRgbF(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
}

QRhiResourceUpdateBatch* nvgRhiResourceBatch(NVGcontext* ctx) { return ctxOf(nvgInternalParams(ctx)->userPtr)->batch(); }

void nvgRhiCustom(NVGcontext* ctx, std::function<void(QRhiCommandBuffer*, QRhiRenderTarget*)> draw)
{
	RhiContext* c = ctxOf(nvgInternalParams(ctx)->userPtr);
	RhiCall call;
	call.custom = c->customs.size();
	c->customs.append(std::move(draw));
	c->calls.append(call);
}

#endif // NANOVG_RHI_IMPLEMENTATION
