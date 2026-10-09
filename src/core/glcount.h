/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Per-frame counters of the WebGL calls the device makes.
 *
 * Every WebGL call crosses from WebAssembly into JavaScript, which is where the
 * time of a Direct3D 8 layer goes, so the number of calls per frame is the
 * metric that is tuned (and, with -dxwebgl2-report, reported). The counting is a
 * plain increment of a global per call; define DXWEBGL2_NO_GL_COUNTERS to remove
 * it.
 *
 * Include after <GLES3/gl3.h>: the wrappers are macros that increment the
 * counter of the call's category and then call the real function.
 */
#pragma once

#include <cstdint>

namespace dxwebgl2 {

enum GLCategory
{
    GLC_DRAW,    ///< glDrawElements/glDrawArrays/glClear
    GLC_STATE,   ///< enables, blend, depth, stencil, viewport, pixel store...
    GLC_BIND,    ///< texture/buffer/framebuffer/program/vertex array/sampler binds, attachments
    GLC_UNIFORM, ///< glUniform*
    GLC_UPLOAD,  ///< buffer and texture data, texture/sampler parameters, storage
    GLC_ATTRIB,  ///< vertex attribute pointers and enables
    GLC_OTHER,   ///< object creation/deletion, queries
    GLC_COUNT
};

/// Counters of the current frame (reset by Present). Plain globals so that the
/// call wrappers cost one increment.
struct GLCounters
{
    uint32_t calls[GLC_COUNT];
    uint32_t total;
    uint32_t sync;        ///< calls that wait for the GPU (glReadPixels, glGet*, glCheckFramebufferStatus)
    uint32_t uploadBytes; ///< bytes handed to glBufferData/SubData and glTex(Sub)Image
};
extern GLCounters g_gl;

} // namespace dxwebgl2

#ifndef DXWEBGL2_NO_GL_COUNTERS

#define DXW_GLC(cat, fn) (::dxwebgl2::g_gl.calls[::dxwebgl2::cat]++, ::dxwebgl2::g_gl.total++, fn)
#define DXW_GLS(fn) (::dxwebgl2::g_gl.sync++, ::dxwebgl2::g_gl.total++, ::dxwebgl2::g_gl.calls[::dxwebgl2::GLC_OTHER]++, fn)

#define glDrawElements(...) DXW_GLC(GLC_DRAW, glDrawElements)(__VA_ARGS__)
#define glDrawArrays(...) DXW_GLC(GLC_DRAW, glDrawArrays)(__VA_ARGS__)
#define glClear(...) DXW_GLC(GLC_DRAW, glClear)(__VA_ARGS__)

#define glEnable(...) DXW_GLC(GLC_STATE, glEnable)(__VA_ARGS__)
#define glDisable(...) DXW_GLC(GLC_STATE, glDisable)(__VA_ARGS__)
#define glBlendFunc(...) DXW_GLC(GLC_STATE, glBlendFunc)(__VA_ARGS__)
#define glBlendFuncSeparate(...) DXW_GLC(GLC_STATE, glBlendFuncSeparate)(__VA_ARGS__)
#define glBlendEquation(...) DXW_GLC(GLC_STATE, glBlendEquation)(__VA_ARGS__)
#define glDepthFunc(...) DXW_GLC(GLC_STATE, glDepthFunc)(__VA_ARGS__)
#define glDepthMask(...) DXW_GLC(GLC_STATE, glDepthMask)(__VA_ARGS__)
#define glDepthRangef(...) DXW_GLC(GLC_STATE, glDepthRangef)(__VA_ARGS__)
#define glCullFace(...) DXW_GLC(GLC_STATE, glCullFace)(__VA_ARGS__)
#define glFrontFace(...) DXW_GLC(GLC_STATE, glFrontFace)(__VA_ARGS__)
#define glStencilFunc(...) DXW_GLC(GLC_STATE, glStencilFunc)(__VA_ARGS__)
#define glStencilOp(...) DXW_GLC(GLC_STATE, glStencilOp)(__VA_ARGS__)
#define glStencilMask(...) DXW_GLC(GLC_STATE, glStencilMask)(__VA_ARGS__)
#define glColorMask(...) DXW_GLC(GLC_STATE, glColorMask)(__VA_ARGS__)
#define glPolygonOffset(...) DXW_GLC(GLC_STATE, glPolygonOffset)(__VA_ARGS__)
#define glViewport(...) DXW_GLC(GLC_STATE, glViewport)(__VA_ARGS__)
#define glScissor(...) DXW_GLC(GLC_STATE, glScissor)(__VA_ARGS__)
#define glClearColor(...) DXW_GLC(GLC_STATE, glClearColor)(__VA_ARGS__)
#define glClearDepthf(...) DXW_GLC(GLC_STATE, glClearDepthf)(__VA_ARGS__)
#define glClearStencil(...) DXW_GLC(GLC_STATE, glClearStencil)(__VA_ARGS__)
#define glPixelStorei(...) DXW_GLC(GLC_STATE, glPixelStorei)(__VA_ARGS__)

