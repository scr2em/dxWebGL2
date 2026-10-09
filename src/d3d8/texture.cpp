/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Textures, surfaces and volumes.
 *
 * Every texture level keeps a system-memory "shadow" copy in the D3D memory
 * layout. LockRect hands out pointers into it and UnlockRect uploads the
 * modified region to GL, converting to a GL-compatible layout (see
 * PrepareForGL). Render targets and DEFAULT pool textures only materialize a
 * shadow when they are locked, by reading the GL texture back.
 */
#include "d3d8/resources.h"

namespace dxwebgl2 {

//------------------------------------------------------------------------------
// DeviceChild
//------------------------------------------------------------------------------
DeviceChild::DeviceChild(Device *dev, bool takeRef) : m_device(dev), m_holdsRef(takeRef)
{
    if (takeRef && dev) dev->AddRef();
}

DeviceChild::~DeviceChild()
{
    if (m_holdsRef && m_device) m_device->Release();
}

void DeviceChild::DropDeviceRef()
{
    if (m_holdsRef && m_device) m_device->Release();
    m_holdsRef = false;
}

HRESULT DeviceChild::GetDeviceCommon(IDirect3DDevice8 **ppDevice)
{
    if (!ppDevice) return D3DERR_INVALIDCALL;
    *ppDevice = m_device;
    m_device->AddRef();
    return D3D_OK;
}

static std::string GuidKey(REFGUID g) { return std::string(reinterpret_cast<const char *>(&g), sizeof(GUID)); }

HRESULT DeviceChild::SetPrivateDataCommon(REFGUID guid, const void *data, DWORD size, DWORD)
{
    PrivateData &pd = m_private[GuidKey(guid)];
    pd.bytes.assign(static_cast<const uint8_t *>(data), static_cast<const uint8_t *>(data) + size);
    return D3D_OK;
}

HRESULT DeviceChild::GetPrivateDataCommon(REFGUID guid, void *data, DWORD *size)
{
    auto it = m_private.find(GuidKey(guid));
    if (it == m_private.end() || !size) return D3DERR_NOTFOUND;
    if (!data || *size < it->second.bytes.size())
    {
        *size = (DWORD)it->second.bytes.size();
        return D3DERR_MOREDATA;
    }
    memcpy(data, it->second.bytes.data(), it->second.bytes.size());
    *size = (DWORD)it->second.bytes.size();
    return D3D_OK;
}

HRESULT DeviceChild::FreePrivateDataCommon(REFGUID guid)
{
    return m_private.erase(GuidKey(guid)) ? D3D_OK : D3DERR_NOTFOUND;
}

//------------------------------------------------------------------------------
// Texture2D
//------------------------------------------------------------------------------
Texture2D::Texture2D(Device *dev, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool)
    : ResourceImpl<IDirect3DTexture8>(dev),
      TextureBase(dev, D3DRTYPE_TEXTURE, w, h, 1, levels, usage, fmt, pool)
{
    m_surfaces.assign(m_levelCount ? m_levelCount : 0, nullptr);
}

Texture2D::~Texture2D()
{
    for (Surface *s : m_surfaces) delete s;
}

Surface *Texture2D::LevelSurface(UINT level)
{
    if (level >= m_levelCount) return nullptr;
    if (!m_surfaces[level]) m_surfaces[level] = new Surface(this, static_cast<IDirect3DTexture8 *>(this), 0, level);
    return m_surfaces[level];
}

HRESULT Texture2D::GetLevelDesc(UINT Level, D3DSURFACE_DESC *d)
{
    if (!d || Level >= m_levelCount) return D3DERR_INVALIDCALL;
    d->Format = m_format;
    d->Type = D3DRTYPE_SURFACE;
    d->Usage = m_usage;
    d->Pool = m_pool;
    d->Size = LevelAt(0, Level).slicePitch;
    d->MultiSampleType = D3DMULTISAMPLE_NONE;
    d->Width = LevelWidth(Level);
    d->Height = LevelHeight(Level);
    return D3D_OK;
}

HRESULT Texture2D::GetSurfaceLevel(UINT level, IDirect3DSurface8 **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    Surface *s = LevelSurface(level);
    if (!s) return D3DERR_INVALIDCALL;
    s->AddRef();
    *pp = s;
    return D3D_OK;
}

HRESULT Texture2D::LockRect(UINT Level, D3DLOCKED_RECT *lr, const RECT *rect, DWORD flags)
{
    return LockLevel(0, Level, lr, nullptr, rect, nullptr, flags);
}

HRESULT Texture2D::UnlockRect(UINT Level) { return UnlockLevel(0, Level); }

//------------------------------------------------------------------------------
// CubeTexture
//------------------------------------------------------------------------------
CubeTexture::CubeTexture(Device *dev, UINT edge, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool)
    : ResourceImpl<IDirect3DCubeTexture8>(dev),
      TextureBase(dev, D3DRTYPE_CUBETEXTURE, edge, edge, 1, levels, usage, fmt, pool)
{
    m_surfaces.assign(6 * m_levelCount, nullptr);
}

CubeTexture::~CubeTexture()
{
    for (Surface *s : m_surfaces) delete s;
}

Surface *CubeTexture::LevelSurface(UINT face, UINT level)
{
    if (face >= 6 || level >= m_levelCount) return nullptr;
    Surface *&s = m_surfaces[face * m_levelCount + level];
    if (!s) s = new Surface(this, static_cast<IDirect3DCubeTexture8 *>(this), face, level);
    return s;
}

HRESULT CubeTexture::GetLevelDesc(UINT Level, D3DSURFACE_DESC *d)
{
    if (!d || Level >= m_levelCount) return D3DERR_INVALIDCALL;
    d->Format = m_format;
    d->Type = D3DRTYPE_SURFACE;
    d->Usage = m_usage;
    d->Pool = m_pool;
    d->Size = LevelAt(0, Level).slicePitch;
    d->MultiSampleType = D3DMULTISAMPLE_NONE;
    d->Width = LevelWidth(Level);
    d->Height = LevelHeight(Level);
    return D3D_OK;
}

HRESULT CubeTexture::GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface8 **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    Surface *s = LevelSurface((UINT)face, level);
    if (!s) return D3DERR_INVALIDCALL;
    s->AddRef();
    *pp = s;
    return D3D_OK;
}

