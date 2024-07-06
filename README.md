# nanovgXC #

nanovgXC is a small library for rendering vector graphics, based on [nanovg](https://github.com/memononen/nanovg) and under the same permissive zlib license.  The [API](/src/nanovg.h) is nearly identical to nanovg; the major user-facing changes are:

* rendering of arbitrary paths with "exact coverage" antialiasing
  * it is not necessary to specify whether each subpath encloses a solid area or a hole
  * including very thin (a few pixels or less) filled paths, with which nanovg's antialiasing technique has some difficulties
* support for both even-odd and non-zero fill rules
* support for rendering text as paths
* signed distance field text rendering
* gradients with more than 2 stops
* dashed strokes

Cursory testing suggests that nanovgXC is several times faster than skia for GPU and multithreaded CPU rendering - perhaps [Cunningham's Law](https://meta.wikimedia.org/wiki/Cunningham%27s_Law) will inspire more careful testing.  See [example/skia-test/Makefile](/example/skia-test/Makefile).

Three rendering backends are available:

1. [nanovg_vtex](/src/nanovg_vtex.h): Single pass OpenGL backend using vector texture approach (frag shader iterates over all edges for path, read from texture).  Large paths are broken up into tiles.
2. [nanovg_gl](/src/nanovg_gl.h): Two pass OpenGL backend using one of three approaches:
    * GL_EXT_shader_framebuffer_fetch - iOS (also works on many desktop GPUs but with poor performance)
    * GL_ARB_shader_image_load_store/GL_OES_shader_image_atomic - Android (ES 3.1+) and Windows/Linux (GL 4 level hardware)
    * no extensions - switches between two framebuffers for each path (one for accumulating winding, one for final output).  Not as slow as it sounds on desktop GPUs - faster than software renderer for large paths.
3. [nanovg_sw](/src/nanovg_sw.h): software renderer backend based on [nanosvg](https://github.com/memononen/nanosvg) and [stb_truetype](https://github.com/nothings/stb), supporting both "exact coverage" and sub-scanline rendering (see below).  Supports multi-threaded rendering, with each thread rendering a separate region of the output.  This significantly improves performance on desktop platforms, less so on mobile.

### Text Rendering ###

Text can be rendered using the signed distance field (SDF) method (with 4 samples per pixel) or a [summed area table](https://en.wikipedia.org/wiki/Summed-area_table) method.  Pass the `NVG_SDF_TEXT` flag to `nvglCreate()` or `nvgswCreate()` to use SDF text rendering.  Both approaches support continuous scaling of text and arbitrary subpixel positioning of glyphs with a single atlas with similar quality and performance (which is not great for the software renderer).  With SDF rendering, `nvgFontBlur()` can be used to adjust the weight of text.  Text at font sizes above a threshold set by `nvgAtlasTextThreshold()` is rendered directly as paths.  The font size used for the atlas is twice this threshold.  Text at all sizes below the threshold is rendered from the single atlas.

The atlas is managed by `fontstash.h` (modified from the original nanovg fontstash).  To avoid unnecessary duplication, a single fontstash context can be shared between multiple nanovg contexts by passing the `NVG_NO_FONTSTASH` flag to `nvglCreate()` or `nvgswCreate()`, then calling `nvgSetFontStash()`.

The nanovg_sw backend can be used to generate SDF textures when created with the `NVGSW_SDFGEN` flag.  In this mode, the output framebuffer is treated as an array of floats.  See `createFontstash()` in [example_sdl.c](/example/example_sdl.c) for an example.  Compared with stb_truetype, SDF generation is about 10x faster and OpenType (cubic Bezier) outlines are supported.


### "Exact Coverage" ###

The exact coverage antialiasing technique calculates the intersection area of the path and each pixel (represented as a square) to greater than 1/256 accuracy, hence the word "exact".  An early reference to this terminology can be found in [libart](https://people.gnome.org/~mathieu/libart/internals.html).

This technique overestimates the coverage of partially-covered pixels for self-intersecting (i.e. self-overlapping) paths, resulting in incorrect antialiasing for these pixels.  This problem is not unique to GPU implementations of the technique, although with a CPU implementation it is easier to select a different approach for such paths.  The software renderer backend supports another common approach - splitting each scanline into a small number of horizontal "sub-scanlines" and fully including or excluding each path segment from each sub-scanline (while retaining high precision in the horizontal direction).  This can have reduced accuracy, especially for horizontal edges, but does not suffer from the coverage overestimation issue.  Pass the `NVGSW_PATHS_XC` flag to `nvgswCreate` to select the exact coverage algorithm for the software renderer.


### sRGB ###

nanovgXC supports sRGB-aware rendering - if the `NVG_SRGB` flag is passed to `nvglCreate` or `nvgswCreate`, colors will be blended in the linear RGB color space.  Colors are still passed to `nvgRGBA`, etc. in the sRGB color space, as usual.  For the GL backend, the `NVG_IMAGE_SRGB` flag should be passed to `nvgluCreateFramebuffer` (and to the `nvgCreateImage` calls for any images loaded), and `nvgluSetFramebufferSRGB(1)` should be called on desktop platforms.  The SW backend always assumes images are in the sRGB color space (the usual case).

Blending in the linear RGB color space is necessary to obtain the highest antialiasing quality but since most applications do not do this, content rendered with `NVG_SRGB` will look slightly different than when rendered by other applications (e.g., thin lines will look a bit thinner).  Transparency is also affected - this is why the demo looks different with `NVG_SRGB`.


## How to use ##

Add nanovg.c to your sources and then in one source file add:
```C
#define NANOVG_GLES3_IMPLEMENTATION 	// or NANOVG_GL3_IMPLEMENTATION
#include "nanovg_vtex.h"  // or "nanovg_gl.h"
#include "nanovg_gl_utils.h"  // to use framebuffer creation and blitting functions
```
and/or
```C
#define NANOVG_SW_IMPLEMENTATION
#include "nanovg_sw.h"
```

The drawing functions are documented in `nanovg.h`.  For example, to draw a triangle:
```C
#include "nanovg.h"
// ...
NVGcontext* vg = nvglCreate(NVG_SRGB | NVG_AUTOW_DEFAULT);  // or nvgswCreate
// for SW renderer, call nvgswSetFramebuffer to configure output before nvgBeginFrame
nvgBeginFrame(vg);
nvgBeginPath(vg);
nvgMoveTo(vg, 100, 100);
nvgLineTo(vg, 200, 100)
nvgLineTo(vg, 150, 50);
nvgClosePath(vg);
nvgFillColor(vg, nvgRGBA(255,192,0,255));
nvgFill(vg);
nvgEndFrame(vg);
// ... then swap buffers (GL backend) or copy output buffer to screen (SW backend)
```

## Example app ##

The example app uses [SDL2](https://www.libsdl.org/): build SDL2 for your platform then edit the path to SDL library in `Makefile` as needed (the default is `../SDL/Release/`).  For Linux, the appropriate package can be installed instead of building SDL.  The exact versions of SDL used for development are [here](https://github.com/pbsurf/SDL).

A [GLFW](https://www.glfw.org/) version of the sample app is also provided for Windows and Linux.  Replace `make` with `make -f Makefile.glfw` to use.

Building the example app:

The makefile creates the demo executable demo2_sdl(.exe) in Debug/ (make DEBUG=1) or Release/ (make DEBUG=0).

Linux:
* install SDL2 library and headers (libsdl2-dev package on Debian and Ubuntu)
* run `make`

Windows:
* requires Visual C++ (free Visual Studio Community Edition is fine)
* download GNU make built for Windows: http://www.equation.com/servlet/equation.cmd?fa=make and ensure it is available in the path
* in `Makefile`, set `DEPENDBASE` to the parent folder containing all dependencies that `make` should track
* open a Visual Studio command prompt (from the Start Menu) and run `make`

iOS:
* requires XCode to be installed (for iOS SDK) but can be built from command line
* create Makefile.local:
```
PROVISIONING_PROFILE = <path to your .mobileprovision file>
SIGNING_ID = <your code signing ID>
TEAM_ID = <your Apple Developer "team id">
BUNDLE_ID = <bundle identifier for app, e.g., com.styluslabs.demo2_sdl>
```
* run `make`
* the resulting demo2_sdl.app can be installed on a device with [ios-deploy](https://github.com/ios-control/ios-deploy): `ios-deploy --bundle Release/demo2_sdl.app`


## Related Projects ##

* [Pathfinder](https://github.com/servo/pathfinder) - larger and much more sophisticated library from Mozilla that also does exact coverage antialiased path rendering on GPUs
* [piet-gpu](https://github.com/linebender/piet-gpu) - uses compute shaders


## Additional credits ##

* [stb_truetype](https://github.com/nothings/stb) for font loading and rendering
* [stb_image](https://github.com/nothings/stb) for image loading
* OpenGL loader created by [glad](https://github.com/Dav1dde/glad)
