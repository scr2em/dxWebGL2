/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * A stand-in for the <windows.h> of a project that ports a Windows code base to the web with its own Win32
 * compatibility layer. It is deliberately different from dxwebgl2/win32_min.h (own include guard, own spelling
 * of the macros, no DUMMYUNIONNAME / __MSABI_LONG helper macros) to prove that dxWebGL2's headers add no
 * definition when the project provides the Windows types (DXWEBGL2_HAVE_WINDOWS_H or detection through the
 * include guard).
 */
#ifndef PROJECT_COMPAT_WINDOWS_H
#define PROJECT_COMPAT_WINDOWS_H
#ifndef TEST_NO_WINDOWS_GUARD
#define _WINDOWS_ 1 /* the include guard of the Windows SDK header, which dxWebGL2 recognizes */
#endif

#include <stddef.h>
#include <string.h>

#define WINAPI
#define CALLBACK
#define STDMETHODCALLTYPE
#define STDAPICALLTYPE
#define CONST const
#define VOID void
#define FAR
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;
typedef unsigned int UINT;
typedef int INT;
typedef long LONG;
typedef unsigned long ULONG;
typedef short SHORT;
typedef unsigned short USHORT;
typedef char CHAR;
typedef wchar_t WCHAR; /* 16 bits with -fshort-wchar */
typedef float FLOAT;
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef unsigned int UINT_PTR;
typedef long LONG_PTR;
typedef unsigned long ULONG_PTR;
typedef ULONG_PTR DWORD_PTR;
typedef ULONG_PTR SIZE_T;
typedef void *PVOID, *LPVOID, *HANDLE;
typedef const void *LPCVOID;
typedef BYTE *LPBYTE;
typedef DWORD *LPDWORD;
typedef CHAR *LPSTR;
typedef const CHAR *LPCSTR;
typedef WCHAR *LPWSTR;
typedef const WCHAR *LPCWSTR;
typedef LONG HRESULT;
struct HWND__ { int unused; };
typedef struct HWND__ *HWND;
struct HMONITOR__ { int unused; };
typedef struct HMONITOR__ *HMONITOR;
typedef struct HINSTANCE__ *HINSTANCE;

typedef union _LARGE_INTEGER {
    struct { DWORD LowPart; LONG HighPart; };
    LONGLONG QuadPart;
} LARGE_INTEGER;

typedef struct tagPOINT { LONG x, y; } POINT;
typedef struct tagRECT { LONG left, top, right, bottom; } RECT;
typedef struct tagPALETTEENTRY { BYTE peRed, peGreen, peBlue, peFlags; } PALETTEENTRY;
typedef struct _RGNDATAHEADER { DWORD dwSize, iType, nCount, nRgnSize; RECT rcBound; } RGNDATAHEADER;
typedef struct _RGNDATA { RGNDATAHEADER rdh; char Buffer[1]; } RGNDATA;

typedef struct _GUID { unsigned long Data1; unsigned short Data2, Data3; unsigned char Data4[8]; } GUID;
typedef GUID IID;
#define REFGUID const GUID &
#define REFIID const IID &
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    static const GUID name = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}

#define S_OK ((HRESULT)0L)
#define S_FALSE ((HRESULT)1L)
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#define E_POINTER ((HRESULT)0x80004003L)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define MAKE_HRESULT(sev, fac, code) \
    ((HRESULT)(((unsigned long)(sev) << 31) | ((unsigned long)(fac) << 16) | ((unsigned long)(code))))

#endif /* PROJECT_COMPAT_WINDOWS_H */
