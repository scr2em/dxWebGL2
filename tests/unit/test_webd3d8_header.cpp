/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Source compatibility with code written against the original "WebD3D8" library: <WebD3D8/WebD3D8.h> with the old
 * names and enumerators.
 */
#include <d3d8.h>
#include <WebD3D8/WebD3D8.h>

#include <stdio.h>

int main()
{
    int fail = 0;
    WebD3D8_SetCanvas("#canvas");
    WebD3D8_SetDesktopSize(800, 600);
    WebD3D8_SetShaderModel(0x0101, 0x0104);
    WebD3D8_SetPresentMode(WEBD3D8_PRESENT_IMPLICIT);
    WebD3D8_PlatformHooks hooks = {};
    WebD3D8_SetPlatformHooks(&hooks);
    WebD3D8_SetShaderModel(0, 0);
    WebD3D8_SetDebug(0);
    WebD3D8_SetContextProxy(0);
    WebD3D8_SetReleaseTextureShadows(0);
    WebD3D8_SetDisableS3TC(0);
    if (WEBD3D8_PRESENT_EXPLICIT != DXWEBGL2_PRESENT_EXPLICIT || WEBD3D8_PRESENT_IMPLICIT != DXWEBGL2_PRESENT_IMPLICIT) ++fail;
    if (WebD3D8_LookupProc("Direct3DCreate8") != reinterpret_cast<void *>(&Direct3DCreate8)) ++fail;
    enum WebD3D8_PresentMode mode = WEBD3D8_PRESENT_EXPLICIT;
    (void)mode;
    printf(fail ? "TEST_RESULT: FAIL\n" : "TEST_RESULT: PASS\n");
    return fail ? 1 : 0;
}
