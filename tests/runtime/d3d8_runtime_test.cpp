/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Runtime test of the Direct3D 8 on WebGL2 implementation, run in a browser.
 *
 * 1. API checks: state round-trips, resource creation and locking, capabilities, D3DX helpers.
 * 2. Scenes rendered through the plain D3D8 API (fixed-function lighting, multi-texturing, fog,
 *    render-to-texture, DXT textures, pre-transformed 2D primitives, alpha test/blend, and with
 *    --shaders / --scene2 / --scene3 vertex and pixel shaders, texture generation, projection, cube and
 *    volume maps, stencil, point sprites, texture formats, CopyRects, D3DX) in a blocking loop on the
 *    proxied main thread. The last frame is read back through GetFrontBuffer() and probe pixels are
 *    compared with the values in expected_probes.inc (recorded with SwiftShader, compared with a
 *    tolerance so that hardware GL implementations pass as well).
 *
 * The test prints PROBE / CHECK lines and ends with "TEST_RESULT: PASS" or "TEST_RESULT: FAIL ...";
 * tests/runner/run_test.mjs drives it in headless Chromium and turns that into its exit code.
 *
 * Arguments (comma separated in the page's ?args= query): --shaders (vs.1.1 / ps.1.x scenes),
 * --scene2, --scene3, --no-s3tc (decode DXT on the CPU), --implicit (no explicit swap control),
 * --reset (IDirect3DDevice8::Reset in the middle), --debug (GL error checks), --frames=N (long run),
 * --yield (emscripten_sleep(0) per presented frame), --proxy (WebGL proxied to the main thread).
 */
#include <d3d8.h>
#include <d3dx8.h>
#include "dxwebgl2/dxwebgl2.h"

#include <emscripten.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

template <class T> static T Max(T a, T b) { return a > b ? a : b; }
static DWORD FloatToDword(float f) { DWORD v; memcpy(&v, &f, 4); return v; }

static IDirect3D8 *g_d3d;
static IDirect3DDevice8 *g_dev;

static int g_failures = 0; // failed CHECK / EXPECT / probe
static int g_expects = 0;  // evaluated EXPECTs

// A Direct3D call that must succeed.
#define CHECK(expr)                                                                                         \
    do {                                                                                                    \
        HRESULT _hr = (expr);                                                                               \
        if (FAILED(_hr)) {                                                                                  \
            printf("FAILED %s -> 0x%08x (line %d)\n", #expr, (unsigned)_hr, __LINE__);                      \
            ++g_failures;                                                                                   \
        }                                                                                                   \
    } while (0)

// A condition that must hold.
#define EXPECT(cond)                                                                                        \
    do {                                                                                                    \
        ++g_expects;                                                                                        \
        if (!(cond)) {                                                                                      \
            printf("EXPECT FAILED: %s (line %d)\n", #cond, __LINE__);                                       \
            ++g_failures;                                                                                   \
        }                                                                                                   \
    } while (0)

#define EXPECT_HR(expr, expected)                                                                           \
    do {                                                                                                    \
        ++g_expects;                                                                                        \
        HRESULT _hr = (expr);                                                                               \
        if (_hr != (HRESULT)(expected)) {                                                                   \
            printf("EXPECT FAILED: %s -> 0x%08x, expected %s (line %d)\n", #expr, (unsigned)_hr, #expected, __LINE__); \
            ++g_failures;                                                                                   \
        }                                                                                                   \
    } while (0)

struct LitVertex { float x, y, z, nx, ny, nz; float u, v; };
#define LIT_FVF (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1)

struct ColVertex { float x, y, z; DWORD color; };
#define COL_FVF (D3DFVF_XYZ | D3DFVF_DIFFUSE)

struct TexVertex { float x, y, z; DWORD color; float u, v; };
#define TEX_FVF (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1)

struct Tex2Vertex { float x, y, z; DWORD color; float u0, v0, u1, v1; };
#define TEX2_FVF (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX2)

struct RhwVertex { float x, y, z, rhw; DWORD color; float u, v; };
#define RHW_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

static IDirect3DTexture8 *MakeChecker(UINT size, DWORD c0, DWORD c1, bool mips)
{
    IDirect3DTexture8 *tex = nullptr;
    CHECK(g_dev->CreateTexture(size, size, mips ? 0 : 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex));
    if (!tex) return nullptr;
    for (UINT l = 0; l < tex->GetLevelCount(); ++l)
    {
        D3DSURFACE_DESC d;
        tex->GetLevelDesc(l, &d);
        D3DLOCKED_RECT lr;
        CHECK(tex->LockRect(l, &lr, nullptr, 0));
        for (UINT y = 0; y < d.Height; ++y)
        {
            DWORD *row = (DWORD *)((BYTE *)lr.pBits + y * lr.Pitch);
            for (UINT x = 0; x < d.Width; ++x)
            {
                UINT cell = Max<UINT>(1, d.Width / 8);
                row[x] = (((x / cell) + (y / cell)) & 1) ? c1 : c0;
            }
        }
        CHECK(tex->UnlockRect(l));
    }
    return tex;
}


static void SetCommonStates()
{
    g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    g_dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    g_dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    for (int s = 0; s < 4; ++s)
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
        g_dev->SetTextureStageState(s, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
        g_dev->SetTextureStageState(s, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
        g_dev->SetTextureStageState(s, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
        g_dev->SetTextureStageState(s, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
        g_dev->SetTextureStageState(s, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
    }
}

static void SetViewport(DWORD x, DWORD y, DWORD w, DWORD h)
{
    D3DVIEWPORT8 vp = {x, y, w, h, 0.0f, 1.0f};
    CHECK(g_dev->SetViewport(&vp));
}

// Unit cube with per-face normals and UVs.
static void BuildCube(std::vector<LitVertex> &v, std::vector<WORD> &idx)
{
    static const float n[6][3] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    static const float u[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}};
    static const float w[6][3] = {{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    for (int f = 0; f < 6; ++f)
    {
        // Face vertices: corner = n*0.5 + (+-u + +-w)*0.5, ordered clockwise seen from outside (left-handed).
        static const int sx[4] = {-1, -1, 1, 1}, sy[4] = {-1, 1, 1, -1};
        for (int i = 0; i < 4; ++i)
        {
            LitVertex lv;
            lv.x = (n[f][0] + sx[i] * u[f][0] + sy[i] * w[f][0]) * 0.5f;
            lv.y = (n[f][1] + sx[i] * u[f][1] + sy[i] * w[f][1]) * 0.5f;
            lv.z = (n[f][2] + sx[i] * u[f][2] + sy[i] * w[f][2]) * 0.5f;
            lv.nx = n[f][0]; lv.ny = n[f][1]; lv.nz = n[f][2];
            lv.u = (sx[i] + 1) * 0.5f; lv.v = (1 - sy[i]) * 0.5f;
            v.push_back(lv);
        }
        WORD b = (WORD)(f * 4);
        idx.push_back(b); idx.push_back(b + 1); idx.push_back(b + 2);
        idx.push_back(b); idx.push_back(b + 2); idx.push_back(b + 3);
    }
}

static IDirect3DTexture8 *MakeDXT1()
{
    // 16x16 DXT1 with a 4x4 block pattern: red/green alternating plus a punch-through block.
    IDirect3DTexture8 *tex = nullptr;
    HRESULT hr = g_dev->CreateTexture(16, 16, 0, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &tex);
    if (FAILED(hr)) { printf("DXT1 creation failed 0x%x\n", (unsigned)hr); return nullptr; }
    for (UINT l = 0; l < tex->GetLevelCount(); ++l)
    {
        D3DSURFACE_DESC d;
        tex->GetLevelDesc(l, &d);
        D3DLOCKED_RECT lr;
        CHECK(tex->LockRect(l, &lr, nullptr, 0));
        UINT bw = (d.Width + 3) / 4, bh = (d.Height + 3) / 4;
        for (UINT by = 0; by < bh; ++by)
            for (UINT bx = 0; bx < bw; ++bx)
            {
                BYTE *blk = (BYTE *)lr.pBits + by * lr.Pitch + bx * 8;
                WORD c0 = ((bx + by) & 1) ? 0xF800 : 0x07E0; // red / green
                WORD c1 = ((bx + by) & 1) ? 0x001F : 0xFFE0; // blue / yellow
                memcpy(blk, &c0, 2);
                memcpy(blk + 2, &c1, 2);
                // indices: left half c0, right half c1, a gradient row at the bottom
                BYTE rows[4] = {0x50, 0x50, 0x50, 0xE4};
                memcpy(blk + 4, rows, 4);
            }
        CHECK(tex->UnlockRect(l));
    }
    return tex;
}


static bool g_shaders = false;
static int g_wantBackend = DXWEBGL2_BACKEND_WEBGL2; // the backend the device must render through (--backend=)
static DWORD g_vs = 0, g_psMul = 0, g_psBem = 0, g_ps14 = 0;
static IDirect3DTexture8 *g_bump = nullptr;
static DWORD g_inlineShaders[4] = {};

static DWORD AssemblePS(const char *src)
{
    ID3DXBuffer *code = nullptr, *errs = nullptr;
    HRESULT hr = D3DXAssembleShader(src, (UINT)strlen(src), 0, nullptr, &code, &errs);
    if (FAILED(hr))
    {
        printf("assemble failed: %s\n", errs ? (const char *)errs->GetBufferPointer() : "?");
        return 0;
    }
    DWORD h = 0;
    hr = g_dev->CreatePixelShader((const DWORD *)code->GetBufferPointer(), &h);
    if (FAILED(hr)) printf("CreatePixelShader failed 0x%x\n", (unsigned)hr);
    code->Release();
    return h;
}

static void CreateShaders()
{
    ID3DXBuffer *code = nullptr, *errs = nullptr;
    const char *vsSrc =
        "vs.1.1\n"
        "dp4 oPos.x, v0, c0\n"
        "dp4 oPos.y, v0, c1\n"
        "dp4 oPos.z, v0, c2\n"
        "dp4 oPos.w, v0, c3\n"
        "mul oD0, v5, c4\n"
        "mov oT0, v7\n"
        "mov oT1, v7\n";
    HRESULT hr = D3DXAssembleShader(vsSrc, (UINT)strlen(vsSrc), 0, nullptr, &code, &errs);
    if (FAILED(hr)) { printf("vs assemble failed: %s\n", errs ? (const char *)errs->GetBufferPointer() : "?"); return; }
    DWORD decl[] = {D3DVSD_STREAM(0), D3DVSD_REG(0, D3DVSDT_FLOAT3), D3DVSD_REG(5, D3DVSDT_D3DCOLOR),
                    D3DVSD_REG(7, D3DVSDT_FLOAT2), D3DVSD_END()};
    hr = g_dev->CreateVertexShader(decl, (const DWORD *)code->GetBufferPointer(), &g_vs, 0);
    printf("CreateVertexShader -> 0x%x handle %u\n", (unsigned)hr, (unsigned)g_vs);
    EXPECT(SUCCEEDED(hr));
    code->Release();

    g_psMul = AssemblePS("ps.1.1\ntex t0\ntex t1\nmul r0, v0, t0\nmad r0.rgb, t1, c0, r0\n");
    g_psBem = AssemblePS("ps.1.1\ntex t0\ntexbem t1, t0\nmov r0, t1\n");
    g_ps14 = AssemblePS("ps.1.4\ntexld r0, t0\nmul r0, r0, v0\n");
    printf("pixel shaders: %u %u %u\n", (unsigned)g_psMul, (unsigned)g_psBem, (unsigned)g_ps14);

    // Representative inline pixel shaders as applications embed them in their sources: co-issue, instruction
    // modifiers, source modifiers, comments, line continuations inside string literals.
    {
        const char *blend =
            "ps.1.1\n \
            tex t0 \n\
            tex t1 \n\
            tex t2 \n\
            tex t3 \n\
            mul_x2 r0.rgb, v0, t0 ; modulate with the vertex color and double. \n\
            +mov r0.a, t0 ; alpha comes from the base texture. \n\
            lrp r1.rgb, t1.a, t2, t3 ; blend two detail layers by the alpha of the first. \n\
            add_sat r0.rgb, r0, r1\n";
        const char *bump =
            "ps.1.1\n \
            tex t0 \n\
            tex t1 \n\
            texbem t2, t1 ; perturb the lookup of t2 with the bump map in t1.\n\
            mul r0, v0, t0 \n\
            mad r0.rgb, t2, c0, r0 ; add the scaled environment layer.";
        const char *dot =
            "ps.1.1\n \
            tex t0 ;a normal map\n\
            tex t1 ;a vector in tangent space\n\
            tex t2\n\
            dp3_sat r1, t0_bx2, t1_bx2 ; N.L\n\
            mul r0.rgb, r1, t2\n\
            +mul r0.a, v0, t2 \n\
            ;\n";
        const char *swizzle =
            "ps.1.4\n"
            "texld r0, t0\n"
            "mov r1.rgb, r0.r\n"
            "mov r2.rgb, r0.g\n"
            "mul r0.rgb, r1, c0\n"
            "mad r0.rgb, r2, c1, r0\n";
        g_inlineShaders[0] = AssemblePS(blend);
        g_inlineShaders[1] = AssemblePS(bump);
        g_inlineShaders[2] = AssemblePS(dot);
        g_inlineShaders[3] = AssemblePS(swizzle);
        printf("inline shaders blend: %u bump: %u dot: %u swizzle(ps1.4): %u\n", (unsigned)g_inlineShaders[0],
               (unsigned)g_inlineShaders[1], (unsigned)g_inlineShaders[2], (unsigned)g_inlineShaders[3]);
        for (int i = 0; i < 4; ++i) EXPECT(g_inlineShaders[i] != 0);
    }

    // Signed bump map: du/dv pattern.
    g_dev->CreateTexture(16, 16, 1, 0, D3DFMT_V8U8, D3DPOOL_MANAGED, &g_bump);
    if (g_bump)
    {
        D3DLOCKED_RECT lr;
        g_bump->LockRect(0, &lr, nullptr, 0);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
            {
                signed char *p = (signed char *)((BYTE *)lr.pBits + y * lr.Pitch) + x * 2;
                p[0] = (signed char)((x < 8) ? 100 : -100); // U
                p[1] = 0;                                      // V
            }
        g_bump->UnlockRect(0);
    }
}

static void RenderShaderScene(IDirect3DTexture8 *checker, IDirect3DTexture8 *gradient)
{
    struct SV { float x, y, z; DWORD color; float u, v; };
    SV q[4] = {{-0.9f, -0.9f, 0.5f, 0xFFFFFF80, 0, 1}, {-0.9f, 0.9f, 0.5f, 0xFF80FFFF, 0, 0},
               {0.9f, 0.9f, 0.5f, 0xFFFF80FF, 1, 0}, {0.9f, -0.9f, 0.5f, 0xFFFFFFFF, 1, 1}};
    WORD qi[6] = {0, 1, 2, 0, 2, 3};
    D3DXMATRIX id;
    D3DXMatrixIdentity(&id);

    // 1) Vertex shader (vs.1.1) with fixed-function texturing.
    SetCommonStates();
    g_dev->SetTransform(D3DTS_WORLD, &id);
    g_dev->SetTransform(D3DTS_VIEW, &id);
    g_dev->SetTransform(D3DTS_PROJECTION, &id);
    g_dev->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
    SetViewport(0, 0, 100, 100);
    g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_dev->SetTexture(0, checker);
    float c0[4][4];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) c0[i][j] = (i == j) ? 1.0f : 0.0f;
    g_dev->SetVertexShaderConstant(0, c0, 4);
    float tint[4] = {1.0f, 0.6f, 0.6f, 1.0f};
    g_dev->SetVertexShaderConstant(4, tint, 1);
    DWORD decl;
    (void)decl;
    g_dev->SetVertexShader(g_vs);
    CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, qi, D3DFMT_INDEX16, q, sizeof(SV)));
    g_dev->SetVertexShader(TEX_FVF);

    // 2) ps.1.1 with two textures and a constant.
    SetViewport(100, 0, 100, 100);
    g_dev->SetTexture(0, checker);
    g_dev->SetTexture(1, gradient);
    float k[4] = {0.0f, 0.5f, 0.0f, 0.0f};
    g_dev->SetPixelShaderConstant(0, k, 1);
    g_dev->SetPixelShader(g_psMul);
    CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, qi, D3DFMT_INDEX16, q, sizeof(SV)));

    // 3) texbem: the checker is displaced horizontally by the signed bump map.
    SetViewport(200, 0, 100, 100);
    g_dev->SetTexture(0, g_bump);
    g_dev->SetTexture(1, checker);
    g_dev->SetTextureStageState(1, D3DTSS_BUMPENVMAT00, FloatToDword(0.25f));
    g_dev->SetTextureStageState(1, D3DTSS_BUMPENVMAT01, FloatToDword(0.0f));
    g_dev->SetTextureStageState(1, D3DTSS_BUMPENVMAT10, FloatToDword(0.0f));
    g_dev->SetTextureStageState(1, D3DTSS_BUMPENVMAT11, FloatToDword(0.25f));
    g_dev->SetPixelShader(g_psBem);
    CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, qi, D3DFMT_INDEX16, q, sizeof(SV)));

    // 4) ps.1.4 texld.
    SetViewport(300, 0, 20, 100);
    g_dev->SetTexture(0, gradient);
    g_dev->SetPixelShader(g_ps14);
    CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, qi, D3DFMT_INDEX16, q, sizeof(SV)));

    // 5) The inline shaders, drawn tiny (compiles and links them).
    SetViewport(330, 100, 8, 8);
    for (int s = 0; s < 4; ++s)
    {
        g_dev->SetTexture(0, checker);
        g_dev->SetTexture(1, s == 1 ? g_bump : checker);
        g_dev->SetTexture(2, checker);
        g_dev->SetTexture(3, checker);
        for (int st = 0; st < 4; ++st) g_dev->SetTextureStageState(st, D3DTSS_TEXCOORDINDEX, 0);
        g_dev->SetPixelShader(g_inlineShaders[s]);
        CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, qi, D3DFMT_INDEX16, q, sizeof(SV)));
    }
    for (int st = 1; st < 4; ++st) g_dev->SetTexture(st, nullptr);

    g_dev->SetPixelShader(0);
    g_dev->SetTexture(1, nullptr);
    SetViewport(0, 0, 640, 480);
}


