/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Checked feature tests of the Direct3D 8 library, on the backend --backend=webgl2|webgpu|auto selects (without it the
 * library's default, AUTO; --expect-backend=webgl2|webgpu names the backend AUTO must select).
 *
 * Every case draws through the plain D3D8 API, reads the back buffer and compares pixels (or counters)
 * with values computed by hand; it prints "CHECK <case> <label> PASS|FAIL". Cases that need a fresh
 * device (multisampling, context loss, extension fallbacks) run in their own page load, selected with
 * --mode=<name>; the default mode runs the cases that share one device. tests/run_features.mjs runs the
 * whole matrix in headless Chromium and fails if one check fails.
 */
#include <d3d8.h>
#include <d3dx8.h>
#include "dxwebgl2/dxwebgl2.h"

#include <emscripten.h>
#include <unistd.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static bool g_msaaDisabled = false;
/// The backend the device must render through (--backend=webgl2|webgpu, or --expect-backend= for what
/// DXWEBGL2_BACKEND_AUTO has to select); -1 = whichever AUTO selects (no option, --backend=auto).
static int g_wantBackend = -1;
static IDirect3D8 *g_d3d;
static IDirect3DDevice8 *g_dev;
static int g_pass = 0, g_fail = 0;
static const char *g_case = "";
static const int W = 640, H = 480;

static DWORD FloatToDword(float f) { DWORD v; memcpy(&v, &f, 4); return v; }
template <class T> static T Clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

#define CHECK_HR(expr)                                                                                      \
    do {                                                                                                    \
        HRESULT _hr = (expr);                                                                               \
        if (FAILED(_hr)) { printf("CHECK %s %s FAIL hr=0x%08x (line %d)\n", g_case, #expr, (unsigned)_hr, __LINE__); ++g_fail; } \
    } while (0)

static void Report(const char *label, bool ok, const char *fmt = nullptr, ...)
{
    char extra[256] = "";
    if (fmt)
    {
        va_list a;
        va_start(a, fmt);
        vsnprintf(extra, sizeof extra, fmt, a);
        va_end(a);
    }
    printf("CHECK %s %s %s %s\n", g_case, label, ok ? "PASS" : "FAIL", extra);
    if (ok) ++g_pass; else ++g_fail;
}

//------------------------------------------------------------------------------
// Back buffer access
//------------------------------------------------------------------------------
static DWORD g_px[W * H];

static void Grab()
{
    IDirect3DSurface8 *img = nullptr;
    CHECK_HR(g_dev->CreateImageSurface(W, H, D3DFMT_A8R8G8B8, &img));
    CHECK_HR(g_dev->GetFrontBuffer(img));
    D3DLOCKED_RECT lr;
    CHECK_HR(img->LockRect(&lr, nullptr, D3DLOCK_READONLY));
    for (int y = 0; y < H; ++y) memcpy(g_px + y * W, (BYTE *)lr.pBits + y * lr.Pitch, W * 4);
    CHECK_HR(img->UnlockRect());
    img->Release();
}

static bool ColorNear(DWORD got, DWORD want, int tol)
{
    for (int s = 0; s <= 16; s += 8)
        if (std::abs((int)((got >> s) & 255) - (int)((want >> s) & 255)) > tol) return false;
    return true;
}

/// A check that fails on WebGL2 for a known reason of that backend: reported as XFAIL there (not a failure).
static bool ExpectKnown(const char *label, int x, int y, DWORD rgb, const char *webgl2Reason, int tol = 4)
{
    const DWORD got = g_px[y * W + x] & 0xFFFFFF;
    const bool ok = ColorNear(got, rgb, tol);
    if (!ok && dxwebgl2_GetBackend() == DXWEBGL2_BACKEND_WEBGL2)
    {
        printf("CHECK %s %s XFAIL (%d,%d) got %06x want %06x: known WebGL2 limitation, %s\n", g_case, label, x, y, (unsigned)got,
               (unsigned)rgb, webgl2Reason);
        return false;
    }
    Report(label, ok, "(%d,%d) got %06x want %06x", x, y, (unsigned)got, (unsigned)rgb);
    return ok;
}

static bool Expect(const char *label, int x, int y, DWORD rgb, int tol = 4)
{
    const DWORD got = g_px[y * W + x] & 0xFFFFFF;
    const bool ok = ColorNear(got, rgb, tol);
    char extra[96];
    snprintf(extra, sizeof extra, "(%d,%d) got %06x want %06x", x, y, (unsigned)got, (unsigned)rgb);
    Report(label, ok, "%s", extra);
    return ok;
}

static DWORD Pack(float r, float g, float b)
{
    return ((DWORD)Clampv((int)(r * 255.0f + 0.5f), 0, 255) << 16) | ((DWORD)Clampv((int)(g * 255.0f + 0.5f), 0, 255) << 8) |
           (DWORD)Clampv((int)(b * 255.0f + 0.5f), 0, 255);
}

//------------------------------------------------------------------------------
// Drawing helpers
//------------------------------------------------------------------------------
static void BaseStates()
{
    g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    g_dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    g_dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    g_dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    g_dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    for (int s = 0; s < 8; ++s)
    {
        g_dev->SetTexture(s, nullptr);
        g_dev->SetTextureStageState(s, D3DTSS_COLOROP, s == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE);
        g_dev->SetTextureStageState(s, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        g_dev->SetTextureStageState(s, D3DTSS_COLORARG2, D3DTA_CURRENT);
        g_dev->SetTextureStageState(s, D3DTSS_ALPHAOP, s == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE);
        g_dev->SetTextureStageState(s, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        g_dev->SetTextureStageState(s, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
        g_dev->SetTextureStageState(s, D3DTSS_TEXCOORDINDEX, s);
        g_dev->SetTextureStageState(s, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        g_dev->SetTextureStageState(s, D3DTSS_MINFILTER, D3DTEXF_POINT);
        g_dev->SetTextureStageState(s, D3DTSS_MAGFILTER, D3DTEXF_POINT);
        g_dev->SetTextureStageState(s, D3DTSS_MIPFILTER, D3DTEXF_NONE);
        g_dev->SetTextureStageState(s, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
        g_dev->SetTextureStageState(s, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
        g_dev->SetTextureStageState(s, D3DTSS_ADDRESSW, D3DTADDRESS_CLAMP);
        g_dev->SetTextureStageState(s, D3DTSS_BORDERCOLOR, 0);
    }
    g_dev->SetPixelShader(0);
    D3DVIEWPORT8 vp = {0, 0, (DWORD)W, (DWORD)H, 0.0f, 1.0f};
    g_dev->SetViewport(&vp);
}

static void ClearScreen(DWORD color = 0xFF101010)
{
    CHECK_HR(g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, color, 1.0f, 0));
}

// Pre-transformed vertex with up to four 3-component texture coordinate sets.
struct RV
{
    float x, y, z, rhw;
    DWORD color;
    float t[4][3];
};
#define RV_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX4 | D3DFVF_TEXCOORDSIZE3(0) | D3DFVF_TEXCOORDSIZE3(1) | D3DFVF_TEXCOORDSIZE3(2) | D3DFVF_TEXCOORDSIZE3(3))

/// Pixel-aligned rectangle [x0,x1)x[y0,y1) with the given diffuse color and the same texture
/// coordinates (u0,v0)-(u1,v1) on all four sets (third component w).
static void Rect(float x0, float y0, float x1, float y1, float z, DWORD color, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1, float w = 0)
{
    RV v[4];
    const float xs[4] = {x0, x0, x1, x1}, ys[4] = {y0, y1, y1, y0};
    const float us[4] = {u0, u0, u1, u1}, vs[4] = {v0, v1, v1, v0};
    for (int i = 0; i < 4; ++i)
    {
        v[i].x = xs[i] - 0.5f; v[i].y = ys[i] - 0.5f; v[i].z = z; v[i].rhw = 1.0f; v[i].color = color;
        for (int s = 0; s < 4; ++s) { v[i].t[s][0] = us[i]; v[i].t[s][1] = vs[i]; v[i].t[s][2] = w; }
    }
    WORD idx[6] = {0, 1, 2, 0, 2, 3};
    g_dev->SetVertexShader(RV_FVF);
    CHECK_HR(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, idx, D3DFMT_INDEX16, v, sizeof(RV)));
}

/// Rectangle with explicit per-set texture coordinates (same for all four corners' interpolation).
static void RectUV(float x0, float y0, float x1, float y1, float z, DWORD color, const float tl[4][3], const float br[4][3])
{
    RV v[4];
    const float xs[4] = {x0, x0, x1, x1}, ys[4] = {y0, y1, y1, y0};
    const int fx[4] = {0, 0, 1, 1}, fy[4] = {0, 1, 1, 0};
    for (int i = 0; i < 4; ++i)
    {
        v[i].x = xs[i] - 0.5f; v[i].y = ys[i] - 0.5f; v[i].z = z; v[i].rhw = 1.0f; v[i].color = color;
        for (int s = 0; s < 4; ++s)
        {
            v[i].t[s][0] = fx[i] ? br[s][0] : tl[s][0];
            v[i].t[s][1] = fy[i] ? br[s][1] : tl[s][1];
            v[i].t[s][2] = (fx[i] || fy[i]) ? br[s][2] : tl[s][2];
        }
    }
    WORD idx[6] = {0, 1, 2, 0, 2, 3};
    g_dev->SetVertexShader(RV_FVF);
    CHECK_HR(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, idx, D3DFMT_INDEX16, v, sizeof(RV)));
}

static IDirect3DTexture8 *MakeSolid(UINT w, UINT h, DWORD argb, D3DFORMAT fmt = D3DFMT_A8R8G8B8, UINT levels = 1)
{
    IDirect3DTexture8 *tex = nullptr;
    CHECK_HR(g_dev->CreateTexture(w, h, levels, 0, fmt, D3DPOOL_MANAGED, &tex));
    if (!tex) return nullptr;
    D3DLOCKED_RECT lr;
    CHECK_HR(tex->LockRect(0, &lr, nullptr, 0));
    for (UINT y = 0; y < h; ++y)
        for (UINT x = 0; x < w; ++x) ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = argb;
    CHECK_HR(tex->UnlockRect(0));
    return tex;
}

static DWORD Assemble(const std::string &src, bool vertex, const DWORD *decl = nullptr)
{
    ID3DXBuffer *code = nullptr, *errs = nullptr;
    HRESULT hr = D3DXAssembleShader(src.data(), (UINT)src.size(), 0, nullptr, &code, &errs);
    if (FAILED(hr))
    {
        printf("assemble failed: %s\n", errs ? (const char *)errs->GetBufferPointer() : "?");
        return 0;
    }
    DWORD h = 0;
    if (vertex) hr = g_dev->CreateVertexShader(decl, (const DWORD *)code->GetBufferPointer(), &h, 0);
    else hr = g_dev->CreatePixelShader((const DWORD *)code->GetBufferPointer(), &h);
    code->Release();
    if (FAILED(hr)) { printf("create shader failed 0x%x\n", (unsigned)hr); return 0; }
    return h;
}

struct LV { float x, y, z, rhw; DWORD color; float u, v; };
#define LV_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

//------------------------------------------------------------------------------
// Case: border color addressing (emulated in the shader)
//------------------------------------------------------------------------------
static void CaseBorder()
{
    g_case = "border";
    BaseStates();
    ClearScreen();
    IDirect3DTexture8 *tex = MakeSolid(2, 2, 0xFFFF0000); // opaque red
    g_dev->SetTexture(0, tex);
    g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_dev->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_BORDER);
    g_dev->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_BORDER);
    g_dev->SetTextureStageState(0, D3DTSS_BORDERCOLOR, 0xFF00FF00);
    CHECK_HR(g_dev->BeginScene());
    // 200x200 quad showing u,v in [-0.5, 1.5]: the middle half is the texture, the rest border.
    Rect(100, 100, 300, 300, 0.5f, 0xFFFFFFFF, -0.5f, -0.5f, 1.5f, 1.5f);
    // Second quad: border only in U (V clamps).
    g_dev->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    g_dev->SetTextureStageState(0, D3DTSS_BORDERCOLOR, 0xFF0000FF);
    Rect(340, 100, 540, 300, 0.5f, 0xFFFFFFFF, -0.5f, -0.5f, 1.5f, 1.5f);
    CHECK_HR(g_dev->EndScene());
    Grab();
    Expect("center is the texture", 200, 200, 0xFF0000);
    Expect("left outside is the border color", 110, 200, 0x00FF00);
    Expect("above is the border color", 200, 110, 0x00FF00);
    Expect("corner is the border color", 110, 110, 0x00FF00);
    Expect("U-only border: center is the texture", 440, 200, 0xFF0000);
    Expect("U-only border: left is the border", 350, 200, 0x0000FF);
    Expect("U-only border: top row clamps to the texture", 440, 110, 0xFF0000);
    g_dev->SetTexture(0, nullptr);
    tex->Release();
}

//------------------------------------------------------------------------------
// Case: flat shading uses the first vertex of the triangle
//------------------------------------------------------------------------------
struct CV { float x, y, z, rhw; DWORD color; };
#define CV_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

static void CaseFlat(const char *name)
{
    g_case = name;
    BaseStates();
    ClearScreen();
    g_dev->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_FLAT);
    g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    g_dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    g_dev->SetVertexShader(CV_FVF);
    CHECK_HR(g_dev->BeginScene());
    // Triangle list: first vertex red decides the color of the whole triangle.
    CV list[6] = {{20, 20, 0.5f, 1, 0xFFFF0000}, {20, 120, 0.5f, 1, 0xFF00FF00}, {120, 120, 0.5f, 1, 0xFF0000FF},
                  {140, 20, 0.5f, 1, 0xFFFFFF00}, {140, 120, 0.5f, 1, 0xFF00FFFF}, {240, 120, 0.5f, 1, 0xFFFF00FF}};
    CHECK_HR(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, list, sizeof(CV)));
    // Strip: triangle p takes the color of vertex p.
    CV strip[5] = {{20, 200, 0.5f, 1, 0xFFFF0000}, {20, 300, 0.5f, 1, 0xFF00FF00}, {70, 200, 0.5f, 1, 0xFF0000FF},
                   {70, 300, 0.5f, 1, 0xFFFFFF00}, {120, 200, 0.5f, 1, 0xFFFF00FF}};
    CHECK_HR(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 3, strip, sizeof(CV)));
    // Indexed list through the same path.
    CV iv[3] = {{300, 20, 0.5f, 1, 0xFFFFFFFF}, {300, 120, 0.5f, 1, 0xFF000000}, {400, 120, 0.5f, 1, 0xFF808080}};
    WORD idx[3] = {1, 2, 0}; // first index selects the black vertex
    CHECK_HR(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 3, 1, idx, D3DFMT_INDEX16, iv, sizeof(CV)));
    CHECK_HR(g_dev->EndScene());
    Grab();
    Expect("list triangle 0 is red", 40, 100, 0xFF0000);
    Expect("list triangle 1 is yellow", 160, 100, 0xFFFF00);
    // Strip triangle 0: (v0,v1,v2) covers x 20..70 left part; triangle 1: (v2,v1,v3) middle; triangle 2: (v2,v3,v4).
    Expect("strip triangle 0 is red (vertex 0)", 30, 220, 0xFF0000);
    Expect("strip triangle 2 is blue (vertex 2)", 85, 215, 0x0000FF);
    Expect("indexed triangle takes the first index (black)", 310, 110, 0x000000);
}

