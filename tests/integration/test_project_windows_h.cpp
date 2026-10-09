/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Integration with a project that has its own <windows.h> / <objbase.h> (see project_windows/): the dxWebGL2
 * headers must add no Windows definitions, and the result must have exactly the Direct3D 8 ABI. Built with
 * -fshort-wchar like such projects are, and linked against the library, which was built without it.
 *
 * The include order below is the one of a project that includes <windows.h> first. TEST_DEFINE_FLAG selects
 * whether DXWEBGL2_HAVE_WINDOWS_H is defined explicitly (CMake option DXWEBGL2_USE_PROJECT_WINDOWS_H) or detected.
 */
#ifdef TEST_DEFINE_FLAG
#define DXWEBGL2_HAVE_WINDOWS_H 1
#endif

#include <windows.h>
#include <objbase.h>

#include <d3d8.h>
#include <d3dx8.h>
#include <dxwebgl2/dxwebgl2.h>

#include <stddef.h>
#include <stdio.h>

// The project's definitions are the ones in effect (not dxWebGL2's).
#ifdef DXWEBGL2_WIN32_MIN_H
static_assert(sizeof(wchar_t) == 2, "-fshort-wchar");
#endif
static_assert(sizeof(wchar_t) == 2, "this test is built with -fshort-wchar");
static_assert(sizeof(WCHAR) == 2, "WCHAR");

#define DXW_SIZE(T, n) static_assert(sizeof(T) == (n), "sizeof(" #T ")");
#define DXW_OFFSET(T, f, n) static_assert(offsetof(T, f) == (n), "offsetof(" #T ", " #f ")");
#define DXW_VALUE(name, v) static_assert((long long)(name) == (v), #name);
#include "data/d3d8_abi_expected.inc"

int main()
{
    // The library, built with dxwebgl2's own Windows types, links and works with the project's.
    IDirect3D8 *d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!d3d) { printf("Direct3DCreate8 failed\n"); return 1; }
    D3DADAPTER_IDENTIFIER8 id;
    HRESULT hr = d3d->GetAdapterIdentifier(0, 0, &id);
    D3DDISPLAYMODE mode;
    HRESULT hr2 = d3d->GetAdapterDisplayMode(0, &mode);
    d3d->Release();

    D3DXMATRIX m;
    D3DXMatrixScaling(&m, 2, 3, 4);
    WCHAR text[16];
    D3DXGetErrorStringW(D3DERR_INVALIDCALL, text, 16);
    bool ok = SUCCEEDED(hr) && SUCCEEDED(hr2) && id.Driver[0] != 0 && m._22 == 3.0f && text[0] == 'I' && text[1] == 'n';
    printf("%s\n", ok ? "TEST_RESULT: PASS" : "TEST_RESULT: FAIL");
    return ok ? 0 : 1;
}
