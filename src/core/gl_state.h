/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * GLState: the WebGL2 context state shared by every resource and by the device.
 *
 * It owns what the resource classes need from "the" WebGL2 context, independent of any Direct3D device:
 * the capabilities of the implementation, the cache of what is bound to the buffer targets and texture units
 * (WebGL calls are expensive, redundant binds are filtered here) and a scratch framebuffer for read-backs. Buffer and texture storage (BufferStorage, TextureBase) depend on
 * this class only, so they can serve any API front end.
 *
 * A front end's device derives from GLState (the Direct3D 8 Device does).
 */
#pragma once

#include "core/types.h"

namespace dxwebgl2 {

class GLState
{
public:
    /// Capabilities of the GL implementation (valid once the context exists).
    const GLCaps &Caps() const { return m_glcaps; }
    /// DXT textures are decoded on the CPU when the implementation cannot sample S3TC.
    bool IsDecodingDXT() const { return !m_glcaps.s3tc; }

    /// Binds a texture to the dedicated upload unit.
    void BindForUpload(GLenum target, GLuint tex);
    /// Must be called before deleting a GL texture so cached bindings are dropped.
    void ForgetTexture(GLuint tex);
    void ForgetBuffer(GLuint buf);

    /// Reads the RGBA8 contents of a texture level (or of a renderbuffer) through the scratch framebuffer.
    bool ReadTextureLevel(GLuint tex, GLenum target, GLenum faceTarget, int level, uint32_t w, uint32_t h,
                          const RECT &rc, uint8_t *rgbaOut);
    bool ReadRenderbuffer(GLuint rb, uint32_t w, uint32_t h, const RECT &rc, uint8_t *rgbaOut);
    GLuint ScratchFramebuffer() const { return m_scratchFbo; }

    /// Binds a buffer to ARRAY_BUFFER / ELEMENT_ARRAY_BUFFER through the state cache.
    void BindBuffer(GLenum target, GLuint buf)
    {
        GLuint &cached = BufferBindingSlot(target);
        if (cached != buf) { glBindBuffer(target, buf); cached = buf; }
    }

    /// GLObject registry: everything that owns WebGL names registers here, so that all of it can be told when the
    /// context is lost and when a new one is current (see GLObject).
    void RegisterGL(GLObject *o);
    void UnregisterGL(GLObject *o);
    void NotifyContextLost();
    void NotifyContextRestored();

public:
    //-- Data (public for the front end's device, which derives from this class) ----
    GLCaps m_glcaps;
    GLuint m_scratchFbo = 0;
    bool m_targetsDirty = true; ///< render target attachments must be re-established before the next draw

    // What is bound to the buffer targets and to the texture units (0xFFFFFFFF: unknown).
    GLuint m_boundArrayBuffer = 0xFFFFFFFF;
    GLuint m_boundElementBuffer = 0xFFFFFFFF;
    GLuint m_boundTex[MAX_STAGES + 1][3];
    int m_activeUnit = -1;
    uint64_t m_attribSig = 0; ///< signature of the vertex attribute setup; reset whenever a buffer is deleted

    GLObject *m_glObjects = nullptr; ///< every object that owns WebGL names

protected:
    GLState() = default;
    ~GLState() = default;

    /// Where the cache keeps what is bound to `target` (a front end whose element array binding belongs to a vertex
    /// array object redirects it).
    virtual GLuint &BufferBindingSlot(GLenum target) { return target == GL_ARRAY_BUFFER ? m_boundArrayBuffer : m_boundElementBuffer; }
    /// A texture / buffer is about to be deleted: drop what the front end caches about it (framebuffer attachments,
    /// vertex array objects).
    virtual void OnForgetTexture(GLuint) {}
    virtual void OnForgetBuffer(GLuint) {}
    /// A texture is about to be read back: bring it up to date (multisample resolve).
    virtual void PrepareTextureRead(GLuint) {}
};

} // namespace dxwebgl2