//------------------------------------------------------------------------------
// Case: ps.1.2 / 1.3 / 1.4 texture instructions that were missing
//------------------------------------------------------------------------------
static void CasePixelShaderOps()
{
    g_case = "ps_ops";
    BaseStates();
    ClearScreen();

    // texdp3: t1 = dot(coords of stage 1, t0.rgb) in all channels.
    {
        IDirect3DTexture8 *t0 = MakeSolid(2, 2, 0xFF3366CC); // r=0.2 g=0.4 b=0.8
        DWORD ps = Assemble("ps.1.2\ntex t0\ntexdp3 t1, t0\nmov r0, t1\n", false);
        g_dev->SetTexture(0, t0);
        g_dev->SetPixelShader(ps);
        CHECK_HR(g_dev->BeginScene());
        const float tl[4][3] = {{0, 0, 0}, {1, 0.5f, 0.25f}, {0, 0, 0}, {0, 0, 0}};
        RectUV(10, 10, 60, 60, 0.5f, 0xFFFFFFFF, tl, tl);
        CHECK_HR(g_dev->EndScene());
        g_dev->SetPixelShader(0);
        g_dev->DeletePixelShader(ps);
        t0->Release();
        Grab();
        // dot((1,0.5,0.25), (0.2,0.4,0.8)) = 0.2 + 0.2 + 0.2 = 0.6
        Expect("texdp3 replicates the dot product", 35, 35, Pack(0.6f, 0.6f, 0.6f), 4);
    }
    // texdp3tex: the dot product is the u coordinate of a 1D lookup into stage 1's texture.
    {
        IDirect3DTexture8 *t0 = MakeSolid(2, 2, 0xFF808080); // 0.5 gray
        IDirect3DTexture8 *ramp = nullptr;
        CHECK_HR(g_dev->CreateTexture(4, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &ramp));
        D3DLOCKED_RECT lr;
        CHECK_HR(ramp->LockRect(0, &lr, nullptr, 0));
        const DWORD ramp4[4] = {0xFF200000, 0xFF004000, 0xFF000080, 0xFFC0C0C0};
        memcpy(lr.pBits, ramp4, 16);
        CHECK_HR(ramp->UnlockRect(0));
        DWORD ps = Assemble("ps.1.2\ntex t0\ntexdp3tex t1, t0\nmov r0, t1\n", false);
        g_dev->SetTexture(0, t0);
        g_dev->SetTexture(1, ramp);
        g_dev->SetPixelShader(ps);
        CHECK_HR(g_dev->BeginScene());
        // dot((1.2, 0, 0), (0.5,0.5,0.5)) = 0.6 -> texel floor(0.6*4) = 2
        const float tl[4][3] = {{0, 0, 0}, {1.2f, 0, 0}, {0, 0, 0}, {0, 0, 0}};
        RectUV(70, 10, 120, 60, 0.5f, 0xFFFFFFFF, tl, tl);
        CHECK_HR(g_dev->EndScene());
        g_dev->SetPixelShader(0);
        g_dev->SetTexture(1, nullptr);
        g_dev->DeletePixelShader(ps);
        t0->Release();
        ramp->Release();
        Grab();
        Expect("texdp3tex looks up texel 2", 95, 35, 0x000080, 4);
    }
    // texm3x3 with an identity matrix returns the texel; alpha is 1.
    {
        IDirect3DTexture8 *t0 = MakeSolid(2, 2, 0xFF4080C0);
        DWORD ps = Assemble("ps.1.2\ntex t0\ntexm3x3pad t1, t0\ntexm3x3pad t2, t0\ntexm3x3 t3, t0\nmov r0, t3\n", false);
        g_dev->SetTexture(0, t0);
        g_dev->SetPixelShader(ps);
        CHECK_HR(g_dev->BeginScene());
        const float tl[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        RectUV(130, 10, 180, 60, 0.5f, 0xFFFFFFFF, tl, tl);
        CHECK_HR(g_dev->EndScene());
        g_dev->SetPixelShader(0);
        g_dev->DeletePixelShader(ps);
        t0->Release();
        Grab();
        Expect("texm3x3 identity matrix", 155, 35, 0x4080C0, 4);
    }
    // texm3x3 with a scaling matrix: rows (2,0,0), (0,1,0), (0,0,0.5).
    {
        IDirect3DTexture8 *t0 = MakeSolid(2, 2, 0xFF204060);
        DWORD ps = Assemble("ps.1.2\ntex t0\ntexm3x3pad t1, t0\ntexm3x3pad t2, t0\ntexm3x3 t3, t0\nmov r0, t3\n", false);
        g_dev->SetTexture(0, t0);
        g_dev->SetPixelShader(ps);
        CHECK_HR(g_dev->BeginScene());
        const float tl[4][3] = {{0, 0, 0}, {2, 0, 0}, {0, 1, 0}, {0, 0, 0.5f}};
        RectUV(190, 10, 240, 60, 0.5f, 0xFFFFFFFF, tl, tl);
        CHECK_HR(g_dev->EndScene());
        g_dev->SetPixelShader(0);
        g_dev->DeletePixelShader(ps);
        t0->Release();
        Grab();
        Expect("texm3x3 scaling matrix", 215, 35, Pack(0.25f, 0.25f, 0.188f), 5);
    }

    // texm3x2depth: depth = dot0 / dot1 = 0.2 -> the green quad written at z 0.9 ends up in front of z 0.5.
    {
        g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        IDirect3DTexture8 *t0 = MakeSolid(2, 2, 0xFF800000); // r = 0.5 (0x80)
        DWORD ps = Assemble("ps.1.3\ntex t0\ntexm3x2pad t1, t0\ntexm3x2depth t2, t0\nmov r0, c0\n", false);
        const float green[4] = {0, 1, 0, 1};
        g_dev->SetPixelShaderConstant(0, green, 1);
        g_dev->SetTexture(0, t0);
        g_dev->SetPixelShader(ps);
        CHECK_HR(g_dev->BeginScene());
        // dot0 = 0.4 * 0.5 = 0.2, dot1 = 2.0 * 0.5 = 1.0 -> depth 0.2
        const float tl[4][3] = {{0, 0, 0}, {0.4f, 0, 0}, {2.0f, 0, 0}, {0, 0, 0}};
        RectUV(10, 100, 60, 150, 0.9f, 0xFFFFFFFF, tl, tl);
        g_dev->SetPixelShader(0);
        g_dev->SetTexture(0, nullptr);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        g_dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        Rect(10, 100, 60, 150, 0.5f, 0xFFFF0000);   // behind the written depth 0.2: must fail the depth test
        Rect(70, 100, 120, 150, 0.5f, 0xFFFF0000);  // control: nothing written there
        CHECK_HR(g_dev->EndScene());
        g_dev->DeletePixelShader(ps);
        t0->Release();
        g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        Grab();
        Expect("texm3x2depth overwrites the depth (red rejected, green stays)", 35, 125, 0x00FF00);
        Expect("control quad without depth write is red", 95, 125, 0xFF0000);
    }
    // texdepth (ps.1.4): depth = r5.r / r5.g = 0.25 / 0.5 = 0.5 replaces the geometric depth of the quad.
    {
        g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        ClearScreen();
        DWORD ps = Assemble("ps.1.4\ndef c1, 0.25, 0.5, 0.0, 0.0\nmov r5, c1\ntexdepth r5\nmov r0, c0\n", false);
        const float blue[4] = {0, 0, 1, 1};
        g_dev->SetPixelShaderConstant(0, blue, 1);
        g_dev->SetPixelShader(ps);
        CHECK_HR(g_dev->BeginScene());
        Rect(10, 200, 60, 250, 0.1f, 0xFFFFFFFF);   // geometric z 0.1, shader depth 0.5
        g_dev->SetPixelShader(0);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        g_dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        Rect(10, 200, 60, 250, 0.3f, 0xFFFF0000);   // 0.3 < 0.5: in front of the written depth (it would lose against 0.1)
        Rect(70, 200, 120, 250, 0.3f, 0xFFFF0000);  // control
        Rect(10, 260, 60, 310, 0.1f, 0xFFFFFFFF);   // second pair: shader depth 0.5 versus a quad at 0.6 behind it
        CHECK_HR(g_dev->EndScene());
        g_dev->DeletePixelShader(ps);
        g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        Grab();
        Expect("texdepth: shader depth 0.5 replaces geometric 0.1 (z 0.3 is in front)", 35, 225, 0xFF0000);
    }
    {
        g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        ClearScreen();
        DWORD ps = Assemble("ps.1.4\ndef c1, 0.25, 0.5, 0.0, 0.0\nmov r5, c1\ntexdepth r5\nmov r0, c0\n", false);
        const float blue[4] = {0, 0, 1, 1};
        g_dev->SetPixelShaderConstant(0, blue, 1);
        g_dev->SetPixelShader(ps);
        CHECK_HR(g_dev->BeginScene());
        Rect(10, 200, 60, 250, 0.9f, 0xFFFFFFFF);   // geometric z 0.9, shader depth 0.5
        g_dev->SetPixelShader(0);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        g_dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        Rect(10, 200, 60, 250, 0.7f, 0xFFFF0000);   // 0.7 > 0.5: behind the written depth, rejected (it would pass against 0.9)
        Rect(70, 200, 120, 250, 0.7f, 0xFFFF0000);  // control
        CHECK_HR(g_dev->EndScene());
        g_dev->DeletePixelShader(ps);
        g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        Grab();
        Expect("texdepth: z 0.7 is behind the shader depth 0.5 (blue stays)", 35, 225, 0x0000FF);
        Expect("texdepth: control quad is red", 95, 225, 0xFF0000);
    }
    g_dev->SetPixelShader(0);
}

//------------------------------------------------------------------------------
// Case: D3DXFilterTexture filter types
//------------------------------------------------------------------------------
static DWORD Level1Pixel(IDirect3DTexture8 *tex, UINT x, UINT y)
{
    D3DLOCKED_RECT lr;
    DWORD v = 0;
    if (SUCCEEDED(tex->LockRect(1, &lr, nullptr, D3DLOCK_READONLY)))
    {
        v = ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x];
        tex->UnlockRect(1);
    }
    return v;
}

