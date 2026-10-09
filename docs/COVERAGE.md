# API coverage (Direct3D 8)

Legend: **yes** = implemented and exercised by the tests, **yes\*** = implemented, see the note, **ignored** = accepted
(and returned by the matching `Get*`) without effect, **no** = fails with the error shown, **n/a** = not part of this
layer. The table reflects the sources in `src/d3d8`, `src/core`; the runtime test (`tests/runtime`) is the evidence for
"exercised".

Whenever an unsupported feature is used, the library logs `[dxWebGL2] unsupported: <what>` once; the diagnostics report
(see [Diagnostics](#diagnostics-and-test-aids)) also lists the approximated ones. Nothing is counted or formatted while
the report is off.

## `Direct3DCreate8` and `IDirect3D8`

| Method | State |
|---|---|
| `Direct3DCreate8(sdkVersion)` | yes (the version is not checked) |
| `QueryInterface` / `AddRef` / `Release` | yes\* `QueryInterface` returns the object itself for any IID (the objects implement the interface you already hold) |
| `RegisterSoftwareDevice` | no (`D3DERR_NOTAVAILABLE`) |
| `GetAdapterCount` | yes (1) |
| `GetAdapterIdentifier` | yes\* driver `webgl2.dll`, description from the WebGL renderer string, vendor/device ids 0 |
| `GetAdapterModeCount`, `EnumAdapterModes` | yes\* a standard list of resolutions up to the desktop size (`dxwebgl2_SetDesktopSize`), formats X8R8G8B8 and R5G6B5, 60 Hz |
| `GetAdapterDisplayMode` | yes (desktop size, X8R8G8B8) |
| `CheckDeviceType` | yes (HAL only; A8R8G8B8, X8R8G8B8, R5G6B5, A1R5G5B5, X1R5G5B5) |
| `CheckDeviceFormat` | yes (from the format table below; render targets, depth/stencil, textures, cube, volume) |
| `CheckDeviceMultiSampleType` | yes\* 2, 4 and 8 samples are answered statically; the device clamps the request to `GL_MAX_SAMPLES` (see [Multisampling](#multisampling)) |
| `CheckDepthStencilMatch` | yes (any depth format) |
| `GetDeviceCaps` | yes\* a DX8-class card with hardware T&L, see below |
| `GetAdapterMonitor` | yes\* a dummy handle |
| `CreateDevice` | yes (HAL only, one device; the focus window may be null) |

## `IDirect3DDevice8`

| Group | Methods | State |
|---|---|---|
| Lifetime | `TestCooperativeLevel`, `Reset` | yes: `D3DERR_DEVICELOST` while the WebGL context is lost, `D3DERR_DEVICENOTRESET` once the browser restored it, `Reset` recreates the WebGL objects (see [Context loss](#context-loss-and-restore)) |
| Information | `GetDirect3D`, `GetDeviceCaps`, `GetDisplayMode`, `GetCreationParameters` | yes |
| | `GetAvailableTextureMem` | yes\* fixed 256 MB |
| | `ResourceManagerDiscardBytes` | ignored |
| | `GetInfo` | no (`D3DERR_INVALIDCALL`) |
| | `GetRasterStatus` | yes\* never in vblank, scan line 0 |
| Cursor | `SetCursorProperties`, `SetCursorPosition`, `ShowCursor` | ignored (the browser draws the cursor); `ShowCursor` returns the previous setting |
| Swap chain | `CreateAdditionalSwapChain` | no (`D3DERR_NOTAVAILABLE`) |
| Presentation | `Present` | yes\* always presents the whole back buffer; the rectangles, window override and dirty region are ignored |
| | `GetBackBuffer` | yes (index 0) |
| | `GetFrontBuffer` | yes\* copies the back buffer into an `A8R8G8B8` image surface |
| | `SetGammaRamp`, `GetGammaRamp` | yes (applied by a lookup table when the frame is shown) |
| Resources | `CreateTexture`, `CreateCubeTexture`, `CreateVolumeTexture` | yes (formats below; pools are accepted, the library keeps a system-memory copy) |
| | `CreateVertexBuffer`, `CreateIndexBuffer` | yes (16-bit and 32-bit indices) |
| | `CreateRenderTarget`, `CreateDepthStencilSurface`, `CreateImageSurface` | yes (multisample types 2..8 are honoured, clamped to the GL maximum). A depth buffer that is larger than the render target, or has another sample count, gets a private one of the right size, because WebGL refuses mismatching attachments |
| | `CopyRects`, `UpdateTexture` | yes\* converts between formats; copying into a render target is not implemented |
| Targets | `SetRenderTarget`, `GetRenderTarget`, `GetDepthStencilSurface` | yes |
| Scene | `BeginScene`, `EndScene` | yes (state checked, no other effect) |
| | `Clear` | yes (rectangles, color/z/stencil) |
| Transforms | `SetTransform`, `GetTransform`, `MultiplyTransform` | yes (world, view, projection, texture 0-7; the extra world matrices exist but vertex blending is not implemented) |
| | `SetViewport`, `GetViewport`, `SetMaterial`, `GetMaterial` | yes |
| | `SetLight`, `GetLight`, `LightEnable`, `GetLightEnable` | yes (up to 8 enabled lights; directional, point, spot; light indices up to 255) |
| | `SetClipPlane`, `GetClipPlane` | yes (6 planes) |
| | `SetClipStatus`, `GetClipStatus` | ignored |
| State | `SetRenderState`, `GetRenderState` | yes, see the table below |
| | `SetTextureStageState`, `GetTextureStageState` | yes, see below |
| | `BeginStateBlock`, `EndStateBlock`, `ApplyStateBlock`, `CaptureStateBlock`, `DeleteStateBlock`, `CreateStateBlock` | yes\* blocks capture the complete state |
| | `ValidateDevice` | ignored (one pass) |
| Textures | `SetTexture`, `GetTexture` | yes |
| | `SetPaletteEntries`, `GetPaletteEntries`, `SetCurrentTexturePalette`, `GetCurrentTexturePalette` | no (`D3DERR_INVALIDCALL`; palettized formats are not texture formats) |
| Drawing | `DrawPrimitive`, `DrawIndexedPrimitive`, `DrawPrimitiveUP`, `DrawIndexedPrimitiveUP` | yes (point/line lists and strips, triangle lists/strips/fans; fill modes wireframe and point are rebuilt as lines/points) |
| | `ProcessVertices` | no (`D3DERR_INVALIDCALL`) |
| | `DrawRectPatch`, `DrawTriPatch`, `DeletePatch` | no (`D3DERR_INVALIDCALL`) |
| Streams | `SetStreamSource`, `GetStreamSource`, `SetIndices`, `GetIndices` | yes (`BaseVertexIndex` honoured) |
| Vertex shaders | `CreateVertexShader`, `SetVertexShader`, `GetVertexShader`, `DeleteVertexShader`, `Set/GetVertexShaderConstant`, `GetVertexShaderDeclaration`, `GetVertexShaderFunction` | yes: vs.1.1 (96 constants) and FVF codes as shader handles |
| Pixel shaders | `CreatePixelShader`, `SetPixelShader`, `GetPixelShader`, `DeletePixelShader`, `Set/GetPixelShaderConstant`, `GetPixelShaderFunction` | yes: ps.1.0-1.4 (32 constants) |

### Resources (`IDirect3DResource8` and derived)

| Interface | State |
|---|---|
| `IDirect3DTexture8`, `IDirect3DCubeTexture8`, `IDirect3DVolumeTexture8` | yes (`LockRect`/`LockBox`, `GetLevelDesc`, `GetSurfaceLevel`, `SetLOD`; dirty regions are ignored) |
| `IDirect3DSurface8`, `IDirect3DVolume8` | yes (`GetDesc`, `LockRect`/`LockBox`, `GetContainer`) |
| `IDirect3DVertexBuffer8`, `IDirect3DIndexBuffer8` | yes (`Lock` ranges, `D3DLOCK_*` flags, `GetDesc`) |
| `SetPrivateData` etc. | yes (stored per resource); `SetPriority`, `PreLoad` ignored |
| `IDirect3DSwapChain8` | not provided |

### Surface formats

| Format | Texture | Render target | Notes |
|---|---|---|---|
| A8R8G8B8, X8R8G8B8 | yes | yes | |
| R5G6B5, A1R5G5B5, X1R5G5B5, A4R4G4B4 | yes | yes (X4R4G4B4: texture only) | converted on upload where GL's layout differs |
| R8G8B8 | yes | no | |
| A8, L8, A8L8, A4L4 | yes | no | |
| V8U8, Q8W8V8U8 | yes | no | signed bump map formats (for `texbem`) |
| DXT1-DXT5 | yes | no | hardware S3TC when the browser exposes it, CPU decoder otherwise; CPU encoder for conversions |
| D16, D24X8, D24S8, D32, D15S1, D24X4S4, D16_LOCKABLE | n/a | depth/stencil surfaces | |
| R3G3B2, A8R3G3B2, P8, A8P8, L6V5U5, X8L8V8U8, V16U16, W11V11U10, UYVY, YUY2 | no | no | image surfaces / data only |

### Render states

*Effective*: `ZENABLE`, `ZWRITEENABLE`, `ZFUNC`, `ZBIAS`, `FILLMODE`, `SHADEMODE`, `CULLMODE`, `COLORWRITEENABLE`,
`ALPHABLENDENABLE`, `SRCBLEND`, `DESTBLEND`, `BLENDOP`, `ALPHATESTENABLE`, `ALPHAREF`, `ALPHAFUNC`, `STENCILENABLE`,
`STENCILFAIL`, `STENCILZFAIL`, `STENCILPASS`, `STENCILFUNC`, `STENCILREF`, `STENCILMASK`, `STENCILWRITEMASK`, `LIGHTING`,
`AMBIENT`, `COLORVERTEX`, the four `*MATERIALSOURCE`, `SPECULARENABLE`, `LOCALVIEWER`, `NORMALIZENORMALS`, `FOGENABLE`,
`FOGCOLOR`, `FOGTABLEMODE`, `FOGVERTEXMODE`, `FOGSTART`, `FOGEND`, `FOGDENSITY`, `RANGEFOGENABLE`, `TEXTUREFACTOR`,
`CLIPPLANEENABLE`, `POINTSIZE`, `POINTSIZE_MIN`, `POINTSIZE_MAX`, `POINTSPRITEENABLE`, `POINTSCALEENABLE`,
`POINTSCALE_A/B/C`.

*Stored, no effect*: `DITHERENABLE`, `LASTPIXEL`, `CLIPPING` (clipping is always on), `EDGEANTIALIAS`,
`MULTISAMPLEANTIALIAS`, `MULTISAMPLEMASK`, `WRAP0`-`WRAP7`, `LINEPATTERN`, `ZVISIBLE`, `SOFTWAREVERTEXPROCESSING`,
`VERTEXBLEND`, `INDEXEDVERTEXBLENDENABLE`, `TWEENFACTOR`, `PATCHEDGESTYLE`, `PATCHSEGMENTS`, `POSITIONORDER`,
`NORMALORDER`, `DEBUGMONITORTOKEN`.

`D3DRS_ZBIAS` is implemented with polygon offset, but the caps do not advertise `D3DPRASTERCAPS_ZBIAS` on purpose:
applications then bias depth through the projection matrix, which behaves identically everywhere.

### Texture stage states

All stage states are stored. Effective: `COLOROP`, `COLORARG0/1/2`, `ALPHAOP`, `ALPHAARG0/1/2`, `RESULTARG`
(`D3DTA_TEMP`), `TEXCOORDINDEX` (including `CAMERASPACENORMAL`, `CAMERASPACEPOSITION`, `CAMERASPACEREFLECTIONVECTOR`),
`TEXTURETRANSFORMFLAGS` (`COUNT1-4`, `PROJECTED`), `ADDRESSU/V/W`, `BORDERCOLOR`, `MAGFILTER`, `MINFILTER`,
`MIPFILTER`, `MIPMAPLODBIAS`, `MAXMIPLEVEL`, `MAXANISOTROPY`, `BUMPENVMAT00-11`, `BUMPENVLSCALE/LOFFSET` (for the
`texbem`/`texbeml` pixel shader instructions).

Not implemented: the fixed-function operations `D3DTOP_BUMPENVMAP` and `D3DTOP_BUMPENVMAPLUMINANCE` (treated as
`SELECTARG1`); use a pixel shader with `texbem` instead.

### Shaders

* **Vertex shaders 1.1**: all instructions (`mov add sub mul mad rcp rsq dp3 dp4 min max slt sge exp expp log logp lit dst
  frc m4x4 m4x3 m3x4 m3x3 m3x2`), address register `a0`, relative constant addressing, `def`, constants from the
  declaration (`D3DVSD_CONST`), swizzles, write masks, negation. Outputs `oPos oD0 oD1 oFog oPts oT0-3`.
* **Pixel shaders 1.0-1.4**: arithmetic (`mov add sub mul mad lrp dp3 dp4 cnd cmp`, `bem`), instruction/source modifiers
  (`_x2 _x4 _d2 _sat`, `1-r`, `-r`, `_bias`, `_bx2`), co-issue (`+`), `def`, `phase`; texture instructions `tex texld
  texcoord texcrd texkill texbem texbeml texreg2ar texreg2gb texreg2rgb texm3x2pad texm3x2tex texm3x3pad texm3x3tex
  texm3x3spec texm3x3vspec`.
  Also `texdp3`, `texdp3tex`, `texm3x3`, `texm3x2depth` (ps.1.2/1.3) and `texdepth` (ps.1.4; the depth is written through
  `gl_FragDepth`, mapped to the viewport depth range). Not supported (creation fails and is counted by the diagnostics
  report): `texm3x3diff`.
* The shader versions are reported in the caps only if you call `dxwebgl2_SetShaderModel` (or pass
  `-dxwebgl2-shaders` on the command line); the default is "no programmable shaders" so that applications keep to the
  fixed-function pipeline.
* `D3DXAssembleShader` assembles vs.1.x and ps.1.x text (comments, `\` line continuations of C string literals,
  co-issue, all modifiers).

### Capabilities (`D3DCAPS8`)

HAL device, `D3DCAPS2_FULLSCREENGAMMA | CANRENDERWINDOWED | DYNAMICTEXTURES | CANMANAGERESOURCE`, hardware T&L
(`D3DDEVCAPS_HWTRANSFORMANDLIGHT`), texture size min(GL maximum, 4096), 8 texture stages and 4 simultaneous textures,
8 lights, 6 clip planes, 16 streams, 3D textures up to 256, cube maps, anisotropic filtering up to the GL maximum,
point sprites, all blend factors, `D3DPRASTERCAPS_FOGTABLE/FOGVERTEX/FOGRANGE/ANISOTROPY/MIPMAPLODBIAS`, 8 stencil
operations, all texture operations except the bump environment ones, no vertex blending (`MaxVertexBlendMatrices == 0`),
no patches, multisampling up to `GL_MAX_SAMPLES`. `GetDeviceCaps` on the device refines the texture limits from the actual GL
implementation.

## D3DX 8

| Area | State |
|---|---|
| `D3DXFilterTexture`, `D3DXLoadSurfaceFromSurface/Memory` filters | all filter types behave as documented: `POINT` (nearest texel), `LINEAR` (four nearest texels), `TRIANGLE` (every covered texel contributes equally), `BOX` (area average; an enlarging box falls back to linear), `NONE` (copy the overlap, transparent black elsewhere), the `MIRROR_U/V` flags; `DITHER` is ignored (8 bit targets) | yes |
| Math: `D3DXVECTOR2/3/4`, `D3DXMATRIX`, `D3DXMATRIXA16`, `D3DXQUATERNION`, `D3DXPLANE`, `D3DXCOLOR` with all operators and every `D3DXVec*`, `D3DXMatrix*`, `D3DXQuaternion*`, `D3DXPlane*`, `D3DXColor*`, `D3DXFresnelTerm` of D3DX 8 | yes |
| `D3DXCreateMatrixStack`, `ID3DXMatrixStack` | yes |
| `ID3DXBuffer`, `D3DXGetErrorStringA/W`, `D3DXGetFVFVertexSize`, `D3DXAssembleShader`, `D3DXAssembleShaderFromFileA/W` | yes |
| `D3DXCheckTextureRequirements` (2D, cube, volume), `D3DXCreateTexture`, `D3DXCreateCubeTexture`, `D3DXCreateVolumeTexture` | yes |
| `D3DXLoadSurfaceFromSurface`, `D3DXLoadSurfaceFromMemory` (filters NONE, POINT, LINEAR, TRIANGLE, BOX; color key) | yes |
| `D3DXGetImageInfoFromFileInMemory/A/W`, `D3DXLoadSurfaceFromFileInMemory/A/W`, `D3DXCreateTextureFromFile[InMemory][Ex]` (DDS, TGA, BMP) | yes (PNG and JPEG are not decoded) |
| `D3DXFilterTexture` (mip chain generation, 2D / cube / volume), `D3DXFillTexture` | yes |
| `D3DXFillCubeTexture`, `D3DXFillVolumeTexture`, `D3DXSaveSurfaceToFile`, `D3DXSaveTextureToFile`, `D3DXComputeNormalMap`, `D3DXCreateCubeTextureFromFile*`, `D3DXCreateVolumeTextureFromFile*`, `*FromResource*`, `D3DXLoadVolumeFrom*` | no (not declared) |
| Meshes (`ID3DXMesh`, ...), shapes (`D3DXCreateBox`, ...), effects, `ID3DXFont`, `ID3DXSprite`, `ID3DXRenderToSurface` | no (not declared) |

Things that differ from Microsoft's D3DX on purpose or by limitation: images are decoded by the library itself
(DDS/TGA/BMP), the assembler supports only shader models 1.x, `D3DXMatrixInverse` returns null for a singular matrix.

## Context loss and restore

WebGL contexts are lost on GPU resets, driver crashes, sleep/wake and GPU switches. The device installs
`webglcontextlost` (with `preventDefault`, otherwise the browser never restores the context) and `webglcontextrestored`
listeners on the canvas (this works on the `OffscreenCanvas` of a rendering thread, too) and polls `isContextLost()` from
`TestCooperativeLevel`, `Present` and `Clear`.

1. Lost: `Present`, `Clear` and `TestCooperativeLevel` return `D3DERR_DEVICELOST`, draws are skipped, every WebGL object
   forgets its name. `TestCooperativeLevel` also yields to the browser through the `OnFramePresented` hook
   (`dxwebgl2_SetPlatformHooks`), because applications stop calling `Present` while the device is lost and the restore event
   can only run when the thread yields.
2. Restored: `TestCooperativeLevel` returns `D3DERR_DEVICENOTRESET`; the application releases its default-pool resources
   and calls `Reset()`, as it would after a Direct3D 8 device loss.
3. `Reset()` creates the WebGL objects again: every vertex and index buffer from its system memory copy, every managed
   texture from its shadow copy (default-pool textures come back empty, as in Direct3D), render targets and depth buffers
   empty; programs recompile lazily; the present program, the vertex array cache and the samplers are rebuilt.

Test aids: `dxwebgl2_LoseContextAfterFrames`, `dxwebgl2_LoseContextNow`, `-dxwebgl2-loseafter=N[,M]`. Test:
`d3d8_features_test --mode=contextloss` (two losses, a texture created while lost, render target, pixel shader, managed
buffers).

## Multisampling

`CreateDevice` / `Reset` with `MultiSampleType` 2..8 and `D3DSWAPEFFECT_DISCARD` render into a multisampled renderbuffer
(with a multisampled depth buffer) and resolve into the back buffer texture with `glBlitFramebuffer` before `Present`,
`GetFrontBuffer`, `CopyRects`, `LockRect` or a read-back. `D3DSURFACE_DESC::MultiSampleType` is reported, so applications
that refuse render-to-texture effects under multisampling (as Direct3D 8 requires) keep working. When the context offers
fewer samples the count is clamped (reported as approximated); when it offers none the device is created without
multisampling. Tests: `--mode=msaa`, `--mode=msaa-off`.

## Diagnostics and test aids

`dxwebgl2_SetReport(seconds)` (or `-dxwebgl2-report[=seconds]` on the command line, `?arg=-dxwebgl2-report` in the page
URL) prints, every interval, the draw calls, the WebGL calls by kind and the Direct3D calls per frame, the uniform groups
uploaded, programs compiled, context losses, and a table of every feature that was **unsupported**, **approximated**,
**failed** (a create or a compile), an **event** (context lost / restored) or a **slow path** (GPU read-back, CPU DXT
decode). Nothing is counted or formatted while the report is off. `dxwebgl2_GetStats` returns the counters of the last
frame, `dxwebgl2_GetHitCount` the number of hits of a feature (while the report is on).

Other aids: `dxwebgl2_SetFeatureOverrides` (pretend that optional WebGL extensions are missing, to test the fallbacks),
`-dxwebgl2-shaders` (advertise vs.1.1 / ps.1.4), `-dxwebgl2-debug[=flags]`.

## Performance notes

The device keeps WebGL work low: per-uniform-group version counters with packed uniform arrays (a state change costs one
call per program that uses it), a cache of vertex array objects keyed by layout / buffers / strides (the base vertex is
passed to the draw call when `WEBGL_draw_instanced_base_vertex_base_instance` exists, otherwise the vertex array is
promoted after the second sighting), attachment caching for framebuffers, `Clear` / `Present` keeping the pipeline
cache valid, point size code only in point programs, redundant `SetTransform` filtering. A typical scene of about 80 draws
needs about 400 WebGL calls per frame with no vertex attribute calls and no synchronous calls. The feature test
`state_cache` guards it (at most 3 WebGL calls per repeated draw).

## Running the checks

```
scripts/test.sh                                   # everything (configure, build, ctest, consumers, browser tests)
node tests/runner/run_regression.mjs <build>/tests   # frame hashes of the runtime test scenes (tests/runner/reference_hashes.json)
node tests/runner/run_features.mjs   <build>/tests   # 7 modes of d3d8_features_test, checked pixels
```

The hashes are those of headless Chromium with SwiftShader; another renderer may differ in the last bit (`--update`
rewrites the reference file after an intended change).