HRESULT CubeTexture::LockRect(D3DCUBEMAP_FACES face, UINT Level, D3DLOCKED_RECT *lr, const RECT *rect, DWORD flags)
{
    return LockLevel((UINT)face, Level, lr, nullptr, rect, nullptr, flags);
}

HRESULT CubeTexture::UnlockRect(D3DCUBEMAP_FACES face, UINT Level) { return UnlockLevel((UINT)face, Level); }

//------------------------------------------------------------------------------
// VolumeTexture / Volume
//------------------------------------------------------------------------------
VolumeTexture::VolumeTexture(Device *dev, UINT w, UINT h, UINT d, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool)
    : ResourceImpl<IDirect3DVolumeTexture8>(dev),
      TextureBase(dev, D3DRTYPE_VOLUMETEXTURE, w, h, d, levels, usage, fmt, pool)
{
    m_volumes.assign(m_levelCount, nullptr);
}

VolumeTexture::~VolumeTexture()
{
    for (Volume *v : m_volumes) delete v;
}

HRESULT VolumeTexture::GetLevelDesc(UINT Level, D3DVOLUME_DESC *d)
{
    if (!d || Level >= m_levelCount) return D3DERR_INVALIDCALL;
    LevelData &lv = LevelAt(0, Level);
    d->Format = m_format;
    d->Type = D3DRTYPE_VOLUME;
    d->Usage = m_usage;
    d->Pool = m_pool;
    d->Size = lv.slicePitch * lv.depth;
    d->Width = lv.width;
    d->Height = lv.height;
    d->Depth = lv.depth;
    return D3D_OK;
}

HRESULT VolumeTexture::GetVolumeLevel(UINT Level, IDirect3DVolume8 **pp)
{
    if (!pp || Level >= m_levelCount) return D3DERR_INVALIDCALL;
    if (!m_volumes[Level]) m_volumes[Level] = new Volume(this, Level);
    m_volumes[Level]->AddRef();
    *pp = m_volumes[Level];
    return D3D_OK;
}

HRESULT VolumeTexture::LockBox(UINT Level, D3DLOCKED_BOX *lb, const D3DBOX *box, DWORD flags)
{
    return LockLevel(0, Level, nullptr, lb, nullptr, box, flags);
}

HRESULT VolumeTexture::UnlockBox(UINT Level) { return UnlockLevel(0, Level); }

Volume::Volume(VolumeTexture *parent, UINT level) : m_parent(parent), m_level(level) { m_refs = 0; }

ULONG Volume::AddRef()
{
    m_parent->AddRef();
    return ++m_refs;
}

ULONG Volume::Release()
{
    VolumeTexture *p = m_parent;
    ULONG r = --m_refs;
    p->Release();
    return r;
}

