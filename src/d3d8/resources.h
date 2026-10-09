/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Direct3D 8 resources (buffers, textures, surfaces).
 */
#pragma once

#include "d3d8/device.h"

#include "core/buffer_storage.h"
#include "core/texture_storage.h"

namespace dxwebgl2 {

/// Base of all objects that belong to a device. Holds a reference on the
/// device (as Direct3D 8 does) so GL objects can always be released safely.
class DeviceChild
{
public:
    /// \param takeRef  hold a reference on the device (all user-visible objects do)
    explicit DeviceChild(Device *dev, bool takeRef = true);
    virtual ~DeviceChild();
    /// For objects owned by the device itself: drops the device reference so
    /// there is no ownership cycle.
    void DropDeviceRef();
    Device *GetDeviceInternal() const { return m_device; }

protected:
    HRESULT GetDeviceCommon(IDirect3DDevice8 **ppDevice);
    HRESULT SetPrivateDataCommon(REFGUID guid, const void *data, DWORD size, DWORD flags);
    HRESULT GetPrivateDataCommon(REFGUID guid, void *data, DWORD *size);
    HRESULT FreePrivateDataCommon(REFGUID guid);

    Device *m_device;
    bool m_holdsRef;
    struct PrivateData { std::vector<uint8_t> bytes; };
    std::map<std::string, PrivateData> m_private;
};

/// IDirect3DResource8 methods on top of Unknown<Iface>.
template <class Iface>
class ResourceImpl : public Unknown<Iface>, public DeviceChild
{
public:
    explicit ResourceImpl(Device *dev) : DeviceChild(dev) {}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice8 **p) override { return GetDeviceCommon(p); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g, const void *d, DWORD s, DWORD f) override { return SetPrivateDataCommon(g, d, s, f); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g, void *d, DWORD *s) override { return GetPrivateDataCommon(g, d, s); }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID g) override { return FreePrivateDataCommon(g); }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD p) override { DWORD o = m_priority; m_priority = p; return o; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return m_priority; }
    void STDMETHODCALLTYPE PreLoad() override {}

protected:
    DWORD m_priority = 0;
};

//------------------------------------------------------------------------------
// Buffers
//------------------------------------------------------------------------------
class VertexBuffer final : public ResourceImpl<IDirect3DVertexBuffer8>
{
public:
    VertexBuffer(Device *dev, UINT length, DWORD usage, DWORD fvf, D3DPOOL pool);
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_VERTEXBUFFER; }
    HRESULT STDMETHODCALLTYPE Lock(UINT o, UINT s, BYTE **pp, DWORD f) override { return m_storage.Lock(o, s, pp, f); }
    HRESULT STDMETHODCALLTYPE Unlock() override { return m_storage.Unlock(); }
    HRESULT STDMETHODCALLTYPE GetDesc(D3DVERTEXBUFFER_DESC *pDesc) override;
    GLuint Flush() { return m_storage.Flush(); }
    DWORD FVF() const { return m_fvf; }
    BufferStorage m_storage;
private:
    DWORD m_fvf, m_usage;
    D3DPOOL m_pool;
};

class IndexBuffer final : public ResourceImpl<IDirect3DIndexBuffer8>
{
public:
    IndexBuffer(Device *dev, UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool);
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_INDEXBUFFER; }
    HRESULT STDMETHODCALLTYPE Lock(UINT o, UINT s, BYTE **pp, DWORD f) override { return m_storage.Lock(o, s, pp, f); }
    HRESULT STDMETHODCALLTYPE Unlock() override { return m_storage.Unlock(); }
    HRESULT STDMETHODCALLTYPE GetDesc(D3DINDEXBUFFER_DESC *pDesc) override;
    GLuint Flush() { return m_storage.Flush(); }
    D3DFORMAT Format() const { return m_format; }
    BufferStorage m_storage;
private:
    D3DFORMAT m_format;
    DWORD m_usage;
    D3DPOOL m_pool;
};

//------------------------------------------------------------------------------
// Textures
//------------------------------------------------------------------------------
class Texture2D final : public ResourceImpl<IDirect3DTexture8>, public TextureBase
{
public:
    Texture2D(Device *dev, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool);
    ~Texture2D() override;
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_TEXTURE; }
    DWORD STDMETHODCALLTYPE SetLOD(DWORD l) override { DWORD o = m_lod; m_lod = l; return o; }
    DWORD STDMETHODCALLTYPE GetLOD() override { return m_lod; }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { return m_levelCount; }
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) override;
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT Level, IDirect3DSurface8 **ppSurfaceLevel) override;
    HRESULT STDMETHODCALLTYPE LockRect(UINT Level, D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT Level) override;
    HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT *) override { return D3D_OK; }
    Surface *LevelSurface(UINT level);
private:
    std::vector<Surface *> m_surfaces;
};

class CubeTexture final : public ResourceImpl<IDirect3DCubeTexture8>, public TextureBase
{
public:
    CubeTexture(Device *dev, UINT edge, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool);
    ~CubeTexture() override;
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_CUBETEXTURE; }
    DWORD STDMETHODCALLTYPE SetLOD(DWORD l) override { DWORD o = m_lod; m_lod = l; return o; }
    DWORD STDMETHODCALLTYPE GetLOD() override { return m_lod; }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { return m_levelCount; }
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) override;
    HRESULT STDMETHODCALLTYPE GetCubeMapSurface(D3DCUBEMAP_FACES FaceType, UINT Level, IDirect3DSurface8 **ppCubeMapSurface) override;
    HRESULT STDMETHODCALLTYPE LockRect(D3DCUBEMAP_FACES FaceType, UINT Level, D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect(D3DCUBEMAP_FACES FaceType, UINT Level) override;
    HRESULT STDMETHODCALLTYPE AddDirtyRect(D3DCUBEMAP_FACES, const RECT *) override { return D3D_OK; }
    Surface *LevelSurface(UINT face, UINT level);
private:
    std::vector<Surface *> m_surfaces;
};

