/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * WebGPU backend: render passes, clears, render pipelines, bind groups and draws.
 *
 * Draws are recorded into a render pass that stays open while the render targets do not change. Clears of the whole
 * target before the first draw of a pass become its load operations; other clears (rectangles, or after draws) draw a
 * quad. Primitive types and fill modes WebGPU lacks are rebuilt on the CPU (triangle fans and wireframe / point fill as
 * index lists, point lists as instanced quads), see docs/WEBGPU.md, section 3.
 */
#include "core/webgpu/wgpu_backend.h"

namespace dxwebgl2 {

namespace {

inline WGPUStringView Label(const char *s) { return WGPUStringView{s, WGPU_STRLEN}; }

WGPUCompareFunction ToWG(CompareFunc f)
{
    switch (f)
    {
    case CompareFunc::Never: return WGPUCompareFunction_Never;
    case CompareFunc::Less: return WGPUCompareFunction_Less;
    case CompareFunc::Equal: return WGPUCompareFunction_Equal;
    case CompareFunc::LessEqual: return WGPUCompareFunction_LessEqual;
    case CompareFunc::Greater: return WGPUCompareFunction_Greater;
    case CompareFunc::NotEqual: return WGPUCompareFunction_NotEqual;
    case CompareFunc::GreaterEqual: return WGPUCompareFunction_GreaterEqual;
    default: return WGPUCompareFunction_Always;
    }
}

WGPUBlendFactor ToWG(BlendFactor b)
{
    switch (b)
    {
    case BlendFactor::Zero: return WGPUBlendFactor_Zero;
    case BlendFactor::One: return WGPUBlendFactor_One;
    case BlendFactor::SrcColor: return WGPUBlendFactor_Src;
    case BlendFactor::InvSrcColor: return WGPUBlendFactor_OneMinusSrc;
    case BlendFactor::SrcAlpha: return WGPUBlendFactor_SrcAlpha;
    case BlendFactor::InvSrcAlpha: return WGPUBlendFactor_OneMinusSrcAlpha;
    case BlendFactor::DstAlpha: return WGPUBlendFactor_DstAlpha;
    case BlendFactor::InvDstAlpha: return WGPUBlendFactor_OneMinusDstAlpha;
    case BlendFactor::DstColor: return WGPUBlendFactor_Dst;
    case BlendFactor::InvDstColor: return WGPUBlendFactor_OneMinusDst;
    default: return WGPUBlendFactor_SrcAlphaSaturated;
    }
}

WGPUBlendOperation ToWG(BlendOp op)
{
    switch (op)
    {
    case BlendOp::Subtract: return WGPUBlendOperation_Subtract;
    case BlendOp::RevSubtract: return WGPUBlendOperation_ReverseSubtract;
    case BlendOp::Min: return WGPUBlendOperation_Min;
    case BlendOp::Max: return WGPUBlendOperation_Max;
    default: return WGPUBlendOperation_Add;
    }
}

WGPUStencilOperation ToWG(StencilOp op)
{
    switch (op)
    {
    case StencilOp::Zero: return WGPUStencilOperation_Zero;
    case StencilOp::Replace: return WGPUStencilOperation_Replace;
    case StencilOp::IncrSat: return WGPUStencilOperation_IncrementClamp;
    case StencilOp::DecrSat: return WGPUStencilOperation_DecrementClamp;
    case StencilOp::Invert: return WGPUStencilOperation_Invert;
    case StencilOp::IncrWrap: return WGPUStencilOperation_IncrementWrap;
    case StencilOp::DecrWrap: return WGPUStencilOperation_DecrementWrap;
    default: return WGPUStencilOperation_Keep;
    }
}

/// Vertex format and WGSL input kind of a vertex element.
WGPUVertexFormat ElementFormat(const VertexElement &e, uint32_t &kind)
{
    kind = WGSL_IN_FLOAT;
    if (e.glType == 1)
    {
        if (!e.normalized) kind = WGSL_IN_UINT;
        return e.normalized ? WGPUVertexFormat_Unorm8x4 : WGPUVertexFormat_Uint8x4;
    }
    if (e.glType == 2)
    {
        if (!e.normalized) kind = WGSL_IN_SINT;
        if (e.size <= 2) return e.normalized ? WGPUVertexFormat_Snorm16x2 : WGPUVertexFormat_Sint16x2;
        return e.normalized ? WGPUVertexFormat_Snorm16x4 : WGPUVertexFormat_Sint16x4;
    }
    switch (e.size)
    {
    case 1: return WGPUVertexFormat_Float32;
    case 2: return WGPUVertexFormat_Float32x2;
    case 3: return WGPUVertexFormat_Float32x3;
    default: return WGPUVertexFormat_Float32x4;
    }
}

const char *kClearShader = R"(
struct C { color: vec4f, depth: vec4f };
@group(0) @binding(0) var<uniform> c: C;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    let p = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
    return vec4f(p * 2.0 - 1.0, c.depth.x, 1.0);
}
@fragment fn fs() -> @location(0) vec4f { return c.color; }
)";

} // namespace

