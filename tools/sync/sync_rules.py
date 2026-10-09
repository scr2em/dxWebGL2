# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2026 dxWebGL2 contributors
#
# This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
# See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
"""The mechanical transformation from Dependencies/WebD3D8 (game port repository) to dxWebGL2.

`transform(raw)` maps {upstream relative path: text} to {dxWebGL2 relative path: text}.

It does exactly what was done by hand for the first import, so that it can be repeated on newer
upstream files and diffed against the older ones (see sync_from_game.py):

  * file mapping: the flat src/ directory becomes src/core (API independent WebGL2 core),
    src/api (shared COM plumbing) and src/d3d8 (the Direct3D 8 front end); a few files are
    split at marker lines (the markers are comment banners / declarations of the upstream code,
    they raise an error with the marker's name when upstream moved them);
  * namespace and symbol renames (webd3d8 -> dxwebgl2, WebD3D8_* -> dxwebgl2_*);
  * include paths;
  * the license header (SPDX, "dxWebGL2 contributors") instead of the game's;
  * wording rules that replace game specific remarks by generic ones.

Hand edits that were made on top of the imported files (for example the GLState base class of
Device) are not part of this transformation; the three-way merge keeps them.
"""
import re


class SyncError(Exception):
    pass


NS_NAME = "webd3d8"
NS_OPEN = "namespace %s {\n" % NS_NAME
NS_CLOSE = "} // namespace %s" % NS_NAME

SPDX_HEADER = """/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
"""

SPDX_HASH = """# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2026 dxWebGL2 contributors
#
# This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
# See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
"""

SPDX_JS = """// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copyright (C) 2026 dxWebGL2 contributors
//
// This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
// See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
"""

SPDX_HTML = """<!--
  SPDX-License-Identifier: GPL-3.0-or-later
  Copyright (C) 2026 dxWebGL2 contributors
  This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
  See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
-->
"""

# ----------------------------------------------------------------------------------------------
# Parsing of the upstream file layout
# ----------------------------------------------------------------------------------------------
LICENSE_RE = re.compile(r"\A/\*\n\*\*\tCommand & Conquer Generals Zero Hour\(tm\)\n.*?\*/\n", re.S)
DESC_RE = re.compile(r"\A/\*\n(\*\*.*?)\*/\n", re.S)


class Raw:
    """An upstream C++ file: description comment + body."""

    def __init__(self, name, text):
        self.name = name
        m = LICENSE_RE.match(text)
        if not m:
            raise SyncError("%s: the game's license header was not found at the top" % name)
        text = text[m.end():]
        self.desc = ""
        m = DESC_RE.match(text)
        if m:
            lines = []
            for l in m.group(1).split("\n"):
                if l.startswith("**"):
                    l = l[2:]
                    if l.startswith(" "):
                        l = l[1:]
                lines.append(l)
            desc = "\n".join(lines).strip("\n")
            desc = re.sub(r"^WebAssembly port: ?", "", desc)
            if re.match(r"[a-z]{3,}\b(?![.\d])", desc):
                desc = desc[:1].upper() + desc[1:]
            self.desc = desc
            text = text[m.end():]
        self.body = text

    # --- pieces ---------------------------------------------------------------------------
    def prologue(self):
        """Everything before the namespace opens (pragma once, includes)."""
        i = self.body.find(NS_OPEN)
        if i < 0:
            raise SyncError("%s: '%s' not found" % (self.name, NS_OPEN.strip()))
        return self.body[:i]

    def content(self):
        """Everything inside the namespace."""
        i = self.body.find(NS_OPEN)
        j = self.body.rfind(NS_CLOSE)
        if i < 0 or j < 0:
            raise SyncError("%s: namespace not found" % self.name)
        return self.body[i + len(NS_OPEN):j]

    def tail(self):
        """Everything after the namespace closes (extern "C" API functions...)."""
        j = self.body.rfind(NS_CLOSE)
        if j < 0:
            raise SyncError("%s: namespace not found" % self.name)
        k = self.body.find("\n", j)
        return self.body[k + 1:] if k >= 0 else ""


def find(text, pattern, what, start=0):
    m = re.compile(pattern, re.M).search(text, start)
    if not m:
        raise SyncError("marker not found: %s (pattern %r); update tools/sync/sync_rules.py" % (what, pattern))
    return m.start()


def between(text, start_pat, end_pat, what):
    a = find(text, start_pat, what + " (start)")
    b = len(text) if end_pat is None else find(text, end_pat, what + " (end)", a + 1)
    return text[a:b]


def banner(title):
    return r"^//-+\n// %s\n//-+\n" % re.escape(title)


def cut_function(text, signature_pat, what):
    """Removes a top-level function (from its signature line to the closing brace at column 0, plus the blank
    lines after it). Returns (text without the function, the function)."""
    a = find(text, signature_pat, what)
    m = re.compile(r"^}\n+", re.M).search(text, a)
    if not m:
        raise SyncError("end of function not found: %s" % what)
    return text[:a] + text[m.end():], text[a:m.end()]


def cut_out(text, start_pat, end_pat, what):
    """Returns (text without the region, the region)."""
    a = find(text, start_pat, what + " (start)")
    b = len(text) if end_pat is None else find(text, end_pat, what + " (end)", a + 1)
    return text[:a] + text[b:], text[a:b]


# ----------------------------------------------------------------------------------------------
# Output builders
# ----------------------------------------------------------------------------------------------
def comment_block(desc):
    out = ["/*"]
    for l in desc.split("\n"):
        out.append((" * " + l).rstrip())
    out.append(" */")
    return "\n".join(out) + "\n"


def cpp_file(desc, prologue, content, tail="", ns=True):
    s = SPDX_HEADER + comment_block(desc)
    pro = prologue.strip("\n")
    if pro:
        s += pro + "\n"
    if ns:
        s += "\n" + NS_OPEN + "\n" + content.strip("\n") + "\n\n" + NS_CLOSE + "\n"
    s += tail
    return s


