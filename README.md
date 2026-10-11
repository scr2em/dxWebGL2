# dxWebGL2

**Direct3D on WebGL2 (and WebGPU) for Emscripten.** dxWebGL2 implements the Direct3D 8 API (`IDirect3D8`,
`IDirect3DDevice8`, the resources, the fixed-function pipeline, vertex and pixel shaders 1.x, a subset of D3DX 8) on top
of WebGL2, so that a Direct3D 8 renderer can be compiled to WebAssembly and run in the browser largely unchanged. A
WebGPU backend renders the same application through WebGPU instead (optional, see [docs/WEBGPU.md](docs/WEBGPU.md)).
Direct3D 9 is the next front end ([docs/ROADMAP.md](docs/ROADMAP.md)); the library is built in layers for that reason:

```
  your Direct3D 8 code
          |   <d3d8.h> <d3dx8.h>, Direct3DCreate8()          include/dx8, third_party/wine
  +-------v--------------------------------------+
  |  src/d3d8   Direct3D 8 front end (COM objects)|          the "thin" API layer
  +-------v--------------------------------------+
  |  src/core   core: buffers, textures, formats (DXT...), program keys, the backend interface,
  |             fixed-function pipeline -> GLSL / WGSL generators, vs/ps 1.x bytecode -> GLSL / WGSL translators
  +-------v--------------------------------------+
  |  src/core/gl  WebGL2 backend: state cache,  |  src/core/webgpu  WebGPU backend: pipelines, bind groups,
  |               programs, draws               |                   uniform rings, render passes (optional)
  +-------v--------------------------------------+
          WebGL2 / WebGPU (Emscripten, emdawnwebgpu)
```

