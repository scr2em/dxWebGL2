/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Hello triangle: the smallest Direct3D 8 program that does something visible. A vertex-colored triangle
 * spins around the Y axis. Everything below is plain Direct3D 8 (and two D3DX math helpers); the only
 * dxWebGL2 specific lines are the include and the call that selects how frames reach the page.
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
    float x, y, z;
    DWORD color;
};
const DWORD kVertexFormat = D3DFVF_XYZ | D3DFVF_DIFFUSE;

IDirect3D8 *g_d3d = nullptr;
IDirect3DDevice8 *g_device = nullptr;
int g_frame = 0;
int g_maxFrames = -1; // --frames=N: stop after N frames (used by the automatic test)

void Frame()
{
    // The device owns the back buffer; draw into it and present.
    g_device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(32, 48, 64), 1.0f, 0);

    D3DXMATRIX world;
    D3DXMatrixRotationY(&world, 0.03f * (float)g_frame);
    g_device->SetTransform(D3DTS_WORLD, &world);

    static const Vertex triangle[3] = {
        {0.0f, 1.0f, 0.0f, 0xFFFF3030},   // top: red
        {-1.0f, -0.8f, 0.0f, 0xFF30FF30}, // left: green
        {1.0f, -0.8f, 0.0f, 0xFF3030FF},  // right: blue
    };

    g_device->BeginScene();
    g_device->SetVertexShader(kVertexFormat);
    g_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, triangle, sizeof(Vertex));
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

    // The page's requestAnimationFrame loop below presents the canvas when a frame returns.
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
    pp.AutoDepthStencilFormat = D3DFMT_D16;
    // There are no windows on the web: the focus window is not used and may be null.
    HRESULT hr = g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, nullptr, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp,
                                     &g_device);
    if (FAILED(hr))
    {
        printf("CreateDevice failed: 0x%08x\n", (unsigned)hr);
        return 1;
    }

    // Camera: looking at the origin from 3 units away, 45 degrees field of view.
    D3DXMATRIX view, proj;
    D3DXVECTOR3 eye(0, 0, -3), at(0, 0, 0), up(0, 1, 0);
    D3DXMatrixLookAtLH(&view, &eye, &at, &up);
    D3DXMatrixPerspectiveFovLH(&proj, D3DX_PI / 4, 640.0f / 480.0f, 0.1f, 100.0f);
    g_device->SetTransform(D3DTS_VIEW, &view);
    g_device->SetTransform(D3DTS_PROJECTION, &proj);
    g_device->SetRenderState(D3DRS_LIGHTING, FALSE);   // colors come from the vertices
    g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

    // 0 fps: let the browser pick (requestAnimationFrame); 1: simulate an infinite loop so main() does not return.
    emscripten_set_main_loop(Frame, 0, 1);
    return 0;
}