static void CaseFilters()
{
    g_case = "d3dx_filters";
    static const DWORD kFilters[] = {D3DX_FILTER_POINT, D3DX_FILTER_LINEAR, D3DX_FILTER_TRIANGLE, D3DX_FILTER_BOX};
    static const char *kNames[] = {"POINT", "LINEAR", "TRIANGLE", "BOX"};
    for (int f = 0; f < 4; ++f)
    {
        // 4x4 level 0: x gradient 0,64,128,192 in red, y gradient in green.
        IDirect3DTexture8 *tex = nullptr;
        CHECK_HR(g_dev->CreateTexture(4, 4, 2, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex));
        D3DLOCKED_RECT lr;
        CHECK_HR(tex->LockRect(0, &lr, nullptr, 0));
        for (UINT y = 0; y < 4; ++y)
            for (UINT x = 0; x < 4; ++x)
                ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = 0xFF000000 | ((x * 64) << 16) | ((y * 64) << 8);
        CHECK_HR(tex->UnlockRect(0));
        CHECK_HR(D3DXFilterTexture(tex, nullptr, 0, kFilters[f]));
        const DWORD p = Level1Pixel(tex, 0, 0);
        const int r = (p >> 16) & 255, g = (p >> 8) & 255;
        char label[64];
        snprintf(label, sizeof label, "%s level 1 texel (0,0)", kNames[f]);
        // BOX/TRIANGLE: average of the 2x2 block = (32, 32). LINEAR: bilinear at the block center = same
        // average. POINT: one of the four texels.
        bool ok;
        if (f == 0) ok = (r == 0 || r == 64) && (g == 0 || g == 64);
        else ok = std::abs(r - 32) <= 1 && std::abs(g - 32) <= 1;
        Report(label, ok, "got r=%d g=%d", r, g);
        tex->Release();
    }
    // Non-power-of-two style source (3x3 -> 1x1 through a 3 level chain is not possible in D3D8; use 8x2 -> 4x1):
    {
        IDirect3DTexture8 *tex = nullptr;
        CHECK_HR(g_dev->CreateTexture(8, 2, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex));
        D3DLOCKED_RECT lr;
        CHECK_HR(tex->LockRect(0, &lr, nullptr, 0));
        for (UINT y = 0; y < 2; ++y)
            for (UINT x = 0; x < 8; ++x) ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = (x & 1) ? 0xFFFFFFFF : 0xFF000000;
        CHECK_HR(tex->UnlockRect(0));
        CHECK_HR(D3DXFilterTexture(tex, nullptr, 0, D3DX_FILTER_BOX));
        const DWORD p = Level1Pixel(tex, 1, 0);
        Report("BOX averages the high-contrast pairs", std::abs((int)(p & 255) - 128) <= 1, "got %02x", (unsigned)(p & 255));
        tex->Release();
    }
    // D3DXLoadSurfaceFromSurface with NONE: the overlapping region is copied, the rest is transparent black.
    {
        IDirect3DSurface8 *src = nullptr, *dst = nullptr;
        CHECK_HR(g_dev->CreateImageSurface(4, 4, D3DFMT_A8R8G8B8, &src));
        CHECK_HR(g_dev->CreateImageSurface(8, 8, D3DFMT_A8R8G8B8, &dst));
        D3DLOCKED_RECT lr;
        CHECK_HR(src->LockRect(&lr, nullptr, 0));
        for (UINT y = 0; y < 4; ++y)
            for (UINT x = 0; x < 4; ++x) ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = 0xFFFF8040;
        CHECK_HR(src->UnlockRect());
        CHECK_HR(dst->LockRect(&lr, nullptr, 0));
        for (UINT y = 0; y < 8; ++y)
            for (UINT x = 0; x < 8; ++x) ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = 0xFF00FF00;
        CHECK_HR(dst->UnlockRect());
        CHECK_HR(D3DXLoadSurfaceFromSurface(dst, nullptr, nullptr, src, nullptr, nullptr, D3DX_FILTER_NONE, 0));
        CHECK_HR(dst->LockRect(&lr, nullptr, D3DLOCK_READONLY));
        const DWORD inside = ((DWORD *)((BYTE *)lr.pBits + 1 * lr.Pitch))[1];
        const DWORD outside = ((DWORD *)((BYTE *)lr.pBits + 6 * lr.Pitch))[6];
        CHECK_HR(dst->UnlockRect());
        Report("FILTER_NONE copies the overlap", inside == 0xFFFF8040, "got %08x", (unsigned)inside);
        Report("FILTER_NONE leaves transparent black outside", outside == 0x00000000, "got %08x", (unsigned)outside);
        src->Release();
        dst->Release();
    }
}

//------------------------------------------------------------------------------
// Case: state caching (redundant state changes must not reach WebGL)
//------------------------------------------------------------------------------
static void CaseStateCache()
{
    g_case = "state_cache";
    BaseStates();
    ClearScreen();
    IDirect3DTexture8 *tex = MakeSolid(4, 4, 0xFFC0C0C0);
    // 40 quads in one vertex buffer, drawn with SetIndices(ib, 4 * i): the base vertex differs per draw,
    // like the sub-allocations of a dynamic vertex buffer pool.
    IDirect3DVertexBuffer8 *vb = nullptr;
    IDirect3DIndexBuffer8 *ib = nullptr;
    CHECK_HR(g_dev->CreateVertexBuffer(40 * 4 * sizeof(LV), D3DUSAGE_WRITEONLY, LV_FVF, D3DPOOL_MANAGED, &vb));
    CHECK_HR(g_dev->CreateIndexBuffer(6 * 2, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib));
    LV *v;
    CHECK_HR(vb->Lock(0, 0, (BYTE **)&v, 0));
    for (int i = 0; i < 40; ++i)
    {
        const float x0 = (float)(i * 10) - 0.5f, x1 = x0 + 8, y0 = 9.5f, y1 = 17.5f;
        v[i * 4 + 0] = {x0, y0, 0.5f, 1, 0x80FFFFFF, 0, 0};
        v[i * 4 + 1] = {x0, y1, 0.5f, 1, 0x80FFFFFF, 0, 1};
        v[i * 4 + 2] = {x1, y1, 0.5f, 1, 0x80FFFFFF, 1, 1};
        v[i * 4 + 3] = {x1, y0, 0.5f, 1, 0x80FFFFFF, 1, 0};
    }
    CHECK_HR(vb->Unlock());
    WORD *ix;
    CHECK_HR(ib->Lock(0, 0, (BYTE **)&ix, 0));
    const WORD idx[6] = {0, 1, 2, 0, 2, 3};
    memcpy(ix, idx, sizeof idx);
    CHECK_HR(ib->Unlock());
    auto frame = [&](bool redundant) {
        CHECK_HR(g_dev->BeginScene());
        g_dev->SetStreamSource(0, vb, sizeof(LV));
        g_dev->SetVertexShader(LV_FVF);
        for (int i = 0; i < 40; ++i)
        {
            g_dev->SetTexture(0, tex);
            g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            g_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            g_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            if (redundant)
            {
                g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
                D3DXMATRIX id;
                D3DXMatrixIdentity(&id);
                g_dev->SetTransform(D3DTS_WORLD, &id);
                g_dev->SetTransform(D3DTS_VIEW, &id);
            }
            g_dev->SetIndices(ib, i * 4);
            CHECK_HR(g_dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2));
        }
        CHECK_HR(g_dev->EndScene());
        CHECK_HR(g_dev->Present(nullptr, nullptr, nullptr, nullptr));
    };
    // The first frames set up programs, buffers and vertex arrays.
    for (int i = 0; i < 4; ++i) frame(false);
    dxwebgl2_Stats a, b;
    frame(false);
    dxwebgl2_GetStats(&a);
    frame(true);
    dxwebgl2_GetStats(&b);
    Report("40 draws counted", a.draws == 40, "draws=%u", a.draws);
    Report("redundant D3D state calls add no WebGL calls", b.glCalls == a.glCalls, "plain %u redundant %u", a.glCalls, b.glCalls);
    Report("repeated geometry: <= 3 WebGL calls per draw", a.glCalls <= 40 * 3 + 12, "%u calls for 40 draws (draw %u state %u bind %u uniform %u upload %u attrib %u other %u)",
           a.glCalls, a.glDraw, a.glState, a.glBind, a.glUniform, a.glUpload, a.glAttrib, a.glOther);
    Grab();
    Expect("the quads were drawn", 5, 13, 0xC0C0C0, 4);
    Expect("a later quad (base vertex 28) was drawn", 74, 13, 0xC0C0C0, 4);
    Expect("the last quad (base vertex 156) was drawn", 394, 13, 0xC0C0C0, 4);
    Expect("between quads is untouched", 9, 13, 0x101010, 2);
    g_dev->SetTexture(0, nullptr);
    g_dev->SetStreamSource(0, nullptr, 0);
    g_dev->SetIndices(nullptr, 0);
    tex->Release();
    vb->Release();
    ib->Release();
}

//------------------------------------------------------------------------------
// Case: diagnostics counters
//------------------------------------------------------------------------------
static void CaseDiagnostics()
{
    g_case = "diagnostics";
    BaseStates();
    ClearScreen();
    CHECK_HR(g_dev->BeginScene());
    g_dev->SetRenderState(D3DRS_WRAP0, D3DWRAP_U | D3DWRAP_V);
    g_dev->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_1WEIGHTS);
    Rect(10, 10, 50, 50, 0.5f, 0xFFFFFFFF);
    g_dev->SetRenderState(D3DRS_WRAP0, 0);
    g_dev->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
    g_dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
    Rect(60, 10, 100, 50, 0.5f, 0xFFFFFFFF);
    g_dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    CHECK_HR(g_dev->EndScene());
    Report("WRAP0 hit counted", dxwebgl2_GetHitCount("WRAP0") >= 1, "%u", dxwebgl2_GetHitCount("WRAP0"));
    Report("VERTEXBLEND hit counted", dxwebgl2_GetHitCount("VERTEXBLEND") >= 1);
    Report("WIREFRAME hit counted", dxwebgl2_GetHitCount("WIREFRAME") >= 1);
    Report("nothing recorded for supported features", dxwebgl2_GetHitCount("ZBIAS") == 0 && dxwebgl2_GetHitCount("LIGHT") == 0);
    // A failing create is counted.
    IDirect3DTexture8 *t = nullptr;
    HRESULT hr = g_dev->CreateTexture(16, 16, 1, 0, D3DFMT_P8, D3DPOOL_MANAGED, &t);
    Report("P8 texture refused", FAILED(hr));
    Report("failed create counted", dxwebgl2_GetHitCount("CreateTexture") >= 1);
    dxwebgl2_PrintReport();
}

