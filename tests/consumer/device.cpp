/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * A page that creates a device with the default backend (DXWEBGL2_BACKEND_AUTO), clears the back buffer and presents
 * it. It prints how it was built (BUILD webgpu=0|1 jspi=0|1), the backend AUTO selected (BACKEND webgl2|webgpu) and
 * CONSUMER_DEVICE_DONE; tests/runner/run_auto.mjs checks them and the colour of the canvas in browsers with and without
 * WebGPU.
 */
#include <d3d8.h>
#include <dxwebgl2/dxwebgl2.h>

#include <stdio.h>

#ifndef CONSUMER_LIBRARY_HAS_WEBGPU
#define CONSUMER_LIBRARY_HAS_WEBGPU 0
#endif
#ifndef CONSUMER_LINKED_WITH_JSPI
#define CONSUMER_LINKED_WITH_JSPI 0
#endif

int main()
{
    printf("BUILD webgpu=%d jspi=%d\n", CONSUMER_LIBRARY_HAS_WEBGPU, CONSUMER_LINKED_WITH_JSPI);
    dxwebgl2_SetPresentMode(DXWEBGL2_PRESENT_IMPLICIT); // the frame is shown when main returns
    IDirect3D8 *d3d = Direct3DCreate8(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.Windowed = TRUE;
    IDirect3DDevice8 *dev = nullptr;
    if (!d3d || FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, nullptr, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev)))
    {
        printf("CreateDevice failed\nCONSUMER_DEVICE_DONE\n");
        return 1;
    }
    printf("BACKEND %s\n", dxwebgl2_GetBackend() == DXWEBGL2_BACKEND_WEBGPU ? "webgpu" : "webgl2");
    printf("renderer: %s\n", dxwebgl2_GetRendererString());
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(32, 96, 160), 1.0f, 0);
    dev->Present(nullptr, nullptr, nullptr, nullptr);
    printf("CONSUMER_DEVICE_DONE\n");
    return 0;
}
