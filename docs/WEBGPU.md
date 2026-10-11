# WebGPU backend

Status: **step 3 is done: the default backend, `DXWEBGL2_BACKEND_AUTO`, selects WebGPU** when the library is built with
it, the application can wait for the device (JSPI) and the browser gives an adapter and a device, and WebGL2 otherwise
(section 6). The WebGPU backend renders everything the WebGL2 backend renders, the fixed-function pipeline and vertex /
pixel shaders 1.x alike. It is built with `-DDXWEBGL2_WEBGPU=ON` (Emscripten's emdawnwebgpu port), the application is
linked with `-sJSPI`; `dxwebgl2_SetBackend(DXWEBGL2_BACKEND_WEBGL2|WEBGPU)` or `-dxwebgl2-backend=webgl2|webgpu` force a
backend. Every runtime, feature and regression test runs on both backends, and the choice of AUTO is tested in browsers
with and without WebGPU (section 7). The two emulations WebGL2 lacked (vertex strides that are not a multiple of 4, DXT
textures whose size is not) are shared with it now (section 10).

```
  your Direct3D 8 code
          |
  +-------v-----------------------------------------------+
  |  src/d3d8   Direct3D 8 front end: state, COM objects,  |   translates state into neutral descriptions
  |             program keys, uniform values               |   (PipelineState, ProgramKey, DrawCall, ...)
  +-------v-----------------------------------------------+
  |  src/core   backend.h (the interface), resources with  |   BufferStorage, TextureBase, SurfaceStorage:
  |             system memory copies, formats, GLSL and    |   CPU copies + backend handles
  |             WGSL generation, shader translators        |
  +-------v-------------------------+---------------------+
  |  src/core/gl   WebGL2 backend   |  src/core/webgpu    |
  +---------------------------------+---------------------+
```

## 1. The backend interface

`src/core/backend.h` is what a Direct3D front end asks of the GPU API underneath. It was cut at the level WebGPU works
at, not at the level of GL calls:

| Area | Interface | Notes |
|---|---|---|
| Device | `Initialize(w, h)`, `Shutdown()`, `Caps()` (`GpuCaps`), `Description()`, `CanvasSize()` | the context/device on the configured canvas |
| Back buffer | `CreateBackBuffer(w, h, samples)`, `DestroyBackBuffer()`, `SetGammaRamp()`, `Present()`, `ReadBackBuffer()` | the back buffer is a texture of the backend; `Present()` draws it to the canvas (with the gamma ramp) |
| Device loss | `IsContextLost()`, `HasContextEvent()`/`ClearContextEvent()`, `OnContextLost()`, `RestoreContext()`, test aids | the front end keeps the Direct3D semantics (`D3DERR_DEVICELOST`, `DEVICENOTRESET`, `Reset`) |
| Resources | `CreateBuffer/DestroyBuffer/WriteBuffer`, `CreateTexture/DestroyTexture/WriteTexture/ReadTexture`, `CreateSurface/DestroySurface/ReadSurface`, `GetSampler(SamplerDesc)` | opaque handles (`GpuBuffer`, `GpuTexture`, `GpuSurface`, `GpuSampler`); data is handed over in the Direct3D memory layout of the `D3DFORMAT`, the backend converts what its API needs |
| Programs | `CreateProgram(ProgramKey, vs object, ps object)`, `DestroyProgram()` | the backend generates the code for its API from the key (GLSL or WGSL) |
| Rendering | `SetRenderTargets(color, depth)`, `Clear(ClearCall)`, `Draw(DrawCall)` | one call per draw with everything it depends on |
| Cache hints | `InvalidateState()`, `InvalidateVertexInput()` | the front end reset its state / changed its streams |

The key types: `DrawCall` (topology including fans, fill mode, counts, base vertex, the full `PipelineState`, the
`Program`, the uniform version counters and a `UniformSource`, a `TextureBinding` per stage, the `VertexLayout` and the
16 streams or user memory, the index buffer or user indices), `PipelineState` (neutral enumerations, compared with
`memcmp`), `UniformSource` / `UniformSink` (the front end produces the values of a uniform group, the backend writes
them to GL uniforms or into a uniform block), `RenderTargetRef` (refers to the front end's storage objects so that the
binding survives a device loss), `SamplerDesc` (Direct3D numbering) and `GpuObject` (everything that owns backend
objects; told about a loss and a restore). The CPU emulation of primitive types and fill modes that both backends need
(`ExpandTriangles`, `TrianglesToLines`) and the padding of vertex strides the GPU APIs reject (`PaddedVertexStride`,
`RepackVertices`) are part of the interface module.

What step 2 changed in the interface: `WriteBuffer`'s `data` is documented to point into the caller's copy of the whole
buffer (`BufferStorage` always did that), so that a backend can widen a range to whole 4-byte words. Nothing else:
the GL leftovers listed by step 1 stay harmless (`InvalidateVertexInput()` is ignored by WebGPU; the uniform versions
per program are what the WebGPU backend needs too, see section 2; the format table's GL columns are not used by it).

## 2. How Direct3D state maps to WebGPU (as implemented)

### Render pipelines

`GPURenderPipeline`s are created on first use and cached by `WgPipelineKey`: the program, the vertex input signature of
the layout (section 4), `VertexLayout::id`, the stride and stream of every vertex buffer slot (the streams the layout
uses, at most 8: WebGPU's `maxVertexBuffers`), the topology, instancing (point lists, section 3), cull mode and front
face (the flipped GL convention of `PipelineState::frontFace` inverted: WebGPU's framebuffer y points down like
Direct3D's), blend (enable, factors, operation; `min`/`max` force the factors to one, which GL ignores and WebGPU
requires), the colour write mask, depth write and compare (writes only with the depth test, like GL), stencil (front =
back), depth bias (`ZBIAS` -> `depthBias` = polygon offset units, `depthBiasSlopeScale` = factor, triangles only), the
formats of the attachments and the sample count. The last key is compared first (`memcmp`), then a hash map; a
program's pipelines are released with it.

Viewport (with the depth range), scissor and the stencil reference are dynamic pass state, set only when they change.

### Bind groups and uniforms

* **Group 0: the program's uniform block at a dynamic offset.** The WGSL struct of a program holds the uniforms it
  reads, in `UniformId` order, 16-byte aligned; the light and texture transform arrays of fixed-function programs are
  sized for the key (one light: 7 vectors instead of 56). The program keeps a CPU copy of its block; before a draw the
  groups whose version changed since the program last received them are written into it through the `UniformSink`
  (the same version counters as GL); a changed block, or the first draw after a submit, appends the block to a CPU
  staging ring (256-byte aligned) and binds group 0 at that offset. The ring is uploaded with **one `writeBuffer` per
  submit**, right before it, and starts over afterwards. One binding instead of one per uniform group: Direct3D has 16
  groups and `maxDynamicUniformBuffersPerPipelineLayout` is 8; and one copy per changed draw is cheaper than one
  `writeBuffer` per group.
* **Group 1: textures and samplers** of the stages (`tx<s>` binding 2s, `sm<s>` binding 2s+1). The bind group layout
  follows the stage texture types of the key (2D, cube, 3D); stages without a texture (or whose texture is the render
  target) get 1x1 white textures; `TextureBinding::baseLevel` (`D3DTSS_MAXMIPLEVEL`) selects a view with that
  `baseMipLevel`. Bind groups are cached by (layout, texture id, base level, sampler id); a destroyed texture's entries
  are released with it.
* **Samplers** are cached per `SamplerDesc`. A texture sampled without mip mapping uses `lodMaxClamp` 0.25 with
  nearest mip selection (level 0 only, while the min / mag filter decision still follows the level of detail; a clamp
  to 0 would always magnify); anisotropy only when every filter is linear (a WebGPU rule). `BORDER` is `clamp-to-edge`
  plus the shader emulation (`StageKey::borderMask`), `MIRRORONCE` is `mirror-repeat`.

### Buffers, textures and the order of queue writes

* Vertex and index buffers are `GPUBuffer`s (`VERTEX | INDEX | COPY_DST`); `WriteBuffer` -> `queue.writeBuffer`, the
  range widened to whole words inside the caller's copy. Draws from user memory, rebuilt index lists (fans, fill modes)
  and gathered or repacked vertices go through a second staging ring, uploaded like the uniform ring.
* Textures are `GPUTexture`s (`TEXTURE_BINDING | COPY_DST | COPY_SRC`, `| RENDER_ATTACHMENT` for render target
  formats); `WriteTexture` -> `queue.writeTexture` after the format conversion (section 3); compressed copies cover
  whole blocks (the physical size of small mip levels). Surfaces (render targets, depth buffers) are textures.
* **Ordering.** Queue writes take effect before the commands of the next submit, while the commands of a frame are
  recorded into one encoder submitted at `Present`. A write into a buffer or texture that a recorded command uses
  (drawn with, sampled, rendered into since the last submit) therefore submits what was recorded first, so that the
  earlier commands see the earlier contents (the classic dynamic vertex buffer pattern, `LockRect` on a texture drawn
  with earlier in the frame). The diagnostics report counts these submits (`Perf` hits).

### Render passes and clears

Draws are recorded into a `GPURenderPassEncoder` that stays open while the render targets do not change. It ends on
`SetRenderTargets` with other targets, before read-backs and uploads that need a submit, and at `Present`. A clear of
the whole target before the first draw of a pass becomes the pass's load operations (colour, depth, stencil
separately); other clears (rectangles, the viewport, or after draws) draw a full-screen triangle per rectangle with
viewport and scissor set to it: colour from the shader, depth through z with `always` and writes, stencil by `replace`
with the reference. Clears that no draw follows get a pass of their own. A depth buffer whose size or sample count
differs from the colour target (Direct3D 8 allows a larger one) is replaced by a private one of the target's size.

### Back buffer, presentation, multisampling

The back buffer is an `rgba8unorm` texture. `Present` draws it into the canvas texture (`getCurrentTexture()`, the
canvas configured with the preferred format, `bgra8unorm` here, `opaque`) with `textureLoad`, through the gamma ramp
table when one is set, and submits. Like WebGL2 from a pthread, the browser shows the frame when the thread yields
(`OnFramePresented` with `emscripten_sleep(0)` for loops that never return; `--frames=600 --yield` tests it). A
multisampled back buffer is a 4x texture resolved into the back buffer at the end of every pass (`resolveTarget`); every
request of 2 or more samples is rendered with 4 (`GpuCaps::maxSamples` = 4, the front end reports the clamped type).

## 3. Direct3D 8 features WebGPU lacks, and their emulation (as implemented)

| Feature | WebGPU | Emulation |
|---|---|---|
| Triangle fans | no fan topology | expanded to a triangle list on the CPU (`ExpandTriangles`, indices through the stream ring); each triangle starts with its second vertex so that flat shading takes Direct3D's vertex |
| Point lists with sizes and sprites (`POINTSIZE*`, `POINTSCALE*`, `POINTSPRITEENABLE`) | points are 1 pixel | every point list is drawn as instanced quads: 4 vertices of a strip per point, the vertex buffers step per instance, the vertex stage computes the size as the GLSL one does (`u_pt`) and offsets the corners in clip space; the sprite coordinate (u right, v down) is passed to the fragment stage. Indexed point lists gather their vertices on the CPU first |
| Fill modes `WIREFRAME` / `POINT` | no polygon mode | CPU rebuild to line / point lists, as WebGL2 does |
| Line width | 1 only | as WebGL2 (1 pixel) |
| `D3DTADDRESS_BORDER` | no border colour | shader emulation (`StageKey::borderMask`, `u_border`), `clamp-to-edge` sampler |
| `D3DTADDRESS_MIRRORONCE` | no | `mirror-repeat` (as WebGL2) |
| `A8R8G8B8` | | `bgra8unorm`, uploaded unchanged (same byte order) |
| `X8R8G8B8` | no "X" formats | `bgra8unorm` with alpha set to 255 on upload |
| `R8G8B8`, 16-bit colour (`R5G6B5`, `A1R5G5B5`, `X1R5G5B5`, `A4R4G4B4`, `X4R4G4B4`), `L8`, `A8`, `A8L8`, `A4L4` | no 24-bit, packed 16-bit, luminance or alpha formats | expanded to `bgra8unorm` on upload (`UnpackPixel`: the same expansion as the WebGL2 path, `A8` reads (0, 0, 0, a) like `GL_ALPHA`); 16-bit render targets are `bgra8unorm` (read-backs convert back to the surface format) |
| `V8U8`, `Q8W8V8U8` | | `rg8snorm`, `rgba8snorm` |
| DXT1-5 | `bc1/bc2/bc3-rgba-unorm` behind `texture-compression-bc` | requested when the adapter has it; textures whose size is not a multiple of 4 (a WebGPU rule for BC) and volumes are decoded on the CPU (`DecodeDXT`) to `rgba8unorm`, as are all of them without the feature or with `dxwebgl2_SetDisableS3TC`. The decision is the core's (`DecodeDxtOnCpu`, `TextureDesc::decodeDxt`), the same for WebGL2, whose S3TC extension has the same rules |
| Depth formats | `depth16unorm`, `depth24plus`, `depth24plus-stencil8` | `D16` -> `depth16unorm`; `D24X8`, `D32` -> `depth24plus`; `D24S8`, `D24X4S4`, `D15S1` -> `depth24plus-stencil8` |
| Inputs the vertex format does not provide (GL reads (0,0,0,1)) | every shader input needs a buffer | the vertex stage is generated per input signature (section 4) and uses (0,0,0,1) for the others |
| Integer vertex data (`UBYTE4`, `SHORT2`, `SHORT4` without normalization) | integer formats are read as integers | `uint8x4` / `sint16x2|4`, converted to float in the shader (the signature says which) |
| Vertex strides that are not a multiple of 4 | `arrayStride` must be | the vertices are repacked (`RepackVertices`, `PaddedVertexStride`) through the stream ring. WebGL2 needs the same for strides that are not a multiple of the component size: it keeps a padded copy per vertex buffer (section 10) |
| Flat shading | `@interpolate(flat)` takes the first vertex, like Direct3D | none needed |
| Base vertex | `drawIndexed(..., baseVertex)` | none needed |
| Multisampling 2-16 samples | 1 or 4 samples | 4 for every request (the front end reports the clamped type) |
| Clear restricted to rectangles / the viewport, clears after draws | `loadOp: clear` clears the whole attachment at the start of a pass | quad (section 2) |
| Depth buffer larger than the render target | attachments must have the same size | private depth buffer of the target's size |
| Alpha test, fog, user clip planes, texture coordinate generation, the stage cascade | no fixed function | generated WGSL, as for WebGL2 (clip planes by `discard`) |
| Gamma ramp | | applied by the present pass |
| Synchronous read-back (`LockRect` on render targets, `GetFrontBuffer`, `CopyRects` from a render target) | `mapAsync` only | `copyTextureToBuffer`, submit, then wait for `mapAsync`: suspends the calling thread (JSPI, section 5) |
| Texture that is the current render target and also sampled | validation error | the stage gets the white texture (the front end's rule) |
| Explicit swap control | no commit | the frame is shown when the thread yields (as WebGL2 from a pthread) |

Clip space: WebGPU's depth range is [0, 1] like Direct3D's (no z remapping) and its framebuffer y axis points down like
Direct3D's, so the vertex stage does not negate y; the half pixel offset of Direct3D 8 pixel centres stays (`+x`, `-y`
in clip space). Pre-transformed (`XYZRHW`) vertices are mapped with the same formulas, y downwards.

## 4. Shader generation (as implemented)

* **Fixed function**: `core/fixedfunction_wgsl.cpp` generates the WGSL vertex and fragment stages from the
  `ProgramKey` with the decisions of the GLSL generator (`core/fixedfunction.cpp`): lights, fog, texture coordinate
  generation and transforms, the stage cascade, specular, alpha test, clip planes, point sizes. The expressions of the
  stage cascade are shared (`StageArgExpr`, `StageOpExpr`, emitting either language; the GLSL output is byte-identical,
  the WebGL2 frame hashes did not change). `core/wgsl.h` holds the conventions both stages and the shader translators
  share: the uniform struct, the varyings struct (`c0`, `c1`, fog and point sprite coordinate in one vector, clip
  distances in two, `t0..t7`; at most 13 locations of WebGPU's 16), the texture bindings.
* **Vertex input signature**: WGSL needs a buffer for every vertex input and reads integer formats as integers, so
  the vertex module depends on which registers the layout provides and with which type (2 bits per `D3DVSDE_*`
  register). The program key was not extended (that would have split WebGL2's programs): a WebGPU program keeps one
  vertex module per signature, created when a draw first uses it; the uniform layout does not depend on it.
* **Shaders**: `core/shader_wgsl.cpp` translates vs.1.1 and ps.1.0-1.4 to WGSL from the same decoding as the GLSL
  translators: the token parameters (`DecodeParam`, `OperandCount`) and, for pixel shaders, the decoded instruction
  list of the shader object (`PixelShaderObject::Impl`, moved to `shader_internal.h`); the vertex shader WGSL emitter
  re-reads the stored token stream. The GLSL translators are unchanged. A full `ShaderIR` (ROADMAP M2) is still the
  way to add shader model 2/3 to both.
* WGSL specifics handled: no swizzle assignment (masked writes go component by component), no ternary operator
  (`select`), `textureSample` only in uniform control flow (samples are never conditional: `texkill` records the kill
  and discards at the end, clip planes and the alpha test discard after the samples, the `texm3x3pad` sequence is
  resolved while generating), `frag_depth` for `texdepth` / `texm3x2depth`, names that do not collide with the
  `t#` registers.
* Compilation errors arrive asynchronously in WebGPU (an invalid pipeline then invalidates the frame's command
  buffer); with `dxwebgl2_SetDebug(1)` every module is checked right away (an error scope, waited for) and its source
  logged. Validation errors are logged (`[dxWebGL2] WebGPU validation error: ...`) and fail the test runners.

## 5. Threading, presentation and asynchrony

Verified in this environment (headless Chromium from `/opt/pw-browsers/chromium`): WebGPU is exposed with
`--enable-unsafe-webgpu --enable-features=Vulkan --use-vulkan=swiftshader --use-webgpu-adapter=swiftshader
--use-angle=swiftshader` (adapter `google swiftshader`, with `texture-compression-bc`), on the main thread and in a
dedicated worker rendering to a transferred `OffscreenCanvas` (the `-sPROXY_TO_PTHREAD` test programs); without the
Vulkan flags `requestDevice` fails.

`requestAdapter`, `requestDevice` and `mapAsync` are asynchronous, `IDirect3D8::CreateDevice` and `LockRect` are not.
**Decision (implemented): JSPI.** The calling thread waits in `wgpuInstanceWaitAny` (instance created with
`TimedWaitAny`), which suspends it. The backend is only offered when the build can suspend (`emscripten_has_asyncify()`,
JSPI or ASYNCIFY); otherwise the device uses WebGL2 and the reason is logged. Whether the *calling code* can suspend
cannot be detected (JSPI traps when a function that suspends is reached from a call that is not a promising export), so
with `DXWEBGL2_BACKEND_AUTO` too a JSPI application creates its device from code that can suspend. Calls that wait must come from code that
can suspend: a `-sPROXY_TO_PTHREAD` main, or `main` itself on the main thread (the examples create their device there).
A read-back from a `requestAnimationFrame` callback on the main thread cannot wait: render from a pthread, or avoid
read-backs there. Rendering itself never waits.

Device loss: `GPUDevice.lost` (and the test aid `GPUDevice.destroy()`, `dxwebgl2_LoseContextNow`) sets the event flag
the front end polls; the device is reported lost once, then restorable (after `TestRestoreContext` for the test aid),
and `Reset()` creates a new adapter and device (the adapter of a lost device cannot create another) and every
`GpuObject` again from its system memory copy. The feature test's `contextloss` mode passes on WebGPU.

## 6. Run-time selection

```c
enum dxwebgl2_Backend {
    DXWEBGL2_BACKEND_AUTO = 0,     /* default: WebGPU when available, WebGL2 otherwise */
    DXWEBGL2_BACKEND_WEBGL2 = 1,
    DXWEBGL2_BACKEND_WEBGPU = 2,   /* needs DXWEBGL2_WEBGPU=ON and -sJSPI; otherwise WebGL2 with a logged reason */
};
void dxwebgl2_SetBackend(int backend);   /* before CreateDevice(); also -dxwebgl2-backend=auto|webgl2|webgpu */
int dxwebgl2_GetBackend(void);           /* backend of the device created last */
```

`CreateBackend()` (`src/core/backend.cpp`) decides when a device is created. For WebGPU (requested, or tried by AUTO)
`CreateWebGPUBackend` creates the instance, adapter and device (`WebGPUBackend::Acquire`) and returns null with the
reason, never aborting, when any of it is missing:

| Situation | `DXWEBGL2_BACKEND_AUTO` | `DXWEBGL2_BACKEND_WEBGPU` |
|---|---|---|
| library built without `DXWEBGL2_WEBGPU` | WebGL2, nothing logged | WebGL2, logged and counted (`Approximated`) |
| application not linked with `-sJSPI` / `-sASYNCIFY` | WebGL2, one line: `WebGPU is not available, using WebGL2: the application is not linked with -sJSPI ...` | WebGL2, logged and counted |
| no `navigator.gpu`, or `requestAdapter()` returns null (a browser without WebGPU, or Chromium without `--enable-unsafe-webgpu` on Linux) | WebGL2, one line: `... no adapter: ...` | WebGL2, logged and counted |
| `requestDevice()` fails | WebGL2, one line: `... requestDevice failed: ...` | WebGL2, logged and counted |
| adapter and device | **WebGPU** | WebGPU |

AUTO's fallback is not an approximation (WebGL2 is a complete backend): one log line, no diagnostics hit. A canvas keeps
the kind of context it got first, so once a device rendered to a canvas through WebGL2, AUTO keeps WebGL2 for the next
devices on that canvas (no second attempt, no second line). `dxwebgl2_GetRendererString()` reports "WebGPU: google
swiftshader" or "WebGL2: ...". CMake: `DXWEBGL2_WEBGPU` (default `OFF`) compiles `src/core/webgpu` and adds
`--use-port=emdawnwebgpu` to the link options of the library's users; `scripts/test.sh` builds with it.

Why WebGPU by default: it passes the same test suite as WebGL2 (frame hashes per backend, pictures compared, sections
7 and 9), and its frames cost less CPU time (section 9). Applications that cannot suspend where they create the device,
or that prefer WebGL2, select it explicitly.

## 7. Testing (as implemented)

* Every runner takes `--backend=webgl2|webgpu|auto` (`run_test.mjs` passes it to the page, `run_features.mjs` to every
  mode, `run_regression.mjs` selects the hashes, `run_example.mjs` passes `-dxwebgl2-backend=` to the page); without
  it the page gets no backend option, i.e. the library's default, AUTO. `scripts/test.sh` names the backend of every
  run, so its WebGL2 runs stay WebGL2 runs (same screenshots, same hashes, the same reference keys). Every browser
  starts with the WebGPU flags (they do not change WebGL2's pictures) unless a runner asks for a browser without them
  (`--no-webgpu-browser`: `navigator.gpu` exists, `requestAdapter()` returns null). The test programs check that the
  device renders through the requested backend (`--expect-backend=` names the one AUTO must select) and print it
  (`BACKEND webgl2|webgpu`); WebGPU errors in the log fail a run. `scripts/test.sh` runs the whole browser suite once per
  backend (`--backend webgl2|webgpu` for one), then the AUTO checks.
* **AUTO** (`scripts/test.sh`, step "Default backend"): `run_regression.mjs --backend=auto --expect-backend=webgpu`
  renders the default and shaders scenes through AUTO in a browser with WebGPU (the hashes must be the `webgpu:` ones),
  and with `--expect-backend=webgl2 --no-webgpu-browser` in a browser without WebGPU (the WebGL2 hashes, and exactly one
  `WebGPU is not available` line). `run_auto.mjs` opens the pages of `tests/consumer/device.cpp`, built by
  `tests/consumer/run.sh` against the installed package (WebGPU backend; linked with and without `-sJSPI`) and against
  the sources with `add_subdirectory` (without `DXWEBGL2_WEBGPU`), in browsers with WebGPU, without an adapter, without
  `navigator.gpu` and with a `requestDevice()` that rejects (init scripts): each must get the backend of the table in
  section 6, log the line it says and show its cleared canvas.
* **Frame hashes per backend**: `reference_hashes.json` has the WebGL2 hashes under the scene names (unchanged) and
  the WebGPU ones under `webgpu:<scene>`.
* **Picture comparison**: `tests/runner/compare_backends.mjs` renders the runtime test scenes with both backends and
  compares the screenshots (tolerance 8 per channel; a scene fails when more than 0.5% of its pixels differ by more);
  `--out` keeps the pictures and a difference picture per scene. `scripts/test.sh` runs it.
* **Feature tests** for the emulations (both backends): `clears` (rectangles, after draws, depth and stencil
  rectangles, several rectangles, the viewport), `primitives` (indexed fan with a base vertex, point lists from
  buffers with and without indices, point sprite orientation, wireframe and point fill, a 22-byte stride from user
  memory and from a vertex buffer), `formats` (`X1R5G5B5`, `X4R4G4B4`, `R8G8B8`, `A4L4`, `X8R8G8B8` with alpha 0 in
  memory, DXT1 of 6x6, a DXT1 volume), `updates` (a dynamic
  vertex buffer rewritten with `DISCARD` and appended with `NOOVERWRITE` within a frame, a texture updated between two
  draws, render targets of 32 and 16 bits read back, a lockable render target). Until step 3 two checks failed on
  WebGL2 (reported as `XFAIL`): the 22-byte stride and the 6x6 DXT1 texture; both pass on both backends now
  (section 10).
* **Performance**: `d3d8_runtime_test --perf=N` draws N lit, textured quads per frame from a vertex buffer (world
  matrix and material per draw, texture every 8 draws) and prints the CPU time per frame on the rendering thread and the
  API calls per frame (the `STATS` categories count WebGPU calls for the WebGPU backend: draw = draws, state =
  pipeline and dynamic state, bind = bind groups, vertex/index buffers and passes, upload = `writeBuffer` /
  `writeTexture`, other = object creation and submits).

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

## 9. Step 2: what was done and how it was verified

* `src/core/webgpu/` (`wgpu.h`, `wgpu_backend.h/.cpp`, `wgpu_resources.cpp`, `wgpu_program.cpp`, `wgpu_draw.cpp`):
  the backend of sections 2, 3 and 5.
* `src/core/wgsl.h`, `src/core/fixedfunction_wgsl.cpp`, `src/core/shader_wgsl.cpp`: the WGSL generators (section 4).
  Shared with GLSL: the stage cascade expressions, the decoded pixel shader instructions.
* Front end: unchanged except the device messages naming the WebGPU device; `-dxwebgl2-backend=` among the command
  line options; the examples pass their arguments to `dxwebgl2_ParseArguments` and link `-sJSPI` when the backend is
  built.
* WebGL2 unchanged: the frame hashes of the WebGL2 scenes are identical before and after.

Results (headless Chromium 141, SwiftShader, `scripts/test.sh`):

| Test | WebGL2 | WebGPU |
|---|---|---|
| runtime test, 8 modes (default, shaders, scene2, scene3, no-s3tc, reset, implicit, 600 frames from a non-yielding thread with `--yield`) | 8 pass (104/110 expectations, 13-24 probes) | 8 pass (same expectations and probes, tolerance 6) |
| feature tests, 7 modes | all pass: basic 106 (+2 XFAIL), basic-ext 106 (+2), basic-nopv 106 (+2), basic-nos3tc 107 (+1), contextloss 19, msaa 10, msaa-off 10 | all pass: basic / basic-ext / basic-nopv / basic-nos3tc 108, contextloss 19, msaa 10, msaa-off 10 |
| frame hashes | default `1d18ca44807a52b7`, shaders `ed22b76b734dad2a`, scene2 `7cf909c436852d70`, scene3 `dce88a9773c5c88d`, default-nos3tc `1d18ca44807a52b7` (unchanged) | default `72150f84d703cfff`, shaders `de40f7a50b002796`, scene2 `978e065802f7aefd`, scene3 `91c551c5fd217ded`, default-nos3tc `72150f84d703cfff` (BC sampling and the CPU decoder agree exactly) |
| examples | 2 pass | 2 pass (identical pictures) |

Picture comparison WebGL2 / WebGPU (screenshots of the canvas, 640x480):

| Scene | max | mean | pixels > 8 |
|---|---|---|---|
| default | 1 | 0.007 | 0 |
| shaders | 1 | 0.007 | 0 |
| scene2 | 223 | 0.309 | 481 (0.157%) |
| scene3 | 1 | 0.044 | 0 |
| default-nos3tc | 1 | 0.007 | 0 |

The differences of scene2 are rasterization ties, not shading: the wireframe sphere (line pixels step differently) and
the diagonal of a flat-shaded quad whose edge passes exactly through pixel centres (which triangle owns them). WebGL2
renders the image upside down (y up), so the top-left fill rule and the line steps fall on the other side; WebGPU's
framebuffer y points down like Direct3D's, so WebGPU follows Direct3D there. Elsewhere the backends agree to 1/255
(rounding).

Performance (`--perf=N`, CPU time per frame on the rendering pthread, 60 frames after 10 warm-up frames, SwiftShader,
this machine, two runs each; the averages vary from run to run, the WebGL2 ones because of frames in which the thread
waits for the GPU process):

| draws per frame | WebGL2: average / best ms, API calls | WebGPU: average / best ms, API calls, bytes uploaded |
|---|---|---|
| 100 | 3.1-3.5 / 0.16-0.18, 330 | 0.7-1.5 / 0.26-0.27, 230, 51 KB |
| 500 | 14-21 / 0.58, 1580 | 2.2-3.4 / 0.98-1.04, 1080, 256 KB |
| 2000 | 64-70 / 4.7-6.8, 6267 | 6.0-8.5 / 3.8-3.9, 4303, 1 MB |

So the best frames cost about 1.2 us per draw on WebGL2 and 2 us on WebGPU (more JavaScript per call in the bindings),
but the WebGPU frames do not stall: on average a frame of WebGPU draws costs 4 to 10 times less here. GPU time is not
measured (SwiftShader runs in the GPU process).

Per draw the WebGPU backend makes about 2 calls (bind group 0 at the block's offset, the draw; the texture bind group
when it changes), WebGL2 about 3 (uniforms, texture bind, draw); the uniform data goes up once per frame (512 bytes per
draw here: the program's block when the world matrix or the material changed).

## 10. Step 3: what was done and how it was verified

* **AUTO prefers WebGPU** (`src/core/backend.cpp`, `src/core/webgpu/wgpu_backend.*`): section 6. `CreateWebGPUBackend`
  returns null with the reason instead of logging it (the caller logs one line); a `requestDevice()` that fails is not
  reported as a device loss; a failed attempt releases its instance and adapter.
* **Vertex strides WebGL rejects** (`src/core/gl/gl_draw.cpp`, `gl_resources.cpp`): WebGL requires the stride and the
  offset of an attribute to be multiples of its component size. A stream that breaks the rule is drawn from a copy
  padded to `PaddedVertexStride` (a multiple of 4; element offsets in Direct3D 8 vertices are multiples of 4 already):
  user memory is padded while it is streamed, a vertex buffer gets a second GL buffer that is filled once and then only
  for the vertices written since (`WriteBuffer` records the range), so a static buffer is repacked once. The helpers
  (`PaddedVertexStride`, `RepackVertices`, `core/backend.h`) are shared with the WebGPU backend's repacking.
* **DXT sizes WebGL rejects** (`src/core/format.cpp`, `texture_storage.cpp`): `WEBGL_compressed_texture_s3tc` needs a
  top level made of whole 4x4 blocks, like WebGPU's BC formats, and neither API has 3D S3TC textures. The decision to
  decode on the CPU (`DecodeDxtOnCpu`) moved into the core and is the same for both backends; volume levels are decoded
  slice by slice (`DecodeDXTVolume`).
* Feature checks: the 22-byte stride and the 6x6 DXT1 texture pass on WebGL2 (no `XFAIL` left); added: a 22-byte stride
  from a vertex buffer, rewritten after a draw and drawn with a base vertex, and a DXT1 volume texture.
* The WebGL2 and WebGPU frame hashes are unchanged, and so is the picture comparison.

Results (headless Chromium 141, SwiftShader, `scripts/test.sh --clean`):

| Test | WebGL2 | WebGPU |
|---|---|---|
| runtime test, 8 modes | 8 pass (same expectations and probes as before) | 8 pass |
| feature tests, 7 modes | all pass, no `XFAIL`: basic / basic-ext / basic-nopv / basic-nos3tc 111, contextloss 19, msaa 10, msaa-off 10 | all pass: the same counts |
| frame hashes | unchanged (`1d18ca44807a52b7`, `ed22b76b734dad2a`, `7cf909c436852d70`, `dce88a9773c5c88d`) | unchanged (`72150f84d703cfff`, `de40f7a50b002796`, `978e065802f7aefd`, `91c551c5fd217ded`) |
| examples | 2 pass | 2 pass |
| picture comparison | the same numbers as in section 9 | |

AUTO: in the browser with WebGPU the default and shaders scenes render through WebGPU with the `webgpu:` hashes; in
the browser without WebGPU through WebGL2 with the WebGL2 hashes and the single line `WebGPU is not available, using
WebGL2: no adapter: ...`. The consumer pages: package with WebGPU and JSPI -> WebGPU; the same without an adapter,
without `navigator.gpu` or with a failing `requestDevice` -> WebGL2 (one line each); without JSPI -> WebGL2 (one
line); built without `DXWEBGL2_WEBGPU` -> WebGL2 (no line).

## 11. Next steps

1. Dynamic buffers: rename (a new `GPUBuffer` per rewrite of a buffer used in the frame, recycled after the submit)
   instead of submitting; NOOVERWRITE appends could skip both when the written range was not drawn from. The WebGPU
   backend could keep a padded copy per vertex buffer like WebGL2 instead of repacking per draw.
2. `ShaderIR` (ROADMAP M2) for both shader languages, before shader model 2/3.
3. Optional features: `clip-distances` for user clip planes, `texture-component-swizzle` for luminance / alpha formats
   (no expansion), timestamp queries for GPU timings.
4. Strip draws from index buffers: WebGPU restarts strips at index 0xFFFF / 0xFFFFFFFF (Direct3D does not).
