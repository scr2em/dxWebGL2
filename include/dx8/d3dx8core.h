/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * d3dx8core.h
 *
 * D3DX8 core declarations: ID3DXBuffer, error strings, the FVF size helper
 * and the vertex/pixel shader assembler. Implemented in src/d3d8/d3dx_misc.cpp.
 */
#include "d3dx8.h"

#ifndef __D3DX8CORE_H__
#define __D3DX8CORE_H__


/* Errors of D3DX (facility 0x876, like the Direct3D errors). */
#ifndef MAKE_DDHRESULT
#define MAKE_DDHRESULT(code) MAKE_HRESULT(1, 0x876, code)
#endif

enum _D3DXERR {
    D3DXERR_CANNOTMODIFYINDEXBUFFER = MAKE_DDHRESULT(2900),
    D3DXERR_INVALIDMESH = MAKE_DDHRESULT(2901),
    D3DXERR_CANNOTATTRSORT = MAKE_DDHRESULT(2902),
    D3DXERR_SKINNINGNOTSUPPORTED = MAKE_DDHRESULT(2903),
    D3DXERR_TOOMANYINFLUENCES = MAKE_DDHRESULT(2904),
    D3DXERR_INVALIDDATA = MAKE_DDHRESULT(2905),
    D3DXERR_LOADEDMESHASNODATA = MAKE_DDHRESULT(2906),
};

#ifdef __cplusplus

/*
 * ID3DXBuffer: arbitrary size data returned by D3DX.
 */
typedef interface ID3DXBuffer ID3DXBuffer;
typedef interface ID3DXBuffer *LPD3DXBUFFER;

DEFINE_GUID(IID_ID3DXBuffer, 0x932e6a7e, 0xc68e, 0x45dd, 0xa7, 0xbf, 0x53, 0xd1, 0x9c, 0x86, 0xdb, 0x1f);

#undef INTERFACE
#define INTERFACE ID3DXBuffer

DECLARE_INTERFACE_(ID3DXBuffer, IUnknown)
{
    /*** IUnknown methods ***/
    STDMETHOD(QueryInterface)(THIS_ REFIID iid, LPVOID * ppv) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;

    /*** ID3DXBuffer methods ***/
    STDMETHOD_(LPVOID, GetBufferPointer)(THIS) PURE;
    STDMETHOD_(DWORD, GetBufferSize)(THIS) PURE;
};
#undef INTERFACE

#endif /* __cplusplus */

/* D3DXASM flags for D3DXAssembleShader. */
#define D3DXASM_DEBUG (1 << 0)
#define D3DXASM_SKIPVALIDATION (1 << 1)

#ifdef __cplusplus
extern "C" {
#endif

/* Number of bytes of a vertex of the given FVF. */
UINT WINAPI D3DXGetFVFVertexSize(DWORD FVF);

/* Text of a Direct3D / D3DX error code. */
HRESULT WINAPI D3DXGetErrorStringA(HRESULT hr, LPSTR pBuffer, UINT BufferLen);
HRESULT WINAPI D3DXGetErrorStringW(HRESULT hr, LPWSTR pBuffer, UINT BufferLen);
#ifdef UNICODE
#define D3DXGetErrorString D3DXGetErrorStringW
#else
#define D3DXGetErrorString D3DXGetErrorStringA
#endif

#ifdef __cplusplus
/* Assembles the text of a vs.1.x / ps.1.x shader to a token stream. */
HRESULT WINAPI D3DXAssembleShader(LPCVOID pSrcData, UINT SrcDataLen, DWORD Flags, LPD3DXBUFFER *ppConstants,
                                  LPD3DXBUFFER *ppCompiledShader, LPD3DXBUFFER *ppCompilationErrors);
HRESULT WINAPI D3DXAssembleShaderFromFileA(LPCSTR pSrcFile, DWORD Flags, LPD3DXBUFFER *ppConstants,
                                           LPD3DXBUFFER *ppCompiledShader, LPD3DXBUFFER *ppCompilationErrors);
HRESULT WINAPI D3DXAssembleShaderFromFileW(LPCWSTR pSrcFile, DWORD Flags, LPD3DXBUFFER *ppConstants,
                                           LPD3DXBUFFER *ppCompiledShader, LPD3DXBUFFER *ppCompilationErrors);
#ifdef UNICODE
#define D3DXAssembleShaderFromFile D3DXAssembleShaderFromFileW
#else
#define D3DXAssembleShaderFromFile D3DXAssembleShaderFromFileA
#endif
#endif /* __cplusplus */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __D3DX8CORE_H__ */