class Volume;
class VolumeTexture final : public ResourceImpl<IDirect3DVolumeTexture8>, public TextureBase
{
public:
    VolumeTexture(Device *dev, UINT w, UINT h, UINT d, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool);
    ~VolumeTexture() override;
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_VOLUMETEXTURE; }
    DWORD STDMETHODCALLTYPE SetLOD(DWORD l) override { DWORD o = m_lod; m_lod = l; return o; }
    DWORD STDMETHODCALLTYPE GetLOD() override { return m_lod; }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { return m_levelCount; }
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT Level, D3DVOLUME_DESC *pDesc) override;
    HRESULT STDMETHODCALLTYPE GetVolumeLevel(UINT Level, IDirect3DVolume8 **ppVolumeLevel) override;
    HRESULT STDMETHODCALLTYPE LockBox(UINT Level, D3DLOCKED_BOX *pLockedVolume, const D3DBOX *pBox, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE UnlockBox(UINT Level) override;
    HRESULT STDMETHODCALLTYPE AddDirtyBox(const D3DBOX *) override { return D3D_OK; }
private:
    std::vector<Volume *> m_volumes;
};

/// A level of a volume texture.
class Volume final : public Unknown<IDirect3DVolume8>
{
public:
    Volume(VolumeTexture *parent, UINT level);
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice8 **pp) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID, void **pp) override;
    HRESULT STDMETHODCALLTYPE GetDesc(D3DVOLUME_DESC *pDesc) override;
    HRESULT STDMETHODCALLTYPE LockBox(D3DLOCKED_BOX *pLockedVolume, const D3DBOX *pBox, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE UnlockBox() override;
protected:
    void OnZeroRefs() override {}
private:
    VolumeTexture *m_parent;
    UINT m_level;
};

//------------------------------------------------------------------------------
// Surfaces
//------------------------------------------------------------------------------
class Surface final : public Unknown<IDirect3DSurface8>, public DeviceChild, public GLObject
{
public:
    enum Kind { Level, Image, RenderTarget, DepthStencil, BackBuffer };

    /// Surface of a texture level; lifetime is tied to the texture.
    Surface(TextureBase *tex, IUnknown *owner, UINT face, UINT level);
    /// Standalone surface. `samples` > 1 makes a multisampled render target / depth buffer.
    Surface(Device *dev, Kind kind, UINT w, UINT h, D3DFORMAT fmt, int samples = 0);
    ~Surface() override;
    void OnContextLost() override { m_rb = 0; }
    void OnContextRestored() override;

    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice8 **p) override { return GetDeviceCommon(p); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g, const void *d, DWORD s, DWORD f) override { return SetPrivateDataCommon(g, d, s, f); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g, void *d, DWORD *s) override { return GetPrivateDataCommon(g, d, s); }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID g) override { return FreePrivateDataCommon(g); }
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID riid, void **ppContainer) override;
    HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC *pDesc) override;
    HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect() override;

    // Internal
    Kind GetKind() const { return m_kind; }
    D3DFORMAT Format() const { return m_format; }
    UINT Width() const { return m_width; }
    UINT Height() const { return m_height; }
    bool IsRenderable() const { return m_kind == RenderTarget || m_kind == BackBuffer || (m_kind == Level && m_tex && m_tex->m_isRT); }
    uint32_t UniqueId() const { return m_id; }
    TextureBase *Texture() const { return m_tex; }
    UINT Face() const { return m_face; }
    UINT MipLevel() const { return m_level; }
    GLuint Renderbuffer() const { return m_rb; }
    /// Samples per pixel (0 = not multisampled).
    int Samples() const;
    /// Attaches this surface to the currently bound framebuffer unless `cachedKey` (the value returned
    /// by the previous call for that framebuffer) says it already is. Returns the new key.
    uint64_t AttachColor(uint64_t cachedKey);
    uint64_t AttachDepth(bool &hasStencil, uint64_t cachedKey);
    /// Reads a rectangle into `dst` converted to `dstFormat`.
    bool ReadPixelsTo(const RECT &rc, D3DFORMAT dstFormat, void *dst, UINT dstPitch);
    /// Writes a rectangle from `src` (in `srcFormat`) at the position of rc.
    bool WritePixelsFrom(const RECT &rc, D3DFORMAT srcFormat, const void *src, UINT srcPitch);
    void SetPool(D3DPOOL p) { m_pool = p; }
    void SetUsage(DWORD u) { m_usage = u; }

protected:
    void OnZeroRefs() override;

private:
    Kind m_kind;
    D3DFORMAT m_format;
    UINT m_width, m_height;
    D3DPOOL m_pool = D3DPOOL_DEFAULT;
    DWORD m_usage = 0;
    TextureBase *m_tex = nullptr;
    IUnknown *m_owner = nullptr;
    UINT m_face = 0, m_level = 0;
    std::vector<uint8_t> m_data; // Image surfaces
    GLuint m_rb = 0;             // RenderTarget/DepthStencil
    int m_samples = 0;
    bool m_locked = false;
    DWORD m_lockFlags = 0;
    RECT m_lockRect = {};
    std::vector<uint8_t> m_lockTemp; // render-target read-back
    uint32_t m_id;
};

} // namespace dxwebgl2
