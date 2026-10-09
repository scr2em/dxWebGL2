/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
#include <d3d8.h>
#include <d3dx8.h>
#include <dxwebgl2/dxwebgl2.h>

#include <stdio.h>

int main()
{
    printf("dxWebGL2 %s\n", dxwebgl2_GetVersionString());
    IDirect3D8 *d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!d3d) return 1;
    D3DADAPTER_IDENTIFIER8 id;
    HRESULT hr = d3d->GetAdapterIdentifier(0, 0, &id);
    D3DXMATRIX m;
    D3DXMatrixTranslation(&m, 1, 2, 3);
    d3d->Release();
    const bool ok = SUCCEEDED(hr) && m._42 == 2.0f;
    printf("%s\n", ok ? "CONSUMER_OK" : "CONSUMER_FAILED");
    return ok ? 0 : 1;
}