# Header files that were divided keep their name in the includes of the upstream sources; this tells
# which new header an upstream include of a *core* file or of a *front end* file means.
INCLUDE_MAP_CORE = {
    "common.h": "core/common.h",
    "format.h": "core/format.h",
    "device.h": "core/types.h",
    "program.h": "core/program.h",
    "shader.h": "core/shader.h",
    "shader_internal.h": "core/shader_internal.h",
    "resources.h": "core/types.h",
    "glcount.h": "core/glcount.h",
    "diag.h": "core/diag.h",
}
INCLUDE_MAP_D3D8 = {
    "common.h": "core/common.h",
    "format.h": "core/format.h",
    "device.h": "d3d8/device.h",
    "program.h": "core/program.h",
    "shader.h": "core/shader.h",
    "shader_internal.h": "core/shader_internal.h",
    "resources.h": "d3d8/resources.h",
    "d3d8_headers.h": "d3d8/d3d8_headers.h",
    "d3d8_internal.h": "d3d8/d3d8_internal.h",
    "glcount.h": "core/glcount.h",
    "diag.h": "core/diag.h",
}


def map_includes(text, layer):
    table = INCLUDE_MAP_CORE if layer == "core" else INCLUDE_MAP_D3D8

    def sub(m):
        name = m.group(1)
        if name in table:
            return '#include "%s"' % table[name]
        return m.group(0)

    return re.sub(r'#include "([\w.]+)"', sub, text)


# Wording rules: game specific remarks become generic ones (the technical rationale is kept). Each
# rule must match at most once per file; the leftover check below reports anything game specific
# that no rule caught, so new upstream remarks do not slip in unnoticed.
WORDING = [
    (r"skinning happens on the CPU in WW3D2\.",
     "engines that skin on the CPU do not need the weights."),
    (r"WW3D2 then biases depth through the\n(\s*)// projection matrix, which behaves identically everywhere\.",
     r"applications then bias depth through the\n\1// projection matrix, which behaves identically everywhere."),
    (r"keep WW3D2 on its well-trodden fixed-function paths",
     "keep applications on their well-trodden fixed-function paths"),
    (r"as the game never\n/// asks for anything but the interface it already has",
     "as applications of this API never\n/// ask for anything but the interface they already have"),
    (r"The game's inline shaders use ",
     "Inline shaders in application sources commonly use "),
    (r"The cursor is drawn by the browser / the game \(RM_WINDOWS, RM_W3D, RM_POLYGON modes\)\.",
     "The cursor is drawn by the browser or by the application."),
    (r"which keeps the game on its\n/// fixed-function code paths",
     "which keeps applications on their\n/// fixed-function code paths"),
    (r"link the game with -sJSPI", "link the application with -sJSPI"),
    (r"Resolves the entry points the game looks up with GetProcAddress\(\) on\n/// \"D3D8\.DLL\"",
     "Resolves the entry points that an application looks up with GetProcAddress() on\n/// \"D3D8.DLL\""),
    (r"The\n/// Win32 compatibility layer's LoadLibrary\(\"D3D8\.DLL\"\) can be backed by this\.",
     "A\n/// Win32 compatibility layer's LoadLibrary(\"D3D8.DLL\") can be backed by this."),
    (r"configuration entry points of the Direct3D 8 on WebGL2\n\*\* implementation \(Dependencies/WebD3D8\)",
     "configuration entry points of the Direct3D 8 on WebGL2\n** implementation"),
    (r"let the platform layer steer", "let the host application steer"),
    (r"Optional callbacks into the platform layer, so that web_d3d8 does not\n/// depend on it at link level\.",
     "Optional callbacks into the host application, so that the library does not\n/// depend on it at link level."),
    (r"Called after CreateDevice\(\) and Reset\(\)", "Called after CreateDevice() and Reset()"),
    (r"the part of D3DXAssembleShader\n \* the game uses for its inline pixel shaders\)",
     "the part of D3DXAssembleShader\n * that applications use for inline shaders)"),
    (r"functions used by WW3D2 and\n \* W3DDevice \(texture creation", "functions (texture creation"),
    (r"The inline pixel shaders of the game \(W3DWater\.cpp, W3DProfilerFrameCapture\.cpp\), verbatim\.",
     "Representative inline pixel shaders (water, texture swizzling, trapezoid warping)."),
    (r"The game's own inline shaders", "The inline shaders"),
    (r"\bg_gameShaders\b", "g_inlineShaders"),
    (r"\bgame shader\b", "inline shader"),
    # --- context loss / diagnostics / feature test wording ---
    (r"\(WW3D2's DX8Wrapper::Reset_Device\)", "(its reset path)"),
    (r"; the game will reset the device when it is back", "; the application is expected to reset the device when it is back"),
    (r"\(WW3D::Begin_Render gives up\)", "(a render loop that gives up after a failed frame)"),
    (r"\(the game renders the\n(\s*)// water reflection into a small texture with the back buffer's depth buffer\)",
     r"(applications render\n\1// reflections into a small texture with the back buffer's depth buffer)"),
    (r"\(the game then disables its render-to-texture effects\)", "(applications then disable their render-to-texture effects)"),
    (r"the game actually uses \(state values", "an application actually uses (state values"),
    (r"feature the game hit \(render state values", "feature the application hit (render state values"),
    (r"exercises the game's device-lost path", "exercises the application's device-lost path"),
    (r"so the game uses its shader paths", "so applications take their shader paths"),
    (r"The C runtime's argument vector, defined by Dependencies/WebCompat when linked\n// into the game\. Weak so that the library also links without it\.",
     "The C runtime's argument vector (defined by a Win32 compatibility layer, if any). Weak so that the\n// library also links without it."),
    (r"like the sub-allocations of WW3D2's dynamic vertex buffers\.", "like the sub-allocations of a dynamic vertex buffer pool."),
    (r"The game's own shader sources \(assembled at run time\)", "Inline shader sources (assembled at run time)"),
    (r"\(the shader files of the game use it for constant register names\)", "(shader sources use it for constant register names)"),
    (r"Every pixel shader source of the game must assemble", "Every pixel shader source must assemble"),
    (r"Context loss: the game's device-lost path end to end", "Context loss: the device-lost path end to end"),
    (r"The game's DX8Wrapper::Reset_Device: release", "A typical application reset: release"),
    (r"state caching, the game's shader sources", "state caching, inline shader sources"),
    (r"WEBGL_lose_context \+ the game's Reset path", "WEBGL_lose_context + the application's Reset path"),
    (r'"game_shaders"', '"inline_shaders"'),
    (r'"game_vshaders"', '"inline_vshaders"'),
    (r"\bCaseGameShaders\b", "CaseInlineShaders"),
    (r"\bCaseGameVertexShaders\b", "CaseInlineVertexShaders"),
    (r'by the "game"', 'by the "application"'),
    # test shader file names -> embedded sources (see DEST_RULES)
    (r'"Trees\.nvp"', '"modulate2x.psh"'), (r"\bTrees\.nvp\b", "modulate2x.psh"),
    (r'"fterrain\.nvp"', '"blend2_lit.psh"'), (r"\bfterrain\.nvp\b", "blend2_lit.psh"),
    (r'"fterrain0\.nvp"', '"single_lit.psh"'), (r"\bfterrain0\.nvp\b", "single_lit.psh"),
    (r'"fterrainnoise\.nvp"', '"blend2_lit_mod1.psh"'), (r"\bfterrainnoise\.nvp\b", "blend2_lit_mod1.psh"),
    (r'"fterrainnoise2\.nvp"', '"blend2_lit_mod2.psh"'), (r"\bfterrainnoise2\.nvp\b", "blend2_lit_mod2.psh"),
    (r'"invmonochrome\.nvp"', '"inv_mono.psh"'), (r"\binvmonochrome\.nvp\b", "inv_mono.psh"),
    (r'"monochrome\.nvp"', '"mono.psh"'), (r"\bmonochrome\.nvp\b", "mono.psh"),
    (r'"motionblur\.nvp"', '"bem_blend.psh"'), (r"\bmotionblur\.nvp\b", "bem_blend.psh"),
    (r'"roadnoise2\.nvp"', '"three_mod_lit.psh"'), (r"\broadnoise2\.nvp\b", "three_mod_lit.psh"),
    (r'"terrainnoise2\.nvp"', '"lrp_lit_mod2.psh"'), (r"\bterrainnoise2\.nvp\b", "lrp_lit_mod2.psh"),
    (r'"terrainnoise\.nvp"', '"lrp_lit_mod1.psh"'), (r"\bterrainnoise\.nvp\b", "lrp_lit_mod1.psh"),
    (r'"terrain\.nvp"', '"lrp_lit.psh"'), (r"\bterrain\.nvp\b", "lrp_lit.psh"),
    (r'"wave\.nvp"', '"bem.psh"'), (r"\bwave\.nvp\b", "bem.psh"),
    (r'"Trees\.nvv"', '"wind_sway.vsh"'), (r"\bTrees\.nvv\b", "wind_sway.vsh"),
    (r'"wave\.nvv"', '"proj_texgen.vsh"'), (r"\bwave\.nvv\b", "proj_texgen.vsh"),
]