//------------------------------------------------------------------------------
// Inline shader sources (assembled at run time)
//------------------------------------------------------------------------------
/// The shader sources of this test (equivalents of the shaders that applications typically ship), by name.
static const char *InlineShaderSource(const std::string &name)
{
    static const struct { const char *name; const char *src; } kSources[] = {
        {"modulate2x.psh", "ps.1.1\ntex t0\nmov r0, v0\nmul r0, r0, t0\nmul_x2 r0.rgb, r0, c1\nmov r0.a, t0.a\n"},
        {"blend2_lit.psh", "ps.1.1\ntex t0\ntex t1\nmul r0, t1, t0\nmul r0, r0, v0\n"},
        {"single_lit.psh", "ps.1.1\ntex t1\nmov r0, t1\nmul r0, r0, v0\n"},
        {"blend2_lit_mod1.psh", "ps.1.1\ntex t0\ntex t1\ntex t2\nmul r0, t1, t0\nmul r0, r0, v0\nmul r0, r0, t2\n"},
        {"blend2_lit_mod2.psh", "ps.1.1\ntex t0\ntex t1\ntex t2\ntex t3\nmul r0, t1, t0\nmul r0, r0, v0\nmul r0, r0, t2\nmul r0, r0, t3\n"},
        {"inv_mono.psh", "ps.1.1\ntex t0\ndp3 r1, t0, c0\nmul r1, r1, c1\nlrp r0, c2, 1-r1, t0\n"},
        {"mono.psh", "ps.1.1\ntex t0\ndp3 r1, t0, c0\nmul r1, r1, c1\nlrp r0, c2, r1, t0\n"},
        {"bem_blend.psh", "ps.1.1\ntex t0\ntex t1\ntexbem t2, t1\ntex t3\nmul r0.rgb, v0, t0\n+add r0.a, v0, t0\n"
                          "mul r1, t2, c0\nadd r0.rgb, r0, r1\n+mul r0.a, r0, t3\n"},
        {"three_mod_lit.psh", "ps.1.1\ntex t0\ntex t1\ntex t2\nmul r0, t0, t1\nmul r0, r0, t2\nmul r0, r0, v0\n"},
        {"lrp_lit.psh", "ps.1.1\ntex t0\ntex t1\nlrp r0, v0.a, t1, t0\nmul r0, r0, v0\n"},
        {"lrp_lit_mod1.psh", "ps.1.1\ntex t0\ntex t1\ntex t2\nlrp r0, v0.a, t1, t0\nmul r0, r0, v0\nmul r0, r0, t2\n"},
        {"lrp_lit_mod2.psh", "ps.1.1\ntex t0\ntex t1\ntex t2\ntex t3\nlrp r0, v0.a, t1, t0\nmul r0, r0, v0\nmul r0, r0, t2\nmul r0, r0, t3\n"},
        {"bem.psh", "ps.1.1\ntex t0\ntexbem t1, t0\n#ifndef WITH_ALPHA_TEXTURE\nmul r0, t1, v0\n#else\ntex t2\n"
                    "mul r0.rgb, t1, v0\n+mul r0.a, t2, v0\n#endif\n"},
        // Sway: skew by a per-instance table entry selected with a0 (relative constant addressing), scale the colour.
        {"wind_sway.vsh",
         "vs.1.1\nmov r2, v1.wwzw\nsub r2, v0, r2\nmov a0.x, v1\nmov r0, c[a0.x+8]\nmad r1, r2.zzzw, r0, v0\nm4x4 oPos, r1, c4\n"
         "mov r2, v1.yyyw\nmul oD0, v2, r2\nmov oT0, v7\nadd r1, v0, c32\nmul oT1, r1, c33\n"},
        // Projective texture coordinates: dp4 transform, rcp, mad with write masks.
        {"proj_texgen.vsh",
         "#define POSITION v0\n#define DIFFUSE v1\n#define TEXCOORD v2\n#define ROW0 2\n#define ROW1 3\n#define ROW2 4\n#define ROW3 5\n#define PROJ_ROW 6\n"
         "vs.1.1\ndp4 r0.x, POSITION, c[ROW0]\ndp4 r0.y, POSITION, c[ROW1]\ndp4 r0.z, POSITION, c[ROW2]\n"
         "dp4 r0.w, POSITION, c[ROW3]\nmov oPos, r0\nrcp r1.w, r0.w\nmul r1.xy, r0.xy, r1.w\n"
         "mad oT1.xy, r1.xy, c[PROJ_ROW].xy, c[PROJ_ROW].zw\nmov oT0, TEXCOORD\nmov oD0, DIFFUSE\n"},
    };
    for (const auto &e : kSources)
        if (name == e.name) return e.src;
    return nullptr;
}

static std::string ReadShaderFile(const char *name)
{
    const char *src = InlineShaderSource(name);
    if (!src) { printf("missing shader source %s\n", name); return ""; }
    return src;
}

/// A tiny NVASM-style preprocessor: #define NAME value, #ifdef/#ifndef/#else/#endif and the replacement of
/// the defined names (shader sources use it for constant register names).
static std::string Preprocess(const std::string &src, const std::vector<std::string> &defines)
{
    std::vector<std::pair<std::string, std::string>> macros;
    for (const std::string &d : defines) macros.push_back({d, "1"});
    std::vector<bool> active{true};
    std::string out;
    size_t pos = 0;
    while (pos <= src.size())
    {
        size_t nl = src.find('\n', pos);
        std::string line = src.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? src.size() + 1 : nl + 1;
        size_t s = line.find_first_not_of(" \t\r");
        if (s != std::string::npos && line[s] == '#')
        {
            char dir[16] = "", a[64] = "", b[64] = "";
            sscanf(line.c_str() + s + 1, "%15s %63s %63s", dir, a, b);
            if (!strcmp(dir, "define")) { if (active.back()) macros.push_back({a, b}); }
            else if (!strcmp(dir, "ifdef") || !strcmp(dir, "ifndef"))
            {
                bool defined = false;
                for (auto &m : macros) if (m.first == a) defined = true;
                active.push_back(active.back() && (!strcmp(dir, "ifdef") ? defined : !defined));
            }
            else if (!strcmp(dir, "else")) { bool prev = active.size() > 1 ? active[active.size() - 2] : true; active.back() = prev && !active.back(); }
            else if (!strcmp(dir, "endif")) { if (active.size() > 1) active.pop_back(); }
            out += "\n";
            continue;
        }
        if (!active.back()) { out += "\n"; continue; }
        // Replace whole-word macro names.
        std::string res;
        for (size_t i = 0; i < line.size();)
        {
            if (isalpha((unsigned char)line[i]) || line[i] == '_')
            {
                size_t j = i;
                while (j < line.size() && (isalnum((unsigned char)line[j]) || line[j] == '_')) ++j;
                std::string word = line.substr(i, j - i);
                bool done = false;
                for (auto &m : macros)
                    if (m.first == word) { res += m.second; done = true; break; }
                if (!done) res += word;
                i = j;
            }
            else res += line[i++];
        }
        out += res + "\n";
    }
    return out;
}

static DWORD AssembleFile(const char *file, bool vertex, const DWORD *decl, const std::vector<std::string> &defs = {})
{
    std::string src = Preprocess(ReadShaderFile(file), defs);
    if (src.empty()) return 0;
    return Assemble(src, vertex, decl);
}

static void CaseInlineShaders()
{
    g_case = "inline_shaders";
    // Every pixel shader source must assemble, translate and link; the arithmetic ones are
    // checked against a hand computation.
    static const char *kPixel[] = {"modulate2x.psh", "blend2_lit.psh", "single_lit.psh", "blend2_lit_mod1.psh", "blend2_lit_mod2.psh",
                                   "inv_mono.psh", "mono.psh", "bem_blend.psh", "three_mod_lit.psh", "lrp_lit.psh",
                                   "lrp_lit_mod1.psh", "lrp_lit_mod2.psh", "bem.psh"};
    BaseStates();
    IDirect3DTexture8 *ta = MakeSolid(2, 2, 0xFF804020); // 0.502, 0.251, 0.125
    IDirect3DTexture8 *tb = MakeSolid(2, 2, 0xFF2080C0); // 0.125, 0.502, 0.753
    IDirect3DTexture8 *tc = MakeSolid(2, 2, 0xFFC0C0C0); // 0.753
    IDirect3DTexture8 *td = MakeSolid(2, 2, 0xFF808080); // 0.502
    IDirect3DTexture8 *bump = nullptr;
    CHECK_HR(g_dev->CreateTexture(2, 2, 1, 0, D3DFMT_V8U8, D3DPOOL_MANAGED, &bump));
    if (bump)
    {
        D3DLOCKED_RECT lr;
        CHECK_HR(bump->LockRect(0, &lr, nullptr, 0));
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x) { signed char *p = (signed char *)((BYTE *)lr.pBits + y * lr.Pitch) + x * 2; p[0] = 0; p[1] = 0; }
        CHECK_HR(bump->UnlockRect(0));
    }
    const float ca = 0.502f, cb = 0.251f, cc = 0.125f;
    (void)ca; (void)cb; (void)cc;

    struct Expect1 { const char *file; int x; };
    int col = 0;
    ClearScreen();
    CHECK_HR(g_dev->BeginScene());
    const DWORD diffuse = 0xFFA0C0E0; // v0: r 0.627 g 0.753 b 0.878 a 1.0 -> lrp factor v0.a = 1.0
    for (const char *file : kPixel)
    {
        DWORD ps = AssembleFile(file, false, nullptr, {});
        char label[96];
        snprintf(label, sizeof label, "%s assembles and links", file);
        if (!ps) { Report(label, false, "(assemble/create failed)"); continue; }
        g_dev->SetTexture(0, ta);
        g_dev->SetTexture(1, !strcmp(file, "bem.psh") || !strcmp(file, "bem_blend.psh") ? bump : tb);
        g_dev->SetTexture(2, tc);
        g_dev->SetTexture(3, td);
        const float k0[4] = {0.299f, 0.587f, 0.114f, 0}, k1[4] = {1, 1, 1, 1}, k2[4] = {0.5f, 0.5f, 0.5f, 0.5f};
        g_dev->SetPixelShaderConstant(0, k0, 1);
        g_dev->SetPixelShaderConstant(1, k1, 1);
        g_dev->SetPixelShaderConstant(2, k2, 1);
        g_dev->SetPixelShader(ps);
        const float x0 = 8.0f + col * 40.0f;
        Rect(x0, 400, x0 + 32, 432, 0.5f, diffuse);
        ++col;
        g_dev->SetPixelShader(0);
        // Probe after the frame: remember the expected value per shader.
        (void)label;
    }
    CHECK_HR(g_dev->EndScene());
    Grab();
    // Expected colors, computed by hand from the sources (diffuse v0 = (0.627, 0.753, 0.878, 1.0)):
    const float v[4] = {0xA0 / 255.0f, 0xC0 / 255.0f, 0xE0 / 255.0f, 1.0f};
    const float A[3] = {0x80 / 255.0f, 0x40 / 255.0f, 0x20 / 255.0f}, B[3] = {0x20 / 255.0f, 0x80 / 255.0f, 0xC0 / 255.0f};
    const float C = 0xC0 / 255.0f, D = 0x80 / 255.0f;
    struct Want { int index; float r, g, b; const char *what; };
    auto prod = [&](float a, float b2) { return a * b2; };
    (void)prod;
    std::vector<Want> wants;
    // index: position in kPixel
    // modulate2x.psh: r0 = v0 * t0; r0.rgb = r0 * c1 * 2 (c1 = 1) -> clamp
    wants.push_back({0, Clampv(v[0] * A[0] * 2, 0.f, 1.f), Clampv(v[1] * A[1] * 2, 0.f, 1.f), Clampv(v[2] * A[2] * 2, 0.f, 1.f), "modulate2x.psh"});
    // blend2_lit.psh: t1 * t0 * v0
    wants.push_back({1, B[0] * A[0] * v[0], B[1] * A[1] * v[1], B[2] * A[2] * v[2], "blend2_lit.psh"});
    // single_lit.psh: t1 * v0
    wants.push_back({2, B[0] * v[0], B[1] * v[1], B[2] * v[2], "single_lit.psh"});
    // blend2_lit_mod1.psh: t1 * t0 * v0 * t2
    wants.push_back({3, B[0] * A[0] * v[0] * C, B[1] * A[1] * v[1] * C, B[2] * A[2] * v[2] * C, "blend2_lit_mod1.psh"});
    // blend2_lit_mod2.psh: ... * t2 * t3
    wants.push_back({4, B[0] * A[0] * v[0] * C * D, B[1] * A[1] * v[1] * C * D, B[2] * A[2] * v[2] * C * D, "blend2_lit_mod2.psh"});
    // inv_mono.psh: r1 = dot3(t0, c0) * c1; r0 = lrp(c2, 1-r1, t0) = 0.5*(1-r1) + 0.5*t0
    {
        const float g = A[0] * 0.299f + A[1] * 0.587f + A[2] * 0.114f;
        wants.push_back({5, 0.5f * (1 - g) + 0.5f * A[0], 0.5f * (1 - g) + 0.5f * A[1], 0.5f * (1 - g) + 0.5f * A[2], "inv_mono.psh"});
        wants.push_back({6, 0.5f * g + 0.5f * A[0], 0.5f * g + 0.5f * A[1], 0.5f * g + 0.5f * A[2], "mono.psh"});
    }
    // three_mod_lit.psh: t0 * t1 * t2 * v0 (t0 = A, t1 = B, t2 = C)
    wants.push_back({8, A[0] * B[0] * C * v[0], A[1] * B[1] * C * v[1], A[2] * B[2] * C * v[2], "three_mod_lit.psh"});
    // lrp_lit.psh: lrp(v0.a=1, t1, t0) * v0 = t1 * v0
    wants.push_back({9, B[0] * v[0], B[1] * v[1], B[2] * v[2], "lrp_lit.psh"});
    // lrp_lit_mod1.psh: t1 * v0 * t2
    wants.push_back({10, B[0] * v[0] * C, B[1] * v[1] * C, B[2] * v[2] * C, "lrp_lit_mod1.psh"});
    // lrp_lit_mod2.psh: t1 * v0 * t2 * t3
    wants.push_back({11, B[0] * v[0] * C * D, B[1] * v[1] * C * D, B[2] * v[2] * C * D, "lrp_lit_mod2.psh"});
    for (const Want &w : wants)
    {
        char label[96];
        snprintf(label, sizeof label, "%s arithmetic", w.what);
        Expect(label, 8 + w.index * 40 + 16, 416, Pack(w.r, w.g, w.b), 6);
    }
    g_dev->SetPixelShader(0);
    for (int s = 0; s < 4; ++s) g_dev->SetTexture(s, nullptr);
    ta->Release(); tb->Release(); tc->Release(); td->Release();
    if (bump) bump->Release();
}

