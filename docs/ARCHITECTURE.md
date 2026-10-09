# Architecture

dxWebGL2 is organized in layers so that a second API (Direct3D 9, see [ROADMAP.md](ROADMAP.md)) can be put on top of
the same WebGL2 machinery.

```
include/dxwebgl2/      public C API (dxwebgl2.h) and the Win32/COM base (win32_min.h)
include/dx8/           D3DX 8 headers (own)          third_party/wine/include: d3d8*.h (Wine)
src/api/               helpers shared by the API front ends: COM plumbing (IUnknown base, Ref<T>)
src/d3d8/              the Direct3D 8 front end
src/core/              the WebGL2 core
```

`tools/check_layering.py` (run by `scripts/test.sh`) enforces the dependency rule:

* `src/core` includes only `src/core` (and the public `dxwebgl2/` headers) and never mentions an `IDirect3D*` COM
  interface;
* `src/api` may include `src/core`;
* `src/d3d8` may include `src/core` and `src/api`.

## The core (`src/core`)

| File | Contents |
|---|---|
| `common.h`, `util.cpp` | configuration set through the C API (`Config`), logging, row-major `Mat4` helpers, small utilities, UTF-16 to UTF-8 |
| `api_types.h` | the single place that decides which API headers the core speaks in (today `<d3d8.h>`; a Direct3D 9 build switches to `<d3d9.h>`) |
| `types.h` | limits (stages, streams, constants), `GLCaps`, `VertexLayout`/`VertexElement`, `PipelineState` (the GL state derived from render states) |
| `gl_state.h/.cpp` | `GLState`: capabilities, the cache of what is bound to the buffer targets and texture units, the upload texture unit, read-back through a scratch framebuffer, and the registry of `GLObject`s (everything that owns WebGL names, so that all of it learns about a context loss and restore). A front end's device derives from it and overrides a few hooks for the caches it keeps itself (element array binding of the bound vertex array object, framebuffer attachment keys, the multisampled back buffer) |
| `diag.*`, `glcount.h` | the diagnostics: hit table of unsupported / approximated features, per-frame Direct3D and WebGL call counters, the periodic report, `-dxwebgl2-*` command line options. Off by default; every hit site tests one flag |
| `buffer_storage.*` | `BufferStorage`: a CPU copy of a vertex/index buffer plus the GL buffer, modified ranges are uploaded lazily right before a draw |
| `texture_storage.*` | `TextureBase`: 2D, cube and volume textures with their mip levels, a system-memory shadow copy per level, locking, upload (with format conversion), read-back |
| `format.*` | the table of surface formats (`D3DFORMAT` to GL internal format/type), pixel conversion between formats, DXT1-5 decoder (CPU path when S3TC is missing) and encoder |
| `vertex_layout.cpp` | vertex layouts from FVF codes |
| `program.*` | `ProgramKey` (everything that influences the generated shader code), program compile/link, uniform lookup |
| `fixedfunction.cpp` | generates GLSL ES 3.00 for the fixed-function pipeline from a `ProgramKey`: transform, lighting (up to 8 lights), fog, point sprites, clip planes, the texture stage cascade, alpha test |
| `shader.h`, `shader_internal.h`, `shader_vs.cpp`, `shader_ps.cpp`, `shader_assembler.cpp` | the shader bytecode translators: vs.1.1 and ps.1.0-1.4 token streams to GLSL, and the text assembler behind `D3DXAssembleShader` |

### Why this split is the base for Direct3D 9

* **Programs and shaders are keyed by state, not by API objects.** A front end fills a `ProgramKey` from whatever its
  API calls "state"; the core generates, compiles and caches the program. Direct3D 9 differs in how state is set
  (sampler states instead of texture stage state, vertex declarations), not in what the pipeline needs.
* **Resources are independent of COM.** `BufferStorage` and `TextureBase` need only `GLState`; the front end wraps them
  into its interface objects.
* **The translators work on token streams.** The version token and the instruction encoding decide the parser; shader
  model 2.0/3.0 adds a second decoder in front of the same GLSL emitter, see ROADMAP.

What is still in the Direct3D 8 front end and moves to the core when the second front end needs it
(see ROADMAP, "Extraction steps"): the device's draw path (`ApplyPipeline`, texture binding, uniform upload,
attribute binding), context creation, presentation and the back buffer.

## The Direct3D 8 front end (`src/d3d8`)

