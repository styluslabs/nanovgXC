#version 440
// nanovg QRhi backend (nanovg_rhi.cpp), fragment stage. Port of nanovgXC's
// nanovg_vtex.h fragment shader: the exact coverage of a pixel is the sum of
// areaEdge2() over the edges of the path (or of its tile), read from a RGBA32F
// texture; everything else (paints, scissor, summed area text) is unchanged.

layout(location = 0) in vec2 ftcoord;
layout(location = 1) flat in vec2 fedges; // fills: first edge and edge count of the quad

layout(location = 0) out vec4 outColor;

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

layout(binding = 1) uniform sampler2D imageTex;
layout(binding = 2) uniform sampler2D edgeTex;

// must match NVGpathFlags
#define NVG_PATH_EVENODD 0x1
#define NVG_PATH_NO_AA 0x2
#define NVG_PATH_CONVEX 0x4

float coversCenter(vec2 v0, vec2 v1)
{
	// no AA - just determine if center of pixel (0,0) is inside trapezoid
	if(v1.x <= 0.0 || v0.x > 0.0 || v0.x == v1.x)
		return 0.0;
	return v0.y * (v1.x - v0.x) - v0.x * (v1.y - v0.y) > 0.0 ? 1.0 : 0.0;
}

// pixel center is (0,0)
float areaEdge2(vec2 v0, vec2 v1)
{
	if((fillMode & NVG_PATH_NO_AA) != 0)
		return v1.x < v0.x ? coversCenter(v1, v0) : -coversCenter(v0, v1);
	if(v0.y < -0.5 && v1.y < -0.5) // entirely below pixel
		return 0.0;
	vec2 window = clamp(vec2(v0.x, v1.x), -0.5, 0.5);
	float width = window.y - window.x;
	if(width == 0.0) // entirely left or right
		return 0.0;
	if(v0.y > 0.5 && v1.y > 0.5) // entirely above pixel
		return -width;
	vec2 dv = v1 - v0;
	float slope = dv.y / dv.x;
	float midx = 0.5 * (window.x + window.y);
	float y = v0.y + (midx - v0.x) * slope; // y value at middle of window
	float dy = abs(slope * width);
	// see nanovg_gl.h / nanovg_vtex.h for the derivation
	vec4 sides = vec4(y + 0.5 * dy, y - 0.5 * dy, (0.5 - y) / dy, (-0.5 - y) / dy);
	sides = clamp(sides + 0.5, 0.0, 1.0);
	float area = 0.5 * (sides.z - sides.z * sides.y - 1.0 - sides.x + sides.x * sides.w);
	return area * width;
}

float sdroundrect(vec2 pt, vec2 ext, float rad)
{
	vec2 ext2 = ext - vec2(rad, rad);
	vec2 d = abs(pt) - ext2;
	return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - rad;
}

float scissorMask(vec2 p)
{
	vec2 sc = (abs((scissorMat * vec3(p, 1.0)).xy) - scissorExt);
	sc = vec2(0.5, 0.5) - sc * scissorScale;
	return clamp(sc.x, 0.0, 1.0) * clamp(sc.y, 0.0, 1.0);
}

// summed area table text (the float atlas is sampled with texelFetch, no filtering)
float texFetchLerp(vec2 ij, vec2 ijmin, vec2 ijmax)
{
	vec2 ij00 = clamp(ij, ijmin, ijmax);
	vec2 ij11 = clamp(ij + vec2(1.0), ijmin, ijmax);
	float t00 = texelFetch(imageTex, ivec2(ij00.x, ij00.y), 0).r;
	float t10 = texelFetch(imageTex, ivec2(ij11.x, ij00.y), 0).r;
	float t01 = texelFetch(imageTex, ivec2(ij00.x, ij11.y), 0).r;
	float t11 = texelFetch(imageTex, ivec2(ij11.x, ij11.y), 0).r;
	vec2 f = ij - floor(ij);
	float t0 = t00 + f.x * (t10 - t00);
	float t1 = t01 + f.x * (t11 - t01);
	return t0 + f.y * (t1 - t0);
}

