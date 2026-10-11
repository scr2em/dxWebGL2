/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * The backend interface: everything a Direct3D front end asks of the GPU API underneath.
 *
 * A front end (src/d3d8) translates its API's state into the neutral descriptions below and hands them to a Backend:
 * a draw is one DrawCall carrying the complete pipeline state, the program, the uniform data, the texture and sampler
 * bindings and the vertex/index input, not a sequence of GL state changes. That is the granularity WebGPU works at
 * (render pipelines keyed by state, bind groups, uniform buffers); the WebGL2 backend keeps its state cache and diffs
 * the description against what it applied before. GPU objects are opaque handles (GpuBuffer, GpuTexture, ...) that
 * only the backend that created them looks into. See docs/WEBGPU.md for the design and how WebGPU maps onto it.
 *
 * The core's vocabulary stays Direct3D's where the numbers are shared by Direct3D 8 and 9 (D3DFORMAT, D3DTEXF_*,
 * D3DTADDRESS_*, see core/api_types.h); pipeline state uses small neutral enumerations.
 */
#pragma once

#include "core/types.h"
#include "core/program.h"

namespace dxwebgl2 {

class Backend;
class BufferStorage;
class TextureBase;
class SurfaceStorage;
class VertexShaderObject;
class PixelShaderObject;

//------------------------------------------------------------------------------
// Backend objects (opaque to the front ends; each backend derives its own)
//------------------------------------------------------------------------------
struct GpuBuffer { protected: GpuBuffer() = default; ~GpuBuffer() = default; };
struct GpuTexture { protected: GpuTexture() = default; ~GpuTexture() = default; };
struct GpuSurface { protected: GpuSurface() = default; ~GpuSurface() = default; };
struct GpuSampler { protected: GpuSampler() = default; ~GpuSampler() = default; };

/// Base of everything that owns backend objects. The backend keeps a list of them so that, when the GPU context is
/// lost (WebGL context loss, WebGPU device loss) and a new one is created, every buffer, texture and surface can be
/// recreated from its system memory copy.
class GpuObject
{
public:
    explicit GpuObject(Backend *backend);
    virtual ~GpuObject();
    /// The context is gone: forget the backend objects (they are invalid).
    virtual void OnContextLost() {}
    /// A new context is current: create the backend objects again and re-upload what is known.
    virtual void OnContextRestored() = 0;

private:
    friend class Backend;
    Backend *m_registry;
    GpuObject *m_prev = nullptr, *m_next = nullptr;
};

/// Capabilities of the GPU implementation (valid once the backend is initialized).
struct GpuCaps
{
    int maxTextureSize = 2048;
    int maxCubeSize = 2048;
    int max3DSize = 256;
    int maxVertexAttribs = 16;
    int maxVertexUniformVectors = 256;
    int maxTextureUnits = 16;
    float maxAnisotropy = 1.0f;
    float maxPointSize = 1.0f;
    int maxSamples = 1;
    int uniformAlignment = 256;
    bool s3tc = false;            ///< DXT textures can be sampled directly (otherwise they are decoded on the CPU)
    bool anisotropic = false;
    bool provokingVertex = false; ///< flat shading takes the first vertex of a triangle, like Direct3D
    bool baseVertex = false;      ///< indexed draws can add a base vertex to every index
    std::string renderer = "WebGL2";
    std::string vendor = "WebGL";
    std::string version;
};

//------------------------------------------------------------------------------
// Resource descriptions
//------------------------------------------------------------------------------
enum class BufferKind : uint8_t { Vertex, Index };

/// Texture types; the values are those of StageKey::texType.
enum class TextureKind : uint8_t { Tex2D = 1, Cube = 2, Volume = 3 };

struct TextureDesc
{
    TextureKind kind;
    D3DFORMAT format;
    uint32_t width, height, depth, levels;
    bool decodeDxt;     ///< DXT data is decoded to RGBA8 on upload (GpuCaps::s3tc is false)
};

/// A rectangle (2D, cube face) or the whole level (volume: `depth` slices) of a texture level.
struct TextureRegion
{
    uint32_t face, level;
    uint32_t x, y, width, height, depth;
};

/// A standalone render target or depth/stencil buffer (not a texture level).
struct SurfaceDesc
{
    D3DFORMAT format;
    uint32_t width, height;
    int samples;        ///< 0: not multisampled
    bool depthStencil;
};

/// Sampler state, in Direct3D's D3DTEXF_* / D3DTADDRESS_* numbering (the same in Direct3D 8 and 9).
struct SamplerDesc
{
    uint8_t minFilter, magFilter;
    uint8_t mipFilter;      ///< D3DTEXF_NONE when the texture has one level
    uint8_t mipped;         ///< the texture has a mip chain that is sampled
    uint8_t addressU, addressV, addressW;
    uint8_t maxAnisotropy;  ///< 1 = no anisotropic filtering
};

//------------------------------------------------------------------------------
// Pipeline state
//------------------------------------------------------------------------------
enum class CompareFunc : uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
enum class BlendFactor : uint8_t
{
    Zero, One, SrcColor, InvSrcColor, SrcAlpha, InvSrcAlpha, DstAlpha, InvDstAlpha, DstColor, InvDstColor, SrcAlphaSat
};
enum class BlendOp : uint8_t { Add, Subtract, RevSubtract, Min, Max };
enum class StencilOp : uint8_t { Keep, Zero, Replace, IncrSat, DecrSat, Invert, IncrWrap, DecrWrap };
/// Winding of front faces in the "flipped" GL convention (window y up, the image stored top row first, see
/// docs/ARCHITECTURE.md): the opposite of the winding Direct3D sees on the screen. A backend that renders unflipped
/// (WebGPU: framebuffer y down) uses the other value.
enum class FrontFace : uint8_t { CCW, CW };

/// Fixed-function state of a draw (what WebGPU bakes into a render pipeline, plus the dynamic viewport/stencil
/// reference). Compared with memcmp: fill it after a memset to zero.
struct PipelineState
{
    bool blend, depthTest, depthMask, cull, stencilTest, scissor, polyOffset;
    BlendFactor srcBlend, dstBlend;
    BlendOp blendOp;
    CompareFunc depthFunc;
    FrontFace frontFace;
    CompareFunc stencilFunc;
    StencilOp stencilFail, stencilZFail, stencilPass;
    int32_t stencilRef;
    uint32_t stencilMask, stencilWriteMask;
    uint8_t colorMask;      ///< bits r, g, b, a
    float depthNear, depthFar;
    float polyFactor, polyUnits;
    int32_t vp[4];          ///< viewport x, y, width, height (render target pixels, row 0 at the top)
    int32_t sc[4];          ///< scissor rectangle (when `scissor`)
    /// Marks every field as unknown.
    void Invalidate();
};

//------------------------------------------------------------------------------
// Render targets
//------------------------------------------------------------------------------
/// What a draw or clear renders into. References the front end's storage objects (not the backend objects inside
/// them), so the binding survives the recreation of those objects after a context loss.
struct RenderTargetRef
{
    enum class Kind : uint8_t { None, BackBuffer, TextureLevel, Surface };
    Kind kind = Kind::None;
    TextureBase *texture = nullptr;     ///< TextureLevel
    uint32_t face = 0, level = 0;
    SurfaceStorage *surface = nullptr;  ///< Surface (render target or depth/stencil buffer)
    uint32_t width = 0, height = 0;
    D3DFORMAT format = D3DFMT_UNKNOWN;
};

struct ClearRect { int32_t x, y, width, height; };

struct ClearCall
{
    bool color, depth, stencil;         ///< what to clear (depth/stencil only when the depth buffer has them)
    float rgba[4];
    float z;                            ///< clamped to [0, 1]
    uint32_t stencilValue;
    const ClearRect *rects;             ///< already clipped to the render target, none empty
    uint32_t rectCount;
};

//------------------------------------------------------------------------------
// Uniforms
//------------------------------------------------------------------------------
/// Receives the values of the uniforms of one group (the backend writes them to GL uniforms or to a uniform buffer).
class UniformSink
{
public:
    virtual void Vec4(UniformId u, const float *v, int count) = 0;
    virtual void Vec2(UniformId u, const float *v, int count) = 0;
    virtual void Float(UniformId u, const float *v, int count) = 0;
    virtual void Mat4(UniformId u, const float *m, int count) = 0;

protected:
    ~UniformSink() = default;
};

/// Produces the uniform values from the front end's state. The backend asks for a group when the program has not
/// received the version in DrawCall::uniformVersions yet.
class UniformSource
{
public:
    virtual void WriteUniforms(UniformGroup group, const Program &program, UniformSink &out) = 0;

protected:
    ~UniformSource() = default;
};

//------------------------------------------------------------------------------
// Draw
//------------------------------------------------------------------------------
enum class Topology : uint8_t { PointList, LineList, LineStrip, TriangleList, TriangleStrip, TriangleFan };
enum class FillMode : uint8_t { Solid, Wireframe, Point };

struct TextureBinding
{
    GpuTexture *texture;    ///< null: a 1x1 white texture of the stage's type (no texture, or it is the render target)
    GpuSampler *sampler;    ///< from Backend::GetSampler (ignored without texture)
    int32_t baseLevel;      ///< first mip level sampled (D3DTSS_MAXMIPLEVEL)
};

struct VertexStream
{
    BufferStorage *buffer;  ///< null: no buffer on this stream
    uint32_t stride;        ///< 0: the stride of the vertex layout
};

/// One draw with everything it depends on. Stages are bound for `program->key.stage[s].texType != 0`.
struct DrawCall
{
    Topology topology;
    FillMode fill;
    uint32_t primitiveCount;
    uint32_t count;             ///< vertices (non-indexed) or indices
    bool indexed;
    uint32_t start;             ///< first vertex (non-indexed) or first index of the index buffer
    uint32_t baseVertex;        ///< added to every index (indexed draws from buffers)

