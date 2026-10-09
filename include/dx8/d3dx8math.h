/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * d3dx8math.h
 *
 * D3DX8 math types and functions: vectors, matrices, quaternions, planes and
 * colors, with the class interfaces, operators, function names and signatures
 * of the D3DX 8 API (left-handed coordinates, row vectors, row-major matrices).
 *
 * Written from the public API documentation. The non-inline functions are
 * implemented in src/d3d8/d3dx_math.cpp.
 */
#include "d3dx8.h"

#ifndef __D3DX8MATH_H__
#define __D3DX8MATH_H__

#include <math.h>
#include <dxwebgl2/win32_min.h>
#include <d3d8types.h>

#ifndef D3DXINLINE
#ifdef __cplusplus
#define D3DXINLINE inline
#else
#define D3DXINLINE static inline
#endif
#endif

#define D3DX_PI ((FLOAT)3.141592654f)
#define D3DX_1BYPI ((FLOAT)0.318309886f)

#define D3DXToRadian(degree) ((degree) * (D3DX_PI / 180.0f))
#define D3DXToDegree(radian) ((radian) * (180.0f / D3DX_PI))

/*
 * Data types
 */
#ifdef __cplusplus

typedef struct D3DXVECTOR2 {
    D3DXVECTOR2() {}
    D3DXVECTOR2(const FLOAT *);
    D3DXVECTOR2(FLOAT x, FLOAT y);

    operator FLOAT *();
    operator const FLOAT *() const;

    D3DXVECTOR2 &operator+=(const D3DXVECTOR2 &);
    D3DXVECTOR2 &operator-=(const D3DXVECTOR2 &);
    D3DXVECTOR2 &operator*=(FLOAT);
    D3DXVECTOR2 &operator/=(FLOAT);

    D3DXVECTOR2 operator+() const;
    D3DXVECTOR2 operator-() const;

    D3DXVECTOR2 operator+(const D3DXVECTOR2 &) const;
    D3DXVECTOR2 operator-(const D3DXVECTOR2 &) const;
    D3DXVECTOR2 operator*(FLOAT) const;
    D3DXVECTOR2 operator/(FLOAT) const;

    friend D3DXVECTOR2 operator*(FLOAT, const D3DXVECTOR2 &);

    BOOL operator==(const D3DXVECTOR2 &) const;
    BOOL operator!=(const D3DXVECTOR2 &) const;

    FLOAT x, y;
} D3DXVECTOR2, *LPD3DXVECTOR2;

typedef struct D3DXVECTOR3 : public D3DVECTOR {
    D3DXVECTOR3() {}
    D3DXVECTOR3(const FLOAT *);
    D3DXVECTOR3(const D3DVECTOR &);
    D3DXVECTOR3(FLOAT x, FLOAT y, FLOAT z);

    operator FLOAT *();
    operator const FLOAT *() const;

    D3DXVECTOR3 &operator+=(const D3DXVECTOR3 &);
    D3DXVECTOR3 &operator-=(const D3DXVECTOR3 &);
    D3DXVECTOR3 &operator*=(FLOAT);
    D3DXVECTOR3 &operator/=(FLOAT);

    D3DXVECTOR3 operator+() const;
    D3DXVECTOR3 operator-() const;

    D3DXVECTOR3 operator+(const D3DXVECTOR3 &) const;
    D3DXVECTOR3 operator-(const D3DXVECTOR3 &) const;
    D3DXVECTOR3 operator*(FLOAT) const;
    D3DXVECTOR3 operator/(FLOAT) const;

    friend D3DXVECTOR3 operator*(FLOAT, const struct D3DXVECTOR3 &);

    BOOL operator==(const D3DXVECTOR3 &) const;
    BOOL operator!=(const D3DXVECTOR3 &) const;
} D3DXVECTOR3, *LPD3DXVECTOR3;

typedef struct D3DXVECTOR4 {
    D3DXVECTOR4() {}
    D3DXVECTOR4(const FLOAT *);
    D3DXVECTOR4(FLOAT x, FLOAT y, FLOAT z, FLOAT w);

    operator FLOAT *();
    operator const FLOAT *() const;

    D3DXVECTOR4 &operator+=(const D3DXVECTOR4 &);
    D3DXVECTOR4 &operator-=(const D3DXVECTOR4 &);
    D3DXVECTOR4 &operator*=(FLOAT);
    D3DXVECTOR4 &operator/=(FLOAT);

    D3DXVECTOR4 operator+() const;
    D3DXVECTOR4 operator-() const;

    D3DXVECTOR4 operator+(const D3DXVECTOR4 &) const;
    D3DXVECTOR4 operator-(const D3DXVECTOR4 &) const;
    D3DXVECTOR4 operator*(FLOAT) const;
    D3DXVECTOR4 operator/(FLOAT) const;

    friend D3DXVECTOR4 operator*(FLOAT, const D3DXVECTOR4 &);

    BOOL operator==(const D3DXVECTOR4 &) const;
    BOOL operator!=(const D3DXVECTOR4 &) const;

    FLOAT x, y, z, w;
} D3DXVECTOR4, *LPD3DXVECTOR4;