#define glBindTexture(...) DXW_GLC(GLC_BIND, glBindTexture)(__VA_ARGS__)
#define glActiveTexture(...) DXW_GLC(GLC_BIND, glActiveTexture)(__VA_ARGS__)
#define glBindSampler(...) DXW_GLC(GLC_BIND, glBindSampler)(__VA_ARGS__)
#define glBindBuffer(...) DXW_GLC(GLC_BIND, glBindBuffer)(__VA_ARGS__)
#define glBindFramebuffer(...) DXW_GLC(GLC_BIND, glBindFramebuffer)(__VA_ARGS__)
#define glBindRenderbuffer(...) DXW_GLC(GLC_BIND, glBindRenderbuffer)(__VA_ARGS__)
#define glBindVertexArray(...) DXW_GLC(GLC_BIND, glBindVertexArray)(__VA_ARGS__)
#define glUseProgram(...) DXW_GLC(GLC_BIND, glUseProgram)(__VA_ARGS__)
#define glFramebufferTexture2D(...) DXW_GLC(GLC_BIND, glFramebufferTexture2D)(__VA_ARGS__)
#define glFramebufferRenderbuffer(...) DXW_GLC(GLC_BIND, glFramebufferRenderbuffer)(__VA_ARGS__)
#define glReadBuffer(...) DXW_GLC(GLC_BIND, glReadBuffer)(__VA_ARGS__)
#define glDrawBuffers(...) DXW_GLC(GLC_BIND, glDrawBuffers)(__VA_ARGS__)
#define glDrawElementsInstancedBaseVertexBaseInstanceWEBGL(...) DXW_GLC(GLC_DRAW, glDrawElementsInstancedBaseVertexBaseInstanceWEBGL)(__VA_ARGS__)
#define glBlitFramebuffer(...) DXW_GLC(GLC_DRAW, glBlitFramebuffer)(__VA_ARGS__)
#define glInvalidateFramebuffer(...) DXW_GLC(GLC_BIND, glInvalidateFramebuffer)(__VA_ARGS__)

#define glUniform1i(...) DXW_GLC(GLC_UNIFORM, glUniform1i)(__VA_ARGS__)
#define glUniform1f(...) DXW_GLC(GLC_UNIFORM, glUniform1f)(__VA_ARGS__)
#define glUniform1fv(...) DXW_GLC(GLC_UNIFORM, glUniform1fv)(__VA_ARGS__)
#define glUniform2f(...) DXW_GLC(GLC_UNIFORM, glUniform2f)(__VA_ARGS__)
#define glUniform2fv(...) DXW_GLC(GLC_UNIFORM, glUniform2fv)(__VA_ARGS__)
#define glUniform3f(...) DXW_GLC(GLC_UNIFORM, glUniform3f)(__VA_ARGS__)
#define glUniform3fv(...) DXW_GLC(GLC_UNIFORM, glUniform3fv)(__VA_ARGS__)
#define glUniform4f(...) DXW_GLC(GLC_UNIFORM, glUniform4f)(__VA_ARGS__)
#define glUniform4fv(...) DXW_GLC(GLC_UNIFORM, glUniform4fv)(__VA_ARGS__)
#define glUniformMatrix3fv(...) DXW_GLC(GLC_UNIFORM, glUniformMatrix3fv)(__VA_ARGS__)
#define glUniformMatrix4fv(...) DXW_GLC(GLC_UNIFORM, glUniformMatrix4fv)(__VA_ARGS__)
#define glBindBufferBase(...) DXW_GLC(GLC_BIND, glBindBufferBase)(__VA_ARGS__)
#define glUniformBlockBinding(...) DXW_GLC(GLC_UNIFORM, glUniformBlockBinding)(__VA_ARGS__)

