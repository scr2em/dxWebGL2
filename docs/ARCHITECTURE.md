# Architecture

dxWebGL2 is organized in layers so that a second API (Direct3D 9, see [ROADMAP.md](ROADMAP.md)) can be put on top of
the same core, and so that the core can render through a second GPU API (WebGPU, see [WEBGPU.md](WEBGPU.md)).

```
include/dxwebgl2/      public C API (dxwebgl2.h) and the Win32/COM base (win32_min.h)
include/dx8/           D3DX 8 headers (own)          third_party/wine/include: d3d8*.h (Wine)
src/api/               helpers shared by the API front ends: COM plumbing (IUnknown base, Ref<T>)
src/d3d8/              the Direct3D 8 front end
src/core/              the API independent core and the backend interface (backend.h)
src/core/gl/           the WebGL2 backend
src/core/webgpu/       the WebGPU backend (optional: DXWEBGL2_WEBGPU, docs/WEBGPU.md)
```

`tools/check_layering.py` (run by `scripts/test.sh`) enforces the dependency rules:

* `src/core` includes only `src/core` (and the public `dxwebgl2/` headers) and never mentions an `IDirect3D*` COM
  interface;
* only the backends (`src/core/gl`, `src/core/webgpu`) include a GPU API header and call the GPU API; the rest of
  `src/core` reaches the GPU through `core/backend.h` (the GL enum values of the format table are the one exception);
* `src/api` may include `src/core` (not a backend);
* `src/d3d8` may include `src/core` (not a backend) and `src/api`, and uses no GPU API call, type or constant.

## The core (`src/core`)

| File | Contents |
|---|---|
| `common.h`, `util.cpp` | configuration set through the C API (`Config`), logging, row-major `Mat4` helpers, small utilities, UTF-16 to UTF-8 |
| `api_types.h` | the single place that decides which API headers the core speaks in (today `<d3d8.h>`; a Direct3D 9 build switches to `<d3d9.h>`) |
| `types.h` | limits (stages, streams, constants), uniform groups, `VertexLayout`/`VertexElement` |
| `backend.h/.cpp` | the backend interface: `Backend` (device, back buffer and presentation, device loss, resources, programs, render targets, `Clear`, `Draw`), the opaque handles `GpuBuffer`/`GpuTexture`/`GpuSurface`/`GpuSampler`, `GpuCaps`, the neutral `PipelineState`, `DrawCall`, `ClearCall`, `RenderTargetRef`, `SamplerDesc`, the `UniformSource`/`UniformSink` pair, the registry of `GpuObject`s (everything that owns backend objects, so that all of it learns about a context loss and restore), backend selection (`dxwebgl2_SetBackend`) and the CPU emulation of primitive types and fill modes |
| `diag.*`, `glcount.h` | the diagnostics: hit table of unsupported / approximated features, per-frame Direct3D and GPU API call counters, the periodic report, `-dxwebgl2-*` command line options. Off by default; every hit site tests one flag |
| `buffer_storage.*` | `BufferStorage`: a CPU copy of a vertex/index buffer plus the backend buffer, modified ranges are uploaded lazily right before a draw |
| `texture_storage.*` | `TextureBase`: 2D, cube and volume textures with their mip levels, a system-memory shadow copy per level, locking, upload, read-back; `SurfaceStorage`: standalone render targets and depth/stencil buffers |
| `format.*` | the table of surface formats (with the WebGL2 internal format/type), pixel conversion between formats, the WebGL2 upload layout (`PrepareForGL`), DXT1-5 decoder (CPU path when S3TC is missing) and encoder |
| `vertex_layout.cpp` | vertex layouts from FVF codes |
| `program.*` | `ProgramKey` (everything that influences the generated shader code), the `Program` a backend creates for a key, uniform names and groups |
| `fixedfunction.cpp` | generates GLSL ES 3.00 for the fixed-function pipeline from a `ProgramKey`: transform, lighting (up to 8 lights), fog, point sprites, clip planes, the texture stage cascade, alpha test; the expressions of the stage cascade are shared with the WGSL generator (`StageArgExpr`, `StageOpExpr`) |
| `wgsl.h`, `fixedfunction_wgsl.cpp` | the WGSL conventions of the WebGPU backend (uniform block layout, varyings, vertex input signatures, point quads) and the WGSL generator of the fixed-function pipeline, making the same decisions as the GLSL one |
| `shader.h`, `shader_internal.h`, `shader_vs.cpp`, `shader_ps.cpp`, `shader_assembler.cpp` | the shader bytecode translators: vs.1.1 and ps.1.0-1.4 token streams to GLSL, and the text assembler behind `D3DXAssembleShader` |
| `shader_wgsl.cpp` | vs.1.1 and ps.1.0-1.4 to WGSL, from the same decoding (the token parameters; for pixel shaders the decoded instruction list of the shader object) |

