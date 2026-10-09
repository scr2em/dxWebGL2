/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * d3dx8math.inl
 *
 * Inline parts of d3dx8math.h: class members and the small D3DX math helpers.
 * Included by d3dx8math.h.
 */
#ifndef __D3DX8MATH_INL__
#define __D3DX8MATH_INL__

#ifdef __cplusplus

/*
 * D3DXVECTOR2
 */
D3DXINLINE D3DXVECTOR2::D3DXVECTOR2(const FLOAT *f) : x(f[0]), y(f[1]) {}
D3DXINLINE D3DXVECTOR2::D3DXVECTOR2(FLOAT fx, FLOAT fy) : x(fx), y(fy) {}
D3DXINLINE D3DXVECTOR2::operator FLOAT *() { return &x; }
D3DXINLINE D3DXVECTOR2::operator const FLOAT *() const { return &x; }
D3DXINLINE D3DXVECTOR2 &D3DXVECTOR2::operator+=(const D3DXVECTOR2 &v) { x += v.x; y += v.y; return *this; }
D3DXINLINE D3DXVECTOR2 &D3DXVECTOR2::operator-=(const D3DXVECTOR2 &v) { x -= v.x; y -= v.y; return *this; }
D3DXINLINE D3DXVECTOR2 &D3DXVECTOR2::operator*=(FLOAT f) { x *= f; y *= f; return *this; }
D3DXINLINE D3DXVECTOR2 &D3DXVECTOR2::operator/=(FLOAT f) { FLOAT inv = 1.0f / f; x *= inv; y *= inv; return *this; }
D3DXINLINE D3DXVECTOR2 D3DXVECTOR2::operator+() const { return *this; }
D3DXINLINE D3DXVECTOR2 D3DXVECTOR2::operator-() const { return D3DXVECTOR2(-x, -y); }
D3DXINLINE D3DXVECTOR2 D3DXVECTOR2::operator+(const D3DXVECTOR2 &v) const { return D3DXVECTOR2(x + v.x, y + v.y); }
D3DXINLINE D3DXVECTOR2 D3DXVECTOR2::operator-(const D3DXVECTOR2 &v) const { return D3DXVECTOR2(x - v.x, y - v.y); }
D3DXINLINE D3DXVECTOR2 D3DXVECTOR2::operator*(FLOAT f) const { return D3DXVECTOR2(x * f, y * f); }
D3DXINLINE D3DXVECTOR2 D3DXVECTOR2::operator/(FLOAT f) const { FLOAT inv = 1.0f / f; return D3DXVECTOR2(x * inv, y * inv); }
D3DXINLINE D3DXVECTOR2 operator*(FLOAT f, const D3DXVECTOR2 &v) { return D3DXVECTOR2(f * v.x, f * v.y); }
D3DXINLINE BOOL D3DXVECTOR2::operator==(const D3DXVECTOR2 &v) const { return x == v.x && y == v.y; }
D3DXINLINE BOOL D3DXVECTOR2::operator!=(const D3DXVECTOR2 &v) const { return x != v.x || y != v.y; }

/*
 * D3DXVECTOR3
 */
