/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Copyright (C) 2026 dxWebGL2 contributors
 *
 * This file is part of dxWebGL2, a Direct3D on WebGL2 translation layer.
 * See the LICENSE file or <https://www.gnu.org/licenses/> for the license terms.
 */
/*
 * The WebGPU API (webgpu.h of Emscripten's emdawnwebgpu port) and the macros that count the calls of the WebGPU
 * backend in the same categories as the WebGL ones (core/glcount.h): draw = draws, state = pipeline and dynamic
 * state, bind = bind groups, vertex/index buffers and render passes, upload = writeBuffer/writeTexture, other =
 * object creation. Included only by src/core/webgpu.
 */
#pragma once

#include <webgpu/webgpu.h>

#include "core/glcount.h"

#ifndef DXWEBGL2_NO_GL_COUNTERS

#define DXW_WGC(cat, fn) (::dxwebgl2::g_gl.calls[::dxwebgl2::cat]++, ::dxwebgl2::g_gl.total++, fn)

#define wgpuRenderPassEncoderDraw(...) DXW_WGC(GLC_DRAW, wgpuRenderPassEncoderDraw)(__VA_ARGS__)
#define wgpuRenderPassEncoderDrawIndexed(...) DXW_WGC(GLC_DRAW, wgpuRenderPassEncoderDrawIndexed)(__VA_ARGS__)

#define wgpuRenderPassEncoderSetPipeline(...) DXW_WGC(GLC_STATE, wgpuRenderPassEncoderSetPipeline)(__VA_ARGS__)
#define wgpuRenderPassEncoderSetViewport(...) DXW_WGC(GLC_STATE, wgpuRenderPassEncoderSetViewport)(__VA_ARGS__)
#define wgpuRenderPassEncoderSetScissorRect(...) DXW_WGC(GLC_STATE, wgpuRenderPassEncoderSetScissorRect)(__VA_ARGS__)
#define wgpuRenderPassEncoderSetStencilReference(...) DXW_WGC(GLC_STATE, wgpuRenderPassEncoderSetStencilReference)(__VA_ARGS__)

#define wgpuRenderPassEncoderSetBindGroup(...) DXW_WGC(GLC_BIND, wgpuRenderPassEncoderSetBindGroup)(__VA_ARGS__)
#define wgpuRenderPassEncoderSetVertexBuffer(...) DXW_WGC(GLC_BIND, wgpuRenderPassEncoderSetVertexBuffer)(__VA_ARGS__)
#define wgpuRenderPassEncoderSetIndexBuffer(...) DXW_WGC(GLC_BIND, wgpuRenderPassEncoderSetIndexBuffer)(__VA_ARGS__)
#define wgpuCommandEncoderBeginRenderPass(...) DXW_WGC(GLC_BIND, wgpuCommandEncoderBeginRenderPass)(__VA_ARGS__)
#define wgpuRenderPassEncoderEnd(...) DXW_WGC(GLC_BIND, wgpuRenderPassEncoderEnd)(__VA_ARGS__)

#define wgpuQueueWriteBuffer(...) DXW_WGC(GLC_UPLOAD, wgpuQueueWriteBuffer)(__VA_ARGS__)
#define wgpuQueueWriteTexture(...) DXW_WGC(GLC_UPLOAD, wgpuQueueWriteTexture)(__VA_ARGS__)
#define wgpuQueueSubmit(...) DXW_WGC(GLC_OTHER, wgpuQueueSubmit)(__VA_ARGS__)

#define wgpuDeviceCreateBuffer(...) DXW_WGC(GLC_OTHER, wgpuDeviceCreateBuffer)(__VA_ARGS__)
#define wgpuDeviceCreateTexture(...) DXW_WGC(GLC_OTHER, wgpuDeviceCreateTexture)(__VA_ARGS__)
#define wgpuTextureCreateView(...) DXW_WGC(GLC_OTHER, wgpuTextureCreateView)(__VA_ARGS__)
#define wgpuDeviceCreateSampler(...) DXW_WGC(GLC_OTHER, wgpuDeviceCreateSampler)(__VA_ARGS__)
#define wgpuDeviceCreateBindGroup(...) DXW_WGC(GLC_OTHER, wgpuDeviceCreateBindGroup)(__VA_ARGS__)
#define wgpuDeviceCreateRenderPipeline(...) DXW_WGC(GLC_OTHER, wgpuDeviceCreateRenderPipeline)(__VA_ARGS__)
#define wgpuDeviceCreateShaderModule(...) DXW_WGC(GLC_OTHER, wgpuDeviceCreateShaderModule)(__VA_ARGS__)
#define wgpuDeviceCreateCommandEncoder(...) DXW_WGC(GLC_OTHER, wgpuDeviceCreateCommandEncoder)(__VA_ARGS__)
#define wgpuCommandEncoderFinish(...) DXW_WGC(GLC_OTHER, wgpuCommandEncoderFinish)(__VA_ARGS__)

#endif // DXWEBGL2_NO_GL_COUNTERS