HRESULT Volume::GetDevice(IDirect3DDevice8 **pp)
{
    return m_parent->GetDevice(pp);
}

HRESULT Volume::GetContainer(REFIID, void **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    *pp = static_cast<IDirect3DVolumeTexture8 *>(m_parent);
    m_parent->AddRef();
    return D3D_OK;
}

HRESULT Volume::GetDesc(D3DVOLUME_DESC *d) { return m_parent->GetLevelDesc(m_level, d); }
HRESULT Volume::LockBox(D3DLOCKED_BOX *lb, const D3DBOX *box, DWORD f) { return m_parent->LockBox(m_level, lb, box, f); }
HRESULT Volume::UnlockBox() { return m_parent->UnlockBox(m_level); }

//------------------------------------------------------------------------------
// Surface
//------------------------------------------------------------------------------
Surface::Surface(TextureBase *tex, IUnknown *owner, UINT face, UINT level)
    : DeviceChild(static_cast<Device *>(tex->m_dev), false), GLObject(nullptr), m_kind(Level), m_format(tex->m_format),
      m_width(tex->LevelWidth(level)), m_height(tex->LevelHeight(level)),
      m_pool(tex->m_pool), m_usage(tex->m_usage), m_tex(tex), m_owner(owner),
      m_face(face), m_level(level), m_id(NextResourceId())
{
    m_refs = 0;
}

Surface::Surface(Device *dev, Kind kind, UINT w, UINT h, D3DFORMAT fmt, int samples)
    : DeviceChild(dev, true), GLObject(dev), m_kind(kind), m_format(fmt),
      m_width(w), m_height(h), m_id(NextResourceId())
{
    m_samples = samples >= 2 ? Min<int>(samples, dev->Caps().maxSamples) : 0;
    if (m_samples < 2) m_samples = 0;
    const FormatInfo *info = GetFormatInfo(fmt);
    if (kind == Image)
    {
        m_pool = D3DPOOL_SYSTEMMEM;
        m_data.assign((size_t)FormatPitch(fmt, w) * FormatRows(fmt, h), 0);
    }
    else if (kind == RenderTarget)
    {
        m_usage = D3DUSAGE_RENDERTARGET;
        glGenRenderbuffers(1, &m_rb);
        glBindRenderbuffer(GL_RENDERBUFFER, m_rb);
        GLenum internal = info && info->glInternal && info->renderTarget ? info->glInternal : GL_RGBA8;
        if (m_samples) glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples, internal, w, h);
        else glRenderbufferStorage(GL_RENDERBUFFER, internal, w, h);
    }
    else if (kind == DepthStencil)
    {
        m_usage = D3DUSAGE_DEPTHSTENCIL;
        glGenRenderbuffers(1, &m_rb);
        glBindRenderbuffer(GL_RENDERBUFFER, m_rb);
        if (m_samples) glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples, DepthRenderbufferFormat(fmt), w, h);
        else glRenderbufferStorage(GL_RENDERBUFFER, DepthRenderbufferFormat(fmt), w, h);
    }
    else if (kind == BackBuffer)
    {
        m_usage = D3DUSAGE_RENDERTARGET;
    }
}

Surface::~Surface()
{
    if (m_rb) glDeleteRenderbuffers(1, &m_rb);
}

int Surface::Samples() const
{
    if (m_kind == BackBuffer) return m_device->BackBufferSamples();
    return m_samples;
}

void Surface::OnContextRestored()
{
    if (m_kind != RenderTarget && m_kind != DepthStencil) return;
    const FormatInfo *info = GetFormatInfo(m_format);
    glGenRenderbuffers(1, &m_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, m_rb);
    GLenum internal = m_kind == DepthStencil ? DepthRenderbufferFormat(m_format)
                                             : (info && info->glInternal && info->renderTarget ? info->glInternal : GL_RGBA8);
    if (m_samples) glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples, internal, m_width, m_height);
    else glRenderbufferStorage(GL_RENDERBUFFER, internal, m_width, m_height);
}

ULONG Surface::AddRef()
{
    if (m_owner) m_owner->AddRef();
    return ++m_refs;
}

ULONG Surface::Release()
{
    IUnknown *owner = m_owner;
    ULONG r = m_refs ? --m_refs : 0;
    if (owner)
        owner->Release(); // may destroy this surface
    else if (r == 0)
        OnZeroRefs();
    return r;
}

void Surface::OnZeroRefs() { delete this; }

HRESULT Surface::GetContainer(REFIID, void **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    if (!m_owner) { *pp = nullptr; return D3DERR_INVALIDCALL; }
    m_owner->AddRef();
    *pp = m_owner;
    return D3D_OK;
}