//------------------------------------------------------------------------------
// Scene set 2: texture generation, projection, cube/volume maps, point sprites,
// lights, stencil, wireframe and clip planes (--scene2).
//------------------------------------------------------------------------------
static bool g_scene2 = false;

struct SphereVertex { float x, y, z, nx, ny, nz; };
#define SPHERE_FVF (D3DFVF_XYZ | D3DFVF_NORMAL)

static void BuildSphere(std::vector<SphereVertex> &v, std::vector<WORD> &idx, int n)
{
    for (int i = 0; i <= n; ++i)
        for (int j = 0; j <= n; ++j)
        {
            float th = 3.14159265f * i / n, ph = 2.0f * 3.14159265f * j / n;
            SphereVertex s;
            s.x = std::sin(th) * std::cos(ph); s.y = std::cos(th); s.z = std::sin(th) * std::sin(ph);
            s.nx = s.x; s.ny = s.y; s.nz = s.z;
            v.push_back(s);
        }
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
        {
            WORD a = (WORD)(i * (n + 1) + j), b = (WORD)(a + 1), c = (WORD)(a + n + 1), d = (WORD)(c + 1);
            idx.push_back(a); idx.push_back(b); idx.push_back(c);
            idx.push_back(b); idx.push_back(d); idx.push_back(c);
        }
}

static void Cell(int i)
{
    SetViewport((i % 4) * 160, (i / 4) * 160, 160, 160);
}

static void CameraTransforms(float dist = 3.0f)
{
    D3DXMATRIX world, view, proj;
    D3DXMatrixIdentity(&world);
    D3DXVECTOR3 eye(0, 0, -dist), at(0, 0, 0), up(0, 1, 0);
    D3DXMatrixLookAtLH(&view, &eye, &at, &up);
    D3DXMatrixPerspectiveFovLH(&proj, 0.8f, 1.0f, 0.5f, 30.0f);
    g_dev->SetTransform(D3DTS_WORLD, &world);
    g_dev->SetTransform(D3DTS_VIEW, &view);
    g_dev->SetTransform(D3DTS_PROJECTION, &proj);
}