#define glBufferData(...) DXW_GLC(GLC_UPLOAD, glBufferData)(__VA_ARGS__)
#define glBufferSubData(...) DXW_GLC(GLC_UPLOAD, glBufferSubData)(__VA_ARGS__)
#define glTexImage2D(...) DXW_GLC(GLC_UPLOAD, glTexImage2D)(__VA_ARGS__)
#define glTexImage3D(...) DXW_GLC(GLC_UPLOAD, glTexImage3D)(__VA_ARGS__)
#define glTexSubImage2D(...) DXW_GLC(GLC_UPLOAD, glTexSubImage2D)(__VA_ARGS__)
#define glTexSubImage3D(...) DXW_GLC(GLC_UPLOAD, glTexSubImage3D)(__VA_ARGS__)
#define glCompressedTexSubImage2D(...) DXW_GLC(GLC_UPLOAD, glCompressedTexSubImage2D)(__VA_ARGS__)
#define glCompressedTexSubImage3D(...) DXW_GLC(GLC_UPLOAD, glCompressedTexSubImage3D)(__VA_ARGS__)
#define glTexStorage2D(...) DXW_GLC(GLC_UPLOAD, glTexStorage2D)(__VA_ARGS__)
#define glTexStorage3D(...) DXW_GLC(GLC_UPLOAD, glTexStorage3D)(__VA_ARGS__)
#define glTexParameteri(...) DXW_GLC(GLC_UPLOAD, glTexParameteri)(__VA_ARGS__)
#define glSamplerParameteri(...) DXW_GLC(GLC_UPLOAD, glSamplerParameteri)(__VA_ARGS__)
#define glSamplerParameterf(...) DXW_GLC(GLC_UPLOAD, glSamplerParameterf)(__VA_ARGS__)
#define glSamplerParameterfv(...) DXW_GLC(GLC_UPLOAD, glSamplerParameterfv)(__VA_ARGS__)
#define glRenderbufferStorage(...) DXW_GLC(GLC_UPLOAD, glRenderbufferStorage)(__VA_ARGS__)
#define glRenderbufferStorageMultisample(...) DXW_GLC(GLC_UPLOAD, glRenderbufferStorageMultisample)(__VA_ARGS__)
#define glMapBufferRange(...) DXW_GLC(GLC_UPLOAD, glMapBufferRange)(__VA_ARGS__)
#define glUnmapBuffer(...) DXW_GLC(GLC_UPLOAD, glUnmapBuffer)(__VA_ARGS__)
#define glCopyBufferSubData(...) DXW_GLC(GLC_UPLOAD, glCopyBufferSubData)(__VA_ARGS__)

#define glVertexAttribPointer(...) DXW_GLC(GLC_ATTRIB, glVertexAttribPointer)(__VA_ARGS__)
#define glVertexAttribIPointer(...) DXW_GLC(GLC_ATTRIB, glVertexAttribIPointer)(__VA_ARGS__)
#define glEnableVertexAttribArray(...) DXW_GLC(GLC_ATTRIB, glEnableVertexAttribArray)(__VA_ARGS__)
#define glDisableVertexAttribArray(...) DXW_GLC(GLC_ATTRIB, glDisableVertexAttribArray)(__VA_ARGS__)
#define glVertexAttrib4f(...) DXW_GLC(GLC_ATTRIB, glVertexAttrib4f)(__VA_ARGS__)

#define glReadPixels(...) DXW_GLS(glReadPixels)(__VA_ARGS__)
#define glCheckFramebufferStatus(...) DXW_GLS(glCheckFramebufferStatus)(__VA_ARGS__)
#define glGetError(...) DXW_GLS(glGetError)(__VA_ARGS__)

#endif // DXWEBGL2_NO_GL_COUNTERS
