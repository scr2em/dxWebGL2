# Roadmap: Direct3D 9

Status: **planned, nothing of Direct3D 9 is implemented yet.** This document is the concrete plan, the list of
differences that the code has to absorb, and the preparation that is already in the repository.

The goal is `Direct3DCreate9()` / `IDirect3DDevice9` (plus a D3DX 9 subset) on the same WebGL2 core, so that Direct3D 9 era
renderers (SM2/SM3 shaders, vertex declarations, sampler states) can be compiled for the browser like Direct3D 8 ones.
`IDirect3D9Ex`/`IDirect3DDevice9Ex` (Windows Vista features), Direct3D 10/11 and HLSL compilation at run time are not goals.

## What is already prepared

* **Two layers** ([ARCHITECTURE.md](ARCHITECTURE.md)): `src/core` has no COM and no Direct3D 8 interface in sight; a new
  `src/d3d9` front end sits next to `src/d3d8`, sharing `src/api` (COM base, `Ref<T>`) and `src/core`.
* **One header decides the API vocabulary of the core**: `src/core/api_types.h`. `D3DFORMAT`, `D3DPOOL`, `D3DMATRIX`,
  the FVF bit layout, the vertex shader token layout of shader model 1 and the render state / texture stage state numbering
  are the same numbers in Direct3D 8 and 9 (9 is a superset: `d3d9types.h` extends the enumerations). The core therefore
  compiles against either set of headers.
* **Header supply**: Wine also has `d3d9.h`, `d3d9types.h`, `d3d9caps.h` and `d3dx9*.h` under the same license as the
  Direct3D 8 ones (LGPL-2.1-or-later, mingw-w64 carries copies). They are vendored the way `third_party/wine` is,
  with `tools/check_header_compat.py` generalized to compare the D3D9 interfaces and values against a reference.
* **Keys, not objects**: programs and pipeline state are described by plain structures (`ProgramKey`,
  `PipelineState`) that a front end fills; adding a second front end does not touch generated code.
* **Resource storage independent of COM**: `BufferStorage`, `TextureBase`, `SurfaceStorage` (the `Backend` interface is
  their only dependency).
* **The GPU work is behind `core/backend.h`**: context, back buffer, presentation, device loss, resources, programs and
  the draw path (`Backend::Draw(DrawCall)`) are in the WebGL2 backend (`src/core/gl`); a front end only translates its
  state (see [WEBGPU.md](WEBGPU.md), which uses the same seam for a WebGPU backend).
* **Translators take token streams** and the shader emitters are isolated in `src/core/shader_*.cpp`.

## Differences to absorb

### Device and object model