### The WebGL2 backend (`src/core/gl`)

| File | Contents |
|---|---|
| `gl.h` | the GLES 3 / WebGL headers and the macros that count every GL call (`glcount.h`); included only here |
| `gl_backend.h` | `WebGL2Backend` and its GL objects (`GLBuffer`, `GLTexture`, `GLSurface`, `GLSampler`, `GLProgram`, the vertex array object cache entries) |
| `gl_backend.cpp` | context creation and capabilities, base objects (framebuffers, white textures, scratch vertex array), back buffer (multisampled when requested) and resolve, present pass with the gamma ramp, context loss and restore |
| `gl_resources.cpp` | the binding cache (buffer targets, texture units, upload unit), buffers, textures (storage, upload with format conversion), renderbuffers, sampler objects, read-back through a scratch framebuffer |
| `gl_program.cpp` | GLSL generation through the core's emitters, compile/link, uniform lookup |
| `gl_draw.cpp` | render target attachment (with substitute depth buffers), clears, state diffing of the `PipelineState`, uniform upload by group version, texture/sampler binding, vertex attributes (vertex array object cache, streaming buffers), the draw with the wireframe / flat shading rebuild |

GL objects remember the generation of the context they were created in; the generation changes when the context is
lost and when it is restored, so nothing of a dead context is deleted.

### The WebGPU backend (`src/core/webgpu`)