static void RenderScene2(IDirect3DTexture8 *checker)
{
    static std::vector<SphereVertex> sv;
    static std::vector<WORD> si;
    if (sv.empty()) BuildSphere(sv, si, 24);
    const UINT ns = (UINT)sv.size(), nt = (UINT)si.size() / 3;

    static IDirect3DCubeTexture8 *cube = nullptr;
    static IDirect3DVolumeTexture8 *vol = nullptr;
    if (!cube)
    {
        CHECK(g_dev->CreateCubeTexture(32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &cube));
        static const DWORD faceColor[6] = {0xFFFF2020, 0xFF802020, 0xFF20FF20, 0xFF208020, 0xFF2020FF, 0xFF202080};
        for (int f = 0; f < 6; ++f)
        {
            D3DLOCKED_RECT lr;
            CHECK(cube->LockRect((D3DCUBEMAP_FACES)f, 0, &lr, nullptr, 0));
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x)
                    ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = ((x / 8 + y / 8) & 1) ? faceColor[f] : (faceColor[f] | 0x00606060);
            CHECK(cube->UnlockRect((D3DCUBEMAP_FACES)f, 0));
        }
        CHECK(g_dev->CreateVolumeTexture(8, 8, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &vol));
        D3DLOCKED_BOX lb;
        CHECK(vol->LockBox(0, &lb, nullptr, 0));
        for (int z = 0; z < 8; ++z)
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    ((DWORD *)((BYTE *)lb.pBits + z * lb.SlicePitch + y * lb.RowPitch))[x] = 0xFF000000 | ((x * 36) << 16) | ((y * 36) << 8) | (z * 36);
        CHECK(vol->UnlockBox(0));
    }

    // Cell 0: environment mapped sphere (camera space reflection vector -> cube map).
    {
        SetCommonStates();
        Cell(0);
        CameraTransforms();
        g_dev->SetTexture(0, cube);
        g_dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
        g_dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3);
        D3DXMATRIX invView, view;
        g_dev->GetTransform(D3DTS_VIEW, &view);
        D3DXMatrixInverse(&invView, nullptr, &view);
        g_dev->SetTransform(D3DTS_TEXTURE0, &invView);
        g_dev->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
        g_dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        g_dev->SetVertexShader(SPHERE_FVF);
        CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, ns, nt, si.data(), D3DFMT_INDEX16, sv.data(), sizeof(SphereVertex)));
    }

    // Cell 1: projected texture on a ground plane (camera space position + projective transform).
    {
        SetCommonStates();
        Cell(1);
        D3DXMATRIX world, view, proj;
        D3DXMatrixIdentity(&world);
        D3DXVECTOR3 eye(0, 2.2f, -3.0f), at(0, -1, 1), up(0, 1, 0);
        D3DXMatrixLookAtLH(&view, &eye, &at, &up);
        D3DXMatrixPerspectiveFovLH(&proj, 0.9f, 1.0f, 0.5f, 30.0f);
        g_dev->SetTransform(D3DTS_WORLD, &world);
        g_dev->SetTransform(D3DTS_VIEW, &view);
        g_dev->SetTransform(D3DTS_PROJECTION, &proj);
        // Projector: looks straight down from above (0,3,1), orthographic 2x2 world units.
        D3DXMATRIX pView, pProj, bias, invView, m;
        D3DXVECTOR3 pe(0, 3, 1), pa(0, -1, 1), pu(0, 0, 1);
        D3DXMatrixLookAtLH(&pView, &pe, &pa, &pu);
        D3DXMatrixOrthoLH(&pProj, 2.0f, 2.0f, 0.1f, 10.0f);
        D3DXMatrixIdentity(&bias);
        bias._11 = 0.5f; bias._22 = -0.5f; bias._33 = 1.0f; bias._41 = 0.5f; bias._42 = 0.5f;
        D3DXMatrixInverse(&invView, nullptr, &view);
        D3DXMatrixMultiply(&m, &invView, &pView);
        D3DXMatrixMultiply(&m, &m, &pProj);
        D3DXMatrixMultiply(&m, &m, &bias);
        g_dev->SetTransform(D3DTS_TEXTURE0, &m);
        g_dev->SetTexture(0, checker);
        g_dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
        g_dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3 | D3DTTFF_PROJECTED);
        g_dev->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
        g_dev->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
        g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        ColVertex gv[4] = {{-3, -1, -1, 0xFF804020}, {-3, -1, 4, 0xFF804020}, {3, -1, 4, 0xFF804020}, {3, -1, -1, 0xFF804020}};
        WORD gi[6] = {0, 1, 2, 0, 2, 3};
        g_dev->SetVertexShader(COL_FVF);
        CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, gi, D3DFMT_INDEX16, gv, sizeof(ColVertex)));
    }

    // Cell 2: point sprites (textured, distance scaled).
    {
        SetCommonStates();
        Cell(2);
        CameraTransforms(4.0f);
        g_dev->SetTexture(0, checker);
        g_dev->SetRenderState(D3DRS_POINTSPRITEENABLE, TRUE);
        g_dev->SetRenderState(D3DRS_POINTSCALEENABLE, TRUE);
        g_dev->SetRenderState(D3DRS_POINTSIZE, FloatToDword(0.35f));
        g_dev->SetRenderState(D3DRS_POINTSIZE_MIN, FloatToDword(1.0f));
        g_dev->SetRenderState(D3DRS_POINTSCALE_A, FloatToDword(0.0f));
        g_dev->SetRenderState(D3DRS_POINTSCALE_B, FloatToDword(0.0f));
        g_dev->SetRenderState(D3DRS_POINTSCALE_C, FloatToDword(0.05f));
        ColVertex pts[4] = {{-1, -1, 0, 0xFFFF8080}, {1, -1, 1, 0xFF80FF80}, {-1, 1, 2, 0xFF8080FF}, {1, 1, 3, 0xFFFFFF80}};
        g_dev->SetVertexShader(COL_FVF);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_POINTLIST, 4, pts, sizeof(ColVertex)));
        g_dev->SetRenderState(D3DRS_POINTSPRITEENABLE, FALSE);
        g_dev->SetRenderState(D3DRS_POINTSCALEENABLE, FALSE);
    }

    // Cell 3: volume texture.
    {
        SetCommonStates();
        Cell(3);
        CameraTransforms();
        g_dev->SetTexture(0, vol);
        g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        struct VV { float x, y, z; DWORD c; float u, v, w; } vq[4] = {
            {-1, -1, 0, 0xFFFFFFFF, 0, 1, 0.1f}, {-1, 1, 0, 0xFFFFFFFF, 0, 0, 0.5f}, {1, 1, 0, 0xFFFFFFFF, 1, 0, 0.9f}, {1, -1, 0, 0xFFFFFFFF, 1, 1, 0.5f}};
        WORD vi[6] = {0, 1, 2, 0, 2, 3};
        g_dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0));
        g_dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
        g_dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
        CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, vi, D3DFMT_INDEX16, vq, sizeof(VV)));
    }

    // Cell 4: point + spot lights with specular on a sphere.
    {
        SetCommonStates();
        Cell(4);
        CameraTransforms();
        D3DMATERIAL8 mat;
        memset(&mat, 0, sizeof mat);
        mat.Diffuse.r = mat.Diffuse.g = mat.Diffuse.b = mat.Diffuse.a = 0.8f;
        mat.Ambient = mat.Diffuse;
        mat.Specular.r = mat.Specular.g = mat.Specular.b = 1.0f;
        mat.Power = 25.0f;
        g_dev->SetMaterial(&mat);
        D3DLIGHT8 l0, l1;
        memset(&l0, 0, sizeof l0);
        l0.Type = D3DLIGHT_POINT;
        l0.Diffuse.r = 1.0f; l0.Diffuse.g = 0.3f; l0.Diffuse.b = 0.3f;
        l0.Specular.r = l0.Specular.g = l0.Specular.b = 1.0f;
        l0.Position.x = 2.0f; l0.Position.y = 1.0f; l0.Position.z = -2.0f;
        l0.Range = 20.0f; l0.Attenuation1 = 0.15f;
        memset(&l1, 0, sizeof l1);
        l1.Type = D3DLIGHT_SPOT;
        l1.Diffuse.r = 0.2f; l1.Diffuse.g = 0.4f; l1.Diffuse.b = 1.0f;
        l1.Position.x = -2.5f; l1.Position.y = 0.5f; l1.Position.z = -2.5f;
        l1.Direction.x = 2.5f; l1.Direction.y = -0.5f; l1.Direction.z = 2.0f;
        l1.Range = 20.0f; l1.Attenuation0 = 1.0f; l1.Theta = 0.5f; l1.Phi = 1.0f; l1.Falloff = 1.0f;
        g_dev->SetLight(0, &l0);
        g_dev->SetLight(1, &l1);
        g_dev->LightEnable(0, TRUE);
        g_dev->LightEnable(1, TRUE);
        g_dev->SetRenderState(D3DRS_LIGHTING, TRUE);
        g_dev->SetRenderState(D3DRS_SPECULARENABLE, TRUE);
        g_dev->SetRenderState(D3DRS_COLORVERTEX, FALSE);
        g_dev->SetRenderState(D3DRS_AMBIENT, 0xFF202020);
        g_dev->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        g_dev->SetVertexShader(SPHERE_FVF);
        CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, ns, nt, si.data(), D3DFMT_INDEX16, sv.data(), sizeof(SphereVertex)));
    }

    // Cell 5: stencil (diamond mask) then a colored quad drawn only inside it.
    {
        SetCommonStates();
        Cell(5);
        CameraTransforms();
        g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        g_dev->SetRenderState(D3DRS_STENCILENABLE, TRUE);
        g_dev->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
        g_dev->SetRenderState(D3DRS_STENCILREF, 1);
        g_dev->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
        g_dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
        ColVertex diamond[4] = {{0, -1, 0, 0xFFFFFFFF}, {-1, 0, 0, 0xFFFFFFFF}, {0, 1, 0, 0xFFFFFFFF}, {1, 0, 0, 0xFFFFFFFF}};
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        g_dev->SetVertexShader(COL_FVF);
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, diamond, sizeof(ColVertex)));
        g_dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
        g_dev->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
        g_dev->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
        ColVertex big[4] = {{-1.4f, -1.4f, 0, 0xFFFF4040}, {-1.4f, 1.4f, 0, 0xFF40FF40}, {1.4f, 1.4f, 0, 0xFF4040FF}, {1.4f, -1.4f, 0, 0xFFFFFF40}};
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, big, sizeof(ColVertex)));
        g_dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    }

    // Cell 6: wireframe sphere.
    {
        SetCommonStates();
        Cell(6);
        CameraTransforms();
        g_dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        g_dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        g_dev->SetVertexShader(SPHERE_FVF);
        CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, ns, nt, si.data(), D3DFMT_INDEX16, sv.data(), sizeof(SphereVertex)));
        g_dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    }

    // Cell 7: user clip plane (keeps x < 0.2) over a gradient quad, with alpha-blended + additive passes.
    {
        SetCommonStates();
        Cell(7);
        CameraTransforms();
        g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        float plane[4] = {-1.0f, 0.0f, 0.0f, 0.2f};
        g_dev->SetClipPlane(0, plane);
        g_dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 1);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        ColVertex cq[4] = {{-1.4f, -1.4f, 0, 0xFFFF2020}, {-1.4f, 1.4f, 0, 0xFFFFFF20}, {1.4f, 1.4f, 0, 0xFF20FF20}, {1.4f, -1.4f, 0, 0xFF2020FF}};
        g_dev->SetVertexShader(COL_FVF);
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, cq, sizeof(ColVertex)));
        g_dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
        // Additive overlay.
        g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        g_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        g_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
        ColVertex ov[4] = {{-0.5f, -0.5f, 0, 0xFF404040}, {-0.5f, 0.5f, 0, 0xFF404040}, {0.5f, 0.5f, 0, 0xFF404040}, {0.5f, -0.5f, 0, 0xFF404040}};
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, ov, sizeof(ColVertex)));
    }
}


//------------------------------------------------------------------------------
// Scene set 3: texture formats, UpdateTexture, CopyRects and D3DX (--scene3).
// Every cell fills a texture with a known color and probes the rendered result.
//------------------------------------------------------------------------------
static bool g_scene3 = false;

struct FmtCase { D3DFORMAT fmt; const char *name; DWORD argb; DWORD expect; };

