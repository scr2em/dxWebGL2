/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * WebGL2 backend: GLSL ES 3.00 programs (generation by the core's emitters, compilation, linking, uniform lookup).
 */
#include "core/gl/gl_backend.h"
#include "core/shader.h"

namespace dxwebgl2 {

static GLuint CompileShader(GLenum type, const std::string &src)
{
    GLuint sh = glCreateShader(type);
    const char *p = src.c_str();
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[2048];
        GLsizei n = 0;
        glGetShaderInfoLog(sh, sizeof log - 1, &n, log);
        log[n] = 0;
        Log("%s shader compile error: %s", type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        DXW_HIT(Failed, "GLSL %s shader failed to compile", type == GL_VERTEX_SHADER ? "vertex" : "fragment");
        if (GetConfig().debug) Log("source:\n%s", src.c_str());
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLProgram *WebGL2Backend::CompileProgram(const ProgramKey &key, const std::string &vsSrc, const std::string &fsSrc)
{
    GLProgram *prog = new GLProgram();
    prog->key = key;
    prog->generation = m_generation;
    ++g_d3d.programsCreated;
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs)
    {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        for (GLint &l : prog->loc) l = -1;
        return prog; // invalid, remembered so we do not retry every draw
    }
    prog->id = glCreateProgram();
    glAttachShader(prog->id, vs);
    glAttachShader(prog->id, fs);
    glLinkProgram(prog->id);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog->id, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[2048];
        GLsizei n = 0;
        glGetProgramInfoLog(prog->id, sizeof log - 1, &n, log);
        log[n] = 0;
        Log("program link error: %s", log);
        DXW_HIT(Failed, "GLSL program failed to link");
        glDeleteProgram(prog->id);
        prog->id = 0;
        for (GLint &l : prog->loc) l = -1;
        return prog;
    }
    for (int i = 0; i < U_COUNT; ++i)
    {
        prog->loc[i] = glGetUniformLocation(prog->id, UniformName((UniformId)i));
        if (prog->loc[i] >= 0)
        {
            prog->usedGroups |= 1u << UniformGroupOf((UniformId)i);
            prog->usedUniforms |= 1u << i;
        }
    }

    // How much of the packed transform block the program reads.
    {
        const bool wv = SourceUsesIdentifier(vsSrc, "u_wv"), nm = SourceUsesIdentifier(vsSrc, "u_nm"), world = SourceUsesIdentifier(vsSrc, "u_world");
        prog->xfCount = world ? 15 : (wv || nm ? 11 : 4);
    }

    // Samplers are fixed: stage N always samples texture unit N.
    glUseProgram(prog->id);
    for (int i = 0; i < MAX_STAGES; ++i)
    {
        char name[8];
        snprintf(name, sizeof name, "s%d", i);
        GLint l = glGetUniformLocation(prog->id, name);
        if (l >= 0) glUniform1i(l, i);
    }
    prog->valid = true;
    return prog;
}

Program *WebGL2Backend::CreateProgram(const ProgramKey &key, const VertexShaderObject *vso, const PixelShaderObject *pso)
{
    std::string vs = vso ? BuildTranslatedVertexShader(*vso, key) : GenerateFixedFunctionVertexShader(key);
    std::string fs = pso ? pso->Generate(key) : GenerateFixedFunctionFragmentShader(key);
    GLProgram *prog = CompileProgram(key, vs, fs);
    m_boundProgram = 0xFFFFFFFF; // creation changed the current program
    return prog;
}

void WebGL2Backend::DestroyProgram(Program *program)
{
    GLProgram *p = static_cast<GLProgram *>(program);
    if (!p) return;
    if (p->id && IsFresh(p->generation)) glDeleteProgram(p->id);
    delete p;
}

} // namespace dxwebgl2