static void CaseInlineVertexShaders()
{
    g_case = "inline_vshaders";
    BaseStates();
    ClearScreen();
    IDirect3DTexture8 *tex = MakeSolid(2, 2, 0xFFFFFFFF);

    // wind_sway.vsh: v0 position, v1 wave data, v2 diffuse, v7 texture; relative addressing c[a0.x+8].
    {
        DWORD decl[] = {D3DVSD_STREAM(0), D3DVSD_REG(0, D3DVSDT_FLOAT3), D3DVSD_REG(1, D3DVSDT_FLOAT4), D3DVSD_REG(2, D3DVSDT_D3DCOLOR),
                        D3DVSD_REG(7, D3DVSDT_FLOAT2), D3DVSD_END()};
        DWORD vs = AssembleFile("wind_sway.vsh", true, decl);
        Report("wind_sway.vsh creates", vs != 0);
        struct TV { float x, y, z; float wi, scale, base, w; DWORD c; float u, v; };
        // Identity projection in c4-c7 (rows of the transposed matrix = columns... identity is symmetric).
        float id[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
        g_dev->SetVertexShaderConstant(4, id, 4);
        float wave[4] = {0, 0, 0, 0};
        g_dev->SetVertexShaderConstant(8, wave, 1);         // wave entry 0: no skew
        float wave1[4] = {0.5f, 0, 0, 0};
        g_dev->SetVertexShaderConstant(9, wave1, 1);        // wave entry 1: skews x by 0.5 * height
        float off[4] = {0, 0, 0, 0}, scl[4] = {1, 1, 1, 1};
        g_dev->SetVertexShaderConstant(32, off, 1);
        g_dev->SetVertexShaderConstant(33, scl, 1);
        D3DVIEWPORT8 vp = {20, 20, 200, 200, 0.0f, 1.0f};
        g_dev->SetViewport(&vp);
        g_dev->SetVertexShader(vs);
        g_dev->SetTexture(0, tex);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        g_dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        g_dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        // A triangle covering the left half of the viewport; wave index 0, color scale 0.5 on a white diffuse.
        TV tri[3] = {{-0.9f, -0.9f, 0.5f, 0, 0.5f, 0.0f, 1, 0xFFFFFFFF, 0, 0}, {-0.9f, 0.9f, 0.5f, 0, 0.5f, 0.0f, 1, 0xFFFFFFFF, 0, 0},
                     {0.0f, -0.9f, 0.5f, 0, 0.5f, 0.0f, 1, 0xFFFFFFFF, 0, 0}};
        CHECK_HR(g_dev->BeginScene());
        CHECK_HR(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, tri, sizeof(TV)));
        CHECK_HR(g_dev->EndScene());
        Grab();
        // Viewport 20..220 x 20..220: NDC (-0.9..0) maps to x 30..120, y flips (top is +1).
        Expect("wind_sway.vsh: position transform + color scale 0.5", 50, 190, 0x808080, 4);
        // Wave skew: index 1 moves x by 0.5 * (z of v0 minus base) -> a triangle with height above the base.
        ClearScreen();
        TV tri2[3] = {{-0.9f, -0.9f, 0.0f, 1, 1.0f, 0.0f, 1, 0xFFFFFFFF, 0, 0}, {-0.9f, 0.9f, 0.0f, 1, 1.0f, 0.0f, 1, 0xFFFFFFFF, 0, 0},
                      {0.0f, -0.9f, 0.0f, 1, 1.0f, 0.0f, 1, 0xFFFFFFFF, 0, 0}};
        // v0.z = 0 and base z = v1.z = 0, so the skew is 0 for every vertex; use z = 0.4 to get a 0.2 shift in x.
        for (auto &v : tri2) v.z = 0.4f;
        CHECK_HR(g_dev->BeginScene());
        CHECK_HR(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, tri2, sizeof(TV)));
        CHECK_HR(g_dev->EndScene());
        Grab();
        // x' = x + (z - base) * 0.5 = x + 0.2 -> the triangle covers up to x = 0.2 instead of 0.0 at the bottom: pixel at NDC x 0.15, y -0.85
        // (screen x = 20 + (0.15+1)*100 = 135, y = 20 + (1+0.85)*100 = 205 is inside only with the skew).
        Expect("wind_sway.vsh: relative addressing c[a0.x+8] skews x", 128, 203, 0xFFFFFF, 4);
        g_dev->DeleteVertexShader(vs);
    }

    // proj_texgen.vsh: dp4 transform, rcp, mad with write masks.
    {
        DWORD decl[] = {D3DVSD_STREAM(0), D3DVSD_REG(0, D3DVSDT_FLOAT3), D3DVSD_REG(1, D3DVSDT_D3DCOLOR), D3DVSD_REG(2, D3DVSDT_FLOAT2),
                        D3DVSD_REG(3, D3DVSDT_FLOAT2), D3DVSD_END()};
        DWORD vs = AssembleFile("proj_texgen.vsh", true, decl);
        Report("proj_texgen.vsh creates", vs != 0);
        float id[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
        g_dev->SetVertexShaderConstant(2, id, 4);
        float proj[4][4] = {{0.5f, 0, 0, 0.5f}, {0, 0.5f, 0, 0.5f}, {0, 0, 0, 1}, {0, 0, 0, 1}};
        g_dev->SetVertexShaderConstant(6, proj, 4);
        IDirect3DTexture8 *t = MakeSolid(2, 2, 0xFF00C000);
        struct WV { float x, y, z; DWORD c; float u, v; float u2, v2; };
        WV q[4] = {{-0.5f, -0.5f, 0.5f, 0xFFFFFFFF, 0, 0, 0, 0}, {-0.5f, 0.5f, 0.5f, 0xFFFFFFFF, 0, 0, 0, 0},
                   {0.5f, 0.5f, 0.5f, 0xFFFFFFFF, 0, 0, 0, 0}, {0.5f, -0.5f, 0.5f, 0xFFFFFFFF, 0, 0, 0, 0}};
        WORD idx[6] = {0, 1, 2, 0, 2, 3};
        D3DVIEWPORT8 vp = {300, 20, 200, 200, 0.0f, 1.0f};
        g_dev->SetViewport(&vp);
        ClearScreen();
        g_dev->SetVertexShader(vs);
        g_dev->SetTexture(0, t);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        g_dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        CHECK_HR(g_dev->BeginScene());
        CHECK_HR(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, idx, D3DFMT_INDEX16, q, sizeof(WV)));
        CHECK_HR(g_dev->EndScene());
        Grab();
        Expect("proj_texgen.vsh: quad covers the middle of the viewport", 400, 120, 0x00C000, 4);
        Expect("proj_texgen.vsh: nothing outside the quad", 310, 30, 0x101010, 2);
        g_dev->DeleteVertexShader(vs);
        t->Release();
    }
    g_dev->SetTexture(0, nullptr);
    tex->Release();
    D3DVIEWPORT8 vp = {0, 0, (DWORD)W, (DWORD)H, 0.0f, 1.0f};
    g_dev->SetViewport(&vp);
    g_dev->SetVertexShader(D3DFVF_XYZ);
}

//------------------------------------------------------------------------------
// Cases: what a backend emulates (docs/WEBGPU.md, section 3). The pictures must be the same on every backend:
// clears of rectangles and after draws, triangle fans and point lists from buffers, point sprites, the fill modes,
// strides that are not a multiple of 4, the 16/24-bit and luminance formats, writes into buffers and textures that
// were drawn with earlier in the frame, and read-backs of render targets.
//------------------------------------------------------------------------------
static void Quad(float x0, float y0, float x1, float y1, float z, DWORD color)
{
    g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    g_dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    g_dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    g_dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    Rect(x0, y0, x1, y1, z, color);
}

static void CaseClears()
{
    g_case = "clears";
    BaseStates();
    ClearScreen(0xFF101010);
    CHECK_HR(g_dev->BeginScene());
    // A rectangle of the target.
    D3DRECT r1 = {50, 50, 150, 150};
    CHECK_HR(g_dev->Clear(1, &r1, D3DCLEAR_TARGET, 0xFFFF0000, 1.0f, 0));
    // A clear after a draw, overlapping it.
    Quad(200, 50, 300, 150, 0.5f, 0xFF00FF00);
    D3DRECT r2 = {250, 100, 350, 200};
    CHECK_HR(g_dev->Clear(1, &r2, D3DCLEAR_TARGET, 0xFF0000FF, 1.0f, 0));
    // Depth of a rectangle: a quad at z 0.4 is hidden where the depth was cleared to 0.2.
    D3DRECT r3 = {400, 50, 500, 150};
    CHECK_HR(g_dev->Clear(1, &r3, D3DCLEAR_ZBUFFER, 0, 0.2f, 0));
    g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    Quad(380, 30, 520, 170, 0.4f, 0xFFFF0000);
    g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    // Stencil of a rectangle: a quad with the stencil test passes only there.
    D3DRECT r4 = {50, 250, 150, 350};
    CHECK_HR(g_dev->Clear(1, &r4, D3DCLEAR_STENCIL, 0, 1.0f, 5));
    g_dev->SetRenderState(D3DRS_STENCILENABLE, TRUE);
    g_dev->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
    g_dev->SetRenderState(D3DRS_STENCILREF, 5);
    g_dev->SetRenderState(D3DRS_STENCILMASK, 0xFF);
    Quad(30, 230, 170, 370, 0.5f, 0xFFFFFF00);
    g_dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    // Two rectangles in one call.
    D3DRECT r5[2] = {{200, 250, 240, 290}, {260, 250, 300, 290}};
    CHECK_HR(g_dev->Clear(2, r5, D3DCLEAR_TARGET, 0xFFFF00FF, 1.0f, 0));
    // Without rectangles a clear covers the viewport.
    D3DVIEWPORT8 vp = {400, 250, 100, 100, 0.0f, 1.0f};
    g_dev->SetViewport(&vp);
    CHECK_HR(g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF00FFFF, 1.0f, 0));
    D3DVIEWPORT8 full = {0, 0, (DWORD)W, (DWORD)H, 0.0f, 1.0f};
    g_dev->SetViewport(&full);
    CHECK_HR(g_dev->EndScene());
    Grab();
    Expect("rectangle cleared", 100, 100, 0xFF0000);
    Expect("outside the rectangle untouched", 160, 100, 0x101010, 2);
    Expect("draw before a clear stays outside it", 225, 75, 0x00FF00);
    Expect("clear after a draw covers it", 275, 125, 0x0000FF);
    Expect("clear after a draw outside the draw", 325, 175, 0x0000FF);
    Expect("depth rectangle hides the quad behind it", 450, 100, 0x101010, 2);
    Expect("quad drawn outside the depth rectangle", 390, 40, 0xFF0000);
    Expect("stencil rectangle lets the quad through", 100, 300, 0xFFFF00);
    Expect("stencil test fails outside the rectangle", 40, 240, 0x101010, 2);
    Expect("first of two rectangles", 220, 270, 0xFF00FF);
    Expect("between two rectangles untouched", 250, 270, 0x101010, 2);
    Expect("second of two rectangles", 280, 270, 0xFF00FF);
    Expect("clear without rectangles covers the viewport", 450, 300, 0x00FFFF);
    Expect("clear without rectangles leaves the rest", 390, 300, 0x101010, 2);
}