static void RenderScene3()
{
    static const FmtCase cases[] = {
        {D3DFMT_A8R8G8B8, "A8R8G8B8", 0xFF3070C0, 0x3070C0},
        {D3DFMT_X8R8G8B8, "X8R8G8B8", 0x003070C0, 0x3070C0},
        {D3DFMT_R5G6B5, "R5G6B5", 0xFF3070C0, 0x316DC6},
        {D3DFMT_A1R5G5B5, "A1R5G5B5", 0xFF3070C0, 0x316BC6},
        {D3DFMT_A4R4G4B4, "A4R4G4B4", 0xFF3070C0, 0x3377CC},
        {D3DFMT_L8, "L8", 0xFF808080, 0x808080},
        {D3DFMT_A8, "A8 (rgb 0)", 0x80FFFFFF, 0x000000},
        {D3DFMT_A8L8, "A8L8", 0xFF404040, 0x404040},
        {D3DFMT_DXT1, "DXT1", 0, 0x00FF00},
    };
    D3DXMATRIX id;
    D3DXMatrixIdentity(&id);
    struct QV { float x, y, z, rhw; DWORD c; float u, v; };
    int cell = 0;
    for (const FmtCase &fc : cases)
    {
        SetCommonStates();
        const int cx = (cell % 4) * 160, cy = (cell / 4) * 160;
        ++cell;
        SetViewport(cx, cy, 160, 160);
        g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        IDirect3DTexture8 *sys = nullptr, *tex = nullptr;
        HRESULT hr = g_dev->CreateTexture(16, 16, 1, 0, fc.fmt, D3DPOOL_SYSTEMMEM, &sys);
        if (FAILED(hr)) { printf("CASE %s: create failed 0x%x\n", fc.name, (unsigned)hr); continue; }
        D3DLOCKED_RECT lr;
        sys->LockRect(0, &lr, nullptr, 0);
        if (fc.fmt == D3DFMT_DXT1)
        {
            for (int by = 0; by < 4; ++by)
                for (int bx = 0; bx < 4; ++bx)
                {
                    BYTE *blk = (BYTE *)lr.pBits + by * lr.Pitch + bx * 8;
                    WORD c0 = 0x07E0, c1 = 0x07E0;
                    memcpy(blk, &c0, 2); memcpy(blk + 2, &c1, 2);
                    memset(blk + 4, 0, 4);
                }
        }
        else
        {
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                {
                    BYTE *p = (BYTE *)lr.pBits + y * lr.Pitch;
                    DWORD a = fc.argb >> 24, r = (fc.argb >> 16) & 255, g = (fc.argb >> 8) & 255, b = fc.argb & 255;
                    switch (fc.fmt)
                    {
                    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: ((DWORD *)p)[x] = fc.argb; break;
                    case D3DFMT_R5G6B5: ((WORD *)p)[x] = (WORD)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); break;
                    case D3DFMT_A1R5G5B5: ((WORD *)p)[x] = (WORD)((a ? 0x8000 : 0) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)); break;
                    case D3DFMT_A4R4G4B4: ((WORD *)p)[x] = (WORD)(((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)); break;
                    case D3DFMT_L8: p[x] = (BYTE)r; break;
                    case D3DFMT_A8: p[x] = (BYTE)a; break;
                    case D3DFMT_A8L8: ((WORD *)p)[x] = (WORD)((a << 8) | r); break;
                    default: break;
                    }
                }
        }
        sys->UnlockRect(0);
        hr = g_dev->CreateTexture(16, 16, 1, 0, fc.fmt, D3DPOOL_DEFAULT, &tex);
        if (FAILED(hr)) { printf("CASE %s: default create failed 0x%x\n", fc.name, (unsigned)hr); sys->Release(); continue; }
        CHECK(g_dev->UpdateTexture(sys, tex));
        sys->Release();
        g_dev->SetTexture(0, tex);
        g_dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
        g_dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        g_dev->SetVertexShader(RHW_FVF);
        QV q[4] = {{(float)cx + 10, (float)cy + 10, 0.5f, 1, 0xFFFFFFFF, 0, 0}, {(float)cx + 150, (float)cy + 10, 0.5f, 1, 0xFFFFFFFF, 1, 0},
                   {(float)cx + 10, (float)cy + 150, 0.5f, 1, 0xFFFFFFFF, 0, 1}, {(float)cx + 150, (float)cy + 150, 0.5f, 1, 0xFFFFFFFF, 1, 1}};
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(QV)));
        tex->Release();
    }
    // Lighting checks: a quad facing the camera, lit head-on by a directional light.
    // Cell 9: identity world. Cell 10: rotated world (the normal matrix must follow).
    // Both must come out as diffuse * 1.0 = (127, 64, 191).
    for (int k = 0; k < 2; ++k)
    {
        SetCommonStates();
        const int c = 9 + k;
        SetViewport((c % 4) * 160, (c / 4) * 160, 160, 160);
        g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        D3DXMATRIX world, view, proj, rot;
        D3DXVECTOR3 eye(0, 0, -3), at(0, 0, 0), up(0, 1, 0);
        D3DXMatrixLookAtLH(&view, &eye, &at, &up);
        D3DXMatrixPerspectiveFovLH(&proj, 0.8f, 1.0f, 0.5f, 30.0f);
        // Rotating the world about Y by 0.5 rad turns the quad normal (0,0,-1) to (-sin, 0, -cos).
        D3DXMatrixRotationY(&world, k ? 0.5f : 0.0f);
        g_dev->SetTransform(D3DTS_WORLD, &world);
        g_dev->SetTransform(D3DTS_VIEW, &view);
        g_dev->SetTransform(D3DTS_PROJECTION, &proj);
        D3DMATERIAL8 mat;
        memset(&mat, 0, sizeof mat);
        mat.Diffuse.r = 0.5f; mat.Diffuse.g = 0.25f; mat.Diffuse.b = 0.75f; mat.Diffuse.a = 1.0f;
        g_dev->SetMaterial(&mat);
        D3DLIGHT8 l;
        memset(&l, 0, sizeof l);
        l.Type = D3DLIGHT_DIRECTIONAL;
        l.Diffuse.r = l.Diffuse.g = l.Diffuse.b = 1.0f;
        // Light travels along +normal-opposite: direction = -(rotated normal) in WORLD space.
        const float a = k ? 0.5f : 0.0f;
        l.Direction.x = std::sin(a); l.Direction.y = 0.0f; l.Direction.z = std::cos(a);
        g_dev->SetLight(0, &l);
        g_dev->LightEnable(0, TRUE);
        g_dev->SetRenderState(D3DRS_LIGHTING, TRUE);
        g_dev->SetRenderState(D3DRS_COLORVERTEX, FALSE);
        g_dev->SetRenderState(D3DRS_AMBIENT, 0);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        struct LV { float x, y, z, nx, ny, nz; } lq[4] = {{-1, -1, 0, 0, 0, -1}, {-1, 1, 0, 0, 0, -1}, {1, 1, 0, 0, 0, -1}, {1, -1, 0, 0, 0, -1}};
        g_dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_NORMAL);
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, lq, sizeof(LV)));
    }
    // World light direction for cell 10 above is (sin a, 0, cos a) = the light travelling toward +z/+x,
    // i.e. hitting a surface whose normal is (-sin a, 0, -cos a): exactly the rotated quad normal.
    SetViewport(0, 0, 640, 480);
}

static void RenderFrame(float t, IDirect3DTexture8 *checker, IDirect3DTexture8 *gradient, IDirect3DTexture8 *dxt,
                        IDirect3DTexture8 *rtTex, IDirect3DSurface8 *rtDepth, IDirect3DSurface8 *bbSurface,
                        IDirect3DSurface8 *bbDepth, bool doRT)
{
    // Render-to-texture first.
    if (doRT)
    {
        IDirect3DSurface8 *rtSurf = nullptr;
        CHECK(rtTex->GetSurfaceLevel(0, &rtSurf));
        CHECK(g_dev->SetRenderTarget(rtSurf, rtDepth));
        SetViewport(0, 0, 128, 128);
        CHECK(g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF303060, 1.0f, 0));
        SetCommonStates();
        g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        g_dev->SetVertexShader(COL_FVF);
        // Triangle: apex (red) at the top, green bottom-left, blue bottom-right.
        ColVertex tri[3] = {{0.0f, 0.8f, 0.5f, 0xFFFF0000}, {0.8f, -0.8f, 0.5f, 0xFF0000FF}, {-0.8f, -0.8f, 0.5f, 0xFF00FF00}};
        D3DXMATRIX id;
        D3DXMatrixIdentity(&id);
        g_dev->SetTransform(D3DTS_WORLD, &id);
        g_dev->SetTransform(D3DTS_VIEW, &id);
        g_dev->SetTransform(D3DTS_PROJECTION, &id);
        CHECK(g_dev->BeginScene());
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, tri, sizeof(ColVertex)));
        CHECK(g_dev->EndScene());
        CHECK(g_dev->SetRenderTarget(bbSurface, bbDepth));
        rtSurf->Release();
    }

    SetViewport(0, 0, 640, 480);
    CHECK(g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0xFF203040, 1.0f, 0));
    CHECK(g_dev->BeginScene());
    if (g_scene3)
    {
        RenderScene3();
        CHECK(g_dev->EndScene());
        SetViewport(0, 0, 640, 480);
        CHECK(g_dev->Present(nullptr, nullptr, nullptr, nullptr));
        return;
    }
    if (g_scene2)
    {
        RenderScene2(checker);
        CHECK(g_dev->EndScene());
        SetViewport(0, 0, 640, 480);
        CHECK(g_dev->Present(nullptr, nullptr, nullptr, nullptr));
        return;
    }

    //-------------------------------------------------------------------
    // Scene A (top-left): lit, textured, rotating cube (indexed, static VB/IB).
    //-------------------------------------------------------------------
    {
        SetCommonStates();
        SetViewport(0, 0, 320, 240);
        D3DXMATRIX world, rx, ry, view, proj;
        D3DXMatrixRotationY(&ry, 0.6f + t);
        D3DXMatrixRotationX(&rx, 0.5f);
        D3DXMatrixMultiply(&world, &ry, &rx);
        D3DXVECTOR3 eye(0, 1.0f, -3.2f), at(0, 0, 0), up(0, 1, 0);
        D3DXMatrixLookAtLH(&view, &eye, &at, &up);
        D3DXMatrixPerspectiveFovLH(&proj, 0.9f, 320.0f / 240.0f, 0.5f, 20.0f);
        g_dev->SetTransform(D3DTS_WORLD, &world);
        g_dev->SetTransform(D3DTS_VIEW, &view);
        g_dev->SetTransform(D3DTS_PROJECTION, &proj);

        D3DMATERIAL8 mat;
        memset(&mat, 0, sizeof mat);
        mat.Diffuse.r = 1.0f; mat.Diffuse.g = 0.85f; mat.Diffuse.b = 0.6f; mat.Diffuse.a = 1.0f;
        mat.Ambient = mat.Diffuse;
        g_dev->SetMaterial(&mat);
        D3DLIGHT8 light;
        memset(&light, 0, sizeof light);
        light.Type = D3DLIGHT_DIRECTIONAL;
        light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = 1.0f;
        light.Direction.x = -0.5f; light.Direction.y = -0.6f; light.Direction.z = 0.7f;
        g_dev->SetLight(0, &light);
        g_dev->LightEnable(0, TRUE);
        g_dev->SetRenderState(D3DRS_LIGHTING, TRUE);
        g_dev->SetRenderState(D3DRS_AMBIENT, 0xFF404040);
        g_dev->SetRenderState(D3DRS_COLORVERTEX, FALSE);
        g_dev->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
        g_dev->SetTexture(0, checker);

        static IDirect3DVertexBuffer8 *vb = nullptr;
        static IDirect3DIndexBuffer8 *ib = nullptr;
        static int count = 0;
        if (!vb)
        {
            std::vector<LitVertex> v;
            std::vector<WORD> idx;
            BuildCube(v, idx);
            count = (int)idx.size() / 3;
            CHECK(g_dev->CreateVertexBuffer((UINT)(v.size() * sizeof(LitVertex)), D3DUSAGE_WRITEONLY, LIT_FVF, D3DPOOL_MANAGED, &vb));
            BYTE *p;
            CHECK(vb->Lock(0, 0, &p, 0));
            memcpy(p, v.data(), v.size() * sizeof(LitVertex));
            CHECK(vb->Unlock());
            CHECK(g_dev->CreateIndexBuffer((UINT)(idx.size() * 2), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib));
            CHECK(ib->Lock(0, 0, &p, 0));
            memcpy(p, idx.data(), idx.size() * 2);
            CHECK(ib->Unlock());
        }
        CHECK(g_dev->SetStreamSource(0, vb, sizeof(LitVertex)));
        CHECK(g_dev->SetIndices(ib, 0));
        CHECK(g_dev->SetVertexShader(LIT_FVF));
        CHECK(g_dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 24, 0, count));
        g_dev->SetStreamSource(0, nullptr, 0);
        g_dev->SetIndices(nullptr, 0);
    }

    //-------------------------------------------------------------------
    // Scene B (top-right): ground plane with 2 texture stages, fog, TFACTOR.
    //-------------------------------------------------------------------
    {
        SetCommonStates();
        SetViewport(320, 0, 320, 240);
        D3DXMATRIX world, view, proj;
        D3DXMatrixIdentity(&world);
        D3DXVECTOR3 eye(0, 1.5f, -1.0f), at(0, 0, 4), up(0, 1, 0);
        D3DXMatrixLookAtLH(&view, &eye, &at, &up);
        D3DXMatrixPerspectiveFovLH(&proj, 1.0f, 320.0f / 240.0f, 0.3f, 50.0f);
        g_dev->SetTransform(D3DTS_WORLD, &world);
        g_dev->SetTransform(D3DTS_VIEW, &view);
        g_dev->SetTransform(D3DTS_PROJECTION, &proj);
        g_dev->SetRenderState(D3DRS_FOGENABLE, TRUE);
        g_dev->SetRenderState(D3DRS_FOGCOLOR, 0xFF8090A0);
        g_dev->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
        g_dev->SetRenderState(D3DRS_FOGSTART, FloatToDword(2.0f));
        g_dev->SetRenderState(D3DRS_FOGEND, FloatToDword(9.0f));
        g_dev->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFC0C0);
        g_dev->SetTexture(0, checker);
        g_dev->SetTexture(1, gradient);
        g_dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
        g_dev->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        g_dev->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
        g_dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        g_dev->SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_MODULATE);
        g_dev->SetTextureStageState(2, D3DTSS_COLORARG1, D3DTA_TFACTOR);
        g_dev->SetTextureStageState(2, D3DTSS_COLORARG2, D3DTA_CURRENT);
        g_dev->SetTextureStageState(2, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        g_dev->SetTextureStageState(2, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
        Tex2Vertex quad[4] = {
            {-3, 0, 0, 0xFFFFFFFF, 0, 4, 0, 1},
            {-3, 0, 12, 0xFFFFFFFF, 0, 0, 0, 0},
            {3, 0, 12, 0xFFFFFFFF, 4, 0, 1, 0},
            {3, 0, 0, 0xFFFFFFFF, 4, 4, 1, 1},
        };
        WORD qi[6] = {0, 1, 2, 0, 2, 3};
        g_dev->SetVertexShader(TEX2_FVF);
        CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, qi, D3DFMT_INDEX16, quad, sizeof(Tex2Vertex)));
    }

    //-------------------------------------------------------------------
    // Scene C (bottom-left): render target texture on a quad (triangle must point up).
    //-------------------------------------------------------------------
    {
        SetCommonStates();
        SetViewport(0, 240, 320, 240);
        D3DXMATRIX id, ortho;
        D3DXMatrixIdentity(&id);
        D3DXMatrixOrthoLH(&ortho, 4.0f, 3.0f, 0.0f, 10.0f);
        g_dev->SetTransform(D3DTS_WORLD, &id);
        g_dev->SetTransform(D3DTS_VIEW, &id);
        g_dev->SetTransform(D3DTS_PROJECTION, &ortho);
        g_dev->SetTexture(0, rtTex);
        TexVertex q[4] = {
            {-1.5f, -1.2f, 1, 0xFFFFFFFF, 0, 1}, {-1.5f, 1.2f, 1, 0xFFFFFFFF, 0, 0},
            {1.5f, 1.2f, 1, 0xFFFFFFFF, 1, 0}, {1.5f, -1.2f, 1, 0xFFFFFFFF, 1, 1},
        };
        WORD qi[6] = {0, 1, 2, 0, 2, 3};
        g_dev->SetVertexShader(TEX_FVF);
        CHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, qi, D3DFMT_INDEX16, q, sizeof(TexVertex)));
    }

    //-------------------------------------------------------------------
    // Scene D (bottom-right): pre-transformed primitives, DXT1, blending, alpha test, lines, points.
    //-------------------------------------------------------------------
    {
        SetCommonStates();
        SetViewport(320, 240, 320, 240);
        // DXT1 quad.
        g_dev->SetTexture(0, dxt);
        g_dev->SetVertexShader(RHW_FVF);
        g_dev->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
        g_dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
        g_dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
        RhwVertex dq[4] = {
            {10, 10, 0.5f, 1, 0xFFFFFFFF, 0, 0}, {138, 10, 0.5f, 1, 0xFFFFFFFF, 1, 0},
            {10, 138, 0.5f, 1, 0xFFFFFFFF, 0, 1}, {138, 138, 0.5f, 1, 0xFFFFFFFF, 1, 1},
        };
        // Viewport-relative: XYZRHW coordinates are in the viewport.
        for (auto &v : dq) { v.x += 320; v.y += 240; }
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, dq, sizeof(RhwVertex)));

        // Alpha blended, vertex colored quad over the DXT texture.
        g_dev->SetTexture(0, nullptr);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        g_dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        g_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        g_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        RhwVertex bq[4] = {
            {80, 60, 0.4f, 1, 0x80FFFFFF, 0, 0}, {200, 60, 0.4f, 1, 0x80FF0000, 1, 0},
            {80, 180, 0.4f, 1, 0x800000FF, 0, 1}, {200, 180, 0.4f, 1, 0x80FFFF00, 1, 1},
        };
        for (auto &v : bq) { v.x += 320; v.y += 240; }
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, bq, sizeof(RhwVertex)));

        // Alpha-tested checker (alpha ref 128) with checker texture whose alpha toggles.
        g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
        g_dev->SetRenderState(D3DRS_ALPHAREF, 0x80);
        g_dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
        g_dev->SetTexture(0, gradient);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        g_dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        RhwVertex aq[4] = {
            {220, 20, 0.3f, 1, 0xFFFFFFFF, 0, 0}, {300, 20, 0.3f, 1, 0xFFFFFFFF, 1, 0},
            {220, 100, 0.3f, 1, 0xFFFFFFFF, 0, 1}, {300, 100, 0.3f, 1, 0xFFFFFFFF, 1, 1},
        };
        for (auto &v : aq) { v.x += 320; v.y += 240; }
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, aq, sizeof(RhwVertex)));
        g_dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

        // Lines and points.
        g_dev->SetTexture(0, nullptr);
        g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        struct PV { float x, y, z, rhw; DWORD c; };
        PV lines[4] = {{330, 440, 0.2f, 1, 0xFFFFFF00}, {440, 470, 0.2f, 1, 0xFFFFFF00}, {330, 470, 0.2f, 1, 0xFF00FFFF}, {440, 440, 0.2f, 1, 0xFF00FFFF}};
        g_dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_LINELIST, 2, lines, sizeof(PV)));
        g_dev->SetRenderState(D3DRS_POINTSIZE, FloatToDword(9.0f));
        PV pts[3] = {{470, 450, 0.2f, 1, 0xFFFF00FF}, {500, 450, 0.2f, 1, 0xFF00FF00}, {530, 450, 0.2f, 1, 0xFFFFFFFF}};
        CHECK(g_dev->DrawPrimitiveUP(D3DPT_POINTLIST, 3, pts, sizeof(PV)));
    }

    if (g_shaders) RenderShaderScene(checker, gradient);

    CHECK(g_dev->EndScene());
    SetViewport(0, 0, 640, 480);
    CHECK(g_dev->Present(nullptr, nullptr, nullptr, nullptr));
}