It started as `WebD3D8`, the renderer backend of the WebAssembly port of Command & Conquer Generals Zero Hour
([scr2em/Generals-Zero-Hours-Web](https://github.com/scr2em/Generals-Zero-Hours-Web)), and was extracted into a
standalone library. License: **GPL-3.0-or-later** ([LICENSE](LICENSE), [NOTICE.md](NOTICE.md)).

> **Browser support.** Developed and tested with Chrome/Chromium (141) on WebGL2 provided by SwiftShader and by GPUs,
> and on WebGPU provided by SwiftShader's Vulkan. Other browsers have not been tested.

## Status

| Area | State |
|---|---|
| Direct3D 8 device: scenes, transforms, lights, materials, fog, clip planes, state blocks, viewports | implemented |
| Fixed-function texture stage cascade (all `D3DTOP_*` except `BUMPENVMAP*`, texture coordinate generation, transforms, projected / 3D / cube textures) | implemented |
| Textures (2D, cube, volume), surfaces, render targets, depth/stencil, vertex and index buffers, UP draws | implemented |
| Texture formats incl. DXT1-5 (hardware S3TC or CPU decode), 16-bit, signed bump formats | implemented |
| Vertex shaders 1.1, pixel shaders 1.0-1.4 (translated to GLSL ES 3.00) | implemented (only `texm3x3diff` is not, see [COVERAGE](docs/COVERAGE.md)) |
| Device loss: WebGL context loss and restore mapped to `D3DERR_DEVICELOST` / `DEVICENOTRESET` / `Reset()` | implemented |
| Multisampling (2-8 samples, resolved on present / read-back) | implemented (clamped to `GL_MAX_SAMPLES`) |
| Diagnostics report (`dxwebgl2_SetReport`, `-dxwebgl2-report`): per-frame call counts, unsupported / approximated features | implemented |
| D3DX 8: math (complete), matrix stack, assembler, buffers, texture/surface creation, loading (DDS/TGA/BMP), filtering | subset |
| Additional swap chains, palettized textures, patches, `ProcessVertices`, vertex blending | not supported (`D3DERR_NOTAVAILABLE` / `D3DERR_INVALIDCALL`, or absent from the caps) |
| WebGPU backend (`-DDXWEBGL2_WEBGPU=ON`, linked with `-sJSPI`): all of the above, shaders included. The default backend, `DXWEBGL2_BACKEND_AUTO`, selects it when the browser offers WebGPU (WebGL2 otherwise); `dxwebgl2_SetBackend` / `-dxwebgl2-backend=webgl2\|webgpu` force one | implemented ([docs/WEBGPU.md](docs/WEBGPU.md)) |
| Direct3D 9 | planned, see [docs/ROADMAP.md](docs/ROADMAP.md) |
| Tests | header ABI, D3DX math, wide strings, project `windows.h` integration, 100+ API checks, 8 rendering modes, 7 feature test modes (context loss, multisampling, shader instructions, emulations, ...) and a frame hash regression in headless Chromium, each on both backends, a picture comparison of the backends, and the default backend's choice in browsers with and without WebGPU |

[docs/COVERAGE.md](docs/COVERAGE.md) lists the API surface method by method.

### Limitations worth knowing

* **Direct3D 8 only for now**, and only the HAL device type, one adapter, one swap chain.
* Capabilities are those of a typical DX8-era card with hardware T&L; shader versions are reported only when you ask
  (`dxwebgl2_SetShaderModel`), which keeps applications on the fixed-function code paths by default.
* No window system: the "window" handle is ignored (`nullptr` works), the cursor is the browser's.
* `Present()` hands the frame to the browser; how that works from threads is subtle, see
  [Threading and presentation](#threading-offscreencanvas-and-present).
* Reading render targets back (`LockRect` on a render target, `GetFrontBuffer`) goes through `glReadPixels` and stalls
  the GPU; managed resources keep a system-memory copy. With the WebGPU backend a read-back waits for `mapAsync` by
  suspending the calling thread (JSPI): it works from a `-sPROXY_TO_PTHREAD` main or other code that can suspend, not
  from a `requestAnimationFrame` callback on the main thread. Creating the device waits for the WebGPU adapter in the
  same way, so an application linked with `-sJSPI` (with the WebGPU backend built) creates its device from `main`, a
  `-sPROXY_TO_PTHREAD` main or a JSPI export, or selects `DXWEBGL2_BACKEND_WEBGL2`.
* Emulation differences are logged once per distinct message (`[dxWebGL2] unsupported: ...` on the console); the
  diagnostics report (`dxwebgl2_SetReport(seconds)`, or `-dxwebgl2-report` on the command line) lists everything
  unsupported or approximated that an application actually used, together with per-frame WebGL call counts.

## Quick start

Requirements: [Emscripten](https://emscripten.org/) (tested with 6.0.11), CMake >= 3.20 and a generator such as Ninja. The
build **only works with Emscripten** and says so otherwise.

```sh
source /path/to/emsdk/emsdk_env.sh
git clone https://github.com/scr2em/dxWebGL2.git && cd dxWebGL2
emcmake cmake -S . -B build -G Ninja
cmake --build build
```

Then serve the examples and open them in Chrome:

```sh
python3 -m http.server -d build/examples 8000      # http://localhost:8000/textured_cube.html
```

### Use it in your project

With `FetchContent` (or `add_subdirectory`):

```cmake
include(FetchContent)
FetchContent_Declare(dxwebgl2 GIT_REPOSITORY https://github.com/scr2em/dxWebGL2.git GIT_TAG main EXCLUDE_FROM_ALL)
set(DXWEBGL2_BUILD_TESTS OFF)
set(DXWEBGL2_BUILD_EXAMPLES OFF)
FetchContent_MakeAvailable(dxwebgl2)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE dxWebGL2::dxwebgl2)
```

or from an installed package (`cmake --install build --prefix <dir>`):

```cmake
find_package(dxWebGL2 REQUIRED)
target_link_libraries(app PRIVATE dxWebGL2::dxwebgl2)
```

```sh
# the Emscripten toolchain only searches its own sysroot for packages: name the prefix in both variables
emcmake cmake -S app -B app/build -DCMAKE_PREFIX_PATH=<dir> -DCMAKE_FIND_ROOT_PATH=<dir>
```

Linking `dxWebGL2::dxwebgl2` adds the include directories for `<d3d8.h>`, `<d3dx8.h>` and
`<dxwebgl2/dxwebgl2.h>`, and the WebGL2 link options `-sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2`
(with `DXWEBGL2_WEBGPU=ON` also `--use-port=emdawnwebgpu`, Emscripten's WebGPU bindings; link the application with
`-sJSPI` to be able to use the WebGPU backend: the default backend then selects it in browsers that offer WebGPU, and the
device must be created from code that can suspend, see [docs/WEBGPU.md](docs/WEBGPU.md)).

| Target | Use |
|---|---|
| `dxWebGL2::dxwebgl2` | the library (also `web_d3d8` inside a build tree, the name of the original) |
| `dxWebGL2::dxwebgl2_pthreads` | the same compiled with `-pthread`: **needed when your application is linked with `-pthread` / `-sPROXY_TO_PTHREAD`**, because every object of a threaded build must have the atomics feature. Built when `DXWEBGL2_BUILD_PTHREADS=ON` (default for a top-level build) |
| `dxWebGL2::d3d8_headers` | only the headers |

CMake options: `DXWEBGL2_BUILD_PTHREADS`, `DXWEBGL2_BUILD_TESTS`, `DXWEBGL2_BUILD_EXAMPLES`, `DXWEBGL2_INSTALL`,
`DXWEBGL2_USE_PROJECT_WINDOWS_H` (+ `DXWEBGL2_WINDOWS_H_TARGET`), `DXWEBGL2_WARNINGS_AS_ERRORS`, `DXWEBGL2_WEBGPU`
(build the WebGPU backend, default `OFF`).

### Code

```cpp
#include <d3d8.h>
#include <d3dx8.h>
#include <dxwebgl2/dxwebgl2.h>
#include <emscripten.h>

IDirect3DDevice8 *g_device;

void Frame() {
    g_device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(32, 48, 64), 1.0f, 0);
    g_device->BeginScene();
    /* ... SetTransform, SetTexture, DrawIndexedPrimitive ... */
    g_device->EndScene();
    g_device->Present(nullptr, nullptr, nullptr, nullptr);
}

int main() {
    dxwebgl2_SetPresentMode(DXWEBGL2_PRESENT_IMPLICIT);   // frames are shown when the callback returns
    IDirect3D8 *d3d = Direct3DCreate8(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = 640;  pp.BackBufferHeight = 480;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;  pp.SwapEffect = D3DSWAPEFFECT_DISCARD;  pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;  pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, nullptr /* no window on the web */,
                      D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &g_device);
    emscripten_set_main_loop(Frame, 0, 1);
}
```

Your page needs `<canvas id="canvas">` (or call `dxwebgl2_SetCanvas("#my-canvas")` before `CreateDevice`). The
[examples](examples/) are complete programs with a page shell: [hello triangle](examples/hello_triangle/README.md) and a
[textured, lit cube](examples/textured_cube/README.md).

## The public C API

`<dxwebgl2/dxwebgl2.h>` (the old `<WebD3D8/WebD3D8.h>` forwards to it, with the old `WebD3D8_*` names as macros):

| Function | Purpose |
|---|---|
| `dxwebgl2_SetCanvas(selector)` | canvas the WebGL2 (or WebGPU) context is created on (`"#canvas"` by default) |
| `dxwebgl2_SetBackend(backend)`, `dxwebgl2_GetBackend()` | GPU API to render through: `DXWEBGL2_BACKEND_AUTO` (default: WebGPU when the library is built with `DXWEBGL2_WEBGPU`, the application is linked with `-sJSPI` and the browser has a WebGPU adapter and device; WebGL2 otherwise, with one log line), `_WEBGL2`, `_WEBGPU` (WebGL2 with a logged reason when WebGPU cannot be used, see [docs/WEBGPU.md](docs/WEBGPU.md)), and the one in use. Also `-dxwebgl2-backend=auto\|webgl2\|webgpu` on the command line |
| `dxwebgl2_SetDesktopSize(w, h)` | the "desktop" reported by `GetAdapterDisplayMode` / mode enumeration |
| `dxwebgl2_SetShaderModel(vs, ps)` | shader versions reported in `D3DCAPS8` (`0x0101`, `0x0104`; 0 = none, the default) |
| `dxwebgl2_SetPresentMode(mode)` | `DXWEBGL2_PRESENT_EXPLICIT` (default) or `DXWEBGL2_PRESENT_IMPLICIT` |
| `dxwebgl2_SetPlatformHooks(&hooks)` | `OnClientSize` (canvas was resized), `OnFramePresented` (after every `Present`) |
| `dxwebgl2_SetContextProxy(mode)` | 1 = forward every GL call to the main thread |
| `dxwebgl2_SetDebug(flags)` | GL error checks, unsupported-feature log, periodic frame report, state toggles for bisecting |
| `dxwebgl2_SetReleaseTextureShadows`, `dxwebgl2_SetDisableS3TC` | memory / testing switches |
| `dxwebgl2_LookupProc(name)` | `GetProcAddress("D3D8.DLL", "Direct3DCreate8")` replacement |
| `dxwebgl2_GetRendererString()`, `dxwebgl2_GetVersionString()` | information |

## Using it in an existing Win32 code base

The Direct3D headers need `DWORD`, `HRESULT`, `RECT`, `GUID`, `IUnknown`... If your project has its own `windows.h`
(a Win32 compatibility layer), tell dxWebGL2 and its headers add nothing on top:

```cmake
set(DXWEBGL2_USE_PROJECT_WINDOWS_H ON)
set(DXWEBGL2_WINDOWS_H_TARGET my_win32_headers)   # interface target with your windows.h include dirs and flags
```

(or define `DXWEBGL2_HAVE_WINDOWS_H`, or include your `<windows.h>` before any dxWebGL2 header: it is recognized).
Nothing in the library depends on the size of `wchar_t`: `WCHAR` is a UTF-16 code unit, so `-fshort-wchar` projects and
projects with a 32-bit `wchar_t` link against the same binary. The Direct3D 8 headers are **not** Microsoft's: they are
Wine's, patched, plus our own D3DX 8 headers, verified to have the same ABI, see [docs/HEADERS.md](docs/HEADERS.md).
Code written against `WebD3D8` keeps compiling through `<WebD3D8/WebD3D8.h>`; [docs/SYNC.md](docs/SYNC.md) lists the
exact steps for the game port.

## Threading, OffscreenCanvas and Present

* **Main thread rendering** (what the examples do): link `dxWebGL2::dxwebgl2`, call
  `dxwebgl2_SetPresentMode(DXWEBGL2_PRESENT_IMPLICIT)` and call `Present()` from a `requestAnimationFrame` /
  `emscripten_set_main_loop` callback: the browser shows the canvas when the callback returns.
* **Rendering from a pthread** (a game loop that never returns to the event loop, `-sPROXY_TO_PTHREAD=1`): link
  `dxWebGL2::dxwebgl2_pthreads`, build the application with `-sOFFSCREENCANVAS_SUPPORT=1` (and
  `-sOFFSCREENCANVASES_TO_PTHREAD=#canvas` when the thread is not the proxied main one) and serve the page with
  `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp` (needed for
  `SharedArrayBuffer`; `tests/runner/serve.mjs` is a small server that does it). The default present mode is
  *explicit swap control*: the context is created with `explicitSwapControl` and `Present()` commits the frame. If
  the build or browser cannot do that the device falls back to implicit presentation by itself.
* **Frames from a loop that never yields**: Chromium does not show the frames of a pthread that renders to an
  OffscreenCanvas without ever returning to its event loop, with or without explicit swap control. Yield once per frame
  from the `OnFramePresented` hook: link with `-sJSPI` (or `-sASYNCIFY`) and call `emscripten_sleep(0)` there. The
  runtime test's `--yield` and `--frames=N` modes exercise exactly this.
* `dxwebgl2_SetContextProxy(1)` forwards every GL call to the main thread (`-sOFFSCREEN_FRAMEBUFFER`): slow, but works
  without OffscreenCanvas.
* The library itself is not thread safe: use a device from one thread at a time.

## Build, test, examples

```sh
scripts/test.sh                  # configure, build, run everything (needs Emscripten, Ninja, node, Playwright + Chromium)
scripts/test.sh --no-browser     # skip the headless Chromium parts
scripts/test.sh --backend webgpu # the browser tests on one backend only (webgl2, webgpu; default: both, then AUTO)
```

`scripts/test.sh` builds with the WebGPU backend and runs the repository checks (SPDX headers, architecture layering,
interface golden file), the node tests (header ABI, D3DX math, wide strings, integration with a project `windows.h`)
and, in headless Chromium, once per backend: the runtime test in eight modes, the feature tests (context loss,
multisampling, shader instructions, D3DX filters, state caching, the emulations of the WebGPU backend, diagnostics),
the frame hash regression and both examples; then it compares the pictures of the two backends
(`tests/runner/compare_backends.mjs`) and checks the default backend: `DXWEBGL2_BACKEND_AUTO` renders the WebGPU pictures in
a browser with WebGPU and the WebGL2 pictures in one without, and small programs built with and without the WebGPU
backend and JSPI get the backend they should (`tests/runner/run_auto.mjs`). Every run names its backend
(`--backend=webgl2|webgpu`); without the option a runner leaves the choice to the library's default. Playwright is found through `NODE_PATH` (default `/opt/node22/lib/node_modules`)
and the browser through `PLAYWRIGHT_BROWSERS_PATH` (default `/opt/pw-browsers`) or `CHROMIUM_PATH`. See
[docs/CI.md](docs/CI.md) for running it in continuous integration (no workflow files are shipped).

## Documentation

* [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) - layers, coordinate conventions, how a draw call is processed
* [docs/COVERAGE.md](docs/COVERAGE.md) - API coverage, method by method
* [docs/HEADERS.md](docs/HEADERS.md) - header provenance, licenses, ABI verification, Win32 base
* [docs/ROADMAP.md](docs/ROADMAP.md) - the Direct3D 9 plan
* [docs/WEBGPU.md](docs/WEBGPU.md) - the backend interface and the WebGPU backend plan
* [docs/SYNC.md](docs/SYNC.md) - relation to the game port, the sync script, switching the game to this library
* [docs/CI.md](docs/CI.md), [CONTRIBUTING.md](CONTRIBUTING.md), [NOTICE.md](NOTICE.md)

## License

GPL-3.0-or-later, Copyright (C) 2026 dxWebGL2 contributors. The Wine headers in `third_party/wine` are
LGPL-2.1-or-later. See [NOTICE.md](NOTICE.md).
