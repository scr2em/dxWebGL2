/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Device creation, back buffer, presentation, device loss and render target handling. The GPU work is the
 * backend's (core/backend.h).
 */
#include "d3d8/d3d8_internal.h"
#include "core/program.h"
#include "d3d8/resources.h"
#include "core/shader.h"

namespace dxwebgl2 {

/// The (single) device, for the test aids of the C interface.
static Device *g_primaryDevice = nullptr;

//------------------------------------------------------------------------------
// Creation
//------------------------------------------------------------------------------
Device *Device::Create(IDirect3D8 *d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior,
                       D3DPRESENT_PARAMETERS *pp)
{
    Device *dev = new Device();
    if (!dev->Initialize(d3d, adapter, type, focus, behavior, pp))
    {
        dev->m_refs = 0;
        delete dev;
        return nullptr;
    }
    return dev;
}

/// Forgets what the device caches about backend objects (fresh or restored context).
void Device::ResetBackendCaches()
{
    memset(m_stageSampler, 0, sizeof m_stageSampler);
    m_samplerDirtyMask = 0xFF;
    m_curProgram = nullptr;
    m_keyDirty = true;
}

static void IdentityGammaRamp(D3DGAMMARAMP &g)
{
    memset(&g, 0, sizeof g);
    for (int i = 0; i < 256; ++i) g.red[i] = g.green[i] = g.blue[i] = (WORD)(i * 257);
}

bool Device::Initialize(IDirect3D8 *d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior,
                        D3DPRESENT_PARAMETERS *pp)
{
    ParseCommandLineOnce();
    g_primaryDevice = this;
    m_d3d = d3d;
    m_d3d->AddRef();
    m_creation.AdapterOrdinal = adapter;
    m_creation.DeviceType = type;
    m_creation.hFocusWindow = focus;
    m_creation.BehaviorFlags = behavior;

    D3DPRESENT_PARAMETERS params = *pp;
    m_backend = CreateBackend();
    if (!m_backend->Initialize(params.BackBufferWidth, params.BackBufferHeight)) return false;
    SetRendererString(m_backend->Description());
    ResetBackendCaches();
    IdentityGammaRamp(m_gamma);

    m_pp = params;
    if (!CreateBackBuffer(params)) return false;
    ResetState();

    if (GetConfig().loseAfterFrames > 0)
    {
        m_loseCountdown = GetConfig().loseAfterFrames;
        m_loseArmed = true;
    }

    // Persist the corrected parameters for the caller.
    *pp = m_pp;
    return true;
}

Device::~Device()
{
    delete m_backend;
}

void Device::ReleasePrograms()
{
    for (auto &p : m_programs) m_backend->DestroyProgram(p.second);
    m_programs.clear();
    m_curProgram = nullptr;
}

void Device::OnZeroRefs()
{
    // Last reference: tear everything down while the GPU context still exists.
    ReleasePrograms();
    for (auto &s : m_vertexShaders) delete s.second;
    m_vertexShaders.clear();
    for (auto &s : m_pixelShaders) delete s.second;
    m_pixelShaders.clear();
    m_curRT = nullptr;
    m_curDS = nullptr;
    m_s = DeviceState();
    DestroyBackBuffer();
    if (g_diagOn) DiagPrintReport("device released");
    m_backend->Shutdown();
    if (g_primaryDevice == this) g_primaryDevice = nullptr;
    if (m_d3d) { m_d3d->Release(); m_d3d = nullptr; }
    delete this;
}

//------------------------------------------------------------------------------
// Back buffer
//------------------------------------------------------------------------------
bool Device::CreateBackBuffer(const D3DPRESENT_PARAMETERS &pp)
{
    UINT w = pp.BackBufferWidth, h = pp.BackBufferHeight;
    if (!w || !h)
    {
        int cw = 0, ch = 0;
        m_backend->CanvasSize(cw, ch);
        w = cw > 0 ? cw : 640;
        h = ch > 0 ? ch : 480;
    }
    m_pp.BackBufferWidth = w;
    m_pp.BackBufferHeight = h;
    if (m_pp.BackBufferFormat == D3DFMT_UNKNOWN) m_pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    if (m_pp.BackBufferCount == 0) m_pp.BackBufferCount = 1;

    // Multisampling: D3D asks for a sample count (2..16); the backend gives what the GPU offers, at most
    // GpuCaps::maxSamples. The report of the actually used type goes back to the application.
    m_samples = 0;
    if (pp.MultiSampleType >= 2 && pp.SwapEffect == D3DSWAPEFFECT_DISCARD && Caps().maxSamples >= 2)
        m_samples = Min<int>((int)pp.MultiSampleType, Caps().maxSamples);
    m_pp.MultiSampleType = m_samples ? (D3DMULTISAMPLE_TYPE)m_samples : D3DMULTISAMPLE_NONE;
    if (pp.MultiSampleType >= 2 && !m_samples) DXW_HIT(Unsupported, "multisample back buffer requested (%d) but not available", (int)pp.MultiSampleType);
    else if (m_samples && (int)pp.MultiSampleType != m_samples) DXW_HIT(Approximated, "multisample back buffer %dx clamped to %dx", (int)pp.MultiSampleType, m_samples);
    if (!m_backend->CreateBackBuffer(w, h, m_samples)) return false;
    if (GetConfig().hooks.OnClientSize) GetConfig().hooks.OnClientSize(w, h);

    // The device owns one reference of its default surfaces.
    m_backBuffer = new Surface(this, Surface::BackBuffer, w, h, m_pp.BackBufferFormat);
    m_backBuffer->DropDeviceRef();
    m_curRT = m_backBuffer;

    if (m_pp.EnableAutoDepthStencil)
    {
        D3DFORMAT zf = m_pp.AutoDepthStencilFormat;
        const FormatInfo *zi = GetFormatInfo(zf);
        if (!zi || !zi->depth) { zf = D3DFMT_D24S8; m_pp.AutoDepthStencilFormat = zf; }
        m_defaultDepth = new Surface(this, Surface::DepthStencil, w, h, zf, m_samples);
        m_defaultDepth->DropDeviceRef();
        m_curDS = m_defaultDepth;
    }
    m_rtWidth = w;
    m_rtHeight = h;
    UpdateRenderTargets();
    return true;
}

void Device::DestroyBackBuffer()
{
    m_curRT = nullptr;
    m_curDS = nullptr;
    if (m_backBuffer) m_backBuffer->Release();
    if (m_defaultDepth) m_defaultDepth->Release();
    m_backBuffer = nullptr;
    m_defaultDepth = nullptr;
    m_backend->DestroyBackBuffer();
    m_samples = 0;
    UpdateRenderTargets();
}

//------------------------------------------------------------------------------
// Present
//------------------------------------------------------------------------------
HRESULT Device::Present(const RECT *, const RECT *, HWND, const RGNDATA *)
{
    TickTestContextLoss();
    if (!CheckContext())
    {
        // Lost (or restored but not Reset yet). The application polls TestCooperativeLevel(); give the
        // browser a chance to run the events that restore the context.
        DiagEndFrame();
        if (GetConfig().hooks.OnFramePresented) GetConfig().hooks.OnFramePresented();
        return D3DERR_DEVICELOST;
    }
    if (GetConfig().debug && m_presentCounter % 120 == 0) m_backend->DebugDescribeBackBuffer();
    m_backend->Present();
    if (GetConfig().debug && ++m_presentCounter % 120 == 1)
        Log("present #%u, %u draw calls so far", m_presentCounter, (unsigned)m_drawCounter);
    DiagEndFrame();
    if (GetConfig().hooks.OnFramePresented) GetConfig().hooks.OnFramePresented();
    return D3D_OK;
}

void Device::SetGammaRamp(DWORD, const D3DGAMMARAMP *pRamp)
{
    if (!pRamp) return;
    m_gamma = *pRamp;
    bool identity = true;
    for (int i = 0; i < 256 && identity; ++i)
    {
        const int id = i * 257;
        if (std::abs((int)m_gamma.red[i] - id) > 256 || std::abs((int)m_gamma.green[i] - id) > 256 ||
            std::abs((int)m_gamma.blue[i] - id) > 256)
            identity = false;
    }
    m_gammaActive = !identity;
    m_backend->SetGammaRamp(m_gamma.red, m_gamma.green, m_gamma.blue, m_gammaActive);
}

void Device::GetGammaRamp(D3DGAMMARAMP *pRamp)
{
    if (pRamp) *pRamp = m_gamma;
}

//------------------------------------------------------------------------------
// Context loss
//
// The GPU context can be lost at any time (GPU process crash, driver reset, a
// GPU switch, the browser reclaiming it). The device then behaves like a
// Direct3D 8 device that was lost: Present()/Clear()/TestCooperativeLevel()
// report D3DERR_DEVICELOST and drawing is skipped. Once the browser has
// restored the context, TestCooperativeLevel() returns D3DERR_DEVICENOTRESET;
// the application (its reset path) releases its
// non-managed resources and calls Reset(), which creates every GPU object
// again from the system memory copies the resources keep (RestoreContext()).
//------------------------------------------------------------------------------
/// What the GPU context is called in messages: the WebGL2 context or the WebGPU device.
static const char *ContextName(const Backend *b) { return b->Kind() == BackendKind::WebGPU ? "WebGPU device" : "WebGL context"; }

void Device::OnContextLostInternal()
{
    m_contextLost = true;
    m_needsReset = false;
    m_inScene = false;
    ++g_totals.contextLosses;
    fprintf(stderr, "[dxWebGL2] The %s was lost (graphics driver reset or the browser reclaimed the GPU); the application is expected to reset the device when it is back\n",
            ContextName(m_backend));
    DXW_HIT(Event, "%s lost", ContextName(m_backend));
    m_backend->OnContextLost();
    if (m_loseArmed && m_restoreCountdown <= 0) m_restoreCountdown = GetConfig().loseRestoreFrames;
}

bool Device::CheckContext()
{
    m_backend->ClearContextEvent();
    const bool lostNow = m_backend->IsContextLost();
    if (lostNow && !m_contextLost) OnContextLostInternal();
    else if (!lostNow && m_contextLost)
    {
        // The browser restored the context: all GPU objects are gone, so the device wants a Reset().
        m_contextLost = false;
        m_needsReset = true;
        ++g_totals.contextRestores;
        fprintf(stderr, "[dxWebGL2] The %s was restored; waiting for the application to Reset() the device\n", ContextName(m_backend));
        DXW_HIT(Event, "%s restored", ContextName(m_backend));
    }
    return !m_contextLost && !m_needsReset;
}

/// Test aid behind -dxwebgl2-loseafter=N[,M]: drops the context after N presented frames and asks the
/// browser to restore it M presented frames later.
void Device::TickTestContextLoss()
{
    if (!m_loseArmed) return;
    if (m_loseCountdown > 0)
    {
        if (--m_loseCountdown == 0)
        {
            const bool webgpu = m_backend->Kind() == BackendKind::WebGPU;
            if (m_backend->TestLoseContext())
                fprintf(stderr, "[dxWebGL2] test: %s called\n", webgpu ? "GPUDevice.destroy()" : "WEBGL_lose_context.loseContext()");
            else
            {
                fprintf(stderr, "[dxWebGL2] test: %s is not available\n", webgpu ? "GPUDevice.destroy()" : "WEBGL_lose_context");
                m_loseArmed = false;
            }
            m_restoreCountdown = GetConfig().loseRestoreFrames;
        }
        return;
    }
    if (m_restoreCountdown > 0 && --m_restoreCountdown == 0)
    {
        fprintf(stderr, "[dxWebGL2] test: %s\n", m_backend->Kind() == BackendKind::WebGPU ? "a new WebGPU device may be created"
                                                                                       : "WEBGL_lose_context.restoreContext() called");
        m_backend->TestRestoreContext();
        m_loseArmed = false;
    }
}

HRESULT Device::TestCooperativeLevel()
{
    if (!CheckContext())
    {
        if (m_contextLost)
        {
            // While the context is lost the application polls here once per frame and may not reach
            // Present() at all (a render loop that gives up after a failed frame). Give the browser the chance to run the
            // events that restore the context, and let the test aid count the frames.
            TickTestContextLoss();
            if (GetConfig().hooks.OnFramePresented) GetConfig().hooks.OnFramePresented();
            return D3DERR_DEVICELOST;
        }
        return D3DERR_DEVICENOTRESET;
    }
    return D3D_OK;
}

/// Creates every GPU object again on the new context.
bool Device::RestoreContext()
{
    if (!m_backend->RestoreContext()) return false;
    // Programs belong to the dead context (the backend deletes no GPU object of it).
    ReleasePrograms();
    SetRendererString(m_backend->Description());
    ResetBackendCaches();
    // The gamma ramp starts over as the identity (the backend keeps whether it is applied).
    IdentityGammaRamp(m_gamma);
    m_backend->SetGammaRamp(m_gamma.red, m_gamma.green, m_gamma.blue, m_gammaActive);
    m_needsReset = false;
    fprintf(stderr, "[dxWebGL2] The device was restored on the new %s\n", ContextName(m_backend));
    return true;
}

UINT Device::GetAvailableTextureMem() { return 256u * 1024u * 1024u; }
HRESULT Device::ResourceManagerDiscardBytes(DWORD) { return D3D_OK; }

HRESULT Device::GetDirect3D(IDirect3D8 **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    *pp = m_d3d;
    m_d3d->AddRef();
    return D3D_OK;
}

HRESULT Device::GetDeviceCaps(D3DCAPS8 *caps)
{
    if (!caps) return D3DERR_INVALIDCALL;
    FillDeviceCaps(caps, &Caps());
    return D3D_OK;
}

HRESULT Device::GetDisplayMode(D3DDISPLAYMODE *m)
{
    if (!m) return D3DERR_INVALIDCALL;
    m->Width = m_pp.BackBufferWidth;
    m->Height = m_pp.BackBufferHeight;
    m->RefreshRate = 60;
    m->Format = (m_pp.BackBufferFormat == D3DFMT_R5G6B5 || m_pp.BackBufferFormat == D3DFMT_X1R5G5B5) ? m_pp.BackBufferFormat : D3DFMT_X8R8G8B8;
    return D3D_OK;
}

HRESULT Device::GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *p)
{
    if (!p) return D3DERR_INVALIDCALL;
    *p = m_creation;
    return D3D_OK;
}

