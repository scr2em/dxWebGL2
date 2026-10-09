/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Configuration, logging and matrix helpers.
 */
#include "core/common.h"
#include "core/diag.h"

#include <cstdarg>
#include <set>

namespace dxwebgl2 {

Config &GetConfig()
{
    static Config config;
    return config;
}

void Log(const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    fprintf(stderr, "[dxWebGL2] %s\n", buf);
}

void LogUnsupported(const char *what)
{
    if (g_diagOn) DiagHitf(Hit::Unsupported, "%s", what);
    static std::set<std::string> reported;
    if (reported.insert(what).second)
        Log("unsupported: %s", what);
}

/// UTF-16 string (for example a file name) to UTF-8. Independent of the size of wchar_t: WCHAR holds UTF-16 code units (a 32 bit
/// WCHAR, if a project defines it that way, is taken as UTF-32).
std::string WideToUtf8(const WCHAR *w)
{
    std::string s;
    if (!w) return s;
    for (; *w; ++w)
    {
        uint32_t c = sizeof(WCHAR) == 2 ? (uint32_t)(uint16_t)*w : (uint32_t)*w;
        if (sizeof(WCHAR) == 2 && c >= 0xD800 && c < 0xDC00 && (uint16_t)w[1] >= 0xDC00 && (uint16_t)w[1] < 0xE000)
        {
            c = 0x10000 + ((c - 0xD800) << 10) + ((uint32_t)(uint16_t)w[1] - 0xDC00);
            ++w;
        }
        if (c < 0x80) s.push_back((char)c);
        else if (c < 0x800) { s.push_back((char)(0xC0 | (c >> 6))); s.push_back((char)(0x80 | (c & 0x3F))); }
        else if (c < 0x10000)
        {
            s.push_back((char)(0xE0 | (c >> 12)));
            s.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            s.push_back((char)(0x80 | (c & 0x3F)));
        }
        else
        {
            s.push_back((char)(0xF0 | (c >> 18)));
            s.push_back((char)(0x80 | ((c >> 12) & 0x3F)));
            s.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            s.push_back((char)(0x80 | (c & 0x3F)));
        }
    }
    return s;
}

Mat4 Mat4::Identity()
{
    Mat4 r;
    memset(r.m, 0, sizeof r.m);
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 Mat4::operator*(const Mat4 &o) const
{
    Mat4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
        {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k)
                s += m[i * 4 + k] * o.m[k * 4 + j];
            r.m[i * 4 + j] = s;
        }
    return r;
}

Mat4 Mat4::Transposed() const
{
    Mat4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[j * 4 + i] = m[i * 4 + j];
    return r;
}

bool Mat4::Inverse(Mat4 &out) const
{
    // Gauss-Jordan elimination with partial pivoting.
    double a[4][8];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
        {
            a[i][j] = m[i * 4 + j];
            a[i][4 + j] = (i == j) ? 1.0 : 0.0;
        }
    for (int c = 0; c < 4; ++c)
    {
        int piv = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::fabs(a[r][c]) > std::fabs(a[piv][c])) piv = r;
        if (std::fabs(a[piv][c]) < 1e-30) return false;
        if (piv != c)
            for (int j = 0; j < 8; ++j) { double tmp_ = a[c][j]; a[c][j] = a[piv][j]; a[piv][j] = tmp_; }
        double d = 1.0 / a[c][c];
        for (int j = 0; j < 8; ++j) a[c][j] *= d;
        for (int r = 0; r < 4; ++r)
        {
            if (r == c) continue;
            double f = a[r][c];
            if (f == 0.0) continue;
            for (int j = 0; j < 8; ++j) a[r][j] -= f * a[c][j];
        }
    }
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            out.m[i * 4 + j] = (float)a[i][4 + j];
    return true;
}

/// Normal matrix of a row-major (row-vector) model-view matrix M, for upload
/// as a column-major GL mat3 that is applied as `normalMatrix * normal`.
/// Row vectors transform as n' = n * inverse(M3)^T, i.e. n'(column) =
/// inverse(M3) * n(column), so the GL matrix is inverse(M3) itself.
void NormalMatrix3(const Mat4 &mv, float out[9])
{
    double a = mv.m[0], b = mv.m[1], c = mv.m[2];
    double d = mv.m[4], e = mv.m[5], f = mv.m[6];
    double g = mv.m[8], h = mv.m[9], i = mv.m[10];
    double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::fabs(det) < 1e-30)
    {
        for (int k = 0; k < 9; ++k) out[k] = (k % 4 == 0) ? 1.0f : 0.0f;
        return;
    }
    double id = 1.0 / det;
    double inv[3][3] = {
        {(e * i - f * h) * id, (c * h - b * i) * id, (b * f - c * e) * id},
        {(f * g - d * i) * id, (a * i - c * g) * id, (c * d - a * f) * id},
        {(d * h - e * g) * id, (b * g - a * h) * id, (a * e - b * d) * id},
    };
    for (int r = 0; r < 3; ++r)
        for (int col = 0; col < 3; ++col)
            out[col * 3 + r] = (float)inv[r][col];
}

} // namespace dxwebgl2

extern "C" {

const char *dxwebgl2_GetVersionString(void) { return DXWEBGL2_VERSION_STRING; }

void dxwebgl2_SetCanvas(const char *selector)
{
    if (selector && *selector) dxwebgl2::GetConfig().canvas = selector;
}

void dxwebgl2_SetDesktopSize(unsigned width, unsigned height)
{
    dxwebgl2::GetConfig().desktopWidth = width;
    dxwebgl2::GetConfig().desktopHeight = height;
}

void dxwebgl2_SetShaderModel(unsigned vs, unsigned ps)
{
    dxwebgl2::GetConfig().vsVersion = vs;
    dxwebgl2::GetConfig().psVersion = ps;
}

void dxwebgl2_SetPlatformHooks(const dxwebgl2_PlatformHooks *hooks)
{
    dxwebgl2::GetConfig().hooks = hooks ? *hooks : dxwebgl2_PlatformHooks{};
}
void dxwebgl2_SetPresentMode(int mode) { dxwebgl2::GetConfig().presentMode = mode; }
void dxwebgl2_SetDisableS3TC(int disable) { dxwebgl2::GetConfig().disableS3TC = disable != 0; }
void dxwebgl2_SetContextProxy(int mode) { dxwebgl2::GetConfig().contextProxy = mode; }
void dxwebgl2_SetDebug(int flags)
{
    dxwebgl2::GetConfig().debug = (flags & 1) != 0;
    dxwebgl2::GetConfig().debugForce = flags >> 1;
}
void dxwebgl2_SetReleaseTextureShadows(int enable) { dxwebgl2::GetConfig().releaseTextureShadows = enable != 0; }

} // extern "C"