# Applied to the text of every output, in order.
RENAMES = [
    (r'#include "\.\./src/d3d8_headers\.h"', "#include <d3d8.h>\n#include <d3dx8.h>"),
    (r"-webd3d8", "-dxwebgl2-"),
    (r"/tmp/webd3d8_", "/tmp/dxwebgl2_"),
    (r"\bwebd3d8_", "dxwebgl2_"),
    (r"\bwebd3d8([A-Z])", r"dxwebgl2\1"),
    (r"tests/reference_hashes\.json", "tests/runner/reference_hashes.json"),
    (r"\bWD3D_", "DXW_"),
    (r"\bWebD3D8_", "dxwebgl2_"),
    (r"\bWEBD3D8_", "DXWEBGL2_"),
    (r"\bwebd3d8\b", "dxwebgl2"),
    (r"Dependencies/WebD3D8", "dxWebGL2"),
    (r"\bWebD3D8/WebD3D8\.h", "dxwebgl2/dxwebgl2.h"),
    (r"\bWebD3D8\b", "dxWebGL2"),
    (r"\bweb_d3d8_test\b", "d3d8_runtime_test"),
    (r"\bweb_d3d8_features\b", "d3d8_features_test"),
    (r"\bweb_d3d8\b", "dxwebgl2"),
    (r"WebAssembly port: ?", ""),
    # The unused device argument of CreateProgram went away with the core / front end split.
    (r"CreateProgram\(Device \*dev, ", "CreateProgram("),
    (r"CreateProgram\(Device \*, ", "CreateProgram("),
    (r"CreateProgram\(this, ", "CreateProgram("),
    # The real D3DX error code (MAKE_DDHRESULT(2905)); the upstream value was a typo.
    (r"#define D3DXERR_INVALIDDATA_ \(\(HRESULT\)0x88760B54\)", "#define D3DXERR_INVALIDDATA_ ((HRESULT)D3DXERR_INVALIDDATA)"),
]