HRESULT Device::SetCursorProperties(UINT, UINT, IDirect3DSurface8 *)
{
    // The cursor is drawn by the browser or by the application.
    return D3D_OK;
}

void Device::SetCursorPosition(UINT, UINT, DWORD) {}

BOOL Device::ShowCursor(BOOL bShow)
{
    BOOL old = m_cursorVisible;
    m_cursorVisible = bShow;
    return old;
}

HRESULT Device::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *, IDirect3DSwapChain8 **pp)
{
    if (pp) *pp = nullptr;
    DXWEBGL2_UNSUPPORTED("CreateAdditionalSwapChain");
    return D3DERR_NOTAVAILABLE;
}

HRESULT Device::GetRasterStatus(D3DRASTER_STATUS *s)
{
    if (!s) return D3DERR_INVALIDCALL;
    s->InVBlank = FALSE;
    s->ScanLine = 0;
    return D3D_OK;
}

HRESULT Device::Reset(D3DPRESENT_PARAMETERS *pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    CheckContext();
    if (m_contextLost) return D3DERR_DEVICELOST;
    if (m_needsReset && !RestoreContext()) return D3DERR_DEVICELOST;
    D3DPRESENT_PARAMETERS params = *pp;
    m_curRT = nullptr;
    m_curDS = nullptr;
    DestroyBackBuffer();
    m_pp = params;
    if (!CreateBackBuffer(params)) return D3DERR_INVALIDCALL;
    ResetState();
    *pp = m_pp;
    return D3D_OK;
}