//------------------------------------------------------------------------------
// API checks: behaviour that does not need a picture
//------------------------------------------------------------------------------
static void CheckAdapterAndCaps(bool shaders)
{
    // The requested backend, not a silent fallback (AUTO selects WebGL2 for now, docs/WEBGPU.md).
    EXPECT(dxwebgl2_GetBackend() == g_wantBackend);
    EXPECT(g_d3d->GetAdapterCount() == 1);
    D3DADAPTER_IDENTIFIER8 aid;
    EXPECT_HR(g_d3d->GetAdapterIdentifier(0, 0, &aid), D3D_OK);
    EXPECT(aid.Driver[0] != 0 && aid.Description[0] != 0);
    EXPECT_HR(g_d3d->GetAdapterIdentifier(1, 0, &aid), D3DERR_INVALIDCALL);

    D3DDISPLAYMODE m;
    EXPECT(g_d3d->GetAdapterModeCount(0) > 0);
    EXPECT_HR(g_d3d->EnumAdapterModes(0, 0, &m), D3D_OK);
    EXPECT(m.Width >= 640 && m.Height >= 480);
    EXPECT_HR(g_d3d->EnumAdapterModes(0, g_d3d->GetAdapterModeCount(0), &m), D3DERR_INVALIDCALL);
    EXPECT_HR(g_d3d->GetAdapterDisplayMode(0, &m), D3D_OK);

    EXPECT_HR(g_d3d->CheckDeviceType(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DFMT_X8R8G8B8, FALSE), D3D_OK);
    EXPECT(FAILED(g_d3d->CheckDeviceType(0, D3DDEVTYPE_REF, D3DFMT_X8R8G8B8, D3DFMT_X8R8G8B8, FALSE)));
    EXPECT_HR(g_d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8), D3D_OK);
    EXPECT_HR(g_d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE,
                                       D3DFMT_A8R8G8B8), D3D_OK);
    EXPECT_HR(g_d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE,
                                       D3DFMT_D24S8), D3D_OK);
    EXPECT_HR(g_d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE,
                                       D3DFMT_DXT1), D3DERR_NOTAVAILABLE);
    EXPECT_HR(g_d3d->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, TRUE, D3DMULTISAMPLE_NONE), D3D_OK);

    D3DCAPS8 c;
    EXPECT_HR(g_d3d->GetDeviceCaps(0, D3DDEVTYPE_HAL, &c), D3D_OK);
    EXPECT(c.DeviceType == D3DDEVTYPE_HAL);
    EXPECT(c.MaxTextureWidth >= 2048 && c.MaxTextureHeight >= 2048);
    EXPECT(c.MaxSimultaneousTextures >= 2 && c.MaxTextureBlendStages >= 2);
    EXPECT(c.MaxActiveLights >= 8 && c.MaxStreams >= 1 && c.MaxUserClipPlanes >= 1);
    EXPECT((c.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) != 0);
    EXPECT((c.TextureCaps & D3DPTEXTURECAPS_CUBEMAP) != 0 && (c.TextureCaps & D3DPTEXTURECAPS_VOLUMEMAP) != 0);
    if (shaders)
    {
        EXPECT(c.VertexShaderVersion == D3DVS_VERSION(1, 1));
        EXPECT(c.PixelShaderVersion == D3DPS_VERSION(1, 4));
    }
    else
    {
        EXPECT(c.VertexShaderVersion == 0 && c.PixelShaderVersion == 0);
    }
    D3DCAPS8 c2;
    EXPECT_HR(g_dev->GetDeviceCaps(&c2), D3D_OK);
    EXPECT(c2.MaxTextureWidth <= c.MaxTextureWidth + 0u || c2.MaxTextureWidth >= 2048);
    EXPECT_HR(g_dev->TestCooperativeLevel(), D3D_OK);
    IDirect3D8 *d3d = nullptr;
    EXPECT_HR(g_dev->GetDirect3D(&d3d), D3D_OK);
    EXPECT(d3d == g_d3d);
    if (d3d) d3d->Release();
}

