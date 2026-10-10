/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Uniform names and groups shared by the shader generators and the backends (compilation is the backend's,
 * see core/gl/gl_program.cpp).
 */
#include "core/program.h"

#include <cctype>

namespace dxwebgl2 {

static const char *const kUniformNames[U_COUNT] = {
    "u_xf[0]", "u_tm[0]", "u_view[0]", "u_clip[0]", "u_pt[0]", "u_mat[0]", "u_lt[0]", "u_ambient", "u_fogp[0]",
    "u_tfactor", "u_alphaRef", "u_lod[0]", "u_bump[0]", "u_bumpl[0]", "c[0]", "pc[0]", "u_border[0]",
};

/// Which group each uniform belongs to.
static const uint8_t kUniformGroup[U_COUNT] = {
    G_XFORM, G_TEXMAT, G_VIEWPORT, G_CLIP, G_POINT, G_MATERIAL, G_LIGHTS, G_AMBIENT, G_FOG,
    G_TFACTOR, G_ALPHAREF, G_LOD, G_BUMP, G_BUMP, G_VSC, G_PSC, G_BORDER,
};

const char *UniformName(UniformId u) { return kUniformNames[u]; }

UniformGroup UniformGroupOf(UniformId u) { return (UniformGroup)kUniformGroup[u]; }

bool SourceUsesIdentifier(const std::string &src, const char *name)
{
    const size_t n = strlen(name);
    // Only the code after the declarations counts (the macros that define the names mention them all).
    size_t from = src.find("// end of uniforms\n");
    from = from == std::string::npos ? 0 : from;
    for (size_t at = src.find(name, from); at != std::string::npos; at = src.find(name, at + 1))
    {
        const char after = at + n < src.size() ? src[at + n] : ' ';
        const char before = at ? src[at - 1] : ' ';
        if (!(isalnum((unsigned char)after) || after == '_') && !(isalnum((unsigned char)before) || before == '_')) return true;
    }
    return false;
}

} // namespace dxwebgl2