# The feature test used shader files of the game; the library carries equivalent sources of its own.
FEATURE_SHADERS_CPP = r"""
/// The shader sources of this test (equivalents of the shaders that applications typically ship), by name.
static const char *InlineShaderSource(const std::string &name)
{
    static const struct { const char *name; const char *src; } kSources[] = {
        {"modulate2x.psh", "ps.1.1\ntex t0\nmov r0, v0\nmul r0, r0, t0\nmul_x2 r0.rgb, r0, c1\nmov r0.a, t0.a\n"},
        {"blend2_lit.psh", "ps.1.1\ntex t0\ntex t1\nmul r0, t1, t0\nmul r0, r0, v0\n"},
        {"single_lit.psh", "ps.1.1\ntex t1\nmov r0, t1\nmul r0, r0, v0\n"},
        {"blend2_lit_mod1.psh", "ps.1.1\ntex t0\ntex t1\ntex t2\nmul r0, t1, t0\nmul r0, r0, v0\nmul r0, r0, t2\n"},
        {"blend2_lit_mod2.psh", "ps.1.1\ntex t0\ntex t1\ntex t2\ntex t3\nmul r0, t1, t0\nmul r0, r0, v0\nmul r0, r0, t2\nmul r0, r0, t3\n"},
        {"inv_mono.psh", "ps.1.1\ntex t0\ndp3 r1, t0, c0\nmul r1, r1, c1\nlrp r0, c2, 1-r1, t0\n"},
        {"mono.psh", "ps.1.1\ntex t0\ndp3 r1, t0, c0\nmul r1, r1, c1\nlrp r0, c2, r1, t0\n"},
        {"bem_blend.psh", "ps.1.1\ntex t0\ntex t1\ntexbem t2, t1\ntex t3\nmul r0.rgb, v0, t0\n+add r0.a, v0, t0\n"
                          "mul r1, t2, c0\nadd r0.rgb, r0, r1\n+mul r0.a, r0, t3\n"},
        {"three_mod_lit.psh", "ps.1.1\ntex t0\ntex t1\ntex t2\nmul r0, t0, t1\nmul r0, r0, t2\nmul r0, r0, v0\n"},
        {"lrp_lit.psh", "ps.1.1\ntex t0\ntex t1\nlrp r0, v0.a, t1, t0\nmul r0, r0, v0\n"},
        {"lrp_lit_mod1.psh", "ps.1.1\ntex t0\ntex t1\ntex t2\nlrp r0, v0.a, t1, t0\nmul r0, r0, v0\nmul r0, r0, t2\n"},
        {"lrp_lit_mod2.psh", "ps.1.1\ntex t0\ntex t1\ntex t2\ntex t3\nlrp r0, v0.a, t1, t0\nmul r0, r0, v0\nmul r0, r0, t2\nmul r0, r0, t3\n"},
        {"bem.psh", "ps.1.1\ntex t0\ntexbem t1, t0\n#ifndef WITH_ALPHA_TEXTURE\nmul r0, t1, v0\n#else\ntex t2\n"
                    "mul r0.rgb, t1, v0\n+mul r0.a, t2, v0\n#endif\n"},
        // Sway: skew by a per-instance table entry selected with a0 (relative constant addressing), scale the colour.
        {"wind_sway.vsh",
         "vs.1.1\nmov r2, v1.wwzw\nsub r2, v0, r2\nmov a0.x, v1\nmov r0, c[a0.x+8]\nmad r1, r2.zzzw, r0, v0\nm4x4 oPos, r1, c4\n"
         "mov r2, v1.yyyw\nmul oD0, v2, r2\nmov oT0, v7\nadd r1, v0, c32\nmul oT1, r1, c33\n"},
        // Projective texture coordinates: dp4 transform, rcp, mad with write masks.
        {"proj_texgen.vsh",
         "#define POSITION v0\n#define DIFFUSE v1\n#define TEXCOORD v2\n#define ROW0 2\n#define ROW1 3\n#define ROW2 4\n#define ROW3 5\n#define PROJ_ROW 6\n"
         "vs.1.1\ndp4 r0.x, POSITION, c[ROW0]\ndp4 r0.y, POSITION, c[ROW1]\ndp4 r0.z, POSITION, c[ROW2]\n"
         "dp4 r0.w, POSITION, c[ROW3]\nmov oPos, r0\nrcp r1.w, r0.w\nmul r1.xy, r0.xy, r1.w\n"
         "mad oT1.xy, r1.xy, c[PROJ_ROW].xy, c[PROJ_ROW].zw\nmov oT0, TEXCOORD\nmov oD0, DIFFUSE\n"},
    };
    for (const auto &e : kSources)
        if (name == e.name) return e.src;
    return nullptr;
}

static std::string ReadShaderFile(const char *name)
{
    const char *src = InlineShaderSource(name);
    if (!src) { printf("missing shader source %s\n", name); return ""; }
    return src;
}
"""

