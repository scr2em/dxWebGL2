/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Textured, lit cube: fixed-function Direct3D 8 with vertex and index buffers, a procedurally generated
 * mip-mapped texture, a directional light plus ambient light, the depth buffer and the texture stage cascade
 * (texture modulated with the lit vertex color).
 */
#include <d3d8.h>
#include <d3dx8.h>
#include <dxwebgl2/dxwebgl2.h>

#include <emscripten.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct Vertex
{
    float x, y, z;    // position
    float nx, ny, nz; // normal
    float u, v;       // texture coordinates
};
const DWORD kVertexFormat = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1;

IDirect3D8 *g_d3d = nullptr;
IDirect3DDevice8 *g_device = nullptr;
IDirect3DVertexBuffer8 *g_vb = nullptr;
IDirect3DIndexBuffer8 *g_ib = nullptr;
IDirect3DTexture8 *g_texture = nullptr;
int g_frame = 0;
int g_maxFrames = -1; // --frames=N: stop after N frames (used by the automatic test)

// A 64x64 checkerboard with a colored border, with a full mip chain.
bool CreateTexture()
{
    if (FAILED(g_device->CreateTexture(64, 64, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_texture))) return false;
    for (UINT level = 0; level < g_texture->GetLevelCount(); ++level)
    {
        D3DSURFACE_DESC desc;
        g_texture->GetLevelDesc(level, &desc);
        D3DLOCKED_RECT lr;
        if (FAILED(g_texture->LockRect(level, &lr, nullptr, 0))) return false;
        const UINT cell = desc.Width >= 8 ? desc.Width / 8 : 1;
        for (UINT y = 0; y < desc.Height; ++y)
        {
            DWORD *row = reinterpret_cast<DWORD *>(static_cast<BYTE *>(lr.pBits) + y * lr.Pitch);
            for (UINT x = 0; x < desc.Width; ++x)
            {
                const bool border = x < cell / 2 || y < cell / 2 || x >= desc.Width - cell / 2 || y >= desc.Height - cell / 2;
                const bool odd = (((x / cell) + (y / cell)) & 1) != 0;
                row[x] = border ? 0xFFE0A030 : (odd ? 0xFF2A6FDB : 0xFFF2F2F2);
            }
        }
        g_texture->UnlockRect(level);
    }
    return true;
}

// 24 vertices (4 per face, so each face has its own normal and texture coordinates) and 36 indices.
bool CreateCube()
{
    static const float normals[6][3] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    static const float tangents[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}};
    static const float bitangents[6][3] = {{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    static const int sx[4] = {-1, -1, 1, 1}, sy[4] = {-1, 1, 1, -1};

    if (FAILED(g_device->CreateVertexBuffer(24 * sizeof(Vertex), D3DUSAGE_WRITEONLY, kVertexFormat, D3DPOOL_MANAGED, &g_vb))) return false;
    if (FAILED(g_device->CreateIndexBuffer(36 * sizeof(WORD), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &g_ib))) return false;

    Vertex *v = nullptr;
    WORD *idx = nullptr;
    if (FAILED(g_vb->Lock(0, 0, reinterpret_cast<BYTE **>(&v), 0))) return false;
    if (FAILED(g_ib->Lock(0, 0, reinterpret_cast<BYTE **>(&idx), 0))) return false;
    for (int f = 0; f < 6; ++f)
    {
        for (int i = 0; i < 4; ++i)
        {
            Vertex &o = v[f * 4 + i];
            o.x = (normals[f][0] + sx[i] * tangents[f][0] + sy[i] * bitangents[f][0]) * 0.5f;
            o.y = (normals[f][1] + sx[i] * tangents[f][1] + sy[i] * bitangents[f][1]) * 0.5f;
            o.z = (normals[f][2] + sx[i] * tangents[f][2] + sy[i] * bitangents[f][2]) * 0.5f;
            o.nx = normals[f][0];
            o.ny = normals[f][1];
            o.nz = normals[f][2];
            o.u = (sx[i] + 1) * 0.5f;
            o.v = (1 - sy[i]) * 0.5f;
        }
        const WORD b = static_cast<WORD>(f * 4);
        const WORD face[6] = {b, WORD(b + 1), WORD(b + 2), b, WORD(b + 2), WORD(b + 3)}; // clockwise seen from outside
        memcpy(idx + f * 6, face, sizeof face);
    }
    g_vb->Unlock();
    g_ib->Unlock();
    return true;
}

void SetupLighting()
{
    g_device->SetRenderState(D3DRS_LIGHTING, TRUE);
    g_device->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_XRGB(60, 60, 70));
    g_device->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);

    D3DMATERIAL8 material = {};
    material.Diffuse = {1.0f, 1.0f, 1.0f, 1.0f};
    material.Ambient = {1.0f, 1.0f, 1.0f, 1.0f};
    g_device->SetMaterial(&material);

    D3DLIGHT8 light = {};
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse = {1.0f, 0.95f, 0.85f, 1.0f};
    light.Direction = {-0.5f, -1.0f, 0.7f};
    g_device->SetLight(0, &light);
    g_device->LightEnable(0, TRUE);
}