static void CheckStateRoundTrips()
{
    DWORD v = 0;
    CHECK(g_dev->SetRenderState(D3DRS_AMBIENT, 0x12345678));
    CHECK(g_dev->GetRenderState(D3DRS_AMBIENT, &v));
    EXPECT(v == 0x12345678);
    CHECK(g_dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_ADD));
    CHECK(g_dev->GetTextureStageState(1, D3DTSS_COLOROP, &v));
    EXPECT(v == D3DTOP_ADD);

    D3DMATRIX m = {};
    for (int i = 0; i < 16; ++i) (&m._11)[i] = 0.5f + (float)i;
    CHECK(g_dev->SetTransform(D3DTS_WORLD, &m));
    D3DMATRIX m2 = {};
    CHECK(g_dev->GetTransform(D3DTS_WORLD, &m2));
    EXPECT(memcmp(&m, &m2, sizeof m) == 0);
    CHECK(g_dev->SetTransform(D3DTS_TEXTURE2, &m));
    memset(&m2, 0, sizeof m2);
    CHECK(g_dev->GetTransform(D3DTS_TEXTURE2, &m2));
    EXPECT(memcmp(&m, &m2, sizeof m) == 0);

    D3DVIEWPORT8 vp = {10, 20, 100, 50, 0.25f, 0.75f}, vp2 = {};
    CHECK(g_dev->SetViewport(&vp));
    CHECK(g_dev->GetViewport(&vp2));
    EXPECT(memcmp(&vp, &vp2, sizeof vp) == 0);

    D3DMATERIAL8 mat = {}, mat2 = {};
    mat.Diffuse = {0.1f, 0.2f, 0.3f, 0.4f};
    mat.Power = 12.0f;
    CHECK(g_dev->SetMaterial(&mat));
    CHECK(g_dev->GetMaterial(&mat2));
    EXPECT(memcmp(&mat, &mat2, sizeof mat) == 0);

    D3DLIGHT8 l = {}, l2 = {};
    l.Type = D3DLIGHT_POINT;
    l.Diffuse = {1, 0.5f, 0.25f, 1};
    l.Position = {1, 2, 3};
    l.Range = 100.0f;
    l.Attenuation0 = 1.0f;
    EXPECT(FAILED(g_dev->GetLight(3, &l2))); // never set
    CHECK(g_dev->SetLight(3, &l));
    CHECK(g_dev->GetLight(3, &l2));
    EXPECT(memcmp(&l, &l2, sizeof l) == 0);
    BOOL en = TRUE;
    CHECK(g_dev->GetLightEnable(3, &en));
    EXPECT(en == FALSE);
    CHECK(g_dev->LightEnable(3, TRUE));
    CHECK(g_dev->GetLightEnable(3, &en));
    EXPECT(en == TRUE);
    CHECK(g_dev->LightEnable(3, FALSE));
    l.Type = (D3DLIGHTTYPE)99;
    EXPECT_HR(g_dev->SetLight(4, &l), D3DERR_INVALIDCALL);

    float plane[4] = {0, 1, 0, -2}, plane2[4] = {};
    CHECK(g_dev->SetClipPlane(1, plane));
    CHECK(g_dev->GetClipPlane(1, plane2));
    EXPECT(memcmp(plane, plane2, sizeof plane) == 0);
    EXPECT_HR(g_dev->SetClipPlane(40, plane), D3DERR_INVALIDCALL);

    // Fixed-function shader handles are FVF codes.
    DWORD handle = 0;
    CHECK(g_dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_TEX1));
    CHECK(g_dev->GetVertexShader(&handle));
    EXPECT(handle == (D3DFVF_XYZ | D3DFVF_TEX1));

    // A state block restores what it captured.
    CHECK(g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE));
    DWORD token = 0;
    CHECK(g_dev->CreateStateBlock(D3DSBT_ALL, &token));
    CHECK(g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE));
    CHECK(g_dev->ApplyStateBlock(token));
    CHECK(g_dev->GetRenderState(D3DRS_ZENABLE, &v));
    EXPECT(v == D3DZB_TRUE);
    CHECK(g_dev->DeleteStateBlock(token));
    EXPECT_HR(g_dev->ApplyStateBlock(token), D3DERR_INVALIDCALL);
}

static void CheckResources()
{
    // Textures: mip chain, descriptors, lock round trip, reference counting.
    IDirect3DTexture8 *t = nullptr;
    CHECK(g_dev->CreateTexture(32, 16, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t));
    if (t)
    {
        EXPECT(t->GetLevelCount() == 6);
        D3DSURFACE_DESC d = {};
        CHECK(t->GetLevelDesc(0, &d));
        EXPECT(d.Width == 32 && d.Height == 16 && d.Format == D3DFMT_A8R8G8B8 && d.Pool == D3DPOOL_MANAGED &&
               d.Type == D3DRTYPE_SURFACE); // the description of a level is that of a surface
        CHECK(t->GetLevelDesc(5, &d));
        EXPECT(d.Width == 1 && d.Height == 1);
        EXPECT(FAILED(t->GetLevelDesc(6, &d)));
        EXPECT(t->GetType() == D3DRTYPE_TEXTURE);

        D3DLOCKED_RECT lr;
        CHECK(t->LockRect(0, &lr, nullptr, 0));
        EXPECT(lr.Pitch >= 32 * 4);
        for (UINT y = 0; y < 16; ++y)
            for (UINT x = 0; x < 32; ++x) ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = 0xFF000000u | (y << 8) | x;
        CHECK(t->UnlockRect(0));
        EXPECT(FAILED(t->UnlockRect(0))); // not locked
        RECT part = {4, 2, 12, 10};
        CHECK(t->LockRect(0, &lr, &part, D3DLOCK_READONLY));
        EXPECT(((DWORD *)lr.pBits)[0] == (0xFF000000u | (2 << 8) | 4));
        EXPECT(((DWORD *)((BYTE *)lr.pBits + lr.Pitch))[1] == (0xFF000000u | (3 << 8) | 5));
        CHECK(t->UnlockRect(0));

        IDirect3DSurface8 *surf = nullptr;
        CHECK(t->GetSurfaceLevel(1, &surf));
        if (surf)
        {
            CHECK(surf->GetDesc(&d));
            EXPECT(d.Width == 16 && d.Height == 8);
            IDirect3DTexture8 *container = nullptr;
            CHECK(surf->GetContainer(IID_IDirect3DTexture8, (void **)&container));
            EXPECT(container == t);
            if (container) container->Release();
            surf->Release();
        }
        IDirect3DDevice8 *dev = nullptr;
        CHECK(t->GetDevice(&dev));
        EXPECT(dev == g_dev);
        if (dev) dev->Release();
        EXPECT(t->AddRef() == 2);
        EXPECT(t->Release() == 1);
        EXPECT(t->Release() == 0);
    }
    IDirect3DTexture8 *none = (IDirect3DTexture8 *)1;
    EXPECT_HR(g_dev->CreateTexture(16, 16, 1, 0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED, &none), D3DERR_INVALIDCALL);
    EXPECT(none == nullptr);

    // Cube and volume textures.
    IDirect3DCubeTexture8 *cube = nullptr;
    CHECK(g_dev->CreateCubeTexture(16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &cube));
    if (cube)
    {
        D3DSURFACE_DESC d = {};
        CHECK(cube->GetLevelDesc(0, &d));
        EXPECT(d.Width == 16 && d.Height == 16);
        D3DLOCKED_RECT lr;
        CHECK(cube->LockRect(D3DCUBEMAP_FACE_NEGATIVE_Y, 0, &lr, nullptr, 0));
        ((DWORD *)lr.pBits)[0] = 0xFF123456;
        CHECK(cube->UnlockRect(D3DCUBEMAP_FACE_NEGATIVE_Y, 0));
        CHECK(cube->LockRect(D3DCUBEMAP_FACE_NEGATIVE_Y, 0, &lr, nullptr, D3DLOCK_READONLY));
        EXPECT(((DWORD *)lr.pBits)[0] == 0xFF123456);
        CHECK(cube->UnlockRect(D3DCUBEMAP_FACE_NEGATIVE_Y, 0));
        cube->Release();
    }
    IDirect3DVolumeTexture8 *vol = nullptr;
    CHECK(g_dev->CreateVolumeTexture(8, 8, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &vol));
    if (vol)
    {
        D3DLOCKED_BOX lb;
        CHECK(vol->LockBox(0, &lb, nullptr, 0));
        EXPECT(lb.RowPitch >= 8 * 4 && lb.SlicePitch >= lb.RowPitch * 8);
        *(DWORD *)((BYTE *)lb.pBits + 3 * lb.SlicePitch) = 0xFFABCDEF;
        CHECK(vol->UnlockBox(0));
        CHECK(vol->LockBox(0, &lb, nullptr, D3DLOCK_READONLY));
        EXPECT(*(DWORD *)((BYTE *)lb.pBits + 3 * lb.SlicePitch) == 0xFFABCDEF);
        CHECK(vol->UnlockBox(0));
        vol->Release();
    }

    // Buffers.
    IDirect3DVertexBuffer8 *vb = nullptr;
    CHECK(g_dev->CreateVertexBuffer(256, D3DUSAGE_WRITEONLY, D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_DEFAULT, &vb));
    IDirect3DIndexBuffer8 *ib = nullptr;
    CHECK(g_dev->CreateIndexBuffer(12, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib));
    if (vb && ib)
    {
        D3DVERTEXBUFFER_DESC vd = {};
        CHECK(vb->GetDesc(&vd));
        EXPECT(vd.Size == 256 && vd.FVF == (D3DFVF_XYZ | D3DFVF_DIFFUSE) && vd.Type == D3DRTYPE_VERTEXBUFFER &&
               vd.Pool == D3DPOOL_DEFAULT && (vd.Usage & D3DUSAGE_WRITEONLY));
        D3DINDEXBUFFER_DESC id = {};
        CHECK(ib->GetDesc(&id));
        EXPECT(id.Size == 12 && id.Format == D3DFMT_INDEX16 && id.Type == D3DRTYPE_INDEXBUFFER);
        BYTE *p = nullptr;
        CHECK(vb->Lock(16, 32, &p, 0));
        memset(p, 0x5A, 32);
        CHECK(vb->Unlock());
        EXPECT(FAILED(vb->Unlock()));
        EXPECT(FAILED(vb->Lock(300, 8, &p, 0)));
        CHECK(vb->Lock(0, 0, &p, D3DLOCK_READONLY));
        EXPECT(p[16] == 0x5A && p[47] == 0x5A && p[48] == 0);
        CHECK(vb->Unlock());

        CHECK(g_dev->SetStreamSource(0, vb, 16));
        CHECK(g_dev->SetIndices(ib, 7));
        IDirect3DVertexBuffer8 *gv = nullptr;
        UINT stride = 0;
        CHECK(g_dev->GetStreamSource(0, &gv, &stride));
        EXPECT(gv == vb && stride == 16);
        if (gv) gv->Release();
        IDirect3DIndexBuffer8 *gi = nullptr;
        UINT base = 0;
        CHECK(g_dev->GetIndices(&gi, &base));
        EXPECT(gi == ib && base == 7);
        if (gi) gi->Release();
        CHECK(g_dev->SetStreamSource(0, nullptr, 0));
        CHECK(g_dev->SetIndices(nullptr, 0));
    }
    if (vb) vb->Release();
    if (ib) ib->Release();

    // Surfaces: CopyRects between image surfaces.
    IDirect3DSurface8 *a = nullptr, *b = nullptr;
    CHECK(g_dev->CreateImageSurface(8, 8, D3DFMT_A8R8G8B8, &a));
    CHECK(g_dev->CreateImageSurface(8, 8, D3DFMT_A8R8G8B8, &b));
    if (a && b)
    {
        D3DLOCKED_RECT lr;
        CHECK(a->LockRect(&lr, nullptr, 0));
        for (UINT y = 0; y < 8; ++y)
            for (UINT x = 0; x < 8; ++x) ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = 0xFF000000u | (y * 8 + x);
        CHECK(a->UnlockRect());
        RECT src = {2, 2, 6, 6};
        POINT dst = {1, 1};
        CHECK(g_dev->CopyRects(a, &src, 1, b, &dst));
        CHECK(b->LockRect(&lr, nullptr, D3DLOCK_READONLY));
        EXPECT(((DWORD *)((BYTE *)lr.pBits + 1 * lr.Pitch))[1] == (0xFF000000u | (2 * 8 + 2)));
        EXPECT(((DWORD *)((BYTE *)lr.pBits + 4 * lr.Pitch))[4] == (0xFF000000u | (5 * 8 + 5)));
        CHECK(b->UnlockRect());
    }
    if (a) a->Release();
    if (b) b->Release();

    // Render target and depth surface descriptors.
    IDirect3DSurface8 *rt = nullptr, *ds = nullptr;
    CHECK(g_dev->CreateRenderTarget(64, 32, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, TRUE, &rt));
    CHECK(g_dev->CreateDepthStencilSurface(64, 32, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &ds));
    D3DSURFACE_DESC d = {};
    if (rt) { CHECK(rt->GetDesc(&d)); EXPECT(d.Width == 64 && d.Height == 32 && (d.Usage & D3DUSAGE_RENDERTARGET)); rt->Release(); }
    if (ds) { CHECK(ds->GetDesc(&d)); EXPECT(d.Format == D3DFMT_D24S8 && (d.Usage & D3DUSAGE_DEPTHSTENCIL)); ds->Release(); }
}