HRESULT Device::GetBackBuffer(UINT index, D3DBACKBUFFER_TYPE, IDirect3DSurface8 **pp)
{
    if (!pp || index != 0) return D3DERR_INVALIDCALL;
    *pp = m_backBuffer;
    m_backBuffer->AddRef();
    return D3D_OK;
}

HRESULT Device::GetFrontBuffer(IDirect3DSurface8 *dest)
{
    if (!dest) return D3DERR_INVALIDCALL;
    Surface *dst = static_cast<Surface *>(dest);
    UINT w = Min(dst->Width(), m_backBuffer->Width()), h = Min(dst->Height(), m_backBuffer->Height());
    if (dst->Format() != D3DFMT_A8R8G8B8) return D3DERR_INVALIDCALL;
    std::vector<uint8_t> tmp((size_t)w * h * 4);
    RECT rc = {0, 0, (LONG)w, (LONG)h};
    if (!m_backBuffer->ReadPixelsTo(rc, D3DFMT_A8R8G8B8, tmp.data(), w * 4)) return D3DERR_INVALIDCALL;
    for (size_t i = 0; i < (size_t)w * h; ++i) tmp[i * 4 + 3] = 255;
    return dst->WritePixelsFrom(rc, D3DFMT_A8R8G8B8, tmp.data(), w * 4) ? D3D_OK : D3DERR_INVALIDCALL;
}

