/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * The WebGPU backend.
 *
 * Implements core/backend.h on WebGPU (Emscripten's emdawnwebgpu port, <webgpu/webgpu.h>); the design is
 * docs/WEBGPU.md. A draw becomes a render pipeline (cached by program, vertex input and fixed-function state), two bind
 * groups (the program's uniform block at a dynamic offset of a per-submit ring; textures and samplers, cached), vertex
 * and index buffers and the draw, recorded into a render pass that stays open while the render targets do not change.
 *
 *   wgpu_backend.cpp    device creation (asynchronous, waited for with JSPI), capabilities, back buffer, present pass,
 *                       command submission, device loss
 *   wgpu_resources.cpp  buffers, textures (format table and upload conversions), surfaces, samplers, read-back
 *   wgpu_program.cpp    WGSL programs (shader modules per vertex input signature, uniform blocks)
 *   wgpu_draw.cpp       render passes, clears, render pipelines, bind groups, draws
 *
 * Ordering. queue.writeBuffer / writeTexture take effect before the commands of the next submit, while the commands
 * of the frame are recorded into one encoder that is submitted at Present. Two consequences:
 *   - the uniform blocks and the data of draws from user memory are collected in CPU staging rings during the frame and
 *     uploaded with one writeBuffer each right before the submit (the rings start over after every submit);
 *   - a write into a buffer or texture that a recorded command already uses (it was drawn with, or rendered into,
 *     since the last submit) first submits what was recorded, so that the earlier commands see the earlier contents.
 */
#pragma once

#include "core/webgpu/wgpu.h"
#include "core/backend.h"
#include "core/texture_storage.h"
#include "core/buffer_storage.h"
#include "core/wgsl.h"
#include "core/shader.h"

#include <memory>

namespace dxwebgl2 {

enum
{
    WG_MAX_SLOTS = 8,           ///< vertex buffer slots (WebGPU's maxVertexBuffers)
    WG_UNIFORM_ALIGN = 256,     ///< minUniformBufferOffsetAlignment
};

/// How a D3DFORMAT is stored in WebGPU and converted on upload.
enum class WgConv : uint8_t
{
    None,        ///< bytes unchanged
    ForceAlpha,  ///< X8R8G8B8 -> bgra8unorm with alpha 255
    Expand,      ///< any other uncompressed format -> bgra8unorm through UnpackPixel (16-bit, 24-bit, luminance, alpha)
    DecodeDXT,   ///< DXT1-5 decoded on the CPU -> rgba8unorm
};
struct WgFormat
{
    WGPUTextureFormat format;
    WgConv conv;
    uint8_t bytes;      ///< bytes per texel uploaded (0 for block compressed)
    uint8_t blockBytes; ///< bytes per 4x4 block for block compressed storage
};

struct WGBuffer final : GpuBuffer
{
    WGPUBuffer buffer = nullptr;
    uint32_t size = 0;          ///< GPU size (a multiple of 4)
    uint32_t cpuSize = 0;       ///< size of the caller's copy (CreateBuffer)
    uint64_t useSerial = 0;     ///< encoder that last read it
};

struct WGTexture final : GpuTexture
{
    WGPUTexture texture = nullptr;
    TextureDesc desc{};
    WgFormat fmt{};
    bool renderTarget = false;
    uint32_t uid = 0;                       ///< unique, never reused (bind group cache keys)
    std::vector<WGPUTextureView> views;     ///< sampling views per base level
    std::vector<WGPUTextureView> targets;   ///< render target views per face * levels + level
    uint64_t useSerial = 0;                 ///< encoder that last used it
    WGPUTextureView View(uint32_t baseLevel);
    WGPUTextureView Target(uint32_t face, uint32_t level);
};

struct WGSurface final : GpuSurface
{
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;
    SurfaceDesc desc{};
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    uint32_t samples = 1;
    uint64_t useSerial = 0;
};

struct WGSampler final : GpuSampler
{
    WGPUSampler sampler = nullptr;
    uint32_t uid = 0;
};

/// A vertex stage module for one input signature (see core/wgsl.h).
struct WGVertexVariant
{
    uint32_t signature;
    WGPUShaderModule module;
};

class WGProgram final : public Program
{
public:
    uint32_t uid = 0;
    WGPUShaderModule fs = nullptr;
    std::vector<WGVertexVariant> vs;
    std::shared_ptr<VertexShaderObject> vso;   ///< copy of the vertex shader (more input signatures may follow)
    WgslUniformLayout layout{};
    std::vector<uint8_t> block;     ///< CPU copy of the uniform block
    uint32_t ringOffset = 0;        ///< where the block is in the uniform ring
    uint64_t ringEpoch = 0;         ///< ring epoch the offset belongs to (0: never written)
    bool blockDirty = true;
    uint32_t texSig = 0;            ///< texture type (2 bits) per stage: the layout of bind group 1
    WGPUPipelineLayout pipelineLayout = nullptr;
    WGPUBindGroupLayout texLayout = nullptr;    ///< owned by the backend's layout cache
};

/// The render pipeline state of a draw (memset to zero, compared with memcmp).
struct WgPipelineKey
{
    uint32_t program, vsSignature, layoutId;
    uint16_t slotStride[WG_MAX_SLOTS];
    uint8_t slotStream[WG_MAX_SLOTS];
    uint8_t slotCount, topology, instanced, cullMode, frontFace;
    uint8_t blend, srcBlend, dstBlend, blendOp, colorMask;
    uint8_t depthWrite, depthFunc;
    uint8_t stencilFunc, stencilFail, stencilZFail, stencilPass, stencilMask, stencilWriteMask;
    uint8_t samples, pad;
    int32_t depthBias;
    float depthSlope;
    uint32_t colorFormat, depthFormat;
};

class WebGPUBackend final : public Backend, private UniformSink
{
public:
    WebGPUBackend() : Backend(BackendKind::WebGPU) {}
    ~WebGPUBackend() override;

    /// Creates the adapter and the device (waits for them, which needs a build that can suspend). False when WebGPU is
    /// not usable (no JSPI, no navigator.gpu, no adapter, requestDevice failed), with the reason in Unavailable(); the
    /// caller then uses WebGL2. Nothing is logged here: the caller logs the one line.
    bool Acquire();
    const std::string &Unavailable() const { return m_unavailable; }

    //-- Backend (wgpu_backend.cpp) ------------------------------------------------------------
    bool Initialize(uint32_t width, uint32_t height) override;
    void Shutdown() override;
    std::string Description() const override { return "WebGPU: " + m_caps.renderer; }
    void CanvasSize(int &width, int &height) override;
    void InvalidateState() override;
    void InvalidateVertexInput() override {}
    bool CreateBackBuffer(uint32_t width, uint32_t height, int samples) override;
    void DestroyBackBuffer() override;
    void SetGammaRamp(const uint16_t *red, const uint16_t *green, const uint16_t *blue, bool active) override;
    void Present() override;
    bool ReadBackBuffer(const RECT &rc, uint8_t *rgbaOut) override;
    void DebugDescribeBackBuffer() override;
    bool IsContextLost() override;
    bool HasContextEvent() const override { return m_ctxEvent != 0; }
    void ClearContextEvent() override { m_ctxEvent = 0; }
    void OnContextLost() override;
    bool RestoreContext() override;
    bool TestLoseContext() override;
    bool TestRestoreContext() override;

    //-- Backend (wgpu_resources.cpp) ------------------------------------------------------------
    GpuBuffer *CreateBuffer(BufferKind kind, uint32_t size) override;
    void DestroyBuffer(GpuBuffer *buffer) override;
    void WriteBuffer(GpuBuffer *buffer, uint32_t offset, uint32_t size, const void *data, bool initial) override;
    GpuTexture *CreateTexture(const TextureDesc &desc) override;
    void DestroyTexture(GpuTexture *texture) override;
    void WriteTexture(GpuTexture *texture, const TextureRegion &region, const uint8_t *data, uint32_t rowPitch) override;
    bool ReadTexture(GpuTexture *texture, uint32_t face, uint32_t level, const RECT &rc, uint8_t *rgbaOut) override;
    GpuSurface *CreateSurface(const SurfaceDesc &desc) override;
    void DestroySurface(GpuSurface *surface) override;
    bool ReadSurface(GpuSurface *surface, const RECT &rc, uint8_t *rgbaOut) override;
    GpuSampler *GetSampler(const SamplerDesc &desc) override;

    //-- Backend (wgpu_program.cpp) ----------------------------------------------------------------
    Program *CreateProgram(const ProgramKey &key, const VertexShaderObject *vs, const PixelShaderObject *ps) override;
    void DestroyProgram(Program *program) override;

    //-- Backend (wgpu_draw.cpp) -------------------------------------------------------------------
    void SetRenderTargets(const RenderTargetRef &color, const RenderTargetRef &depth) override;
    void Clear(const ClearCall &call) override;
    bool Draw(const DrawCall &call) override;

    // Called from the device callbacks (wgpu_backend.cpp).
    void OnDeviceLost(int reason, const char *message);
    void OnUncapturedError(int type, const char *message);

private:
    // wgpu_backend.cpp
    bool RequestDevice();
    void ReleaseDevice();
    void QueryCaps();
    bool ConfigureSurface();
    void CreateBaseObjects();
    void DestroyBaseObjects();
    void CreatePresentPipeline();
    WGPUCommandEncoder Encoder();
    /// Ends the render pass (running pending clears), uploads the staging rings and submits everything recorded.
    void Submit();
    bool WaitFuture(WGPUFuture future);
    /// Copies a rectangle of a texture to memory as RGBA8 (submits first, waits for the GPU).
    bool ReadTextureRect(WGPUTexture texture, WGPUTextureFormat format, uint32_t layer, uint32_t level, const RECT &rc, uint8_t *rgbaOut);
    void GrowRing(WGPUBuffer &buffer, uint32_t &capacity, uint32_t need, WGPUBufferUsage usage, const char *label);

    // wgpu_resources.cpp
    WgFormat MapFormat(D3DFORMAT format, bool decode) const;
    void CreateTextureObject(WGTexture *t);
    void ReleaseTextureObject(WGTexture *t);
    void ForgetTextureInBindGroups(uint32_t uid);
    const uint8_t *ConvertForUpload(const WGTexture &t, const uint8_t *src, uint32_t srcPitch, uint32_t w, uint32_t h,
                                    uint32_t &outPitch);

    // wgpu_program.cpp
    WGPUShaderModule CompileModule(const std::string &code, const char *label);
    WGPUShaderModule VertexModule(WGProgram *p, uint32_t signature);
    WGPUBindGroupLayout TextureLayout(uint32_t texSig);
    void UploadUniforms(WGProgram *p, const uint32_t *versions, UniformSource &source);

    // wgpu_draw.cpp
    void EnsurePass();
    void EndPass();
    void BeginPass();
    void ResolveAttachments();
    WGPUTextureView SubstituteDepth(uint32_t w, uint32_t h, uint32_t samples, bool stencil, WGPUTextureFormat &format);
    WGPURenderPipeline GetPipeline(const WgPipelineKey &key, const DrawCall &c, WGProgram *p);
    WGPURenderPipeline GetClearPipeline(bool color, bool depth, bool stencil);
    WGPUBindGroup TextureBindGroup(WGProgram *p, const DrawCall &c);
    uint32_t StreamAlloc(uint32_t size, uint32_t align);
    uint32_t UniformAlloc(uint32_t size);
    void ReserveRings(uint32_t streamBytes, uint32_t uniformBytes);
    void SetViewport(float x, float y, float w, float h, float zn, float zf);
    void SetScissor(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    void SetStencilRef(uint32_t ref);
    void BindVertexBuffer(uint32_t slot, WGPUBuffer buffer, uint64_t offset);
    void ResetPassCache();

    // UniformSink: writes into the block of the program being drawn with.
    void Vec4(UniformId u, const float *v, int count) override;
    void Vec2(UniformId u, const float *v, int count) override;
    void Float(UniformId u, const float *v, int count) override;
    void Mat4(UniformId u, const float *m, int count) override;
    WGProgram *m_sinkProgram = nullptr;

    //-- Device ---------------------------------------------------------------------------------------
    WGPUInstance m_instance = nullptr;
    WGPUAdapter m_adapter = nullptr;
    WGPUDevice m_device = nullptr;
    WGPUQueue m_queue = nullptr;
    WGPUSurface m_surface = nullptr;
    WGPUTextureFormat m_canvasFormat = WGPUTextureFormat_BGRA8Unorm;
    bool m_bc = false;                  ///< texture-compression-bc on the device
    std::string m_unavailable;          ///< why Acquire() / RequestDevice() failed
    volatile int m_ctxEvent = 0;        ///< 1 = lost, 2 = a new device can be created
    bool m_lost = false;                ///< the device is lost (or was destroyed by the test aid)
    bool m_lostByTest = false;          ///< ... by TestLoseContext (restorable only after TestRestoreContext)
    bool m_restorable = false;          ///< a new device may be requested (RestoreContext)
    uint32_t m_errorCount = 0;

    //-- Back buffer and present --------------------------------------------------------------------
    uint32_t m_bbWidth = 0, m_bbHeight = 0;
    int m_samples = 0;                  ///< multisampling of the back buffer as requested (0 = off; 4 samples used)
    WGPUTexture m_bb = nullptr, m_bbMs = nullptr;
    WGPUTextureView m_bbView = nullptr, m_bbMsView = nullptr;
    WGPURenderPipeline m_presentPipeline = nullptr, m_presentGammaPipeline = nullptr;
    WGPUBindGroupLayout m_presentLayout = nullptr;
    WGPUBindGroup m_presentGroup = nullptr;
    WGPUTexture m_gammaTex = nullptr;
    WGPUTextureView m_gammaView = nullptr;
    WGPUSampler m_pointSampler = nullptr;
    WGPUBuffer m_presentParams = nullptr;
    uint8_t m_gammaLut[256 * 4] = {};
    bool m_gammaActive = false, m_gammaDirty = true;

    //-- Commands -------------------------------------------------------------------------------------
    WGPUCommandEncoder m_encoder = nullptr;
    WGPURenderPassEncoder m_pass = nullptr;
    uint64_t m_serial = 1;              ///< current encoder (incremented by every submit)
    uint64_t m_epoch = 1;               ///< ring epoch (incremented by every submit)
    bool m_recorded = false;            ///< commands were recorded into m_encoder

    //-- Render targets ---------------------------------------------------------------------------------
    RenderTargetRef m_color, m_depth;
    bool m_targetsDirty = true;
    // The attachments of the pass (valid when !m_targetsDirty).
    WGPUTextureView m_attColor = nullptr, m_attResolve = nullptr, m_attDepth = nullptr;
    WGPUTextureFormat m_attColorFormat = WGPUTextureFormat_Undefined, m_attDepthFormat = WGPUTextureFormat_Undefined;
    uint32_t m_attWidth = 0, m_attHeight = 0, m_attSamples = 1;
    bool m_attStencil = false;
    WGTexture *m_attTexture = nullptr;  ///< texture rendered into (use tracking)
    WGSurface *m_attSurface = nullptr, *m_attDepthSurface = nullptr;
    // Clears waiting for the next pass (its load operations).
    bool m_pendColor = false, m_pendDepth = false, m_pendStencil = false;
    WGPUColor m_pendColorValue{};
    float m_pendDepthValue = 1.0f;
    uint32_t m_pendStencilValue = 0;
    struct SubDepth { WGPUTexture tex; WGPUTextureView view; uint32_t w, h, samples; bool stencil; };
    std::vector<SubDepth> m_subDepth;

    //-- Pass state cache -----------------------------------------------------------------------------------
    WGPURenderPipeline m_curPipeline = nullptr;
    WGPUBindGroup m_curGroup0 = nullptr, m_curGroup1 = nullptr;
    uint32_t m_curGroup0Offset = 0xFFFFFFFF;
    WGPUBuffer m_curVB[WG_MAX_SLOTS] = {};
    uint64_t m_curVBOffset[WG_MAX_SLOTS] = {};
    WGPUBuffer m_curIB = nullptr;
    uint64_t m_curIBOffset = 0;
    WGPUIndexFormat m_curIBFormat = WGPUIndexFormat_Undefined;
    float m_curViewport[6] = {};
    bool m_viewportValid = false;
    uint32_t m_curScissor[4] = {};
    bool m_scissorValid = false;
    uint32_t m_curStencilRef = 0;

    //-- Caches -------------------------------------------------------------------------------------------
    WGPUBindGroupLayout m_uniformLayout = nullptr;
    std::unordered_map<uint32_t, WGPUBindGroupLayout> m_texLayouts;
    std::unordered_map<uint64_t, std::vector<std::pair<WgPipelineKey, WGPURenderPipeline>>> m_pipelines;
    WgPipelineKey m_lastKey{};
    WGPURenderPipeline m_lastPipeline = nullptr;
    struct BindGroupKey { WGPUBindGroupLayout layout; uint32_t tex[MAX_STAGES], level[MAX_STAGES], sampler[MAX_STAGES]; };
    std::unordered_map<uint64_t, std::vector<std::pair<BindGroupKey, WGPUBindGroup>>> m_bindGroups;
    std::unordered_map<std::string, WGSampler *> m_samplers;
    WGPUSampler m_whiteSampler = nullptr;
    WGPUTexture m_whiteTex[3] = {};
    WGPUTextureView m_whiteView[3] = {};
    std::unordered_map<uint32_t, WGPURenderPipeline> m_clearPipelines;
    WGPUShaderModule m_clearModule = nullptr;
    WGPUPipelineLayout m_clearPipelineLayout = nullptr;
    uint32_t m_nextUid = 1;

    //-- Staging rings -----------------------------------------------------------------------------------------
    WGPUBuffer m_uniformBuffer = nullptr;
    uint32_t m_uniformCap = 0;
    std::vector<uint8_t> m_uniformStaging;
    uint32_t m_uniformUsed = 0;
    WGPUBindGroup m_uniformGroup = nullptr;
    WGPUBuffer m_streamBuffer = nullptr;
    uint32_t m_streamCap = 0;
    std::vector<uint8_t> m_streamStaging;
    uint32_t m_streamUsed = 0;

    //-- Scratch ------------------------------------------------------------------------------------------------
    std::vector<uint8_t> m_uploadScratch;
    std::vector<uint32_t> m_triScratch, m_lineScratch;
    unsigned m_drawCounter = 0;
};

} // namespace dxwebgl2