    const PipelineState *pipeline;
    Program *program;
    const uint32_t *uniformVersions;    ///< G_COUNT version counters
    UniformSource *uniforms;
    TextureBinding textures[MAX_STAGES];

    const VertexLayout *layout;
    VertexStream streams[MAX_STREAMS];
    const void *userVertices;   ///< non-null: stream 0 comes from user memory (DrawPrimitiveUP)
    uint32_t userStride, userVertexCount;

    BufferStorage *indexBuffer; ///< indexed draws from buffers
    const void *userIndices;    ///< indexed draws from user memory
    bool index32;

    uint32_t debugTag;          ///< FVF or shader handle, for log messages
};

//------------------------------------------------------------------------------
// The backend
//------------------------------------------------------------------------------
enum class BackendKind : uint8_t { WebGL2 = DXWEBGL2_BACKEND_WEBGL2, WebGPU = DXWEBGL2_BACKEND_WEBGPU };

class Backend
{
public:
    virtual ~Backend() = default;
    BackendKind Kind() const { return m_kind; }
    const GpuCaps &Caps() const { return m_caps; }

    //-- Device -----------------------------------------------------------------------
    /// Creates the context on the configured canvas (GetConfig().canvas); a non-zero `width` x `height` is the size of
    /// the drawing buffer. Fills Caps().
    virtual bool Initialize(uint32_t width, uint32_t height) = 0;
    /// Destroys the backend's own objects and the context (the device is released).
    virtual void Shutdown() = 0;
    /// Description of the GPU for IDirect3D8::GetAdapterIdentifier ("WebGL2: <renderer>").
    virtual std::string Description() const = 0;
    virtual void CanvasSize(int &width, int &height) = 0;
    /// Forgets every cached piece of state (the next draw applies everything).
    virtual void InvalidateState() = 0;
    /// The front end changed its vertex streams or vertex format: cached vertex input setups are re-established on the
    /// next draw even when they look the same.
    virtual void InvalidateVertexInput() = 0;

