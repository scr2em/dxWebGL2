/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * Per-frame counters of the calls the backend makes into the browser's GPU API (WebGL today).
 *
 * Every WebGL call crosses from WebAssembly into JavaScript, which is where the
 * time of a Direct3D 8 layer goes, so the number of calls per frame is the
 * metric that is tuned (and, with -dxwebgl2-report, reported). The counting is a
 * plain increment of a global per call; the wrappers that do it for the GL calls
 * are in core/gl/gl.h (define DXWEBGL2_NO_GL_COUNTERS to remove them).
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
