/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Types shared by the core and the Direct3D front ends: limits, GL capabilities, vertex layouts and
 * the GL pipeline state.
 */
#pragma once

#include "core/common.h"
#include "core/diag.h"
#include "core/format.h"

namespace dxwebgl2 {

class GLState;
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
    UPLOAD_UNIT = 8, ///< texture unit used for resource uploads (stages use 0..7)
};

/// Base of everything that owns WebGL objects. The GL state keeps a list of them so that, when the
/// WebGL context is lost and a new one is created, every buffer, texture and renderbuffer can be
/// recreated from its system memory copy.
class GLObject
{
public:
    explicit GLObject(GLState *state);
    virtual ~GLObject();
    /// The context is gone: forget the GL names (the objects are invalid).
    virtual void OnContextLost() {}
    /// A new context is current: create the GL objects again and re-upload what is known.
    virtual void OnContextRestored() = 0;

private:
    friend class GLState;
    GLState *m_glDevice;
    GLObject *m_glPrev = nullptr, *m_glNext = nullptr;
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

/// Capabilities of the GL implementation.
struct GLCaps
{
    GLint maxTextureSize = 2048;
    GLint maxCubeSize = 2048;
    GLint max3DSize = 256;
    GLint maxVertexAttribs = 16;
    GLint maxVertexUniformVectors = 256;
    GLint maxTextureUnits = 16;
    float maxAnisotropy = 1.0f;
    float maxPointSize = 1.0f;
    GLint maxSamples = 1;
    GLint uniformAlignment = 256;
    bool s3tc = false;
    bool anisotropic = false;
    bool provokingVertex = false;   ///< WEBGL_provoking_vertex
    bool baseVertex = false;        ///< WEBGL_draw_instanced_base_vertex_base_instance
    std::string renderer = "WebGL2";
    std::string vendor = "WebGL";
    std::string version;
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
    uint8_t glType;      ///< 0 = float, 1 = ubyte, 2 = short
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

/// The GL pipeline state derived from the D3D render states.
struct PipelineState
{
    bool blend, depthTest, depthMask, cull, stencilTest, scissor, polyOffset;
    GLenum srcBlend, dstBlend, blendOp, depthFunc, frontFace, stencilFunc, stencilFail, stencilZFail, stencilPass;
    GLint stencilRef;
    GLuint stencilMask, stencilWriteMask;
    uint8_t colorMask; // bits r,g,b,a
    float depthNear, depthFar;
    float polyFactor, polyUnits;
    GLint vp[4];
    GLint sc[4];
    void Invalidate();
};

} // namespace dxwebgl2