HRESULT Device::BeginScene()
{
    if (m_inScene) return D3DERR_INVALIDCALL;
    m_inScene = true;
    return D3D_OK;
}

HRESULT Device::EndScene()
{
    if (!m_inScene) return D3DERR_INVALIDCALL;
    m_inScene = false;
    return D3D_OK;
}

HRESULT Device::ValidateDevice(DWORD *pNumPasses)
{
    if (pNumPasses) *pNumPasses = 1;
    return D3D_OK;
}

HRESULT Device::GetInfo(DWORD, void *, DWORD) { return D3DERR_INVALIDCALL; }

HRESULT Device::SetPaletteEntries(UINT, const PALETTEENTRY *) { return D3DERR_INVALIDCALL; }
HRESULT Device::GetPaletteEntries(UINT, PALETTEENTRY *) { return D3DERR_INVALIDCALL; }
HRESULT Device::SetCurrentTexturePalette(UINT) { return D3DERR_INVALIDCALL; }
HRESULT Device::GetCurrentTexturePalette(UINT *) { return D3DERR_INVALIDCALL; }
HRESULT Device::SetClipStatus(const D3DCLIPSTATUS8 *) { return D3D_OK; }
HRESULT Device::GetClipStatus(D3DCLIPSTATUS8 *s) { if (s) memset(s, 0, sizeof *s); return D3D_OK; }
HRESULT Device::DrawRectPatch(UINT, const float *, const D3DRECTPATCH_INFO *) { return D3DERR_INVALIDCALL; }
HRESULT Device::DrawTriPatch(UINT, const float *, const D3DTRIPATCH_INFO *) { return D3DERR_INVALIDCALL; }
HRESULT Device::DeletePatch(UINT) { return D3DERR_INVALIDCALL; }