//------------------------------------------------------------------------------
// Staging rings
//------------------------------------------------------------------------------
/// Makes room in the staging rings for a draw: submits what was recorded when a ring is full (the rings start over),
/// and grows a ring that is too small even when empty.
void WebGPUBackend::ReserveRings(uint32_t streamBytes, uint32_t uniformBytes)
{
    const uint32_t blockMax = WgslMaxUniformBlockSize();
    if (m_streamUsed + streamBytes + 64 > m_streamCap || m_uniformUsed + uniformBytes + blockMax + WG_UNIFORM_ALIGN > m_uniformCap)
    {
        Submit();
        GrowRing(m_streamBuffer, m_streamCap, streamBytes + 64, WGPUBufferUsage_Vertex | WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst, "stream ring");
        GrowRing(m_uniformBuffer, m_uniformCap, uniformBytes + blockMax + WG_UNIFORM_ALIGN, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, "uniform ring");
    }
}

uint32_t WebGPUBackend::StreamAlloc(uint32_t size, uint32_t align)
{
    const uint32_t at = (m_streamUsed + align - 1) / align * align;
    m_streamUsed = at + ((size + 3) & ~3u);
    if (m_streamStaging.size() < m_streamUsed) m_streamStaging.resize(Max<size_t>(m_streamUsed, m_streamStaging.size() * 2));
    return at;
}

uint32_t WebGPUBackend::UniformAlloc(uint32_t size)
{
    const uint32_t at = (m_uniformUsed + WG_UNIFORM_ALIGN - 1) / WG_UNIFORM_ALIGN * WG_UNIFORM_ALIGN;
    m_uniformUsed = at + size;
    if (m_uniformStaging.size() < m_uniformUsed) m_uniformStaging.resize(Max<size_t>(m_uniformUsed, m_uniformStaging.size() * 2));
    return at;
}

//------------------------------------------------------------------------------
// Render targets and passes
//------------------------------------------------------------------------------
static bool SameTarget(const RenderTargetRef &a, const RenderTargetRef &b)
{
    return a.kind == b.kind && a.texture == b.texture && a.face == b.face && a.level == b.level && a.surface == b.surface &&
           a.width == b.width && a.height == b.height && a.format == b.format;
}

void WebGPUBackend::SetRenderTargets(const RenderTargetRef &color, const RenderTargetRef &depth)
{
    if (SameTarget(color, m_color) && SameTarget(depth, m_depth) && !m_targetsDirty) return;
    EndPass();
    m_color = color;
    m_depth = depth;
    m_targetsDirty = true;
}

/// A private depth buffer of the size / sample count of the colour target: Direct3D 8 accepts a larger depth buffer
/// (and the front end one with another sample count), WebGPU needs attachments of the same size.
WGPUTextureView WebGPUBackend::SubstituteDepth(uint32_t w, uint32_t h, uint32_t samples, bool stencil, WGPUTextureFormat &format)
{
    format = stencil ? WGPUTextureFormat_Depth24PlusStencil8 : WGPUTextureFormat_Depth24Plus;
    for (SubDepth &d : m_subDepth)
        if (d.w == w && d.h == h && d.samples == samples && d.stencil == stencil) return d.view;
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    td.label = Label("substitute depth buffer");
    td.usage = WGPUTextureUsage_RenderAttachment;
    td.size = {w, h, 1};
    td.format = format;
    td.sampleCount = samples;
    SubDepth d = {wgpuDeviceCreateTexture(m_device, &td), nullptr, w, h, samples, stencil};
    d.view = wgpuTextureCreateView(d.tex, nullptr);
    m_subDepth.push_back(d);
    DXW_HIT(Approximated, "depth buffer substituted for a size/multisample mismatch (%ux%u, %u samples)", w, h, samples);
    return d.view;
}

void WebGPUBackend::ResolveAttachments()
{
    m_attColor = m_attResolve = m_attDepth = nullptr;
    m_attTexture = nullptr;
    m_attSurface = m_attDepthSurface = nullptr;
    m_attColorFormat = m_attDepthFormat = WGPUTextureFormat_Undefined;
    m_attSamples = 1;
    m_attStencil = false;
    const RenderTargetRef &rt = m_color;
    switch (rt.kind)
    {
    case RenderTargetRef::Kind::BackBuffer:
        m_attColor = m_samples ? m_bbMsView : m_bbView;
        m_attResolve = m_samples ? m_bbView : nullptr;
        m_attSamples = m_samples ? 4 : 1;
        m_attColorFormat = WGPUTextureFormat_RGBA8Unorm;
        m_attWidth = m_bbWidth;
        m_attHeight = m_bbHeight;
        break;
    case RenderTargetRef::Kind::TextureLevel:
    {
        rt.texture->EnsureGpu();
        WGTexture *t = static_cast<WGTexture *>(rt.texture->Gpu());
        if (t && t->texture)
        {
            m_attColor = t->Target(rt.face, rt.level);
            m_attColorFormat = t->fmt.format;
            m_attTexture = t;
        }
        m_attWidth = Max<uint32_t>(1, t ? t->desc.width >> rt.level : rt.width);
        m_attHeight = Max<uint32_t>(1, t ? t->desc.height >> rt.level : rt.height);
        break;
    }
    case RenderTargetRef::Kind::Surface:
    {
        WGSurface *s = static_cast<WGSurface *>(rt.surface->Gpu());
        if (s)
        {
            m_attColor = s->view;
            m_attColorFormat = s->format;
            m_attSamples = s->samples;
            m_attSurface = s;
        }
        m_attWidth = rt.width;
        m_attHeight = rt.height;
        break;
    }
    default:
        break;
    }
    if (m_depth.kind == RenderTargetRef::Kind::Surface && m_attColor)
    {
        WGSurface *ds = static_cast<WGSurface *>(m_depth.surface->Gpu());
        if (ds)
        {
            const bool stencil = ds->format == WGPUTextureFormat_Depth24PlusStencil8;
            if (ds->desc.width != m_attWidth || ds->desc.height != m_attHeight || ds->samples != m_attSamples)
                m_attDepth = SubstituteDepth(m_attWidth, m_attHeight, m_attSamples, stencil, m_attDepthFormat);
            else
            {
                m_attDepth = ds->view;
                m_attDepthFormat = ds->format;
                m_attDepthSurface = ds;
            }
            m_attStencil = stencil;
        }
    }
}

