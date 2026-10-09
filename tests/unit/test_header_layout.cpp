/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * ABI of the Direct3D 8 headers: code written against Microsoft's headers must compile and behave the same
 * against the freely licensed ones that dxWebGL2 ships.
 *
 * data/d3d8_abi_expected.inc holds the size of every structure, the offset of every member and the value of
 * every constant and enumerator, as computed from the Microsoft Direct3D 8 headers for wasm32 (4-byte packing
 * like 32-bit Windows) by tools/check_header_compat.py. This file turns them into compile time assertions:
 * if it compiles, the layout is right.
 *
 * (The order of the methods of every COM interface, which is the binary interface of the vtables, is checked
 * against data/d3d8_interfaces.golden by tools/check_header_compat.py --check-golden, run by scripts/test.sh.)
 */
#include <d3d8.h>
#include <d3dx8.h>

#include <stddef.h>
#include <stdio.h>

// Only meaningful on a 32-bit target with the Windows data model.
static_assert(sizeof(void *) == 4, "the expected layout is the 32-bit one");
static_assert(sizeof(DWORD) == 4 && sizeof(LONG) == 4 && sizeof(ULONG) == 4 && sizeof(BOOL) == 4, "Win32 integer sizes");
static_assert(sizeof(WORD) == 2 && sizeof(BYTE) == 1 && sizeof(WCHAR) == 2 && sizeof(HRESULT) == 4, "Win32 integer sizes");
static_assert(sizeof(LARGE_INTEGER) == 8 && sizeof(GUID) == 16 && sizeof(RECT) == 16 && sizeof(POINT) == 8, "Win32 structures");
static_assert(sizeof(PALETTEENTRY) == 4, "PALETTEENTRY");
static_assert(sizeof(D3DCOLOR) == 4 && sizeof(D3DFORMAT) == 4 && sizeof(D3DPOOL) == 4, "Direct3D enumerations are 32 bits");

#define DXW_SIZE(T, n) static_assert(sizeof(T) == (n), "sizeof(" #T ")");
#define DXW_OFFSET(T, f, n) static_assert(offsetof(T, f) == (n), "offsetof(" #T ", " #f ")");
#define DXW_VALUE(name, v) static_assert((long long)(name) == (v), #name);
#include "data/d3d8_abi_expected.inc"

// The D3DX math classes are used as arrays of floats.
static_assert(sizeof(D3DXVECTOR2) == 8 && sizeof(D3DXVECTOR3) == 12 && sizeof(D3DXVECTOR4) == 16, "D3DX vectors");
static_assert(sizeof(D3DXMATRIX) == 64 && sizeof(D3DXQUATERNION) == 16 && sizeof(D3DXPLANE) == 16 &&
              sizeof(D3DXCOLOR) == 16, "D3DX matrices and quaternions");
static_assert(sizeof(D3DXMATRIXA16) == 64 && alignof(D3DXMATRIXA16) == 16, "D3DXMATRIXA16");
static_assert(offsetof(D3DXMATRIX, _44) == 60 && offsetof(D3DXVECTOR3, z) == 8, "D3DX member order");
static_assert(sizeof(D3DXIMAGE_INFO) == 28, "D3DXIMAGE_INFO");

int main()
{
    printf("header layout OK\n");
    return 0;
}