//------------------------------------------------------------------------------
// Resource creation
//------------------------------------------------------------------------------
HRESULT Device::CreateTexture(UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture8 **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    *pp = nullptr;
    const FormatInfo *info = GetFormatInfo(fmt);
    if (!info || !info->texture)
    {
        DXW_HIT(Failed, "CreateTexture format %s not supported", info ? info->name : "unknown");
        return D3DERR_INVALIDCALL;
    }
    Texture2D *t = new Texture2D(this, w, h, levels, usage, fmt, pool);
    if (!t->Valid())
    {
        DXW_HIT(Failed, "CreateTexture %ux%u %s pool %d usage 0x%x failed", w, h, info->name, (int)pool, (unsigned)usage);
        t->Release();
        return D3DERR_INVALIDCALL;
    }
    if (g_diagOn && t->m_decode) DiagHit(Hit::Perf, "DXT texture decoded on the CPU (no WEBGL_compressed_texture_s3tc)");
    *pp = t;
    return D3D_OK;
}

HRESULT Device::CreateCubeTexture(UINT edge, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DCubeTexture8 **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    *pp = nullptr;
    CubeTexture *t = new CubeTexture(this, edge, levels, usage, fmt, pool);
    if (!t->Valid())
    {
        DXW_HIT(Failed, "CreateCubeTexture %u format %d failed", edge, (int)fmt);
        t->Release();
        return D3DERR_INVALIDCALL;
    }
    *pp = t;
    return D3D_OK;
}

