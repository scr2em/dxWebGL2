/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * WGSL generation for the Direct3D 8 fixed-function pipeline (the WebGPU backend's programs). It makes the same
 * decisions as the GLSL generator (fixedfunction.cpp) and shares the texture stage cascade expressions with it; the
 * differences are syntax (no ternary operator, no swizzle assignment, explicit vector constructors) and the clip space
 * conventions described in core/wgsl.h.
 */
#include "core/wgsl.h"

#include <cctype>
#include <cstdarg>

namespace dxwebgl2 {

namespace {

void Add(std::string &s, const char *fmt, ...)
{
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    s += buf;
}

/// Struct member names and WGSL types of the uniforms (UniformId order).
struct UniformDecl { const char *name, *type; uint16_t size; };
const UniformDecl kUniforms[U_COUNT] = {
    {"xf", "array<vec4f, 15>", 15 * 16},
    {"tm", "array<mat4x4f, 8>", 8 * 64},
    {"view", "array<vec4f, 2>", 2 * 16},
    {"clip", "array<vec4f, 6>", 6 * 16},
    {"pt", "array<vec4f, 2>", 2 * 16},
    {"mtl", "array<vec4f, 5>", 5 * 16},
    {"lt", "array<vec4f, 56>", 56 * 16},
    {"ambient", "vec4f", 16},
    {"fogp", "array<vec4f, 2>", 2 * 16},
    {"tfactor", "vec4f", 16},
    {"alpharef", "vec4f", 16},
    {"lod", "array<vec4f, 2>", 2 * 16},   // 8 floats, packed
    {"bump", "array<vec4f, 8>", 8 * 16},
    {"bumpl", "array<vec4f, 8>", 8 * 16}, // vec2 per stage in .xy
    {"vsc", "array<vec4f, 96>", 96 * 16},
    {"psc", "array<vec4f, 8>", 8 * 16},
    {"border", "array<vec4f, 8>", 8 * 16},
};

/// The uniforms `code` reads (`u.<name>` as a whole identifier).
uint32_t ScanUniforms(const std::string &code)
{
    uint32_t used = 0;
    for (int i = 0; i < U_COUNT; ++i)
    {
        const std::string pat = std::string("u.") + kUniforms[i].name;
        for (size_t at = code.find(pat); at != std::string::npos; at = code.find(pat, at + 1))
        {
            const char before = at ? code[at - 1] : ' ';
            const char after = at + pat.size() < code.size() ? code[at + pat.size()] : ' ';
            if ((isalnum((unsigned char)before) || before == '_') || isalnum((unsigned char)after) || after == '_') continue;
            used |= 1u << i;
            break;
        }
    }
    return used;
}

const char *kComp = "xyzw";

/// The light parameter `field` (0 position, 1 direction, 2 diffuse, 3 specular, 4 ambient, 5 attenuation, 6 spot).
std::string Light(int light, int field)
{
    char b[32];
    snprintf(b, sizeof b, "u.lt[%d]", light * 7 + field);
    return b;
}

} // namespace

//------------------------------------------------------------------------------
// Pieces shared with the shader translators (shader_wgsl.cpp)
//------------------------------------------------------------------------------
std::string WgslInput(uint32_t signature, int reg)
{
    char b[64];
    switch (WgslInputKind(signature, reg))
    {
    case WGSL_IN_FLOAT: snprintf(b, sizeof b, "a.r%d", reg); break;
    case WGSL_IN_UINT: case WGSL_IN_SINT: snprintf(b, sizeof b, "vec4f(a.r%d)", reg); break;
    default: return "vec4f(0.0, 0.0, 0.0, 1.0)";
    }
    return b;
}

uint32_t WgslInputKindOf(const VertexElement &e)
{
    if (e.glType == 1 && !e.normalized) return WGSL_IN_UINT;
    if (e.glType == 2 && !e.normalized) return WGSL_IN_SINT;
    return WGSL_IN_FLOAT;
}

uint32_t WgslSignatureOf(const VertexLayout &layout)
{
    uint32_t sig = 0;
    for (int i = 0; i < layout.count; ++i)
    {
        const VertexElement &e = layout.elems[i];
        if (!WgslInputKind(sig, e.reg)) sig |= WgslInputKindOf(e) << (e.reg * 2);
    }
    return sig;
}

std::string WgslVertexInputs(uint32_t sig)
{
    std::string s = "struct A {\n    @builtin(vertex_index) vi: u32,\n";
    for (int r = 0; r < 16; ++r)
    {
        const uint32_t k = WgslInputKind(sig, r);
        if (k) Add(s, "    @location(%d) r%d: %s,\n", r, r, k == WGSL_IN_FLOAT ? "vec4f" : (k == WGSL_IN_UINT ? "vec4u" : "vec4i"));
    }
    s += "};\n";
    return s;
}

std::string WgslPointQuad(const ProgramKey &key)
{
    if (!key.points) return "    let pc = vec2f(0.0);\n";
    return "    psz = clamp(psz, u.pt[0].y, u.pt[0].z);\n"
           "    let pc = vec2f(f32(a.vi & 1u), f32(a.vi >> 1u));\n"
           "    o.pos.x += (pc.x * 2.0 - 1.0) * psz * u.view[0].x * o.pos.w;\n"
           "    o.pos.y -= (pc.y * 2.0 - 1.0) * psz * u.view[0].y * o.pos.w;\n";
}

std::string WgslTexCoord(const ProgramKey &key, int stage)
{
    if (key.pointSprite) return "vec4f(v.fog.zw, 0.0, 1.0)";
    char b[16];
    snprintf(b, sizeof b, "v.t%d", key.vsHandle ? key.stage[stage].coordIndex : stage);
    return b;
}

std::string WgslTextureFetch(const ProgramKey &key, int stage, const std::string &coord, const std::string &bias)
{
    const StageKey &st = key.stage[stage];
    char head[96];
    if (bias.empty()) snprintf(head, sizeof head, "textureSample(tx%d, sm%d, ", stage, stage);
    else snprintf(head, sizeof head, "textureSampleBias(tx%d, sm%d, ", stage, stage);
    std::string tex = head + coord + (bias.empty() ? "" : ", " + bias) + ")";
    if (!st.borderMask || st.texType == 2) return tex;
    char tail[64];
    snprintf(tail, sizeof tail, ", u.border[%d], %d)", stage, (int)st.borderMask);
    return std::string(st.texType == 1 ? "d3d_b2(" : "d3d_b3(") + coord + ", " + tex + tail;
}

std::string WgslLodBias(int stage)
{
    char b[32];
    snprintf(b, sizeof b, "u.lod[%d].%c", stage / 4, kComp[stage & 3]);
    return b;
}

std::string WgslTextureDeclarations(const ProgramKey &key, uint32_t stageMask)
{
    std::string s;
    for (int i = 0; i < MAX_STAGES; ++i)
    {
        const StageKey &st = key.stage[i];
        if (!(stageMask & (1u << i)) || !st.texType) continue;
        const char *type = st.texType == 1 ? "texture_2d<f32>" : (st.texType == 2 ? "texture_cube<f32>" : "texture_3d<f32>");
        Add(s, "@group(1) @binding(%d) var tx%d: %s;\n@group(1) @binding(%d) var sm%d: sampler;\n", i * 2, i, type, i * 2 + 1, i);
    }
    s += "fn d3d_b2(c: vec2f, t: vec4f, b: vec4f, m: i32) -> vec4f {\n"
         "    let o = ((m & 1) != 0 && (c.x < 0.0 || c.x > 1.0)) || ((m & 2) != 0 && (c.y < 0.0 || c.y > 1.0));\n"
         "    return select(t, b, o);\n}\n"
         "fn d3d_b3(c: vec3f, t: vec4f, b: vec4f, m: i32) -> vec4f {\n"
         "    let o = ((m & 1) != 0 && (c.x < 0.0 || c.x > 1.0)) || ((m & 2) != 0 && (c.y < 0.0 || c.y > 1.0)) ||\n"
         "            ((m & 4) != 0 && (c.z < 0.0 || c.z > 1.0));\n"
         "    return select(t, b, o);\n}\n";
    return s;
}

std::string WgslFragmentTail(const ProgramKey &key)
{
    std::string s;
    if (key.fogEnable)
    {
        switch (key.fogTableMode)
        {
        case 0: s += "    let ff = clamp(v.fog.x, 0.0, 1.0);\n"; break;
        case D3DFOG_LINEAR: s += "    let ff = clamp((u.fogp[0].y - v.fog.y) * u.fogp[0].w, 0.0, 1.0);\n"; break;
        case D3DFOG_EXP: s += "    let ff = clamp(exp(-u.fogp[0].z * v.fog.y), 0.0, 1.0);\n"; break;
        default: s += "    let ff = clamp(exp(-(u.fogp[0].z * v.fog.y) * (u.fogp[0].z * v.fog.y)), 0.0, 1.0);\n"; break;
        }
        s += "    col = vec4f(mix(u.fogp[1].xyz, col.rgb, ff), col.a);\n";
    }
    // Clip planes and the alpha test discard after the texture samples (which need uniform control flow).
    for (int i = 0; i < MAX_CLIP_PLANES; ++i)
        if (key.clipMask & (1u << i)) Add(s, "    if (v.%s.%c < 0.0) { discard; }\n", i < 4 ? "clipA" : "clipB", kComp[i & 3]);
    if (key.alphaTest)
    {
        s += "    let a8 = floor(col.a * 255.0 + 0.5);\n";
        const char *cond = nullptr;
        switch (key.alphaFunc)
        {
        case D3DCMP_NEVER: cond = "false"; break;
        case D3DCMP_LESS: cond = "a8 < u.alpharef.x"; break;
        case D3DCMP_EQUAL: cond = "a8 == u.alpharef.x"; break;
        case D3DCMP_LESSEQUAL: cond = "a8 <= u.alpharef.x"; break;
        case D3DCMP_GREATER: cond = "a8 > u.alpharef.x"; break;
        case D3DCMP_NOTEQUAL: cond = "a8 != u.alpharef.x"; break;
        case D3DCMP_GREATEREQUAL: cond = "a8 >= u.alpharef.x"; break;
        default: cond = nullptr; break; // ALWAYS
        }
        if (cond) Add(s, "    if (!(%s)) { discard; }\n", cond);
    }
    return s;
}

uint32_t WgslScanUniforms(const std::string &code) { return ScanUniforms(code); }

//------------------------------------------------------------------------------
// Uniform block and varyings
//------------------------------------------------------------------------------
namespace {

/// Elements of the arrays that depend on the program: the lights and texture transforms the fixed-function vertex
/// stage reads (the block is copied per draw, so it is kept as small as the program allows).
uint32_t ArrayLength(int u, const ProgramKey &key)
{
    if (u == U_LT && !key.vsHandle) return Max<uint32_t>(1, key.lightCount) * 7;
    if (u == U_TM && !key.vsHandle)
    {
        uint32_t n = 1;
        for (int i = 0; i < MAX_STAGES; ++i)
            if ((key.tcMask & (1u << i)) && key.stage[i].xformCount) n = (uint32_t)i + 1;
        return n;
    }
    return 0; // the full length
}

uint32_t MemberSize(int u, const ProgramKey &key)
{
    const uint32_t n = ArrayLength(u, key);
    if (!n) return kUniforms[u].size;
    return n * (u == U_TM ? 64 : 16);
}

} // namespace

WgslUniformLayout MakeWgslUniformLayout(uint32_t used, const ProgramKey &key)
{
    WgslUniformLayout l;
    l.used = used;
    uint32_t off = 0;
    for (int i = 0; i < U_COUNT; ++i)
    {
        if (used & (1u << i))
        {
            l.offset[i] = (uint16_t)off;
            l.size[i] = (uint16_t)MemberSize(i, key);
            off += l.size[i];
        }
        else
        {
            l.offset[i] = UINT16_MAX;
            l.size[i] = 0;
        }
    }
    l.blockSize = off ? off : 16;
    return l;
}

uint32_t WgslMaxUniformBlockSize()
{
    uint32_t n = 0;
    for (const UniformDecl &d : kUniforms) n += d.size;
    return n;
}

std::string WgslUniformDeclarations(uint32_t used, const ProgramKey &key)
{
    std::string s = "struct U {\n";
    for (int i = 0; i < U_COUNT; ++i)
    {
        if (!(used & (1u << i))) continue;
        const uint32_t n = ArrayLength(i, key);
        if (n) Add(s, "    %s: array<%s, %u>,\n", kUniforms[i].name, i == U_TM ? "mat4x4f" : "vec4f", n);
        else Add(s, "    %s: %s,\n", kUniforms[i].name, kUniforms[i].type);
    }
    if (!used) s += "    pad0: vec4f,\n";
    s += "};\n@group(0) @binding(0) var<uniform> u: U;\n";
    return s;
}

std::string WgslVaryingDeclarations(const ProgramKey &key)
{
    std::string s = "struct V {\n    @builtin(position) pos: vec4f,\n";
    const char *flat = key.flatShade ? " @interpolate(flat)" : "";
    Add(s, "    @location(0)%s c0: vec4f,\n    @location(1)%s c1: vec4f,\n", flat, flat);
    // x: vertex fog factor, y: fog distance, zw: point sprite coordinate.
    s += "    @location(2) fog: vec4f,\n";
    if (key.clipMask & 0x0F) s += "    @location(3) clipA: vec4f,\n";
    if (key.clipMask & 0x30) s += "    @location(4) clipB: vec4f,\n";
    for (int i = 0; i < MAX_STAGES; ++i)
        if (key.tcMask & (1u << i)) Add(s, "    @location(%d) t%d: vec4f,\n", 5 + i, i);
    s += "};\n";
    return s;
}

std::string WgslModule(const ProgramKey &key, uint32_t usedUniforms, const WgslStage &stage)
{
    return WgslUniformDeclarations(usedUniforms, key) + WgslVaryingDeclarations(key) + stage.code;
}

//------------------------------------------------------------------------------
// Vertex stage
//------------------------------------------------------------------------------
WgslStage GenerateFixedFunctionVertexWGSL(const ProgramKey &key, uint32_t sig)
{
    WgslStage out;
    std::string s;

    // Inputs (locations are the D3DVSDE register numbers).
    s += WgslVertexInputs(sig);

    s += "@vertex fn vs_main(a: A) -> V {\n    var o: V;\n";
    Add(s, "    let a_pos = %s;\n", WgslInput(sig, 0).c_str());
    if (key.hasNormal) Add(s, "    let a_norm = %s;\n", WgslInput(sig, 3).c_str());
    if (key.hasPSize) Add(s, "    let a_psize = %s;\n", WgslInput(sig, 4).c_str());
    if (key.hasDiffuse) Add(s, "    let vdiff = (%s).bgra;\n", WgslInput(sig, 5).c_str());
    else s += "    let vdiff = vec4f(1.0);\n";
    if (key.hasSpecular) Add(s, "    let vspec = (%s).bgra;\n", WgslInput(sig, 6).c_str());
    else s += "    let vspec = vec4f(0.0);\n";
    uint32_t setsNeeded = 0;
    for (int i = 0; i < MAX_STAGES; ++i)
        if ((key.tcMask & (1u << i)) && key.stage[i].texGen == 0) setsNeeded |= 1u << (key.stage[i].coordIndex & 7);
    for (int i = 0; i < 8; ++i)
        if (setsNeeded & (1u << i)) Add(s, "    let a_t%d = %s;\n", i, WgslInput(sig, 7 + i).c_str());

    s += "    var vfog = 1.0;\n    var fz = 0.0;\n";
    if (key.rhw)
    {
        // Pre-transformed vertices: window coordinates (x, y, z in [0,1], 1/w).
        s += "    let rw = select(1.0, 1.0 / a_pos.w, a_pos.w != 0.0);\n";
        s += "    let nx = (a_pos.x + 0.5 - u.view[1].x) / u.view[1].z * 2.0 - 1.0;\n";
        s += "    let ny = 1.0 - (a_pos.y + 0.5 - u.view[1].y) / u.view[1].w * 2.0;\n";
        s += "    o.pos = vec4f(nx * rw, ny * rw, a_pos.z * rw, rw);\n";
        s += "    let eyePos = vec4f(0.0);\n    var eyeNormal = vec3f(0.0, 0.0, 1.0);\n";
        s += "    o.c0 = vdiff;\n    o.c1 = vspec;\n";
        if (key.fogVertexMode) s += "    vfog = vspec.a;\n";
        s += "    fz = a_pos.z;\n";
    }
    else
    {
        out.xfCount = 11;
        s += "    let pos4 = vec4f(a_pos.xyz, 1.0);\n";
        s += "    let oPos = mat4x4f(u.xf[0], u.xf[1], u.xf[2], u.xf[3]) * pos4;\n";
        // Direct3D 8 pixel centres are on integer coordinates: half a pixel right and down.
        s += "    o.pos = vec4f(oPos.x + oPos.w * u.view[0].x, oPos.y - oPos.w * u.view[0].y, oPos.z, oPos.w);\n";
        s += "    let eyePos = mat4x4f(u.xf[4], u.xf[5], u.xf[6], u.xf[7]) * pos4;\n";
        if (key.hasNormal)
        {
            s += "    var eyeNormal = mat3x3f(u.xf[8].xyz, u.xf[9].xyz, u.xf[10].xyz) * a_norm.xyz;\n";
            if (key.normalize) s += "    eyeNormal = normalize(eyeNormal);\n";
        }
        else
            s += "    var eyeNormal = vec3f(0.0);\n";

        if (!key.lighting)
            s += "    o.c0 = vdiff;\n    o.c1 = vspec;\n";
        else
        {
            // Material color sources (0 = material, 1 = vertex diffuse, 2 = vertex specular).
            auto src = [&](uint8_t sel, const char *material) -> std::string {
                if (!key.colorVertex || sel == 0) return material;
                return sel == 1 ? "vdiff" : "vspec";
            };
            Add(s, "    let mE = %s;\n    let mA = %s;\n    let mD = %s;\n    let mS = %s;\n",
                src(key.srcEmissive, "u.mtl[0]").c_str(), src(key.srcAmbient, "u.mtl[1]").c_str(),
                src(key.srcDiffuse, "u.mtl[2]").c_str(), src(key.srcSpecular, "u.mtl[3]").c_str());
            s += "    var lAmb = u.ambient.rgb;\n    var lDiff = vec3f(0.0);\n    var lSpec = vec3f(0.0);\n";
            if (key.lightCount)
            {
                if (key.specularEnable)
                    s += key.localViewer ? "    let viewDir = normalize(-eyePos.xyz);\n" : "    let viewDir = vec3f(0.0, 0.0, -1.0);\n";
                for (int i = 0; i < key.lightCount; ++i)
                {
                    const int type = key.lightType[i]; // 0 directional, 1 point, 2 spot
                    s += "    {\n        var att = 1.0;\n";
                    if (type == 0)
                        Add(s, "        let L = -%s.xyz;\n", Light(i, 1).c_str());
                    else
                    {
                        Add(s, "        let Lv = %s.xyz - eyePos.xyz;\n        let dist = length(Lv);\n", Light(i, 0).c_str());
                        s += "        let L = Lv / max(dist, 1e-6);\n";
                        const std::string at = Light(i, 5);
                        Add(s, "        att = select(1.0 / (%s.x + %s.y * dist + %s.z * dist * dist), 0.0, dist > %s.w);\n",
                            at.c_str(), at.c_str(), at.c_str(), at.c_str());
                        if (type == 2)
                        {
                            const std::string sp = Light(i, 6);
                            Add(s, "        let rho = dot(-L, %s.xyz);\n", Light(i, 1).c_str());
                            Add(s, "        let spot = select(select(pow((rho - %s.y) * %s.w, %s.z), 0.0, rho <= %s.y), 1.0, rho > %s.x);\n",
                                sp.c_str(), sp.c_str(), sp.c_str(), sp.c_str(), sp.c_str());
                            s += "        att = att * spot;\n";
                        }
                    }
                    Add(s, "        lAmb += att * %s.xyz;\n", Light(i, 4).c_str());
                    s += "        let ndl = max(dot(eyeNormal, L), 0.0);\n";
                    Add(s, "        lDiff += att * ndl * %s.xyz;\n", Light(i, 2).c_str());
                    if (key.specularEnable)
                    {
                        s += "        if (ndl > 0.0) {\n            let H = normalize(L + viewDir);\n";
                        s += "            let sp = select(1.0, pow(max(dot(eyeNormal, H), 0.0), u.mtl[4].x), u.mtl[4].x > 0.0);\n";
                        Add(s, "            lSpec += att * sp * %s.xyz;\n        }\n", Light(i, 3).c_str());
                    }
                    s += "    }\n";
                }
            }
            s += "    o.c0 = vec4f(saturate(mE.rgb + mA.rgb * lAmb + mD.rgb * lDiff), saturate(mD.a));\n";
            if (key.specularEnable) s += "    o.c1 = vec4f(saturate(mS.rgb * lSpec), 0.0);\n";
            else s += "    o.c1 = vec4f(0.0);\n";
        }

        // Fog factor (1 = unfogged).
        s += key.rangeFog ? "    fz = length(eyePos.xyz);\n" : "    fz = abs(eyePos.z);\n";
        switch (key.fogVertexMode)
        {
        case D3DFOG_LINEAR: s += "    vfog = clamp((u.fogp[0].y - fz) * u.fogp[0].w, 0.0, 1.0);\n"; break;
        case D3DFOG_EXP: s += "    vfog = clamp(exp(-u.fogp[0].z * fz), 0.0, 1.0);\n"; break;
        case D3DFOG_EXP2: s += "    vfog = clamp(exp(-(u.fogp[0].z * fz) * (u.fogp[0].z * fz)), 0.0, 1.0);\n"; break;
        default: break;
        }
    }

    // Texture coordinates.
    for (int i = 0; i < MAX_STAGES; ++i)
    {
        if (!(key.tcMask & (1u << i))) continue;
        const StageKey &st = key.stage[i];
        std::string expr;
        switch (st.texGen)
        {
        case 1: expr = "vec4f(eyeNormal, 1.0)"; break;
        case 2: expr = "vec4f(eyePos.xyz, 1.0)"; break;
        case 3: expr = "vec4f(reflect(normalize(eyePos.xyz), normalize(eyeNormal)), 1.0)"; break;
        default:
        {
            char b[32];
            snprintf(b, sizeof b, "a_t%d", st.coordIndex & 7);
            expr = b;
            break;
        }
        }
        if (st.xformCount) Add(s, "    o.t%d = u.tm[%d] * %s;\n", i, i, expr.c_str());
        else Add(s, "    o.t%d = %s;\n", i, expr.c_str());
    }

    // User clip planes (world space plane equations).
    for (int i = 0; i < MAX_CLIP_PLANES; ++i)
        if (key.clipMask & (1u << i))
        {
            const char *var = i < 4 ? "clipA" : "clipB";
            if (key.rhw) Add(s, "    o.%s.%c = 1.0;\n", var, kComp[i & 3]);
            else
            {
                out.xfCount = 15;
                Add(s, "    o.%s.%c = dot(u.clip[%d], mat4x4f(u.xf[11], u.xf[12], u.xf[13], u.xf[14]) * vec4f(a_pos.xyz, 1.0));\n",
                    var, kComp[i & 3], i);
            }
        }

    // Point lists: the corners of the point's quad (4 vertices of a strip per instance), the sprite coordinate.
    if (key.points)
    {
        if (key.hasPSize) s += "    var psz = a_psize.x;\n";
        else s += "    var psz = u.pt[0].x;\n";
        if (key.pointScale && !key.rhw)
        {
            s += "    let pd = length(eyePos.xyz);\n";
            s += "    psz = u.pt[0].w * psz * inverseSqrt(max(u.pt[1].x + u.pt[1].y * pd + u.pt[1].z * pd * pd, 1e-12));\n";
        }
    }
    s += WgslPointQuad(key);
    s += "    o.fog = vec4f(vfog, fz, pc);\n";
    s += "    return o;\n}\n";
    out.code = s;
    out.usedUniforms = ScanUniforms(s);
    return out;
}

//------------------------------------------------------------------------------
// Fragment stage
//------------------------------------------------------------------------------
WgslStage GenerateFixedFunctionFragmentWGSL(const ProgramKey &key)
{
    WgslStage out;
    std::string s;
    s += WgslTextureDeclarations(key, (1u << key.stageCount) - 1);
    s += "@fragment fn fs_main(v: V) -> @location(0) vec4f {\n";
    s += "    let diff = v.c0;\n    let spec = v.c1;\n    var cur = diff;\n    var tmp = vec4f(0.0);\n";
    for (int i = 0; i < key.stageCount; ++i)
    {
        const StageKey &st = key.stage[i];
        s += "    {\n";
        // Texture sample (unconditional: textureSample needs uniform control flow).
        const std::string tc = WgslTexCoord(key, i);
        if (st.texType)
        {
            std::string coord, bias;
            if (st.lodBias) bias = WgslLodBias(i);
            if (st.texType == 1)
            {
                if (st.projected && st.xformCount >= 2)
                {
                    const char comp = kComp[Clamp<int>(st.xformCount - 1, 1, 3)];
                    coord = "(" + tc + ".xy / " + tc + "." + comp + ")";
                }
                else coord = tc + ".xy";
            }
            else
            {
                if (st.projected && st.xformCount >= 3)
                {
                    const char comp = kComp[Clamp<int>(st.xformCount - 1, 2, 3)];
                    coord = "(" + tc + ".xyz / " + tc + "." + comp + ")";
                }
                else coord = tc + ".xyz";
            }
            Add(s, "        let tex = %s;\n", WgslTextureFetch(key, i, coord, bias).c_str());
        }
        else
            s += "        let tex = vec4f(1.0);\n";

        auto emitArg = [&](const char *name, uint8_t arg, bool colorPipe) {
            Add(s, "        let %s = %s;\n", name, StageArgExpr(arg, colorPipe, true).c_str());
        };
        emitArg("ca0", st.colorArg0, true); emitArg("ca1", st.colorArg1, true); emitArg("ca2", st.colorArg2, true);
        Add(s, "        let cres = %s;\n", StageOpExpr(st.colorOp, "ca0", "ca1", "ca2", true).c_str());
        if (st.alphaOp == D3DTOP_DISABLE)
            s += "        let ares = cur.a;\n";
        else
        {
            emitArg("aa0", st.alphaArg0, false); emitArg("aa1", st.alphaArg1, false); emitArg("aa2", st.alphaArg2, false);
            Add(s, "        let ares = (%s).a;\n", StageOpExpr(st.alphaOp, "aa0", "aa1", "aa2", true).c_str());
        }
        Add(s, "        %s = saturate(vec4f(cres.rgb, ares));\n", st.resultTemp ? "tmp" : "cur");
        s += "    }\n";
    }

    s += "    var col = cur;\n";
    if (key.specularEnable) s += "    col = vec4f(saturate(col.rgb + spec.rgb), col.a);\n";

    s += WgslFragmentTail(key);
    s += "    return col;\n}\n";
    out.code = s;
    out.usedUniforms = ScanUniforms(s);
    return out;
}

} // namespace dxwebgl2
