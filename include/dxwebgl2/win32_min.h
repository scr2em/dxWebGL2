/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * dxwebgl2/win32_min.h
 *
 * The small part of the Win32 / COM base that the Direct3D 8 headers and the
 * dxWebGL2 library need: fixed-size integer typedefs with the sizes of 32-bit
 * Windows, GUID/IID, HRESULT, RECT/POINT/PALETTEENTRY/RGNDATA, opaque handles,
 * and the C++ COM declaration macros (IUnknown, STDMETHOD, DECLARE_INTERFACE_).
 *
 * Two integration modes:
 *
 *  1. Standalone (default). Nothing else defines Windows types: this header
 *     provides them.
 *
 *  2. The project already has its own <windows.h> / <objbase.h> (a Win32
 *     compatibility layer, or a real Windows SDK). Either include that
 *     <windows.h> before any dxWebGL2 header (it is detected through the usual
 *     include guards), or define DXWEBGL2_HAVE_WINDOWS_H (for example with
 *     -DDXWEBGL2_HAVE_WINDOWS_H on the command line, which the CMake option
 *     DXWEBGL2_USE_PROJECT_WINDOWS_H does). Then this header only includes
 *     <windows.h> and <objbase.h> and adds the few helper macros that may be
 *     missing from them; it defines no types, so nothing can clash.
 *
 * Character types: the library never depends on the size of wchar_t. In
 * standalone mode WCHAR is wchar_t when that is 16 bits wide (for example with
 * -fshort-wchar, like on Windows) and char16_t otherwise; the wide-string
 * entry points (the D3DX "W" functions) always interpret their arguments as
 * UTF-16 code units. A project that provides its own windows.h decides what
 * WCHAR is, and the library treats it as 16-bit code units either way.
 */
#ifndef DXWEBGL2_WIN32_MIN_H
#define DXWEBGL2_WIN32_MIN_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The Direct3D structures use nameless structs/unions inside unions. */
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#endif

/* 4-byte structure packing, as the Direct3D 8 SDK headers use on 32-bit
 * Windows. wasm32 has the same ILP32 data model, so the same layout is used:
 * structures then have the sizes and offsets that code written for 32-bit
 * Windows expects (tests/test_header_layout.cpp locks them down). */
#if !defined(DXWEBGL2_PACK4)
#if defined(__i386__) || defined(_M_IX86) || defined(__EMSCRIPTEN__) || defined(__wasm32__)
#define DXWEBGL2_PACK4 1
#endif
#endif

/* Integration mode detection. */
#if !defined(DXWEBGL2_HAVE_WINDOWS_H)
#if defined(_WIN32) || defined(_WINDOWS_) || defined(_WINDEF_) || defined(WEBCOMPAT_WINDOWS_H) || \
    defined(__WINE_WINDOWS_H)
#define DXWEBGL2_HAVE_WINDOWS_H 1
#endif
#endif

#if defined(DXWEBGL2_HAVE_WINDOWS_H)

/* ------------------------------------------------------------------------- *
 * The project provides the Windows types.
 * ------------------------------------------------------------------------- */
#include <windows.h>
#include <objbase.h>

#else /* !DXWEBGL2_HAVE_WINDOWS_H */

/* ------------------------------------------------------------------------- *
 * Standalone definitions.
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C" {
#endif

/* Calling conventions and decorations mean nothing on WebAssembly. */
#ifndef WINAPI
#define WINAPI
#define WINAPIV
#define APIENTRY
#define CALLBACK
#define PASCAL
#endif
#ifndef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE
#endif
#ifndef STDAPICALLTYPE
#define STDAPICALLTYPE
#endif
#ifndef CONST
#define CONST const
#endif
#ifndef VOID
#define VOID void
#endif
#ifndef FAR
#define FAR
#define NEAR
#endif
#ifndef IN
#define IN
#define OUT
#define OPTIONAL
#endif

