// implementations of the header only libraries nanovg.c uses, as in example_sdl.c;
// their implementation parts expect the C library headers to be included already
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FONTSTASH_IMPLEMENTATION
#include "fontstash.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
