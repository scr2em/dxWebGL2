/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Link compatibility with the original "WebD3D8" library: the old function names exist as symbols, also when they are
 * only declared (weak) by the code that uses them and nothing else references them: a Win32 compatibility layer
 * resolves LoadLibrary("D3D8.DLL") / GetProcAddress("Direct3DCreate8") like this. This file must not include the
 * WebD3D8 compatibility header (its macros would rename the declarations).
 */
#include <d3d8.h>

#include <stdio.h>

extern "C" void *WebD3D8_LookupProc(const char *name) __attribute__((weak));
extern "C" const char *WebD3D8_GetRendererString(void) __attribute__((weak));

int main()
{
    int fail = 0;
    IDirect3D8 *d3d = Direct3DCreate8(D3D_SDK_VERSION); // pulls the library's Direct3D object file, like any application
    if (!d3d) ++fail;
    if (d3d) d3d->Release();
    if (!WebD3D8_LookupProc) { printf("FAILED: WebD3D8_LookupProc was not linked\n"); ++fail; }
    else
    {
        if (WebD3D8_LookupProc("Direct3DCreate8") != reinterpret_cast<void *>(&Direct3DCreate8)) { printf("FAILED: lookup\n"); ++fail; }
        if (WebD3D8_LookupProc("NotAnEntryPoint") != nullptr) { printf("FAILED: lookup of an unknown name\n"); ++fail; }
    }
    if (!WebD3D8_GetRendererString || !WebD3D8_GetRendererString()) { printf("FAILED: renderer string\n"); ++fail; }
    printf(fail ? "TEST_RESULT: FAIL\n" : "TEST_RESULT: PASS\n");
    return fail ? 1 : 0;
}
