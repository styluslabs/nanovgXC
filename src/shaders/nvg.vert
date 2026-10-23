#version 440
// nanovg QRhi backend (nanovg_rhi.cpp), vertex stage: positions arrive in
// target pixels, top left origin; for text the second pair is the atlas
// coordinate, for fills the first edge and the edge count of the quad (one
// draw covers all tiles of a path). Port of nanovgXC's nanovg_vtex.h vertex shader.

layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 tcoord;

layout(location = 0) out vec2 ftcoord;
layout(location = 1) flat out vec2 fedges;

// keep identical in nvg.vert and nvg.frag; mirrored by RhiUniforms in nanovg_rhi.cpp
layout(std140, binding = 0) uniform buf {
	mat3 scissorMat;
	mat3 paintMat;
	vec4 innerCol;
	vec4 outerCol;
	vec2 scissorExt;
	vec2 scissorScale;
	vec2 extent;
	float radius;
	float feather;
	float strokeMult;
	float strokeThr;
	int texType;
	int fillMode;
	vec2 viewSize;
	float fbYUp; // 1: framebuffer origin bottom left (OpenGL)
	float ndcYSign; // -1 where clip space y points down (Vulkan)
	int type;
	int nedges;
	int edgeOffset;
	int edgeTexWidth;
};

void main()
{
	ftcoord = tcoord;
	fedges = tcoord;
	gl_Position = vec4(2.0 * pos.x / viewSize.x - 1.0, ndcYSign * (1.0 - 2.0 * pos.y / viewSize.y), 0.0, 1.0);
}