Built with `-DDXWEBGL2_WEBGPU=ON` (Emscripten's emdawnwebgpu port, `<webgpu/webgpu.h>`); the design and the emulations
are in [WEBGPU.md](WEBGPU.md).

| File | Contents |
|---|---|
| `wgpu.h` | `<webgpu/webgpu.h>` and the macros that count the WebGPU calls in the categories of `glcount.h`; included only here |
| `wgpu_backend.h` | `WebGPUBackend` and its objects (`WGBuffer`, `WGTexture`, `WGSurface`, `WGSampler`, `WGProgram`), the render pipeline key |
| `wgpu_backend.cpp` | adapter and device creation (waited for with JSPI), capabilities, back buffer (4x multisampled when requested), present pass with the gamma ramp, command submission with the staging rings, read-back (`mapAsync`), device loss |
| `wgpu_resources.cpp` | buffers, textures (the format table and the conversions on upload), render target and depth/stencil surfaces, samplers |
| `wgpu_program.cpp` | programs: the WGSL modules (a vertex module per vertex input signature), the uniform block of a program, the pipeline layout |
| `wgpu_draw.cpp` | render passes and clears (load operations, or a quad), render pipeline and bind group caches, the draw with the fan / fill mode / point sprite emulation |

### Why this split is the base for Direct3D 9 and for WebGPU

* **Programs and shaders are keyed by state, not by API objects.** A front end fills a `ProgramKey` from whatever its
  API calls "state"; the backend generates, compiles and caches the program. Direct3D 9 differs in how state is set
  (sampler states instead of texture stage state, vertex declarations), not in what the pipeline needs.
* **A draw is one description.** The front end hands the backend a `DrawCall` with the complete pipeline state,
  bindings and vertex input, which is what WebGPU's render pipelines and bind groups need; the WebGL2 backend diffs it
  against its cache.
* **Resources are independent of COM and of the GPU API.** `BufferStorage`, `TextureBase` and `SurfaceStorage` hold
  the system memory copies and a backend handle; the front end wraps them into its interface objects.
* **The translators work on token streams.** The version token and the instruction encoding decide the parser; shader
  model 2.0/3.0 adds a second decoder in front of the same emitter, and WGSL is a second emitter, see ROADMAP and
  WEBGPU.

## The Direct3D 8 front end (`src/d3d8`)

| File | Contents |
|---|---|
| `d3d8.cpp` | `Direct3DCreate8`, the `IDirect3D8` object (adapter, modes, format checks), the `D3DCAPS8` (static; refined once a device exists), `dxwebgl2_LookupProc` |
| `device.h/.cpp` | the `IDirect3DDevice8` implementation: creation (selects the backend), back buffer surfaces, present, device loss state (`D3DERR_DEVICELOST` / `DEVICENOTRESET` / `Reset`), queries, resource creation, render target handling |
| `device_state.cpp` | render states, texture stage states, transforms, lights, state blocks, streams, shader objects and constants |
| `device_draw.cpp` | the translation of the device state for the backend (see below), `Clear` |
| `resources.h`, `buffers.cpp`, `texture.cpp` | the COM objects for textures, surfaces, volumes and buffers on top of `BufferStorage` / `TextureBase` / `SurfaceStorage` |
| `vertex_declaration.cpp` | `D3DVSD_*` vertex declarations to a `VertexLayout` |
| `d3dx_*.cpp` | the D3DX 8 functions (math, matrix stack, assembler front end/buffers/error strings, textures and surfaces) |

## Life of a draw call

1. `DrawIndexedPrimitive(...)` -> `Device::DrawCommon` maps the primitive type to a `Topology`.
2. `PrepareDraw` (front end):
   * `SelectProgram`: when render state that influences code changed (`m_keyDirty`), a `ProgramKey` is built from the
     device state (`BuildProgramKey`: vertex format, lighting, fog, per stage color/alpha ops and texture types ...).
     The device looks the key up in its program cache; on a miss the backend generates the code (GLSL:
     `GenerateFixedFunction*` or the translated shader) and compiles/links it (`Backend::CreateProgram`).
3. `DrawCommon` fills a `DrawCall`: the `PipelineState` derived from the render states (`DerivePipeline`: blend, depth,
   cull, stencil, viewport...), the program, the uniform version counters with the device as `UniformSource`, the
   texture and (cached) sampler of every stage (`BindTextures`; the render target itself and missing textures become
   the backend's white texture), the vertex layout and streams, the index buffer.
4. `Backend::Draw` (WebGL2 backend):
   * `ApplyRenderTargets`: attaches the current render target / depth surface to the framebuffer (when they changed).
   * `ApplyPipeline`: applies only the differences to the `PipelineState` applied before.
   * `UploadUniforms`: only the uniform groups whose version counter changed since this program last received them;
     the values come from `Device::WriteUniforms`.
   * `BindTextures`: textures, sampler objects and base levels per stage.
   * the vertex buffers are flushed (`BufferStorage::Flush`, uploading the locked range) and the attribute pointers set
     from the `VertexLayout` (cached vertex array objects); user-pointer draws stream through ring buffers.
   * `glDrawElements` / `glDrawArrays` with the primitive type mapped (wireframe fill and, without
     `WEBGL_provoking_vertex`, flat shading rebuild the index list on the CPU).

## Conventions that matter when reading or changing the code

* **Everything is rendered "flipped"** (WebGL2 backend): row 0 of every framebuffer attachment is the top row of the
  image, exactly like Direct3D surfaces, so render targets sampled as textures and surface read-backs need no vertical
  flip. The vertex stage negates clip-space Y and the winding order is adjusted (`PipelineState::frontFace` is given in
  that convention). `Present()` flips the back buffer when drawing it to the canvas. The WebGPU backend needs none of
  this: WebGPU's framebuffer y points down like Direct3D's (it inverts `frontFace`).
* **Clip space depth** is converted from Direct3D's `[0, w]` to GL's `[-w, w]` in the vertex shader (WebGPU's is
  `[0, w]` already), and half a pixel is added to emulate Direct3D 8's pixel centers.
* **Matrices are row-major with row vectors** like Direct3D. They are uploaded as they are (`transpose = GL_FALSE`),
  which GL reads as the transposed matrix, so the generated shaders multiply `matrix * vector`.
* **Vertex attribute locations equal the `D3DVSDE_*` register numbers**, so declarations map one to one; `D3DCOLOR`
  attributes are swizzled to RGBA in the vertex shader.
* **GL call cost matters** (each WebGL call crosses from WebAssembly into JavaScript): the WebGL2 backend keeps bind
  caches, per-group uniform versions, lazy buffer uploads, a sampler object cache, vertex array objects and state
  diffing in `ApplyPipeline`. The runtime test prints the call counts per category (`STATS` lines) so that changes
  can be compared.
* **Managed resources keep a system-memory copy** (`D3DPOOL_MANAGED`): it makes `Lock` cheap and lets resources be
  re-uploaded. `dxwebgl2_SetReleaseTextureShadows(1)` drops the copy after upload at the price of a read-back on the next
  lock.
* **The GPU context can disappear**: `Device::CheckContext` polls the backend, `GpuObject::OnContextLost` /
  `OnContextRestored` let every resource drop and recreate its backend objects from the system-memory copies (see
  COVERAGE, "Context loss and restore").
* **Diagnostics**: `dxwebgl2_SetReport` prints the feature hit table and call counts; `dxwebgl2_SetDebug(1)` adds GL error checks after the stages of a draw, logs unsupported features and
  periodically the draw calls and what is on the back buffer.