float summedTextCov(vec2 st)
{
	ivec2 tex_wh = textureSize(imageTex, 0);
	vec2 ij = st * vec2(tex_wh);
	vec2 ijmin = floor(ij / extent) * extent;
	vec2 ijmax = ijmin + extent - vec2(1.0);
	ij -= vec2(0.999999);
	float dx = paintMat[0][0] / 2.0;
	float dy = paintMat[1][1] / 2.0;
	float s11 = texFetchLerp(ij + vec2(dx, dy), ijmin, ijmax);
	float s01 = texFetchLerp(ij + vec2(-dx, dy), ijmin, ijmax);
	float s10 = texFetchLerp(ij + vec2(dx, -dy), ijmin, ijmax);
	float s00 = texFetchLerp(ij + vec2(-dx, -dy), ijmin, ijmax);
	float cov = (s11 - s01 - s10 + s00) / (255.0 * 4.0 * dx * dy);
	return clamp(cov, 0.0, 1.0);
}

vec4 edgeFetch(int i)
{
	int row = i / edgeTexWidth;
	return texelFetch(edgeTex, ivec2(i - row * edgeTexWidth, row), 0);
}

float coverage(float W)
{
	if((fillMode & NVG_PATH_CONVEX) != 0)
		return 1.0;
	// mod(W, 2.0) written out (exact: halving and doubling are): for mod() SPIRV-Cross
	// emits float..float4 overloads in HLSL, and the HLSL compiler of Wine (vkd3d-shader,
	// Wine 9.0) cannot pick one for scalars, so the plots stayed black there.
	if((fillMode & NVG_PATH_EVENODD) != 0)
		return 1.0 - abs(W - 2.0 * floor(W * 0.5) - 1.0);
	return min(abs(W), 1.0); // non-zero fill
}

void main()
{
	vec2 fpos = fbYUp > 0.5 ? vec2(gl_FragCoord.x, viewSize.y - gl_FragCoord.y) : gl_FragCoord.xy;
	float W = 0.0;
	// the edge range comes with the quad; text (type 5) uses tcoord for the atlas
	int first = int(fedges.x);
	int count = type == 5 ? 0 : int(fedges.y);
	for(int ii = 0; ii < count; ++ii) {
		vec4 edge = edgeFetch(first + ii);
		W += areaEdge2(edge.zw - fpos, edge.xy - fpos);
	}
	float cov = coverage(W);
	vec4 result = vec4(0.0);
	if(type == 1) { // no scissor
		result = innerCol * cov;
	} else if(type == 2) { // solid colour
		result = innerCol * (scissorMask(fpos) * cov);
	} else if(type == 3) { // gradient
		vec2 pt = (paintMat * vec3(fpos, 1.0)).xy;
		float d = clamp((sdroundrect(pt, extent, radius) + feather * 0.5) / feather, 0.0, 1.0);
		vec4 color = texType > 0 ? texture(imageTex, vec2(d, 0.0)) : mix(innerCol, outerCol, d);
		if(texType == 1)
			color = vec4(color.rgb * color.a, color.a);
		result = color * (scissorMask(fpos) * cov);
	} else if(type == 4) { // image
		vec2 pt = (paintMat * vec3(fpos, 1.0)).xy / extent;
		vec4 color = texture(imageTex, pt);
		if(texType == 1)
			color = vec4(color.rgb * color.a, color.a);
		else if(texType == 2)
			color = vec4(color.r);
		color *= innerCol;
		result = color * (scissorMask(fpos) * cov);
	} else if(type == 5) { // text from the atlas, no coverage()
		result = vec4(scissorMask(fpos) * summedTextCov(ftcoord)) * innerCol;
	}
	outColor = result;
}