# Mechanical edits of single destination files that go with the restructuring (the core classes depend on GLState instead of
# the front end's Device, shared id counters become functions, ...). They are part of the transformation, not hand
# edits, so that the three-way merge of newer upstream code does not conflict on these lines. (dest regex, pattern,
# replacement); a rule that matches nothing is skipped.
DEST_RULES = [
    # The Device derives from GLState (src/core/gl_state.h), which owns what the resource classes need from the GL
    # context: capabilities, the buffer / texture binding caches, the scratch framebuffer used for read-backs.
    (r"^src/d3d8/device\.h$", r"class Device final : public Unknown<IDirect3DDevice8>\n",
     "class Device final : public Unknown<IDirect3DDevice8>, public GLState\n", "class Device"),
    (r"^src/d3d8/device\.h$", r"    const GLCaps &Caps\(\) const \{ return m_glcaps; \}\n", "", "GLState::Caps"),
    (r"^src/d3d8/device\.h$", r"    /// Binds a texture to the dedicated upload unit\.\n    void BindForUpload\(GLenum target, GLuint tex\);\n", "",
     "GLState::BindForUpload"),
    (r"^src/d3d8/device\.h$", r"    /// Must be called before deleting a GL texture so cached bindings are dropped\.\n    void ForgetTexture\(GLuint tex\);\n", "",
     "GLState::ForgetTexture"),
    (r"^src/d3d8/device\.h$", r"    void ForgetBuffer\(GLuint buf\);\n", "", "GLState::ForgetBuffer"),
    (r"^src/d3d8/device\.h$", r"    /// Reads the RGBA8 contents of a texture level \(or of the current read framebuffer\)\.\n"
     r"    bool ReadTextureLevel\(GLuint tex, GLenum target, GLenum faceTarget, int level, uint32_t w, uint32_t h,\n"
     r"                          const RECT &rc, uint8_t \*rgbaOut\);\n    bool ReadRenderbuffer\(GLuint rb, uint32_t w, uint32_t h, const RECT &rc, uint8_t \*rgbaOut\);\n", "",
     "GLState::ReadTextureLevel / ReadRenderbuffer"),
    (r"^src/d3d8/device\.h$", r"    GLuint ScratchFramebuffer\(\) const \{ return m_scratchFbo; \}\n", "", "GLState::ScratchFramebuffer"),
    (r"^src/d3d8/device\.h$", r"    /// Binds a buffer to ARRAY_BUFFER / ELEMENT_ARRAY_BUFFER through the state cache\.\n    void BindBuffer\(GLenum target, GLuint buf\)\n"
     r"    \{\n(?:        //[^\n]*\n)?        GLuint &cached = [^\n]*\n"
     r"        if \(cached != buf\) \{ glBindBuffer\(target, buf\); cached = buf; \}\n    \}\n", "", "GLState::BindBuffer"),
    # The hooks through which GLState reaches what the device caches (the element array binding of the bound vertex array
    # object, framebuffer attachment keys, vertex array objects, the multisampled back buffer).
    (r"^src/d3d8/device\.h$", r"    Device\(\) = default;\n",
     "    Device() = default;\n"
     "    // Hooks of GLState\n"
     "    GLuint &BufferBindingSlot(GLenum target) override\n"
     "    {\n"
     "        // The element array binding belongs to the vertex array object that is bound.\n"
     "        return target == GL_ARRAY_BUFFER ? m_boundArrayBuffer : (m_curVao ? m_curVao->element : m_boundElementBuffer);\n"
     "    }\n"
     "    void OnForgetTexture(GLuint tex) override;\n"
     "    void OnForgetBuffer(GLuint buf) override;\n"
     "    void PrepareTextureRead(GLuint tex) override;\n", "Device hooks"),
    (r"^src/d3d8/device\.h$", r"    void RegisterGL\(GLObject \*o\);\n    void UnregisterGL\(GLObject \*o\);\n", "", "GLState::RegisterGL"),
    (r"^src/d3d8/device\.h$", r"    GLObject \*m_glObjects = nullptr;[^\n]*\n", "", "GLState::m_glObjects"),
    (r"^src/d3d8/device\.cpp$", r"for \(GLObject \*o = m_glObjects; o; o = o->m_glNext\) o->OnContextLost\(\);", "NotifyContextLost();", "OnContextLost loop"),
    (r"^src/d3d8/device\.cpp$", r"for \(GLObject \*o = m_glObjects; o; o = o->m_glNext\) o->OnContextRestored\(\);", "NotifyContextRestored();", "OnContextRestored loop"),
    (r"^src/core/types\.h$", r"explicit GLObject\(Device \*dev\);", "explicit GLObject(GLState *state);", "GLObject ctor"),
    (r"^src/core/types\.h$", r"    friend class Device;\n    Device \*m_glDevice;", "    friend class GLState;\n    GLState *m_glDevice;", "GLObject members"),
    (r"^src/core/types\.h$", r"The device keeps a list of them so that", "The GL state keeps a list of them so that"),
    (r"^src/core/types\.h$", r" \(see Device::RestoreContext\(\)\)\.", "."),
    (r"^src/d3d8/device\.h$", r"    bool IsDecodingDXT\(\) const \{ return !m_glcaps\.s3tc; \}\n", "", "GLState::IsDecodingDXT"),
    (r"^src/d3d8/device\.h$", r"    GLCaps m_glcaps;\n", "", "GLState::m_glcaps"),
    (r"^src/d3d8/device\.h$", r"    GLuint m_fbo = 0, m_scratchFbo = 0;\n    bool m_targetsDirty = true;\n", "    GLuint m_fbo = 0;\n",
     "GLState::m_scratchFbo, m_targetsDirty"),
    (r"^src/d3d8/device\.h$", r"    GLuint m_boundArrayBuffer = 0xFFFFFFFF;\n", "", "GLState::m_boundArrayBuffer"),
    (r"^src/d3d8/device\.h$", r"    GLuint m_boundElementBuffer = 0xFFFFFFFF;\n", "", "GLState::m_boundElementBuffer"),
    (r"^src/d3d8/device\.h$", r"    GLuint m_boundTex\[MAX_STAGES \+ 1\]\[3\];\n", "", "GLState::m_boundTex"),
    (r"^src/d3d8/device\.h$", r"    int m_activeUnit = -1;\n", "", "GLState::m_activeUnit"),
    (r"^src/d3d8/device\.h$", r"    uint64_t m_attribSig = 0;\n", "", "GLState::m_attribSig"),
    (r"^src/core/(buffer_storage|texture_storage)\.(h|cpp)$", r"\bDevice \*dev\b", "GLState *dev"),
    (r"^src/core/(buffer_storage|texture_storage)\.h$", r"\bDevice \*m_dev;", "GLState *m_dev;"),
    (r"^src/core/texture_storage\.cpp$", r"static uint32_t g_nextId = 1;\n",
     "uint32_t NextResourceId()\n{\n    static uint32_t next = 1;\n    return next++;\n}\n"),
    (r"^src/(core/texture_storage|d3d8/texture)\.cpp$", r"\bg_nextId\+\+", "NextResourceId()"),
    (r"^src/d3d8/texture\.cpp$", r"static uint32_t g_nextId = 1;\n\n", ""),
    (r"^src/d3d8/texture\.cpp$", r"DeviceChild\(tex->m_dev, false\)", "DeviceChild(static_cast<Device *>(tex->m_dev), false)"),
    (r"^src/core/texture_storage\.h$", r"class GLState;\n",
     "class GLState;\n\n/// Process-wide unique id for textures and surfaces (render target attachments are tracked by id).\nuint32_t NextResourceId();\n"),
    (r"^src/core/vertex_layout\.cpp$", r"static uint32_t g_nextLayoutId = 1;\n",
     "uint32_t NextVertexLayoutId()\n{\n    static uint32_t next = 1;\n    return next++;\n}\n"),
    (r"^src/(core/vertex_layout|d3d8/vertex_declaration)\.cpp$", r"\bg_nextLayoutId\+\+", "NextVertexLayoutId()"),
    (r"^src/core/types\.h$", r"UINT FVFVertexSize\(DWORD fvf\);\n",
     "UINT FVFVertexSize(DWORD fvf);\n/// Process-wide unique id of a vertex layout (layouts are compared by id in program keys).\nuint32_t NextVertexLayoutId();\n"),
    (r"^src/d3d8/d3dx_misc\.cpp$", r'#include "core/shader\.h"\n',
     '#include "d3d8/d3d8_headers.h"\n#include "api/com.h"\n#include "core/shader.h"\n'),
    (r"^src/d3d8/d3dx_texture\.cpp$", r'#include "d3d8/resources\.h"\n', '#include "d3d8/d3d8_headers.h"\n#include "d3d8/resources.h"\n'),
    (r"^src/core/fixedfunction\.cpp$", r"\nconst char \*kSwizzle\[\]", "\n[[maybe_unused]] const char *kSwizzle[]"),
    (r"^src/core/buffer_storage\.h$", r"\n    DWORD m_usage;", "\n    [[maybe_unused]] DWORD m_usage;"),
    (r"^tests/runtime/d3d8_features_test\.cpp$", r"(?s)static std::string ReadShaderFile\(const char \*name\)\n\{.*?\n\}\n",
     FEATURE_SHADERS_CPP.lstrip("\n"), "ReadShaderFile"),
]