typedef struct D3DXMATRIX : public D3DMATRIX {
    D3DXMATRIX() {}
    D3DXMATRIX(const FLOAT *);
    D3DXMATRIX(const D3DMATRIX &);
    D3DXMATRIX(FLOAT _11, FLOAT _12, FLOAT _13, FLOAT _14, FLOAT _21, FLOAT _22, FLOAT _23, FLOAT _24, FLOAT _31,
               FLOAT _32, FLOAT _33, FLOAT _34, FLOAT _41, FLOAT _42, FLOAT _43, FLOAT _44);

    FLOAT &operator()(UINT Row, UINT Col);
    FLOAT operator()(UINT Row, UINT Col) const;

    operator FLOAT *();
    operator const FLOAT *() const;

    D3DXMATRIX &operator*=(const D3DXMATRIX &);
    D3DXMATRIX &operator+=(const D3DXMATRIX &);
    D3DXMATRIX &operator-=(const D3DXMATRIX &);
    D3DXMATRIX &operator*=(FLOAT);
    D3DXMATRIX &operator/=(FLOAT);

    D3DXMATRIX operator+() const;
    D3DXMATRIX operator-() const;

    D3DXMATRIX operator*(const D3DXMATRIX &) const;
    D3DXMATRIX operator+(const D3DXMATRIX &) const;
    D3DXMATRIX operator-(const D3DXMATRIX &) const;
    D3DXMATRIX operator*(FLOAT) const;
    D3DXMATRIX operator/(FLOAT) const;

    friend D3DXMATRIX operator*(FLOAT, const D3DXMATRIX &);

    BOOL operator==(const D3DXMATRIX &) const;
    BOOL operator!=(const D3DXMATRIX &) const;
} D3DXMATRIX, *LPD3DXMATRIX;

/* A matrix aligned to 16 bytes, for SIMD friendly code. */
struct alignas(16) _D3DXMATRIXA16 : public D3DXMATRIX {
    _D3DXMATRIXA16() {}
    _D3DXMATRIXA16(const FLOAT *f) : D3DXMATRIX(f) {}
    _D3DXMATRIXA16(const D3DMATRIX &m) : D3DXMATRIX(m) {}
    _D3DXMATRIXA16(FLOAT _11, FLOAT _12, FLOAT _13, FLOAT _14, FLOAT _21, FLOAT _22, FLOAT _23, FLOAT _24,
                   FLOAT _31, FLOAT _32, FLOAT _33, FLOAT _34, FLOAT _41, FLOAT _42, FLOAT _43, FLOAT _44)
        : D3DXMATRIX(_11, _12, _13, _14, _21, _22, _23, _24, _31, _32, _33, _34, _41, _42, _43, _44)
    {
    }
    _D3DXMATRIXA16 &operator=(const D3DXMATRIX &rhs)
    {
        memcpy(&this->m, &rhs.m, sizeof(this->m));
        return *this;
    }
};
typedef struct _D3DXMATRIXA16 D3DXMATRIXA16, *LPD3DXMATRIXA16;

typedef struct D3DXQUATERNION {
    D3DXQUATERNION() {}
    D3DXQUATERNION(const FLOAT *);
    D3DXQUATERNION(FLOAT x, FLOAT y, FLOAT z, FLOAT w);

    operator FLOAT *();
    operator const FLOAT *() const;

    D3DXQUATERNION &operator+=(const D3DXQUATERNION &);
    D3DXQUATERNION &operator-=(const D3DXQUATERNION &);
    D3DXQUATERNION &operator*=(const D3DXQUATERNION &);
    D3DXQUATERNION &operator*=(FLOAT);
    D3DXQUATERNION &operator/=(FLOAT);

    D3DXQUATERNION operator+() const;
    D3DXQUATERNION operator-() const;

    D3DXQUATERNION operator+(const D3DXQUATERNION &) const;
    D3DXQUATERNION operator-(const D3DXQUATERNION &) const;
    D3DXQUATERNION operator*(const D3DXQUATERNION &) const;
    D3DXQUATERNION operator*(FLOAT) const;
    D3DXQUATERNION operator/(FLOAT) const;

    friend D3DXQUATERNION operator*(FLOAT, const D3DXQUATERNION &);

    BOOL operator==(const D3DXQUATERNION &) const;
    BOOL operator!=(const D3DXQUATERNION &) const;

    FLOAT x, y, z, w;
} D3DXQUATERNION, *LPD3DXQUATERNION;