| Direct3D 8 | Direct3D 9 | Plan |
|---|---|---|
| `Direct3DCreate8(220)` | `Direct3DCreate9(D3D_SDK_VERSION)`; `IDirect3D9::EnumAdapterModes(adapter, format, index)`, `GetAdapterModeCount(adapter, format)`, `CheckDeviceFormatConversion`, `GetDeviceCaps` -> `D3DCAPS9` (larger, `MaxVertexShader30InstructionSlots`, ...) | new `Direct3D9` object next to `Direct3D8`; caps filled by one shared function with per-version tail |
| `SetVertexShader(handle)` takes FVF *or* shader handle | `SetFVF`, `SetVertexShader(IDirect3DVertexShader9*)`, `SetVertexDeclaration(IDirect3DVertexDeclaration9*)`, `SetPixelShader(IDirect3DPixelShader9*)` | shader and declaration objects become real COM objects wrapping the core's `VertexShaderObject` / `PixelShaderObject` / `VertexLayout` |
| `SetStreamSource(n, vb, stride)`, `SetIndices(ib, baseVertex)` | `SetStreamSource(n, vb, offset, stride)`, `SetIndices(ib)`, base vertex in `DrawIndexedPrimitive(type, BaseVertexIndex, MinVertexIndex, NumVertices, StartIndex, PrimCount)`, `SetStreamSourceFreq` (instancing) | `BindAttributes` already takes a base vertex; add the byte offset; instancing maps to `glVertexAttribDivisor` + `glDrawElementsInstanced` |
| Pools: `DEFAULT / MANAGED / SYSTEMMEM` | adds `SCRATCH`, `D3DUSAGE_DYNAMIC`, `D3DLOCK_DISCARD / NOOVERWRITE` semantics that matter, `CreateX(..., HANDLE *sharedHandle)` | `BufferStorage::Lock` flags already distinguish discard; honor `DYNAMIC` with `glBufferData(NULL)` orphaning |
| `CopyRects`, `UpdateTexture`, `GetFrontBuffer` | `StretchRect`, `UpdateSurface`, `UpdateTexture`, `GetRenderTargetData`, `ColorFill`, `GetFrontBufferData`, `GetBackBuffer(swapchain, index, type)`; swap chains: `GetSwapChain`, `CreateAdditionalSwapChain` | `StretchRect` -> `glBlitFramebuffer`; additional swap chains stay unsupported (one canvas) |
| `SetRenderTarget(rt, ds)` | `SetRenderTarget(index, rt)` (multiple render targets), `SetDepthStencilSurface`, `D3DRS_COLORWRITEENABLE1-3` | `glDrawBuffers` (WebGL2 guarantees at least 4); `MaxSimultaneousRenderTargets = min(4, GL_MAX_DRAW_BUFFERS)` |
| `D3DPRESENT_PARAMETERS` (d3d8 layout) | different layout: `MultiSampleQuality`, `hDeviceWindow`, `FullScreen_RefreshRateInHz`, `PresentationInterval` position; `Present` flags, `PresentEx` not needed | separate struct, same `Present` path |
| none | queries: `CreateQuery(D3DQUERYTYPE_EVENT / OCCLUSION / TIMESTAMP)` | `EVENT` -> `glFenceSync`; `OCCLUSION` -> `ANY_SAMPLES_PASSED` queries (WebGL2 core) |
| `D3DFMT_*` of DX8 | adds `A16B16G16R16F`, `R32F`, `A32B32G32R32F`, `G16R16`, `A2R10G10B10`, `D32F_LOCKABLE`, `INTZ`/`DF24` FOURCC depth textures, `NULL`... | extend `core/format.cpp` (`GL_RGBA16F` needs `EXT_color_buffer_float`, `EXT_float_blend`) |

### State: render states, sampler states, texture stage states

