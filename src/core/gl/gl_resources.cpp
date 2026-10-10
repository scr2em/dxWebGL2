/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * WebGL2 backend: buffers, textures, renderbuffers, sampler objects, read-back, and the binding cache they share.
 */
#include "core/gl/gl_backend.h"

namespace dxwebgl2 {

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

//------------------------------------------------------------------------------
// Binding cache
//------------------------------------------------------------------------------
void WebGL2Backend::BindForUpload(GLenum target, GLuint tex)
{
    if (m_activeUnit != GL_UPLOAD_UNIT)
    {
        glActiveTexture(GL_TEXTURE0 + GL_UPLOAD_UNIT);
        m_activeUnit = GL_UPLOAD_UNIT;
    }
    int slot = target == GL_TEXTURE_2D ? 0 : (target == GL_TEXTURE_CUBE_MAP ? 1 : 2);
    if (m_boundTex[GL_UPLOAD_UNIT][slot] != tex)
    {
        glBindTexture(target, tex);
        m_boundTex[GL_UPLOAD_UNIT][slot] = tex;
    }
}

void WebGL2Backend::ForgetTexture(GLuint tex)
{
    // Deleting a texture unbinds it everywhere; mirror that in the cache.
    for (auto &unit : m_boundTex)
        for (GLuint &t : unit)
            if (t == tex) t = 0;
    m_targetsDirty = true;
    m_fboColorKey = m_fboDepthKey = 0;
}

void WebGL2Backend::ForgetBuffer(GLuint buf)
{
    if (m_boundArrayBuffer == buf) m_boundArrayBuffer = 0xFFFFFFFF;
    if (m_boundElementBuffer == buf) m_boundElementBuffer = 0xFFFFFFFF;
    m_attribSig = 0;
    // Vertex array objects that use the buffer would keep it alive.
    for (auto it = m_vaoCache.begin(); it != m_vaoCache.end();)
    {
        VaoEntry *e = it->second;
        bool uses = e->element == buf;
        for (int i = 0; i < VAO_KEY_STREAMS; ++i) uses = uses || (e->key.buf[i] == buf);
        if (!uses) { ++it; continue; }
        if (m_curVao == e) { glBindVertexArray(m_vao); m_curVao = nullptr; }
        if (e->vao) glDeleteVertexArrays(1, &e->vao);
        delete e;
        it = m_vaoCache.erase(it);
    }
}

//------------------------------------------------------------------------------
// Read-back
//------------------------------------------------------------------------------
bool WebGL2Backend::ReadTextureLevel(GLuint tex, GLenum target, GLenum faceTarget, int level, const RECT &rc, uint8_t *rgbaOut)
{
    if (target == GL_TEXTURE_3D) return false;
    if (tex == m_bbColor) ResolveBackBuffer();
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

bool WebGL2Backend::ReadRenderbuffer(GLuint rb, const RECT &rc, uint8_t *rgbaOut)
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

//------------------------------------------------------------------------------
// Buffers
//------------------------------------------------------------------------------
GpuBuffer *WebGL2Backend::CreateBuffer(BufferKind kind, uint32_t)
{
    GLBuffer *b = new GLBuffer();
    // WebGL fixes a buffer's role on first use: index buffers may only ever be bound to ELEMENT_ARRAY_BUFFER, vertex
    // buffers to ARRAY_BUFFER, so uploads go through the buffer's own target.
    b->target = kind == BufferKind::Index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    b->generation = m_generation;
    glGenBuffers(1, &b->name);
    if (!b->name) { delete b; return nullptr; } // context lost
    return b;
}

void WebGL2Backend::DestroyBuffer(GpuBuffer *buffer)
{
    GLBuffer *b = static_cast<GLBuffer *>(buffer);
    if (!b) return;
    if (b->name && IsFresh(b->generation))
    {
        ForgetBuffer(b->name);
        glDeleteBuffers(1, &b->name);
    }
    delete b;
}

void WebGL2Backend::WriteBuffer(GpuBuffer *buffer, uint32_t offset, uint32_t size, const void *data, bool initial)
{
    GLBuffer *b = static_cast<GLBuffer *>(buffer);
    if (!b) return;
    BindBuffer(b->target, b->name);
    if (initial) glBufferData(b->target, (GLsizeiptr)size, data, GL_DYNAMIC_DRAW);
    else glBufferSubData(b->target, offset, size, data);
}

//------------------------------------------------------------------------------
// Textures
//------------------------------------------------------------------------------
GpuTexture *WebGL2Backend::CreateTexture(const TextureDesc &d)
{
    GLTexture *t = new GLTexture();
    t->target = d.kind == TextureKind::Cube ? GL_TEXTURE_CUBE_MAP : (d.kind == TextureKind::Volume ? GL_TEXTURE_3D : GL_TEXTURE_2D);
    t->info = GetFormatInfo(d.format);
    t->decode = d.decodeDxt;
    t->generation = m_generation;
    glGenTextures(1, &t->name);
    BindForUpload(t->target, t->name);

    const GLenum internal = t->decode ? GL_RGBA8 : t->info->glInternal;
    const bool legacy = internal == GL_ALPHA || internal == GL_LUMINANCE || internal == GL_LUMINANCE_ALPHA;
    if (!legacy)
    {
        if (t->target == GL_TEXTURE_3D)
            glTexStorage3D(t->target, d.levels, internal, d.width, d.height, d.depth);
        else
            glTexStorage2D(t->target, d.levels, internal, d.width, d.height);
    }
    else
    {
        const uint32_t faces = d.kind == TextureKind::Cube ? 6 : 1;
        for (uint32_t f = 0; f < faces; ++f)
            for (uint32_t l = 0; l < d.levels; ++l)
                glTexImage2D(t->FaceTarget(f), l, internal, Max<uint32_t>(1, d.width >> l), Max<uint32_t>(1, d.height >> l), 0,
                             t->info->glFormat, t->info->glType, nullptr);
    }
    glTexParameteri(t->target, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(t->target, GL_TEXTURE_MAX_LEVEL, (GLint)d.levels - 1);
    t->baseLevel = 0;
    if (!t->name) { delete t; return nullptr; } // context lost
    return t;
}

void WebGL2Backend::DestroyTexture(GpuTexture *texture)
{
    GLTexture *t = static_cast<GLTexture *>(texture);
    if (!t) return;
    if (t->name && IsFresh(t->generation))
    {
        ForgetTexture(t->name);
        glDeleteTextures(1, &t->name);
    }
    delete t;
}

void WebGL2Backend::WriteTexture(GpuTexture *texture, const TextureRegion &r, const uint8_t *src, uint32_t rowPitch)
{
    GLTexture *t = static_cast<GLTexture *>(texture);
    if (!t) return;
    const FormatInfo &info = *t->info;
    BindForUpload(t->target, t->name);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const GLenum fmt = t->decode ? GL_RGBA : info.glFormat;
    const GLenum type = t->decode ? GL_UNSIGNED_BYTE : info.glType;

    if (t->target == GL_TEXTURE_3D)
    {
        // The whole level: its slices are consecutive rows.
        const uint8_t *data = PrepareForGL(info, src, rowPitch, r.width, r.height * r.depth, t->decode, m_uploadScratch);
        if (info.blockBytes && !t->decode)
            glCompressedTexSubImage3D(t->target, r.level, 0, 0, 0, r.width, r.height, r.depth, info.glInternal,
                                      (GLsizei)(rowPitch * FormatRows(info.d3d, r.height) * r.depth), data);
        else
            glTexSubImage3D(t->target, r.level, 0, 0, 0, r.width, r.height, r.depth, fmt, type, data);
        return;
    }

    const uint8_t *data = PrepareForGL(info, src, rowPitch, r.width, r.height, t->decode, m_uploadScratch);
    if (info.blockBytes && !t->decode)
    {
        uint32_t size = ((r.width + 3) / 4) * ((r.height + 3) / 4) * info.blockBytes;
        glCompressedTexSubImage2D(t->FaceTarget(r.face), r.level, r.x, r.y, r.width, r.height, info.glInternal, size, data);
    }
    else
        glTexSubImage2D(t->FaceTarget(r.face), r.level, r.x, r.y, r.width, r.height, fmt, type, data);
}

bool WebGL2Backend::ReadTexture(GpuTexture *texture, uint32_t face, uint32_t level, const RECT &rc, uint8_t *rgbaOut)
{
    GLTexture *t = static_cast<GLTexture *>(texture);
    if (!t) return false;
    return ReadTextureLevel(t->name, t->target, t->FaceTarget(face), (int)level, rc, rgbaOut);
}

//------------------------------------------------------------------------------
// Surfaces (renderbuffers)
//------------------------------------------------------------------------------
/// Renderbuffer storage format for a depth/stencil D3DFORMAT.
static GLenum DepthRenderbufferFormat(D3DFORMAT format)
{
    switch (format)
    {
    case D3DFMT_D16: case D3DFMT_D16_LOCKABLE: return GL_DEPTH_COMPONENT16;
    case D3DFMT_D32: case D3DFMT_D24X8: return GL_DEPTH_COMPONENT24;
    case D3DFMT_D24S8: case D3DFMT_D24X4S4: case D3DFMT_D15S1: return GL_DEPTH24_STENCIL8;
    default: return GL_DEPTH24_STENCIL8;
    }
}

GpuSurface *WebGL2Backend::CreateSurface(const SurfaceDesc &d)
{
    GLSurface *s = new GLSurface();
    s->generation = m_generation;
    const FormatInfo *info = GetFormatInfo(d.format);
    const GLenum internal = d.depthStencil ? DepthRenderbufferFormat(d.format)
                                           : (info && info->glInternal && info->renderTarget ? info->glInternal : GL_RGBA8);
    glGenRenderbuffers(1, &s->renderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, s->renderbuffer);
    if (d.samples) glRenderbufferStorageMultisample(GL_RENDERBUFFER, d.samples, internal, d.width, d.height);
    else glRenderbufferStorage(GL_RENDERBUFFER, internal, d.width, d.height);
    if (!s->renderbuffer) { delete s; return nullptr; } // context lost
    return s;
}

void WebGL2Backend::DestroySurface(GpuSurface *surface)
{
    GLSurface *s = static_cast<GLSurface *>(surface);
    if (!s) return;
    if (s->renderbuffer && IsFresh(s->generation)) glDeleteRenderbuffers(1, &s->renderbuffer);
    delete s;
}

bool WebGL2Backend::ReadSurface(GpuSurface *surface, const RECT &rc, uint8_t *rgbaOut)
{
    GLSurface *s = static_cast<GLSurface *>(surface);
    return ReadRenderbuffer(s ? s->renderbuffer : 0, rc, rgbaOut);
}

//------------------------------------------------------------------------------
// Sampler objects
//------------------------------------------------------------------------------
static GLenum MapAddress(uint8_t a)
{
    switch (a)
    {
    case D3DTADDRESS_WRAP: return GL_REPEAT;
    case D3DTADDRESS_MIRROR: case D3DTADDRESS_MIRRORONCE: return GL_MIRRORED_REPEAT;
    default: return GL_CLAMP_TO_EDGE; // CLAMP, BORDER (no border color in WebGL; emulated in the shader)
    }
}

GpuSampler *WebGL2Backend::GetSampler(const SamplerDesc &d)
{
    std::string key(reinterpret_cast<const char *>(&d), sizeof d);
    auto it = m_samplers.find(key);
    if (it != m_samplers.end()) return it->second;

    GLSampler *sampler = new GLSampler();
    GLuint s = 0;
    glGenSamplers(1, &s);
    GLenum gmin;
    const bool minLinear = d.minFilter != D3DTEXF_POINT && d.minFilter != D3DTEXF_NONE;
    if (!d.mipped) gmin = minLinear ? GL_LINEAR : GL_NEAREST;
    else if (d.mipFilter == D3DTEXF_POINT) gmin = minLinear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
    else gmin = minLinear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
    glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, gmin);
    glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, (d.magFilter == D3DTEXF_POINT || d.magFilter == D3DTEXF_NONE) ? GL_NEAREST : GL_LINEAR);
    glSamplerParameteri(s, GL_TEXTURE_WRAP_S, MapAddress(d.addressU));
    glSamplerParameteri(s, GL_TEXTURE_WRAP_T, MapAddress(d.addressV));
    glSamplerParameteri(s, GL_TEXTURE_WRAP_R, MapAddress(d.addressW));
    if (m_caps.anisotropic) glSamplerParameterf(s, GL_TEXTURE_MAX_ANISOTROPY_EXT, (float)d.maxAnisotropy);
    sampler->name = s;
    m_samplers[key] = sampler;
    return sampler;
}

} // namespace dxwebgl2
