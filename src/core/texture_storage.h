/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * GPU texture storage with CPU shadow copies, shared by 2D, cube and volume textures, and the storage of standalone
 * render targets and depth/stencil buffers.
 */
#pragma once

#include "core/backend.h"

namespace dxwebgl2 {

/// Process-wide unique id for textures and surfaces (render target attachments are tracked by id).
uint32_t NextResourceId();

struct LevelData
{
    uint32_t width = 0, height = 0, depth = 1;
    uint32_t pitch = 0;      // bytes per row (or block row)
    uint32_t slicePitch = 0; // bytes per slice
    std::vector<uint8_t> shadow;
    bool shadowValid = false; ///< shadow holds the current contents
    bool glValid = false;     ///< something was uploaded to the GPU (or rendered into it)
    bool locked = false;
    DWORD lockFlags = 0;
    RECT lockRect = {};
    D3DBOX lockBox = {};
};

/// Storage common to 2D, cube and volume textures.
class TextureBase : public GpuObject
{
public:
    TextureBase(Backend *backend, D3DRESOURCETYPE type, UINT w, UINT h, UINT d, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool);
    virtual ~TextureBase();
    void OnContextLost() override;
    void OnContextRestored() override;

    bool Valid() const { return m_valid; }
    /// Creates the GPU texture when there is none (system memory textures get one when they are first drawn with).
    bool EnsureGpu();
    /// The GPU texture (null for system memory textures and while the context is lost).
    GpuTexture *Gpu() const { return m_gpu; }
    TextureKind Kind() const { return m_kind; }

    HRESULT LockLevel(UINT face, UINT level, D3DLOCKED_RECT *lr, D3DLOCKED_BOX *lb, const RECT *rect, const D3DBOX *box, DWORD flags);
    HRESULT UnlockLevel(UINT face, UINT level);
    /// Writes pixels already in this texture's format into a level (used by CopyRects etc.).
    bool WriteRect(UINT face, UINT level, const RECT &rc, const void *src, UINT srcPitch);
    /// Reads pixels in this texture's own format.
    bool ReadRect(UINT face, UINT level, const RECT &rc, void *dst, UINT dstPitch);
    void UploadWhole(UINT face, UINT level);
    LevelData &LevelAt(UINT face, UINT level) { return m_levels[face * m_levelCount + level]; }
    uint32_t LevelWidth(UINT level) const { return Max<uint32_t>(1, m_width >> level); }
    uint32_t LevelHeight(UINT level) const { return Max<uint32_t>(1, m_height >> level); }

    Backend *m_backend;
    D3DRESOURCETYPE m_type;
    D3DFORMAT m_format;
    DWORD m_usage;
    D3DPOOL m_pool;
    uint32_t m_width, m_height, m_depth, m_levelCount = 0, m_faces = 1;
    TextureKind m_kind = TextureKind::Tex2D;
    GpuTexture *m_gpu = nullptr;
    const FormatInfo *m_info = nullptr;
    bool m_decode = false;       ///< DXT decoded to RGBA8 on upload
    bool m_isRT = false;
    DWORD m_lod = 0;
    uint32_t m_uniqueId;
    std::vector<LevelData> m_levels;

protected:
    bool m_valid = false;
    void UploadRegion(UINT face, UINT level, const RECT *rc, const D3DBOX *box);
    bool ReadbackLevel(UINT face, UINT level);
    void AllocateShadow(LevelData &lv);
    void ReleaseGpu();
    bool m_glAllocated = false;   ///< the texture has (had) GPU storage
};

/// A standalone render target or depth/stencil buffer (a renderbuffer in WebGL terms).
class SurfaceStorage : public GpuObject
{
public:
    SurfaceStorage(Backend *backend, const SurfaceDesc &desc);
    ~SurfaceStorage() override;
    void OnContextLost() override;
    void OnContextRestored() override;
    GpuSurface *Gpu() const { return m_gpu; }
    const SurfaceDesc &Desc() const { return m_desc; }

private:
    Backend *m_backend;
    SurfaceDesc m_desc;
    GpuSurface *m_gpu = nullptr;
};

} // namespace dxwebgl2
