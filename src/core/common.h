/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Common definitions of the Direct3D 8 on WebGL2 library.
 */
#pragma once

#include "core/api_types.h"

#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
// Declarations of the WebGL extension entry points (before glcount.h turns GL calls into macros).
#include <webgl/webgl2_ext.h>
#include <emscripten/html5_webgl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "dxwebgl2/dxwebgl2.h"
#include "core/glcount.h"

namespace dxwebgl2 {

//------------------------------------------------------------------------------
// Global configuration (see dxWebGL2.h)
//------------------------------------------------------------------------------
struct Config
{
    std::string canvas = "#canvas";
    unsigned desktopWidth = 0;
    unsigned desktopHeight = 0;
    unsigned vsVersion = 0;
    unsigned psVersion = 0;
    int presentMode = DXWEBGL2_PRESENT_EXPLICIT;
    dxwebgl2_PlatformHooks hooks = {};
    bool debug = false;
    int debugForce = 0;	// diagnosis: 1 = no culling, 2 = no blending, 4 = no depth test, for every draw
    bool releaseTextureShadows = false;
    bool disableS3TC = false;
    int contextProxy = 0;
    int featureOverrides = 0;     ///< dxwebgl2_SetFeatureOverrides
    int reportSeconds = 0;
    int loseAfterFrames = 0;      ///< test aid: drop the context after this many presented frames
    int loseRestoreFrames = 120;  ///< ... and restore it this many frames later
};
Config &GetConfig();

//------------------------------------------------------------------------------
// Logging
//------------------------------------------------------------------------------
void Log(const char *fmt, ...);
/// Logs a call that is not (fully) supported. Each distinct message is only
/// reported once.
void LogUnsupported(const char *what);

#define DXWEBGL2_UNSUPPORTED(what) ::dxwebgl2::LogUnsupported(what)

//------------------------------------------------------------------------------
// Small helpers
//------------------------------------------------------------------------------
template <class T> inline T Min(T a, T b) { return a < b ? a : b; }
template <class T> inline T Max(T a, T b) { return a > b ? a : b; }
template <class T> inline T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline float DwordToFloat(DWORD v) { float f; memcpy(&f, &v, sizeof f); return f; }
inline DWORD FloatToDword(float f) { DWORD v; memcpy(&v, &f, sizeof v); return v; }

inline uint32_t NextPow2(uint32_t v) { uint32_t p = 1; while (p < v) p <<= 1; return p; }

/// UTF-16 string (for example a file name) to UTF-8. WCHAR holds UTF-16 code units whatever the size of wchar_t.
std::string WideToUtf8(const WCHAR *w);

/// FNV-1a hash of a byte range.
inline uint64_t HashBytes(const void *data, size_t size, uint64_t h = 1469598103934665603ull)
{
    const uint8_t *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

/// Row-major 4x4 matrix helpers working on D3DMATRIX (row-vector convention).
struct Mat4
{
    float m[16]; // row-major, m[row*4+col]
    static Mat4 Identity();
    static Mat4 FromD3D(const D3DMATRIX &d) { Mat4 r; memcpy(r.m, &d, sizeof r.m); return r; }
    Mat4 operator*(const Mat4 &o) const; // this applied first, then o (D3D order)
    Mat4 Transposed() const;
    bool Inverse(Mat4 &out) const;
};
void NormalMatrix3(const Mat4 &modelView, float out[9]);

} // namespace dxwebgl2