    //-- Back buffer and presentation ---------------------------------------------------
    /// Creates the back buffer (and sizes the canvas). `samples` 0 = not multisampled.
    virtual bool CreateBackBuffer(uint32_t width, uint32_t height, int samples) = 0;
    virtual void DestroyBackBuffer() = 0;
    /// Gamma ramp applied when the back buffer is presented (`active` false: identity).
    virtual void SetGammaRamp(const uint16_t *red, const uint16_t *green, const uint16_t *blue, bool active) = 0;
    /// Shows the back buffer on the canvas.
    virtual void Present() = 0;
    /// Reads a rectangle of the back buffer as RGBA8.
    virtual bool ReadBackBuffer(const RECT &rc, uint8_t *rgbaOut) = 0;
    /// Debug log of what the back buffer holds (dxwebgl2_SetDebug).
    virtual void DebugDescribeBackBuffer() = 0;

    //-- Context loss --------------------------------------------------------------------
    virtual bool IsContextLost() = 0;
    /// True when the platform signalled a loss or a restore since the last ClearContextEvent() (cheap, polled per draw).
    virtual bool HasContextEvent() const = 0;
    virtual void ClearContextEvent() = 0;
    /// The front end noticed the loss: every GpuObject forgets its backend objects.
    virtual void OnContextLost();
    /// Creates everything again on the new context and tells every GpuObject. False when it is not usable.
    virtual bool RestoreContext() = 0;
    /// Test aids (WEBGL_lose_context): drop / restore the context. False when not available.
    virtual bool TestLoseContext() = 0;
    virtual bool TestRestoreContext() = 0;

