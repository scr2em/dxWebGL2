/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Draw calls and clears: the translation of the D3D device state into what the backend draws with
 * (program key, pipeline state, uniform values, texture and sampler bindings, vertex and index input).
 */
#include "d3d8/d3d8_internal.h"
#include "core/program.h"
#include "d3d8/resources.h"
#include "core/shader.h"

namespace dxwebgl2 {

//------------------------------------------------------------------------------
// Enum mapping
//------------------------------------------------------------------------------
static CompareFunc MapCmp(DWORD f)
{
    switch (f)
    {
    case D3DCMP_NEVER: return CompareFunc::Never;
    case D3DCMP_LESS: return CompareFunc::Less;
    case D3DCMP_EQUAL: return CompareFunc::Equal;
    case D3DCMP_LESSEQUAL: return CompareFunc::LessEqual;
    case D3DCMP_GREATER: return CompareFunc::Greater;
    case D3DCMP_NOTEQUAL: return CompareFunc::NotEqual;
    case D3DCMP_GREATEREQUAL: return CompareFunc::GreaterEqual;
    default: return CompareFunc::Always;
    }
}

static BlendFactor MapBlend(DWORD b, bool isSrc)
{
    switch (b)
    {
    case D3DBLEND_ZERO: return BlendFactor::Zero;
    case D3DBLEND_ONE: return BlendFactor::One;
    case D3DBLEND_SRCCOLOR: return BlendFactor::SrcColor;
    case D3DBLEND_INVSRCCOLOR: return BlendFactor::InvSrcColor;
    case D3DBLEND_SRCALPHA: return BlendFactor::SrcAlpha;
    case D3DBLEND_INVSRCALPHA: return BlendFactor::InvSrcAlpha;
    case D3DBLEND_DESTALPHA: return BlendFactor::DstAlpha;
    case D3DBLEND_INVDESTALPHA: return BlendFactor::InvDstAlpha;
    case D3DBLEND_DESTCOLOR: return BlendFactor::DstColor;
    case D3DBLEND_INVDESTCOLOR: return BlendFactor::InvDstColor;
    case D3DBLEND_SRCALPHASAT: return BlendFactor::SrcAlphaSat;
    case D3DBLEND_BOTHSRCALPHA: return isSrc ? BlendFactor::SrcAlpha : BlendFactor::InvSrcAlpha;
    case D3DBLEND_BOTHINVSRCALPHA: return isSrc ? BlendFactor::InvSrcAlpha : BlendFactor::SrcAlpha;
    default: return isSrc ? BlendFactor::One : BlendFactor::Zero;
    }
}

static BlendOp MapBlendOp(DWORD op)
{
    switch (op)
    {
    case D3DBLENDOP_SUBTRACT: return BlendOp::Subtract;
    case D3DBLENDOP_REVSUBTRACT: return BlendOp::RevSubtract;
    case D3DBLENDOP_MIN: return BlendOp::Min;
    case D3DBLENDOP_MAX: return BlendOp::Max;
    default: return BlendOp::Add;
    }
}

static StencilOp MapStencilOp(DWORD op)
{
    switch (op)
    {
    case D3DSTENCILOP_ZERO: return StencilOp::Zero;
    case D3DSTENCILOP_REPLACE: return StencilOp::Replace;
    case D3DSTENCILOP_INCRSAT: return StencilOp::IncrSat;
    case D3DSTENCILOP_DECRSAT: return StencilOp::DecrSat;
    case D3DSTENCILOP_INVERT: return StencilOp::Invert;
    case D3DSTENCILOP_INCR: return StencilOp::IncrWrap;
    case D3DSTENCILOP_DECR: return StencilOp::DecrWrap;
    default: return StencilOp::Keep;
    }
}

static inline void ColorToVec4(DWORD c, float *out)
{
    out[0] = ((c >> 16) & 255) / 255.0f;
    out[1] = ((c >> 8) & 255) / 255.0f;
    out[2] = (c & 255) / 255.0f;
    out[3] = ((c >> 24) & 255) / 255.0f;
}

//------------------------------------------------------------------------------
// Pipeline state
//------------------------------------------------------------------------------
PipelineState Device::DerivePipeline() const
{
    PipelineState p;
    memset(&p, 0, sizeof p);
    const DWORD *rs = m_s.rs;
    const Surface *ds = static_cast<const Surface *>(m_curDS.get());
    const FormatInfo *dsInfo = ds ? GetFormatInfo(ds->Format()) : nullptr;

    p.depthTest = ds && rs[D3DRS_ZENABLE] != D3DZB_FALSE;
    p.depthFunc = MapCmp(rs[D3DRS_ZFUNC]);
    p.depthMask = ds && rs[D3DRS_ZWRITEENABLE];
    p.depthNear = m_s.viewport.MinZ;
    p.depthFar = m_s.viewport.MaxZ;

    p.blend = rs[D3DRS_ALPHABLENDENABLE] != 0;
    p.srcBlend = MapBlend(rs[D3DRS_SRCBLEND], true);
    p.dstBlend = MapBlend(rs[D3DRS_DESTBLEND], false);
    p.blendOp = MapBlendOp(rs[D3DRS_BLENDOP]);

    // See the winding discussion in device.h: the framebuffer is flipped, so
    // D3D's default (cull counter-clockwise) keeps GL's default front face.
    switch (rs[D3DRS_CULLMODE])
    {
    case D3DCULL_NONE: p.cull = false; p.frontFace = FrontFace::CCW; break;
    case D3DCULL_CW: p.cull = true; p.frontFace = FrontFace::CW; break;
    default: p.cull = true; p.frontFace = FrontFace::CCW; break;
    }

    p.stencilTest = dsInfo && dsInfo->stencil && rs[D3DRS_STENCILENABLE];
    p.stencilFunc = MapCmp(rs[D3DRS_STENCILFUNC]);
    p.stencilRef = (int32_t)(rs[D3DRS_STENCILREF] & 0xFF);
    p.stencilMask = rs[D3DRS_STENCILMASK] & 0xFF;
    p.stencilWriteMask = rs[D3DRS_STENCILWRITEMASK] & 0xFF;
    p.stencilFail = MapStencilOp(rs[D3DRS_STENCILFAIL]);
    p.stencilZFail = MapStencilOp(rs[D3DRS_STENCILZFAIL]);
    p.stencilPass = MapStencilOp(rs[D3DRS_STENCILPASS]);

    p.colorMask = (uint8_t)(rs[D3DRS_COLORWRITEENABLE] & 0xF);

    const int zbias = (int)rs[D3DRS_ZBIAS];
    p.polyOffset = zbias != 0;
    p.polyFactor = -(float)zbias;
    p.polyUnits = -(float)zbias * 2.0f;

    p.vp[0] = (int32_t)m_s.viewport.X;
    p.vp[1] = (int32_t)m_s.viewport.Y;
    p.vp[2] = (int32_t)m_s.viewport.Width;
    p.vp[3] = (int32_t)m_s.viewport.Height;
    p.scissor = false;
    p.sc[0] = p.sc[1] = p.sc[2] = p.sc[3] = 0;
    return p;
}

//------------------------------------------------------------------------------
// Program selection
//------------------------------------------------------------------------------
static bool LayoutHas(const VertexLayout *l, int reg)
{
    for (int i = 0; i < l->count; ++i) if (l->elems[i].reg == reg) return true;
    return false;
}

static TextureBase *AsTextureBase(IDirect3DBaseTexture8 *t)
{
    if (!t) return nullptr;
    switch (t->GetType())
    {
    case D3DRTYPE_TEXTURE: return static_cast<Texture2D *>(static_cast<IDirect3DTexture8 *>(t));
    case D3DRTYPE_CUBETEXTURE: return static_cast<CubeTexture *>(static_cast<IDirect3DCubeTexture8 *>(t));
    case D3DRTYPE_VOLUMETEXTURE: return static_cast<VolumeTexture *>(static_cast<IDirect3DVolumeTexture8 *>(t));
    default: return nullptr;
    }
}

static uint8_t TexTypeOf(TextureBase *tb)
{
    if (!tb || !tb->Valid()) return 0;
    return (uint8_t)tb->Kind();
}

void Device::BuildProgramKey(ProgramKey &key)
{
    memset(&key, 0, sizeof key);
    const DWORD *rs = m_s.rs;
    const DWORD vsh = m_s.vertexShader;
    VertexShaderObject *vso = (vsh & 1) ? FindVertexShader(vsh) : nullptr;
    const bool customVS = vso && vso->hasFunction;
    PixelShaderObject *pso = m_s.pixelShader ? FindPixelShader(m_s.pixelShader) : nullptr;

    key.vsHandle = customVS ? vsh : 0;
    key.psHandle = pso ? m_s.pixelShader : 0;

    const VertexLayout *layout = vso ? &vso->layout : LayoutFromFVF(vsh);
    if (!customVS)
    {
        key.rhw = !vso && (vsh & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
        key.hasNormal = LayoutHas(layout, D3DVSDE_NORMAL);
        key.hasDiffuse = LayoutHas(layout, D3DVSDE_DIFFUSE);
        key.hasSpecular = LayoutHas(layout, D3DVSDE_SPECULAR);
        key.hasPSize = LayoutHas(layout, D3DVSDE_PSIZE);
        key.lighting = rs[D3DRS_LIGHTING] && !key.rhw;
        key.specularEnable = rs[D3DRS_SPECULARENABLE] != 0;
        key.localViewer = rs[D3DRS_LOCALVIEWER] != 0;
        key.normalize = rs[D3DRS_NORMALIZENORMALS] != 0;
        key.colorVertex = rs[D3DRS_COLORVERTEX] != 0;
        key.srcDiffuse = (uint8_t)rs[D3DRS_DIFFUSEMATERIALSOURCE];
        key.srcSpecular = (uint8_t)rs[D3DRS_SPECULARMATERIALSOURCE];
        key.srcAmbient = (uint8_t)rs[D3DRS_AMBIENTMATERIALSOURCE];
        key.srcEmissive = (uint8_t)rs[D3DRS_EMISSIVEMATERIALSOURCE];
        if (key.lighting)
        {
            int n = 0;
            for (const LightState &ls : m_s.lights)
            {
                if (!ls.defined || !ls.enabled) continue;
                if (n >= MAX_SHADER_LIGHTS) break;
                key.lightType[n++] = ls.light.Type == D3DLIGHT_DIRECTIONAL ? 0 : (ls.light.Type == D3DLIGHT_POINT ? 1 : 2);
            }
            key.lightCount = (uint8_t)n;
        }
    }
    if (rs[D3DRS_FOGENABLE])
    {
        key.fogEnable = 1;
        key.fogVertexMode = (uint8_t)rs[D3DRS_FOGVERTEXMODE];
        key.fogTableMode = (uint8_t)rs[D3DRS_FOGTABLEMODE];
        key.rangeFog = rs[D3DRS_RANGEFOGENABLE] != 0;
        if (!key.fogVertexMode && !key.fogTableMode) key.fogEnable = 0;
        // Table fog wins over vertex fog.
        if (key.fogTableMode) key.fogVertexMode = 0;
    }
    key.alphaTest = rs[D3DRS_ALPHATESTENABLE] != 0;
    key.alphaFunc = (uint8_t)rs[D3DRS_ALPHAFUNC];
    key.clipMask = (uint8_t)(rs[D3DRS_CLIPPLANEENABLE] & 0x3F);
    key.points = m_drawingPoints ? 1 : 0;
    key.pointSprite = m_drawingPoints && rs[D3DRS_POINTSPRITEENABLE];
    key.pointScale = rs[D3DRS_POINTSCALEENABLE] != 0;
    key.flatShade = rs[D3DRS_SHADEMODE] == D3DSHADE_FLAT;
    if (!key.alphaTest) key.alphaFunc = 0;

    // Texture stages.
    int count = 0;
    if (pso)
    {
        uint32_t m = pso->textureStageMask;
        while (m) { ++count; m >>= 1; }
    }
    else
    {
        while (count < MAX_STAGES && m_s.tss[count][D3DTSS_COLOROP] != D3DTOP_DISABLE) ++count;
        key.specularEnable = key.specularEnable && true;
    }
    key.stageCount = (uint8_t)count;
    for (int s = 0; s < count; ++s)
    {
        const DWORD *t = m_s.tss[s];
        StageKey &st = key.stage[s];
        TextureBase *tb = AsTextureBase(m_s.textures[s].get());
        if (tb && tb->m_pool == D3DPOOL_SYSTEMMEM) tb->EnsureGpu();
        st.texType = TexTypeOf(tb);
        st.colorOp = (uint8_t)t[D3DTSS_COLOROP];
        st.alphaOp = (uint8_t)t[D3DTSS_ALPHAOP];
        st.colorArg0 = (uint8_t)t[D3DTSS_COLORARG0];
        st.colorArg1 = (uint8_t)t[D3DTSS_COLORARG1];
        st.colorArg2 = (uint8_t)t[D3DTSS_COLORARG2];
        st.alphaArg0 = (uint8_t)t[D3DTSS_ALPHAARG0];
        st.alphaArg1 = (uint8_t)t[D3DTSS_ALPHAARG1];
        st.alphaArg2 = (uint8_t)t[D3DTSS_ALPHAARG2];
        st.resultTemp = (t[D3DTSS_RESULTARG] & 0xF) == D3DTA_TEMP;
        const DWORD tci = t[D3DTSS_TEXCOORDINDEX];
        st.coordIndex = (uint8_t)Min<DWORD>(tci & 0xFFFF, customVS ? 3u : 7u);
        st.texGen = (uint8_t)((tci >> 16) & 3);
        const DWORD ttf = t[D3DTSS_TEXTURETRANSFORMFLAGS];
        st.xformCount = (uint8_t)Min<DWORD>(ttf & 0xFF, 4u);
        st.projected = (ttf & D3DTTFF_PROJECTED) != 0;
        st.lodBias = DwordToFloat(t[D3DTSS_MIPMAPLODBIAS]) != 0.0f;
        st.borderMask = (uint8_t)((t[D3DTSS_ADDRESSU] == D3DTADDRESS_BORDER ? 1 : 0) | (t[D3DTSS_ADDRESSV] == D3DTADDRESS_BORDER ? 2 : 0) |
                                  (t[D3DTSS_ADDRESSW] == D3DTADDRESS_BORDER && st.texType == 3 ? 4 : 0));
        const bool needsTexture = pso ? ((pso->textureStageMask >> s) & 1) : st.texType != 0;
        if (needsTexture && st.texType)
        {
            if (customVS) key.tcMask |= (uint8_t)(1u << st.coordIndex);
            else key.tcMask |= (uint8_t)(1u << s);
        }
        else if (pso && ((pso->textureStageMask >> s) & 1))
        {
            // Shader reads the coordinates of a stage without a texture (texcoord/texcrd).
            if (customVS) key.tcMask |= (uint8_t)(1u << st.coordIndex);
            else key.tcMask |= (uint8_t)(1u << s);
        }
    }
    if (pso) key.specularEnable = 0; // ps.1.x has no specular add stage
}

bool Device::SelectProgram()
{
    if (m_keyDirty || !m_curProgram || m_curProgramPoints != m_drawingPoints)
    {
        ProgramKey key;
        BuildProgramKey(key);
        std::string k(reinterpret_cast<const char *>(&key), sizeof key);
        auto it = m_programs.find(k);
        Program *prog;
        if (it != m_programs.end())
            prog = it->second;
        else
        {
            VertexShaderObject *vso = key.vsHandle ? FindVertexShader(key.vsHandle) : nullptr;
            PixelShaderObject *pso = key.psHandle ? FindPixelShader(key.psHandle) : nullptr;
            prog = m_backend->CreateProgram(key, vso, pso);
            m_programs[k] = prog;
        }
        m_curProgram = prog;
        m_curProgramPoints = m_drawingPoints;
        m_keyDirty = false;
    }
    return m_curProgram && m_curProgram->valid;
}

//------------------------------------------------------------------------------
// Uniforms
//------------------------------------------------------------------------------
void Device::UpdateDerivedMatrices()
{
    if (!m_derivedDirty) return;
    m_wv = m_s.world[0] * m_s.view;
    m_wvp = m_wv * m_s.proj;
    NormalMatrix3(m_wv, m_nm);
    m_derivedDirty = false;
}

/// The values of a uniform group, from the device state. The backend asks for a group when the program it draws with
/// has not received the group's current version (m_ver) yet.
void Device::WriteUniforms(UniformGroup g, const Program &program, UniformSink &out)
{
    const Program *p = &program;
    const ProgramKey &key = p->key;
    switch (g)
    {
    case G_XFORM:
    {
        UpdateDerivedMatrices();
        // wvp, wv, normal matrix (3 columns), world: the program reads a prefix of this block.
        float xf[15 * 4];
        memcpy(xf, m_wvp.m, 64);
        if (p->xfCount > 4)
        {
            memcpy(xf + 16, m_wv.m, 64);
            for (int c = 0; c < 3; ++c) { memcpy(xf + 32 + c * 4, m_nm + c * 3, 12); xf[32 + c * 4 + 3] = 0.0f; }
            memcpy(xf + 44, m_s.world[0].m, 64);
        }
        out.Vec4(U_XF, xf, p->xfCount);
        break;
    }
    case G_TEXMAT:
    {
        float tm[8 * 16];
        for (int i = 0; i < 8; ++i) memcpy(tm + i * 16, m_s.tex[i].m, sizeof(float) * 16);
        out.Mat4(U_TM, tm, 8);
        break;
    }
    case G_VIEWPORT:
    {
        const float vw = Max<float>(1.0f, (float)m_s.viewport.Width), vh = Max<float>(1.0f, (float)m_s.viewport.Height);
        const float v[8] = {1.0f / vw, 1.0f / vh, m_s.viewport.MinZ, m_s.viewport.MaxZ - m_s.viewport.MinZ,
                            (float)m_s.viewport.X, (float)m_s.viewport.Y, vw, vh};
        out.Vec4(U_VIEW, v, 2);
        break;
    }
    case G_CLIP:
        out.Vec4(U_CLIP, &m_s.clipPlanes[0][0], MAX_CLIP_PLANES);
        break;
    case G_LIGHTS:
    {
        if (!(key.lighting && key.lightCount)) break;
        float lt[8 * 7][4] = {};
        int n = 0;
        const Mat4 &v = m_s.view;
        for (const LightState &ls : m_s.lights)
        {
            if (!ls.defined || !ls.enabled) continue;
            if (n >= key.lightCount) break;
            const D3DLIGHT8 &l = ls.light;
            float (*o)[4] = lt + n * 7;
            // World -> view (row vectors).
            o[0][0] = l.Position.x * v.m[0] + l.Position.y * v.m[4] + l.Position.z * v.m[8] + v.m[12];
            o[0][1] = l.Position.x * v.m[1] + l.Position.y * v.m[5] + l.Position.z * v.m[9] + v.m[13];
            o[0][2] = l.Position.x * v.m[2] + l.Position.y * v.m[6] + l.Position.z * v.m[10] + v.m[14];
            float d[3] = {l.Direction.x * v.m[0] + l.Direction.y * v.m[4] + l.Direction.z * v.m[8],
                          l.Direction.x * v.m[1] + l.Direction.y * v.m[5] + l.Direction.z * v.m[9],
                          l.Direction.x * v.m[2] + l.Direction.y * v.m[6] + l.Direction.z * v.m[10]};
            float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len > 0) { d[0] /= len; d[1] /= len; d[2] /= len; }
            memcpy(o[1], d, sizeof d);
            o[2][0] = l.Diffuse.r; o[2][1] = l.Diffuse.g; o[2][2] = l.Diffuse.b;
            o[3][0] = l.Specular.r; o[3][1] = l.Specular.g; o[3][2] = l.Specular.b;
            o[4][0] = l.Ambient.r; o[4][1] = l.Ambient.g; o[4][2] = l.Ambient.b;
            o[5][0] = l.Attenuation0; o[5][1] = l.Attenuation1; o[5][2] = l.Attenuation2;
            o[5][3] = Min(l.Range, 3.0e38f);
            const float ct = std::cos(l.Theta * 0.5f), cp = std::cos(l.Phi * 0.5f);
            o[6][0] = ct; o[6][1] = cp; o[6][2] = l.Falloff;
            o[6][3] = (ct - cp) != 0.0f ? 1.0f / (ct - cp) : 1.0f;
            ++n;
        }
        // Only the lights the program was generated for are sent.
        out.Vec4(U_LT, &lt[0][0], key.lightCount * 7);
        break;
    }
    case G_MATERIAL:
    {
        const D3DMATERIAL8 &m = m_s.material;
        const float mat[5][4] = {{m.Emissive.r, m.Emissive.g, m.Emissive.b, m.Emissive.a},
                                 {m.Ambient.r, m.Ambient.g, m.Ambient.b, m.Ambient.a},
                                 {m.Diffuse.r, m.Diffuse.g, m.Diffuse.b, m.Diffuse.a},
                                 {m.Specular.r, m.Specular.g, m.Specular.b, m.Specular.a},
                                 {m.Power, 0, 0, 0}};
        out.Vec4(U_MAT, &mat[0][0], 5);
        break;
    }
    case G_AMBIENT:
    {
        float c[4];
        ColorToVec4(m_s.rs[D3DRS_AMBIENT], c);
        out.Vec4(U_AMBIENT, c, 1);
        break;
    }
    case G_FOG:
    {
        const DWORD *rs = m_s.rs;
        const float start = DwordToFloat(rs[D3DRS_FOGSTART]), end = DwordToFloat(rs[D3DRS_FOGEND]);
        const float range = end - start;
        float f[8] = {start, end, DwordToFloat(rs[D3DRS_FOGDENSITY]), range != 0.0f ? 1.0f / range : 1.0f, 0, 0, 0, 0};
        ColorToVec4(rs[D3DRS_FOGCOLOR], f + 4);
        out.Vec4(U_FOGP, f, 2);
        break;
    }
    case G_TFACTOR:
    {
        float c[4];
        ColorToVec4(m_s.rs[D3DRS_TEXTUREFACTOR], c);
        out.Vec4(U_TFACTOR, c, 1);
        break;
    }
    case G_ALPHAREF:
    {
        const float ref = (float)(m_s.rs[D3DRS_ALPHAREF] & 0xFF);
        out.Float(U_ALPHAREF, &ref, 1);
        break;
    }
    case G_POINT:
    {
        const DWORD *rs = m_s.rs;
        const float maxSize = Min(DwordToFloat(rs[D3DRS_POINTSIZE_MAX]), Caps().maxPointSize);
        const float pt[8] = {DwordToFloat(rs[D3DRS_POINTSIZE]), Min(DwordToFloat(rs[D3DRS_POINTSIZE_MIN]), maxSize), maxSize,
                             (float)m_s.viewport.Height,
                             DwordToFloat(rs[D3DRS_POINTSCALE_A]), DwordToFloat(rs[D3DRS_POINTSCALE_B]), DwordToFloat(rs[D3DRS_POINTSCALE_C]), 0};
        out.Vec4(U_PT, pt, 2);
        break;
    }
    case G_LOD:
    {
        float lod[8];
        for (int i = 0; i < 8; ++i) lod[i] = DwordToFloat(m_s.tss[i][D3DTSS_MIPMAPLODBIAS]);
        out.Float(U_LOD, lod, 8);
        break;
    }
    case G_BUMP:
    {
        if (p->Uses(U_BUMP))
        {
            float bump[8][4];
            for (int i = 0; i < 8; ++i)
                for (int k = 0; k < 4; ++k) bump[i][k] = DwordToFloat(m_s.tss[i][D3DTSS_BUMPENVMAT00 + k]);
            out.Vec4(U_BUMP, &bump[0][0], 8);
        }
        if (p->Uses(U_BUMPL))
        {
            float bl[8][2];
            for (int i = 0; i < 8; ++i)
            {
                bl[i][0] = DwordToFloat(m_s.tss[i][D3DTSS_BUMPENVLSCALE]);
                bl[i][1] = DwordToFloat(m_s.tss[i][D3DTSS_BUMPENVLOFFSET]);
            }
            out.Vec2(U_BUMPL, &bl[0][0], 8);
        }
        break;
    }
    case G_BORDER:
    {
        float b[8][4];
        for (int i = 0; i < 8; ++i) ColorToVec4(m_s.tss[i][D3DTSS_BORDERCOLOR], b[i]);
        out.Vec4(U_BORDER, &b[0][0], 8);
        break;
    }
    case G_VSC:
        out.Vec4(U_VSC, &m_s.vsConst[0][0], VS_CONSTANTS);
        break;
    case G_PSC:
        out.Vec4(U_PSC, &m_s.psConst[0][0], 8);
        break;
    case G_COUNT:
        break;
    }
}

//------------------------------------------------------------------------------
// Textures
//------------------------------------------------------------------------------
SamplerDesc Device::SamplerFor(int stage, TextureBase *tb) const
{
    const DWORD *t = m_s.tss[stage];
    DWORD minf = t[D3DTSS_MINFILTER], magf = t[D3DTSS_MAGFILTER], mipf = t[D3DTSS_MIPFILTER];
    const bool mipped = tb->m_levelCount > 1 && mipf != D3DTEXF_NONE;
    SamplerDesc k;
    memset(&k, 0, sizeof k);
    k.minFilter = (uint8_t)minf; k.magFilter = (uint8_t)magf; k.mipFilter = (uint8_t)(mipped ? mipf : D3DTEXF_NONE); k.mipped = mipped;
    k.addressU = (uint8_t)t[D3DTSS_ADDRESSU]; k.addressV = (uint8_t)t[D3DTSS_ADDRESSV]; k.addressW = (uint8_t)t[D3DTSS_ADDRESSW];
    const bool aniso = Caps().anisotropic && (minf == D3DTEXF_ANISOTROPIC || magf == D3DTEXF_ANISOTROPIC);
    k.maxAnisotropy = aniso ? (uint8_t)Clamp<DWORD>(t[D3DTSS_MAXANISOTROPY], 1, 16) : 1;
    return k;
}

/// The texture and sampler of every stage the program samples.
void Device::BindTextures(DrawCall &call)
{
    const ProgramKey &key = m_curProgram->key;
    // A texture that is also the current render target cannot be sampled (feedback loop): the stage gets the
    // backend's white texture instead.
    const Surface *rt = static_cast<const Surface *>(m_curRT.get());
    const TextureBase *feedback = rt && rt->GetKind() == Surface::Level ? rt->Texture() : nullptr;
    for (int s = 0; s < key.stageCount; ++s)
    {
        const StageKey &st = key.stage[s];
        if (!st.texType) continue;
        TextureBase *tb = AsTextureBase(m_s.textures[s].get());
        GpuTexture *gpu = tb && tb != feedback ? tb->Gpu() : nullptr;
        TextureBinding &b = call.textures[s];
        if (gpu)
        {
            if (m_samplerDirtyMask & (1u << s))
            {
                m_stageSampler[s] = m_backend->GetSampler(SamplerFor(s, tb));
                m_samplerDirtyMask &= ~(1u << s);
            }
            b.texture = gpu;
            b.sampler = m_stageSampler[s];
            b.baseLevel = (int32_t)Min<DWORD>(m_s.tss[s][D3DTSS_MAXMIPLEVEL], tb->m_levelCount - 1);
        }
        else
        {
            b.texture = nullptr;
            b.sampler = nullptr;
            b.baseLevel = 0;
        }
    }
}

//------------------------------------------------------------------------------
// Draw
//------------------------------------------------------------------------------
static bool PrimitiveInfo(D3DPRIMITIVETYPE type, UINT primCount, Topology &topology, UINT &vertexCount)
{
    switch (type)
    {
    case D3DPT_POINTLIST: topology = Topology::PointList; vertexCount = primCount; return true;
    case D3DPT_LINELIST: topology = Topology::LineList; vertexCount = primCount * 2; return true;
    case D3DPT_LINESTRIP: topology = Topology::LineStrip; vertexCount = primCount + 1; return true;
    case D3DPT_TRIANGLELIST: topology = Topology::TriangleList; vertexCount = primCount * 3; return true;
    case D3DPT_TRIANGLESTRIP: topology = Topology::TriangleStrip; vertexCount = primCount + 2; return true;
    case D3DPT_TRIANGLEFAN: topology = Topology::TriangleFan; vertexCount = primCount + 2; return true;
    default: return false;
    }
}

/// Diagnostics (-dxwebgl2-report): records the Direct3D 8 features of the current draw that are ignored
/// or emulated approximately. Only called while the report is on.
void Device::DiagCheckDrawState(Topology topology)
{
    const DWORD *rs = m_s.rs;
    const ProgramKey &key = m_curProgram->key;
    for (int i = 0; i < 8; ++i)
        if (rs[D3DRS_WRAP0 + i]) DXW_HIT(Unsupported, "rs WRAP%d (cylindrical texture wrapping) is ignored", i);
    if (rs[D3DRS_VERTEXBLEND] != D3DVBF_DISABLE) DXW_HIT(Unsupported, "rs VERTEXBLEND (vertex blending) is ignored");
    if (rs[D3DRS_INDEXEDVERTEXBLENDENABLE]) DXW_HIT(Unsupported, "rs INDEXEDVERTEXBLENDENABLE is ignored");
    if (DwordToFloat(rs[D3DRS_TWEENFACTOR]) != 0.0f) DXW_HIT(Unsupported, "rs TWEENFACTOR (vertex tweening) is ignored");
    if (rs[D3DRS_ZBIAS]) DXW_HIT(Approximated, "rs ZBIAS=%u emulated with polygon offset", (unsigned)rs[D3DRS_ZBIAS]);
    if (rs[D3DRS_LINEPATTERN]) DXW_HIT(Unsupported, "rs LINEPATTERN is ignored");
    if (rs[D3DRS_EDGEANTIALIAS]) DXW_HIT(Unsupported, "rs EDGEANTIALIAS is ignored");
    if (!rs[D3DRS_CLIPPING]) DXW_HIT(Approximated, "rs CLIPPING=FALSE (clipping is always on)");
    if (!rs[D3DRS_MULTISAMPLEANTIALIAS] && m_samples) DXW_HIT(Unsupported, "rs MULTISAMPLEANTIALIAS=FALSE on a multisampled target is ignored");
    if (rs[D3DRS_ZENABLE] != D3DZB_TRUE && rs[D3DRS_ZENABLE] != D3DZB_FALSE) DXW_HIT(Approximated, "rs ZENABLE=%u (w-buffering) uses z", (unsigned)rs[D3DRS_ZENABLE]);
    if (rs[D3DRS_FILLMODE] == D3DFILL_POINT && topology != Topology::PointList) DXW_HIT(Approximated, "rs FILLMODE=POINT");
    if (rs[D3DRS_BLENDOP] > D3DBLENDOP_MAX) DXW_HIT(Unsupported, "rs BLENDOP=%u", (unsigned)rs[D3DRS_BLENDOP]);
    if (rs[D3DRS_SRCBLEND] == D3DBLEND_BOTHSRCALPHA || rs[D3DRS_SRCBLEND] == D3DBLEND_BOTHINVSRCALPHA)
        DXW_HIT(Approximated, "rs SRCBLEND=BOTHSRCALPHA/BOTHINVSRCALPHA expanded to a blend pair");
    if (rs[D3DRS_FOGENABLE] && !rs[D3DRS_FOGVERTEXMODE] && !rs[D3DRS_FOGTABLEMODE]) DXW_HIT(Approximated, "fog enabled with no fog mode (ignored)");
    if (rs[D3DRS_FOGENABLE] && rs[D3DRS_FOGVERTEXMODE] && !rs[D3DRS_FOGTABLEMODE] && key.vsHandle)
        DXW_HIT(Approximated, "vertex fog with a vertex shader uses oFog");
    if (rs[D3DRS_SHADEMODE] == D3DSHADE_FLAT && !Caps().provokingVertex) DXW_HIT(Approximated, "flat shading without WEBGL_provoking_vertex");
    if (rs[D3DRS_SHADEMODE] != D3DSHADE_FLAT && rs[D3DRS_SHADEMODE] != D3DSHADE_GOURAUD) DXW_HIT(Unsupported, "rs SHADEMODE=%u", (unsigned)rs[D3DRS_SHADEMODE]);
    for (int s = 0; s < key.stageCount; ++s)
    {
        const StageKey &st = key.stage[s];
        const DWORD *t = m_s.tss[s];
        if (!m_curProgram->key.psHandle)
        {
            if (st.colorOp == D3DTOP_BUMPENVMAP || st.colorOp == D3DTOP_BUMPENVMAPLUMINANCE)
                DXW_HIT(Unsupported, "tss COLOROP=BUMPENVMAP[LUMINANCE] in the fixed-function pipeline");
            if (st.colorOp == D3DTOP_PREMODULATE || st.alphaOp == D3DTOP_PREMODULATE)
                DXW_HIT(Approximated, "tss OP=PREMODULATE (treated as SELECTARG1)");
        }
        if (t[D3DTSS_ADDRESSU] == D3DTADDRESS_MIRRORONCE || t[D3DTSS_ADDRESSV] == D3DTADDRESS_MIRRORONCE)
            DXW_HIT(Approximated, "tss ADDRESS=MIRRORONCE (treated as MIRROR)");
        if (t[D3DTSS_ADDRESSU] == D3DTADDRESS_BORDER || t[D3DTSS_ADDRESSV] == D3DTADDRESS_BORDER)
            DXW_HIT(Approximated, "tss ADDRESS=BORDER (hard cut, no filtering at the border)");
        if (t[D3DTSS_MAGFILTER] == D3DTEXF_FLATCUBIC || t[D3DTSS_MAGFILTER] == D3DTEXF_GAUSSIANCUBIC)
            DXW_HIT(Approximated, "tss MAGFILTER=CUBIC (treated as LINEAR)");
        if ((t[D3DTSS_TEXTURETRANSFORMFLAGS] & 0xFF) > 4) DXW_HIT(Unsupported, "tss TEXTURETRANSFORMFLAGS count %u", (unsigned)(t[D3DTSS_TEXTURETRANSFORMFLAGS] & 0xFF));
    }
    const DWORD vsh = m_s.vertexShader;
    if (!(vsh & 1) && vsh)
    {
        const DWORD pos = vsh & D3DFVF_POSITION_MASK;
        if (pos >= D3DFVF_XYZB1 && pos <= D3DFVF_XYZB5) DXW_HIT(Unsupported, "FVF with vertex blend weights (weights are skipped)");
        if (vsh & D3DFVF_LASTBETA_UBYTE4) DXW_HIT(Unsupported, "FVF LASTBETA_UBYTE4");
    }
}

bool Device::PrepareDraw(Topology topology)
{
    m_drawingPoints = topology == Topology::PointList;
    ++m_drawCounter;
    if (m_backend->HasContextEvent()) CheckContext();
    if (m_contextLost || m_needsReset) return false;
    if (GetConfig().debug && m_presentCounter % 120 == 0)
        Log("draw #%u: topology %u, fvf 0x%x, vs %u, alphablend %u, src %u dst %u, zenable %u, cull %u, tex0 %d, rt %ux%u", m_drawCounter, (unsigned)topology,
            (unsigned)m_s.vertexShader, (unsigned)m_s.vertexShader, m_s.rs[D3DRS_ALPHABLENDENABLE], m_s.rs[D3DRS_SRCBLEND], m_s.rs[D3DRS_DESTBLEND],
            m_s.rs[D3DRS_ZENABLE], m_s.rs[D3DRS_CULLMODE], m_s.textures[0] ? 1 : 0, m_rtWidth, m_rtHeight);
    if (GetConfig().debug && m_presentCounter % 120 == 0)
    {
        const Mat4 &w = m_s.world[0], &v = m_s.view, &p = m_s.proj;
        Log("  viewport %u,%u %ux%u z %.2f-%.2f", (unsigned)m_s.viewport.X, (unsigned)m_s.viewport.Y, (unsigned)m_s.viewport.Width, (unsigned)m_s.viewport.Height, m_s.viewport.MinZ, m_s.viewport.MaxZ);
        Log("  world %g %g %g %g / %g %g %g %g / %g %g %g %g / %g %g %g %g", w.m[0], w.m[1], w.m[2], w.m[3], w.m[4], w.m[5], w.m[6], w.m[7], w.m[8], w.m[9], w.m[10], w.m[11], w.m[12], w.m[13], w.m[14], w.m[15]);
        Log("  view  %g %g %g %g / %g %g %g %g / %g %g %g %g / %g %g %g %g", v.m[0], v.m[1], v.m[2], v.m[3], v.m[4], v.m[5], v.m[6], v.m[7], v.m[8], v.m[9], v.m[10], v.m[11], v.m[12], v.m[13], v.m[14], v.m[15]);
        Log("  lighting %u, alphatest %u func %u ref %u, colorwrite 0x%x, fog %u, stage0 colorop %u arg1 %u arg2 %u alphaop %u aarg1 %u aarg2 %u; stage1 colorop %u",
            m_s.rs[D3DRS_LIGHTING], m_s.rs[D3DRS_ALPHATESTENABLE], m_s.rs[D3DRS_ALPHAFUNC], m_s.rs[D3DRS_ALPHAREF], m_s.rs[D3DRS_COLORWRITEENABLE], m_s.rs[D3DRS_FOGENABLE],
            m_s.tss[0][D3DTSS_COLOROP], m_s.tss[0][D3DTSS_COLORARG1], m_s.tss[0][D3DTSS_COLORARG2], m_s.tss[0][D3DTSS_ALPHAOP], m_s.tss[0][D3DTSS_ALPHAARG1], m_s.tss[0][D3DTSS_ALPHAARG2], m_s.tss[1][D3DTSS_COLOROP]);
        Log("  proj  %g %g %g %g / %g %g %g %g / %g %g %g %g / %g %g %g %g", p.m[0], p.m[1], p.m[2], p.m[3], p.m[4], p.m[5], p.m[6], p.m[7], p.m[8], p.m[9], p.m[10], p.m[11], p.m[12], p.m[13], p.m[14], p.m[15]);
    }
    if (!SelectProgram()) return false;
    if (g_diagOn) DiagCheckDrawState(topology);
    return true;
}

bool Device::DrawCommon(D3DPRIMITIVETYPE type, UINT primCount, bool indexed, UINT start, UINT baseVertex,
                        const void *userVerts, UINT userStride, const void *userIndices, D3DFORMAT userIndexFmt, UINT userVertCount)
{
    Topology topology;
    UINT count;
    if (!primCount || !PrimitiveInfo(type, primCount, topology, count)) return false;
    if (!PrepareDraw(topology)) return false;
    ++g_d3d.draws;
    g_d3d.primitives += primCount;
    if (userVerts) ++g_d3d.drawsUP;
    else if (indexed) ++g_d3d.drawsIndexed;

    DrawCall c = {};
    c.topology = topology;
    const DWORD fill = m_s.rs[D3DRS_FILLMODE];
    c.fill = fill == D3DFILL_WIREFRAME ? FillMode::Wireframe : (fill == D3DFILL_POINT ? FillMode::Point : FillMode::Solid);
    c.primitiveCount = primCount;
    c.count = count;
    c.indexed = indexed;
    c.start = start;
    c.baseVertex = baseVertex;

    const PipelineState pipeline = DerivePipeline();
    c.pipeline = &pipeline;
    c.program = m_curProgram;
    c.uniformVersions = m_ver;
    c.uniforms = this;
    BindTextures(c);

    const DWORD vsh = m_s.vertexShader;
    VertexShaderObject *vso = (vsh & 1) ? FindVertexShader(vsh) : nullptr;
    c.layout = vso ? &vso->layout : LayoutFromFVF(vsh);
    for (int i = 0; i < MAX_STREAMS; ++i)
    {
        VertexBuffer *vb = static_cast<VertexBuffer *>(m_s.streams[i].vb.get());
        c.streams[i].buffer = vb ? &vb->m_storage : nullptr;
        c.streams[i].stride = m_s.streams[i].stride;
    }
    c.userVertices = userVerts;
    c.userStride = userStride;
    c.userVertexCount = userVertCount;
    if (indexed)
    {
        if (userVerts)
        {
            c.userIndices = userIndices;
            c.index32 = userIndexFmt == D3DFMT_INDEX32;
        }
        else
        {
            IndexBuffer *ib = static_cast<IndexBuffer *>(m_s.indices.get());
            c.indexBuffer = ib ? &ib->m_storage : nullptr;
            c.index32 = ib && ib->Format() == D3DFMT_INDEX32;
        }
    }
    c.debugTag = vsh;
    return m_backend->Draw(c);
}

HRESULT Device::DrawPrimitive(D3DPRIMITIVETYPE type, UINT start, UINT primCount)
{
    return DrawCommon(type, primCount, false, start, 0, nullptr, 0, nullptr, D3DFMT_UNKNOWN, 0) ? D3D_OK : D3DERR_INVALIDCALL;
}

HRESULT Device::DrawIndexedPrimitive(D3DPRIMITIVETYPE type, UINT, UINT, UINT startIndex, UINT primCount)
{
    return DrawCommon(type, primCount, true, startIndex, m_s.baseVertexIndex, nullptr, 0, nullptr, D3DFMT_UNKNOWN, 0)
               ? D3D_OK : D3DERR_INVALIDCALL;
}

HRESULT Device::DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT primCount, const void *verts, UINT stride)
{
    if (!verts || !stride) return D3DERR_INVALIDCALL;
    Topology topology; UINT count;
    if (!PrimitiveInfo(type, primCount, topology, count)) return D3DERR_INVALIDCALL;
    return DrawCommon(type, primCount, false, 0, 0, verts, stride, nullptr, D3DFMT_UNKNOWN, count) ? D3D_OK : D3DERR_INVALIDCALL;
}

