# WebGPU backend

Status: **step 1 of the plan is done: the backend seam.** The Direct3D 8 front end renders through an abstract
`Backend` (`src/core/backend.h`); the WebGL2 code lives behind it in `src/core/gl/`. The WebGPU backend itself is not
written yet: `dxwebgl2_SetBackend(DXWEBGL2_BACKEND_WEBGPU)` logs a message and uses WebGL2, and
`DXWEBGL2_BACKEND_AUTO` (the default) selects WebGL2.

The goal is a second backend, selectable at run time (automatically WebGPU when the browser offers it, WebGL2
otherwise), that renders the same Direct3D 8 application unchanged. This document is the design: the interface, how
Direct3D 8 state maps onto WebGPU objects, what WebGPU lacks and how it is emulated, shader generation, threading and
asynchrony, the selection API, and the test plan.

```
  your Direct3D 8 code
          |
  +-------v-----------------------------------------------+
  |  src/d3d8   Direct3D 8 front end: state, COM objects,  |   translates state into neutral descriptions
  |             program keys, uniform values               |   (PipelineState, ProgramKey, DrawCall, ...)
  +-------v-----------------------------------------------+
  |  src/core   backend.h (the interface), resources with  |   BufferStorage, TextureBase, SurfaceStorage:
  |             system memory copies, formats, GLSL        |   CPU copies + backend handles
  |             generation, shader translators             |
  +-------v-------------------------+---------------------+
  |  src/core/gl   WebGL2 backend   |  src/core/webgpu    |   (WebGPU: step 2)
  +---------------------------------+---------------------+
```

## 1. The backend interface

`src/core/backend.h` is what a Direct3D front end asks of the GPU API. It was cut at the level WebGPU works at, not at
the level of GL calls:

| Area | Interface | Notes |
|---|---|---|
| Device | `Initialize(w, h)`, `Shutdown()`, `Caps()` (`GpuCaps`), `Description()`, `CanvasSize()` | the context/device on the configured canvas |
| Back buffer | `CreateBackBuffer(w, h, samples)`, `DestroyBackBuffer()`, `SetGammaRamp()`, `Present()`, `ReadBackBuffer()` | the back buffer is a texture of the backend; `Present()` draws it to the canvas (with the gamma ramp) |
| Device loss | `IsContextLost()`, `HasContextEvent()`/`ClearContextEvent()`, `OnContextLost()`, `RestoreContext()`, test aids | the front end keeps the Direct3D semantics (`D3DERR_DEVICELOST`, `DEVICENOTRESET`, `Reset`) |
| Resources | `CreateBuffer/DestroyBuffer/WriteBuffer`, `CreateTexture/DestroyTexture/WriteTexture/ReadTexture`, `CreateSurface/DestroySurface/ReadSurface`, `GetSampler(SamplerDesc)` | opaque handles (`GpuBuffer`, `GpuTexture`, `GpuSurface`, `GpuSampler`); data is handed over in the Direct3D memory layout of the `D3DFORMAT`, the backend converts what its API needs |
| Programs | `CreateProgram(ProgramKey, vs object, ps object)`, `DestroyProgram()` | the backend generates the code for its API from the key (GLSL today, WGSL next) |
| Rendering | `SetRenderTargets(color, depth)`, `Clear(ClearCall)`, `Draw(DrawCall)` | one call per draw with everything it depends on |
| Cache hints | `InvalidateState()`, `InvalidateVertexInput()` | the front end reset its state / changed its streams |

The key types:

* **`DrawCall`**: topology (including fans), fill mode, counts, base vertex; the full `PipelineState`; the `Program`;
  the uniform version counters and a `UniformSource`; one `TextureBinding` (texture handle, sampler handle, base mip
  level) per stage; the `VertexLayout`, the 16 streams (`BufferStorage` + stride) or user memory; the index buffer or
  user indices. Nothing is applied piecemeal: a WebGPU backend turns it into (pipeline, bind groups, vertex/index
  buffers, draw), the WebGL2 backend diffs it against its state cache and issues only the GL calls that differ.
* **`PipelineState`**: blend (factors, op, enable), depth (test, function, write, range), stencil (test, function,
  reference, masks, operations), culling and front face, colour write mask, polygon offset, viewport, scissor. Neutral
  enumerations (`CompareFunc`, `BlendFactor`, `BlendOp`, `StencilOp`, `FrontFace`), filled after a `memset` so that it
  can be hashed and compared with `memcmp`.
