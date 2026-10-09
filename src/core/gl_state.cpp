/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * GL state cache and helpers shared by all resources: texture and buffer binding caches, read-back
 * of render targets.
 */
#include "core/gl_state.h"

namespace dxwebgl2 {

void PipelineState::Invalidate()
{
    memset(this, 0xFF, sizeof *this);
}

//------------------------------------------------------------------------------
// Registry of the objects that own WebGL names
//------------------------------------------------------------------------------
GLObject::GLObject(GLState *state) : m_glDevice(state)
{
    if (state) state->RegisterGL(this);
}

GLObject::~GLObject()
{
    if (m_glDevice) m_glDevice->UnregisterGL(this);
}

void GLState::RegisterGL(GLObject *o)
{
    o->m_glPrev = nullptr;
    o->m_glNext = m_glObjects;
    if (m_glObjects) m_glObjects->m_glPrev = o;
    m_glObjects = o;
}

void GLState::UnregisterGL(GLObject *o)
{
    if (o->m_glPrev) o->m_glPrev->m_glNext = o->m_glNext;
    else if (m_glObjects == o) m_glObjects = o->m_glNext;
    if (o->m_glNext) o->m_glNext->m_glPrev = o->m_glPrev;
    o->m_glPrev = o->m_glNext = nullptr;
}

void GLState::NotifyContextLost()
{
    for (GLObject *o = m_glObjects; o; o = o->m_glNext) o->OnContextLost();
}

void GLState::NotifyContextRestored()
{
    for (GLObject *o = m_glObjects; o; o = o->m_glNext) o->OnContextRestored();
}

//------------------------------------------------------------------------------
// GL helpers for the resources
//------------------------------------------------------------------------------
void GLState::BindForUpload(GLenum target, GLuint tex)
{
    if (m_activeUnit != UPLOAD_UNIT)
    {
        glActiveTexture(GL_TEXTURE0 + UPLOAD_UNIT);
        m_activeUnit = UPLOAD_UNIT;
    }
    int slot = target == GL_TEXTURE_2D ? 0 : (target == GL_TEXTURE_CUBE_MAP ? 1 : 2);
    if (m_boundTex[UPLOAD_UNIT][slot] != tex)
    {
        glBindTexture(target, tex);
        m_boundTex[UPLOAD_UNIT][slot] = tex;
    }
}

void GLState::ForgetTexture(GLuint tex)
{
    // Deleting a texture unbinds it everywhere; mirror that in the cache.
    for (auto &unit : m_boundTex)
        for (GLuint &t : unit)
            if (t == tex) t = 0;
    m_targetsDirty = true;
    OnForgetTexture(tex);
}

void GLState::ForgetBuffer(GLuint buf)
{
    if (m_boundArrayBuffer == buf) m_boundArrayBuffer = 0xFFFFFFFF;
    if (m_boundElementBuffer == buf) m_boundElementBuffer = 0xFFFFFFFF;
    m_attribSig = 0;
    OnForgetBuffer(buf);
}

bool GLState::ReadTextureLevel(GLuint tex, GLenum target, GLenum faceTarget, int level, uint32_t, uint32_t,
                              const RECT &rc, uint8_t *rgbaOut)
{
    if (target == GL_TEXTURE_3D) return false;
    PrepareTextureRead(tex);
    if (g_diagOn) DiagHit(Hit::Perf, "GPU read-back of a texture or the back buffer (glReadPixels)");
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_scratchFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, faceTarget, tex, level);
    bool ok = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok)
    {
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, GL_RGBA, GL_UNSIGNED_BYTE, rgbaOut);
    }
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, faceTarget, 0, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    return ok;
}

bool GLState::ReadRenderbuffer(GLuint rb, uint32_t, uint32_t, const RECT &rc, uint8_t *rgbaOut)
{
    if (g_diagOn) DiagHit(Hit::Perf, "GPU read-back of a render target surface (glReadPixels)");
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_scratchFbo);
    glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    bool ok = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok)
    {
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, GL_RGBA, GL_UNSIGNED_BYTE, rgbaOut);
    }
    glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    return ok;
}

} // namespace dxwebgl2