struct XV { float x, y, z, rhw; DWORD color; };

static void CasePrimitives()
{
    g_case = "primitives";
    BaseStates();
    ClearScreen(0xFF101010);
    g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    g_dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    // A vertex buffer: 4 unused vertices, then a fan (a square around (100,100)), then 4 points.
    IDirect3DVertexBuffer8 *vb = nullptr;
    IDirect3DIndexBuffer8 *ib = nullptr;
    CHECK_HR(g_dev->CreateVertexBuffer(16 * sizeof(XV), D3DUSAGE_WRITEONLY, CV_FVF, D3DPOOL_MANAGED, &vb));
    CHECK_HR(g_dev->CreateIndexBuffer(16 * 2, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib));
    XV *v;
    CHECK_HR(vb->Lock(0, 0, (BYTE **)&v, 0));
    for (int i = 0; i < 4; ++i) v[i] = {0, 0, 0.5f, 1, 0xFFFFFFFF};
    v[4] = {100, 100, 0.5f, 1, 0xFF00FF00}; // centre
    v[5] = {60, 60, 0.5f, 1, 0xFF00FF00};
    v[6] = {140, 60, 0.5f, 1, 0xFF00FF00};
    v[7] = {140, 140, 0.5f, 1, 0xFF00FF00};
    v[8] = {60, 140, 0.5f, 1, 0xFF00FF00};
    v[9] = {60, 60, 0.5f, 1, 0xFF00FF00};
    for (int i = 0; i < 4; ++i) v[10 + i] = {200.0f + i * 30, 100, 0.5f, 1, 0xFFFF8000};
    v[14] = v[15] = {0, 0, 0.5f, 1, 0};
    CHECK_HR(vb->Unlock());
    WORD *ix;
    CHECK_HR(ib->Lock(0, 0, (BYTE **)&ix, 0));
    const WORD fan[6] = {0, 1, 2, 3, 4, 5};     // relative to base vertex 4
    const WORD pts[2] = {9, 7};                 // points 3 and 1 of the row, through base vertex 4
    memcpy(ix, fan, sizeof fan);
    memcpy(ix + 6, pts, sizeof pts);
    CHECK_HR(ib->Unlock());
    CHECK_HR(g_dev->BeginScene());
    g_dev->SetVertexShader(CV_FVF);
    CHECK_HR(g_dev->SetStreamSource(0, vb, sizeof(XV)));
    CHECK_HR(g_dev->SetIndices(ib, 4));
    CHECK_HR(g_dev->DrawIndexedPrimitive(D3DPT_TRIANGLEFAN, 0, 6, 0, 4));
    // Points of 8 pixels from the buffer: two without indices, one through the index buffer and the base vertex.
    g_dev->SetRenderState(D3DRS_POINTSIZE, FloatToDword(8.0f));
    CHECK_HR(g_dev->DrawPrimitive(D3DPT_POINTLIST, 10, 2));         // points 0 and 1 of the row
    CHECK_HR(g_dev->DrawIndexedPrimitive(D3DPT_POINTLIST, 0, 10, 6, 1)); // index 9 -> vertex 13 (point 3)
    g_dev->SetRenderState(D3DRS_POINTSIZE, FloatToDword(1.0f));
    g_dev->SetStreamSource(0, nullptr, 0);
    g_dev->SetIndices(nullptr, 0);

    // Point sprite: the texture's corners in the sprite's corners (u right, v down).
    IDirect3DTexture8 *quad = nullptr;
    CHECK_HR(g_dev->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &quad));
    if (quad)
    {
        D3DLOCKED_RECT lr;
        CHECK_HR(quad->LockRect(0, &lr, nullptr, 0));
        ((DWORD *)lr.pBits)[0] = 0xFFFF0000; ((DWORD *)lr.pBits)[1] = 0xFF00FF00;
        ((DWORD *)((BYTE *)lr.pBits + lr.Pitch))[0] = 0xFF0000FF; ((DWORD *)((BYTE *)lr.pBits + lr.Pitch))[1] = 0xFFFFFFFF;
        CHECK_HR(quad->UnlockRect(0));
    }
    g_dev->SetTexture(0, quad);
    g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    g_dev->SetRenderState(D3DRS_POINTSPRITEENABLE, TRUE);
    g_dev->SetRenderState(D3DRS_POINTSIZE, FloatToDword(32.0f));
    XV sprite = {400, 100, 0.5f, 1, 0xFFFFFFFF};
    CHECK_HR(g_dev->DrawPrimitiveUP(D3DPT_POINTLIST, 1, &sprite, sizeof(XV)));
    g_dev->SetRenderState(D3DRS_POINTSPRITEENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_POINTSIZE, FloatToDword(1.0f));
    g_dev->SetTexture(0, nullptr);
    g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);

    // Wireframe: the edges of a triangle, not its inside; point fill: its corners.
    g_dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
    XV tri[3] = {{50, 250, 0.5f, 1, 0xFFFFFF00}, {150, 250, 0.5f, 1, 0xFFFFFF00}, {50, 350, 0.5f, 1, 0xFFFFFF00}};
    CHECK_HR(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, tri, sizeof(XV)));
    g_dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_POINT);
    XV tri2[3] = {{200, 250, 0.5f, 1, 0xFF00FFFF}, {300, 250, 0.5f, 1, 0xFF00FFFF}, {200, 350, 0.5f, 1, 0xFF00FFFF}};
    CHECK_HR(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, tri2, sizeof(XV)));
    g_dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);

    // A stride that is not a multiple of 4 (22 bytes: the vertex and two padding bytes).
    BYTE packed[3 * 22] = {};
    const XV odd[3] = {{350, 250, 0.5f, 1, 0xFFFF00FF}, {450, 250, 0.5f, 1, 0xFFFF00FF}, {350, 350, 0.5f, 1, 0xFFFF00FF}};
    for (int i = 0; i < 3; ++i) memcpy(packed + i * 22, &odd[i], sizeof(XV));
    CHECK_HR(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, packed, 22));
    CHECK_HR(g_dev->EndScene());
    Grab();
    Expect("indexed fan with a base vertex", 100, 80, 0x00FF00);
    Expect("indexed fan, last triangle", 70, 100, 0x00FF00);
    Expect("outside the fan", 50, 100, 0x101010, 2);
    Expect("point from a buffer (first vertex 10)", 200, 100, 0xFF8000);
    Expect("point from a buffer (second)", 230, 100, 0xFF8000);
    Expect("8 pixel point covers its square", 203, 97, 0xFF8000);
    Expect("indexed point list with a base vertex", 290, 100, 0xFF8000);
    Expect("point not drawn", 260, 100, 0x101010, 2);
    Expect("sprite top left is texel (0,0)", 390, 90, 0xFF0000);
    Expect("sprite top right is texel (1,0)", 410, 90, 0x00FF00);
    Expect("sprite bottom left is texel (0,1)", 390, 110, 0x0000FF);
    Expect("sprite bottom right is texel (1,1)", 410, 110, 0xFFFFFF);
    Expect("wireframe: top edge", 100, 250, 0xFFFF00);
    Expect("wireframe: left edge", 50, 300, 0xFFFF00);
    Expect("wireframe: no inside", 70, 270, 0x101010, 2);
    Expect("point fill: a corner", 200, 250, 0x00FFFF);
    Expect("point fill: no inside", 220, 270, 0x101010, 2);
    ExpectKnown("stride of 22 bytes", 370, 270, 0xFF00FF, "WebGL needs vertex strides that are a multiple of the component size");
    vb->Release();
    ib->Release();
    if (quad) quad->Release();
}

static void CaseFormats()
{
    g_case = "formats";
    BaseStates();
    ClearScreen(0xFF101010);
    struct F { D3DFORMAT fmt; const char *name; DWORD expect; };
    // The texel is written in the format's own layout; the expected colour is what sampling it returns.
    const F formats[] = {
        {D3DFMT_X1R5G5B5, "X1R5G5B5", 0x3173C5}, {D3DFMT_X4R4G4B4, "X4R4G4B4", 0x3377CC}, {D3DFMT_R8G8B8, "R8G8B8", 0x3070C0},
        {D3DFMT_A4L4, "A4L4", 0xAAAAAA},         {D3DFMT_X8R8G8B8, "X8R8G8B8", 0x3070C0}, {D3DFMT_DXT1, "DXT1 6x6", 0x3171C6},
    };
    CHECK_HR(g_dev->BeginScene());
    g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    std::vector<IDirect3DTexture8 *> keep;
    int n = 0;
    for (const F &f : formats)
    {
        const UINT size = f.fmt == D3DFMT_DXT1 ? 6 : 4;
        IDirect3DTexture8 *t = nullptr;
        if (FAILED(g_dev->CreateTexture(size, size, 1, 0, f.fmt, D3DPOOL_MANAGED, &t)) || !t)
        {
            Report(f.name, false, "CreateTexture failed");
            continue;
        }
        D3DLOCKED_RECT lr;
        CHECK_HR(t->LockRect(0, &lr, nullptr, 0));
        for (UINT y = 0; y < (f.fmt == D3DFMT_DXT1 ? 2u : size); ++y)
            for (UINT x = 0; x < (f.fmt == D3DFMT_DXT1 ? 2u : size); ++x)
            {
                BYTE *p = (BYTE *)lr.pBits + y * lr.Pitch;
                switch (f.fmt)
                {
                case D3DFMT_X1R5G5B5: ((WORD *)p)[x] = (WORD)((0x30 >> 3) << 10 | (0x70 >> 3) << 5 | (0xC0 >> 3)); break;
                case D3DFMT_X4R4G4B4: ((WORD *)p)[x] = (WORD)(3 << 8 | 7 << 4 | 12); break;
                case D3DFMT_R8G8B8: p[x * 3] = 0xC0; p[x * 3 + 1] = 0x70; p[x * 3 + 2] = 0x30; break;
                case D3DFMT_A4L4: p[x] = 0xFA; break;
                case D3DFMT_X8R8G8B8: ((DWORD *)p)[x] = 0x003070C0; break; // alpha 0 must read as 1
                case D3DFMT_DXT1:
                {
                    // One block per 4x4: both endpoints the same 565 colour, every index 0.
                    const WORD c = (WORD)((0x30 >> 3) << 11 | (0x70 >> 2) << 5 | (0xC0 >> 3));
                    BYTE *b = p + x * 8;
                    b[0] = (BYTE)c; b[1] = (BYTE)(c >> 8); b[2] = (BYTE)c; b[3] = (BYTE)(c >> 8);
                    b[4] = b[5] = b[6] = b[7] = 0;
                    break;
                }
                default: break;
                }
            }
        CHECK_HR(t->UnlockRect(0));
        g_dev->SetTexture(0, t);
        const float x0 = 20.0f + (n % 6) * 100, y0 = 20.0f;
        Rect(x0, y0, x0 + 80, y0 + 80, 0.5f, 0xFFFFFFFF);
        keep.push_back(t);
        ++n;
    }
    // X8R8G8B8 with alpha 0 in memory: alpha blending must see an opaque texel.
    if (keep.size() > 4)
    {
        g_dev->SetTexture(0, keep[4]);
        g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        g_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        g_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        Rect(20, 150, 100, 230, 0.5f, 0xFFFFFFFF);
        g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    }
    g_dev->SetTexture(0, nullptr);
    CHECK_HR(g_dev->EndScene());
    Grab();
    n = 0;
    for (const F &f : formats)
    {
        char label[64];
        snprintf(label, sizeof label, "%s sampled", f.name);
        if (f.fmt == D3DFMT_DXT1)
            ExpectKnown(label, 60 + (n % 6) * 100, 60, f.expect, "S3TC textures need a size that is a multiple of 4", 6);
        else
            Expect(label, 60 + (n % 6) * 100, 60, f.expect, 6);
        ++n;
    }
    Expect("X8R8G8B8 blends as opaque", 60, 190, 0x3070C0, 6);
    for (IDirect3DTexture8 *t : keep) t->Release();
}