static void CheckD3DX()
{
    EXPECT(D3DXGetFVFVertexSize(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1) == 32);
    EXPECT(D3DXGetFVFVertexSize(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX2) == 16 + 4 + 4 + 16);

    char text[64] = {};
    EXPECT_HR(D3DXGetErrorStringA(D3DERR_INVALIDCALL, text, sizeof text), D3D_OK);
    EXPECT(text[0] != 0);
    WCHAR wtext[64] = {};
    EXPECT_HR(D3DXGetErrorStringW(D3DERR_INVALIDCALL, wtext, 64), D3D_OK);
    bool same = true;
    for (int i = 0; text[i] || wtext[i]; ++i) same = same && wtext[i] == (WCHAR)text[i];
    EXPECT(same);

    // The assembler reports errors in a buffer.
    ID3DXBuffer *code = nullptr, *errs = nullptr;
    const char *bad = "ps.1.1\nbogus r0, t0\n";
    EXPECT(FAILED(D3DXAssembleShader(bad, (UINT)strlen(bad), 0, nullptr, &code, &errs)));
    EXPECT(code == nullptr);
    if (errs)
    {
        EXPECT(errs->GetBufferSize() > 0);
        errs->Release();
    }
    const char *good = "ps.1.1\ntex t0\nmov r0, t0\n";
    errs = nullptr;
    CHECK(D3DXAssembleShader(good, (UINT)strlen(good), 0, nullptr, &code, &errs));
    if (code)
    {
        const DWORD *tokens = (const DWORD *)code->GetBufferPointer();
        EXPECT(code->GetBufferSize() >= 4 * sizeof(DWORD));
        EXPECT(tokens[0] == D3DPS_VERSION(1, 1));
        EXPECT(tokens[code->GetBufferSize() / sizeof(DWORD) - 1] == 0x0000FFFF);
        code->Release();
    }
    if (errs) errs->Release();

    // Texture creation parameters.
    UINT w = 100, h = 30, levels = 0;
    D3DFORMAT fmt = D3DFMT_UNKNOWN;
    CHECK(D3DXCheckTextureRequirements(g_dev, &w, &h, &levels, 0, &fmt, D3DPOOL_MANAGED));
    EXPECT(w >= 100 && h >= 30 && levels >= 1 && fmt != D3DFMT_UNKNOWN);

    // Mip chain generation: a white level 0 gives white levels.
    IDirect3DTexture8 *t = nullptr;
    CHECK(D3DXCreateTexture(g_dev, 16, 16, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t));
    if (t)
    {
        EXPECT(t->GetLevelCount() == 5);
        D3DLOCKED_RECT lr;
        CHECK(t->LockRect(0, &lr, nullptr, 0));
        for (UINT y = 0; y < 16; ++y)
            for (UINT x = 0; x < 16; ++x) ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = 0xFF80C040;
        CHECK(t->UnlockRect(0));
        CHECK(D3DXFilterTexture(t, nullptr, 0, D3DX_FILTER_BOX));
        CHECK(t->LockRect(4, &lr, nullptr, D3DLOCK_READONLY));
        EXPECT(*(DWORD *)lr.pBits == 0xFF80C040);
        CHECK(t->UnlockRect(4));
        t->Release();
    }

    // Procedural fill.
    CHECK(D3DXCreateTexture(g_dev, 8, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t));
    if (t)
    {
        struct Fill
        {
            static VOID Fn(D3DXVECTOR4 *out, D3DXVECTOR2 *uv, D3DXVECTOR2 *, LPVOID) { *out = D3DXVECTOR4(uv->x, 0.0f, 1.0f, 1.0f); }
        };
        CHECK(D3DXFillTexture(t, Fill::Fn, nullptr));
        D3DLOCKED_RECT lr;
        CHECK(t->LockRect(0, &lr, nullptr, D3DLOCK_READONLY));
        DWORD left = ((DWORD *)lr.pBits)[0], right = ((DWORD *)lr.pBits)[7];
        EXPECT(((left >> 16) & 255) < ((right >> 16) & 255)); // red grows with u
        EXPECT((left & 255) == 255 && (right & 255) == 255);  // blue is constant
        CHECK(t->UnlockRect(0));
        t->Release();
    }

    // Scaling from memory: a 2x2 image to 4x4 with point filtering keeps the quadrants.
    IDirect3DSurface8 *dst = nullptr;
    CHECK(g_dev->CreateImageSurface(4, 4, D3DFMT_A8R8G8B8, &dst));
    if (dst)
    {
        const DWORD src[4] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF};
        RECT sr = {0, 0, 2, 2};
        CHECK(D3DXLoadSurfaceFromMemory(dst, nullptr, nullptr, src, D3DFMT_A8R8G8B8, 8, nullptr, &sr, D3DX_FILTER_POINT, 0));
        D3DLOCKED_RECT lr;
        CHECK(dst->LockRect(&lr, nullptr, D3DLOCK_READONLY));
        EXPECT(((DWORD *)lr.pBits)[0] == 0xFFFF0000);
        EXPECT(((DWORD *)lr.pBits)[3] == 0xFF00FF00);
        EXPECT(((DWORD *)((BYTE *)lr.pBits + 3 * lr.Pitch))[0] == 0xFF0000FF);
        EXPECT(((DWORD *)((BYTE *)lr.pBits + 3 * lr.Pitch))[3] == 0xFFFFFFFF);
        CHECK(dst->UnlockRect());
        dst->Release();
    }

    // Image files: an uncompressed true-color TGA of 2x1 pixels.
    const BYTE tga[] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 0, 32, 8 | 0x20,
                        0x00, 0x00, 0xFF, 0xFF,  // BGRA: red
                        0x00, 0xFF, 0x00, 0xFF}; // green
    D3DXIMAGE_INFO info = {};
    CHECK(D3DXGetImageInfoFromFileInMemory(tga, sizeof tga, &info));
    EXPECT(info.Width == 2 && info.Height == 1 && info.ImageFileFormat == D3DXIFF_TGA);
    EXPECT(FAILED(D3DXGetImageInfoFromFileInMemory(tga, 4, &info)));
    CHECK(D3DXCreateTextureFromFileInMemoryEx(g_dev, tga, sizeof tga, D3DX_DEFAULT, D3DX_DEFAULT, 1, 0, D3DFMT_A8R8G8B8,
                                              D3DPOOL_MANAGED, D3DX_FILTER_NONE, D3DX_FILTER_NONE, 0, nullptr, nullptr, &t));
    if (t)
    {
        D3DLOCKED_RECT lr;
        D3DSURFACE_DESC d = {};
        t->GetLevelDesc(0, &d);
        EXPECT(d.Width >= 2);
        CHECK(t->LockRect(0, &lr, nullptr, D3DLOCK_READONLY));
        EXPECT(((DWORD *)lr.pBits)[0] == 0xFFFF0000);
        CHECK(t->UnlockRect(0));
        t->Release();
    }

    // Math sanity (the exhaustive tests are in test_d3dx_math).
    D3DXMATRIX rot, inv, prod;
    D3DXMatrixRotationY(&rot, 0.7f);
    D3DXMatrixInverse(&inv, nullptr, &rot);
    D3DXMatrixMultiply(&prod, &rot, &inv);
    bool ident = true;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) ident = ident && fabsf(prod(i, j) - (i == j ? 1.0f : 0.0f)) < 1e-5f;
    EXPECT(ident);
}

static void RunApiChecks(bool shaders)
{
    printf("-- API checks\n");
    int before = g_failures;
    CheckAdapterAndCaps(shaders);
    CheckStateRoundTrips();
    CheckResources();
    CheckD3DX();
    printf("-- API checks done: %d expectations, %d failures\n", g_expects, g_failures - before);
}

//------------------------------------------------------------------------------
// --perf=N: N textured, lit draws per frame from a vertex/index buffer (the world matrix and the material change per
// draw, the texture every 8 draws); prints the CPU time per frame on the rendering thread and the API calls per frame.
//------------------------------------------------------------------------------
static void RunPerf(int draws)
{
    IDirect3DTexture8 *tex[2] = {MakeChecker(64, 0xFFF0F0F0, 0xFF2060C0, true), MakeChecker(64, 0xFFC03020, 0xFF20C030, true)};
    IDirect3DVertexBuffer8 *vb = nullptr;
    IDirect3DIndexBuffer8 *ib = nullptr;
    CHECK(g_dev->CreateVertexBuffer(4 * sizeof(LitVertex), D3DUSAGE_WRITEONLY, LIT_FVF, D3DPOOL_MANAGED, &vb));
    CHECK(g_dev->CreateIndexBuffer(6 * 2, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib));
    BYTE *p = nullptr;
    const LitVertex quad[4] = {{-1, -1, 0, 0, 0, -1, 0, 1}, {-1, 1, 0, 0, 0, -1, 0, 0}, {1, 1, 0, 0, 0, -1, 1, 0}, {1, -1, 0, 0, 0, -1, 1, 1}};
    CHECK(vb->Lock(0, 0, &p, 0));
    memcpy(p, quad, sizeof quad);
    CHECK(vb->Unlock());
    const WORD idx[6] = {0, 1, 2, 0, 2, 3};
    CHECK(ib->Lock(0, 0, &p, 0));
    memcpy(p, idx, sizeof idx);
    CHECK(ib->Unlock());
    D3DXMATRIX view, proj;
    D3DXVECTOR3 eye(0, 0, -30), at(0, 0, 0), up(0, 1, 0);
    D3DXMatrixLookAtLH(&view, &eye, &at, &up);
    D3DXMatrixPerspectiveFovLH(&proj, D3DX_PI / 4, 640.0f / 480.0f, 1.0f, 100.0f);
    g_dev->SetTransform(D3DTS_VIEW, &view);
    g_dev->SetTransform(D3DTS_PROJECTION, &proj);
    g_dev->SetRenderState(D3DRS_LIGHTING, TRUE);
    g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    D3DLIGHT8 light = {};
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = 1.0f;
    light.Direction = D3DXVECTOR3(0.2f, -0.3f, 1.0f);
    g_dev->SetLight(0, &light);
    g_dev->LightEnable(0, TRUE);
    g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    g_dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    g_dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    g_dev->SetStreamSource(0, vb, sizeof(LitVertex));
    g_dev->SetIndices(ib, 0);
    g_dev->SetVertexShader(LIT_FVF);
    const int frames = 70, warmup = 10;
    double total = 0, best = 1e9;
    dxwebgl2_Stats st = {};
    for (int f = 0; f < frames; ++f)
    {
        const double t0 = emscripten_get_now();
        g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF203040, 1.0f, 0);
        g_dev->BeginScene();
        for (int i = 0; i < draws; ++i)
        {
            D3DXMATRIX world, scale;
            D3DXMatrixRotationZ(&world, f * 0.02f + i * 0.1f);
            D3DXMatrixScaling(&scale, 0.4f, 0.4f, 0.4f);
            world = scale * world;
            world._41 = (float)(i % 40) - 20.0f;
            world._42 = (float)((i / 40) % 30) - 15.0f;
            g_dev->SetTransform(D3DTS_WORLD, &world);
            D3DMATERIAL8 m = {};
            m.Diffuse.r = (i % 7) / 7.0f; m.Diffuse.g = (i % 5) / 5.0f; m.Diffuse.b = 1.0f; m.Diffuse.a = 1.0f;
            m.Ambient = m.Diffuse;
            g_dev->SetMaterial(&m);
            if (i % 8 == 0) g_dev->SetTexture(0, tex[(i / 8) & 1]);
            g_dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2);
        }
        g_dev->EndScene();
        g_dev->Present(nullptr, nullptr, nullptr, nullptr);
        const double dt = emscripten_get_now() - t0;
        dxwebgl2_GetStats(&st);
        if (f >= warmup) { total += dt; best = dt < best ? dt : best; }
    }
    printf("PERF %d draws per frame: %.2f ms per frame on average, %.2f ms best (CPU, rendering thread), %u API calls, %u bytes uploaded per frame\n",
           draws, total / (frames - warmup), best, st.glCalls, st.uploadBytes);
    g_dev->SetTexture(0, nullptr);
    g_dev->SetStreamSource(0, nullptr, 0);
    g_dev->SetIndices(nullptr, 0);
    tex[0]->Release();
    tex[1]->Release();
    vb->Release();
    ib->Release();
}