    //-- Resources -------------------------------------------------------------------------
    /// Create* return null when the context is lost. Destroy* also take objects of a lost context (they are freed
    /// without a GPU call).
    virtual GpuBuffer *CreateBuffer(BufferKind kind, uint32_t size) = 0;
    virtual void DestroyBuffer(GpuBuffer *buffer) = 0;
    /// Uploads `size` bytes at `offset`. `initial`: the first upload, `data` is the whole buffer. `data` points into the
    /// caller's copy of the whole buffer (the `size` bytes given to CreateBuffer start at `data - offset`), so that a
    /// backend can widen the range (WebGPU copies multiples of 4 bytes).
    virtual void WriteBuffer(GpuBuffer *buffer, uint32_t offset, uint32_t size, const void *data, bool initial) = 0;

    virtual GpuTexture *CreateTexture(const TextureDesc &desc) = 0;
    virtual void DestroyTexture(GpuTexture *texture) = 0;
    /// Uploads a region from memory in the texture's D3DFORMAT layout (`rowPitch` bytes per row or block row).
    virtual void WriteTexture(GpuTexture *texture, const TextureRegion &region, const uint8_t *data, uint32_t rowPitch) = 0;
    /// Reads a rectangle of a 2D or cube level as RGBA8 (false for volumes and when not possible).
    virtual bool ReadTexture(GpuTexture *texture, uint32_t face, uint32_t level, const RECT &rc, uint8_t *rgbaOut) = 0;

    virtual GpuSurface *CreateSurface(const SurfaceDesc &desc) = 0;
    virtual void DestroySurface(GpuSurface *surface) = 0;
    virtual bool ReadSurface(GpuSurface *surface, const RECT &rc, uint8_t *rgbaOut) = 0;

    /// Sampler object for a description (cached by the backend; valid until the context is lost or restored).
    virtual GpuSampler *GetSampler(const SamplerDesc &desc) = 0;

    /// Generates (GLSL / WGSL), compiles and links the program of a key. `vs` / `ps` are the shader objects of
    /// key.vsHandle / key.psHandle (null for the fixed-function stages). Invalid programs are returned too (valid
    /// false) so that they are not retried every draw.
    virtual Program *CreateProgram(const ProgramKey &key, const VertexShaderObject *vs, const PixelShaderObject *ps) = 0;
    virtual void DestroyProgram(Program *program) = 0;

    //-- Rendering ---------------------------------------------------------------------------
    /// Selects the render targets of the following draws and clears (applied lazily).
    virtual void SetRenderTargets(const RenderTargetRef &color, const RenderTargetRef &depth) = 0;
    virtual void Clear(const ClearCall &call) = 0;
    /// False when the draw cannot be done (missing vertex or index data).
    virtual bool Draw(const DrawCall &call) = 0;

    //-- Registry of GpuObjects -----------------------------------------------------------------
    void RegisterObject(GpuObject *o);
    void UnregisterObject(GpuObject *o);

protected:
    explicit Backend(BackendKind kind) : m_kind(kind) {}
    void NotifyContextLost();
    void NotifyContextRestored();

    GpuCaps m_caps;

private:
    BackendKind m_kind;
    GpuObject *m_objects = nullptr;
};

/// Creates the backend selected with dxwebgl2_SetBackend and records it as the active one (dxwebgl2_GetBackend).
/// DXWEBGL2_BACKEND_AUTO selects WebGPU when the library has the backend, the application can wait for the device
/// (JSPI) and the browser has an adapter and a device, and WebGL2 otherwise (one log line says why).
Backend *CreateBackend();

//------------------------------------------------------------------------------
// CPU emulation of primitive types and fill modes, shared by the backends
//------------------------------------------------------------------------------
/// Vertex `i` of a draw: `indices ? indices[i] + base : first + i`.
struct IndexFetch
{
    const void *indices;
    bool index32;
    uint32_t base, first;
    uint32_t operator()(uint32_t i) const
    {
        if (!indices) return first + i;
        return (index32 ? static_cast<const uint32_t *>(indices)[i] : static_cast<const uint16_t *>(indices)[i]) + base;
    }
};

/// The triangles of a list, strip or fan as a triangle list (each keeps its winding). `rotate` moves the vertex
/// Direct3D takes the flat shading colour from (the first) to the end, for APIs that use the last one.
void ExpandTriangles(Topology topology, uint32_t primitiveCount, const IndexFetch &fetch, bool rotate, std::vector<uint32_t> &out);
/// The edges of a triangle list as a line list (wireframe fill mode).
void TrianglesToLines(const std::vector<uint32_t> &triangles, std::vector<uint32_t> &out);

} // namespace dxwebgl2