static void CaseUpdates()
{
    g_case = "updates";
    BaseStates();
    ClearScreen(0xFF101010);
    g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    g_dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    // A dynamic vertex buffer written again (DISCARD) after a draw that used it, then appended to (NOOVERWRITE).
    IDirect3DVertexBuffer8 *vb = nullptr;
    CHECK_HR(g_dev->CreateVertexBuffer(8 * sizeof(XV), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, CV_FVF, D3DPOOL_DEFAULT, &vb));
    auto fill = [&](UINT first, float x0, DWORD color, DWORD flags) {
        XV *v;
        CHECK_HR(vb->Lock(first * sizeof(XV), 4 * sizeof(XV), (BYTE **)&v, flags));
        v[0] = {x0, 50, 0.5f, 1, color}; v[1] = {x0 + 80, 50, 0.5f, 1, color};
        v[2] = {x0, 130, 0.5f, 1, color}; v[3] = {x0 + 80, 130, 0.5f, 1, color};
        CHECK_HR(vb->Unlock());
    };
    CHECK_HR(g_dev->BeginScene());
    g_dev->SetVertexShader(CV_FVF);
    CHECK_HR(g_dev->SetStreamSource(0, vb, sizeof(XV)));
    fill(0, 20, 0xFFFF0000, D3DLOCK_DISCARD);
    CHECK_HR(g_dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2));
    fill(0, 120, 0xFF00FF00, D3DLOCK_DISCARD);
    CHECK_HR(g_dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2));
    fill(4, 220, 0xFF0000FF, D3DLOCK_NOOVERWRITE);
    CHECK_HR(g_dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 4, 2));
    g_dev->SetStreamSource(0, nullptr, 0);

    // A texture drawn with, then written, then drawn with again.
    IDirect3DTexture8 *t = MakeSolid(4, 4, 0xFFFFFF00);
    g_dev->SetTexture(0, t);
    g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    Rect(20, 200, 100, 280, 0.5f, 0xFFFFFFFF);
    D3DLOCKED_RECT lr;
    CHECK_HR(t->LockRect(0, &lr, nullptr, 0));
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = 0xFF00FFFF;
    CHECK_HR(t->UnlockRect(0));
    Rect(120, 200, 200, 280, 0.5f, 0xFFFFFFFF);
    g_dev->SetTexture(0, nullptr);
    CHECK_HR(g_dev->EndScene());

    // Render targets read back: a texture level (CopyRects to an image surface) and a lockable surface, 32 and 16 bit.
    IDirect3DSurface8 *bb = nullptr, *bbDepth = nullptr;
    CHECK_HR(g_dev->GetRenderTarget(&bb));
    CHECK_HR(g_dev->GetDepthStencilSurface(&bbDepth));
    const D3DFORMAT rtFormats[2] = {D3DFMT_A8R8G8B8, D3DFMT_R5G6B5};
    for (int k = 0; k < 2; ++k)
    {
        IDirect3DTexture8 *rt = nullptr;
        CHECK_HR(g_dev->CreateTexture(32, 32, 1, D3DUSAGE_RENDERTARGET, rtFormats[k], D3DPOOL_DEFAULT, &rt));
        if (!rt) continue;
        IDirect3DSurface8 *s = nullptr;
        CHECK_HR(rt->GetSurfaceLevel(0, &s));
        CHECK_HR(g_dev->SetRenderTarget(s, nullptr));
        CHECK_HR(g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF204080, 1.0f, 0));
        CHECK_HR(g_dev->BeginScene());
        Quad(8, 8, 24, 24, 0.5f, 0xFFF0C010);
        CHECK_HR(g_dev->EndScene());
        CHECK_HR(g_dev->SetRenderTarget(bb, bbDepth));
        IDirect3DSurface8 *img = nullptr;
        CHECK_HR(g_dev->CreateImageSurface(32, 32, D3DFMT_A8R8G8B8, &img));
        if (img)
        {
            CHECK_HR(g_dev->CopyRects(s, nullptr, 0, img, nullptr));
            D3DLOCKED_RECT ir;
            CHECK_HR(img->LockRect(&ir, nullptr, D3DLOCK_READONLY));
            const DWORD edge = ((DWORD *)ir.pBits)[0] & 0xFFFFFF, mid = ((DWORD *)((BYTE *)ir.pBits + 16 * ir.Pitch))[16] & 0xFFFFFF;
            CHECK_HR(img->UnlockRect());
            char l1[64], l2[64];
            snprintf(l1, sizeof l1, "%s render target read back: cleared", k ? "R5G6B5" : "A8R8G8B8");
            snprintf(l2, sizeof l2, "%s render target read back: drawn", k ? "R5G6B5" : "A8R8G8B8");
            Report(l1, ColorNear(edge, 0x204080, k ? 8 : 2), "got %06x", (unsigned)edge);
            Report(l2, ColorNear(mid, 0xF0C010, k ? 8 : 2), "got %06x", (unsigned)mid);
            img->Release();
        }
        s->Release();
        rt->Release();
    }
    IDirect3DSurface8 *lockable = nullptr;
    CHECK_HR(g_dev->CreateRenderTarget(16, 16, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, TRUE, &lockable));
    if (lockable)
    {
        CHECK_HR(g_dev->SetRenderTarget(lockable, nullptr));
        CHECK_HR(g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF60A0E0, 1.0f, 0));
        CHECK_HR(g_dev->SetRenderTarget(bb, bbDepth));
        D3DLOCKED_RECT sr;
        CHECK_HR(lockable->LockRect(&sr, nullptr, D3DLOCK_READONLY));
        const DWORD c = ((DWORD *)((BYTE *)sr.pBits + 5 * sr.Pitch))[7] & 0xFFFFFF;
        CHECK_HR(lockable->UnlockRect());
        Report("lockable render target surface read back", ColorNear(c, 0x60A0E0, 2), "got %06x", (unsigned)c);
        lockable->Release();
    }
    bb->Release();
    bbDepth->Release();

    Grab();
    Expect("first contents of the dynamic buffer", 60, 90, 0xFF0000);
    Expect("rewritten (DISCARD) contents", 160, 90, 0x00FF00);
    Expect("appended (NOOVERWRITE) contents", 260, 90, 0x0000FF);
    Expect("texture before the update", 60, 240, 0xFFFF00);
    Expect("texture after the update", 160, 240, 0x00FFFF);
    t->Release();
    vb->Release();
}

//------------------------------------------------------------------------------
// Main
//------------------------------------------------------------------------------
static bool CreateDeviceWith(int samples, UINT w = W, UINT h = H)
{
    D3DPRESENT_PARAMETERS pp;
    memset(&pp, 0, sizeof pp);
    pp.BackBufferWidth = w;
    pp.BackBufferHeight = h;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.MultiSampleType = (D3DMULTISAMPLE_TYPE)samples;
    HRESULT hr = g_d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &g_dev);
    if (FAILED(hr)) { printf("CreateDevice failed 0x%08x\n", (unsigned)hr); return false; }
    // A silent fallback to another backend fails the run.
    const char *saved = g_case;
    g_case = "device";
    Report("renders through the requested backend", g_wantBackend < 0 || dxwebgl2_GetBackend() == g_wantBackend, "%d",
           dxwebgl2_GetBackend());
    g_case = saved;
    return true;
}

// Defined in the other translation units of this test (added below).
void RunContextLossMode();
void RunMultisampleMode();

int main(int argc, char **argv)
{
    std::string mode = "basic";
    bool report = true;
    int overrides = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (!strncmp(argv[i], "--mode=", 7)) mode = argv[i] + 7;
        if (!strcmp(argv[i], "--noprovoking")) overrides |= 1;
        if (!strcmp(argv[i], "--nomsaa")) { overrides |= 8; g_msaaDisabled = true; }
        if (!strcmp(argv[i], "--noreport")) report = false;
        if (!strcmp(argv[i], "--debug")) dxwebgl2_SetDebug(1);
        if (!strcmp(argv[i], "--no-s3tc")) dxwebgl2_SetDisableS3TC(1);
        if (!strcmp(argv[i], "--backend=webgpu")) { dxwebgl2_SetBackend(DXWEBGL2_BACKEND_WEBGPU); g_wantBackend = DXWEBGL2_BACKEND_WEBGPU; }
        if (!strcmp(argv[i], "--backend=webgl2")) { dxwebgl2_SetBackend(DXWEBGL2_BACKEND_WEBGL2); g_wantBackend = DXWEBGL2_BACKEND_WEBGL2; }
        if (!strcmp(argv[i], "--backend=auto")) dxwebgl2_SetBackend(DXWEBGL2_BACKEND_AUTO);
    }
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--expect-backend=webgl2")) g_wantBackend = DXWEBGL2_BACKEND_WEBGL2;
        if (!strcmp(argv[i], "--expect-backend=webgpu")) g_wantBackend = DXWEBGL2_BACKEND_WEBGPU;
    }
    dxwebgl2_PlatformHooks hooks = {};
    hooks.OnFramePresented = [] { emscripten_sleep(0); };
    dxwebgl2_SetPlatformHooks(&hooks);
    dxwebgl2_SetShaderModel(0x0101, 0x0104);
    dxwebgl2_SetFeatureOverrides(overrides);
    if (report) dxwebgl2_SetReport(3600);
    printf("d3d8_features_test starting, mode %s\n", mode.c_str());

    g_d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!g_d3d) { printf("Direct3DCreate8 failed\n"); return 1; }

    if (mode == "basic")
    {
        if (!CreateDeviceWith(0)) return 1;
        printf("renderer: %s\n", dxwebgl2_GetRendererString());
        CaseBorder();
        CaseFlat(overrides & 1 ? "flat_fallback" : "flat");
        CasePixelShaderOps();
        CaseFilters();
        CaseStateCache();
        CaseInlineShaders();
        CaseInlineVertexShaders();
        CaseClears();
        CasePrimitives();
        CaseFormats();
        CaseUpdates();
        if (report) CaseDiagnostics();
    }
    else if (mode == "contextloss") { if (!CreateDeviceWith(0)) return 1; RunContextLossMode(); }
    else if (mode == "msaa") { RunMultisampleMode(); }
    else printf("unknown mode\n");

    printf("SUMMARY pass=%d fail=%d\n", g_pass, g_fail);
    printf("TEST_DONE\n");
    fflush(stdout);
    return 0;
}

//------------------------------------------------------------------------------
// Context loss: the device-lost path end to end
//------------------------------------------------------------------------------
struct LossFixture
{
    IDirect3DTexture8 *tex = nullptr;       // managed, survives
    IDirect3DVertexBuffer8 *vb = nullptr;   // managed
    IDirect3DIndexBuffer8 *ib = nullptr;    // managed
    IDirect3DTexture8 *rt = nullptr;        // default pool render target: released and recreated by the "application"
    IDirect3DSurface8 *rtDepth = nullptr;
    DWORD ps = 0;
};
static LossFixture g_fx;