static DWORD g_front[640 * 480];

static void ReadBack()
{
    IDirect3DSurface8 *img = nullptr;
    CHECK(g_dev->CreateImageSurface(640, 480, D3DFMT_A8R8G8B8, &img));
    CHECK(g_dev->GetFrontBuffer(img));
    D3DLOCKED_RECT lr;
    CHECK(img->LockRect(&lr, nullptr, D3DLOCK_READONLY));
    for (int y = 0; y < 480; ++y) memcpy(g_front + y * 640, (BYTE *)lr.pBits + y * lr.Pitch, 640 * 4);
    CHECK(img->UnlockRect());
    img->Release();
}

// Expected probe colors per mode ("default", "shaders", "scene2", "scene3"), see expected_probes.inc.
struct ExpectedProbe { const char *mode, *name; int x, y; unsigned rgb; };
static const ExpectedProbe kExpectedProbes[] = {
#include "expected_probes.inc"
};
static const char *g_mode = "default";
static int g_probes = 0, g_unchecked = 0;
// Allowed difference per color channel: the reference values come from a software rasterizer, other GL
// implementations differ in rounding and interpolation.
static const int kProbeTolerance = 6;

static void Probe(const char *name, int x, int y)
{
    DWORD c = g_front[y * 640 + x];
    int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    printf("PROBE %s (%d,%d) = %02x%02x%02x\n", name, x, y, r, g, b);
    ++g_probes;
    for (const ExpectedProbe &e : kExpectedProbes)
    {
        if (strcmp(e.mode, g_mode) || strcmp(e.name, name) || e.x != x || e.y != y) continue;
        int er = (e.rgb >> 16) & 255, eg = (e.rgb >> 8) & 255, eb = e.rgb & 255;
        bool ok = abs(r - er) <= kProbeTolerance && abs(g - eg) <= kProbeTolerance && abs(b - eb) <= kProbeTolerance;
        if (!ok)
        {
            printf("PROBE FAILED %s (%d,%d): got %02x%02x%02x, expected %06x\n", name, x, y, r, g, b, e.rgb);
            ++g_failures;
        }
        return;
    }
    ++g_unchecked;
    printf("PROBE UNCHECKED %s (%d,%d) in mode %s\n", name, x, y, g_mode);
}

int main(int argc, char **argv)
{
    bool blocking = true;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--implicit")) dxwebgl2_SetPresentMode(DXWEBGL2_PRESENT_IMPLICIT);
        if (!strcmp(argv[i], "--no-s3tc")) dxwebgl2_SetDisableS3TC(1);
        if (!strcmp(argv[i], "--shaders")) { dxwebgl2_SetShaderModel(0x0101, 0x0104); g_shaders = true; }
        if (!strcmp(argv[i], "--debug")) dxwebgl2_SetDebug(1);
        if (!strcmp(argv[i], "--scene2")) g_scene2 = true;
        if (!strcmp(argv[i], "--scene3")) g_scene3 = true;
        // The GPU API to render through (dxwebgl2_SetBackend).
        if (!strcmp(argv[i], "--backend=webgl2")) dxwebgl2_SetBackend(DXWEBGL2_BACKEND_WEBGL2);
        if (!strcmp(argv[i], "--backend=webgpu")) { dxwebgl2_SetBackend(DXWEBGL2_BACKEND_WEBGPU); g_wantBackend = DXWEBGL2_BACKEND_WEBGPU; }
        if (!strcmp(argv[i], "--backend=auto")) dxwebgl2_SetBackend(DXWEBGL2_BACKEND_AUTO);
    }
    (void)blocking;
    if (g_shaders) g_mode = "shaders";
    else if (g_scene2) g_mode = "scene2";
    else if (g_scene3) g_mode = "scene3";
    for (int i = 1; i < argc; ++i)
        if (!strncmp(argv[i], "--frames=", 9)) g_mode = "long-run"; // the animation ends elsewhere: probes are not compared
    dxwebgl2_PlatformHooks hooks = {};
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--yield")) hooks.OnFramePresented = [] { emscripten_sleep(0); };
        if (!strcmp(argv[i], "--proxy")) dxwebgl2_SetContextProxy(1);
    }
    dxwebgl2_SetPlatformHooks(&hooks);
    printf("d3d8_runtime_test starting\n");

    g_d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!g_d3d) { printf("Direct3DCreate8 failed\n"); return 1; }
    D3DADAPTER_IDENTIFIER8 id;
    g_d3d->GetAdapterIdentifier(0, 0, &id);
    printf("adapter: %s / %s\n", id.Driver, id.Description);
    D3DDISPLAYMODE dm;
    g_d3d->GetAdapterDisplayMode(0, &dm);
    printf("desktop mode %ux%u fmt %d, %u modes\n", dm.Width, dm.Height, (int)dm.Format, g_d3d->GetAdapterModeCount(0));

    D3DPRESENT_PARAMETERS pp;
    memset(&pp, 0, sizeof pp);
    pp.BackBufferWidth = 640;
    pp.BackBufferHeight = 480;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    HRESULT hr = g_d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &g_dev);
    if (FAILED(hr)) { printf("CreateDevice failed 0x%08x\n", (unsigned)hr); return 1; }
    D3DCAPS8 caps;
    g_dev->GetDeviceCaps(&caps);
    printf("caps: max texture %u, max simultaneous textures %u, PS %x VS %x\n", (unsigned)caps.MaxTextureWidth,
           (unsigned)caps.MaxSimultaneousTextures, (unsigned)caps.PixelShaderVersion, (unsigned)caps.VertexShaderVersion);

    for (int i = 1; i < argc; ++i)
        if (!strncmp(argv[i], "--perf=", 7))
        {
            // Timing only: the scenes, probes and checks are not run.
            RunPerf(atoi(argv[i] + 7));
            printf("TEST_DONE\n");
            printf(g_failures ? "TEST_RESULT: FAIL (perf)\n" : "TEST_RESULT: PASS (perf)\n");
            fflush(stdout);
            return 0;
        }

    RunApiChecks(g_shaders);

    IDirect3DTexture8 *checker = MakeChecker(64, 0xFFF0F0F0, 0xFF2060C0, true);
    IDirect3DTexture8 *gradient = MakeChecker(32, 0xFFFFFFFF, 0x40404040, true); // alpha-less in practice, still a texture
    // Make the "gradient" texture alpha-varying: half opaque / half transparent (alpha test target).
    {
        D3DLOCKED_RECT lr;
        gradient->LockRect(0, &lr, nullptr, 0);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
                ((DWORD *)((BYTE *)lr.pBits + y * lr.Pitch))[x] = (x < 16) ? 0xFFFFA020 : 0x20202020;
        gradient->UnlockRect(0);
    }
    IDirect3DTexture8 *dxt = MakeDXT1();
    if (g_shaders) CreateShaders();

    IDirect3DTexture8 *rtTex = nullptr;
    CHECK(g_dev->CreateTexture(128, 128, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &rtTex));
    IDirect3DSurface8 *rtDepth = nullptr;
    CHECK(g_dev->CreateDepthStencilSurface(128, 128, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &rtDepth));
    IDirect3DSurface8 *bb = nullptr, *bbDepth = nullptr;
    CHECK(g_dev->GetRenderTarget(&bb));
    CHECK(g_dev->GetDepthStencilSurface(&bbDepth));

    bool doReset = false;
    for (int i = 1; i < argc; ++i) if (!strcmp(argv[i], "--reset")) doReset = true;
    int kFrames = 90;
    for (int i = 1; i < argc; ++i)
        if (!strncmp(argv[i], "--frames=", 9)) kFrames = atoi(argv[i] + 9);
    // WebGL call counts over the run (summed per frame from dxwebgl2_GetStats), printed as STATS lines: they show whether a
    // change to the draw path adds calls (compare the lines of two builds; they do not depend on timing).
    dxwebgl2_Stats total = {}, st = {};
    for (int f = 0; f < kFrames; ++f)
    {
        if (doReset && f == 40)
        {
            // Back buffer references must be dropped before Reset(), as with real D3D8.
            bb->Release(); bbDepth->Release();
            pp.BackBufferWidth = 640; pp.BackBufferHeight = 480;
            HRESULT rhr = g_dev->Reset(&pp);
            printf("Reset -> 0x%x\n", (unsigned)rhr);
            g_dev->GetRenderTarget(&bb);
            g_dev->GetDepthStencilSurface(&bbDepth);
        }
        RenderFrame(f * 0.02f, checker, gradient, dxt, rtTex, rtDepth, bb, bbDepth, true);
        dxwebgl2_GetStats(&st);
        total.draws += st.draws; total.glCalls += st.glCalls; total.glDraw += st.glDraw; total.glState += st.glState;
        total.glBind += st.glBind; total.glUniform += st.glUniform; total.glUpload += st.glUpload; total.glAttrib += st.glAttrib;
        total.glOther += st.glOther; total.glSync += st.glSync; total.uploadBytes += st.uploadBytes;
        if (f == 0 || f == kFrames - 1)
        {
            ReadBack();
            printf("frame %d read back\n", f);
        }
        usleep(16000);
    }
    auto printStats = [](const char *what, const dxwebgl2_Stats &s) {
        printf("STATS %s: %u draws, %u WebGL calls (draw %u, state %u, bind %u, uniform %u, upload %u, attrib %u, other %u, sync %u), %u bytes uploaded\n",
               what, s.draws, s.glCalls, s.glDraw, s.glState, s.glBind, s.glUniform, s.glUpload, s.glAttrib, s.glOther, s.glSync, s.uploadBytes);
    };
    printStats("last frame", st);
    printStats("all frames", total);

    // Probes (frame kFrames-1 contents).
    Probe("clear-bg", 5, 235);
    Probe("scene-A-cube-center", 160, 120);
    Probe("scene-B-fog-far", 480, 12);
    Probe("scene-B-near", 480, 225);
    Probe("scene-C-rt-apex-top", 160, 330);
    Probe("scene-C-rt-bottom-left", 70, 440);
    Probe("scene-C-rt-bottom-right", 250, 440);
    Probe("scene-D-dxt-0", 335, 255);
    Probe("scene-D-dxt-1", 370, 255);
    Probe("scene-D-blend", 480, 340);
    Probe("scene-D-alphatest-kept", 560, 270);
    Probe("scene-D-alphatest-cut", 600, 270);
    Probe("scene-D-point", 470, 450);
    if (g_scene3)
        for (int i = 0; i < 11; ++i) Probe("scene3-cell", (i % 4) * 160 + 80, (i / 4) * 160 + 80);
    if (g_shaders)
    {
        Probe("vs-quad", 50, 50);
        Probe("ps11-quad", 150, 50);
        Probe("texbem-left", 215, 50);
        Probe("texbem-right", 285, 50);
        Probe("ps14-quad", 310, 50);
    }
    {
        // Checksum for regression comparisons.
        unsigned long long h = 1469598103934665603ull;
        for (int i = 0; i < 640 * 480; ++i) { h ^= g_front[i]; h *= 1099511628211ull; }
        printf("FRAME_HASH %016llx\n", h);
    }
    printf("TEST_DONE\n");
    // Several probes in the unchecked modes are fine (the checksum above covers them for regressions),
    // but the default mode must be fully covered.
    if (!strcmp(g_mode, "default") && g_unchecked) { printf("%d probes have no expectation\n", g_unchecked); ++g_failures; }
    if (g_failures == 0)
        printf("TEST_RESULT: PASS (%d expectations, %d probes, mode %s)\n", g_expects, g_probes, g_mode);
    else
        printf("TEST_RESULT: FAIL (%d failures, %d expectations, %d probes, mode %s)\n", g_failures, g_expects, g_probes, g_mode);
    fflush(stdout);
    return g_failures ? 1 : 0;
}
