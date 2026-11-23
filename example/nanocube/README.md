# nanocube

Minimal Qt `QRhiWidget` example for the nanovg QRhi backend (`src/nanovg_rhi.h`):
a shaded cube that spins continuously. All drawing goes through nanovg, the faces
are filled as paths.

- drag with the left mouse button to turn the cube, on release it keeps spinning
- release while holding still to stop it
- double click to restore the start spin

## Requirements

- Qt 6.7 or newer (`QRhiWidget`) with the Qt Shader Tools module: the shaders in
  `src/shaders` are compiled by `qsb` during the build
- a C++17 compiler

## Build

Out of source, with `qmake` from the Qt installation (e.g. `~/Qt/6.11.2/gcc/bin/qmake`):

```bash
mkdir build && cd build
qmake path/to/nanovgXC/example/nanocube/nanocube.pro
make
```

Or open `nanocube.pro` in Qt Creator.

## Run

```bash
./nanocube [render backend]
```

The optional render backend is one of `opengl`, `vulkan`, `d3d11`, `d3d12`, `metal`;
without it Qt picks the platform default (OpenGL on Linux, Direct3D 11 on Windows,
Metal on macOS). The backend in use is printed at start, e.g. `render backend Vulkan`.

## Files

- `main.cpp` the widget; compiles the backend (`NANOVG_RHI_IMPLEMENTATION`)
- `nanovg_impl.c` implementations of fontstash and stb_image used by `nanovg.c`
- `nanocube.qrc` the font `example/fonts/Roboto-Regular.ttf` (SIL OFL, `example/LICENSE_OFL.txt`)
- from the repository: `src/nanovg.c`, `src/nanovg.h`, `src/fontstash.h`,
  `src/nanovg_rhi.h`, `src/shaders/nvg.vert`, `src/shaders/nvg.frag`, `example/stb`