static void CreateLossFixture()
{
    g_fx.tex = MakeSolid(4, 4, 0xFFFF8000);
    CHECK_HR(g_dev->CreateVertexBuffer(4 * sizeof(LV), D3DUSAGE_WRITEONLY, LV_FVF, D3DPOOL_MANAGED, &g_fx.vb));
    LV *v;
    CHECK_HR(g_fx.vb->Lock(0, 0, (BYTE **)&v, 0));
    const float xs[4] = {50, 50, 150, 150}, ys[4] = {50, 150, 150, 50}, us[4] = {0, 0, 1, 1}, vs[4] = {0, 1, 1, 0};
    for (int i = 0; i < 4; ++i) v[i] = {xs[i] - 0.5f, ys[i] - 0.5f, 0.5f, 1.0f, 0xFFFFFFFF, us[i], vs[i]};
    CHECK_HR(g_fx.vb->Unlock());
    CHECK_HR(g_dev->CreateIndexBuffer(6 * 2, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &g_fx.ib));
    WORD *ix;
    CHECK_HR(g_fx.ib->Lock(0, 0, (BYTE **)&ix, 0));
    const WORD idx[6] = {0, 1, 2, 0, 2, 3};
    memcpy(ix, idx, sizeof idx);
    CHECK_HR(g_fx.ib->Unlock());
    CHECK_HR(g_dev->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_fx.rt));
    CHECK_HR(g_dev->CreateDepthStencilSurface(64, 64, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &g_fx.rtDepth));
    g_fx.ps = Assemble("ps.1.1\ntex t0\nmul r0, t0, v0\n", false);
}

static void RenderLossFixture()
{
    BaseStates();
    // Render target pass: a magenta quad into the texture.
    IDirect3DSurface8 *bb = nullptr, *bbDepth = nullptr, *rtSurf = nullptr;
    CHECK_HR(g_dev->GetRenderTarget(&bb));
    CHECK_HR(g_dev->GetDepthStencilSurface(&bbDepth));
    CHECK_HR(g_fx.rt->GetSurfaceLevel(0, &rtSurf));
    CHECK_HR(g_dev->SetRenderTarget(rtSurf, g_fx.rtDepth));
    D3DVIEWPORT8 vp = {0, 0, 64, 64, 0, 1};
    g_dev->SetViewport(&vp);
    CHECK_HR(g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFFFF00FF, 1.0f, 0));
    CHECK_HR(g_dev->SetRenderTarget(bb, bbDepth));
    rtSurf->Release(); bb->Release(); bbDepth->Release();
    D3DVIEWPORT8 full = {0, 0, (DWORD)W, (DWORD)H, 0, 1};
    g_dev->SetViewport(&full);
    ClearScreen();
    CHECK_HR(g_dev->BeginScene());
    // Fixed function textured quad from the managed buffers.
    g_dev->SetTexture(0, g_fx.tex);
    g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    CHECK_HR(g_dev->SetStreamSource(0, g_fx.vb, sizeof(LV)));
    CHECK_HR(g_dev->SetIndices(g_fx.ib, 0));
    g_dev->SetVertexShader(LV_FVF);
    CHECK_HR(g_dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2));
    // Pixel shader quad.
    g_dev->SetPixelShader(g_fx.ps);
    Rect(200, 50, 300, 150, 0.5f, 0xFFFFFFFF);
    g_dev->SetPixelShader(0);
    // The render target as a texture.
    g_dev->SetTexture(0, g_fx.rt);
    Rect(300, 300, 400, 400, 0.5f, 0xFFFFFFFF);
    CHECK_HR(g_dev->EndScene());
    g_dev->SetTexture(0, nullptr);
    g_dev->SetStreamSource(0, nullptr, 0);
    g_dev->SetIndices(nullptr, 0);
}

static void CheckLossFixture(const char *when)
{
    Grab();
    char l[96];
    snprintf(l, sizeof l, "%s: managed texture + vertex/index buffer", when);
    Expect(l, 100, 100, 0xFF8000);
    snprintf(l, sizeof l, "%s: pixel shader program", when);
    Expect(l, 250, 50, 0xFF8000);
    snprintf(l, sizeof l, "%s: render target texture", when);
    Expect(l, 350, 350, 0xFF00FF);
}

void RunContextLossMode()
{
    g_case = "contextloss";
    CreateLossFixture();
    RenderLossFixture();
    CHECK_HR(g_dev->Present(nullptr, nullptr, nullptr, nullptr));
    CheckLossFixture("before the loss");

    // Drop the context; the library restores it a few presented frames later.
    dxwebgl2_LoseContextNow(8);
    int lostFrames = 0, notResetFrames = 0;
    bool sawLost = false, sawNotReset = false;
    HRESULT hr = D3D_OK;
    for (int i = 0; i < 2000; ++i)
    {
        HRESULT presentHr = g_dev->Present(nullptr, nullptr, nullptr, nullptr);
        hr = g_dev->TestCooperativeLevel();
        if (presentHr == D3DERR_DEVICELOST) sawLost = true;
        if (hr == D3DERR_DEVICELOST) ++lostFrames;
        if (hr == D3DERR_DEVICENOTRESET) { sawNotReset = true; ++notResetFrames; break; }
        if (hr == D3D_OK && sawLost) break;
        emscripten_sleep(2);
    }
    Report("Present reports D3DERR_DEVICELOST", sawLost);
    Report("TestCooperativeLevel reports DEVICELOST while lost", lostFrames > 0, "%d frames", lostFrames);
    Report("TestCooperativeLevel reports DEVICENOTRESET after the restore", sawNotReset);

    // Drawing while not reset must be harmless.
    HRESULT clr = g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
    Report("Clear reports the lost state", clr == D3DERR_DEVICELOST || clr == D3DERR_DEVICENOTRESET, "0x%x", (unsigned)clr);

    // A typical application reset: release everything in the default pool, Reset, recreate.
    if (g_fx.rt) { g_fx.rt->Release(); g_fx.rt = nullptr; }
    if (g_fx.rtDepth) { g_fx.rtDepth->Release(); g_fx.rtDepth = nullptr; }
    D3DPRESENT_PARAMETERS pp;
    memset(&pp, 0, sizeof pp);
    pp.BackBufferWidth = W;
    pp.BackBufferHeight = H;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    hr = g_dev->Reset(&pp);
    Report("Reset succeeds on the restored context", SUCCEEDED(hr), "0x%x", (unsigned)hr);
    Report("TestCooperativeLevel is OK after Reset", g_dev->TestCooperativeLevel() == D3D_OK);
    CHECK_HR(g_dev->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_fx.rt));
    CHECK_HR(g_dev->CreateDepthStencilSurface(64, 64, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &g_fx.rtDepth));

    RenderLossFixture();
    CHECK_HR(g_dev->Present(nullptr, nullptr, nullptr, nullptr));
    CheckLossFixture("after the restore");

    dxwebgl2_Stats st;
    dxwebgl2_GetStats(&st);
    Report("one loss and one restore counted", st.contextLosses == 1 && st.contextRestores == 1, "%u/%u", st.contextLosses, st.contextRestores);

    // A second loss and restore (with textures that were created while lost).
    dxwebgl2_LoseContextNow(4);
    for (int i = 0; i < 2000; ++i)
    {
        g_dev->Present(nullptr, nullptr, nullptr, nullptr);
        if (g_dev->TestCooperativeLevel() == D3DERR_DEVICENOTRESET) break;
        emscripten_sleep(2);
    }
    IDirect3DTexture8 *whileLost = MakeSolid(4, 4, 0xFF00FFFF);
    if (g_fx.rt) { g_fx.rt->Release(); g_fx.rt = nullptr; }
    if (g_fx.rtDepth) { g_fx.rtDepth->Release(); g_fx.rtDepth = nullptr; }
    hr = g_dev->Reset(&pp);
    Report("second Reset succeeds", SUCCEEDED(hr));
    CHECK_HR(g_dev->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_fx.rt));
    CHECK_HR(g_dev->CreateDepthStencilSurface(64, 64, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &g_fx.rtDepth));
    RenderLossFixture();
    BaseStates();
    CHECK_HR(g_dev->BeginScene());
    g_dev->SetTexture(0, whileLost);
    g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    Rect(450, 300, 550, 400, 0.5f, 0xFFFFFFFF);
    CHECK_HR(g_dev->EndScene());
    CHECK_HR(g_dev->Present(nullptr, nullptr, nullptr, nullptr));
    CheckLossFixture("after the second restore");
    Grab();
    Expect("texture created while the context was lost", 500, 350, 0x00FFFF);
}

//------------------------------------------------------------------------------
// Multisampling
//------------------------------------------------------------------------------
void RunMultisampleMode()
{
    g_case = "msaa";
    HRESULT ck2 = g_d3d->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, TRUE, D3DMULTISAMPLE_4_SAMPLES);
    HRESULT ckd = g_d3d->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, D3DFMT_D24S8, TRUE, D3DMULTISAMPLE_4_SAMPLES);
    Report("4x multisampling reported for the back buffer", SUCCEEDED(ck2));
    Report("4x multisampling reported for the depth format", SUCCEEDED(ckd));
    Report("16x is refused", FAILED(g_d3d->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, TRUE, D3DMULTISAMPLE_16_SAMPLES)));

    if (!CreateDeviceWith(D3DMULTISAMPLE_4_SAMPLES)) return;
    IDirect3DSurface8 *bb = nullptr;
    CHECK_HR(g_dev->GetRenderTarget(&bb));
    D3DSURFACE_DESC desc;
    bb->GetDesc(&desc);
    const bool ms = desc.MultiSampleType != D3DMULTISAMPLE_NONE;
    const bool unavailable = g_msaaDisabled;
    Report(unavailable ? "back buffer falls back to no multisampling" : "back buffer reports its multisample type",
           ms != unavailable, "type %d", (int)desc.MultiSampleType);
    bb->Release();

    BaseStates();
    ClearScreen(0xFF000000);
    CHECK_HR(g_dev->BeginScene());
    // A white triangle with a diagonal edge on black: edge pixels get intermediate values when antialiased.
    CV tri[3] = {{100, 100, 0.5f, 1, 0xFFFFFFFF}, {300, 100, 0.5f, 1, 0xFFFFFFFF}, {100, 300, 0.5f, 1, 0xFFFFFFFF}};
    g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    g_dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    g_dev->SetVertexShader(CV_FVF);
    CHECK_HR(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, tri, sizeof(CV)));
    CHECK_HR(g_dev->EndScene());
    Grab();
    int partial = 0;
    for (int y = 100; y < 300; ++y)
        for (int x = 100; x < 300; ++x)
        {
            const int g = g_px[y * W + x] & 255;
            if (g > 16 && g < 240) ++partial;
        }
    Report(unavailable ? "hard edges without multisampling" : "antialiased edge has intermediate coverage values",
           unavailable ? partial == 0 : partial > 100, "%d edge pixels", partial);
    Expect("interior is white", 150, 150, 0xFFFFFF);
    Expect("outside is black", 280, 280, 0x000000);

    // Render-to-texture while the back buffer is multisampled (depth buffer mismatch is handled).
    IDirect3DTexture8 *rt = nullptr;
    CHECK_HR(g_dev->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &rt));
    IDirect3DSurface8 *rtSurf = nullptr, *depth = nullptr;
    CHECK_HR(rt->GetSurfaceLevel(0, &rtSurf));
    CHECK_HR(g_dev->GetRenderTarget(&bb));
    CHECK_HR(g_dev->GetDepthStencilSurface(&depth));
    HRESULT hr = g_dev->SetRenderTarget(rtSurf, depth);
    Report("SetRenderTarget(texture, multisampled depth) is accepted", SUCCEEDED(hr), "0x%x", (unsigned)hr);
    D3DVIEWPORT8 vp = {0, 0, 64, 64, 0, 1};
    g_dev->SetViewport(&vp);
    CHECK_HR(g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF00FF00, 1.0f, 0));
    CHECK_HR(g_dev->SetRenderTarget(bb, depth));
    D3DVIEWPORT8 full = {0, 0, (DWORD)W, (DWORD)H, 0, 1};
    g_dev->SetViewport(&full);
    ClearScreen(0xFF000000);
    CHECK_HR(g_dev->BeginScene());
    g_dev->SetTexture(0, rt);
    g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    Rect(400, 100, 500, 200, 0.5f, 0xFFFFFFFF);
    CHECK_HR(g_dev->EndScene());
    CHECK_HR(g_dev->Present(nullptr, nullptr, nullptr, nullptr));
    Grab();
    Expect("render target texture drawn into the multisampled back buffer", 450, 150, 0x00FF00);
    rtSurf->Release(); depth->Release(); bb->Release(); rt->Release();
    g_dev->SetTexture(0, nullptr);
}
