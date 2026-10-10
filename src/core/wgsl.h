/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * WGSL generation for the WebGPU backend: the fixed-function pipeline from a ProgramKey (the counterpart of the GLSL
 * generator in fixedfunction.cpp, with the same decisions), the uniform block layout, and the conventions both shader
 * stages of a program share. Text generation only: no GPU API in here (see docs/WEBGPU.md, section 4).
 *
 * Conventions of the generated code:
 *   - uniforms: one struct `U` at @group(0) @binding(0) (`u.xf`, `u.tm`, ...), holding only the uniforms the program
 *     uses, in UniformId order, every member 16-byte aligned (WgslUniformLayout), copied to a uniform ring per draw
 *     that changed it;
 *   - textures: stage s samples `tx<s>` (binding 2s) with `sm<s>` (binding 2s+1) of @group(1);
 *   - vertex inputs: @location(n) is the D3DVSDE_* register n; an input signature says which registers the vertex
 *     layout provides and with which component type (WGSL needs a buffer for every input and integer formats are
 *     read as integers), the others read (0, 0, 0, 1) like an unbound GL attribute;
 *   - clip space: WebGPU's framebuffer y points down and its depth range is [0, 1] like Direct3D's, so positions are
 *     not flipped; only the Direct3D 8 half pixel offset is applied;
 *   - point lists are drawn as instanced quads (4 vertices of a strip per point, the vertex attributes step per
 *     instance): the vertex stage offsets the corners by the point size and passes the sprite coordinate.
 */
#pragma once

#include "core/program.h"

namespace dxwebgl2 {

/// Input register kinds of an input signature (2 bits per D3DVSDE_* register).
enum WgslInput : uint32_t { WGSL_IN_NONE = 0, WGSL_IN_FLOAT = 1, WGSL_IN_UINT = 2, WGSL_IN_SINT = 3 };
inline uint32_t WgslInputKind(uint32_t signature, int reg) { return (signature >> (reg * 2)) & 3; }

/// Byte offsets of the uniforms of a program's block (UINT16_MAX: not part of it).
struct WgslUniformLayout
{
    uint16_t offset[U_COUNT];
    uint16_t size[U_COUNT];  ///< bytes of the member
    uint32_t used = 0;       ///< bit per UniformId
    uint32_t blockSize = 0;  ///< bytes of the struct
};
WgslUniformLayout MakeWgslUniformLayout(uint32_t usedUniforms, const ProgramKey &key);
/// Largest block a program can have (every uniform).
uint32_t WgslMaxUniformBlockSize();

/// One generated stage: the code without the uniform struct, and what it reads of the uniforms.
struct WgslStage
{
    std::string code;
    uint32_t usedUniforms = 0;  ///< bit per UniformId
    int xfCount = 4;            ///< vec4s of U_XF read (4: wvp, 11: + wv and the normal matrix, 15: + world)
};

/// The declaration of the uniform struct and its binding for the uniforms in `usedUniforms` (the light and texture
/// transform arrays of fixed-function programs have the length the key needs).
std::string WgslUniformDeclarations(uint32_t usedUniforms, const ProgramKey &key);
/// The varyings shared by the vertex and fragment stages of `key` (struct `V`).
std::string WgslVaryingDeclarations(const ProgramKey &key);

/// Vertex stage `vs_main` of the fixed-function pipeline for `key`, reading the inputs of `inputSignature`.
WgslStage GenerateFixedFunctionVertexWGSL(const ProgramKey &key, uint32_t inputSignature);
/// Fragment stage `fs_main`: the texture stage cascade, specular add, fog, clip planes and the alpha test.
WgslStage GenerateFixedFunctionFragmentWGSL(const ProgramKey &key);

/// A complete module: the uniform struct for `usedUniforms` (the union of both stages, so that they agree on the
/// layout), the varyings and the stage's code.
std::string WgslModule(const ProgramKey &key, uint32_t usedUniforms, const WgslStage &stage);

//-- Pieces shared by the fixed-function generator and the shader translators ---------------------------------------
/// Input register `reg` as a vec4f (`a.r<reg>`, converted from integers, or (0, 0, 0, 1) when not provided).
std::string WgslInput(uint32_t signature, int reg);
/// The input kind of a vertex element, and the input signature of a vertex layout.
uint32_t WgslInputKindOf(const VertexElement &e);
uint32_t WgslSignatureOf(const VertexLayout &layout);
/// The vertex input struct `A` of a signature (plus the vertex index).
std::string WgslVertexInputs(uint32_t signature);
/// Point lists: clamps `psz`, offsets the corner of the quad, defines the sprite coordinate `pc` (zero otherwise).
std::string WgslPointQuad(const ProgramKey &key);
/// Texture coordinates of stage `stage` in the fragment stage (a vec4f).
std::string WgslTexCoord(const ProgramKey &key, int stage);
/// Samples stage `stage` at `coord` (vec2f / vec3f expression), with the border colour emulation of the stage.
std::string WgslTextureFetch(const ProgramKey &key, int stage, const std::string &coord, const std::string &bias);
/// The mip LOD bias of a stage.
std::string WgslLodBias(int stage);
/// Texture and sampler bindings of the stages in `stageMask` (with a texture type), and the border helpers.
std::string WgslTextureDeclarations(const ProgramKey &key, uint32_t stageMask);
/// Fog, clip planes and the alpha test on `var col` (the caller returns it).
std::string WgslFragmentTail(const ProgramKey &key);
/// The uniforms generated code reads.
uint32_t WgslScanUniforms(const std::string &code);

} // namespace dxwebgl2
