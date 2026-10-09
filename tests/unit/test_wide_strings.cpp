/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * The wide-string entry points of D3DX ("W" functions) take UTF-16 whatever the size of wchar_t. This test is
 * built twice: with the default 32-bit wchar_t (WCHAR is then char16_t) and with -fshort-wchar (WCHAR is
 * wchar_t, as with the Windows SDK), against the same library binary. Needs no GL context: runs in node.
 */
#include <d3d8.h>
#include <d3dx8.h>

#include <stdio.h>
#include <string.h>

static_assert(sizeof(WCHAR) == 2, "WCHAR is a UTF-16 code unit");

static int g_fail = 0;
#define CHECK(c)                                                  \
    do {                                                          \
        if (!(c)) { printf("FAILED line %d: %s\n", __LINE__, #c); ++g_fail; } \
    } while (0)

// u"..." literals are char16_t; convert to whatever WCHAR is.
static const WCHAR *W(const char16_t *s) { return reinterpret_cast<const WCHAR *>(s); }

static bool WriteFile(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

int main()
{
    printf("sizeof(wchar_t) = %u, sizeof(WCHAR) = %u\n", (unsigned)sizeof(wchar_t), (unsigned)sizeof(WCHAR));

    // A 2x1 true-color TGA at a path with a character outside the BMP (surrogate pair) and an accent.
    const unsigned char tga[] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 0, 32, 8 | 0x20,
                                 0x00, 0x00, 0xFF, 0xFF, 0x00, 0xFF, 0x00, 0xFF};
    const char *utf8Path = "/tmp/dxwebgl2_\xC3\xA9_\xF0\x90\x90\xB7.tga"; // e-acute and U+10437
    CHECK(WriteFile(utf8Path, tga, sizeof tga));
    D3DXIMAGE_INFO info = {};
    CHECK(SUCCEEDED(D3DXGetImageInfoFromFileW(W(u"/tmp/dxwebgl2_é_\U00010437.tga"), &info)));
    CHECK(info.Width == 2 && info.Height == 1 && info.ImageFileFormat == D3DXIFF_TGA);
    CHECK(FAILED(D3DXGetImageInfoFromFileW(W(u"/tmp/dxwebgl2_missing.tga"), &info)));
    CHECK(FAILED(D3DXGetImageInfoFromFileW(nullptr, &info)));

    // Shader assembly from a file.
    const char *src = "ps.1.1\ntex t0\nmov r0, t0\n";
    CHECK(WriteFile("/tmp/dxwebgl2_ps.psh", src, strlen(src)));
    ID3DXBuffer *code = nullptr, *errs = nullptr;
    CHECK(SUCCEEDED(D3DXAssembleShaderFromFileW(W(u"/tmp/dxwebgl2_ps.psh"), 0, nullptr, &code, &errs)));
    CHECK(code && code->GetBufferSize() >= 16);
    if (code) code->Release();
    if (errs) errs->Release();

    // Error strings are plain ASCII in both widths.
    WCHAR text[32] = {};
    CHECK(SUCCEEDED(D3DXGetErrorStringW(D3DERR_INVALIDCALL, text, 32)));
    CHECK(text[0] == 'I');
    CHECK(SUCCEEDED(D3DXGetErrorStringW(D3DERR_INVALIDCALL, text, 4)) && text[3] == 0);

    printf(g_fail ? "TEST_RESULT: FAIL\n" : "TEST_RESULT: PASS\n");
    return g_fail ? 1 : 0;
}