HRESULT Device::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT minIndex, UINT numIndices, UINT primCount,
                                       const void *indices, D3DFORMAT fmt, const void *verts, UINT stride)
{
    if (!verts || !indices || !stride) return D3DERR_INVALIDCALL;
    return DrawCommon(type, primCount, true, 0, 0, verts, stride, indices, fmt, minIndex + numIndices) ? D3D_OK : D3DERR_INVALIDCALL;
}

HRESULT Device::ProcessVertices(UINT, UINT, UINT, IDirect3DVertexBuffer8 *, DWORD)
{
    DXWEBGL2_UNSUPPORTED("ProcessVertices");
    return D3DERR_INVALIDCALL;
}

//------------------------------------------------------------------------------
// Clear
//------------------------------------------------------------------------------
HRESULT Device::Clear(DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
    if (m_backend->HasContextEvent()) CheckContext();
    if (m_contextLost || m_needsReset) return D3DERR_DEVICELOST;
    if (count && !rects) return D3DERR_INVALIDCALL;
    ++g_d3d.clears;

    ClearCall c = {};
    c.color = (flags & D3DCLEAR_TARGET) != 0;
    ColorToVec4(color, c.rgba);
    const Surface *ds = static_cast<const Surface *>(m_curDS.get());
    c.depth = (flags & D3DCLEAR_ZBUFFER) && ds;
    c.z = Clamp(z, 0.0f, 1.0f);
    const FormatInfo *dsInfo = ds ? GetFormatInfo(ds->Format()) : nullptr;
    c.stencil = (flags & D3DCLEAR_STENCIL) && dsInfo && dsInfo->stencil;
    c.stencilValue = stencil;

    m_clearRects.clear();
    if (count)
    {
        for (DWORD i = 0; i < count; ++i)
        {
            const D3DRECT &r = rects[i];
            LONG x1 = Clamp<LONG>(r.x1, 0, m_rtWidth), x2 = Clamp<LONG>(r.x2, 0, m_rtWidth);
            LONG y1 = Clamp<LONG>(r.y1, 0, m_rtHeight), y2 = Clamp<LONG>(r.y2, 0, m_rtHeight);
            if (x2 > x1 && y2 > y1) m_clearRects.push_back({(int32_t)x1, (int32_t)y1, (int32_t)(x2 - x1), (int32_t)(y2 - y1)});
        }
    }
    else
        m_clearRects.push_back({(int32_t)m_s.viewport.X, (int32_t)m_s.viewport.Y, (int32_t)m_s.viewport.Width, (int32_t)m_s.viewport.Height});
    c.rects = m_clearRects.data();
    c.rectCount = (uint32_t)m_clearRects.size();
    m_backend->Clear(c);
    return D3D_OK;
}

} // namespace dxwebgl2