# The "shaders" scene of the runtime test draws with inline shaders of this repository (the game's own were replaced, see
# docs/SYNC.md), so its frame hash differs from upstream's.
REFERENCE_HASH_OVERRIDES = {"shaders": "ed22b76b734dad2a"}

GLSTATE_NOTIFY = """void GLState::NotifyContextLost()
{
    for (GLObject *o = m_glObjects; o; o = o->m_glNext) o->OnContextLost();
}

void GLState::NotifyContextRestored()
{
    for (GLObject *o = m_glObjects; o; o = o->m_glNext) o->OnContextRestored();
}

"""

# Words that must not remain in the library sources after the rules ran (the leftover check).
GAME_WORDS = re.compile(r"\b(WW3D2?|W3D\w*|Generals|Zero Hour|Renegade|Westwood|C&C|WebCompat|the game|game's|RM_\w+)\b")


def apply_rules(text, warnings, dest):
    for pat, rep in WORDING:
        text = re.sub(pat, rep, text)
    for pat, rep in RENAMES:
        text = re.sub(pat, rep, text)
    for i, line in enumerate(text.split("\n"), 1):
        m = GAME_WORDS.search(line)
        if m:
            warnings.append("%s:%d: game specific wording left: %r" % (dest, i, m.group(0)))
    return text