* **`UniformSource` / `UniformSink`**: the front end produces the values of a uniform group
  (`Device::WriteUniforms`), the backend receives them through a sink (`Vec4`, `Vec2`, `Float`, `Mat4` per
  `UniformId`). The WebGL2 sink calls `glUniform*`; a WebGPU sink writes into a uniform buffer at the offsets of its
  WGSL layout. Which groups are sent is decided by the backend from the version counters in the call.
* **`RenderTargetRef`**: what a draw renders into, referring to the front end's *storage objects* (`TextureBase` +
  face/level, `SurfaceStorage`, or the back buffer), so that the binding survives the recreation of GPU objects after a
  device loss.
* **`SamplerDesc`**: filters, address modes and anisotropy in Direct3D numbering (identical in Direct3D 8 and 9); the
  backend caches sampler objects per description, the front end keeps the handle per stage.
* **`GpuObject`**: base of every core object that owns backend objects (`BufferStorage`, `TextureBase`,
  `SurfaceStorage`). The backend keeps the list and tells every object when the context is lost and when a new one is
  ready, so it can recreate its GPU objects from its system memory copy (WebGL context loss today, `GPUDevice.lost`
  tomorrow).

The CPU emulation of primitive types and fill modes that both backends need (`ExpandTriangles`, `TrianglesToLines`)
is part of the interface module.

### What the WebGL2 backend keeps internally

The state cache (bound buffers/textures/samplers/program/framebuffer, the applied `PipelineState`, clear values), the
vertex array object cache, the streaming buffers of user pointer draws, the framebuffer object with its attachment
keys and the substitute depth buffers, the white textures for stages without one, the present program and the
multisample resolve. Objects of a lost context are recognised by a context *generation* stored in every GL object, so
that nothing of a dead context is deleted.

### Known GL leftovers in the interface (to revisit in step 2)

* `InvalidateVertexInput()` exists only to keep the WebGL2 vertex array signature reset where the device did it; a
  WebGPU backend ignores it.
* Uniform versions are tracked per program (`Program::ver`), which is what GL needs (uniforms live in the program).
  WebGPU keeps one buffer per group and can track versions per group; the interface allows both.
* The format table (`core/format.cpp`) carries the WebGL2 storage columns and the upload conversion
  (`PrepareForGL`); the WebGPU backend gets its own format table and conversions in `src/core/webgpu/`.

## 2. Mapping the state to WebGPU

### Render pipelines

`GPURenderPipeline`s are created lazily and cached by a key made of:

* the program (the two WGSL shader modules, which come from the `ProgramKey`),
* the vertex input: `VertexLayout::id` plus the stride of every stream used (`GPUVertexBufferLayout`s),
* the topology (`point-list`, `line-list`, `line-strip`, `triangle-list`, `triangle-strip`; fans are expanded, see
  below) and the strip index format,
* from `PipelineState`: blend enable/factors/operation, colour write mask, depth compare/write, stencil
  compare/operations/read mask/write mask (front = back for Direct3D 8), cull mode and front face, depth bias
  (`ZBIAS` -> `depthBias`, `depthBiasSlopeScale`),
* the formats of the colour and depth attachments and the sample count.

Dynamic state stays outside the key: viewport (`setViewport`, including the depth range), scissor
(`setScissorRect`), stencil reference (`setStencilReference`), and in Direct3D 9 the blend constant. A small
two-level cache (hash of the key, then a full compare) keeps the per-draw cost at one hash when nothing changes; the
pipeline creation itself is the expensive part and happens once per combination (applications use a few hundred).

### Bind groups

* group 0: uniforms. One `GPUBuffer` with a ring of uniform blocks per frame; each uniform group is a slice written
  with `queue.writeBuffer` only when its version changed; bound with dynamic offsets so that changing a group does
  not create a bind group.
* group 1: textures and samplers of stages 0-7. The layout follows the stage texture types of the `ProgramKey`
  (2D, cube, 3D view dimensions); stages without a texture (or whose texture is the render target) get 1x1 white
  dummies; `TextureBinding::baseLevel` (`D3DTSS_MAXMIPLEVEL`) selects a texture view with that `baseMipLevel`. Bind
  groups are cached by (texture views, samplers).