void WebGPUBackend::ResetPassCache()
{
    m_curPipeline = nullptr;
    m_curGroup0 = m_curGroup1 = nullptr;
    m_curGroup0Offset = 0xFFFFFFFF;
    for (int i = 0; i < WG_MAX_SLOTS; ++i) { m_curVB[i] = nullptr; m_curVBOffset[i] = 0; }
    m_curIB = nullptr;
    m_viewportValid = m_scissorValid = false;
    m_curStencilRef = 0;
}

void WebGPUBackend::BeginPass()
{
    WGPURenderPassColorAttachment ca = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    ca.view = m_attColor;
    ca.resolveTarget = m_attResolve;
    ca.loadOp = m_pendColor ? WGPULoadOp_Clear : WGPULoadOp_Load;
    ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = m_pendColorValue;
    WGPURenderPassDepthStencilAttachment da = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    da.view = m_attDepth;
    da.depthLoadOp = m_pendDepth ? WGPULoadOp_Clear : WGPULoadOp_Load;
    da.depthStoreOp = WGPUStoreOp_Store;
    da.depthClearValue = m_pendDepthValue;
    if (m_attStencil)
    {
        da.stencilLoadOp = m_pendStencil ? WGPULoadOp_Clear : WGPULoadOp_Load;
        da.stencilStoreOp = WGPUStoreOp_Store;
        da.stencilClearValue = m_pendStencilValue;
    }
    WGPURenderPassDescriptor rp = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &ca;
    rp.depthStencilAttachment = m_attDepth ? &da : nullptr;
    m_pass = wgpuCommandEncoderBeginRenderPass(Encoder(), &rp);
    m_pendColor = m_pendDepth = m_pendStencil = false;
    m_recorded = true;
    if (m_attTexture) m_attTexture->useSerial = m_serial;
    if (m_attSurface) m_attSurface->useSerial = m_serial;
    if (m_attDepthSurface) m_attDepthSurface->useSerial = m_serial;
    ResetPassCache();
    ++g_d3d.rtSwitches;
}

void WebGPUBackend::EnsurePass()
{
    if (m_targetsDirty)
    {
        EndPass();
        ResolveAttachments();
        m_targetsDirty = false;
    }
    if (!m_pass && m_attColor) BeginPass();
}

void WebGPUBackend::EndPass()
{
    if (!m_pass && (m_pendColor || m_pendDepth || m_pendStencil) && m_device && !m_lost)
    {
        // Clears that no draw followed: a pass of their own.
        if (m_targetsDirty)
        {
            ResolveAttachments();
            m_targetsDirty = false;
        }
        if (m_attColor) BeginPass();
        m_pendColor = m_pendDepth = m_pendStencil = false;
    }
    if (m_pass)
    {
        wgpuRenderPassEncoderEnd(m_pass);
        wgpuRenderPassEncoderRelease(m_pass);
        m_pass = nullptr;
    }
}

//------------------------------------------------------------------------------
// Dynamic state
//------------------------------------------------------------------------------
void WebGPUBackend::SetViewport(float x, float y, float w, float h, float zn, float zf)
{
    zn = Clamp(zn, 0.0f, 1.0f);
    zf = Clamp(zf, 0.0f, 1.0f);
    if (zn > zf) zn = zf;
    const float v[6] = {x, y, w, h, zn, zf};
    if (m_viewportValid && memcmp(v, m_curViewport, sizeof v) == 0) return;
    memcpy(m_curViewport, v, sizeof v);
    m_viewportValid = true;
    wgpuRenderPassEncoderSetViewport(m_pass, x, y, w, h, zn, zf);
}

void WebGPUBackend::SetScissor(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    x = Min(x, m_attWidth);
    y = Min(y, m_attHeight);
    w = Min(w, m_attWidth - x);
    h = Min(h, m_attHeight - y);
    const uint32_t s[4] = {x, y, w, h};
    if (m_scissorValid && memcmp(s, m_curScissor, sizeof s) == 0) return;
    memcpy(m_curScissor, s, sizeof s);
    m_scissorValid = true;
    wgpuRenderPassEncoderSetScissorRect(m_pass, x, y, w, h);
}

void WebGPUBackend::SetStencilRef(uint32_t ref)
{
    if (ref == m_curStencilRef) return;
    m_curStencilRef = ref;
    wgpuRenderPassEncoderSetStencilReference(m_pass, ref);
}

void WebGPUBackend::BindVertexBuffer(uint32_t slot, WGPUBuffer buffer, uint64_t offset)
{
    if (m_curVB[slot] == buffer && m_curVBOffset[slot] == offset) return;
    m_curVB[slot] = buffer;
    m_curVBOffset[slot] = offset;
    wgpuRenderPassEncoderSetVertexBuffer(m_pass, slot, buffer, offset, WGPU_WHOLE_SIZE);
}