# ----------------------------------------------------------------------------------------------
# The transformation
# ----------------------------------------------------------------------------------------------
def transform(raw, warnings):
    out = {}
    used = set()

    def get(name):
        if name not in raw:
            raise SyncError("upstream file missing: %s" % name)
        used.add(name)
        return Raw(name, raw[name])

    def emit(dest, text, layer):
        text = map_includes(text, layer)
        for rule in DEST_RULES:
            dest_pat, pat, rep = rule[:3]
            if re.search(dest_pat, dest):
                new_text, n = re.subn(pat, lambda m, rep=rep: rep, text)
                if n == 0 and len(rule) > 3:
                    warnings.append("%s: expected text not found (%s): upstream changed it, reconcile by hand" % (dest, rule[3]))
                text = new_text
        out[dest] = apply_rules(text, warnings, dest)

    # --- public header -------------------------------------------------------------------------
    if "include/WebD3D8/WebD3D8.h" in raw:
        r = get("include/WebD3D8/WebD3D8.h")
        emit("include/dxwebgl2/dxwebgl2.h", SPDX_HEADER + comment_block(r.desc) + r.body, "core")

    # --- common.h: config/helpers (core) and the COM plumbing (api) ---------------------------------------
    r = get("src/common.h")
    content = r.content()
    core_part, com_part = (content[:find(content, banner("COM plumbing"), "common.h: COM plumbing banner")],
                           content[find(content, banner("COM plumbing"), "common.h: COM plumbing banner"):])
    pro = r.prologue().replace('#include "d3d8_headers.h"', '#include "core/api_types.h"')
    emit("src/core/common.h", cpp_file(r.desc, pro, core_part), "core")
    emit("src/api/com.h",
         cpp_file("COM plumbing shared by the Direct3D front ends: reference counted IUnknown base and an\n"
                  "intrusive smart pointer.",
                  '#pragma once\n\n#include "core/api_types.h"\n\n#include <utility>\n', com_part), "core")

    # --- device.h: core types / the Device class ----------------------------------------------------------------
    r = get("src/device.h")
    c = r.content()
    # chunk boundaries (in file order)
    p_enum = find(c, r"^enum\n\{\n    MAX_STAGES", "device.h: limits enum")
    p_caps = find(c, r"^/// Capabilities of the GL implementation\.", "device.h: GLCaps")
    p_vdecl = find(c, banner("Vertex declarations"), "device.h: vertex declarations banner")
    p_parse = find(c, r"^/// Parses a D3DVSD_\* declaration", "device.h: ParseVertexDeclaration comment")
    p_fvfsize = find(c, r"^UINT FVFVertexSize", "device.h: FVFVertexSize")
    p_state = find(c, banner("State"), "device.h: state banner")
    p_pipe = find(c, r"^/// The GL pipeline state derived", "device.h: PipelineState")
    p_dev = find(c, r"^class Device final", "device.h: class Device")
    fwd = c[:p_enum]
    core_types = c[p_enum:p_parse] + c[p_fvfsize:p_state] + c[p_pipe:p_dev]
    d3d8_decl = c[p_parse:p_fvfsize]
    d3d8_state = c[p_state:p_pipe]
    d3d8_device = c[p_dev:]
    pro = r.prologue()
    emit("src/core/types.h",
         cpp_file("Types shared by the core and the Direct3D front ends: limits, GL capabilities, vertex layouts and\n"
                  "the GL pipeline state.",
                  pro,
                  "class GLState;\nclass Program;\nclass VertexShaderObject;\nclass PixelShaderObject;\n\n" + core_types), "core")
    emit("src/d3d8/device.h",
         cpp_file(r.desc, pro.replace('#include "format.h"\n', '#include "format.h"\n#include "api/com.h"\n'
                                      '#include "core/types.h"\n#include "core/gl_state.h"\n'),
                  fwd + d3d8_decl + "\n" + d3d8_state + d3d8_device), "d3d8")

    # --- device.cpp: the Device and the GL helpers that became GLState -----------------------------------------
    r = get("src/device.cpp")
    c = r.content()
    # The helpers of the resource classes (what GLState provides) are moved function by function: anything else that is
    # added to the device later stays in the Device.
    rest = c
    moved = []
    device_hooks = {}
    for fn in ("BindForUpload", "ForgetTexture", "ForgetBuffer", "ReadTextureLevel", "ReadRenderbuffer"):
        rest, body = cut_function(rest, r"^(?:void|bool) Device::%s\(" % fn, "device.cpp: Device::" + fn)
        body = body.replace("Device::", "GLState::")
        # What depends on the front end (framebuffer attachment keys, vertex array objects, the multisampled back buffer)
        # stays in the device and is reached through the virtual hooks of GLState.
        if fn == "ForgetTexture":
            body, n = re.subn(r"    (m_fboColorKey = m_fboDepthKey = 0;[^\n]*)\n", "    OnForgetTexture(tex);\n", body)
            device_hooks["texture"] = re.search(r"m_fboColorKey = m_fboDepthKey = 0;[^\n]*", c).group(0) if n else None
        elif fn == "ForgetBuffer":
            m = re.search(r"    // Vertex array objects that use the buffer would keep it alive\.\n    for \(auto it = m_vaoCache.*?\n    }\n", body, re.S)
            if not m:
                raise SyncError("device.cpp: Device::ForgetBuffer: vertex array loop not found")
            device_hooks["buffer"] = m.group(0)
            body = body.replace(m.group(0), "    OnForgetBuffer(buf);\n")
        elif fn == "ReadTextureLevel":
            body, n = re.subn(r"    if \(tex == m_bbColor\) ResolveBackBuffer\(\);\n", "    PrepareTextureRead(tex);\n", body)
            if not n:
                raise SyncError("device.cpp: Device::ReadTextureLevel: back buffer resolve not found")
        moved.append(body)
    # The registry of the objects that own WebGL names moved to GLState (the resource classes in the core need it).
    registry = []
    for sig in (r"^GLObject::GLObject\(", r"^GLObject::~GLObject\(", r"^void Device::RegisterGL\(", r"^void Device::UnregisterGL\("):
        rest, body = cut_function(rest, sig, "device.cpp: " + sig)
        registry.append(body.replace("Device::", "GLState::"))
    registry = "".join(registry)
    registry = registry.replace("GLObject::GLObject(Device *dev) : m_glDevice(dev)", "GLObject::GLObject(GLState *state) : m_glDevice(state)")
    registry = registry.replace("if (dev) dev->RegisterGL(this);", "if (state) state->RegisterGL(this);")
    rest = re.sub(r"//-+\n// GLObject registry\n//-+\n", "", rest)
    rest, invalidate = cut_out(rest, r"^void PipelineState::Invalidate\(\)", r"^//-+\n// Creation\n", "device.cpp: PipelineState::Invalidate")
    # the banner of the moved block is left behind when nothing else follows it: drop it
    rest = re.sub(r"\n//-+\n// GL helpers for the resources\n//-+\n\s*$", "\n", rest)
    hooks = ("\n//" + "-" * 78 + "\n// Hooks of GLState for what the device caches\n//" + "-" * 78 + "\n"
             "void Device::OnForgetTexture(GLuint)\n{\n    " + (device_hooks.get("texture") or "m_fboColorKey = m_fboDepthKey = 0;") + "\n}\n\n"
             "void Device::OnForgetBuffer(GLuint buf)\n{\n" + device_hooks["buffer"] + "}\n\n"
             "void Device::PrepareTextureRead(GLuint tex)\n{\n    if (tex == m_bbColor) ResolveBackBuffer();\n}\n")
    rest = rest.rstrip("\n") + "\n" + hooks
    emit("src/d3d8/device.cpp", cpp_file(r.desc, r.prologue(), rest, r.tail()), "d3d8")
    helpers = (invalidate + "//" + "-" * 78 + "\n// Registry of the objects that own WebGL names\n//" + "-" * 78 + "\n" + registry + GLSTATE_NOTIFY +
               "//" + "-" * 78 + "\n// GL helpers for the resources\n//" + "-" * 78 + "\n" + "".join(moved))
    emit("src/core/gl_state.cpp",
         cpp_file("GL state cache and helpers shared by all resources: texture and buffer binding caches, read-back\n"
                  "of render targets.",
                  '#include "core/gl_state.h"\n', helpers), "core")

    # --- resources.h ------------------------------------------------------------------------------------
    r = get("src/resources.h")
    c = r.content()
    rest, bufs = cut_out(c, r"^/// Shared CPU-shadow \+ GL buffer logic", r"^class VertexBuffer final",
                         "resources.h: BufferStorage")
    rest, texs = cut_out(rest, r"^struct LevelData", r"^class Texture2D final", "resources.h: TextureBase")
    emit("src/core/buffer_storage.h",
         cpp_file("CPU-shadowed GL buffer storage used by vertex and index buffers.",
                  '#pragma once\n\n#include "core/types.h"\n', "class GLState;\n\n" + bufs), "core")
    emit("src/core/texture_storage.h",
         cpp_file("GL texture storage with CPU shadow copies, shared by 2D, cube and volume textures.",
                  '#pragma once\n\n#include "core/types.h"\n', "class GLState;\n\n" + texs), "core")
    emit("src/d3d8/resources.h",
         cpp_file(r.desc, r.prologue() + '#include "core/buffer_storage.h"\n#include "core/texture_storage.h"\n', rest),
         "d3d8")

    # --- buffers.cpp -------------------------------------------------------------------------------------
    r = get("src/buffers.cpp")
    c = r.content()
    rest, storage = cut_out(c, r"^BufferStorage::BufferStorage", r"^VertexBuffer::VertexBuffer", "buffers.cpp: BufferStorage")
    emit("src/core/buffer_storage.cpp",
         cpp_file("CPU-shadowed GL buffer storage: Lock() writes into a system-memory copy, the modified range is\n"
                  "pushed to the GL buffer lazily, right before a draw call reads from it, so several locks between\n"
                  "two draws cost a single upload.",
                  '#include "core/buffer_storage.h"\n#include "core/gl_state.h"\n', storage), "core")
    emit("src/d3d8/buffers.cpp", cpp_file(r.desc, r.prologue(), rest), "d3d8")

    # --- texture.cpp -------------------------------------------------------------------------------------
    r = get("src/texture.cpp")
    c = r.content()
    rest, storage = cut_out(c, banner("TextureBase"), banner("Texture2D"), "texture.cpp: TextureBase")
    emit("src/core/texture_storage.cpp",
         cpp_file("GL texture storage with CPU shadow copies: allocation, level locking, upload and read-back.",
                  '#include "core/texture_storage.h"\n#include "core/gl_state.h"\n',
                  "static uint32_t g_nextId = 1;\n\n" + storage), "core")
    emit("src/d3d8/texture.cpp", cpp_file(r.desc, r.prologue(), rest), "d3d8")

    # --- vertexformat.cpp ------------------------------------------------------------------------------------
    r = get("src/vertexformat.cpp")
    c = r.content()
    fvf, decl = cut_out(c, r"^bool ParseVertexDeclaration", None, "vertexformat.cpp: ParseVertexDeclaration")
    emit("src/core/vertex_layout.cpp",
         cpp_file("Vertex layouts from FVF codes; layouts are attribute lists bound to the D3DVSDE_* register numbers,\n"
                  "which double as GL attribute locations.",
                  r.prologue(), fvf), "core")
    emit("src/d3d8/vertex_declaration.cpp",
         cpp_file("Vertex layouts from Direct3D 8 D3DVSD_* declarations.", r.prologue(), decl), "d3d8")

    # --- plain moves ---------------------------------------------------------------------------------------------
    plain = {
        "src/util.cpp": ("src/core/util.cpp", "core"),
        "src/format.h": ("src/core/format.h", "core"),
        "src/format.cpp": ("src/core/format.cpp", "core"),
        "src/program.h": ("src/core/program.h", "core"),
        "src/program.cpp": ("src/core/program.cpp", "core"),
        "src/fixedfunction.cpp": ("src/core/fixedfunction.cpp", "core"),
        "src/shader.h": ("src/core/shader.h", "core"),
        "src/shader_internal.h": ("src/core/shader_internal.h", "core"),
        "src/shader_assembler.cpp": ("src/core/shader_assembler.cpp", "core"),
        "src/shader_vs.cpp": ("src/core/shader_vs.cpp", "core"),
        "src/shader_ps.cpp": ("src/core/shader_ps.cpp", "core"),
        "src/d3d8.cpp": ("src/d3d8/d3d8.cpp", "d3d8"),
        "src/d3d8_internal.h": ("src/d3d8/d3d8_internal.h", "d3d8"),
        "src/d3dx_math.cpp": ("src/d3d8/d3dx_math.cpp", "d3d8"),
        "src/d3dx_misc.cpp": ("src/d3d8/d3dx_misc.cpp", "d3d8"),
        "src/d3dx_texture.cpp": ("src/d3d8/d3dx_texture.cpp", "d3d8"),
        "src/device_draw.cpp": ("src/d3d8/device_draw.cpp", "d3d8"),
        "src/device_state.cpp": ("src/d3d8/device_state.cpp", "d3d8"),
        "tests/web_d3d8_test.cpp": ("tests/runtime/d3d8_runtime_test.cpp", "d3d8"),
    }
    for name, (dest, layer) in plain.items():
        if name not in raw:
            raise SyncError("upstream file missing: %s" % name)
        r = get(name)
        text = SPDX_HEADER + (comment_block(r.desc) if r.desc else "") + r.body
        emit(dest, text, layer)

    # Newer upstream files (diagnostics, GL call counters, feature test).
    optional_plain = {
        "src/glcount.h": ("src/core/glcount.h", "core"),
        "src/diag.h": ("src/core/diag.h", "core"),
        "src/diag.cpp": ("src/core/diag.cpp", "core"),
        "tests/web_d3d8_features.cpp": ("tests/runtime/d3d8_features_test.cpp", "d3d8"),
    }
    for name, (dest, layer) in optional_plain.items():
        if name in raw:
            r = get(name)
            emit(dest, SPDX_HEADER + (comment_block(r.desc) if r.desc else "") + r.body, layer)

    # d3d8_headers.h: the Direct3D headers (the game's WebCompat windows.h/objbase.h are not needed here).
    r = get("src/d3d8_headers.h")
    emit("src/d3d8/d3d8_headers.h",
         SPDX_HEADER + comment_block("Includes the Direct3D 8 and D3DX 8 API headers (dxwebgl2/win32_min.h provides the Win32 base).") +
         "#pragma once\n\n#include <dxwebgl2/win32_min.h>\n\n#include <d3d8.h>\n#include <d3dx8.h>\n", "d3d8")

    # --- test runner files --------------------------------------------------------------------------------
    if "tests/run_test.mjs" in raw:
        emit("tests/runner/run_test.mjs", SPDX_JS + raw["tests/run_test.mjs"], "d3d8")
        used.add("tests/run_test.mjs")
    if "tests/serve.mjs" in raw:
        emit("tests/runner/serve.mjs", SPDX_JS + raw["tests/serve.mjs"], "d3d8")
        used.add("tests/serve.mjs")
    if "tests/run_features.mjs" in raw:
        emit("tests/runner/run_features.mjs", SPDX_JS + raw["tests/run_features.mjs"], "d3d8")
        used.add("tests/run_features.mjs")
    if "tests/run_regression.mjs" in raw:
        emit("tests/runner/run_regression.mjs", SPDX_JS + raw["tests/run_regression.mjs"], "d3d8")
        used.add("tests/run_regression.mjs")
    if "tests/reference_hashes.json" in raw:
        import json
        hashes = json.loads(raw["tests/reference_hashes.json"])
        hashes.update({k: v for k, v in REFERENCE_HASH_OVERRIDES.items() if k in hashes})
        out["tests/runner/reference_hashes.json"] = json.dumps(hashes, indent=2) + "\n"
        used.add("tests/reference_hashes.json")
    if "tests/shell.html" in raw:
        html = raw["tests/shell.html"]
        if html.lower().startswith("<!doctype html>"):
            first, rest = html.split("\n", 1)
            html = first + "\n" + SPDX_HTML + rest   # the doctype stays first (a comment before it is allowed but unusual)
        else:
            html = SPDX_HTML + html
        emit("tests/runtime/shell.html", html, "d3d8")
        used.add("tests/shell.html")

    # --- ignored / not mapped ---------------------------------------------------------------------------
    ignored = {"CMakeLists.txt", "tests/.gitignore", "COVERAGE.md"}   # COVERAGE.md: statuses are merged into docs/COVERAGE.md by hand
    for name in sorted(set(raw) - used - ignored):
        if name.startswith("src/") and name.endswith((".cpp", ".h")):
            base = name[len("src/"):]
            warnings.append("%s is new upstream and has no mapping in sync_rules.py: placed in src/d3d8/%s - "
                            "move it to src/core if it is API independent" % (name, base))
            r = get(name)
            layer = "d3d8"
            emit("src/d3d8/" + base, SPDX_HEADER + (comment_block(r.desc) if r.desc else "") + r.body, layer)
        elif name.startswith("tests/"):
            warnings.append("%s is new upstream and has no mapping in sync_rules.py: placed in tests/runner/" % name)
            emit("tests/runner/" + name[len("tests/"):], raw[name], "d3d8")
        else:
            warnings.append("%s is new upstream and has no mapping in sync_rules.py: ignored" % name)
    return out