typedef struct D3DXPLANE {
    D3DXPLANE() {}
    D3DXPLANE(const FLOAT *);
    D3DXPLANE(FLOAT a, FLOAT b, FLOAT c, FLOAT d);

    operator FLOAT *();
    operator const FLOAT *() const;

    D3DXPLANE &operator*=(FLOAT);
    D3DXPLANE &operator/=(FLOAT);

    D3DXPLANE operator+() const;
    D3DXPLANE operator-() const;

    D3DXPLANE operator*(FLOAT) const;
    D3DXPLANE operator/(FLOAT) const;

    friend D3DXPLANE operator*(FLOAT, const D3DXPLANE &);

    BOOL operator==(const D3DXPLANE &) const;
    BOOL operator!=(const D3DXPLANE &) const;

    FLOAT a, b, c, d;
} D3DXPLANE, *LPD3DXPLANE;

typedef struct D3DXCOLOR {
    D3DXCOLOR() {}
    D3DXCOLOR(DWORD argb);
    D3DXCOLOR(const FLOAT *);
    D3DXCOLOR(const D3DCOLORVALUE &);
    D3DXCOLOR(FLOAT r, FLOAT g, FLOAT b, FLOAT a);

    operator DWORD() const;

    operator FLOAT *();
    operator const FLOAT *() const;

    operator D3DCOLORVALUE *();
    operator const D3DCOLORVALUE *() const;

    operator D3DCOLORVALUE &();
    operator const D3DCOLORVALUE &() const;

    D3DXCOLOR &operator+=(const D3DXCOLOR &);
    D3DXCOLOR &operator-=(const D3DXCOLOR &);
    D3DXCOLOR &operator*=(FLOAT);
    D3DXCOLOR &operator/=(FLOAT);

    D3DXCOLOR operator+() const;
    D3DXCOLOR operator-() const;

    D3DXCOLOR operator+(const D3DXCOLOR &) const;
    D3DXCOLOR operator-(const D3DXCOLOR &) const;
    D3DXCOLOR operator*(FLOAT) const;
    D3DXCOLOR operator/(FLOAT) const;

    friend D3DXCOLOR operator*(FLOAT, const D3DXCOLOR &);

    BOOL operator==(const D3DXCOLOR &) const;
    BOOL operator!=(const D3DXCOLOR &) const;

    FLOAT r, g, b, a;
} D3DXCOLOR, *LPD3DXCOLOR;

#else /* !__cplusplus */

typedef struct D3DXVECTOR2 {
    FLOAT x, y;
} D3DXVECTOR2, *LPD3DXVECTOR2;
typedef struct _D3DVECTOR D3DXVECTOR3, *LPD3DXVECTOR3;
typedef struct D3DXVECTOR4 {
    FLOAT x, y, z, w;
} D3DXVECTOR4, *LPD3DXVECTOR4;
typedef struct _D3DMATRIX D3DXMATRIX, *LPD3DXMATRIX;
typedef struct D3DXQUATERNION {
    FLOAT x, y, z, w;
} D3DXQUATERNION, *LPD3DXQUATERNION;
typedef struct D3DXPLANE {
    FLOAT a, b, c, d;
} D3DXPLANE, *LPD3DXPLANE;
typedef struct D3DXCOLOR {
    FLOAT r, g, b, a;
} D3DXCOLOR, *LPD3DXCOLOR;

#endif /* __cplusplus */

/*
 * Matrix stack
 */
#ifdef __cplusplus

#undef INTERFACE
#define INTERFACE ID3DXMatrixStack

DECLARE_INTERFACE_(ID3DXMatrixStack, IUnknown)
{
    /*** IUnknown methods ***/
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppvObj) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;

    /*** ID3DXMatrixStack methods ***/
    STDMETHOD(Pop)(THIS) PURE;
    STDMETHOD(Push)(THIS) PURE;
    STDMETHOD(LoadIdentity)(THIS) PURE;
    STDMETHOD(LoadMatrix)(THIS_ const D3DXMATRIX *pM) PURE;
    STDMETHOD(MultMatrix)(THIS_ const D3DXMATRIX *pM) PURE;
    STDMETHOD(MultMatrixLocal)(THIS_ const D3DXMATRIX *pM) PURE;
    STDMETHOD(RotateAxis)(THIS_ const D3DXVECTOR3 *pV, FLOAT Angle) PURE;
    STDMETHOD(RotateAxisLocal)(THIS_ const D3DXVECTOR3 *pV, FLOAT Angle) PURE;
    STDMETHOD(RotateYawPitchRoll)(THIS_ FLOAT Yaw, FLOAT Pitch, FLOAT Roll) PURE;
    STDMETHOD(RotateYawPitchRollLocal)(THIS_ FLOAT Yaw, FLOAT Pitch, FLOAT Roll) PURE;
    STDMETHOD(Scale)(THIS_ FLOAT x, FLOAT y, FLOAT z) PURE;
    STDMETHOD(ScaleLocal)(THIS_ FLOAT x, FLOAT y, FLOAT z) PURE;
    STDMETHOD(Translate)(THIS_ FLOAT x, FLOAT y, FLOAT z) PURE;
    STDMETHOD(TranslateLocal)(THIS_ FLOAT x, FLOAT y, FLOAT z) PURE;
    STDMETHOD_(D3DXMATRIX *, GetTop)(THIS) PURE;
};
#undef INTERFACE