//------------------------------------------------------------------------------
// Clear
//------------------------------------------------------------------------------
WGPURenderPipeline WebGPUBackend::GetClearPipeline(bool color, bool depth, bool stencil)
{
    const uint32_t key = (color ? 1u : 0u) | (depth ? 2u : 0u) | (stencil ? 4u : 0u) | (m_attSamples << 3) |
                         ((uint32_t)m_attColorFormat << 8) | ((uint32_t)(m_attDepth ? m_attDepthFormat : 0) << 20);
    auto it = m_clearPipelines.find(key);
    if (it != m_clearPipelines.end()) return it->second;
    if (!m_clearModule)
    {
        m_clearModule = CompileModule(kClearShader, "clear");
        WGPUPipelineLayoutDescriptor pld = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        pld.bindGroupLayoutCount = 1;
        pld.bindGroupLayouts = &m_uniformLayout;
        m_clearPipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &pld);
    }
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = m_attColorFormat;
    target.writeMask = color ? WGPUColorWriteMask_All : WGPUColorWriteMask_None;
    WGPUFragmentState fs = WGPU_FRAGMENT_STATE_INIT;
    fs.module = m_clearModule;
    fs.entryPoint = Label("fs");
    fs.targetCount = 1;
    fs.targets = &target;
    WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
    ds.format = m_attDepthFormat;
    ds.depthWriteEnabled = depth ? WGPUOptionalBool_True : WGPUOptionalBool_False;
    ds.depthCompare = WGPUCompareFunction_Always;
    const WGPUStencilFaceState sf = {WGPUCompareFunction_Always, WGPUStencilOperation_Keep, WGPUStencilOperation_Keep,
                                     stencil ? WGPUStencilOperation_Replace : WGPUStencilOperation_Keep};
    ds.stencilFront = ds.stencilBack = sf;
    ds.stencilReadMask = 0xFF;
    ds.stencilWriteMask = stencil ? 0xFF : 0;
    WGPURenderPipelineDescriptor pd = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pd.label = Label("clear");
    pd.layout = m_clearPipelineLayout;
    pd.vertex.module = m_clearModule;
    pd.vertex.entryPoint = Label("vs");
    pd.depthStencil = m_attDepth ? &ds : nullptr;
    pd.multisample.count = m_attSamples;
    pd.fragment = &fs;
    WGPURenderPipeline p = wgpuDeviceCreateRenderPipeline(m_device, &pd);
    m_clearPipelines[key] = p;
    return p;
}

void WebGPUBackend::Clear(const ClearCall &c)
{
    if (!m_device || m_lost) return;
    if (m_targetsDirty && !m_pass)
    {
        ResolveAttachments();
        m_targetsDirty = false;
    }
    if (!m_attColor) return;
    const bool depth = c.depth && m_attDepth, stencil = c.stencil && m_attStencil;
    if (!c.color && !depth && !stencil) return;
    const bool whole = c.rectCount == 1 && c.rects[0].x == 0 && c.rects[0].y == 0 && (uint32_t)c.rects[0].width == m_attWidth &&
                       (uint32_t)c.rects[0].height == m_attHeight;
    if (whole && !m_pass)
    {
        // Before any draw of the pass: the load operations of the pass do it.
        if (c.color) { m_pendColor = true; m_pendColorValue = {c.rgba[0], c.rgba[1], c.rgba[2], c.rgba[3]}; }
        if (depth) { m_pendDepth = true; m_pendDepthValue = c.z; }
        if (stencil) { m_pendStencil = true; m_pendStencilValue = c.stencilValue & 0xFF; }
        return;
    }
    // A quad per rectangle with the clear values (colour output, depth through z, stencil by replacing with the reference).
    ReserveRings(0, c.rectCount * WG_UNIFORM_ALIGN);
    EnsurePass();
    if (!m_pass) return;
    WGPURenderPipeline pipe = GetClearPipeline(c.color, depth, stencil);
    wgpuRenderPassEncoderSetPipeline(m_pass, pipe);
    m_curPipeline = pipe;
    const float values[8] = {c.rgba[0], c.rgba[1], c.rgba[2], c.rgba[3], c.z, 0, 0, 0};
    const uint32_t off = UniformAlloc(sizeof values);
    memcpy(&m_uniformStaging[off], values, sizeof values);
    wgpuRenderPassEncoderSetBindGroup(m_pass, 0, m_uniformGroup, 1, &off);
    m_curGroup0 = m_uniformGroup;
    m_curGroup0Offset = off;
    if (stencil) SetStencilRef(c.stencilValue & 0xFF);
    for (uint32_t i = 0; i < c.rectCount; ++i)
    {
        const ClearRect &r = c.rects[i];
        SetViewport((float)r.x, (float)r.y, (float)r.width, (float)r.height, 0.0f, 1.0f);
        SetScissor((uint32_t)r.x, (uint32_t)r.y, (uint32_t)r.width, (uint32_t)r.height);
        wgpuRenderPassEncoderDraw(m_pass, 3, 1, 0, 0);
    }
    if (g_diagOn) DXW_HIT(Approximated, "WebGPU: clear of a rectangle (or after draws) drawn as a quad");
}