### Uniforms

The uniform groups and names of `UniformId` stay; the WGSL emitter declares one struct per group with explicit
offsets (16-byte vec4 rows, matrices as 4 vec4s, the float arrays `u_lod` padded to vec4 or packed into vec4s), and the
WebGPU `UniformSink` writes `Vec4/Vec2/Float/Mat4` data at those offsets. `maxUniformBufferBindingSize` (64 KB) is far
above what Direct3D 8 needs (96 vertex shader constants).

### Buffers and textures

* Vertex and index buffers: `GPUBuffer` with `VERTEX|INDEX|COPY_DST`; `WriteBuffer` -> `queue.writeBuffer` (ranges
  widened to 4-byte multiples, the CPU copy makes that free). User pointer draws stream through a ring buffer, as in
  WebGL2.
* Textures: `GPUTexture` with `TEXTURE_BINDING|COPY_DST` (`|RENDER_ATTACHMENT` for render targets);
  `WriteTexture` -> `queue.writeTexture` (no row alignment requirement, unlike buffer copies), the data converted to the
  WebGPU format first (see the format table below). Surfaces (render targets, depth buffers) are textures with
  `RENDER_ATTACHMENT`.

### Render passes

Draws are recorded into a `GPURenderPassEncoder` that stays open while the render targets do not change. A pass ends
on `SetRenderTargets` with other targets, on a read-back, on a texture upload of an attachment, and on `Present`. Clears
at the start of a pass become `loadOp: 'clear'`; clears in the middle of a pass, and clears restricted to rectangles
(`Clear` with `D3DRECT`s, or a viewport smaller than the target), draw a quad with the clear values (colour, depth via
the quad's z, stencil via the stencil reference with `replace`). The command encoder is submitted at `Present` and
before read-backs.

## 3. Direct3D 8 features WebGPU lacks, and their emulation

| Feature | WebGPU | Emulation |
|---|---|---|
| Triangle fans (`D3DPT_TRIANGLEFAN`) | no fan topology | expanded to a triangle list (`ExpandTriangles`); for static index buffers the expansion is cached per (buffer, range, version) |
| Point sprites, point size (`D3DRS_POINTSIZE*`, `POINTSPRITEENABLE`, `POINTSCALE*`) | points are 1 pixel | point lists are drawn as instanced quads: the vertex shader reads the point's vertex from a storage buffer (or a per-instance vertex buffer), computes the size as today (`u_pt`) and offsets the corners in clip space; sprite texture coordinates from the corner |
| Fill modes `WIREFRAME` / `POINT` | no polygon mode | CPU rebuild to line/point lists, as WebGL2 does today |
| Line width | 1 only | as WebGL2 (1 pixel) |
| `D3DTADDRESS_BORDER` | no border colour | shader emulation (already in the generated code: `StageKey::borderMask`, `u_border`), `clamp-to-edge` sampler |
| `D3DTADDRESS_MIRRORONCE` | no | `mirror-repeat` (as WebGL2) |
| 16-bit colour formats (`R5G6B5`, `A1R5G5B5`, `X1R5G5B5`, `A4R4G4B4`, `X4R4G4B4`) | no packed 16-bit formats | expanded to `rgba8unorm` on upload; such render targets are `rgba8unorm` (the application still sees its format; read-backs convert back) |
| `R8G8B8`, `X8R8G8B8` | no 24-bit, no "X" formats | `rgba8unorm` with alpha set to 1 on upload (`A8R8G8B8` can be uploaded unconverted as `bgra8unorm`) |
| `L8`, `A8`, `A8L8`, `A4L4` (luminance/alpha) | no luminance/alpha formats | expanded to `rgba8unorm` on upload (alternative: `r8unorm`/`rg8unorm` plus a per-stage swizzle in the program key) |
| `V8U8`, `Q8W8V8U8` | | `rg8snorm`, `rgba8snorm` |
| DXT1-5 | `bc1/bc2/bc3-rgba-unorm` behind the optional feature `texture-compression-bc` | requested at `requestDevice`; without it the existing CPU decoder (`DecodeDXT`) produces RGBA8, as for WebGL2 without S3TC |
| Depth formats | `depth16unorm`, `depth24plus`, `depth24plus-stencil8`, `depth32float` | `D16` -> `depth16unorm`; `D24X8`, `D32` -> `depth24plus`; `D24S8`, `D24X4S4`, `D15S1` -> `depth24plus-stencil8`; lockable depth is not readable (as today) |
| Constant vertex attributes (inputs the vertex format does not provide read 0,0,0,1) | every shader input needs a buffer | the WGSL emitter declares only the attributes of the vertex layout and uses (0,0,0,1) for the others, so the program key gains the mask of provided attributes |
| Vertex strides/offsets not multiple of 4 | required to be multiples of 4 | such streams are repacked on the CPU (rare: Direct3D 8 vertex formats are float/DWORD sized) |
| Flat shading (first vertex) | `@interpolate(flat)` uses the first vertex, like Direct3D | none needed (WebGL2 needs `WEBGL_provoking_vertex` or the triangle rotation) |
| Base vertex for indexed draws | `drawIndexed(..., baseVertex)` | none needed |
| Multisampling 2-16 samples | 1 or 4 samples | `D3DMULTISAMPLE_2_SAMPLES`/`4` -> 4, higher counts -> 4 (reported back like the WebGL2 clamp); resolve through `resolveTarget` |
| Clear restricted to rectangles / the viewport | `loadOp: clear` clears the whole attachment | quad draw (see render passes) |
| Alpha test, fog, user clip planes, texture coordinate generation, the stage cascade | no fixed function | generated shaders, as for WebGL2 (clip planes with `discard`, or `clip_distances` where available) |
| Gamma ramp | | applied by the present pass, as for WebGL2 |
| Synchronous read-back (`LockRect` on render targets, `GetFrontBuffer`) | `mapAsync` only | `copyTextureToBuffer`, submit, then wait for `mapAsync`: suspends the calling thread (JSPI, see section 5) |
| Texture that is the current render target and also sampled | validation error | the stage gets the dummy texture (already the front end's rule) |
| Explicit swap control (`DXWEBGL2_PRESENT_EXPLICIT`) | no commit; `wgpuSurfacePresent` is not supported by Emscripten | the frame is shown when the thread yields (the `OnFramePresented` hook with `emscripten_sleep(0)`, as needed with WebGL2 from a pthread) |

Clip space and the "flipped" rendering need less work than with GL: WebGPU's depth range is [0, 1] like Direct3D's
(no z remapping in the vertex stage) and its framebuffer y axis points down like Direct3D's, so the WGSL vertex
epilogue does not negate y, `FrontFace` is inverted by the backend, and the present pass does not flip. The half
pixel offset of Direct3D 8 pixel centres stays.

## 4. Shader generation

WGSL emitters are added next to the GLSL ones and share everything that is not syntax:

* the `ProgramKey` (plus the attribute mask above),
* the token stream parsing of vs.1.1 / ps.1.x. The translators currently decode tokens straight into GLSL text; the
  ROADMAP's milestone M2 ("split decode from emit": token stream -> `ShaderIR` -> emitter) is done first, the SM1 GLSL
  output staying byte-identical, then a WGSL emitter is added on the IR. The Direct3D 9 SM2/3 decoder later feeds both.
* the fixed-function generator (`fixedfunction.cpp`) gets its emission split the same way: the decisions (which
  lights, which texture coordinates, the stage cascade) are shared, the expressions are emitted per language.

WGSL specifics the emitter has to respect: uniforms are struct members in bind group 0, textures and samplers are
separate bindings, `textureSample` only in uniform control flow (the generated code samples unconditionally, alpha
test discards after sampling; dynamic branches use `textureSampleLevel`/`textureSampleGrad`), no point size output
(sprites are emulated), clip-space conventions as described above, `@builtin(front_facing)` for `vFace` (SM3).

## 5. Threading, presentation and asynchrony

Verified in this environment (headless Chromium from `/opt/pw-browsers/chromium`, localhost page):

* WebGPU is exposed with `--enable-unsafe-webgpu --enable-features=Vulkan --use-vulkan=swiftshader
  --use-webgpu-adapter=swiftshader --use-angle=swiftshader`; the adapter reports vendor `google`, architecture
  `swiftshader`.
* It also works in a dedicated worker rendering to a transferred `OffscreenCanvas` (preferred canvas format
  `bgra8unorm`), which is the situation of a `-sPROXY_TO_PTHREAD` application.
* Without the Vulkan flags `requestDevice` fails with "A valid external Instance reference no longer exists".
* Emscripten 6.0.11 compiles `#include <webgpu/webgpu.h>` with `--use-port=emdawnwebgpu`. Its `wgpuInstanceWaitAny`
  (blocking on a future) is implemented only when the build can suspend (`-sASYNCIFY` or `-sJSPI`; otherwise it
  aborts), `wgpuSurfacePresent` is not supported (frames are presented when the thread returns to the event loop).

`requestAdapter`, `requestDevice` and `mapAsync` are asynchronous, while `IDirect3D8::CreateDevice` and
`LockRect` are synchronous. Options considered:

1. **JSPI** (`-sJSPI`): the calling thread suspends in `wgpuInstanceWaitAny` until the promise settles. No cost for
   code that does not suspend; Chromium (the supported browser) ships it; the test programs already link with
   `-sJSPI` for the per-frame yield. Calls that may suspend must come from a JSPI-wrapped entry (main with
   `-sPROXY_TO_PTHREAD`, or callbacks Emscripten wraps).
2. **ASYNCIFY**: works in more browsers but instruments every function that can be on the stack during a suspension:
   larger and slower code everywhere, for a library whose cost is per-call overhead.
3. **Initialise in JavaScript before `main`** (a pre-created `GPUDevice` handed to the module): solves device creation
   only; read-backs still need to wait, and Emscripten deprecates the pre-initialised device path.
4. **Asynchronous read-back API**: not possible behind Direct3D 8's synchronous `Lock`.

**Decision: JSPI.** The WebGPU backend is only offered when the build can suspend (`emscripten_has_asyncify()` returns
2 for JSPI, 1 for ASYNCIFY, which also works but is not recommended); `DXWEBGL2_BACKEND_AUTO` falls back to WebGL2
otherwise, and an explicit `DXWEBGL2_BACKEND_WEBGPU` request logs why it is not available. Device creation waits for
`requestAdapter`/`requestDevice` (with `texture-compression-bc` requested when the adapter has it), read-backs wait for
`mapAsync`. Rendering itself never waits.

Presentation: the canvas (`#canvas` or `dxwebgl2_SetCanvas`) gets a `webgpu` context configured with
`navigator.gpu.getPreferredCanvasFormat()` (`bgra8unorm` here) and `alphaMode: 'opaque'`. The back buffer is a
texture of its own (read-backs, `GetFrontBuffer`, the gamma ramp and multisampling need it); `Present` draws it into
`getCurrentTexture()` with the gamma pass and submits. As with WebGL2 from a pthread, the browser shows the frame when
the thread yields; applications that never return to the event loop yield from the `OnFramePresented` hook. Device
loss: `GPUDevice.lost` sets the same event flag the WebGL2 backend's canvas listeners set, and the front end runs the
same lost / not reset / `Reset()` sequence; `RestoreContext()` requests a new device.

## 6. Run-time selection

Public API (`include/dxwebgl2/dxwebgl2.h`, implemented):

```c
enum dxwebgl2_Backend {
    DXWEBGL2_BACKEND_AUTO = 0,     /* WebGPU when available, WebGL2 otherwise (default; WebGL2 until step 2) */
    DXWEBGL2_BACKEND_WEBGL2 = 1,
    DXWEBGL2_BACKEND_WEBGPU = 2,   /* not implemented yet: logs a message and uses WebGL2 */
};
void dxwebgl2_SetBackend(int backend);   /* before CreateDevice() */
int dxwebgl2_GetBackend(void);           /* backend of the device created last; AUTO before the first device */
```

`CreateBackend()` (`src/core/backend.cpp`) is the only place that decides. In step 2, `AUTO` picks WebGPU when the
build can suspend, `navigator.gpu` exists on the rendering thread, an adapter is returned and `requestDevice`
succeeds; any failure falls back to WebGL2 with a logged reason (and a diagnostics hit). A command line option
`-dxwebgl2-backend=auto|webgl2|webgpu` will join the other `-dxwebgl2-*` options. `dxwebgl2_GetRendererString()`
already reports the backend ("WebGL2: ..."; "WebGPU: <adapter>" later). The old `WebD3D8_*` compatibility names are
not extended with these functions (they did not exist in that library).

## 7. Testing

* **Every rendering test on both backends.** The runners (`run_test.mjs`, `run_features.mjs`, `run_regression.mjs`,
  `run_example.mjs`) get `--backend=webgl2|webgpu`, passed to the page and to `dxwebgl2_SetBackend`; the browser is
  started with the WebGPU flags listed in section 5 (harmless for WebGL2). `scripts/test.sh` runs each mode twice.
  The test programs report the active backend (`dxwebgl2_GetBackend`) so that a silent fallback fails the WebGPU run.
* **Probes**: the runtime test's probe table (`expected_probes.inc`, tolerance 6 per channel) is shared; where the
  backends legitimately differ (rasterisation rules at polygon edges, DXT decoding, point sprite corners), a per-backend
  override table records the difference with a comment.
* **Image comparison between backends**: the screenshots of both runs are compared with a tolerance (per-pixel
  channel difference and a maximum share of differing pixels), so that differences show up as a picture diff, not
  only as a hash change.
* **Frame hashes stay per backend**: `reference_hashes.json` gets one entry per (scene, backend); identical output
  across GPUs is only expected within one backend on SwiftShader.
* **Call counts**: the runtime test prints `STATS` lines (WebGL calls per category for the last frame and the whole
  run, from `dxwebgl2_GetStats`); the WebGPU backend counts its API calls in the same categories (draw, state = pipeline
  and dynamic state, bind = bind groups and buffers, upload = `writeBuffer`/`writeTexture`), plus pipeline and bind
  group creations.
* **Feature tests**: device loss (WebGPU: `GPUDevice.destroy()` as the test aid), multisampling (4x only), the
  emulations of section 3 (fans, point sprites, 16-bit and luminance formats, border colour, rectangle clears,
  read-backs) get dedicated checked scenes.
* The node tests, the layering check (which enforces that only `src/core/gl` and later `src/core/webgpu` include a GPU
  API) and the package consumers are backend independent.

## 8. Step 1: what was done and how it was verified

* `src/core/backend.h/.cpp`: the interface, the `GpuObject` registry, backend selection, primitive emulation helpers.
* `src/core/gl/`: the WebGL2 backend (`gl.h` with the GLES headers and the call counters, `gl_backend.h/.cpp`,
  `gl_resources.cpp`, `gl_program.cpp`, `gl_draw.cpp`), made of the code that was in `d3d8/device.cpp`,
  `d3d8/device_draw.cpp`, `d3d8/texture.cpp`, `core/gl_state.*`, `core/program.cpp`, `core/texture_storage.cpp` and
  `core/buffer_storage.cpp`.
* `src/d3d8` makes no WebGL call and uses no GL type; `src/core` outside `src/core/gl` includes no GL header (except
  the format table's enum values) and calls nothing. `tools/check_layering.py` enforces both.
* Zero behaviour change: the full `scripts/test.sh` passes before and after, the frame hashes are identical, and the
  per-category WebGL call counts and uploaded bytes of the runtime test scenes (`STATS` lines) are identical, for
  example (SwiftShader, no `WEBGL_provoking_vertex`, no base vertex extension):

  | Scene (90 frames) | WebGL calls, last frame (draw/state/bind/uniform/upload/attrib) | WebGL calls, whole run | before = after |
  |---|---|---|---|
  | default | 101 (12/20/34/16/10/9) | 9338 | yes |
  | `--shaders` | 209 (20/25/70/35/26/33) | 19094 | yes |
  | `--scene2` | 144 (14/40/33/25/17/15) | 13223 | yes |
  | `--scene3` | 188 (15/34/72/19/48/0) | 17123 | yes |

## 9. Next steps (step 2 onwards)

1. `src/core/webgpu/`: device creation with JSPI, caps, back buffer and present pass, buffers, textures (format table
   and conversions), samplers, render passes and clears, pipeline and bind group caches, uniform buffer ring, draws
   (fan expansion), read-backs.
2. ShaderIR split of the SM1 translators (GLSL output unchanged), WGSL emitters for the fixed-function pipeline and the
   translated shaders.
3. Point sprite emulation, 16-bit/luminance/X formats, DXT through `texture-compression-bc`, multisample clamp.
4. Test runners with `--backend`, cross-backend image comparison, per-backend hashes; `AUTO` switched to prefer WebGPU
   once the suites pass.