* **`SetSamplerState(sampler, D3DSAMP_*, value)`** replaces the *sampling* half of the texture stage states
  (`ADDRESSU/V/W`, `BORDERCOLOR`, `MAGFILTER/MINFILTER/MIPFILTER`, `MIPMAPLODBIAS`, `MAXMIPLEVEL`, `MAXANISOTROPY`;
  plus `SRGBTEXTURE`, `ELEMENTINDEX`, `DMAPOFFSET`), and addresses samplers 0-15 plus the vertex samplers
  `D3DVERTEXTEXTURESAMPLER0-3`. In Direct3D 9 the *combiner* half (`COLOROP`, `COLORARG*`, `ALPHAOP`, `TEXCOORDINDEX`,
  `TEXTURETRANSFORMFLAGS`, `RESULTARG`, `CONSTANT`, `BUMPENV*`) stays in `SetTextureStageState` for the fixed-function
  pipeline. The code already separates the two worlds: `StageKey` (combiner, part of the core's `ProgramKey`) and the
  sampler objects (`SamplerDesc` in `core/backend.h`: filters, addresses and anisotropy in `D3DTEXF_*`/`D3DTADDRESS_*`
  numbering, cached by the backend, `Backend::GetSampler`; LOD bias and border color are shader uniforms). The Direct3D 9
  front end fills the same `SamplerDesc` from `D3DSAMP_*`; the Direct3D 8 front end keeps deriving it from the texture
  stage states (`Device::SamplerFor`). The Direct3D 9 front end's state gets `samplers[20]`.
* **Render states** are mostly additions: `D3DRS_DEPTHBIAS`/`SLOPESCALEDEPTHBIAS` (float, replaces `ZBIAS`) ->
  `glPolygonOffset`; `SCISSORTESTENABLE` + `SetScissorRect`; `BLENDFACTOR`, `SEPARATEALPHABLENDENABLE`,
  `SRCBLENDALPHA/DESTBLENDALPHA/BLENDOPALPHA` -> `glBlendFuncSeparate`/`glBlendEquationSeparate` (`PipelineState`
  gets the alpha halves); two sided stencil (`TWOSIDEDSTENCILMODE`, `CCW_STENCIL*`) -> `glStencilFuncSeparate`;
  `SRGBWRITEENABLE` -> `FRAMEBUFFER_SRGB`-like handling in the shader epilogue (WebGL2 has no enable; convert in the
  fragment shader when the target is not an sRGB format); `WRAP0-15`; `ANTIALIASEDLINEENABLE` ignored;
  `D3DRS_MULTISAMPLEMASK` stays ignored. `ZBIAS`, `LINEPATTERN`, `ZVISIBLE`, `EDGEANTIALIAS` disappear.
* **Fixed-function** is unchanged except for the vertex formats of the declaration path (below), `D3DRS_COLORVERTEX`
  semantics are the same, `SetTexture(stage)` addresses 16 stages and the combiner works on the first 8.
* `D3DRS_FOGVERTEXMODE`/`RANGEFOGENABLE` are the same; the pixel shader fog of SM3 applications is explicit.

### Vertex declarations

`D3DVERTEXELEMENT9 { Stream, Offset, Type, Method, Usage, UsageIndex }` replace the `D3DVSD_*` token streams.

* New front-end parser `src/d3d9/vertex_declaration.cpp` producing the same `VertexLayout` (`VertexElement` already
  holds stream, offset, component count, GL type, normalized flag and the "D3DCOLOR needs a BGRA swizzle" flag).
* `D3DDECLTYPE_*`: `FLOAT1-4`, `D3DCOLOR`, `UBYTE4`, `SHORT2/4` map as today; new: `UBYTE4N`, `SHORT2N/4N`,
  `USHORT2N/4N` (normalized integer attributes), `UDEC3`/`DEC3N` (`GL_INT_2_10_10_10_REV`, note the 3-component
  case), `FLOAT16_2/4` (`GL_HALF_FLOAT`).
* **Semantics -> attribute locations**: in Direct3D 8 the register number *is* the location (`D3DVSDE_*`). In Direct3D 9
  vertex shader inputs are matched by semantic (`dcl_position0`, `dcl_texcoord3`, ...) with the usage taken from the
  shader's `dcl` instructions and the declaration. The core keeps *fixed locations per (usage, index)* (position 0,
  blend weight 1, blend indices 2, normal 3, psize 4, color0 5, color1 6, texcoord0-7 at 7-14, tangent/binormal/
  tessfactor/positiont/color/fog/depth/sample above, within the 16 locations GL guarantees: bind with
  `glBindAttribLocation` before linking) so that `VertexLayout` stays independent of the shader; the SM2/3 translator
  declares `in vec4 in_<usage><index>` with `layout(location=N)`.
* Fixed-function vertex processing from a declaration needs the FVF-equivalent decode (`LayoutFromFVF` has the
  mapping); declarations that are not FVF-expressible require a vertex shader, as in Direct3D 9.

### Shader models 2.0 / 2.x / 3.0

Today `shader_vs.cpp` / `shader_ps.cpp` decode **shader model 1** token streams (`0xFFFE0101`, `0xFFFF0104`) straight
into GLSL fragments. Plan, in this order:

1. **Split decode from emit** (refactor without behavior change, covered by the existing shader tests): tokens ->
   `ShaderIR` (opcode enum independent of the encoding, typed registers `r t v c a o`, source modifiers, write masks and
   swizzles, `def` tables) -> GLSL. The SM1 decoders become the first producers. `PixelShaderObject::Generate(key)`
   stays the entry point since sampler types come from the program key (SM1) or from `dcl_2d/cube/volume` (SM2/3).