void Frame()
{
    g_device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(32, 48, 64), 1.0f, 0);

    D3DXMATRIX rx, ry, world;
    D3DXMatrixRotationX(&rx, 0.021f * (float)g_frame);
    D3DXMatrixRotationY(&ry, 0.034f * (float)g_frame);
    D3DXMatrixMultiply(&world, &rx, &ry);
    g_device->SetTransform(D3DTS_WORLD, &world);

    g_device->BeginScene();
    g_device->SetTexture(0, g_texture);
    g_device->SetStreamSource(0, g_vb, sizeof(Vertex));
    g_device->SetIndices(g_ib, 0);
    g_device->SetVertexShader(kVertexFormat);
    g_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 24, 0, 12);
    g_device->EndScene();
    g_device->Present(nullptr, nullptr, nullptr, nullptr);

    ++g_frame;
    if (g_maxFrames >= 0 && g_frame >= g_maxFrames)
    {
        emscripten_cancel_main_loop();
        printf("EXAMPLE_DONE\n");
    }
}

} // namespace

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (!strncmp(argv[i], "--frames=", 9)) g_maxFrames = atoi(argv[i] + 9);
    // The library's options (?args=-dxwebgl2-report, -dxwebgl2-backend=webgpu, ...).
    dxwebgl2_ParseArguments(argc, argv);

    dxwebgl2_SetPresentMode(DXWEBGL2_PRESENT_IMPLICIT);

    g_d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!g_d3d)
    {
        printf("Direct3DCreate8 failed\n");
        return 1;
    }

    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = 640;
    pp.BackBufferHeight = 480;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    HRESULT hr = g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, nullptr, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp,
                                     &g_device);
    if (FAILED(hr))
    {
        printf("CreateDevice failed: 0x%08x\n", (unsigned)hr);
        return 1;
    }
    if (!CreateTexture() || !CreateCube())
    {
        printf("resource creation failed\n");
        return 1;
    }

    D3DXMATRIX view, proj;
    D3DXVECTOR3 eye(0, 1.2f, -3.2f), at(0, 0, 0), up(0, 1, 0);
    D3DXMatrixLookAtLH(&view, &eye, &at, &up);
    D3DXMatrixPerspectiveFovLH(&proj, D3DX_PI / 4, 640.0f / 480.0f, 0.1f, 100.0f);
    g_device->SetTransform(D3DTS_VIEW, &view);
    g_device->SetTransform(D3DTS_PROJECTION, &proj);

    SetupLighting();
    g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    // Stage 0: texture * diffuse (the lit vertex color); trilinear filtering of the mip chain.
    g_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    g_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    g_device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    g_device->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    g_device->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    g_device->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);

    emscripten_set_main_loop(Frame, 0, 1);
    return 0;
}
