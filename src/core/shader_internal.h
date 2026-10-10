/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Shared definitions of the shader assembler and the
 * bytecode -> GLSL translators.
 */
#pragma once

#include "core/shader.h"

#include <array>
#include <map>

namespace dxwebgl2 {

enum RegType
{
    RT_TEMP = 0,
    RT_INPUT = 1,
    RT_CONST = 2,
    RT_ADDR_TEX = 3, ///< a0 in vertex shaders, t# in pixel shaders
    RT_RASTOUT = 4,
    RT_ATTROUT = 5,
    RT_TEXCRDOUT = 6,
};

/// A decoded destination or source parameter token.
struct Param
{
    DWORD token = 0;
    int type = 0;
    int reg = 0;
    bool relative = false;
    // destination
    uint8_t writeMask = 0xF; ///< bit 0 = x ... bit 3 = w
    bool saturate = false;
    int shift = 0;           ///< result multiplier exponent: +1 = x2, +2 = x4, -1 = d2, ...
    // source
    uint8_t swizzle[4] = {0, 1, 2, 3};
    int srcMod = 0;          ///< D3DSPSM_* >> 24
};

inline Param DecodeParam(DWORD t)
{
    Param p;
    p.token = t;
    p.type = (int)((t & D3DSP_REGTYPE_MASK) >> D3DSP_REGTYPE_SHIFT);
    p.reg = (int)(t & D3DSP_REGNUM_MASK);
    p.relative = (t & D3DVS_ADDRESSMODE_MASK) != 0;
    p.writeMask = (uint8_t)((t >> 16) & 0xF);
    p.saturate = (t & D3DSPDM_SATURATE) != 0;
    int sh = (int)((t & D3DSP_DSTSHIFT_MASK) >> D3DSP_DSTSHIFT_SHIFT);
    p.shift = sh >= 8 ? sh - 16 : sh;
    for (int i = 0; i < 4; ++i) p.swizzle[i] = (uint8_t)((t >> (16 + 2 * i)) & 3);
    p.srcMod = (int)((t & D3DSP_SRCMOD_MASK) >> D3DSP_SRCMOD_SHIFT);
    return p;
}

inline bool IsIdentitySwizzle(const Param &p)
{
    return p.swizzle[0] == 0 && p.swizzle[1] == 1 && p.swizzle[2] == 2 && p.swizzle[3] == 3;
}

/// A decoded pixel shader instruction (the shared representation the GLSL and WGSL emitters generate code from).
struct PSInst
{
    DWORD op = 0;
    bool coissue = false;
    int n = 0;
    Param p[4];
};

struct PixelShaderObject::Impl
{
    uint32_t version = 0;
    std::vector<PSInst> insts;
    std::map<int, std::array<float, 4>> defs;
};

/// Number of parameter tokens (destination included) that follow an
/// instruction token. -1 for unknown opcodes.
int OperandCount(DWORD opcode, bool pixelShader, DWORD version);

const char *OpcodeName(DWORD opcode);

/// "xyzw" style swizzle suffix for GLSL, e.g. ".xxyz".
std::string SwizzleString(const Param &p);
std::string MaskString(uint8_t mask);

} // namespace dxwebgl2