/* Basic types with the sizes of 32-bit Windows (long is 32 bits on wasm32). */
typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef int INT;
typedef unsigned int UINT;
typedef short SHORT;
typedef unsigned short USHORT;
typedef char CHAR;
typedef unsigned char UCHAR;
typedef float FLOAT;
typedef double DOUBLE;
typedef unsigned char BOOLEAN;
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef long long INT64;
typedef unsigned long long UINT64;
typedef int INT32;
typedef unsigned int UINT32;
typedef short INT16;
typedef unsigned short UINT16;
typedef signed char INT8;
typedef unsigned char UINT8;
#if defined(__SIZEOF_LONG__) && __SIZEOF_LONG__ == 4
typedef long LONG;
typedef unsigned long ULONG;
typedef unsigned long DWORD;
#else
typedef int LONG;
typedef unsigned int ULONG;
typedef unsigned int DWORD;
#endif
/* Pointer-sized integers (same width as the pointer, 32 bits on wasm32). */
typedef intptr_t INT_PTR;
typedef uintptr_t UINT_PTR;
typedef intptr_t LONG_PTR;
typedef uintptr_t ULONG_PTR;
typedef ULONG_PTR DWORD_PTR;
typedef ULONG_PTR SIZE_T;
typedef LONG_PTR SSIZE_T;

/* UTF-16 code unit. See the notes at the top of this file. */
#if defined(__cplusplus) && defined(__SIZEOF_WCHAR_T__) && __SIZEOF_WCHAR_T__ == 2
typedef wchar_t WCHAR;
#elif defined(__cplusplus)
typedef char16_t WCHAR;
#else
typedef unsigned short WCHAR;
#endif

typedef void *PVOID, *LPVOID;
typedef const void *LPCVOID;
typedef BOOL *PBOOL, *LPBOOL;
typedef BYTE *PBYTE, *LPBYTE;
typedef WORD *PWORD, *LPWORD;
typedef DWORD *PDWORD, *LPDWORD;
typedef LONG *PLONG, *LPLONG;
typedef ULONG *PULONG;
typedef UINT *PUINT, *LPUINT;
typedef INT *PINT, *LPINT;
typedef FLOAT *PFLOAT;
typedef CHAR *PCHAR, *PSTR, *LPSTR;
typedef const CHAR *PCSTR, *LPCSTR;
typedef WCHAR *PWSTR, *LPWSTR;
typedef const WCHAR *PCWSTR, *LPCWSTR;

typedef LONG HRESULT;
typedef void *HANDLE;
#define DXWEBGL2_DECLARE_HANDLE(name) \
    struct name##__ {                 \
        int unused;                   \
    };                                \
    typedef struct name##__ *name
#ifndef DECLARE_HANDLE
#define DECLARE_HANDLE(name) DXWEBGL2_DECLARE_HANDLE(name)
#endif
DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HINSTANCE);
DECLARE_HANDLE(HMONITOR);
DECLARE_HANDLE(HDC);
typedef HINSTANCE HMODULE;

/* Structures. */
typedef union _LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG HighPart;
    };
    struct {
        DWORD LowPart;
        LONG HighPart;
    } u;
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
    struct {
        DWORD LowPart;
        DWORD HighPart;
    };
    struct {
        DWORD LowPart;
        DWORD HighPart;
    } u;
    ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

typedef struct tagPOINT {
    LONG x;
    LONG y;
} POINT, *PPOINT, *LPPOINT;

typedef struct tagSIZE {
    LONG cx;
    LONG cy;
} SIZE, *PSIZE, *LPSIZE;

typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT, *PRECT, *LPRECT;
typedef const RECT *LPCRECT;

typedef struct tagPALETTEENTRY {
    BYTE peRed;
    BYTE peGreen;
    BYTE peBlue;
    BYTE peFlags;
} PALETTEENTRY, *PPALETTEENTRY, *LPPALETTEENTRY;

typedef struct _RGNDATAHEADER {
    DWORD dwSize;
    DWORD iType;
    DWORD nCount;
    DWORD nRgnSize;
    RECT rcBound;
} RGNDATAHEADER, *PRGNDATAHEADER;

typedef struct _RGNDATA {
    RGNDATAHEADER rdh;
    char Buffer[1];
} RGNDATA, *PRGNDATA, *LPRGNDATA;

/* GUIDs. */
typedef struct _GUID {
    unsigned long Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char Data4[8];
} GUID;
typedef GUID IID;
typedef GUID CLSID;
typedef GUID *LPGUID;
typedef const GUID *LPCGUID;
typedef IID *LPIID;
typedef CLSID *LPCLSID;

#ifdef __cplusplus
#define REFGUID const GUID &
#define REFIID const IID &
#define REFCLSID const CLSID &
#else
#define REFGUID const GUID *
#define REFIID const IID *
#define REFCLSID const CLSID *
#endif