HRESULT Device::CreateVolumeTexture(UINT w, UINT h, UINT d, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DVolumeTexture8 **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    *pp = nullptr;
    VolumeTexture *t = new VolumeTexture(this, w, h, d, levels, usage, fmt, pool);
    if (!t->Valid())
    {
        DXW_HIT(Failed, "CreateVolumeTexture %ux%ux%u format %d failed", w, h, d, (int)fmt);
        t->Release();
        return D3DERR_INVALIDCALL;
    }
    *pp = t;
    return D3D_OK;
}

HRESULT Device::CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer8 **pp)
{
    if (!pp || length == 0) return D3DERR_INVALIDCALL;
    *pp = new VertexBuffer(this, length, usage, fvf, pool);
    return D3D_OK;
}

HRESULT Device::CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DIndexBuffer8 **pp)
{
    if (!pp || length == 0) return D3DERR_INVALIDCALL;
    if (fmt != D3DFMT_INDEX16 && fmt != D3DFMT_INDEX32) return D3DERR_INVALIDCALL;
    *pp = new IndexBuffer(this, length, usage, fmt, pool);
    return D3D_OK;
}

HRESULT Device::CreateRenderTarget(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms, BOOL, IDirect3DSurface8 **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    *pp = nullptr;
    const FormatInfo *info = GetFormatInfo(fmt);
    if (!info || !info->renderTarget || !w || !h)
    {
        DXW_HIT(Failed, "CreateRenderTarget %ux%u format %s failed", w, h, info ? info->name : "?");
        return D3DERR_INVALIDCALL;
    }
    if (ms >= 2 && Caps().maxSamples < 2)
    {
        DXW_HIT(Failed, "CreateRenderTarget with %d samples (not available)", (int)ms);
        return D3DERR_NOTAVAILABLE;
    }
    *pp = new Surface(this, Surface::RenderTarget, w, h, fmt, ms >= 2 ? (int)ms : 0);
    return D3D_OK;
}

HRESULT Device::CreateDepthStencilSurface(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms, IDirect3DSurface8 **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    *pp = nullptr;
    const FormatInfo *info = GetFormatInfo(fmt);
    if (!info || !info->depth || !w || !h)
    {
        DXW_HIT(Failed, "CreateDepthStencilSurface %ux%u format %s failed", w, h, info ? info->name : "?");
        return D3DERR_INVALIDCALL;
    }
    if (ms >= 2 && Caps().maxSamples < 2)
    {
        DXW_HIT(Failed, "CreateDepthStencilSurface with %d samples (not available)", (int)ms);
        return D3DERR_NOTAVAILABLE;
    }
    *pp = new Surface(this, Surface::DepthStencil, w, h, fmt, ms >= 2 ? (int)ms : 0);
    return D3D_OK;
}

HRESULT Device::CreateImageSurface(UINT w, UINT h, D3DFORMAT fmt, IDirect3DSurface8 **pp)
{
    if (!pp || !w || !h) return D3DERR_INVALIDCALL;
    const FormatInfo *info = GetFormatInfo(fmt);
    if (!info || info->depth)
    {
        DXW_HIT(Failed, "CreateImageSurface format %d failed", (int)fmt);
        return D3DERR_INVALIDCALL;
    }
    *pp = new Surface(this, Surface::Image, w, h, fmt);
    return D3D_OK;
}

