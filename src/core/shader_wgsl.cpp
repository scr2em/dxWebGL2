/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * vs.1.1 and ps.1.0-1.4 to WGSL (the WebGPU backend's counterpart of shader_vs.cpp / shader_ps.cpp).
 *
 * The decoding is shared with the GLSL translators: the token parameters (DecodeParam, OperandCount) and, for pixel
 * shaders, the decoded instruction list of the shader object (PixelShaderObject::Impl). The emission follows the GLSL
 * one instruction by instruction; the WGSL differences are syntax (no swizzle assignment: masked writes go component by
 * component, comparisons through select) and uniformity: textureSample must be in uniform control flow, so texkill
 * records the kill and discards at the end, and the texm3x3pad sequence is resolved while generating instead of at run
 * time.
 */
#include "core/shader_internal.h"
#include "core/wgsl.h"

#include <cstdarg>

namespace dxwebgl2 {

namespace {

std::string Fmt(const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    return buf;
}

/// `dst.<mask> = (expr).<mask>` without swizzle assignment.
std::string MaskedStore(const std::string &dst, uint8_t mask, const std::string &value)
{
    std::string s;
    for (int c = 0; c < 4; ++c)
        if (mask & (1u << c)) s += Fmt(" %s.%c = %s.%c;", dst.c_str(), "xyzw"[c], value.c_str(), "xyzw"[c]);
    return s;
}

//------------------------------------------------------------------------------
// Vertex shaders
//------------------------------------------------------------------------------
struct VSWgsl
{
    const VertexShaderObject &vs;
    std::string body;
    std::string error;
    int tmp = 0;

    explicit VSWgsl(const VertexShaderObject &o) : vs(o) {}

    std::string Base(const Param &p, int offset = 0)
    {
        switch (p.type)
        {
        case RT_TEMP: return Fmt("r%d", p.reg + offset);
        case RT_INPUT: return Fmt("v%d", p.reg + offset);
        case RT_CONST:
            if (p.relative) return Fmt("u.vsc[clamp(a0 + %d, 0, %d)]", p.reg + offset, VS_CONSTANTS - 1);
            return Fmt("u.vsc[%d]", Clamp(p.reg + offset, 0, VS_CONSTANTS - 1));
        default: error = "unsupported source register type"; return "vec4f(0.0)";
        }
    }

    std::string Src(const Param &p, int offset = 0)
    {
        std::string e = Base(p, offset);
        if (!IsIdentitySwizzle(p)) e += SwizzleString(p);
        if (p.srcMod == (D3DSPSM_NEG >> 24)) e = "(-" + e + ")";
        return e;
    }

    std::string DestName(const Param &p)
    {
        switch (p.type)
        {
        case RT_TEMP: return Fmt("r%d", p.reg);
        case RT_RASTOUT: return p.reg == 0 ? "oPos" : (p.reg == 1 ? "oFog" : "oPts");
        case RT_ATTROUT: return Fmt("oD%d", Clamp(p.reg, 0, 1));
        case RT_TEXCRDOUT: return Fmt("oT%d", Clamp(p.reg, 0, 3));
        default: error = "unsupported destination register type"; return "r0";
        }
    }

    void Assign(const Param &d, const std::string &expr)
    {
        if (d.type == RT_ADDR_TEX)
        {
            body += Fmt("    a0 = i32(floor((%s).x));\n", expr.c_str());
            return;
        }
        const std::string name = DestName(d);
        if (d.writeMask == 0xF) body += Fmt("    %s = %s;\n", name.c_str(), expr.c_str());
        else
        {
            const std::string e = Fmt("e%d", tmp++);
            body += Fmt("    { let %s = %s;%s }\n", e.c_str(), expr.c_str(), MaskedStore(name, d.writeMask, e).c_str());
        }
    }

    bool Run(const DWORD *code, size_t count)
    {
        size_t i = 1;
        while (i < count)
        {
            const DWORD tok = code[i++];
            const DWORD op = tok & 0xFFFF;
            if (op == D3DSIO_COMMENT) { i += (tok >> 16) & 0x7FFF; continue; }
            if (op == D3DSIO_END) break;
            if (op == D3DSIO_NOP) continue;
            if (op == D3DSIO_DEF) { i += 5; continue; } // the values are in the shader object (constants)
            const int n = OperandCount(op, false, 0x0101);
            if (n < 0 || i + n > count) { error = Fmt("unsupported vertex shader instruction %u", (unsigned)op); return false; }
            Param p[4];
            for (int k = 0; k < n; ++k) p[k] = DecodeParam(code[i + k]);
            i += n;
            if (!Instruction(op, p, n)) return false;
        }
        return error.empty();
    }

    bool Instruction(DWORD op, Param *p, int n)
    {
        const Param &d = p[0];
        std::string s0, s1, s2;
        if (n > 1) s0 = Src(p[1]);
        if (n > 2) s1 = Src(p[2]);
        if (n > 3) s2 = Src(p[3]);
        std::string e;
        switch (op)
        {
        case D3DSIO_MOV: e = s0; break;
        case D3DSIO_ADD: e = Fmt("%s + %s", s0.c_str(), s1.c_str()); break;
        case D3DSIO_SUB: e = Fmt("%s - %s", s0.c_str(), s1.c_str()); break;
        case D3DSIO_MUL: e = Fmt("%s * %s", s0.c_str(), s1.c_str()); break;
        case D3DSIO_MAD: e = Fmt("%s * %s + %s", s0.c_str(), s1.c_str(), s2.c_str()); break;
        case D3DSIO_RCP: e = Fmt("vec4f(d3d_rcp((%s).x))", s0.c_str()); break;
        case D3DSIO_RSQ: e = Fmt("vec4f(d3d_rsq((%s).x))", s0.c_str()); break;
        case D3DSIO_DP3: e = Fmt("vec4f(dot((%s).xyz, (%s).xyz))", s0.c_str(), s1.c_str()); break;
        case D3DSIO_DP4: e = Fmt("vec4f(dot(%s, %s))", s0.c_str(), s1.c_str()); break;
        case D3DSIO_MIN: e = Fmt("min(%s, %s)", s0.c_str(), s1.c_str()); break;
        case D3DSIO_MAX: e = Fmt("max(%s, %s)", s0.c_str(), s1.c_str()); break;
        case D3DSIO_SLT: e = Fmt("select(vec4f(0.0), vec4f(1.0), %s < %s)", s0.c_str(), s1.c_str()); break;
        case D3DSIO_SGE: e = Fmt("select(vec4f(0.0), vec4f(1.0), %s >= %s)", s0.c_str(), s1.c_str()); break;
        case D3DSIO_EXP: case D3DSIO_EXPP: e = Fmt("d3d_exp((%s).x)", s0.c_str()); break;
        case D3DSIO_LOG: case D3DSIO_LOGP: e = Fmt("d3d_log((%s).x)", s0.c_str()); break;
        case D3DSIO_LIT: e = Fmt("d3d_lit(%s)", s0.c_str()); break;
        case D3DSIO_DST: e = Fmt("vec4f(1.0, (%s).y * (%s).y, (%s).z, (%s).w)", s0.c_str(), s1.c_str(), s0.c_str(), s1.c_str()); break;
        case D3DSIO_FRC: e = Fmt("fract(%s)", s0.c_str()); break;
        case D3DSIO_M4x4: case D3DSIO_M4x3: case D3DSIO_M3x4: case D3DSIO_M3x3: case D3DSIO_M3x2:
        {
            const int rows = (op == D3DSIO_M4x4 || op == D3DSIO_M3x4) ? 4 : (op == D3DSIO_M4x3 || op == D3DSIO_M3x3 ? 3 : 2);
            const bool four = (op == D3DSIO_M4x4 || op == D3DSIO_M4x3);
            const std::string v = four ? s0 : "(" + s0 + ").xyz";
            std::string comp[4] = {"0.0", "0.0", "0.0", "1.0"};
            for (int r = 0; r < rows; ++r)
            {
                std::string row = Base(p[2], r);
                if (!four) row += ".xyz";
                comp[r] = Fmt("dot(%s, %s)", v.c_str(), row.c_str());
            }
            e = Fmt("vec4f(%s, %s, %s, %s)", comp[0].c_str(), comp[1].c_str(), comp[2].c_str(), comp[3].c_str());
            break;
        }
        default:
            error = Fmt("unsupported vertex shader instruction %s", OpcodeName(op));
            return false;
        }
        Assign(d, e);
        return true;
    }
};

const char *kVSHelpersWGSL =
    "fn d3d_rcp(x: f32) -> f32 { return select(1.0 / x, 3.402823e38, x == 0.0); }\n"
    "fn d3d_rsq(x: f32) -> f32 { let a = abs(x); return select(inverseSqrt(a), 3.402823e38, a == 0.0); }\n"
    "fn d3d_exp(x: f32) -> vec4f { let f = floor(x); return vec4f(exp2(f), x - f, exp2(x), 1.0); }\n"
    "fn d3d_log(x: f32) -> vec4f {\n"
    "    let a = abs(x); let l = select(log2(a), -3.402823e38, a == 0.0); let f = floor(l);\n"
    "    return vec4f(f, a / exp2(f), l, 1.0); }\n"
    "fn d3d_lit(s: vec4f) -> vec4f {\n"
    "    let d = max(s.x, 0.0);\n"
    "    let sp = select(0.0, pow(max(s.y, 0.0), clamp(s.w, -127.9961, 127.9961)), s.x > 0.0);\n"
    "    return vec4f(1.0, d, sp, 1.0); }\n";

//------------------------------------------------------------------------------
// Pixel shaders
//------------------------------------------------------------------------------
struct PSWgsl
{
    const PixelShaderObject::Impl &impl;
    const ProgramKey &key;
    std::string s;
    std::string error;
    std::string pendingEval, pendingStore;
    bool m3Used = false;
    bool usesDepth = false;
    bool usesKill = false;
    int m3Pads = 0;   ///< texm3x3pad instructions so far (the GLSL translator counts them at run time)
    int tmpCounter = 0;

    PSWgsl(const PixelShaderObject::Impl &i, const ProgramKey &k) : impl(i), key(k) {}

    bool is14() const { return impl.version >= 0x0104; }
    const char *Range() const { return is14() ? "8.0" : "1.0"; }

    std::string RegName(const Param &p) const
    {
        switch (p.type)
        {
        case RT_TEMP: return Fmt("r%d", p.reg);
        case RT_INPUT: return Fmt("v%d", p.reg);
        case RT_ADDR_TEX: return Fmt("t%d", p.reg);
        case RT_CONST:
            if (impl.defs.count(p.reg)) return Fmt("d%d", p.reg);
            return Fmt("u.psc[%d]", Clamp(p.reg, 0, 7));
        default: return "vec4f(0.0)";
        }
    }

    std::string Src(const Param &p) const
    {
        std::string e = RegName(p);
        if (!IsIdentitySwizzle(p)) e += SwizzleString(p);
        switch (p.srcMod << 24)
        {
        case D3DSPSM_NEG: return "(-" + e + ")";
        case D3DSPSM_BIAS: return "(" + e + " - 0.5)";
        case D3DSPSM_BIASNEG: return "(-(" + e + " - 0.5))";
        case D3DSPSM_SIGN: return "((" + e + " - 0.5) * 2.0)";
        case D3DSPSM_SIGNNEG: return "(-((" + e + " - 0.5) * 2.0))";
        case D3DSPSM_COMP: return "(1.0 - " + e + ")";
        case D3DSPSM_X2: return "(" + e + " * 2.0)";
        case D3DSPSM_X2NEG: return "(-(" + e + " * 2.0))";
        default: return e;
        }
    }

    std::string Coord(int stage) const
    {
        const std::string tc = WgslTexCoord(key, stage);
        const StageKey &st = key.stage[stage];
        if (st.projected && st.xformCount >= 2 && !key.pointSprite)
        {
            const char comp = "xyzw"[Clamp<int>(st.xformCount - 1, 1, 3)];
            return Fmt("vec4f(%s.xyz / %s.%c, 1.0)", tc.c_str(), tc.c_str(), comp);
        }
        return tc;
    }

    std::string Sample(int stage, const std::string &coord) const
    {
        const StageKey &st = key.stage[stage];
        if (!st.texType) return "vec4f(1.0)";
        const std::string bias = st.lodBias ? WgslLodBias(stage) : "";
        return WgslTextureFetch(key, stage, st.texType == 1 ? Fmt("(%s).xy", coord.c_str()) : Fmt("(%s).xyz", coord.c_str()), bias);
    }

    void Flush()
    {
        s += pendingEval;
        s += pendingStore;
        pendingEval.clear();
        pendingStore.clear();
    }

    /// Queues `dst = expr` with write mask / modifiers; the results of a co-issued pair are evaluated before either is
    /// stored.
    void Emit(const PSInst &in, const std::string &expr, bool applyRange = true)
    {
        const Param &d = in.p[0];
        const int id = tmpCounter++;
        std::string e = expr;
        if (d.shift > 0) e = Fmt("(%s) * %d.0", e.c_str(), 1 << d.shift);
        else if (d.shift < 0) e = Fmt("(%s) / %d.0", e.c_str(), 1 << -d.shift);
        if (d.saturate) e = "saturate(" + e + ")";
        else if (applyRange) e = Fmt("clamp(%s, vec4f(-%s), vec4f(%s))", e.c_str(), Range(), Range());
        const std::string name = RegName(d);
        if (!in.coissue) Flush();
        pendingEval += Fmt("    let e%d = %s;\n", id, e.c_str());
        if (d.writeMask == 0xF) pendingStore += Fmt("    %s = e%d;\n", name.c_str(), id);
        else pendingStore += "   " + MaskedStore(name, d.writeMask, Fmt("e%d", id)) + "\n";
    }

    bool Run()
    {
        for (const PSInst &in : impl.insts)
            if (!Instruction(in)) return false;
        Flush();
        return error.empty();
    }

    bool Instruction(const PSInst &in)
    {
        const Param *p = in.p;
        const DWORD op = in.op;
        auto src = [&](int i) { return Src(p[i]); };

        switch (op)
        {
        case D3DSIO_MOV: Emit(in, src(1)); return true;
        case D3DSIO_ADD: Emit(in, src(1) + " + " + src(2)); return true;
        case D3DSIO_SUB: Emit(in, src(1) + " - " + src(2)); return true;
        case D3DSIO_MUL: Emit(in, src(1) + " * " + src(2)); return true;
        case D3DSIO_MAD: Emit(in, src(1) + " * " + src(2) + " + " + src(3)); return true;
        case D3DSIO_LRP: Emit(in, Fmt("mix(%s, %s, %s)", src(3).c_str(), src(2).c_str(), src(1).c_str())); return true;
        case D3DSIO_DP3: Emit(in, Fmt("vec4f(dot((%s).xyz, (%s).xyz))", src(1).c_str(), src(2).c_str())); return true;
        case D3DSIO_DP4: Emit(in, Fmt("vec4f(dot(%s, %s))", src(1).c_str(), src(2).c_str())); return true;
        case D3DSIO_CND: Emit(in, Fmt("select(%s, %s, %s > vec4f(0.5))", src(3).c_str(), src(2).c_str(), src(1).c_str())); return true;
        case D3DSIO_CMP: Emit(in, Fmt("select(%s, %s, %s >= vec4f(0.0))", src(3).c_str(), src(2).c_str(), src(1).c_str())); return true;
        case D3DSIO_BEM:
        {
            const int stage = p[0].reg;
            const std::string a = src(1), b = src(2);
            Emit(in, Fmt("vec4f((%s).x + u.bump[%d].x * (%s).x + u.bump[%d].z * (%s).y, (%s).y + u.bump[%d].y * (%s).x + u.bump[%d].w * (%s).y, 0.0, 0.0)",
                         a.c_str(), stage, b.c_str(), stage, b.c_str(), a.c_str(), stage, b.c_str(), stage, b.c_str()));
            return true;
        }
        default: break;
        }

        // Texture addressing instructions.
        Flush();
        if (is14())
        {
            if (op == D3DSIO_TEX)
            {
                // texld rN, tM|rM: sampler N, coordinates from the source register.
                const int stage = p[0].reg;
                std::string coord = RegName(p[1]);
                if (!IsIdentitySwizzle(p[1])) coord += SwizzleString(p[1]);
                if (p[1].type == RT_ADDR_TEX) coord = Coord(p[1].reg);
                if ((p[1].srcMod << 24) == D3DSPSM_DZ) coord = Fmt("vec4f((%s).xy / (%s).z, 0.0, 1.0)", coord.c_str(), coord.c_str());
                else if ((p[1].srcMod << 24) == D3DSPSM_DW) coord = Fmt("vec4f((%s).xy / (%s).w, 0.0, 1.0)", coord.c_str(), coord.c_str());
                PSInst tmp = in;
                tmp.coissue = false;
                Emit(tmp, Sample(stage, coord), false);
                return true;
            }
            if (op == D3DSIO_TEXCOORD)
            {
                Emit(in, Fmt("vec4f((%s).xyz, 1.0)", Coord(p[1].reg).c_str()), false);
                return true;
            }
            if (op == D3DSIO_TEXDEPTH)
            {
                usesDepth = true;
                const std::string r = RegName(p[0]);
                s += Fmt("    fragDepth = select(1.0, %s.r / %s.g, %s.g != 0.0);\n", r.c_str(), r.c_str(), r.c_str());
                s += Fmt("    %s = vec4f(1.0);\n", r.c_str());
                return true;
            }
            error = Fmt("instruction %s is not valid in ps.1.4", OpcodeName(op));
            return false;
        }

        const int n = p[0].reg;
        const std::string t = Fmt("t%d", n);
        switch (op)
        {
        case D3DSIO_TEX:
            s += Fmt("    %s = %s;\n", t.c_str(), Sample(n, Coord(n)).c_str());
            return true;
        case D3DSIO_TEXCOORD:
            s += Fmt("    %s = vec4f(saturate((%s).xyz), 1.0);\n", t.c_str(), Coord(n).c_str());
            return true;
        case D3DSIO_TEXKILL:
            usesKill = true;
            s += Fmt("    killed = killed || any(%s.xyz < vec3f(0.0));\n", t.c_str());
            return true;
        case D3DSIO_TEXBEM: case D3DSIO_TEXBEML:
        {
            const std::string m = Fmt("t%d", p[1].reg);
            s += Fmt("    { let uv = (%s).xy + vec2f(u.bump[%d].x * %s.r + u.bump[%d].z * %s.g, u.bump[%d].y * %s.r + u.bump[%d].w * %s.g);\n",
                     Coord(n).c_str(), n, m.c_str(), n, m.c_str(), n, m.c_str(), n, m.c_str());
            s += Fmt("      %s = %s;\n", t.c_str(), Sample(n, "vec4f(uv, 0.0, 1.0)").c_str());
            if (op == D3DSIO_TEXBEML)
                s += Fmt("      %s = vec4f(%s.rgb * clamp(%s.b * u.bumpl[%d].x + u.bumpl[%d].y, 0.0, 1.0), %s.a); }\n", t.c_str(), t.c_str(),
                         m.c_str(), n, n, t.c_str());
            else
                s += "    }\n";
            return true;
        }
        case D3DSIO_TEXREG2AR:
            s += Fmt("    %s = %s;\n", t.c_str(), Sample(n, Fmt("vec4f(t%d.a, t%d.r, 0.0, 1.0)", p[1].reg, p[1].reg)).c_str());
            return true;
        case D3DSIO_TEXREG2GB:
            s += Fmt("    %s = %s;\n", t.c_str(), Sample(n, Fmt("vec4f(t%d.g, t%d.b, 0.0, 1.0)", p[1].reg, p[1].reg)).c_str());
            return true;
        case D3DSIO_TEXREG2RGB:
            s += Fmt("    %s = %s;\n", t.c_str(), Sample(n, Fmt("vec4f(t%d.rgb, 1.0)", p[1].reg)).c_str());
            return true;
        case D3DSIO_TEXM3x2PAD:
            m3Used = true;
            s += Fmt("    m3d0 = dot(%s.xyz, t%d.xyz);\n", Coord(n).c_str(), p[1].reg);
            return true;
        case D3DSIO_TEXM3x2TEX:
            m3Used = true;
            s += Fmt("    %s = %s;\n", t.c_str(), Sample(n, Fmt("vec4f(m3d0, dot(%s.xyz, t%d.xyz), 0.0, 1.0)", Coord(n).c_str(), p[1].reg)).c_str());
            return true;
        case D3DSIO_TEXM3x3PAD:
            m3Used = true;
            if (m3Pads++ == 0) s += Fmt("    m3d0 = dot(%s.xyz, t%d.xyz); m3w0 = %s.w;\n", Coord(n).c_str(), p[1].reg, Coord(n).c_str());
            else s += Fmt("    m3d1 = dot(%s.xyz, t%d.xyz); m3w1 = %s.w;\n", Coord(n).c_str(), p[1].reg, Coord(n).c_str());
            return true;
        case D3DSIO_TEXM3x3TEX:
            m3Used = true;
            s += Fmt("    %s = %s;\n", t.c_str(), Sample(n, Fmt("vec4f(m3d0, m3d1, dot(%s.xyz, t%d.xyz), 1.0)", Coord(n).c_str(), p[1].reg)).c_str());
            return true;
        case D3DSIO_TEXM3x3SPEC: case D3DSIO_TEXM3x3VSPEC:
        {
            m3Used = true;
            const std::string eye = (op == D3DSIO_TEXM3x3SPEC) ? Fmt("(%s).xyz", Src(p[2]).c_str()) : Fmt("vec3f(m3w0, m3w1, %s.w)", Coord(n).c_str());
            s += Fmt("    { let nn = vec3f(m3d0, m3d1, dot(%s.xyz, t%d.xyz)); let ee = %s;\n", Coord(n).c_str(), p[1].reg, eye.c_str());
            s += "      let rr = 2.0 * (dot(nn, ee) / max(dot(nn, nn), 1e-12)) * nn - ee;\n";
            s += Fmt("      %s = %s; }\n", t.c_str(), Sample(n, "vec4f(rr, 1.0)").c_str());
            return true;
        }
        case D3DSIO_TEXDP3:
            s += Fmt("    %s = vec4f(dot((%s).xyz, t%d.xyz));\n", t.c_str(), Coord(n).c_str(), p[1].reg);
            return true;
        case D3DSIO_TEXDP3TEX:
            s += Fmt("    %s = %s;\n", t.c_str(), Sample(n, Fmt("vec4f(dot((%s).xyz, t%d.xyz), 0.0, 0.0, 1.0)", Coord(n).c_str(), p[1].reg)).c_str());
            return true;
        case D3DSIO_TEXM3x3:
            m3Used = true;
            s += Fmt("    %s = vec4f(m3d0, m3d1, dot((%s).xyz, t%d.xyz), 1.0);\n", t.c_str(), Coord(n).c_str(), p[1].reg);
            return true;
        case D3DSIO_TEXM3x2DEPTH:
            m3Used = true;
            usesDepth = true;
            s += Fmt("    { let zw = dot((%s).xyz, t%d.xyz); fragDepth = select(1.0, m3d0 / zw, zw != 0.0); %s = vec4f(fragDepth); }\n",
                     Coord(n).c_str(), p[1].reg, t.c_str());
            return true;
        default:
            error = Fmt("unsupported pixel shader instruction %s", OpcodeName(op));
            return false;
        }
    }
};

} // namespace

WgslStage GenerateTranslatedVertexWGSL(const VertexShaderObject &vs, const ProgramKey &key, uint32_t sig)
{
    WgslStage out;
    VSWgsl tr(vs);
    if (!tr.Run(vs.bytecode.data(), vs.bytecode.size()))
    {
        Log("vertex shader translation to WGSL failed: %s", tr.error.c_str());
        DXW_HIT(Unsupported, "vertex shader (WGSL): %s", tr.error.c_str());
        return out;
    }
    std::string s = WgslVertexInputs(sig);
    s += kVSHelpersWGSL;
    s += "@vertex fn vs_main(a: A) -> V {\n    var o: V;\n";
    for (int i = 0; i < 16; ++i)
        if (vs.inputMask & (1u << i))
            s += Fmt("    let v%d = %s%s;\n", i, WgslInput(sig, i).c_str(), (vs.d3dColorMask & (1u << i)) ? ".bgra" : "");
    for (int i = 0; i < 12; ++i) s += Fmt("    var r%d = vec4f(0.0);\n", i);
    s += "    var a0 = 0;\n";
    s += "    var oPos = vec4f(0.0, 0.0, 0.0, 1.0);\n    var oD0 = vec4f(0.0);\n    var oD1 = vec4f(0.0);\n";
    s += "    var oFog = vec4f(1.0);\n    var oPts = vec4f(1.0);\n";
    for (int i = 0; i < 4; ++i) s += Fmt("    var oT%d = vec4f(0.0);\n", i);
    s += tr.body;
    // Direct3D 8 pixel centres: half a pixel right and down (WebGPU's y points down like Direct3D's, z is [0, 1]).
    s += "    o.pos = vec4f(oPos.x + oPos.w * u.view[0].x, oPos.y - oPos.w * u.view[0].y, oPos.z, oPos.w);\n";
    s += "    o.c0 = saturate(oD0);\n    o.c1 = saturate(oD1);\n";
    s += vs.writesFog ? "    let vfog = clamp(oFog.x, 0.0, 1.0);\n" : "    let vfog = 1.0;\n";
    for (int i = 0; i < 4; ++i)
        if (key.tcMask & (1u << i)) s += Fmt("    o.t%d = oT%d;\n", i, i);
    for (int i = 0; i < MAX_CLIP_PLANES; ++i)
        if (key.clipMask & (1u << i)) s += Fmt("    o.%s.%c = dot(u.clip[%d], oPos);\n", i < 4 ? "clipA" : "clipB", "xyzw"[i & 3], i);
    if (key.points) s += vs.writesPointSize ? "    var psz = oPts.x;\n" : "    var psz = u.pt[0].x;\n";
    s += WgslPointQuad(key);
    s += "    o.fog = vec4f(vfog, oPos.w, pc);\n    return o;\n}\n";
    out.code = s;
    out.usedUniforms = WgslScanUniforms(s);
    out.xfCount = 4;
    return out;
}

WgslStage GeneratePixelShaderWGSL(const PixelShaderObject &ps, const ProgramKey &key)
{
    WgslStage out;
    PSWgsl gen(*ps.impl, key);
    if (!gen.Run())
    {
        Log("pixel shader translation to WGSL failed: %s", gen.error.c_str());
        DXW_HIT(Unsupported, "pixel shader (WGSL): %s", gen.error.c_str());
        return out;
    }
    std::string s = WgslTextureDeclarations(key, ps.textureStageMask);
    if (gen.usesDepth)
    {
        s += "struct F { @location(0) color: vec4f, @builtin(frag_depth) depth: f32 };\n";
        s += "@fragment fn fs_main(v: V) -> F {\n";
    }
    else
        s += "@fragment fn fs_main(v: V) -> @location(0) vec4f {\n";
    for (const auto &d : ps.impl->defs)
        s += Fmt("    let d%d = vec4f(%.9g, %.9g, %.9g, %.9g);\n", d.first, d.second[0], d.second[1], d.second[2], d.second[3]);
    s += "    let v0 = v.c0;\n    let v1 = v.c1;\n";
    for (int i = 0; i < 6; ++i) s += Fmt("    var r%d = vec4f(0.0);\n", i);
    for (int i = 0; i < 6; ++i)
    {
        // Texture registers start out as the interpolated coordinates.
        if ((ps.textureStageMask & (1u << i)) && i < MAX_STAGES) s += Fmt("    var t%d = %s;\n", i, gen.Coord(i).c_str());
        else s += Fmt("    var t%d = vec4f(0.0);\n", i);
    }
    if (gen.m3Used) s += "    var m3d0 = 0.0;\n    var m3d1 = 0.0;\n    var m3w0 = 0.0;\n    var m3w1 = 0.0;\n";
    if (gen.usesDepth) s += "    var fragDepth = v.pos.z;\n";
    if (gen.usesKill) s += "    var killed = false;\n";
    s += gen.s;
    s += "    var col = saturate(r0);\n";
    if (gen.usesKill) s += "    if (killed) { discard; }\n";
    s += WgslFragmentTail(key);
    if (gen.usesDepth)
    {
        // The depth written by the shader is a Direct3D window depth in [0,1]; frag_depth is the value after the
        // viewport depth range.
        s += "    var f: F;\n    f.color = col;\n    f.depth = u.view[0].z + saturate(fragDepth) * u.view[0].w;\n    return f;\n}\n";
    }
    else
        s += "    return col;\n}\n";
    out.code = s;
    out.usedUniforms = WgslScanUniforms(s);
    return out;
}

} // namespace dxwebgl2