| File | Contents |
|---|---|
| `d3d8.cpp` | `Direct3DCreate8`, the `IDirect3D8` object (adapter, modes, format checks), the `D3DCAPS8` (static; refined once a device exists), `dxwebgl2_LookupProc` |
| `device.h/.cpp` | the `IDirect3DDevice8` implementation: creation, WebGL2 context, back buffer (multisampled when requested), present, context loss detection and restore, queries, resource creation, render target handling, the vertex array object cache |
| `device_state.cpp` | render states, texture stage states, transforms, lights, state blocks, streams, shader objects and constants |
| `device_draw.cpp` | the draw path (see below), `Clear` |
| `resources.h`, `buffers.cpp`, `texture.cpp` | the COM objects for textures, surfaces, volumes and buffers on top of `BufferStorage` / `TextureBase` |
| `vertex_declaration.cpp` | `D3DVSD_*` vertex declarations to a `VertexLayout` |
| `d3dx_*.cpp` | the D3DX 8 functions (math, matrix stack, assembler front end/buffers/error strings, textures and surfaces) |

## Life of a draw call

1. `DrawIndexedPrimitive(...)` -> `Device::DrawCommon` validates the stream setup.
2. `PrepareDraw`:
   * `SelectProgram`: when render state that influences code changed (`m_keyDirty`), a `ProgramKey` is built from the
     device state (`BuildProgramKey`: vertex format, lighting, fog, per stage color/alpha ops and texture types ...).
     The device looks the key up in its program cache; on a miss the GLSL is generated (`GenerateFixedFunction*` or the
     translated shader) and compiled/linked (`CreateProgram`).
   * `ApplyRenderTargets`: attaches the current render target / depth surface to the framebuffer.
   * `ApplyPipeline`: derives a `PipelineState` (blend, depth, cull, stencil, scissor, viewport...) from the render
     states and applies only the differences to what was applied before.
   * `UploadUniforms`: only the uniform groups whose version counter changed since this program last received them.
   * `BindTextures`: textures and (cached) sampler objects per stage; stages without a texture get a 1x1 white texture.
3. `BindAttributes`: the vertex buffers are flushed (`BufferStorage::Flush`, uploading the locked range) and the
   attribute pointers set from the `VertexLayout`; user-pointer draws stream through ring buffers.
4. `glDrawElements` / `glDrawArrays` with the primitive type mapped (fans and wireframe handled).

## Conventions that matter when reading or changing the code

* **Everything is rendered "flipped"**: row 0 of every framebuffer attachment is the top row of the image, exactly like
  Direct3D surfaces, so render targets sampled as textures and surface read-backs need no vertical flip. The vertex
  stage negates clip-space Y and the winding order is adjusted. `Present()` flips the back buffer when drawing it to
  the canvas.
* **Clip space depth** is converted from Direct3D's `[0, w]` to GL's `[-w, w]` in the vertex shader, and half a pixel is
  added to emulate Direct3D 8's pixel centers.
* **Matrices are row-major with row vectors** like Direct3D. They are uploaded as they are (`transpose = GL_FALSE`),
  which GL reads as the transposed matrix, so the generated shaders multiply `matrix * vector`.
* **Vertex attribute locations equal the `D3DVSDE_*` register numbers**, so declarations map one to one; `D3DCOLOR`
  attributes are swizzled to RGBA in the vertex shader.
* **GL call cost matters** (each WebGL call crosses from WebAssembly into JavaScript): bind caches in `GLState`,
  per-group uniform versions, lazy buffer uploads, a sampler object cache, state diffing in `ApplyPipeline`.
* **Managed resources keep a system-memory copy** (`D3DPOOL_MANAGED`): it makes `Lock` cheap and lets resources be
  re-uploaded. `dxwebgl2_SetReleaseTextureShadows(1)` drops the copy after upload at the price of a read-back on the next
  lock.
* **The WebGL context can disappear**: `Device::CheckContext` polls it, `GLObject::OnContextLost` / `OnContextRestored` let every
  resource drop and recreate its WebGL names from the system-memory copies (see COVERAGE, "Context loss and restore").
* **Diagnostics**: `dxwebgl2_SetReport` prints the feature hit table and call counts; `dxwebgl2_SetDebug(1)` adds GL error checks after the stages of a draw, logs unsupported features and
  periodically the draw calls and what is on the back buffer.