HRESULT Device::CopyRects(IDirect3DSurface8 *pSrc, const RECT *srcRects, UINT cRects, IDirect3DSurface8 *pDst, const POINT *dstPoints)
{
    if (!pSrc || !pDst) return D3DERR_INVALIDCALL;
    Surface *src = static_cast<Surface *>(pSrc), *dst = static_cast<Surface *>(pDst);
    RECT whole = {0, 0, (LONG)src->Width(), (LONG)src->Height()};
    UINT n = cRects ? cRects : 1;
    for (UINT i = 0; i < n; ++i)
    {
        RECT r = (cRects && srcRects) ? srcRects[i] : whole;
        POINT p = (cRects && dstPoints) ? dstPoints[i] : POINT{r.left, r.top};
        if (!(cRects && srcRects)) { p.x = 0; p.y = 0; }
        r.left = Clamp<LONG>(r.left, 0, src->Width());
        r.right = Clamp<LONG>(r.right, 0, src->Width());
        r.top = Clamp<LONG>(r.top, 0, src->Height());
        r.bottom = Clamp<LONG>(r.bottom, 0, src->Height());
        LONG w = r.right - r.left, h = r.bottom - r.top;
        if (w <= 0 || h <= 0) continue;
        // Clip against the destination.
        w = Min<LONG>(w, (LONG)dst->Width() - p.x);
        h = Min<LONG>(h, (LONG)dst->Height() - p.y);
        if (w <= 0 || h <= 0 || p.x < 0 || p.y < 0) return D3DERR_INVALIDCALL;
        r.right = r.left + w; r.bottom = r.top + h;
        RECT dr = {p.x, p.y, p.x + w, p.y + h};

        const FormatInfo *fi = GetFormatInfo(dst->Format());
        uint32_t pitch = FormatPitch(dst->Format(), w);
        std::vector<uint8_t> tmp((size_t)pitch * FormatRows(dst->Format(), h));
        if (fi && fi->blockBytes) { r.left &= ~3; r.top &= ~3; }
        if (!src->ReadPixelsTo(r, dst->Format(), tmp.data(), pitch)) return D3DERR_INVALIDCALL;
        if (!dst->WritePixelsFrom(dr, dst->Format(), tmp.data(), pitch)) return D3DERR_INVALIDCALL;
    }
    return D3D_OK;
}

HRESULT Device::UpdateTexture(IDirect3DBaseTexture8 *pSrc, IDirect3DBaseTexture8 *pDst)
{
    if (!pSrc || !pDst) return D3DERR_INVALIDCALL;
    if (pSrc->GetType() != pDst->GetType()) return D3DERR_INVALIDCALL;
    TextureBase *src, *dst;
    switch (pSrc->GetType())
    {
    case D3DRTYPE_TEXTURE: src = static_cast<Texture2D *>(static_cast<IDirect3DTexture8 *>(pSrc)); dst = static_cast<Texture2D *>(static_cast<IDirect3DTexture8 *>(pDst)); break;
    case D3DRTYPE_CUBETEXTURE: src = static_cast<CubeTexture *>(static_cast<IDirect3DCubeTexture8 *>(pSrc)); dst = static_cast<CubeTexture *>(static_cast<IDirect3DCubeTexture8 *>(pDst)); break;
    case D3DRTYPE_VOLUMETEXTURE:
    {
        // Volumes: copy the shadow level by level.
        VolumeTexture *s = static_cast<VolumeTexture *>(static_cast<IDirect3DVolumeTexture8 *>(pSrc));
        VolumeTexture *d = static_cast<VolumeTexture *>(static_cast<IDirect3DVolumeTexture8 *>(pDst));
        if (s->m_format != d->m_format) return D3DERR_INVALIDCALL;
        d->EnsureGpu();
        UINT skip = s->m_levelCount > d->m_levelCount ? s->m_levelCount - d->m_levelCount : 0;
        for (UINT l = 0; l < d->m_levelCount && l + skip < s->m_levelCount; ++l)
        {
            LevelData &sl = s->LevelAt(0, l + skip), &dl = d->LevelAt(0, l);
            if (sl.shadow.empty() || sl.slicePitch * sl.depth != dl.slicePitch * dl.depth) continue;
            dl.shadow = sl.shadow;
            dl.shadowValid = true;
            d->UploadWhole(0, l);
        }
        return D3D_OK;
    }
    default: return D3DERR_INVALIDCALL;
    }
    if (src->m_format != dst->m_format) return D3DERR_INVALIDCALL;
    UINT skip = src->m_levelCount > dst->m_levelCount ? src->m_levelCount - dst->m_levelCount : 0;
    for (UINT f = 0; f < dst->m_faces; ++f)
        for (UINT l = 0; l < dst->m_levelCount && l + skip < src->m_levelCount; ++l)
        {
            LevelData &sl = src->LevelAt(f, l + skip);
            LevelData &dl = dst->LevelAt(f, l);
            if (sl.width != dl.width || sl.height != dl.height || sl.shadow.empty()) continue;
            RECT rc = {0, 0, (LONG)dl.width, (LONG)dl.height};
            dst->WriteRect(f, l, rc, sl.shadow.data(), sl.pitch);
        }
    return D3D_OK;
}

