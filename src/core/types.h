/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Types shared by the core and the Direct3D front ends: limits, uniform groups and vertex layouts. What the front
 * ends exchange with a backend (pipeline state, draws, GPU objects) is in core/backend.h.
 */
#pragma once

#include "core/common.h"
#include "core/diag.h"
#include "core/format.h"

namespace dxwebgl2 {

class Program;
class VertexShaderObject;
class PixelShaderObject;

enum
{
    MAX_STAGES = 8,
    MAX_STREAMS = 16,
    MAX_SHADER_LIGHTS = 8,
    MAX_CLIP_PLANES = 6,
    VS_CONSTANTS = 96,
    PS_CONSTANTS = 32,
    RS_COUNT = 256,
    TSS_COUNT = 33,
};

/// Groups of uniforms that change together. The device keeps one version counter per group and
/// every program remembers the version it last received, so a state change re-uploads only the
/// uniforms that depend on it, and only into the programs that use them.
enum UniformGroup
{
    G_XFORM,    ///< world/view/projection derived matrices (wvp, wv, world, normal matrix)
    G_TEXMAT,   ///< texture transforms
    G_VIEWPORT, ///< pixel size, viewport, depth range
    G_CLIP,     ///< user clip planes
    G_LIGHTS,   ///< light parameters (view space)
    G_MATERIAL,
    G_AMBIENT,
    G_FOG,
    G_TFACTOR,
    G_ALPHAREF,
    G_POINT,    ///< point size parameters
    G_LOD,      ///< mip LOD biases
    G_BUMP,     ///< bump environment matrices and luminance scale/offset
    G_BORDER,   ///< texture border colors
    G_VSC,      ///< vertex shader constants
    G_PSC,      ///< pixel shader constants
    G_COUNT
};

//------------------------------------------------------------------------------
// Vertex declarations
//------------------------------------------------------------------------------
struct VertexElement
{
    uint8_t reg;         ///< D3DVSDE_* register / attribute location
    uint8_t stream;
    uint16_t offset;
    uint8_t size;        ///< components
    uint8_t glType;      ///< component type: 0 = float, 1 = ubyte, 2 = short
    uint8_t normalized;
    uint8_t d3dColor;    ///< D3DCOLOR: swizzled to RGBA by the vertex shader
};

struct VertexLayout
{
    VertexElement elems[16];
    int count = 0;
    uint16_t stride[MAX_STREAMS] = {};
    uint32_t id = 0;      ///< unique id for caching
    DWORD fvf = 0;        ///< non-zero when built from an FVF
};

const VertexLayout *LayoutFromFVF(DWORD fvf);
UINT FVFVertexSize(DWORD fvf);
/// Process-wide unique id of a vertex layout (layouts are compared by id in program keys).
uint32_t NextVertexLayoutId();

} // namespace dxwebgl2