/* Like in most Win32 compatibility layers, GUID constants get internal
 * linkage: no INITGUID translation unit and no import library is needed. */
#ifndef DEFINE_GUID
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    static const GUID name = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}
#endif

/* HRESULT. */
#define S_OK ((HRESULT)0L)
#define S_FALSE ((HRESULT)1L)
#define E_UNEXPECTED ((HRESULT)0x8000FFFFL)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#define E_POINTER ((HRESULT)0x80004003L)
#define E_HANDLE ((HRESULT)0x80070006L)
#define E_ABORT ((HRESULT)0x80004004L)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_ACCESSDENIED ((HRESULT)0x80070005L)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define MAKE_HRESULT(sev, fac, code) \
    ((HRESULT)(((unsigned long)(sev) << 31) | ((unsigned long)(fac) << 16) | ((unsigned long)(code))))

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define MAX_PATH 260
#define LOBYTE(w) ((BYTE)(((DWORD_PTR)(w)) & 0xff))
#define HIBYTE(w) ((BYTE)((((DWORD_PTR)(w)) >> 8) & 0xff))
#define LOWORD(l) ((WORD)(((DWORD_PTR)(l)) & 0xffff))
#define HIWORD(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))

#ifdef __cplusplus
} /* extern "C" */
#endif

/* COM declaration macros. The interfaces are only usable from C++ (no lpVtbl
 * style C interface), which is all the library implements. */
#ifdef __cplusplus
#ifndef interface
#define interface struct
#endif
#define STDMETHOD(method) virtual HRESULT STDMETHODCALLTYPE method
#define STDMETHOD_(type, method) virtual type STDMETHODCALLTYPE method
#define STDMETHODV(method) virtual HRESULT STDMETHODVCALLTYPE method
#define STDMETHODV_(type, method) virtual type STDMETHODVCALLTYPE method
#define STDMETHODVCALLTYPE
#define STDMETHODIMP HRESULT STDMETHODCALLTYPE
#define STDMETHODIMP_(type) type STDMETHODCALLTYPE
#define PURE = 0
#define THIS_
#define THIS void
#define DECLARE_INTERFACE(iface) interface iface
#define DECLARE_INTERFACE_(iface, baseiface) interface iface : public baseiface
#define DECLARE_INTERFACE_IID_(iface, baseiface, iid) interface iface : public baseiface

inline bool operator==(const GUID &a, const GUID &b) { return memcmp(&a, &b, sizeof(GUID)) == 0; }
inline bool operator!=(const GUID &a, const GUID &b) { return !(a == b); }
#define IsEqualGUID(a, b) ((a) == (b))
#define IsEqualIID(a, b) ((a) == (b))
#define IsEqualCLSID(a, b) ((a) == (b))

DEFINE_GUID(IID_IUnknown, 0x00000000, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

interface IUnknown {
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) = 0;
    virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
    virtual ULONG STDMETHODCALLTYPE Release() = 0;
};
typedef IUnknown *LPUNKNOWN;
#endif /* __cplusplus */

#endif /* DXWEBGL2_HAVE_WINDOWS_H */

/* ------------------------------------------------------------------------- *
 * Helpers that both modes need and that a project's windows.h may lack.
 * ------------------------------------------------------------------------- */
/* Constants of the form __MSABI_LONG(0x2) in the Direct3D headers: a 32-bit
 * long on every target this library supports. */
#ifndef __MSABI_LONG
#define __MSABI_LONG(x) x
#endif
/* Anonymous union/struct member names (empty: nameless members are used). */
#ifndef DUMMYUNIONNAME
#define DUMMYUNIONNAME
#define DUMMYUNIONNAME1
#define DUMMYUNIONNAME2
#define DUMMYUNIONNAME3
#define DUMMYUNIONNAME4
#define DUMMYSTRUCTNAME
#define DUMMYSTRUCTNAME1
#define DUMMYSTRUCTNAME2
#define DUMMYSTRUCTNAME3
#define DUMMYSTRUCTNAME4
#endif
#ifndef DECLARE_INTERFACE_IID_
#ifdef __cplusplus
#define DECLARE_INTERFACE_IID_(iface, baseiface, iid) interface iface : public baseiface
#endif
#endif
#ifndef THIS_
#define THIS_
#define THIS void
#endif
#ifndef PURE
#define PURE = 0
#endif

#endif /* DXWEBGL2_WIN32_MIN_H */
