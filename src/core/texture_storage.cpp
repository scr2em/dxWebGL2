/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * GL texture storage with CPU shadow copies: allocation, level locking, upload and read-back.
 */
#include "core/texture_storage.h"
#include "core/gl_state.h"

namespace dxwebgl2 {

uint32_t NextResourceId()
{
    static uint32_t next = 1;
    return next++;
}

//------------------------------------------------------------------------------
// TextureBase
//------------------------------------------------------------------------------
TextureBase::TextureBase(GLState *dev, D3DRESOURCETYPE type, UINT w, UINT h, UINT d, UINT levels,
                         DWORD usage, D3DFORMAT fmt, D3DPOOL pool)
    : GLObject(dev), m_dev(dev), m_type(type), m_format(fmt), m_usage(usage), m_pool(pool),
      m_width(w), m_height(h), m_depth(d), m_uniqueId(NextResourceId())
{
    m_info = GetFormatInfo(fmt);
    if (!m_info || !m_info->texture || w == 0 || h == 0 || d == 0) return;
    if (type == D3DRTYPE_CUBETEXTURE) { m_faces = 6; m_target = GL_TEXTURE_CUBE_MAP; }
    else if (type == D3DRTYPE_VOLUMETEXTURE) { m_faces = 1; m_target = GL_TEXTURE_3D; }
    else { m_faces = 1; m_target = GL_TEXTURE_2D; }
    if (type == D3DRTYPE_CUBETEXTURE) m_height = m_width;

    const GLCaps &caps = dev->Caps();
    GLint maxSize = (type == D3DRTYPE_CUBETEXTURE) ? caps.maxCubeSize : (type == D3DRTYPE_VOLUMETEXTURE ? caps.max3DSize : caps.maxTextureSize);
    if ((GLint)m_width > maxSize || (GLint)m_height > maxSize || (GLint)m_depth > caps.max3DSize) return;

    uint32_t full = FullMipCount(m_width, m_height, m_depth);
    m_levelCount = (levels == 0 || levels > full) ? full : levels;
    m_isRT = (usage & D3DUSAGE_RENDERTARGET) != 0;
    if (m_isRT && !m_info->renderTarget) return;
    m_decode = m_info->conv == UploadConv::DXT && dev->IsDecodingDXT();

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
        m_valid = EnsureGL();
}

TextureBase::~TextureBase()
{
    ReleaseGL();
}

void TextureBase::OnContextRestored()
{
    // Textures that never had a GL object (system memory) stay without one. The others are created
    // again and refilled from the shadow copies; levels whose copy was released (DEFAULT pool, see
    // UnlockLevel) come back empty, exactly like Direct3D 8 resources in the default pool.
    const bool had = m_glAllocated;
    m_tex = 0;
    m_glAllocated = false;
    m_baseLevel = 0;
    if (!had) return;
    EnsureGL();
    for (LevelData &lv : m_levels)
        if (!lv.shadowValid && lv.glValid && !m_isRT)
        {
            DXW_HIT(Event, "texture contents lost with the context (default pool, game must reload)");
            break;
        }
}

void TextureBase::ReleaseGL()
{
    if (m_tex)
    {
        m_dev->ForgetTexture(m_tex);
        glDeleteTextures(1, &m_tex);
        m_tex = 0;
    }
}

GLenum TextureBase::FaceTarget(UINT face) const
{
    return m_target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : m_target;
}

bool TextureBase::EnsureGL()
{
    if (m_tex) return true;
    if (!m_info) return false;
    glGenTextures(1, &m_tex);
    m_dev->BindForUpload(m_target, m_tex);

    const GLenum internal = m_decode ? GL_RGBA8 : m_info->glInternal;
    const bool legacy = internal == GL_ALPHA || internal == GL_LUMINANCE || internal == GL_LUMINANCE_ALPHA;
    if (!legacy)
    {
        if (m_target == GL_TEXTURE_3D)
            glTexStorage3D(m_target, m_levelCount, internal, m_width, m_height, m_depth);
        else
            glTexStorage2D(m_target, m_levelCount, internal, m_width, m_height);
    }
    else
    {
        for (uint32_t f = 0; f < m_faces; ++f)
            for (uint32_t l = 0; l < m_levelCount; ++l)
                glTexImage2D(FaceTarget(f), l, internal, LevelWidth(l), LevelHeight(l), 0, m_info->glFormat, m_info->glType, nullptr);
    }
    glTexParameteri(m_target, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(m_target, GL_TEXTURE_MAX_LEVEL, (GLint)m_levelCount - 1);
    m_baseLevel = 0;
    m_glAllocated = true;

    // Contents written while there was no GL texture (system memory pool).
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
    if (!m_tex || m_target == GL_TEXTURE_3D || m_info->blockBytes)
    {
        lv.shadowValid = true;
        return false;
    }
    std::vector<uint8_t> rgba((size_t)lv.width * lv.height * 4);
    RECT rc = {0, 0, (LONG)lv.width, (LONG)lv.height};
    if (!m_dev->ReadTextureLevel(m_tex, m_target, FaceTarget(face), level, lv.width, lv.height, rc, rgba.data()))
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
    if (m_tex && !m_info->blockBytes &&
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
    if (!m_tex) return; // system memory textures are not on the GPU
    lv.glValid = true;

    m_dev->BindForUpload(m_target, m_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (m_target == GL_TEXTURE_3D)
    {
        const uint8_t *data = PrepareForGL(*m_info, lv.shadow.data(), lv.pitch, lv.width, lv.height * lv.depth, m_decode, m_scratch);
        const GLenum fmt = m_decode ? GL_RGBA : m_info->glFormat;
        const GLenum type = m_decode ? GL_UNSIGNED_BYTE : m_info->glType;
        if (m_info->blockBytes && !m_decode)
            glCompressedTexSubImage3D(m_target, level, 0, 0, 0, lv.width, lv.height, lv.depth, m_info->glInternal,
                                      (GLsizei)(lv.slicePitch * lv.depth), data);
        else
            glTexSubImage3D(m_target, level, 0, 0, 0, lv.width, lv.height, lv.depth, fmt, type, data);
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

    const uint8_t *data = PrepareForGL(*m_info, src, lv.pitch, w, h, m_decode, m_scratch);
    if (m_info->blockBytes && !m_decode)
    {
        uint32_t size = ((w + 3) / 4) * ((h + 3) / 4) * m_info->blockBytes;
        glCompressedTexSubImage2D(FaceTarget(face), level, rc.left, rc.top, w, h, m_info->glInternal, size, data);
    }
    else
    {
        const GLenum fmt = m_decode ? GL_RGBA : m_info->glFormat;
        const GLenum type = m_decode ? GL_UNSIGNED_BYTE : m_info->glType;
        glTexSubImage2D(FaceTarget(face), level, rc.left, rc.top, w, h, fmt, type, data);
    }
}

void TextureBase::UploadWhole(UINT face, UINT level)
{
    if (m_target == GL_TEXTURE_3D) { D3DBOX b = {}; UploadRegion(face, level, nullptr, &b); }
    else UploadRegion(face, level, nullptr, nullptr);
}

bool TextureBase::WriteRect(UINT face, UINT level, const RECT &rc, const void *src, UINT srcPitch)
{
    if (!m_valid || face >= m_faces || level >= m_levelCount || m_target == GL_TEXTURE_3D) return false;
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
    if (m_tex && !m_info->blockBytes && full &&
        ((m_pool == D3DPOOL_DEFAULT && !keepDefault) || (m_pool == D3DPOOL_MANAGED && GetConfig().releaseTextureShadows)))
    {
        std::vector<uint8_t>().swap(lv.shadow);
        lv.shadowValid = false;
    }
    return true;
}

bool TextureBase::ReadRect(UINT face, UINT level, const RECT &rc, void *dst, UINT dstPitch)
{
    if (!m_valid || face >= m_faces || level >= m_levelCount || m_target == GL_TEXTURE_3D) return false;
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

} // namespace dxwebgl2