D3DXINLINE D3DXVECTOR3::D3DXVECTOR3(const FLOAT *f) { x = f[0]; y = f[1]; z = f[2]; }
D3DXINLINE D3DXVECTOR3::D3DXVECTOR3(const D3DVECTOR &v) { x = v.x; y = v.y; z = v.z; }
D3DXINLINE D3DXVECTOR3::D3DXVECTOR3(FLOAT fx, FLOAT fy, FLOAT fz) { x = fx; y = fy; z = fz; }
D3DXINLINE D3DXVECTOR3::operator FLOAT *() { return &x; }
D3DXINLINE D3DXVECTOR3::operator const FLOAT *() const { return &x; }
D3DXINLINE D3DXVECTOR3 &D3DXVECTOR3::operator+=(const D3DXVECTOR3 &v) { x += v.x; y += v.y; z += v.z; return *this; }
D3DXINLINE D3DXVECTOR3 &D3DXVECTOR3::operator-=(const D3DXVECTOR3 &v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
D3DXINLINE D3DXVECTOR3 &D3DXVECTOR3::operator*=(FLOAT f) { x *= f; y *= f; z *= f; return *this; }
D3DXINLINE D3DXVECTOR3 &D3DXVECTOR3::operator/=(FLOAT f) { FLOAT inv = 1.0f / f; x *= inv; y *= inv; z *= inv; return *this; }
D3DXINLINE D3DXVECTOR3 D3DXVECTOR3::operator+() const { return *this; }
D3DXINLINE D3DXVECTOR3 D3DXVECTOR3::operator-() const { return D3DXVECTOR3(-x, -y, -z); }
D3DXINLINE D3DXVECTOR3 D3DXVECTOR3::operator+(const D3DXVECTOR3 &v) const { return D3DXVECTOR3(x + v.x, y + v.y, z + v.z); }
D3DXINLINE D3DXVECTOR3 D3DXVECTOR3::operator-(const D3DXVECTOR3 &v) const { return D3DXVECTOR3(x - v.x, y - v.y, z - v.z); }
D3DXINLINE D3DXVECTOR3 D3DXVECTOR3::operator*(FLOAT f) const { return D3DXVECTOR3(x * f, y * f, z * f); }
D3DXINLINE D3DXVECTOR3 D3DXVECTOR3::operator/(FLOAT f) const { FLOAT inv = 1.0f / f; return D3DXVECTOR3(x * inv, y * inv, z * inv); }
D3DXINLINE D3DXVECTOR3 operator*(FLOAT f, const D3DXVECTOR3 &v) { return D3DXVECTOR3(f * v.x, f * v.y, f * v.z); }
D3DXINLINE BOOL D3DXVECTOR3::operator==(const D3DXVECTOR3 &v) const { return x == v.x && y == v.y && z == v.z; }
D3DXINLINE BOOL D3DXVECTOR3::operator!=(const D3DXVECTOR3 &v) const { return x != v.x || y != v.y || z != v.z; }

/*
 * D3DXVECTOR4
 */
D3DXINLINE D3DXVECTOR4::D3DXVECTOR4(const FLOAT *f) : x(f[0]), y(f[1]), z(f[2]), w(f[3]) {}
D3DXINLINE D3DXVECTOR4::D3DXVECTOR4(FLOAT fx, FLOAT fy, FLOAT fz, FLOAT fw) : x(fx), y(fy), z(fz), w(fw) {}
D3DXINLINE D3DXVECTOR4::operator FLOAT *() { return &x; }
D3DXINLINE D3DXVECTOR4::operator const FLOAT *() const { return &x; }
D3DXINLINE D3DXVECTOR4 &D3DXVECTOR4::operator+=(const D3DXVECTOR4 &v) { x += v.x; y += v.y; z += v.z; w += v.w; return *this; }
D3DXINLINE D3DXVECTOR4 &D3DXVECTOR4::operator-=(const D3DXVECTOR4 &v) { x -= v.x; y -= v.y; z -= v.z; w -= v.w; return *this; }
D3DXINLINE D3DXVECTOR4 &D3DXVECTOR4::operator*=(FLOAT f) { x *= f; y *= f; z *= f; w *= f; return *this; }
D3DXINLINE D3DXVECTOR4 &D3DXVECTOR4::operator/=(FLOAT f) { FLOAT inv = 1.0f / f; x *= inv; y *= inv; z *= inv; w *= inv; return *this; }
D3DXINLINE D3DXVECTOR4 D3DXVECTOR4::operator+() const { return *this; }
D3DXINLINE D3DXVECTOR4 D3DXVECTOR4::operator-() const { return D3DXVECTOR4(-x, -y, -z, -w); }
D3DXINLINE D3DXVECTOR4 D3DXVECTOR4::operator+(const D3DXVECTOR4 &v) const { return D3DXVECTOR4(x + v.x, y + v.y, z + v.z, w + v.w); }
D3DXINLINE D3DXVECTOR4 D3DXVECTOR4::operator-(const D3DXVECTOR4 &v) const { return D3DXVECTOR4(x - v.x, y - v.y, z - v.z, w - v.w); }
D3DXINLINE D3DXVECTOR4 D3DXVECTOR4::operator*(FLOAT f) const { return D3DXVECTOR4(x * f, y * f, z * f, w * f); }
D3DXINLINE D3DXVECTOR4 D3DXVECTOR4::operator/(FLOAT f) const { FLOAT inv = 1.0f / f; return D3DXVECTOR4(x * inv, y * inv, z * inv, w * inv); }
D3DXINLINE D3DXVECTOR4 operator*(FLOAT f, const D3DXVECTOR4 &v) { return D3DXVECTOR4(f * v.x, f * v.y, f * v.z, f * v.w); }
D3DXINLINE BOOL D3DXVECTOR4::operator==(const D3DXVECTOR4 &v) const { return x == v.x && y == v.y && z == v.z && w == v.w; }
D3DXINLINE BOOL D3DXVECTOR4::operator!=(const D3DXVECTOR4 &v) const { return x != v.x || y != v.y || z != v.z || w != v.w; }

/*
 * D3DXMATRIX
 */
D3DXINLINE D3DXMATRIX::D3DXMATRIX(const FLOAT *f) { memcpy(&_11, f, sizeof(D3DXMATRIX)); }
D3DXINLINE D3DXMATRIX::D3DXMATRIX(const D3DMATRIX &mat) { memcpy(&_11, &mat, sizeof(D3DXMATRIX)); }
D3DXINLINE D3DXMATRIX::D3DXMATRIX(FLOAT f11, FLOAT f12, FLOAT f13, FLOAT f14, FLOAT f21, FLOAT f22, FLOAT f23, FLOAT f24,
                                  FLOAT f31, FLOAT f32, FLOAT f33, FLOAT f34, FLOAT f41, FLOAT f42, FLOAT f43, FLOAT f44)
{
    _11 = f11; _12 = f12; _13 = f13; _14 = f14;
    _21 = f21; _22 = f22; _23 = f23; _24 = f24;
    _31 = f31; _32 = f32; _33 = f33; _34 = f34;
    _41 = f41; _42 = f42; _43 = f43; _44 = f44;
}
D3DXINLINE FLOAT &D3DXMATRIX::operator()(UINT iRow, UINT iCol) { return m[iRow][iCol]; }
D3DXINLINE FLOAT D3DXMATRIX::operator()(UINT iRow, UINT iCol) const { return m[iRow][iCol]; }
D3DXINLINE D3DXMATRIX::operator FLOAT *() { return &_11; }
D3DXINLINE D3DXMATRIX::operator const FLOAT *() const { return &_11; }
D3DXINLINE D3DXMATRIX &D3DXMATRIX::operator*=(const D3DXMATRIX &mat)
{
    D3DXMatrixMultiply(this, this, &mat);
    return *this;
}
D3DXINLINE D3DXMATRIX &D3DXMATRIX::operator+=(const D3DXMATRIX &mat)
{
    for (int i = 0; i < 16; ++i) (&_11)[i] += (&mat._11)[i];
    return *this;
}
D3DXINLINE D3DXMATRIX &D3DXMATRIX::operator-=(const D3DXMATRIX &mat)
{
    for (int i = 0; i < 16; ++i) (&_11)[i] -= (&mat._11)[i];
    return *this;
}
D3DXINLINE D3DXMATRIX &D3DXMATRIX::operator*=(FLOAT f)
{
    for (int i = 0; i < 16; ++i) (&_11)[i] *= f;
    return *this;
}
D3DXINLINE D3DXMATRIX &D3DXMATRIX::operator/=(FLOAT f)
{
    FLOAT inv = 1.0f / f;
    for (int i = 0; i < 16; ++i) (&_11)[i] *= inv;
    return *this;
}
D3DXINLINE D3DXMATRIX D3DXMATRIX::operator+() const { return *this; }
D3DXINLINE D3DXMATRIX D3DXMATRIX::operator-() const
{
    D3DXMATRIX r;
    for (int i = 0; i < 16; ++i) (&r._11)[i] = -(&_11)[i];
    return r;
}
D3DXINLINE D3DXMATRIX D3DXMATRIX::operator*(const D3DXMATRIX &mat) const
{
    D3DXMATRIX r;
    D3DXMatrixMultiply(&r, this, &mat);
    return r;
}
D3DXINLINE D3DXMATRIX D3DXMATRIX::operator+(const D3DXMATRIX &mat) const
{
    D3DXMATRIX r;
    for (int i = 0; i < 16; ++i) (&r._11)[i] = (&_11)[i] + (&mat._11)[i];
    return r;
}
D3DXINLINE D3DXMATRIX D3DXMATRIX::operator-(const D3DXMATRIX &mat) const
{
    D3DXMATRIX r;
    for (int i = 0; i < 16; ++i) (&r._11)[i] = (&_11)[i] - (&mat._11)[i];
    return r;
}
D3DXINLINE D3DXMATRIX D3DXMATRIX::operator*(FLOAT f) const
{
    D3DXMATRIX r;
    for (int i = 0; i < 16; ++i) (&r._11)[i] = (&_11)[i] * f;
    return r;
}
D3DXINLINE D3DXMATRIX D3DXMATRIX::operator/(FLOAT f) const
{
    D3DXMATRIX r;
    FLOAT inv = 1.0f / f;
    for (int i = 0; i < 16; ++i) (&r._11)[i] = (&_11)[i] * inv;
    return r;
}
D3DXINLINE D3DXMATRIX operator*(FLOAT f, const D3DXMATRIX &mat)
{
    D3DXMATRIX r;
    for (int i = 0; i < 16; ++i) (&r._11)[i] = f * (&mat._11)[i];
    return r;
}
D3DXINLINE BOOL D3DXMATRIX::operator==(const D3DXMATRIX &mat) const { return 0 == memcmp(this, &mat, sizeof(D3DXMATRIX)); }
D3DXINLINE BOOL D3DXMATRIX::operator!=(const D3DXMATRIX &mat) const { return 0 != memcmp(this, &mat, sizeof(D3DXMATRIX)); }

/*
 * D3DXQUATERNION
 */
D3DXINLINE D3DXQUATERNION::D3DXQUATERNION(const FLOAT *f) : x(f[0]), y(f[1]), z(f[2]), w(f[3]) {}
D3DXINLINE D3DXQUATERNION::D3DXQUATERNION(FLOAT fx, FLOAT fy, FLOAT fz, FLOAT fw) : x(fx), y(fy), z(fz), w(fw) {}
D3DXINLINE D3DXQUATERNION::operator FLOAT *() { return &x; }
D3DXINLINE D3DXQUATERNION::operator const FLOAT *() const { return &x; }
D3DXINLINE D3DXQUATERNION &D3DXQUATERNION::operator+=(const D3DXQUATERNION &q) { x += q.x; y += q.y; z += q.z; w += q.w; return *this; }
D3DXINLINE D3DXQUATERNION &D3DXQUATERNION::operator-=(const D3DXQUATERNION &q) { x -= q.x; y -= q.y; z -= q.z; w -= q.w; return *this; }
D3DXINLINE D3DXQUATERNION &D3DXQUATERNION::operator*=(const D3DXQUATERNION &q)
{
    D3DXQuaternionMultiply(this, this, &q);
    return *this;
}
D3DXINLINE D3DXQUATERNION &D3DXQUATERNION::operator*=(FLOAT f) { x *= f; y *= f; z *= f; w *= f; return *this; }
D3DXINLINE D3DXQUATERNION &D3DXQUATERNION::operator/=(FLOAT f) { FLOAT inv = 1.0f / f; x *= inv; y *= inv; z *= inv; w *= inv; return *this; }
D3DXINLINE D3DXQUATERNION D3DXQUATERNION::operator+() const { return *this; }
D3DXINLINE D3DXQUATERNION D3DXQUATERNION::operator-() const { return D3DXQUATERNION(-x, -y, -z, -w); }
D3DXINLINE D3DXQUATERNION D3DXQUATERNION::operator+(const D3DXQUATERNION &q) const { return D3DXQUATERNION(x + q.x, y + q.y, z + q.z, w + q.w); }
D3DXINLINE D3DXQUATERNION D3DXQUATERNION::operator-(const D3DXQUATERNION &q) const { return D3DXQUATERNION(x - q.x, y - q.y, z - q.z, w - q.w); }
D3DXINLINE D3DXQUATERNION D3DXQUATERNION::operator*(const D3DXQUATERNION &q) const
{
    D3DXQUATERNION r;
    D3DXQuaternionMultiply(&r, this, &q);
    return r;
}
D3DXINLINE D3DXQUATERNION D3DXQUATERNION::operator*(FLOAT f) const { return D3DXQUATERNION(x * f, y * f, z * f, w * f); }
D3DXINLINE D3DXQUATERNION D3DXQUATERNION::operator/(FLOAT f) const { FLOAT inv = 1.0f / f; return D3DXQUATERNION(x * inv, y * inv, z * inv, w * inv); }
D3DXINLINE D3DXQUATERNION operator*(FLOAT f, const D3DXQUATERNION &q) { return D3DXQUATERNION(f * q.x, f * q.y, f * q.z, f * q.w); }
D3DXINLINE BOOL D3DXQUATERNION::operator==(const D3DXQUATERNION &q) const { return x == q.x && y == q.y && z == q.z && w == q.w; }
D3DXINLINE BOOL D3DXQUATERNION::operator!=(const D3DXQUATERNION &q) const { return x != q.x || y != q.y || z != q.z || w != q.w; }

/*
 * D3DXPLANE
 */
D3DXINLINE D3DXPLANE::D3DXPLANE(const FLOAT *f) : a(f[0]), b(f[1]), c(f[2]), d(f[3]) {}
D3DXINLINE D3DXPLANE::D3DXPLANE(FLOAT fa, FLOAT fb, FLOAT fc, FLOAT fd) : a(fa), b(fb), c(fc), d(fd) {}
D3DXINLINE D3DXPLANE::operator FLOAT *() { return &a; }
D3DXINLINE D3DXPLANE::operator const FLOAT *() const { return &a; }
D3DXINLINE D3DXPLANE &D3DXPLANE::operator*=(FLOAT f) { a *= f; b *= f; c *= f; d *= f; return *this; }
D3DXINLINE D3DXPLANE &D3DXPLANE::operator/=(FLOAT f) { FLOAT inv = 1.0f / f; a *= inv; b *= inv; c *= inv; d *= inv; return *this; }
D3DXINLINE D3DXPLANE D3DXPLANE::operator+() const { return *this; }
D3DXINLINE D3DXPLANE D3DXPLANE::operator-() const { return D3DXPLANE(-a, -b, -c, -d); }
D3DXINLINE D3DXPLANE D3DXPLANE::operator*(FLOAT f) const { return D3DXPLANE(a * f, b * f, c * f, d * f); }
D3DXINLINE D3DXPLANE D3DXPLANE::operator/(FLOAT f) const { FLOAT inv = 1.0f / f; return D3DXPLANE(a * inv, b * inv, c * inv, d * inv); }
D3DXINLINE D3DXPLANE operator*(FLOAT f, const D3DXPLANE &p) { return D3DXPLANE(f * p.a, f * p.b, f * p.c, f * p.d); }
D3DXINLINE BOOL D3DXPLANE::operator==(const D3DXPLANE &p) const { return a == p.a && b == p.b && c == p.c && d == p.d; }
D3DXINLINE BOOL D3DXPLANE::operator!=(const D3DXPLANE &p) const { return a != p.a || b != p.b || c != p.c || d != p.d; }

/*
 * D3DXCOLOR
 */
D3DXINLINE D3DXCOLOR::D3DXCOLOR(DWORD dw)
{
    const FLOAT f = 1.0f / 255.0f;
    r = f * (FLOAT)(unsigned char)(dw >> 16);
    g = f * (FLOAT)(unsigned char)(dw >> 8);
    b = f * (FLOAT)(unsigned char)(dw >> 0);
    a = f * (FLOAT)(unsigned char)(dw >> 24);
}
D3DXINLINE D3DXCOLOR::D3DXCOLOR(const FLOAT *f) : r(f[0]), g(f[1]), b(f[2]), a(f[3]) {}
D3DXINLINE D3DXCOLOR::D3DXCOLOR(const D3DCOLORVALUE &c) : r(c.r), g(c.g), b(c.b), a(c.a) {}
D3DXINLINE D3DXCOLOR::D3DXCOLOR(FLOAT fr, FLOAT fg, FLOAT fb, FLOAT fa) : r(fr), g(fg), b(fb), a(fa) {}
D3DXINLINE D3DXCOLOR::operator DWORD() const
{
    auto q = [](FLOAT v) -> DWORD {
        return v >= 1.0f ? 255u : (v <= 0.0f ? 0u : (DWORD)(v * 255.0f + 0.5f));
    };
    return (q(a) << 24) | (q(r) << 16) | (q(g) << 8) | q(b);
}
D3DXINLINE D3DXCOLOR::operator FLOAT *() { return &r; }
D3DXINLINE D3DXCOLOR::operator const FLOAT *() const { return &r; }
D3DXINLINE D3DXCOLOR::operator D3DCOLORVALUE *() { return (D3DCOLORVALUE *)&r; }
D3DXINLINE D3DXCOLOR::operator const D3DCOLORVALUE *() const { return (const D3DCOLORVALUE *)&r; }
D3DXINLINE D3DXCOLOR::operator D3DCOLORVALUE &() { return *((D3DCOLORVALUE *)&r); }
D3DXINLINE D3DXCOLOR::operator const D3DCOLORVALUE &() const { return *((const D3DCOLORVALUE *)&r); }
D3DXINLINE D3DXCOLOR &D3DXCOLOR::operator+=(const D3DXCOLOR &c) { r += c.r; g += c.g; b += c.b; a += c.a; return *this; }
D3DXINLINE D3DXCOLOR &D3DXCOLOR::operator-=(const D3DXCOLOR &c) { r -= c.r; g -= c.g; b -= c.b; a -= c.a; return *this; }
D3DXINLINE D3DXCOLOR &D3DXCOLOR::operator*=(FLOAT f) { r *= f; g *= f; b *= f; a *= f; return *this; }
D3DXINLINE D3DXCOLOR &D3DXCOLOR::operator/=(FLOAT f) { FLOAT inv = 1.0f / f; r *= inv; g *= inv; b *= inv; a *= inv; return *this; }
D3DXINLINE D3DXCOLOR D3DXCOLOR::operator+() const { return *this; }
D3DXINLINE D3DXCOLOR D3DXCOLOR::operator-() const { return D3DXCOLOR(-r, -g, -b, -a); }
D3DXINLINE D3DXCOLOR D3DXCOLOR::operator+(const D3DXCOLOR &c) const { return D3DXCOLOR(r + c.r, g + c.g, b + c.b, a + c.a); }
D3DXINLINE D3DXCOLOR D3DXCOLOR::operator-(const D3DXCOLOR &c) const { return D3DXCOLOR(r - c.r, g - c.g, b - c.b, a - c.a); }
D3DXINLINE D3DXCOLOR D3DXCOLOR::operator*(FLOAT f) const { return D3DXCOLOR(r * f, g * f, b * f, a * f); }
D3DXINLINE D3DXCOLOR D3DXCOLOR::operator/(FLOAT f) const { FLOAT inv = 1.0f / f; return D3DXCOLOR(r * inv, g * inv, b * inv, a * inv); }
D3DXINLINE D3DXCOLOR operator*(FLOAT f, const D3DXCOLOR &c) { return D3DXCOLOR(f * c.r, f * c.g, f * c.b, f * c.a); }
D3DXINLINE BOOL D3DXCOLOR::operator==(const D3DXCOLOR &c) const { return r == c.r && g == c.g && b == c.b && a == c.a; }
D3DXINLINE BOOL D3DXCOLOR::operator!=(const D3DXCOLOR &c) const { return r != c.r || g != c.g || b != c.b || a != c.a; }

#endif /* __cplusplus */

/*
 * Inline functions
 */
#ifdef __cplusplus
extern "C" {
#endif

/* 2D vector */
D3DXINLINE FLOAT D3DXVec2Length(const D3DXVECTOR2 *v) { return sqrtf(v->x * v->x + v->y * v->y); }
D3DXINLINE FLOAT D3DXVec2LengthSq(const D3DXVECTOR2 *v) { return v->x * v->x + v->y * v->y; }
D3DXINLINE FLOAT D3DXVec2Dot(const D3DXVECTOR2 *a, const D3DXVECTOR2 *b) { return a->x * b->x + a->y * b->y; }
D3DXINLINE FLOAT D3DXVec2CCW(const D3DXVECTOR2 *a, const D3DXVECTOR2 *b) { return a->x * b->y - a->y * b->x; }
D3DXINLINE D3DXVECTOR2 *D3DXVec2Add(D3DXVECTOR2 *o, const D3DXVECTOR2 *a, const D3DXVECTOR2 *b)
{
    o->x = a->x + b->x; o->y = a->y + b->y;
    return o;
}
D3DXINLINE D3DXVECTOR2 *D3DXVec2Subtract(D3DXVECTOR2 *o, const D3DXVECTOR2 *a, const D3DXVECTOR2 *b)
{
    o->x = a->x - b->x; o->y = a->y - b->y;
    return o;
}
D3DXINLINE D3DXVECTOR2 *D3DXVec2Minimize(D3DXVECTOR2 *o, const D3DXVECTOR2 *a, const D3DXVECTOR2 *b)
{
    o->x = a->x < b->x ? a->x : b->x; o->y = a->y < b->y ? a->y : b->y;
    return o;
}
D3DXINLINE D3DXVECTOR2 *D3DXVec2Maximize(D3DXVECTOR2 *o, const D3DXVECTOR2 *a, const D3DXVECTOR2 *b)
{
    o->x = a->x > b->x ? a->x : b->x; o->y = a->y > b->y ? a->y : b->y;
    return o;
}
D3DXINLINE D3DXVECTOR2 *D3DXVec2Scale(D3DXVECTOR2 *o, const D3DXVECTOR2 *v, FLOAT s)
{
    o->x = v->x * s; o->y = v->y * s;
    return o;
}
D3DXINLINE D3DXVECTOR2 *D3DXVec2Lerp(D3DXVECTOR2 *o, const D3DXVECTOR2 *a, const D3DXVECTOR2 *b, FLOAT s)
{
    o->x = a->x + s * (b->x - a->x); o->y = a->y + s * (b->y - a->y);
    return o;
}

/* 3D vector */
D3DXINLINE FLOAT D3DXVec3Length(const D3DXVECTOR3 *v) { return sqrtf(v->x * v->x + v->y * v->y + v->z * v->z); }
D3DXINLINE FLOAT D3DXVec3LengthSq(const D3DXVECTOR3 *v) { return v->x * v->x + v->y * v->y + v->z * v->z; }
D3DXINLINE FLOAT D3DXVec3Dot(const D3DXVECTOR3 *a, const D3DXVECTOR3 *b) { return a->x * b->x + a->y * b->y + a->z * b->z; }
D3DXINLINE D3DXVECTOR3 *D3DXVec3Cross(D3DXVECTOR3 *o, const D3DXVECTOR3 *a, const D3DXVECTOR3 *b)
{
    D3DXVECTOR3 t;
    t.x = a->y * b->z - a->z * b->y;
    t.y = a->z * b->x - a->x * b->z;
    t.z = a->x * b->y - a->y * b->x;
    *o = t;
    return o;
}
D3DXINLINE D3DXVECTOR3 *D3DXVec3Add(D3DXVECTOR3 *o, const D3DXVECTOR3 *a, const D3DXVECTOR3 *b)
{
    o->x = a->x + b->x; o->y = a->y + b->y; o->z = a->z + b->z;
    return o;
}
D3DXINLINE D3DXVECTOR3 *D3DXVec3Subtract(D3DXVECTOR3 *o, const D3DXVECTOR3 *a, const D3DXVECTOR3 *b)
{
    o->x = a->x - b->x; o->y = a->y - b->y; o->z = a->z - b->z;
    return o;
}
D3DXINLINE D3DXVECTOR3 *D3DXVec3Minimize(D3DXVECTOR3 *o, const D3DXVECTOR3 *a, const D3DXVECTOR3 *b)
{
    o->x = a->x < b->x ? a->x : b->x; o->y = a->y < b->y ? a->y : b->y; o->z = a->z < b->z ? a->z : b->z;
    return o;
}
D3DXINLINE D3DXVECTOR3 *D3DXVec3Maximize(D3DXVECTOR3 *o, const D3DXVECTOR3 *a, const D3DXVECTOR3 *b)
{
    o->x = a->x > b->x ? a->x : b->x; o->y = a->y > b->y ? a->y : b->y; o->z = a->z > b->z ? a->z : b->z;
    return o;
}
D3DXINLINE D3DXVECTOR3 *D3DXVec3Scale(D3DXVECTOR3 *o, const D3DXVECTOR3 *v, FLOAT s)
{
    o->x = v->x * s; o->y = v->y * s; o->z = v->z * s;
    return o;
}
D3DXINLINE D3DXVECTOR3 *D3DXVec3Lerp(D3DXVECTOR3 *o, const D3DXVECTOR3 *a, const D3DXVECTOR3 *b, FLOAT s)
{
    o->x = a->x + s * (b->x - a->x); o->y = a->y + s * (b->y - a->y); o->z = a->z + s * (b->z - a->z);
    return o;
}

/* 4D vector */
D3DXINLINE FLOAT D3DXVec4Length(const D3DXVECTOR4 *v) { return sqrtf(v->x * v->x + v->y * v->y + v->z * v->z + v->w * v->w); }
D3DXINLINE FLOAT D3DXVec4LengthSq(const D3DXVECTOR4 *v) { return v->x * v->x + v->y * v->y + v->z * v->z + v->w * v->w; }
D3DXINLINE FLOAT D3DXVec4Dot(const D3DXVECTOR4 *a, const D3DXVECTOR4 *b)
{
    return a->x * b->x + a->y * b->y + a->z * b->z + a->w * b->w;
}
D3DXINLINE D3DXVECTOR4 *D3DXVec4Add(D3DXVECTOR4 *o, const D3DXVECTOR4 *a, const D3DXVECTOR4 *b)
{
    o->x = a->x + b->x; o->y = a->y + b->y; o->z = a->z + b->z; o->w = a->w + b->w;
    return o;
}
D3DXINLINE D3DXVECTOR4 *D3DXVec4Subtract(D3DXVECTOR4 *o, const D3DXVECTOR4 *a, const D3DXVECTOR4 *b)
{
    o->x = a->x - b->x; o->y = a->y - b->y; o->z = a->z - b->z; o->w = a->w - b->w;
    return o;
}
D3DXINLINE D3DXVECTOR4 *D3DXVec4Minimize(D3DXVECTOR4 *o, const D3DXVECTOR4 *a, const D3DXVECTOR4 *b)
{
    o->x = a->x < b->x ? a->x : b->x; o->y = a->y < b->y ? a->y : b->y;
    o->z = a->z < b->z ? a->z : b->z; o->w = a->w < b->w ? a->w : b->w;
    return o;
}
D3DXINLINE D3DXVECTOR4 *D3DXVec4Maximize(D3DXVECTOR4 *o, const D3DXVECTOR4 *a, const D3DXVECTOR4 *b)
{
    o->x = a->x > b->x ? a->x : b->x; o->y = a->y > b->y ? a->y : b->y;
    o->z = a->z > b->z ? a->z : b->z; o->w = a->w > b->w ? a->w : b->w;
    return o;
}
D3DXINLINE D3DXVECTOR4 *D3DXVec4Scale(D3DXVECTOR4 *o, const D3DXVECTOR4 *v, FLOAT s)
{
    o->x = v->x * s; o->y = v->y * s; o->z = v->z * s; o->w = v->w * s;
    return o;
}
D3DXINLINE D3DXVECTOR4 *D3DXVec4Lerp(D3DXVECTOR4 *o, const D3DXVECTOR4 *a, const D3DXVECTOR4 *b, FLOAT s)
{
    o->x = a->x + s * (b->x - a->x); o->y = a->y + s * (b->y - a->y);
    o->z = a->z + s * (b->z - a->z); o->w = a->w + s * (b->w - a->w);
    return o;
}

/* 4x4 matrix */
D3DXINLINE D3DXMATRIX *D3DXMatrixIdentity(D3DXMATRIX *o)
{
    o->m[0][1] = o->m[0][2] = o->m[0][3] = 0.0f;
    o->m[1][0] = o->m[1][2] = o->m[1][3] = 0.0f;
    o->m[2][0] = o->m[2][1] = o->m[2][3] = 0.0f;
    o->m[3][0] = o->m[3][1] = o->m[3][2] = 0.0f;
    o->m[0][0] = o->m[1][1] = o->m[2][2] = o->m[3][3] = 1.0f;
    return o;
}
D3DXINLINE BOOL D3DXMatrixIsIdentity(const D3DXMATRIX *m)
{
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (m->m[i][j] != (i == j ? 1.0f : 0.0f)) return FALSE;
    return TRUE;
}

/* Quaternion */
D3DXINLINE FLOAT D3DXQuaternionLength(const D3DXQUATERNION *q) { return sqrtf(q->x * q->x + q->y * q->y + q->z * q->z + q->w * q->w); }
D3DXINLINE FLOAT D3DXQuaternionLengthSq(const D3DXQUATERNION *q) { return q->x * q->x + q->y * q->y + q->z * q->z + q->w * q->w; }
D3DXINLINE FLOAT D3DXQuaternionDot(const D3DXQUATERNION *a, const D3DXQUATERNION *b)
{
    return a->x * b->x + a->y * b->y + a->z * b->z + a->w * b->w;
}
D3DXINLINE D3DXQUATERNION *D3DXQuaternionIdentity(D3DXQUATERNION *o)
{
    o->x = o->y = o->z = 0.0f;
    o->w = 1.0f;
    return o;
}
D3DXINLINE BOOL D3DXQuaternionIsIdentity(const D3DXQUATERNION *q) { return q->x == 0.0f && q->y == 0.0f && q->z == 0.0f && q->w == 1.0f; }
D3DXINLINE D3DXQUATERNION *D3DXQuaternionConjugate(D3DXQUATERNION *o, const D3DXQUATERNION *q)
{
    o->x = -q->x; o->y = -q->y; o->z = -q->z; o->w = q->w;
    return o;
}

/* Plane */
D3DXINLINE FLOAT D3DXPlaneDot(const D3DXPLANE *p, const D3DXVECTOR4 *v) { return p->a * v->x + p->b * v->y + p->c * v->z + p->d * v->w; }
D3DXINLINE FLOAT D3DXPlaneDotCoord(const D3DXPLANE *p, const D3DXVECTOR3 *v) { return p->a * v->x + p->b * v->y + p->c * v->z + p->d; }
D3DXINLINE FLOAT D3DXPlaneDotNormal(const D3DXPLANE *p, const D3DXVECTOR3 *v) { return p->a * v->x + p->b * v->y + p->c * v->z; }

/* Color */
D3DXINLINE D3DXCOLOR *D3DXColorNegative(D3DXCOLOR *o, const D3DXCOLOR *c)
{
    o->r = 1.0f - c->r; o->g = 1.0f - c->g; o->b = 1.0f - c->b; o->a = 1.0f - c->a;
    return o;
}
D3DXINLINE D3DXCOLOR *D3DXColorAdd(D3DXCOLOR *o, const D3DXCOLOR *a, const D3DXCOLOR *b)
{
    o->r = a->r + b->r; o->g = a->g + b->g; o->b = a->b + b->b; o->a = a->a + b->a;
    return o;
}
D3DXINLINE D3DXCOLOR *D3DXColorSubtract(D3DXCOLOR *o, const D3DXCOLOR *a, const D3DXCOLOR *b)
{
    o->r = a->r - b->r; o->g = a->g - b->g; o->b = a->b - b->b; o->a = a->a - b->a;
    return o;
}
D3DXINLINE D3DXCOLOR *D3DXColorScale(D3DXCOLOR *o, const D3DXCOLOR *c, FLOAT s)
{
    o->r = c->r * s; o->g = c->g * s; o->b = c->b * s; o->a = c->a * s;
    return o;
}
D3DXINLINE D3DXCOLOR *D3DXColorModulate(D3DXCOLOR *o, const D3DXCOLOR *a, const D3DXCOLOR *b)
{
    o->r = a->r * b->r; o->g = a->g * b->g; o->b = a->b * b->b; o->a = a->a * b->a;
    return o;
}
D3DXINLINE D3DXCOLOR *D3DXColorLerp(D3DXCOLOR *o, const D3DXCOLOR *a, const D3DXCOLOR *b, FLOAT s)
{
    o->r = a->r + s * (b->r - a->r); o->g = a->g + s * (b->g - a->g);
    o->b = a->b + s * (b->b - a->b); o->a = a->a + s * (b->a - a->a);
    return o;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __D3DX8MATH_INL__ */