//------------------------------------------------------------------------------
// Render targets

//------------------------------------------------------------------------------
// Render targets
//------------------------------------------------------------------------------
HRESULT Device::SetRenderTarget(IDirect3DSurface8 *rt, IDirect3DSurface8 *zs)
{
    if (rt)
    {
        Surface *s = static_cast<Surface *>(rt);
        if (!s->IsRenderable()) return D3DERR_INVALIDCALL;
        m_curRT = rt;
        m_s.viewport.X = 0; m_s.viewport.Y = 0;
        m_s.viewport.Width = s->Width(); m_s.viewport.Height = s->Height();
        m_s.viewport.MinZ = 0.0f; m_s.viewport.MaxZ = 1.0f;
        m_rtWidth = s->Width(); m_rtHeight = s->Height();
        Dirty(G_VIEWPORT);
        Dirty(G_POINT);
        ++g_d3d.rtSwitches;
    }
    if (zs)
    {
        Surface *s = static_cast<Surface *>(zs);
        if (s->GetKind() != Surface::DepthStencil) return D3DERR_INVALIDCALL;
        m_curDS = zs;
    }
    UpdateRenderTargets();
    return D3D_OK;
}

HRESULT Device::GetRenderTarget(IDirect3DSurface8 **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    *pp = m_curRT.get();
    if (*pp) (*pp)->AddRef();
    return *pp ? D3D_OK : D3DERR_NOTFOUND;
}

HRESULT Device::GetDepthStencilSurface(IDirect3DSurface8 **pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    *pp = m_curDS.get();
    if (*pp) (*pp)->AddRef();
    return *pp ? D3D_OK : D3DERR_NOTFOUND;
}

void Device::UpdateRenderTargets()
{
    const Surface *rt = static_cast<const Surface *>(m_curRT.get());
    const Surface *ds = static_cast<const Surface *>(m_curDS.get());
    m_backend->SetRenderTargets(rt ? rt->TargetRef() : RenderTargetRef(), ds ? ds->TargetRef() : RenderTargetRef());
}

} // namespace dxwebgl2

extern "C" {

void dxwebgl2_LoseContextAfterFrames(int frames, int restoreAfterFrames)
{
    dxwebgl2::GetConfig().loseAfterFrames = frames;
    dxwebgl2::GetConfig().loseRestoreFrames = restoreAfterFrames > 0 ? restoreAfterFrames : 120;
}

void dxwebgl2_LoseContextNow(int restoreAfterFrames)
{
    dxwebgl2::Device *dev = dxwebgl2::g_primaryDevice;
    if (!dev) return;
    dxwebgl2::GetConfig().loseRestoreFrames = restoreAfterFrames > 0 ? restoreAfterFrames : 1;
    dev->StartTestContextLoss();
}

} // extern "C"