2. **SM2/SM3 decoder** (`shader_decode_sm2.cpp`): version tokens `0xFFFE0200/0300`, `0xFFFF0200/0300`; the instruction
   length is in bits 24-27 of the opcode token (SM1 has none); `dcl` (usage/index, samplers), `def`/`defi`/`defb`;
   registers `v# o# oPos oD# oT# oFog oPts oC# oDepth s# vPos vFace aL a0 b# i# p0 l#`; `D3DSPR_*` register type
   extension bits (`RegisterType` high bits at 11-12); relative addressing on `a0`/`aL`; source modifiers `abs`, `abs+neg`, `not`,
   `x2`, `dz`, `dw`; `_pp`, `_centroid` (ignored), `_sat`.
3. **Instructions** to add on top of SM1: `abs`, `crs`, `dp2add`, `nrm`, `pow`, `sincos`, `lrp` (already), `cmp`
   (already), `mova`, `sge/slt` (already), `exp/log/rcp/rsq` (already), `texld/texldp/texldb/texldl/texkill/texldd`
   (`texture(sampler, c)` / `textureProj` / bias / `textureLod` / `textureGrad`), `dsx/dsy` (`dFdx/dFdy`),
   `if/ifc/else/endif`, `rep/endrep`, `loop/endloop`, `break/breakc/breakp`, `call/callnz/label/ret`, `setp`,
   `lit/dst/frc/min/max/mad/mul/add/dp3/dp4/m4x4...` (already).
4. **Constants**: `float4 c#` (vs up to 256 in SM2 and 256 for SM3; ps 32 (SM2) / 224 (SM3)), `int4 i#`
   (`ivec4` uniforms, loop counters), `bool b#` (int uniforms). WebGL2 guarantees 256 vertex and 224 fragment uniform vectors; the
   translator queries `GL_MAX_*_UNIFORM_VECTORS` and fails shader creation (logged) beyond that. Constant data comes through uniform
   arrays as for SM1, uploaded by the existing version-counter groups (`G_VSC/G_PSC`), with additional groups for `i`/`b`.
5. **Control flow**: GLSL ES 3.00 has real `if`/`for`/`while` (no WebGL1 limits); `loop aL, i0` -> `for` with the
   bounds from the integer constant; non-constant `rep`/`call` map to structured code, `label`/`call` become functions
   (SM2.x/3 subroutines); `break`/`breakc` map directly. Irreducible flow (not producible by the HLSL compiler) is rejected.
6. **SM3 specifics**: `vPos` -> `gl_FragCoord` (with the y flip of the "flipped rendering" convention), `vFace` ->
   `gl_FrontFacing` (as +/-1), `oDepth` -> `gl_FragDepth`, `o#` output registers with `dcl` semantics mapped to varying
   locations by (usage, index) so vertex and pixel shaders link regardless of register numbers, vertex texture fetch
   (`texldl` in a vertex shader, `D3DVERTEXTEXTURESAMPLER*`: WebGL2 `MAX_VERTEX_TEXTURE_IMAGE_UNITS` >= 16; the filter modes are
   limited to what the GPU provides).
7. **Multiple render targets** `oC0-3` -> `layout(location=n) out vec4`.
8. Out of scope: shader debugging info, `D3DXCompileShader` (needs an HLSL compiler; applications ship `fxc` bytecode)
   and effect files (`ID3DXEffect`, a separate layer on top of D3DX).

### D3DX 9 subset

Same approach as D3DX 8, vendored Wine `d3dx9*.h` for the declarations and own implementations of what is needed:

1. Math: everything of D3DX 8 plus `D3DXMatrixDecompose`, `D3DXMatrixTranspose`, `D3DXVec*Array`, `D3DXFLOAT16`, SH (low).
2. Textures and surfaces: `D3DXCreateTexture/CubeTexture/VolumeTexture`, `D3DXCreateTextureFromFile[InMemory][Ex]`
   (DDS, TGA, BMP + **PNG and JPEG** through the browser's decoders, `createImageBitmap`, or small embedded decoders), `D3DXLoadSurfaceFrom*`,
   `D3DXFilterTexture`, `D3DXGetImageInfo*`, `D3DXSaveTextureToFileInMemory` (PNG), `D3DXComputeNormalMap`.
3. `ID3DXBuffer`, `D3DXCreateBuffer`, `D3DXCreateMatrixStack`, `D3DXAssembleShader` for SM2/3 syntax.
4. `ID3DXSprite` (a batched textured quad renderer on top of the device, straightforward), `ID3DXFont` (glyphs from an
   offscreen 2D canvas), simple shapes (`D3DXCreateBox/Sphere/Cylinder/Torus/Polygon/Teapot`) and the basic `ID3DXMesh`
   operations (`DrawSubset`, `LockVertexBuffer`, `OptimizeInplace` as no-op, `ComputeNormals`).
5. Not planned: effects, skinned meshes, progressive meshes, `D3DXCompileShader`.

## Extraction steps in the code

The Direct3D 9 front end needs the draw path of today's `Device`. In this order (each step keeps the Direct3D 8 tests
green, the runtime test renders identical frames):

| Step | Content | Result |
|---|---|---|
| 1 | **Done** (backend seam): `CreateContext`, `QueryGLCaps`, the present program, the back buffer and the context-loss handling moved from `d3d8/device.cpp` into the WebGL2 backend (`core/gl/gl_backend.cpp`) | `Backend` owned by the front end device |
| 2 | **Done** (backend seam): `DrawCall` is the neutral description of one draw (pipeline state, bound textures + sampler handles, stream bindings, program, uniform versions and source); `ApplyPipeline`, `BindTextures`, `UploadUniforms`, `BindAttributes`, `UploadStream` and the draw are in `Backend::Draw(const DrawCall&)` (`core/gl/gl_draw.cpp`) | the D3D8 device only translates its state into a `DrawCall` |
| 3 | Move `BuildProgramKey` to take a neutral `FixedFunctionState` (lighting, fog, stage combiners ...) | shared by both fixed-function front ends |
| 4 | `ShaderIR` split, then the SM2/SM3 decoder (above) | pixel/vertex shaders 2.0-3.0 |
| 5 | `src/d3d9` front end: `Direct3D9`, `Device9`, resources (the `Surface`/`Texture`/`Buffer` COM wrappers are mostly API shaped, copy and adapt), declarations, samplers, queries | a Direct3D 9 `d3d9_runtime_test` |
| 6 | D3DX 9 subset | |

## Milestones

* **M1 (refactor)**: steps 1-3 (1 and 2 done with the backend seam). No new API; all existing tests and the frame hashes unchanged.
* **M2 (SM IR)**: step 4 first half; SM1 through the IR with identical output GLSL (golden strings in tests).
* **M3 (D3D9 fixed-function)**: `Direct3DCreate9`, device, resources, declarations, samplers, state; the D3D9 port of the runtime test
  (scenes A-D, fixed-function) passes; header compatibility check for D3D9 (`check_header_compat.py` with a D3D9 reference).
* **M4 (SM2/3)**: translator, `dcl`, loops/branches, MRT, `vPos`/`vFace`; a shader conformance test that compares against
  reference pictures (a small set of known SM2/3 shaders written as bytecode with the assembler).
* **M5 (D3DX 9)**: textures/surfaces/math/sprite/font.
* **M6**: instancing, queries, float formats, sRGB.

## Test strategy for D3D9

* `tests/runtime/d3d8_runtime_test.cpp` has a Direct3D 9 twin built from the same scene descriptions; probe tables per
  API (`expected_probes.inc`) since the pixel centers/texel conventions are the same but a few defaults differ.
* Interface golden file and the ABI header check run for the D3D9 headers too (`tests/data/d3d9_*`).
* Layering check (`tools/check_layering.py`) gets `src/d3d9` with the same rules as `src/d3d8`.