HRESULT Surface::GetDesc(D3DSURFACE_DESC *d)
{
    if (!d) return D3DERR_INVALIDCALL;
    d->Format = m_format;
    d->Type = D3DRTYPE_SURFACE;
    d->Usage = m_usage;
    d->Pool = m_pool;
    d->Size = FormatLevelSize(m_format, m_width, m_height);
    d->MultiSampleType = (D3DMULTISAMPLE_TYPE)Samples();
    d->Width = m_width;
    d->Height = m_height;
    return D3D_OK;
}

HRESULT Surface::LockRect(D3DLOCKED_RECT *lr, const RECT *rect, DWORD flags)
{
    if (!lr) return D3DERR_INVALIDCALL;
    if (m_kind == Level)
        return m_tex->LockLevel(m_face, m_level, lr, nullptr, rect, nullptr, flags);
    if (m_locked) return D3DERR_INVALIDCALL;

    RECT rc = {0, 0, (LONG)m_width, (LONG)m_height};
    if (rect)
    {
        if (rect->left < 0 || rect->top < 0 || rect->right > (LONG)m_width || rect->bottom > (LONG)m_height ||
            rect->left >= rect->right || rect->top >= rect->bottom)
            return D3DERR_INVALIDCALL;
        rc = *rect;
    }
    const FormatInfo *info = GetFormatInfo(m_format);
    if (!info) return D3DERR_INVALIDCALL;

    if (m_kind == Image)
    {
        uint32_t pitch = FormatPitch(m_format, m_width);
        size_t off = info->blockBytes ? (size_t)(rc.top / 4) * pitch + (size_t)(rc.left / 4) * info->blockBytes
                                      : (size_t)rc.top * pitch + (size_t)rc.left * (info->bits / 8);
        lr->Pitch = pitch;
        lr->pBits = m_data.data() + off;
    }
    else if (m_kind == RenderTarget || m_kind == BackBuffer)
    {
        // Read the GPU contents back into a temporary in the surface format.
        uint32_t pitch = FormatPitch(m_format, m_width);
        m_lockTemp.assign((size_t)pitch * m_height, 0);
        RECT full = {0, 0, (LONG)m_width, (LONG)m_height};
        ReadPixelsTo(full, m_format, m_lockTemp.data(), pitch);
        lr->Pitch = pitch;
        lr->pBits = m_lockTemp.data() + (size_t)rc.top * pitch + (size_t)rc.left * (info->bits / 8);
    }
    else
        return D3DERR_INVALIDCALL;
    m_locked = true;
    m_lockFlags = flags;
    m_lockRect = rc;
    return D3D_OK;
}

HRESULT Surface::UnlockRect()
{
    if (m_kind == Level) return m_tex->UnlockLevel(m_face, m_level);
    if (!m_locked) return D3DERR_INVALIDCALL;
    m_locked = false;
    if ((m_kind == RenderTarget || m_kind == BackBuffer) && !(m_lockFlags & D3DLOCK_READONLY))
        DXWEBGL2_UNSUPPORTED("writing to a render target surface through LockRect");
    if (m_kind == RenderTarget || m_kind == BackBuffer) std::vector<uint8_t>().swap(m_lockTemp);
    return D3D_OK;
}

uint64_t Surface::AttachColor(uint64_t cachedKey)
{
    uint64_t key = 0;
    switch (m_kind)
    {
    case Level:
        m_tex->EnsureGL();
        key = (1ull << 60) | ((uint64_t)m_tex->m_tex << 24) | ((uint64_t)m_face << 16) | (uint64_t)m_level;
        if (key != cachedKey)
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_tex->FaceTarget(m_face), m_tex->m_tex, m_level);
        break;
    case RenderTarget:
        key = (2ull << 60) | m_rb;
        if (key != cachedKey) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_rb);
        break;
    case BackBuffer:
        if (m_device->BackBufferSamples())
        {
            key = (3ull << 60) | m_device->BackBufferRenderbuffer();
            if (key != cachedKey) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_device->BackBufferRenderbuffer());
        }
        else
        {
            key = (4ull << 60) | m_device->BackBufferTexture();
            if (key != cachedKey) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_device->BackBufferTexture(), 0);
        }
        break;
    default: break;
    }
    return key;
}

