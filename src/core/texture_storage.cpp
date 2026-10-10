/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * GPU texture storage with CPU shadow copies: allocation, level locking, upload and read-back; standalone render
 * target and depth/stencil storage.
 */
#include "core/texture_storage.h"

namespace dxwebgl2 {

uint32_t NextResourceId()
{
    static uint32_t next = 1;
    return next++;
}

//------------------------------------------------------------------------------
// TextureBase
//------------------------------------------------------------------------------
TextureBase::TextureBase(Backend *backend, D3DRESOURCETYPE type, UINT w, UINT h, UINT d, UINT levels,
                         DWORD usage, D3DFORMAT fmt, D3DPOOL pool)
    : GpuObject(backend), m_backend(backend), m_type(type), m_format(fmt), m_usage(usage), m_pool(pool),
      m_width(w), m_height(h), m_depth(d), m_uniqueId(NextResourceId())
{
    m_info = GetFormatInfo(fmt);
    if (!m_info || !m_info->texture || w == 0 || h == 0 || d == 0) return;
    if (type == D3DRTYPE_CUBETEXTURE) { m_faces = 6; m_kind = TextureKind::Cube; }
    else if (type == D3DRTYPE_VOLUMETEXTURE) { m_faces = 1; m_kind = TextureKind::Volume; }
    else { m_faces = 1; m_kind = TextureKind::Tex2D; }
    if (type == D3DRTYPE_CUBETEXTURE) m_height = m_width;

    const GpuCaps &caps = backend->Caps();
    int maxSize = (type == D3DRTYPE_CUBETEXTURE) ? caps.maxCubeSize : (type == D3DRTYPE_VOLUMETEXTURE ? caps.max3DSize : caps.maxTextureSize);
    if ((int)m_width > maxSize || (int)m_height > maxSize || (int)m_depth > caps.max3DSize) return;

    uint32_t full = FullMipCount(m_width, m_height, m_depth);
    m_levelCount = (levels == 0 || levels > full) ? full : levels;
    m_isRT = (usage & D3DUSAGE_RENDERTARGET) != 0;
    if (m_isRT && !m_info->renderTarget) return;
    m_decode = m_info->conv == UploadConv::DXT && !caps.s3tc;

    m_levels.resize(m_faces * m_levelCount);
    for (uint32_t f = 0; f < m_faces; ++f)
        for (uint32_t l = 0; l < m_levelCount; ++l)
        {
            LevelData &lv = LevelAt(f, l);
            lv.width = LevelWidth(l);
            lv.height = LevelHeight(l);
            lv.depth = Max<uint32_t>(1, m_depth >> l);
            lv.pitch = FormatPitch(fmt, lv.width);
            lv.slicePitch = lv.pitch * FormatRows(fmt, lv.height);
            lv.glValid = m_isRT;
        }
    m_valid = true;
    if (pool != D3DPOOL_SYSTEMMEM && pool != D3DPOOL_SCRATCH)
        m_valid = EnsureGpu();
}

TextureBase::~TextureBase()
{
    ReleaseGpu();
}

void TextureBase::OnContextLost()
{
    // The backend object died with the context: free the handle (no GPU call). m_glAllocated stays: the texture had
    // GPU storage and gets it again on restore.
    if (m_gpu) m_backend->DestroyTexture(m_gpu);
    m_gpu = nullptr;
}

void TextureBase::OnContextRestored()
{
    // Textures that never had a GPU object (system memory) stay without one. The others are created
    // again and refilled from the shadow copies; levels whose copy was released (DEFAULT pool, see
    // UnlockLevel) come back empty, exactly like Direct3D 8 resources in the default pool.
    const bool had = m_glAllocated;
    if (m_gpu) m_backend->DestroyTexture(m_gpu); // created while the context was lost
    m_gpu = nullptr;
    m_glAllocated = false;
    if (!had) return;
    EnsureGpu();
    for (LevelData &lv : m_levels)
        if (!lv.shadowValid && lv.glValid && !m_isRT)
        {
            DXW_HIT(Event, "texture contents lost with the context (default pool, game must reload)");
            break;
        }
}

void TextureBase::ReleaseGpu()
{
    if (m_gpu)
    {
        m_backend->DestroyTexture(m_gpu);
        m_gpu = nullptr;
    }
}

bool TextureBase::EnsureGpu()
{
    if (m_gpu) return true;
    if (!m_info) return false;
    const TextureDesc desc = {m_kind, m_format, m_width, m_height, m_depth, m_levelCount, m_decode};
    m_gpu = m_backend->CreateTexture(desc);
    m_glAllocated = true;

    // Contents written while there was no GPU texture (system memory pool).
    for (uint32_t f = 0; f < m_faces; ++f)
        for (uint32_t l = 0; l < m_levelCount; ++l)
            if (LevelAt(f, l).shadowValid) UploadRegion(f, l, nullptr, nullptr);
    return true;
}

void TextureBase::AllocateShadow(LevelData &lv)
{
    if (lv.shadow.empty())
    {
        lv.shadow.assign((size_t)lv.slicePitch * lv.depth, 0);
        lv.shadowValid = false;
    }
}

bool TextureBase::ReadbackLevel(UINT face, UINT level)
{
    LevelData &lv = LevelAt(face, level);
    AllocateShadow(lv);
    if (!m_gpu || m_kind == TextureKind::Volume || m_info->blockBytes)
    {
        lv.shadowValid = true;
        return false;
    }
    std::vector<uint8_t> rgba((size_t)lv.width * lv.height * 4);
    RECT rc = {0, 0, (LONG)lv.width, (LONG)lv.height};
    if (!m_backend->ReadTexture(m_gpu, face, level, rc, rgba.data()))
    {
        lv.shadowValid = true;
        return false;
    }
    // RGBA bytes -> A8R8G8B8 (B,G,R,A) -> this format.
    for (size_t i = 0; i < (size_t)lv.width * lv.height; ++i)
        std::swap(rgba[i * 4], rgba[i * 4 + 2]);
    ConvertPixels(D3DFMT_A8R8G8B8, rgba.data(), lv.width * 4, m_format, lv.shadow.data(), lv.pitch, lv.width, lv.height);
    lv.shadowValid = true;
    return true;
}

HRESULT TextureBase::LockLevel(UINT face, UINT level, D3DLOCKED_RECT *lr, D3DLOCKED_BOX *lb,
                               const RECT *rect, const D3DBOX *box, DWORD flags)
{
    if (!m_valid || face >= m_faces || level >= m_levelCount) return D3DERR_INVALIDCALL;
    LevelData &lv = LevelAt(face, level);
    if (lv.locked) return D3DERR_INVALIDCALL;

    RECT rc = {0, 0, (LONG)lv.width, (LONG)lv.height};
    D3DBOX bx = {0, 0, lv.width, lv.height, 0, lv.depth};
    if (rect)
    {
        if (rect->left < 0 || rect->top < 0 || rect->right > (LONG)lv.width || rect->bottom > (LONG)lv.height ||
            rect->left >= rect->right || rect->top >= rect->bottom)
            return D3DERR_INVALIDCALL;
        rc = *rect;
    }
    if (box)
    {
        if (box->Right > lv.width || box->Bottom > lv.height || box->Back > lv.depth ||
            box->Left >= box->Right || box->Top >= box->Bottom || box->Front >= box->Back)
            return D3DERR_INVALIDCALL;
        bx = *box;
    }
    if (m_info->blockBytes) { rc.left &= ~3; rc.top &= ~3; }

    AllocateShadow(lv);
    if (!lv.shadowValid)
    {
        if (lv.glValid && !(flags & D3DLOCK_DISCARD)) ReadbackLevel(face, level);
        lv.shadowValid = true;
    }

    uint8_t *base = lv.shadow.data();
    uint32_t bpp = m_info->blockBytes ? m_info->blockBytes : m_info->bits / 8;
    if (m_type == D3DRTYPE_VOLUMETEXTURE)
    {
        if (!lb) return D3DERR_INVALIDCALL;
        lb->RowPitch = lv.pitch;
        lb->SlicePitch = lv.slicePitch;
        lb->pBits = base + (size_t)bx.Front * lv.slicePitch + (size_t)bx.Top * lv.pitch + (size_t)bx.Left * bpp;
        lv.lockBox = bx;
    }
    else
    {
        if (!lr) return D3DERR_INVALIDCALL;
        size_t rowOffset = m_info->blockBytes ? (size_t)(rc.top / 4) * lv.pitch : (size_t)rc.top * lv.pitch;
        size_t colOffset = m_info->blockBytes ? (size_t)(rc.left / 4) * bpp : (size_t)rc.left * bpp;
        lr->Pitch = lv.pitch;
        lr->pBits = base + rowOffset + colOffset;
        lv.lockRect = rc;
    }
    lv.locked = true;
    lv.lockFlags = flags;
    return D3D_OK;
}

HRESULT TextureBase::UnlockLevel(UINT face, UINT level)
{
    if (!m_valid || face >= m_faces || level >= m_levelCount) return D3DERR_INVALIDCALL;
    LevelData &lv = LevelAt(face, level);
    if (!lv.locked) return D3DERR_INVALIDCALL;
    lv.locked = false;
    if (lv.lockFlags & D3DLOCK_READONLY) return D3D_OK;

    if (m_type == D3DRTYPE_VOLUMETEXTURE)
        UploadRegion(face, level, nullptr, &lv.lockBox);
    else
        UploadRegion(face, level, &lv.lockRect, nullptr);

    // DEFAULT pool contents live on the GPU only; managed ones optionally too.
    const bool full = lv.lockRect.left == 0 && lv.lockRect.top == 0 &&
                      (uint32_t)lv.lockRect.right == lv.width && (uint32_t)lv.lockRect.bottom == lv.height;
    const bool keepDefault = (m_usage & (D3DUSAGE_DYNAMIC | D3DUSAGE_RENDERTARGET)) != 0;
    if (m_gpu && !m_info->blockBytes &&
        ((m_pool == D3DPOOL_DEFAULT && !keepDefault) || (m_pool == D3DPOOL_MANAGED && GetConfig().releaseTextureShadows)) &&
        (full || m_type == D3DRTYPE_VOLUMETEXTURE))
    {
        std::vector<uint8_t>().swap(lv.shadow);
        lv.shadowValid = false;
    }
    return D3D_OK;
}

void TextureBase::UploadRegion(UINT face, UINT level, const RECT *rcIn, const D3DBOX *)
{
    LevelData &lv = LevelAt(face, level);
    lv.shadowValid = true;
    if (!m_gpu) return; // system memory textures are not on the GPU
    lv.glValid = true;

    if (m_kind == TextureKind::Volume)
    {
        // Volumes are uploaded level by level.
        const TextureRegion region = {face, level, 0, 0, lv.width, lv.height, lv.depth};
        m_backend->WriteTexture(m_gpu, region, lv.shadow.data(), lv.pitch);
        return;
    }

    RECT rc = {0, 0, (LONG)lv.width, (LONG)lv.height};
    if (rcIn) rc = *rcIn;
    uint32_t w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (m_info->blockBytes)
    {
        // Block aligned region.
        rc.left &= ~3; rc.top &= ~3;
        rc.right = Min<LONG>((rc.right + 3) & ~3, (LONG)lv.width);
        rc.bottom = Min<LONG>((rc.bottom + 3) & ~3, (LONG)lv.height);
        w = rc.right - rc.left; h = rc.bottom - rc.top;
    }

    const uint8_t *src;
    if (m_info->blockBytes)
        src = lv.shadow.data() + (size_t)(rc.top / 4) * lv.pitch + (size_t)(rc.left / 4) * m_info->blockBytes;
    else
        src = lv.shadow.data() + (size_t)rc.top * lv.pitch + (size_t)rc.left * (m_info->bits / 8);
    const TextureRegion region = {face, level, (uint32_t)rc.left, (uint32_t)rc.top, w, h, 1};
    m_backend->WriteTexture(m_gpu, region, src, lv.pitch);
}

void TextureBase::UploadWhole(UINT face, UINT level)
{
    if (m_kind == TextureKind::Volume) { D3DBOX b = {}; UploadRegion(face, level, nullptr, &b); }
    else UploadRegion(face, level, nullptr, nullptr);
}

bool TextureBase::WriteRect(UINT face, UINT level, const RECT &rc, const void *src, UINT srcPitch)
{
    if (!m_valid || face >= m_faces || level >= m_levelCount || m_kind == TextureKind::Volume) return false;
    LevelData &lv = LevelAt(face, level);
    const bool full = rc.left == 0 && rc.top == 0 && (uint32_t)rc.right == lv.width && (uint32_t)rc.bottom == lv.height;
    AllocateShadow(lv);
    if (!lv.shadowValid)
    {
        if (lv.glValid && !full) ReadbackLevel(face, level);
        lv.shadowValid = true;
    }
    RECT r = rc;
    if (m_info->blockBytes) { r.left &= ~3; r.top &= ~3; }
    uint32_t bpp = m_info->blockBytes ? m_info->blockBytes : m_info->bits / 8;
    uint32_t rowBytes, rows;
    uint8_t *dst;
    if (m_info->blockBytes)
    {
        rowBytes = (((rc.right - r.left) + 3) / 4) * bpp;
        rows = ((rc.bottom - r.top) + 3) / 4;
        dst = lv.shadow.data() + (size_t)(r.top / 4) * lv.pitch + (size_t)(r.left / 4) * bpp;
    }
    else
    {
        rowBytes = (rc.right - rc.left) * bpp;
        rows = rc.bottom - rc.top;
        dst = lv.shadow.data() + (size_t)rc.top * lv.pitch + (size_t)rc.left * bpp;
    }
    const uint8_t *s = static_cast<const uint8_t *>(src);
    for (uint32_t y = 0; y < rows; ++y) memcpy(dst + (size_t)y * lv.pitch, s + (size_t)y * srcPitch, rowBytes);
    UploadRegion(face, level, &r, nullptr);

    const bool keepDefault = (m_usage & (D3DUSAGE_DYNAMIC | D3DUSAGE_RENDERTARGET)) != 0;
    if (m_gpu && !m_info->blockBytes && full &&
        ((m_pool == D3DPOOL_DEFAULT && !keepDefault) || (m_pool == D3DPOOL_MANAGED && GetConfig().releaseTextureShadows)))
    {
        std::vector<uint8_t>().swap(lv.shadow);
        lv.shadowValid = false;
    }
    return true;
}

bool TextureBase::ReadRect(UINT face, UINT level, const RECT &rc, void *dst, UINT dstPitch)
{
    if (!m_valid || face >= m_faces || level >= m_levelCount || m_kind == TextureKind::Volume) return false;
    LevelData &lv = LevelAt(face, level);
    AllocateShadow(lv);
    if (!lv.shadowValid)
    {
        if (lv.glValid) ReadbackLevel(face, level);
        lv.shadowValid = true;
    }
    uint32_t bpp = m_info->blockBytes ? m_info->blockBytes : m_info->bits / 8;
    uint32_t rowBytes, rows;
    const uint8_t *s;
    if (m_info->blockBytes)
    {
        rowBytes = (((rc.right - (rc.left & ~3)) + 3) / 4) * bpp;
        rows = ((rc.bottom - (rc.top & ~3)) + 3) / 4;
        s = lv.shadow.data() + (size_t)((rc.top & ~3) / 4) * lv.pitch + (size_t)((rc.left & ~3) / 4) * bpp;
    }
    else
    {
        rowBytes = (rc.right - rc.left) * bpp;
        rows = rc.bottom - rc.top;
        s = lv.shadow.data() + (size_t)rc.top * lv.pitch + (size_t)rc.left * bpp;
    }
    uint8_t *d = static_cast<uint8_t *>(dst);
    for (uint32_t y = 0; y < rows; ++y) memcpy(d + (size_t)y * dstPitch, s + (size_t)y * lv.pitch, rowBytes);
    return true;
}

//------------------------------------------------------------------------------
// SurfaceStorage
//------------------------------------------------------------------------------
SurfaceStorage::SurfaceStorage(Backend *backend, const SurfaceDesc &desc) : GpuObject(backend), m_backend(backend), m_desc(desc)
{
    m_gpu = m_backend->CreateSurface(m_desc);
}

SurfaceStorage::~SurfaceStorage()
{
    if (m_gpu) m_backend->DestroySurface(m_gpu);
}

void SurfaceStorage::OnContextLost()
{
    if (m_gpu) m_backend->DestroySurface(m_gpu); // no GPU call: the object died with the context
    m_gpu = nullptr;
}

void SurfaceStorage::OnContextRestored()
{
    if (m_gpu) m_backend->DestroySurface(m_gpu);
    m_gpu = m_backend->CreateSurface(m_desc);
}

} // namespace dxwebgl2