typedef struct ID3DXMatrixStack *LPD3DXMATRIXSTACK;
#endif /* __cplusplus */

/*
 * Functions
 */
#ifdef __cplusplus
extern "C" {
#endif

/* 2D vector */
D3DXINLINE FLOAT D3DXVec2Length(const D3DXVECTOR2 *pV);
D3DXINLINE FLOAT D3DXVec2LengthSq(const D3DXVECTOR2 *pV);
D3DXINLINE FLOAT D3DXVec2Dot(const D3DXVECTOR2 *pV1, const D3DXVECTOR2 *pV2);
D3DXINLINE FLOAT D3DXVec2CCW(const D3DXVECTOR2 *pV1, const D3DXVECTOR2 *pV2);
D3DXINLINE D3DXVECTOR2 *D3DXVec2Add(D3DXVECTOR2 *pOut, const D3DXVECTOR2 *pV1, const D3DXVECTOR2 *pV2);
D3DXINLINE D3DXVECTOR2 *D3DXVec2Subtract(D3DXVECTOR2 *pOut, const D3DXVECTOR2 *pV1, const D3DXVECTOR2 *pV2);
D3DXINLINE D3DXVECTOR2 *D3DXVec2Minimize(D3DXVECTOR2 *pOut, const D3DXVECTOR2 *pV1, const D3DXVECTOR2 *pV2);
D3DXINLINE D3DXVECTOR2 *D3DXVec2Maximize(D3DXVECTOR2 *pOut, const D3DXVECTOR2 *pV1, const D3DXVECTOR2 *pV2);
D3DXINLINE D3DXVECTOR2 *D3DXVec2Scale(D3DXVECTOR2 *pOut, const D3DXVECTOR2 *pV, FLOAT s);
D3DXINLINE D3DXVECTOR2 *D3DXVec2Lerp(D3DXVECTOR2 *pOut, const D3DXVECTOR2 *pV1, const D3DXVECTOR2 *pV2, FLOAT s);

D3DXVECTOR2 *WINAPI D3DXVec2Normalize(D3DXVECTOR2 *pOut, const D3DXVECTOR2 *pV);
D3DXVECTOR2 *WINAPI D3DXVec2Hermite(D3DXVECTOR2 *pOut, const D3DXVECTOR2 *pV1, const D3DXVECTOR2 *pT1,
                                    const D3DXVECTOR2 *pV2, const D3DXVECTOR2 *pT2, FLOAT s);
D3DXVECTOR2 *WINAPI D3DXVec2CatmullRom(D3DXVECTOR2 *pOut, const D3DXVECTOR2 *pV0, const D3DXVECTOR2 *pV1,
                                       const D3DXVECTOR2 *pV2, const D3DXVECTOR2 *pV3, FLOAT s);
D3DXVECTOR2 *WINAPI D3DXVec2BaryCentric(D3DXVECTOR2 *pOut, const D3DXVECTOR2 *pV1, const D3DXVECTOR2 *pV2,
                                        const D3DXVECTOR2 *pV3, FLOAT f, FLOAT g);
D3DXVECTOR4 *WINAPI D3DXVec2Transform(D3DXVECTOR4 *pOut, const D3DXVECTOR2 *pV, const D3DXMATRIX *pM);
D3DXVECTOR2 *WINAPI D3DXVec2TransformCoord(D3DXVECTOR2 *pOut, const D3DXVECTOR2 *pV, const D3DXMATRIX *pM);
D3DXVECTOR2 *WINAPI D3DXVec2TransformNormal(D3DXVECTOR2 *pOut, const D3DXVECTOR2 *pV, const D3DXMATRIX *pM);

/* 3D vector */
D3DXINLINE FLOAT D3DXVec3Length(const D3DXVECTOR3 *pV);
D3DXINLINE FLOAT D3DXVec3LengthSq(const D3DXVECTOR3 *pV);
D3DXINLINE FLOAT D3DXVec3Dot(const D3DXVECTOR3 *pV1, const D3DXVECTOR3 *pV2);
D3DXINLINE D3DXVECTOR3 *D3DXVec3Cross(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV1, const D3DXVECTOR3 *pV2);
D3DXINLINE D3DXVECTOR3 *D3DXVec3Add(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV1, const D3DXVECTOR3 *pV2);
D3DXINLINE D3DXVECTOR3 *D3DXVec3Subtract(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV1, const D3DXVECTOR3 *pV2);
D3DXINLINE D3DXVECTOR3 *D3DXVec3Minimize(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV1, const D3DXVECTOR3 *pV2);
D3DXINLINE D3DXVECTOR3 *D3DXVec3Maximize(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV1, const D3DXVECTOR3 *pV2);
D3DXINLINE D3DXVECTOR3 *D3DXVec3Scale(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV, FLOAT s);
D3DXINLINE D3DXVECTOR3 *D3DXVec3Lerp(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV1, const D3DXVECTOR3 *pV2, FLOAT s);

D3DXVECTOR3 *WINAPI D3DXVec3Normalize(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV);
D3DXVECTOR3 *WINAPI D3DXVec3Hermite(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV1, const D3DXVECTOR3 *pT1,
                                    const D3DXVECTOR3 *pV2, const D3DXVECTOR3 *pT2, FLOAT s);
D3DXVECTOR3 *WINAPI D3DXVec3CatmullRom(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV0, const D3DXVECTOR3 *pV1,
                                       const D3DXVECTOR3 *pV2, const D3DXVECTOR3 *pV3, FLOAT s);
D3DXVECTOR3 *WINAPI D3DXVec3BaryCentric(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV1, const D3DXVECTOR3 *pV2,
                                        const D3DXVECTOR3 *pV3, FLOAT f, FLOAT g);
D3DXVECTOR4 *WINAPI D3DXVec3Transform(D3DXVECTOR4 *pOut, const D3DXVECTOR3 *pV, const D3DXMATRIX *pM);
D3DXVECTOR3 *WINAPI D3DXVec3TransformCoord(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV, const D3DXMATRIX *pM);
D3DXVECTOR3 *WINAPI D3DXVec3TransformNormal(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV, const D3DXMATRIX *pM);
D3DXVECTOR3 *WINAPI D3DXVec3Project(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV, const D3DVIEWPORT8 *pViewport,
                                    const D3DXMATRIX *pProjection, const D3DXMATRIX *pView, const D3DXMATRIX *pWorld);
D3DXVECTOR3 *WINAPI D3DXVec3Unproject(D3DXVECTOR3 *pOut, const D3DXVECTOR3 *pV, const D3DVIEWPORT8 *pViewport,
                                      const D3DXMATRIX *pProjection, const D3DXMATRIX *pView,
                                      const D3DXMATRIX *pWorld);

/* 4D vector */
D3DXINLINE FLOAT D3DXVec4Length(const D3DXVECTOR4 *pV);
D3DXINLINE FLOAT D3DXVec4LengthSq(const D3DXVECTOR4 *pV);
D3DXINLINE FLOAT D3DXVec4Dot(const D3DXVECTOR4 *pV1, const D3DXVECTOR4 *pV2);
D3DXINLINE D3DXVECTOR4 *D3DXVec4Add(D3DXVECTOR4 *pOut, const D3DXVECTOR4 *pV1, const D3DXVECTOR4 *pV2);
D3DXINLINE D3DXVECTOR4 *D3DXVec4Subtract(D3DXVECTOR4 *pOut, const D3DXVECTOR4 *pV1, const D3DXVECTOR4 *pV2);
D3DXINLINE D3DXVECTOR4 *D3DXVec4Minimize(D3DXVECTOR4 *pOut, const D3DXVECTOR4 *pV1, const D3DXVECTOR4 *pV2);
D3DXINLINE D3DXVECTOR4 *D3DXVec4Maximize(D3DXVECTOR4 *pOut, const D3DXVECTOR4 *pV1, const D3DXVECTOR4 *pV2);
D3DXINLINE D3DXVECTOR4 *D3DXVec4Scale(D3DXVECTOR4 *pOut, const D3DXVECTOR4 *pV, FLOAT s);
D3DXINLINE D3DXVECTOR4 *D3DXVec4Lerp(D3DXVECTOR4 *pOut, const D3DXVECTOR4 *pV1, const D3DXVECTOR4 *pV2, FLOAT s);

D3DXVECTOR4 *WINAPI D3DXVec4Cross(D3DXVECTOR4 *pOut, const D3DXVECTOR4 *pV1, const D3DXVECTOR4 *pV2,
                                  const D3DXVECTOR4 *pV3);
D3DXVECTOR4 *WINAPI D3DXVec4Normalize(D3DXVECTOR4 *pOut, const D3DXVECTOR4 *pV);
D3DXVECTOR4 *WINAPI D3DXVec4Hermite(D3DXVECTOR4 *pOut, const D3DXVECTOR4 *pV1, const D3DXVECTOR4 *pT1,
                                    const D3DXVECTOR4 *pV2, const D3DXVECTOR4 *pT2, FLOAT s);
D3DXVECTOR4 *WINAPI D3DXVec4CatmullRom(D3DXVECTOR4 *pOut, const D3DXVECTOR4 *pV0, const D3DXVECTOR4 *pV1,
                                       const D3DXVECTOR4 *pV2, const D3DXVECTOR4 *pV3, FLOAT s);
D3DXVECTOR4 *WINAPI D3DXVec4BaryCentric(D3DXVECTOR4 *pOut, const D3DXVECTOR4 *pV1, const D3DXVECTOR4 *pV2,
                                        const D3DXVECTOR4 *pV3, FLOAT f, FLOAT g);
D3DXVECTOR4 *WINAPI D3DXVec4Transform(D3DXVECTOR4 *pOut, const D3DXVECTOR4 *pV, const D3DXMATRIX *pM);

/* 4x4 matrix */
D3DXINLINE D3DXMATRIX *D3DXMatrixIdentity(D3DXMATRIX *pOut);
D3DXINLINE BOOL D3DXMatrixIsIdentity(const D3DXMATRIX *pM);

FLOAT WINAPI D3DXMatrixfDeterminant(const D3DXMATRIX *pM);
D3DXMATRIX *WINAPI D3DXMatrixTranspose(D3DXMATRIX *pOut, const D3DXMATRIX *pM);
D3DXMATRIX *WINAPI D3DXMatrixMultiply(D3DXMATRIX *pOut, const D3DXMATRIX *pM1, const D3DXMATRIX *pM2);
D3DXMATRIX *WINAPI D3DXMatrixMultiplyTranspose(D3DXMATRIX *pOut, const D3DXMATRIX *pM1, const D3DXMATRIX *pM2);
D3DXMATRIX *WINAPI D3DXMatrixInverse(D3DXMATRIX *pOut, FLOAT *pDeterminant, const D3DXMATRIX *pM);
D3DXMATRIX *WINAPI D3DXMatrixScaling(D3DXMATRIX *pOut, FLOAT sx, FLOAT sy, FLOAT sz);
D3DXMATRIX *WINAPI D3DXMatrixTranslation(D3DXMATRIX *pOut, FLOAT x, FLOAT y, FLOAT z);
D3DXMATRIX *WINAPI D3DXMatrixRotationX(D3DXMATRIX *pOut, FLOAT Angle);
D3DXMATRIX *WINAPI D3DXMatrixRotationY(D3DXMATRIX *pOut, FLOAT Angle);
D3DXMATRIX *WINAPI D3DXMatrixRotationZ(D3DXMATRIX *pOut, FLOAT Angle);
D3DXMATRIX *WINAPI D3DXMatrixRotationAxis(D3DXMATRIX *pOut, const D3DXVECTOR3 *pV, FLOAT Angle);
D3DXMATRIX *WINAPI D3DXMatrixRotationQuaternion(D3DXMATRIX *pOut, const D3DXQUATERNION *pQ);
D3DXMATRIX *WINAPI D3DXMatrixRotationYawPitchRoll(D3DXMATRIX *pOut, FLOAT Yaw, FLOAT Pitch, FLOAT Roll);
D3DXMATRIX *WINAPI D3DXMatrixTransformation(D3DXMATRIX *pOut, const D3DXVECTOR3 *pScalingCenter,
                                            const D3DXQUATERNION *pScalingRotation, const D3DXVECTOR3 *pScaling,
                                            const D3DXVECTOR3 *pRotationCenter, const D3DXQUATERNION *pRotation,
                                            const D3DXVECTOR3 *pTranslation);
D3DXMATRIX *WINAPI D3DXMatrixAffineTransformation(D3DXMATRIX *pOut, FLOAT Scaling, const D3DXVECTOR3 *pRotationCenter,
                                                  const D3DXQUATERNION *pRotation, const D3DXVECTOR3 *pTranslation);
D3DXMATRIX *WINAPI D3DXMatrixLookAtRH(D3DXMATRIX *pOut, const D3DXVECTOR3 *pEye, const D3DXVECTOR3 *pAt,
                                      const D3DXVECTOR3 *pUp);
D3DXMATRIX *WINAPI D3DXMatrixLookAtLH(D3DXMATRIX *pOut, const D3DXVECTOR3 *pEye, const D3DXVECTOR3 *pAt,
                                      const D3DXVECTOR3 *pUp);
D3DXMATRIX *WINAPI D3DXMatrixPerspectiveRH(D3DXMATRIX *pOut, FLOAT w, FLOAT h, FLOAT zn, FLOAT zf);
D3DXMATRIX *WINAPI D3DXMatrixPerspectiveLH(D3DXMATRIX *pOut, FLOAT w, FLOAT h, FLOAT zn, FLOAT zf);
D3DXMATRIX *WINAPI D3DXMatrixPerspectiveFovRH(D3DXMATRIX *pOut, FLOAT fovy, FLOAT aspect, FLOAT zn, FLOAT zf);
D3DXMATRIX *WINAPI D3DXMatrixPerspectiveFovLH(D3DXMATRIX *pOut, FLOAT fovy, FLOAT aspect, FLOAT zn, FLOAT zf);
D3DXMATRIX *WINAPI D3DXMatrixPerspectiveOffCenterRH(D3DXMATRIX *pOut, FLOAT l, FLOAT r, FLOAT b, FLOAT t, FLOAT zn,
                                                    FLOAT zf);
D3DXMATRIX *WINAPI D3DXMatrixPerspectiveOffCenterLH(D3DXMATRIX *pOut, FLOAT l, FLOAT r, FLOAT b, FLOAT t, FLOAT zn,
                                                    FLOAT zf);
D3DXMATRIX *WINAPI D3DXMatrixOrthoRH(D3DXMATRIX *pOut, FLOAT w, FLOAT h, FLOAT zn, FLOAT zf);
D3DXMATRIX *WINAPI D3DXMatrixOrthoLH(D3DXMATRIX *pOut, FLOAT w, FLOAT h, FLOAT zn, FLOAT zf);
D3DXMATRIX *WINAPI D3DXMatrixOrthoOffCenterRH(D3DXMATRIX *pOut, FLOAT l, FLOAT r, FLOAT b, FLOAT t, FLOAT zn,
                                              FLOAT zf);
D3DXMATRIX *WINAPI D3DXMatrixOrthoOffCenterLH(D3DXMATRIX *pOut, FLOAT l, FLOAT r, FLOAT b, FLOAT t, FLOAT zn,
                                              FLOAT zf);
D3DXMATRIX *WINAPI D3DXMatrixShadow(D3DXMATRIX *pOut, const D3DXVECTOR4 *pLight, const D3DXPLANE *pPlane);
D3DXMATRIX *WINAPI D3DXMatrixReflect(D3DXMATRIX *pOut, const D3DXPLANE *pPlane);

/* Quaternion */
D3DXINLINE FLOAT D3DXQuaternionLength(const D3DXQUATERNION *pQ);
D3DXINLINE FLOAT D3DXQuaternionLengthSq(const D3DXQUATERNION *pQ);
D3DXINLINE FLOAT D3DXQuaternionDot(const D3DXQUATERNION *pQ1, const D3DXQUATERNION *pQ2);
D3DXINLINE D3DXQUATERNION *D3DXQuaternionIdentity(D3DXQUATERNION *pOut);
D3DXINLINE BOOL D3DXQuaternionIsIdentity(const D3DXQUATERNION *pQ);
D3DXINLINE D3DXQUATERNION *D3DXQuaternionConjugate(D3DXQUATERNION *pOut, const D3DXQUATERNION *pQ);

void WINAPI D3DXQuaternionToAxisAngle(const D3DXQUATERNION *pQ, D3DXVECTOR3 *pAxis, FLOAT *pAngle);
D3DXQUATERNION *WINAPI D3DXQuaternionRotationMatrix(D3DXQUATERNION *pOut, const D3DXMATRIX *pM);
D3DXQUATERNION *WINAPI D3DXQuaternionRotationAxis(D3DXQUATERNION *pOut, const D3DXVECTOR3 *pV, FLOAT Angle);
D3DXQUATERNION *WINAPI D3DXQuaternionRotationYawPitchRoll(D3DXQUATERNION *pOut, FLOAT Yaw, FLOAT Pitch, FLOAT Roll);
D3DXQUATERNION *WINAPI D3DXQuaternionMultiply(D3DXQUATERNION *pOut, const D3DXQUATERNION *pQ1,
                                              const D3DXQUATERNION *pQ2);
D3DXQUATERNION *WINAPI D3DXQuaternionNormalize(D3DXQUATERNION *pOut, const D3DXQUATERNION *pQ);
D3DXQUATERNION *WINAPI D3DXQuaternionInverse(D3DXQUATERNION *pOut, const D3DXQUATERNION *pQ);
D3DXQUATERNION *WINAPI D3DXQuaternionLn(D3DXQUATERNION *pOut, const D3DXQUATERNION *pQ);
D3DXQUATERNION *WINAPI D3DXQuaternionExp(D3DXQUATERNION *pOut, const D3DXQUATERNION *pQ);
D3DXQUATERNION *WINAPI D3DXQuaternionSlerp(D3DXQUATERNION *pOut, const D3DXQUATERNION *pQ1, const D3DXQUATERNION *pQ2,
                                           FLOAT t);
D3DXQUATERNION *WINAPI D3DXQuaternionSquad(D3DXQUATERNION *pOut, const D3DXQUATERNION *pQ1, const D3DXQUATERNION *pA,
                                           const D3DXQUATERNION *pB, const D3DXQUATERNION *pC, FLOAT t);
void WINAPI D3DXQuaternionSquadSetup(D3DXQUATERNION *pAOut, D3DXQUATERNION *pBOut, D3DXQUATERNION *pCOut,
                                     const D3DXQUATERNION *pQ0, const D3DXQUATERNION *pQ1, const D3DXQUATERNION *pQ2,
                                     const D3DXQUATERNION *pQ3);
D3DXQUATERNION *WINAPI D3DXQuaternionBaryCentric(D3DXQUATERNION *pOut, const D3DXQUATERNION *pQ1,
                                                 const D3DXQUATERNION *pQ2, const D3DXQUATERNION *pQ3, FLOAT f,
                                                 FLOAT g);

/* Plane */
D3DXINLINE FLOAT D3DXPlaneDot(const D3DXPLANE *pP, const D3DXVECTOR4 *pV);
D3DXINLINE FLOAT D3DXPlaneDotCoord(const D3DXPLANE *pP, const D3DXVECTOR3 *pV);
D3DXINLINE FLOAT D3DXPlaneDotNormal(const D3DXPLANE *pP, const D3DXVECTOR3 *pV);

D3DXPLANE *WINAPI D3DXPlaneNormalize(D3DXPLANE *pOut, const D3DXPLANE *pP);
D3DXVECTOR3 *WINAPI D3DXPlaneIntersectLine(D3DXVECTOR3 *pOut, const D3DXPLANE *pP, const D3DXVECTOR3 *pV1,
                                           const D3DXVECTOR3 *pV2);
D3DXPLANE *WINAPI D3DXPlaneFromPointNormal(D3DXPLANE *pOut, const D3DXVECTOR3 *pPoint, const D3DXVECTOR3 *pNormal);
D3DXPLANE *WINAPI D3DXPlaneFromPoints(D3DXPLANE *pOut, const D3DXVECTOR3 *pV1, const D3DXVECTOR3 *pV2,
                                      const D3DXVECTOR3 *pV3);
D3DXPLANE *WINAPI D3DXPlaneTransform(D3DXPLANE *pOut, const D3DXPLANE *pP, const D3DXMATRIX *pM);

/* Color */
D3DXINLINE D3DXCOLOR *D3DXColorNegative(D3DXCOLOR *pOut, const D3DXCOLOR *pC);
D3DXINLINE D3DXCOLOR *D3DXColorAdd(D3DXCOLOR *pOut, const D3DXCOLOR *pC1, const D3DXCOLOR *pC2);
D3DXINLINE D3DXCOLOR *D3DXColorSubtract(D3DXCOLOR *pOut, const D3DXCOLOR *pC1, const D3DXCOLOR *pC2);
D3DXINLINE D3DXCOLOR *D3DXColorScale(D3DXCOLOR *pOut, const D3DXCOLOR *pC, FLOAT s);
D3DXINLINE D3DXCOLOR *D3DXColorModulate(D3DXCOLOR *pOut, const D3DXCOLOR *pC1, const D3DXCOLOR *pC2);
D3DXINLINE D3DXCOLOR *D3DXColorLerp(D3DXCOLOR *pOut, const D3DXCOLOR *pC1, const D3DXCOLOR *pC2, FLOAT s);

D3DXCOLOR *WINAPI D3DXColorAdjustSaturation(D3DXCOLOR *pOut, const D3DXCOLOR *pC, FLOAT s);
D3DXCOLOR *WINAPI D3DXColorAdjustContrast(D3DXCOLOR *pOut, const D3DXCOLOR *pC, FLOAT c);

/* Misc */
FLOAT WINAPI D3DXFresnelTerm(FLOAT CosTheta, FLOAT RefractionIndex);

#ifdef __cplusplus
/* Creates a matrix stack (see ID3DXMatrixStack above). */
HRESULT WINAPI D3DXCreateMatrixStack(DWORD Flags, LPD3DXMATRIXSTACK *ppStack);
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#include "d3dx8math.inl"

#endif /* __D3DX8MATH_H__ */