uint64_t Surface::AttachDepth(bool &hasStencil, uint64_t cachedKey)
{
    const FormatInfo *info = GetFormatInfo(m_format);
    hasStencil = info && info->stencil;
    if (m_kind != DepthStencil) return cachedKey;
    // The renderbuffer is DEPTH24_STENCIL8 for stencil formats and a pure
    // depth format otherwise; attach to the matching point and detach the other.
    const uint64_t key = ((hasStencil ? 5ull : 6ull) << 60) | m_rb;
    if (key == cachedKey) return key;
    if (hasStencil)
    {
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_rb);
    }
    else
    {
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_rb);
    }
    return key;
}

bool Surface::ReadPixelsTo(const RECT &rc, D3DFORMAT dstFormat, void *dst, UINT dstPitch)
{
    const FormatInfo *info = GetFormatInfo(m_format);
    if (!info) return false;
    const uint32_t w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w == 0 || h == 0) return true;

    if (m_kind == Image)
    {
        uint32_t pitch = FormatPitch(m_format, m_width);
        const uint8_t *src = info->blockBytes
            ? m_data.data() + (size_t)(rc.top / 4) * pitch + (size_t)(rc.left / 4) * info->blockBytes
            : m_data.data() + (size_t)rc.top * pitch + (size_t)rc.left * (info->bits / 8);
        return ConvertPixels(m_format, src, pitch, dstFormat, dst, dstPitch, w, h);
    }

    if (m_kind == Level)
    {
        // Texture level: use the shadow copy (reading back from GL if needed).
        if (dstFormat == m_format)
            return m_tex->ReadRect(m_face, m_level, rc, dst, dstPitch);
        std::vector<uint8_t> tmp;
        uint32_t pitch = info->blockBytes ? ((w + 3) / 4) * info->blockBytes : w * (info->bits / 8);
        tmp.resize((size_t)pitch * (info->blockBytes ? (h + 3) / 4 : h));
        if (!m_tex->ReadRect(m_face, m_level, rc, tmp.data(), pitch)) return false;
        return ConvertPixels(m_format, tmp.data(), pitch, dstFormat, dst, dstPitch, w, h);
    }

    if (m_kind == RenderTarget || m_kind == BackBuffer)
    {
        std::vector<uint8_t> rgba((size_t)w * h * 4);
        bool ok = (m_kind == BackBuffer)
            ? m_device->ReadTextureLevel(m_device->BackBufferTexture(), GL_TEXTURE_2D, GL_TEXTURE_2D, 0, m_width, m_height, rc, rgba.data())
            : m_device->ReadRenderbuffer(m_rb, m_width, m_height, rc, rgba.data());
        if (!ok) return false;
        for (size_t i = 0; i < (size_t)w * h; ++i) std::swap(rgba[i * 4], rgba[i * 4 + 2]); // -> BGRA
        if (m_kind == BackBuffer || !info->hasAlpha)
            for (size_t i = 0; i < (size_t)w * h; ++i) if (!info->hasAlpha) rgba[i * 4 + 3] = 255;
        return ConvertPixels(D3DFMT_A8R8G8B8, rgba.data(), w * 4, dstFormat, dst, dstPitch, w, h);
    }
    return false;
}

bool Surface::WritePixelsFrom(const RECT &rc, D3DFORMAT srcFormat, const void *src, UINT srcPitch)
{
    const FormatInfo *info = GetFormatInfo(m_format);
    if (!info) return false;
    const uint32_t w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w == 0 || h == 0) return true;

    if (m_kind == Image)
    {
        uint32_t pitch = FormatPitch(m_format, m_width);
        uint8_t *dst = info->blockBytes
            ? m_data.data() + (size_t)(rc.top / 4) * pitch + (size_t)(rc.left / 4) * info->blockBytes
            : m_data.data() + (size_t)rc.top * pitch + (size_t)rc.left * (info->bits / 8);
        return ConvertPixels(srcFormat, src, srcPitch, m_format, dst, pitch, w, h);
    }
    if (m_kind == Level)
    {
        if (srcFormat == m_format)
            return m_tex->WriteRect(m_face, m_level, rc, src, srcPitch);
        uint32_t pitch = info->blockBytes ? ((w + 3) / 4) * info->blockBytes : w * (info->bits / 8);
        std::vector<uint8_t> tmp((size_t)pitch * (info->blockBytes ? (h + 3) / 4 : h));
        if (!ConvertPixels(srcFormat, src, srcPitch, m_format, tmp.data(), pitch, w, h)) return false;
        return m_tex->WriteRect(m_face, m_level, rc, tmp.data(), pitch);
    }
    DXWEBGL2_UNSUPPORTED("copying pixels into a render target surface");
    return false;
}

} // namespace dxwebgl2