//------------------------------------------------------------------------------
// Pipelines
//------------------------------------------------------------------------------
WGPURenderPipeline WebGPUBackend::GetPipeline(const WgPipelineKey &k, const DrawCall &c, WGProgram *p)
{
    if (m_lastPipeline && memcmp(&k, &m_lastKey, sizeof k) == 0) return m_lastPipeline;
    const uint64_t h = HashBytes(&k, sizeof k);
    auto &list = m_pipelines[h];
    for (auto &e : list)
        if (memcmp(&e.first, &k, sizeof k) == 0)
        {
            m_lastKey = k;
            m_lastPipeline = e.second;
            return e.second;
        }

    // Vertex buffers: one per used stream, its attributes at the D3DVSDE register locations.
    const VertexLayout *layout = c.layout;
    WGPUVertexBufferLayout vbl[WG_MAX_SLOTS];
    WGPUVertexAttribute attrs[WG_MAX_SLOTS][16];
    uint32_t seen = 0;
    for (uint32_t s = 0; s < k.slotCount; ++s)
    {
        vbl[s] = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
        vbl[s].stepMode = k.instanced ? WGPUVertexStepMode_Instance : WGPUVertexStepMode_Vertex;
        vbl[s].arrayStride = k.slotStride[s];
        vbl[s].attributes = attrs[s];
        size_t n = 0;
        for (int i = 0; i < layout->count; ++i)
        {
            const VertexElement &e = layout->elems[i];
            if (e.stream != k.slotStream[s] || (seen & (1u << e.reg))) continue;
            seen |= 1u << e.reg;
            uint32_t kind;
            attrs[s][n] = WGPU_VERTEX_ATTRIBUTE_INIT;
            attrs[s][n].format = ElementFormat(e, kind);
            attrs[s][n].offset = e.offset;
            attrs[s][n].shaderLocation = e.reg;
            ++n;
        }
        vbl[s].attributeCount = n;
    }

    WGPUBlendState blend;
    blend.color = {(WGPUBlendOperation)k.blendOp, (WGPUBlendFactor)k.srcBlend, (WGPUBlendFactor)k.dstBlend};
    blend.alpha = blend.color;
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = (WGPUTextureFormat)k.colorFormat;
    target.blend = k.blend ? &blend : nullptr;
    target.writeMask = k.colorMask;
    WGPUFragmentState fs = WGPU_FRAGMENT_STATE_INIT;
    fs.module = p->fs;
    fs.entryPoint = Label("fs_main");
    fs.targetCount = 1;
    fs.targets = &target;

    WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
    ds.format = (WGPUTextureFormat)k.depthFormat;
    ds.depthWriteEnabled = k.depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
    ds.depthCompare = (WGPUCompareFunction)k.depthFunc;
    const WGPUStencilFaceState sf = {(WGPUCompareFunction)k.stencilFunc, (WGPUStencilOperation)k.stencilFail,
                                     (WGPUStencilOperation)k.stencilZFail, (WGPUStencilOperation)k.stencilPass};
    ds.stencilFront = ds.stencilBack = sf;
    ds.stencilReadMask = k.stencilMask;
    ds.stencilWriteMask = k.stencilWriteMask;
    ds.depthBias = k.depthBias;
    ds.depthBiasSlopeScale = k.depthSlope;

    WGPURenderPipelineDescriptor pd = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pd.layout = p->pipelineLayout;
    pd.vertex.module = VertexModule(p, k.vsSignature);
    pd.vertex.entryPoint = Label("vs_main");
    pd.vertex.bufferCount = k.slotCount;
    pd.vertex.buffers = vbl;
    pd.primitive.topology = (WGPUPrimitiveTopology)k.topology;
    pd.primitive.frontFace = (WGPUFrontFace)k.frontFace;
    pd.primitive.cullMode = (WGPUCullMode)k.cullMode;
    pd.depthStencil = k.depthFormat ? &ds : nullptr;
    pd.multisample.count = k.samples;
    pd.fragment = &fs;
    WGPURenderPipeline pipe = wgpuDeviceCreateRenderPipeline(m_device, &pd);
    list.push_back({k, pipe});
    m_lastKey = k;
    m_lastPipeline = pipe;
    return pipe;
}

//------------------------------------------------------------------------------
// Textures
//------------------------------------------------------------------------------
WGPUBindGroup WebGPUBackend::TextureBindGroup(WGProgram *p, const DrawCall &c)
{
    BindGroupKey key;
    memset(&key, 0, sizeof key);
    key.layout = p->texLayout;
    WGPUTextureView views[MAX_STAGES] = {};
    WGPUSampler samplers[MAX_STAGES] = {};
    for (int s = 0; s < MAX_STAGES; ++s)
    {
        const uint32_t type = (p->texSig >> (s * 2)) & 3;
        if (!type) continue;
        const TextureBinding &b = c.textures[s];
        WGTexture *t = static_cast<WGTexture *>(b.texture);
        if (t && t->texture && (uint32_t)t->desc.kind == type)
        {
            views[s] = t->View((uint32_t)Max(0, b.baseLevel));
            key.tex[s] = t->uid;
            key.level[s] = (uint32_t)Max(0, b.baseLevel);
            WGSampler *sm = static_cast<WGSampler *>(b.sampler);
            samplers[s] = sm && sm->sampler ? sm->sampler : m_whiteSampler;
            key.sampler[s] = sm ? sm->uid : 0;
            t->useSerial = m_serial;
        }
        else
        {
            views[s] = m_whiteView[type - 1];
            samplers[s] = m_whiteSampler;
        }
    }
    const uint64_t h = HashBytes(&key, sizeof key);
    auto &list = m_bindGroups[h];
    for (auto &e : list)
        if (memcmp(&e.first, &key, sizeof key) == 0) return e.second;
    WGPUBindGroupEntry entries[MAX_STAGES * 2];
    size_t n = 0;
    for (int s = 0; s < MAX_STAGES; ++s)
    {
        if (!views[s]) continue;
        entries[n] = WGPU_BIND_GROUP_ENTRY_INIT;
        entries[n].binding = (uint32_t)s * 2;
        entries[n].textureView = views[s];
        ++n;
        entries[n] = WGPU_BIND_GROUP_ENTRY_INIT;
        entries[n].binding = (uint32_t)s * 2 + 1;
        entries[n].sampler = samplers[s];
        ++n;
    }
    WGPUBindGroupDescriptor gd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    gd.layout = p->texLayout;
    gd.entryCount = n;
    gd.entries = entries;
    WGPUBindGroup g = wgpuDeviceCreateBindGroup(m_device, &gd);
    list.push_back({key, g});
    return g;
}

