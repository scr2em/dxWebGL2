/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * WebGPU backend: buffers, textures (the format table and the conversions on upload), render target and depth/stencil
 * surfaces, samplers, read-back.
 */
#include "core/webgpu/wgpu_backend.h"

namespace dxwebgl2 {

namespace {

inline WGPUStringView Label(const char *s) { return WGPUStringView{s, WGPU_STRLEN}; }

WGPUTextureFormat DepthFormat(D3DFORMAT f)
{
    switch (f)
    {
    case D3DFMT_D16: case D3DFMT_D16_LOCKABLE: return WGPUTextureFormat_Depth16Unorm;
    case D3DFMT_D32: case D3DFMT_D24X8: return WGPUTextureFormat_Depth24Plus;
    default: return WGPUTextureFormat_Depth24PlusStencil8; // D24S8, D24X4S4, D15S1
    }
}

WGPUAddressMode MapAddress(uint8_t a)
{
    switch (a)
    {
    case D3DTADDRESS_WRAP: return WGPUAddressMode_Repeat;
    case D3DTADDRESS_MIRROR: case D3DTADDRESS_MIRRORONCE: return WGPUAddressMode_MirrorRepeat;
    default: return WGPUAddressMode_ClampToEdge; // CLAMP, BORDER (emulated in the shader)
    }
}

} // namespace

//------------------------------------------------------------------------------
// Buffers
//------------------------------------------------------------------------------
GpuBuffer *WebGPUBackend::CreateBuffer(BufferKind, uint32_t size)
{
    if (!m_device || m_lost) return nullptr;
    WGBuffer *b = new WGBuffer();
    b->cpuSize = size;
    b->size = Max<uint32_t>(4, (size + 3) & ~3u);
    WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
    bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
    bd.size = b->size;
    b->buffer = wgpuDeviceCreateBuffer(m_device, &bd);
    return b;
}

void WebGPUBackend::DestroyBuffer(GpuBuffer *buffer)
{
    WGBuffer *b = static_cast<WGBuffer *>(buffer);
    if (!b) return;
    for (int i = 0; i < WG_MAX_SLOTS; ++i)
        if (m_curVB[i] == b->buffer) m_curVB[i] = nullptr;
    if (m_curIB == b->buffer) m_curIB = nullptr;
    if (b->buffer)
    {
        // Commands that use it may still be recorded: destroy() would invalidate them, release() keeps it alive until then.
        wgpuBufferRelease(b->buffer);
    }
    delete b;
}

void WebGPUBackend::WriteBuffer(GpuBuffer *buffer, uint32_t offset, uint32_t size, const void *data, bool)
{
    WGBuffer *b = static_cast<WGBuffer *>(buffer);
    if (!b || !b->buffer || !size || m_lost) return;
    // A recorded draw reads the buffer: it must see the contents from before this write.
    if (b->useSerial == m_serial && m_recorded)
    {
        if (g_diagOn) DiagHit(Hit::Perf, "WebGPU: submit before rewriting a buffer drawn with in this frame");
        Submit();
    }
    // writeBuffer copies multiples of 4 bytes at multiples of 4: widen the range inside the caller's copy of the buffer
    // (data - offset is its start), padding past its end.
    const uint8_t *base = static_cast<const uint8_t *>(data) - offset;
    const uint32_t begin = offset & ~3u;
    const uint32_t end = Min<uint32_t>((offset + size + 3) & ~3u, b->size);
    if (end <= b->cpuSize)
        wgpuQueueWriteBuffer(m_queue, b->buffer, begin, base + begin, end - begin);
    else
    {
        m_uploadScratch.assign(end - begin, 0);
        memcpy(m_uploadScratch.data(), base + begin, b->cpuSize - begin);
        wgpuQueueWriteBuffer(m_queue, b->buffer, begin, m_uploadScratch.data(), end - begin);
    }
    g_gl.uploadBytes += end - begin;
}

//------------------------------------------------------------------------------
// Formats
//------------------------------------------------------------------------------
/// WebGPU storage of a D3DFORMAT (docs/WEBGPU.md, section 3): what WebGPU has is used as it is, A8R8G8B8 as bgra8unorm
/// (the same byte order), X8R8G8B8 with the alpha set, the 16/24-bit, luminance and alpha formats expanded to
/// bgra8unorm, DXT as BC when the device has texture-compression-bc (decoded to rgba8unorm otherwise).
WgFormat WebGPUBackend::MapFormat(D3DFORMAT format, bool decode) const
{
    switch (format)
    {
    case D3DFMT_A8R8G8B8: return {WGPUTextureFormat_BGRA8Unorm, WgConv::None, 4, 0};
    case D3DFMT_X8R8G8B8: return {WGPUTextureFormat_BGRA8Unorm, WgConv::ForceAlpha, 4, 0};
    case D3DFMT_V8U8: return {WGPUTextureFormat_RG8Snorm, WgConv::None, 2, 0};
    case D3DFMT_Q8W8V8U8: return {WGPUTextureFormat_RGBA8Snorm, WgConv::None, 4, 0};
    case D3DFMT_DXT1:
        if (!decode) return {WGPUTextureFormat_BC1RGBAUnorm, WgConv::None, 0, 8};
        return {WGPUTextureFormat_RGBA8Unorm, WgConv::DecodeDXT, 4, 0};
    case D3DFMT_DXT2: case D3DFMT_DXT3:
        if (!decode) return {WGPUTextureFormat_BC2RGBAUnorm, WgConv::None, 0, 16};
        return {WGPUTextureFormat_RGBA8Unorm, WgConv::DecodeDXT, 4, 0};
    case D3DFMT_DXT4: case D3DFMT_DXT5:
        if (!decode) return {WGPUTextureFormat_BC3RGBAUnorm, WgConv::None, 0, 16};
        return {WGPUTextureFormat_RGBA8Unorm, WgConv::DecodeDXT, 4, 0};
    default: return {WGPUTextureFormat_BGRA8Unorm, WgConv::Expand, 4, 0};
    }
}

/// Converts a rectangle of D3D memory to the WebGPU format of `t`; returns the data and its row pitch.
const uint8_t *WebGPUBackend::ConvertForUpload(const WGTexture &t, const uint8_t *src, uint32_t srcPitch, uint32_t w, uint32_t h,
                                               uint32_t &outPitch)
{
    const D3DFORMAT f = t.desc.format;
    switch (t.fmt.conv)
    {
    case WgConv::None:
        outPitch = srcPitch;
        return src;
    case WgConv::DecodeDXT:
        outPitch = w * 4;
        m_uploadScratch.resize((size_t)w * h * 4);
        DecodeDXT(f, src, srcPitch, w, h, m_uploadScratch.data(), outPitch, true);
        return m_uploadScratch.data();
    case WgConv::ForceAlpha:
        outPitch = w * 4;
        m_uploadScratch.resize((size_t)w * h * 4);
        for (uint32_t y = 0; y < h; ++y)
        {
            const uint8_t *s = src + (size_t)y * srcPitch;
            uint8_t *d = &m_uploadScratch[(size_t)y * outPitch];
            memcpy(d, s, (size_t)w * 4);
            for (uint32_t x = 0; x < w; ++x) d[x * 4 + 3] = 255;
        }
        return m_uploadScratch.data();
    case WgConv::Expand:
    {
        const FormatInfo *info = GetFormatInfo(f);
        const uint32_t bpp = info ? Max<uint32_t>(1, info->bits / 8) : 4;
        outPitch = w * 4;
        m_uploadScratch.resize((size_t)w * h * 4);
        for (uint32_t y = 0; y < h; ++y)
        {
            const uint8_t *s = src + (size_t)y * srcPitch;
            uint8_t *d = &m_uploadScratch[(size_t)y * outPitch];
            for (uint32_t x = 0; x < w; ++x, s += bpp, d += 4)
            {
                const uint32_t argb = UnpackPixel(f, s); // memory order B, G, R, A = bgra8unorm
                memcpy(d, &argb, 4);
            }
        }
        return m_uploadScratch.data();
    }
    }
    outPitch = srcPitch;
    return src;
}

//------------------------------------------------------------------------------
// Textures
//------------------------------------------------------------------------------
WGPUTextureView WGTexture::View(uint32_t baseLevel)
{
    if (!texture) return nullptr;
    baseLevel = Min(baseLevel, desc.levels - 1);
    if (views.size() < desc.levels) views.resize(desc.levels, nullptr);
    if (!views[baseLevel])
    {
        WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        vd.dimension = desc.kind == TextureKind::Cube ? WGPUTextureViewDimension_Cube
                                                      : (desc.kind == TextureKind::Volume ? WGPUTextureViewDimension_3D : WGPUTextureViewDimension_2D);
        vd.baseMipLevel = baseLevel;
        vd.mipLevelCount = desc.levels - baseLevel;
        views[baseLevel] = wgpuTextureCreateView(texture, &vd);
    }
    return views[baseLevel];
}

WGPUTextureView WGTexture::Target(uint32_t face, uint32_t level)
{
    if (!texture || !renderTarget) return nullptr;
    const size_t i = (size_t)face * desc.levels + level;
    if (targets.size() < (size_t)(desc.kind == TextureKind::Cube ? 6 : 1) * desc.levels)
        targets.resize((size_t)(desc.kind == TextureKind::Cube ? 6 : 1) * desc.levels, nullptr);
    if (i >= targets.size()) return nullptr;
    if (!targets[i])
    {
        WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        vd.dimension = WGPUTextureViewDimension_2D;
        vd.baseMipLevel = level;
        vd.mipLevelCount = 1;
        vd.baseArrayLayer = face;
        vd.arrayLayerCount = 1;
        targets[i] = wgpuTextureCreateView(texture, &vd);
    }
    return targets[i];
}

void WebGPUBackend::CreateTextureObject(WGTexture *t)
{
    const TextureDesc &d = t->desc;
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
    if (t->renderTarget) td.usage |= WGPUTextureUsage_RenderAttachment;
    td.dimension = d.kind == TextureKind::Volume ? WGPUTextureDimension_3D : WGPUTextureDimension_2D;
    td.size = {d.width, d.height, d.kind == TextureKind::Cube ? 6u : (d.kind == TextureKind::Volume ? d.depth : 1u)};
    td.format = t->fmt.format;
    td.mipLevelCount = d.levels;
    t->texture = wgpuDeviceCreateTexture(m_device, &td);
}

void WebGPUBackend::ReleaseTextureObject(WGTexture *t)
{
    for (WGPUTextureView v : t->views)
        if (v) wgpuTextureViewRelease(v);
    for (WGPUTextureView v : t->targets)
        if (v) wgpuTextureViewRelease(v);
    t->views.clear();
    t->targets.clear();
    if (t->texture) { wgpuTextureRelease(t->texture); t->texture = nullptr; }
}

GpuTexture *WebGPUBackend::CreateTexture(const TextureDesc &d)
{
    if (!m_device || m_lost) return nullptr;
    WGTexture *t = new WGTexture();
    t->desc = d;
    t->uid = m_nextUid++;
    const FormatInfo *info = GetFormatInfo(d.format);
    t->renderTarget = info && info->renderTarget && d.kind != TextureKind::Volume;
    // DXT is decoded on the CPU when the core decided so (DecodeDxtOnCpu: no texture-compression-bc, a size that is not
    // a multiple of the block size, volumes); BC textures otherwise.
    const bool decode = d.decodeDxt || !m_bc;
    t->fmt = MapFormat(d.format, decode);
    if (t->fmt.conv == WgConv::Expand && info && info->blockBytes) t->fmt = MapFormat(d.format, true);
    CreateTextureObject(t);
    return t;
}

void WebGPUBackend::ForgetTextureInBindGroups(uint32_t uid)
{
    for (auto it = m_bindGroups.begin(); it != m_bindGroups.end();)
    {
        auto &list = it->second;
        for (size_t i = 0; i < list.size();)
        {
            bool uses = false;
            for (int s = 0; s < MAX_STAGES; ++s) uses = uses || list[i].first.tex[s] == uid;
            if (uses)
            {
                if (m_curGroup1 == list[i].second) m_curGroup1 = nullptr;
                wgpuBindGroupRelease(list[i].second);
                list[i] = list.back();
                list.pop_back();
            }
            else ++i;
        }
        it = list.empty() ? m_bindGroups.erase(it) : std::next(it);
    }
}

void WebGPUBackend::DestroyTexture(GpuTexture *texture)
{
    WGTexture *t = static_cast<WGTexture *>(texture);
    if (!t) return;
    if (m_attTexture == t)
    {
        EndPass();
        m_attTexture = nullptr;
        m_targetsDirty = true;
    }
    ForgetTextureInBindGroups(t->uid);
    ReleaseTextureObject(t);
    delete t;
}

void WebGPUBackend::WriteTexture(GpuTexture *texture, const TextureRegion &r, const uint8_t *src, uint32_t rowPitch)
{
    WGTexture *t = static_cast<WGTexture *>(texture);
    if (!t || !t->texture || m_lost) return;
    // A recorded command samples or renders into the texture: it must see the contents from before this write.
    if (t->useSerial == m_serial && m_recorded)
    {
        if (g_diagOn) DiagHit(Hit::Perf, "WebGPU: submit before updating a texture used in this frame");
        Submit();
    }
    const bool block = t->fmt.blockBytes != 0;
    uint32_t w = r.width, h = r.height;
    const uint32_t rows = r.depth > 1 ? r.height * r.depth : r.height;
    uint32_t pitch = rowPitch;
    const uint8_t *data;
    if (t->fmt.conv == WgConv::DecodeDXT && r.depth > 1)
    {
        // DXT slices end with a partial block row when their height is not a multiple of 4: decoded one by one.
        pitch = w * 4;
        m_uploadScratch.resize((size_t)w * h * 4 * r.depth);
        DecodeDXTVolume(t->desc.format, src, rowPitch, w, h, r.depth, m_uploadScratch.data(), true);
        data = m_uploadScratch.data();
    }
    else if (r.depth > 1 || t->desc.kind == TextureKind::Volume)
        data = ConvertForUpload(*t, src, rowPitch, w, rows, pitch); // the slices of a volume level are consecutive rows
    else
        data = ConvertForUpload(*t, src, rowPitch, w, h, pitch);
    WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    dst.texture = t->texture;
    dst.mipLevel = r.level;
    dst.origin = {r.x, r.y, t->desc.kind == TextureKind::Cube ? r.face : 0u};
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.bytesPerRow = pitch;
    WGPUExtent3D size = {w, h, Max<uint32_t>(1, r.depth)};
    size_t bytes;
    if (block)
    {
        // Copies of compressed levels cover whole blocks (the physical size of the small mip levels).
        size.width = (w + 3) & ~3u;
        size.height = (h + 3) & ~3u;
        layout.rowsPerImage = size.height / 4;
        bytes = (size_t)pitch * (size.height / 4 - 1) + (size_t)(size.width / 4) * t->fmt.blockBytes;
    }
    else
    {
        // The exact amount (the rows of a sub-rectangle are followed by less than a full pitch).
        layout.rowsPerImage = h;
        bytes = (size_t)pitch * (h * size.depthOrArrayLayers - 1) + (size_t)w * t->fmt.bytes;
    }
    wgpuQueueWriteTexture(m_queue, &dst, data, bytes, &layout, &size);
    g_gl.uploadBytes += (uint32_t)bytes;
}

bool WebGPUBackend::ReadTexture(GpuTexture *texture, uint32_t face, uint32_t level, const RECT &rc, uint8_t *rgbaOut)
{
    WGTexture *t = static_cast<WGTexture *>(texture);
    if (!t || !t->texture || m_lost || t->desc.kind == TextureKind::Volume) return false;
    return ReadTextureRect(t->texture, t->fmt.format, t->desc.kind == TextureKind::Cube ? face : 0, level, rc, rgbaOut);
}

//------------------------------------------------------------------------------
// Surfaces
//------------------------------------------------------------------------------
GpuSurface *WebGPUBackend::CreateSurface(const SurfaceDesc &d)
{
    if (!m_device || m_lost) return nullptr;
    WGSurface *s = new WGSurface();
    s->desc = d;
    s->samples = d.samples ? 4 : 1;
    s->format = d.depthStencil ? DepthFormat(d.format) : MapFormat(d.format, true).format;
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    td.label = Label(d.depthStencil ? "depth/stencil surface" : "render target surface");
    td.usage = WGPUTextureUsage_RenderAttachment;
    if (!d.depthStencil && s->samples == 1) td.usage |= WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding;
    td.size = {d.width, d.height, 1};
    td.format = s->format;
    td.sampleCount = s->samples;
    s->texture = wgpuDeviceCreateTexture(m_device, &td);
    s->view = wgpuTextureCreateView(s->texture, nullptr);
    return s;
}

void WebGPUBackend::DestroySurface(GpuSurface *surface)
{
    WGSurface *s = static_cast<WGSurface *>(surface);
    if (!s) return;
    if (m_attSurface == s || m_attDepthSurface == s)
    {
        EndPass();
        m_attSurface = m_attDepthSurface = nullptr;
        m_targetsDirty = true;
    }
    if (s->view) wgpuTextureViewRelease(s->view);
    if (s->texture) wgpuTextureRelease(s->texture);
    delete s;
}

bool WebGPUBackend::ReadSurface(GpuSurface *surface, const RECT &rc, uint8_t *rgbaOut)
{
    WGSurface *s = static_cast<WGSurface *>(surface);
    if (!s || !s->texture || m_lost || s->desc.depthStencil || s->samples != 1) return false;
    return ReadTextureRect(s->texture, s->format, 0, 0, rc, rgbaOut);
}

//------------------------------------------------------------------------------
// Samplers
//------------------------------------------------------------------------------
GpuSampler *WebGPUBackend::GetSampler(const SamplerDesc &d)
{
    std::string key(reinterpret_cast<const char *>(&d), sizeof d);
    auto it = m_samplers.find(key);
    if (it != m_samplers.end()) return it->second;

    WGSampler *s = new WGSampler();
    s->uid = m_nextUid++;
    WGPUSamplerDescriptor sd = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sd.addressModeU = MapAddress(d.addressU);
    sd.addressModeV = MapAddress(d.addressV);
    sd.addressModeW = MapAddress(d.addressW);
    const bool minLinear = d.minFilter != D3DTEXF_POINT && d.minFilter != D3DTEXF_NONE;
    const bool magLinear = d.magFilter != D3DTEXF_POINT && d.magFilter != D3DTEXF_NONE;
    sd.minFilter = minLinear ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
    sd.magFilter = magLinear ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
    if (d.mipped)
    {
        sd.mipmapFilter = d.mipFilter == D3DTEXF_POINT ? WGPUMipmapFilterMode_Nearest : WGPUMipmapFilterMode_Linear;
        sd.lodMinClamp = 0.0f;
        sd.lodMaxClamp = 32.0f;
    }
    else
    {
        // Only the base level, with the min/mag choice still made from the level of detail (a clamp to 0 would always
        // magnify): nearest mip selection never leaves level 0 below 0.5.
        sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        sd.lodMinClamp = 0.0f;
        sd.lodMaxClamp = 0.25f;
    }
    // Anisotropic filtering needs linear filtering everywhere in WebGPU.
    const bool allLinear = minLinear && magLinear && sd.mipmapFilter == WGPUMipmapFilterMode_Linear;
    sd.maxAnisotropy = allLinear ? (uint16_t)Max<uint8_t>(1, d.maxAnisotropy) : 1;
    s->sampler = m_device ? wgpuDeviceCreateSampler(m_device, &sd) : nullptr;
    m_samplers[key] = s;
    return s;
}

} // namespace dxwebgl2
