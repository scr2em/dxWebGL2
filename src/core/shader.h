/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Direct3D 8 vertex/pixel shader objects.
 *
 * vs.1.1 and ps.1.0-1.4 token streams are parsed and translated to GLSL ES
 * 3.00 (shader_translate.cpp); text shaders are assembled to token streams by
 * D3DXAssembleShader (shader_assembler.cpp).
 */
#pragma once

#include "core/types.h"
#include "core/program.h"
#include "core/wgsl.h"

namespace dxwebgl2 {

class VertexShaderObject
{
public:
    VertexLayout layout;            ///< from the declaration
    bool hasFunction = false;       ///< false: declaration only, fixed-function processing
    std::string body;               ///< GLSL statements of main() computing oPos, oD0, oD1, oFog, oPts, oT[0..3]
    uint32_t inputMask = 0;         ///< v# registers read by the program
    uint32_t d3dColorMask = 0;      ///< registers declared as D3DCOLOR (need a BGRA swizzle)
    std::vector<DWORD> bytecode;
    std::vector<DWORD> declaration;
    float defConstants[VS_CONSTANTS][4]; ///< values of `def c#` and D3DVSD_CONST
    uint8_t defMask[VS_CONSTANTS];
    bool writesPointSize = false;
    bool writesFog = false;
    DWORD handle = 0;
};

class PixelShaderObject
{
public:
    std::vector<DWORD> bytecode;
    uint32_t version = 0;           ///< major<<8 | minor
    uint32_t textureStageMask = 0;  ///< stages sampled / whose coordinates are read
    uint32_t sampledMask = 0;
    struct Impl;
    Impl *impl = nullptr;
    ~PixelShaderObject();
    /// Generates the fragment shader for the sampler types in `key`.
    std::string Generate(const ProgramKey &key) const;
    DWORD handle = 0;
};

/// Creates a vertex shader object. `layout` and `declConstants` (c# preloads as (address, x, y, z, w) quintuples)
/// come from the front end's vertex declaration; `function` is the vs.1.x token stream (null: the shader only
/// carries a declaration and vertices are processed by the fixed-function pipeline).
bool CreateVertexShaderObject(const VertexLayout &layout, const std::vector<float> &declConstants,
                              const DWORD *function, VertexShaderObject **out);
bool CreatePixelShaderObject(const DWORD *function, PixelShaderObject **out);

/// Assembles vs.1.x / ps.1.x text to a token stream. Returns false on error
/// (message in `errors`).
bool AssembleShader(const char *source, size_t length, std::vector<DWORD> &out, std::string &errors);

/// Wraps the translated vertex shader body into a complete GLSL vertex shader.
std::string BuildTranslatedVertexShader(const VertexShaderObject &vs, const ProgramKey &key);

/// WGSL (WebGPU backend, core/wgsl.h conventions): the vertex stage of a vs.1.1 shader for an input signature, and the
/// fragment stage of a ps.1.x shader. The code is empty when the shader cannot be translated.
WgslStage GenerateTranslatedVertexWGSL(const VertexShaderObject &vs, const ProgramKey &key, uint32_t inputSignature);
WgslStage GeneratePixelShaderWGSL(const PixelShaderObject &ps, const ProgramKey &key);

} // namespace dxwebgl2