//------------------------------------------------------------------------------
// Draw
//------------------------------------------------------------------------------
bool WebGPUBackend::Draw(const DrawCall &c)
{
    ++m_drawCounter;
    WGProgram *p = static_cast<WGProgram *>(c.program);
    if (!m_device || m_lost) return true;
    if (!p || !p->valid) return false;
    const VertexLayout *layout = c.layout;
    if (!layout || !layout->count) return false;
    const bool userData = c.userVertices != nullptr;
    const bool indexed = c.indexed;

    // Vertex buffer slots: the streams the layout uses, in order.
    int slotOfStream[MAX_STREAMS];
    for (int &s : slotOfStream) s = -1;
    uint8_t slotStream[WG_MAX_SLOTS] = {};
    uint32_t slotStride[WG_MAX_SLOTS] = {};
    uint32_t slotCount = 0;
    uint32_t signature = 0;
    for (int i = 0; i < layout->count; ++i)
    {
        const VertexElement &e = layout->elems[i];
        if (slotOfStream[e.stream] < 0)
        {
            if (slotCount == WG_MAX_SLOTS) { LogUnsupported("more than 8 vertex streams with the WebGPU backend"); return false; }
            slotOfStream[e.stream] = (int)slotCount;
            slotStream[slotCount] = e.stream;
            slotStride[slotCount] = userData && e.stream == 0 ? c.userStride
                                                               : (c.streams[e.stream].stride ? c.streams[e.stream].stride : layout->stride[e.stream]);
            ++slotCount;
        }
        uint32_t kind;
        ElementFormat(e, kind);
        if (!WgslInputKind(signature, e.reg)) signature |= kind << (e.reg * 2);
    }
    // Buffers from storage: pending uploads first (they may submit what was recorded).
    for (uint32_t s = 0; s < slotCount; ++s)
    {
        if (userData && slotStream[s] == 0) continue;
        BufferStorage *vb = c.streams[slotStream[s]].buffer;
        if (!vb) return false;
        vb->Flush();
        if (!vb->Gpu()) return false;
    }
    if (indexed && !userData)
    {
        if (!c.indexBuffer) return false;
        c.indexBuffer->Flush();
        if (!c.indexBuffer->Gpu()) return false;
    }

    // What has to go through the stream ring.
    const bool points = c.topology == Topology::PointList;
    const bool triangles = c.topology == Topology::TriangleList || c.topology == Topology::TriangleStrip || c.topology == Topology::TriangleFan;
    const bool wire = triangles && c.fill != FillMode::Solid;
    const bool rebuild = wire || c.topology == Topology::TriangleFan;
    const uint32_t isz = c.index32 ? 4 : 2;
    const uint8_t *indexSrc = nullptr;
    if (indexed) indexSrc = userData ? static_cast<const uint8_t *>(c.userIndices) : c.indexBuffer->Data() + (size_t)c.start * isz;
    const IndexFetch fetch = {indexed ? indexSrc : nullptr, c.index32, userData ? 0u : (indexed ? c.baseVertex : 0u),
                              userData ? 0u : (indexed ? 0u : c.start)};
    // Strides that are not a multiple of 4 (WebGPU's arrayStride) are repacked (PaddedVertexStride, core/backend.h).
    bool repack[WG_MAX_SLOTS] = {};
    for (uint32_t s = 0; s < slotCount; ++s) repack[s] = PaddedVertexStride(slotStride[s]) != slotStride[s];
    const bool gather = points && indexed; // indexed point lists: the vertices are gathered, then drawn as instances

    std::vector<uint32_t> *indices = nullptr;
    WGPUPrimitiveTopology topology;
    switch (c.topology)
    {
    case Topology::PointList: topology = WGPUPrimitiveTopology_TriangleStrip; break;
    case Topology::LineList: topology = WGPUPrimitiveTopology_LineList; break;
    case Topology::LineStrip: topology = WGPUPrimitiveTopology_LineStrip; break;
    case Topology::TriangleStrip: topology = WGPUPrimitiveTopology_TriangleStrip; break;
    default: topology = WGPUPrimitiveTopology_TriangleList; break;
    }
    if (rebuild)
    {
        // WebGPU has no fans and no polygon mode: the triangles as a list (fans keep Direct3D's flat shading vertex,
        // the second of each triangle, first), then their edges or vertices.
        ExpandTriangles(c.topology, c.primitiveCount, fetch, c.topology == Topology::TriangleFan, m_triScratch);
        indices = &m_triScratch;
        topology = WGPUPrimitiveTopology_TriangleList;
        if (wire && c.fill == FillMode::Wireframe)
        {
            TrianglesToLines(m_triScratch, m_lineScratch);
            indices = &m_lineScratch;
            topology = WGPUPrimitiveTopology_LineList;
        }
        else if (wire)
            topology = WGPUPrimitiveTopology_PointList;
        if (g_diagOn)
        {
            if (wire) DXW_HIT(Approximated, "fill mode %s rebuilt from the triangle list on the CPU", c.fill == FillMode::Wireframe ? "WIREFRAME" : "POINT");
            else DXW_HIT(Approximated, "WebGPU: triangle fan expanded to a triangle list");
        }
    }

    // Ring space: user vertices (or gathered / repacked ones), user or rebuilt indices, the uniform block.
    const uint32_t vertexCount = userData ? c.userVertexCount : 0;
    uint32_t streamBytes = 0;
    for (uint32_t s = 0; s < slotCount; ++s)
    {
        const uint32_t stride = PaddedVertexStride(slotStride[s]);
        if (gather) streamBytes += c.count * stride + 16;
        else if (userData && slotStream[s] == 0) streamBytes += vertexCount * stride + 16;
        else if (repack[s]) streamBytes += (c.streams[slotStream[s]].buffer->Size() / Max<uint32_t>(1, slotStride[s]) + 1) * stride + 16;
    }
    if (indices) streamBytes += (uint32_t)indices->size() * 4 + 16;
    else if (indexed && userData) streamBytes += c.count * isz + 16;
    UploadUniforms(p, c.uniformVersions, *c.uniforms);
    ReserveRings(streamBytes, p->layout.blockSize + WG_UNIFORM_ALIGN);

    // Vertex data placement: buffer + offset per slot.
    WGPUBuffer slotBuffer[WG_MAX_SLOTS] = {};
    uint64_t slotOffset[WG_MAX_SLOTS] = {};
    for (uint32_t s = 0; s < slotCount; ++s)
    {
        const uint32_t stride = slotStride[s];
        const uint32_t outStride = PaddedVertexStride(stride);
        const uint8_t *src;
        uint32_t srcVertices;
        WGBuffer *gb = nullptr;
        if (userData && slotStream[s] == 0)
        {
            src = static_cast<const uint8_t *>(c.userVertices);
            srcVertices = vertexCount;
        }
        else
        {
            BufferStorage *vb = c.streams[slotStream[s]].buffer;
            gb = static_cast<WGBuffer *>(vb->Gpu());
            src = vb->Data();
            srcVertices = stride ? vb->Size() / stride : 0;
        }
        if (gather)
        {
            const uint32_t at = StreamAlloc(c.count * outStride, 4);
            for (uint32_t i = 0; i < c.count; ++i)
            {
                const uint32_t v = fetch(i);
                uint8_t *d = &m_streamStaging[at + (size_t)i * outStride];
                if (v < srcVertices) memcpy(d, src + (size_t)v * stride, stride);
                else memset(d, 0, stride);
            }
            slotBuffer[s] = m_streamBuffer;
            slotOffset[s] = at;
            slotStride[s] = outStride;
        }
        else if (!gb || repack[s])
        {
            const uint32_t n = gb ? srcVertices : vertexCount;
            const uint32_t at = StreamAlloc(n * outStride, 4);
            RepackVertices(src, stride, n, outStride, &m_streamStaging[at]);
            slotBuffer[s] = m_streamBuffer;
            slotOffset[s] = at;
            slotStride[s] = outStride;
        }
        else
        {
            slotBuffer[s] = gb->buffer;
            slotOffset[s] = 0;
            gb->useSerial = m_serial;
        }
    }

    // Index data.
    WGPUBuffer ibuf = nullptr;
    uint64_t ioffset = 0;
    WGPUIndexFormat iformat = c.index32 ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16;
    uint32_t firstIndex = 0, drawCount = c.count;
    int32_t baseVertex = 0;
    if (indices)
    {
        const uint32_t at = StreamAlloc((uint32_t)indices->size() * 4, 4);
        memcpy(&m_streamStaging[at], indices->data(), indices->size() * 4);
        ibuf = m_streamBuffer;
        ioffset = at;
        iformat = WGPUIndexFormat_Uint32;
        drawCount = (uint32_t)indices->size();
    }
    else if (indexed && !gather)
    {
        if (userData)
        {
            const uint32_t at = StreamAlloc(c.count * isz, 4);
            memcpy(&m_streamStaging[at], c.userIndices, (size_t)c.count * isz);
            ibuf = m_streamBuffer;
            ioffset = at;
        }
        else
        {
            WGBuffer *ib = static_cast<WGBuffer *>(c.indexBuffer->Gpu());
            ibuf = ib->buffer;
            ib->useSerial = m_serial;
            firstIndex = c.start;
            baseVertex = (int32_t)c.baseVertex;
        }
    }

    // The program's uniform block, when it changed or the ring started over.
    if (p->blockDirty || p->ringEpoch != m_epoch)
    {
        const uint32_t at = UniformAlloc(p->layout.blockSize);
        memcpy(&m_uniformStaging[at], p->block.data(), p->layout.blockSize);
        p->ringOffset = at;
        p->ringEpoch = m_epoch;
        p->blockDirty = false;
    }

    EnsurePass();
    if (!m_pass) return true;

    // Render pipeline.
    const PipelineState &ps = *c.pipeline;
    WgPipelineKey k;
    memset(&k, 0, sizeof k);
    k.program = p->uid;
    k.vsSignature = signature;
    k.layoutId = layout->id;
    k.slotCount = (uint8_t)slotCount;
    for (uint32_t s = 0; s < slotCount; ++s) { k.slotStride[s] = (uint16_t)slotStride[s]; k.slotStream[s] = slotStream[s]; }
    k.topology = (uint8_t)topology;
    k.instanced = points ? 1 : 0;
    const bool filled = topology == WGPUPrimitiveTopology_TriangleList || topology == WGPUPrimitiveTopology_TriangleStrip;
    // Point quads are not culled; the winding of the flipped GL convention (PipelineState::frontFace) is the opposite
    // of WebGPU's, whose framebuffer y points down like Direct3D's.
    k.cullMode = (uint8_t)(ps.cull && filled && !points ? WGPUCullMode_Back : WGPUCullMode_None);
    k.frontFace = (uint8_t)(ps.frontFace == FrontFace::CCW ? WGPUFrontFace_CW : WGPUFrontFace_CCW);
    k.colorMask = ps.colorMask;
    if (ps.blend)
    {
        k.blend = 1;
        k.blendOp = (uint8_t)ToWG(ps.blendOp);
        const bool minmax = ps.blendOp == BlendOp::Min || ps.blendOp == BlendOp::Max; // factors must be one (ignored by GL)
        k.srcBlend = (uint8_t)(minmax ? WGPUBlendFactor_One : ToWG(ps.srcBlend));
        k.dstBlend = (uint8_t)(minmax ? WGPUBlendFactor_One : ToWG(ps.dstBlend));
    }
    if (m_attDepth)
    {
        k.depthFormat = (uint32_t)m_attDepthFormat;
        k.depthWrite = ps.depthTest && ps.depthMask;
        k.depthFunc = (uint8_t)(ps.depthTest ? ToWG(ps.depthFunc) : WGPUCompareFunction_Always);
        if (m_attStencil && ps.stencilTest)
        {
            k.stencilFunc = (uint8_t)ToWG(ps.stencilFunc);
            k.stencilFail = (uint8_t)ToWG(ps.stencilFail);
            k.stencilZFail = (uint8_t)ToWG(ps.stencilZFail);
            k.stencilPass = (uint8_t)ToWG(ps.stencilPass);
            k.stencilMask = (uint8_t)ps.stencilMask;
            k.stencilWriteMask = (uint8_t)ps.stencilWriteMask;
        }
        else
        {
            k.stencilFunc = (uint8_t)WGPUCompareFunction_Always;
            k.stencilFail = k.stencilZFail = k.stencilPass = (uint8_t)WGPUStencilOperation_Keep;
            k.stencilMask = 0xFF;
        }
        if (ps.polyOffset && filled && !points)
        {
            k.depthBias = (int32_t)ps.polyUnits;
            k.depthSlope = ps.polyFactor;
        }
    }
    k.samples = (uint8_t)m_attSamples;
    k.colorFormat = (uint32_t)m_attColorFormat;
    WGPURenderPipeline pipe = GetPipeline(k, c, p);
    if (pipe != m_curPipeline)
    {
        wgpuRenderPassEncoderSetPipeline(m_pass, pipe);
        m_curPipeline = pipe;
        ++g_d3d.programSwitches;
    }

    // Bind groups.
    if (m_curGroup0 != m_uniformGroup || m_curGroup0Offset != p->ringOffset)
    {
        wgpuRenderPassEncoderSetBindGroup(m_pass, 0, m_uniformGroup, 1, &p->ringOffset);
        m_curGroup0 = m_uniformGroup;
        m_curGroup0Offset = p->ringOffset;
    }
    if (p->texSig)
    {
        WGPUBindGroup g = TextureBindGroup(p, c);
        if (g != m_curGroup1)
        {
            wgpuRenderPassEncoderSetBindGroup(m_pass, 1, g, 0, nullptr);
            m_curGroup1 = g;
            ++g_d3d.textureBinds;
        }
    }

    // Vertex and index buffers.
    for (uint32_t s = 0; s < slotCount; ++s) BindVertexBuffer(s, slotBuffer[s], slotOffset[s]);
    if (ibuf && (ibuf != m_curIB || ioffset != m_curIBOffset || iformat != m_curIBFormat))
    {
        wgpuRenderPassEncoderSetIndexBuffer(m_pass, ibuf, iformat, ioffset, WGPU_WHOLE_SIZE);
        m_curIB = ibuf;
        m_curIBOffset = ioffset;
        m_curIBFormat = iformat;
    }

    // Dynamic state.
    SetViewport((float)ps.vp[0], (float)ps.vp[1], (float)ps.vp[2], (float)ps.vp[3], ps.depthNear, ps.depthFar);
    if (ps.scissor) SetScissor((uint32_t)Max(0, ps.sc[0]), (uint32_t)Max(0, ps.sc[1]), (uint32_t)Max(0, ps.sc[2]), (uint32_t)Max(0, ps.sc[3]));
    else SetScissor(0, 0, m_attWidth, m_attHeight);
    if (m_attStencil) SetStencilRef((uint32_t)ps.stencilRef & 0xFF);

    // The draw.
    if (points)
        wgpuRenderPassEncoderDraw(m_pass, 4, gather ? c.count : c.count, 0, gather || userData ? 0 : c.start);
    else if (ibuf)
        wgpuRenderPassEncoderDrawIndexed(m_pass, drawCount, 1, firstIndex, baseVertex, 0);
    else
        wgpuRenderPassEncoderDraw(m_pass, c.count, 1, userData ? 0 : c.start, 0);
    return true;
}

} // namespace dxwebgl2
